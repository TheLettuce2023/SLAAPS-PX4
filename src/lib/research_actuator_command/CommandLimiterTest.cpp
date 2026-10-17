#include "CommandLimiter.hpp"

#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>

using namespace research_actuator_command;

namespace
{
int checks = 0;
int failures = 0;

void check(bool condition, const std::string &name)
{
	++checks;

	if (!condition) {
		++failures;
		std::cerr << "FAILED: " << name << '\n';
	}
}

// All values below are exact, non-operational math fixtures, not vehicle data.
LimiterConfiguration fixture()
{
	LimiterConfiguration config{};
	config.maximum_dt_s = 0.5F;

	for (size_t i = 0; i < kChannelCount; ++i) {
		config.channels[i] = {i == kEdfChannelIndex ? 0.F : -1.F, 1.F, 2.F, 1.F};
	}

	return config;
}

void expectFailure(const LimiterConfiguration &config, const Command &desired,
		   const Command &previous, float dt, LimitStatus expected, const std::string &name)
{
	const Command sentinel{17.F, -19.F, 23.F, -29.F, 31.F};
	Command output = sentinel;
	LimitReport report{0xA5, 0x5A};
	check(limitCommand(config, desired, previous, dt, output, report) == expected, name + "_status");
	check(std::memcmp(output.data(), sentinel.data(), sizeof(float) * kChannelCount) == 0,
	      name + "_output_unchanged");
	check(report.amplitude_limited_mask == 0xA5 && report.slew_limited_mask == 0x5A,
	      name + "_report_unchanged");
}

void exactFixtures()
{
	const auto config = fixture();
	const Command previous{0.5F, 0.F, 0.F, 0.F, 0.F};
	const Command desired{2.F, 2.F, -2.F, 0.125F, -0.125F};
	const Command expected{1.F, 0.5F, -0.25F, 0.125F, -0.125F};
	Command output{};
	LimitReport report{};
	check(limitCommand(config, desired, previous, 0.25F, output, report) == LimitStatus::Ok, "exact_status");
	check(output == expected, "exact_all_five_outputs");
	check(report.amplitude_limited_mask == 7 && report.slew_limited_mask == 6, "exact_independent_masks");
	check(limitCommand(config, previous, previous, config.maximum_dt_s, output, report) == LimitStatus::Ok,
	      "inclusive_max_dt");
	check(output == previous && report.amplitude_limited_mask == 0 && report.slew_limited_mask == 0,
	      "unchanged_target_no_limit");

	for (size_t i = 0; i < kChannelCount; ++i) {
		Command target = previous;
		target[i] = -2.F;
		check(limitCommand(config, target, previous, 0.25F, output, report) == LimitStatus::Ok,
		      "channel_lower_status");
		check(output[i] == previous[i] - 0.25F, "channel_lower_asymmetric_slew");
		check(report.amplitude_limited_mask == (1U << i) && report.slew_limited_mask == (1U << i),
		      "channel_lower_masks");

		for (size_t j = 0; j < kChannelCount; ++j) {
			check(i == j || output[j] == previous[j], "no_cross_channel_mix");
		}

		auto held = config;
		held.channels[i].minimum = previous[i];
		held.channels[i].maximum = previous[i];
		check(limitCommand(held, target, previous, 0.25F, output, report) == LimitStatus::Ok,
		      "equal_bounds_valid");
		check(output == previous && report.amplitude_limited_mask == (1U << i)
		      && report.slew_limited_mask == 0, "equal_bounds_not_rate_limit");

		held = config;
		held.channels[i].fall_rate_per_s = 0.F;
		check(limitCommand(held, target, previous, 0.25F, output, report) == LimitStatus::Ok
		      && output == previous, "zero_fall_holds");
		target[i] = 2.F;
		held.channels[i].rise_rate_per_s = 0.F;
		check(limitCommand(held, target, previous, 0.25F, output, report) == LimitStatus::Ok
		      && output == previous, "zero_rise_holds");
	}

	Command alias = desired;
	check(limitCommand(config, alias, previous, 0.25F, alias, report) == LimitStatus::Ok && alias == expected,
	      "desired_output_alias");
	alias = previous;
	check(limitCommand(config, desired, alias, 0.25F, alias, report) == LimitStatus::Ok && alias == expected,
	      "previous_output_alias");
	alias = previous;
	check(limitCommand(config, alias, alias, 0.25F, alias, report) == LimitStatus::Ok && alias == previous,
	      "all_command_alias");
}

void invalidFixtures()
{
	const float nan = std::numeric_limits<float>::quiet_NaN();
	const float inf = std::numeric_limits<float>::infinity();
	const auto config = fixture();
	const Command previous{0.5F, 0.F, 0.F, 0.F, 0.F};
	const Command desired{0.75F, 0.25F, -0.25F, 0.125F, -0.125F};

	for (float invalid : {0.F, -0.25F, std::nextafter(config.maximum_dt_s, inf), nan, inf, -inf}) {
		expectFailure(config, desired, previous, invalid, LimitStatus::InvalidTimestep, "invalid_dt");
	}

	for (float invalid : {0.F, -0.25F, nan, inf, -inf}) {
		auto bad = config;
		bad.maximum_dt_s = invalid;
		expectFailure(bad, desired, previous, 0.25F, LimitStatus::InvalidConfiguration, "invalid_max_dt");
	}

	for (size_t i = 0; i < kChannelCount; ++i) {
		for (float invalid : {nan, inf, -inf}) {
			Command bad = desired;
			bad[i] = invalid;
			expectFailure(config, bad, previous, 0.25F, LimitStatus::InvalidInput, "nonfinite_desired");
			bad = previous;
			bad[i] = invalid;
			expectFailure(config, desired, bad, 0.25F, LimitStatus::InvalidPreviousCommand, "nonfinite_previous");

			for (int field = 0; field < 4; ++field) {
				auto bad_config = config;
				auto &channel = bad_config.channels[i];

				switch (field) {
				case 0: channel.minimum = invalid; break;

				case 1: channel.maximum = invalid; break;

				case 2: channel.rise_rate_per_s = invalid; break;

				case 3: channel.fall_rate_per_s = invalid; break;
				}

				expectFailure(bad_config, desired, previous, 0.25F, LimitStatus::InvalidConfiguration,
					      "nonfinite_channel_config");
			}
		}

		for (bool upper : {false, true}) {
			auto bad_config = config;
			auto &channel = bad_config.channels[i];

			if (upper) {
				channel.maximum = std::nextafter(1.F, inf);

			} else {
				channel.minimum = std::nextafter(i == 0 ? 0.F : -1.F, -inf);
			}

			expectFailure(bad_config, desired, previous, 0.25F, LimitStatus::InvalidConfiguration,
				      "outside_channel_domain");
			Command bad_previous = previous;
			bad_previous[i] = upper ? std::nextafter(1.F, inf) : std::nextafter(config.channels[i].minimum, -inf);
			expectFailure(config, desired, bad_previous, 0.25F, LimitStatus::InvalidPreviousCommand,
				      "outside_previous_bounds");
			bad_config = config;

			if (upper) {
				bad_config.channels[i].rise_rate_per_s = -1.F;

			} else {
				bad_config.channels[i].fall_rate_per_s = -1.F;
			}

			expectFailure(bad_config, desired, previous, 0.25F, LimitStatus::InvalidConfiguration, "negative_rate");
		}

		auto bad_config = config;
		bad_config.channels[i].minimum = 0.75F;
		bad_config.channels[i].maximum = 0.25F;
		expectFailure(bad_config, desired, previous, 0.25F, LimitStatus::InvalidConfiguration, "inverted_bounds");
		bad_config = config;
		bad_config.channels[i].minimum = 0.75F;
		expectFailure(bad_config, desired, previous, 0.25F, LimitStatus::InvalidPreviousCommand,
			      "bounds_shrink_no_recovery_jump");
	}
}

void roundingAndRangeFixtures()
{
	const float largest = std::numeric_limits<float>::max();
	const float smallest = std::numeric_limits<float>::denorm_min();
	auto config = fixture();
	Command previous{0.5F, 0.5F, -0.5F, 0.F, -0.F};
	Command desired{largest, largest, -largest, largest, -largest};
	Command output{};
	LimitReport report{};
	check(limitCommand(config, desired, previous, smallest, output, report) == LimitStatus::Ok,
	      "tiny_dt_valid");
	check(output[0] == previous[0] && output[1] == previous[1] && output[2] == previous[2],
	      "sub_ulp_change_cannot_exceed_slew");
	check(report.amplitude_limited_mask == 31 && report.slew_limited_mask == 31, "extreme_desired_masks");
	config.maximum_dt_s = largest;

	for (auto &channel : config.channels) {
		channel.rise_rate_per_s = largest;
		channel.fall_rate_per_s = largest;
	}

	check(limitCommand(config, desired, previous, largest, output, report) == LimitStatus::Ok,
	      "wide_rate_dt_intermediates");
	check(output == Command{1.F, 1.F, -1.F, 1.F, -1.F} && report.slew_limited_mask == 0,
	      "wide_rate_dt_bounded_outputs");

	config = fixture();

	// A double sum can round outward although rate*dt is below one float ULP.
	const float near_unit = 1.F + std::ldexp(1.F, -23);
	const float just_under_ulp = std::ldexp(1.F, -24) * (1.F - std::ldexp(1.F, -23));
	config.channels[0].rise_rate_per_s = near_unit;
	check(limitCommand(config, desired, previous, just_under_ulp, output, report) == LimitStatus::Ok,
	      "outward_endpoint_rounding_status");
	check(static_cast<double>(output[0]) - previous[0] <= static_cast<double>(near_unit) * just_under_ulp,
	      "outward_endpoint_rounding_must_not_exceed_rate");
	config = fixture();
	config.channels[2].fall_rate_per_s = near_unit;
	check(limitCommand(config, desired, previous, just_under_ulp, output, report) == LimitStatus::Ok,
	      "lower_outward_rounding_status");
	check(static_cast<double>(previous[2]) - output[2] <= static_cast<double>(near_unit) * just_under_ulp,
	      "lower_outward_rounding_must_not_exceed_rate");
	config = fixture();
	config.maximum_dt_s = 1.F;
	config.channels[1].rise_rate_per_s = 1.F;
	config.channels[2].fall_rate_per_s = 1.F;
	Command tiny_previous = previous;
	tiny_previous[1] = -smallest;
	tiny_previous[2] = smallest;
	check(limitCommand(config, desired, tiny_previous, 1.F, output, report) == LimitStatus::Ok,
	      "opposite_sign_subnormal_status");
	check(output[1] == std::nextafter(1.F, 0.F) && output[2] == std::nextafter(-1.F, 0.F),
	      "opposite_sign_subnormal_conservative_endpoint");
	config = fixture();

	// Non-binary decimal fixtures verify strict bounds after float rounding.
	for (int index = 1; index <= 31; ++index) {
		const float dt = static_cast<float>(index) / 100.F;

		for (auto &channel : config.channels) {
			channel.rise_rate_per_s = 0.3F;
			channel.fall_rate_per_s = 0.7F;
		}

		check(limitCommand(config, desired, previous, dt, output, report) == LimitStatus::Ok, "rounding_status");

		for (size_t i = 0; i < kChannelCount; ++i) {
			const double delta = static_cast<double>(output[i]) - previous[i];
			check(delta <= static_cast<double>(config.channels[i].rise_rate_per_s) * dt
			      && -delta <= static_cast<double>(config.channels[i].fall_rate_per_s) * dt,
			      "strict_representable_slew_interval");
			check(output[i] >= config.channels[i].minimum && output[i] <= config.channels[i].maximum,
			      "strict_amplitude_interval");
		}
	}
}
} // namespace

int main()
{
	exactFixtures();
	invalidFixtures();
	roundingAndRangeFixtures();
	std::cout << checks << " non-operational fixture checks, " << failures << " failures\n";
	return failures == 0 ? 0 : 1;
}
