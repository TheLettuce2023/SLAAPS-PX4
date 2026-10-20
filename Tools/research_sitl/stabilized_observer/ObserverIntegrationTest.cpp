#if !defined(__PX4_POSIX)
#error Observation test publishers are restricted to the private native fixture.
#endif
#include <px4_platform_common/module.h>
#include <drivers/drv_hrt.h>
#include <parameters/param.h>
#include <uORB/Publication.hpp>
#include <uORB/Subscription.hpp>
#include <uORB/topics/manual_control_setpoint.h>
#include <uORB/topics/vehicle_attitude.h>
#include <uORB/topics/vehicle_angular_velocity.h>
#include <uORB/topics/sensor_selection.h>
#include <uORB/topics/estimator_status.h>
#include <uORB/topics/estimator_status_flags.h>
#include <uORB/topics/actuator_armed.h>
#include <uORB/topics/bench_servo_command.h>
#include <uORB/topics/actuator_servos.h>
#include <uORB/topics/actuator_motors.h>
#include <uORB/topics/stabilized_observer_status.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>

extern "C" int research_stabilized_manual_main(int argc, char *argv[]);
namespace
{
unsigned checks{}, failures{};
void check(bool value, const char *name)
{
	++checks;

	if (!value) { ++failures; PX4_ERR("FAIL %s", name); }
}
int command(const char *operation, const char *profile = nullptr)
{
	char *args[] = {const_cast<char *>("research_stabilized_manual"), const_cast<char *>(operation), const_cast<char *>(profile)};
	return research_stabilized_manual_main(profile ? 3 : 2, args);
}
struct Fixture {
	uORB::Publication<manual_control_setpoint_s> manual{ORB_ID(manual_control_setpoint)};
	uORB::Publication<vehicle_attitude_s> attitude{ORB_ID(vehicle_attitude)};
	uORB::Publication<vehicle_angular_velocity_s> rate{ORB_ID(vehicle_angular_velocity)};
	uORB::Publication<sensor_selection_s> selection{ORB_ID(sensor_selection)};
	uORB::Publication<estimator_status_s> estimator{ORB_ID(estimator_status)};
	uORB::Publication<estimator_status_flags_s> flags{ORB_ID(estimator_status_flags)};
	uORB::Publication<actuator_armed_s> armed{ORB_ID(actuator_armed)};
	uORB::Subscription status{ORB_ID(stabilized_observer_status)};
	uORB::Subscription bench{ORB_ID(bench_servo_command)};
	uORB::Subscription servos{ORB_ID(actuator_servos)};
	uORB::Subscription motors{ORB_ID(actuator_motors)};
	stabilized_observer_status_s last{};
	stabilized_observer_status_s candidate{};
	bool saw_candidate{};
	uint8_t reset{};
	uint32_t gyro{101};
	bool killed{}, bad_health{}, omit_manual{}, future{};
	void steps(float roll, unsigned count = 20)
	{
		saw_candidate = false;
		const auto begin = hrt_absolute_time();

		for (unsigned i = 0; i < count; ++i) {
			const auto now = hrt_absolute_time();
			manual_control_setpoint_s m{};
			m.timestamp = m.timestamp_sample = future ? now + 1000000 : now;
			m.valid = true;
			m.data_source = manual_control_setpoint_s::SOURCE_MAVLINK_0 + 2;
			m.source_system_id = 255;
			m.source_component_id = 190;
			m.roll = roll;

			if (!omit_manual) { check(manual.publish(m), "manual publish"); }

			vehicle_attitude_s q{};
			q.timestamp = q.timestamp_sample = now;
			q.q[0] = 1.0F;
			q.quat_reset_counter = reset;
			check(attitude.publish(q), "attitude publish");
			vehicle_angular_velocity_s w{};
			w.timestamp = w.timestamp_sample = now;
			check(rate.publish(w), "rate publish");
			sensor_selection_s s{};
			s.timestamp = now;
			s.gyro_device_id = gyro;
			s.accel_device_id = 102;
			check(selection.publish(s), "selection publish");
			estimator_status_s e{};
			e.timestamp = now;
			e.gyro_device_id = gyro;
			e.accel_device_id = 102;
			check(estimator.publish(e), "estimator publish");
			estimator_status_flags_s f{};
			f.timestamp = f.timestamp_sample = now;
			f.cs_tilt_align = f.cs_yaw_align = true;
			f.fs_bad_hdg = bad_health;
			check(flags.publish(f), "flags publish");
			actuator_armed_s a{};
			a.timestamp = now;
			a.kill = killed;
			check(armed.publish(a), "permission publish");
			px4_usleep(10000);

			if (status.copy(&last)) {
				printf("OBS time=%llu session=%llu state=%u fault=%u valid=%u candidate=%u cycles=%u\n",
				       (unsigned long long)last.timestamp, (unsigned long long)last.session,
				       last.state, last.fault, last.inputs_valid, last.candidate_valid, last.cycles);

				if (last.candidate_valid && last.timestamp >= begin) { candidate = last; saw_candidate = true; }

				check(!last.outputs_enabled, "diagnostics never grant outputs");

				if (!last.candidate_valid) {
					for (float value : last.canard_candidate) { check(fabsf(value) <= 0.0F, "invalid candidate cleared"); }
				}
			}

			check(!bench.updated() && !servos.updated() && !motors.updated(), "no actuator publication");
		}
	}
	// NotDue dispatches deliberately publish no candidate. Verify an actual
	// successful calculation observed in this interval as well as current state.
	bool holding() const { return last.state == 3 && last.inputs_valid && saw_candidate && candidate.state == 3; }
};
}

extern "C" __EXPORT int stabilized_observer_test_main(int argc, char *argv[])
{
	if (argc != 2) { return 1; }

	for (const char *name : {"EKF2_MULTI_IMU", "EKF2_MULTI_MAG", "SENS_IMU_MODE", "SENS_MAG_MODE"}) {
		const int32_t value = strncmp(name, "SENS", 4) == 0 ? 1 : 0;
		check(param_set(param_find(name), &value) == 0, "single estimator test configuration");
	}
	Fixture f;
	check(command("start", argv[1]) == 0, "start Ready");
	f.steps(0.0F);
	check(f.last.state == 1 && !f.last.candidate_valid && f.last.session == 0, "no automatic observation");
	check(command("enable") != 0, "no output enable command");
	check(command("observe") == 0, "explicit observation");
	f.steps(0.0F);
	check(f.holding(), "initial hold");
	const auto session = f.last.session;
	f.steps(0.2F);
	check(f.last.state == 2 && f.saw_candidate && f.candidate.state == 2 && f.candidate.rate_setpoint[0] > 0.0F, "rate command");
	f.steps(0.0F);
	check(f.holding() && fabsf(f.candidate.hold_reference[0] - 1.0F) < 0.00001F, "capture hold on release");
	check(command("disable") == 0, "disable request");
	f.steps(0.0F);
	check(f.last.state == 4 && !f.last.candidate_valid && f.last.session == session, "disable latched");
	check(command("observe") != 0, "no same-session recovery");
	check(command("stop") == 0, "stop");

	for (unsigned fault = 0; fault < 7; ++fault) {
		check(command("start", argv[1]) == 0, "restart");
		f.steps(0.0F);
		check(command("observe") == 0, "new observation session");
		f.steps(0.0F);
		check(f.holding() && f.last.session > session, "fresh centered recovery");

		if (fault == 0) { ++f.reset; }

		if (fault == 1) { ++f.gyro; }

		if (fault == 2) { f.killed = true; }

		if (fault == 3) { f.bad_health = true; }

		if (fault == 4) { f.omit_manual = true; }

		if (fault == 5) { f.future = true; }

		if (fault == 6) { const int32_t value = 2; check(param_set(param_find("EKF2_MULTI_IMU"), &value) == 0, "unsupported estimator profile"); }

		f.steps(0.0F, 30);
		check(f.last.state == 4 && !f.last.candidate_valid, "fault revokes observation");
		f.killed = f.bad_health = f.omit_manual = f.future = false;

		if (fault == 6) { const int32_t value = 0; check(param_set(param_find("EKF2_MULTI_IMU"), &value) == 0, "restore profile"); }

		f.steps(0.0F);
		check(f.last.state == 4 && command("observe") != 0, "valid samples cannot clear latch");
		check(command("stop") == 0, "faulted stop");
	}

	PX4_INFO("OBSERVER_RESULT checks=%u failures=%u", checks, failures);
	return failures ? 1 : 0;
}
