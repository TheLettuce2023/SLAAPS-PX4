#include "WrenchAllocator.hpp"

#include <cmath>
#include <float.h>
#include <string.h>

namespace slaaps
{
namespace
{
constexpr unsigned kRows{6};
constexpr unsigned kInputs{5};
constexpr unsigned kSurfaces{4};
constexpr unsigned kActiveSets{243}; // Three statuses for each of five actuators.
constexpr double kPivotTolerance{1.0e-12}; // Numerical resolution, not physical authority.
constexpr double kResolutionMargin{100.0};
constexpr uint32_t kSignBit{0x80000000U};
constexpr uint32_t kMagnitudeBits{0x7fffffffU};

static_assert(FLT_RADIX == 2 && FLT_MANT_DIG == 24 && FLT_MAX_EXP == 128
	      &&sizeof(float) == sizeof(uint32_t), "Wrench bounds require binary32 floats");

bool physicalCommand(unsigned input, float value)
{
	return std::isfinite(value) && (input < kSurfaces ? value >= -1.F && value <= 1.F : value >= 0.F);
}

float adjacentFloat(float value, bool positive)
{
	uint32_t bits{};
	memcpy(&bits, &value, sizeof(bits));

	if (!(bits & kMagnitudeBits)) {
		bits = positive ? 1U : kSignBit | 1U;

	} else if (bool(bits & kSignBit) == positive) {
		--bits;

	} else {
		++bits;
	}

	memcpy(&value, &bits, sizeof(value));
	return value;
}

// Same partial-pivot Gauss-Jordan mechanism as the established four-canard
// allocator, with a fixed five-variable workspace. No variable-sized storage.
bool solve(double system[kInputs][kInputs + 1], unsigned size, double solution[kInputs])
{
	double scale = 0.0;

	for (unsigned row = 0; row < size; ++row) {
		for (unsigned column = 0; column < size; ++column) {
			scale = ::fmax(scale, std::fabs(system[row][column]));
		}
	}

	for (unsigned row = 0; row < size; ++row) {
		unsigned pivot = row;

		for (unsigned candidate = row + 1; candidate < size; ++candidate) {
			if (std::fabs(system[candidate][row]) > std::fabs(system[pivot][row])) { pivot = candidate; }
		}

		if (!std::isfinite(system[pivot][row]) || std::fabs(system[pivot][row]) <= kPivotTolerance * scale) { return false; }

		for (unsigned column = row; column <= size; ++column) {
			const double temporary = system[row][column];
			system[row][column] = system[pivot][column];
			system[pivot][column] = temporary;
		}

		const double divisor = system[row][row];

		for (unsigned column = row; column <= size; ++column) { system[row][column] /= divisor; }

		for (unsigned other = 0; other < size; ++other) {
			if (other == row) { continue; }

			const double factor = system[other][row];

			for (unsigned column = row; column <= size; ++column) {
				system[other][column] -= factor * system[row][column];
			}
		}
	}

	for (unsigned row = 0; row < size; ++row) {
		solution[row] = system[row][size];

		if (!std::isfinite(solution[row])) { return false; }
	}

	return true;
}
}

bool inwardWrenchBound(double value, bool lower_bound, float &result)
{
	if (!std::isfinite(value) || std::fabs(value) > static_cast<double>(FLT_MAX)) { return false; }

	float candidate = static_cast<float>(value);

	if ((lower_bound && static_cast<double>(candidate) < value)
	    || (!lower_bound && static_cast<double>(candidate) > value)) {
		candidate = adjacentFloat(candidate, lower_bound);
	}

	if (!std::isfinite(candidate)) { return false; }

	result = candidate;
	return true;
}

bool validWrenchAllocationConfiguration(const WrenchAllocationConfiguration &configuration)
{
	for (unsigned input = 0; input < kInputs; ++input) {
		if (!physicalCommand(input, configuration.reference[input])
		    || !physicalCommand(input, configuration.minimum[input]) || !physicalCommand(input, configuration.maximum[input])
		    || configuration.minimum[input] > configuration.maximum[input]
		    || !std::isfinite(configuration.positive_slew_per_s[input]) || configuration.positive_slew_per_s[input] <= 0.F
		    || !std::isfinite(configuration.negative_slew_per_s[input]) || configuration.negative_slew_per_s[input] <= 0.F) {
			return false;
		}
	}

	for (unsigned row = 0; row < kRows; ++row) {
		if (!std::isfinite(configuration.model.offset[row])
		    || !std::isfinite(configuration.residual_tolerance[row]) || configuration.residual_tolerance[row] <= 0.F) { return false; }
	}

	return validWrenchAllocationScaling(configuration);
}

bool validWrenchAllocationScaling(const WrenchAllocationConfiguration &configuration)
{
	if (!std::isfinite(configuration.regularization) || configuration.regularization <= 0.F) { return false; }

	for (float scale : configuration.regularization_scale) {
		if (!std::isfinite(scale) || scale <= 0.F) { return false; }
	}

	for (unsigned row = 0; row < kRows; ++row) {
		if (!std::isfinite(configuration.axis_weight[row]) || configuration.axis_weight[row] <= 0.F) { return false; }

		for (float coefficient : configuration.model.effectiveness[row]) {
			if (!std::isfinite(coefficient)) { return false; }
		}
	}

	// Solve in dimensionless x = scale*u, with lambda*||x-scale*reference||^2.
	// Keep the regularizer above the resolution of the scaled normal matrix,
	// including deficient-column-rank models. No six-row rank test is meaningful.
	double largest_diagonal = 0.0;

	for (unsigned input = 0; input < kInputs; ++input) {
		double diagonal = 0.0;

		for (unsigned row = 0; row < kRows; ++row) {
			const double coefficient = static_cast<double>(configuration.model.effectiveness[row][input])
						   / static_cast<double>(configuration.regularization_scale[input]);
			diagonal += static_cast<double>(configuration.axis_weight[row]) * coefficient * coefficient;
		}

		if (!std::isfinite(diagonal)) { return false; }

		largest_diagonal = ::fmax(largest_diagonal, diagonal);
	}

	return static_cast<double>(configuration.regularization) >= kResolutionMargin * kPivotTolerance * largest_diagonal;
}

WrenchAllocationResult allocateWrench(const WrenchAllocationConfiguration &configuration,
				      const float (&requested)[6], const float (&previous)[5], float dt_s, WrenchAllocationReport &report)
{
	if (!validWrenchAllocationConfiguration(configuration)) { return WrenchAllocationResult::InvalidConfiguration; }

	if (!std::isfinite(dt_s) || dt_s <= 0.F) { return WrenchAllocationResult::InvalidInput; }

	for (float value : requested) {
		if (!std::isfinite(value)) { return WrenchAllocationResult::InvalidInput; }
	}

	float lower[kInputs] {}, upper[kInputs] {};
	double lower_scaled[kInputs] {}, upper_scaled[kInputs] {}, normal[kInputs][kInputs] {}, rhs[kInputs] {};

	for (unsigned input = 0; input < kInputs; ++input) {
		if (!physicalCommand(input, previous[input])) { return WrenchAllocationResult::InvalidInput; }

		const double low = ::fmax(static_cast<double>(configuration.minimum[input]), static_cast<double>(previous[input])
					  - static_cast<double>(dt_s) * static_cast<double>(configuration.negative_slew_per_s[input]));
		const double high = ::fmin(static_cast<double>(configuration.maximum[input]), static_cast<double>(previous[input])
					   + static_cast<double>(dt_s) * static_cast<double>(configuration.positive_slew_per_s[input]));

		if (low > high) { return WrenchAllocationResult::Infeasible; }

		if (!inwardWrenchBound(low, true, lower[input]) || !inwardWrenchBound(high, false, upper[input])) {
			return WrenchAllocationResult::ArithmeticFailure;
		}

		if (lower[input] > upper[input]) { return WrenchAllocationResult::Infeasible; }

		lower_scaled[input] = static_cast<double>(configuration.regularization_scale[input]) * static_cast<double>(lower[input]);
		upper_scaled[input] = static_cast<double>(configuration.regularization_scale[input]) * static_cast<double>(upper[input]);
	}

	for (unsigned input = 0; input < kInputs; ++input) {
		const double scale = configuration.regularization_scale[input];
		rhs[input] = static_cast<double>(configuration.regularization) * scale * static_cast<double>(configuration.reference[input]);

		for (unsigned other = 0; other < kInputs; ++other) {
			normal[input][other] = input == other ? static_cast<double>(configuration.regularization) : 0.0;

			for (unsigned row = 0; row < kRows; ++row) {
				const double coefficient = static_cast<double>(configuration.model.effectiveness[row][input]) / scale;
				normal[input][other] += static_cast<double>(configuration.axis_weight[row]) * coefficient
							* (static_cast<double>(configuration.model.effectiveness[row][other])
							   / static_cast<double>(configuration.regularization_scale[other]));
			}

			if (!std::isfinite(normal[input][other])) { return WrenchAllocationResult::ArithmeticFailure; }
		}

		for (unsigned row = 0; row < kRows; ++row) {
			rhs[input] += static_cast<double>(configuration.axis_weight[row])
				      * (static_cast<double>(configuration.model.effectiveness[row][input]) / scale)
				      * (static_cast<double>(requested[row]) - static_cast<double>(configuration.model.offset[row]));
		}

		if (!std::isfinite(rhs[input])) { return WrenchAllocationResult::ArithmeticFailure; }
	}

	double best = DBL_MAX;
	float best_command[kInputs] {};
	bool have_best = false;

	for (unsigned active_set = 0; active_set < kActiveSets; ++active_set) {
		unsigned code = active_set, count = 0, free_inputs[kInputs] {}, states[kInputs] {};
		double scaled[kInputs] {}, system[kInputs][kInputs + 1] {}, solution[kInputs] {};

		for (unsigned input = 0; input < kInputs; ++input) {
			states[input] = code % 3;
			code /= 3;

			if (!states[input]) { free_inputs[count++] = input; }

			else { scaled[input] = states[input] == 1 ? lower_scaled[input] : upper_scaled[input]; }
		}

		for (unsigned row = 0; row < count; ++row) {
			const unsigned input = free_inputs[row];
			system[row][count] = rhs[input];

			for (unsigned other = 0; other < kInputs; ++other) {
				if (states[other]) { system[row][count] -= normal[input][other] * scaled[other]; }
			}

			for (unsigned column = 0; column < count; ++column) { system[row][column] = normal[input][free_inputs[column]]; }
		}

		if (count && !solve(system, count, solution)) { return WrenchAllocationResult::ArithmeticFailure; }

		bool within_bounds = true;

		for (unsigned row = 0; row < count; ++row) {
			const unsigned input = free_inputs[row];
			scaled[input] = solution[row];
			within_bounds &= scaled[input] >= lower_scaled[input] && scaled[input] <= upper_scaled[input];
		}

		if (!within_bounds) { continue; }

		float command[kInputs] {};

		for (unsigned input = 0; input < kInputs; ++input) {
			const double value = scaled[input] / static_cast<double>(configuration.regularization_scale[input]);

			if (!std::isfinite(value)) { return WrenchAllocationResult::ArithmeticFailure; }

			command[input] = states[input] ? (states[input] == 1 ? lower[input] : upper[input]) :
					 static_cast<float>(::fmax(static_cast<double>(lower[input]), ::fmin(static_cast<double>(upper[input]), value)));

			if (command[input] < lower[input]) { command[input] = lower[input]; }

			if (command[input] > upper[input]) { command[input] = upper[input]; }

			scaled[input] = static_cast<double>(configuration.regularization_scale[input]) * static_cast<double>(command[input]);
		}

		// Drop constant terms only for comparing candidates. This avoids losing
		// a bounded command's improvement beneath a large affine/reference cost.
		double objective = 0.0;

		for (unsigned input = 0; input < kInputs; ++input) {
			double gradient = -2.0 * rhs[input];

			for (unsigned other = 0; other < kInputs; ++other) { gradient += normal[input][other] * scaled[other]; }

			objective += scaled[input] * gradient;
		}

		if (!std::isfinite(objective)) { return WrenchAllocationResult::ArithmeticFailure; }

		if (!have_best || objective < best) {
			have_best = true;
			best = objective;

			for (unsigned input = 0; input < kInputs; ++input) { best_command[input] = command[input]; }
		}
	}

	if (!have_best) { return WrenchAllocationResult::ArithmeticFailure; }

	WrenchAllocationReport candidate{};
	bool accepted_residual = true;
	candidate.realized_edf_thrust_n = best_command[kInputs - 1];

	for (unsigned input = 0; input < kInputs; ++input) {
		candidate.command[input] = best_command[input];
		candidate.saturation_lower[input] = best_command[input] <= lower[input];
		candidate.saturation_upper[input] = best_command[input] >= upper[input];
		const double displacement = static_cast<double>(configuration.regularization_scale[input])
					    * (static_cast<double>(best_command[input]) - static_cast<double>(configuration.reference[input]));
		candidate.objective += static_cast<double>(configuration.regularization) * displacement * displacement;
	}

	for (unsigned row = 0; row < kRows; ++row) {
		double achieved = configuration.model.offset[row];

		for (unsigned input = 0; input < kInputs; ++input) {
			achieved += static_cast<double>(configuration.model.effectiveness[row][input]) * static_cast<double>(best_command[input]);
		}

		const double residual = static_cast<double>(requested[row]) - achieved;

		if (!std::isfinite(achieved) || !std::isfinite(residual) || std::fabs(achieved) > static_cast<double>(FLT_MAX)
		    || std::fabs(residual) > static_cast<double>(FLT_MAX)) { return WrenchAllocationResult::ArithmeticFailure; }

		candidate.achieved[row] = static_cast<float>(achieved);
		candidate.residual[row] = static_cast<float>(residual);
		candidate.objective += static_cast<double>(configuration.axis_weight[row]) * residual * residual;
		accepted_residual &= std::fabs(residual) <= static_cast<double>(configuration.residual_tolerance[row]);
	}

	if (!std::isfinite(candidate.objective)) { return WrenchAllocationResult::ArithmeticFailure; }

	if (!accepted_residual) { return WrenchAllocationResult::ResidualExceeded; }

	report = candidate;
	return WrenchAllocationResult::Success;
}

} // namespace slaaps
