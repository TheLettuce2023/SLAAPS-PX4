#include "WireStatus.hpp"

#include <array>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>

using namespace research_acceleration;

namespace research_acceleration
{
// Counter seeding only; no mutation of guard/session/ACK state.
struct WireAdapterAdmissionTestPeer {
	static void exhaust(WireAdapter &adapter) { adapter.snapshot_.last_report_sequence = UINT64_MAX - 1; }
};
}

namespace
{
int checks = 0;
int failures = 0;
const StatusRecipient directed{1, 1, StatusDelivery::Directed};

void check(bool value, const std::string &name)
{
	++checks;

	if (!value) { ++failures; std::cerr << "FAILED: " << name << '\n'; }
}

template<typename T> std::array<unsigned char, sizeof(T)> bytes(const T &value)
{
	std::array<unsigned char, sizeof(T)> copy{};
	std::memcpy(copy.data(), &value, sizeof(T));
	return copy;
}

ReportEvidence baseEvidence()
{
	ReportEvidence e{};
	e.captured = true;
	e.controller_instance_id = 11;
	e.state_reference_valid = true;
	e.clock_mapping_epoch = 41;
	e.arm_state = ArmState::Armed;
	e.mode_state = ResearchModeState::Selected;
	e.session_gate = SessionGate::Established;
	e.transport_connected = true;
	e.clock_mapping_valid = true;
	e.trusted_source = 21;
	e.trusted_session = 31;
	e.acknowledgement_current = true;
	e.has_last_accepted_sequence = true;
	e.last_accepted_sequence = 1;
	e.last_accepted_authenticated = true;
	e.rejection_count = 1;
	return e;
}

IngressReport requestReport()
{
	IngressReport r{};
	r.report_sequence = 7;
	r.transition_epoch = 3;
	r.validator_time_us = 1115;
	r.kind = IngressReportKind::Operation;
	r.code = IngressCode::Completed;
	r.has_s1_decision = true;
	r.s1_decision = {DecisionCode::Accepted, true, true, false, DecisionCode::Accepted, true, 7, 1};
	r.correlation = {IngressPacketKind::Request, 21, 31, 11, 3, 41, 1, 0};
	r.evidence = baseEvidence();
	r.evidence.packet_mavlink2 = true;
	r.evidence.packet_parser_authenticated = true;
	r.evidence.packet_header_source_bound = true;
	r.evidence.packet_target_bound = true;
	r.evidence.decision_origin = StatusDecisionOrigin::S1;
	r.evidence.syntactically_valid = true;
	r.evidence.conservative_age_valid = true;
	r.evidence.conservative_age_us = 7;
	return r;
}

AdapterReport snapshotReport()
{
	const auto request = requestReport();
	AdapterReport r{};
	r.report_sequence = 8;
	r.transition_epoch = 3;
	r.validator_time_us = 1116;
	r.kind = AdapterReportKind::Access;
	r.code = AdapterCode::EligibleRequest;
	r.has_s1_decision = true;
	r.s1_decision = request.s1_decision;
	r.evidence = request.evidence;
	r.evidence.packet_mavlink2 = false;
	r.evidence.packet_parser_authenticated = false;
	r.evidence.packet_header_source_bound = false;
	r.evidence.packet_target_bound = false;
	return r;
}

AdminReport adminReport()
{
	AdminReport r{};
	r.report_sequence = 3;
	r.transition_epoch = 2;
	r.validator_time_us = 1000;
	r.kind = AdminReportKind::Operation;
	r.code = AdminCode::Completed;
	r.correlation = {true, 21, 31, 0, false, 0, false};
	r.evidence = baseEvidence();
	r.evidence.decision_origin = StatusDecisionOrigin::Local;
	r.evidence.local_wire_decision_code = 1;
	r.evidence.acknowledgement_current = false;
	r.evidence.arm_state = ArmState::Disarmed;
	return r;
}

template<typename R>
WireStatusV1 success(const R &r, uint16_t code, uint8_t status_class, const std::string &name)
{
	const auto before = bytes(r);
	WireStatusV1 out{};
	check(buildStatusV1(r, directed, out) == StatusBuildCode::Ok, name + "_build");
	check(bytes(r) == before, name + "_report_immutable");
	check(out.decision_code == code && out.status_class == status_class, name + "_code_class");
	uint8_t payload[106] {};
	check(encodeStatusV1Payload(out, payload) == StatusEncodeCode::Ok && payload[105] != 0, name + "_encode");
	return out;
}

template<typename R>
void reject(const R &r, StatusBuildCode expected, const std::string &name,
	    const StatusRecipient &recipient = directed)
{
	WireStatusV1 out{};
	std::memset(&out, 0xA5, sizeof(out));
	const auto before = bytes(out);
	const auto report_before = bytes(r);
	check(buildStatusV1(r, recipient, out) == expected, name + "_failure_code");
	check(bytes(out) == before && bytes(r) == report_before, name + "_unchanged");
}

template<typename R>
void commonFailures(const R &original)
{
	auto r = original;
	reject(r, StatusBuildCode::InvalidRecipient, "mixed_target", {0, 1, StatusDelivery::Directed});
	reject(r, StatusBuildCode::InvalidRecipient, "zero_directed", {0, 0, StatusDelivery::Directed});
	reject(r, StatusBuildCode::InvalidRecipient, "nonzero_broadcast", {1, 1, StatusDelivery::OperatorBroadcastSnapshot});
	reject(r, StatusBuildCode::InvalidRecipient, "unknown_delivery", {1, 1, static_cast<StatusDelivery>(99)});
	r.evidence.captured = false;
	reject(r, StatusBuildCode::MissingEvidence, "missing");
	// Evidence failure has priority over recipient/identity.
	r.transition_epoch = 0;
	reject(r, StatusBuildCode::MissingEvidence, "evidence_priority", {0, 0, StatusDelivery::Directed});
	r = original; r.transition_epoch = 0;
	reject(r, StatusBuildCode::InvalidIdentity, "zero_epoch");
	r = original; r.evidence.controller_instance_id = 0;
	reject(r, StatusBuildCode::InvalidIdentity, "zero_controller");
	r = original; r.report_sequence = 0;
	reject(r, StatusBuildCode::InvalidReport, "zero_sequence");
	r = original; r.kind = static_cast<decltype(r.kind)>(99);
	reject(r, StatusBuildCode::InvalidReport, "unknown_kind");
	r = original; r.evidence.decision_origin = static_cast<StatusDecisionOrigin>(99);
	reject(r, StatusBuildCode::UnrepresentableDecision, "unknown_origin");
	r = original; r.evidence.arm_state = static_cast<ArmState>(99);
	reject(r, StatusBuildCode::InvalidStateCombination, "unknown_arm");
	r = original; r.evidence.mode_state = static_cast<ResearchModeState>(99);
	reject(r, StatusBuildCode::InvalidStateCombination, "unknown_mode");
	r = original; r.evidence.session_gate = static_cast<SessionGate>(99);
	reject(r, StatusBuildCode::InvalidStateCombination, "unknown_session");
	r = original; r.evidence.trusted_source = 0;
	reject(r, StatusBuildCode::InvalidStateCombination, "partial_owner");
	r = original; r.evidence.clock_mapping_epoch = 0;
	reject(r, StatusBuildCode::InvalidStateCombination, "valid_map_zero_epoch");
}

void localMatrix()
{
	struct IngressCase { IngressCode local; uint16_t wire; };
	const IngressCase cases[] = {
		{IngressCode::Completed, 1}, {IngressCode::NotConfigured, 3},
		{IngressCode::TrustedStateFault, 79}, {IngressCode::CounterExhausted, 78},
		{IngressCode::MalformedPacket, 74}, {IngressCode::IngressRejected, 64},
		{IngressCode::IngressRejected, 65}, {IngressCode::IngressRejected, 5}, {IngressCode::IngressRejected, 66},
		{IngressCode::UnsupportedSessionOperation, 73}, {IngressCode::WireOperationReplay, 75},
		{IngressCode::SessionGateRejected, 6}, {IngressCode::SessionGateRejected, 76}, {IngressCode::SessionGateRejected, 11},
		{IngressCode::BindingMismatch, 68}, {IngressCode::BindingMismatch, 69}, {IngressCode::BindingMismatch, 72},
		{IngressCode::BindingMismatch, 5}, {IngressCode::BindingMismatch, 7}, {IngressCode::BindingMismatch, 81},
		{IngressCode::AcknowledgementRequired, 70}, {IngressCode::AcknowledgementFrozen, 71},
		{IngressCode::AcknowledgementStatusMismatch, 80}, {IngressCode::AcknowledgementOrdering, 71},
		{IngressCode::AcknowledgementTimingInvalid, 23}, {IngressCode::AcknowledgementTimingInvalid, 16},
		{IngressCode::AcknowledgementTimingInvalid, 17}, {IngressCode::AcknowledgementTimingInvalid, 20},
		{IngressCode::MapInvalid, 15}, {IngressCode::MapIntervalInvalid, 77}, {IngressCode::MapArithmeticInvalid, 82},
		{IngressCode::WireVersionInvalid, 4}, {IngressCode::WireEnumInvalid, 25}, {IngressCode::WireEnumInvalid, 26},
		{IngressCode::WireEnumInvalid, 27}, {IngressCode::WireTimeInvalid, 21}, {IngressCode::WireTimeInvalid, 23},
		{IngressCode::WireNonFinite, 28}
	};

	for (const auto &item : cases) {
		auto r = requestReport();
		r.code = item.local;
		r.correlation.packet_kind = IngressPacketKind::Acknowledgement;
		r.evidence.decision_origin = StatusDecisionOrigin::Local;
		r.evidence.local_wire_decision_code = item.wire;
		r.evidence.syntactically_valid = false;
		r.evidence.conservative_age_valid = false;
		r.evidence.conservative_age_us = 0;
		// The retained benign pre-poll is deliberately Accepted/eligible.
		const auto s = success(r, item.wire, 2, "ingress_local");
		check(!(s.status_flags & 11) && s.last_accepted_sequence == 1, "local_never_borrows_old_cache");

		for (uint16_t invalid : {uint16_t(0), uint16_t(63), uint16_t(84)}) {
			r.evidence.local_wire_decision_code = invalid;
			reject(r, StatusBuildCode::UnrepresentableDecision, "invalid_local");
		}
	}

	struct AdminCase { AdminCode local; uint16_t wire; };

	const AdminCase admin_cases[] = {
		{AdminCode::Completed, 1}, {AdminCode::NotConfigured, 3}, {AdminCode::TrustedStateFault, 79},
		{AdminCode::CounterExhausted, 78}, {AdminCode::SupervisorMissing, 8},
		{AdminCode::SupervisorDuplicate, 75}, {AdminCode::SupervisorStreamFault, 79},
		{AdminCode::MalformedPacket, 74}, {AdminCode::UnsupportedOperation, 73}, {AdminCode::PacketOperationReplay, 75},
		{AdminCode::IngressRejected, 64}, {AdminCode::IngressRejected, 65}, {AdminCode::IngressRejected, 5},
		{AdminCode::IngressRejected, 66}, {AdminCode::BindingMismatch, 68}, {AdminCode::BindingMismatch, 79},
		{AdminCode::BindingMismatch, 69}, {AdminCode::BindingMismatch, 8}, {AdminCode::BindingMismatch, 5},
		{AdminCode::BindingMismatch, 7}, {AdminCode::BindingMismatch, 10}, {AdminCode::BindingMismatch, 72},
		{AdminCode::BindingMismatch, 81}, {AdminCode::EstablishmentContextRejected, 7},
		{AdminCode::EstablishmentContextRejected, 33}, {AdminCode::EstablishmentContextRejected, 9},
		{AdminCode::EstablishmentContextRejected, 15}, {AdminCode::EstablishmentContextRejected, 35},
		{AdminCode::EstablishmentContextRejected, 24}, {AdminCode::EstablishmentContextRejected, 34},
		{AdminCode::EstablishmentContextRejected, 37}, {AdminCode::ContextRejected, 33},
		{AdminCode::ContextRejected, 9}, {AdminCode::NoEstablishedSession, 6},
		{AdminCode::NoEstablishedSession, 76}, {AdminCode::NoEstablishedSession, 11}
	};

	for (const auto &item : admin_cases) {
		auto r = adminReport();
		r.code = item.local;
		r.evidence.local_wire_decision_code = item.wire;

		if (item.wire == 37) { r.evidence.latched = true; r.evidence.first_latched_code = DecisionCode::ProducerTimeRegressed; }

		check(!(success(r, item.wire, 2, "admin_local").status_flags & 1), "admin_never_eligible");
		r.evidence.local_wire_decision_code = 0;
		reject(r, StatusBuildCode::UnrepresentableDecision, "admin_missing_detail");
	}

	auto clear = adminReport(); clear.code = AdminCode::ClearNotLatched;
	reject(clear, StatusBuildCode::UnrepresentableDecision, "clear_not_latched_unrepresentable");

	for (const auto local : {AdapterCode::TrustedStateFault, AdapterCode::CounterExhausted, AdapterCode::NotConfigured}) {
		auto r = snapshotReport(); r.kind = AdapterReportKind::Terminal; r.code = local;
		r.has_s1_decision = false; r.evidence = baseEvidence();
		r.evidence.decision_origin = StatusDecisionOrigin::Local;
		r.evidence.local_wire_decision_code = local == AdapterCode::TrustedStateFault ? 79 : (local == AdapterCode::CounterExhausted ? 78 : 3);
		r.evidence.state_reference_valid = false; r.evidence.acknowledgement_current = false;
		const auto s = success(r, r.evidence.local_wire_decision_code, 4, "adapter_terminal");
		check(s.arm_state == 3 && s.research_mode_state == 3 && s.transport_state == 3 && s.clock_mapping_state == 3
		      && s.clock_mapping_epoch == 0 && !(s.status_flags & 1), "terminal_unknown_state");
	}
}

void flagsStatesAndClasses()
{
	for (auto code : {AdapterCode::GuardRejected, AdapterCode::NoEligibleRequest, AdapterCode::Observed}) {
		auto contradictory = snapshotReport(); contradictory.code = code;
		reject(contradictory, StatusBuildCode::UnrepresentableDecision, "contradictory_adapter_success");
	}
	commonFailures(requestReport()); commonFailures(snapshotReport()); commonFailures(adminReport());

	for (unsigned bit = 0; bit < 10; ++bit) {
		auto r = requestReport();

		switch (bit) {
		case 0: r.evidence.packet_mavlink2 = false; break;

		case 1: r.evidence.packet_parser_authenticated = false; break;

		case 2: r.evidence.acknowledgement_current = false; break;

		case 3: r.evidence.arm_state = ArmState::Disarmed; break;

		case 4: r.evidence.mode_state = ResearchModeState::NotSelected; break;

		case 5: r.evidence.transport_connected = false; break;

		case 6: r.evidence.clock_mapping_valid = false; break;

		case 7: r.evidence.session_gate = SessionGate::Revoked; break;

		case 8: r.evidence.latched = true; r.evidence.first_latched_code = DecisionCode::ProducerTimeRegressed;
			r.s1_decision.latched = true; r.s1_decision.first_latched_code = DecisionCode::ProducerTimeRegressed; break;

		case 9: r.s1_decision.eligible_for_control = false; break;
		}

		check(!(success(r, 1, 1, "eligibility_conjunction").status_flags & 1), "no_false_eligible");
	}

	for (auto cause : {DecisionCode::ProducerTimeRegressed, DecisionCode::ValidatorTimeRegressed}) {
		auto r = requestReport(); r.code = IngressCode::S1Rejected; r.s1_decision.code = cause;
		r.s1_decision.eligible_for_control = false; r.s1_decision.latched = true; r.s1_decision.first_latched_code = cause;
		r.evidence.latched = true; r.evidence.first_latched_code = cause;
		const auto s = success(r, cause == DecisionCode::ProducerTimeRegressed ? 18 : 19, 1, "latch");
		check(s.session_state == 5 && (s.status_flags & 4) && s.first_latched_code == s.decision_code, "latch_fields");
		r.s1_decision.first_latched_code = DecisionCode::WrongSource; r.evidence.first_latched_code = DecisionCode::WrongSource;
		reject(r, StatusBuildCode::InvalidStateCombination, "invalid_latch_cause");
	}
	auto absent = requestReport();
	absent.evidence.conservative_age_valid = false; absent.evidence.conservative_age_us = 0;
	absent.s1_decision.conservative_age_valid = false; absent.s1_decision.conservative_age_us = 0;
	absent.evidence.has_last_accepted_sequence = false; absent.evidence.last_accepted_sequence = 0;
	absent.evidence.last_accepted_authenticated = false;
	const auto no_history = success(absent, 1, 1, "absent_optional");
	check(!(no_history.status_flags & 24) && no_history.last_accepted_sequence == 0 && no_history.conservative_age_us == 0, "absent_zero");
	auto snap = snapshotReport();
	WireStatusV1 out{};
	check(buildStatusV1(snap, {0, 0, StatusDelivery::OperatorBroadcastSnapshot}, out) == StatusBuildCode::Ok
	      && out.target_system == 0 && out.target_component == 0, "explicit_snapshot_broadcast");
	reject(requestReport(), StatusBuildCode::InvalidRecipient, "packet_broadcast", {0, 0, StatusDelivery::OperatorBroadcastSnapshot});
	reject(adminReport(), StatusBuildCode::InvalidRecipient, "admin_broadcast", {0, 0, StatusDelivery::OperatorBroadcastSnapshot});
	snap.code = AdapterCode::Observed; snap.kind = AdapterReportKind::Observation; snap.has_s1_decision = false;
	snap.evidence = baseEvidence(); snap.evidence.decision_origin = StatusDecisionOrigin::None;
	success(snap, 0, 5, "healthy_no_primary");

	for (unsigned raw = 0; raw < 3; ++raw) {
		snap.evidence.arm_state = raw == 0 ? ArmState::Disarmed : (raw == 1 ? ArmState::Armed : ArmState::Unknown);
		snap.evidence.mode_state = raw == 0 ? ResearchModeState::NotSelected : (raw == 1 ? ResearchModeState::Selected :
					   ResearchModeState::Unknown);
		const auto s = success(snap, 0, 5, "enum_states");
		check(s.arm_state == raw + 1 && s.research_mode_state == raw + 1, "enum_explicit_mapping");
	}

	const SessionGate gates[] = {SessionGate::NoTrustedSession, SessionGate::Established, SessionGate::EstablishmentRequired, SessionGate::Revoked};

	for (unsigned i = 0; i < 4; ++i) {
		snap.evidence.session_gate = gates[i];
		check(success(snap, 0, 5, "session_states").session_state == i + 1, "session_mapping");
	}

	snap.evidence.trusted_source = 0; snap.evidence.trusted_session = 0;
	check(success(snap, 0, 5, "unknown_ownership").source_id == 0, "unknown_owner_zero");
	snap.kind = AdapterReportKind::Barrier; snap.evidence.decision_origin = StatusDecisionOrigin::Local;
	snap.evidence.local_wire_decision_code = 70; snap.evidence.acknowledgement_current = false;
	success(snap, 70, 3, "adapter_barrier");
	snap.evidence.acknowledgement_current = true;
	reject(snap, StatusBuildCode::UnrepresentableDecision, "barrier_ack_contradiction");
	auto admin = adminReport(); admin.kind = AdminReportKind::AdministrativeBarrier; admin.code = AdminCode::Observed;
	admin.correlation = {}; admin.evidence.local_wire_decision_code = 70;
	success(admin, 70, 3, "admin_barrier");
	admin.correlation.has_packet = true;
	reject(admin, StatusBuildCode::InvalidReport, "barrier_packet");
	auto ingress = requestReport(); ingress.kind = IngressReportKind::ExogenousBarrier; ingress.code = IngressCode::Observed;
	ingress.has_s1_decision = false; ingress.correlation = {}; ingress.evidence = admin.evidence;
	success(ingress, 70, 3, "ingress_barrier");
}

void encoderMatrix()
{
	const auto valid = success(requestReport(), 1, 1, "encoder_base");

	for (unsigned i = 0; i < 22; ++i) {
		auto s = valid;

		switch (i) {
		case 0: s.protocol_version = 0; break;

		case 1: s.controller_instance_id = 0; break;

		case 2: s.transition_epoch = 0; break;

		case 3: s.decision_code = 63; break;

		case 4: s.status_flags |= 64; break;

		case 5: s.status_class = 0; break;

		case 6: s.arm_state = 0; break;

		case 7: s.research_mode_state = 4; break;

		case 8: s.session_state = 6; break;

		case 9: s.transport_state = 0; break;

		case 10: s.clock_mapping_state = 4; break;

		case 11: s.target_system = 0; break;

		case 12: s.status_class = 5; break;

		case 13: s.clock_mapping_epoch = 0; break;

		case 14: s.status_flags &= ~8U; break;

		case 15: s.status_flags &= ~16U; break;

		case 16: s.first_latched_code = 18; break;

		case 17: s.status_flags |= 4; break;

		case 18: s.decision_code = 0; break;

		case 19: s.decision_code = 2; break;

		case 20: s.status_flags &= ~32U; break;

		case 21: s.status_flags &= ~2U; break;
		}

		uint8_t output[106]; std::memset(output, 0x5A, sizeof(output));
		const auto before = bytes(output);
		check(encodeStatusV1Payload(s, output) == StatusEncodeCode::InvalidStatus && bytes(output) == before, "encoder_failure_no_write");
	}

	uint64_t WireStatusV1::* fields[] = {
		&WireStatusV1::controller_instance_id, &WireStatusV1::transition_epoch, &WireStatusV1::clock_mapping_epoch,
		&WireStatusV1::source_id, &WireStatusV1::session_id, &WireStatusV1::correlation_sequence,
		&WireStatusV1::last_accepted_sequence, &WireStatusV1::validator_time_us, &WireStatusV1::status_sequence,
		&WireStatusV1::rejection_count, &WireStatusV1::conservative_age_us
	};

	for (unsigned i = 0; i < 11; ++i) {
		auto s = valid; s.*fields[i] = UINT64_MAX;
		uint8_t output[106] {};
		check(encodeStatusV1Payload(s, output) == StatusEncodeCode::Ok, "uint64_extreme_encode");

		for (unsigned j = 0; j < 8; ++j) { check(output[i * 8 + j] == 255, "uint64_extreme_offset"); }
	}
}

ContractConfig config()
{
	return {kWireProtocolVersionV1, 21, DisarmedPolicy::ValidateOnly,
		{Vec3f(-10.F, -10.F, -10.F), Vec3f(10.F, 10.F, 10.F), 12.F}, {10, 100, 50, 10, 5}, 0.01F};
}
TrustedStateSample sample(uint64_t generation = 51, TimeUs time = 1000, ArmState arm = ArmState::Disarmed)
{
	return {generation, true, {time, arm, ResearchModeState::Selected, true, true, true},
		{true, 21, 31, 11, 41, 1000, 2, 0, 1000}};
}
DecodedSessionV1 session(uint64_t epoch, uint64_t sequence, uint8_t operation, TimeUs raw = 0, uint64_t status = 0)
{
	return {11, epoch, 41, 21, 31, sequence, raw, status, kWireProtocolVersionV1, 1, 1, operation, 0, {0, 0, 0, 0}};
}
DecodedRequestV1 request(uint64_t sequence = 1, TimeUs creation = 110, TimeUs expiry = 130)
{
	return {{21, 31, sequence, creation, expiry, 11, 3, 41, 1.F, 0.F, 0.F, kWireProtocolVersionV1, 1, 1, 1}, 1, 1, 0, {0, 0, 0, 0}};
}
const SessionIngressVerdict authenticated{true, true, true, true};
IngressResult setup(WireAdapter &adapter)
{
	SupervisorEvent grant{1, 51, 1, 11, SupervisorAction::Establish, true, {21, 31}};
	check(adapter.processSession(sample(), session(1, 0, 1), authenticated, &grant).code == AdminCode::Completed, "real_establish");
	adapter.poll(sample(52, 1100, ArmState::Armed));
	check(adapter.processAcknowledgement(sample(52, 1110, ArmState::Armed), session(3, 1, 3, 105, 4), authenticated).code
	      == IngressCode::Completed, "real_ack");
	return adapter.processRequest(sample(52, 1115, ArmState::Armed), request(), authenticated);
}

void lifecycle()
{
	WireAdapter adapter(config(), {11});
	const auto accepted = setup(adapter);
	check(accepted.code == IngressCode::Completed, "real_accepted");
	const auto report_before = bytes(accepted);
	const auto adapter_before = bytes(adapter);
	success(accepted.reports[0], 1, 1, "real_historical_before");
	reject(accepted.reports[0], StatusBuildCode::InvalidRecipient, "real_failure", {0, 0, StatusDelivery::Directed});
	check(bytes(adapter) == adapter_before && bytes(accepted) == report_before, "pure_mapper_no_adapter_mutation");
	const auto duplicate = adapter.processRequest(sample(52, 1116, ArmState::Armed), request(), authenticated);
	const auto dup_status = success(duplicate.reports[0], 12, 1, "real_duplicate");
	check(!(dup_status.status_flags & 1) && dup_status.last_accepted_sequence == 1, "duplicate_retains_history_not_eligibility");
	auto malformed = request(2, 111, 140); malformed.flags = 1;
	const auto rejected = adapter.processRequest(sample(52, 1116, ArmState::Armed), malformed, authenticated);
	const auto bad_status = success(rejected.reports[0], 74, 1, "real_local_with_old_cache");
	check(!(bad_status.status_flags & 11), "local_no_cache_syntax_or_age");
	CanonicalRequest output{};
	check(adapter.getEligibleRequest(sample(52, 1116, ArmState::Armed), output).code == AdapterCode::EligibleRequest
	      && output.sequence == 1, "mapping_did_not_discard_cache");
	const auto disarmed = adapter.poll(sample(53, 1117));
	SupervisorEvent revoke{2, 53, disarmed.snapshot.transition_epoch, 11, SupervisorAction::Revoke, true, {21, 31}};
	const auto revoked = adapter.processSession(sample(53, 1117), session(disarmed.snapshot.transition_epoch, 2, 2), authenticated, &revoke);
	check(revoked.code == AdminCode::Completed && revoked.report_count == 2, "real_revoke");

	for (unsigned i = 0; i < revoked.report_count; ++i) {
		const auto s = success(revoked.reports[i], i == 0 ? 70 : 1, i == 0 ? 3 : 2, "revoke_history");
		check(s.last_accepted_sequence == 1 && (s.status_flags & 16) && s.session_state == 4, "revoke_keeps_history");
	}

	auto next = sample(54, 1120); next.map.session = 32; next.map.map_epoch = 42;
	SupervisorEvent establish{3, 54, revoked.snapshot.transition_epoch + 1, 11, SupervisorAction::Establish, true, {21, 32}};
	auto packet = session(establish.expected_transition_epoch, 3, 1); packet.session = 32; packet.clock_mapping_epoch = 42;
	const auto established = adapter.processSession(next, packet, authenticated, &establish);
	check(established.code == AdminCode::Completed && established.report_count == 3, "real_reestablish");

	if (established.report_count == 3) {
		check(established.reports[0].evidence.has_last_accepted_sequence
		      && !established.reports[1].evidence.has_last_accepted_sequence
		      && !established.reports[2].evidence.has_last_accepted_sequence, "history_reset_at_action_only");

		for (unsigned i = 0; i < 3; ++i) { success(established.reports[i], i == 2 ? 1 : 70, i == 2 ? 2 : 3, "reestablish_history"); }
	}

	check(bytes(accepted) == report_before, "old_request_evidence_not_rewritten");
	check(success(accepted.reports[0], 1, 1, "real_historical_after").status_flags == 59, "old_acceptance_remains_historical");

	for (bool producer : {false, true}) {
		WireAdapter latched(config(), {11}); setup(latched);
		IngressResult latch{};

		if (producer) {
			latched.processRequest(sample(52, 1125, ArmState::Armed), request(2, 120, 145), authenticated);
			latch = latched.processRequest(sample(52, 1126, ArmState::Armed), request(3, 115, 145), authenticated);

		} else {
			latch = latched.processRequest(sample(52, 1114, ArmState::Armed), request(2, 111, 140), authenticated);
		}

		const auto latch_status = success(latch.reports[latch.report_count - 1], producer ? 18 : 19, 1, "real_latch");
		check((latch_status.status_flags & 4) && latch_status.session_state == 5, "real_latch_fields");
		SupervisorEvent clear{2, 53, latch.snapshot.transition_epoch + 1, 11, SupervisorAction::ClearLatchedFault, true, {21, 31}};
		const auto cleared = latched.clearLatchedFault(sample(53, 1130), clear);
		check(cleared.code == AdminCode::Completed && cleared.report_count == 3, "real_clear");

		if (cleared.report_count == 3) {
			check(cleared.reports[0].evidence.latched && !cleared.reports[1].evidence.latched
			      && !cleared.reports[2].evidence.latched, "clear_barrier_chronology");

			for (unsigned i = 0; i < 3; ++i) { success(cleared.reports[i], i == 2 ? 1 : 70, i == 2 ? 2 : 3, "clear_history"); }
		}
	}
}

void validateOnlyAndClearBindings()
{
	WireAdapter adapter(config(), {11});
	setup(adapter);
	const auto disarmed = adapter.poll(sample(53, 1116));
	const auto ack = adapter.processAcknowledgement(sample(53, 1125),
			 session(disarmed.snapshot.transition_epoch, 2, 3, 120, disarmed.snapshot.barrier_report_sequence), authenticated);
	check(ack.code == IngressCode::Completed, "disarmed_ack");
	auto packet = request(2, 125, 145); packet.wire.transition_epoch = disarmed.snapshot.transition_epoch;
	const auto validated = adapter.processRequest(sample(53, 1130), packet, authenticated);
	check(validated.has_operation_s1_decision && validated.operation_s1_decision.code == DecisionCode::ValidatedDisarmed,
	      "validate_only_actual_s1");
	const auto status = success(validated.reports[0], 2, 1, "validate_only_status");
	check(!(status.status_flags & 1) && status.last_accepted_sequence == 1, "validate_only_preserves_last_accepted");

	for (unsigned field = 0; field < 5; ++field) {
		WireAdapter bound(config(), {11}); setup(bound);
		const auto transition = bound.poll(sample(53, 1120));
		SupervisorEvent grant{2, 53, transition.snapshot.transition_epoch, 11, SupervisorAction::ClearLatchedFault, true, {21, 31}};
		const uint16_t expected[] = {8, 8, 5, 7, 79};

		switch (field) {
		case 0: grant.action = SupervisorAction::Revoke; break;

		case 1: grant.has_session_binding = false; grant.session = {}; break;

		case 2: grant.session.owner = 22; break;

		case 3: grant.session.session = 32; break;

		case 4: grant.expected_state_generation = 52; break;
		}

		const auto result = bound.clearLatchedFault(sample(53, 1120), grant);
		check(result.code == AdminCode::BindingMismatch, "clear_binding_rejected");
		success(result.reports[result.report_count - 1], expected[field], 2, "clear_binding_precise_cause");
	}

	WireAdapter unbound(config(), {11});
	SupervisorEvent unexpected{1, 51, 1, 11, SupervisorAction::ClearLatchedFault, true, {21, 31}};
	const auto result = unbound.clearLatchedFault(sample(), unexpected);
	success(result.reports[result.report_count - 1], 10, 2, "clear_unexpected_binding_without_history");
	WireAdapter not_latched(config(), {11});
	SupervisorEvent clear{1, 51, 1, 11, SupervisorAction::ClearLatchedFault, false, {0, 0}};
	const auto no_clear = not_latched.clearLatchedFault(sample(), clear);
	check(no_clear.code == AdminCode::ClearNotLatched, "actual_clear_not_latched");
	reject(no_clear.reports[no_clear.report_count - 1], StatusBuildCode::UnrepresentableDecision, "actual_clear_unrepresentable");
	WireAdapter map_fault(config(), {11}); setup(map_fault);
	auto invalid_map = sample(53, 1120, ArmState::Armed); invalid_map.map.valid = false;
	invalid_map.request_guard_context.clock_mapping_valid = false;
	const auto fault = map_fault.poll(invalid_map);
	const auto fault_status = success(fault.reports[fault.report_count - 1], 15, 5, "actual_invalid_map");
	check(fault_status.clock_mapping_state == 2 && fault_status.clock_mapping_epoch == 0 && !(fault_status.status_flags & 1),
	      "invalid_map_no_eligible");
}

void terminalsAndGrantDetails()
{
	WireAdapter first(config(), {11});
	auto invalid = sample(); invalid.generation = 0;
	const auto bad = first.poll(invalid);
	check(bad.report_count == 1, "invalid_first_report");
	reject(bad.reports[0], StatusBuildCode::InvalidIdentity, "invalid_first_no_epoch_repair");
	check(first.poll(sample()).report_count == 0, "repeated_terminal_silent");

	for (unsigned kind = 0; kind < 3; ++kind) {
		WireAdapter adapter(config(), {11}); setup(adapter);
		WireAdapterAdmissionTestPeer::exhaust(adapter);

		if (kind == 0) {
			const auto result = adapter.poll(sample(52, 1116, ArmState::Armed));
			check(result.report_count == 1, "counter_once");
			check(success(result.reports[0], 78, 4, "counter_adapter").status_sequence == UINT64_MAX, "reserved_sequence");

		} else if (kind == 1) {
			const auto result = adapter.processSession(sample(52, 1116, ArmState::Armed), session(3, 2, 2), authenticated, nullptr);
			check(result.report_count == 1, "counter_admin_once");
			check(success(result.reports[0], 78, 2, "counter_admin").correlation_sequence == 2, "terminal_admin_correlation");

		} else {
			const auto result = adapter.processRequest(sample(52, 1116, ArmState::Armed), request(9), authenticated);
			check(result.report_count == 1, "counter_request_once");
			check(success(result.reports[0], 78, 1, "counter_request").correlation_sequence == 9, "terminal_request_correlation");
		}

		check(adapter.poll(sample(52, 1117, ArmState::Armed)).report_count == 0, "counter_repeated_silent");
	}

	for (unsigned field = 0; field < 7; ++field) {
		WireAdapter adapter(config(), {11});
		SupervisorEvent grant{1, 51, 1, 11, SupervisorAction::Establish, true, {21, 31}};
		const uint16_t expected[] = {68, 79, 69, 8, 8, 5, 7};

		switch (field) {
		case 0: grant.expected_controller_instance_id = 12; break;

		case 1: grant.expected_state_generation = 50; break;

		case 2: grant.expected_transition_epoch = 2; break;

		case 3: grant.action = SupervisorAction::Revoke; break;

		case 4: grant.has_session_binding = false; grant.session = {}; grant.action = SupervisorAction::ClearLatchedFault; break;

		case 5: grant.session.owner = 22; break;

		case 6: grant.session.session = 32; break;
		}

		const auto result = adapter.processSession(sample(), session(1, 0, 1), authenticated, &grant);
		check(result.code == AdminCode::BindingMismatch, "real_grant_binding_rejected");
		success(result.reports[result.report_count - 1], expected[field], 2, "real_grant_precise_cause");
	}
}
}

int main()
{
	localMatrix();
	flagsStatesAndClasses();
	encoderMatrix();
	lifecycle();
	validateOnlyAndClearBindings();
	terminalsAndGrantDetails();
	std::cout << checks << " checks; " << failures << " failures\n";
	return failures ? 1 : 0;
}
