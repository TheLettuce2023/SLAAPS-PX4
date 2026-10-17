// Offline parser unit tests. Public disposable fixture bytes are not deployment keys.
#include "SignedMavlink.hpp"

#include <cstring>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace
{
using namespace research_sitl;
using namespace research_acceleration;
using Frame = std::vector<uint8_t>;
constexpr uint64_t kSeed = 10000000;
constexpr uint64_t kTransition = 5;
constexpr uint64_t kMap = 6;
unsigned checks = 0;

void require(bool value, const char *message)
{
	if (!value) { throw std::runtime_error(message); }

	++checks;
}

SignedMavlinkConfig configuration()
{
	SignedMavlinkConfig config{};

	for (unsigned i = 0; i < sizeof(config.key); ++i) { config.key[i] = static_cast<uint8_t>(i + 1); }

	config.source = 11;
	config.session = 12;
	config.controller_instance = 13;
	config.signing_seed = kSeed;
	return config;
}

Frame requestFrame(uint64_t timestamp,
		   const std::function<void(mavlink_research_acceleration_request_t &)> &edit = {},
		   uint8_t system = 42, uint8_t component = 191, uint8_t link = 7,
		   bool wrong_key = false, int length = -1, bool sign = true)
{
	const auto config = configuration();
	mavlink_research_acceleration_request_t value{};
	value.source_id = config.source;
	value.session_id = config.session;
	value.sequence = 1;
	value.producer_monotonic_us = 100;
	value.producer_expiry_monotonic_us = 80100;
	value.controller_instance_id = config.controller_instance;
	value.transition_epoch = kTransition;
	value.clock_mapping_epoch = kMap;
	value.acceleration_x_frd_m_s2 = 1.25F;
	value.protocol_version = kWireProtocolVersionV1;
	value.target_system = 1;
	value.target_component = 1;
	value.frame = 1;
	value.quantity = 1;
	value.reference_point = 1;

	if (edit) { edit(value); }

	mavlink_signing_t signing{};
	std::memcpy(signing.secret_key, config.key, sizeof(config.key));

	if (wrong_key) { signing.secret_key[0] ^= 1; }

	signing.flags = MAVLINK_SIGNING_FLAG_SIGN_OUTGOING;
	signing.link_id = link;
	signing.timestamp = timestamp;
	mavlink_status_t status{};
	status.signing = sign ? &signing : nullptr;
	mavlink_message_t message{};
	mavlink_msg_research_acceleration_request_encode_status(system, component, &status, &message, &value);

	if (length >= 0) {
		// Restore absent zero-tail bytes overwritten by finalization's checksum.
		std::memset(_MAV_PAYLOAD_NON_CONST(&message) + message.len, 0, MAVLINK_MAX_PAYLOAD_LEN - message.len);

		if (length > MAVLINK_MSG_ID_RESEARCH_ACCELERATION_REQUEST_LEN) {
			_MAV_PAYLOAD_NON_CONST(&message)[length - 1] = 1;
		}

		signing.timestamp = timestamp;
		mavlink_finalize_message_buffer(&message, system, component, &status, static_cast<uint8_t>(length),
						static_cast<uint8_t>(length), MAVLINK_MSG_ID_RESEARCH_ACCELERATION_REQUEST_CRC);
	}

	uint8_t output[MAVLINK_MAX_PACKET_LEN] {};
	const auto count = mavlink_msg_to_send_buffer(output, &message);
	return Frame(output, output + count);
}

SignedParseResult parse(SignedMavlink &receiver, const Frame &frame, bool channel = true)
{
	return receiver.authenticate(frame.data(), frame.size(), channel, kTransition, kMap);
}

void rejectionThenValid(const Frame &bad, SignedParseReason reason, bool verified = false, bool channel = true)
{
	SignedMavlink receiver(configuration());
	const auto rejected = parse(receiver, bad, channel);
	require(rejected.reason == reason, "unexpected parser rejection reason");
	require(rejected.signature_verified == verified, "unexpected authentication evidence");
	require(rejected.kind == SignedPacketKind::None && !rejected.verdict.parser_authenticated,
		"rejection exposed dispatch verdict");
	const auto accepted = parse(receiver, requestFrame(kSeed + 1));
	require(accepted.reason == SignedParseReason::Ok, "rejection poisoned legitimate replay stream");
}

void parserCases()
{
	const uint64_t future = kSeed + 100000000;
	const auto valid = requestFrame(kSeed + 1);
	SignedMavlink receiver(configuration());
	require(receiver.configured(), "fixture configuration");
	const auto accepted = parse(receiver, valid);
	require(accepted.reason == SignedParseReason::Ok && accepted.signature_verified
		&& accepted.kind == SignedPacketKind::Request && accepted.verdict.parser_authenticated,
		"genuine signature did not authenticate");
	require(accepted.request.wire.acceleration_x_frd_m_s2 == 1.25F, "generated request decoding");
	require(parse(receiver, valid).reason == SignedParseReason::Replay, "identical replay accepted");
	require(parse(receiver, requestFrame(kSeed)).reason == SignedParseReason::Replay, "older timestamp accepted");
	require(parse(receiver, requestFrame(kSeed + 2)).reason == SignedParseReason::Ok, "valid next timestamp rejected");

	auto corrupt = requestFrame(future);
	corrupt.back() ^= 1;
	rejectionThenValid(corrupt, SignedParseReason::Signature);
	corrupt = requestFrame(future);
	corrupt[10] ^= 1;
	rejectionThenValid(corrupt, SignedParseReason::Crc);
	rejectionThenValid(requestFrame(future, {}, 42, 191, 7, true), SignedParseReason::Signature);
	rejectionThenValid(requestFrame(future, {}, 42, 191, 7, false, -1, false), SignedParseReason::Envelope);
	rejectionThenValid(requestFrame(future, {}, 43), SignedParseReason::HeaderBinding, true);
	rejectionThenValid(requestFrame(future, {}, 42, 192), SignedParseReason::HeaderBinding, true);
	rejectionThenValid(requestFrame(future, {}, 42, 191, 9), SignedParseReason::LinkBinding, true);
	rejectionThenValid(requestFrame(future), SignedParseReason::Channel, false, false);
	rejectionThenValid(requestFrame(future, [](auto & v) { ++v.source_id; }), SignedParseReason::SourceBinding, true);
	rejectionThenValid(requestFrame(future, [](auto & v) { ++v.session_id; }), SignedParseReason::SourceBinding, true);
	rejectionThenValid(requestFrame(future, [](auto & v) { ++v.target_system; }), SignedParseReason::TargetBinding, true);
	rejectionThenValid(requestFrame(future, [](auto & v) { ++v.target_component; }), SignedParseReason::TargetBinding, true);
	rejectionThenValid(requestFrame(future, [](auto & v) { ++v.controller_instance_id; }), SignedParseReason::ContextBinding, true);
	rejectionThenValid(requestFrame(future, [](auto & v) { --v.transition_epoch; }), SignedParseReason::ContextBinding, true);
	rejectionThenValid(requestFrame(future, [](auto & v) { --v.clock_mapping_epoch; }), SignedParseReason::ContextBinding, true);
	rejectionThenValid(requestFrame(future, [](auto & v) { v.flags = 1; }), SignedParseReason::PayloadShape, true);
	rejectionThenValid(requestFrame(future, [](auto & v) { v.reserved[3] = 1; }), SignedParseReason::PayloadShape, true);
	rejectionThenValid(requestFrame(future, {}, 42, 191, 7, false, 84), SignedParseReason::LogicalLength);
	rejectionThenValid(requestFrame(future, {}, 42, 191, 7, false, 91), SignedParseReason::LogicalLength);

	corrupt = valid;
	corrupt.insert(corrupt.end(), valid.begin(), valid.end());
	rejectionThenValid(corrupt, SignedParseReason::Envelope);
	corrupt = valid;
	corrupt.push_back(0);
	rejectionThenValid(corrupt, SignedParseReason::Envelope);
	corrupt = valid;
	corrupt.insert(corrupt.begin(), 0);
	rejectionThenValid(corrupt, SignedParseReason::Envelope);
	rejectionThenValid({}, SignedParseReason::Envelope);
	SignedMavlink split(configuration());
	const auto middle = valid.begin() + valid.size() / 2;
	require(parse(split, Frame(valid.begin(), middle)).reason == SignedParseReason::Envelope, "split prefix accepted");
	require(parse(split, Frame(middle, valid.end())).reason == SignedParseReason::Envelope, "split suffix accepted");
	require(parse(split, valid).reason == SignedParseReason::Ok, "split buffered across datagrams");

	corrupt = valid;
	corrupt[2] |= 2;
	rejectionThenValid(corrupt, SignedParseReason::Envelope);
	corrupt = valid;
	corrupt[3] = 1;
	rejectionThenValid(corrupt, SignedParseReason::Envelope);
	corrupt = valid;
	corrupt[7] = 0xff;
	rejectionThenValid(corrupt, SignedParseReason::MessageIdentity);

	SignedMavlink restarted(configuration());
	require(parse(restarted, requestFrame(kSeed + 1, {}, 42, 191, 7, true)).reason == SignedParseReason::Signature,
		"another run key accepted");
}

Frame sessionFrame(uint64_t timestamp, uint8_t operation, uint8_t length = 71,
		   const std::function<void(mavlink_research_acceleration_session_t &)> &edit = {})
{
	const auto config = configuration();
	mavlink_research_acceleration_session_t value{};
	value.controller_instance_id = config.controller_instance;
	value.transition_epoch = kTransition;
	value.clock_mapping_epoch = kMap;
	value.source_id = config.source;
	value.session_id = config.session;
	value.operation_sequence = 17;
	value.protocol_version = kWireProtocolVersionV1;
	value.target_system = 1;
	value.target_component = 1;
	value.operation = operation;

	if (operation == 3) {
		value.producer_acknowledgement_monotonic_us = 123;
		value.acknowledged_status_sequence = 7;
	}

	if (edit) { edit(value); }

	mavlink_signing_t signing{};
	std::memcpy(signing.secret_key, config.key, sizeof(config.key));
	signing.flags = MAVLINK_SIGNING_FLAG_SIGN_OUTGOING;
	signing.link_id = config.incoming_link_id;
	signing.timestamp = timestamp;
	mavlink_status_t status{};
	status.signing = &signing;
	mavlink_message_t message{};
	mavlink_msg_research_acceleration_session_encode_status(42, 191, &status, &message, &value);
	uint8_t encoded[MAVLINK_MAX_PACKET_LEN] {};
	mavlink_msg_to_send_buffer(encoded, &message);
	Frame frame(encoded, encoded + MAVLINK_NUM_HEADER_BYTES + message.len);
	// Preserve each permitted zero tail instead of letting finalization trim it.
	frame.resize(MAVLINK_NUM_HEADER_BYTES + length, 0);
	frame[1] = length;
	uint16_t checksum = crc_calculate(frame.data() + 1, static_cast<uint16_t>(frame.size() - 1));
	crc_accumulate(MAVLINK_MSG_ID_RESEARCH_ACCELERATION_SESSION_CRC, &checksum);
	const uint8_t crc[2] {static_cast<uint8_t>(checksum), static_cast<uint8_t>(checksum >> 8)};
	uint8_t signature[MAVLINK_SIGNATURE_BLOCK_LEN] {};
	signing.timestamp = timestamp;
	mavlink_sign_packet(&signing, signature, frame.data(), MAVLINK_NUM_HEADER_BYTES,
			    frame.data() + MAVLINK_NUM_HEADER_BYTES, length, crc);
	frame.insert(frame.end(), crc, crc + sizeof(crc));
	frame.insert(frame.end(), signature, signature + sizeof(signature));
	return frame;
}

void sessionCases()
{
	for (uint8_t operation = 1; operation <= 3; ++operation) {
		for (uint8_t length = 71; length <= 76; ++length) {
			SignedMavlink receiver(configuration());
			const auto frame = sessionFrame(kSeed + 1, operation, length);
			require(frame[1] == length, "session fixture zero tail length");
			const auto result = parse(receiver, frame);
			require(result.reason == SignedParseReason::Ok && result.signature_verified
				&& result.kind == SignedPacketKind::Session && result.verdict.parser_authenticated,
				"signed session/zero tail rejected");
			require(result.session.operation == operation && result.session.operation_sequence == 17
				&& result.session.controller_instance_id == configuration().controller_instance,
				"session decoded fields mismatch");
			require(result.session.producer_acknowledgement_monotonic_us == (operation == 3 ? 123U : 0U)
				&& result.session.acknowledged_status_sequence == (operation == 3 ? 7U : 0U),
				"session acknowledgement fields mismatch");
			require(parse(receiver, frame).reason == SignedParseReason::Replay, "session replay accepted");
		}
	}

	const auto reject = [](const Frame & frame, SignedParseReason expected, bool authenticated) {
		SignedMavlink receiver(configuration());
		const auto result = parse(receiver, frame);
		require(result.reason == expected && result.signature_verified == authenticated,
			"session rejection evidence mismatch");
		require(result.kind == SignedPacketKind::None && !result.verdict.parser_authenticated,
			"session rejection dispatched");
		require(parse(receiver, sessionFrame(kSeed + 1, 3)).reason == SignedParseReason::Ok,
			"session rejection poisoned signing floor");
	};
	constexpr uint64_t future = kSeed + 100000000;
	reject(sessionFrame(future, 0), SignedParseReason::PayloadShape, true);
	reject(sessionFrame(future, 4), SignedParseReason::PayloadShape, true);
	reject(sessionFrame(future, 255), SignedParseReason::PayloadShape, true);
	reject(sessionFrame(future, 1, 70), SignedParseReason::LogicalLength, false);
	reject(sessionFrame(future, 1, 77), SignedParseReason::LogicalLength, false);
	reject(sessionFrame(future, 1, 71, [](auto & v) { --v.controller_instance_id; }), SignedParseReason::ContextBinding, true);
	reject(sessionFrame(future, 2, 71, [](auto & v) { --v.transition_epoch; }), SignedParseReason::ContextBinding, true);
	reject(sessionFrame(future, 3, 71, [](auto & v) { --v.clock_mapping_epoch; }), SignedParseReason::ContextBinding, true);
	reject(sessionFrame(future, 1, 71, [](auto & v) { ++v.source_id; }), SignedParseReason::SourceBinding, true);
	reject(sessionFrame(future, 2, 71, [](auto & v) { ++v.session_id; }), SignedParseReason::SourceBinding, true);
	auto bad_signature = sessionFrame(future, 3);
	bad_signature.back() ^= 1;
	reject(bad_signature, SignedParseReason::Signature, false);
}

void stagedContextCases()
{
	const auto high = requestFrame(kSeed + 100000000);

	for (unsigned mismatch = 0; mismatch < 4; ++mismatch) {
		SignedMavlink receiver(configuration());
		require(parse(receiver, requestFrame(kSeed + 1)).reason == SignedParseReason::Ok, "staged baseline");
		const auto staged = receiver.authenticate(high.data(), high.size(), true, kTransition, kMap, true);
		require(staged.reason == SignedParseReason::Ok && staged.signature_verified, "staged authentication");

		if (mismatch == 0) {
			require(!receiver.commitPending(kTransition + 1, kMap), "expired context committed replay");

		} else if (mismatch == 1) {
			require(!receiver.commitPending(kTransition, kMap + 1), "changed map committed replay");

		} else if (mismatch == 2) {
			// Owner discards a candidate when the re-sampled map is invalid.
			receiver.discardPending();

		} else {
			require(!receiver.commitPending(kTransition, 0), "invalid map committed replay");
		}

		require(!receiver.commitPending(kTransition, kMap), "rejected pending replay survived");
		require(parse(receiver, requestFrame(kSeed + 2)).reason == SignedParseReason::Ok,
			"stale staged high timestamp poisoned next valid lower timestamp");
	}

	SignedMavlink committed(configuration());
	const auto next = sessionFrame(kSeed + 1, 3);
	require(committed.authenticate(next.data(), next.size(), true, kTransition, kMap, true).reason == SignedParseReason::Ok,
		"staged session authentication");
	require(committed.commitPending(kTransition, kMap), "current staged context did not commit");
	require(!committed.commitPending(kTransition, kMap), "pending commit reused");
	require(parse(committed, next).reason == SignedParseReason::Replay, "committed staged session replay accepted");

	SignedMavlink superseded(configuration());
	require(superseded.authenticate(high.data(), high.size(), true, kTransition, kMap, true).reason == SignedParseReason::Ok,
		"pending supersession setup");
	require(parse(superseded, {}).reason == SignedParseReason::Envelope, "superseding malformed frame");
	require(!superseded.commitPending(kTransition, kMap), "new authentication retained old pending state");
	require(parse(superseded, requestFrame(kSeed + 1)).reason == SignedParseReason::Ok,
		"superseded staged timestamp poisoned receiver");
}

void statusCase()
{
	const auto config = configuration();
	SignedMavlink receiver(config);
	WireStatusV1 value{};
	value.controller_instance_id = config.controller_instance;
	value.transition_epoch = kTransition;
	value.clock_mapping_epoch = kMap;
	value.source_id = config.source;
	value.session_id = config.session;
	value.validator_time_us = 100;
	value.status_sequence = 1;
	value.protocol_version = kWireProtocolVersionV1;
	value.decision_code = 1;
	value.status_class = 1;
	value.arm_state = 2;
	value.research_mode_state = 2;
	value.session_state = 2;
	value.transport_state = 1;
	value.clock_mapping_state = 1;
	value.status_flags = 59;
	value.last_accepted_sequence = 1;
	value.correlation_sequence = 1;
	value.target_system = config.source_system;
	value.target_component = config.source_component;
	uint8_t frame[MAVLINK_MAX_PACKET_LEN] {};
	size_t size = 999;
	require(!receiver.signStatus(value, kSeed, frame, 1, size) && size == 999 && frame[0] == 0,
		"failed status signing modified output");
	require(receiver.signStatus(value, kSeed, frame, sizeof(frame), size) && size == 131, "status signing failed");
	mavlink_signing_t signing{};
	std::memcpy(signing.secret_key, config.key, sizeof(config.key));
	signing.timestamp = kSeed;
	mavlink_signing_streams_t streams{};
	mavlink_status_t parser{};
	parser.signing = &signing;
	parser.signing_streams = &streams;
	mavlink_status_t completed{};
	mavlink_message_t receiving{};
	mavlink_message_t message{};
	uint8_t result = MAVLINK_FRAMING_INCOMPLETE;

	for (size_t i = 0; i < size; ++i) {
		result = mavlink_frame_char_buffer(&receiving, &parser, frame[i], &message, &completed);
	}

	require(result == MAVLINK_FRAMING_OK && signing.last_status == MAVLINK_SIGNING_STATUS_OK,
		"generated status signature verification");
	require(message.sysid == 1 && message.compid == 1 && message.signature[0] == 8,
		"status direction binding");
	mavlink_research_acceleration_status_t decoded{};
	mavlink_msg_research_acceleration_status_decode(&message, &decoded);
	require(decoded.status_sequence == value.status_sequence && decoded.target_system == 42
		&& decoded.target_component == 191 && decoded.status_flags == value.status_flags,
		"generated status decoded fields");
	require(receiver.signStatus(value, kSeed, frame, sizeof(frame), size), "status timestamp advancement");

	for (size_t i = 0; i < size; ++i) {
		result = mavlink_frame_char_buffer(&receiving, &parser, frame[i], &message, &completed);
	}

	require(result == MAVLINK_FRAMING_OK, "status signing repeated a timestamp");
	require(!receiver.signStatus(value, uint64_t{1} << 48, frame, sizeof(frame), size), "timestamp overflow accepted");
}
} // namespace

int main()
{
	try {
		parserCases();
		sessionCases();
		stagedContextCases();
		statusCase();
		std::cout << "SignedMavlink PASS " << checks << " checks\n";
		return 0;

	} catch (const std::exception &error) {
		std::cerr << "SignedMavlink FAIL: " << error.what() << '\n';
		return 1;
	}
}
