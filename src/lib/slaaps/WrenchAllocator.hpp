#pragma once

#include "VehicleConfiguration.hpp"
#include <stdint.h>

namespace slaaps
{

enum class WrenchAllocationResult : uint8_t {
	Success,
	InvalidConfiguration,
	InvalidInput,
	ArithmeticFailure,
	Infeasible,
	ResidualExceeded
};

struct WrenchAllocationConfiguration {
	WrenchModel model{}; // W = offset + B*u; body FRD rows Fx,Fy,Fz [N], Mx,My,Mz [N m].
	float reference[5] {}; // C1..C4 normalized; EDF thrust [N]. Regularization target, not a second affine offset.
	float minimum[5] {};
	float maximum[5] {};
	float positive_slew_per_s[5] {};
	float negative_slew_per_s[5] {};
	float regularization_scale[5] {}; // Inverse actuator units: scale*(u-reference) is dimensionless.
	float residual_tolerance[6] {}; // Positive physical acceptance tolerances, one for every wrench row.
	float axis_weight[6] {}; // Positive squared-residual objective weights, not independent-axis authority.
	float regularization{};
};

struct WrenchAllocationReport {
	float command[5] {};
	float achieved[6] {};
	float residual[6] {}; // Requested minus achieved, evaluated from the returned float commands.
	float realized_edf_thrust_n{}; // Solver: command[4]. Vehicle adapter: forward-mapped representable owner target.
	bool saturation_lower[5] {}; // Effective hard/rate intersection; a fixed channel sets both flags.
	bool saturation_upper[5] {};
	double objective{};
};

// Fixed-storage, bounded weighted least squares: at most 3^5 box active sets.
// No six-row rank assumption, output ownership, physical defaults or clock.
// The mixed-unit regularizer uses the explicitly supplied actuator scales.
bool validWrenchAllocationConfiguration(const WrenchAllocationConfiguration &configuration);

// Shared numerical-resolution check only: finite physical columns, positive
// objective weights/scales and resolvable regularization. No actuator bounds,
// six-axis authority, output permission or measured qualification is implied.
bool validWrenchAllocationScaling(const WrenchAllocationConfiguration &configuration);
ConfigurationResult validateVehicleWrenchAllocationScaling(const VehicleConfiguration &configuration,
		const WrenchModel &model);

// Round toward the interior of a lower/upper bound. Reject nonfinite or
// unrepresentable doubles, preserving result. Uses the target's binary32 format.
bool inwardWrenchBound(double value, bool lower_bound, float &result);

// Previous commands may be outside a changed hard limit. Empty hard/rate
// intersection is Infeasible. ResidualExceeded rejects the regularized optimum;
// it does not prove that no other tolerance-feasible point exists. Every failure
// preserves report. The exhaustive mechanism has no qualified runtime budget.
WrenchAllocationResult allocateWrench(const WrenchAllocationConfiguration &configuration,
				      const float (&requested)[6], const float (&previous)[5], float dt_s,
				      WrenchAllocationReport &report);

// Accepted-profile adapter is implemented separately by the production owner.
WrenchAllocationResult allocateVehicleWrench(const VehicleConfiguration &configuration, const WrenchModel &model,
		const float (&requested)[6], const float (&previous)[5], float previous_edf_command, float dt_s,
		WrenchAllocationReport &report);

// Current-output solve: EDF is fixed to forward(actual owner command), with
// identical canard hard/rate limits and all six residual checks. It does not
// invert thrust to manufacture a different "current" normalized command.
WrenchAllocationResult allocateVehicleWrenchAtCurrentThrust(const VehicleConfiguration &configuration,
		const WrenchModel &model, const float (&requested)[6], const float (&previous)[5],
		float actual_edf_command, float dt_s, WrenchAllocationReport &report);

} // namespace slaaps
