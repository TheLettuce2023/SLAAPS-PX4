#include "ControlRuntimeOwner.hpp"

#include <cmath>
#include <cstdint>
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

bool samePipelineReport(const PipelineReport &left, const PipelineReport &right)
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

bool sameRuntimeReport(const RuntimeReport &left, const RuntimeReport &right)
{
	return left.timestamp_us == right.timestamp_us
	       && left.delta_us == right.delta_us
	       && samePipelineReport(left.pipeline, right.pipeline);
}

RuntimeReport sentinelReport()
{
	RuntimeReport report{};
	report.timestamp_us = 101U;
	report.delta_us = 103U;
	report.pipeline.candidate_command = {17.0F, -19.0F, 23.0F, -29.0F, 31.0F};
	report.pipeline.limit = {0xA5, 0x5A};
	report.pipeline.effectiveness.achieved = {1.0F, 2.0F, 3.0F, 4.0F, 5.0F, 6.0F};
	report.pipeline.effectiveness.residual = {-1.0F, -2.0F, -3.0F, -4.0F, -5.0F, -6.0F};
	report.pipeline.effectiveness.lower_saturation_mask = 0x12;
	report.pipeline.effectiveness.upper_saturation_mask = 0x21;
	report.pipeline.effectiveness.configured_axis_mask = 0x3F;
	report.pipeline.authority.axis_norm = {7.0, 8.0, 9.0, 10.0, 11.0, 12.0};
	report.pipeline.authority.independence = {-7.0, -8.0, -9.0, -10.0, -11.0, -12.0};
	report.pipeline.authority.selected_axis_mask = 0x1F;
	report.pipeline.authority.weak_axis_mask = 0x20;
	report.pipeline.authority.dependent_axis_mask = 0x40;
	report.pipeline.authority.rank = 5U;
	return report;
}

// Exact non-operational fixture. It is not a model of the research vehicle and
// establishes no schedule, actuator sign, authority, gain, limit or safe trim.
RuntimeConfiguration fixture(uint32_t schedule_interval_us = 250000U)
{
	RuntimeConfiguration configuration{};
	configuration.schedule_interval_us = schedule_interval_us;
	configuration.pipeline.limiter.maximum_dt_s = 0.5F;

	for (std::size_t actuator = 0; actuator < kChannelCount; ++actuator) {
		configuration.pipeline.limiter.channels[actuator] = {
			actuator == kEdfChannelIndex ? 0.0F : -1.0F,
			1.0F,
			1.0F,
			1.0F
		};
	}

	configuration.pipeline.effectiveness[0] = {1.0F, 0.0F, 0.0F, 0.0F, 0.0F};
	configuration.pipeline.effectiveness[1] = {0.0F, 1.0F, 0.0F, 0.0F, 0.0F};
	configuration.pipeline.effectiveness[2] = {0.0F, 0.0F, 1.0F, 0.0F, 0.0F};
	configuration.pipeline.effectiveness[3] = {0.0F, 0.0F, 0.0F, 1.0F, 0.0F};
	configuration.pipeline.effectiveness[4] = {0.0F, 0.0F, 0.0F, 0.0F, 1.0F};
	configuration.pipeline.effectiveness[5] = {1.0F, 1.0F, 0.0F, 0.0F, 0.0F};
	configuration.pipeline.trim = {0.25F, 0.0F, 0.0F, 0.0F, 0.0F};
	configuration.pipeline.authority.priority = {ControlAxis::Roll, ControlAxis::Pitch, ControlAxis::Yaw,
						     ControlAxis::ThrustX, ControlAxis::ThrustY, ControlAxis::ThrustZ
						    };
	configuration.pipeline.authority.minimum_axis_norm = 0.01;
	configuration.pipeline.authority.minimum_independence = 1.0e-10;
	configuration.pipeline.required_axis_mask = 0x1FU;
	return configuration;
}

RuntimeInput validInput()
{
	RuntimeInput input{};
	input.demand = {1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F};
	input.desired_command = {0.75F, 0.5F, -0.5F, 0.25F, -0.25F};
	return input;
}

void configurationAndOwnershipFixtures()
{
	static_assert(!std::is_copy_constructible<ControlRuntimeOwner>::value,
		      "runtime must have one owner");
	static_assert(!std::is_move_constructible<ControlRuntimeOwner>::value,
		      "runtime state must not move");

	ControlRuntimeOwner owner;
	check(owner.state() == RuntimeState::Unconfigured, "initial_state_unconfigured");
	check(!owner.primed() && owner.scheduleIntervalUs() == 0U && owner.lastCycleTimeUs() == 0U,
	      "initial_timing_state_clear");
	check(!owner.pipeline().configured(), "initial_pipeline_unconfigured");

	auto configuration = fixture(0U);
	RuntimeResult result = owner.configure(configuration);
	check(result.status == RuntimeStatus::InvalidSchedule, "zero_schedule_rejected");
	check(owner.state() == RuntimeState::Unconfigured && !owner.pipeline().configured(),
	      "zero_schedule_preserves_state");

	configuration = fixture(500001U);
	result = owner.configure(configuration);
	check(result.status == RuntimeStatus::InvalidSchedule, "schedule_above_pipeline_dt_rejected");
	check(owner.state() == RuntimeState::Unconfigured && owner.scheduleIntervalUs() == 0U,
	      "long_schedule_preserves_state");

	configuration = fixture();
	configuration.pipeline.limiter.maximum_dt_s = std::numeric_limits<float>::quiet_NaN();
	result = owner.configure(configuration);
	check(result.status == RuntimeStatus::PipelineFailure
	      && result.pipeline_status == PipelineStatus::InvalidConfiguration,
	      "invalid_pipeline_dt_classified");
	check(owner.state() == RuntimeState::Unconfigured, "invalid_pipeline_dt_preserves_state");

	configuration = fixture();
	configuration.pipeline.effectiveness[0][0] = std::numeric_limits<float>::infinity();
	result = owner.configure(configuration);
	check(result.status == RuntimeStatus::PipelineFailure
	      && result.pipeline_status == PipelineStatus::InvalidConfiguration,
	      "invalid_pipeline_configuration_rejected");
	check(owner.state() == RuntimeState::Unconfigured && !owner.pipeline().configured(),
	      "invalid_pipeline_configuration_preserves_state");

	result = owner.configure(fixture());
	check(result.status == RuntimeStatus::Ok && result.pipeline_status == PipelineStatus::Ok,
	      "valid_configuration_status");
	check(owner.state() == RuntimeState::Ready && owner.scheduleIntervalUs() == 250000U,
	      "valid_configuration_ready_not_running");
	check(owner.pipeline().configured() && owner.pipeline().previousCommand() == fixture().pipeline.trim,
	      "valid_configuration_owns_pipeline_at_trim");

	const Command saved_command = owner.pipeline().previousCommand();
	configuration = fixture(125000U);
	configuration.pipeline.required_axis_mask = 0x20U;
	result = owner.configure(configuration);
	check(result.status == RuntimeStatus::PipelineFailure
	      && result.pipeline_status == PipelineStatus::InsufficientAuthority,
	      "failed_ready_reconfiguration_status");
	check(owner.state() == RuntimeState::Ready && owner.scheduleIntervalUs() == 250000U
	      && owner.pipeline().previousCommand() == saved_command,
	      "failed_ready_reconfiguration_preserves_state");

	configuration = fixture(500000U);
	configuration.pipeline.trim[0] = 0.4F;
	result = owner.configure(configuration);
	check(result.status == RuntimeStatus::Ok, "exact_maximum_schedule_accepted");
	check(owner.state() == RuntimeState::Ready && owner.scheduleIntervalUs() == 500000U
	      && owner.pipeline().previousCommand() == configuration.pipeline.trim,
	      "valid_reconfiguration_replaces_ready_state");
}

void lifecycleAndFailurePreservationFixtures()
{
	ControlRuntimeOwner owner;
	RuntimeReport report = sentinelReport();
	const RuntimeReport sentinel = report;
	const RuntimeInput input = validInput();

	check(owner.prime(1000000U) == RuntimeStatus::NotConfigured,
	      "prime_before_configuration_rejected");
	RuntimeResult result = owner.runCycle(1250000U, input, report);
	check(result.status == RuntimeStatus::NotConfigured, "cycle_before_configuration_rejected");
	check(sameRuntimeReport(report, sentinel), "unconfigured_cycle_preserves_report");
	check(owner.start() == RuntimeStatus::NotConfigured, "start_before_configuration_rejected");

	check(owner.configure(fixture()).status == RuntimeStatus::Ok, "lifecycle_configuration");
	check(owner.prime(1000000U) == RuntimeStatus::NotRunning, "prime_before_start_rejected");
	result = owner.runCycle(1250000U, input, report);
	check(result.status == RuntimeStatus::NotRunning, "cycle_before_start_rejected");
	check(sameRuntimeReport(report, sentinel) && owner.lastCycleTimeUs() == 0U,
	      "not_running_cycle_preserves_state_and_report");

	check(owner.start() == RuntimeStatus::Ok, "explicit_start_status");
	check(owner.state() == RuntimeState::Running && !owner.primed(),
	      "start_running_unprimed");
	result = owner.runCycle(1250000U, input, report);
	check(result.status == RuntimeStatus::NotPrimed, "cycle_before_prime_rejected");
	check(sameRuntimeReport(report, sentinel) && owner.lastCycleTimeUs() == 0U,
	      "unprimed_cycle_preserves_state_and_report");

	check(owner.prime(1000000U) == RuntimeStatus::Primed, "prime_status");
	check(owner.primed() && owner.lastCycleTimeUs() == 1000000U,
	      "prime_captures_schedule_epoch");
	check(owner.prime(1100000U) == RuntimeStatus::AlreadyPrimed,
	      "second_prime_rejected");
	check(owner.lastCycleTimeUs() == 1000000U, "second_prime_preserves_epoch");
	check(owner.start() == RuntimeStatus::AlreadyRunning, "second_start_rejected");
	check(owner.lastCycleTimeUs() == 1000000U && owner.primed(),
	      "second_start_preserves_running_state");

	const Command initial_command = owner.pipeline().previousCommand();
	result = owner.configure(fixture(125000U));
	check(result.status == RuntimeStatus::AlreadyRunning, "running_reconfiguration_rejected");
	check(owner.scheduleIntervalUs() == 250000U && owner.lastCycleTimeUs() == 1000000U
	      && owner.pipeline().previousCommand() == initial_command,
	      "running_reconfiguration_preserves_state");

	for (RuntimeTimeUs invalid_time : {RuntimeTimeUs{1000000U}, RuntimeTimeUs{999999U}}) {
		report = sentinel;
		result = owner.runCycle(invalid_time, input, report);
		check(result.status == RuntimeStatus::InvalidTime, "nonincreasing_cycle_time_rejected");
		check(sameRuntimeReport(report, sentinel) && owner.lastCycleTimeUs() == 1000000U
		      && owner.pipeline().previousCommand() == initial_command,
		      "invalid_time_preserves_state_and_report");
	}

	RuntimeInput invalid_input = input;
	invalid_input.desired_command[2] = std::numeric_limits<float>::quiet_NaN();
	report = sentinel;
	result = owner.runCycle(1250000U, invalid_input, report);
	check(result.status == RuntimeStatus::PipelineFailure
	      && result.pipeline_status == PipelineStatus::InvalidDesiredCommand,
	      "invalid_desired_command_status");
	check(sameRuntimeReport(report, sentinel) && owner.lastCycleTimeUs() == 1000000U
	      && owner.pipeline().previousCommand() == initial_command,
	      "invalid_desired_command_preserves_state_and_report");

	invalid_input = input;
	invalid_input.demand[4] = std::numeric_limits<float>::infinity();
	report = sentinel;
	result = owner.runCycle(1250000U, invalid_input, report);
	check(result.status == RuntimeStatus::PipelineFailure
	      && result.pipeline_status == PipelineStatus::InvalidDemand,
	      "invalid_demand_status");
	check(sameRuntimeReport(report, sentinel) && owner.lastCycleTimeUs() == 1000000U
	      && owner.pipeline().previousCommand() == initial_command,
	      "invalid_demand_preserves_state_and_report");

	report = sentinel;
	result = owner.runCycle(1500001U, input, report);
	check(result.status == RuntimeStatus::PipelineFailure
	      && result.pipeline_status == PipelineStatus::InvalidTimestep,
	      "late_cycle_status");
	check(sameRuntimeReport(report, sentinel) && owner.lastCycleTimeUs() == 1000000U
	      && owner.pipeline().previousCommand() == initial_command,
	      "late_cycle_preserves_state_and_report");

	report = sentinel;
	result = owner.runCycle(1250000U, input, report);
	check(result.status == RuntimeStatus::Ok && result.pipeline_status == PipelineStatus::Ok,
	      "successful_cycle_status");
	check(report.timestamp_us == 1250000U && report.delta_us == 250000U,
	      "successful_cycle_timing_report");
	check(report.pipeline.candidate_command == Command{0.5F, 0.25F, -0.25F, 0.25F, -0.25F},
	      "successful_cycle_limited_candidate");
	check(report.pipeline.limit.amplitude_limited_mask == 0U
	      && report.pipeline.limit.slew_limited_mask == 0x07U,
	      "successful_cycle_limit_diagnostics");
	check(near(report.pipeline.effectiveness.achieved[0], 0.25F)
	      && near(report.pipeline.effectiveness.achieved[1], 0.25F)
	      && near(report.pipeline.effectiveness.achieved[2], -0.25F)
	      && near(report.pipeline.effectiveness.achieved[5], 0.5F),
	      "successful_cycle_effectiveness_diagnostics");
	check(owner.lastCycleTimeUs() == 1250000U
	      && owner.pipeline().previousCommand() == report.pipeline.candidate_command,
	      "successful_cycle_advances_owner_state_once");

	result = owner.runCycle(1500000U, input, report);
	check(result.status == RuntimeStatus::Ok, "second_successful_cycle_status");
	check(report.timestamp_us == 1500000U && report.delta_us == 250000U,
	      "second_successful_cycle_timing_report");
	check(report.pipeline.candidate_command == input.desired_command
	      && report.pipeline.limit.slew_limited_mask == 0U,
	      "second_successful_cycle_uses_owned_previous_command");

	owner.stop();
	check(owner.state() == RuntimeState::Unconfigured && !owner.primed(),
	      "stop_revokes_runtime_configuration");
	check(owner.scheduleIntervalUs() == 0U && owner.lastCycleTimeUs() == 0U,
	      "stop_clears_scheduling_state");
	check(owner.start() == RuntimeStatus::NotConfigured, "restart_requires_configuration");
	check(owner.prime(2000000U) == RuntimeStatus::NotConfigured,
	      "prime_after_stop_requires_configuration");
	report = sentinel;
	result = owner.runCycle(2250000U, input, report);
	check(result.status == RuntimeStatus::NotConfigured && sameRuntimeReport(report, sentinel),
	      "cycle_after_stop_rejected_without_report_change");

	auto replacement = fixture(125000U);
	replacement.pipeline.trim[0] = 0.4F;
	check(owner.configure(replacement).status == RuntimeStatus::Ok,
	      "configuration_after_stop_status");
	check(owner.state() == RuntimeState::Ready
	      && owner.pipeline().previousCommand() == replacement.pipeline.trim,
	      "configuration_after_stop_resets_pipeline_state");
}

void timestampBoundaryFixtures()
{
	ControlRuntimeOwner owner;
	RuntimeReport report = sentinelReport();
	const RuntimeReport sentinel = report;
	const RuntimeInput input = validInput();
	const RuntimeTimeUs maximum_time = std::numeric_limits<RuntimeTimeUs>::max();

	check(owner.configure(fixture()).status == RuntimeStatus::Ok,
	      "timestamp_boundary_configuration");
	check(owner.start() == RuntimeStatus::Ok, "timestamp_boundary_start");
	check(owner.prime(maximum_time - 250000U) == RuntimeStatus::Primed,
	      "timestamp_boundary_prime");
	RuntimeResult result = owner.runCycle(maximum_time, input, report);
	check(result.status == RuntimeStatus::Ok, "timestamp_boundary_success");
	check(report.timestamp_us == maximum_time && report.delta_us == 250000U,
	      "timestamp_boundary_delta_without_overflow");

	const Command saved_command = owner.pipeline().previousCommand();
	report = sentinel;
	result = owner.runCycle(0U, input, report);
	check(result.status == RuntimeStatus::InvalidTime, "wrapped_timestamp_rejected");
	check(sameRuntimeReport(report, sentinel) && owner.lastCycleTimeUs() == maximum_time
	      && owner.pipeline().previousCommand() == saved_command,
	      "wrapped_timestamp_preserves_state_and_report");

	owner.stop();
	auto wide_schedule = fixture(std::numeric_limits<uint32_t>::max());
	wide_schedule.pipeline.limiter.maximum_dt_s = 5000.0F;
	result = owner.configure(wide_schedule);
	check(result.status == RuntimeStatus::Ok, "explicit_uint32_schedule_supported");
	check(owner.scheduleIntervalUs() == std::numeric_limits<uint32_t>::max(),
	      "explicit_uint32_schedule_preserved");
}

} // namespace

int main()
{
	configurationAndOwnershipFixtures();
	lifecycleAndFailurePreservationFixtures();
	timestampBoundaryFixtures();
	std::cout << checks << " non-operational runtime-owner checks, " << failures << " failures\n";
	return failures == 0 ? 0 : 1;
}
