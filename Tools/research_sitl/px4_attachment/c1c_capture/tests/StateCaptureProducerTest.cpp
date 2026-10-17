#include "../producer/StateCaptureProducer.hpp"

#include <cstdio>
#include <initializer_list>
#include <limits>
#include <type_traits>

namespace research_sitl
{
namespace state_capture
{
struct StateCaptureProducerTestPeer {
	static void counters(Producer &producer, uint64_t sequence, uint64_t epoch)
	{
		producer.summary_.attempted_sequence = sequence;
		producer.summary_.transition_epoch = epoch;
	}
};
}
}

namespace
{
using namespace research_sitl::state_capture;
constexpr State Disarmed{1, 0};
constexpr State Armed{2, 0};
constexpr State Altitude{1, 1};
constexpr State Termination{1, 13};
constexpr Identities Identity{11, 13, 17};
constexpr uint64_t Maximum = std::numeric_limits<uint64_t>::max();
unsigned checks = 0;
unsigned failures = 0;

void check(bool condition, const char *message)
{
	++checks;

	if (!condition) { ++failures; std::fprintf(stderr, "FAIL: %s\n", message); }
}

bool equal(State a, State b)
{
	return a.arming_state == b.arming_state && a.nav_state == b.nav_state;
}

void receipt(const std::optional<Receipt> &value, Kind kind, uint64_t sequence,
	     uint64_t epoch, uint64_t stamp, State before, State after)
{
	check(value.has_value(), "receipt exists");

	if (!value) { return; }

	check(value->producer_lifetime_id == 11 && value->clock_epoch == 17, "receipt source identity");
	check(value->kind == kind, "exact receipt kind");
	check(value->sequence == sequence && value->transition_epoch == epoch, "exact counters");
	check(value->producer_time_us == stamp, "exact source timestamp");
	check(equal(value->before, before) && equal(value->after, after), "exact before/after pair");
}

struct Fixture {
	Bootstrap bootstrap;
	Producer producer{bootstrap};
	Fixture()
	{
		check(bootstrap.configure(Identity), "configure synthetic lifetime");
		receipt(producer.initial(Disarmed, 100), Kind::Initial, 1, 1, 100, Disarmed, Disarmed);
		producer.completePublication(true);
	}
};

void sticky(Producer &producer, Fault fault, State actual, uint64_t finish_time)
{
	const Summary prior = producer.snapshot();
	check(prior.first_fault == fault, "exact first fault");
	check(!producer.sample(Disarmed, finish_time), "faulted sample produces no receipt");
	check(!producer.transition(Disarmed, Armed, finish_time), "faulted transition produces no receipt");
	check(!producer.initial(Disarmed, finish_time), "faulted producer cannot reinitialize");
	const Summary done = producer.finish(actual, finish_time);
	check(done.finished && done.first_fault == fault, "finish preserves sticky first fault");
	check(done.attempted_sequence == prior.attempted_sequence && done.emitted_sequence == prior.emitted_sequence &&
	      done.transition_epoch == prior.transition_epoch, "failed summary retains exact attempt/delivery counters");
	check(equal(done.retained_state, prior.retained_state) && equal(done.emitted_state, prior.emitted_state),
	      "failed summary distinguishes attempted and delivered state");
	check(equal(done.actual_state, actual) && done.final_time_us == finish_time, "failed summary tells actual final state/time");
	producer.completePublication(false);
	check(!producer.sample(Armed, 0), "no emission after finish");
	const Summary repeated = producer.finish(Armed, 0);
	check(repeated.first_fault == done.first_fault && repeated.final_time_us == done.final_time_us &&
	      equal(repeated.actual_state, done.actual_state), "finished summary immutable");
}

void bootstrapCases()
{
	{
		Bootstrap bootstrap;
		Producer producer(bootstrap);
		check(!producer.initial(Disarmed, 100), "missing bootstrap rejected");
		sticky(producer, Fault::BootstrapUnavailable, Disarmed, 110);
		check(!bootstrap.configure(Identity), "late bootstrap cannot repair failed claim");
	}

	for (unsigned field = 0; field < 3; ++field) {
		Bootstrap bootstrap;
		Identities invalid = Identity;

		if (field == 0) { invalid.producer_lifetime_id = 0; }

		if (field == 1) { invalid.observer_lifetime_id = 0; }

		if (field == 2) { invalid.clock_epoch = 0; }

		check(!bootstrap.configure(invalid), "zero lifetime rejected");
		check(!bootstrap.configure(Identity), "invalid bootstrap cannot reset");
		Identities output{101, 103, 107};
		check(!bootstrap.claim(output) && output.producer_lifetime_id == 101 &&
		      output.observer_lifetime_id == 103 && output.clock_epoch == 107, "failed claim leaves output untouched");
	}

	{
		Bootstrap bootstrap;
		check(bootstrap.configure(Identity), "first reservation");
		check(!bootstrap.configure(Identity), "repeated reservation rejected");
		Producer producer(bootstrap);
		check(!producer.initial(Disarmed, 100), "repeated reservation permanently invalid");
	}

	{
		Fixture fixture;
		Producer second(fixture.bootstrap);
		check(!second.initial(Disarmed, 110), "second owner claim rejected");
		check(!fixture.producer.sample(Disarmed, 110), "first owner sees lost bootstrap");
		sticky(fixture.producer, Fault::BootstrapUnavailable, Disarmed, 120);
	}

	{
		Fixture fixture;
		check(!fixture.producer.initial(Disarmed, 110), "repeated Initial rejected");
		sticky(fixture.producer, Fault::AlreadyStarted, Disarmed, 120);
	}
}

void syntheticSourceSeams()
{
	Fixture fixture;
	auto &producer = fixture.producer;
	check(!producer.transition(Disarmed, Disarmed, 101), "unchanged assignment emits nothing");
	check(producer.snapshot().attempted_sequence == 1 && producer.snapshot().first_fault == Fault::None,
	      "unchanged assignment consumes no counter");
	// These are synthetic helper inputs, never live arming or mode commands.
	receipt(producer.transition(Disarmed, Armed, 102), Kind::Transition, 2, 2, 102, Disarmed, Armed);
	producer.completePublication(true);
	receipt(producer.transition(Armed, Disarmed, 102), Kind::Transition, 3, 3, 102, Armed, Disarmed);
	producer.completePublication(true);
	receipt(producer.transition(Disarmed, Altitude, 103), Kind::Transition, 4, 4, 103, Disarmed, Altitude);
	producer.completePublication(true);
	receipt(producer.transition(Altitude, Termination, 103), Kind::Transition, 5, 5, 103, Altitude, Termination);
	producer.completePublication(true);
	receipt(producer.sample(Termination, 104), Kind::Sample, 6, 5, 104, Termination, Termination);
	producer.completePublication(true);
	const Summary done = producer.finish(Termination, 105);
	check(done.finished && done.has_state && done.first_fault == Fault::None, "healthy final summary");
	check(done.identities.producer_lifetime_id == 11 && done.identities.observer_lifetime_id == 13 &&
	      done.identities.clock_epoch == 17, "all summary lifetimes");
	check(done.attempted_sequence == 6 && done.emitted_sequence == 6 && done.transition_epoch == 5 &&
	      done.emitted_transition_epoch == 5, "summary reconciles every intermediate assignment");
	check(done.producer_time_us == 104 && done.emitted_producer_time_us == 104 && done.final_time_us == 105,
	      "summary retains source and stop timestamps");
	check(equal(done.retained_state, Termination) && equal(done.actual_state, Termination) &&
	      equal(done.emitted_state, Termination), "summary reconciles actual/emitted state");
	check(!producer.initial(Armed, 106) && !producer.sample(Termination, 106), "finished producer does not restart");
}

void faultCases()
{
	{
		Fixture fixture;
		check(!fixture.producer.sample(Altitude, 101), "Sample cannot hide omitted transition");
		sticky(fixture.producer, Fault::StateContradiction, Altitude, 102);
	}
	{
		Fixture fixture;
		check(!fixture.producer.transition(Armed, Disarmed, 101), "transition before must equal retained state");
		sticky(fixture.producer, Fault::StateContradiction, Disarmed, 102);
	}

	for (uint64_t stamp : {uint64_t{0}, uint64_t{99}}) {
		Fixture fixture;
		check(!fixture.producer.sample(Disarmed, stamp), "invalid source clock rejected");
		sticky(fixture.producer, stamp == 0 ? Fault::TimeInvalid : Fault::TimeRegressed, Disarmed, 110);
	}
	{
		Fixture fixture;
		check(!fixture.producer.transition(Disarmed, Disarmed, 105), "unchanged observation checks time");
		check(!fixture.producer.sample(Disarmed, 104), "time cannot regress behind unchanged assignment");
		sticky(fixture.producer, Fault::TimeRegressed, Disarmed, 110);
	}

	for (State invalid : {State{0, 0}, State{3, 0}, State{1, 9}, State{1, 11}, State{1, 16}, State{1, 31}, State{1, 255}}) {
		Bootstrap bootstrap;
		check(bootstrap.configure(Identity), "invalid-state fixture reservation");
		Producer producer(bootstrap);
		check(!producer.initial(invalid, 100), "invalid native state rejected");
		sticky(producer, Fault::InvalidState, invalid, 101);
	}
	{
		Fixture fixture;
		receipt(fixture.producer.transition(Disarmed, Altitude, 101), Kind::Transition, 2, 2, 101, Disarmed, Altitude);
		fixture.producer.completePublication(false);
		const Summary failed = fixture.producer.snapshot();
		check(failed.attempted_sequence == 2 && failed.emitted_sequence == 1 &&
		      failed.transition_epoch == 2 && failed.emitted_transition_epoch == 1, "failed publication does not fake delivery");
		check(equal(failed.retained_state, Altitude) && equal(failed.emitted_state, Disarmed) &&
		      failed.producer_time_us == 101 && failed.emitted_producer_time_us == 100, "failed publication preserves both states/times");
		sticky(fixture.producer, Fault::PublishFailed, Altitude, 102);
	}
	{
		Fixture fixture;
		receipt(fixture.producer.sample(Disarmed, 101), Kind::Sample, 2, 1, 101, Disarmed, Disarmed);
		check(!fixture.producer.sample(Disarmed, 102), "unacknowledged publication cannot be overwritten");
		sticky(fixture.producer, Fault::PublicationPending, Disarmed, 103);
	}
	{
		Fixture fixture;
		fixture.producer.completePublication(true);
		sticky(fixture.producer, Fault::PublicationPending, Disarmed, 101);
	}
	{
		Fixture fixture;
		const Summary done = fixture.producer.finish(Altitude, 101);
		check(done.first_fault == Fault::StateContradiction && equal(done.actual_state, Altitude) &&
		      equal(done.emitted_state, Disarmed), "final summary exposes otherwise unobserved final state change");
	}
}

void counterCases()
{
	{
		Fixture fixture;
		StateCaptureProducerTestPeer::counters(fixture.producer, Maximum - 1, 1);
		receipt(fixture.producer.sample(Disarmed, 101), Kind::Sample, Maximum, 1, 101, Disarmed, Disarmed);
		fixture.producer.completePublication(true);
		check(!fixture.producer.sample(Disarmed, 102), "delivery exhaustion cannot wrap");
		sticky(fixture.producer, Fault::CounterExhausted, Disarmed, 103);
	}
	{
		Fixture fixture;
		StateCaptureProducerTestPeer::counters(fixture.producer, 1, Maximum - 1);
		receipt(fixture.producer.transition(Disarmed, Altitude, 101), Kind::Transition, 2, Maximum, 101, Disarmed, Altitude);
		fixture.producer.completePublication(true);
		receipt(fixture.producer.sample(Altitude, 102), Kind::Sample, 3, Maximum, 102, Altitude, Altitude);
		fixture.producer.completePublication(true);
		check(!fixture.producer.transition(Altitude, Termination, 103), "transition exhaustion cannot wrap or consume sequence");
		check(fixture.producer.snapshot().attempted_sequence == 3, "counter reservation is atomic");
		sticky(fixture.producer, Fault::CounterExhausted, Termination, 104);
	}
	{
		Fixture fixture;
		receipt(fixture.producer.sample(Disarmed, Maximum), Kind::Sample, 2, 1, Maximum, Disarmed, Disarmed);
		fixture.producer.completePublication(true);
		check(fixture.producer.finish(Disarmed, Maximum).first_fault == Fault::None, "maximum equal timestamps need no addition");
	}
}
}

int main()
{
	static_assert(!std::is_copy_constructible<research_sitl::state_capture::Producer>::value, "single source owner");
	bootstrapCases();
	syntheticSourceSeams();
	faultCases();
	counterCases();
	std::printf("C1C synthetic capture helper: %u checks, %u failures; no live armed behavior\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
