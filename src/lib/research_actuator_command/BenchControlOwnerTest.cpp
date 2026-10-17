#include "BenchControlOwner.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <type_traits>

using namespace research_actuator_command;

namespace
{
int checks = 0;
int failures = 0;

void check(bool condition, const char *name)
{
	++checks;

	if (!condition) {
		++failures;
		std::cerr << "FAILED: " << name << '\n';
	}
}

bool near(float a, float b)
{
	return std::fabs(a - b) < 0.0001F;
}

// Synthetic software fixture only: these are NOT installed signs, calibration,
// joystick tolerance, scheduling qualification or approved servo slew rates.
BenchConfiguration fixture()
{
	BenchConfiguration c{};
	c.mavlink_instance = 2U;
	c.source_system_id = 42U;
	c.source_component_id = 77U;
	c.neutral_entry_tolerance = 0.05F;
	c.maximum_cycle_gap_us = 100000U;
	c.mix = {{{1.0F, 1.0F, 1.0F}, {1.0F, -1.0F, -1.0F}, {-1.0F, 1.0F, -1.0F}, {-1.0F, -1.0F, 1.0F}}};

	for (auto &servo : c.servos) {
		servo = {1400.0F, 1500.0F, 1600.0F, 10.0F, 1000.0F, 1000.0F};
	}

	c.servos[1] = {1600.0F, 1500.0F, 1400.0F, 10.0F, 1000.0F, 1000.0F};
	return c;
}

ManualControlSample sample(RuntimeTimeUs time)
{
	ManualControlSample s{};
	s.timestamp_us = time;
	s.timestamp_sample_us = time;
	s.source = ManualControlSource::Mavlink;
	s.mavlink_instance = 2U;
	s.source_system_id = 42U;
	s.source_component_id = 77U;
	s.valid = true;
	s.axes[3] = std::numeric_limits<float>::quiet_NaN();
	return s;
}

void configuration()
{
	check(!std::is_copy_constructible<BenchControlOwner>::value, "single_owner_copy");
	check(!std::is_move_constructible<BenchControlOwner>::value, "single_owner_move");
	check(!BenchControlOwner::outputsEnabled(), "no_outputs");
	BenchControlOwner owner;
	BenchReport report{};
	check(!owner.neutralCandidate(report), "no_uncalibrated_neutral");
	check(owner.configure({}) == BenchStatus::InvalidConfiguration, "zero_config");

	for (int fault = 0; fault < 19; ++fault) {
		auto c = fixture();

		switch (fault) {
		case 0: c.source_system_id = 0U; break;

		case 1: c.source_component_id = 0U; break;

		case 2: c.mavlink_instance = 6U; break;

		case 3: c.maximum_cycle_gap_us = kBenchIntervalUs - 1U; break;

		case 4: c.maximum_cycle_gap_us = kBenchSampleAgeUs + 1U; break;

		case 5: c.neutral_entry_tolerance = -0.1F; break;

		case 6: c.neutral_entry_tolerance = 1.0F; break;

		case 7: c.neutral_entry_tolerance = NAN; break;

		case 8: c.servos[0].negative_pwm_us = 499.0F; break;

		case 9: c.servos[0].positive_pwm_us = 2501.0F; break;

		case 10: c.servos[0].neutral_pwm_us = 1700.0F; break;

		case 11: c.servos[0].maximum_deflection_deg = 13.01F; break;

		case 12: c.servos[0].maximum_deflection_deg = 0.0F; break;

		case 13: c.servos[0].rise_deg_per_s = 0.0F; break;

		case 14: c.servos[0].fall_deg_per_s = INFINITY; break;

		case 15: c.mix[0][0] = NAN; break;

		case 16: c.mix[0] = {}; break;
		case 17:
			for (auto &row : c.mix) { row[2] = 0.0F; }

			break;

		case 18: c.servos[0].neutral_pwm_us = NAN; break;
		}

		check(owner.configure(c) == BenchStatus::InvalidConfiguration, "bad_configuration");
		check(owner.state() == BenchState::Disabled, "configuration_failure_preserved");
	}

	check(owner.configure(fixture()) == BenchStatus::Ok, "configured");
	check(owner.state() == BenchState::Ready && owner.neutralRequested(), "ready_neutral");
	check(owner.configure(fixture()) == BenchStatus::InvalidState, "no_live_reconfigure");
	check(owner.neutralCandidate(report) && report.pwm_us[0] == 1500.0F, "calibrated_neutral");
	auto s = sample(1000000U);
	s.axes[0] = 0.1F;
	check(owner.enable(1000000U, s) == BenchStatus::NotCentered, "center_entry");
	s.axes[0] = 0.0F;
	s.source_system_id = 43U;
	check(owner.enable(1000000U, s) == BenchStatus::InputRejected, "entry_identity");
	check(owner.state() == BenchState::Ready, "failed_entry_no_authority");
	check(owner.enable(1000000U, sample(1000000U)) == BenchStatus::Ok, "explicit_enable");
	check(owner.enable(1000001U, sample(1000001U)) == BenchStatus::InvalidState, "duplicate_enable");
}

void mapping()
{
	BenchControlOwner owner;
	check(owner.configure(fixture()) == BenchStatus::Ok, "map_config");
	check(owner.enable(1000000U, sample(1000000U)) == BenchStatus::Ok, "map_enable");
	auto s = sample(1020000U);
	s.axes = {1.0F, 1.0F, 1.0F, NAN};
	BenchReport report{};
	check(owner.cycle(1020000U, s, report) == BenchStatus::Ok, "combined_mix");
	check(near(report.mix_scale, 1.0F / 3.0F), "uniform_scale");
	check(near(report.normalized[1], 1.0F), "positive_endpoint");
	check(near(report.normalized[2], -1.0F / 3.0F), "preserved_ratio");
	check(near(report.pwm_us[0], 1600.0F) && near(report.pwm_us[1], 4600.0F / 3.0F), "reversal_once");
	check(report.normalized[0] == 0.0F && !report.neutral_requested, "edf_inhibited");
	s.axes[3] = INFINITY;
	check(owner.cycle(1040000U, s, report) == BenchStatus::Ok, "ignored_throttle_held");
	check(report.normalized[0] == 0.0F, "edf_ignored_throttle");
	owner.disable();
	check(owner.neutralRequested() && owner.state() == BenchState::Ready, "operator_disable");
	check(owner.neutralCandidate(report) && report.normalized == Command{}, "disable_neutral_intent");
	check(owner.cycle(1060000U, s, report) == BenchStatus::InvalidState, "disabled_cycle");
	check(owner.enable(1060000U, s) == BenchStatus::NotCentered, "reentry_requires_center");
	owner.reset();
	check(owner.state() == BenchState::Disabled && !owner.neutralCandidate(report), "reset_requires_configuration");

	auto c = fixture();

	for (auto &servo : c.servos) { servo.rise_deg_per_s = 10.0F; servo.fall_deg_per_s = 5.0F; }

	check(owner.configure(c) == BenchStatus::Ok, "slew_config");
	check(owner.enable(2000000U, sample(2000000U)) == BenchStatus::Ok, "slew_enable");
	s = sample(2020000U);
	s.axes[0] = 1.0F;
	check(owner.cycle(2020000U, s, report) == BenchStatus::Ok, "slew_cycle");
	check(near(report.deflection_deg[0], 0.2F) && near(report.deflection_deg[2], -0.1F), "asymmetric_slew");
}

void failuresLatch()
{
	for (int failure = 0; failure < 12; ++failure) {
		BenchControlOwner owner;
		check(owner.configure(fixture()) == BenchStatus::Ok, "fault_config");
		check(owner.enable(1000000U, sample(1000000U)) == BenchStatus::Ok, "fault_enable");
		auto s = sample(1020000U);
		RuntimeTimeUs now = 1020000U;

		switch (failure) {
		case 0: s.valid = false; break;

		case 1: s.source_system_id++; break;

		case 2: s.source_component_id++; break;

		case 3: s.mavlink_instance++; break;

		case 4: s.source = ManualControlSource::Rc; break;

		case 5: s.axes[0] = NAN; break;

		case 6: s.axes[1] = 1.01F; break;

		case 7: s.timestamp_sample_us = 1U; break;

		case 8: s.timestamp_us = now + 1U; break;

		case 9: now = 999999U; break;

		case 10: now = 1100001U; break;

		case 11: s = sample(1000000U); s.axes[0] = 0.1F; break;
		}

		BenchReport report{};
		report.timestamp_us = 99U;
		check(owner.cycle(now, s, report) != BenchStatus::Ok, "fault_rejected");
		check(owner.state() == BenchState::Fault && owner.neutralRequested(), "fault_latched");
		check(report.timestamp_us == 99U && owner.lastAcceptedReport().timestamp_us == 1000000U, "failure_preserves_calculation");
		const auto fault = owner.fault();
		owner.disable();
		check(owner.state() == BenchState::Fault && owner.fault() == fault, "disable_cannot_clear_fault");
		check(owner.enable(1200000U, sample(1200000U)) == BenchStatus::InvalidState, "reconnect_no_auto_reenable");
		check(owner.neutralCandidate(report) && report.pwm_us[0] == 1500.0F, "fault_neutral_request");
		owner.reset();
		check(owner.configure(fixture()) == BenchStatus::Ok, "explicit_reset_configure");
	}
}

void schedulingAndMasks()
{
	BenchControlOwner owner;
	check(owner.configure(fixture()) == BenchStatus::Ok, "time_config");
	auto s = sample(1000000U);
	check(owner.enable(1000000U, s) == BenchStatus::Ok, "time_enable");
	BenchReport report{};
	report.timestamp_us = 99U;
	check(owner.cycle(1000001U, s, report) == BenchStatus::NotDue && report.timestamp_us == 99U, "early_no_output");

	for (RuntimeTimeUs now = 1020000U; now <= 1500000U; now += kBenchIntervalUs) {
		check(owner.cycle(now, s, report) == BenchStatus::Ok, "held_until_inclusive_age");
	}

	check(owner.cycle(1500001U, s, report) == BenchStatus::InputRejected, "stale_detected_before_due");
	check(owner.inputStatus() == ManualControlStatus::StaleSample, "stale_reason");

	ManualControlInput boundary;
	ManualControlConfiguration c{};
	c.source_policy = ManualSourcePolicy::MavlinkOnly;
	c.maximum_sample_age_us = kBenchSampleAgeUs;
	c.mavlink_instance = 2U;
	c.mavlink_system_id = 42U;
	c.mavlink_component_id = 77U;
	c.required_axis_mask = kAttitudeManualAxesMask;
	c.command_map[1][0] = 1.0F;
	c.command_map[2][1] = 1.0F;
	c.command_map[3][2] = 1.0F;
	check(boundary.configure(c) == ManualControlStatus::Ok, "three_axis_explicit");
	c.command_map[0][3] = 1.0F;
	check(boundary.configure(c) == ManualControlStatus::InvalidConfiguration, "disabled_axis_cannot_map_edf");
	c.command_map[0][3] = 0.0F;
	c.required_axis_mask = 0U;
	check(boundary.configure(c) == ManualControlStatus::InvalidConfiguration, "empty_mask_rejected");
	c.required_axis_mask = 0x87U;
	check(boundary.configure(c) == ManualControlStatus::InvalidConfiguration, "unknown_mask_rejected");
}

void axisSweep()
{
	for (float roll : {-1.0F, 0.0F, 1.0F}) {
		for (float pitch : {-1.0F, 0.0F, 1.0F}) {
			for (float yaw : {-1.0F, 0.0F, 1.0F}) {
				BenchControlOwner owner;
				check(owner.configure(fixture()) == BenchStatus::Ok, "sweep_config");
				check(owner.enable(1000000U, sample(1000000U)) == BenchStatus::Ok, "sweep_enable");
				auto s = sample(1020000U);
				s.axes = {roll, pitch, yaw, NAN};
				BenchReport report{};
				check(owner.cycle(1020000U, s, report) == BenchStatus::Ok, "sweep_cycle");
				const float raw[4] = {roll + pitch + yaw, roll - pitch - yaw, -roll + pitch - yaw, -roll - pitch + yaw};
				float largest = 1.0F;

				for (float value : raw) { largest = std::fmax(largest, std::fabs(value)); }

				for (std::size_t i = 0; i < kBenchCanardCount; ++i) {
					check(near(report.normalized[i + 1U], raw[i] / largest), "sweep_ratio");
					check(report.pwm_us[i] >= 1400.0F && report.pwm_us[i] <= 1600.0F, "sweep_pwm_bounds");
					check(std::fabs(report.deflection_deg[i]) <= 10.0F, "sweep_angle_bounds");
				}

				check(report.normalized[0] == 0.0F && !owner.outputsEnabled(), "sweep_no_edf_or_output");
			}
		}
	}

	BenchControlOwner owner;
	auto c = fixture();
	c.mix[0] = {std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), 0.0F};
	check(owner.configure(c) == BenchStatus::Ok, "finite_extreme_mix");
	check(owner.enable(1000000U, sample(1000000U)) == BenchStatus::Ok, "extreme_enable");
	auto s = sample(1020000U);
	s.axes = {1.0F, 1.0F, 0.0F, NAN};
	BenchReport report{};
	check(owner.cycle(1020000U, s, report) == BenchStatus::InputRejected, "mix_overflow_rejected");
	check(owner.inputStatus() == ManualControlStatus::ArithmeticFailure && owner.neutralRequested(), "overflow_latched_neutral");
}
} // namespace

int main()
{
	configuration();
	mapping();
	failuresLatch();
	schedulingAndMasks();
	axisSweep();
	std::cout << checks << " non-operational bench-control checks, " << failures << " failures\n";
	return failures == 0 ? 0 : 1;
}
