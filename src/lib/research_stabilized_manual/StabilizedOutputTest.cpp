#include "StabilizedOutput.hpp"
#include "StabilizedOutputDriver.hpp"
#include "ObserverProfile.hpp"

#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <limits>

using namespace research_stabilized_manual;
namespace
{
unsigned checks{}, failures{};
constexpr TimeUs kStart{1000000};
constexpr TimeUs kTick{4000};

void check(bool condition, const char *message)
{
	++checks;

	if (!condition) { ++failures; printf("FAIL: %s\n", message); }
}

bool near(float a, float b) { return std::fabs(a - b) < 0.001F; }

OutputConfiguration fixtureConfiguration()
{
	// Synthetic calibration, asymmetric pulse spans and alternating reversal.
	// No installed-vehicle geometry, limits, fallback or timing is implied.
	OutputConfiguration result{};
	result.lease_us = 20000;
	result.acknowledgement_us = 8000;
	result.on_fault = OutputFallback::Calibrated;

	for (unsigned i = 0; i < 4; ++i) {
		result.servos[i] = {1100.0F, 1450.0F, 2100.0F, 500, 1500, 2500};

		if (i % 2 != 0) { result.servos[i] = {2100.0F, 1450.0F, 1100.0F, 500, 1500, 2500}; }

		result.minimum[i] = -1.0F;
		result.maximum[i] = 1.0F;
		result.fallback[i] = 0.1F; // Deliberately not zero/trim.
	}

	return result;
}

struct Pair {
	OutputConfiguration config{fixtureConfiguration()};
	StabilizedOutputProducer producer{};
	StabilizedOutputConsumer consumer{};
	TimeUs now{kStart};
	Report report{};
	OutputFrame frame{};
	OutputAcknowledgement ack{};

	Pair()
	{
		check(producer.begin(now, now, config, true) == OutputStatus::Ok, "producer explicit session");
		check(consumer.begin(now, now, config, true) == OutputStatus::Ok, "consumer explicit session");
		report.state = State::Hold;
	}
	void send()
	{
		now += kTick;
		report.timestamp_us = now;
		report.allocation.command = {0.5F, -0.5F, 1.0F, -1.0F};
		check(producer.submit(now, true, Status::Ok, report, frame) == OutputStatus::Ok, "successful report submitted");
	}
	void accept()
	{
		check(consumer.accept(now, true, frame, ack) == OutputStatus::Ok, "consumer validates command");
	}
	void acknowledge()
	{
		check(producer.acknowledge(now, true, ack) == OutputStatus::Ok, "producer accepts matching acknowledgement");
	}
	void cycle()
	{
		send();
		accept();
		acknowledge();
	}
};

void mappingTests()
{
	const auto config = fixtureConfiguration();
	check(!validOutputConfiguration({}), "absent configuration rejected");
	check(validOutputConfiguration(config), "explicit synthetic configuration accepted");

	for (unsigned i = 0; i < 4; ++i) {
		for (unsigned mutation = 0; mutation < 14; ++mutation) {
			auto bad = config;

			switch (mutation) {
			case 0: bad.servos[i].negative_pwm_us = NAN; break;

			case 1: bad.servos[i].zero_pwm_us = INFINITY; break;

			case 2: bad.servos[i].positive_pwm_us = 2501.0F; break;

			case 3: bad.servos[i].zero_pwm_us = bad.servos[i].positive_pwm_us; break;

			case 4: bad.servos[i].zero_pwm_us = 501.0F; break;

			case 5: bad.servos[i].driver_minimum_us = 0; break;

			case 6: bad.servos[i].driver_center_us = 500; break;

			case 7: bad.servos[i].driver_maximum_us = 1500; break;

			case 8: bad.minimum[i] = -1.01F; break;

			case 9: bad.maximum[i] = INFINITY; break;

			case 10: bad.minimum[i] = bad.maximum[i]; break;

			case 11: bad.fallback[i] = NAN; break;

			case 12: bad.minimum[i] = 0.2F; break;

			case 13: bad.maximum[i] = -0.2F; break;
			}

			check(!validOutputConfiguration(bad), "invalid calibration/range rejected");
			OutputCandidate untouched{};
			untouched.pwm_us[0] = 123.0F;
			check(!mapStabilizedOutput(bad, {}, untouched) && near(untouched.pwm_us[0], 123.0F),
			      "invalid profile does not partially map");
		}
	}

	for (unsigned mutation = 0; mutation < 6; ++mutation) {
		auto bad = config;

		switch (mutation) {
		case 0: bad.lease_us = 0; break;

		case 1: bad.lease_us = static_cast<TimeUs>(UINT32_MAX) + 1; break;

		case 2: bad.acknowledgement_us = 0; break;

		case 3: bad.acknowledgement_us = bad.lease_us; break;

		case 4: bad.on_fault = OutputFallback::Unspecified; break;

		case 5: bad.on_fault = static_cast<OutputFallback>(255); break;
		}

		check(!validOutputConfiguration(bad), "incomplete policy rejected");
	}

	// Sweep independently recomputed asymmetric pulse and inverse driver mapping.
	for (int step = -100; step <= 100; ++step) {
		const float value = static_cast<float>(step) / 100.0F;
		OutputCandidate candidate{};
		check(mapStabilizedOutput(config, {value, value, value, value}, candidate), "calibrated sweep maps");

		for (unsigned i = 0; i < 4; ++i) {
			const float negative = i % 2 == 0 ? 1100.0F : 2100.0F;
			const float positive = i % 2 == 0 ? 2100.0F : 1100.0F;
			const float expected = value < 0 ? (-value * negative + (1.0F + value) * 1450.0F)
					       : (value * positive + (1.0F - value) * 1450.0F);
			check(near(candidate.pwm_us[i], expected), "piecewise calibration including reversal");
			check(near(1500.0F + candidate.driver_control[i] * 1000.0F, expected), "driver mapping recovers pulse");
		}
	}

	for (unsigned i = 0; i < 4; ++i) {
		for (float invalid : {NAN, INFINITY, -INFINITY, 1.001F, -1.001F}) {
			OutputCandidate preserved{};
			preserved.pwm_us[0] = 42.0F;
			CanardVector command{};
			command[i] = invalid;
			check(!mapStabilizedOutput(config, command, preserved) && near(preserved.pwm_us[0], 42.0F),
			      "bad command preserves all caller channels");
		}
	}
}

void lifecycleTests()
{
	StabilizedOutputConsumer boot;
	OutputAcknowledgement ack{};
	check(boot.sample(kStart, true).disposition == OutputDisposition::Disarmed, "boot is disarmed");
	check(boot.accept(kStart, true, {kStart, kStart, 1, {}}, ack) == OutputStatus::InvalidState,
	      "traffic cannot establish authority");
	Pair pair;
	check(pair.consumer.sample(pair.now, true).disposition == OutputDisposition::Disarmed, "grant alone is disarmed");
	pair.cycle();
	check(pair.producer.state() == OutputState::Active, "matching ack advances producer");
	check(pair.consumer.sample(pair.now, true).disposition == OutputDisposition::Command, "valid command candidate available");
	const TimeUs timestamp = pair.frame.timestamp_us;

	for (TimeUs elapsed = 1; elapsed < pair.config.lease_us; elapsed += 1000) {
		check(pair.consumer.sample(timestamp + elapsed, true).disposition == OutputDisposition::Command, "consumer lease before boundary");
	}

	auto expired = pair.consumer.sample(timestamp + pair.config.lease_us, true);
	check(expired.disposition == OutputDisposition::Fallback && near(expired.pwm_us[0], 1515.0F)
	      && near(expired.pwm_us[1], 1415.0F), "expiry uses explicit calibrated fallback, not allocator trim");
	pair.frame.timestamp_us = timestamp + pair.config.lease_us;
	++pair.frame.sequence;
	check(pair.consumer.accept(pair.frame.timestamp_us, true, pair.frame, ack) == OutputStatus::Expired,
	      "late producer cannot renew expired consumer");
	check(pair.consumer.begin(pair.frame.timestamp_us, pair.frame.timestamp_us, pair.config, true) == OutputStatus::InvalidState,
	      "new grant requires explicit disable");
	pair.producer.disable();
	pair.consumer.disable();
	check(pair.consumer.sample(pair.frame.timestamp_us, true).disposition == OutputDisposition::Disarmed, "disable clears candidate");
	check(pair.consumer.begin(pair.frame.timestamp_us, kStart, pair.config, true) == OutputStatus::InvalidTime, "session reuse rejected");
	pair.consumer.disable();
	const TimeUs restart = pair.frame.timestamp_us + kTick;
	check(pair.consumer.begin(restart, restart, pair.config, true) == OutputStatus::Ok, "deliberate newer session admitted");
	check(pair.consumer.sample(restart, true).disposition == OutputDisposition::Disarmed, "new session awaits fresh command");
	check(pair.consumer.accept(restart, true, pair.frame, ack) == OutputStatus::InvalidCommand, "preceding session traffic rejected");

	for (unsigned side = 0; side < 2; ++side) {
		Pair silence;
		const TimeUs expiry = silence.now + silence.config.lease_us;

		if (side == 0) {
			check(silence.producer.poll(expiry, true) == OutputStatus::Expired, "producer initial silence bounded");

		} else {
			check(silence.consumer.sample(expiry, true).disposition == OutputDisposition::Disarmed
			      && silence.consumer.fault() == OutputStatus::Expired, "consumer initial silence bounded without fallback output");
		}
	}

	Pair no_ack;
	no_ack.send();
	no_ack.accept();
	check(no_ack.producer.poll(no_ack.now + no_ack.config.acknowledgement_us - 1, true) == OutputStatus::Waiting,
	      "ack pending before deadline");
	check(no_ack.producer.acknowledge(no_ack.now + no_ack.config.acknowledgement_us, true, no_ack.ack) == OutputStatus::Expired,
	      "ack timeout equality latches even when ack arrives");
	check(no_ack.consumer.sample(no_ack.now + no_ack.config.lease_us, true).disposition == OutputDisposition::Fallback,
	      "lost ack eventually expires independent consumer");

	Pair no_poll;
	no_poll.cycle();
	no_poll.frame.timestamp_us += no_poll.config.lease_us;
	++no_poll.frame.sequence;
	check(no_poll.consumer.accept(no_poll.frame.timestamp_us, true, no_poll.frame, ack) == OutputStatus::Expired,
	      "accept checks prior lease even without intermediate sample");

	Pair not_due;
	not_due.cycle();
	OutputFrame sentinel{};
	sentinel.sequence = 42;
	check(not_due.producer.submit(not_due.now + 1, true, Status::NotDue, not_due.report, sentinel) == OutputStatus::NoCommand
	      && sentinel.sequence == 42, "NotDue leaves caller frame unchanged");
	check(not_due.producer.poll(not_due.now + not_due.config.lease_us, true) == OutputStatus::Expired, "NotDue does not renew producer lease");

	Pair waiting;
	waiting.send();
	Report later = waiting.report;
	++later.timestamp_us;
	check(waiting.producer.submit(waiting.now + 1, true, Status::Ok, later, sentinel) == OutputStatus::Waiting
	      && sentinel.sequence == 42, "one outstanding frame, no overwrite");
	check(waiting.producer.submit(waiting.now + 2, true, Status::StateChanged, later, sentinel) == OutputStatus::InvalidCommand,
	      "controller fault overrides pending ack");
}

void corruptionTests()
{
	for (unsigned mutation = 0; mutation < 10; ++mutation) {
		Pair pair;
		pair.send();

		switch (mutation) {
		case 0: --pair.frame.session; break;

		case 1: ++pair.frame.session; break;

		case 2: pair.frame.sequence = 0; break;

		case 3: ++pair.frame.sequence; break;

		case 4: pair.frame.timestamp_us = kStart; break;

		case 5: ++pair.frame.timestamp_us; break;

		default: pair.frame.command[mutation - 6] = NAN; break;
		}

		pair.ack.sequence = 99;
		check(pair.consumer.accept(pair.now, true, pair.frame, pair.ack) == OutputStatus::InvalidCommand
		      && pair.ack.sequence == 99, "corrupt frame faults without acknowledgement");
		check(pair.consumer.sample(pair.now, true).disposition == OutputDisposition::Disarmed, "first corrupt frame cannot produce output");
	}

	for (unsigned mutation = 0; mutation < 6; ++mutation) {
		Pair pair;
		pair.send();
		pair.accept();

		switch (mutation) {
		case 0: --pair.ack.session; break;

		case 1: ++pair.ack.sequence; break;

		case 2: --pair.ack.command_timestamp_us; break;

		case 3: --pair.ack.accepted_us; break;

		case 4: ++pair.ack.accepted_us; break;

		case 5: ++pair.ack.session; break;
		}

		check(pair.producer.acknowledge(pair.now, true, pair.ack) == OutputStatus::InvalidAcknowledgement, "corrupt ack revokes producer");
	}

	Pair duplicate;
	duplicate.cycle();
	check(duplicate.consumer.accept(duplicate.now, true, duplicate.frame, duplicate.ack) == OutputStatus::InvalidCommand,
	      "duplicate frame does not renew");
	check(duplicate.producer.acknowledge(duplicate.now, true, duplicate.ack) == OutputStatus::InvalidAcknowledgement,
	      "duplicate ack does not renew");

	for (unsigned mutation = 0; mutation < 5; ++mutation) {
		Pair pair;
		pair.now += kTick;
		pair.report.timestamp_us = pair.now;

		switch (mutation) {
		case 0: pair.report.timestamp_us = kStart; break;

		case 1: ++pair.report.timestamp_us; break;

		case 2: pair.report.state = State::Fault; break;

		case 3: pair.report.allocation.command[3] = INFINITY; break;

		case 4: pair.now += pair.config.acknowledgement_us; break;
		}

		pair.frame.sequence = 42;
		check(pair.producer.submit(pair.now, true, Status::Ok, pair.report, pair.frame) == OutputStatus::InvalidCommand
		      && pair.frame.sequence == 42, "invalid calculation cannot generate a frame");
	}
}

void permissionAndClockTests()
{
	for (unsigned mutation = 0; mutation < 4; ++mutation) {
		Pair pair;
		pair.cycle();
		const bool permitted = mutation >= 2;
		const TimeUs now = permitted ? pair.now - 1 : pair.now;
		const OutputStatus expected = permitted ? OutputStatus::InvalidTime : OutputStatus::PermissionLost;

		if (mutation % 2 == 0) {
			check(pair.producer.poll(now, permitted) == expected, "producer permission/clock loss");
			check(pair.producer.poll(pair.now + 1, true) == expected, "producer loss remains latched");

		} else {
			const auto expected_disposition = permitted ? OutputDisposition::Fallback : OutputDisposition::Disarmed;
			check(pair.consumer.sample(now, permitted).disposition == expected_disposition
			      && pair.consumer.fault() == expected, "consumer independently handles permission/clock loss");
			check(pair.consumer.sample(pair.now + 1, true).disposition == expected_disposition,
			      "restored permission cannot recover consumer");
		}
	}

	Pair fallback_loss;
	fallback_loss.cycle();
	const TimeUs expired = fallback_loss.now + fallback_loss.config.lease_us;
	check(fallback_loss.consumer.sample(expired, true).disposition == OutputDisposition::Fallback, "expiry first requests fallback");
	check(fallback_loss.consumer.sample(expired + 1, false).disposition == OutputDisposition::Disarmed,
	      "permission loss overrides prior fallback");
	check(fallback_loss.consumer.sample(expired + 2, true).disposition == OutputDisposition::Disarmed,
	      "fallback inhibition latches after permission restoration");

	auto config = fixtureConfiguration();
	config.on_fault = OutputFallback::Disarmed;
	StabilizedOutputConsumer disarm;
	OutputAcknowledgement ack{};
	check(disarm.begin(kStart, kStart, config, true) == OutputStatus::Ok, "explicit disarmed fallback config");
	check(disarm.accept(kStart + 1, true, {kStart, kStart + 1, 1, {}}, ack) == OutputStatus::Ok, "disarmed fallback session command");
	check(disarm.sample(kStart + 2, false).disposition == OutputDisposition::Disarmed, "disarmed policy honored after activation");

	for (unsigned mutation = 0; mutation < 4; ++mutation) {
		StabilizedOutputProducer producer;
		StabilizedOutputConsumer consumer;
		const TimeUs now = mutation == 0 ? 0 : kStart;
		const TimeUs session = mutation == 1 ? 0 : mutation == 2 ? now + 1 : now;
		const bool permitted = mutation != 3;
		check(producer.begin(now, session, config, permitted) != OutputStatus::Ok, "invalid producer grant");
		check(consumer.begin(now, session, config, permitted) != OutputStatus::Ok, "invalid consumer grant");
		check(consumer.sample(kStart, true).disposition == OutputDisposition::Disarmed, "invalid grant never actuates");
	}

	// Subtraction-based deadlines remain valid near uint64 max; no deadline sums.
	Pair near_wrap;
	near_wrap.producer.disable();
	near_wrap.consumer.disable();
	const TimeUs high = std::numeric_limits<TimeUs>::max() - 30000;
	check(near_wrap.producer.begin(high, high, near_wrap.config, true) == OutputStatus::Ok, "large time producer grant");
	check(near_wrap.consumer.begin(high, high, near_wrap.config, true) == OutputStatus::Ok, "large time consumer grant");
	near_wrap.now = high;
	near_wrap.cycle();
	check(near_wrap.consumer.sample(near_wrap.now + near_wrap.config.lease_us, true).disposition == OutputDisposition::Fallback,
	      "large clock expiry does not wrap");
}

void controllerIntegration()
{
	// Use the independently exercised observer profile parser and actual control
	// core; synthetic truth feedback only, with no runtime or output publication.
	const char *text =
		"SLAAPS_STABILIZED_OBSERVER_V1 2 255 190 "
		"100000 100000 50000 4000 60000 1100000 600000 "
		"0.01 0.04 0.06 "
		"1 -1 1  1 1 1  20 20 20  20 20 20  100 100 100  4 4 4 "
		"0.4 0.8 0.8  0.05 0.1 0.1  0.003 0.006 0.006  0.2 0.2 0.2  1 1 1 "
		"0.25 -0.25 0.25 -0.25  0.25 0.25 -0.25 -0.25  0.25 -0.25 -0.25 0.25 "
		"0 0 0 0  -1 -1 -1 -1  1 1 1 1  5 5 5 5  5 5 5 5 "
		"1 1 1  0.00001 0.01 0.01 0.001";
	FILE *stream = tmpfile();
	check(stream != nullptr, "integration profile stream");

	if (!stream) { return; }

	fputs(text, stream);
	rewind(stream);
	ObserverProfile profile{};
	const bool parsed = readObserverProfile(stream, profile);
	fclose(stream);
	check(parsed, "integration synthetic control profile parses");

	if (!parsed) { return; }

	StabilizedManualControl control;
	Pair pair;
	ManualSample manual{};
	manual.source = research_actuator_command::ManualControlSource::Mavlink;
	manual.mavlink_instance = 2;
	manual.source_system_id = 255;
	manual.source_component_id = 190;
	manual.valid = true;
	StateSample state{};
	state.valid = true;
	state.source_epoch = 1;
	auto stamp = [&]() {
		manual.timestamp_us = manual.timestamp_sample_us = pair.now;
		state.attitude_timestamp_us = state.attitude_sample_us = pair.now;
		state.rate_timestamp_us = state.rate_sample_us = pair.now;
	};
	stamp();
	check(control.configure(profile.control) == Status::Ok && control.enable(pair.now, manual, state) == Status::Ok,
	      "actual controller starts centered");

	for (unsigned tick = 0; tick < 50; ++tick) {
		pair.now += kTick;
		stamp();
		manual.axes[0] = tick < 25 ? 0.4F : 0.0F;
		check(pair.producer.poll(pair.now, true) == OutputStatus::Ok, "previous ack precedes next control calculation");
		const Status result = control.cycle(pair.now, manual, state, pair.report);
		check(result == Status::Ok, "actual control calculation succeeds");
		check(pair.report.state == (tick < 25 ? State::Rate : State::Hold), "rate then attitude hold reaches output boundary");
		check(pair.producer.submit(pair.now, true, result, pair.report, pair.frame) == OutputStatus::Ok, "actual controller report accepted");
		pair.accept();
		pair.acknowledge();
		const auto candidate = pair.consumer.sample(pair.now, true);
		check(candidate.disposition == OutputDisposition::Command, "controller-to-consumer command disposition");

		for (unsigned i = 0; i < 4; ++i) {
			const auto &servo = pair.config.servos[i];
			const float offset = candidate.pwm_us[i] - servo.zero_pwm_us;
			const float inferred = offset / (pair.frame.command[i] < 0 ? servo.zero_pwm_us - servo.negative_pwm_us
							 : servo.positive_pwm_us - servo.zero_pwm_us);
			check(near(inferred, pair.report.allocation.command[i]), "mapping recovers actual allocator displacement");
		}
	}

	pair.now += kTick;
	stamp();
	manual.valid = false;
	const Status fault = control.cycle(pair.now, manual, state, pair.report);
	check(fault == Status::InputRejected, "actual manual loss faults control");
	check(pair.producer.submit(pair.now, true, fault, pair.report, pair.frame) == OutputStatus::InvalidCommand,
	      "stale successful report cannot survive failed calculation");
	check(pair.consumer.sample(pair.report.timestamp_us + pair.config.lease_us, true).disposition == OutputDisposition::Fallback,
	      "producer failure independently expires final command");
}

void driverTests()
{
	struct Sink : StabilizedOutputSink {
		TimeUs completed{kStart};
		bool handled{true};
		OutputCandidate last{};
		OutputWriteResult write(const OutputCandidate &candidate) override
		{
			last = candidate;
			return {completed, handled};
		}
	};
	const auto config = fixtureConfiguration();
	StabilizedOutputDriver driver;
	Sink sink;
	check(driver.begin(kStart, kStart, config, true) == OutputStatus::InvalidState, "grant requires confirmed initial disarm");
	check(driver.dispatch(kStart, true, nullptr, sink).revoke_acknowledged, "sink-confirmed initial disarm");
	check(driver.begin(kStart, kStart, config, true) == OutputStatus::Ok, "grant after confirmed initial disarm");
	check(driver.begin(kStart + 1, kStart + 1, config, true) == OutputStatus::InvalidState, "competing grant cannot replace session");
	OutputFrame frame{kStart, kStart + kTick, 1, {0.5F, -0.5F, 0.5F, -0.5F}};
	sink.completed = frame.timestamp_us + 100;
	const auto applied = driver.dispatch(frame.timestamp_us, true, &frame, sink);
	check(applied.command_acknowledged && applied.acknowledgement.accepted_us == sink.completed, "ack uses driver completion time");
	check(applied.handled && sink.last.disposition == OutputDisposition::Command && near(sink.last.pwm_us[0], 1775.0F),
	      "sink sees complete calibrated candidate");
	driver.revoke();
	check(!driver.disarmedConfirmed() && driver.begin(sink.completed, sink.completed, config, true) == OutputStatus::InvalidState,
	      "revoke request alone cannot release ownership");
	sink.handled = false;
	check(!driver.dispatch(sink.completed, true, nullptr, sink).revoke_acknowledged
	      && !driver.disarmedConfirmed(), "rejected disarm prevents handoff");
	sink.handled = true;
	check(driver.dispatch(sink.completed, true, nullptr, sink).revoke_acknowledged
	      && driver.disarmedConfirmed(), "successful disarm permits deliberate handoff");
	check(driver.begin(sink.completed, sink.completed, config, true) == OutputStatus::Ok, "new session after driver-confirmed revoke");
	check(!driver.dispatch(sink.completed, true, &frame, sink).command_acknowledged, "old-session traffic cannot acquire new output grant");

	for (unsigned failure = 0; failure < 5; ++failure) {
		StabilizedOutputDriver faulty;
		Sink test;
		check(faulty.dispatch(kStart, true, nullptr, test).revoke_acknowledged, "fault fixture confirmed disarm");
		check(faulty.begin(kStart, kStart, config, true) == OutputStatus::Ok, "fault fixture grant");
		test.completed = frame.timestamp_us;

		switch (failure) {
		case 0: test.handled = false; break;

		case 1: test.completed = frame.timestamp_us - 1; break;

		case 2: test.completed = frame.timestamp_us + config.acknowledgement_us; break;

		case 3: test.completed = frame.timestamp_us + config.lease_us; break;

		case 4: break; // Current permission loss.
		}

		const auto rejected = faulty.dispatch(frame.timestamp_us, failure != 4, &frame, test);
		check(!rejected.command_acknowledged && rejected.acknowledgement.session == 0
		      && faulty.state() == DriverState::Fault, "sink rejection timing or permission failure suppresses ack");
		test.handled = true;
		test.completed = frame.timestamp_us + config.lease_us + 1;
		check(faulty.dispatch(test.completed, true, nullptr, test).candidate.disposition == OutputDisposition::Disarmed,
		      "failed driver path independently dispatches disarmed output");
		check(faulty.begin(test.completed, test.completed, config, true) == OutputStatus::InvalidState,
		      "driver recovery cannot automatically grant");
		faulty.revoke();
		check(faulty.dispatch(test.completed, true, nullptr, test).revoke_acknowledged, "explicit recovery reaches sink");
		check(faulty.begin(test.completed, test.completed, config, true) == OutputStatus::Ok, "explicit new session after fault");
	}

	StabilizedOutputDriver expired;
	Sink test;
	check(expired.dispatch(kStart, true, nullptr, test).revoke_acknowledged, "expiry fixture disarmed");
	check(expired.begin(kStart, kStart, config, true) == OutputStatus::Ok, "expiry fixture grant");
	test.completed = frame.timestamp_us;
	check(expired.dispatch(test.completed, true, &frame, test).command_acknowledged, "expiry fixture applies command");
	test.completed += config.lease_us;
	auto result = expired.dispatch(test.completed, true, nullptr, test);
	check(result.handled && !result.command_acknowledged && result.candidate.disposition == OutputDisposition::Fallback,
	      "producer silence independently dispatches configured fallback");
	++test.completed;
	check(expired.dispatch(test.completed, false, nullptr, test).candidate.disposition == OutputDisposition::Disarmed,
	      "permission loss supersedes already applied fallback");
}

void splitDriverTests()
{
	const auto config = fixtureConfiguration();
	StabilizedOutputDriver driver;
	DriverWrite initial{}, competing{};
	check(!driver.complete(1, {kStart, true}, true).revoke_acknowledged, "orphan completion cannot confirm disarm");
	check(driver.prepare(kStart, true, nullptr, initial) && driver.pending(initial.ticket),
	      "initial disarm prepared with nonzero ticket");
	check(!driver.disarmedConfirmed(), "preparation alone never confirms physical-driver handling");
	check(!driver.prepare(kStart, true, nullptr, competing) && !competing.ticket && driver.pending(initial.ticket),
	      "overlapping preparation leaves first pending write intact");
	check(!driver.complete(initial.ticket + 1, {kStart, true}, true).handled && driver.pending(initial.ticket),
	      "wrong completion cannot consume pending write");
	check(driver.complete(initial.ticket, {kStart, true}, false).revoke_acknowledged,
	      "disarm can complete while permission is absent");
	check(!driver.complete(initial.ticket, {kStart, true}, true).revoke_acknowledged,
	      "duplicate disarm completion cannot acknowledge twice");
	check(driver.begin(kStart, kStart, config, true) == OutputStatus::Ok, "split path grant");
	OutputFrame frame{kStart, kStart + kTick, 1, {0.5F, -0.5F, 0.5F, -0.5F}};
	DriverWrite command{};
	check(driver.prepare(frame.timestamp_us, true, &frame, command) && command.ticket > initial.ticket
	      && command.candidate.disposition == OutputDisposition::Command, "prepare exact mapped command");
	check(!driver.complete(initial.ticket, {frame.timestamp_us, true}, true).command_acknowledged
	      && driver.pending(command.ticket), "old callback cannot acknowledge current command");
	auto result = driver.complete(command.ticket, {frame.timestamp_us + 10, true}, true);
	check(result.command_acknowledged && result.acknowledgement.sequence == 1
	      && result.acknowledgement.accepted_us == frame.timestamp_us + 10, "matching completion acknowledges once");
	check(!driver.complete(command.ticket, {frame.timestamp_us + 10, true}, true).command_acknowledged,
	      "duplicate command completion ignored");

	for (unsigned fault = 0; fault < 4; ++fault) {
		StabilizedOutputDriver item;
		DriverWrite write{};
		check(item.prepare(kStart, true, nullptr, write)
		      && item.complete(write.ticket, {kStart, true}, true).revoke_acknowledged, "phase fault initial disarm");
		check(item.begin(kStart, kStart, config, true) == OutputStatus::Ok, "phase fault grant");
		check(item.prepare(frame.timestamp_us, true, &frame, write), "phase fault command prepared");
		const uint64_t old_ticket = write.ticket;

		if (fault == 0) {
			item.revoke();
			check(!item.pending(old_ticket), "revoke invalidates a not-yet-started write");
			result = item.complete(old_ticket, {frame.timestamp_us + 10, true}, true);
			check(result.handled && !result.command_acknowledged && !result.revoke_acknowledged,
			      "in-flight command completion cannot masquerade as revoke acknowledgment");

		} else if (fault == 1) {
			check(!item.cancel(old_ticket + 1) && item.pending(old_ticket), "wrong cancellation cannot abandon current write");
			check(item.cancel(old_ticket) && !item.pending(old_ticket), "matching abandoned write canceled");
			check(!item.complete(old_ticket, {frame.timestamp_us + 10, true}, true).command_acknowledged,
			      "abandoned completion cannot acknowledge");

		} else if (fault == 2) {
			result = item.complete(old_ticket, {frame.timestamp_us + 10, true}, false);
			check(result.handled && !result.command_acknowledged && item.state() == DriverState::Fault,
			      "permission lost during driver handling suppresses acknowledgment");

		} else {
			result = item.complete(old_ticket, {frame.timestamp_us + config.acknowledgement_us, true}, true);
			check(result.handled && !result.command_acknowledged && item.state() == DriverState::Fault,
			      "completion-time deadline remains enforced");
		}

		const TimeUs recovered = frame.timestamp_us + config.lease_us;
		check(item.begin(recovered, recovered, config, true) == OutputStatus::InvalidState,
		      "completion fault cannot release ownership");
		item.revoke();
		check(item.prepare(recovered, true, nullptr, write) && write.ticket > old_ticket
		      && write.candidate.disposition == OutputDisposition::Disarmed, "new disarm follows invalidated command");
		check(!item.complete(old_ticket, {recovered, true}, true).revoke_acknowledged && item.pending(write.ticket),
		      "delayed command completion cannot complete newer disarm");
		check(item.begin(recovered, recovered, config, true) == OutputStatus::InvalidState,
		      "pending disarm blocks new grant");
		check(item.complete(write.ticket, {recovered, true}, true).revoke_acknowledged,
		      "matching new disarm completion permits handoff");
		check(item.begin(recovered, recovered, config, true) == OutputStatus::Ok, "fresh grant after split completion");
	}

	// A synchronous sink can trigger a lifecycle decision before returning.
	// Its command write must never be mistaken for the newly requested disarm.
	struct RevokingSink : StabilizedOutputSink {
		StabilizedOutputDriver owner{};
		TimeUs now{kStart};
		OutputWriteResult write(const OutputCandidate &candidate) override
		{
			if (candidate.disposition == OutputDisposition::Command) { owner.revoke(); }

			return {now, true};
		}
	} sink;
	check(sink.owner.dispatch(kStart, true, nullptr, sink).revoke_acknowledged, "reentrant sink initial disarm");
	check(sink.owner.begin(kStart, kStart, config, true) == OutputStatus::Ok, "reentrant sink grant");
	sink.now = frame.timestamp_us;
	result = sink.owner.dispatch(sink.now, true, &frame, sink);
	check(result.handled && !result.command_acknowledged && !result.revoke_acknowledged
	      && !sink.owner.disarmedConfirmed(), "synchronous command cannot acknowledge a revoke requested during its write");
	check(sink.owner.dispatch(++sink.now, true, nullptr, sink).revoke_acknowledged,
	      "next synchronous disarm confirms actual revoke");
}
}

int main()
{
	mappingTests();
	lifecycleTests();
	corruptionTests();
	permissionAndClockTests();
	controllerIntegration();
	driverTests();
	splitDriverTests();
	printf("Stabilized output: %u checks, %u failures\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
