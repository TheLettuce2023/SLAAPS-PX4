#pragma once

#if defined(MAVLINK_NO_SIGNATURE_CHECK)
#error "C1H requires generated MAVLink signature verification"
#endif

#include <research/mavlink.h>
#include <lib/research_acceleration/WireStatus.hpp>

#include <cstddef>
#include <cstdint>

namespace research_sitl
{

struct SignedMavlinkConfig {
	uint8_t key[32] {};
	uint8_t source_system{42};
	uint8_t source_component{191};
	uint8_t target_system{1};
	uint8_t target_component{1};
	uint8_t incoming_link_id{7};
	uint8_t outgoing_link_id{8};
	uint64_t source{0};
	uint64_t session{0};
	uint64_t controller_instance{0};
	uint64_t signing_seed{0};
};

enum class SignedPacketKind : uint8_t { None, Request, Session };
enum class SignedParseReason : uint8_t {
	Ok, NotConfigured, Envelope, MessageIdentity, LogicalLength, Channel,
	Crc, Signature, Replay, HeaderBinding, LinkBinding, PayloadShape,
	SourceBinding, TargetBinding, ContextBinding
};

struct SignedParseResult {
	SignedParseReason reason{SignedParseReason::NotConfigured};
	SignedPacketKind kind{SignedPacketKind::None};
	bool signature_verified{false};
	research_acceleration::DecodedRequestV1 request{};
	research_acceleration::DecodedSessionV1 session{};
	research_acceleration::SessionIngressVerdict verdict{};
};

// Serialized, host-only raw-frame boundary. No socket or administrative API.
// Every rejection preserves committed replay state. Key material is never logged.
class SignedMavlink final
{
public:
	explicit SignedMavlink(const SignedMavlinkConfig &config) noexcept;
	~SignedMavlink();
	SignedMavlink(const SignedMavlink &) = delete;
	SignedMavlink &operator=(const SignedMavlink &) = delete;
	SignedMavlink(SignedMavlink &&) = delete;
	SignedMavlink &operator=(SignedMavlink &&) = delete;
	bool configured() const noexcept { return configured_; }
	SignedParseResult authenticate(const uint8_t *datagram, size_t length, bool channel_matches,
				       uint64_t expected_transition, uint64_t expected_map, bool stage_only = false) noexcept;
	// Live owners stage authentication, resample state/time, then commit against
	// the current context before dispatch. The caller also requires a valid map.
	// Any failed commit or subsequent authentication discards the pending state.
	bool commitPending(uint64_t expected_transition, uint64_t expected_map) noexcept;
	void discardPending() noexcept;
	// Timestamp is a caller-owned signing-clock observation in 10 us units.
	// Success advances the private outgoing floor; failure leaves output/size unchanged.
	bool signStatus(const research_acceleration::WireStatusV1 &status, uint64_t timestamp,
			uint8_t *output, size_t capacity, size_t &size) noexcept;

private:
	SignedMavlinkConfig config_;
	bool configured_{false};
	mavlink_signing_t incoming_{};
	mavlink_signing_streams_t streams_{};
	mavlink_signing_t pending_signing_{};
	mavlink_signing_streams_t pending_streams_{};
	uint64_t pending_transition_{0};
	uint64_t pending_map_{0};
	bool has_pending_{false};
	mavlink_signing_t outgoing_{};
	uint8_t outgoing_sequence_{0};
};

} // namespace research_sitl
