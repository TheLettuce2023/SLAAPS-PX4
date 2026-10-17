// Offline generated-code conformance fixture; no transport or authority claim.
#include <diagnostic/mavlink.h>

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
constexpr uint8_t kSystem = 42;
constexpr uint8_t kComponent = 191;
constexpr uint8_t kSequence = 17;
constexpr uint32_t kProtocolVersion = 0x00010000;
constexpr size_t kUnsignedOverhead = 12;

static_assert(MAVLINK_MSG_ID_RESEARCH_DIAGNOSTIC_REQUEST == 200010, "request ID");
static_assert(MAVLINK_MSG_ID_RESEARCH_DIAGNOSTIC_STATUS == 200011, "status ID");
static_assert(MAVLINK_MSG_ID_RESEARCH_DIAGNOSTIC_REQUEST_LEN == 108, "request length");
static_assert(MAVLINK_MSG_ID_RESEARCH_DIAGNOSTIC_STATUS_LEN == 136, "status length");
// Generated base MIN_LEN differs from the W1 logical zero-tail minimum.
static_assert(MAVLINK_MSG_ID_RESEARCH_DIAGNOSTIC_REQUEST_MIN_LEN == 108, "request base length");
static_assert(MAVLINK_MSG_ID_RESEARCH_DIAGNOSTIC_STATUS_MIN_LEN == 136, "status base length");

void printHex(const uint8_t *bytes, size_t count)
{
	constexpr char digits[] = "0123456789abcdef";

	for (size_t index = 0; index < count; ++index) {
		std::cout << digits[bytes[index] >> 4] << digits[bytes[index] & 15];
	}
}

void printFrame(const mavlink_message_t &message)
{
	uint8_t bytes[MAVLINK_MAX_PACKET_LEN] {};
	const auto length = mavlink_msg_to_send_buffer(bytes, &message);

	if (message.magic != MAVLINK_STX || message.incompat_flags != 0 || message.compat_flags != 0
	    || length != message.len + kUnsignedOverhead) {
		throw std::runtime_error("encoder did not produce unsigned MAVLink 2");
	}

	printHex(bytes, length);
	std::cout << '\n';
}

void emit()
{
	// Values match the independently transcribed W1 diagnostic byte vectors.
	mavlink_research_diagnostic_request_t request{};
	request.native_run_id = 11;
	request.owner_lifetime_id = 12;
	request.controller_instance_id = 13;
	request.source_id = 14;
	request.transport_session_id = 15;
	request.request_sequence = 16;
	request.expected_transition_epoch = 17;
	request.producer_monotonic_us = 100;
	request.producer_expiry_monotonic_us = 180;
	request.acceleration_x_frd_m_s2 = 1.5F;
	request.acceleration_y_frd_m_s2 = -2.25F;
	request.acceleration_z_frd_m_s2 = 0.5F;
	request.protocol_version = kProtocolVersion;
	request.domain = 1;
	request.profile = 1;
	request.target_system = kSystem;
	request.target_component = kComponent;
	request.frame = 1;
	request.quantity = 1;
	request.reference_point = 1;
	mavlink_message_t message{};
	mavlink_status_t transmit{};
	transmit.current_tx_seq = kSequence;
	mavlink_msg_research_diagnostic_request_encode_status(kSystem, kComponent, &transmit, &message, &request);
	printFrame(message);

	mavlink_research_diagnostic_status_t status{};
	status.native_run_id = 11;
	status.owner_lifetime_id = 12;
	status.controller_instance_id = 13;
	status.source_id = 14;
	status.transport_session_id = 15;
	status.request_sequence = 16;
	status.status_sequence = 21;
	status.transition_epoch = 17;
	status.source_sequence = 3;
	status.state_generation = 2;
	status.arrival_native_us = 1000;
	status.evaluation_native_us = 1002;
	status.source_checked_native_us = 1001;
	status.protocol_version = kProtocolVersion;
	status.reason = 1;
	status.domain = 1;
	status.profile = 1;
	status.execution_origin = 1;
	status.outcome = 1;
	status.arm_state = 1;
	status.raw_nav_state = 3;
	status.target_system = kSystem;
	status.target_component = kComponent;
	transmit.current_tx_seq = kSequence;
	mavlink_msg_research_diagnostic_status_encode_status(kSystem, kComponent, &transmit, &message, &status);
	printFrame(message);
}

int nibble(char value)
{
	if (value >= '0' && value <= '9') { return value - '0'; }

	if (value >= 'a' && value <= 'f') { return value - 'a' + 10; }

	if (value >= 'A' && value <= 'F') { return value - 'A' + 10; }

	return -1;
}

bool parse(const std::string &hex, bool framed_output)
{
	if (hex.size() % 2 != 0 || hex.size() < kUnsignedOverhead * 2
	    || hex.size() > MAVLINK_MAX_PACKET_LEN * 2) { return false; }

	std::vector<uint8_t> bytes;
	bytes.reserve(hex.size() / 2);

	for (size_t offset = 0; offset < hex.size(); offset += 2) {
		const int high = nibble(hex[offset]);
		const int low = nibble(hex[offset + 1]);

		if (high < 0 || low < 0) { return false; }

		bytes.push_back(static_cast<uint8_t>(high * 16 + low));
	}

	// The wrapper accepts exactly one unsigned MAVLink 2 diagnostic frame.
	// Generated mavlink_frame_char_buffer validates the frame and CRC below.
	if (bytes[0] != MAVLINK_STX || bytes[2] != 0 || bytes[3] != 0
	    || bytes.size() != bytes[1] + kUnsignedOverhead) { return false; }

	const uint32_t id = bytes[7] | (uint32_t(bytes[8]) << 8) | (uint32_t(bytes[9]) << 16);
	unsigned nominal = 0;
	unsigned minimum = 0;

	switch (id) {
	case MAVLINK_MSG_ID_RESEARCH_DIAGNOSTIC_REQUEST: nominal = 108; minimum = 103; break;

	case MAVLINK_MSG_ID_RESEARCH_DIAGNOSTIC_STATUS: nominal = 136; minimum = 132; break;

	default: break; // Parse unknown IDs, but never dispatch them as diagnostic.
	}

	if (nominal != 0 && (bytes[1] < minimum || bytes[1] > nominal)) { return false; }

	mavlink_message_t receiving{};
	mavlink_message_t decoded{};
	mavlink_status_t parser{};
	mavlink_status_t result{};

	for (size_t index = 0; index < bytes.size(); ++index) {
		const auto framing = mavlink_frame_char_buffer(&receiving, &parser, bytes[index], &decoded, &result);

		if (index + 1 == bytes.size()) {
			if (framing != MAVLINK_FRAMING_OK) { return false; }

		} else if (framing != MAVLINK_FRAMING_INCOMPLETE) { return false; }
	}

	if (decoded.msgid != id || nominal == 0) { return false; }

	mavlink_message_t reencoded{};
	mavlink_status_t output{};
	output.current_tx_seq = decoded.seq;

	switch (id) {
	case MAVLINK_MSG_ID_RESEARCH_DIAGNOSTIC_REQUEST: {
			mavlink_research_diagnostic_request_t value{};
			mavlink_msg_research_diagnostic_request_decode(&decoded, &value);
			mavlink_msg_research_diagnostic_request_encode_status(decoded.sysid, decoded.compid, &output, &reencoded, &value);
			break;
		}

	case MAVLINK_MSG_ID_RESEARCH_DIAGNOSTIC_STATUS: {
			mavlink_research_diagnostic_status_t value{};
			mavlink_msg_research_diagnostic_status_decode(&decoded, &value);
			mavlink_msg_research_diagnostic_status_encode_status(decoded.sysid, decoded.compid, &output, &reencoded, &value);
			break;
		}
	}

	if (framed_output) { printFrame(reencoded); return true; }

	if (reencoded.len > nominal) { return false; }

	std::cout << id << ' ';
	// Finalization stores checksum bytes after the trimmed payload in payload64.
	printHex(reinterpret_cast<const uint8_t *>(_MAV_PAYLOAD(&reencoded)), reencoded.len);
	const uint8_t zeros[MAVLINK_MAX_PAYLOAD_LEN] {};
	printHex(zeros, nominal - reencoded.len);
	std::cout << '\n';
	return true;
}
} // namespace

int main(int argc, char **argv)
{
	try {
		if (argc == 2 && std::string(argv[1]) == "--emit") { emit(); return 0; }

		if (argc == 3 && std::string(argv[1]) == "--parse") { return parse(argv[2], false) ? 0 : 1; }

		if (argc == 3 && std::string(argv[1]) == "--roundtrip") { return parse(argv[2], true) ? 0 : 1; }

	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}

	std::cerr << "usage: fixture --emit | --parse HEX | --roundtrip HEX\n";
	return 2;
}
