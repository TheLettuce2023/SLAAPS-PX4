#include "WrenchAllocator.hpp"

#include <cmath>
#include <float.h>

namespace slaaps
{

// Exactly the physical weighting and actuator-unit normalization consumed by
// both current/future solves and the accepted acceleration configuration gate.
static ConfigurationResult configureScaling(const VehicleConfiguration &configuration,
		WrenchAllocationConfiguration &allocation)
{
	allocation.regularization = configuration.regularization;
	const char *parameters[kWrenchAxes] {"SLA_WR_FT_X", "SLA_WR_FT_Y", "SLA_WR_FT_Z",
					     "SLA_WR_MT_X", "SLA_WR_MT_Y", "SLA_WR_MT_Z"
					    };

	for (unsigned axis = 0; axis < kAxes; ++axis) {
		allocation.residual_tolerance[axis] = configuration.wrench.force_residual_tolerance[axis];
		allocation.residual_tolerance[axis + kAxes] = configuration.wrench.moment_residual_tolerance[axis];
	}

	for (unsigned row = 0; row < kWrenchAxes; ++row) {
		const double tolerance = allocation.residual_tolerance[row];

		if (!std::isfinite(tolerance) || tolerance <= 0.0) { return {ConfigurationFault::InvalidAccelerationControl, parameters[row]}; }

		const double weight = 1.0 / (tolerance * tolerance);

		if (!std::isfinite(weight) || weight > static_cast<double>(FLT_MAX) || weight <= 0.0) { return {ConfigurationFault::InvalidAccelerationControl, parameters[row]}; }

		allocation.axis_weight[row] = static_cast<float>(weight);
	}

	for (unsigned i = 0; i < kCanards; ++i) { allocation.regularization_scale[i] = 1.F; }

	const float span = configuration.edf_thrust[kThrustPoints - 1] - configuration.edf_thrust[0];

	if (!std::isfinite(span) || span <= 0.F) { return {ConfigurationFault::InvalidAccelerationControl, "SLA_EDF_T4"}; }

	allocation.regularization_scale[kCanards] = 1.F / span;

	if (!std::isfinite(allocation.regularization_scale[kCanards]) || allocation.regularization_scale[kCanards] <= 0.F) {
		return {ConfigurationFault::InvalidAccelerationControl, "SLA_EDF_T4"};
	}

	return {};
}

ConfigurationResult validateVehicleWrenchAllocationScaling(const VehicleConfiguration &configuration,
		const WrenchModel &model)
{
	WrenchAllocationConfiguration allocation{};
	allocation.model = model;
	const auto result = configureScaling(configuration, allocation);

	if (!result.valid()) { return result; }

	return validWrenchAllocationScaling(allocation) ? ConfigurationResult{}
	       : ConfigurationResult{ConfigurationFault::InvalidAccelerationControl, "SLA_ALLOC_REG"};
}

static WrenchAllocationResult allocateVehicleWrenchImpl(const VehicleConfiguration &configuration,
		const WrenchModel &model, const float (&requested)[6], const float (&previous)[5],
		float previous_edf_command, float dt_s, WrenchAllocationReport &report, bool current_only)
{
	if (configuration.wrench.enabled != 1 || !validateWrenchConfiguration(configuration).valid()) {
		return WrenchAllocationResult::InvalidConfiguration;
	}

	if (!std::isfinite(dt_s) || dt_s <= 0.F) { return WrenchAllocationResult::InvalidInput; }

	WrenchAllocationConfiguration allocation{};
	allocation.model = model;

	if (!configureScaling(configuration, allocation).valid()) { return WrenchAllocationResult::InvalidConfiguration; }

	for (unsigned canard = 0; canard < kCanards; ++canard) {
		const auto &servo = configuration.canard[canard];
		allocation.reference[canard] = servo.trim;
		allocation.minimum[canard] = servo.minimum;
		allocation.maximum[canard] = servo.maximum;
		allocation.positive_slew_per_s[canard] = servo.slew_positive;
		allocation.negative_slew_per_s[canard] = servo.slew_negative;
	}

	float previous_thrust{};

	if (!commandToThrust(configuration, previous_edf_command, previous_thrust)
	    || !(previous_thrust <= previous[kCanards] && previous_thrust >= previous[kCanards])) {
		return WrenchAllocationResult::InvalidInput;
	}

	// EDF slew is defined in owner-command units, not N/s. Convert the exact
	// permitted command interval through the same piecewise calibrated curve;
	// a single thrust-rate approximation would be wrong across curve knots.
	const double command_minimum = configuration.schedule.enabled ? static_cast<double>(configuration.schedule.edf_command[0]) : 0.0;
	const double command_maximum = configuration.schedule.enabled ? static_cast<double>(configuration.schedule.edf_command[1]) : 1.0;
	const double rise = configuration.schedule.enabled ? ::fmin(static_cast<double>(configuration.edf_rise),
			    static_cast<double>(configuration.schedule.edf_transient_rate)) : static_cast<double>(configuration.edf_rise);
	const double fall = configuration.schedule.enabled ? ::fmin(static_cast<double>(configuration.edf_fall),
			    static_cast<double>(configuration.schedule.edf_transient_rate)) : static_cast<double>(configuration.edf_fall);

	if (static_cast<double>(previous_edf_command) < command_minimum || static_cast<double>(previous_edf_command) > command_maximum) {
		return WrenchAllocationResult::InvalidInput;
	}

	// A candidate must remain inside the calibrated envelope now; rejecting it
	// on the next schedule observation would be too late for this command.
	const double lower_command = current_only ? static_cast<double>(previous_edf_command) : ::fmax(command_minimum,
				     static_cast<double>(previous_edf_command)
				     - fall * static_cast<double>(dt_s));
	const double upper_command = current_only ? static_cast<double>(previous_edf_command) : ::fmin(command_maximum,
				     static_cast<double>(previous_edf_command)
				     + rise * static_cast<double>(dt_s));
	float lower{}, upper{};

	if (!inwardWrenchBound(lower_command, true, lower) || !inwardWrenchBound(upper_command, false, upper)) {
		return WrenchAllocationResult::ArithmeticFailure;
	}

	if (!commandToThrust(configuration, lower, allocation.minimum[kCanards])
	    || !commandToThrust(configuration, upper, allocation.maximum[kCanards])) {
		return WrenchAllocationResult::InvalidConfiguration;
	}

	const float thrust_span = configuration.edf_thrust[kThrustPoints - 1] - configuration.edf_thrust[0];
	// Penalize EDF motion from the accepted command. A scheduled affine model
	// has no single fixed-condition reference thrust to reuse here.
	allocation.reference[kCanards] = previous[kCanards];
	// The hard interval above already imposes exact EDF slew. This finite bound
	// permits traversing the whole thrust range and cannot relax that interval.
	const float full_range_rate = static_cast<float>(static_cast<double>(thrust_span) / static_cast<double>(dt_s));
	allocation.positive_slew_per_s[kCanards] = full_range_rate;
	allocation.negative_slew_per_s[kCanards] = full_range_rate;
	WrenchAllocationReport candidate{};
	const auto result = allocateWrench(allocation, requested, previous, dt_s, candidate);

	if (result != WrenchAllocationResult::Success) { return result; }

	float allocated_command{};

	if (current_only) {
		// The supplied owner command is authoritative at float-curve plateaus.
		allocated_command = previous_edf_command;

	} else if (!thrustToCommand(configuration, candidate.command[kCanards], allocated_command)
		   || static_cast<double>(allocated_command) < lower_command
		   || static_cast<double>(allocated_command) > upper_command) {
		// A floating-point round trip must not enlarge the owner-command bounds.
		return WrenchAllocationResult::ArithmeticFailure;
	}

	if (!commandToThrust(configuration, allocated_command, candidate.realized_edf_thrust_n)) {
		return WrenchAllocationResult::ArithmeticFailure;
	}

	// Preserve the Newton request that the owner will invert, but evaluate the
	// wrench at its representable command's forward-curve thrust. Inversion and
	// forward interpolation need not be exact inverses in binary32 arithmetic.
	candidate.objective = 0.0;

	for (unsigned input = 0; input < kWrenchInputs; ++input) {
		const double applied = input == kCanards ? candidate.realized_edf_thrust_n : candidate.command[input];
		const double displacement = static_cast<double>(allocation.regularization_scale[input])
					    * (applied - static_cast<double>(allocation.reference[input]));
		candidate.objective += static_cast<double>(allocation.regularization) * displacement * displacement;
	}

	for (unsigned row = 0; row < kWrenchAxes; ++row) {
		double achieved = model.offset[row];

		for (unsigned input = 0; input < kWrenchInputs; ++input) {
			const double applied = input == kCanards ? candidate.realized_edf_thrust_n : candidate.command[input];
			achieved += static_cast<double>(model.effectiveness[row][input]) * applied;
		}

		const double residual = static_cast<double>(requested[row]) - achieved;

		if (!std::isfinite(achieved) || !std::isfinite(residual) || std::fabs(achieved) > static_cast<double>(FLT_MAX)
		    || std::fabs(residual) > static_cast<double>(FLT_MAX)) { return WrenchAllocationResult::ArithmeticFailure; }

		if (std::fabs(residual) > static_cast<double>(allocation.residual_tolerance[row])) { return WrenchAllocationResult::ResidualExceeded; }

		candidate.achieved[row] = static_cast<float>(achieved);
		candidate.residual[row] = static_cast<float>(residual);
		candidate.objective += static_cast<double>(allocation.axis_weight[row]) * residual * residual;
	}

	if (!std::isfinite(candidate.objective)) { return WrenchAllocationResult::ArithmeticFailure; }

	report = candidate;
	return WrenchAllocationResult::Success;
}

WrenchAllocationResult allocateVehicleWrench(const VehicleConfiguration &configuration,
		const WrenchModel &model, const float (&requested)[6], const float (&previous)[5],
		float previous_edf_command, float dt_s, WrenchAllocationReport &report)
{
	return allocateVehicleWrenchImpl(configuration, model, requested, previous, previous_edf_command, dt_s, report, false);
}

WrenchAllocationResult allocateVehicleWrenchAtCurrentThrust(const VehicleConfiguration &configuration,
		const WrenchModel &model, const float (&requested)[6], const float (&previous)[5],
		float actual_edf_command, float dt_s, WrenchAllocationReport &report)
{
	return allocateVehicleWrenchImpl(configuration, model, requested, previous, actual_edf_command, dt_s, report, true);
}

} // namespace slaaps
