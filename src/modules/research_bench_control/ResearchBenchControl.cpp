#include <px4_platform_common/module.h>
#include <px4_platform_common/px4_work_queue/ScheduledWorkItem.hpp>
#include <parameters/param.h>
#include <uORB/Publication.hpp>
#include <uORB/Subscription.hpp>
#include <uORB/topics/actuator_armed.h>
#include <uORB/topics/bench_servo_command.h>
#include <lib/research_actuator_command/BenchOutputLease.hpp>
#include <lib/research_actuator_command/Px4ManualControlInput.hpp>
#include <lib/research_actuator_command/ManualSnapshot.hpp>
#include <drivers/drv_hrt.h>
#include <cmath>
#include <cstring>

using namespace research_actuator_command;

// Only Run mutates the control owner. CLI submits one bounded atomic request;
// ModuleBase serializes CLI/object lifetime. No separate H7 runtime is created.
class ResearchBenchControl final : public ModuleBase, public px4::ScheduledWorkItem
{
public:
	static Descriptor desc;
	explicit ResearchBenchControl(const BenchOutputProfile &profile) :
		ScheduledWorkItem(MODULE_NAME, px4::wq_configurations::test1), _profile(profile) {}
	static int task_spawn(int argc, char *argv[]);
	static int custom_command(int argc, char *argv[]);
	static int print_usage(const char *reason = nullptr);
	int print_status() override
	{
		PX4_INFO("state=%d (0 ready, 1 active, 2 revoked); no physical acceptance", _status.load());
		return 0;
	}
private:
	void Run() override;
	bool routingValid() const;
	bool publish(const BenchReport &report, bool enabled, RuntimeTimeUs now);
	void revoke(RuntimeTimeUs now);
	static bool parameter(const char *name, int32_t expected)
	{
		int32_t value{};
		const param_t handle = param_find(name);
		return handle != PARAM_INVALID && param_get(handle, &value) == 0 && value == expected;
	}
	BenchOutputProfile _profile;
	BenchControlOwner _owner{};
	uORB::Subscription _manual{ORB_ID(manual_control_setpoint)};
	uORB::Subscription _armed{ORB_ID(actuator_armed)};
	uORB::Publication<bench_servo_command_s> _output{ORB_ID(bench_servo_command)};
	px4::atomic<int> _request{0}; // 1 enable, 2 disable; no queue/allocation.
	px4::atomic<int> _status{0};
	RuntimeTimeUs _session{};
	bool _revoked{};
};

ModuleBase::Descriptor ResearchBenchControl::desc{task_spawn, custom_command, print_usage};

bool ResearchBenchControl::routingValid() const
{
	// Read only. No automatic parameter writes or output assignments. This
	// module is scoped to Pixhawk MAIN1-4, with all remaining PWM pins disabled.
	char name[24] {};

	for (unsigned i = 1; i <= 8; ++i) {
		snprintf(name, sizeof(name), "PWM_MAIN_FUNC%u", i);

		if (!parameter(name, i <= 4 ? 9999 + i : 0)) { return false; }

		snprintf(name, sizeof(name), "PWM_AUX_FUNC%u", i);

		if (!parameter(name, 0)) { return false; }

		if (i <= 4) {
			const auto &servo = _profile.control.servos[i - 1];

			if (std::floor(servo.neutral_pwm_us) < servo.neutral_pwm_us) { return false; }

			const char *fields[] = {"MIN", "MAX", "CENT", "DIS", "FAIL"};
			const int32_t expected[] = {500, 2500, static_cast<int32_t>(servo.neutral_pwm_us), 0, 0};

			for (unsigned f = 0; f < 5; ++f) {
				snprintf(name, sizeof(name), "PWM_MAIN_%s%u", fields[f], i);

				if (!parameter(name, expected[f])) { return false; }
			}
		}
	}

	return parameter("PWM_MAIN_REV", 0);
}

bool ResearchBenchControl::publish(const BenchReport &report, bool enabled, RuntimeTimeUs now)
{
	bench_servo_command_s message{};
	message.timestamp = now;
	message.session = _session;
	message.lease_us = _profile.lease_us;
	message.enabled = enabled;
	BenchVector control{};

	if (enabled && !benchPwmToControl(report.pwm_us, _profile.control, control)) { return false; }

	for (unsigned i = 0; i < kBenchCanardCount; ++i) {
		message.control[i] = control[i];
	}

	return _output.publish(message);
}

void ResearchBenchControl::revoke(RuntimeTimeUs now)
{
	_owner.disable();
	_revoked = true;
	_status.store(2);

	// Stop renewing the lease even if publication fails. A consumer expires
	// independently; neither this publication nor a timeout proves motion.
	if (_session != 0) { (void)publish({}, false, now); }
}

void ResearchBenchControl::Run()
{
	if (should_exit()) {
		revoke(hrt_absolute_time());
		ScheduleClear();
		exit_and_cleanup(desc);
		return;
	}

	const int request = _request.fetch_and(0);

	if (request == 2) { revoke(hrt_absolute_time()); }

	if (_revoked) { return; }

	actuator_armed_s armed{};
	const bool permitted = _armed.copy(&armed) && armed.timestamp != 0 && !armed.armed && armed.prearmed
			       && !armed.lockdown && !armed.kill && !armed.termination && !armed.in_esc_calibration_mode;

	if (!routingValid() || !permitted) {
		if (_session != 0 || request == 1) { revoke(hrt_absolute_time()); }

		return;
	}

	manual_control_setpoint_s message{};
	const auto now = copyManualSnapshot(_manual, message, hrt_absolute_time);
	auto sample = manualControlSampleFromMessage(message);

	if (request == 1 && _owner.state() == BenchState::Ready) {
		// Center check sees unshaped axes; shaping cannot hide entry offset.
		for (unsigned i = 0; i < kBenchAxisCount; ++i) {
			if (!std::isfinite(sample.axes[i]) || std::fabs(sample.axes[i]) > _profile.control.neutral_entry_tolerance) {
				revoke(now); return;
			}

			sample.axes[i] = benchDeadzone(sample.axes[i], _profile.deadzone);
		}

		if (_owner.enable(now, sample) != BenchStatus::Ok) { revoke(now); return; }

		_session = now;
		_status.store(1);
		BenchReport neutral{};

		if (!_owner.neutralCandidate(neutral) || !publish(neutral, true, now)) { revoke(now); }

		return;
	}

	if (_owner.state() != BenchState::Active) { return; }

	for (unsigned i = 0; i < kBenchAxisCount; ++i) { sample.axes[i] = benchDeadzone(sample.axes[i], _profile.deadzone); }

	BenchReport report{};
	const auto result = _owner.cycle(now, sample, report);

	if (result == BenchStatus::NotDue) { return; }

	if (result != BenchStatus::Ok || !publish(report, true, now)) { revoke(now); }
}

int ResearchBenchControl::task_spawn(int argc, char *argv[])
{
	if (argc != 2) { return print_usage("start requires an explicit profile path"); }

	FILE *stream = fopen(argv[1], "r");
	BenchOutputProfile profile{};
	const bool valid = readBenchOutputProfile(stream, profile);

	if (stream) { fclose(stream); }

	if (!valid) { PX4_ERR("invalid profile; outputs remain disabled"); return PX4_ERROR; }

	auto *instance = new ResearchBenchControl(profile);

	if (!instance) { return PX4_ERROR; }

	if (instance->_owner.configure(profile.control) != BenchStatus::Ok) { delete instance; return PX4_ERROR; }

	// Allocate the uORB publication at start, before any enabled control cycle.
	// Advertising without data does not send a command.
	if (!instance->_output.advertise()) { delete instance; return PX4_ERROR; }

	desc.object.store(instance);
	desc.task_id = task_id_is_work_queue;
	instance->ScheduleOnInterval(kBenchIntervalUs);
	return PX4_OK;
}

int ResearchBenchControl::custom_command(int argc, char *argv[])
{
	auto *instance = get_instance<ResearchBenchControl>(desc);

	if (!instance || argc != 1) { return PX4_ERROR; }

	const int request = strcmp(argv[0], "enable") == 0 ? 1 : strcmp(argv[0], "disable") == 0 ? 2 : 0;

	if (!request) { return print_usage("unknown command"); }

	// Disable takes precedence over a pending enable. Enabling never clears a
	// fault: stop/start with explicit profile is the only reset lifecycle.
	if (request == 2) { instance->_request.store(2); return PX4_OK; }

	int expected = 0;
	return instance->_request.compare_exchange(&expected, request) ? PX4_OK : PX4_ERROR;
}

int ResearchBenchControl::print_usage(const char *reason)
{
	if (reason) { PX4_WARN("%s", reason); }

	PRINT_MODULE_USAGE_NAME("research_bench_control", "research");
	PRINT_MODULE_USAGE_COMMAND_DESCR("start <profile>", "Start disabled; no default profile or output assignments");
	PRINT_MODULE_USAGE_COMMAND_DESCR("enable", "Request centered bench session; requires reviewed hardware setup");
	PRINT_MODULE_USAGE_COMMAND_DESCR("disable", "Revoke; stop/start required before another enable");
	PRINT_MODULE_USAGE_DEFAULT_COMMANDS();
	return reason ? PX4_ERROR : PX4_OK;
}

extern "C" __EXPORT int research_bench_control_main(int argc, char *argv[])
{
	return ModuleBase::main(ResearchBenchControl::desc, argc, argv);
}
