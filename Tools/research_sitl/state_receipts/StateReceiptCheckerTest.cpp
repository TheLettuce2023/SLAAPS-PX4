#include "StateReceiptChecker.hpp"

#include <array>
#include <iostream>
#include <limits>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>

namespace research_sitl::state_receipts
{
struct StateReceiptCheckerTestPeer {
	// test_peer_injection: only counters are seeded, never state, time or fault.
	static void counters(ReceiptChecker &checker, uint64_t sequence, uint64_t epoch)
	{
		checker.snapshot_.receipt_sequence = sequence;
		checker.snapshot_.transition_epoch = epoch;
	}
};
} // namespace research_sitl::state_receipts

using namespace research_sitl::state_receipts;

namespace
{
constexpr uint64_t producer = 11;
constexpr uint64_t observer = 13;
constexpr uint64_t clock_epoch = 17;
constexpr uint64_t maximum = std::numeric_limits<uint64_t>::max();
constexpr State disarmed{1, 0};
constexpr State armed{2, 0};
constexpr State offboard{1, 14}; // Raw pinned PX4 nav value, never research Selected.
constexpr uint32_t allowed_nav = (uint32_t{1} << 0) | (uint32_t{1} << 14);
size_t checks = 0;
size_t failures = 0;
size_t variants = 0;

void check(bool value, const std::string &name)
{
	++checks;

	if (!value) { ++failures; std::cerr << "FAILED: " << name << '\n'; }
}

// All identities, timestamps and max_age are synthetic software fixture data.
// They are not a qualified clock, live-state freshness budget or flight result.
Config config() { return {producer, observer, clock_epoch, 10, allowed_nav}; }

Observation input(ReceiptKind kind = ReceiptKind::Initial, uint64_t sequence = 1, uint64_t transition = 1,
		  uint64_t source_time = 100, uint64_t receive_time = 105, State before = disarmed, State after = disarmed)
{
	return {observer, receive_time, true, {producer, clock_epoch, sequence, transition, source_time, kind, before, after}};
}

auto retained(const Snapshot &value)
{
	return std::make_tuple(value.has_state, value.receipt_sequence, value.transition_epoch,
			       value.state.arming_state, value.state.nav_state, value.producer_time_us, value.local_time_us);
}

auto complete(const Snapshot &value)
{
	return std::make_tuple(value.code, value.first_fault, retained(value), value.age_valid, value.age_us);
}

void accepted(const Snapshot &value, Code code, uint64_t sequence, uint64_t transition, State state,
	      uint64_t source_time, uint64_t local_time, uint64_t age, const std::string &name)
{
	check(value.code == code && value.first_fault == FaultReason::None && value.has_state, name + ": accepted state");
	check(value.receipt_sequence == sequence && value.transition_epoch == transition, name + ": exact counters");
	check(value.state.arming_state == state.arming_state && value.state.nav_state == state.nav_state,
	      name + ": exact raw state");
	check(value.producer_time_us == source_time && value.local_time_us == local_time
	      && value.age_valid && value.age_us == age, name + ": independently expected times and age");
}

void fault(ReceiptChecker &checker, const Snapshot &previous, const Snapshot &result, FaultReason reason,
	   const std::string &name)
{
	++variants;
	check(result.code == Code::Faulted && result.first_fault == reason, name + ": exact first fault");
	check(retained(result) == retained(previous), name + ": no partial state/counter/time commit");
	check(!result.age_valid && result.age_us == 0, name + ": fault cannot carry age readiness");
	const auto after_poll = checker.poll(0, 0, 0, false);
	const auto after_observe = checker.observe(Observation{});
	check(complete(after_poll) == complete(result) && complete(after_observe) == complete(result),
	      name + ": first fault and all retained fields sticky");
}

struct Fixture {
	ReceiptChecker checker{config()};
	Snapshot initial{checker.observe(input())};
	Fixture() { accepted(initial, Code::Fresh, 1, 1, disarmed, 100, 105, 5, "fixture initial"); }
};

template<typename T, typename = void>
struct HasReset : std::false_type {};
template<typename T>
struct HasReset<T, std::void_t<decltype(std::declval<T &>().reset())>> : std::true_type {};
template<typename T, typename = void>
struct HasSelection : std::false_type {};
template<typename T>
struct HasSelection<T, std::void_t<decltype(std::declval<T &>().selection)>> : std::true_type {};

void constructionAndAllowMask()
{
	static_assert(!std::is_default_constructible<ReceiptChecker>::value, "explicit config required");
	static_assert(!std::is_copy_constructible<ReceiptChecker>::value && !std::is_copy_assignable<ReceiptChecker>::value,
		      "producer provenance must not copy");
	static_assert(!std::is_move_constructible<ReceiptChecker>::value && !std::is_move_assignable<ReceiptChecker>::value,
		      "producer provenance must not move");
	static_assert(!HasReset<ReceiptChecker>::value, "no public continuity reset");
	static_assert(!HasSelection<Snapshot>::value, "receipt snapshots grant no diagnostic selection");

	for (unsigned variant = 0; variant < 13; ++variant) {
		auto settings = config();

		switch (variant) {
		case 0: settings.expected_producer_lifetime_id = 0; break;

		case 1: settings.expected_observer_lifetime_id = 0; break;

		case 2: settings.expected_clock_epoch = 0; break;

		case 3: settings.max_age_us = 0; break;

		case 4: settings.allowed_nav_mask = 0; break;

		default: {
				const std::array<unsigned, 4> forbidden{9, 11, 16, 31};
				settings.allowed_nav_mask = uint32_t{1} << forbidden[(variant - 5) % forbidden.size()];

				if (variant >= 9) { settings.allowed_nav_mask |= allowed_nav; }

				break;
			}
		}

		ReceiptChecker checker(settings);
		Snapshot empty{};
		fault(checker, empty, checker.observe(input()), FaultReason::InvalidConfig,
		      "invalid config " + std::to_string(variant));
	}

	// Enumerated pinned native nav values, not a reproduction of production mask arithmetic.
	const std::array<uint8_t, 28> native{0, 1, 2, 3, 4, 5, 6, 7, 8, 10, 12, 13, 14, 15,
					     17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30};

	for (const uint8_t nav : native) {
		auto settings = config();
		settings.allowed_nav_mask = uint32_t{1} << nav;
		ReceiptChecker checker(settings);
		const State state{1, nav};
		accepted(checker.observe(input(ReceiptKind::Initial, 1, 1, 100, 105, state, state)),
			 Code::Fresh, 1, 1, state, 100, 105, 5, "explicit native nav " + std::to_string(nav));
		++variants;
	}

	ReceiptChecker immutable(config());
	auto settings = config();
	ReceiptChecker copied_settings(settings);
	settings.max_age_us = 1;
	settings.expected_producer_lifetime_id = 0;
	accepted(copied_settings.observe(input()), Code::Fresh, 1, 1, disarmed, 100, 105, 5, "constructor retains immutable config copy");
	(void)immutable;
}

void chainAndFreshness()
{
	ReceiptChecker waiting(config());
	const auto first_poll = waiting.poll(observer, clock_epoch, 50, true);
	check(first_poll.code == Code::AwaitingInitial && first_poll.first_fault == FaultReason::None && !first_poll.has_state
	      && first_poll.receipt_sequence == 0 && first_poll.transition_epoch == 0 && first_poll.producer_time_us == 0
	      && first_poll.local_time_us == 50 && !first_poll.age_valid && first_poll.age_us == 0,
	      "poll before initial is not state readiness");
	const auto equal_poll = waiting.poll(observer, clock_epoch, 50, true);
	check(complete(equal_poll) == complete(first_poll), "equal pre-initial local time accepted");
	accepted(waiting.observe(input()), Code::Fresh, 1, 1, disarmed, 100, 105, 5, "initial after waiting polls");
	accepted(waiting.observe(input(ReceiptKind::Sample, 2, 1, 110, 115)), Code::Fresh, 2, 1, disarmed,
		 110, 115, 5, "sample consumes delivery but not transition");
	accepted(waiting.observe(input(ReceiptKind::Transition, 3, 2, 120, 125, disarmed, armed)), Code::Fresh,
		 3, 2, armed, 120, 125, 5, "actual intermediate Armed receipt");
	accepted(waiting.observe(input(ReceiptKind::Transition, 4, 3, 120, 125, armed, disarmed)), Code::Fresh,
		 4, 3, disarmed, 120, 125, 5, "Disarmed receipt preserves same-microsecond transition order");
	accepted(waiting.observe(input(ReceiptKind::Transition, 5, 4, 130, 135, disarmed, offboard)), Code::Fresh,
		 5, 4, offboard, 130, 135, 5, "raw-nav-only transition stays raw state");
	accepted(waiting.observe(input(ReceiptKind::Sample, 6, 4, 140, 145, offboard, offboard)), Code::Fresh,
		 6, 4, offboard, 140, 145, 5, "unchanged raw Offboard sample grants no separate authority");
	variants += 8;

	Fixture freshness;
	accepted(freshness.checker.poll(observer, clock_epoch, 110, true), Code::Fresh, 1, 1, disarmed, 100, 110, 10,
		 "poll exact freshness bound");
	accepted(freshness.checker.poll(observer, clock_epoch, 111, true), Code::Stale, 1, 1, disarmed, 100, 111, 11,
		 "poll one beyond freshness bound retains provenance");
	const auto fresh = freshness.checker.observe(input(ReceiptKind::Sample, 2, 1, 112, 112));
	accepted(fresh, Code::Fresh, 2, 1, disarmed, 112, 112, 0, "stale to fresh by next continuous sample");
	auto changed_copy = fresh;
	changed_copy.state = armed;
	changed_copy.receipt_sequence = 999;
	changed_copy.local_time_us = 999;
	accepted(freshness.checker.poll(observer, clock_epoch, 112, true), Code::Fresh, 2, 1, disarmed, 112, 112, 0,
		 "snapshot is a value copy, not mutable retained state");
	check(changed_copy.state.arming_state == 2, "mutated test copy remains independent");
	variants += 4;

	ReceiptChecker stale(config());
	accepted(stale.observe(input(ReceiptKind::Initial, 1, 1, 100, 111)), Code::Stale, 1, 1, disarmed, 100, 111, 11,
		 "stale initial still consumes continuity");
	const auto stale_sample = stale.observe(input(ReceiptKind::Sample, 2, 1, 100, 112));
	accepted(stale_sample, Code::Stale, 2, 1, disarmed, 100, 112, 12, "stale sample consumes continuity");
	fault(stale, stale_sample, stale.observe(input(ReceiptKind::Sample, 2, 1, 112, 112)), FaultReason::ReceiptContinuity,
	      "stale sample sequence cannot be replayed to refresh");
	variants += 2;
}

void identitiesQualificationAndKinds()
{
	for (unsigned variant = 0; variant < 7; ++variant) {
		Fixture f;
		auto bad = input(ReceiptKind::Sample, 2, 1, 110, 115);
		FaultReason reason{};

		switch (variant) {
		case 0: bad.observer_lifetime_id = 0; reason = FaultReason::ObserverLifetimeMismatch; break;

		case 1: ++bad.observer_lifetime_id; reason = FaultReason::ObserverLifetimeMismatch; break;

		case 2: bad.receipt.producer_lifetime_id = 0; reason = FaultReason::ProducerLifetimeMismatch; break;

		case 3: ++bad.receipt.producer_lifetime_id; reason = FaultReason::ProducerLifetimeMismatch; break;

		case 4: bad.receipt.clock_epoch = 0; reason = FaultReason::ClockEpochMismatch; break;

		case 5: ++bad.receipt.clock_epoch; reason = FaultReason::ClockEpochMismatch; break;

		case 6: bad.clock_qualified = false; reason = FaultReason::ClockUnqualified; break;
		}

		fault(f.checker, f.initial, f.checker.observe(bad), reason, "observation identity/clock " + std::to_string(variant));
	}

	for (unsigned variant = 0; variant < 5; ++variant) {
		Fixture f;
		const auto id = variant == 0 ? 0 : variant == 1 ? observer + 1 : observer;
		const auto epoch = variant == 2 ? 0 : variant == 3 ? clock_epoch + 1 : clock_epoch;
		const auto expected = variant < 2 ? FaultReason::ObserverLifetimeMismatch
				      : variant < 4 ? FaultReason::ClockEpochMismatch : FaultReason::ClockUnqualified;
		fault(f.checker, f.initial, f.checker.poll(id, epoch, 110, variant != 4), expected,
		      "poll identity/clock " + std::to_string(variant));
	}

	for (const auto kind : {static_cast<ReceiptKind>(3), static_cast<ReceiptKind>(255)}) {
		Fixture f;
		fault(f.checker, f.initial, f.checker.observe(input(kind, 2, 1, 110, 115)), FaultReason::InvalidKind, "unknown receipt kind");
	}
	const std::array<State, 10> invalid_states{{{0, 0}, {3, 0}, {255, 0}, {1, 1}, {1, 9}, {1, 11}, {1, 16}, {1, 31}, {1, 32}, {1, 255}}};

	for (const auto invalid : invalid_states) {
		for (unsigned side = 0; side < 2; ++side) {
			Fixture f;
			auto bad = input(ReceiptKind::Sample, 2, 1, 110, 115);
			(side == 0 ? bad.receipt.before : bad.receipt.after) = invalid;
			fault(f.checker, f.initial, f.checker.observe(bad), FaultReason::InvalidState,
			      "unknown/disallowed raw state on side " + std::to_string(side) + " arm "
			      + std::to_string(invalid.arming_state) + " nav " + std::to_string(invalid.nav_state));
		}
	}
}

void initialContinuityAndContradictions()
{
	for (const auto kind : {ReceiptKind::Transition, ReceiptKind::Sample}) {
		ReceiptChecker checker(config());
		fault(checker, Snapshot{}, checker.observe(input(kind)), FaultReason::MissingInitial, "first non-initial receipt");
	}

	for (unsigned variant = 0; variant < 7; ++variant) {
		ReceiptChecker checker(config());
		auto bad = input();
		const std::array<uint64_t, 3> counters{0, 2, maximum};

		if (variant < 3) { bad.receipt.sequence = counters[variant]; }

		else if (variant < 6) { bad.receipt.transition_epoch = counters[variant - 3]; }

		else { bad.receipt.before = armed; }

		const auto expected = variant < 3 ? FaultReason::ReceiptContinuity : variant < 6
				      ? FaultReason::TransitionEpochContradiction : FaultReason::StateContradiction;
		fault(checker, Snapshot{}, checker.observe(bad), expected, "invalid initial fields " + std::to_string(variant));
	}

	Fixture repeated;
	fault(repeated.checker, repeated.initial, repeated.checker.observe(input(ReceiptKind::Initial, 2, 1, 110, 115)),
	      FaultReason::RepeatedInitial, "repeated initial never resets lifetime");

	for (const auto sequence : {uint64_t{0}, uint64_t{1}, uint64_t{3}}) {
		Fixture f;
		fault(f.checker, f.initial, f.checker.observe(input(ReceiptKind::Sample, sequence, 1, 110, 115)),
		      FaultReason::ReceiptContinuity, "zero/duplicate/gapped sequence " + std::to_string(sequence));
	}
	Fixture regressed;
	const auto prior = regressed.checker.observe(input(ReceiptKind::Sample, 2, 1, 110, 115));
	fault(regressed.checker, prior, regressed.checker.observe(input(ReceiptKind::Sample, 1, 1, 120, 125)),
	      FaultReason::ReceiptContinuity, "regressed nonzero delivery sequence");

	for (unsigned variant = 0; variant < 6; ++variant) {
		Fixture f;
		auto bad = input(ReceiptKind::Sample, 2, 1, 110, 115);

		if (variant == 0) { bad.receipt.before = armed; }

		if (variant == 1) { bad.receipt.after = armed; }

		if (variant == 2) { bad.receipt.before = armed; bad.receipt.after = armed; }

		if (variant == 3) { bad.receipt.before = offboard; bad.receipt.after = offboard; }

		if (variant == 4) { bad.receipt.transition_epoch = 0; }

		if (variant == 5) { bad.receipt.transition_epoch = 2; }

		fault(f.checker, f.initial, f.checker.observe(bad), variant < 4 ? FaultReason::StateContradiction
		      : FaultReason::TransitionEpochContradiction, "sample contradiction " + std::to_string(variant));
	}

	for (unsigned variant = 0; variant < 5; ++variant) {
		Fixture f;
		auto bad = input(ReceiptKind::Transition, 2, 2, 110, 115, disarmed, armed);

		if (variant == 0) { bad.receipt.before = offboard; }

		if (variant == 1) { bad.receipt.after = disarmed; }

		if (variant == 2) { bad.receipt.transition_epoch = 1; }

		if (variant == 3) { bad.receipt.transition_epoch = 3; }

		if (variant == 4) { bad.receipt.transition_epoch = 0; }

		fault(f.checker, f.initial, f.checker.observe(bad), variant < 2 ? FaultReason::StateContradiction
		      : FaultReason::TransitionEpochContradiction, "transition contradiction " + std::to_string(variant));
	}
}

void timeAndValidationOrder()
{
	for (unsigned variant = 0; variant < 5; ++variant) {
		Fixture f;
		auto bad = input(ReceiptKind::Sample, 2, 1, 110, 115);

		if (variant == 0) { bad.receive_time_us = 0; }

		if (variant == 1) { bad.receive_time_us = 104; }

		if (variant == 2) { bad.receipt.producer_time_us = 0; }

		if (variant == 3) { bad.receipt.producer_time_us = 116; }

		if (variant == 4) { bad.receipt.producer_time_us = 99; }

		fault(f.checker, f.initial, f.checker.observe(bad), variant == 1 || variant == 4 ? FaultReason::TimeRegressed
		      : FaultReason::TimeInvalid, "receipt time defect " + std::to_string(variant));
	}

	for (const auto now : {uint64_t{0}, uint64_t{104}}) {
		Fixture f;
		fault(f.checker, f.initial, f.checker.poll(observer, clock_epoch, now, true),
		      now == 0 ? FaultReason::TimeInvalid : FaultReason::TimeRegressed, "poll invalid local time");
	}
	ReceiptChecker before_initial(config());
	const auto checked_time = before_initial.poll(observer, clock_epoch, 200, true);
	fault(before_initial, checked_time, before_initial.observe(input()), FaultReason::TimeRegressed,
	      "pre-initial poll time cannot be regressed by first receipt");

	for (unsigned variant = 0; variant < 5; ++variant) {
		Fixture f;
		auto bad = input(ReceiptKind::Sample, 3, 1, 110, 115);
		FaultReason first{};

		if (variant == 0) { bad.observer_lifetime_id = 0; bad.clock_qualified = false; first = FaultReason::ObserverLifetimeMismatch; }

		if (variant == 1) { bad.clock_qualified = false; bad.receive_time_us = 0; first = FaultReason::ClockUnqualified; }

		if (variant == 2) { bad.receive_time_us = 0; bad.receipt.producer_lifetime_id = 0; first = FaultReason::TimeInvalid; }

		if (variant == 3) { bad.receipt.producer_lifetime_id = 0; bad.receipt.kind = static_cast<ReceiptKind>(255); first = FaultReason::ProducerLifetimeMismatch; }

		if (variant == 4) { bad.receipt.producer_time_us = 116; first = FaultReason::TimeInvalid; }

		fault(f.checker, f.initial, f.checker.observe(bad), first, "ordered simultaneous defects " + std::to_string(variant));
	}

	ReceiptChecker same_microsecond(config());
	accepted(same_microsecond.observe(input(ReceiptKind::Initial, 1, 1, 100, 100)), Code::Fresh, 1, 1, disarmed,
		 100, 100, 0, "equal first producer and receive timestamps");
	accepted(same_microsecond.observe(input(ReceiptKind::Sample, 2, 1, 100, 100)), Code::Fresh, 2, 1, disarmed,
		 100, 100, 0, "equal producer and local timestamps across receipts");
	variants += 2;
}

void overflowAndMaximumTimes()
{
	Fixture delivery;
	StateReceiptCheckerTestPeer::counters(delivery.checker, maximum - 1, 1);
	const auto last = delivery.checker.observe(input(ReceiptKind::Sample, maximum, 1, 110, 115));
	accepted(last, Code::Fresh, maximum, 1, disarmed, 110, 115, 5, "last representable receipt sequence");
	const auto polled = delivery.checker.poll(observer, clock_epoch, 116, true);
	accepted(polled, Code::Fresh, maximum, 1, disarmed, 110, 116, 6, "poll at exhausted receipt sequence is nonmutating");
	fault(delivery.checker, polled, delivery.checker.observe(input(ReceiptKind::Sample, 0, 1, 120, 125)),
	      FaultReason::CounterExhausted, "delivery sequence exhaustion before wrap arithmetic");
	Fixture epochs;
	StateReceiptCheckerTestPeer::counters(epochs.checker, 1, maximum - 1);
	const auto final_transition = epochs.checker.observe(input(ReceiptKind::Transition, 2, maximum, 110, 115, disarmed, armed));
	accepted(final_transition, Code::Fresh, 2, maximum, armed, 110, 115, 5, "last representable transition epoch");
	const auto unchanged = epochs.checker.observe(input(ReceiptKind::Sample, 3, maximum, 120, 125, armed, armed));
	accepted(unchanged, Code::Fresh, 3, maximum, armed, 120, 125, 5, "sample at epoch MAX needs no increment");
	fault(epochs.checker, unchanged, epochs.checker.observe(input(ReceiptKind::Transition, 4, 0, 130, 135, armed, disarmed)),
	      FaultReason::CounterExhausted, "transition epoch exhaustion never wraps");
	ReceiptChecker times(config());
	accepted(times.observe(input(ReceiptKind::Initial, 1, 1, maximum - 1, maximum)), Code::Fresh,
		 1, 1, disarmed, maximum - 1, maximum, 1, "ordered timestamps at uint64 MAX");
	accepted(times.observe(input(ReceiptKind::Sample, 2, 1, maximum, maximum)), Code::Fresh,
		 2, 1, disarmed, maximum, maximum, 0, "producer timestamp MAX and equal local timestamp");
	ReceiptChecker old(config());
	accepted(old.observe(input(ReceiptKind::Initial, 1, 1, 1, maximum)), Code::Stale,
		 1, 1, disarmed, 1, maximum, maximum - 1, "large ordered subtraction stays exact");
	variants += 7;
}
} // namespace

int main()
{
	constructionAndAllowMask();
	chainAndFreshness();
	identitiesQualificationAndKinds();
	initialContinuityAndContradictions();
	timeAndValidationOrder();
	overflowAndMaximumTimes();
	std::cout << "State receipt checker: " << variants << " finite variants, " << checks << " checks, " << failures << " failures\n";
	return failures == 0 ? 0 : 1;
}
