#pragma once

#include "WireAdapter.hpp"

#include <cstdint>

namespace research_acceleration
{

enum class StatusDelivery : uint8_t { Directed, OperatorBroadcastSnapshot };

struct StatusRecipient {
	uint8_t target_system;
	uint8_t target_component;
	StatusDelivery delivery;
};

// Typed frozen C0a payload fields, not a packed struct or a MAVLink frame.
struct WireStatusV1 {
	uint64_t controller_instance_id;
	uint64_t transition_epoch;
	uint64_t clock_mapping_epoch;
	uint64_t source_id;
	uint64_t session_id;
	uint64_t correlation_sequence;
	uint64_t last_accepted_sequence;
	TimeUs validator_time_us;
	uint64_t status_sequence;
	uint64_t rejection_count;
	TimeUs conservative_age_us;
	uint32_t protocol_version;
	uint16_t decision_code;
	uint16_t first_latched_code;
	uint16_t status_flags;
	uint8_t target_system;
	uint8_t target_component;
	uint8_t status_class;
	uint8_t arm_state;
	uint8_t research_mode_state;
	uint8_t session_state;
	uint8_t transport_state;
	uint8_t clock_mapping_state;
};

enum class StatusBuildCode : uint8_t {
	Ok, InvalidRecipient, MissingEvidence, InvalidIdentity, InvalidReport,
	UnrepresentableDecision, InvalidStateCombination
};
enum class StatusEncodeCode : uint8_t { Ok, InvalidStatus };

// Pure conversion of historical evidence. Every failure leaves output unchanged.
bool mapDecisionCodeV1(DecisionCode code, uint16_t &output);
StatusBuildCode buildStatusV1(const AdapterReport &report, const StatusRecipient &recipient, WireStatusV1 &output);
StatusBuildCode buildStatusV1(const AdminReport &report, const StatusRecipient &recipient, WireStatusV1 &output);
StatusBuildCode buildStatusV1(const IngressReport &report, const StatusRecipient &recipient, WireStatusV1 &output);
StatusEncodeCode encodeStatusV1Payload(const WireStatusV1 &status, uint8_t (&output)[106]);

} // namespace research_acceleration
