#include "NativeClosedDiagnosticOwner.hpp"

#include <array>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <memory>
#include <tuple>
#include <type_traits>

namespace research_acceleration
{
struct WireAdapterStateTestPeer {
	static void reportCounter(WireAdapter &adapter, uint64_t sequence) { adapter.snapshot_.last_report_sequence = sequence; }
};
}

namespace research_sitl
{
struct NativeClosedOwnerTestPeer {
	static std::unique_ptr<NativeClosedDiagnosticOwner> make(const NativeClosedOwnerConfig &config)
	{
		return std::unique_ptr<NativeClosedDiagnosticOwner>(new NativeClosedDiagnosticOwner(config));
	}
	static void initial(NativeClosedDiagnosticOwner &owner, RawState state, uint64_t time) { owner.initial(state, time); }
	static void transition(NativeClosedDiagnosticOwner &owner, RawState before, RawState after, uint64_t time)
	{
		owner.transition(before, after, time);
	}
	static void sample(NativeClosedDiagnosticOwner &owner, RawState state, uint64_t time) { owner.sample(state, time); }
	static void stop(NativeClosedDiagnosticOwner &owner, RawState state, uint64_t time) { owner.stop(state, time); }
	static void lost(NativeClosedDiagnosticOwner &owner) { owner.sourceContinuityLost(); }
	static void adapterReports(NativeClosedDiagnosticOwner &owner, uint64_t sequence)
	{
		research_acceleration::WireAdapterStateTestPeer::reportCounter(owner.adapter_, sequence);
	}
	// This HOST-only fixture seeds source counters only. adapterReports above
	// separately seeds just the real adapter report counter for sentinel coverage.
	static void counters(NativeClosedDiagnosticOwner &owner, uint64_t sequence, uint64_t epoch, uint64_t generation)
	{
		owner.source_.sequence = sequence;
		owner.source_.transition_epoch = epoch;
		owner.source_.physical_generation = generation;
	}
};
}

namespace
{
using namespace research_acceleration;
using namespace research_sitl;
using Peer = NativeClosedOwnerTestPeer;
constexpr RawState Disarmed{1, 0};
constexpr RawState Armed{2, 0};
constexpr RawState Offboard{1, 14};
constexpr uint64_t Maximum = std::numeric_limits<uint64_t>::max();
unsigned checks{};
unsigned failures{};

void check(bool condition, const char *message)
{
	++checks;

	if (!condition) { ++failures; std::fprintf(stderr, "FAIL: %s\n", message); }
}

NativeClosedOwnerConfig configuration()
{
	return {{
			kWireProtocolVersionV1, 7, DisarmedPolicy::ValidateOnly,
			{{-10.F, -10.F, -10.F}, {10.F, 10.F, 10.F}, 12.F},
			{80000, 80000, 60000, 8000, 4000}, 0.01F
		}, {11}, {13, 17, kDiagnosticExecutionOrigin}, 19, 23};
}

bool same(RawState a, RawState b)
{
	return a.arming_state == b.arming_state && a.nav_state == b.nav_state;
}

auto sourceFields(const NativeSourceSnapshot &value)
{
	return std::make_tuple(value.initialized, value.state.arming_state, value.state.nav_state, value.sequence,
			       value.transition_epoch, value.physical_generation, value.committed_time_us, value.checked_time_us);
}

auto summaryFields(const NativeOwnerSummary &value)
{
	return std::make_tuple(value.state, value.fault, sourceFields(value.source), value.record_count, value.has_closure);
}

void physical(const TrustedStateSample &value, uint64_t generation, uint64_t time, bool armed)
{
	check(value.generation == generation && value.observer_continuous, "trusted physical reference generation/continuity");
	const auto &context = value.request_guard_context;
	check(context.validator_receive_time_us == time, "actual source timestamp reaches adapter");
	check(context.arm_state == (armed ? ArmState::Armed : ArmState::Disarmed), "raw arming state mapped exactly");
	check(context.mode_state == ResearchModeState::NotSelected, "raw native nav never creates research Selected");
	check(!context.clock_mapping_valid && !context.transport_connected && !context.required_state_valid,
	      "unknown map/transport/estimator never fabricated valid");
	const auto &map = value.map;
	check(!map.valid && map.controller_instance_id == 11 && map.source == 0 && map.session == 0 && map.map_epoch == 0 &&
	      map.validator_minus_producer_us == 0 && map.uncertainty_us == 0 && map.valid_from_producer_us == 0 &&
	      map.valid_until_producer_us == 0, "entire invalid map zero except controller identity");
}

void noAuthority(const DiagnosticSnapshot &value)
{
	check(value.domain == DiagnosticDomain::SitlDisarmedDiagnostic, "native result remains diagnostic rather than Flight");
	check(value.identity.owner_lifetime_id == 13 && value.identity.native_run_id == 17 &&
	      value.identity.origin == DiagnosticExecutionOrigin::SyntheticNativeContract, "explicit synthetic-native execution identity");
	check(!value.operator_attached && value.operator_instance == 0, "no operator ever existed");
	check(!value.session.has_last_successful_session && !value.session.has_supervisor_floor &&
	      !value.session.has_wire_operation_floor, "no administrative or successful session history");
	check(!value.admission.acknowledgement_current, "no ACK authority");
	check(!value.has_last_accepted_sequence && value.last_accepted_sequence == 0 && !value.last_accepted_authenticated,
	      "no accepted Flight request history");
	check(!value.latched && value.first_latched_code == DecisionCode::Accepted, "source checking creates no guard time latch");
}

void boundedResult(const DiagnosticResult &result)
{
	check(result.record_count <= 4, "bounded real adapter report array");
	noAuthority(result.snapshot);
	check(!result.grant_consumed, "no supervisor grant consumed");

	for (uint8_t index = 0; index < result.record_count && index < 4; ++index) {
		const auto &report = result.records[index];
		noAuthority(report.snapshot);
		check(!report.eligible_for_control && (!report.common_decision || !report.common_decision->eligible_for_control),
		      "all historical reports deny control eligibility");
		check(report.snapshot.adapter.last_report_sequence == report.report_sequence &&
		      report.snapshot.adapter.transition_epoch == report.transition_epoch, "exact immutable per-report snapshot counters");
		check(!report.correlation.authentication && report.correlation.operator_instance == 0 &&
		      !report.correlation.admin.has_supervisor_event, "no invented authentication or operator event");
	}

	for (uint8_t index = result.record_count; index < 4; ++index) {
		check(result.records[index].report_sequence == 0 && !result.records[index].state_reference_valid &&
		      !result.records[index].common_decision, "unused report slots initialized");
	}
}

NativeClosureRecord closure(NativeClosedDiagnosticOwner &owner)
{
	NativeClosureRecord result{};
	check(owner.copyClosure(result), "closed owner exposes retained closure value");
	return result;
}

void immutableAfterClose(NativeClosedDiagnosticOwner &owner)
{
	const auto before = owner.summary();
	const auto retained = closure(owner);
	Peer::initial(owner, Armed, 0);
	Peer::transition(owner, Armed, Offboard, Maximum);
	Peer::sample(owner, RawState{255, 255}, 0);
	Peer::stop(owner, Disarmed, Maximum);
	Peer::lost(owner);
	check(summaryFields(owner.summary()) == summaryFields(before), "all postclose entry points preserve final source/counts/fault");
	const auto after = closure(owner);
	check(after.fault == retained.fault && sourceFields(after.retained_source) == sourceFields(retained.retained_source) &&
	      after.result.snapshot.adapter.last_report_sequence == retained.result.snapshot.adapter.last_report_sequence &&
	      after.result.snapshot.rejection_count == retained.result.snapshot.rejection_count, "first closure cannot be replaced or repolled");
}

void sourceFault(NativeClosedDiagnosticOwner &owner, NativeOwnerFault expected, uint64_t committed_time,
		 uint64_t checked_time, uint64_t sequence, uint64_t generation, uint64_t rejections)
{
	const auto summary = owner.summary();
	check(summary.state == NativeOwnerState::Faulted && summary.fault == expected && summary.has_closure,
	      "exact permanent source fault");
	check(summary.source.sequence == sequence && summary.source.physical_generation == generation &&
	      summary.source.committed_time_us == committed_time && summary.source.checked_time_us == checked_time,
	      "fault cannot install rejected source fields or lower checked floor");
	const auto final = closure(owner);
	check(final.fault == expected && final.attempted.has_value() && !final.adapter_input,
	      "rejected raw input distinct from nullopt adapter lifecycle input");
	check(final.result.code == DiagnosticCode::Terminal && final.result.snapshot.terminal,
	      "source failure terminalizes actual adapter");
	check(final.result.record_count == 1 && !final.result.has_operation_record &&
	      final.result.records[0].kind == DiagnosticReportKind::Terminal, "source failure has terminal report without normal poll");
	check(final.result.records[0].correlation.close_reason == (expected == NativeOwnerFault::EvidenceExhausted
			? DiagnosticCloseReason::OwnerEvidenceExhausted : DiagnosticCloseReason::SourceContinuityLost),
	      "source failure lifecycle reason is truthful");
	check(final.result.snapshot.rejection_count == rejections, "invalid source never reaches guard poll");

	if (sequence > 0) {
		check(final.result.records[0].state_reference_valid, "terminal preserves actual prior physical reference");
		physical(final.result.records[0].physical, generation, committed_time, false);

	} else {
		check(!final.result.records[0].state_reference_valid, "fault before Initial invents no reference");
	}

	boundedResult(final.result);
	immutableAfterClose(owner);
}

void golden()
{
	auto owner = Peer::make(configuration());
	check(owner->summary().state == NativeOwnerState::AwaitingInitial, "valid owner starts awaiting actual Initial");
	Peer::initial(*owner, Disarmed, 100);
	Peer::sample(*owner, Disarmed, 101);
	Peer::transition(*owner, Disarmed, Offboard, 102);
	Peer::transition(*owner, Offboard, Disarmed, 102);
	// Explicit synthetic source inputs. No PX4 arming or vehicle command occurs.
	Peer::transition(*owner, Disarmed, Armed, 103);
	Peer::transition(*owner, Armed, Disarmed, 103);
	Peer::sample(*owner, Disarmed, 104);
	check(owner->summary().source.sequence == 7 && owner->summary().source.physical_generation == 5,
	      "source sequence distinct from physical generation");
	Peer::stop(*owner, Disarmed, 105);
	const auto summary = owner->summary();
	check(summary.state == NativeOwnerState::Stopped && summary.fault == NativeOwnerFault::None &&
	      summary.record_count == 7 && summary.source.sequence == 7, "Stop not a source receipt or normal archive slot");
	constexpr uint64_t generations[] = {1, 1, 2, 3, 4, 5, 5};
	constexpr uint64_t times[] = {100, 101, 102, 102, 103, 103, 104};
	constexpr RawState states[] = {Disarmed, Disarmed, Offboard, Disarmed, Armed, Disarmed, Disarmed};
	uint64_t report_sequence = 0;

	for (size_t index = 0; index < 7; ++index) {
		NativeSourceRecord record{};
		check(owner->copyRecord(index, record), "copy actual archived source turn");
		check(record.after.sequence == index + 1 && record.after.physical_generation == generations[index] &&
		      record.after.transition_epoch == generations[index] && same(record.after.state, states[index]), "golden source state/counters");
		check(record.after.committed_time_us == times[index] && record.after.checked_time_us == times[index], "golden source chronology");
		physical(record.adapter_input, generations[index], times[index], index == 4);
		const auto &result = record.result;
		boundedResult(result);
		const bool barrier = index == 0 || (index >= 2 && index <= 5);
		check(result.code == DiagnosticCode::Rejected && result.record_count == (barrier ? 2 : 1) &&
		      result.has_operation_record && result.operation_record_index == (barrier ? 1 : 0), "golden exact adapter shape");
		check(result.snapshot.selection == DiagnosticSelectionState::Disabled && result.snapshot.session.gate == SessionGate::NoTrustedSession,
		      "closed observations create neither selection nor session");

		for (uint8_t report = 0; report < result.record_count && report < 4; ++report) {
			check(result.records[report].report_sequence == ++report_sequence, "golden entire report stream contiguous");
			check(result.records[report].transition_epoch == generations[index], "adapter barriers count raw pair ABA");
		}

		if (barrier) {
			check(result.records[0].kind == DiagnosticReportKind::PhysicalBarrier &&
			      result.records[0].snapshot.rejection_count == index, "physical prefix precedes guard rejection");
		}

		const auto &operation = result.records[barrier ? 1 : 0];
		check(operation.correlation.operation == DiagnosticOperationKind::Poll, "every normal native source event uses actual adapter poll");
		const auto *cause = std::get_if<DecisionCode>(&operation.cause);
		check(operation.kind == DiagnosticReportKind::Operation && cause && *cause == DecisionCode::ClockMappingInvalid,
		      "real adapter preserves closed prerequisite order");
		check(operation.common_decision.has_value(), "real common Decision retained rather than fabricated poll status");

		if (operation.common_decision) {
			const auto &decision = *operation.common_decision;
			check(decision.code == DecisionCode::ClockMappingInvalid && !decision.syntactically_valid &&
			      !decision.conservative_age_valid && decision.conservative_age_us == 0 && decision.rejection_count == index + 1,
			      "exact common closed rejection and absent age evidence");
		}
	}

	check(report_sequence == 12, "independently counted twelve reports before Stop");
	const auto final = closure(*owner);
	boundedResult(final.result);
	check(final.fault == NativeOwnerFault::None && final.adapter_input.has_value() && final.attempted &&
	      final.attempted->kind == NativeSourceKind::Stop, "normal Stop records truthful current receipt separately");

	if (final.adapter_input) { physical(*final.adapter_input, 5, 105, false); }

	check(final.result.code == DiagnosticCode::AlreadyDisabled && final.result.record_count == 1 &&
	      final.result.records[0].report_sequence == 13 && final.result.snapshot.rejection_count == 8,
	      "actual normal lifecycle closure has one real adapter turn");
	check(final.result.records[0].correlation.close_reason == DiagnosticCloseReason::OwnerStop &&
	      !final.result.snapshot.terminal && final.result.snapshot.session.gate == SessionGate::EstablishmentRequired,
	      "permanent owner Stop does not mislabel recoverable adapter closure or invent operator loss");
	immutableAfterClose(*owner);
}

void unchangedChronology()
{
	{
		auto owner = Peer::make(configuration());
		Peer::initial(*owner, Disarmed, 100);
		Peer::transition(*owner, Disarmed, Disarmed, 200);
		const auto changed = owner->summary();
		check(changed.source.sequence == 1 && changed.source.physical_generation == 1 && changed.record_count == 1 &&
		      changed.source.committed_time_us == 100 && changed.source.checked_time_us == 200, "unchanged assignment updates checked floor only");
		Peer::sample(*owner, Disarmed, 150);
		sourceFault(*owner, NativeOwnerFault::TimeRegressed, 100, 200, 1, 1, 1);
		check(closure(*owner).attempted->source_time_us == 150, "rejected raw time is not substituted for retained physical100");
	}
	{
		auto owner = Peer::make(configuration());
		Peer::initial(*owner, Disarmed, 100);
		Peer::transition(*owner, Disarmed, Disarmed, 200);
		Peer::sample(*owner, Disarmed, 200);
		Peer::stop(*owner, Disarmed, 200);
		check(owner->summary().state == NativeOwnerState::Stopped && owner->summary().record_count == 2,
		      "equal checked time valid for Sample and Stop");
		NativeSourceRecord sample{};
		check(owner->copyRecord(1, sample) && sample.before.checked_time_us == 200 && sample.before.committed_time_us == 100 &&
		      sample.after.committed_time_us == 200, "archive distinguishes preceding checked floor from committed time");
	}

	for (unsigned variant = 0; variant < 4; ++variant) {
		auto owner = Peer::make(configuration());
		Peer::initial(*owner, Disarmed, 100);
		const RawState state = variant == 0 ? Offboard : variant == 1 ? RawState{0, 0} : Disarmed;
		Peer::transition(*owner, state, state, variant == 2 ? 0 : variant == 3 ? 99 : 101);
		const NativeOwnerFault fault = variant == 0 ? NativeOwnerFault::StateContradiction :
					       variant == 1 ? NativeOwnerFault::InvalidState :
					       variant == 2 ? NativeOwnerFault::TimeInvalid : NativeOwnerFault::TimeRegressed;
		sourceFault(*owner, fault, 100, 100, 1, 1, 1);
	}
}

void malformedSource()
{
	for (unsigned operation = 0; operation < 3; ++operation) {
		auto owner = Peer::make(configuration());

		if (operation == 0) { Peer::sample(*owner, Disarmed, 100); }

		if (operation == 1) { Peer::transition(*owner, Disarmed, Offboard, 100); }

		if (operation == 2) { Peer::stop(*owner, Disarmed, 100); }

		sourceFault(*owner, NativeOwnerFault::MissingInitial, 0, 0, 0, 0, 0);
	}

	for (RawState invalid : {
		     RawState{0, 0}, RawState{3, 0}, RawState{255, 0}, RawState{1, 9},
		     RawState{1, 11}, RawState{1, 16}, RawState{1, 31}, RawState{1, 255}
	     }) {
		auto owner = Peer::make(configuration());
		Peer::initial(*owner, invalid, 100);
		sourceFault(*owner, NativeOwnerFault::InvalidState, 0, 0, 0, 0, 0);
	}

	for (unsigned variant = 0; variant < 8; ++variant) {
		auto owner = Peer::make(configuration());
		Peer::initial(*owner, Disarmed, 100);
		NativeOwnerFault expected = NativeOwnerFault::StateContradiction;

		switch (variant) {
		case 0: Peer::initial(*owner, Disarmed, 101); expected = NativeOwnerFault::RepeatedInitial; break;

		case 1: Peer::sample(*owner, Offboard, 101); break;

		case 2: Peer::transition(*owner, Offboard, Disarmed, 101); break;

		case 3: Peer::sample(*owner, Disarmed, 0); expected = NativeOwnerFault::TimeInvalid; break;

		case 4: Peer::sample(*owner, Disarmed, 99); expected = NativeOwnerFault::TimeRegressed; break;

		case 5: Peer::stop(*owner, Offboard, 101); break;

		case 6: Peer::stop(*owner, Disarmed, 99); expected = NativeOwnerFault::TimeRegressed; break;

		default: Peer::sample(*owner, RawState{1, 31}, 101); expected = NativeOwnerFault::InvalidState; break;
		}

		sourceFault(*owner, expected, 100, 100, 1, 1, 1);
	}
}

void archiveCapacity()
{
	for (unsigned count : {255U, 256U}) {
		auto owner = Peer::make(configuration());
		Peer::initial(*owner, Disarmed, 100);

		for (unsigned index = 1; index < count; ++index) { Peer::sample(*owner, Disarmed, 100 + index); }

		Peer::transition(*owner, Disarmed, Disarmed, 1000);
		check(owner->summary().record_count == count && owner->summary().source.checked_time_us == 1000,
		      "unchanged assignment requires no archive slot even when full");
		Peer::stop(*owner, Disarmed, 1000);
		check(owner->summary().state == NativeOwnerState::Stopped && owner->summary().record_count == count &&
		      owner->summary().source.sequence == count, "reserved Stop closure survives normal archive boundary");
		const auto final = closure(*owner);
		check(final.result.records[0].report_sequence == count + 2 && final.result.snapshot.rejection_count == count + 1,
		      "Stop report stream independent of normal archive count");
	}
	{
		auto owner = Peer::make(configuration());
		Peer::initial(*owner, Disarmed, 100);

		for (unsigned index = 1; index < 256; ++index) { Peer::sample(*owner, Disarmed, 100 + index); }

		Peer::sample(*owner, Disarmed, 356);
		check(owner->summary().record_count == 256, "overflow never overwrites normal prefix");
		sourceFault(*owner, NativeOwnerFault::EvidenceExhausted, 355, 355, 256, 1, 256);

		for (unsigned index = 0; index < 256; ++index) {
			NativeSourceRecord record{};
			check(owner->copyRecord(index, record) && record.after.sequence == index + 1 &&
			      record.after.committed_time_us == 100 + index && record.result.snapshot.rejection_count == index + 1,
			      "all 256 original full-result records remain readable in order");
		}

		const auto final = closure(*owner);
		check(final.result.records[0].correlation.close_reason == DiagnosticCloseReason::OwnerEvidenceExhausted,
		      "evidence exhaustion has truthful distinct lifecycle reason");
	}
}

void counterBoundaries()
{
	for (unsigned field = 0; field < 3; ++field) {
		auto owner = Peer::make(configuration());
		Peer::initial(*owner, Disarmed, 100);
		Peer::counters(*owner, field == 0 ? Maximum : 1, field == 1 ? Maximum : 1, field == 2 ? Maximum : 1);
		const auto before = owner->summary();
		Peer::transition(*owner, Disarmed, Offboard, 101);
		const auto after = owner->summary();
		check(after.state == NativeOwnerState::Faulted && after.fault == NativeOwnerFault::CounterExhausted &&
		      sourceFields(before.source) == sourceFields(after.source) && after.record_count == 1,
		      "source counter reservation rejects overflow before any partial commit");
		const auto final = closure(*owner);
		check(final.result.snapshot.adapter.state_generation == 1 && final.result.snapshot.rejection_count == 1 &&
		      final.result.records[0].physical.request_guard_context.validator_receive_time_us == 100,
		      "counter test injection never changes actual retained adapter input");
	}

	{
		auto owner = Peer::make(configuration());
		Peer::initial(*owner, Disarmed, Maximum);
		Peer::sample(*owner, Disarmed, Maximum);
		Peer::stop(*owner, Disarmed, Maximum);
		check(owner->summary().state == NativeOwnerState::Stopped, "equal UINT64_MAX time needs no overflowing addition");
	}
}

void lifetimeLoss()
{
	for (bool initialized : {false, true}) {
		auto owner = Peer::make(configuration());

		if (initialized) { Peer::initial(*owner, Disarmed, 100); }

		const auto before = owner->summary();
		Peer::lost(*owner);
		const auto after = owner->summary();
		check(after.state == NativeOwnerState::Faulted && after.fault == NativeOwnerFault::SourceLifetimeLost &&
		      sourceFields(after.source) == sourceFields(before.source) && after.record_count == before.record_count,
		      "actual lifetime-loss seam retains last-valid source without invented raw input");
		const auto final = closure(*owner);
		check(final.fault == NativeOwnerFault::SourceLifetimeLost && !final.attempted && !final.adapter_input &&
		      final.result.code == DiagnosticCode::Terminal && final.result.record_count == 1 &&
		      final.result.records[0].correlation.close_reason == DiagnosticCloseReason::SourceContinuityLost,
		      "lifetime loss produces real nullopt source-loss closure");
		check(final.result.records[0].state_reference_valid == initialized &&
		      final.result.snapshot.rejection_count == (initialized ? 1U : 0U), "lifetime loss never polls invented physical state");
		boundedResult(final.result);
		immutableAfterClose(*owner);
	}
	{
		auto owner = Peer::make(configuration());
		Peer::initial(*owner, Disarmed, 100);
		Peer::stop(*owner, Disarmed, 101);
		Peer::lost(*owner);
		check(owner->summary().state == NativeOwnerState::Stopped && owner->summary().fault == NativeOwnerFault::None &&
		      closure(*owner).result.records[0].correlation.close_reason == DiagnosticCloseReason::OwnerStop,
		      "later lifetime loss cannot relabel first normal Stop");
	}
}

void actualAdapterTerminal()
{
	for (bool exhausted : {false, true}) {
		auto owner = Peer::make(configuration());
		Peer::initial(*owner, Disarmed, 100);

		if (exhausted) { Peer::adapterReports(*owner, Maximum - 1); }

		else { Peer::counters(*owner, 1, 2, 2); }

		const auto before = owner->summary();

		if (exhausted) { Peer::sample(*owner, Disarmed, 101); }

		else { Peer::transition(*owner, Disarmed, Offboard, 101); }

		const auto after = owner->summary();
		check(after.state == NativeOwnerState::Faulted && after.fault == NativeOwnerFault::AdapterTerminal &&
		      after.record_count == 1 && sourceFields(after.source) == sourceFields(before.source),
		      "actual adapter terminal leaves normal source candidate uncommitted");
		const auto final = closure(*owner);
		check(final.attempted && final.adapter_input && final.adapter_input->generation == (exhausted ? 1U : 3U) &&
		      final.adapter_input->request_guard_context.validator_receive_time_us == 101,
		      "adapter-terminal closure retains exact candidate actually submitted");
		check(final.result.code == DiagnosticCode::Terminal && final.result.record_count == 1 &&
		      final.result.records[0].kind == DiagnosticReportKind::Terminal &&
		      final.result.records[0].report_sequence == (exhausted ? Maximum : 3U) &&
		      final.result.records[0].correlation.operation == DiagnosticOperationKind::Poll &&
		      !final.result.records[0].correlation.close_reason,
		      "real terminal result retained once without a second lifecycle turn");
		check(final.result.snapshot.adapter.state_generation == 1 && final.result.snapshot.rejection_count == 1 &&
		      final.result.records[0].physical.request_guard_context.validator_receive_time_us == 100,
		      "actual terminal preserves last adapter physical reference before invalid/capacity turn");
		const auto *cause = std::get_if<DiagnosticLocalReason>(&final.result.records[0].cause);
		check(cause && *cause == (exhausted ? DiagnosticLocalReason::CounterExhausted : DiagnosticLocalReason::TrustedStateFault),
		      "real adapter terminal fault/counter cause not synthesized by owner");
		boundedResult(final.result);
		immutableAfterClose(*owner);
	}
}

void configurationAndReadIsolation()
{
	for (unsigned field = 0; field < 15; ++field) {
		auto config = configuration();

		switch (field) {
		case 0: config.contract.protocol_version = 1; break;

		case 1: config.contract.permitted_source = 0; break;

		case 2: config.contract.disarmed_policy = DisarmedPolicy::Reject; break;

		case 3: config.contract.acceleration_bounds.maximum_norm_m_s2 = 13.F; break;

		case 4: config.contract.timing.minimum_validity_us = 79999; break;

		case 5: config.contract.timing.maximum_validity_us = 80001; break;

		case 6: config.contract.timing.maximum_command_age_us = 60001; break;

		case 7: config.contract.timing.maximum_future_skew_us = 8001; break;

		case 8: config.contract.timing.maximum_clock_uncertainty_us = 4001; break;

		case 9: config.contract.quaternion_norm_tolerance = 0.02F; break;

		case 10: config.adapter_identity.controller_instance_id = 0; break;

		case 11: config.identity.owner_lifetime_id = 0; break;

		case 12: config.identity.native_run_id = 0; break;

		case 13: config.producer_lifetime_id = 0; break;

		default: config.clock_epoch = 0; break;
		}

		auto owner = Peer::make(config);
		check(owner->summary().state == NativeOwnerState::Faulted && owner->summary().fault == NativeOwnerFault::InvalidConfiguration &&
		      !owner->summary().source.initialized && owner->summary().record_count == 0, "invalid explicit profile/identity closes at construction");
		check(closure(*owner).fault == NativeOwnerFault::InvalidConfiguration, "configuration fault never pretends operator loss");
		immutableAfterClose(*owner);
	}

	for (unsigned field = 0; field < 8; ++field) {
		auto config = configuration();

		if (field < 3) { config.contract.acceleration_bounds.minimum_m_s2(field) = -11.F; }

		else if (field < 6) { config.contract.acceleration_bounds.maximum_m_s2(field - 3) = 11.F; }

		else { config.identity.origin = field == 6 ? DiagnosticExecutionOrigin::SyntheticOffline : DiagnosticExecutionOrigin::NativePosixSitl; }

		auto owner = Peer::make(config);
		check(owner->summary().state == NativeOwnerState::Faulted && owner->summary().fault == NativeOwnerFault::InvalidConfiguration &&
		      owner->summary().record_count == 0 && !owner->summary().source.initialized,
		      "each axis bound and compiled-origin mismatch rejected before source observation");
		const auto final = closure(*owner);
		check(final.fault == NativeOwnerFault::InvalidConfiguration && !final.attempted && !final.adapter_input,
		      "invalid bootstrap values never become synthetic physical input");
		immutableAfterClose(*owner);
	}

	{
		auto owner = Peer::make(configuration());
		NativeSourceRecord output{};
		output.after.sequence = 777;
		NativeClosureRecord final{};
		final.retained_source.sequence = 888;
		check(!owner->copyRecord(0, output) && output.after.sequence == 777 &&
		      !owner->copyClosure(final) && final.retained_source.sequence == 888, "open owner rejects export without changing output");
		Peer::initial(*owner, Disarmed, 100);
		check(!owner->copyRecord(0, output) && output.after.sequence == 777, "observing owner cannot export mutable history");
		Peer::stop(*owner, Disarmed, 101);

		for (size_t index : {size_t{1}, size_t{256}, std::numeric_limits<size_t>::max()}) {
			check(!owner->copyRecord(index, output) && output.after.sequence == 777, "out-of-range copy leaves sentinel intact");
		}
		check(owner->copyRecord(0, output), "closed history copied by value");
		output.after.sequence = 999;
		check(owner->copyRecord(0, output) && output.after.sequence == 1, "caller cannot mutate retained history through copy");
	}

	{
		const auto config = configuration();
		WireAdapter ordinary(config.contract, config.adapter_identity);
		check(ordinary.diagnosticSnapshot().domain == DiagnosticDomain::Flight,
		      "ordinary constructor remains Flight under native-contract profile");
		TrustedStateSample input{1, true, {100, ArmState::Disarmed, ResearchModeState::NotSelected, false, false, false}, {}};
		input.map.controller_instance_id = 11;
		const auto rejected = ordinary.diagnosticTurn(input, DiagPoll{});
		check(rejected.code == DiagnosticCode::WrongDomain && rejected.record_count == 0 && !ordinary.diagnosticSnapshot().initialized,
		      "ordinary instance cannot run native diagnostic turn");
	}
}
}

int main()
{
	static_assert(!std::is_copy_constructible<NativeClosedDiagnosticOwner>::value, "one owner lifetime");
	static_assert(!std::is_move_constructible<NativeClosedDiagnosticOwner>::value, "stable owner address");
	static_assert(sizeof(NativeClosedDiagnosticOwner) <= 2U * 1024U * 1024U, "bounded owner storage budget");
	check(kDiagnosticExecutionOrigin == DiagnosticExecutionOrigin::SyntheticNativeContract, "this executable is HOST contract evidence only");
	golden();
	unchangedChronology();
	malformedSource();
	archiveCapacity();
	counterBoundaries();
	lifetimeLoss();
	actualAdapterTerminal();
	configurationAndReadIsolation();
	std::printf("C1D native-contract HOST: %u checks, %u failures; owner=%zu bytes, record=%zu bytes, closure=%zu bytes; no native PX4 execution\n",
		    checks, failures, sizeof(NativeClosedDiagnosticOwner), sizeof(NativeSourceRecord), sizeof(NativeClosureRecord));
	return failures == 0 ? 0 : 1;
}
