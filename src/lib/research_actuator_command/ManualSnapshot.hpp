#pragma once

#include "ManualControlInput.hpp"

namespace research_actuator_command
{

// Capture first, then read the evaluation clock. A concurrent publisher may
// advance while copy() runs; taking time first would reject that valid sample
// as future-dated. Do not clamp either timestamp or relax the input validator.
// Source/clock are statically bound; no allocation or second runtime owner.
template<typename Source, typename Message, typename Clock>
RuntimeTimeUs copyManualSnapshot(Source &source, Message &message, Clock clock)
{
	message = {};
	(void)source.copy(&message);
	return clock();
}

} // namespace research_actuator_command
