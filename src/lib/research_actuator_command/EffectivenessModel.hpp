#pragma once

#include "CommandLimiter.hpp"

#include <cstddef>
#include <cstdint>

namespace research_actuator_command
{

constexpr std::size_t kControlAxisCount{6};

enum class ControlAxis : std::size_t {
	Roll = 0,
	Pitch,
	Yaw,
	ThrustX,
	ThrustY,
	ThrustZ
};

enum class EffectivenessStatus : uint8_t {
	Ok,
	InvalidConfiguration,
	InvalidDemand,
	InvalidCommand,
	ArithmeticFailure
};

using ControlVector = FixedArray<float, kControlAxisCount>;
using EffectivenessMatrix = FixedArray<FixedArray<float, kChannelCount>, kControlAxisCount>;

struct EffectivenessConfiguration {
	EffectivenessMatrix effectiveness;
	Command trim;
	Command minimum;
	Command maximum;
};

struct EffectivenessReport {
	ControlVector achieved;
	ControlVector residual;
	uint8_t lower_saturation_mask;
	uint8_t upper_saturation_mask;
	uint8_t configured_axis_mask;
};

// Pure five-actuator effectiveness evaluation for the PX4 normalized control
// axis order: roll, pitch, yaw, thrust X, thrust Y, thrust Z. Logical actuator
// order is EDF followed by four canards, matching CommandLimiter. Matrix columns
// map command displacement from trim to normalized control; they are not
// physical force/moment coefficients and this function does not allocate.
//
// The caller must supply every matrix value, bound and trim. Each actuator must
// have at least one exactly nonzero configured effect. This checks structure,
// finite arithmetic and command domains only; it does not establish rank,
// conditioning, authority, feasibility or aerodynamic validity. Commands must
// already be within their configured bounds. On failure, report is unchanged.
EffectivenessStatus evaluateEffectiveness(const EffectivenessConfiguration &configuration,
		const ControlVector &demand,
		const Command &command,
		EffectivenessReport &report);

} // namespace research_actuator_command
