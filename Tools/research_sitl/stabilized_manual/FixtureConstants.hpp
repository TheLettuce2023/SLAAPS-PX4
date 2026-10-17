#pragma once

#include <array>
#include <cstdint>

// SM3 synthetic rotational fixture, not SLAAPS aerodynamic or hardware data.
namespace research_stabilized_fixture
{
constexpr uint64_t kStepUs = 4000;
constexpr uint64_t kLeaseUs = 100000;
constexpr double kAirflowMps = 20.0; // Prescribed body-local airflow; no EDF wash.
constexpr double kFixedThrustN = 10.0; // Body +X at CG, reacted by ball restraint.
constexpr double kMomentScaleNm = 0.4;
constexpr double kServoTimeConstantS = 0.05;
constexpr double kMaximumDeflectionRad = 0.22689280275926285; // 13 degrees, synthetic.
constexpr std::array<double, 3> kInertiaKgM2{0.03, 0.08, 0.08};
constexpr std::array<double, 3> kDampingNmS{0.03, 0.04, 0.04};
constexpr double kEffectiveness[3][4] = {
	{0.25, -0.25, 0.25, -0.25},
	{0.25, 0.25, -0.25, -0.25},
	{0.25, -0.25, -0.25, 0.25}
};
} // namespace research_stabilized_fixture
