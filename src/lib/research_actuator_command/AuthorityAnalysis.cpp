#include "AuthorityAnalysis.hpp"

#include <cmath>

namespace research_actuator_command
{
namespace
{

using AxisVector = FixedArray<double, kChannelCount>;
using OrthonormalBasis = FixedArray<AxisVector, kChannelCount>;

bool validPolicy(const AuthorityPolicy &policy)
{
	if (!std::isfinite(policy.minimum_axis_norm)
	    || policy.minimum_axis_norm <= 0.0
	    || !std::isfinite(policy.minimum_independence)
	    || policy.minimum_independence <= 0.0
	    || policy.minimum_independence > 1.0) {
		return false;
	}

	uint8_t seen_axes = 0U;

	for (ControlAxis axis : policy.priority) {
		const std::size_t index = static_cast<std::size_t>(axis);

		if (index >= kControlAxisCount) {
			return false;
		}

		const uint8_t bit = static_cast<uint8_t>(1U << index);

		if ((seen_axes & bit) != 0U) {
			return false;
		}

		seen_axes = static_cast<uint8_t>(seen_axes | bit);
	}

	return seen_axes == static_cast<uint8_t>((1U << kControlAxisCount) - 1U);
}

bool finiteMatrix(const EffectivenessMatrix &effectiveness)
{
	for (const auto &axis : effectiveness) {
		for (float coefficient : axis) {
			if (!std::isfinite(coefficient)) {
				return false;
			}
		}
	}

	return true;
}

double robustNorm(const EffectivenessMatrix::value_type &axis)
{
	double maximum = 0.0;

	for (float coefficient : axis) {
		const double magnitude = std::fabs(static_cast<double>(coefficient));
		maximum = magnitude > maximum ? magnitude : maximum;
	}

	if (maximum <= 0.0) {
		return 0.0;
	}

	double scaled_sum = 0.0;

	for (float coefficient : axis) {
		const double scaled = static_cast<double>(coefficient) / maximum;
		scaled_sum += scaled * scaled;
	}

	return maximum * std::sqrt(scaled_sum);
}

double squaredNorm(const AxisVector &axis)
{
	double result = 0.0;

	for (double value : axis) {
		result += value * value;
	}

	return result;
}

double dot(const AxisVector &left, const AxisVector &right)
{
	double result = 0.0;

	for (std::size_t actuator = 0; actuator < kChannelCount; ++actuator) {
		result += left[actuator] * right[actuator];
	}

	return result;
}

} // namespace

AuthorityStatus analyzeAuthority(const EffectivenessMatrix &effectiveness,
				 const AuthorityPolicy &policy,
				 AuthorityReport &report)
{
	if (!finiteMatrix(effectiveness)) {
		return AuthorityStatus::InvalidMatrix;
	}

	if (!validPolicy(policy)) {
		return AuthorityStatus::InvalidPolicy;
	}

	AuthorityReport candidate{};
	OrthonormalBasis basis{};
	std::size_t basis_count = 0U;

	for (ControlAxis control_axis : policy.priority) {
		const std::size_t axis = static_cast<std::size_t>(control_axis);
		const uint8_t axis_bit = static_cast<uint8_t>(1U << axis);
		const double axis_norm = robustNorm(effectiveness[axis]);

		if (!std::isfinite(axis_norm)) {
			return AuthorityStatus::ArithmeticFailure;
		}

		candidate.axis_norm[axis] = axis_norm;

		if (axis_norm < policy.minimum_axis_norm) {
			candidate.weak_axis_mask = static_cast<uint8_t>(candidate.weak_axis_mask | axis_bit);
			continue;
		}

		AxisVector residual{};

		for (std::size_t actuator = 0; actuator < kChannelCount; ++actuator) {
			residual[actuator] = static_cast<double>(effectiveness[axis][actuator]) / axis_norm;
		}

		// Reorthogonalization reduces loss of orthogonality for nearly dependent
		// rows without imposing a vehicle-specific tolerance.
		for (int pass = 0; pass < 2; ++pass) {
			for (std::size_t basis_index = 0; basis_index < basis_count; ++basis_index) {
				const double projection = dot(residual, basis[basis_index]);

				for (std::size_t actuator = 0; actuator < kChannelCount; ++actuator) {
					residual[actuator] -= projection * basis[basis_index][actuator];
				}
			}
		}

		double independence = squaredNorm(residual);

		if (!std::isfinite(independence)) {
			return AuthorityStatus::ArithmeticFailure;
		}

		if (independence > 1.0) {
			independence = 1.0;
		}

		candidate.independence[axis] = independence;

		if (basis_count >= kChannelCount || independence < policy.minimum_independence) {
			candidate.dependent_axis_mask = static_cast<uint8_t>(candidate.dependent_axis_mask | axis_bit);
			continue;
		}

		const double residual_norm = std::sqrt(independence);

		if (!std::isfinite(residual_norm) || residual_norm <= 0.0) {
			return AuthorityStatus::ArithmeticFailure;
		}

		for (std::size_t actuator = 0; actuator < kChannelCount; ++actuator) {
			basis[basis_count][actuator] = residual[actuator] / residual_norm;
		}

		++basis_count;
		candidate.selected_axis_mask = static_cast<uint8_t>(candidate.selected_axis_mask | axis_bit);
	}

	candidate.rank = static_cast<uint8_t>(basis_count);
	report = candidate;
	return AuthorityStatus::Ok;
}

} // namespace research_actuator_command
