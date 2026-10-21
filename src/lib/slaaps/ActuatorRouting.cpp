#include "ActuatorRouting.hpp"

#include <cmath>

namespace slaaps
{
namespace
{
constexpr unsigned kMinimumPulseUs = 500;
constexpr unsigned kMaximumPulseUs = 2500;
constexpr unsigned kMaximumPwmRateHz = 400;
constexpr unsigned kMicrosecondsPerSecond = 1000000;

bool bankValid(OutputBank bank) { return bank == OutputBank::Main || bank == OutputBank::Aux; }
bool pulseValid(unsigned pulse) { return pulse >= kMinimumPulseUs && pulse <= kMaximumPulseUs; }

// PWM calibration is an exact integral microsecond value, not a tolerance.
bool pulseMatches(float configured, unsigned hardware)
{
	return std::isfinite(configured) && configured >= hardware && configured <= hardware;
}
}

RoutingResult makeRoutingPlan(const VehicleConfiguration &c, const OutputInventory &inventory,
			      bool flight, RoutingPlan &plan)
{
	if (inventory.count > kMaximumOutputChannels) { return {RoutingFault::InvalidInventory, -1}; }

	RoutingPlan candidate{};
	bool assigned[kCanards] {};

	for (unsigned index = 0; index < inventory.count; ++index) {
		const auto &channel = inventory.channels[index];
		const int diagnostic = static_cast<int>(index);

		if (!bankValid(channel.bank) || channel.channel >= kMaximumChannelsPerBank) {
			return {RoutingFault::InvalidInventory, diagnostic};
		}

		for (unsigned previous = 0; previous < index; ++previous) {
			if (inventory.channels[previous].bank == channel.bank && inventory.channels[previous].channel == channel.channel) {
				return {RoutingFault::DuplicatePin, diagnostic};
			}
		}

		if (channel.function == 0) { continue; }

		const bool canard = channel.function >= kCanardFunctionFirst && channel.function < kCanardFunctionFirst + kCanards;

		// A separate raw motor/servo/passthrough function would bypass SLAAPS
		// ownership. Other payload outputs need an explicit future allowlist.
		if (!canard && channel.function != kEdfFunction) { return {RoutingFault::UnsupportedFunction, diagnostic}; }

		if (channel.protocol != OutputProtocol::Pwm || channel.rate_hz == 0 || channel.rate_hz > kMaximumPwmRateHz) {
			return {RoutingFault::UnsupportedProtocol, diagnostic};
		}

		if (!pulseValid(channel.minimum_us) || !pulseValid(channel.maximum_us) || channel.minimum_us >= channel.maximum_us
		    || static_cast<unsigned>(channel.maximum_us) * channel.rate_hz >= kMicrosecondsPerSecond) {
			return {RoutingFault::InvalidCalibration, diagnostic};
		}

		if (canard) {
			const unsigned logical = channel.function - kCanardFunctionFirst;
			const auto &servo = c.canard[logical];

			if (assigned[logical]) { return {RoutingFault::DuplicateFunction, diagnostic}; }

			if (!pulseMatches(servo.pwm_negative, channel.reversed ? channel.maximum_us : channel.minimum_us)
			    || !pulseMatches(servo.pwm_positive, channel.reversed ? channel.minimum_us : channel.maximum_us)
			    || !pulseMatches(servo.pwm_neutral, channel.center_us)
			    || channel.center_us <= channel.minimum_us || channel.center_us >= channel.maximum_us) {
				return {RoutingFault::InvalidCalibration, diagnostic};
			}

			// Current output contract revokes to no pulse. Neutral positioning,
			// if desired, is an explicit admitted command before disarm.
			if (channel.disarmed_us != 0 || channel.failsafe_us != 0) { return {RoutingFault::InvalidOffValue, diagnostic}; }

			candidate.canard[logical] = channel;
			assigned[logical] = true;

		} else {
			if (candidate.edf_assigned) { return {RoutingFault::DuplicateFunction, diagnostic}; }

			// Forward-only EDF: stopped output may be no pulse or minimum
			// calibrated ESC pulse. A high failsafe or reversal is forbidden.
			if (channel.reversed || (channel.disarmed_us != 0 && channel.disarmed_us != channel.minimum_us)
			    || channel.failsafe_us != channel.disarmed_us) {
				return {RoutingFault::InvalidOffValue, diagnostic};
			}

			candidate.edf = channel;
			candidate.edf_assigned = true;
		}

		candidate.bank_mask |= 1U << static_cast<unsigned>(channel.bank);
	}

	for (bool present : assigned) {
		if (!present) { return {RoutingFault::MissingCanard, -1}; }
	}

	if (flight && !candidate.edf_assigned) { return {RoutingFault::MissingEdf, -1}; }

	plan = candidate;
	return {};
}
} // namespace slaaps
