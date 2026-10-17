#pragma once

#include "EffectivenessModel.hpp"

#include <cstdint>

namespace research_actuator_command
{

using AxisPriority = FixedArray<ControlAxis, kControlAxisCount>;

enum class AuthorityStatus : uint8_t {
	Ok,
	InvalidMatrix,
	InvalidPolicy,
	ArithmeticFailure
};

struct AuthorityPolicy {
	AxisPriority priority;
	double minimum_axis_norm;
	double minimum_independence;
};

struct AuthorityReport {
	FixedArray<double, kControlAxisCount> axis_norm;
	FixedArray<double, kControlAxisCount> independence;
	uint8_t selected_axis_mask;
	uint8_t weak_axis_mask;
	uint8_t dependent_axis_mask;
	uint8_t rank;
};

// Analyzes the rows of a caller-supplied normalized effectiveness matrix in
// caller-supplied priority order. A row is weak when its Euclidean norm is
// below minimum_axis_norm. For every other row, independence is the squared
// residual norm after projecting its unit direction away from higher-priority
// selected rows. Rows below minimum_independence are dependent.
//
// This is a priority-dependent structural diagnostic, not a singular-value
// condition number or a physical authority claim. Zero actuator columns are
// accepted so the caller can analyze failed or unavailable actuators. The
// policy must contain each control axis exactly once, both thresholds must be
// finite and positive, and minimum_independence must not exceed one. On any
// failure, report is unchanged.
AuthorityStatus analyzeAuthority(const EffectivenessMatrix &effectiveness,
				 const AuthorityPolicy &policy,
				 AuthorityReport &report);

} // namespace research_actuator_command
