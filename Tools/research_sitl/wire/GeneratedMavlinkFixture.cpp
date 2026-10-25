// Generated-binding cross-language fixture; no runtime transport or authority.
#ifndef GENERATED_MAVLINK_HEADER
#define GENERATED_MAVLINK_HEADER <research/mavlink.h>
#endif
#include GENERATED_MAVLINK_HEADER

#ifndef GENERATED_MAVLINK_REQUEST_ID
#define GENERATED_MAVLINK_REQUEST_ID 200000
#endif

#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
constexpr uint8_t kFixtureSystem = 42;
constexpr uint8_t kFixtureComponent = 191;
constexpr uint8_t kFixtureSequence = 17;
constexpr uint32_t kProtocolVersion = 0x00010000;
constexpr unsigned kUnsignedFrameOverhead = 12;

static_assert(MAVLINK_MSG_ID_RESEARCH_ACCELERATION_REQUEST == GENERATED_MAVLINK_REQUEST_ID, "request ID");
static_assert(MAVLINK_MSG_ID_RESEARCH_ACCELERATION_SESSION == GENERATED_MAVLINK_REQUEST_ID + 1, "session ID");
static_assert(MAVLINK_MSG_ID_RESEARCH_ACCELERATION_STATUS == GENERATED_MAVLINK_REQUEST_ID + 2, "status ID");
static_assert(MAVLINK_MSG_ID_RESEARCH_ACCELERATION_REQUEST_LEN ==
	      (GENERATED_MAVLINK_REQUEST_ID == 200010 ? 106 : 90), "request length");
static_assert(MAVLINK_MSG_ID_RESEARCH_ACCELERATION_SESSION_LEN == 76, "session length");
static_assert(MAVLINK_MSG_ID_RESEARCH_ACCELERATION_STATUS_LEN == 106, "status length");
// Generated base MIN_LEN is not the contract's trimmed logical minimum.
static_assert(MAVLINK_MSG_ID_RESEARCH_ACCELERATION_REQUEST_MIN_LEN == 90, "request base length");
static_assert(MAVLINK_MSG_ID_RESEARCH_ACCELERATION_SESSION_MIN_LEN == 76, "session base length");
static_assert(MAVLINK_MSG_ID_RESEARCH_ACCELERATION_STATUS_MIN_LEN == 106, "status base length");

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
	    || length != message.len + kUnsignedFrameOverhead) {
		throw std::runtime_error("encoder did not produce an unsigned MAVLink 2 frame");
	}

	printHex(bytes, length);
	std::cout << '\n';
}

void emit()
{
	// Independently transcribed golden values from the frozen wire tables/tests.
	mavlink_research_acceleration_request_t request{};
	request.source_id = 1;
	request.session_id = 2;
	request.sequence = 3;
	request.producer_monotonic_us = 4;
	request.producer_expiry_monotonic_us = 5;
	request.controller_instance_id = 6;
	request.transition_epoch = 7;
	request.clock_mapping_epoch = 8;
	request.acceleration_x_frd_m_s2 = 1.5F;
	request.acceleration_y_frd_m_s2 = -2.25F;
	request.acceleration_z_frd_m_s2 = 0.5F;
	request.protocol_version = kProtocolVersion;
	request.target_system = 9;
	request.target_component = 10;
	request.frame = 1;
	request.quantity = 1;
	request.reference_point = 1;
	mavlink_message_t message{};
	mavlink_status_t status{};
	status.current_tx_seq = kFixtureSequence;
	mavlink_msg_research_acceleration_request_encode_status(kFixtureSystem, kFixtureComponent, &status, &message, &request);
	printFrame(message);

	mavlink_research_acceleration_session_t session{};
	session.controller_instance_id = 11;
	session.transition_epoch = 12;
	session.clock_mapping_epoch = 13;
	session.source_id = 14;
	session.session_id = 15;
	session.operation_sequence = 16;
	session.producer_acknowledgement_monotonic_us = 17;
	session.acknowledged_status_sequence = 18;
	session.protocol_version = kProtocolVersion;
	session.target_system = 19;
	session.target_component = 20;
	session.operation = 3;
	status.current_tx_seq = kFixtureSequence;
	mavlink_msg_research_acceleration_session_encode_status(kFixtureSystem, kFixtureComponent, &status, &message, &session);
	printFrame(message);

	mavlink_research_acceleration_status_t report{};
	report.controller_instance_id = 21;
	report.transition_epoch = 22;
	report.clock_mapping_epoch = 23;
	report.source_id = 24;
	report.session_id = 25;
	report.correlation_sequence = 26;
	report.last_accepted_sequence = 27;
	report.validator_time_us = 28;
	report.status_sequence = 29;
	report.rejection_count = 30;
	report.conservative_age_us = 31;
	report.protocol_version = kProtocolVersion;
	report.decision_code = 1;
	report.status_flags = 59;
	report.target_system = 33;
	report.target_component = 34;
	report.status_class = 1;
	report.arm_state = 2;
	report.research_mode_state = 2;
	report.session_state = 2;
	report.transport_state = 1;
	report.clock_mapping_state = 1;
	status.current_tx_seq = kFixtureSequence;
	mavlink_msg_research_acceleration_status_encode_status(kFixtureSystem, kFixtureComponent, &status, &message, &report);
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
	if (hex.size() % 2 != 0 || hex.size() < kUnsignedFrameOverhead * 2
	    || hex.size() > MAVLINK_MAX_PACKET_LEN * 2) { return false; }

	std::vector<uint8_t> bytes;

	for (size_t offset = 0; offset < hex.size(); offset += 2) {
		const int high = nibble(hex[offset]);
		const int low = nibble(hex[offset + 1]);

		if (high < 0 || low < 0) { return false; }

		bytes.push_back(static_cast<uint8_t>(high * 16 + low));
	}

	// This test wrapper accepts exactly one unsigned MAVLink 2 frame, no stream
	// resynchronization or authentication claims. Generated bindings do parsing.
	if (bytes[0] != MAVLINK_STX || bytes[2] != 0 || bytes[3] != 0
	    || bytes.size() != bytes[1] + kUnsignedFrameOverhead) { return false; }

	const uint32_t id = bytes[7] | (uint32_t(bytes[8]) << 8) | (uint32_t(bytes[9]) << 16);
	unsigned nominal = 0;
	unsigned minimum = 0;

	switch (id) {
	case MAVLINK_MSG_ID_RESEARCH_ACCELERATION_REQUEST:
		nominal = MAVLINK_MSG_ID_RESEARCH_ACCELERATION_REQUEST_LEN; minimum = 85; break;

	case MAVLINK_MSG_ID_RESEARCH_ACCELERATION_SESSION: nominal = 76; minimum = 71; break;

	case MAVLINK_MSG_ID_RESEARCH_ACCELERATION_STATUS: nominal = 106; minimum = 106; break;

	default: break; // Unknown IDs exercise generated framing, never typed dispatch.
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
	mavlink_status_t transmit{};
	transmit.current_tx_seq = decoded.seq;

	switch (id) {
	case MAVLINK_MSG_ID_RESEARCH_ACCELERATION_REQUEST: {
			mavlink_research_acceleration_request_t value{};
			mavlink_msg_research_acceleration_request_decode(&decoded, &value);
#if MAVLINK_MSG_ID_RESEARCH_ACCELERATION_REQUEST_LEN == 106
			float attitude[4] {};
			mavlink_msg_research_acceleration_request_get_desired_attitude_ned_from_body(&decoded, attitude);

			if (std::memcmp(attitude, value.desired_attitude_ned_from_body, sizeof(attitude)) != 0) { return false; }

#endif
			mavlink_msg_research_acceleration_request_encode_status(decoded.sysid, decoded.compid, &transmit, &reencoded, &value);
			break;
		}

	case MAVLINK_MSG_ID_RESEARCH_ACCELERATION_SESSION: {
			mavlink_research_acceleration_session_t value{};
			mavlink_msg_research_acceleration_session_decode(&decoded, &value);
			mavlink_msg_research_acceleration_session_encode_status(decoded.sysid, decoded.compid, &transmit, &reencoded, &value);
			break;
		}

	case MAVLINK_MSG_ID_RESEARCH_ACCELERATION_STATUS: {
			mavlink_research_acceleration_status_t value{};
			mavlink_msg_research_acceleration_status_decode(&decoded, &value);
			mavlink_msg_research_acceleration_status_encode_status(decoded.sysid, decoded.compid, &transmit, &reencoded, &value);
			break;
		}
	}

	if (framed_output) { printFrame(reencoded); return true; }

	std::cout << id << ' ';

	// Finalization stores checksum bytes immediately after the trimmed payload
	// in payload64. Those bytes are NOT absent zero-tail fields.
	if (reencoded.len > nominal) { return false; }

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
