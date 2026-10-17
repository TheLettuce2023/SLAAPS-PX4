// C1E parser contract fixture. Vectors come from independent Python bytes.
#include "DiagnosticSignedMavlink.hpp"
#include "SignedVectors.hpp"

#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace
{
using namespace research_sitl;
using Frame = std::vector<uint8_t>;
constexpr uint64_t kTimestamp = 10000100;
constexpr uint8_t kChannel = 3;
unsigned checks = 0;

void require(bool condition, const char *message)
{
	if (!condition) { throw std::runtime_error(message); }

	++checks;
}

uint8_t nibble(char digit)
{
	if (digit >= '0' && digit <= '9') { return static_cast<uint8_t>(digit - '0'); }

	if (digit >= 'a' && digit <= 'f') { return static_cast<uint8_t>(digit - 'a' + 10); }

	throw std::runtime_error("invalid generated vector digit");
}

Frame fromHex(const char *hex)
{
	const size_t length = std::strlen(hex);
	require(length % 2 == 0, "odd fixture hex length");
	Frame bytes;
	bytes.reserve(length / 2);

	for (size_t index = 0; index < length; index += 2) {
		bytes.push_back(static_cast<uint8_t>((nibble(hex[index]) << 4) | nibble(hex[index + 1])));
	}

	return bytes;
}

DiagnosticParserConfig configuration()
{
	DiagnosticParserConfig value{};

	for (unsigned index = 0; index < sizeof(value.key); ++index) {
		value.key[index] = static_cast<uint8_t>(index + 1);
	}

	value.native_run_id = 11;
	value.owner_lifetime_id = 12;
	value.controller_instance_id = 13;
	value.source_id = 14;
	value.transport_session_id = 15;
	value.signing_seed = kTimestamp - 1;
	value.channel = kChannel;
	return value;
}

DiagnosticStageResult stage(DiagnosticSignedMavlink &parser, const char *hex, uint8_t channel = kChannel)
{
	const Frame bytes = fromHex(hex);
	return parser.stageDatagram({bytes.data(), bytes.size()}, channel);
}

void expectReject(const char *hex, DiagnosticStageCode expected, const char *label,
		  uint8_t channel = kChannel)
{
	DiagnosticSignedMavlink parser(configuration());
	require(parser.configured(), "fixture parser not configured");
	const auto result = stage(parser, hex, channel);

	if (result.code != expected) {
		std::cerr << label << ": got stage code " << static_cast<unsigned>(result.code)
			  << ", expected " << static_cast<unsigned>(expected) << '\n';
		throw std::runtime_error("wrong rejection stage");
	}

	require(result.token == 0 && !parser.hasPending(), "reject exposed a candidate");
	const auto valid = stage(parser, independent_vectors::valid);
	require(valid.code == DiagnosticStageCode::Published,
		"rejected frame poisoned the next authentic request");
	require(parser.discardPending(valid.token), "could not discard probe candidate");
}

void rejectionCases()
{
	using namespace independent_vectors;
	expectReject(high_bad_signature, DiagnosticStageCode::BadSignature, "forged high timestamp");
	expectReject(bad_signature, DiagnosticStageCode::BadSignature, "bad signature");
	expectReject(wrong_key, DiagnosticStageCode::BadSignature, "wrong key");
	expectReject(bad_crc, DiagnosticStageCode::BadCrc, "bad crc");
	expectReject(unsigned_frame, DiagnosticStageCode::MalformedEnvelope, "unsigned");
	expectReject(mavlink1, DiagnosticStageCode::MalformedEnvelope, "MAVLink 1");
	expectReject(truncated, DiagnosticStageCode::MalformedEnvelope, "truncated");
	expectReject(trailing, DiagnosticStageCode::MalformedEnvelope, "trailing byte");
	expectReject(concatenated, DiagnosticStageCode::MalformedEnvelope, "concatenated");
	expectReject(split_prefix, DiagnosticStageCode::MalformedEnvelope, "split prefix");
	expectReject(split_suffix, DiagnosticStageCode::MalformedEnvelope, "split suffix");
	expectReject(unsupported_compat, DiagnosticStageCode::MalformedEnvelope, "compat flags");
	expectReject(unsupported_incompat, DiagnosticStageCode::MalformedEnvelope, "incompat flags");
	expectReject(wrong_message, DiagnosticStageCode::WrongMessage, "wrong message");
	expectReject(valid, DiagnosticStageCode::WrongChannel, "wrong channel", kChannel + 1);
	expectReject(wrong_system, DiagnosticStageCode::WrongHeader, "wrong system");
	expectReject(wrong_component, DiagnosticStageCode::WrongHeader, "wrong component");
	expectReject(wrong_link, DiagnosticStageCode::WrongLink, "wrong link");
	expectReject(wrong_run, DiagnosticStageCode::WrongIdentity, "wrong run");
	expectReject(wrong_owner, DiagnosticStageCode::WrongIdentity, "wrong owner");
	expectReject(wrong_controller, DiagnosticStageCode::WrongIdentity, "wrong controller");
	expectReject(wrong_source, DiagnosticStageCode::WrongIdentity, "wrong source");
	expectReject(wrong_transport, DiagnosticStageCode::WrongIdentity, "wrong transport");
	expectReject(wrong_target, DiagnosticStageCode::WrongTarget, "wrong target system");
	expectReject(wrong_target_component, DiagnosticStageCode::WrongTarget, "wrong target component");
	expectReject(wrong_map, DiagnosticStageCode::InvalidPayload, "nonzero map");
	expectReject(zero_transition, DiagnosticStageCode::InvalidPayload, "zero transition");
	expectReject(wrong_domain, DiagnosticStageCode::InvalidPayload, "wrong domain");
	expectReject(wrong_flags, DiagnosticStageCode::InvalidPayload, "flags");
	expectReject(wrong_reserved, DiagnosticStageCode::InvalidPayload, "reserved");
	expectReject(nonfinite_acceleration, DiagnosticStageCode::InvalidPayload, "nonfinite acceleration");
	DiagnosticSignedMavlink bad_configuration(DiagnosticParserConfig{});
	require(!bad_configuration.configured(), "zero configuration accepted");
	require(stage(bad_configuration, valid).code == DiagnosticStageCode::NotConfigured,
		"unconfigured parser staged ingress");
}

void replayCases()
{
	using namespace independent_vectors;
	DiagnosticSignedMavlink parser(configuration());
	const auto first = stage(parser, valid);
	require(first.code == DiagnosticStageCode::Published && first.token != 0, "valid frame not staged");
	require(first.request.native_run_id == 11 && first.request.clock_mapping_epoch == 0
		&& first.request.request_sequence == 16 && first.frame.signing_timestamp == kTimestamp
		&& first.frame.sequence == 17 && first.frame.channel == kChannel
		&& first.frame.link_id == 7 && first.frame.payload_length == 103,
		"candidate values or frame metadata changed");
	require(stage(parser, high_bad_signature).code == DiagnosticStageCode::Busy,
		"occupied parser authenticated another datagram");
	require(!parser.commitPending(first.token + 1) && parser.hasPending(), "wrong token committed");
	require(!parser.discardPending(first.token + 1) && parser.hasPending(), "wrong token discarded");
	require(parser.discardPending(first.token) && !parser.hasPending(), "discard failed");
	require(!parser.commitPending(first.token), "discarded token committed later");
	const auto retry = stage(parser, valid);
	require(retry.code == DiagnosticStageCode::Published && retry.token > first.token,
		"discard incorrectly consumed replay or reused token");
	require(parser.commitPending(retry.token) && !parser.hasPending(), "completed turn failed to commit");
	require(!parser.commitPending(retry.token), "committed token reused");
	require(stage(parser, valid).code == DiagnosticStageCode::Replay, "committed duplicate accepted");
	require(stage(parser, valid_old).code == DiagnosticStageCode::Replay, "older timestamp accepted");
	const auto next = stage(parser, valid_next);
	require(next.code == DiagnosticStageCode::Published && next.token > retry.token,
		"new timestamp rejected after replay tests");
	require(parser.commitPending(next.token), "next timestamp failed to commit");
	DiagnosticSignedMavlink full(configuration());
	const auto full_result = stage(full, valid_full);
	require(full_result.code == DiagnosticStageCode::Published && full_result.frame.payload_length == 108,
		"permitted full zero tail rejected");
	require(full.discardPending(full_result.token), "full payload discard failed");
}

void printHex(const uint8_t *data, size_t count)
{
	constexpr char digits[] = "0123456789abcdef";

	for (size_t index = 0; index < count; ++index) {
		std::cout << digits[data[index] >> 4] << digits[data[index] & 15];
	}
}

void statusCases()
{
	DiagnosticSignedMavlink parser(configuration());
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
	status.protocol_version = 0x10000;
	status.reason = 4;
	status.domain = 1;
	status.profile = 1;
	status.execution_origin = 2;
	status.outcome = 1;
	status.arm_state = 1;
	status.raw_nav_state = 3;
	status.target_system = 42;
	status.target_component = 191;
	uint8_t buffer[MAVLINK_MAX_PACKET_LEN] {};
	size_t length = 0;
	status.eligible_for_control = 1;
	require(!parser.signStatus(status, kTimestamp + 5, buffer, sizeof(buffer), length),
		"eligible status signed");
	status.eligible_for_control = 0;
	status.reason = 1;
	status.outcome = 2;
	require(!parser.signStatus(status, kTimestamp + 5, buffer, sizeof(buffer), length),
		"contradictory outcome/reason signed");
	status.reason = 4;
	status.outcome = 1;
	require(!parser.signStatus(status, kTimestamp + 5, buffer, 1, length), "short output accepted");
	require(length == 0, "failed signing mutated output length");
	require(parser.signStatus(status, kTimestamp + 5, buffer, sizeof(buffer), length), "valid status not signed");
	require(buffer[0] == 0xfd && buffer[2] == 1 && buffer[3] == 0
		&& buffer[5] == 1 && buffer[6] == 1 && buffer[length - 13] == 8,
		"outgoing endpoint or signature envelope changed");
	std::cout << "STATUS ";
	printHex(buffer, length);
	std::cout << '\n';
	size_t next_length = 0;
	require(parser.signStatus(status, kTimestamp + 5, buffer, sizeof(buffer), next_length),
		"second monotonic status not signed");
	std::cout << "STATUS ";
	printHex(buffer, next_length);
	std::cout << '\n';
}

} // namespace

int main()
{
	try {
		rejectionCases();
		replayCases();
		statusCases();
		std::cout << "PASS " << checks << '\n';
		return 0;

	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
