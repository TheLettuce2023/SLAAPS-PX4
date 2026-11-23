#pragma once

#include "StabilizedManualControl.hpp"

namespace research_stabilized_manual
{

// Same raw manual-input policy used by the controller and independent consumer.
// Readiness may additionally validate throttle without granting propulsion.
research_actuator_command::ManualControlStatus configureStabilizedManualInput(const Configuration &configuration,
		research_actuator_command::ManualControlInput &input, bool include_throttle = false);

// Requires a validated, immutable Configuration. Checks numerical state,
// freshness and optional previous-sample continuity without integration or
// mutation. The normalized attitude is assigned only after successful checks.
//
// A changed source epoch is always StateChanged. A changed attitude reset counter within the same epoch (an estimator
// attitude reset, for example EKF2 switching to 3D magnetometer fusion) is StateChanged unless the caller passes
// attitude_reset: then the sample is accepted and *attitude_reset reports the reset, so the caller can re-base any
// attitude reference it holds. Every other continuity check still applies.
Status validateStabilizedState(TimeUs now, const StateSample &sample, const Configuration &configuration,
			       const StateSample *previous, Quaternion &attitude, bool *attitude_reset = nullptr);

} // namespace research_stabilized_manual
