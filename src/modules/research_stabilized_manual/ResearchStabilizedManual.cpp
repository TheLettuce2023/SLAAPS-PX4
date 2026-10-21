#include <px4_platform_common/module.h>
#include <px4_platform_common/px4_work_queue/ScheduledWorkItem.hpp>
#include <lib/research_stabilized_manual/ObserverProfile.hpp>
#include <drivers/drv_hrt.h>
#include <uORB/Publication.hpp>
#include <uORB/topics/stabilized_observer_status.h>
#include <cstring>
#include "SelectedStateInput.hpp"

using namespace research_stabilized_manual;

namespace
{
// Isolate diagnostic allocation work from flight-controller queues. This stack
// reservation is a build provision, not a measured hardware high-water mark.
constexpr px4::wq_config_t kObserverQueue{"wq:stabilized_observer", 6000, -5};
}

// Observation only: this translation unit has exactly one diagnostic publisher.
// There is no bench/servo/motor/setpoint publisher and no output enable command.
class ResearchStabilizedManual final : public ModuleBase, public px4::ScheduledWorkItem
{
public:
	static Descriptor desc;
	explicit ResearchStabilizedManual(const ObserverProfile &profile) :
		ScheduledWorkItem(MODULE_NAME, kObserverQueue), _profile(profile) {}
	static int task_spawn(int argc, char *argv[]);
	static int custom_command(int argc, char *argv[]);
	static int print_usage(const char *reason = nullptr);
	int print_status() override
	{
		PX4_INFO("observer state=%d fault=%d; actuator output unavailable", _state.load(), _fault.load());
		return 0;
	}
private:
	void Run() override;
	void revoke(Status reason)
	{
		_owner.disable();
		_state.store(static_cast<int>(State::Fault));
		_fault.store(static_cast<int>(reason));
		_revoked = true;
	}
	ObserverProfile _profile;
	StabilizedManualControl _owner{};
	SelectedStateInput _inputs{};
	uint32_t _enabled_epoch{};
	uORB::Publication<stabilized_observer_status_s> _diagnostics{ORB_ID(stabilized_observer_status)};
	px4::atomic<int> _request{0}; // Observe=1; disable=2 takes precedence.
	px4::atomic<int> _state{static_cast<int>(State::Ready)};
	px4::atomic<int> _fault{static_cast<int>(Status::Ok)};
	TimeUs _session{};
	TimeUs _last_cycle{};
	TimeUs _estimator_time{};
	uint32_t _gyro{}, _accel{}, _cycles{};
	bool _revoked{};
};

ModuleBase::Descriptor ResearchStabilizedManual::desc{task_spawn, custom_command, print_usage};

void ResearchStabilizedManual::Run()
{
	if (should_exit()) {
		revoke(Status::InvalidState);
		stabilized_observer_status_s terminal{};
		terminal.timestamp = hrt_absolute_time();
		terminal.session = _session;
		terminal.state = static_cast<uint8_t>(State::Disabled);
		terminal.fault = static_cast<uint8_t>(Status::InvalidState);
		(void)_diagnostics.publish(terminal);
		ScheduleClear();
		exit_and_cleanup(desc);
		return;
	}

	const int request = _request.fetch_and(0);

	if (request == 2) { revoke(Status::InvalidState); }

	const auto inputs = _inputs.capture(_profile);
	const TimeUs now = inputs.now;
	const auto &manual = inputs.manual;
	const auto &sample = inputs.state;
	const auto &revisions = inputs.revisions;
	const auto source_epoch = sample.source_epoch;

	if (!_revoked && _session && (!sample.valid || source_epoch != _enabled_epoch || inputs.gyro != _gyro
				      || inputs.accel != _accel || inputs.estimator_time < _estimator_time)) {
		revoke(Status::StateChanged);
	}

	_estimator_time = inputs.estimator_time;
	Report report{};
	bool candidate_valid = false;

	if (!_revoked && request == 1) {
		const Status result = _owner.enable(now, manual, sample);

		if (result != Status::Ok) { revoke(result); }

		else {
			_session = _last_cycle = now;
			_enabled_epoch = source_epoch;
			_gyro = inputs.gyro;
			_accel = inputs.accel;
			_state.store(static_cast<int>(_owner.state()));
		}

	} else if (!_revoked && _owner.eligible() && now != _last_cycle) {
		const Status result = _owner.cycle(now, manual, sample, report);

		if (result == Status::Ok) {
			_last_cycle = now;

			if (_cycles != UINT32_MAX) { ++_cycles; }

			candidate_valid = true;
			_state.store(static_cast<int>(_owner.state()));

		} else if (result != Status::NotDue) { revoke(result); }
	}

	stabilized_observer_status_s diagnostic{};
	diagnostic.timestamp = now;
	diagnostic.timestamp_sample = sample.rate_sample_us;
	diagnostic.session = _session;
	diagnostic.source_epoch = source_epoch;

	for (unsigned i = 0; i < PublisherBinding::kTopics; ++i) { diagnostic.publisher_revision[i] = revisions[i]; }

	diagnostic.gyro_device_id = inputs.gyro;
	diagnostic.accel_device_id = inputs.accel;
	diagnostic.attitude_reset_counter = sample.attitude_reset_counter;
	diagnostic.state = _state.load();
	diagnostic.fault = _fault.load();
	diagnostic.cycles = _cycles;
	diagnostic.inputs_valid = sample.valid && manual.valid;
	diagnostic.candidate_valid = candidate_valid;

	if (candidate_valid) {
		for (unsigned i = 0; i < 4; ++i) {
			diagnostic.hold_reference[i] = report.hold_reference(i);
			diagnostic.canard_candidate[i] = report.allocation.command[i];
		}

		for (unsigned i = 0; i < 3; ++i) {
			diagnostic.rate_setpoint[i] = report.rate_setpoint_rad_s(i);
			diagnostic.requested_moment[i] = report.requested_moment(i);
			diagnostic.allocation_residual[i] = report.allocation.residual(i);
			diagnostic.integral[i] = report.integral(i);
		}
	}

	if (!_diagnostics.publish(diagnostic) && !_revoked) { revoke(Status::ArithmeticFailure); }
}

int ResearchStabilizedManual::task_spawn(int argc, char *argv[])
{
	if (argc != 2) { return print_usage("explicit observer profile required"); }

	ObserverProfile profile{};
	FILE *stream = fopen(argv[1], "r");
	const bool valid = readObserverProfile(stream, profile);

	if (stream) { fclose(stream); }

	if (!valid) { return print_usage("invalid profile"); }

	auto *instance = new ResearchStabilizedManual(profile);

	if (!instance) { return PX4_ERROR; }

	if (instance->_owner.configure(profile.control) != Status::Ok || !instance->_diagnostics.advertise()) {
		delete instance;
		return PX4_ERROR;
	}

	desc.object.store(instance);
	desc.task_id = task_id_is_work_queue;
	instance->ScheduleOnInterval(profile.control.minimum_cycle_us);
	return PX4_OK;
}

int ResearchStabilizedManual::custom_command(int argc, char *argv[])
{
	auto *instance = get_instance<ResearchStabilizedManual>(desc);

	if (!instance || argc != 1) { return PX4_ERROR; }

	if (strcmp(argv[0], "disable") == 0) { instance->_request.store(2); return PX4_OK; }

	if (strcmp(argv[0], "observe") != 0 || instance->_state.load() != static_cast<int>(State::Ready)) { return PX4_ERROR; }

	int expected = 0;
	return instance->_request.compare_exchange(&expected, 1) ? PX4_OK : PX4_ERROR;
}

int ResearchStabilizedManual::print_usage(const char *reason)
{
	if (reason) { PX4_WARN("%s", reason); }

	PRINT_MODULE_USAGE_NAME("research_stabilized_manual", "research");
	PRINT_MODULE_USAGE_COMMAND_DESCR("start <profile>", "Explicit diagnostic profile; initially Ready");
	PRINT_MODULE_USAGE_COMMAND_DESCR("observe", "Calculate candidates only; no actuator output");
	PRINT_MODULE_USAGE_COMMAND_DESCR("disable", "Latch observation off; stop/start required to reset");
	PRINT_MODULE_USAGE_DEFAULT_COMMANDS();
	return reason ? PX4_ERROR : PX4_OK;
}

extern "C" __EXPORT int research_stabilized_manual_main(int argc, char *argv[])
{
	return ModuleBase::main(ResearchStabilizedManual::desc, argc, argv);
}
