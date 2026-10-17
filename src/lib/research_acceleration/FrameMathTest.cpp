#include "FrameMath.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <string>

using namespace research_acceleration;

namespace
{

int failures = 0;
int checks = 0;

void check(bool condition, const std::string &name)
{
	++checks;

	if (!condition) {
		std::cerr << "FAILED: " << name << '\n';
		++failures;
	}
}

bool near(float actual, float expected, float tolerance = 1e-4F)
{
	return std::fabs(actual - expected) <= tolerance;
}

bool nearVector(const Vec3f &actual, const Vec3f &expected)
{
	return near(actual(0), expected(0)) && near(actual(1), expected(1)) && near(actual(2), expected(2));
}

void boundaryFixtures()
{
	// Pure numeric fixtures: no operational tolerances, gravity or rates.
	const float nan = std::numeric_limits<float>::quiet_NaN();
	const float inf = std::numeric_limits<float>::infinity();
	const float largest = std::numeric_limits<float>::max();
	const float tolerance = 0.125F; // Exactly representable for boundary oracles.
	const QuaternionNedFromFrd identity(1.F, 0.F, 0.F, 0.F);
	const Vec3f sentinel(17.F, -19.F, 23.F);
	const Vec3f input(1.F, 2.F, 3.F);
	Vec3f result;

	for (float scale : {0.875F, 1.125F}) {
		check(rotateFrdToNed(QuaternionNedFromFrd(scale, 0.F, 0.F, 0.F), tolerance, input, result) == MathStatus::Ok
		      && nearVector(result, input), "fixture_exact_norm_boundary_" + std::to_string(scale));
	}

	for (float scale : {std::nextafter(0.875F, 0.F), std::nextafter(1.125F, inf)}) {
		result = sentinel;
		check(rotateFrdToNed(QuaternionNedFromFrd(scale, 0.F, 0.F, 0.F), tolerance, input, result)
		      == MathStatus::QuaternionNormInvalid && nearVector(result, sentinel),
		      "fixture_outside_norm_boundary_" + std::to_string(scale));
	}

	int index = 0;

	for (float invalid_tolerance : {0.F, -0.1F, 1.F, nan, inf, -inf}) {
		result = sentinel;
		check(rotateFrdToNed(identity, invalid_tolerance, input, result) == MathStatus::QuaternionNormInvalid
		      && nearVector(result, sentinel), "fixture_invalid_tolerance_" + std::to_string(index++));
	}

	const float half_root_two = std::sqrt(0.5F);

	for (int axis = 0; axis < 3; ++axis) {
		QuaternionNedFromFrd rotation(half_root_two, 0.F, 0.F, 0.F);
		rotation(axis + 1) = half_root_two;
		const QuaternionNedFromFrd negative(-rotation(0), -rotation(1), -rotation(2), -rotation(3));

		for (int basis = 0; basis < 3; ++basis) {
			Vec3f unit;
			unit(basis) = 1.F;
			Vec3f expected;

			if (axis == basis) {
				expected(basis) = 1.F;

			} else if (basis == (axis + 1) % 3) {
				expected((axis + 2) % 3) = 1.F;

			} else {
				expected((axis + 1) % 3) = -1.F;
			}

			const std::string name = "fixture_basis_" + std::to_string(axis) + "_" + std::to_string(basis);
			check(rotateFrdToNed(rotation, tolerance, unit, result) == MathStatus::Ok
			      && nearVector(result, expected), name + "_forward");
			check(rotateFrdToNed(negative, tolerance, unit, result) == MathStatus::Ok
			      && nearVector(result, expected), name + "_negative_quaternion");
			check(rotateNedToFrd(rotation, tolerance, expected, result) == MathStatus::Ok
			      && nearVector(result, unit), name + "_inverse");
		}
	}

	index = 0;

	for (float invalid : {nan, inf, -inf}) {
		for (int axis = 0; axis < 3; ++axis) {
			Vec3f bad = input;
			bad(axis) = invalid;
			const std::string name = "fixture_nonfinite_" + std::to_string(index) + "_" + std::to_string(axis);
			result = sentinel;
			check(rotateFrdToNed(identity, tolerance, bad, result) == MathStatus::NonFinite
			      && nearVector(result, sentinel), name + "_forward");
			check(rotateNedToFrd(identity, tolerance, bad, result) == MathStatus::NonFinite
			      && nearVector(result, sentinel), name + "_inverse");
			check(heldInertialAccelerationFrdToNed(identity, tolerance, bad, result) == MathStatus::NonFinite
			      && nearVector(result, sentinel), name + "_held");
			check(specificForceFrdFromInertialAccelerationFrd(identity, tolerance, Vec3f(0.F, 0.F, 1.F), bad, result)
			      == MathStatus::NonFinite && nearVector(result, sentinel), name + "_specific_force");
			check(inertialAccelerationFrdFromBodyVelocityDerivative(bad, input, input, result) == MathStatus::NonFinite
			      && nearVector(result, sentinel), name + "_derivative");
			check(inertialAccelerationFrdFromBodyVelocityDerivative(input, bad, input, result) == MathStatus::NonFinite
			      && nearVector(result, sentinel), name + "_angular_rate");
			check(inertialAccelerationFrdFromBodyVelocityDerivative(input, input, bad, result) == MathStatus::NonFinite
			      && nearVector(result, sentinel), name + "_ground_velocity");
		}

		for (int component = 0; component < 4; ++component) {
			auto bad_q = identity;
			bad_q(component) = invalid;
			result = sentinel;
			check(rotateFrdToNed(bad_q, tolerance, input, result) == MathStatus::NonFinite
			      && nearVector(result, sentinel), "fixture_bad_quaternion_" + std::to_string(index)
			      + "_" + std::to_string(component));
		}

		++index;
	}

	index = 0;

	for (const Vec3f &gravity : {
		     Vec3f(1.F, 0.F, 1.F), Vec3f(0.F, 1.F, 1.F), Vec3f(0.F, 0.F, -1.F),
		     Vec3f(0.F, 0.F, 0.F), Vec3f(nan, 0.F, 1.F), Vec3f(0.F, inf, 1.F), Vec3f(0.F, 0.F, nan)
	     }) {
		result = sentinel;
		check(specificForceFrdFromInertialAccelerationFrd(identity, tolerance, gravity, input, result)
		      == MathStatus::GravityInvalid && nearVector(result, sentinel),
		      "fixture_invalid_gravity_" + std::to_string(index++));
	}

	const QuaternionNedFromFrd nose_up(half_root_two, 0.F, half_root_two, 0.F);
	check(specificForceFrdFromInertialAccelerationFrd(identity, tolerance, Vec3f(-0.F, -0.F, 2.F), Vec3f(), result)
	      == MathStatus::Ok && nearVector(result, Vec3f(0.F, 0.F, -2.F)), "fixture_signed_zero_gravity_horizontal");
	result = sentinel;
	check(specificForceFrdFromInertialAccelerationFrd(identity, tolerance, Vec3f(0.F, 0.F, -0.F), Vec3f(), result)
	      == MathStatus::GravityInvalid && nearVector(result, sentinel), "fixture_signed_zero_gravity_down_rejected");
	check(specificForceFrdFromInertialAccelerationFrd(QuaternionNedFromFrd(0.F, 0.F, 0.F, 0.F),
			tolerance, Vec3f(0.F, 0.F, 2.F), Vec3f(), result) == MathStatus::QuaternionNormInvalid
	      && nearVector(result, sentinel), "fixture_specific_force_invalid_quaternion_propagation");
	check(specificForceFrdFromInertialAccelerationFrd(nose_up, tolerance, Vec3f(0.F, 0.F, 2.F), Vec3f(), result)
	      == MathStatus::Ok && nearVector(result, Vec3f(2.F, 0.F, 0.F)), "fixture_nose_up_constant_inertial_velocity");
	check(specificForceFrdFromInertialAccelerationFrd(nose_up, tolerance, Vec3f(0.F, 0.F, 2.F), Vec3f(-2.F, 0.F, 0.F), result)
	      == MathStatus::Ok && nearVector(result, Vec3f()), "fixture_nose_up_freefall");

	// 45-degree rotation: (max,max,0) has an unrepresentable rotated component.
	const QuaternionNedFromFrd eighth_turn(std::cos(0.3926990817F), 0.F, 0.F, std::sin(0.3926990817F));
	const Vec3f extreme(largest, largest, 0.F);
	result = sentinel;
	check(rotateFrdToNed(eighth_turn, tolerance, extreme, result) == MathStatus::Overflow
	      && nearVector(result, sentinel), "fixture_rotation_overflow_preserves_output");
	check(rotateNedToFrd(eighth_turn, tolerance, extreme, result) == MathStatus::Overflow
	      && nearVector(result, sentinel), "fixture_inverse_overflow_preserves_output");
	check(heldInertialAccelerationFrdToNed(eighth_turn, tolerance, extreme, result) == MathStatus::Overflow
	      && nearVector(result, sentinel), "fixture_held_overflow_preserves_output");
	check(specificForceFrdFromInertialAccelerationFrd(identity, tolerance, Vec3f(0.F, 0.F, largest),
			Vec3f(0.F, 0.F, -largest), result) == MathStatus::Overflow
	      && nearVector(result, sentinel), "fixture_specific_force_overflow_preserves_output");

	for (int axis = 0; axis < 3; ++axis) {
		Vec3f omega;
		Vec3f ground_velocity;
		Vec3f expected;
		omega(axis) = 2.F;
		ground_velocity((axis + 1) % 3) = 3.F;
		expected((axis + 2) % 3) = 6.F;
		check(inertialAccelerationFrdFromBodyVelocityDerivative(Vec3f(), omega, ground_velocity, result)
		      == MathStatus::Ok && nearVector(result, expected), "fixture_cross_basis_" + std::to_string(axis));
		check(inertialAccelerationFrdFromBodyVelocityDerivative(-expected, omega, ground_velocity, result)
		      == MathStatus::Ok && nearVector(result, Vec3f()), "fixture_rotating_ground_velocity_cancellation_" + std::to_string(axis));
		check(inertialAccelerationFrdFromBodyVelocityDerivative(Vec3f(), ground_velocity, omega, result)
		      == MathStatus::Ok && nearVector(result, -expected), "fixture_cross_reverse_sign_" + std::to_string(axis));
	}
}

} // namespace

int main()
{
	// All literals in this executable are non-operational math fixtures.
	const QuaternionNedFromFrd identity(1.0F, 0.0F, 0.0F, 0.0F);
	const float fixture_quaternion_tolerance_non_operational = 0.01F;
	const Vec3f fixture_vector(2.0F, -3.0F, 4.0F);
	Vec3f output(-7.0F, -7.0F, -7.0F);

	check(rotateFrdToNed(identity, fixture_quaternion_tolerance_non_operational, fixture_vector, output) == MathStatus::Ok,
	      "fixture_identity_frd_to_ned_status");
	check(nearVector(output, fixture_vector), "fixture_identity_frd_to_ned_value");

	const float half_root_two = std::sqrt(0.5F);
	const QuaternionNedFromFrd nose_up(half_root_two, 0.0F, half_root_two, 0.0F);
	check(rotateFrdToNed(nose_up, fixture_quaternion_tolerance_non_operational, Vec3f(1.0F, 0.0F, 0.0F), output)
	      == MathStatus::Ok, "fixture_nose_up_status");
	check(nearVector(output, Vec3f(0.0F, 0.0F, -1.0F)), "fixture_nose_up_frd_positive_x_is_ned_up");

	Vec3f round_trip;
	check(rotateNedToFrd(nose_up, fixture_quaternion_tolerance_non_operational, output, round_trip) == MathStatus::Ok,
	      "fixture_inverse_rotation_status");
	check(nearVector(round_trip, Vec3f(1.0F, 0.0F, 0.0F)), "fixture_inverse_rotation_value");

	const QuaternionNedFromFrd almost_unit(1.001F, 0.0F, 0.0F, 0.0F);
	check(rotateFrdToNed(almost_unit, fixture_quaternion_tolerance_non_operational, fixture_vector, output) == MathStatus::Ok,
	      "fixture_within_norm_tolerance_normalises");
	check(nearVector(output, fixture_vector), "fixture_normalised_value");

	output = Vec3f(-7.0F, -7.0F, -7.0F);
	check(rotateFrdToNed(QuaternionNedFromFrd(0.0F, 0.0F, 0.0F, 0.0F), fixture_quaternion_tolerance_non_operational,
			     fixture_vector, output) == MathStatus::QuaternionNormInvalid, "fixture_zero_quaternion_rejected");
	check(nearVector(output, Vec3f(-7.0F, -7.0F, -7.0F)), "fixture_quaternion_failure_preserves_output");

	check(rotateFrdToNed(QuaternionNedFromFrd(std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F, 0.0F),
			     fixture_quaternion_tolerance_non_operational, fixture_vector, output) == MathStatus::NonFinite,
	      "fixture_nonfinite_quaternion_rejected");
	check(rotateFrdToNed(identity, fixture_quaternion_tolerance_non_operational,
			     Vec3f(std::numeric_limits<float>::infinity(), 0.0F, 0.0F), output) == MathStatus::NonFinite,
	      "fixture_nonfinite_vector_rejected");

	const Vec3f fixture_held(4.0F, 0.0F, 0.0F);
	Vec3f held_identity;
	Vec3f held_nose_up;
	check(heldInertialAccelerationFrdToNed(identity, fixture_quaternion_tolerance_non_operational, fixture_held,
					       held_identity) == MathStatus::Ok, "fixture_held_identity_status");
	check(heldInertialAccelerationFrdToNed(nose_up, fixture_quaternion_tolerance_non_operational, fixture_held,
					       held_nose_up) == MathStatus::Ok, "fixture_held_rotating_status");
	check(nearVector(held_identity, Vec3f(4.0F, 0.0F, 0.0F)) && nearVector(held_nose_up, Vec3f(0.0F, 0.0F, -4.0F)),
	      "fixture_held_frd_changes_ned_with_attitude");

	const Vec3f fixture_gravity_ned(0.0F, 0.0F, 9.81F);
	Vec3f specific_force;
	check(specificForceFrdFromInertialAccelerationFrd(identity, fixture_quaternion_tolerance_non_operational,
			fixture_gravity_ned, Vec3f(0.0F, 0.0F, 0.0F), specific_force) == MathStatus::Ok,
	      "fixture_zero_inertial_acceleration_status");
	check(nearVector(specific_force, Vec3f(0.0F, 0.0F, -9.81F)), "fixture_zero_inertial_acceleration_specific_force");
	check(specificForceFrdFromInertialAccelerationFrd(identity, fixture_quaternion_tolerance_non_operational,
			fixture_gravity_ned, fixture_gravity_ned, specific_force) == MathStatus::Ok
	      && nearVector(specific_force, Vec3f(0.0F, 0.0F, 0.0F)), "fixture_freefall_specific_force");

	Vec3f acceleration;
	check(inertialAccelerationFrdFromBodyVelocityDerivative(Vec3f(0.0F, 0.0F, 0.0F), Vec3f(0.0F, 0.0F, 0.0F),
			Vec3f(7.0F, 1.0F, -2.0F), acceleration) == MathStatus::Ok && nearVector(acceleration, Vec3f(0.0F, 0.0F, 0.0F)),
	      "fixture_constant_ground_velocity_has_zero_acceleration");
	check(inertialAccelerationFrdFromBodyVelocityDerivative(Vec3f(1.0F, 0.0F, 0.0F), Vec3f(0.0F, 0.0F, 2.0F),
			Vec3f(3.0F, 0.0F, 0.0F), acceleration) == MathStatus::Ok && nearVector(acceleration, Vec3f(1.0F, 6.0F, 0.0F)),
	      "fixture_rotating_ground_velocity_derivative");

	acceleration = Vec3f(-5.0F, -5.0F, -5.0F);
	check(inertialAccelerationFrdFromBodyVelocityDerivative(
		      Vec3f(std::numeric_limits<float>::infinity(), 0.0F, 0.0F), Vec3f(0.0F, 0.0F, 0.0F),
		      Vec3f(0.0F, 0.0F, 0.0F), acceleration) == MathStatus::NonFinite,
	      "fixture_derivative_nonfinite_rejected");
	check(nearVector(acceleration, Vec3f(-5.0F, -5.0F, -5.0F)), "fixture_derivative_failure_preserves_output");

	check(rotateFrdToNed(QuaternionNedFromFrd(-1.0F, 0.0F, 0.0F, 0.0F), fixture_quaternion_tolerance_non_operational,
			     fixture_vector, output) == MathStatus::Ok && nearVector(output, fixture_vector), "fixture_negative_identity");
	specific_force = Vec3f(-6.0F, -6.0F, -6.0F);
	check(specificForceFrdFromInertialAccelerationFrd(identity, fixture_quaternion_tolerance_non_operational,
			Vec3f(1.0F, 0.0F, 9.81F), Vec3f(0.0F, 0.0F, 0.0F), specific_force) == MathStatus::GravityInvalid, "fixture_gravity_x_invalid");
	check(specificForceFrdFromInertialAccelerationFrd(identity, fixture_quaternion_tolerance_non_operational,
			Vec3f(0.0F, 0.0F, 0.0F), Vec3f(0.0F, 0.0F, 0.0F), specific_force) == MathStatus::GravityInvalid, "fixture_zero_gravity_invalid");
	check(nearVector(specific_force, Vec3f(-6.0F, -6.0F, -6.0F)), "fixture_gravity_failure_preserves_output");
	acceleration = Vec3f(-5.0F, -5.0F, -5.0F);
	check(inertialAccelerationFrdFromBodyVelocityDerivative(Vec3f(0.0F, 0.0F, 0.0F), Vec3f(1.0e30F, 0.0F, 0.0F),
			Vec3f(0.0F, 1.0e30F, 0.0F), acceleration) == MathStatus::Overflow, "fixture_derivative_overflow");
	check(nearVector(acceleration, Vec3f(-5.0F, -5.0F, -5.0F)), "fixture_derivative_overflow_preserves_output");
	boundaryFixtures();
	std::cout << checks << " checks; " << failures << " failures\n";
	return failures == 0 ? 0 : 1;
}
