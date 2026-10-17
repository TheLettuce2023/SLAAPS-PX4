#include "CanardAllocator.hpp"

#include <cmath>
#include <float.h>

namespace research_stabilized_manual
{
namespace
{
constexpr unsigned kActiveSets{81}; // Three statuses for each of four surfaces.
constexpr double kPivotTolerance{1.0e-12}; // Relative numerical solver tolerance, not physical authority.

bool solve(double system[4][5], unsigned size, double solution[4])
{
	double scale = 0.0;

	for (unsigned i = 0; i < size; ++i) {
		for (unsigned j = 0; j < size; ++j) { scale = std::fmax(scale, std::fabs(system[i][j])); }
	}

	for (unsigned i = 0; i < size; ++i) {
		unsigned pivot = i;

		for (unsigned j = i + 1; j < size; ++j) {
			if (std::fabs(system[j][i]) > std::fabs(system[pivot][i])) { pivot = j; }
		}

		if (!std::isfinite(system[pivot][i]) || std::fabs(system[pivot][i]) <= kPivotTolerance * scale) { return false; }

		for (unsigned j = i; j <= size; ++j) {
			const double temporary = system[i][j];
			system[i][j] = system[pivot][j];
			system[pivot][j] = temporary;
		}

		const double divisor = system[i][i];

		for (unsigned j = i; j <= size; ++j) { system[i][j] /= divisor; }

		for (unsigned row = 0; row < size; ++row) {
			if (row == i) { continue; }

			const double factor = system[row][i];

			for (unsigned j = i; j <= size; ++j) { system[row][j] -= factor * system[i][j]; }
		}
	}

	for (unsigned i = 0; i < size; ++i) {
		solution[i] = system[i][size];

		if (!std::isfinite(solution[i])) { return false; }
	}

	return true;
}
}

bool validAllocationConfiguration(const AllocationConfiguration &configuration)
{
	if (!std::isfinite(configuration.regularization) || configuration.regularization <= 0.0F
	    || !std::isfinite(configuration.minimum_row_norm) || configuration.minimum_row_norm <= 0.0F
	    || !std::isfinite(configuration.minimum_independence) || configuration.minimum_independence <= 0.0F
	    || configuration.minimum_independence > 1.0F || !std::isfinite(configuration.saturation_tolerance)
	    || configuration.saturation_tolerance < 0.0F) { return false; }

	double normalized[3][4] {};

	for (unsigned axis = 0; axis < 3; ++axis) {
		if (!std::isfinite(configuration.axis_weight(axis)) || configuration.axis_weight(axis) <= 0.0F) { return false; }

		double norm_squared = 0.0;

		for (unsigned channel = 0; channel < 4; ++channel) {
			const double value = configuration.effectiveness[axis][channel];

			if (!std::isfinite(value)) { return false; }

			norm_squared += value * value;
		}

		const double norm = std::sqrt(norm_squared);

		if (norm < static_cast<double>(configuration.minimum_row_norm)) { return false; }

		for (unsigned channel = 0; channel < 4; ++channel) {
			normalized[axis][channel] = static_cast<double>(configuration.effectiveness[axis][channel]) / norm;
		}
	}

	// The four-column normal matrix has a null direction before regularization.
	// Reject a regularizer below numerical resolution rather than silently
	// replacing an interior optimum with a surviving bound-only active set.
	double largest_diagonal = 0.0;

	for (unsigned channel = 0; channel < 4; ++channel) {
		double diagonal = 0.0;

		for (unsigned axis = 0; axis < 3; ++axis) {
			const double coefficient = configuration.effectiveness[axis][channel];
			diagonal += static_cast<double>(configuration.axis_weight(axis)) * coefficient * coefficient;
		}

		largest_diagonal = std::fmax(largest_diagonal, diagonal);
	}

	constexpr double kResolutionMargin{100.0};

	if (static_cast<double>(configuration.regularization) < kResolutionMargin * kPivotTolerance * largest_diagonal) { return false; }

	// Modified Gram-Schmidt of normalized rows. Structural independence only.
	for (unsigned axis = 0; axis < 3; ++axis) {
		for (unsigned previous = 0; previous < axis; ++previous) {
			double projection = 0.0;

			for (unsigned channel = 0; channel < 4; ++channel) { projection += normalized[axis][channel] * normalized[previous][channel]; }

			for (unsigned channel = 0; channel < 4; ++channel) { normalized[axis][channel] -= projection * normalized[previous][channel]; }
		}

		double norm_squared = 0.0;

		for (double value : normalized[axis]) { norm_squared += value * value; }

		if (norm_squared < static_cast<double>(configuration.minimum_independence)) { return false; }

		for (double &value : normalized[axis]) { value /= std::sqrt(norm_squared); }
	}

	for (unsigned channel = 0; channel < 4; ++channel) {
		if (!std::isfinite(configuration.minimum[channel]) || !std::isfinite(configuration.maximum[channel])
		    || !std::isfinite(configuration.trim[channel]) || configuration.minimum[channel] < -1.0F
		    || configuration.maximum[channel] > 1.0F || configuration.minimum[channel] >= configuration.maximum[channel]
		    || configuration.trim[channel] < configuration.minimum[channel] || configuration.trim[channel] > configuration.maximum[channel]
		    || !std::isfinite(configuration.positive_slew_per_s[channel]) || configuration.positive_slew_per_s[channel] <= 0.0F
		    || !std::isfinite(configuration.negative_slew_per_s[channel]) || configuration.negative_slew_per_s[channel] <= 0.0F) { return false; }
	}

	return true;
}

bool allocateCanards(const AllocationConfiguration &configuration, const Vector3f &requested,
		     const CanardVector &previous, float dt_s, AllocationReport &report)
{
	if (!validAllocationConfiguration(configuration) || !std::isfinite(dt_s) || dt_s <= 0.0F) { return false; }

	double lower[4] {}, upper[4] {};

	for (unsigned axis = 0; axis < 3; ++axis) {
		if (!std::isfinite(requested(axis))) { return false; }
	}

	for (unsigned channel = 0; channel < 4; ++channel) {
		if (!std::isfinite(previous[channel]) || previous[channel] < configuration.minimum[channel]
		    || previous[channel] > configuration.maximum[channel]) { return false; }

		lower[channel] = std::fmax(configuration.minimum[channel], static_cast<double>(previous[channel])
					   - static_cast<double>(dt_s) * static_cast<double>(configuration.negative_slew_per_s[channel]));
		upper[channel] = std::fmin(configuration.maximum[channel], static_cast<double>(previous[channel])
					   + static_cast<double>(dt_s) * static_cast<double>(configuration.positive_slew_per_s[channel]));
	}

	double best = DBL_MAX;
	double best_command[4] {};

	for (unsigned active_set = 0; active_set < kActiveSets; ++active_set) {
		unsigned code = active_set, count = 0, free_channels[4] {};
		double command[4] {}, residual[3] {};

		for (unsigned axis = 0; axis < 3; ++axis) { residual[axis] = requested(axis); }

		for (unsigned channel = 0; channel < 4; ++channel) {
			const unsigned status = code % 3;
			code /= 3;

			if (status == 0) {
				free_channels[count++] = channel;
				command[channel] = configuration.trim[channel];

			} else {
				command[channel] = status == 1 ? lower[channel] : upper[channel];

				for (unsigned axis = 0; axis < 3; ++axis) {
					residual[axis] -= static_cast<double>(configuration.effectiveness[axis][channel])
							  * (command[channel] - static_cast<double>(configuration.trim[channel]));
				}
			}
		}

		double system[4][5] {}, solution[4] {};

		for (unsigned row = 0; row < count; ++row) {
			for (unsigned column = 0; column < count; ++column) {
				system[row][column] = row == column ? static_cast<double>(configuration.regularization) : 0.0;

				for (unsigned axis = 0; axis < 3; ++axis) {
					system[row][column] += static_cast<double>(configuration.axis_weight(axis))
							       * static_cast<double>(configuration.effectiveness[axis][free_channels[row]])
							       * static_cast<double>(configuration.effectiveness[axis][free_channels[column]]);
				}
			}

			for (unsigned axis = 0; axis < 3; ++axis) {
				system[row][count] += static_cast<double>(configuration.axis_weight(axis))
						      * static_cast<double>(configuration.effectiveness[axis][free_channels[row]]) * residual[axis];
			}
		}

		if (count > 0 && !solve(system, count, solution)) { continue; }

		bool within_bounds = true;

		for (unsigned i = 0; i < count; ++i) {
			const unsigned channel = free_channels[i];
			command[channel] += solution[i];
			within_bounds &= command[channel] >= lower[channel] && command[channel] <= upper[channel];
		}

		if (!within_bounds) { continue; }

		double objective = 0.0;

		for (unsigned axis = 0; axis < 3; ++axis) {
			double error = requested(axis);

			for (unsigned channel = 0; channel < 4; ++channel) {
				error -= static_cast<double>(configuration.effectiveness[axis][channel])
					 * (command[channel] - static_cast<double>(configuration.trim[channel]));
			}

			objective += static_cast<double>(configuration.axis_weight(axis)) * error * error;
		}

		for (unsigned channel = 0; channel < 4; ++channel) {
			const double displacement = command[channel] - static_cast<double>(configuration.trim[channel]);
			objective += static_cast<double>(configuration.regularization) * displacement * displacement;
		}

		if (std::isfinite(objective) && objective < best) {
			best = objective;

			for (unsigned channel = 0; channel < 4; ++channel) { best_command[channel] = command[channel]; }
		}
	}

	if (!(best < DBL_MAX)) { return false; }

	AllocationReport candidate{};
	candidate.objective = best;

	for (unsigned channel = 0; channel < 4; ++channel) {
		candidate.command[channel] = static_cast<float>(best_command[channel]);
	}

	for (unsigned axis = 0; axis < 3; ++axis) {
		double achieved = 0.0;

		for (unsigned channel = 0; channel < 4; ++channel) {
			achieved += static_cast<double>(configuration.effectiveness[axis][channel])
				    * (static_cast<double>(candidate.command[channel]) - static_cast<double>(configuration.trim[channel]));
		}

		candidate.achieved(axis) = static_cast<float>(achieved);
		candidate.residual(axis) = requested(axis) - candidate.achieved(axis);

		if (!std::isfinite(candidate.achieved(axis)) || !std::isfinite(candidate.residual(axis))) { return false; }

		candidate.saturation_positive(axis) = candidate.residual(axis) > configuration.saturation_tolerance;
		candidate.saturation_negative(axis) = candidate.residual(axis) < -configuration.saturation_tolerance;
	}

	report = candidate;
	return true;
}

} // namespace research_stabilized_manual
