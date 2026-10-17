#pragma once

#include "FixtureConstants.hpp"
#include <lib/research_stabilized_manual/StabilizedManualControl.hpp>

namespace research_stabilized_fixture
{
inline research_stabilized_manual::Configuration configuration()
{
	using namespace research_stabilized_manual;
	Configuration c{};
	c.mavlink_instance = 0;
	c.source_system_id = 255;
	c.source_component_id = 190;
	c.maximum_manual_age_us = 100000;
	// Asynchronous Gazebo transport / EKF scheduling budget for this profile.
	// Separate from the 20 ms state / 12 ms loop host profile; measured in SITL.
	c.maximum_state_age_us = 100000;
	c.maximum_state_skew_us = 50000;
	c.minimum_cycle_us = kStepUs;
	c.maximum_cycle_us = 60000;
	c.quaternion_norm_tolerance = 0.01F;
	c.centered_enter = 0.04F;
	c.centered_exit = 0.06F;
	c.stick_sign = Vector3f(1.0F, -1.0F, 1.0F);
	c.maximum_rate_rad_s = Vector3f(1.0F, 1.0F, 1.0F);
	c.maximum_rate_setpoint_accel_rad_s2 = Vector3f(20.0F, 20.0F, 20.0F);
	c.maximum_measured_rate_rad_s = Vector3f(20.0F, 20.0F, 20.0F);
	c.maximum_measured_accel_rad_s2 = Vector3f(100.0F, 100.0F, 100.0F);
	c.attitude_gain = Vector3f(4.0F, 4.0F, 4.0F);
	c.rate_p = Vector3f(0.4F, 0.8F, 0.8F);
	c.rate_d = Vector3f(0.003F, 0.006F, 0.006F);
	// Derive I from this fixture's normalized rigid-body plant and servo lag.
	// Scalar characteristic: a3*s^3 + a2*s^2 + a1*s + I. Place its slow pole
	// at -0.4/s; the old ~-0.11/s pole did not track coupled full-stick commands.
	// This is synthetic tuning, not an installed-vehicle default or proof of
	// the full nonlinear/coupled response (the native campaigns verify that).
	constexpr double kSlowPolePerS = 0.4;

	for (unsigned axis = 0; axis < 3; ++axis) {
		const double inertia = kInertiaKgM2[axis] / kMomentScaleNm;
		const double damping = kDampingNmS[axis] / kMomentScaleNm;
		const double a3 = kServoTimeConstantS * inertia;
		const double a2 = inertia + kServoTimeConstantS * damping + static_cast<double>(c.rate_d(axis));
		const double a1 = damping + static_cast<double>(c.rate_p(axis));
		c.rate_i(axis) = static_cast<float>(kSlowPolePerS * (a1 - kSlowPolePerS * a2 + kSlowPolePerS * kSlowPolePerS * a3));
	}

	c.integrator_limit = Vector3f(0.2F, 0.2F, 0.2F);
	c.maximum_normalized_moment = Vector3f(1.0F, 1.0F, 1.0F);
	c.fixed_simulated_thrust_n = kFixedThrustN;

	for (unsigned row = 0; row < 3; ++row) {
		for (unsigned col = 0; col < 4; ++col) { c.allocation.effectiveness[row][col] = kEffectiveness[row][col]; }
	}

	for (unsigned col = 0; col < 4; ++col) {
		c.allocation.minimum[col] = -1.0F;
		c.allocation.maximum[col] = 1.0F;
		c.allocation.positive_slew_per_s[col] = 5.0F;
		c.allocation.negative_slew_per_s[col] = 5.0F;
	}

	c.allocation.axis_weight = Vector3f(1.0F, 1.0F, 1.0F);
	c.allocation.regularization = 1e-5F;
	c.allocation.minimum_row_norm = 0.01F;
	c.allocation.minimum_independence = 0.01F;
	c.allocation.saturation_tolerance = 0.001F;
	return c;
}
} // namespace research_stabilized_fixture
