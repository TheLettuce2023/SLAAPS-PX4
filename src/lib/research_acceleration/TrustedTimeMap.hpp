#pragma once

#include "RequestGuard.hpp"

#include <cstdint>

namespace research_acceleration
{

static constexpr uint32_t kWireProtocolVersionV1 = 0x00010000;
static constexpr uint8_t kWireFrameFrdV1 = 1;
static constexpr uint8_t kWireQuantityInertialAccelerationV1 = 1;
static constexpr uint8_t kWireReferencePointCgV1 = 1;

// Supplied by a trusted local map service/test fixture, never by payload fields.
// The offset and uncertainty are frozen over the inclusive producer interval.
struct TrustedClockMap {
	bool valid;
	SourceId source;
	SessionId session;
	uint64_t controller_instance_id;
	uint64_t map_epoch;
	int64_t validator_minus_producer_us;
	TimeUs uncertainty_us;
	TimeUs valid_from_producer_us;
	TimeUs valid_until_producer_us;
};

// Already decoded v1 subset. The caller owns C0a target/flags/reserved/length
// validation, plus authentication and live source/epoch checks. Not a wire struct.
struct WireRequestV1 {
	uint64_t source_id;
	uint64_t session_id;
	uint64_t sequence;
	TimeUs producer_monotonic_us;
	TimeUs producer_expiry_monotonic_us;
	uint64_t controller_instance_id;
	uint64_t transition_epoch;
	uint64_t clock_mapping_epoch;
	float acceleration_x_frd_m_s2;
	float acceleration_y_frd_m_s2;
	float acceleration_z_frd_m_s2;
	uint32_t protocol_version;
	uint8_t frame;
	uint8_t quantity;
	uint8_t reference_point;
};

enum class CanonicalizeCode : uint16_t {
	Ok,
	MapInvalid,
	MapBindingMismatch,
	MapIntervalInvalid,
	MapArithmeticInvalid,
	WireVersionInvalid,
	WireEnumInvalid,
	WireTimeInvalid,
	WireNonFinite,
	// A wire transition epoch has no TrustedClockMap counterpart and must be nonzero.
	WireIdentityInvalid
};

// Pure conversion, NOT admission: no freshness, replay, bounds, authentication,
// current transition-epoch equality or uncertainty-limit decision is made here.
// Ok preserves FRD components and raw timestamps, adding only trusted mapped
// timestamps/uncertainty. Any failure leaves canonical byte-for-byte unchanged.
// The caller must check the return value, then use the current-state adapter/S1
// guard before any control use. No object supplied here can establish trust.
CanonicalizeCode canonicalizeRequestV1(const WireRequestV1 &wire,
				       const TrustedClockMap &map,
				       CanonicalRequest &canonical);

} // namespace research_acceleration
