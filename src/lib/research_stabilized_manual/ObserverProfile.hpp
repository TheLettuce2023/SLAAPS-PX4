#pragma once

#include "StabilizedManualControl.hpp"
#include <cstdio>

namespace research_stabilized_manual
{

// Explicit diagnostic configuration, never evidence of physical acceptance.
// No defaults, servo mapping, EDF command or output-enable field are supplied.
struct ObserverProfile {
	Configuration control{};
	TimeUs maximum_status_age_us{};
	TimeUs maximum_permission_age_us{};
};

// Fixed ordered tokens headed SLAAPS_STABILIZED_OBSERVER_V1. Parses into a
// temporary; any malformed, extra or invalid value preserves the caller.
bool readObserverProfile(FILE *stream, ObserverProfile &profile);

} // namespace research_stabilized_manual
