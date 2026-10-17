#include "FrameMath.hpp"

#include <cmath>
#include <limits>

namespace research_acceleration
{
namespace
{

bool finiteVector(const Vec3f &vector)
{
	return std::isfinite(vector(0)) && std::isfinite(vector(1)) && std::isfinite(vector(2));
}

MathStatus normalisedQuaternion(const QuaternionNedFromFrd &input, float tolerance,
				QuaternionNedFromFrd &output)
{
	if (!std::isfinite(tolerance) || tolerance <= 0.0F || tolerance >= 1.0F) {
		return MathStatus::QuaternionNormInvalid;
	}

	for (int index = 0; index < 4; ++index) {
		if (!std::isfinite(input(index))) {
			return MathStatus::NonFinite;
		}
	}

	const double norm_squared = static_cast<double>(input(0)) * static_cast<double>(input(0))
				    + static_cast<double>(input(1)) * static_cast<double>(input(1))
				    + static_cast<double>(input(2)) * static_cast<double>(input(2))
				    + static_cast<double>(input(3)) * static_cast<double>(input(3));

	if (!std::isfinite(norm_squared) || norm_squared <= 0.0) {
		return MathStatus::QuaternionNormInvalid;
	}

	const double norm = std::sqrt(norm_squared);

	if (!std::isfinite(norm) || std::fabs(norm - 1.0) > static_cast<double>(tolerance)) {
		return MathStatus::QuaternionNormInvalid;
	}

	const double inverse_norm = 1.0 / norm;
	QuaternionNedFromFrd candidate(static_cast<float>(static_cast<double>(input(0)) * inverse_norm),
				       static_cast<float>(static_cast<double>(input(1)) * inverse_norm), static_cast<float>(static_cast<double>(input(2)) * inverse_norm),
				       static_cast<float>(static_cast<double>(input(3)) * inverse_norm));

	for (int index = 0; index < 4; ++index) {
		if (!std::isfinite(candidate(index))) {
			return MathStatus::Overflow;
		}
	}

	output = candidate;
	return MathStatus::Ok;
}

MathStatus checkedVector(double x, double y, double z, Vec3f &output)
{
	if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)
	    || x > static_cast<double>(std::numeric_limits<float>::max()) || x < -static_cast<double>(std::numeric_limits<float>::max())
	    || y > static_cast<double>(std::numeric_limits<float>::max()) || y < -static_cast<double>(std::numeric_limits<float>::max())
	    || z > static_cast<double>(std::numeric_limits<float>::max()) || z < -static_cast<double>(std::numeric_limits<float>::max())) {
		return MathStatus::Overflow;
	}

	output = Vec3f(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
	return MathStatus::Ok;
}

} // namespace

MathStatus rotateFrdToNed(const QuaternionNedFromFrd &q_nb, float norm_tolerance,
			  const Vec3f &v_frd, Vec3f &v_ned)
{
	if (!finiteVector(v_frd)) {
		return MathStatus::NonFinite;
	}

	QuaternionNedFromFrd normalised;
	const MathStatus quaternion_status = normalisedQuaternion(q_nb, norm_tolerance, normalised);

	if (quaternion_status != MathStatus::Ok) {
		return quaternion_status;
	}

	const Vec3f rotated = normalised.rotateVector(v_frd);

	if (!finiteVector(rotated)) {
		return MathStatus::Overflow;
	}

	v_ned = rotated;
	return MathStatus::Ok;
}

MathStatus rotateNedToFrd(const QuaternionNedFromFrd &q_nb, float norm_tolerance,
			  const Vec3f &v_ned, Vec3f &v_frd)
{
	if (!finiteVector(v_ned)) {
		return MathStatus::NonFinite;
	}

	QuaternionNedFromFrd normalised;
	const MathStatus quaternion_status = normalisedQuaternion(q_nb, norm_tolerance, normalised);

	if (quaternion_status != MathStatus::Ok) {
		return quaternion_status;
	}

	const Vec3f rotated = normalised.rotateVectorInverse(v_ned);

	if (!finiteVector(rotated)) {
		return MathStatus::Overflow;
	}

	v_frd = rotated;
	return MathStatus::Ok;
}

MathStatus heldInertialAccelerationFrdToNed(const QuaternionNedFromFrd &current_q_nb,
		float norm_tolerance, const Vec3f &a_req_frd_m_s2, Vec3f &a_req_ned_m_s2)
{
	return rotateFrdToNed(current_q_nb, norm_tolerance, a_req_frd_m_s2, a_req_ned_m_s2);
}

MathStatus specificForceFrdFromInertialAccelerationFrd(const QuaternionNedFromFrd &current_q_nb,
		float norm_tolerance, const Vec3f &gravity_ned_m_s2,
		const Vec3f &a_req_frd_m_s2, Vec3f &specific_force_frd_m_s2)
{
	if (!finiteVector(a_req_frd_m_s2)) {
		return MathStatus::NonFinite;
	}

	// The contract's gravity model is NED down only: [0, 0, +g], g > 0.
	// After the finite check, ordered comparisons require exact zero (either sign).
	if (!finiteVector(gravity_ned_m_s2) || gravity_ned_m_s2(0) < 0.0F || gravity_ned_m_s2(0) > 0.0F
	    || gravity_ned_m_s2(1) < 0.0F || gravity_ned_m_s2(1) > 0.0F
	    || gravity_ned_m_s2(2) <= 0.0F) {
		return MathStatus::GravityInvalid;
	}

	Vec3f gravity_frd;
	const MathStatus status = rotateNedToFrd(current_q_nb, norm_tolerance, gravity_ned_m_s2, gravity_frd);

	if (status != MathStatus::Ok) {
		return status;
	}

	return checkedVector(static_cast<double>(a_req_frd_m_s2(0)) - static_cast<double>(gravity_frd(0)),
			     static_cast<double>(a_req_frd_m_s2(1)) - static_cast<double>(gravity_frd(1)),
			     static_cast<double>(a_req_frd_m_s2(2)) - static_cast<double>(gravity_frd(2)), specific_force_frd_m_s2);
}

MathStatus inertialAccelerationFrdFromBodyVelocityDerivative(
	const Vec3f &body_velocity_derivative_m_s2, const Vec3f &angular_rate_frd_rad_s,
	const Vec3f &velocity_frd_m_s, Vec3f &inertial_acceleration_frd_m_s2)
{
	if (!finiteVector(body_velocity_derivative_m_s2) || !finiteVector(angular_rate_frd_rad_s)
	    || !finiteVector(velocity_frd_m_s)) {
		return MathStatus::NonFinite;
	}

	const double cross_x = static_cast<double>(angular_rate_frd_rad_s(1)) * static_cast<double>(velocity_frd_m_s(2))
			       - static_cast<double>(angular_rate_frd_rad_s(2)) * static_cast<double>(velocity_frd_m_s(1));
	const double cross_y = static_cast<double>(angular_rate_frd_rad_s(2)) * static_cast<double>(velocity_frd_m_s(0))
			       - static_cast<double>(angular_rate_frd_rad_s(0)) * static_cast<double>(velocity_frd_m_s(2));
	const double cross_z = static_cast<double>(angular_rate_frd_rad_s(0)) * static_cast<double>(velocity_frd_m_s(1))
			       - static_cast<double>(angular_rate_frd_rad_s(1)) * static_cast<double>(velocity_frd_m_s(0));
	return checkedVector(static_cast<double>(body_velocity_derivative_m_s2(0)) + cross_x,
			     static_cast<double>(body_velocity_derivative_m_s2(1)) + cross_y,
			     static_cast<double>(body_velocity_derivative_m_s2(2)) + cross_z,
			     inertial_acceleration_frd_m_s2);
}

} // namespace research_acceleration
