#include "AuthorityAnalysis.hpp"

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

bool near(double actual, double expected, double tolerance = 1.0e-12)
{
	return std::fabs(actual - expected) <= tolerance;
}

bool sameReport(const AuthorityReport &left, const AuthorityReport &right)
{
	return left.axis_norm == right.axis_norm
	       && left.independence == right.independence
	       && left.selected_axis_mask == right.selected_axis_mask
	       && left.weak_axis_mask == right.weak_axis_mask
	       && left.dependent_axis_mask == right.dependent_axis_mask
	       && left.rank == right.rank;
}

AuthorityPolicy policy()
{
	return {
		{
			ControlAxis::Roll, ControlAxis::Pitch, ControlAxis::Yaw,
			ControlAxis::ThrustX, ControlAxis::ThrustY, ControlAxis::ThrustZ
		},
		0.01,
		1.0e-10
	};
}

// All values are exact, non-operational software fixtures. The matrix is not a
// model of the research vehicle and establishes no actuator signs or authority.
EffectivenessMatrix fixture()
{
	EffectivenessMatrix effectiveness{};
	effectiveness[static_cast<std::size_t>(ControlAxis::Roll)] = {1.0F, 0.0F, 0.0F, 0.0F, 0.0F};
	effectiveness[static_cast<std::size_t>(ControlAxis::Pitch)] = {0.0F, 1.0F, 0.0F, 0.0F, 0.0F};
	effectiveness[static_cast<std::size_t>(ControlAxis::Yaw)] = {0.0F, 0.0F, 1.0F, 0.0F, 0.0F};
	effectiveness[static_cast<std::size_t>(ControlAxis::ThrustX)] = {0.0F, 0.0F, 0.0F, 1.0F, 0.0F};
	effectiveness[static_cast<std::size_t>(ControlAxis::ThrustY)] = {0.0F, 0.0F, 0.0F, 0.0F, 1.0F};
	effectiveness[static_cast<std::size_t>(ControlAxis::ThrustZ)] = {1.0F, 1.0F, 0.0F, 0.0F, 0.0F};
	return effectiveness;
}

void expectFailure(const EffectivenessMatrix &effectiveness, const AuthorityPolicy &analysis_policy,
		   AuthorityStatus expected, const std::string &name)
{
	AuthorityReport report{
		{17.0, -19.0, 23.0, -29.0, 31.0, -37.0},
		{-41.0, 43.0, -47.0, 53.0, -59.0, 61.0},
		0xA5,
		0x5A,
		0x3C,
		5
	};
	const AuthorityReport sentinel = report;
	check(analyzeAuthority(effectiveness, analysis_policy, report) == expected, name + "_status");
	check(sameReport(report, sentinel), name + "_report_unchanged");
}

void priorityAndRankFixtures()
{
	const EffectivenessMatrix effectiveness = fixture();
	AuthorityPolicy analysis_policy = policy();
	AuthorityReport report{};
	check(analyzeAuthority(effectiveness, analysis_policy, report) == AuthorityStatus::Ok,
	      "default_priority_status");
	check(report.rank == 5U && report.selected_axis_mask == 0x1FU,
	      "default_priority_selects_first_five");
	check(report.dependent_axis_mask == 0x20U && report.weak_axis_mask == 0U,
	      "sixth_axis_dependent");
	check(near(report.axis_norm[5], std::sqrt(2.0)) && near(report.independence[5], 0.0),
	      "dependent_axis_metrics");
	check((report.selected_axis_mask | report.weak_axis_mask | report.dependent_axis_mask) == 0x3FU,
	      "every_axis_classified");
	check((report.selected_axis_mask & report.weak_axis_mask) == 0U
	      && (report.selected_axis_mask & report.dependent_axis_mask) == 0U
	      && (report.weak_axis_mask & report.dependent_axis_mask) == 0U,
	      "axis_classes_disjoint");

	analysis_policy.priority = {ControlAxis::ThrustZ, ControlAxis::Pitch, ControlAxis::Yaw,
				    ControlAxis::ThrustX, ControlAxis::ThrustY, ControlAxis::Roll
				   };
	check(analyzeAuthority(effectiveness, analysis_policy, report) == AuthorityStatus::Ok,
	      "alternate_priority_status");
	check(report.rank == 5U && report.selected_axis_mask == 0x3EU
	      && report.dependent_axis_mask == 0x01U,
	      "alternate_priority_changes_retained_axis");
}

void thresholdFixtures()
{
	EffectivenessMatrix effectiveness{};
	effectiveness[0][0] = 1.0F;
	effectiveness[1][1] = std::nextafter(1.0F, 0.0F);
	effectiveness[2][2] = 1.0F;
	AuthorityPolicy analysis_policy = policy();
	analysis_policy.minimum_axis_norm = 1.0;
	analysis_policy.minimum_independence = 1.0;
	AuthorityReport report{};
	check(analyzeAuthority(effectiveness, analysis_policy, report) == AuthorityStatus::Ok,
	      "threshold_status");
	check((report.selected_axis_mask & 0x05U) == 0x05U,
	      "axis_norm_and_independence_thresholds_inclusive");
	check((report.weak_axis_mask & 0x3AU) == 0x3AU,
	      "axis_below_norm_threshold_or_zero_is_weak");
	check(report.dependent_axis_mask == 0U, "weak_axes_not_dependent");

	effectiveness = {};
	effectiveness[0][0] = 1.0F;
	effectiveness[1] = {1.0F, 1.0F, 0.0F, 0.0F, 0.0F};
	analysis_policy.minimum_axis_norm = 0.01;
	analysis_policy.minimum_independence = 0.51;
	check(analyzeAuthority(effectiveness, analysis_policy, report) == AuthorityStatus::Ok,
	      "independence_threshold_status");
	check((report.dependent_axis_mask & 0x02U) != 0U && near(report.independence[1], 0.5),
	      "axis_below_independence_threshold_is_dependent");
}

void diagnosticFixtures()
{
	EffectivenessMatrix effectiveness{};
	effectiveness[0][0] = 1.0F;
	effectiveness[1][0] = -2.0F;
	AuthorityReport report{};
	check(analyzeAuthority(effectiveness, policy(), report) == AuthorityStatus::Ok,
	      "zero_actuator_columns_accepted");
	check(report.rank == 1U && report.selected_axis_mask == 0x01U
	      && report.dependent_axis_mask == 0x02U && report.weak_axis_mask == 0x3CU,
	      "failed_actuator_diagnostic_classification");

	const EffectivenessMatrix original = fixture();
	EffectivenessMatrix scaled = original;

	for (float &coefficient : scaled[0]) {
		coefficient *= -4.0F;
	}

	for (float &coefficient : scaled[1]) {
		coefficient *= 0.25F;
	}

	AuthorityReport baseline{};
	AuthorityReport scaled_report{};
	check(analyzeAuthority(original, policy(), baseline) == AuthorityStatus::Ok
	      && analyzeAuthority(scaled, policy(), scaled_report) == AuthorityStatus::Ok,
	      "scale_invariance_status");
	check(baseline.selected_axis_mask == scaled_report.selected_axis_mask
	      && baseline.weak_axis_mask == scaled_report.weak_axis_mask
	      && baseline.dependent_axis_mask == scaled_report.dependent_axis_mask
	      && baseline.rank == scaled_report.rank,
	      "nonweak_row_scaling_preserves_classification");

	for (std::size_t axis = 0; axis < kControlAxisCount; ++axis) {
		check(near(baseline.independence[axis], scaled_report.independence[axis]),
		      "nonweak_row_scaling_preserves_independence");
	}

	check(original == fixture(), "analysis_does_not_modify_input");

	const float largest = std::numeric_limits<float>::max();
	effectiveness = fixture();
	effectiveness[0] = {largest, largest, largest, largest, largest};
	effectiveness[1] = {largest, -largest, 0.0F, 0.0F, 0.0F};
	check(analyzeAuthority(effectiveness, policy(), report) == AuthorityStatus::Ok,
	      "finite_float_extremes_status");
	check(std::isfinite(report.axis_norm[0]) && report.axis_norm[0] > static_cast<double>(largest),
	      "finite_float_extremes_have_robust_norm");
}

void invalidFixtures()
{
	const EffectivenessMatrix effectiveness = fixture();
	const double nan = std::numeric_limits<double>::quiet_NaN();
	const double inf = std::numeric_limits<double>::infinity();

	for (double invalid : {0.0, -1.0, nan, inf, -inf}) {
		auto bad = policy();
		bad.minimum_axis_norm = invalid;
		expectFailure(effectiveness, bad, AuthorityStatus::InvalidPolicy, "invalid_axis_norm");
	}

	for (double invalid : {0.0, -1.0, std::nextafter(1.0, inf), nan, inf, -inf}) {
		auto bad = policy();
		bad.minimum_independence = invalid;
		expectFailure(effectiveness, bad, AuthorityStatus::InvalidPolicy, "invalid_independence");
	}

	auto bad_policy = policy();
	bad_policy.priority[5] = ControlAxis::Roll;
	expectFailure(effectiveness, bad_policy, AuthorityStatus::InvalidPolicy, "duplicate_priority_axis");
	bad_policy = policy();
	bad_policy.priority[0] = static_cast<ControlAxis>(kControlAxisCount);
	expectFailure(effectiveness, bad_policy, AuthorityStatus::InvalidPolicy, "out_of_range_priority_axis");

	for (float invalid : {
		     std::numeric_limits<float>::quiet_NaN(),
		     std::numeric_limits<float>::infinity(),
		     -std::numeric_limits<float>::infinity()
	     }) {
		for (std::size_t axis = 0; axis < kControlAxisCount; ++axis) {
			for (std::size_t actuator = 0; actuator < kChannelCount; ++actuator) {
				auto bad = effectiveness;
				bad[axis][actuator] = invalid;
				expectFailure(bad, policy(), AuthorityStatus::InvalidMatrix, "nonfinite_matrix");
			}
		}
	}
}

} // namespace

int main()
{
	priorityAndRankFixtures();
	thresholdFixtures();
	diagnosticFixtures();
	invalidFixtures();
	std::cout << checks << " non-operational authority checks, " << failures << " failures\n";
	return failures == 0 ? 0 : 1;
}
