#include "SignedMavlink.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#if defined(MAVLINK_NO_SIGNATURE_CHECK)
#error "C1H requires generated MAVLink signature verification"
#endif
#if defined(MAVLINK_CHECK_MESSAGE_LENGTH)
#error "C1H validates logical zero-trimmed lengths independently of generated base MIN_LEN"
#endif

namespace research_sitl
{
namespace
{
using namespace research_acceleration;
constexpr size_t kSignedOverhead = MAVLINK_NUM_NON_PAYLOAD_BYTES + MAVLINK_SIGNATURE_BLOCK_LEN;
constexpr uint64_t kSigningLimit = uint64_t{1} << 48;
constexpr uint8_t kRequestLogicalMinimum = 85;
constexpr uint8_t kSessionLogicalMinimum = 71;

static_assert(MAVLINK_MSG_ID_RESEARCH_ACCELERATION_REQUEST == 200000, "private request ID");
static_assert(MAVLINK_MSG_ID_RESEARCH_ACCELERATION_SESSION == 200001, "private session ID");
static_assert(MAVLINK_MSG_ID_RESEARCH_ACCELERATION_STATUS == 200002, "private status ID");
static_assert(MAVLINK_MSG_ID_RESEARCH_ACCELERATION_REQUEST_LEN == 90, "request length");
static_assert(MAVLINK_MSG_ID_RESEARCH_ACCELERATION_SESSION_LEN == 76, "session length");
static_assert(MAVLINK_MSG_ID_RESEARCH_ACCELERATION_STATUS_LEN == 106, "status length");
static_assert(MAVLINK_MSG_ID_RESEARCH_ACCELERATION_REQUEST_CRC == 130, "request CRC extra");
static_assert(MAVLINK_MSG_ID_RESEARCH_ACCELERATION_SESSION_CRC == 188, "session CRC extra");
static_assert(MAVLINK_MSG_ID_RESEARCH_ACCELERATION_STATUS_CRC == 131, "status CRC extra");
static_assert(MAVLINK_NUM_HEADER_BYTES == 10 && kSignedOverhead == 25, "signed V2 layout");

void clearKey(uint8_t (&key)[32]) noexcept
{
	volatile uint8_t *bytes = key;

	for (size_t i = 0; i < sizeof(key); ++i) { bytes[i] = 0; }
}

struct CandidateSigning {
	mavlink_signing_t signing;
	mavlink_signing_streams_t streams;
	~CandidateSigning() { clearKey(signing.secret_key); }
};

bool reservedZero(const uint8_t (&reserved)[4]) noexcept
{
	return reserved[0] == 0 && reserved[1] == 0 && reserved[2] == 0 && reserved[3] == 0;
}
} // namespace

SignedMavlink::SignedMavlink(const SignedMavlinkConfig &config) noexcept : config_(config)
{
	// Only the private host experiment identities are implemented by this boundary.
	configured_ = config.source_system == 42 && config.source_component == 191
		      && config.target_system == 1 && config.target_component == 1
		      && config.incoming_link_id == 7 && config.outgoing_link_id == 8
		      && config.source != 0 && config.session != 0 && config.controller_instance != 0
		      && config.signing_seed != 0 && config.signing_seed < kSigningLimit - 1;
	std::memcpy(incoming_.secret_key, config.key, sizeof(config.key));
	std::memcpy(outgoing_.secret_key, config.key, sizeof(config.key));
	incoming_.timestamp = config.signing_seed;
	incoming_.link_id = config.incoming_link_id;
	outgoing_.timestamp = config.signing_seed;
	outgoing_.link_id = config.outgoing_link_id;
	outgoing_.flags = MAVLINK_SIGNING_FLAG_SIGN_OUTGOING;
}

SignedMavlink::~SignedMavlink()
{
	discardPending();
	clearKey(config_.key);
	clearKey(incoming_.secret_key);
	clearKey(outgoing_.secret_key);
}

SignedParseResult SignedMavlink::authenticate(const uint8_t *datagram, size_t length, bool channel_matches,
		uint64_t expected_transition, uint64_t expected_map, bool stage_only) noexcept
{
	discardPending();
	SignedParseResult result{};

	if (!configured_) { return result; }

	result.reason = SignedParseReason::Channel;

	if (!channel_matches) { return result; }

	result.reason = SignedParseReason::Envelope;

	if (datagram == nullptr || length < kSignedOverhead || length > MAVLINK_MAX_PACKET_LEN
	    || datagram[0] != MAVLINK_STX || datagram[2] != MAVLINK_IFLAG_SIGNED || datagram[3] != 0
	    || length != size_t(datagram[1]) + kSignedOverhead) { return result; }

	const uint32_t id = datagram[7] | (uint32_t(datagram[8]) << 8) | (uint32_t(datagram[9]) << 16);
	result.reason = SignedParseReason::MessageIdentity;

	if (id != MAVLINK_MSG_ID_RESEARCH_ACCELERATION_REQUEST && id != MAVLINK_MSG_ID_RESEARCH_ACCELERATION_SESSION) {
		return result;
	}

	const bool request = id == MAVLINK_MSG_ID_RESEARCH_ACCELERATION_REQUEST;
	const uint8_t minimum = request ? kRequestLogicalMinimum : kSessionLogicalMinimum;
	const uint8_t maximum = request ? MAVLINK_MSG_ID_RESEARCH_ACCELERATION_REQUEST_LEN :
				MAVLINK_MSG_ID_RESEARCH_ACCELERATION_SESSION_LEN;
	result.reason = SignedParseReason::LogicalLength;

	if (datagram[1] < minimum || datagram[1] > maximum) { return result; }

	CandidateSigning candidate{incoming_, streams_};
	candidate.signing.last_status = MAVLINK_SIGNING_STATUS_NONE;
	candidate.signing.accept_unsigned_callback = nullptr;
	mavlink_status_t parser{};
	mavlink_status_t completed{};
	mavlink_message_t receiving{};
	mavlink_message_t message{};
	parser.signing = &candidate.signing;
	parser.signing_streams = &candidate.streams;
	result.reason = SignedParseReason::Signature;

	for (size_t i = 0; i < length; ++i) {
		const auto framing = mavlink_frame_char_buffer(&receiving, &parser, datagram[i], &message, &completed);

		if (framing == MAVLINK_FRAMING_BAD_CRC) {
			result.reason = SignedParseReason::Crc;
			return result;
		}

		if (i + 1 != length) {
			if (framing != MAVLINK_FRAMING_INCOMPLETE) { return result; }

		} else if (framing != MAVLINK_FRAMING_OK || candidate.signing.last_status != MAVLINK_SIGNING_STATUS_OK) {
			if (candidate.signing.last_status == MAVLINK_SIGNING_STATUS_REPLAY
			    || candidate.signing.last_status == MAVLINK_SIGNING_STATUS_OLD_TIMESTAMP) {
				result.reason = SignedParseReason::Replay;
			}

			return result;
		}
	}

	result.signature_verified = true;
	result.reason = SignedParseReason::HeaderBinding;

	if (message.magic != MAVLINK_STX || message.msgid != id || message.len != datagram[1]
	    || message.sysid != config_.source_system || message.compid != config_.source_component) { return result; }

	result.reason = SignedParseReason::LinkBinding;

	if (message.signature[0] != config_.incoming_link_id) { return result; }

	uint64_t source = 0;
	uint64_t session = 0;
	uint64_t controller = 0;
	uint64_t transition = 0;
	uint64_t map = 0;
	uint8_t target_system = 0;
	uint8_t target_component = 0;
	result.reason = SignedParseReason::PayloadShape;

	if (request) {
		mavlink_research_acceleration_request_t value{};
		mavlink_msg_research_acceleration_request_decode(&message, &value);

		if (value.protocol_version != kWireProtocolVersionV1 || value.frame != kWireFrameFrdV1
		    || value.quantity != kWireQuantityInertialAccelerationV1 || value.reference_point != kWireReferencePointCgV1
		    || value.flags != 0 || !reservedZero(value.reserved)
		    || value.producer_expiry_monotonic_us <= value.producer_monotonic_us
		    || !std::isfinite(value.acceleration_x_frd_m_s2) || !std::isfinite(value.acceleration_y_frd_m_s2)
		    || !std::isfinite(value.acceleration_z_frd_m_s2)) { return result; }

		result.request.wire = {value.source_id, value.session_id, value.sequence, value.producer_monotonic_us,
				       value.producer_expiry_monotonic_us, value.controller_instance_id, value.transition_epoch,
				       value.clock_mapping_epoch, value.acceleration_x_frd_m_s2, value.acceleration_y_frd_m_s2,
				       value.acceleration_z_frd_m_s2, value.protocol_version, value.frame, value.quantity, value.reference_point
				      };
		result.request.target_system = value.target_system;
		result.request.target_component = value.target_component;
		source = value.source_id;
		session = value.session_id;
		controller = value.controller_instance_id;
		transition = value.transition_epoch;
		map = value.clock_mapping_epoch;
		target_system = value.target_system;
		target_component = value.target_component;

	} else {
		mavlink_research_acceleration_session_t value{};
		mavlink_msg_research_acceleration_session_decode(&message, &value);

		if (value.protocol_version != kWireProtocolVersionV1 || value.operation < 1 || value.operation > 3
		    || value.flags != 0 || !reservedZero(value.reserved)) { return result; }

		result.session = {value.controller_instance_id, value.transition_epoch, value.clock_mapping_epoch,
				  value.source_id, value.session_id, value.operation_sequence, value.producer_acknowledgement_monotonic_us,
				  value.acknowledged_status_sequence, value.protocol_version, value.target_system, value.target_component,
				  value.operation, value.flags, {0, 0, 0, 0}
				 };
		source = value.source_id;
		session = value.session_id;
		controller = value.controller_instance_id;
		transition = value.transition_epoch;
		map = value.clock_mapping_epoch;
		target_system = value.target_system;
		target_component = value.target_component;
	}

	result.reason = SignedParseReason::SourceBinding;

	if (source != config_.source || session != config_.session) { return result; }

	result.reason = SignedParseReason::TargetBinding;

	if (target_system != config_.target_system || target_component != config_.target_component) { return result; }

	result.reason = SignedParseReason::ContextBinding;

	if (controller != config_.controller_instance || expected_transition == 0 || expected_map == 0
	    || transition != expected_transition || map != expected_map) { return result; }

	// The live owner can defer the durable commit until its post-authentication
	// state/time sample confirms this context is still current.
	if (stage_only) {
		pending_signing_ = candidate.signing;
		pending_streams_ = candidate.streams;
		pending_transition_ = transition;
		pending_map_ = map;
		has_pending_ = true;

	} else {
		incoming_ = candidate.signing;
		streams_ = candidate.streams;
	}

	result.kind = request ? SignedPacketKind::Request : SignedPacketKind::Session;
	result.verdict = {message.magic == MAVLINK_STX, result.signature_verified, true, true};
	result.reason = SignedParseReason::Ok;
	return result;
}

void SignedMavlink::discardPending() noexcept
{
	clearKey(pending_signing_.secret_key);
	pending_signing_ = {};
	pending_streams_ = {};
	pending_transition_ = 0;
	pending_map_ = 0;
	has_pending_ = false;
}

bool SignedMavlink::commitPending(uint64_t expected_transition, uint64_t expected_map) noexcept
{
	const bool matches = has_pending_ && expected_transition != 0 && expected_map != 0
			     && pending_transition_ == expected_transition && pending_map_ == expected_map;

	if (matches) {
		incoming_ = pending_signing_;
		streams_ = pending_streams_;
	}

	discardPending();
	return matches;
}

bool SignedMavlink::signStatus(const research_acceleration::WireStatusV1 &status, uint64_t timestamp,
			       uint8_t *output, size_t capacity, size_t &size) noexcept
{
	using namespace research_acceleration;
	constexpr size_t required = MAVLINK_MSG_ID_RESEARCH_ACCELERATION_STATUS_LEN + kSignedOverhead;
	uint8_t checked_payload[MAVLINK_MSG_ID_RESEARCH_ACCELERATION_STATUS_LEN] {};

	if (!configured_ || output == nullptr || capacity < required || timestamp >= kSigningLimit - 1
	    || outgoing_.timestamp >= kSigningLimit - 1 || status.controller_instance_id != config_.controller_instance
	    || status.target_system != config_.source_system || status.target_component != config_.source_component
	    || encodeStatusV1Payload(status, checked_payload) != StatusEncodeCode::Ok) { return false; }

	mavlink_research_acceleration_status_t value{};
	value.controller_instance_id = status.controller_instance_id;
	value.transition_epoch = status.transition_epoch;
	value.clock_mapping_epoch = status.clock_mapping_epoch;
	value.source_id = status.source_id;
	value.session_id = status.session_id;
	value.correlation_sequence = status.correlation_sequence;
	value.last_accepted_sequence = status.last_accepted_sequence;
	value.validator_time_us = status.validator_time_us;
	value.status_sequence = status.status_sequence;
	value.rejection_count = status.rejection_count;
	value.conservative_age_us = status.conservative_age_us;
	value.protocol_version = status.protocol_version;
	value.decision_code = status.decision_code;
	value.first_latched_code = status.first_latched_code;
	value.status_flags = status.status_flags;
	value.target_system = status.target_system;
	value.target_component = status.target_component;
	value.status_class = status.status_class;
	value.arm_state = status.arm_state;
	value.research_mode_state = status.research_mode_state;
	value.session_state = status.session_state;
	value.transport_state = status.transport_state;
	value.clock_mapping_state = status.clock_mapping_state;
	CandidateSigning candidate{outgoing_, {}};
	candidate.signing.timestamp = std::max(timestamp, outgoing_.timestamp);
	mavlink_status_t transmitter{};
	transmitter.signing = &candidate.signing;
	transmitter.current_tx_seq = outgoing_sequence_;
	mavlink_message_t message{};
	mavlink_msg_research_acceleration_status_encode_status(config_.target_system, config_.target_component,
			&transmitter, &message, &value);
	uint8_t frame[MAVLINK_MAX_PACKET_LEN] {};
	const size_t count = mavlink_msg_to_send_buffer(frame, &message);

	if (count != required || message.magic != MAVLINK_STX || message.incompat_flags != MAVLINK_IFLAG_SIGNED
	    || message.compat_flags != 0 || message.signature[0] != config_.outgoing_link_id
	    || std::memcmp(_MAV_PAYLOAD(&message), checked_payload, sizeof(checked_payload)) != 0) { return false; }

	std::memcpy(output, frame, count);
	size = count;
	outgoing_ = candidate.signing;
	outgoing_sequence_ = transmitter.current_tx_seq;
	return true;
}

} // namespace research_sitl
