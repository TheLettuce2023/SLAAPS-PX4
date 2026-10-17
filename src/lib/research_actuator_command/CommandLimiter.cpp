#include "CommandLimiter.hpp"

#include <cmath>

namespace research_actuator_command
{
namespace
{

bool isFinite(float value)
{
	return std::isfinite(value);
}

template<typename T>
T minimum(T left, T right)
{
	return left < right ? left : right;
}

template<typename T>
T maximum(T left, T right)
{
	return left > right ? left : right;
}

template<typename T>
T constrain(T value, T lower, T upper)
{
	return minimum(maximum(value, lower), upper);
}

bool validConfiguration(const LimiterConfiguration &configuration)
{
	if (!isFinite(configuration.maximum_dt_s) || configuration.maximum_dt_s <= 0.0F) {
		return false;
	}

	for (std::size_t index = 0; index < kChannelCount; ++index) {
		const ChannelLimit &limit = configuration.channels[index];
		const float domain_minimum = index == kEdfChannelIndex ? 0.0F : -1.0F;
		const float domain_maximum = 1.0F;

		if (!isFinite(limit.minimum) || !isFinite(limit.maximum)
		    || !isFinite(limit.rise_rate_per_s) || !isFinite(limit.fall_rate_per_s)
		    || limit.minimum < domain_minimum || limit.maximum > domain_maximum
		    || limit.minimum > limit.maximum || limit.rise_rate_per_s < 0.0F
		    || limit.fall_rate_per_s < 0.0F) {
			return false;
		}
	}

	return true;
}

bool inwardRounded(float &value, double lower, double upper)
{
	if (!std::isfinite(value)) {
		return false;
	}

	if (static_cast<double>(value) < lower) {
		value = nextafterf(value, HUGE_VALF);
	}

	if (static_cast<double>(value) > upper) {
		value = nextafterf(value, -HUGE_VALF);
	}

	return std::isfinite(value) && static_cast<double>(value) >= lower
	       && static_cast<double>(value) <= upper;
}

double conservativeEndpoint(double previous, double change, bool lower)
{
	// TwoSum recovers the residual lost by the endpoint addition. Products of
	// two binary32 values are exact in binary64; all inputs here are finite.
	// Only an outward-rounded sum needs correction. Do not use fast-math:
	// reassociation would invalidate this error-free transform.
	const double sum = previous + change;
	const double change_in_sum = sum - previous;
	const double error = (previous - (sum - change_in_sum)) + (change - change_in_sum);

	if ((lower && error > 0.0) || (!lower && error < 0.0)) {
		return nextafter(sum, lower ? HUGE_VAL : -HUGE_VAL);
	}

	return sum;
}

} // namespace

LimitStatus limitCommand(const LimiterConfiguration &configuration,
			 const Command &desired,
			 const Command &previous,
			 float dt_s,
			 Command &output,
			 LimitReport &report)
{
	if (!validConfiguration(configuration)) {
		return LimitStatus::InvalidConfiguration;
	}

	if (!isFinite(dt_s) || dt_s <= 0.0F || dt_s > configuration.maximum_dt_s) {
		return LimitStatus::InvalidTimestep;
	}

	for (std::size_t index = 0; index < kChannelCount; ++index) {
		if (!isFinite(desired[index])) {
			return LimitStatus::InvalidInput;
		}

		const ChannelLimit &limit = configuration.channels[index];

		if (!isFinite(previous[index]) || previous[index] < limit.minimum
		    || previous[index] > limit.maximum) {
			return LimitStatus::InvalidPreviousCommand;
		}
	}

	Command candidate{};
	LimitReport candidate_report{};

	for (std::size_t index = 0; index < kChannelCount; ++index) {
		const ChannelLimit &limit = configuration.channels[index];
		const float amplitude_target = constrain(desired[index], limit.minimum, limit.maximum);
		const bool amplitude_limited = desired[index] < limit.minimum || desired[index] > limit.maximum;
		const double rise_distance = static_cast<double>(limit.rise_rate_per_s) * static_cast<double>(dt_s);
		const double fall_distance = static_cast<double>(limit.fall_rate_per_s) * static_cast<double>(dt_s);

		if (!std::isfinite(rise_distance) || !std::isfinite(fall_distance)) {
			return LimitStatus::ArithmeticFailure;
		}

		const double lower = maximum(static_cast<double>(limit.minimum),
					     conservativeEndpoint(previous[index], -fall_distance, true));
		const double upper = minimum(static_cast<double>(limit.maximum),
					     conservativeEndpoint(previous[index], rise_distance, false));

		if (!std::isfinite(lower) || !std::isfinite(upper) || lower > upper) {
			return LimitStatus::ArithmeticFailure;
		}

		const double target = static_cast<double>(amplitude_target);
		const bool slew_limited = target < lower || target > upper;
		const double limited = constrain(target, lower, upper);
		float rounded = static_cast<float>(limited);

		if (!inwardRounded(rounded, lower, upper)) {
			return LimitStatus::ArithmeticFailure;
		}

		candidate[index] = rounded;
		const uint8_t channel_mask = static_cast<uint8_t>(1U << index);

		if (amplitude_limited) {
			candidate_report.amplitude_limited_mask = static_cast<uint8_t>(
						candidate_report.amplitude_limited_mask | channel_mask);
		}

		if (slew_limited) {
			candidate_report.slew_limited_mask = static_cast<uint8_t>(
					candidate_report.slew_limited_mask | channel_mask);
		}
	}

	output = candidate;
	report = candidate_report;
	return LimitStatus::Ok;
}

} // namespace research_actuator_command
