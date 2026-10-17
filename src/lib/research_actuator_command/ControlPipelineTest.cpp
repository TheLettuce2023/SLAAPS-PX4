#include "ControlPipeline.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <string>
#include <type_traits>

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

bool sameReport(const PipelineReport &left, const PipelineReport &right)
{
	return left.candidate_command == right.candidate_command
	       && left.limit.amplitude_limited_mask == right.limit.amplitude_limited_mask
	       && left.limit.slew_limited_mask == right.limit.slew_limited_mask
	       && left.effectiveness.achieved == right.effectiveness.achieved
	       && left.effectiveness.residual == right.effectiveness.residual
	       && left.effectiveness.lower_saturation_mask == right.effectiveness.lower_saturation_mask
	       && left.effectiveness.upper_saturation_mask == right.effectiveness.upper_saturation_mask
	       && left.effectiveness.configured_axis_mask == right.effectiveness.configured_axis_mask
	       && left.authority.axis_norm == right.authority.axis_norm
	       && left.authority.independence == right.authority.independence
	       && left.authority.selected_axis_mask == right.authority.selected_axis_mask
	       && left.authority.weak_axis_mask == right.authority.weak_axis_mask
	       && left.authority.dependent_axis_mask == right.authority.dependent_axis_mask
	       && left.authority.rank == right.authority.rank;
}

PipelineReport sentinelReport()
{
	PipelineReport report{};
	report.candidate_command = {17.0F, -19.0F, 23.0F, -29.0F, 31.0F};
	report.limit = {0xA5, 0x5A};
	report.effectiveness.achieved = {1.0F, 2.0F, 3.0F, 4.0F, 5.0F, 6.0F};
	report.effectiveness.residual = {-1.0F, -2.0F, -3.0F, -4.0F, -5.0F, -6.0F};
	report.effectiveness.lower_saturation_mask = 0x12;
	report.effectiveness.upper_saturation_mask = 0x21;
	report.effectiveness.configured_axis_mask = 0x3F;
	report.authority.axis_norm = {7.0, 8.0, 9.0, 10.0, 11.0, 12.0};
	report.authority.independence = {-7.0, -8.0, -9.0, -10.0, -11.0, -12.0};
	report.authority.selected_axis_mask = 0x1F;
	report.authority.weak_axis_mask = 0x20;
	report.authority.dependent_axis_mask = 0x40;
	report.authority.rank = 5U;
	return report;
}

// Exact non-operational fixture. It is not a model of the research vehicle and
// establishes no actuator sign, physical authority, gain, limit or safe trim.
PipelineConfiguration fixture()
{
	PipelineConfiguration configuration{};
	configuration.limiter.maximum_dt_s = 0.5F;

	for (std::size_t actuator = 0; actuator < kChannelCount; ++actuator) {
		configuration.limiter.channels[actuator] = {
			actuator == kEdfChannelIndex ? 0.0F : -1.0F,
			1.0F,
			1.0F,
			1.0F
		};
	}

	configuration.effectiveness[0] = {1.0F, 0.0F, 0.0F, 0.0F, 0.0F};
	configuration.effectiveness[1] = {0.0F, 1.0F, 0.0F, 0.0F, 0.0F};
	configuration.effectiveness[2] = {0.0F, 0.0F, 1.0F, 0.0F, 0.0F};
	configuration.effectiveness[3] = {0.0F, 0.0F, 0.0F, 1.0F, 0.0F};
	configuration.effectiveness[4] = {0.0F, 0.0F, 0.0F, 0.0F, 1.0F};
	configuration.effectiveness[5] = {1.0F, 1.0F, 0.0F, 0.0F, 0.0F};
	configuration.trim = {0.25F, 0.0F, 0.0F, 0.0F, 0.0F};
	configuration.authority.priority = {ControlAxis::Roll, ControlAxis::Pitch, ControlAxis::Yaw,
					    ControlAxis::ThrustX, ControlAxis::ThrustY, ControlAxis::ThrustZ
					   };
	configuration.authority.minimum_axis_norm = 0.01;
	configuration.authority.minimum_independence = 1.0e-10;
	configuration.required_axis_mask = 0x1FU;
	return configuration;
}

void stateAndCompositionFixtures()
{
	static_assert(!std::is_copy_constructible<ControlPipeline>::value, "pipeline must have one owner");
	static_assert(!std::is_move_constructible<ControlPipeline>::value, "pipeline state must not move");

	ControlPipeline pipeline;
	check(!pipeline.configured(), "initially_unconfigured");
	check(pipeline.configure(fixture()) == PipelineStatus::Ok, "configure_status");
	check(pipeline.configured() && pipeline.previousCommand() == fixture().trim,
	      "configure_initializes_trim_state");
	check(pipeline.authorityReport().rank == 5U
	      && pipeline.authorityReport().selected_axis_mask == 0x1FU,
	      "configure_caches_required_authority");

	const Command desired{0.75F, 0.5F, -0.5F, 0.25F, -0.25F};
	const ControlVector demand{1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F};
	PipelineReport report{};
	check(pipeline.evaluate(demand, desired, 0.25F, report) == PipelineStatus::Ok,
	      "first_evaluate_status");
	check(report.candidate_command == Command{0.5F, 0.25F, -0.25F, 0.25F, -0.25F},
	      "first_evaluate_limited_candidate");
	check(report.limit.amplitude_limited_mask == 0U && report.limit.slew_limited_mask == 0x07U,
	      "first_evaluate_limit_masks");
	check(near(report.effectiveness.achieved[0], 0.25F)
	      && near(report.effectiveness.achieved[1], 0.25F)
	      && near(report.effectiveness.achieved[2], -0.25F)
	      && near(report.effectiveness.achieved[3], 0.25F)
	      && near(report.effectiveness.achieved[4], -0.25F)
	      && near(report.effectiveness.achieved[5], 0.5F),
	      "first_evaluate_achieved_control");
	check(near(report.effectiveness.residual[0], 0.75F)
	      && near(report.effectiveness.residual[1], 0.75F)
	      && near(report.effectiveness.residual[2], 1.25F)
	      && near(report.effectiveness.residual[3], 0.75F)
	      && near(report.effectiveness.residual[4], 1.25F)
	      && near(report.effectiveness.residual[5], 0.5F),
	      "first_evaluate_residual_control");
	check(pipeline.previousCommand() == report.candidate_command,
	      "success_advances_state_once");
	check(report.authority.selected_axis_mask == 0x1FU && report.authority.rank == 5U,
	      "evaluation_carries_configured_authority");

	check(pipeline.evaluate(demand, desired, 0.25F, report) == PipelineStatus::Ok,
	      "second_evaluate_status");
	check(report.candidate_command == desired && report.limit.slew_limited_mask == 0U,
	      "second_evaluate_uses_owned_previous_command");
}

void failurePreservationFixtures()
{
	const float nan = std::numeric_limits<float>::quiet_NaN();
	const float inf = std::numeric_limits<float>::infinity();
	const Command desired{0.75F, 0.5F, -0.5F, 0.25F, -0.25F};
	const ControlVector demand{};
	ControlPipeline unconfigured;
	PipelineReport report = sentinelReport();
	const PipelineReport sentinel = report;
	check(unconfigured.evaluate(demand, desired, 0.25F, report) == PipelineStatus::NotConfigured,
	      "unconfigured_status");
	check(sameReport(report, sentinel), "unconfigured_report_unchanged");

	ControlPipeline pipeline;
	check(pipeline.configure(fixture()) == PipelineStatus::Ok, "failure_fixture_configure");
	const Command initial_state = pipeline.previousCommand();

	for (float invalid_dt : {0.0F, -1.0F, std::nextafter(0.5F, inf), nan, inf, -inf}) {
		report = sentinel;
		check(pipeline.evaluate(demand, desired, invalid_dt, report) == PipelineStatus::InvalidTimestep,
		      "invalid_dt_status");
		check(sameReport(report, sentinel) && pipeline.previousCommand() == initial_state,
		      "invalid_dt_preserves_report_and_state");
	}

	for (std::size_t actuator = 0; actuator < kChannelCount; ++actuator) {
		Command invalid_desired = desired;
		invalid_desired[actuator] = nan;
		report = sentinel;
		check(pipeline.evaluate(demand, invalid_desired, 0.25F, report)
		      == PipelineStatus::InvalidDesiredCommand, "invalid_desired_status");
		check(sameReport(report, sentinel) && pipeline.previousCommand() == initial_state,
		      "invalid_desired_preserves_report_and_state");
	}

	for (std::size_t axis = 0; axis < kControlAxisCount; ++axis) {
		ControlVector invalid_demand = demand;
		invalid_demand[axis] = inf;
		report = sentinel;
		check(pipeline.evaluate(invalid_demand, desired, 0.25F, report) == PipelineStatus::InvalidDemand,
		      "invalid_demand_status");
		check(sameReport(report, sentinel) && pipeline.previousCommand() == initial_state,
		      "invalid_demand_preserves_report_and_state");
	}
}

void configurationFixtures()
{
	const float nan = std::numeric_limits<float>::quiet_NaN();
	ControlPipeline pipeline;

	for (uint8_t invalid_mask : {uint8_t{0U}, uint8_t{0x40U}, uint8_t{0xFFU}}) {
		auto bad = fixture();
		bad.required_axis_mask = invalid_mask;
		check(pipeline.configure(bad) == PipelineStatus::InvalidConfiguration,
		      "invalid_required_mask");
		check(!pipeline.configured(), "invalid_required_mask_no_state");
	}

	auto bad = fixture();
	bad.required_axis_mask = 0x20U;
	check(pipeline.configure(bad) == PipelineStatus::InsufficientAuthority,
	      "dependent_required_axis_rejected");

	bad = fixture();
	bad.authority.priority[5] = ControlAxis::Roll;
	check(pipeline.configure(bad) == PipelineStatus::InvalidConfiguration,
	      "invalid_authority_policy_rejected");

	bad = fixture();
	bad.effectiveness[0][0] = nan;
	check(pipeline.configure(bad) == PipelineStatus::InvalidConfiguration,
	      "nonfinite_matrix_rejected");

	bad = fixture();

	for (std::size_t axis = 0; axis < kControlAxisCount; ++axis) {
		bad.effectiveness[axis][4] = 0.0F;
	}

	check(pipeline.configure(bad) == PipelineStatus::InvalidConfiguration,
	      "zero_effectiveness_column_rejected");

	bad = fixture();
	bad.limiter.channels[1].minimum = 0.75F;
	bad.limiter.channels[1].maximum = 0.25F;
	check(pipeline.configure(bad) == PipelineStatus::InvalidConfiguration,
	      "invalid_limiter_rejected");

	bad = fixture();
	bad.trim[0] = 2.0F;
	check(pipeline.configure(bad) == PipelineStatus::InvalidConfiguration,
	      "trim_outside_bounds_rejected");

	check(pipeline.configure(fixture()) == PipelineStatus::Ok, "valid_after_failures");
	PipelineReport report{};
	const Command desired{0.5F, 0.25F, -0.25F, 0.25F, -0.25F};
	check(pipeline.evaluate(ControlVector{}, desired, 0.25F, report) == PipelineStatus::Ok,
	      "state_before_failed_reconfigure");
	const Command saved_state = pipeline.previousCommand();
	bad = fixture();
	bad.authority.minimum_independence = 0.0;
	check(pipeline.configure(bad) == PipelineStatus::InvalidConfiguration,
	      "failed_reconfigure_status");
	check(pipeline.configured() && pipeline.previousCommand() == saved_state,
	      "failed_reconfigure_preserves_valid_state");

	auto replacement = fixture();
	replacement.trim[0] = 0.4F;
	check(pipeline.configure(replacement) == PipelineStatus::Ok,
	      "valid_reconfigure_status");
	check(pipeline.previousCommand() == replacement.trim,
	      "valid_reconfigure_resets_to_new_trim");
}

} // namespace

int main()
{
	stateAndCompositionFixtures();
	failurePreservationFixtures();
	configurationFixtures();
	std::cout << checks << " non-operational pipeline checks, " << failures << " failures\n";
	return failures == 0 ? 0 : 1;
}
