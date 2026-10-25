#pragma once

#include <cstdint>

#include <matrix/matrix/Quaternion.hpp>
#include <matrix/matrix/Vector3.hpp>

namespace research_acceleration
{

using Vec3f = matrix::Vector3f;
using QuaternionNedFromFrd = matrix::Quatf;

enum class MathStatus : uint8_t {
	Ok,
	NonFinite,
	QuaternionNormInvalid,
	GravityInvalid,
	Overflow
};

// Checked normalization and canonical sign; q and -q produce the same attitude.
// This does not infer a reference attitude. Failure preserves output.
MathStatus normaliseAttitudeQuaternion(const QuaternionNedFromFrd &input, float norm_tolerance,
				       QuaternionNedFromFrd &output);

// q_nb rotates a vector expressed in FRD into NED. The output is unchanged
// unless Ok is returned. A finite quaternion is normalised only when its norm
// is within the caller-provided finite tolerance (0, 1) of one.
MathStatus rotateFrdToNed(const QuaternionNedFromFrd &q_nb, float norm_tolerance,
			  const Vec3f &v_frd, Vec3f &v_ned);
MathStatus rotateNedToFrd(const QuaternionNedFromFrd &q_nb, float norm_tolerance,
			  const Vec3f &v_ned, Vec3f &v_frd);
MathStatus heldInertialAccelerationFrdToNed(const QuaternionNedFromFrd &current_q_nb,
		float norm_tolerance, const Vec3f &a_req_frd_m_s2, Vec3f &a_req_ned_m_s2);
MathStatus specificForceFrdFromInertialAccelerationFrd(const QuaternionNedFromFrd &current_q_nb,
		float norm_tolerance, const Vec3f &gravity_ned_m_s2,
		const Vec3f &a_req_frd_m_s2, Vec3f &specific_force_frd_m_s2);

// angular_rate_frd_rad_s is the FRD rate relative to NED at the same instant.
// velocity_frd_m_s is inertial (ground-relative) velocity resolved in FRD.
// Air-relative velocity is not interchangeable: using it needs a wind material
// derivative and is deliberately outside this pure coordinate-math helper.
MathStatus inertialAccelerationFrdFromBodyVelocityDerivative(
	const Vec3f &body_velocity_derivative_m_s2, const Vec3f &angular_rate_frd_rad_s,
	const Vec3f &velocity_frd_m_s, Vec3f &inertial_acceleration_frd_m_s2);

} // namespace research_acceleration
