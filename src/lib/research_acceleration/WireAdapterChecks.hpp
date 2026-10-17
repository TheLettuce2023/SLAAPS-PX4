#pragma once

#include "WireAdapter.hpp"

#include <limits>

namespace research_acceleration
{
namespace wire_adapter_detail
{

constexpr uint64_t kTerminalReportSequence = std::numeric_limits<uint64_t>::max();
constexpr uint64_t kFirstUsableCounterValue = 1U;
constexpr uint64_t kMaximumNormalReportSequence = kTerminalReportSequence - kFirstUsableCounterValue;
constexpr uint8_t kOperationReportCount = static_cast<uint8_t>(kFirstUsableCounterValue);
constexpr uint8_t kTransitionReportCount = 2;
constexpr uint8_t kAdminDirectReportCount = 2;
constexpr uint8_t kAdminTransitionReportCount = 3;
constexpr uint64_t kAdminDirectEpochAdvances = 1U;
constexpr uint64_t kAdminTransitionEpochAdvances = 2U;
constexpr uint8_t kSessionOperationEstablish = 1;
constexpr uint8_t kSessionOperationRevoke = 2;
constexpr uint8_t kSessionOperationAcknowledge = 3;

inline bool validArmState(ArmState state)
{
	return state == ArmState::Disarmed || state == ArmState::Armed || state == ArmState::Unknown;
}

inline bool validModeState(ResearchModeState state)
{
	return state == ResearchModeState::NotSelected || state == ResearchModeState::Selected
	       || state == ResearchModeState::Unknown;
}

inline bool sameContextExceptValidatorTime(const EvaluationContext &left, const EvaluationContext &right)
{
	return left.arm_state == right.arm_state && left.mode_state == right.mode_state
	       && left.clock_mapping_valid == right.clock_mapping_valid
	       && left.transport_connected == right.transport_connected
	       && left.required_state_valid == right.required_state_valid;
}

inline bool sameMap(const TrustedClockMap &left, const TrustedClockMap &right)
{
	return left.valid == right.valid && left.source == right.source && left.session == right.session
	       && left.controller_instance_id == right.controller_instance_id && left.map_epoch == right.map_epoch
	       && left.validator_minus_producer_us == right.validator_minus_producer_us
	       && left.uncertainty_us == right.uncertainty_us
	       && left.valid_from_producer_us == right.valid_from_producer_us
	       && left.valid_until_producer_us == right.valid_until_producer_us;
}

inline bool normalPollDecision(DecisionCode code)
{
	return code == DecisionCode::Accepted || code == DecisionCode::ValidatedDisarmed
	       || code == DecisionCode::NoEligibleRequest;
}

inline bool validSupervisorAction(SupervisorAction action)
{
	return action == SupervisorAction::Establish || action == SupervisorAction::Revoke
	       || action == SupervisorAction::ClearLatchedFault;
}

inline bool validSessionPacket(const DecodedSessionV1 &packet)
{
	return packet.controller_instance_id != 0 && packet.transition_epoch != 0 && packet.clock_mapping_epoch != 0
	       && packet.source != 0 && packet.session != 0 && packet.protocol_version == kWireProtocolVersionV1
	       && packet.target_system != 0 && packet.target_component != 0
	       && (packet.operation == kSessionOperationEstablish || packet.operation == kSessionOperationRevoke
		   || packet.operation == kSessionOperationAcknowledge)
	       && packet.flags == 0 && packet.reserved[0] == 0 && packet.reserved[1] == 0
	       && packet.reserved[2] == 0 && packet.reserved[3] == 0;
}

inline bool validSupervisorShape(const SupervisorEvent &event)
{
	if (!validSupervisorAction(event.action) || event.expected_state_generation == 0
	    || event.expected_transition_epoch == 0 || event.expected_controller_instance_id == 0) {
		return false;
	}

	if (event.has_session_binding) {
		return event.session.owner != 0 && event.session.session != 0;
	}

	return event.action == SupervisorAction::ClearLatchedFault && event.session.owner == 0 && event.session.session == 0;
}

inline bool validIngress(const SessionIngressVerdict &ingress)
{
	return ingress.mavlink2 && ingress.parser_authenticated && ingress.configured_header_channel_source_binding
	       && ingress.configured_target_binding;
}

inline bool sameSession(const TrustedSession &left, const TrustedSession &right)
{
	return left.owner == right.owner && left.session == right.session;
}

inline bool validRequestPacket(const DecodedRequestV1 &packet)
{
	return packet.wire.source_id != 0 && packet.wire.session_id != 0
	       && packet.wire.controller_instance_id != 0 && packet.wire.transition_epoch != 0
	       && packet.wire.clock_mapping_epoch != 0 && packet.target_system != 0 && packet.target_component != 0
	       && packet.flags == 0 && packet.reserved[0] == 0 && packet.reserved[1] == 0
	       && packet.reserved[2] == 0 && packet.reserved[3] == 0;
}

inline bool checkedAdd(TimeUs left, TimeUs right, TimeUs &result)
{
	if (left > std::numeric_limits<TimeUs>::max() - right) {
		return false;
	}

	result = left + right;
	return true;
}

inline bool checkedSubtract(TimeUs left, TimeUs right, TimeUs &result)
{
	if (left < right) {
		return false;
	}

	result = left - right;
	return true;
}

inline bool addSignedOffset(TimeUs producer, int64_t offset, TimeUs &validator)
{
	if (offset >= 0) {
		return checkedAdd(producer, static_cast<TimeUs>(offset), validator);
	}

	const TimeUs magnitude = static_cast<TimeUs>(-(offset + 1)) + 1U;
	return checkedSubtract(producer, magnitude, validator);
}

inline bool validMap(const TrustedClockMap &map)
{
	return map.valid && map.source != 0 && map.session != 0 && map.controller_instance_id != 0
	       && map.map_epoch != 0 && map.uncertainty_us != 0;
}

inline bool prestateAllowsIngress(DecisionCode code)
{
	return code == DecisionCode::Accepted || code == DecisionCode::NoEligibleRequest
	       || code == DecisionCode::ValidatedDisarmed || code == DecisionCode::DisarmedRejected
	       || code == DecisionCode::ModeNotSelected || code == DecisionCode::RequestExpired
	       || code == DecisionCode::RequestTooOld;
}

inline IngressCode canonicalizeIngressCode(CanonicalizeCode code)
{
	switch (code) {
	case CanonicalizeCode::MapInvalid:
		return IngressCode::MapInvalid;

	case CanonicalizeCode::MapBindingMismatch:
		return IngressCode::BindingMismatch;

	case CanonicalizeCode::MapIntervalInvalid:
		return IngressCode::MapIntervalInvalid;

	case CanonicalizeCode::MapArithmeticInvalid:
		return IngressCode::MapArithmeticInvalid;

	case CanonicalizeCode::WireVersionInvalid:
		return IngressCode::WireVersionInvalid;

	case CanonicalizeCode::WireEnumInvalid:
		return IngressCode::WireEnumInvalid;

	case CanonicalizeCode::WireTimeInvalid:
		return IngressCode::WireTimeInvalid;

	case CanonicalizeCode::WireNonFinite:
		return IngressCode::WireNonFinite;

	case CanonicalizeCode::WireIdentityInvalid:
		return IngressCode::MalformedPacket;

	case CanonicalizeCode::Ok:
		break;
	}

	return IngressCode::TrustedStateFault;
}

} // namespace wire_adapter_detail
} // namespace research_acceleration
