#include "EffectivenessModel.hpp"

#include <cmath>
#include <float.h>

namespace research_actuator_command
{
namespace
{

bool isFinite(float value)
{
	return std::isfinite(value);
}

bool validConfiguration(const EffectivenessConfiguration &configuration)
{
	FixedArray<bool, kChannelCount> actuator_has_effect{};

	for (std::size_t axis = 0; axis < kControlAxisCount; ++axis) {
		for (std::size_t actuator = 0; actuator < kChannelCount; ++actuator) {
			const float coefficient = configuration.effectiveness[axis][actuator];

			if (!isFinite(coefficient)) {
				return false;
			}

			actuator_has_effect[actuator] = actuator_has_effect[actuator]
							|| coefficient < 0.0F || coefficient > 0.0F;
		}
	}

	for (std::size_t actuator = 0; actuator < kChannelCount; ++actuator) {
		const float domain_minimum = actuator == kEdfChannelIndex ? 0.0F : -1.0F;

		if (!actuator_has_effect[actuator]
		    || !isFinite(configuration.minimum[actuator])
		    || !isFinite(configuration.maximum[actuator])
		    || !isFinite(configuration.trim[actuator])
		    || configuration.minimum[actuator] < domain_minimum
		    || configuration.maximum[actuator] > 1.0F
		    || configuration.minimum[actuator] > configuration.maximum[actuator]
		    || configuration.trim[actuator] < configuration.minimum[actuator]
		    || configuration.trim[actuator] > configuration.maximum[actuator]) {
			return false;
		}
	}

	return true;
}

bool representableFloat(double value)
{
	return std::isfinite(value)
	       && value >= -static_cast<double>(FLT_MAX)
	       && value <= static_cast<double>(FLT_MAX);
}

} // namespace

EffectivenessStatus evaluateEffectiveness(const EffectivenessConfiguration &configuration,
		const ControlVector &demand,
		const Command &command,
		EffectivenessReport &report)
{
	if (!validConfiguration(configuration)) {
		return EffectivenessStatus::InvalidConfiguration;
	}

	for (float value : demand) {
		if (!isFinite(value)) {
			return EffectivenessStatus::InvalidDemand;
		}
	}

	for (std::size_t actuator = 0; actuator < kChannelCount; ++actuator) {
		if (!isFinite(command[actuator])
		    || command[actuator] < configuration.minimum[actuator]
		    || command[actuator] > configuration.maximum[actuator]) {
			return EffectivenessStatus::InvalidCommand;
		}
	}

	EffectivenessReport candidate{};

	for (std::size_t actuator = 0; actuator < kChannelCount; ++actuator) {
		const uint8_t actuator_bit = static_cast<uint8_t>(1U << actuator);

		if (command[actuator] <= configuration.minimum[actuator]) {
			candidate.lower_saturation_mask = static_cast<uint8_t>(candidate.lower_saturation_mask | actuator_bit);
		}

		if (command[actuator] >= configuration.maximum[actuator]) {
			candidate.upper_saturation_mask = static_cast<uint8_t>(candidate.upper_saturation_mask | actuator_bit);
		}
	}

	for (std::size_t axis = 0; axis < kControlAxisCount; ++axis) {
		double achieved = 0.0;
		bool configured = false;

		for (std::size_t actuator = 0; actuator < kChannelCount; ++actuator) {
			const float coefficient = configuration.effectiveness[axis][actuator];
			const double displacement = static_cast<double>(command[actuator])
						    - static_cast<double>(configuration.trim[actuator]);
			achieved += static_cast<double>(coefficient) * displacement;
			configured = configured || coefficient < 0.0F || coefficient > 0.0F;
		}

		const double residual = static_cast<double>(demand[axis]) - achieved;

		if (!representableFloat(achieved) || !representableFloat(residual)) {
			return EffectivenessStatus::ArithmeticFailure;
		}

		candidate.achieved[axis] = static_cast<float>(achieved);
		candidate.residual[axis] = static_cast<float>(residual);

		if (configured) {
			candidate.configured_axis_mask = static_cast<uint8_t>(candidate.configured_axis_mask | (1U << axis));
		}
	}

	report = candidate;
	return EffectivenessStatus::Ok;
}

} // namespace research_actuator_command
