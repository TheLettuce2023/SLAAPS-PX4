#include "StabilizedManualSession.hpp"
#include "ObserverProfile.hpp"

#include <cmath>
#include <cstdio>

using namespace research_stabilized_manual;
namespace
{
unsigned checks{}, failures{};
constexpr TimeUs kStart{1000000}, kTick{4000};
constexpr uint32_t kReceiver{3};
void check(bool ok, const char *message)
{
	++checks;

	if (!ok) { ++failures; printf("FAIL %s\n", message); }
}
bool near(float a, float b) { return std::fabs(a - b) < 1.0e-6F; }
Configuration controlConfiguration()
{
	// Explicit synthetic profile, shared values with the observer/control tests.
	const char *fixture =
		"SLAAPS_STABILIZED_OBSERVER_V1 2 255 190 "
		"100000 100000 50000 4000 60000 1100000 600000 "
		"0.01 0.04 0.06 "
		"1 -1 1  1 1 1  20 20 20  20 20 20  100 100 100  4 4 4 "
		"0.4 0.8 0.8  0.05 0.1 0.1  0.003 0.006 0.006  0.2 0.2 0.2  1 1 1 "
		"0.25 -0.25 0.25 -0.25  0.25 0.25 -0.25 -0.25  0.25 -0.25 -0.25 0.25 "
		"0 0 0 0  -1 -1 -1 -1  1 1 1 1  5 5 5 5  5 5 5 5 "
		"1 1 1  0.00001 0.01 0.01 0.001";
	FILE *file = tmpfile();
	ObserverProfile profile{};
	check(file != nullptr, "profile stream");

	if (file) {
		fputs(fixture, file);
		rewind(file);
		check(readObserverProfile(file, profile), "synthetic profile parses");
		fclose(file);
	}

	return profile.control;
}
OutputConfiguration outputConfiguration()
{
	OutputConfiguration c{};
	c.lease_us = 40000;
	c.acknowledgement_us = 20000;
	c.on_fault = OutputFallback::Disarmed;

	for (unsigned i = 0; i < 4; ++i) {
		c.servos[i] = {1100, 1450, 2100, 500, 1500, 2500};
		c.minimum[i] = -1;
		c.maximum[i] = 1;
	}

	return c;
}
struct Fixture {
	Configuration control{controlConfiguration()};
	OutputConfiguration output{outputConfiguration()};
	ManualSample manual{};
	StateSample sample{};
	StabilizedManualSession session{};
	Report report{};
	OutputFrame frame{};
	TimeUs now{kStart};
	Fixture()
	{
		manual.source = research_actuator_command::ManualControlSource::Mavlink;
		manual.mavlink_instance = 2;
		manual.source_system_id = 255;
		manual.source_component_id = 190;
		manual.valid = true;
		sample.valid = true;
		sample.source_epoch = 1;
		stamp();
	}
	void stamp()
	{
		manual.timestamp_us = manual.timestamp_sample_us = now;
		sample.attitude_timestamp_us = sample.attitude_sample_us = now;
		sample.rate_timestamp_us = sample.rate_sample_us = now;
	}
	OutputStatus begin(TimeUs token = kStart)
	{
		return session.begin(now, token, control, output, manual, sample, true, kReceiver, kReceiver);
	}
	SessionUpdate update(const BoundOutputAcknowledgement *ack = nullptr, uint32_t revision = kReceiver, bool permitted = true)
	{
		return session.update(now, permitted, manual, sample, revision, ack, report, frame);
	}
	void first()
	{
		check(begin() == OutputStatus::Ok, "explicit session begins");
		now += kTick;
		manual.axes[0] = 0.4F;
		stamp();
		check(update().status == OutputStatus::Ok && frame.sequence == 1, "first rate frame");
	}
	BoundOutputAcknowledgement ack() const
	{
		return {{frame.session, frame.timestamp_us, frame.timestamp_us, frame.sequence}, kReceiver};
	}
};
void lifecycle()
{
	Fixture f;
	check(f.update().status == OutputStatus::InvalidState, "construction cannot produce commands");
	f.first();
	const auto first = f.frame;
	const auto ack = f.ack();
	check(f.session.begin(f.now, f.now, f.control, f.output, f.manual, f.sample, true, kReceiver, kReceiver)
	      == OutputStatus::InvalidState && f.session.active(), "competing begin preserves active session");
	auto result = f.update(&ack);
	check(result.status == OutputStatus::NoCommand && result.acknowledged && f.frame.sequence == first.sequence,
	      "same-clock acknowledgment does not integrate again");
	f.now += 1000;
	f.stamp();
	check(f.update().status == OutputStatus::NoCommand, "minimum cycle interval cannot create a frame");
	f.now = kStart + 2 * kTick;
	f.manual.axes[0] = 0;
	f.stamp();
	check(f.update().status == OutputStatus::Ok && f.report.state == State::Hold && f.frame.sequence == 2,
	      "centered sticks resume attitude hold after acknowledgment");
	f.session.disable();
	check(!f.session.active() && f.session.controlState() == State::Disabled, "explicit disable resets controller");
	check(f.begin() == OutputStatus::InvalidTime, "old session cannot be reused after disable");
	f.session.disable();
	++f.now;
	f.stamp();
	check(f.begin(f.now) == OutputStatus::Ok, "new grant recovers after explicit reset");
}
void waitingDoesNotIntegrate()
{
	Fixture f;
	StabilizedManualControl reference;
	check(reference.configure(f.control) == Status::Ok && reference.enable(f.now, f.manual, f.sample) == Status::Ok,
	      "reference controller begins");
	f.first();
	Report expected{};
	check(reference.cycle(f.now, f.manual, f.sample, expected) == Status::Ok, "reference first cycle");
	const auto first = f.frame;
	const auto ack = f.ack();
	const TimeUs report_time = f.report.timestamp_us;

	for (unsigned i = 1; i <= 3; ++i) {
		f.now = first.timestamp_us + i * 1000;
		f.stamp();
		check(f.update().status == OutputStatus::Waiting && f.report.timestamp_us == report_time
		      && f.frame.sequence == first.sequence, "waiting validates without replacing outputs");
	}

	f.now = first.timestamp_us + kTick;
	f.stamp();
	const auto result = f.update(&ack);
	check(result.status == OutputStatus::Ok && result.acknowledged, "matching ack resumes calculation");
	check(reference.cycle(f.now, f.manual, f.sample, expected) == Status::Ok, "reference resumes after same elapsed time");

	for (unsigned i = 0; i < 3; ++i) {
		check(near(expected.integral(i), f.report.integral(i)), "waiting does not integrate PID");
		check(near(expected.rate_setpoint_rad_s(i), f.report.rate_setpoint_rad_s(i)), "waiting does not advance rate slew");
	}

	for (unsigned i = 0; i < 4; ++i) {
		check(near(expected.allocation.command[i], f.report.allocation.command[i]), "waiting does not advance allocator slew");
		check(near(expected.hold_reference(i), f.report.hold_reference(i)), "waiting does not move hold reference");
	}
}
void faults()
{
	for (unsigned fault = 0; fault < 12; ++fault) {
		Fixture f;
		f.first();
		const auto before = f.frame;
		const auto timestamp = f.report.timestamp_us;
		auto ack = f.ack();
		const BoundOutputAcknowledgement *received = nullptr;
		uint32_t revision = kReceiver;
		bool permitted = true;
		f.now += 1000;
		f.stamp();

		switch (fault) {
		case 0: revision = 0; break;

		case 1: revision = kReceiver + 1; break;

		case 2: received = &ack; ack.source_revision = 0; break;

		case 3: permitted = false; break;

		case 4: f.sample.valid = false; break;

		case 5: ++f.sample.source_epoch; break;

		case 6: f.manual.valid = false; break;

		case 7: f.now = before.timestamp_us - 1; break;

		case 8: f.now = before.timestamp_us + f.output.acknowledgement_us; f.stamp(); break;

		case 9: received = &ack; ++ack.value.sequence; break;

		case 10: received = &ack; ack.value.accepted_us = f.now + 1; break;

		case 11:
			check(f.update().status == OutputStatus::Waiting, "first observed waiting sample");
			++f.now;
			f.sample.rate_sample_us = before.timestamp_us;
			break;
		}

		const auto result = f.update(received, revision, permitted);
		check(result.status != OutputStatus::Ok && result.status != OutputStatus::NoCommand
		      && result.status != OutputStatus::Waiting, "fault rejects session while awaiting ack");
		check(!f.session.active() && f.session.controlState() == State::Disabled, "fault disables control state");
		check(f.frame.sequence == before.sequence && f.frame.timestamp_us == before.timestamp_us
		      && f.report.timestamp_us == timestamp, "fault preserves caller output buffers");
		check(f.session.acknowledgementSourceFault() == (fault <= 2), "ack source fault distinguished from protocol/health faults");
		f.now += kTick;
		f.manual.valid = f.sample.valid = true;
		f.sample.source_epoch = 1;
		f.stamp();
		check(f.update().status == result.status && !f.session.active(), "restored inputs cannot automatically resume");
	}
}
void grantFailures()
{
	for (unsigned fault = 0; fault < 6; ++fault) {
		Fixture f;
		uint32_t expected = kReceiver, current = kReceiver;
		bool permitted = true;

		switch (fault) {
		case 0: expected = 0; break;

		case 1: ++current; break;

		case 2: permitted = false; break;

		case 3: f.manual.axes[0] = 0.4F; break;

		case 4: f.output.maximum[0] = 0.5F; break;

		case 5: f.control.rate_p(0) = NAN; break;
		}

		check(f.session.begin(f.now, f.now, f.control, f.output, f.manual, f.sample, permitted, expected, current)
		      != OutputStatus::Ok && !f.session.active(), "invalid grant cannot enable control");
	}
}
}

int main()
{
	lifecycle();
	waitingDoesNotIntegrate();
	faults();
	grantFailures();
	printf("Stabilized session: %u checks, %u failures\n", checks, failures);
	return failures ? 1 : 0;
}
