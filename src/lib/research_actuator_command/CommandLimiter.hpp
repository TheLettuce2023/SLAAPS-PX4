#pragma once

#include "FixedArray.hpp"

#include <cstddef>
#include <cstdint>

namespace research_actuator_command
{

constexpr std::size_t kChannelCount{5};
constexpr std::size_t kEdfChannelIndex{0};

enum class LimitStatus : uint8_t {
	Ok,
	InvalidConfiguration,
	InvalidInput,
	InvalidTimestep,
	InvalidPreviousCommand,
	ArithmeticFailure
};

struct ChannelLimit {
	float minimum;
	float maximum;
	float rise_rate_per_s;
	float fall_rate_per_s;
};

struct LimiterConfiguration {
	FixedArray<ChannelLimit, kChannelCount> channels;
	float maximum_dt_s;
};

struct LimitReport {
	uint8_t amplitude_limited_mask;
	uint8_t slew_limited_mask;
};

using Command = FixedArray<float, kChannelCount>;

// Pure inactive command-domain math, not a controller, allocator or output driver.
// Logical order: EDF [0, 1], then four independent servos [-1, 1]. Caller bounds
// may be narrower; all rates are normalized command units per second. These
// coordinates imply no pin mapping, surface sign, trim or safe flight response.
// Configuration has no operational defaults. dt_s must be in (0, maximum_dt_s].
// Previous commands must already lie inside the current configured bounds.
// Every failure leaves BOTH output and report unchanged: check the return value
// before using them. Success is not evidence of physical actuator tracking.
// Output may alias desired or previous. Bits 0..4 report amplitude limiting of
// desired, and slew limiting of the amplitude-constrained target, respectively.
LimitStatus limitCommand(const LimiterConfiguration &configuration,
			 const Command &desired,
			 const Command &previous,
			 float dt_s,
			 Command &output,
			 LimitReport &report);

} // namespace research_actuator_command
