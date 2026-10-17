#include "WireAdapter.hpp"
#include "WireStatus.hpp"

#include <array>
#include <cstring>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <string>
#include <tuple>

namespace research_acceleration
{
// test_peer_injection: these seams are unavailable in the ordinary build.
struct DiagnosticAdapterTestPeer {
	static bool attach(WireAdapter &adapter, uint64_t id = 19) { return adapter.attachDiagnosticOperator(id); }
	static DiagnosticResult close(WireAdapter &adapter, const std::optional<TrustedStateSample> &receipt,
				      DiagnosticCloseReason reason = DiagnosticCloseReason::OperatorDetach)
	{
		return adapter.closeDiagnosticLifecycle(receipt, reason);
	}
	static void counters(WireAdapter &adapter, uint64_t report, uint64_t transition, uint64_t selector)
	{
		adapter.snapshot_.last_report_sequence = report;
		adapter.snapshot_.transition_epoch = transition;
		adapter.diagnostic_selector_epoch_ = selector;
	}
	static void supervisor(WireAdapter &adapter, uint64_t floor)
	{
		adapter.session_snapshot_.has_supervisor_floor = true;
		adapter.session_snapshot_.supervisor_floor = floor;
	}
};
} // namespace research_acceleration

using namespace research_acceleration;

namespace
{
using Peer = DiagnosticAdapterTestPeer;
using Kind = DiagnosticReportKind;
constexpr auto selected = DiagnosticSelectionState::SelectedForValidation;
constexpr auto disabled = DiagnosticSelectionState::Disabled;
constexpr uint64_t maximum = std::numeric_limits<uint64_t>::max();
size_t checks = 0;
size_t failures = 0;

void check(bool value, const std::string &name)
{
	++checks;

	if (!value) {
		++failures;
		std::cerr << "FAILED: " << name << '\n';
	}
}

// Synthetic contract trace only: no operational timing, clock or signing claim.
// Canonical guard fixture protocol 1 is distinct from packed wire V1 (65536).
ContractConfig config()
{
	return {kWireProtocolVersionV1, 7, DisarmedPolicy::ValidateOnly,
		{Vec3f(-10.F, -10.F, -10.F), Vec3f(10.F, 10.F, 10.F), 10.F}, {20, 200, 50, 10, 2}, 0.001F};
}

TrustedStateSample sample(TimeUs now = 9950, uint64_t generation = 1)
{
	return {generation, true, {now, ArmState::Disarmed, ResearchModeState::NotSelected, true, true, true},
		{true, 7, 100, 11, 1, 1000, 2, 8000, 10000}};
}

DiagnosticIngress ingress() { return {13, 17, {true, true, true, true}}; }

DiagnosticBinding binding(const WireAdapter &adapter, const TrustedStateSample &physical)
{
	const auto state = adapter.diagnosticSnapshot();
	return {13, 17, 11, state.operator_instance, physical.generation,
		state.adapter.transition_epoch, state.selector_epoch, physical.map};
}

DiagnosticSelectionGrant selectionGrant(const WireAdapter &adapter, const TrustedStateSample &physical,
					DiagnosticAction action)
{
	return {adapter.sessionSnapshot().supervisor_floor + 1, action, binding(adapter, physical)};
}

DiagnosticAdminGrant adminGrant(const WireAdapter &adapter, const TrustedStateSample &physical, SupervisorAction action)
{
	const auto expected = binding(adapter, physical);
	const auto history = adapter.sessionSnapshot();
	const bool bound = action != SupervisorAction::ClearLatchedFault || history.has_last_successful_session;
	const auto session = action == SupervisorAction::ClearLatchedFault ? history.last_successful_session
			     : TrustedSession{7, physical.map.session};
	return {expected, {adapter.sessionSnapshot().supervisor_floor + 1, physical.generation,
			   expected.transition_epoch, 11, action, bound, bound ? session : TrustedSession{}
			  }};
}

DecodedSessionV1 sessionPacket(const WireAdapter &adapter, uint8_t operation, TimeUs raw = 0)
{
	const auto state = adapter.diagnosticSnapshot();
	return {11, state.adapter.transition_epoch, 1, 7, 100, state.session.wire_operation_floor + 1,
		raw, operation == 3 ? state.adapter.barrier_report_sequence : 0,
		kWireProtocolVersionV1, 1, 1, operation, 0, {0, 0, 0, 0}};
}

DecodedRequestV1 requestPacket(const WireAdapter &adapter, uint64_t sequence = 1, TimeUs raw = 9000)
{
	return {{
			7, 100, sequence, raw, raw + 100, 11, adapter.diagnosticSnapshot().adapter.transition_epoch, 1,
			1.F, 0.F, 0.F, kWireProtocolVersionV1, kWireFrameFrdV1,
			kWireQuantityInertialAccelerationV1, kWireReferencePointCgV1
		}, 1, 1, 0, {0, 0, 0, 0}};
}

auto stable(const DiagnosticSnapshot &state)
{
	return std::make_tuple(state.initialized, state.terminal, state.selection, state.selector_epoch,
			       state.operator_attached, state.operator_instance, state.adapter.state_generation,
			       state.adapter.transition_epoch, state.adapter.last_report_sequence,
			       state.session.gate, state.session.supervisor_floor, state.session.wire_operation_floor,
			       state.admission.acknowledgement_current, state.latched, state.rejection_count,
			       state.has_last_accepted_sequence, state.last_accepted_sequence, state.last_accepted_authenticated);
}

void invariant(const DiagnosticResult &result, const std::string &name)
{
	check(result.record_count <= 4, name + ": bounded records");
	check(!result.snapshot.has_last_accepted_sequence && result.snapshot.last_accepted_sequence == 0
	      && !result.snapshot.last_accepted_authenticated, name + ": flight history unchanged");
	check(result.has_operation_record ? result.operation_record_index < result.record_count
	      : result.operation_record_index == 0, name + ": operation index valid");

	for (uint8_t i = 0; i < result.record_count && i < 4; ++i) {
		const auto &record = result.records[i];
		check(record.domain == DiagnosticDomain::SitlDisarmedDiagnostic && record.identity.owner_lifetime_id == 13
		      && record.identity.fixture_run_id == 17, name + ": domain and identities");
		check(!record.eligible_for_control && (!record.common_decision || !record.common_decision->eligible_for_control),
		      name + ": no control eligibility");
		check(record.snapshot.adapter.last_report_sequence == record.report_sequence
		      && record.snapshot.adapter.transition_epoch == record.transition_epoch
		      && record.snapshot.selector_epoch == record.selector_epoch, name + ": post-record counters");
		check(!record.snapshot.has_last_accepted_sequence && !record.snapshot.last_accepted_authenticated,
		      name + ": no flight history in earlier record");

		if (i > 0) { check(record.report_sequence == result.records[i - 1].report_sequence + 1, name + ": consecutive IDs"); }
	}

	for (uint8_t i = result.record_count; i < 4; ++i) {
		check(result.records[i].report_sequence == 0 && !result.records[i].common_decision
		      && !result.records[i].eligible_for_control && !result.records[i].state_reference_valid,
		      name + ": unused slots initialized");
	}

	if (result.record_count > 0 && result.record_count <= 4) {
		check(stable(result.snapshot) == stable(result.records[result.record_count - 1].snapshot), name + ": final snapshot");
	}
}

void shape(const DiagnosticResult &result, std::initializer_list<Kind> kinds, uint8_t operation,
	   DiagnosticCode code, const std::string &name)
{
	invariant(result, name);
	check(result.code == code && result.record_count == kinds.size(), name + ": code and count");
	check(result.has_operation_record && result.operation_record_index == operation, name + ": exact operation index");
	uint8_t index = 0;

	for (const auto kind : kinds) {
		check(index < result.record_count && result.records[index].kind == kind, name + ": ordered kind " + std::to_string(index));
		++index;
	}
}

template<typename T>
void cause(const DiagnosticResult &result, T expected, const std::string &name)
{
	check(result.has_operation_record && result.operation_record_index < result.record_count, name + ": operation exists");

	if (result.has_operation_record && result.operation_record_index < result.record_count) {
		const auto &value = result.records[result.operation_record_index].cause;
		check(std::holds_alternative<T>(value) && std::get<T>(value) == expected, name + ": typed cause");
	}
}

void terminal(const DiagnosticResult &result, const std::string &name, bool sentinel = false)
{
	invariant(result, name);
	check(result.code == DiagnosticCode::Terminal && result.snapshot.terminal
	      && result.snapshot.selection == DiagnosticSelectionState::Faulted
	      && result.snapshot.session.gate == SessionGate::Revoked && !result.snapshot.admission.acknowledgement_current,
	      name + ": terminal closes all authority");
	check(!result.grant_consumed && !result.action_performed && !result.has_operation_record
	      && result.record_count == 1 && result.records[0].kind == Kind::Terminal, name + ": no partial operation");

	if (sentinel) { check(result.records[0].report_sequence == maximum, name + ": sentinel"); }
}

struct Fixture {
	WireAdapter adapter{config(), {11}, DiagnosticOfflineTag{}, {13, 17}};
	TrustedStateSample physical{sample()};

	Fixture() { check(Peer::attach(adapter), "fixture independent operator attachment"); }
	DiagnosticResult observe() { return adapter.diagnosticTurn(physical, DiagObserve{}); }
	DiagnosticResult enable() { return adapter.diagnosticTurn(physical, DiagEnable{selectionGrant(adapter, physical, DiagnosticAction::Enable)}); }
	DiagnosticResult disable() { return adapter.diagnosticTurn(physical, DiagDisable{selectionGrant(adapter, physical, DiagnosticAction::Disable)}); }
	DiagnosticResult establish()
	{
		return adapter.diagnosticTurn(physical, DiagEstablish{sessionPacket(adapter, 1), ingress(),
					      adminGrant(adapter, physical, SupervisorAction::Establish)});
	}
	DiagnosticResult acknowledge()
	{
		physical.request_guard_context.validator_receive_time_us = 9965;
		return adapter.diagnosticTurn(physical, DiagAcknowledge{sessionPacket(adapter, 3, 8960), ingress()});
	}
	DiagnosticResult request(uint64_t sequence = 1, TimeUs raw = 9000)
	{
		return adapter.diagnosticTurn(physical, DiagRequest{requestPacket(adapter, sequence, raw), ingress()});
	}
	void ready()
	{
		check(observe().code == DiagnosticCode::Observed, "setup observe");
		check(enable().code == DiagnosticCode::SelectionEnabled, "setup enable");
		check(establish().code == DiagnosticCode::SessionEstablished, "setup establish");
		check(acknowledge().code == DiagnosticCode::Acknowledged, "setup ACK");
		physical.request_guard_context.validator_receive_time_us = 10010;
		check(request().code == DiagnosticCode::RequestValidatedDisarmed, "setup request");
	}
	void latch()
	{
		ready();
		physical.request_guard_context.validator_receive_time_us = 10011;
		check(request(2, 8999).snapshot.latched, "setup producer latch");
	}
};

void golden()
{
	Fixture f;
	const auto initial = f.adapter.diagnosticSnapshot();
	check(initial.selection == disabled && initial.selector_epoch == 0 && !initial.initialized, "golden initial state");
	const auto observed = f.observe();
	shape(observed, {Kind::PhysicalBarrier, Kind::Operation}, 1, DiagnosticCode::Observed, "golden observe");
	check(observed.records[0].report_sequence == 1 && observed.records[1].report_sequence == 2
	      && observed.snapshot.adapter.transition_epoch == 1 && observed.snapshot.selector_epoch == 1, "golden initial counters");
	const auto enabled = f.enable();
	shape(enabled, {Kind::SelectorBarrier, Kind::Operation}, 1, DiagnosticCode::SelectionEnabled, "golden enable");
	check(enabled.records[0].report_sequence == 3 && enabled.records[1].report_sequence == 4
	      && enabled.snapshot.adapter.transition_epoch == 2 && enabled.snapshot.selector_epoch == 2
	      && enabled.grant_consumed && enabled.action_performed && enabled.snapshot.session.supervisor_floor == 1,
	      "golden enable counters and event");
	const auto established = f.establish();
	shape(established, {Kind::SessionBarrier, Kind::Operation}, 1, DiagnosticCode::SessionEstablished, "golden establish");
	check(established.records[0].report_sequence == 5 && established.records[1].report_sequence == 6
	      && established.snapshot.adapter.transition_epoch == 3 && established.snapshot.selector_epoch == 2
	      && established.snapshot.session.gate == SessionGate::Established
	      && established.snapshot.session.supervisor_floor == 2 && established.snapshot.session.wire_operation_floor == 1
	      && !established.snapshot.admission.acknowledgement_current, "golden establish counters");
	const auto ack = f.acknowledge();
	shape(ack, {Kind::Operation}, 0, DiagnosticCode::Acknowledged, "golden ACK");
	check(ack.records[0].report_sequence == 7 && ack.snapshot.admission.acknowledged_status_sequence == 5
	      && ack.snapshot.admission.acknowledgement_current && ack.snapshot.admission.mapped_acknowledgement_time_us == 9960
	      && ack.snapshot.admission.earliest_acknowledgement_time_us == 9958
	      && ack.snapshot.admission.latest_acknowledgement_time_us == 9962, "golden ACK independent arithmetic");
	f.physical.request_guard_context.validator_receive_time_us = 10010;
	const auto accepted = f.request();
	shape(accepted, {Kind::Operation}, 0, DiagnosticCode::RequestValidatedDisarmed, "golden request");
	cause(accepted, DecisionCode::ValidatedDisarmed, "golden request");
	check(accepted.records[0].report_sequence == 8 && accepted.records[0].common_decision
	      && accepted.records[0].common_decision->conservative_age_valid
	      && accepted.records[0].common_decision->conservative_age_us == 12
	      && accepted.records[0].correlation.ingress.packet_sequence == 1
	      && accepted.records[0].correlation.operation == DiagnosticOperationKind::Request,
	      "golden request arithmetic and correlation");
	f.physical.request_guard_context.validator_receive_time_us = 10011;
	const auto regressed = f.request(2, 8999);
	shape(regressed, {Kind::Operation, Kind::SelectorBarrier}, 0, DiagnosticCode::Rejected, "C07 producer regression");
	cause(regressed, DecisionCode::ProducerTimeRegressed, "C07 producer regression");
	check(regressed.records[0].report_sequence == 9 && regressed.records[1].report_sequence == 10,
	      "golden regression exact report IDs");
	check(regressed.records[0].snapshot.selection == selected && regressed.records[0].snapshot.latched,
	      "golden immutable preclose snapshot");
	// The existing guard calculates age before the regression predicate, but
	// latch(ProducerTimeRegressed) deliberately carries no age evidence. The
	// contract trace's age 14 is independently computed, never fabricated in
	// the actual common Decision retained by this adapter.
	const TimeUs earliest_creation = 8999 + 1000 - 2;
	check(f.physical.request_guard_context.validator_receive_time_us - earliest_creation == 14,
	      "golden producer regression independent age arithmetic");
	check(regressed.records[0].common_decision && !regressed.records[0].common_decision->conservative_age_valid
	      && regressed.records[0].common_decision->conservative_age_us == 0,
	      "golden actual producer-latch decision does not invent age evidence");
	check(regressed.snapshot.selection == disabled && regressed.snapshot.selector_epoch == 3
	      && regressed.snapshot.adapter.transition_epoch == 4, "golden final closure counters");
	check(!regressed.snapshot.admission.acknowledgement_current
	      && regressed.snapshot.session.gate == SessionGate::EstablishmentRequired, "golden final admission closure");
	check(accepted.snapshot.selection == selected && !accepted.snapshot.latched, "golden earlier result remains immutable");
}

void constructionAndDomain()
{
	for (unsigned variant = 0; variant < 5; ++variant) {
		auto settings = config();
		AdapterIdentity controller{11};
		DiagnosticIdentity identity{13, 17};

		switch (variant) {
		case 0: controller.controller_instance_id = 0; break;

		case 1: identity.owner_lifetime_id = 0; break;

		case 2: identity.fixture_run_id = 0; break;

		case 3: settings.timing.minimum_validity_us = settings.timing.maximum_validity_us + 1; break;

		case 4: settings.disarmed_policy = DisarmedPolicy::Reject; break;
		}

		WireAdapter adapter(settings, controller, DiagnosticOfflineTag{}, identity);
		const auto result = adapter.diagnosticTurn(sample(), DiagObserve{});
		check(result.code == DiagnosticCode::NotConfigured && result.record_count == 0
		      && !result.snapshot.initialized && !result.action_performed, "A01 inert construction " + std::to_string(variant));
		const auto before = stable(adapter.diagnosticSnapshot());
		check(!Peer::attach(adapter), "A01 invalid construction cannot attach operator");
		const auto closed = Peer::close(adapter, sample());
		check(closed.code == DiagnosticCode::NotConfigured && closed.record_count == 0
		      && stable(adapter.diagnosticSnapshot()) == before, "A01 lifecycle seam is inert when unconfigured");
	}

	Fixture f;
	f.ready();
	const auto before = stable(f.adapter.diagnosticSnapshot());
	CanonicalRequest output{7, 8, 9, 10, 11, 12, 13, 14, 15, Frame::Frd,
				Quantity::InertialAcceleration, ReferencePoint::Cg, Vec3f(3.F, 4.F, 5.F)};
	std::array<unsigned char, sizeof(output)> sentinel{};
	std::memcpy(sentinel.data(), &output, sizeof(output));
	const auto observe = f.adapter.observeTrustedState(f.physical);
	const auto poll = f.adapter.poll(f.physical);
	const auto access = f.adapter.getEligibleRequest(f.physical, output);
	const auto grant = adminGrant(f.adapter, f.physical, SupervisorAction::Establish);
	const auto session = f.adapter.processSession(f.physical, sessionPacket(f.adapter, 1), ingress().authentication, &grant.event);
	const auto clear = f.adapter.clearLatchedFault(f.physical, grant.event);
	const auto ack = f.adapter.processAcknowledgement(f.physical, sessionPacket(f.adapter, 3, 8960), ingress().authentication);
	const auto request = f.adapter.processRequest(f.physical, requestPacket(f.adapter), ingress().authentication);
	check(observe.code == AdapterCode::ApiDomainMismatch && poll.code == AdapterCode::ApiDomainMismatch
	      && access.code == AdapterCode::ApiDomainMismatch && observe.report_count == 0 && poll.report_count == 0
	      && access.report_count == 0, "A02 ordinary state APIs");
	check(session.code == AdminCode::ApiDomainMismatch && clear.code == AdminCode::ApiDomainMismatch
	      && session.report_count == 0 && clear.report_count == 0, "A02 ordinary admin APIs");
	check(ack.code == IngressCode::ApiDomainMismatch && request.code == IngressCode::ApiDomainMismatch
	      && ack.report_count == 0 && request.report_count == 0, "A02 ordinary ingress APIs");
	check(std::memcmp(sentinel.data(), &output, sizeof(output)) == 0, "A02 getter sentinel byte-identical");
	check(f.adapter.configured() && f.adapter.sessionSnapshot().gate == SessionGate::Established,
	      "A02 read-only ordinary accessors truthful");
	check(stable(f.adapter.diagnosticSnapshot()) == before, "A02 misuse does not mutate diagnostic state");
	WireAdapter flight(config(), {11});
	const auto flight_before = stable(flight.diagnosticSnapshot());
	check(!Peer::attach(flight), "A02 Flight cannot attach diagnostic operator");
	const auto flight_close = Peer::close(flight, sample());
	check(flight_close.code == DiagnosticCode::WrongDomain && flight_close.record_count == 0
	      && stable(flight.diagnosticSnapshot()) == flight_before, "A02 Flight lifecycle seam cannot mutate domain");
	const std::array<DiagnosticTurn, 10> turns{DiagObserve{}, DiagPoll{}, DiagAccess{}, DiagEnable{}, DiagDisable{},
			DiagEstablish{}, DiagRevoke{}, DiagClear{}, DiagAcknowledge{}, DiagRequest{}};

	for (const auto &turn : turns) {
		const auto result = flight.diagnosticTurn(sample(), turn);
		check(result.code == DiagnosticCode::WrongDomain && result.record_count == 0
		      && stable(flight.diagnosticSnapshot()) == flight_before, "A02 every typed operation on Flight");
	}
}

template<typename Report, typename Code>
void frozenMapperRejectsMismatch(Report report, Code mismatch, const std::string &name)
{
	WireStatusV1 output{};
	check(buildStatusV1(report, {1, 1, StatusDelivery::Directed}, output) == StatusBuildCode::Ok,
	      name + ": real Flight report is representable before mutation");
	std::array<unsigned char, sizeof(output)> before{};
	std::memcpy(before.data(), &output, sizeof(output));
	report.code = mismatch;
	check(buildStatusV1(report, {1, 1, StatusDelivery::Directed}, output) != StatusBuildCode::Ok
	      && std::memcmp(&output, before.data(), sizeof(output)) == 0, name + ": local mismatch cannot write frozen status");
}

void frozenMapperIsolation()
{
	// Real ordinary-domain reports first prove the fixture is otherwise valid;
	// only the new local mismatch code is changed for the negative case.
	WireAdapter flight(config(), {11});
	auto physical = sample();
	physical.request_guard_context.mode_state = ResearchModeState::Selected;
	const auto observed = flight.observeTrustedState(physical);
	check(observed.report_count > 0, "B03 actual Flight observation");

	if (observed.report_count == 0) { return; }

	frozenMapperRejectsMismatch(observed.reports[0], AdapterCode::ApiDomainMismatch, "B03 adapter mismatch");
	const SupervisorEvent grant{1, 1, observed.snapshot.transition_epoch, 11, SupervisorAction::Establish, true, {7, 100}};
	const DecodedSessionV1 packet{11, observed.snapshot.transition_epoch, 1, 7, 100, 1, 0, 0,
				      kWireProtocolVersionV1, 1, 1, 1, 0, {0, 0, 0, 0}};
	const auto established = flight.processSession(physical, packet, ingress().authentication, &grant);
	check(established.code == AdminCode::Completed && established.report_count > 0, "B03 actual Flight establishment");

	if (established.report_count == 0) { return; }

	frozenMapperRejectsMismatch(established.reports[established.report_count - 1], AdminCode::ApiDomainMismatch,
				    "B03 admin mismatch");
	physical.request_guard_context.validator_receive_time_us = 9965;
	const DecodedSessionV1 ack{11, established.snapshot.transition_epoch, 1, 7, 100, 2, 8960,
				   established.snapshot.barrier_report_sequence, kWireProtocolVersionV1, 1, 1, 3, 0, {0, 0, 0, 0}};
	const auto acknowledged = flight.processAcknowledgement(physical, ack, ingress().authentication);
	check(acknowledged.code == IngressCode::Completed && acknowledged.report_count > 0, "B03 actual Flight ACK");

	if (acknowledged.report_count == 0) { return; }

	frozenMapperRejectsMismatch(acknowledged.reports[acknowledged.report_count - 1], IngressCode::ApiDomainMismatch,
				    "B03 ingress mismatch");
}

void mutateMap(TrustedClockMap &map, unsigned field)
{
	switch (field) {
	case 0: map.valid = false; break;

	case 1: ++map.source; break;

	case 2: ++map.session; break;

	case 3: ++map.controller_instance_id; break;

	case 4: ++map.map_epoch; break;

	case 5: ++map.validator_minus_producer_us; break;

	case 6: ++map.uncertainty_us; break;

	case 7: ++map.valid_from_producer_us; break;

	case 8: --map.valid_until_producer_us; break;
	}
}

void bindingsAndEvents()
{
	for (unsigned field = 0; field < 16; ++field) {
		Fixture f;
		f.observe();
		auto grant = selectionGrant(f.adapter, f.physical, DiagnosticAction::Enable);
		auto &expected = grant.expected;

		switch (field) {
		case 0: ++expected.owner_lifetime_id; break;

		case 1: ++expected.fixture_run_id; break;

		case 2: ++expected.controller_instance_id; break;

		case 3: ++expected.operator_instance; break;

		case 4: ++expected.state_generation; break;

		case 5: ++expected.transition_epoch; break;

		case 6: ++expected.selector_epoch; break;

		default: mutateMap(expected.prospective_map, field - 7); break;
		}

		const auto result = f.adapter.diagnosticTurn(f.physical, DiagEnable{grant});
		shape(result, {Kind::Operation}, 0, DiagnosticCode::Rejected, "A05 binding " + std::to_string(field));
		check(!result.grant_consumed && !result.action_performed && result.snapshot.session.supervisor_floor == 0
		      && result.snapshot.selection == disabled, "A05 no authority consumed");
	}

	for (unsigned variant = 0; variant < 5; ++variant) {
		Fixture f;
		f.ready();
		auto grant = selectionGrant(f.adapter, f.physical, DiagnosticAction::Disable);
		grant.event_id = variant == 0 ? 2 : variant == 1 ? 1 : variant == 2 ? 0 : 4;

		if (variant == 4) { Peer::supervisor(f.adapter, maximum); grant.event_id = maximum; }

		const auto result = f.adapter.diagnosticTurn(f.physical, DiagDisable{grant});

		if (variant < 2) {
			shape(result, {Kind::Operation}, 0, DiagnosticCode::Rejected, "A06 old event");
			check(result.snapshot.selection == selected && !result.grant_consumed, "A06 replay retains selection");

		} else {
			terminal(result, "A06 stream fault");
		}
	}
}

void physicalStream()
{
	for (unsigned field = 0; field < 10; ++field) {
		Fixture f;
		f.ready();
		++f.physical.generation;

		if (field == 9) { f.physical.request_guard_context.clock_mapping_valid = false; }

		else { mutateMap(f.physical.map, field); }

		const auto result = f.observe();
		const auto name = "A16 map field " + std::to_string(field);

		if (field == 0 || field == 3 || field == 9) { terminal(result, name); }

		else {
			shape(result, {Kind::PhysicalBarrier, Kind::SelectorBarrier, Kind::Operation}, 2, DiagnosticCode::Observed, name);
			check(result.snapshot.selection == disabled && !result.snapshot.admission.acknowledgement_current, name + ": closed");
		}
	}

	for (unsigned variant = 0; variant < 6; ++variant) {
		Fixture f;
		f.ready();
		++f.physical.generation;

		if (variant < 3) {
			f.physical.request_guard_context.arm_state = variant == 0 ? ArmState::Armed : variant == 1 ? ArmState::Unknown : static_cast<ArmState>(255);

		} else {
			f.physical.request_guard_context.mode_state = variant == 3 ? ResearchModeState::Selected : variant == 4 ? ResearchModeState::Unknown :
					static_cast<ResearchModeState>(255);
		}

		const auto result = f.observe();

		if (variant == 2 || variant == 5) { terminal(result, "A18 invalid enum"); }

		else {
			invariant(result, "A18 physical arm/mode closure");
			check(result.record_count == 3 && result.records[0].kind == Kind::PhysicalBarrier
			      && result.records[1].kind == Kind::SelectorBarrier && result.snapshot.selection == disabled,
			      "A18 known/unknown closes before operation");
		}
	}

	for (unsigned variant = 0; variant < 3; ++variant) {
		Fixture f;
		f.ready();
		auto stale = selectionGrant(f.adapter, f.physical, DiagnosticAction::Enable);
		++f.physical.generation;

		if (variant == 0) { f.physical.map.valid = false; f.physical.request_guard_context.clock_mapping_valid = false; }

		if (variant == 1) { f.physical.request_guard_context.transport_connected = false; }

		if (variant == 2) { f.physical.request_guard_context.required_state_valid = false; }

		const auto result = f.adapter.diagnosticTurn(f.physical, DiagEnable{stale});
		shape(result, {Kind::PhysicalBarrier, Kind::SelectorBarrier, Kind::Operation}, 2, DiagnosticCode::Rejected, "A19/A22 safety prefix");
		check(!result.grant_consumed && !result.action_performed && result.snapshot.selection == disabled, "A22 stale grant keeps prefix");
		f.physical = sample(10011, 3);
		const auto restored = f.observe();
		check(restored.snapshot.selection == disabled && !restored.snapshot.admission.acknowledgement_current, "A19 restore does not select");
	}

	for (unsigned variant = 0; variant < 6; ++variant) {
		Fixture f;
		f.ready();
		// Begin generation 2 so the regression variant remains nonzero.
		f.physical.generation = 2;
		f.observe();

		switch (variant) {
		case 0: f.physical.request_guard_context.required_state_valid = false; break;

		case 1: ++f.physical.map.map_epoch; break;

		case 2: f.physical.generation = 4; break;

		case 3: f.physical.generation = 1; break;

		case 4: f.physical.observer_continuous = false; break;

		case 5: f.physical.generation = 0; break;
		}

		const auto result = f.observe();
		terminal(result, "A20 structural stream variant " + std::to_string(variant));
		check(result.snapshot.adapter.state_generation == 2, "A20 retains last valid generation");
	}
}

void admissionAndAccess()
{
	for (unsigned variant = 0; variant < 5; ++variant) {
		Fixture f;
		f.observe();
		f.enable();

		if (variant > 0) { f.establish(); }

		if (variant == 1) { f.disable(); }

		if (variant >= 3) { f.acknowledge(); ++f.physical.generation; }

		// Preserve the original stale-binding fixture, then isolate stale ACK
		// with a freshly observed generation and matching request binding.
		if (variant == 4) { f.observe(); }

		f.physical.request_guard_context.validator_receive_time_us = 10010;
		const auto result = f.request();
		invariant(result, "R14 missing/stale admission");
		check(result.code == DiagnosticCode::Rejected && !result.snapshot.latched, "R14 request never validates");

		if (variant == 0) { cause(result, IngressCode::SessionGateRejected, "R14 missing session first cause"); }

		if (variant == 1) { cause(result, DiagnosticLocalReason::SelectionRequired, "R14 disabled before invalidated session"); }

		if (variant == 2 || variant == 4) { cause(result, IngressCode::AcknowledgementRequired, "R14 absent or stale ACK first cause"); }

		if (variant == 3) { cause(result, IngressCode::BindingMismatch, "R14 old request binding precedes stale ACK check"); }
	}

	for (unsigned variant = 0; variant < 6; ++variant) {
		Fixture f;
		f.ready();
		auto facts = ingress();

		switch (variant) {
		case 0: ++facts.owner_lifetime_id; break;

		case 1: ++facts.fixture_run_id; break;

		case 2: facts.authentication.mavlink2 = false; break;

		case 3: facts.authentication.parser_authenticated = false; break;

		case 4: facts.authentication.configured_header_channel_source_binding = false; break;

		case 5: facts.authentication.configured_target_binding = false; break;
		}

		const auto rejected = f.adapter.diagnosticTurn(f.physical, DiagRequest{requestPacket(f.adapter, 2, 9001), facts});
		shape(rejected, {Kind::Operation}, 0, DiagnosticCode::Rejected, "R15 foreign ingress");
		const auto accepted = f.request(2, 9001);
		check(accepted.code == DiagnosticCode::RequestValidatedDisarmed, "R15 rejection never consumes request floor");
	}

	Fixture f;
	f.ready();
	++f.physical.generation;
	const auto changed = f.observe();
	check(changed.record_count == 2 && changed.snapshot.selection == selected
	      && !changed.snapshot.admission.acknowledgement_current, "A17 physical barrier retains selector, clears ACK");
	const auto missing_ack = f.request(2, 9001);
	cause(missing_ack, IngressCode::AcknowledgementRequired, "A17 new ACK required");

	for (unsigned variant = 0; variant < 2; ++variant) {
		Fixture access;
		access.ready();
		const DiagnosticTurn turn = variant == 0 ? DiagnosticTurn{DiagAccess{}} : DiagnosticTurn{DiagPoll{}};
		const auto disarmed = access.adapter.diagnosticTurn(access.physical, turn);
		invariant(disarmed, "A27 disarmed access/poll");
		check(disarmed.code == DiagnosticCode::NoEligibleRequest, "A27 validated request is never demand");
		++access.physical.generation;
		access.physical.request_guard_context.arm_state = ArmState::Armed;
		const auto armed = access.adapter.diagnosticTurn(access.physical, turn);
		invariant(armed, "A27 armed access/poll");
		check(armed.snapshot.selection == disabled && armed.code != DiagnosticCode::RequestValidatedDisarmed,
		      "A27 arm cannot promote old validation");
	}
}

void administrationAndLifecycle()
{
	Fixture f;
	f.ready();
	const auto first = f.disable();
	shape(first, {Kind::SelectorBarrier, Kind::Operation}, 1, DiagnosticCode::SelectionDisabled, "A10 disable");
	const auto second = f.disable();
	shape(second, {Kind::Operation}, 0, DiagnosticCode::AlreadyDisabled, "A10 idempotent disable");
	check(second.snapshot.adapter.transition_epoch == first.snapshot.adapter.transition_epoch
	      && second.snapshot.selector_epoch == first.snapshot.selector_epoch
	      && second.snapshot.session.supervisor_floor == first.snapshot.session.supervisor_floor + 1
	      && second.snapshot.session.wire_operation_floor == first.snapshot.session.wire_operation_floor,
	      "A10 idempotent counters retain wire floor");
	check(f.enable().code == DiagnosticCode::SelectionEnabled, "A14 re-enable requires explicit event");
	const auto repeated = f.establish();
	check(repeated.code == DiagnosticCode::Rejected && repeated.snapshot.session.wire_operation_floor == 2,
	      "A14 same prior session rejected without wire floor reset");

	for (unsigned variant = 0; variant < 3; ++variant) {
		Fixture admin;

		if (variant == 1) { admin.latch(); } else { admin.ready(); }

		DiagnosticResult result{};

		if (variant == 0) {
			result = admin.adapter.diagnosticTurn(admin.physical, DiagRevoke{sessionPacket(admin.adapter, 2), ingress(),
							      adminGrant(admin.adapter, admin.physical, SupervisorAction::Revoke)});
			shape(result, {Kind::SessionBarrier, Kind::Operation, Kind::SelectorBarrier}, 1, DiagnosticCode::SessionRevoked, "A11 revoke");

		} else {
			result = admin.adapter.diagnosticTurn(admin.physical, DiagClear{adminGrant(admin.adapter, admin.physical, SupervisorAction::ClearLatchedFault)});

			if (variant == 1) {
				shape(result, {Kind::SessionBarrier, Kind::Operation}, 1, DiagnosticCode::LatchCleared, "A11 clear latched");
				check(!result.snapshot.latched && result.snapshot.selection == disabled, "A11 clear retains disabled");

			} else {
				shape(result, {Kind::Operation}, 0, DiagnosticCode::Rejected, "A11 no-latch clear");
				cause(result, AdminCode::ClearNotLatched, "A11 no-latch clear");
				check(result.snapshot.selection == selected, "A11 rejected clear retains selection");
			}
		}
	}

	for (unsigned variant = 0; variant < 4; ++variant) {
		Fixture admin;
		admin.ready();
		auto grant = adminGrant(admin.adapter, admin.physical, variant == 1 ? SupervisorAction::ClearLatchedFault : SupervisorAction::Revoke);
		auto packet = sessionPacket(admin.adapter, 2);
		auto facts = ingress();

		if (variant == 0) { ++grant.expected.operator_instance; }

		if (variant == 1) { --grant.expected.transition_epoch; }

		if (variant == 2) { ++packet.session; }

		if (variant == 3) { facts.authentication.parser_authenticated = false; }

		const DiagnosticTurn turn = variant == 1 ? DiagnosticTurn{DiagClear{grant}} : DiagnosticTurn{DiagRevoke{packet, facts, grant}};
		const auto result = admin.adapter.diagnosticTurn(admin.physical, turn);
		shape(result, {Kind::Operation}, 0, DiagnosticCode::Rejected, "A12 rejected administration");
		check(result.snapshot.selection == selected && result.snapshot.admission.acknowledgement_current,
		      "A12 rejected admin preserves valid selection/ACK");
	}

	for (unsigned variant = 0; variant < 3; ++variant) {
		Fixture clear;
		clear.latch();
		++clear.physical.generation;

		if (variant < 2) { clear.physical.map.valid = false; clear.physical.request_guard_context.clock_mapping_valid = false; }

		else { clear.physical.request_guard_context.required_state_valid = false; }

		auto grant = adminGrant(clear.adapter, clear.physical, SupervisorAction::ClearLatchedFault);
		++grant.expected.transition_epoch;
		++grant.event.expected_transition_epoch;
		const auto result = clear.adapter.diagnosticTurn(clear.physical, DiagClear{grant});
		shape(result, {Kind::PhysicalBarrier, Kind::SessionBarrier, Kind::Operation}, 2, DiagnosticCode::LatchCleared, "A13 unhealthy clear");
		check(!result.snapshot.latched && result.snapshot.selection == disabled
		      && !result.snapshot.admission.acknowledgement_current, "A13 clear grants no readiness");
	}

	for (unsigned variant = 0; variant < 2; ++variant) {
		Fixture close;
		close.ready();
		const auto reason = variant == 0 ? DiagnosticCloseReason::OperatorDetach : DiagnosticCloseReason::OperatorLost;
		const auto result = Peer::close(close.adapter, close.physical, reason);
		shape(result, {Kind::SelectorBarrier, Kind::Operation}, 1, DiagnosticCode::SelectionDisabled, "A23 adapter lifecycle seam");
		check(!result.snapshot.operator_attached && !result.grant_consumed
		      && result.snapshot.session.supervisor_floor == 2 && !result.snapshot.admission.acknowledgement_current
		      && result.records[1].correlation.operation == DiagnosticOperationKind::LifecycleClose
		      && result.records[1].correlation.close_reason == reason, "A23 synchronous closure, no fabricated event");
		check(!Peer::attach(close.adapter, 19) && !Peer::attach(close.adapter, 18)
		      && Peer::attach(close.adapter, 20) && !Peer::attach(close.adapter, 21), "operator lifetime monotonic, no replacement");
		const auto again = Peer::close(close.adapter, close.physical, reason);
		shape(again, {Kind::Operation}, 0, DiagnosticCode::AlreadyDisabled, "A28 already disabled lifecycle");
		check(again.snapshot.selector_epoch == result.snapshot.selector_epoch
		      && again.snapshot.adapter.transition_epoch == result.snapshot.adapter.transition_epoch,
		      "A28 no invented selector transition");
	}

	Fixture missing;
	terminal(Peer::close(missing.adapter, std::nullopt), "A24 missing lifecycle receipt");
}

void regressionAndFourRecords()
{
	for (unsigned variant = 0; variant < 4; ++variant) {
		Fixture f;
		f.ready();
		f.physical.request_guard_context.validator_receive_time_us = 10009;
		DiagnosticTurn turn{DiagPoll{}};

		if (variant == 1) { turn = DiagRequest{requestPacket(f.adapter, 2, 9001), ingress()}; }

		if (variant == 2) { turn = DiagEnable{selectionGrant(f.adapter, f.physical, DiagnosticAction::Enable)}; }

		if (variant == 3) {
			auto grant = adminGrant(f.adapter, f.physical, SupervisorAction::ClearLatchedFault);
			++grant.expected.transition_epoch;
			++grant.expected.selector_epoch;
			++grant.event.expected_transition_epoch;
			turn = DiagClear{grant};
		}

		const auto result = f.adapter.diagnosticTurn(f.physical, turn);
		invariant(result, "A21 regressed validator");
		check(result.records[0].kind == Kind::SelectorBarrier && result.snapshot.selection == disabled,
		      "A21 prefix closes selection");
		check(variant == 3 ? result.code == DiagnosticCode::LatchCleared && !result.snapshot.latched : result.snapshot.latched,
		      "A21 only authorized disarmed clear recovers latch");
	}

	for (unsigned variant = 0; variant < 2; ++variant) {
		Fixture f;
		f.ready();
		++f.physical.generation;
		auto grant = adminGrant(f.adapter, f.physical, variant == 0 ? SupervisorAction::ClearLatchedFault : SupervisorAction::Revoke);
		grant.expected.transition_epoch += variant == 0 ? 2 : 1;
		grant.expected.selector_epoch += variant == 0 ? 1 : 0;
		grant.event.expected_transition_epoch = grant.expected.transition_epoch;
		auto packet = sessionPacket(f.adapter, 2);
		++packet.transition_epoch;

		if (variant == 0) { f.physical.request_guard_context.validator_receive_time_us = 10009; }

		const DiagnosticTurn turn = variant == 0 ? DiagnosticTurn{DiagClear{grant}} : DiagnosticTurn{DiagRevoke{packet, ingress(), grant}};
		const auto result = f.adapter.diagnosticTurn(f.physical, turn);

		if (variant == 0) {
			shape(result, {Kind::PhysicalBarrier, Kind::SelectorBarrier, Kind::SessionBarrier, Kind::Operation}, 3,
			      DiagnosticCode::LatchCleared, "C06 P,S,B,O");

		} else {
			shape(result, {Kind::PhysicalBarrier, Kind::SessionBarrier, Kind::Operation, Kind::SelectorBarrier}, 2,
			      DiagnosticCode::SessionRevoked, "C06 P,B,O,S");
		}

		check(result.snapshot.adapter.transition_epoch == 6 && result.snapshot.selector_epoch == 3
		      && result.snapshot.selection == disabled && result.grant_consumed && result.action_performed,
		      "C06 exact counter deltas and action");
	}
}

void malformedAuthorityAndEnvelopes()
{
	{
		Fixture f;
		f.ready();
		Peer::supervisor(f.adapter, maximum);
		const auto before = f.adapter.diagnosticSnapshot();
		++f.physical.generation;
		f.physical.request_guard_context.validator_receive_time_us = 10009;
		auto grant = selectionGrant(f.adapter, f.physical, DiagnosticAction::Disable);
		grant.event_id = maximum;
		const auto exhausted = f.adapter.diagnosticTurn(f.physical, DiagDisable{grant});
		terminal(exhausted, "supervisor MAX reservation precedes physical and clock mutation", true);
		check(exhausted.snapshot.adapter.state_generation == before.adapter.state_generation
		      && exhausted.snapshot.adapter.transition_epoch == before.adapter.transition_epoch
		      && exhausted.snapshot.selector_epoch == before.selector_epoch
		      && exhausted.snapshot.latched == before.latched
		      && exhausted.snapshot.rejection_count == before.rejection_count
		      && exhausted.snapshot.session.supervisor_floor == maximum,
		      "supervisor MAX preserves generation, epochs, latch and counters before poll");
		const auto repeated = f.adapter.diagnosticTurn(f.physical, DiagDisable{grant});
		check(repeated.code == DiagnosticCode::Terminal && repeated.record_count == 0
		      && stable(repeated.snapshot) == stable(exhausted.snapshot), "supervisor MAX emits sentinel only once");
	}

	// Invalid trusted authority can terminalize after genuine physical/selector
	// prefix records. No requested operation or consumed event is invented.
	for (unsigned variant = 0; variant < 2; ++variant) {
		Fixture f;
		f.ready();
		++f.physical.generation;

		if (variant == 1) { f.physical.request_guard_context.required_state_valid = false; }

		auto grant = selectionGrant(f.adapter, f.physical, DiagnosticAction::Disable);
		grant.event_id = 0;
		const auto result = f.adapter.diagnosticTurn(f.physical, DiagDisable{grant});
		invariant(result, "malformed authority after prefix");
		const uint8_t terminal_index = variant == 0 ? 1 : 2;
		check(result.code == DiagnosticCode::Terminal && result.snapshot.terminal
		      && result.record_count == terminal_index + 1 && !result.has_operation_record
		      && result.operation_record_index == 0 && !result.grant_consumed && !result.action_performed,
		      "malformed authority prefix has no requested operation");
		check(result.records[0].kind == Kind::PhysicalBarrier
		      && result.records[terminal_index].kind == Kind::Terminal
		      && result.records[terminal_index].correlation.operation == DiagnosticOperationKind::Disable
		      && result.snapshot.session.supervisor_floor == 2 && result.snapshot.selection == DiagnosticSelectionState::Faulted
		      && result.snapshot.session.gate == SessionGate::Revoked && !result.snapshot.admission.acknowledgement_current,
		      "malformed authority preserves chronological prefix and terminal closure");
		check(std::holds_alternative<AdminCode>(result.records[terminal_index].cause)
		      && std::get<AdminCode>(result.records[terminal_index].cause) == AdminCode::SupervisorStreamFault,
		      "malformed authority terminal cause remains typed");

		if (variant == 1) {
			check(result.records[1].kind == Kind::SelectorBarrier && result.records[1].snapshot.selection == disabled,
			      "malformed authority retains safety closure before terminal");
		}
	}

	for (unsigned variant = 0; variant < 4; ++variant) {
		Fixture f;
		f.observe();
		DiagnosticTurn turn{};

		if (variant == 0) {
			auto grant = selectionGrant(f.adapter, f.physical, DiagnosticAction::Enable);
			grant.action = static_cast<DiagnosticAction>(255);
			turn = DiagEnable{grant};

		} else {
			auto grant = adminGrant(f.adapter, f.physical, SupervisorAction::Establish);

			if (variant == 1) { grant.event.action = static_cast<SupervisorAction>(255); }

			if (variant == 2) { grant.event.has_session_binding = false; }

			if (variant == 3) { grant.event.expected_state_generation = 0; }

			turn = DiagEstablish{sessionPacket(f.adapter, 1), ingress(), grant};
		}

		terminal(f.adapter.diagnosticTurn(f.physical, turn), "invalid selection action/admin shape " + std::to_string(variant));
	}

	// ACK and Establish independently exercise each envelope field. Establish
	// has no prior wire floor, so operation sequence zero must fail on shape.
	for (unsigned operation = 0; operation < 2; ++operation) {
		for (unsigned field = 0; field < 23; ++field) {
			Fixture f;
			f.observe();
			f.enable();

			if (operation == 0) { f.establish(); f.physical.request_guard_context.validator_receive_time_us = 9965; }

			auto packet = sessionPacket(f.adapter, operation == 0 ? 3 : 1, operation == 0 ? 8960 : 0);
			auto facts = ingress();

			switch (field) {
			case 0: packet.protocol_version = 1; break;

			case 1: ++packet.source; break;

			case 2: ++packet.session; break;

			case 3: ++packet.controller_instance_id; break;

			case 4: ++packet.transition_epoch; break;

			case 5: ++packet.clock_mapping_epoch; break;

			case 6: packet.target_system = 0; break;

			case 7: packet.target_component = 0; break;

			case 8: packet.flags = 1; break;

			case 9: case 10: case 11: case 12: packet.reserved[field - 9] = 1; break;

			case 13: packet.operation = operation == 0 ? 1 : 3; break;

			case 14: packet.operation_sequence = 0; break;

			case 15: packet.producer_acknowledgement_monotonic_us = operation == 0 ? 0 : 1; break;

			case 16: packet.acknowledged_status_sequence = operation == 0 ? 0 : 1; break;

			case 17: ++facts.owner_lifetime_id; break;

			case 18: ++facts.fixture_run_id; break;

			case 19: facts.authentication.mavlink2 = false; break;

			case 20: facts.authentication.parser_authenticated = false; break;

			case 21: facts.authentication.configured_header_channel_source_binding = false; break;

			case 22: facts.authentication.configured_target_binding = false; break;
			}

			const auto before = f.adapter.diagnosticSnapshot();
			const DiagnosticTurn turn = operation == 0 ? DiagnosticTurn{DiagAcknowledge{packet, facts}}
						    : DiagnosticTurn{DiagEstablish{packet, facts, adminGrant(f.adapter, f.physical, SupervisorAction::Establish)}};
			const auto result = f.adapter.diagnosticTurn(f.physical, turn);
			const auto name = std::string(operation == 0 ? "ACK" : "Establish") + " envelope field " + std::to_string(field);
			shape(result, {Kind::Operation}, 0, DiagnosticCode::Rejected, name);
			check(!result.action_performed && !result.snapshot.admission.acknowledgement_current
			      && result.snapshot.selection == selected && result.snapshot.session.gate == before.session.gate
			      && result.snapshot.session.wire_operation_floor == before.session.wire_operation_floor
			      && !result.records[0].common_decision, name + ": rejected before guard action");
			check(result.grant_consumed == (operation == 1)
			      && result.snapshot.session.supervisor_floor == before.session.supervisor_floor + operation,
			      name + ": event consumption distinct from payload rejection");
		}
	}
}

void distinctSessionRecovery()
{
	Fixture f;
	f.latch();
	const auto cleared = f.adapter.diagnosticTurn(f.physical,
			     DiagClear{adminGrant(f.adapter, f.physical, SupervisorAction::ClearLatchedFault)});
	check(cleared.code == DiagnosticCode::LatchCleared, "A14 clear setup");
	check(f.enable().code == DiagnosticCode::SelectionEnabled, "A14 explicit enable after clear");
	const auto old_session = f.establish();
	check(old_session.code == DiagnosticCode::Rejected && old_session.snapshot.session.wire_operation_floor == 2,
	      "A14 clear cannot permit prior session or reset wire floor");
	f.disable();
	++f.physical.generation;
	f.physical.map.session = 101;
	f.physical.map.map_epoch = 2;
	f.physical.map.validator_minus_producer_us = 2000;
	const auto staged = f.observe();
	check(staged.snapshot.selection == disabled, "A15 stage prospective distinct session while Disabled");
	const auto enabled = f.enable();
	check(enabled.code == DiagnosticCode::SelectionEnabled, "A15 enable exact prospective map");
	auto packet = sessionPacket(f.adapter, 1);
	packet.session = 101;
	packet.clock_mapping_epoch = 2;
	const auto established = f.adapter.diagnosticTurn(f.physical,
				 DiagEstablish{packet, ingress(), adminGrant(f.adapter, f.physical, SupervisorAction::Establish)});
	shape(established, {Kind::SessionBarrier, Kind::Operation}, 1, DiagnosticCode::SessionEstablished, "A15 establish staged session");
	check(established.snapshot.selection == selected && established.snapshot.selector_epoch == enabled.snapshot.selector_epoch
	      && established.snapshot.session.last_successful_session.session == 101
	      && established.snapshot.session.wire_operation_floor == 3 && !established.snapshot.admission.acknowledgement_current,
	      "A15 session barrier does not rebind map or selector");
	packet = sessionPacket(f.adapter, 3, 8020);
	packet.session = 101;
	packet.clock_mapping_epoch = 2;
	f.physical.request_guard_context.validator_receive_time_us = 10025;
	const auto acknowledged = f.adapter.diagnosticTurn(f.physical, DiagAcknowledge{packet, ingress()});
	check(acknowledged.code == DiagnosticCode::Acknowledged && acknowledged.snapshot.session.wire_operation_floor == 4,
	      "A15 distinct session requires new ACK and retains wire replay stream");
	auto request = requestPacket(f.adapter, 1, 8030);
	request.wire.session_id = 101;
	request.wire.clock_mapping_epoch = 2;
	f.physical.request_guard_context.validator_receive_time_us = 10040;
	const auto validated = f.adapter.diagnosticTurn(f.physical, DiagRequest{request, ingress()});
	shape(validated, {Kind::Operation}, 0, DiagnosticCode::RequestValidatedDisarmed, "A15 reset request and producer floors");
	cause(validated, DecisionCode::ValidatedDisarmed, "A15 distinct session validation");
}

void capacity()
{
	// C01 four reserved shapes: P,S,O; O,S producer latch (physical would clear
	// ACK); P,S,B,O clear; P,B,O,S revoke. Reservation includes possible closure.
	for (unsigned shape_id = 0; shape_id < 4; ++shape_id) {
		const uint64_t needed = shape_id == 0 ? 3 : shape_id == 1 ? 2 : 4;

		for (unsigned capacity_case = 0; capacity_case < 3; ++capacity_case) {
			Fixture f;
			f.ready();
			const auto before = f.adapter.diagnosticSnapshot();
			const uint64_t available = capacity_case == 0 ? 0 : capacity_case == 1 ? needed : needed - 1;
			Peer::counters(f.adapter, maximum - 1 - available, before.adapter.transition_epoch, before.selector_epoch);
			DiagnosticTurn turn{DiagObserve{}};

			if (shape_id != 1) { ++f.physical.generation; }

			if (shape_id == 0) { f.physical.request_guard_context.required_state_valid = false; }

			if (shape_id == 1) { turn = DiagRequest{requestPacket(f.adapter, 2, 8999), ingress()}; }

			if (shape_id >= 2) {
				auto grant = adminGrant(f.adapter, f.physical, shape_id == 2 ? SupervisorAction::ClearLatchedFault : SupervisorAction::Revoke);
				grant.expected.transition_epoch += shape_id == 2 ? 2 : 1;
				grant.expected.selector_epoch += shape_id == 2 ? 1 : 0;
				grant.event.expected_transition_epoch = grant.expected.transition_epoch;
				auto packet = sessionPacket(f.adapter, 2);
				++packet.transition_epoch;

				if (shape_id == 2) { f.physical.request_guard_context.validator_receive_time_us = 10009; turn = DiagClear{grant}; }

				else { turn = DiagRevoke{packet, ingress(), grant}; }
			}

			const auto result = f.adapter.diagnosticTurn(f.physical, turn);
			const auto name = "C01 shape " + std::to_string(shape_id) + " capacity " + std::to_string(capacity_case);

			if (capacity_case == 1) {
				invariant(result, name);
				check(!result.snapshot.terminal && result.record_count == needed
				      && result.snapshot.adapter.last_report_sequence == maximum - 1, name + ": exact capacity");

				if (shape_id == 0) { shape(result, {Kind::PhysicalBarrier, Kind::SelectorBarrier, Kind::Operation}, 2, DiagnosticCode::Rejected, name); }

				if (shape_id == 1) { shape(result, {Kind::Operation, Kind::SelectorBarrier}, 0, DiagnosticCode::Rejected, name); }

				if (shape_id == 2) { shape(result, {Kind::PhysicalBarrier, Kind::SelectorBarrier, Kind::SessionBarrier, Kind::Operation}, 3, DiagnosticCode::LatchCleared, name); }

				if (shape_id == 3) { shape(result, {Kind::PhysicalBarrier, Kind::SessionBarrier, Kind::Operation, Kind::SelectorBarrier}, 2, DiagnosticCode::SessionRevoked, name); }

			} else {
				terminal(result, name, true);
				check(result.snapshot.adapter.state_generation == before.adapter.state_generation
				      && result.snapshot.adapter.transition_epoch == before.adapter.transition_epoch
				      && result.snapshot.selector_epoch == before.selector_epoch
				      && result.snapshot.session.supervisor_floor == before.session.supervisor_floor
				      && result.snapshot.latched == before.latched, name + ": reservation before poll/admin");
				const auto repeated = f.observe();
				check(repeated.code == DiagnosticCode::Terminal && repeated.record_count == 0
				      && repeated.snapshot.adapter.last_report_sequence == maximum, "C04 once-only terminal sentinel");
			}
		}
	}

	for (uint64_t needed = 1; needed <= 3; ++needed) {
		for (unsigned short_case = 0; short_case < 2; ++short_case) {
			Fixture f;
			f.ready();
			const uint64_t epoch = maximum - needed + short_case;
			Peer::counters(f.adapter, 8, epoch, 2);
			DiagnosticTurn turn{DiagObserve{}};

			if (needed == 1) { turn = DiagDisable{selectionGrant(f.adapter, f.physical, DiagnosticAction::Disable)}; }

			else {
				++f.physical.generation;

				if (needed == 2) { f.physical.request_guard_context.required_state_valid = false; }

				else {
					auto grant = adminGrant(f.adapter, f.physical, SupervisorAction::Revoke);
					++grant.expected.transition_epoch;
					++grant.event.expected_transition_epoch;
					auto packet = sessionPacket(f.adapter, 2);
					++packet.transition_epoch;
					turn = DiagRevoke{packet, ingress(), grant};
				}
			}

			const auto result = f.adapter.diagnosticTurn(f.physical, turn);

			if (short_case) { terminal(result, "C02 transition one short", true); }

			else {
				invariant(result, "C02 exact transition capacity");
				check(!result.snapshot.terminal && result.snapshot.adapter.transition_epoch == maximum,
				      "C02 reaches final epoch without wrap");
			}
		}
	}

	Fixture selector;
	selector.observe();
	Peer::counters(selector.adapter, 2, 1, maximum - 1);
	const auto last = selector.enable();
	check(last.code == DiagnosticCode::SelectionEnabled && last.snapshot.selector_epoch == maximum,
	      "C03 last representable selector change");
	terminal(selector.disable(), "C03 selector overflow", true);
	Fixture idempotent;
	idempotent.observe();
	Peer::counters(idempotent.adapter, 2, maximum, maximum);
	const auto no_change = idempotent.disable();
	check(no_change.code == DiagnosticCode::AlreadyDisabled && !no_change.snapshot.terminal
	      && no_change.snapshot.selector_epoch == maximum && no_change.snapshot.adapter.transition_epoch == maximum,
	      "C03 idempotent disable needs no epoch reservation");
	Fixture invalid;
	invalid.physical.generation = 0;
	const auto first_fault = invalid.observe();
	terminal(first_fault, "C05 invalid first sample");
	check(first_fault.records[0].report_sequence == 1 && !first_fault.records[0].state_reference_valid
	      && first_fault.snapshot.adapter.transition_epoch == 0 && first_fault.snapshot.selector_epoch == 0
	      && first_fault.snapshot.adapter.state_generation == 0, "C05 no invented first state");
}
} // namespace

int main()
{
	golden();
	constructionAndDomain();
	frozenMapperIsolation();
	bindingsAndEvents();
	physicalStream();
	admissionAndAccess();
	administrationAndLifecycle();
	regressionAndFourRecords();
	malformedAuthorityAndEnvelopes();
	distinctSessionRecovery();
	capacity();
	std::cout << "Diagnostic adapter: " << checks << " checks, " << failures << " failures\n";
	return failures == 0 ? 0 : 1;
}
