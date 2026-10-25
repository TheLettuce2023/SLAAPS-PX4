#include "TrustedTimeMap.hpp"

#include <cmath>
#include <cstring>
#include <stdint.h>

namespace research_acceleration
{
namespace
{

bool checkedAdd(TimeUs left, TimeUs right, TimeUs &result)
{
	if (left > UINT64_MAX - right) {
		return false;
	}

	result = left + right;
	return true;
}

bool checkedSubtract(TimeUs left, TimeUs right, TimeUs &result)
{
	if (left < right) {
		return false;
	}

	result = left - right;
	return true;
}

bool addSignedOffset(TimeUs producer, int64_t offset, TimeUs &validator)
{
	if (offset >= 0) {
		return checkedAdd(producer, static_cast<TimeUs>(offset), validator);
	}

	const TimeUs magnitude = static_cast<TimeUs>(-(offset + 1)) + 1U;
	return checkedSubtract(producer, magnitude, validator);
}

bool finite(const WireRequestV1 &wire)
{
	return std::isfinite(wire.acceleration_x_frd_m_s2)
	       && std::isfinite(wire.acceleration_y_frd_m_s2)
	       && std::isfinite(wire.acceleration_z_frd_m_s2);
}

bool validMap(const TrustedClockMap &map)
{
	return map.valid && map.source != 0 && map.session != 0 && map.controller_instance_id != 0
	       && map.map_epoch != 0 && map.uncertainty_us != 0;
}

} // namespace

static CanonicalizeCode canonicalizeRequest(const WireRequest &wire, const TrustedClockMap &map,
		uint32_t version, float quaternion_norm_tolerance, CanonicalRequest &canonical)
{
	if (!validMap(map)) {
		return CanonicalizeCode::MapInvalid;
	}

	if (map.valid_from_producer_us > map.valid_until_producer_us) {
		return CanonicalizeCode::MapIntervalInvalid;
	}

	if (wire.transition_epoch == 0) {
		return CanonicalizeCode::WireIdentityInvalid;
	}

	if (wire.source_id != map.source || wire.session_id != map.session
	    || wire.controller_instance_id != map.controller_instance_id || wire.clock_mapping_epoch != map.map_epoch) {
		return CanonicalizeCode::MapBindingMismatch;
	}

	if (wire.protocol_version != version) {
		return CanonicalizeCode::WireVersionInvalid;
	}

	if (wire.frame != kWireFrameFrdV1 || wire.quantity != kWireQuantityInertialAccelerationV1
	    || wire.reference_point != kWireReferencePointCgV1) {
		return CanonicalizeCode::WireEnumInvalid;
	}

	if (wire.producer_expiry_monotonic_us <= wire.producer_monotonic_us) {
		return CanonicalizeCode::WireTimeInvalid;
	}

	if (wire.producer_monotonic_us < map.valid_from_producer_us
	    || wire.producer_expiry_monotonic_us > map.valid_until_producer_us) {
		return CanonicalizeCode::MapIntervalInvalid;
	}

	if (!finite(wire)) {
		return CanonicalizeCode::WireNonFinite;
	}

	QuaternionNedFromFrd attitude(0.F, 0.F, 0.F, 0.F);

	if (version == kWireProtocolVersionV2) {
		const MathStatus status = normaliseAttitudeQuaternion(QuaternionNedFromFrd(wire.desired_attitude_ned_from_body),
					  quaternion_norm_tolerance, attitude);

		if (status != MathStatus::Ok) {
			return status == MathStatus::NonFinite ? CanonicalizeCode::WireNonFinite : CanonicalizeCode::WireAttitudeInvalid;
		}

	} else {
		for (float value : wire.desired_attitude_ned_from_body) {
			if (!std::isfinite(value)) { return CanonicalizeCode::WireNonFinite; }

			uint32_t bits{};
			static_assert(sizeof(value) == sizeof(bits), "wire quaternion requires binary32");
			memcpy(&bits, &value, sizeof(bits));

			if (bits != 0) { return CanonicalizeCode::WireAttitudeInvalid; }
		}
	}

	TimeUs mapped_creation{};
	TimeUs mapped_expiry{};
	TimeUs raw_duration{};
	TimeUs mapped_duration{};

	if (!addSignedOffset(wire.producer_monotonic_us, map.validator_minus_producer_us, mapped_creation)
	    || !addSignedOffset(wire.producer_expiry_monotonic_us, map.validator_minus_producer_us, mapped_expiry)
	    || !checkedSubtract(wire.producer_expiry_monotonic_us, wire.producer_monotonic_us, raw_duration)
	    || !checkedSubtract(mapped_expiry, mapped_creation, mapped_duration) || raw_duration != mapped_duration) {
		return CanonicalizeCode::MapArithmeticInvalid;
	}

	CanonicalRequest result{};
	result.protocol_version = version;
	result.source = wire.source_id;
	result.session = wire.session_id;
	result.sequence = wire.sequence;
	result.producer_monotonic_us = wire.producer_monotonic_us;
	result.producer_time_in_validator_domain_us = mapped_creation;
	result.producer_expiry_monotonic_us = wire.producer_expiry_monotonic_us;
	result.expiry_time_in_validator_domain_us = mapped_expiry;
	result.clock_mapping_uncertainty_us = map.uncertainty_us;
	result.frame = Frame::Frd;
	result.quantity = Quantity::InertialAcceleration;
	result.reference_point = ReferencePoint::Cg;
	result.acceleration_frd_m_s2 = Vec3f(wire.acceleration_x_frd_m_s2,
					     wire.acceleration_y_frd_m_s2,
					     wire.acceleration_z_frd_m_s2);
	result.command_kind = version == kWireProtocolVersionV2 ? CommandKind::AccelerationAndAttitude : CommandKind::AccelerationOnly;
	result.desired_attitude_ned_from_body = attitude;

	canonical = result;
	return CanonicalizeCode::Ok;
}

CanonicalizeCode canonicalizeRequestV1(const WireRequestV1 &wire, const TrustedClockMap &map,
				       CanonicalRequest &canonical)
{
	return canonicalizeRequest(wire, map, kWireProtocolVersionV1, 0.F, canonical);
}

CanonicalizeCode canonicalizeRequestV2(const WireRequest &wire, const TrustedClockMap &map,
				       float quaternion_norm_tolerance, CanonicalRequest &canonical)
{
	return canonicalizeRequest(wire, map, kWireProtocolVersionV2, quaternion_norm_tolerance, canonical);
}

} // namespace research_acceleration
