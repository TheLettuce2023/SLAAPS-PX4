// Private POSIX test overlay. Synthetic state and memory capture only.
#include <px4_platform_common/defines.h>
#include <px4_platform_common/posix.h>
#include <parameters/param.h>
#include <lib/mixer_module/mixer_module.hpp>
#include <uORB/Publication.hpp>
#include <uORB/Subscription.hpp>
#include <uORB/topics/manual_control_setpoint.h>
#include <uORB/topics/actuator_armed.h>
#include <uORB/topics/actuator_test.h>
#include <uORB/topics/actuator_motors.h>
#include <uORB/topics/bench_servo_command.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <mavlink.h>
#include <px4_platform_common/atomic.h>
#include <modules/manual_control/ManualControl.hpp>
#include <uORB/PublicationMulti.hpp>

extern "C" int research_bench_control_main(int, char **);
extern "C" int manual_control_main(int, char **);

namespace
{
unsigned checks{}, failures{};
void check(bool passed, const char *label)
{
	++checks;

	if (!passed) { ++failures; PX4_ERR("CHECK_FAIL %s", label); }
}
bool near(float a, float b) { return std::isfinite(a) && fabsf(a - b) < 0.2F; }
int command(const char *verb, const char *path = nullptr)
{
	char name[] = "research_bench_control";
	char *args[] = {name, const_cast<char *>(verb), const_cast<char *>(path), nullptr};
	return research_bench_control_main(path ? 3 : 2, args);
}
void set_int(const char *name, int32_t value)
{
	const auto handle = param_find(name);
	check(handle != PARAM_INVALID && param_set(handle, &value) == 0, name);
}
void set_float(const char *name, float value)
{
	const auto handle = param_find(name);
	check(handle != PARAM_INVALID && param_set(handle, &value) == 0, name);
}
void configure()
{
	param_control_autosave(false);
	set_int("COM_RC_IN_MODE", 1);
	set_int("MAN_ARM_GESTURE", 0);
	set_float("MAN_KILL_GEST_T", -1);
	set_float("COM_RC_LOSS_T", 0.5F);
	char name[24] {};

	for (unsigned i = 1; i <= 8; ++i) {
		snprintf(name, sizeof(name), "PWM_MAIN_FUNC%u", i); set_int(name, i <= 4 ? 9999 + i : 0);
		snprintf(name, sizeof(name), "PWM_AUX_FUNC%u", i); set_int(name, 0);

		if (i <= 4) {
			const char *fields[] = {"MIN", "MAX", "CENT", "DIS", "FAIL"};
			const int values[] = {500, 2500, 1500, 0, 0};

			for (unsigned j = 0; j < 5; ++j) {
				snprintf(name, sizeof(name), "PWM_MAIN_%s%u", fields[j], i); set_int(name, values[j]);
			}
		}
	}

	set_int("PWM_MAIN_REV", 0);
}

// Exercise the real selector's snapshot boundary without starting its work
// item. Publishing inside the injected clock deterministically distinguishes
// capture-before-clock from capture-after-clock for all three input slots.
uORB::PublicationMulti<manual_control_setpoint_s> *snapshot_publications{};
unsigned snapshot_clock_reads{};
hrt_abstime snapshot_clock()
{
	++snapshot_clock_reads;

	if (snapshot_clock_reads == 1) {
		for (unsigned i = 0; i < 3; ++i) {
			manual_control_setpoint_s next{};
			next.valid = true; next.data_source = manual_control_setpoint_s::SOURCE_MAVLINK_0;
			next.timestamp = next.timestamp_sample = 1000022;
			next.roll = 0.75F; next.pitch = next.yaw = next.throttle = 0;
			next.source_system_id = 255; next.source_component_id = 190;
			check(snapshot_publications[i].publish(next), "publication at selector clock boundary");
		}
	}

	return snapshot_clock_reads == 1 ? 1000022 : 1000044;
}
class SnapshotSelector final : public ManualControl
{
public:
	void processClock() { processInput(0, snapshot_clock); }
	void processFixed(hrt_abstime now) { processInput(now); }
};
void selector_snapshot()
{
	uORB::PublicationMulti<manual_control_setpoint_s> pubs[3] {
		{ORB_ID(manual_control_input)}, {ORB_ID(manual_control_input)}, {ORB_ID(manual_control_input)}
	};
	snapshot_publications = pubs;
	snapshot_clock_reads = 0;
	SnapshotSelector selector;
	uORB::Subscription selected{ORB_ID(manual_control_setpoint)};

	for (unsigned i = 0; i < 3; ++i) {
		manual_control_setpoint_s first{};
		first.valid = true; first.data_source = manual_control_setpoint_s::SOURCE_MAVLINK_0;
		first.timestamp = first.timestamp_sample = 1000000;
		first.roll = 0.25F; first.pitch = first.yaw = first.throttle = 0;
		first.source_system_id = 255; first.source_component_id = 190;
		check(pubs[i].publish(first), "initial selector input");
		check(pubs[i].get_instance() == static_cast<int>(i), "fixed selector input instances");
	}

	selector.processClock();
	manual_control_setpoint_s choice{};
	check(selected.copy(&choice) && choice.valid, "selector publishes valid captured input");
	check(snapshot_clock_reads == 1, "selector reads live clock once");
	check(choice.timestamp == 1000022 && choice.timestamp_sample == 1000000,
	      "all input snapshots precede evaluation clock");
	check(near(choice.roll, 0.25F), "clock-boundary publication cannot change current snapshot");
	selector.processClock();
	check(selected.copy(&choice) && choice.valid, "next snapshot remains valid");
	check(choice.timestamp == 1000044 && choice.timestamp_sample == 1000022,
	      "next cycle consumes concurrent publication without restamping sample");
	check(near(choice.roll, 0.75F), "next cycle uses newly captured axes");
	selector.processFixed(1000066);
	check(selected.copy(&choice) && choice.timestamp == 1000066, "fixed-clock test seam remains compatible");
	snapshot_publications = nullptr;
}

class CaptureMixingOutput final : public MixingOutput
{
public:
	using MixingOutput::MixingOutput;
	using MixingOutput::output_limit_calc_single;
};

class Capture final : public OutputModuleInterface
{
public:
	Capture() : OutputModuleInterface("bench_capture", px4::wq_configurations::test1),
		mix("PWM_MAIN", 8, *this, MixingOutput::SchedulingPolicy::Disabled, true, false) {}
	void Run() override {}
	void refresh() { updateParams(); }
	bool updateOutputs(float values[MAX_ACTUATORS], unsigned count, unsigned) override
	{
		for (unsigned i = 0; i < count; ++i) { pwm[i] = values[i]; }

		++writes;
		return true; // memory and stock actuator_outputs telemetry only; no driver.
	}
	void tick() { mix.update(); mix.updateSubscriptions(false); }
	float pwm[MAX_ACTUATORS] {};
	unsigned writes{};
	CaptureMixingOutput mix;
};

// Exercise the configured center range, including valid centers outside the
// older mixer's fixed 800..2200 us window. This calls the real mixer without
// starting an owner or a physical output driver.
void center_bounds()
{
	Capture capture;

	for (const auto function : {OutputFunction::Research_Bench1, OutputFunction::Servo1}) {
		set_int("PWM_MAIN_FUNC1", static_cast<int32_t>(function));
		capture.refresh();
		capture.mix.updateSubscriptions(false);

		for (const int center : {650, 2350, 400, 2600}) {
			set_int("PWM_MAIN_CENT1", center);
			capture.refresh();
			const bool valid = center >= 500 && center <= 2500;
			const float expected_center = valid ? static_cast<float>(center) : 1500.0F;
			check(near(capture.mix.output_limit_calc_single(0, 0), expected_center), "configured center validity");
			check(near(capture.mix.output_limit_calc_single(0, -1), 500), "negative endpoint retained");
			check(near(capture.mix.output_limit_calc_single(0, 1), 2500), "positive endpoint retained");
			check(near(capture.mix.output_limit_calc_single(0, -0.5F), (500 + expected_center) * 0.5F),
			      "negative interpolation follows valid center");
			check(near(capture.mix.output_limit_calc_single(0, 0.5F), (2500 + expected_center) * 0.5F),
			      "positive interpolation follows valid center");
		}
	}
}

// Stalls only the private process's owner queue; the capture sink remains on
// the command thread. These durations are fault injection, not timing budgets.
class OwnerQueueStall final : public px4::WorkItem
{
public:
	OwnerQueueStall() : WorkItem("bench_stall", px4::wq_configurations::test1) {}
	~OwnerQueueStall() override { Deinit(); }
	void Run() override { started.store(true); px4_usleep(1000000); finished.store(true); }
	px4::atomic_bool started{false};
	px4::atomic_bool finished{false};
};

class Fixture
{
public:
	explicit Fixture(Capture &capture, bool wire) : sink(capture)
	{
		if (wire) {
			wire_socket = socket(AF_INET, SOCK_DGRAM, 0);
			check(wire_socket >= 0, "private loopback sender socket");
			wire_address.sin_family = AF_INET;
			wire_address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
			wire_address.sin_port = htons(19759);
		}
	}
	~Fixture() { if (wire_socket >= 0) { close(wire_socket); } }
	void safety(bool prearmed = true, bool armed = false, bool kill = false, bool termination = false, bool lockdown = false)
	{
		actuator_armed_s state{};
		state.timestamp = hrt_absolute_time(); state.prearmed = prearmed; state.armed = armed;
		state.kill = kill; state.termination = termination; state.lockdown = lockdown;
		check(armed_pub.publish(state), "synthetic safety publication");
	}
	void input(float roll = 0, float pitch = 0, float yaw = 0, float throttle = 0, unsigned system = 255)
	{
		if (wire_socket >= 0) {
			mavlink_manual_control_t payload{};
			payload.target = wire_target;
			payload.x = std::isfinite(pitch) ? static_cast<int16_t>(pitch * 1000) : INT16_MAX;
			payload.y = std::isfinite(roll) ? static_cast<int16_t>(roll * 1000) : INT16_MAX;
			payload.r = std::isfinite(yaw) ? static_cast<int16_t>(yaw * 1000) : INT16_MAX;
			payload.z = std::isfinite(throttle) ? static_cast<int16_t>(500 + throttle * 500) : INT16_MAX;
			mavlink_message_t packet{};
			mavlink_msg_manual_control_encode(system, 190, &packet, &payload);
			uint8_t bytes[MAVLINK_MAX_PACKET_LEN] {};
			const auto length = mavlink_msg_to_send_buffer(bytes, &packet);
			check(sendto(wire_socket, bytes, length, 0, reinterpret_cast<sockaddr *>(&wire_address),
				     sizeof(wire_address)) == length, "loopback MANUAL_CONTROL sent");
			return;
		}

		manual_control_setpoint_s message{};
		message.timestamp = message.timestamp_sample = hrt_absolute_time() + input_time_offset;
		message.valid = true; message.data_source = manual_control_setpoint_s::SOURCE_MAVLINK_0;
		message.source_system_id = system; message.source_component_id = 190;
		message.roll = roll; message.pitch = pitch; message.yaw = yaw; message.throttle = throttle;
		check(input_pub.publish(message), "manual input publication");
	}
	void drive(float roll, float pitch, float yaw, float throttle = 0, unsigned iterations = 12, unsigned system = 255)
	{
		for (unsigned i = 0; i < iterations; ++i) {
			input(roll, pitch, yaw, throttle, system);
			px4_usleep(25000); // synthetic fixture pacing, not a deployed budget.
			sink.tick();
		}
	}
	void wait(unsigned iterations)
	{
		for (unsigned i = 0; i < iterations; ++i) { px4_usleep(25000); sink.tick(); }
	}
	bool enabled()
	{
		bench_servo_command_s frame{};
		return output.copy(&frame) && frame.enabled;
	}
	bool begin(const char *path)
	{
		safety(); drive(0, 0, 0);
		check(command("start", path) == 0, "explicit start");
		check(command("enable") == 0, "enable request");
		drive(0, 0, 0);
		const bool active = enabled();
		check(active, "real module reached active publication");
		return active;
	}
	void neutral(const char *label)
	{
		for (unsigned i = 0; i < 4; ++i) { check(near(sink.pwm[i], 1500), label); }
	}
	void disabled(const char *label)
	{
		for (unsigned i = 0; i < 4; ++i) { check(near(sink.pwm[i], 0), label); }
	}
	uORB::Publication<manual_control_setpoint_s> input_pub{ORB_ID(manual_control_input)};
	uORB::Publication<actuator_armed_s> armed_pub{ORB_ID(actuator_armed)};
	uORB::Subscription output{ORB_ID(bench_servo_command)};
	Capture &sink;
	int wire_socket{-1};
	uint8_t wire_target{1};
	hrt_abstime input_time_offset{};
	sockaddr_in wire_address{};
};

void nominal(Fixture &f)
{
	uORB::Subscription selected{ORB_ID(manual_control_setpoint)};
	manual_control_setpoint_s choice{};
	check(selected.copy(&choice) && choice.valid && choice.source_system_id == 255
	      && choice.source_component_id == 190, "actual selector preserves identity");
	const float mix[4][3] = {{1, 1, 1}, {1, -1, -1}, {-1, 1, -1}, {-1, -1, 1}};

	for (float r : {-1.0F, 0.0F, 1.0F}) {
		for (float p : {-1.0F, 0.0F, 1.0F}) {
			for (float y : {-1.0F, 0.0F, 1.0F}) {
				f.drive(r, p, y);
				float demand[4] {}; float magnitude = 1;

				for (unsigned i = 0; i < 4; ++i) {
					demand[i] = r * mix[i][0] + p * mix[i][1] + y * mix[i][2];

					if (fabsf(demand[i]) > magnitude) { magnitude = fabsf(demand[i]); }
				}

				for (unsigned i = 0; i < 4; ++i) {
					// Synthetic C2 is reversed; endpoints 1400/1500/1600.
					const float expected = 1500 + (i == 1 ? -100 : 100) * demand[i] / magnitude;

					if (!near(f.sink.pwm[i], expected)) {
						PX4_ERR("POSE r=%g p=%g y=%g channel=%u expected=%g actual=%g enabled=%d",
							(double)r, (double)p, (double)y, i, (double)expected, (double)f.sink.pwm[i], (int)f.enabled());
					}

					check(near(f.sink.pwm[i], expected), "27-pose coupled mix and calibrated reversal");
				}

				for (unsigned i = 4; i < 8; ++i) { check(near(f.sink.pwm[i], 0), "unused MAIN disabled"); }
			}
		}
	}
	f.drive(0.04F, -0.04F, 0.04F); f.neutral("deadzone applied once");
	f.drive(0.525F, 0, 0);
	check(near(f.sink.pwm[0], 1550), "deadzone rescaling");

	for (float throttle : {-1.0F, 1.0F, NAN}) {
		f.drive(0.525F, 0, 0, throttle);
		check(near(f.sink.pwm[0], 1550), "throttle including NaN ignored");
	}
	uORB::Publication<actuator_test_s> competing{ORB_ID(actuator_test)};
	actuator_test_s test{};
	test.timestamp = hrt_absolute_time(); test.function = 10000;
	test.action = actuator_test_s::ACTION_DO_CONTROL; test.value = -1; test.timeout_ms = 200;
	check(competing.publish(test), "competing synthetic actuator_test publication");
	f.drive(0.525F, 0, 0, 0, 2);
	check(near(f.sink.pwm[0], 1550), "actuator_test cannot override dedicated output");
}

void lease_fault(Fixture &f)
{
	// Stop real producer, then inject a isolated synthetic downstream lease to
	// test actual provider independently. This is not an owner-stall claim.
	check(command("stop") == 0, "stop before isolated consumer test"); f.wait(2);
	uORB::Publication<bench_servo_command_s> pub{ORB_ID(bench_servo_command)};
	bench_servo_command_s frame{};
	frame.timestamp = frame.session = hrt_absolute_time(); frame.lease_us = 100000;
	frame.enabled = true;

	for (float &value : frame.control) { value = 0.1F; }

	check(pub.publish(frame), "isolated lease publication"); f.sink.tick();
	check(near(f.sink.pwm[0], 1600), "real consumer accepts new fixture session");
	f.wait(5); f.neutral("actual consumer expires stopped producer");
	frame.timestamp = hrt_absolute_time(); check(pub.publish(frame), "late same-session publication");
	f.sink.tick(); f.neutral("same session cannot renew expired consumer");
	frame.session = ++frame.timestamp; px4_usleep(1000);
	check(pub.publish(frame), "new consumer session publication"); f.sink.tick();
	check(near(f.sink.pwm[0], 1600), "consumer accepts newer explicit session");
}
}

extern "C" __EXPORT int bench_active_test_main(int argc, char *argv[])
{
	if (argc != 3) { return 2; }

	checks = failures = 0;
	configure();

	if (strcmp(argv[1], "center_bounds") == 0) {
		center_bounds();
		PX4_INFO("H9_RESULT scenario=%s checks=%u failures=%u capture_writes=0", argv[1], checks, failures);
		return failures ? 1 : 0;
	}

	if (strcmp(argv[1], "selector_snapshot") == 0) {
		selector_snapshot();
		PX4_INFO("H9_RESULT scenario=%s checks=%u failures=%u capture_writes=0", argv[1], checks, failures);
		return failures ? 1 : 0;
	}

	Capture capture;
	const bool wire = strncmp(argv[1], "wire_", 5) == 0;
	Fixture fixture(capture, wire);
	fixture.safety(); capture.tick(); capture.tick();
	char manual_name[] = "manual_control", start[] = "start", stop[] = "stop";
	char *manual_args[] = {manual_name, start, nullptr};
	check(manual_control_main(2, manual_args) == 0, "start stock selector module");
	fixture.drive(0, 0, 0);
	fixture.disabled("startup without bench owner is disabled");

	if (fixture.begin(argv[2])) {
		const char *scenario = wire ? argv[1] + 5 : argv[1];

		if (strcmp(scenario, "nominal") == 0) {
			nominal(fixture);
			check(command("disable") == 0, "disable request"); fixture.drive(0, 0, 0);
			fixture.neutral("disable neutral");
			check(command("enable") == 0, "reenable queued but cannot clear revoke"); fixture.drive(1, 0, 0);
			fixture.neutral("revoked owner cannot recover without restart");

		} else if (strcmp(scenario, "source") == 0) {
			fixture.drive(0.5F, 0, 0, 0, 4, 254); fixture.neutral("identity change neutral");
			fixture.drive(1, 0, 0); fixture.neutral("identity recovery remains revoked");

		} else if (strcmp(scenario, "invalid") == 0) {
			fixture.drive(NAN, 0, 0, 0, 4); fixture.neutral("invalid axis neutral");

		} else if (strcmp(scenario, "future") == 0) {
			fixture.drive(1, 0, 0);
			check(near(capture.pwm[0], 1600), "active before genuine future input");
			fixture.input_time_offset = 10000000; // Deliberate invalid fixture timestamp.
			fixture.drive(1, 0, 0, 0, 4); fixture.neutral("genuine future input rejected");
			fixture.input_time_offset = 0;
			fixture.drive(1, 0, 0); fixture.neutral("future fault cannot auto-recover");

		} else if (strcmp(scenario, "loss") == 0) {
			fixture.drive(1, 0, 0); fixture.wait(30); fixture.neutral("input loss neutral");
			fixture.drive(1, 0, 0); fixture.neutral("input recovery remains revoked");

		} else if (strcmp(scenario, "lease") == 0) {
			lease_fault(fixture);

		} else if (strcmp(scenario, "routing") == 0) {
			set_int("PWM_MAIN_FUNC1", 10001);
			capture.refresh(); capture.mix.updateSubscriptions(false);
			fixture.drive(1, 0, 0); check(near(capture.pwm[0], 0), "wrong MAIN routing disabled");

		} else if (strcmp(scenario, "target") == 0) {
			fixture.drive(1, 0, 0);
			check(near(capture.pwm[0], 1600), "wire command moved capture before target change");
			fixture.wire_target = 2; fixture.drive(-1, 0, 0, 0, 30);
			fixture.neutral("wrong wire target ignored until existing input expires");
			fixture.wire_target = 1; fixture.drive(1, 0, 0);
			fixture.neutral("correct wire target cannot revive revoked session");

		} else if (strcmp(scenario, "stall") == 0) {
			fixture.drive(1, 0, 0);
			check(near(capture.pwm[0], 1600), "active before owner queue stall");
			OwnerQueueStall stall;
			stall.ScheduleNow();

			for (unsigned i = 0; i < 20 && !stall.started.load(); ++i) { px4_usleep(5000); }

			check(stall.started.load(), "owner queue fault injection started");
			fixture.drive(1, 0, 0, 0, 24);
			check(!stall.finished.load(), "consumer expires while owner queue still blocked");
			fixture.neutral("independent consumer lease expires stalled owner");
			fixture.drive(1, 0, 0, 0, 24);
			check(stall.finished.load(), "owner queue released");
			fixture.neutral("late owner cannot resume expired session");

		} else if (strcmp(scenario, "restart") == 0) {
			fixture.drive(1, 0, 0);
			check(command("stop") == 0, "active owner explicitly stopped");
			// Stop attempts a terminal frame, but independent expiry is the
			// guaranteed fallback. Wait beyond this fixture's 500 ms lease.
			fixture.drive(0, 0, 0, 0, 24); fixture.neutral("stop neutral after fixture lease deadline");

			if (fixture.begin(argv[2])) {
				fixture.drive(1, 0, 0);
				check(near(capture.pwm[0], 1600), "explicit centered restart creates new session");
			}

		} else {
			fixture.drive(1, 0, 0);
			fixture.safety(strcmp(scenario, "prearm") != 0, strcmp(scenario, "armed") == 0,
				       strcmp(scenario, "kill") == 0, strcmp(scenario, "termination") == 0,
				       strcmp(scenario, "lockdown") == 0);
			fixture.wait(4); fixture.disabled("stock safety forces disabled output");
			fixture.safety(); fixture.drive(1, 0, 0); fixture.neutral("safety restoration cannot revive session");
		}
	}

	if (strcmp(argv[1], "lease") != 0) { check(command("stop") == 0, "bench owner stopped"); }

	manual_args[1] = stop;
	check(manual_control_main(2, manual_args) == 0, "stock selector stopped");
	uORB::Subscription motors{ORB_ID(actuator_motors)}; actuator_motors_s motor{};
	check(!motors.copy(&motor), "no motor or EDF command publication");
	check(capture.writes > 0, "actual MixingOutput reached memory sink");
	PX4_INFO("H9_RESULT scenario=%s checks=%u failures=%u capture_writes=%u", argv[1], checks, failures, capture.writes);
	return failures ? 1 : 0;
}
