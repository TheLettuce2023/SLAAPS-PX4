#pragma once

#include <matrix/matrix/math.hpp>
#include <lib/research_actuator_command/FixedArray.hpp>

namespace research_stabilized_manual
{

using Vector3f = matrix::Vector3f;
using CanardVector = research_actuator_command::FixedArray<float, 4>;
using MomentMatrix = research_actuator_command::FixedArray<CanardVector, 3>;

struct AllocationConfiguration {
	MomentMatrix effectiveness{}; // Normalized body moment per normalized surface displacement from trim.
	CanardVector trim{};
	CanardVector minimum{};
	CanardVector maximum{};
	CanardVector positive_slew_per_s{};
	CanardVector negative_slew_per_s{};
	Vector3f axis_weight{};
	float regularization{};
	float minimum_row_norm{};
	float minimum_independence{};
	float saturation_tolerance{};
};

struct AllocationReport {
	CanardVector command{};
	Vector3f achieved{};
	Vector3f residual{};
	matrix::Vector<bool, 3> saturation_positive{};
	matrix::Vector<bool, 3> saturation_negative{};
	double objective{};
};

// Bounded weighted least squares: enumerate all 3^4 free/lower/upper active
// sets and select the minimum objective. Fixed storage; no output or runtime.
// Slew bounds apply to the caller's previously accepted command. Regularization
// penalizes displacement from trim. Every configuration value is caller supplied.
bool validAllocationConfiguration(const AllocationConfiguration &configuration);
bool allocateCanards(const AllocationConfiguration &configuration, const Vector3f &requested,
		     const CanardVector &previous, float dt_s, AllocationReport &report);

} // namespace research_stabilized_manual
