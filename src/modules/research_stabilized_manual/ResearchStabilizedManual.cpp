#include <px4_platform_common/module.h>
#include <px4_platform_common/px4_work_queue/ScheduledWorkItem.hpp>
#include <lib/research_stabilized_manual/ObserverProfile.hpp>
#include <lib/research_actuator_command/Px4ManualControlInput.hpp>
#include <parameters/param.h>
#include <drivers/drv_hrt.h>
#include <uORB/Publication.hpp>
#include <uORB/Subscription.hpp>
#include <uORB/topics/stabilized_observer_status.h>
#include <uORB/topics/actuator_armed.h>
#include <uORB/topics/vehicle_attitude.h>
#include <uORB/topics/vehicle_angular_velocity.h>
#include <uORB/topics/sensor_selection.h>
#include <uORB/topics/estimator_status.h>
#include <uORB/topics/estimator_status_flags.h>
#include <cstring>

using namespace research_stabilized_manual;

namespace
{
// Isolate diagnostic allocation work from flight-controller queues. This stack
// reservation is a build provision, not a measured hardware high-water mark.
constexpr px4::wq_config_t kObserverQueue{"wq:stabilized_observer", 6000, -5};
bool fresh(TimeUs now, TimeUs timestamp, TimeUs maximum)
{
	return timestamp != 0 && timestamp <= now && now - timestamp <= maximum;
}
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
	static bool singleEstimator();
	void revoke(Status reason)
	{
		_owner.disable();
		_state.store(static_cast<int>(State::Fault));
		_fault.store(static_cast<int>(reason));
		_revoked = true;
	}
	ObserverProfile _profile;
	StabilizedManualControl _owner{};
	uORB::Subscription _manual{ORB_ID(manual_control_setpoint)};
	uORB::Subscription _attitude{ORB_ID(vehicle_attitude)};
	uORB::Subscription _rates{ORB_ID(vehicle_angular_velocity)};
	uORB::Subscription _selection{ORB_ID(sensor_selection)};
	uORB::Subscription _estimator{ORB_ID(estimator_status)};
	uORB::Subscription _flags{ORB_ID(estimator_status_flags)};
	uORB::Subscription _armed{ORB_ID(actuator_armed)};
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

bool ResearchStabilizedManual::singleEstimator()
{
	// Current adapter binds estimator instance zero. It does not qualify
	// multi-EKF selection, unobserved publisher replacement or flight health.
	const char *names[] = {"EKF2_MULTI_IMU", "EKF2_MULTI_MAG", "SENS_IMU_MODE", "SENS_MAG_MODE"};
	const int32_t expected[] = {0, 0, 1, 1};

	for (unsigned i = 0; i < 4; ++i) {
		int32_t value{};
		const param_t handle = param_find(names[i]);

		if (handle == PARAM_INVALID || param_get(handle, &value) != 0 || value != expected[i]) { return false; }
	}

	return true;
}

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

	manual_control_setpoint_s manual_message{};
	vehicle_attitude_s attitude{};
	vehicle_angular_velocity_s rates{};
	sensor_selection_s selection{};
	estimator_status_s estimator{};
	estimator_status_flags_s flags{};
	actuator_armed_s armed{};
	// H10 ordering: copy inputs before reading the evaluation clock.
	const bool have = _manual.copy(&manual_message) && _attitude.copy(&attitude) && _rates.copy(&rates)
			  && _selection.copy(&selection) && _estimator.copy(&estimator) && _flags.copy(&flags) && _armed.copy(&armed);
	const bool supported = singleEstimator();
	const TimeUs now = hrt_absolute_time();
	const auto manual = research_actuator_command::manualControlSampleFromMessage(manual_message);
	StateSample sample{};
	sample.attitude_timestamp_us = attitude.timestamp;
	sample.attitude_sample_us = attitude.timestamp_sample;
	sample.rate_timestamp_us = rates.timestamp;
	sample.rate_sample_us = rates.timestamp_sample;
	sample.attitude = Quaternion(attitude.q);
	sample.rate_rad_s = Vector3f(rates.xyz);
	sample.angular_accel_rad_s2 = Vector3f(rates.xyz_derivative);
	sample.attitude_reset_counter = attitude.quat_reset_counter;
	sample.source_epoch = 1; // Diagnostic session only; not general publisher identity.
	sample.valid = have && supported && flags.cs_tilt_align && flags.cs_yaw_align
		       && !flags.fs_bad_hdg && !flags.fs_bad_acc_vertical
		       && selection.gyro_device_id != 0 && selection.accel_device_id != 0
		       && selection.gyro_device_id == estimator.gyro_device_id && selection.accel_device_id == estimator.accel_device_id
		       && fresh(now, estimator.timestamp, _profile.maximum_status_age_us)
		       && fresh(now, flags.timestamp, _profile.maximum_status_age_us)
		       && fresh(now, armed.timestamp, _profile.maximum_permission_age_us)
		       && !armed.armed && !armed.kill && !armed.lockdown && !armed.termination && !armed.in_esc_calibration_mode;

	if (!_revoked && _session && (!sample.valid || selection.gyro_device_id != _gyro
				      || selection.accel_device_id != _accel || estimator.timestamp < _estimator_time)) {
		revoke(Status::StateChanged);
	}

	_estimator_time = estimator.timestamp;
	Report report{};
	bool candidate_valid = false;

	if (!_revoked && request == 1) {
		const Status result = _owner.enable(now, manual, sample);

		if (result != Status::Ok) { revoke(result); }

		else {
			_session = _last_cycle = now;
			_gyro = selection.gyro_device_id;
			_accel = selection.accel_device_id;
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
	diagnostic.gyro_device_id = selection.gyro_device_id;
	diagnostic.accel_device_id = selection.accel_device_id;
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
