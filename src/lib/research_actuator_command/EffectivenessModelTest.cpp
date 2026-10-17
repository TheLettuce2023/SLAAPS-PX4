#include "EffectivenessModel.hpp"

#include <cmath>
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

bool near(float actual, float expected, float tolerance = 1.0e-6F)
{
	return std::fabs(actual - expected) <= tolerance;
}

bool sameReport(const EffectivenessReport &left, const EffectivenessReport &right)
{
	return left.achieved == right.achieved
	       && left.residual == right.residual
	       && left.lower_saturation_mask == right.lower_saturation_mask
	       && left.upper_saturation_mask == right.upper_saturation_mask
	       && left.configured_axis_mask == right.configured_axis_mask;
}

// All values are exact, non-operational software fixtures. The matrix is not a
// model of the research vehicle and establishes no actuator signs or authority.
EffectivenessConfiguration fixture()
{
	EffectivenessConfiguration configuration{};
	configuration.effectiveness[static_cast<std::size_t>(ControlAxis::Roll)] = {0.0F, 1.0F, -1.0F, 1.0F, -1.0F};
	configuration.effectiveness[static_cast<std::size_t>(ControlAxis::Pitch)] = {0.0F, 1.0F, 1.0F, -1.0F, -1.0F};
	configuration.effectiveness[static_cast<std::size_t>(ControlAxis::Yaw)] = {0.0F, 0.5F, -0.5F, -0.5F, 0.5F};
	configuration.effectiveness[static_cast<std::size_t>(ControlAxis::ThrustX)] = {1.0F, 0.0F, 0.0F, 0.0F, 0.0F};
	configuration.effectiveness[static_cast<std::size_t>(ControlAxis::ThrustZ)] = {0.0F, 0.25F, 0.25F, 0.25F, 0.25F};
	configuration.trim = {0.25F, 0.0F, 0.0F, 0.0F, 0.0F};
	configuration.minimum = {0.0F, -1.0F, -1.0F, -1.0F, -1.0F};
	configuration.maximum = {1.0F, 1.0F, 1.0F, 1.0F, 1.0F};
	return configuration;
}

void expectFailure(const EffectivenessConfiguration &configuration, const ControlVector &demand,
		   const Command &command, EffectivenessStatus expected, const std::string &name)
{
	EffectivenessReport report{
		{17.0F, -19.0F, 23.0F, -29.0F, 31.0F, -37.0F},
		{-41.0F, 43.0F, -47.0F, 53.0F, -59.0F, 61.0F},
		0xA5,
		0x5A,
		0x3C
	};
	const EffectivenessReport sentinel = report;
	check(evaluateEffectiveness(configuration, demand, command, report) == expected, name + "_status");
	check(sameReport(report, sentinel), name + "_report_unchanged");
}

void exactFixtures()
{
	const auto configuration = fixture();
	const Command command{0.75F, 0.5F, -0.25F, 0.25F, -0.5F};
	const ControlVector demand{2.0F, 1.0F, 1.0F, 0.75F, 0.0F, 0.5F};
	EffectivenessReport report{};
	check(evaluateEffectiveness(configuration, demand, command, report) == EffectivenessStatus::Ok,
	      "exact_status");
	check(near(report.achieved[0], 1.5F) && near(report.achieved[1], 0.5F)
	      && near(report.achieved[2], 0.0F) && near(report.achieved[3], 0.5F)
	      && near(report.achieved[4], 0.0F) && near(report.achieved[5], 0.0F),
	      "exact_achieved_control");
	check(near(report.residual[0], 0.5F) && near(report.residual[1], 0.5F)
	      && near(report.residual[2], 1.0F) && near(report.residual[3], 0.25F)
	      && near(report.residual[4], 0.0F) && near(report.residual[5], 0.5F),
	      "exact_residual_control");
	check(report.configured_axis_mask == 0x2FU, "configured_axis_mask");
	check(report.lower_saturation_mask == 0U && report.upper_saturation_mask == 0U,
	      "unsaturated_masks");

	const Command saturated{0.0F, -1.0F, 1.0F, -1.0F, 1.0F};
	check(evaluateEffectiveness(configuration, ControlVector{}, saturated, report) == EffectivenessStatus::Ok,
	      "saturated_status");
	check(report.lower_saturation_mask == 0x0BU && report.upper_saturation_mask == 0x14U,
	      "independent_saturation_masks");

	auto fixed = configuration;
	fixed.minimum[2] = fixed.maximum[2] = fixed.trim[2] = 0.25F;
	Command fixed_command = command;
	fixed_command[2] = 0.25F;
	check(evaluateEffectiveness(fixed, ControlVector{}, fixed_command, report) == EffectivenessStatus::Ok,
	      "equal_bound_status");
	check((report.lower_saturation_mask & (1U << 2)) != 0U
	      && (report.upper_saturation_mask & (1U << 2)) != 0U,
	      "equal_bound_sets_both_masks");
}

void invalidConfigurationFixtures()
{
	const float nan = std::numeric_limits<float>::quiet_NaN();
	const float inf = std::numeric_limits<float>::infinity();
	const ControlVector demand{};
	const Command command{0.25F, 0.0F, 0.0F, 0.0F, 0.0F};
	const auto configuration = fixture();

	for (float invalid : {nan, inf, -inf}) {
		auto bad = configuration;
		bad.effectiveness[0][0] = invalid;
		expectFailure(bad, demand, command, EffectivenessStatus::InvalidConfiguration,
			      "nonfinite_effectiveness");
		bad = configuration;
		bad.minimum[1] = invalid;
		expectFailure(bad, demand, command, EffectivenessStatus::InvalidConfiguration,
			      "nonfinite_minimum");
		bad = configuration;
		bad.maximum[1] = invalid;
		expectFailure(bad, demand, command, EffectivenessStatus::InvalidConfiguration,
			      "nonfinite_maximum");
		bad = configuration;
		bad.trim[1] = invalid;
		expectFailure(bad, demand, command, EffectivenessStatus::InvalidConfiguration,
			      "nonfinite_trim");
	}

	for (std::size_t actuator = 0; actuator < kChannelCount; ++actuator) {
		auto bad = configuration;

		for (std::size_t axis = 0; axis < kControlAxisCount; ++axis) {
			bad.effectiveness[axis][actuator] = 0.0F;
		}

		expectFailure(bad, demand, command, EffectivenessStatus::InvalidConfiguration,
			      "zero_effectiveness_column");

		bad = configuration;
		bad.minimum[actuator] = -2.0F;
		expectFailure(bad, demand, command, EffectivenessStatus::InvalidConfiguration,
			      "minimum_outside_domain");
		bad = configuration;
		bad.maximum[actuator] = 2.0F;
		expectFailure(bad, demand, command, EffectivenessStatus::InvalidConfiguration,
			      "maximum_outside_domain");
		bad = configuration;
		bad.minimum[actuator] = 0.75F;
		bad.maximum[actuator] = 0.5F;
		expectFailure(bad, demand, command, EffectivenessStatus::InvalidConfiguration,
			      "inverted_bounds");
		bad = configuration;
		bad.trim[actuator] = std::nextafter(bad.maximum[actuator], inf);
		expectFailure(bad, demand, command, EffectivenessStatus::InvalidConfiguration,
			      "trim_above_bound");
		bad = configuration;
		bad.trim[actuator] = std::nextafter(bad.minimum[actuator], -inf);
		expectFailure(bad, demand, command, EffectivenessStatus::InvalidConfiguration,
			      "trim_below_bound");
	}
}

void invalidInputFixtures()
{
	const float nan = std::numeric_limits<float>::quiet_NaN();
	const float inf = std::numeric_limits<float>::infinity();
	const auto configuration = fixture();
	const ControlVector demand{};
	const Command command{0.25F, 0.0F, 0.0F, 0.0F, 0.0F};

	for (float invalid : {nan, inf, -inf}) {
		for (std::size_t axis = 0; axis < kControlAxisCount; ++axis) {
			ControlVector bad = demand;
			bad[axis] = invalid;
			expectFailure(configuration, bad, command, EffectivenessStatus::InvalidDemand,
				      "nonfinite_demand");
		}

		for (std::size_t actuator = 0; actuator < kChannelCount; ++actuator) {
			Command bad = command;
			bad[actuator] = invalid;
			expectFailure(configuration, demand, bad, EffectivenessStatus::InvalidCommand,
				      "nonfinite_command");
		}
	}

	for (std::size_t actuator = 0; actuator < kChannelCount; ++actuator) {
		Command bad = command;
		bad[actuator] = std::nextafter(configuration.minimum[actuator], -inf);
		expectFailure(configuration, demand, bad, EffectivenessStatus::InvalidCommand,
			      "command_below_bound");
		bad[actuator] = std::nextafter(configuration.maximum[actuator], inf);
		expectFailure(configuration, demand, bad, EffectivenessStatus::InvalidCommand,
			      "command_above_bound");
	}
}

void arithmeticFixtures()
{
	auto configuration = fixture();
	const float largest = std::numeric_limits<float>::max();
	configuration.effectiveness[0][1] = largest;
	configuration.effectiveness[0][2] = largest;
	EffectivenessReport report{};
	const Command command{0.25F, 1.0F, 1.0F, 0.0F, 0.0F};
	expectFailure(configuration, ControlVector{}, command, EffectivenessStatus::ArithmeticFailure,
		      "achieved_overflow");

	configuration = fixture();
	configuration.effectiveness[0][1] = -largest;
	const ControlVector demand{largest, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F};
	check(evaluateEffectiveness(configuration, demand, command, report) == EffectivenessStatus::ArithmeticFailure,
	      "residual_overflow");
}

} // namespace

int main()
{
	exactFixtures();
	invalidConfigurationFixtures();
	invalidInputFixtures();
	arithmeticFixtures();
	std::cout << checks << " non-operational effectiveness checks, " << failures << " failures\n";
	return failures == 0 ? 0 : 1;
}
