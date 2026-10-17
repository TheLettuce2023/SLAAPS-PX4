#include "ManualControlInput.hpp"

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

bool sameInput(const RuntimeInput &left, const RuntimeInput &right)
{
	return left.demand == right.demand && left.desired_command == right.desired_command;
}

bool sameReport(const ManualControlReport &left, const ManualControlReport &right)
{
	return left.timestamp_us == right.timestamp_us
	       && left.timestamp_sample_us == right.timestamp_sample_us
	       && left.sample_age_us == right.sample_age_us
	       && left.source == right.source
	       && left.mavlink_instance == right.mavlink_instance
	       && left.source_system_id == right.source_system_id
	       && left.source_component_id == right.source_component_id
	       && left.buttons == right.buttons
	       && left.new_sample == right.new_sample
	       && left.sticks_moving == right.sticks_moving;
}

RuntimeInput sentinelInput()
{
	RuntimeInput input{};
	input.demand = {11.0F, 13.0F, 17.0F, 19.0F, 23.0F, 29.0F};
	input.desired_command = {31.0F, 37.0F, 41.0F, 43.0F, 47.0F};
	return input;
}

ManualControlReport sentinelReport()
{
	ManualControlReport report{};
	report.timestamp_us = 53U;
	report.timestamp_sample_us = 59U;
	report.sample_age_us = 61U;
	report.source = ManualControlSource::Rc;
	report.mavlink_instance = 67U;
	report.source_system_id = 71U;
	report.source_component_id = 73U;
	report.buttons = 79U;
	report.new_sample = true;
	report.sticks_moving = true;
	return report;
}

// Exact non-operational fixture. These coefficients and identities are test
// data only; they are not a vehicle mix, gain, trim, source ID or timing budget.
ManualControlConfiguration fixture(ManualSourcePolicy policy = ManualSourcePolicy::MavlinkOnly)
{
	ManualControlConfiguration configuration{};
	configuration.source_policy = policy;
	configuration.maximum_sample_age_us = 1000U;

	if (policy == ManualSourcePolicy::RcOnly) {
		configuration.mavlink_instance = kNoMavlinkManualInstance;

	} else {
		configuration.mavlink_instance = 2U;
		configuration.mavlink_system_id = 42U;
		configuration.mavlink_component_id = 77U;
	}

	configuration.demand_offset = {0.1F, -0.1F, 0.2F, -0.2F, 0.3F, -0.3F};
	configuration.demand_map[static_cast<std::size_t>(ControlAxis::Roll)]
	[static_cast<std::size_t>(ManualAxis::Roll)] = 0.5F;
	configuration.demand_map[static_cast<std::size_t>(ControlAxis::Pitch)]
	[static_cast<std::size_t>(ManualAxis::Pitch)] = -0.25F;
	configuration.demand_map[static_cast<std::size_t>(ControlAxis::Yaw)]
	[static_cast<std::size_t>(ManualAxis::Yaw)] = 0.75F;
	configuration.demand_map[static_cast<std::size_t>(ControlAxis::ThrustX)]
	[static_cast<std::size_t>(ManualAxis::Throttle)] = 0.5F;
	configuration.command_offset = {0.2F, 0.1F, -0.1F, 0.0F, 0.0F};
	configuration.command_map[kEdfChannelIndex][static_cast<std::size_t>(ManualAxis::Throttle)] = 0.4F;
	configuration.command_map[1][static_cast<std::size_t>(ManualAxis::Roll)] = 0.25F;
	configuration.command_map[2][static_cast<std::size_t>(ManualAxis::Pitch)] = 0.5F;
	configuration.command_map[3][static_cast<std::size_t>(ManualAxis::Yaw)] = -0.5F;
	return configuration;
}

ManualControlSample mavlinkSample(RuntimeTimeUs sample_time_us = 10000U)
{
	ManualControlSample sample{};
	sample.timestamp_sample_us = sample_time_us;
	sample.timestamp_us = sample_time_us + 50U;
	sample.source = ManualControlSource::Mavlink;
	sample.mavlink_instance = 2U;
	sample.source_system_id = 42U;
	sample.source_component_id = 77U;
	sample.axes = {0.2F, -0.4F, 0.6F, 0.5F};
	sample.buttons = 0x1234U;
	sample.valid = true;
	sample.sticks_moving = true;
	return sample;
}

ManualControlSample rcSample(RuntimeTimeUs sample_time_us = 10000U)
{
	ManualControlSample sample = mavlinkSample(sample_time_us);
	sample.source = ManualControlSource::Rc;
	sample.mavlink_instance = kNoMavlinkManualInstance;
	sample.source_system_id = 0U;
	sample.source_component_id = 0U;
	return sample;
}

void expectRejected(ManualControlInput &boundary,
		    ManualControlStatus expected,
		    RuntimeTimeUs now_us,
		    const ManualControlSample &sample,
		    RuntimeTimeUs expected_last_time,
		    const std::string &name)
{
	RuntimeInput input = sentinelInput();
	const RuntimeInput saved_input = input;
	ManualControlReport report = sentinelReport();
	const ManualControlReport saved_report = report;
	const ManualControlStatus status = boundary.evaluate(now_us, sample, input, report);
	check(status == expected, name + "_status");
	check(sameInput(input, saved_input) && sameReport(report, saved_report), name + "_preserves_outputs");
	check(boundary.lastAcceptedSampleTimeUs() == expected_last_time, name + "_preserves_state");
}

void configurationFixtures()
{
	static_assert(!std::is_copy_constructible<ManualControlInput>::value,
		      "manual boundary must have one state owner");
	static_assert(!std::is_move_constructible<ManualControlInput>::value,
		      "manual boundary state must not move");

	ManualControlInput boundary;
	check(!boundary.configured() && boundary.lastAcceptedSampleTimeUs() == 0U,
	      "initial_state_unconfigured");
	expectRejected(boundary, ManualControlStatus::NotConfigured, 10100U, mavlinkSample(), 0U,
		       "evaluate_before_configuration");

	ManualControlConfiguration configuration{};
	check(boundary.configure(configuration) == ManualControlStatus::InvalidConfiguration,
	      "zero_configuration_rejected");
	check(!boundary.configured(), "zero_configuration_preserves_state");

	configuration = fixture();
	configuration.source_policy = static_cast<ManualSourcePolicy>(99U);
	check(boundary.configure(configuration) == ManualControlStatus::InvalidConfiguration,
	      "unknown_policy_rejected");

	configuration = fixture();
	configuration.maximum_sample_age_us = 0U;
	check(boundary.configure(configuration) == ManualControlStatus::InvalidConfiguration,
	      "zero_age_rejected");

	configuration = fixture();
	configuration.demand_map = {};
	configuration.command_map = {};
	check(boundary.configure(configuration) == ManualControlStatus::InvalidConfiguration,
	      "unmapped_axes_rejected");

	configuration = fixture();
	configuration.mavlink_instance = kMaximumMavlinkManualInstance + 1U;
	check(boundary.configure(configuration) == ManualControlStatus::InvalidConfiguration,
	      "invalid_mavlink_instance_rejected");

	configuration = fixture();
	configuration.mavlink_system_id = 0U;
	check(boundary.configure(configuration) == ManualControlStatus::InvalidConfiguration,
	      "zero_mavlink_system_rejected");

	configuration = fixture();
	configuration.mavlink_component_id = 0U;
	check(boundary.configure(configuration) == ManualControlStatus::InvalidConfiguration,
	      "zero_mavlink_component_rejected");

	configuration = fixture(ManualSourcePolicy::RcOnly);
	configuration.mavlink_system_id = 42U;
	check(boundary.configure(configuration) == ManualControlStatus::InvalidConfiguration,
	      "unused_rc_identity_rejected");

	configuration = fixture();
	configuration.demand_map[0][0] = std::numeric_limits<float>::quiet_NaN();
	check(boundary.configure(configuration) == ManualControlStatus::InvalidConfiguration,
	      "nonfinite_map_rejected");

	configuration = fixture();
	configuration.command_offset[0] = std::numeric_limits<float>::infinity();
	check(boundary.configure(configuration) == ManualControlStatus::InvalidConfiguration,
	      "nonfinite_offset_rejected");

	check(boundary.configure(fixture()) == ManualControlStatus::Ok,
	      "valid_configuration_accepted");
	check(boundary.configured() && boundary.configuration().maximum_sample_age_us == 1000U,
	      "valid_configuration_stored");
}

void mappingAndHoldFixtures()
{
	ManualControlInput boundary;
	check(boundary.configure(fixture()) == ManualControlStatus::Ok,
	      "mapping_configuration");

	RuntimeInput input = sentinelInput();
	ManualControlReport report = sentinelReport();
	ManualControlSample sample = mavlinkSample();
	check(boundary.evaluate(10100U, sample, input, report) == ManualControlStatus::Ok,
	      "new_sample_accepted");
	check(near(input.demand[0], 0.2F) && near(input.demand[1], 0.0F)
	      && near(input.demand[2], 0.65F) && near(input.demand[3], 0.05F)
	      && near(input.demand[4], 0.3F) && near(input.demand[5], -0.3F),
	      "demand_affine_map");
	check(near(input.desired_command[0], 0.4F) && near(input.desired_command[1], 0.15F)
	      && near(input.desired_command[2], -0.3F) && near(input.desired_command[3], -0.3F)
	      && near(input.desired_command[4], 0.0F),
	      "command_affine_map");
	check(report.timestamp_us == 10050U && report.timestamp_sample_us == 10000U
	      && report.sample_age_us == 100U && report.new_sample,
	      "new_sample_timing_report");
	check(report.source == ManualControlSource::Mavlink && report.mavlink_instance == 2U
	      && report.source_system_id == 42U && report.source_component_id == 77U,
	      "new_sample_source_report");
	check(report.buttons == 0x1234U && report.sticks_moving,
	      "new_sample_manual_metadata");
	check(boundary.lastAcceptedSampleTimeUs() == 10000U,
	      "new_sample_advances_state");

	sample.timestamp_us = 10150U;
	sample.sticks_moving = false;
	input = sentinelInput();
	report = sentinelReport();
	check(boundary.evaluate(10200U, sample, input, report) == ManualControlStatus::Ok,
	      "held_sample_accepted");
	check(!report.new_sample && !report.sticks_moving && report.sample_age_us == 200U,
	      "held_sample_report");
	check(near(input.desired_command[0], 0.4F)
	      && boundary.lastAcceptedSampleTimeUs() == 10000U,
	      "held_sample_reuses_mapping_without_advancing_raw_time");

	ManualControlSample conflicting = sample;
	conflicting.axes[0] += 0.1F;
	expectRejected(boundary, ManualControlStatus::ConflictingSample, 10200U, conflicting, 10000U,
		       "changed_payload_same_sample_time");

	ManualControlSample regressed = sample;
	regressed.timestamp_sample_us = 9999U;
	expectRejected(boundary, ManualControlStatus::RegressedSample, 10200U, regressed, 10000U,
		       "regressed_raw_sample");

	regressed = sample;
	regressed.timestamp_us = 10149U;
	expectRejected(boundary, ManualControlStatus::RegressedSample, 10200U, regressed, 10000U,
		       "regressed_held_publication");

	sample = mavlinkSample(10200U);
	check(boundary.evaluate(10300U, sample, input, report) == ManualControlStatus::Ok,
	      "next_sample_accepted");
	check(report.new_sample && boundary.lastAcceptedSampleTimeUs() == 10200U,
	      "next_sample_advances_state");

	ManualControlConfiguration invalid_replacement = fixture();
	invalid_replacement.maximum_sample_age_us = 0U;
	check(boundary.configure(invalid_replacement) == ManualControlStatus::InvalidConfiguration,
	      "invalid_reconfiguration_rejected");
	check(boundary.configured() && boundary.lastAcceptedSampleTimeUs() == 10200U
	      && boundary.configuration().maximum_sample_age_us == 1000U,
	      "invalid_reconfiguration_preserves_state");
	sample.timestamp_us += 10U;
	check(boundary.evaluate(10310U, sample, input, report) == ManualControlStatus::Ok && !report.new_sample,
	      "preserved_configuration_remains_usable");

	auto replacement = fixture();
	replacement.maximum_sample_age_us = 2000U;
	check(boundary.configure(replacement) == ManualControlStatus::Ok,
	      "valid_reconfiguration_accepted");
	check(boundary.lastAcceptedSampleTimeUs() == 0U
	      && boundary.configuration().maximum_sample_age_us == 2000U,
	      "valid_reconfiguration_resets_sequence_state");
}

void rejectionFixtures()
{
	ManualControlInput boundary;
	check(boundary.configure(fixture()) == ManualControlStatus::Ok,
	      "rejection_configuration");
	ManualControlSample sample = mavlinkSample();
	RuntimeInput input{};
	ManualControlReport report{};
	check(boundary.evaluate(10100U, sample, input, report) == ManualControlStatus::Ok,
	      "rejection_baseline_sample");
	constexpr RuntimeTimeUs saved_time{10000U};

	ManualControlSample candidate = mavlinkSample(11000U);
	candidate.valid = false;
	expectRejected(boundary, ManualControlStatus::InvalidSample, 11100U, candidate, saved_time,
		       "invalid_flag");

	candidate = mavlinkSample(11000U);
	candidate.timestamp_sample_us = 0U;
	expectRejected(boundary, ManualControlStatus::InvalidTime, 11100U, candidate, saved_time,
		       "zero_sample_time");

	candidate = mavlinkSample(11000U);
	candidate.timestamp_us = 10999U;
	expectRejected(boundary, ManualControlStatus::InvalidTime, 11100U, candidate, saved_time,
		       "publication_before_sample");

	candidate = mavlinkSample(11000U);
	expectRejected(boundary, ManualControlStatus::InvalidTime, 11049U, candidate, saved_time,
		       "publication_in_future");

	candidate = mavlinkSample(11000U);
	expectRejected(boundary, ManualControlStatus::StaleSample, 12001U, candidate, saved_time,
		       "sample_over_age_limit");

	candidate = mavlinkSample(11000U);
	candidate.source = ManualControlSource::Unknown;
	expectRejected(boundary, ManualControlStatus::InvalidSource, 11100U, candidate, saved_time,
		       "unknown_source");

	candidate = rcSample(11000U);
	expectRejected(boundary, ManualControlStatus::SourceRejected, 11100U, candidate, saved_time,
		       "rc_rejected_by_mavlink_policy");

	for (uint8_t field = 0U; field < 3U; ++field) {
		candidate = mavlinkSample(11000U);

		if (field == 0U) {
			candidate.mavlink_instance = 3U;

		} else if (field == 1U) {
			candidate.source_system_id = 43U;

		} else {
			candidate.source_component_id = 78U;
		}

		expectRejected(boundary, ManualControlStatus::IdentityRejected, 11100U, candidate, saved_time,
			       "wrong_mavlink_identity_" + std::to_string(field));
	}

	for (std::size_t axis = 0; axis < kManualAxisCount; ++axis) {
		candidate = mavlinkSample(11000U);
		candidate.axes[axis] = std::numeric_limits<float>::quiet_NaN();
		expectRejected(boundary, ManualControlStatus::NonFinite, 11100U, candidate, saved_time,
			       "nonfinite_axis_" + std::to_string(axis));

		candidate = mavlinkSample(11000U);
		candidate.axes[axis] = 1.0001F;
		expectRejected(boundary, ManualControlStatus::OutOfRange, 11100U, candidate, saved_time,
			       "high_axis_" + std::to_string(axis));

		candidate = mavlinkSample(11000U);
		candidate.axes[axis] = -1.0001F;
		expectRejected(boundary, ManualControlStatus::OutOfRange, 11100U, candidate, saved_time,
			       "low_axis_" + std::to_string(axis));
	}

	candidate = mavlinkSample(11000U);
	candidate.axes = {-1.0F, 1.0F, -1.0F, 1.0F};
	check(boundary.evaluate(12000U, candidate, input, report) == ManualControlStatus::Ok,
	      "exact_axis_and_age_bounds_accepted");
	check(boundary.lastAcceptedSampleTimeUs() == 11000U,
	      "exact_bounds_advance_state");
}

void sourcePolicyAndArithmeticFixtures()
{
	ManualControlInput rc_boundary;
	check(rc_boundary.configure(fixture(ManualSourcePolicy::RcOnly)) == ManualControlStatus::Ok,
	      "rc_only_configuration");
	RuntimeInput input{};
	ManualControlReport report{};
	check(rc_boundary.evaluate(10100U, rcSample(), input, report) == ManualControlStatus::Ok,
	      "rc_source_accepted");
	check(report.source == ManualControlSource::Rc
	      && report.mavlink_instance == kNoMavlinkManualInstance,
	      "rc_source_report");

	ManualControlSample bad_rc = rcSample(11000U);
	bad_rc.source_system_id = 1U;
	expectRejected(rc_boundary, ManualControlStatus::IdentityRejected, 11100U, bad_rc, 10000U,
		       "rc_nonzero_identity");

	ManualControlInput combined_boundary;
	check(combined_boundary.configure(fixture(ManualSourcePolicy::RcOrMavlink)) == ManualControlStatus::Ok,
	      "combined_configuration");
	check(combined_boundary.evaluate(10100U, rcSample(), input, report) == ManualControlStatus::Ok,
	      "combined_accepts_rc");
	check(combined_boundary.evaluate(11100U, mavlinkSample(11000U), input, report) == ManualControlStatus::Ok,
	      "combined_accepts_configured_gcs");
	check(report.source == ManualControlSource::Mavlink && report.new_sample,
	      "combined_source_transition_report");

	ManualControlInput arithmetic_boundary;
	auto large = fixture();
	large.demand_offset[0] = std::numeric_limits<float>::max();
	large.demand_map[0][0] = std::numeric_limits<float>::max();
	check(arithmetic_boundary.configure(large) == ManualControlStatus::Ok,
	      "finite_large_mapping_configuration");
	ManualControlSample large_sample = mavlinkSample();
	large_sample.axes[0] = 1.0F;
	expectRejected(arithmetic_boundary, ManualControlStatus::ArithmeticFailure, 10100U,
		       large_sample, 0U, "mapping_overflow");

	combined_boundary.reset();
	check(!combined_boundary.configured() && combined_boundary.lastAcceptedSampleTimeUs() == 0U,
	      "reset_revokes_configuration");
	expectRejected(combined_boundary, ManualControlStatus::NotConfigured, 12100U,
		       mavlinkSample(12000U), 0U, "evaluate_after_reset");
}

} // namespace

int main()
{
	configurationFixtures();
	mappingAndHoldFixtures();
	rejectionFixtures();
	sourcePolicyAndArithmeticFixtures();
	std::cout << checks << " non-operational manual-control checks, " << failures << " failures\n";
	return failures == 0 ? 0 : 1;
}
