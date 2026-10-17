#include "WireAdapter.hpp"

#include <array>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>

using namespace research_acceleration;

namespace research_acceleration
{
// Test seam is restricted to local counters/floors and stable guard identity.
struct WireAdapterSessionTestPeer {
	static void seed(WireAdapter &adapter, uint64_t epoch, uint64_t report,
			 bool has_wire_floor, uint64_t wire_floor,
			 bool has_supervisor_floor, uint64_t supervisor_floor)
	{
		adapter.snapshot_.transition_epoch = epoch;
		adapter.snapshot_.last_report_sequence = report;
		adapter.session_snapshot_.has_wire_operation_floor = has_wire_floor;
		adapter.session_snapshot_.wire_operation_floor = wire_floor;
		adapter.session_snapshot_.has_supervisor_floor = has_supervisor_floor;
		adapter.session_snapshot_.supervisor_floor = supervisor_floor;
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
constexpr SourceId kSource = 21;
constexpr SessionId kSessionA = 31;
constexpr SessionId kSessionB = 32;
constexpr uint64_t kMapEpoch = 41;
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

ContractConfig config()
{
	return {kWireProtocolVersionV1, kSource, DisarmedPolicy::ValidateOnly,
		{Vec3f(-10.F, -10.F, -10.F), Vec3f(10.F, 10.F, 10.F), 12.F},
		{10, 100, 50, 10, 5}, 0.01F};
}

TrustedStateSample state(uint64_t generation = kGeneration, TimeUs time = kTime,
			 SessionId map_session = kSessionA)
{
	TrustedStateSample result{};
	result.generation = generation;
	result.observer_continuous = true;
	result.request_guard_context = {time, ArmState::Disarmed, ResearchModeState::Selected, true, true, true};
	result.map = {true, kSource, map_session, kController, kMapEpoch, 0, 2, 0, 2000};
	return result;
}

DecodedSessionV1 packet(uint64_t epoch, SessionId session, uint64_t sequence, uint8_t operation = 1)
{
	return {kController, epoch, kMapEpoch, kSource, session, sequence, 101, 102,
		kWireProtocolVersionV1, 1, 1, operation, 0, {0, 0, 0, 0}};
}

SessionIngressVerdict ingress(bool authenticated = true)
{
	return {true, authenticated, true, true};
}

SupervisorEvent grant(uint64_t event, uint64_t generation, uint64_t epoch,
		      SupervisorAction action, SessionId session, bool has_binding = true)
{
	return {event, generation, epoch, kController, action, has_binding,
		has_binding ? TrustedSession{kSource, session} : TrustedSession{0, 0}};
}

template<typename T>
std::array<unsigned char, sizeof(T)> bytes(const T &value)
{
	std::array<unsigned char, sizeof(T)> result{};
	std::memcpy(result.data(), &value, sizeof(T));
	return result;
}

bool zeroCorrelation(const AdminCorrelation &value)
{
	return !value.has_packet && value.packet_source == 0 && value.packet_session == 0
	       && value.packet_operation_sequence == 0 && !value.has_supervisor_event
	       && value.supervisor_event_id == 0 && !value.supervisor_event_consumed;
}

bool zeroReport(const AdminReport &value)
{
	return value.report_sequence == 0 && value.transition_epoch == 0 && value.validator_time_us == 0
	       && value.kind == AdminReportKind::ExogenousBarrier && value.code == AdminCode::Observed
	       && !value.has_s1_decision && zeroCorrelation(value.correlation);
}

void checkClosed(const AdminResult &result, const std::string &name)
{
	check(result.admission_closed && result.report_count <= 3, name + "_closed_bounded");

	for (uint8_t index = result.report_count; index < 3; ++index) {
		check(zeroReport(result.reports[index]), name + "_unused_zero");
	}
}

AdminResult establishA(WireAdapter &adapter)
{
	auto first = state();
	auto request = packet(1, kSessionA, 0);
	auto event = grant(1, kGeneration, 1, SupervisorAction::Establish, kSessionA);
	const auto result = adapter.processSession(first, request, ingress(), &event);
	checkClosed(result, "admin001");
	check(result.code == AdminCode::Completed && result.report_count == 3
	      && result.reports[0].report_sequence == 1 && result.reports[0].kind == AdminReportKind::ExogenousBarrier
	      && result.reports[0].transition_epoch == 1 && zeroCorrelation(result.reports[0].correlation)
	      && result.reports[1].report_sequence == 2 && result.reports[1].kind == AdminReportKind::AdministrativeBarrier
	      && result.reports[1].transition_epoch == 2 && zeroCorrelation(result.reports[1].correlation)
	      && result.reports[2].report_sequence == 3 && result.reports[2].kind == AdminReportKind::Operation
	      && result.reports[2].has_s1_decision && result.action_performed
	      && result.action_kind == AdminActionKind::EstablishTrustedSession,
	      "admin001_three_report_establish");
	check(result.snapshot.state_generation == kGeneration && result.snapshot.transition_epoch == 2
	      && result.snapshot.barrier_report_sequence == 2 && result.snapshot.last_report_sequence == 3
	      && result.session.gate == SessionGate::Established && result.session.has_wire_operation_floor
	      && result.session.wire_operation_floor == 0 && result.session.has_supervisor_floor
	      && result.session.supervisor_floor == 1 && result.correlation.supervisor_event_consumed,
	      "admin001_snapshot_floors");
	return result;
}

void goldenAndRebind()
{
	WireAdapter adapter(config(), {kController});
	establishA(adapter);
	auto revoke = packet(2, kSessionA, 1, 2);
	auto bad_auth = grant(2, kGeneration, 2, SupervisorAction::Revoke, kSessionA);
	const auto rejected = adapter.processSession(state(), revoke, ingress(false), &bad_auth);
	checkClosed(rejected, "admin003_bad_auth");
	check(rejected.code == AdminCode::IngressRejected && rejected.report_count == 1
	      && rejected.reports[0].report_sequence == 4 && rejected.session.supervisor_floor == 2
	      && rejected.session.wire_operation_floor == 0 && !rejected.action_performed,
	      "admin003_event_consumed_wire_floor_unchanged");
	auto good_revoke = grant(3, kGeneration, 2, SupervisorAction::Revoke, kSessionA);
	const auto revoked = adapter.processSession(state(), revoke, ingress(), &good_revoke);
	check(revoked.code == AdminCode::Completed && revoked.report_count == 2
	      && revoked.reports[0].report_sequence == 5 && revoked.reports[0].kind == AdminReportKind::AdministrativeBarrier
	      && revoked.reports[0].transition_epoch == 3 && revoked.reports[1].report_sequence == 6
	      && revoked.action_performed && revoked.action_kind == AdminActionKind::Revoke
	      && !revoked.has_action_s1_decision && revoked.session.gate == SessionGate::Revoked
	      && revoked.session.wire_operation_floor == 1 && revoked.session.supervisor_floor == 3,
	      "admin003_revoke_void_action");

	auto rebind = state(kGeneration + 1, kTime + 10, kSessionB);
	const auto ordinary = adapter.poll(rebind);
	check(ordinary.report_count == 2 && ordinary.snapshot.transition_epoch == 4
	      && adapter.sessionSnapshot().gate == SessionGate::Revoked, "session_rebind_ordinary_gate_revoke");
	auto no_session = packet(4, kSessionB, 2, 2);
	auto e4 = grant(4, kGeneration + 1, 4, SupervisorAction::Revoke, kSessionB);
	const auto no_established = adapter.processSession(rebind, no_session, ingress(), &e4);
	check(no_established.code == AdminCode::NoEstablishedSession && !no_established.action_performed
	      && no_established.session.supervisor_floor == 4 && no_established.session.wire_operation_floor == 1,
	      "session_rebind_requires_new_establish");
	auto establish_b = packet(4, kSessionB, 2);
	auto e5 = grant(5, kGeneration + 1, 4, SupervisorAction::Establish, kSessionB);
	const auto established_b = adapter.processSession(rebind, establish_b, ingress(), &e5);
	check(established_b.code == AdminCode::Completed && established_b.report_count == 2
	      && established_b.snapshot.transition_epoch == 5 && established_b.session.gate == SessionGate::Established
	      && established_b.session.wire_operation_floor == 2, "admin004_no_invented_generation");
}

void packetAndEventFailures()
{
	auto malformed = packet(1, kSessionA, 0);
	malformed.flags = 1;
	auto e1 = grant(1, kGeneration, 1, SupervisorAction::Establish, kSessionA);
	WireAdapter adapter(config(), {kController});
	const auto bad_packet = adapter.processSession(state(), malformed, ingress(), &e1);
	check(bad_packet.code == AdminCode::MalformedPacket && bad_packet.report_count == 2
	      && bad_packet.correlation.supervisor_event_consumed && bad_packet.session.supervisor_floor == 1,
	      "event_consumes_before_malformed_packet");

	auto duplicate = grant(1, kGeneration, 1, SupervisorAction::Establish, kSessionA);
	const auto duplicate_result = adapter.processSession(state(), packet(1, kSessionA, 0), ingress(), &duplicate);
	check(duplicate_result.code == AdminCode::SupervisorDuplicate && !duplicate_result.correlation.supervisor_event_consumed,
	      "event_duplicate_no_floor_move");

	WireAdapter unsupported_adapter(config(), {kController});
	auto op3 = packet(1, kSessionA, 0, 3);
	const auto unsupported = unsupported_adapter.processSession(state(), op3, ingress(), &e1);
	check(unsupported.code == AdminCode::UnsupportedOperation && unsupported.session.has_supervisor_floor
	      && unsupported.session.supervisor_floor == 1 && !unsupported.session.has_wire_operation_floor,
	      "operation_three_ignored_ack_fields_but_consumes_event");
	op3.producer_acknowledgement_monotonic_us = kMaximum;
	op3.acknowledged_status_sequence = kMaximum;
	WireAdapter ack_fields(config(), {kController});
	const auto ignored = ack_fields.processSession(state(), op3, ingress(), &e1);
	check(ignored.code == AdminCode::UnsupportedOperation && ignored.session.supervisor_floor == 1,
	      "ack_fields_do_not_change_operation_three");

	WireAdapter missing(config(), {kController});
	const auto missing_result = missing.processSession(state(), packet(1, kSessionA, 0), ingress(), nullptr);
	check(missing_result.code == AdminCode::SupervisorMissing && !missing_result.session.has_supervisor_floor,
	      "missing_grant_nonconsuming");

	WireAdapter stream(config(), {kController});
	auto gap = grant(2, kGeneration, 1, SupervisorAction::Establish, kSessionA);
	const auto stream_fault = stream.processSession(state(), packet(1, kSessionA, 0), ingress(), &gap);
	check(stream_fault.code == AdminCode::SupervisorStreamFault && stream_fault.report_count == 1
	      && stream_fault.snapshot.terminal && stream_fault.reports[0].kind == AdminReportKind::Terminal,
	      "gap_terminal_before_barrier");
	check(stream.poll(state()).code == AdapterCode::TrustedStateFault, "terminal_cross_api_mapping");

	WireAdapter max_floor(config(), {kController});
	WireAdapterSessionTestPeer::seed(max_floor, 0, 0, false, 0, true, kMaximum);
	const auto after_max = max_floor.clearLatchedFault(state(), grant(1, kGeneration, 1,
			       SupervisorAction::ClearLatchedFault, 0, false));
	check(after_max.code == AdminCode::SupervisorStreamFault && after_max.snapshot.terminal,
	      "supervisor_floor_maximum_has_no_successor");
}

void clearAndPrecedence()
{
	WireAdapter unlatched(config(), {kController});
	auto clear = grant(1, kGeneration, 1, SupervisorAction::ClearLatchedFault, 0, false);
	const auto no_latch = unlatched.clearLatchedFault(state(), clear);
	check(no_latch.code == AdminCode::ClearNotLatched && no_latch.report_count == 2
	      && no_latch.correlation.supervisor_event_consumed && !no_latch.action_performed,
	      "clear_unlatched_never_calls_s1_clear");

	WireAdapter latched(config(), {kController});
	latched.poll(state(kGeneration, kTime));
	latched.poll(state(kGeneration, kTime - 1));
	auto clear_latch = grant(1, kGeneration, 1, SupervisorAction::ClearLatchedFault, 0, false);
	const auto cleared = latched.clearLatchedFault(state(kGeneration, kTime - 1), clear_latch);
	check(cleared.code == AdminCode::Completed && cleared.action_performed
	      && cleared.action_kind == AdminActionKind::ClearLatch && cleared.has_action_s1_decision
	      && cleared.prestate_s1_decision.latched && cleared.session.gate == SessionGate::EstablishmentRequired,
	      "clear_latched_validator_regression");

	WireAdapter structural(config(), {kController});
	auto bad = state();
	bad.map.controller_instance_id++;
	auto zero = grant(0, kGeneration, 1, SupervisorAction::ClearLatchedFault, 0, false);
	const auto primary = structural.clearLatchedFault(bad, zero);
	check(primary.code == AdminCode::TrustedStateFault && !primary.session.has_supervisor_floor,
	      "structural_fault_precedes_stream");
}

void predicateAndShapeOrdering()
{
	auto establish_packet = packet(1, kSessionA, 0);

	for (auto malformed : {
		     SupervisorEvent{1, kGeneration, 1, 0, SupervisorAction::Establish, true, {kSource, kSessionA}},
		     SupervisorEvent{1, 0, 1, kController, SupervisorAction::Establish, true, {kSource, kSessionA}},
		     SupervisorEvent{1, kGeneration, 0, kController, SupervisorAction::Establish, true, {kSource, kSessionA}},
		     SupervisorEvent{1, kGeneration, 1, kController, SupervisorAction::Establish, false, {0, 0}}
	     }) {
		WireAdapter adapter(config(), {kController});
		const auto result = adapter.processSession(state(), establish_packet, ingress(), &malformed);
		check(result.code == AdminCode::SupervisorStreamFault && result.report_count == 1
		      && !result.session.has_supervisor_floor, "malformed_grant_terminal_before_state_commit");
	}

	WireAdapter establish_map(config(), {kController});
	auto map_mismatch_packet = packet(1, kSessionB, 0);
	auto map_mismatch_grant = grant(1, kGeneration, 1, SupervisorAction::Establish, kSessionB);
	const auto map_mismatch = establish_map.processSession(state(), map_mismatch_packet, ingress(), &map_mismatch_grant);
	check(map_mismatch.code == AdminCode::BindingMismatch && !map_mismatch.action_performed
	      && map_mismatch.action_kind == AdminActionKind::None, "establish_map_source_session_binding_precedes_context");

	WireAdapter adapter(config(), {kController});
	establishA(adapter);
	auto armed = state(kGeneration + 1);
	armed.request_guard_context.arm_state = ArmState::Armed;
	auto revoke = packet(3, kSessionA, 1, 2);
	auto e2 = grant(2, kGeneration + 1, 3, SupervisorAction::Revoke, kSessionA);
	const auto armed_revoke = adapter.processSession(armed, revoke, ingress(), &e2);
	check(armed_revoke.code == AdminCode::ContextRejected && !armed_revoke.action_performed,
	      "armed_revoke_context_after_binding");

	WireAdapter revoked(config(), {kController});
	establishA(revoked);
	auto okay_revoke = packet(2, kSessionA, 1, 2);
	auto e2_okay = grant(2, kGeneration, 2, SupervisorAction::Revoke, kSessionA);
	revoked.processSession(state(), okay_revoke, ingress(), &e2_okay);
	auto wrong_epoch = packet(3, kSessionA, 2, 2);
	wrong_epoch.clock_mapping_epoch++;
	auto e3 = grant(3, kGeneration, 3, SupervisorAction::Revoke, kSessionA);
	const auto gate_first = revoked.processSession(state(), wrong_epoch, ingress(), &e3);
	check(gate_first.code == AdminCode::NoEstablishedSession && !gate_first.action_performed,
	      "revoke_gate_precedes_map_epoch_binding");
}

void floorsCapacityAndOutput()
{
	WireAdapter replay(config(), {kController});
	establishA(replay);
	WireAdapterSessionTestPeer::seed(replay, 2, 3, true, kMaximum, true, 1);
	auto e2 = grant(2, kGeneration, 2, SupervisorAction::Revoke, kSessionA);
	const auto max_operation = replay.processSession(state(), packet(2, kSessionA, kMaximum, 2), ingress(), &e2);
	check(max_operation.code == AdminCode::PacketOperationReplay && max_operation.session.wire_operation_floor == kMaximum,
	      "wire_floor_maximum_no_wrap");

	WireAdapter capacity(config(), {kController});
	WireAdapterSessionTestPeer::seed(capacity, 0, kMaximum - 3, false, 0, false, 0);
	auto e1 = grant(1, kGeneration, 1, SupervisorAction::Establish, kSessionA);
	const auto exhausted = capacity.processSession(state(), packet(1, kSessionA, 0), ingress(), &e1);
	check(exhausted.code == AdminCode::CounterExhausted && exhausted.report_count == 1
	      && exhausted.reports[0].report_sequence == kMaximum && !exhausted.session.has_supervisor_floor,
	      "three_report_capacity_prereserved");

	WireAdapter epoch(config(), {kController});
	WireAdapterSessionTestPeer::seed(epoch, kMaximum - 1, 10, false, 0, false, 0);
	auto epoch_grant = grant(1, kGeneration, kMaximum, SupervisorAction::Establish, kSessionA);
	const auto epoch_exhausted = epoch.processSession(state(), packet(kMaximum, kSessionA, 0), ingress(),
				     &epoch_grant);
	check(epoch_exhausted.code == AdminCode::CounterExhausted && epoch_exhausted.snapshot.transition_epoch == kMaximum - 1
	      && epoch_exhausted.report_count == 1, "admin_epoch_two_advance_no_wrap");

	WireAdapter output(config(), {kController});
	CanonicalRequest request{83, 89, 97, 101, 103, 107, 109, 113, 127, Frame::Frd,
				 Quantity::InertialAcceleration, ReferencePoint::Cg, Vec3f(1.F, 2.F, 3.F)};
	const auto before = bytes(request);
	const auto closed = output.getEligibleRequest(state(), request);
	check(closed.admission_closed && bytes(request) == before, "ordinary_output_unchanged_after_admin_extension");
}

void matrixAndInitializedBoundaries()
{
	for (unsigned bad_bit = 0; bad_bit < 4; ++bad_bit) {
		WireAdapter adapter(config(), {kController});
		auto verdict = ingress();

		if (bad_bit == 0) { verdict.mavlink2 = false; }

		if (bad_bit == 1) { verdict.parser_authenticated = false; }

		if (bad_bit == 2) { verdict.configured_header_channel_source_binding = false; }

		if (bad_bit == 3) { verdict.configured_target_binding = false; }

		auto e1 = grant(1, kGeneration, 1, SupervisorAction::Establish, kSessionA);
		const auto result = adapter.processSession(state(), packet(1, kSessionA, 0), verdict, &e1);
		check(result.code == AdminCode::IngressRejected && result.correlation.supervisor_event_consumed
		      && !result.session.has_wire_operation_floor, "each_ingress_verdict_rejects_after_event_consume");
	}

	for (unsigned ordinary = 0; ordinary < 3; ++ordinary) {
		WireAdapter adapter(config(), {kController});
		establishA(adapter);
		auto rebound = state(kGeneration + 1, kTime + 10, kSessionB);
		CanonicalRequest output{};
		const auto before = bytes(output);
		AdapterResult result{};

		if (ordinary == 0) { result = adapter.observeTrustedState(rebound); }

		if (ordinary == 1) { result = adapter.poll(rebound); }

		if (ordinary == 2) { result = adapter.getEligibleRequest(rebound, output); }

		check(result.report_count == 2 && adapter.sessionSnapshot().gate == SessionGate::Revoked
		      && bytes(output) == before, "ordinary_public_rebind_revokes_established_gate");
	}

	for (unsigned loss = 0; loss < 4; ++loss) {
		WireAdapter adapter(config(), {kController});
		establishA(adapter);
		auto closing = state(kGeneration + 1, kTime + 10);

		if (loss == 0) { closing.map.valid = false; closing.request_guard_context.clock_mapping_valid = false; }

		if (loss == 1) { closing.request_guard_context.transport_connected = false; }

		if (loss == 2) { closing.request_guard_context.required_state_valid = false; }

		if (loss == 3) { closing.request_guard_context.mode_state = ResearchModeState::NotSelected; }

		auto e2 = grant(2, kGeneration + 1, 3, SupervisorAction::Revoke, kSessionA);
		const auto result = adapter.processSession(closing, packet(3, kSessionA, 1, 2), ingress(), &e2);
		check(result.code == AdminCode::Completed && result.action_performed
		      && result.session.gate == SessionGate::Revoked && result.session.wire_operation_floor == 1,
		      "revoke_through_consistent_loss");
	}

	WireAdapter fitted(config(), {kController});
	establishA(fitted);
	WireAdapterSessionTestPeer::seed(fitted, kMaximum - 2, kMaximum - 4, true, 0, true, 1);
	auto e2 = grant(2, kGeneration + 1, kMaximum - 1, SupervisorAction::Revoke, kSessionA);
	const auto fit = fitted.processSession(state(kGeneration + 1), packet(kMaximum - 1, kSessionA, 1, 2), ingress(), &e2);
	check(fit.code == AdminCode::Completed && fit.report_count == 3
	      && fit.snapshot.transition_epoch == kMaximum && fit.reports[2].report_sequence == kMaximum - 1,
	      "initialized_new_generation_three_report_exact_fit");
}

// Additional master-review cases exercise the independent oracle's full
// boundary families. All identities, times and limits below are synthetic.
void exactGoldenAndFieldMatrices()
{
	WireAdapter golden(config(), {kController});
	const auto baseline = golden.observeTrustedState(state());
	check(baseline.report_count == 2 && baseline.snapshot.last_report_sequence == 2,
	      "oracle_ADMIN001_baseline");
	auto event = grant(1, kGeneration + 1, 2, SupervisorAction::Establish, kSessionA);
	const auto result = golden.processSession(state(kGeneration + 1, kTime + 10), packet(2, kSessionA, 0), ingress(), &event);
	checkClosed(result, "oracle_ADMIN002");
	check(result.code == AdminCode::Completed && result.report_count == 3
	      && result.reports[0].report_sequence == 3 && result.reports[0].transition_epoch == 2
	      && result.reports[1].report_sequence == 4 && result.reports[1].transition_epoch == 3
	      && result.reports[2].report_sequence == 5 && result.snapshot.barrier_report_sequence == 4
	      && result.snapshot.barrier_validator_time_us == kTime + 10, "oracle_ADMIN002_exact_order");
	check(result.correlation.has_packet && result.correlation.packet_source == kSource
	      && result.correlation.packet_session == kSessionA && result.correlation.packet_operation_sequence == 0
	      && result.correlation.has_supervisor_event && result.correlation.supervisor_event_id == 1
	      && result.reports[2].correlation.supervisor_event_consumed
	      && zeroCorrelation(result.reports[0].correlation) && zeroCorrelation(result.reports[1].correlation),
	      "oracle_ADMIN002_correlation");

	for (unsigned field = 0; field < 14; ++field) {
		WireAdapter adapter(config(), {kController});
		auto request = packet(1, kSessionA, 0);

		switch (field) {
		case 0: request.controller_instance_id = 0; break;

		case 1: request.transition_epoch = 0; break;

		case 2: request.clock_mapping_epoch = 0; break;

		case 3: request.source = 0; break;

		case 4: request.session = 0; break;

		case 5: request.protocol_version = 0; break;

		case 6: request.target_system = 0; break;

		case 7: request.target_component = 0; break;

		case 8: request.operation = 0; break;

		case 9: request.flags = 1; break;

		default: request.reserved[field - 10] = 1; break;
		}

		auto next = grant(1, kGeneration, 1, SupervisorAction::Establish, kSessionA);
		const auto rejected = adapter.processSession(state(), request, ingress(false), &next);
		checkClosed(rejected, "packet_field");
		check(rejected.code == AdminCode::MalformedPacket && rejected.correlation.supervisor_event_consumed
		      && !rejected.session.has_wire_operation_floor && !rejected.action_performed,
		      "packet_shape_precedes_ingress_" + std::to_string(field));
	}

	for (unsigned field = 0; field < 10; ++field) {
		WireAdapter adapter(config(), {kController});
		auto request = packet(1, kSessionA, 0);
		auto next = grant(1, kGeneration, 1, SupervisorAction::Establish, kSessionA);

		switch (field) {
		case 0: ++next.expected_controller_instance_id; break;

		case 1: ++next.expected_state_generation; break;

		case 2: ++next.expected_transition_epoch; break;

		case 3: ++next.session.owner; break;

		case 4: ++next.session.session; break;

		case 5: next.action = SupervisorAction::Revoke; break;

		case 6: ++request.controller_instance_id; break;

		case 7: ++request.transition_epoch; break;

		case 8: ++request.clock_mapping_epoch; break;

		case 9: ++request.source; ++next.session.owner; break;
		}

		const auto rejected = adapter.processSession(state(), request, ingress(), &next);
		checkClosed(rejected, "binding_field");
		check(rejected.code == AdminCode::BindingMismatch && rejected.correlation.supervisor_event_consumed
		      && !rejected.action_performed && !rejected.session.has_wire_operation_floor,
		      "wellformed_binding_consumes_" + std::to_string(field));
	}

	for (unsigned field = 0; field < 6; ++field) {
		WireAdapter adapter(config(), {kController});
		auto next = grant(1, kGeneration, 1, SupervisorAction::Establish, kSessionA);

		switch (field) {
		case 0: next.event_id = 0; break;

		case 1: next.action = static_cast<SupervisorAction>(255); break;

		case 2: next.session.owner = 0; break;

		case 3: next.session.session = 0; break;

		case 4: next.has_session_binding = false; break;

		case 5: next.action = SupervisorAction::Revoke; next.has_session_binding = false; next.session = {}; break;
		}

		const auto terminal = adapter.processSession(state(), packet(1, kSessionA, 0), ingress(), &next);
		checkClosed(terminal, "malformed_event");
		check(terminal.code == AdminCode::SupervisorStreamFault && !terminal.snapshot.initialized
		      && !terminal.has_prestate_s1_decision && !terminal.correlation.supervisor_event_consumed,
		      "malformed_event_precommit_" + std::to_string(field));
	}
}

void bootstrapContextsAndClosedAccess()
{
	for (auto policy : {DisarmedPolicy::Reject, DisarmedPolicy::ValidateOnly}) {
		for (auto mode : {ResearchModeState::NotSelected, ResearchModeState::Selected}) {
			auto configuration = config();
			configuration.disarmed_policy = policy;
			WireAdapter adapter(configuration, {kController});
			const auto *guard_address = WireAdapterSessionTestPeer::guardAddress(adapter);
			auto sample = state();
			sample.request_guard_context.mode_state = mode;
			auto next = grant(1, kGeneration, 1, SupervisorAction::Establish, kSessionA);
			auto request = packet(1, kSessionA, 0);
			request.producer_acknowledgement_monotonic_us = kMaximum;
			request.acknowledged_status_sequence = kMaximum;
			const auto established = adapter.processSession(sample, request, ingress(), &next);
			checkClosed(established, "bootstrap");
			const DecisionCode expected = mode == ResearchModeState::NotSelected ? DecisionCode::ModeNotSelected
						      : (policy == DisarmedPolicy::Reject ? DecisionCode::DisarmedRejected : DecisionCode::ValidatedDisarmed);
			check(established.code == AdminCode::Completed && established.prestate_s1_decision.code == expected
			      && established.action_s1_decision.code == DecisionCode::Accepted
			      && !established.action_s1_decision.eligible_for_control
			      && established.action_s1_decision.rejection_count == established.prestate_s1_decision.rejection_count,
			      "bootstrap_policy_mode_and_ignored_ack");
			CanonicalRequest output{};
			output.sequence = 97;
			const auto before = bytes(output);
			const auto closed = adapter.getEligibleRequest(sample, output);
			check(closed.admission_closed && closed.report_count == 1 && bytes(output) == before
			      && WireAdapterSessionTestPeer::guardAddress(adapter) == guard_address,
			      "established_session_still_no_request_or_replaced_guard");
		}
	}

	for (unsigned loss = 0; loss < 6; ++loss) {
		WireAdapter adapter(config(), {kController});
		auto sample = state();

		switch (loss) {
		case 0: sample.request_guard_context.arm_state = ArmState::Armed; break;

		case 1: sample.request_guard_context.arm_state = ArmState::Unknown; break;

		case 2: sample.request_guard_context.mode_state = ResearchModeState::Unknown; break;

		case 3: sample.request_guard_context.transport_connected = false; break;

		case 4: sample.request_guard_context.required_state_valid = false; break;

		case 5: sample.map.valid = false; sample.request_guard_context.clock_mapping_valid = false; break;
		}

		auto next = grant(1, kGeneration, 1, SupervisorAction::Establish, kSessionA);
		const auto rejected = adapter.processSession(sample, packet(1, kSessionA, 0), ingress(), &next);
		checkClosed(rejected, "establish_context");
		check(rejected.code == AdminCode::EstablishmentContextRejected && !rejected.action_performed
		      && rejected.correlation.supervisor_event_consumed && !rejected.session.has_wire_operation_floor,
		      "establish_context_veto_" + std::to_string(loss));
	}
}

void retainedLatchAndReplay()
{
	for (unsigned loss = 0; loss < 5; ++loss) {
		WireAdapter adapter(config(), {kController});
		establishA(adapter);
		adapter.poll(state(kGeneration, kTime - 1));
		auto sample = state(kGeneration + 1, loss == 0 ? kTime : kTime - 1);

		if (loss == 1) { sample.map.valid = false; sample.request_guard_context.clock_mapping_valid = false; }

		if (loss == 2) { sample.request_guard_context.transport_connected = false; }

		if (loss == 3) { sample.request_guard_context.required_state_valid = false; }

		if (loss == 4) { sample.request_guard_context.mode_state = ResearchModeState::Unknown; }

		auto next = grant(2, kGeneration + 1, 3, SupervisorAction::ClearLatchedFault, kSessionA);
		const auto cleared = adapter.clearLatchedFault(sample, next);
		checkClosed(cleared, "retained_clear");
		check(cleared.code == AdminCode::Completed && cleared.report_count == 3
		      && cleared.prestate_s1_decision.latched
		      && cleared.prestate_s1_decision.code == (loss == 0 ? DecisionCode::Latched : DecisionCode::ValidatorTimeRegressed)
		      && !cleared.action_s1_decision.latched && cleared.session.wire_operation_floor == 0
		      && cleared.session.has_wire_operation_floor && cleared.session.last_successful_session.session == kSessionA
		      && cleared.session.gate == SessionGate::EstablishmentRequired && !cleared.correlation.has_packet,
		      "retained_clear_loss_and_floors_" + std::to_string(loss));
		// Restore context at a real generation transition; clear reset the S1 clock.
		auto restored = state(kGeneration + 2, kTime - 2);
		adapter.poll(restored);
		auto same = grant(3, kGeneration + 2, 5, SupervisorAction::Establish, kSessionA);
		const auto forbidden = adapter.processSession(restored, packet(5, kSessionA, 1), ingress(), &same);
		check(forbidden.code == AdminCode::EstablishmentContextRejected && !forbidden.prestate_s1_decision.latched
		      && !forbidden.action_performed && forbidden.session.wire_operation_floor == 0,
		      "clear_requires_distinct_session");
		auto rebound = state(kGeneration + 3, kTime - 1, kSessionB);
		auto different = grant(4, kGeneration + 3, 6, SupervisorAction::Establish, kSessionB);
		const auto established = adapter.processSession(rebound, packet(6, kSessionB, 1), ingress(), &different);
		check(established.code == AdminCode::Completed && established.session.wire_operation_floor == 1
		      && established.snapshot.transition_epoch == 7, "clear_distinct_establish_with_retained_wire_floor");
	}

	for (unsigned failure = 0; failure < 4; ++failure) {
		WireAdapter adapter(config(), {kController});
		establishA(adapter);
		auto sample = state(kGeneration + 1);
		auto next = grant(2, kGeneration + 1, 3, SupervisorAction::ClearLatchedFault, kSessionA);

		if (failure == 0) { next.has_session_binding = false; next.session = {}; }

		if (failure == 1) { next.session.session = kSessionB; }

		if (failure < 3) { sample.request_guard_context.arm_state = ArmState::Armed; }

		const auto rejected = adapter.clearLatchedFault(sample, next);
		const auto expected = failure < 2 ? AdminCode::BindingMismatch
				      : (failure == 2 ? AdminCode::ContextRejected : AdminCode::ClearNotLatched);
		check(rejected.code == expected && !rejected.action_performed && rejected.correlation.supervisor_event_consumed
		      && rejected.session.gate == SessionGate::Established, "clear_binding_context_latch_precedence");
	}

	WireAdapter replay(config(), {kController});
	establishA(replay);
	auto revoke_grant = grant(2, kGeneration, 2, SupervisorAction::Revoke, kSessionA);
	check(replay.processSession(state(), packet(2, kSessionA, 2, 2), ingress(), &revoke_grant).code == AdminCode::Completed,
	      "wire_floor_two_from_real_success");

	for (uint64_t sequence : {uint64_t{1}, uint64_t{2}}) {
		auto next = grant(sequence + 2, kGeneration, 3, SupervisorAction::Revoke, kSessionA);
		const auto rejected = replay.processSession(state(), packet(3, kSessionA, sequence, 2), ingress(), &next);
		check(rejected.code == AdminCode::PacketOperationReplay && rejected.session.wire_operation_floor == 2
		      && rejected.correlation.supervisor_event_consumed && !rejected.action_performed,
		      "pure_wire_decrease_and_replay");
	}
}

void completeCapacityAndTerminalMatrix()
{
	for (unsigned transition = 0; transition < 2; ++transition) {
		for (unsigned reject = 0; reject < 2; ++reject) {
			for (unsigned fits = 0; fits < 2; ++fits) {
				WireAdapter adapter(config(), {kController});
				establishA(adapter);
				const uint64_t last = kMaximum - (2 + transition + fits);
				WireAdapterSessionTestPeer::seed(adapter, 2, last, true, 0, true, 1);
				auto next = grant(2, kGeneration + transition, 2 + transition, SupervisorAction::Revoke, kSessionA);
				const auto result = adapter.processSession(state(kGeneration + transition),
						    packet(2 + transition, kSessionA, 1, 2), ingress(reject == 0), &next);
				checkClosed(result, "capacity_matrix");

				if (!fits) {
					check(result.code == AdminCode::CounterExhausted && result.report_count == 1
					      && result.reports[0].report_sequence == kMaximum && result.snapshot.transition_epoch == 2
					      && result.session.supervisor_floor == 1 && !result.has_prestate_s1_decision,
					      "worst_case_capacity_before_later_rejection");

				} else {
					const uint64_t count = 2 + transition - reject;
					check(result.code == (reject ? AdminCode::IngressRejected : AdminCode::Completed)
					      && result.report_count == count && result.snapshot.last_report_sequence == last + count
					      && result.snapshot.transition_epoch == 3 + transition - reject,
					      "capacity_exact_fit_only_actual_ids_consumed");
				}
			}
		}

		for (unsigned fits = 0; fits < 2; ++fits) {
			WireAdapter adapter(config(), {kController});
			establishA(adapter);
			const uint64_t epoch = kMaximum - transition - fits;
			WireAdapterSessionTestPeer::seed(adapter, epoch, 10, true, 0, true, 1);
			auto next = grant(2, kGeneration + transition, epoch + transition, SupervisorAction::Revoke, kSessionA);
			const auto result = adapter.processSession(state(kGeneration + transition),
					    packet(epoch + transition, kSessionA, 1, 2), ingress(), &next);
			check(result.code == (fits ? AdminCode::Completed : AdminCode::CounterExhausted)
			      && result.snapshot.transition_epoch == (fits ? kMaximum : epoch), "initialized_epoch_exact_fit_or_terminal");
		}
	}

	for (unsigned structural = 0; structural < 2; ++structural) {
		for (unsigned no_normal_room = 0; no_normal_room < 2; ++no_normal_room) {
			WireAdapter adapter(config(), {kController});
			establishA(adapter);
			const uint64_t last = no_normal_room ? kMaximum - 1 : 10;
			WireAdapterSessionTestPeer::seed(adapter, 2, last, true, 0, true, 1);
			auto sample = state(kGeneration + 1);

			if (structural) { ++sample.map.controller_instance_id; }

			auto event = grant(0, kGeneration + 1, 3, SupervisorAction::Revoke, kSessionA);
			const auto terminal = adapter.processSession(sample, packet(3, kSessionA, 1, 2), ingress(), &event);
			const auto expected = no_normal_room ? AdminCode::CounterExhausted
					      : (structural ? AdminCode::TrustedStateFault : AdminCode::SupervisorStreamFault);
			check(terminal.code == expected && terminal.report_count == 1
			      && terminal.reports[0].report_sequence == (no_normal_room ? kMaximum : 11)
			      && terminal.snapshot.transition_epoch == 2 && terminal.snapshot.state_generation == kGeneration
			      && !terminal.has_prestate_s1_decision && terminal.session.supervisor_floor == 1,
			      "terminal_classification_no_partial_exogenous_commit");
			const auto repeat = adapter.clearLatchedFault(state(), event);
			check(repeat.code == expected && repeat.report_count == 0 && !repeat.action_performed,
			      "admin_terminal_first_cause_retained");
			const auto ordinary = adapter.poll(state());
			check(ordinary.report_count == 0 && ordinary.code == (no_normal_room ? AdapterCode::CounterExhausted
					: AdapterCode::TrustedStateFault), "admin_terminal_to_ordinary_mapping");
		}
	}

	WireAdapter maximum_event(config(), {kController});
	maximum_event.observeTrustedState(state());
	WireAdapterSessionTestPeer::seed(maximum_event, 1, 2, false, 0, true, kMaximum - 1);
	auto last_event = grant(kMaximum, kGeneration, 1, SupervisorAction::Establish, kSessionA);
	const auto accepted = maximum_event.processSession(state(), packet(1, kSessionA, 0), ingress(), &last_event);
	check(accepted.code == AdminCode::Completed && accepted.session.supervisor_floor == kMaximum,
	      "last_supervisor_event_consumed_once");
	const auto terminal = maximum_event.processSession(state(), packet(2, kSessionA, 1, 2), ingress(), &last_event);
	check(terminal.code == AdminCode::SupervisorStreamFault && !terminal.correlation.supervisor_event_consumed,
	      "maximum_duplicate_is_terminal_exception");
	WireAdapter ordinary_fault(config(), {kController});
	auto invalid = state();
	invalid.observer_continuous = false;
	ordinary_fault.poll(invalid);
	const auto retained = ordinary_fault.clearLatchedFault(state(), last_event);
	check(retained.code == AdminCode::TrustedStateFault && retained.report_count == 0,
	      "ordinary_terminal_to_admin_mapping");
	WireAdapter inert(config(), {0});
	const auto not_configured = inert.clearLatchedFault(invalid, last_event);
	check(not_configured.code == AdminCode::NotConfigured && not_configured.report_count == 0
	      && !not_configured.has_prestate_s1_decision, "unconfigured_precedes_every_input");
}

} // namespace

int main()
{
	goldenAndRebind();
	packetAndEventFailures();
	clearAndPrecedence();
	predicateAndShapeOrdering();
	floorsCapacityAndOutput();
	matrixAndInitializedBoundaries();
	exactGoldenAndFieldMatrices();
	bootstrapContextsAndClosedAccess();
	retainedLatchAndReplay();
	completeCapacityAndTerminalMatrix();
	std::cout << checks << " checks; " << failures << " failures\n";
	return failures == 0 ? 0 : 1;
}
