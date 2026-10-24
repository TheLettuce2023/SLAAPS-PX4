#include "WireStatus.hpp"

#include <cstring>
#include <stddef.h>
#include <stdint.h>

namespace research_acceleration
{

bool mapDecisionCodeV1(DecisionCode code, uint16_t &output)
{
	uint16_t value{};

	switch (code) {
	case DecisionCode::Accepted: value = 1; break;

	case DecisionCode::ValidatedDisarmed: value = 2; break;

	case DecisionCode::NotConfigured: value = 3; break;

	case DecisionCode::WrongProtocolVersion: value = 4; break;

	case DecisionCode::WrongSource: value = 5; break;

	case DecisionCode::NoSession: value = 6; break;

	case DecisionCode::WrongSession: value = 7; break;

	case DecisionCode::UnauthorizedSessionEstablishment: value = 8; break;

	case DecisionCode::SessionChangeWhileArmed: value = 9; break;

	case DecisionCode::InvalidTrustedSession: value = 10; break;

	case DecisionCode::SessionEstablishmentRequired: value = 11; break;

	case DecisionCode::DuplicateSequence: value = 12; break;

	case DecisionCode::OutOfOrderSequence: value = 13; break;

	case DecisionCode::ProducerTimeNotIncreasing: value = 14; break;

	case DecisionCode::ClockMappingInvalid: value = 15; break;

	case DecisionCode::ClockUncertaintyExceeded: value = 16; break;

	case DecisionCode::ProducerTimeInFuture: value = 17; break;

	case DecisionCode::ProducerTimeRegressed: value = 18; break;

	case DecisionCode::ValidatorTimeRegressed: value = 19; break;

	case DecisionCode::RequestTooOld: value = 20; break;

	case DecisionCode::ValidityOutOfRange: value = 21; break;

	case DecisionCode::RequestExpired: value = 22; break;

	case DecisionCode::TimeArithmeticInvalid: value = 23; break;

	case DecisionCode::RequiredStateInvalid: value = 24; break;

	case DecisionCode::WrongFrame: value = 25; break;

	case DecisionCode::WrongQuantity: value = 26; break;

	case DecisionCode::WrongReferencePoint: value = 27; break;

	case DecisionCode::NonFinite: value = 28; break;

	case DecisionCode::AxisOutOfBounds: value = 29; break;

	case DecisionCode::NormOutOfBounds: value = 30; break;

	case DecisionCode::DisarmedRejected: value = 31; break;

	case DecisionCode::ModeNotSelected: value = 32; break;

	case DecisionCode::ArmStateUnknown: value = 33; break;

	case DecisionCode::ModeStateUnknown: value = 34; break;

	case DecisionCode::Disconnected: value = 35; break;

	case DecisionCode::NoEligibleRequest: value = 36; break;

	case DecisionCode::Latched: value = 37; break;

	default: return false;
	}

	output = value;
	return true;
}

namespace
{
// Frozen C0a status flags and classes, not internal enum ordinal values.
constexpr uint16_t kEligible = 1U << 0;
constexpr uint16_t kSyntax = 1U << 1;
constexpr uint16_t kLatched = 1U << 2;
constexpr uint16_t kAge = 1U << 3;
constexpr uint16_t kHistory = 1U << 4;
constexpr uint16_t kAuthenticated = 1U << 5;
constexpr uint16_t kAllFlags = kEligible | kSyntax | kLatched | kAge | kHistory | kAuthenticated;
constexpr uint8_t kRequest = 1;
constexpr uint8_t kSession = 2;
constexpr uint8_t kTransition = 3;
constexpr uint8_t kFault = 4;
constexpr uint8_t kSnapshot = 5;

bool assignedDecision(uint16_t code)
{
	return code <= 37 || (code >= 64 && code <= 83);
}

bool validStatus(const WireStatusV1 &s)
{
	if (s.controller_instance_id == 0 || s.transition_epoch == 0 || s.protocol_version != kWireProtocolVersionV1
	    || !assignedDecision(s.decision_code) || (s.status_flags & ~kAllFlags) != 0
	    || s.status_class < kRequest || s.status_class > kSnapshot
	    || s.arm_state < 1 || s.arm_state > 3 || s.research_mode_state < 1 || s.research_mode_state > 3
	    || s.session_state < 1 || s.session_state > 5 || s.transport_state < 1 || s.transport_state > 3
	    || s.clock_mapping_state < 1 || s.clock_mapping_state > 3) {
		return false;
	}

	if (!((s.target_system != 0 && s.target_component != 0)
	      || (s.target_system == 0 && s.target_component == 0 && s.status_class == kSnapshot))
	    || (s.status_class >= kTransition && s.correlation_sequence != 0)
	    || (s.clock_mapping_state == 1 && s.clock_mapping_epoch == 0)
	    || (!(s.status_flags & kAge) && s.conservative_age_us != 0)
	    || (!(s.status_flags & kHistory) && s.last_accepted_sequence != 0)) {
		return false;
	}

	const bool latched = (s.status_flags & kLatched) != 0;
	const bool eligible = (s.status_flags & kEligible) != 0;

	if (latched != (s.session_state == 5)
	    || (latched && s.first_latched_code != 18 && s.first_latched_code != 19)
	    || (!latched && s.first_latched_code != 0)
	    || ((s.decision_code == 18 || s.decision_code == 19 || s.decision_code == 37) && !latched)
	    || (s.decision_code == 0 && (s.status_class != kSnapshot || eligible))
	    || (s.decision_code == 2 && (s.arm_state != 1 || eligible))) {
		return false;
	}

	return !eligible || (s.decision_code == 1 && (s.status_class == kRequest || s.status_class == kSnapshot)
			     && (s.status_flags & kSyntax) && (s.status_flags & kAuthenticated)
			     && s.arm_state == 2 && s.research_mode_state == 2 && s.session_state == 2
			     && s.transport_state == 1 && s.clock_mapping_state == 1
			     && s.source_id != 0 && s.session_id != 0 && !latched);
}

struct Input {
	const ReportEvidence &evidence;
	const Decision *decision;
	uint64_t sequence;
	uint64_t epoch;
	TimeUs time;
	uint8_t status_class;
	bool has_packet;
	bool barrier;
	bool terminal;
	SourceId source;
	SessionId session;
	uint64_t correlation;
	bool shape_valid;
};

template<size_t N>
bool localCode(const Input &input, uint16_t &code, const uint16_t (&allowed)[N])
{
	if (input.evidence.decision_origin != StatusDecisionOrigin::Local) { return false; }

	for (uint16_t value : allowed) {
		if (input.evidence.local_wire_decision_code == value) { code = value; return true; }
	}

	return false;
}

bool s1Code(const Input &input, uint16_t &code)
{
	return input.evidence.decision_origin == StatusDecisionOrigin::S1 && input.evidence.local_wire_decision_code == 0
	       && input.decision != nullptr && mapDecisionCodeV1(input.decision->code, code);
}

bool zero(const AdminCorrelation &c)
{
	return !c.has_packet && c.packet_source == 0 && c.packet_session == 0 && c.packet_operation_sequence == 0
	       && !c.has_supervisor_event && c.supervisor_event_id == 0 && !c.supervisor_event_consumed;
}

bool zero(const IngressCorrelation &c)
{
	return c.packet_kind == IngressPacketKind::None && c.source == 0 && c.session == 0
	       && c.controller_instance_id == 0 && c.transition_epoch == 0 && c.map_epoch == 0
	       && c.packet_sequence == 0 && c.acknowledged_status_sequence == 0;
}

bool stateEnums(const ReportEvidence &e, uint8_t &arm, uint8_t &mode, uint8_t &session)
{
	switch (e.arm_state) {
	case ArmState::Disarmed: arm = 1; break;

	case ArmState::Armed: arm = 2; break;

	case ArmState::Unknown: arm = 3; break;

	default: return false;
	}

	switch (e.mode_state) {
	case ResearchModeState::NotSelected: mode = 1; break;

	case ResearchModeState::Selected: mode = 2; break;

	case ResearchModeState::Unknown: mode = 3; break;

	default: return false;
	}

	switch (e.session_gate) {
	case SessionGate::NoTrustedSession: session = 1; break;

	case SessionGate::Established: session = 2; break;

	case SessionGate::EstablishmentRequired: session = 3; break;

	case SessionGate::Revoked: session = 4; break;

	default: return false;
	}

	if (!e.state_reference_valid) { arm = 3; mode = 3; }

	if (e.latched) { session = 5; }

	return true;
}

template<typename Resolve>
StatusBuildCode build(const Input &input, const StatusRecipient &recipient, WireStatusV1 &output, Resolve resolve)
{
	const ReportEvidence &e = input.evidence;

	if (!input.shape_valid || input.sequence == 0
	    || (input.sequence == UINT64_MAX && !input.terminal)) {
		return StatusBuildCode::InvalidReport;
	}

	if (!e.captured) { return StatusBuildCode::MissingEvidence; }

	const bool directed = recipient.delivery == StatusDelivery::Directed
			      && recipient.target_system != 0 && recipient.target_component != 0;
	const bool broadcast = recipient.delivery == StatusDelivery::OperatorBroadcastSnapshot
			       && recipient.target_system == 0 && recipient.target_component == 0 && input.status_class == kSnapshot;

	if (!directed && !broadcast) { return StatusBuildCode::InvalidRecipient; }

	if (e.controller_instance_id == 0 || input.epoch == 0) { return StatusBuildCode::InvalidIdentity; }

	uint16_t code{};

	if (!resolve(code) || !assignedDecision(code)
	    || (input.sequence == UINT64_MAX && code != 78)) {
		return StatusBuildCode::UnrepresentableDecision;
	}

	WireStatusV1 status{};
	status.controller_instance_id = e.controller_instance_id;
	status.transition_epoch = input.epoch;
	status.clock_mapping_epoch = e.state_reference_valid && e.clock_mapping_valid ? e.clock_mapping_epoch : 0;
	status.source_id = input.has_packet ? input.source : (input.status_class == kSession ? 0 : e.trusted_source);
	status.session_id = input.has_packet ? input.session : (input.status_class == kSession ? 0 : e.trusted_session);
	status.correlation_sequence = input.correlation;
	status.validator_time_us = input.time;
	status.status_sequence = input.sequence;
	status.rejection_count = e.rejection_count;
	status.protocol_version = kWireProtocolVersionV1;
	status.decision_code = code;
	status.target_system = recipient.target_system;
	status.target_component = recipient.target_component;
	status.status_class = input.status_class;

	if (!stateEnums(e, status.arm_state, status.research_mode_state, status.session_state)
	    || ((e.trusted_source == 0) != (e.trusted_session == 0))
	    || (e.session_gate == SessionGate::Established && e.trusted_source == 0)
	    || (!e.has_last_accepted_sequence && (e.last_accepted_sequence != 0 || e.last_accepted_authenticated))
	    || (input.terminal && (e.state_reference_valid || e.acknowledgement_current))
	    || (!input.has_packet && (e.packet_mavlink2 || e.packet_parser_authenticated
				      || e.packet_header_source_bound || e.packet_target_bound))) {
		return StatusBuildCode::InvalidStateCombination;
	}

	status.transport_state = e.state_reference_valid ? (e.transport_connected ? 1 : 2) : 3;
	status.clock_mapping_state = e.state_reference_valid ? (e.clock_mapping_valid ? 1 : 2) : 3;
	const bool primary_s1 = e.decision_origin == StatusDecisionOrigin::S1;

	if (primary_s1) {
		const Decision &d = *input.decision;

		if (e.syntactically_valid != d.syntactically_valid || e.conservative_age_valid != d.conservative_age_valid
		    || (e.conservative_age_valid && e.conservative_age_us != d.conservative_age_us)
		    || e.rejection_count != d.rejection_count || e.latched != d.latched
		    || (e.latched && e.first_latched_code != d.first_latched_code)) {
			return StatusBuildCode::InvalidStateCombination;
		}

		if (d.syntactically_valid) { status.status_flags |= kSyntax; }

		if (d.conservative_age_valid) { status.status_flags |= kAge; status.conservative_age_us = d.conservative_age_us; }

	} else if (e.syntactically_valid || e.conservative_age_valid || e.conservative_age_us != 0) {
		return StatusBuildCode::InvalidStateCombination;
	}

	if (e.has_last_accepted_sequence) { status.status_flags |= kHistory; status.last_accepted_sequence = e.last_accepted_sequence; }

	if (e.latched) {
		if (!mapDecisionCodeV1(e.first_latched_code, status.first_latched_code)
		    || (status.first_latched_code != 18 && status.first_latched_code != 19)) {
			return StatusBuildCode::InvalidStateCombination;
		}

		status.status_flags |= kLatched;
	}

	const bool eligible_decision = primary_s1 && input.decision->code == DecisionCode::Accepted
				       && input.decision->eligible_for_control && !input.barrier && !input.terminal;
	const bool authenticated = input.has_packet ? (e.packet_mavlink2 && e.packet_parser_authenticated)
				   : (input.status_class == kSnapshot && eligible_decision && e.last_accepted_authenticated);

	if (authenticated) { status.status_flags |= kAuthenticated; }

	if (eligible_decision && (input.status_class == kRequest || input.status_class == kSnapshot)
	    && (status.status_flags & kSyntax) && authenticated && e.acknowledgement_current
	    && status.arm_state == 2 && status.research_mode_state == 2 && status.session_state == 2
	    && status.transport_state == 1 && status.clock_mapping_state == 1
	    && status.source_id != 0 && status.session_id != 0 && !e.latched) {
		status.status_flags |= kEligible;
	}

	if (!validStatus(status)) { return StatusBuildCode::InvalidStateCombination; }

	output = status;
	return StatusBuildCode::Ok;
}

} // namespace

StatusBuildCode buildStatusV1(const AdapterReport &r, const StatusRecipient &recipient, WireStatusV1 &output)
{
	const bool barrier = r.kind == AdapterReportKind::Barrier;
	const bool terminal = r.kind == AdapterReportKind::Terminal;
	const bool ordinary = r.kind == AdapterReportKind::Observation || r.kind == AdapterReportKind::Poll
			      || r.kind == AdapterReportKind::Access;
	const Input input{r.evidence, r.has_s1_decision ? &r.s1_decision : nullptr,
			  r.report_sequence, r.transition_epoch, r.validator_time_us,
			  static_cast<uint8_t>(barrier ? kTransition : (terminal ? kFault : kSnapshot)),
			  false, barrier, terminal, 0, 0, 0, (ordinary || barrier || terminal) &&(!barrier || !r.has_s1_decision)};
	return build(input, recipient, output, [&](uint16_t &code) {
		if (barrier) { return r.code == AdapterCode::Observed && !r.evidence.acknowledgement_current && localCode(input, code, {70}); }

		if (terminal) {
			switch (r.code) {
			case AdapterCode::TrustedStateFault: return localCode(input, code, {79});

			case AdapterCode::CounterExhausted: return localCode(input, code, {78});

			case AdapterCode::NotConfigured: return localCode(input, code, {3});

			default: return false;
			}
		}

		switch (r.code) {
		case AdapterCode::Observed:
			if (!r.has_s1_decision && r.evidence.decision_origin == StatusDecisionOrigin::None
			    && r.evidence.local_wire_decision_code == 0 && !r.evidence.latched) { code = 0; return true; }

			return s1Code(input, code)
			       && (r.kind == AdapterReportKind::Observation
				   ? (code == 1 || code == 2 || code == 36)
				   : (r.kind == AdapterReportKind::Poll && code == 1));

		case AdapterCode::EligibleRequest:
			return r.kind == AdapterReportKind::Access && r.has_s1_decision
			       && r.s1_decision.code == DecisionCode::Accepted && r.s1_decision.eligible_for_control && s1Code(input, code);

		case AdapterCode::NoEligibleRequest:
			return r.kind != AdapterReportKind::Observation && s1Code(input, code)
			       && (code == 2 || code == 36
				   || (code == 1 && r.kind == AdapterReportKind::Access && !r.evidence.acknowledgement_current));

		case AdapterCode::GuardRejected:
			return s1Code(input, code) && code != 1 && code != 2 && code != 36;

		default: return false;
		}
	});
}

StatusBuildCode buildStatusV1(const AdminReport &r, const StatusRecipient &recipient, WireStatusV1 &output)
{
	const bool barrier = r.kind == AdminReportKind::ExogenousBarrier || r.kind == AdminReportKind::AdministrativeBarrier;
	const bool terminal = r.kind == AdminReportKind::Terminal;
	const bool operation = r.kind == AdminReportKind::Operation;
	const auto &c = r.correlation;
	const bool correlation_valid = barrier ? zero(c) : ((c.has_packet || (c.packet_source == 0 && c.packet_session == 0
				       && c.packet_operation_sequence == 0))
				       && (c.has_supervisor_event || (c.supervisor_event_id == 0 && !c.supervisor_event_consumed)));
	const Input input{r.evidence, r.has_s1_decision ? &r.s1_decision : nullptr,
			  r.report_sequence, r.transition_epoch, r.validator_time_us, static_cast<uint8_t>(barrier ? kTransition : kSession),
			  c.has_packet, barrier, terminal, c.packet_source, c.packet_session, c.packet_operation_sequence,
			  (barrier || terminal || operation) &&correlation_valid &&(!barrier || !r.has_s1_decision)};
	return build(input, recipient, output, [&](uint16_t &code) {
		if (barrier) { return r.code == AdminCode::Observed && !r.evidence.acknowledgement_current && localCode(input, code, {70}); }

		if (terminal && r.code != AdminCode::TrustedStateFault && r.code != AdminCode::CounterExhausted
		    && r.code != AdminCode::SupervisorStreamFault && r.code != AdminCode::NotConfigured) { return false; }

		switch (r.code) {
		case AdminCode::Completed:
			return (s1Code(input, code) && r.s1_decision.code == DecisionCode::Accepted) || localCode(input, code, {1});

		case AdminCode::NotConfigured: return localCode(input, code, {3});

		case AdminCode::TrustedStateFault:
		case AdminCode::SupervisorStreamFault: return localCode(input, code, {79});

		case AdminCode::CounterExhausted: return localCode(input, code, {78});

		case AdminCode::SupervisorMissing: return localCode(input, code, {8});

		case AdminCode::SupervisorDuplicate:
		case AdminCode::PacketOperationReplay: return localCode(input, code, {75});

		case AdminCode::MalformedPacket: return localCode(input, code, {74});

		case AdminCode::IngressRejected: return localCode(input, code, {64, 65, 5, 66});

		case AdminCode::UnsupportedOperation: return localCode(input, code, {73});

		case AdminCode::BindingMismatch: return localCode(input, code, {68, 79, 69, 8, 5, 7, 10, 72, 81});

		case AdminCode::EstablishmentContextRejected: return localCode(input, code, {7, 33, 9, 15, 35, 24, 34, 37});

		case AdminCode::ContextRejected: return localCode(input, code, {33, 9});

		case AdminCode::NoEstablishedSession: return localCode(input, code, {6, 76, 11});

		case AdminCode::S1ActionRejected: return s1Code(input, code) && r.s1_decision.code != DecisionCode::Accepted;

		case AdminCode::ClearNotLatched:
		case AdminCode::ApiDomainMismatch:
		case AdminCode::Observed: return false;
		}

		return false;
	});
}

StatusBuildCode buildStatusV1(const IngressReport &r, const StatusRecipient &recipient, WireStatusV1 &output)
{
	const bool barrier = r.kind == IngressReportKind::ExogenousBarrier;
	const bool terminal = r.kind == IngressReportKind::Terminal;
	const bool operation = r.kind == IngressReportKind::Operation;
	const bool request = r.correlation.packet_kind == IngressPacketKind::Request;
	const bool acknowledgement = r.correlation.packet_kind == IngressPacketKind::Acknowledgement;
	const bool correlation_valid = barrier ? zero(r.correlation) : ((request || acknowledgement)
				       && (!request || r.correlation.acknowledged_status_sequence == 0));
	const Input input{r.evidence, r.has_s1_decision ? &r.s1_decision : nullptr,
			  r.report_sequence, r.transition_epoch, r.validator_time_us,
			  static_cast<uint8_t>(barrier ? kTransition : (request ? kRequest : kSession)),
			  !barrier, barrier, terminal, r.correlation.source, r.correlation.session, r.correlation.packet_sequence,
			  (barrier || terminal || operation) &&correlation_valid &&(!barrier || !r.has_s1_decision)};
	return build(input, recipient, output, [&](uint16_t &code) {
		if (barrier) { return r.code == IngressCode::Observed && !r.evidence.acknowledgement_current && localCode(input, code, {70}); }

		if (terminal && r.code != IngressCode::TrustedStateFault && r.code != IngressCode::CounterExhausted
		    && r.code != IngressCode::NotConfigured) { return false; }

		switch (r.code) {
		case IngressCode::Completed:
			return request ? (s1Code(input, code) && r.s1_decision.code == DecisionCode::Accepted) : localCode(input, code, {1});

		case IngressCode::NotConfigured: return localCode(input, code, {3});

		case IngressCode::TrustedStateFault: return localCode(input, code, {79});

		case IngressCode::CounterExhausted: return localCode(input, code, {78});

		case IngressCode::PrestateRejected:
		case IngressCode::S1Rejected: return s1Code(input, code) && r.s1_decision.code != DecisionCode::Accepted;

		case IngressCode::MalformedPacket: return localCode(input, code, {74});

		case IngressCode::IngressRejected: return localCode(input, code, {64, 65, 5, 66});

		case IngressCode::UnsupportedSessionOperation: return localCode(input, code, {73});

		case IngressCode::WireOperationReplay: return localCode(input, code, {75});

		case IngressCode::SessionGateRejected: return localCode(input, code, {6, 76, 11});

		case IngressCode::BindingMismatch: return localCode(input, code, {68, 69, 72, 5, 7, 81});

		case IngressCode::AcknowledgementRequired: return localCode(input, code, {70});

		case IngressCode::AcknowledgementFrozen:
		case IngressCode::AcknowledgementOrdering: return localCode(input, code, {71});

		case IngressCode::AcknowledgementStatusMismatch: return localCode(input, code, {80});

		case IngressCode::AcknowledgementTimingInvalid: return localCode(input, code, {23, 16, 17, 20});

		case IngressCode::MapInvalid: return localCode(input, code, {15});

		case IngressCode::MapIntervalInvalid: return localCode(input, code, {77});

		case IngressCode::MapArithmeticInvalid: return localCode(input, code, {82});

		case IngressCode::WireVersionInvalid: return localCode(input, code, {4});

		case IngressCode::WireEnumInvalid: return localCode(input, code, {25, 26, 27});

		case IngressCode::WireTimeInvalid: return localCode(input, code, {21, 23});

		case IngressCode::WireNonFinite: return localCode(input, code, {28});

		case IngressCode::ApiDomainMismatch:
		case IngressCode::Observed: return false;
		}

		return false;
	});
}

StatusEncodeCode encodeStatusV1Payload(const WireStatusV1 &status, uint8_t (&output)[106])
{
	if (!validStatus(status)) { return StatusEncodeCode::InvalidStatus; }

	uint8_t bytes[106] {};
	unsigned offset = 0;
	const auto put = [&](uint64_t value, unsigned width) {
		for (unsigned index = 0; index < width; ++index) { bytes[offset++] = static_cast<uint8_t>(value >>(8U * index)); }
	};
	const uint64_t wide[] = {status.controller_instance_id, status.transition_epoch, status.clock_mapping_epoch,
				 status.source_id, status.session_id, status.correlation_sequence, status.last_accepted_sequence,
				 status.validator_time_us, status.status_sequence, status.rejection_count, status.conservative_age_us
				};

	for (uint64_t value : wide) { put(value, 8); }

	put(status.protocol_version, 4);
	put(status.decision_code, 2);
	put(status.first_latched_code, 2);
	put(status.status_flags, 2);
	const uint8_t narrow[] = {status.target_system, status.target_component, status.status_class, status.arm_state,
				  status.research_mode_state, status.session_state, status.transport_state, status.clock_mapping_state
				 };

	for (uint8_t value : narrow) { put(value, 1); }

	std::memcpy(output, bytes, sizeof(bytes));
	return StatusEncodeCode::Ok;
}

} // namespace research_acceleration
