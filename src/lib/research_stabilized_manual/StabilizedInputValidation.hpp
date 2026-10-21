#pragma once

#include "StabilizedManualControl.hpp"

namespace research_stabilized_manual
{

// Same raw manual-input policy used by the controller and independent consumer.
research_actuator_command::ManualControlStatus configureStabilizedManualInput(const Configuration &configuration,
		research_actuator_command::ManualControlInput &input);

// Requires a validated, immutable Configuration. Checks numerical state,
// freshness and optional previous-sample continuity without integration or
// mutation. The normalized attitude is assigned only after successful checks.
Status validateStabilizedState(TimeUs now, const StateSample &sample, const Configuration &configuration,
			       const StateSample *previous, Quaternion &attitude);

} // namespace research_stabilized_manual
