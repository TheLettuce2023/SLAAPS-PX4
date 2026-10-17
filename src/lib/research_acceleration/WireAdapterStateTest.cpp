#include "WireAdapter.hpp"

#include <array>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <type_traits>

using namespace research_acceleration;

namespace research_acceleration
{
// Contract-permitted test seam: local counters only, never guard state.
struct WireAdapterStateTestPeer {
	static void seed(WireAdapter &adapter, uint64_t epoch, uint64_t sequence)
	{
		adapter.snapshot_.transition_epoch = epoch;
		adapter.snapshot_.last_report_sequence = sequence;
	}

	static const RequestGuard *guardAddress(const WireAdapter &adapter)
	{
		return &adapter.guard_;
	}
};
} // namespace research_acceleration

namespace
{
constexpr uint64_t kController = 11;
constexpr uint64_t kGeneration = 51;
constexpr TimeUs kTime = 1000;
constexpr uint64_t kMaximum = std::numeric_limits<uint64_t>::max();
int checks = 0;
int failures = 0;

void check(bool condition, const std::string &name)
{
	++checks;

	if (!condition) {
		++failures;
		std::cerr << "FAILED: " << name << '\n';
	}
}

// Identity, timing and acceleration values below are synthetic, not flight settings.
ContractConfig config()
{
	return {kWireProtocolVersionV1, 9, DisarmedPolicy::ValidateOnly,
		{Vec3f(-10.F, -10.F, -10.F), Vec3f(10.F, 10.F, 10.F), 12.F},
		{10, 100, 50, 10, 5}, 0.01F};
}

TrustedStateSample sample(uint64_t generation = kGeneration, TimeUs time = kTime)
{
	TrustedStateSample value{};
	value.generation = generation;
	value.observer_continuous = true;
	value.request_guard_context = {time, ArmState::Armed, ResearchModeState::Selected, true, true, true};
	value.map = {true, 21, 31, kController, 41, 0, 2, 0, 2000};
	return value;
}

CanonicalRequest sentinel()
{
	return {83, 89, 97, 101, 103, 107, 109, 113, 127, Frame::Frd,
		Quantity::InertialAcceleration, ReferencePoint::Cg, Vec3f(131.F, -137.F, -0.F)};
}

template<typename T>
std::array<unsigned char, sizeof(T)> bytes(const T &value)
{
	std::array<unsigned char, sizeof(T)> result{};
	std::memcpy(result.data(), &value, sizeof(T));
	return result;
}

bool zeroDecision(const Decision &value)
{
	return value.code == DecisionCode::Accepted && !value.syntactically_valid
	       && !value.eligible_for_control && !value.latched
	       && value.first_latched_code == DecisionCode::Accepted
	       && !value.conservative_age_valid && value.conservative_age_us == 0
	       && value.rejection_count == 0;
}

bool zeroReport(const AdapterReport &value)
{
	return value.report_sequence == 0 && value.transition_epoch == 0 && value.validator_time_us == 0
	       && value.kind == AdapterReportKind::Observation && value.code == AdapterCode::Observed
	       && !value.has_s1_decision && zeroDecision(value.s1_decision);
}

void checkSnapshot(const AdapterSnapshot &value, bool initialized, bool terminal, uint64_t generation,
		   uint64_t epoch, uint64_t barrier_sequence, TimeUs barrier_time, uint64_t last,
		   const std::string &name)
{
	check(value.initialized == initialized && value.terminal == terminal
	      && value.state_generation == generation && value.transition_epoch == epoch
	      && value.barrier_report_sequence == barrier_sequence
	      && value.barrier_validator_time_us == barrier_time && value.last_report_sequence == last, name);
}

void checkBarrier(const AdapterReport &value, uint64_t sequence, uint64_t epoch, TimeUs time,
		  const std::string &name)
{
	check(value.report_sequence == sequence && value.transition_epoch == epoch
	      && value.validator_time_us == time && value.kind == AdapterReportKind::Barrier
	      && value.code == AdapterCode::Observed && !value.has_s1_decision && zeroDecision(value.s1_decision),
	      name);
}

void checkOperation(const AdapterReport &value, uint64_t sequence, uint64_t epoch, TimeUs time,
		    AdapterReportKind kind, AdapterCode code, DecisionCode decision, uint64_t rejections,
		    const std::string &name)
{
	check(value.report_sequence == sequence && value.transition_epoch == epoch
	      && value.validator_time_us == time && value.kind == kind && value.code == code
	      && value.has_s1_decision && value.s1_decision.code == decision
	      && value.s1_decision.rejection_count == rejections, name);
}

void establish(WireAdapter &adapter)
{
	const AdapterResult result = adapter.observeTrustedState(sample());
	check(result.code == AdapterCode::Observed && result.admission_closed && result.report_count == 2,
	      "state002_result");
	checkBarrier(result.reports[0], 1, 1, kTime, "state002_barrier");
	checkOperation(result.reports[1], 2, 1, kTime, AdapterReportKind::Observation,
		       AdapterCode::Observed, DecisionCode::NoEligibleRequest, 0, "state002_operation");
	checkSnapshot(result.snapshot, true, false, kGeneration, 1, 1, kTime, 2, "state002_snapshot");
}

void expectStructuralFault(const TrustedStateSample &bad, const std::string &name)
{
	WireAdapter adapter(config(), {kController});
	establish(adapter);
	const AdapterResult result = adapter.poll(bad);
	check(result.code == AdapterCode::TrustedStateFault && result.admission_closed && result.report_count == 1
	      && result.snapshot.terminal && result.reports[0].kind == AdapterReportKind::Terminal
	      && result.reports[0].code == AdapterCode::TrustedStateFault
	      && !result.reports[0].has_s1_decision && zeroDecision(result.reports[0].s1_decision),
	      name + "_terminal_without_poll");
	const AdapterResult repeated = adapter.observeTrustedState(sample());
	check(repeated.code == AdapterCode::TrustedStateFault && repeated.report_count == 0
	      && repeated.snapshot.terminal && zeroReport(repeated.reports[0]) && zeroReport(repeated.reports[1]),
	      name + "_repeat_no_action");
}

void state001()
{
	static_assert(!std::is_copy_constructible<WireAdapter>::value, "copy_forbidden");
	static_assert(!std::is_copy_assignable<WireAdapter>::value, "copy_assignment_forbidden");
	static_assert(!std::is_move_constructible<WireAdapter>::value, "move_forbidden");
	static_assert(!std::is_move_assignable<WireAdapter>::value, "move_assignment_forbidden");

	for (bool zero_identity : {true, false}) {
		ContractConfig invalid = config();

		if (!zero_identity) {
			invalid.protocol_version = kWireProtocolVersionV1 + 1;
		}

		WireAdapter adapter(invalid, {zero_identity ? 0 : kController});
		CanonicalRequest output = sentinel();
		const auto before = bytes(output);
		const AdapterResult results[] = {
			adapter.observeTrustedState(sample()), adapter.poll(sample()), adapter.getEligibleRequest(sample(), output)
		};
		check(!adapter.configured(), "state001_not_configured");

		for (const AdapterResult &result : results) {
			check(result.code == AdapterCode::NotConfigured && result.admission_closed && result.report_count == 0
			      && zeroReport(result.reports[0]) && zeroReport(result.reports[1]), "state001_inert_result");
			checkSnapshot(result.snapshot, false, false, 0, 0, 0, 0, 0, "state001_zero_snapshot");
		}

		check(bytes(output) == before, "state001_access_output_unchanged");
	}

	ContractConfig caller = config();
	WireAdapter copied(caller, {kController});
	const RequestGuard *guard = WireAdapterStateTestPeer::guardAddress(copied);
	caller.protocol_version = 0;
	caller.permitted_source = 0;
	check(copied.configured(), "state001_copied_configuration");
	establish(copied);
	WireAdapter other(config(), {kController});
	check(guard != WireAdapterStateTestPeer::guardAddress(other), "state001_single_private_guard");
}

void state002To009()
{
	WireAdapter adapter(config(), {kController});
	establish(adapter);
	const AdapterResult unchanged = adapter.observeTrustedState(sample());
	check(unchanged.code == AdapterCode::Observed && unchanged.report_count == 1,
	      "state003_unchanged_auditable");
	checkOperation(unchanged.reports[0], 3, 1, kTime, AdapterReportKind::Observation,
		       AdapterCode::Observed, DecisionCode::NoEligibleRequest, 0, "state003_operation");
	check(zeroReport(unchanged.reports[1]), "state003_unused_zero");

	const AdapterResult transition = adapter.poll(sample(kGeneration + 1, 1100));
	check(transition.code == AdapterCode::NoEligibleRequest && transition.report_count == 2,
	      "state004_transition");
	checkBarrier(transition.reports[0], 4, 2, 1100, "state004_barrier");
	checkOperation(transition.reports[1], 5, 2, 1100, AdapterReportKind::Poll,
		       AdapterCode::NoEligibleRequest, DecisionCode::NoEligibleRequest, 0, "state004_operation");
	checkSnapshot(transition.snapshot, true, false, kGeneration + 1, 2, 4, 1100, 5, "state004_snapshot");

	CanonicalRequest output = sentinel();
	const auto before = bytes(output);
	const AdapterResult access = adapter.getEligibleRequest(sample(kGeneration + 1, 1100), output);
	check(access.code == AdapterCode::NoEligibleRequest && access.admission_closed && access.report_count == 1,
	      "state009_closed_access");
	checkOperation(access.reports[0], 6, 2, 1100, AdapterReportKind::Access,
		       AdapterCode::NoEligibleRequest, DecisionCode::NoEligibleRequest, 0, "state009_operation");
	check(bytes(output) == before && zeroReport(access.reports[1]), "state009_preserves_output_and_zero_unused");

	auto mismatch = sample(kGeneration + 2);
	mismatch.map.controller_instance_id++;
	const AdapterResult terminal = adapter.poll(mismatch);
	check(terminal.code == AdapterCode::TrustedStateFault && terminal.report_count == 1
	      && terminal.reports[0].report_sequence == 7 && terminal.snapshot.transition_epoch == 2,
	      "state005_controller_mismatch_any_generation");
}

void state006To010()
{
	auto skipped = sample(kGeneration + 2);
	expectStructuralFault(skipped, "state006_skipped");
	auto zero = sample(0);
	expectStructuralFault(zero, "state006_zero_generation");
	auto discontinuous = sample();
	discontinuous.observer_continuous = false;
	expectStructuralFault(discontinuous, "state006_discontinuous");
	auto invalid_arm = sample();
	invalid_arm.request_guard_context.arm_state = static_cast<ArmState>(99);
	expectStructuralFault(invalid_arm, "state006_invalid_arm_enum");
	auto invalid_mode = sample();
	invalid_mode.request_guard_context.mode_state = static_cast<ResearchModeState>(99);
	expectStructuralFault(invalid_mode, "state006_invalid_mode_enum");

	auto changed = sample();
	changed.request_guard_context.arm_state = ArmState::Disarmed;
	expectStructuralFault(changed, "state006_arm_mutation_same_generation");
	changed = sample();
	changed.request_guard_context.mode_state = ResearchModeState::NotSelected;
	expectStructuralFault(changed, "state006_mode_mutation_same_generation");
	changed = sample();
	changed.request_guard_context.transport_connected = false;
	expectStructuralFault(changed, "state006_transport_mutation_same_generation");
	changed = sample();
	changed.request_guard_context.required_state_valid = false;
	expectStructuralFault(changed, "state006_required_mutation_same_generation");
	changed = sample();
	changed.request_guard_context.clock_mapping_valid = false;
	expectStructuralFault(changed, "state006_context_map_validity_mutation_same_generation");

	changed = sample();
	changed.map.source++;
	expectStructuralFault(changed, "state006_source_mutation_same_generation");
	changed = sample();
	changed.map.session++;
	expectStructuralFault(changed, "state006_session_mutation_same_generation");
	changed = sample();
	changed.map.map_epoch++;
	expectStructuralFault(changed, "state006_map_epoch_mutation_same_generation");
	changed = sample();
	changed.map.validator_minus_producer_us++;
	expectStructuralFault(changed, "state006_offset_mutation_same_generation");
	changed = sample();
	changed.map.uncertainty_us++;
	expectStructuralFault(changed, "state006_uncertainty_mutation_same_generation");
	changed = sample();
	changed.map.valid_from_producer_us++;
	expectStructuralFault(changed, "state006_interval_start_mutation_same_generation");
	changed = sample();
	changed.map.valid_until_producer_us--;
	expectStructuralFault(changed, "state006_interval_end_mutation_same_generation");

	WireAdapter rebind(config(), {kController});
	establish(rebind);
	auto rebound = sample(kGeneration + 1, 1100);
	rebound.map.source = 22;
	rebound.map.session = 32;
	const AdapterResult rebound_result = rebind.poll(rebound);
	check(rebound_result.code == AdapterCode::NoEligibleRequest && rebound_result.report_count == 2,
	      "state007_contiguous_rebind_barrier");

	WireAdapter unavailable(config(), {kController});
	establish(unavailable);
	auto invalid_map = sample(kGeneration + 1, 1100);
	invalid_map.map.valid = false;
	invalid_map.request_guard_context.clock_mapping_valid = false;
	const AdapterResult first_rejection = unavailable.poll(invalid_map);
	const AdapterResult second_rejection = unavailable.poll(invalid_map);
	check(first_rejection.code == AdapterCode::GuardRejected && first_rejection.report_count == 2
	      && first_rejection.reports[1].s1_decision.code == DecisionCode::ClockMappingInvalid
	      && first_rejection.reports[1].s1_decision.rejection_count == 1,
	      "state008_false_map_one_s1_poll");
	check(second_rejection.code == AdapterCode::GuardRejected && second_rejection.report_count == 1
	      && second_rejection.reports[0].s1_decision.rejection_count == 2,
	      "state008_rejection_count_delta_one");

	auto malformed_map = sample();
	malformed_map.map.source = 0;
	expectStructuralFault(malformed_map, "state008_zero_source");
	malformed_map = sample();
	malformed_map.map.session = 0;
	expectStructuralFault(malformed_map, "state008_zero_session");
	malformed_map = sample();
	malformed_map.map.map_epoch = 0;
	expectStructuralFault(malformed_map, "state008_zero_epoch");
	malformed_map = sample();
	malformed_map.map.uncertainty_us = 0;
	expectStructuralFault(malformed_map, "state008_zero_uncertainty");
	malformed_map = sample();
	malformed_map.map.valid_from_producer_us = 2001;
	expectStructuralFault(malformed_map, "state008_reversed_interval");
	malformed_map = sample();
	malformed_map.map.controller_instance_id++;
	expectStructuralFault(malformed_map, "state008_controller_mismatch");

	WireAdapter unknown(config(), {kController});
	auto named_unknown = sample();
	named_unknown.request_guard_context.arm_state = ArmState::Unknown;
	const AdapterResult unknown_result = unknown.poll(named_unknown);
	check(unknown_result.code == AdapterCode::GuardRejected && !unknown_result.snapshot.terminal
	      && unknown_result.reports[1].s1_decision.code == DecisionCode::ArmStateUnknown,
	      "state006_named_unknown_s1_rejection");

	WireAdapter clock(config(), {kController});
	establish(clock);
	const AdapterResult regression = clock.poll(sample(kGeneration, kTime - 1));
	const AdapterResult below_floor = clock.poll(sample(kGeneration, kTime - 1));
	const AdapterResult at_floor = clock.poll(sample(kGeneration, kTime));
	check(regression.code == AdapterCode::GuardRejected && !regression.snapshot.terminal
	      && regression.reports[0].s1_decision.code == DecisionCode::ValidatorTimeRegressed
	      && regression.reports[0].s1_decision.latched, "state010_initial_regression");
	check(below_floor.reports[0].s1_decision.code == DecisionCode::ValidatorTimeRegressed,
	      "state010_repeated_below_high_water");
	check(at_floor.reports[0].s1_decision.code == DecisionCode::Latched, "state010_latched_at_high_water");
}

void state011To013()
{
	WireAdapter ordinary(config(), {kController});
	establish(ordinary);
	WireAdapterStateTestPeer::seed(ordinary, 1, kMaximum - 2);
	const AdapterResult normal = ordinary.poll(sample());
	const AdapterResult exhausted = ordinary.poll(sample());
	check(normal.report_count == 1 && normal.reports[0].report_sequence == kMaximum - 1,
	      "state011_last_normal");
	check(exhausted.code == AdapterCode::CounterExhausted && exhausted.report_count == 1
	      && exhausted.reports[0].report_sequence == kMaximum
	      && exhausted.reports[0].kind == AdapterReportKind::Terminal, "state011_terminal_once");

	WireAdapter transition(config(), {kController});
	establish(transition);
	WireAdapterStateTestPeer::seed(transition, 1, kMaximum - 2);
	const AdapterResult no_partial = transition.poll(sample(kGeneration + 1));
	check(no_partial.code == AdapterCode::CounterExhausted && no_partial.report_count == 1
	      && no_partial.reports[0].report_sequence == kMaximum && no_partial.snapshot.transition_epoch == 1,
	      "state011_transition_prereserved");

	auto controller_fault = sample();
	controller_fault.map.controller_instance_id++;
	WireAdapter structural(config(), {kController});
	establish(structural);
	WireAdapterStateTestPeer::seed(structural, 1, kMaximum - 2);
	const AdapterResult structural_normal = structural.poll(controller_fault);
	check(structural_normal.code == AdapterCode::TrustedStateFault
	      && structural_normal.reports[0].report_sequence == kMaximum - 1,
	      "state013_structural_precedes_capacity");

	WireAdapter fallback(config(), {kController});
	establish(fallback);
	WireAdapterStateTestPeer::seed(fallback, 1, kMaximum - 1);
	const AdapterResult structural_exhausted = fallback.poll(controller_fault);
	check(structural_exhausted.code == AdapterCode::CounterExhausted
	      && structural_exhausted.reports[0].report_sequence == kMaximum,
	      "state013_terminal_counter_fallback");

	WireAdapter epoch(config(), {kController});
	establish(epoch);
	WireAdapterStateTestPeer::seed(epoch, kMaximum - 1, 10);
	const AdapterResult reaches_maximum = epoch.poll(sample(kGeneration + 1));
	const AdapterResult unchanged = epoch.poll(sample(kGeneration + 1));
	const AdapterResult epoch_exhausted = epoch.poll(sample(kGeneration + 2));
	check(reaches_maximum.report_count == 2 && reaches_maximum.snapshot.transition_epoch == kMaximum,
	      "state012_epoch_reaches_maximum");
	check(unchanged.code == AdapterCode::NoEligibleRequest && unchanged.report_count == 1,
	      "state012_unchanged_maximum_epoch");
	check(epoch_exhausted.code == AdapterCode::CounterExhausted && epoch_exhausted.snapshot.terminal
	      && epoch_exhausted.snapshot.transition_epoch == kMaximum, "state012_epoch_no_wrap");
}

bool sameSnapshot(const AdapterSnapshot &a, const AdapterSnapshot &b)
{
	return a.initialized == b.initialized && a.terminal == b.terminal
	       && a.state_generation == b.state_generation && a.transition_epoch == b.transition_epoch
	       && a.barrier_report_sequence == b.barrier_report_sequence
	       && a.barrier_validator_time_us == b.barrier_validator_time_us
	       && a.last_report_sequence == b.last_report_sequence;
}

AdapterResult call(WireAdapter &adapter, unsigned operation, const TrustedStateSample &state)
{
	CanonicalRequest output = sentinel();
	const auto before = bytes(output);
	AdapterResult result{};

	if (operation == 0) {
		result = adapter.observeTrustedState(state);

	} else if (operation == 1) {
		result = adapter.poll(state);

	} else {
		result = adapter.getEligibleRequest(state, output);
	}

	check(bytes(output) == before, "all_calls_output_unchanged");
	check(result.admission_closed && result.report_count <= 2, "all_calls_closed_bounded");

	for (unsigned index = result.report_count; index < 2; ++index) {
		check(zeroReport(result.reports[index]), "all_calls_unused_fields_zero");
	}

	for (unsigned index = 0; index < result.report_count; ++index) {
		check(!result.reports[index].s1_decision.eligible_for_control, "all_calls_no_eligibility");
	}

	return result;
}

void repeatTerminal(WireAdapter &adapter, const AdapterResult &terminal)
{
	const auto *identity = WireAdapterStateTestPeer::guardAddress(adapter);

	for (unsigned op = 0; op < 3; ++op) {
		// Even different/invalid samples cannot restart a terminal lifetime.
		const auto result = call(adapter, op, sample(op == 0 ? 0 : kMaximum, op));
		check(result.code == terminal.code && result.report_count == 0
		      && sameSnapshot(result.snapshot, terminal.snapshot), "terminal_complete_snapshot_frozen");
		check(adapter.configured() && WireAdapterStateTestPeer::guardAddress(adapter) == identity,
		      "terminal_configuration_and_guard_identity_retained");
	}
}

void additionalConstructionAndAccess()
{
	std::array<ContractConfig, 9> invalid;
	invalid.fill(config());
	invalid[0].permitted_source = 0;
	invalid[1].timing.maximum_command_age_us = 0;
	invalid[2].timing.minimum_validity_us = 101;
	invalid[3].acceleration_bounds.maximum_norm_m_s2 = 0.F;
	invalid[4].quaternion_norm_tolerance = 1.F;
	invalid[5].disarmed_policy = static_cast<DisarmedPolicy>(99);
	invalid[6].acceleration_bounds.minimum_m_s2(0) = 11.F;
	invalid[7].acceleration_bounds.maximum_m_s2(1) = std::numeric_limits<float>::quiet_NaN();
	invalid[8].quaternion_norm_tolerance = std::numeric_limits<float>::infinity();

	for (const auto &broken : invalid) {
		WireAdapter adapter(broken, {kController});

		for (unsigned op = 0; op < 3; ++op) {
			const auto result = call(adapter, op, sample());
			check(!adapter.configured() && result.code == AdapterCode::NotConfigured
			      && result.report_count == 0 && sameSnapshot(result.snapshot, AdapterSnapshot{}),
			      "invalid_s1_configuration_is_inert");
		}
	}

	ContractConfig caller = config();
	AdapterIdentity identity{kController};
	WireAdapter copied(caller, identity);
	caller.disarmed_policy = DisarmedPolicy::Reject;
	identity.controller_instance_id++;
	auto disarmed = sample();
	disarmed.request_guard_context.arm_state = ArmState::Disarmed;
	const auto validated = call(copied, 0, disarmed);
	check(validated.code == AdapterCode::Observed && validated.reports[1].s1_decision.code == DecisionCode::ValidatedDisarmed,
	      "copied_policy_and_identity_have_behavioral_effect");

	for (unsigned op = 0; op < 3; ++op) {
		WireAdapter adapter(config(), {kController});
		const auto *guard = WireAdapterStateTestPeer::guardAddress(adapter);
		// A rejected context increments S1's counter once, exposing duplicate polls.
		auto missing_map = sample();
		missing_map.map.valid = false;
		missing_map.request_guard_context.clock_mapping_valid = false;
		const auto first = call(adapter, op, missing_map);
		check(first.report_count == 2 && first.code == AdapterCode::GuardRejected
		      && first.reports[1].s1_decision.rejection_count == 1, "all_ops_first_exact_one_poll");
		checkBarrier(first.reports[0], 1, 1, kTime, "all_ops_first_barrier");
		const auto repeated = call(adapter, op, missing_map);
		check(repeated.report_count == 1 && repeated.reports[0].s1_decision.rejection_count == 2,
		      "all_ops_repeat_exact_one_poll");
		missing_map.generation++;
		const auto transition = call(adapter, op, missing_map);
		check(transition.report_count == 2 && transition.reports[1].s1_decision.rejection_count == 3
		      && WireAdapterStateTestPeer::guardAddress(adapter) == guard, "all_ops_transition_one_poll_same_guard");
		auto fault = missing_map;
		fault.map.controller_instance_id++;
		const auto terminal = call(adapter, op, fault);
		check(terminal.code == AdapterCode::TrustedStateFault && terminal.report_count == 1
		      && !terminal.reports[0].has_s1_decision, "all_ops_terminal_without_poll");
		repeatTerminal(adapter, terminal);

		WireAdapter first_bad(config(), {kController});
		const auto invalid_first = call(first_bad, op, fault);
		checkSnapshot(invalid_first.snapshot, false, true, 0, 0, 0, 0, 1, "first_invalid_no_baseline");
		check(invalid_first.reports[0].report_sequence == 1 && invalid_first.reports[0].transition_epoch == 0,
		      "first_invalid_terminal_report_identity");
		repeatTerminal(first_bad, invalid_first);
	}
}

void additionalGenerationsAndClock()
{
	expectStructuralFault(sample(kGeneration - 1), "decreased_generation");
	WireAdapter maximum(config(), {kController});
	const auto first = call(maximum, 2, sample(kMaximum));
	const auto same = call(maximum, 2, sample(kMaximum));
	check(first.report_count == 2 && same.report_count == 1 && !same.snapshot.terminal,
	      "maximum_generation_can_repeat");
	const auto wrapped = call(maximum, 2, sample(0));
	check(wrapped.code == AdapterCode::TrustedStateFault, "generation_must_not_wrap");
	repeatTerminal(maximum, wrapped);
	auto both_false = sample();
	both_false.map.valid = false;
	both_false.request_guard_context.clock_mapping_valid = false;
	expectStructuralFault(both_false, "map_validity_pair_changed_without_generation");
	auto map_false_only = sample(kGeneration + 1);
	map_false_only.map.valid = false;
	expectStructuralFault(map_false_only, "opposite_map_context_disagreement");

	for (unsigned op = 0; op < 3; ++op) {
		WireAdapter unknown(config(), {kController});
		auto mode = sample();
		mode.request_guard_context.mode_state = ResearchModeState::Unknown;
		const auto rejected = call(unknown, op, mode);
		check(rejected.code == AdapterCode::GuardRejected && !rejected.snapshot.terminal
		      && rejected.reports[1].s1_decision.code == DecisionCode::ModeStateUnknown,
		      "named_mode_unknown_is_guard_loss_not_structural");
		WireAdapter clock(config(), {kController});
		call(clock, op, sample());
		const auto regression = call(clock, op, sample(kGeneration, kTime - 1));
		const auto below = call(clock, op, sample(kGeneration, kTime - 1));
		const auto recovered_time = call(clock, op, sample(kGeneration, kTime));
		check(regression.code == AdapterCode::GuardRejected && !regression.snapshot.terminal
		      && regression.reports[0].s1_decision.code == DecisionCode::ValidatorTimeRegressed
		      && regression.reports[0].s1_decision.first_latched_code == DecisionCode::ValidatorTimeRegressed
		      && regression.reports[0].s1_decision.rejection_count == 1, "all_ops_clock_first_cause");
		check(below.reports[0].s1_decision.code == DecisionCode::ValidatorTimeRegressed
		      && below.reports[0].s1_decision.rejection_count == 2, "all_ops_clock_high_water_retained");
		check(recovered_time.reports[0].s1_decision.code == DecisionCode::Latched
		      && recovered_time.reports[0].s1_decision.latched
		      && recovered_time.reports[0].s1_decision.first_latched_code == DecisionCode::ValidatorTimeRegressed
		      && recovered_time.reports[0].s1_decision.rejection_count == 3, "all_ops_clock_latch_not_reset");
	}

	// Exact golden sequence without an intervening accessor: terminal report six.
	WireAdapter golden(config(), {kController});
	call(golden, 0, sample());
	call(golden, 0, sample());
	call(golden, 1, sample(kGeneration + 1, 1100));
	auto bad = sample(kGeneration + 2, 1200);
	bad.map.controller_instance_id++;
	const auto terminal = call(golden, 1, bad);
	checkSnapshot(terminal.snapshot, true, true, kGeneration + 1, 2, 4, 1100, 6, "golden_exact_terminal_six");
	repeatTerminal(golden, terminal);
}

void additionalCounterTerminals()
{
	for (unsigned op = 0; op < 3; ++op) {
		for (bool transition : {false, true}) {
			WireAdapter adapter(config(), {kController});
			establish(adapter);
			WireAdapterStateTestPeer::seed(adapter, 1, transition ? kMaximum - 2 : kMaximum - 1);
			const auto terminal = call(adapter, op, sample(transition ? kGeneration + 1 : kGeneration, 1100));
			check(terminal.code == AdapterCode::CounterExhausted && terminal.report_count == 1
			      && terminal.reports[0].report_sequence == kMaximum
			      && !terminal.reports[0].has_s1_decision && zeroDecision(terminal.reports[0].s1_decision),
			      "all_ops_exhaustion_without_poll");
			checkSnapshot(terminal.snapshot, true, true, kGeneration, 1, 1, kTime, kMaximum,
				      "exhaustion_keeps_complete_barrier_snapshot");
			repeatTerminal(adapter, terminal);
		}
	}

	WireAdapter enough(config(), {kController});
	establish(enough);
	WireAdapterStateTestPeer::seed(enough, 1, kMaximum - 3);
	const auto last_two = call(enough, 1, sample(kGeneration + 1));
	check(last_two.report_count == 2 && last_two.reports[0].report_sequence == kMaximum - 2
	      && last_two.reports[1].report_sequence == kMaximum - 1 && !last_two.snapshot.terminal,
	      "two_last_normal_slots_are_usable");
	const auto terminal = call(enough, 1, sample(kGeneration + 1));
	repeatTerminal(enough, terminal);
}

} // namespace

int main()
{
	state001();
	state002To009();
	state006To010();
	state011To013();
	additionalConstructionAndAccess();
	additionalGenerationsAndClock();
	additionalCounterTerminals();
	std::cout << checks << " checks; " << failures << " failures\n";
	return failures == 0 ? 0 : 1;
}
