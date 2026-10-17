#pragma once

#if defined(MAVLINK_NO_SIGNATURE_CHECK)
#error "C1E requires generated MAVLink signature verification"
#endif

#include <diagnostic/mavlink.h>

#include <cstddef>
#include <cstdint>

namespace research_sitl
{

struct ByteSpan {
	const uint8_t *data{nullptr};
	size_t size{0};
};

struct DiagnosticParserConfig {
	uint8_t key[32] {};
	uint64_t native_run_id{0};
	uint64_t owner_lifetime_id{0};
	uint64_t controller_instance_id{0};
	uint64_t source_id{0};
	uint64_t transport_session_id{0};
	uint64_t signing_seed{0};
	uint8_t channel{0};
	uint8_t source_system{42};
	uint8_t source_component{191};
	uint8_t target_system{1};
	uint8_t target_component{1};
	uint8_t incoming_link_id{7};
	uint8_t outgoing_link_id{8};
};

enum class DiagnosticStageCode : uint8_t {
	Published,
	Busy,
	Closed,
	NotConfigured,
	MalformedEnvelope,
	WrongMessage,
	WrongChannel,
	BadCrc,
	BadSignature,
	Replay,
	WrongHeader,
	WrongLink,
	InvalidPayload,
	WrongIdentity,
	WrongTarget,
	ResourceFault
};

struct DiagnosticFrameMetadata {
	uint64_t signing_timestamp{0};
	uint16_t checksum{0};
	uint8_t signature[MAVLINK_SIGNATURE_BLOCK_LEN] {};
	uint8_t sequence{0};
	uint8_t system{0};
	uint8_t component{0};
	uint8_t channel{0};
	uint8_t link_id{0};
	uint8_t payload_length{0};
};

struct DiagnosticStageResult {
	DiagnosticStageCode code{DiagnosticStageCode::NotConfigured};
	uint64_t token{0};
	mavlink_research_diagnostic_request_t request{};
	DiagnosticFrameMetadata frame{};
};

// Private worker-owned parser. It has no socket, source, adapter or authority API.
// The worker must keep a published token until the owner disposition is known.
class DiagnosticSignedMavlink final
{
public:
	explicit DiagnosticSignedMavlink(const DiagnosticParserConfig &config) noexcept;
	~DiagnosticSignedMavlink();
	DiagnosticSignedMavlink(const DiagnosticSignedMavlink &) = delete;
	DiagnosticSignedMavlink &operator=(const DiagnosticSignedMavlink &) = delete;
	DiagnosticSignedMavlink(DiagnosticSignedMavlink &&) = delete;
	DiagnosticSignedMavlink &operator=(DiagnosticSignedMavlink &&) = delete;

	bool configured() const noexcept { return configured_; }
	bool hasPending() const noexcept { return has_pending_; }
	DiagnosticStageResult stageDatagram(ByteSpan datagram, uint8_t channel) noexcept;
	// Commit only after one actual completed adapter request turn, including rejection.
	bool commitPending(uint64_t token) noexcept;
	// Stale, foreign, closed and aborted candidates do not consume replay.
	bool discardPending(uint64_t token) noexcept;
	// End the worker lifetime and erase every worker-owned key copy.
	void retire() noexcept;
	// The caller supplies the local 10-us signing-clock observation, never an ingress value.
	bool signStatus(const mavlink_research_diagnostic_status_t &status, uint64_t signing_clock,
			uint8_t *output, size_t capacity, size_t &size) noexcept;

private:
	void erasePending() noexcept;
	bool validStatus(const mavlink_research_diagnostic_status_t &status) const noexcept;

	DiagnosticParserConfig config_{};
	bool configured_{false};
	bool closed_{false};
	mavlink_signing_t incoming_{};
	mavlink_signing_streams_t streams_{};
	mavlink_signing_t pending_signing_{};
	mavlink_signing_streams_t pending_streams_{};
	uint64_t pending_token_{0};
	uint64_t next_token_{0};
	bool has_pending_{false};
	mavlink_signing_t outgoing_{};
	uint8_t outgoing_sequence_{0};
};

} // namespace research_sitl
