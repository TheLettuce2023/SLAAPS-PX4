#include "DiagnosticSignedMavlink.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

#if defined(MAVLINK_CHECK_MESSAGE_LENGTH)
#error "C1E checks W1 logical zero-tail lengths independently of generated MIN_LEN"
#endif
#if defined(MAVLINK_CRC_EXTRA) && !MAVLINK_CRC_EXTRA
#error "C1E requires MAVLink CRC extra"
#endif

namespace research_sitl
{
namespace
{
constexpr size_t kSignedOverhead = MAVLINK_NUM_NON_PAYLOAD_BYTES + MAVLINK_SIGNATURE_BLOCK_LEN;
constexpr uint64_t kSigningLimit = uint64_t{1} << 48;
constexpr uint8_t kRequestLogicalMinimum = 103;
constexpr uint8_t kStatusLogicalMinimum = 132;
constexpr uint32_t kProtocolVersion = 0x00010000;
constexpr uint8_t kDiagnosticDomain = 1;
constexpr uint8_t kDiagnosticProfile = 1;
constexpr uint8_t kNativePosixSitl = 2;
constexpr uint8_t kRejected = 1;
constexpr uint8_t kClosed = 2;

static_assert(MAVLINK_MSG_ID_RESEARCH_DIAGNOSTIC_REQUEST == 200010, "private request ID");
static_assert(MAVLINK_MSG_ID_RESEARCH_DIAGNOSTIC_STATUS == 200011, "private status ID");
static_assert(MAVLINK_MSG_ID_RESEARCH_DIAGNOSTIC_REQUEST_LEN == 108, "W1 request length");
static_assert(MAVLINK_MSG_ID_RESEARCH_DIAGNOSTIC_STATUS_LEN == 136, "W1 status length");
static_assert(MAVLINK_MSG_ID_RESEARCH_DIAGNOSTIC_REQUEST_CRC == 187, "W1 request CRC extra");
static_assert(MAVLINK_MSG_ID_RESEARCH_DIAGNOSTIC_STATUS_CRC == 159, "W1 status CRC extra");
static_assert(MAVLINK_NUM_HEADER_BYTES == 10 && kSignedOverhead == 25, "signed MAVLink 2 layout");
static_assert(sizeof(mavlink_research_diagnostic_request_t) <= 128, "bounded request value");
static_assert(sizeof(DiagnosticStageResult) <= 256, "bounded parser result");

void clearKey(uint8_t (&key)[32]) noexcept
{
	volatile uint8_t *bytes = key;

	for (size_t index = 0; index < sizeof(key); ++index) { bytes[index] = 0; }
}

bool nonzeroKey(const uint8_t (&key)[32]) noexcept
{
	uint8_t combined = 0;

	for (uint8_t byte : key) { combined |= byte; }

	return combined != 0;
}

bool reservedZero(const uint8_t (&reserved)[4]) noexcept
{
	return reserved[0] == 0 && reserved[1] == 0 && reserved[2] == 0 && reserved[3] == 0;
}

uint64_t signatureTime(const uint8_t (&signature)[MAVLINK_SIGNATURE_BLOCK_LEN]) noexcept
{
	uint64_t value = 0;

	for (size_t index = 0; index < 6; ++index) { value |= uint64_t(signature[index + 1]) << (index * 8); }

	return value;
}

struct CandidateSigning {
	mavlink_signing_t signing;
	mavlink_signing_streams_t streams;
	~CandidateSigning() { clearKey(signing.secret_key); }
};

bool validRequestShape(const mavlink_research_diagnostic_request_t &value) noexcept
{
	return value.native_run_id != 0 && value.owner_lifetime_id != 0 && value.controller_instance_id != 0
	       && value.source_id != 0 && value.transport_session_id != 0 && value.request_sequence != 0
	       && value.expected_transition_epoch != 0 && value.producer_monotonic_us != 0
	       && value.producer_expiry_monotonic_us > value.producer_monotonic_us
	       && value.clock_mapping_epoch == 0 && value.protocol_version == kProtocolVersion
	       && value.domain == kDiagnosticDomain && value.profile == kDiagnosticProfile
	       && value.target_system != 0 && value.target_component != 0
	       && value.frame == 1 && value.quantity == 1 && value.reference_point == 1
	       && value.flags == 0 && reservedZero(value.reserved)
	       && std::isfinite(value.acceleration_x_frd_m_s2)
	       && std::isfinite(value.acceleration_y_frd_m_s2)
	       && std::isfinite(value.acceleration_z_frd_m_s2);
}

} // namespace

DiagnosticSignedMavlink::DiagnosticSignedMavlink(const DiagnosticParserConfig &config) noexcept : config_(config)
{
	configured_ = config.source_system == 42 && config.source_component == 191
		      && config.target_system == 1 && config.target_component == 1
		      && config.incoming_link_id == 7 && config.outgoing_link_id == 8
		      && config.native_run_id != 0 && config.owner_lifetime_id != 0
		      && config.controller_instance_id != 0 && config.source_id != 0
		      && config.transport_session_id != 0 && config.signing_seed != 0
		      && config.signing_seed < kSigningLimit - 1 && nonzeroKey(config.key);
	std::memcpy(incoming_.secret_key, config.key, sizeof(config.key));
	std::memcpy(outgoing_.secret_key, config.key, sizeof(config.key));
	incoming_.timestamp = config.signing_seed;
	incoming_.link_id = config.incoming_link_id;
	outgoing_.timestamp = config.signing_seed;
	outgoing_.link_id = config.outgoing_link_id;
	outgoing_.flags = MAVLINK_SIGNING_FLAG_SIGN_OUTGOING;
}

DiagnosticSignedMavlink::~DiagnosticSignedMavlink()
{
	retire();
}

DiagnosticStageResult DiagnosticSignedMavlink::stageDatagram(ByteSpan datagram, uint8_t channel) noexcept
{
	DiagnosticStageResult result{};

	if (closed_) { result.code = DiagnosticStageCode::Closed; return result; }

	if (!configured_) { return result; }

	if (has_pending_) { result.code = DiagnosticStageCode::Busy; return result; }

	result.code = DiagnosticStageCode::WrongChannel;

	if (channel != config_.channel) { return result; }

	result.code = DiagnosticStageCode::MalformedEnvelope;

	if (datagram.data == nullptr || datagram.size < kSignedOverhead || datagram.size > MAVLINK_MAX_PACKET_LEN
	    || datagram.data[0] != MAVLINK_STX || datagram.data[2] != MAVLINK_IFLAG_SIGNED
	    || datagram.data[3] != 0 || datagram.size != size_t(datagram.data[1]) + kSignedOverhead) { return result; }

	const uint32_t id = uint32_t(datagram.data[7]) | (uint32_t(datagram.data[8]) << 8)
			    | (uint32_t(datagram.data[9]) << 16);
	result.code = DiagnosticStageCode::WrongMessage;

	if (id != MAVLINK_MSG_ID_RESEARCH_DIAGNOSTIC_REQUEST) { return result; }

	result.code = DiagnosticStageCode::MalformedEnvelope;

	if (datagram.data[1] < kRequestLogicalMinimum
	    || datagram.data[1] > MAVLINK_MSG_ID_RESEARCH_DIAGNOSTIC_REQUEST_LEN) { return result; }

	CandidateSigning candidate{incoming_, streams_};
	candidate.signing.last_status = MAVLINK_SIGNING_STATUS_NONE;
	candidate.signing.accept_unsigned_callback = nullptr;
	mavlink_status_t parser{};
	mavlink_status_t completed{};
	mavlink_message_t receiving{};
	mavlink_message_t message{};
	parser.signing = &candidate.signing;
	parser.signing_streams = &candidate.streams;
	result.code = DiagnosticStageCode::BadSignature;

	for (size_t index = 0; index < datagram.size; ++index) {
		const auto framing = mavlink_frame_char_buffer(&receiving, &parser, datagram.data[index], &message, &completed);

		if (framing == MAVLINK_FRAMING_BAD_CRC) {
			result.code = DiagnosticStageCode::BadCrc;
			return result;
		}

		if (index + 1 != datagram.size) {
			if (framing != MAVLINK_FRAMING_INCOMPLETE) { return result; }

		} else if (framing != MAVLINK_FRAMING_OK || candidate.signing.last_status != MAVLINK_SIGNING_STATUS_OK) {
			if (candidate.signing.last_status == MAVLINK_SIGNING_STATUS_REPLAY
			    || candidate.signing.last_status == MAVLINK_SIGNING_STATUS_OLD_TIMESTAMP) {
				result.code = DiagnosticStageCode::Replay;
			}

			return result;
		}
	}

	result.code = DiagnosticStageCode::WrongHeader;

	if (message.magic != MAVLINK_STX || message.msgid != id || message.len != datagram.data[1]
	    || message.sysid != config_.source_system || message.compid != config_.source_component) { return result; }

	result.code = DiagnosticStageCode::WrongLink;

	if (message.signature[0] != config_.incoming_link_id) { return result; }

	mavlink_research_diagnostic_request_t value{};
	mavlink_msg_research_diagnostic_request_decode(&message, &value);
	result.code = DiagnosticStageCode::InvalidPayload;

	if (!validRequestShape(value)) { return result; }

	result.code = DiagnosticStageCode::WrongIdentity;

	if (value.native_run_id != config_.native_run_id || value.owner_lifetime_id != config_.owner_lifetime_id
	    || value.controller_instance_id != config_.controller_instance_id || value.source_id != config_.source_id
	    || value.transport_session_id != config_.transport_session_id) { return result; }

	result.code = DiagnosticStageCode::WrongTarget;

	if (value.target_system != config_.target_system || value.target_component != config_.target_component) {
		return result;
	}

	result.code = DiagnosticStageCode::ResourceFault;

	if (next_token_ == std::numeric_limits<uint64_t>::max()) { return result; }

	pending_signing_ = candidate.signing;
	pending_streams_ = candidate.streams;
	pending_token_ = ++next_token_;
	has_pending_ = true;
	result.request = value;
	result.token = pending_token_;
	result.frame.signing_timestamp = signatureTime(message.signature);
	result.frame.checksum = message.checksum;
	std::memcpy(result.frame.signature, message.signature, sizeof(result.frame.signature));
	result.frame.sequence = message.seq;
	result.frame.system = message.sysid;
	result.frame.component = message.compid;
	result.frame.channel = channel;
	result.frame.link_id = message.signature[0];
	result.frame.payload_length = message.len;
	result.code = DiagnosticStageCode::Published;
	return result;
}

void DiagnosticSignedMavlink::erasePending() noexcept
{
	clearKey(pending_signing_.secret_key);
	pending_signing_ = {};
	pending_streams_ = {};
	pending_token_ = 0;
	has_pending_ = false;
}

bool DiagnosticSignedMavlink::commitPending(uint64_t token) noexcept
{
	if (!has_pending_ || token == 0 || token != pending_token_) { return false; }

	incoming_ = pending_signing_;
	streams_ = pending_streams_;
	erasePending();
	return true;
}

bool DiagnosticSignedMavlink::discardPending(uint64_t token) noexcept
{
	if (!has_pending_ || token == 0 || token != pending_token_) { return false; }

	erasePending();
	return true;
}

void DiagnosticSignedMavlink::retire() noexcept
{
	erasePending();
	clearKey(config_.key);
	clearKey(incoming_.secret_key);
	clearKey(outgoing_.secret_key);
	configured_ = false;
	closed_ = true;
}

bool DiagnosticSignedMavlink::validStatus(const mavlink_research_diagnostic_status_t &value) const noexcept
{
	if (value.native_run_id != config_.native_run_id || value.owner_lifetime_id != config_.owner_lifetime_id
	    || value.controller_instance_id != config_.controller_instance_id || value.source_id != config_.source_id
	    || value.transport_session_id != config_.transport_session_id || value.request_sequence == 0
	    || value.status_sequence == 0 || value.transition_epoch == 0 || value.source_sequence == 0
	    || value.state_generation == 0 || value.arrival_native_us == 0 || value.evaluation_native_us == 0
	    || value.source_checked_native_us == 0 || value.arrival_native_us > value.evaluation_native_us
	    || value.source_checked_native_us > value.evaluation_native_us
	    || value.source_sequence < value.state_generation || value.clock_mapping_epoch != 0
	    || value.protocol_version != kProtocolVersion || value.domain != kDiagnosticDomain
	    || value.profile != kDiagnosticProfile || value.execution_origin != kNativePosixSitl
	    || value.arm_state < 1 || value.arm_state > 2 || value.raw_nav_state > 30
	    || value.raw_nav_state == 9 || value.raw_nav_state == 11 || value.raw_nav_state == 16
	    || value.research_mode_state != 0 || value.selector_state != 0 || value.estimator_valid != 0
	    || value.clock_map_valid != 0 || value.eligible_for_control != 0 || value.flags != 0
	    || value.target_system != config_.source_system || value.target_component != config_.source_component
	    || !reservedZero(value.reserved)) { return false; }

	return (value.outcome == kRejected && value.reason >= 1 && value.reason <= 5)
	       || (value.outcome == kClosed && value.reason >= 6 && value.reason <= 8);
}

bool DiagnosticSignedMavlink::signStatus(const mavlink_research_diagnostic_status_t &status,
		uint64_t signing_clock, uint8_t *output, size_t capacity, size_t &size) noexcept
{
	if (!configured_ || !validStatus(status) || output == nullptr || signing_clock >= kSigningLimit - 1
	    || outgoing_.timestamp >= kSigningLimit - 1) { return false; }

	CandidateSigning candidate{outgoing_, {}};
	candidate.signing.timestamp = std::max(signing_clock, outgoing_.timestamp + 1);
	mavlink_status_t transmitter{};
	transmitter.signing = &candidate.signing;
	transmitter.current_tx_seq = outgoing_sequence_;
	mavlink_message_t message{};
	mavlink_msg_research_diagnostic_status_encode_status(config_.target_system, config_.target_component,
			&transmitter, &message, &status);
	uint8_t frame[MAVLINK_MAX_PACKET_LEN] {};
	const size_t count = mavlink_msg_to_send_buffer(frame, &message);

	if (message.magic != MAVLINK_STX || message.incompat_flags != MAVLINK_IFLAG_SIGNED
	    || message.compat_flags != 0 || message.msgid != MAVLINK_MSG_ID_RESEARCH_DIAGNOSTIC_STATUS
	    || message.sysid != config_.target_system || message.compid != config_.target_component
	    || message.signature[0] != config_.outgoing_link_id
	    || message.len < kStatusLogicalMinimum || message.len > MAVLINK_MSG_ID_RESEARCH_DIAGNOSTIC_STATUS_LEN
	    || count != size_t(message.len) + kSignedOverhead || count > capacity
	    || signatureTime(message.signature) <= outgoing_.timestamp
	    || signatureTime(message.signature) >= kSigningLimit) { return false; }

	std::memcpy(output, frame, count);
	size = count;
	outgoing_ = candidate.signing;
	outgoing_sequence_ = transmitter.current_tx_seq;
	return true;
}

} // namespace research_sitl
