#include "SyntheticConfiguration.hpp"

#include <cstdio>

int main()
{
	using namespace research_stabilized_manual;
	const AllocationConfiguration allocation = research_stabilized_fixture::configuration().allocation;
	constexpr float kDemand = 0.1F;
	constexpr float kStepS = 0.01F;
	constexpr int kStepsPerPhase = 12;

	if (!validAllocationConfiguration(allocation)) { return 2; }

	std::puts("corner,phase,step,request_x,request_y,request_z,previous_u0,previous_u1,previous_u2,previous_u3,u0,u1,u2,u3,achieved_x,achieved_y,achieved_z,residual_x,residual_y,residual_z,objective,profile");

	for (int corner = 0; corner < 8; ++corner) {
		CanardVector previous{};

		for (unsigned channel = 0; channel < 4; ++channel) {
			previous[channel] = allocation.trim[channel];
		}

		const Vector3f target((corner & 1) ? kDemand : -kDemand,
				      (corner & 2) ? kDemand : -kDemand,
				      (corner & 4) ? kDemand : -kDemand);

		for (int phase = 0; phase < 3; ++phase) {
			const Vector3f requested = phase == 0 ? target : (phase == 1 ? -target : Vector3f{});

			for (int step = 0; step < kStepsPerPhase; ++step) {
				AllocationReport report{};

				if (!allocateCanards(allocation, requested, previous, kStepS, report)) { return 3; }

				std::printf("%d,%s,%d,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.17g,%s\n",
					    corner, phase == 0 ? "hold" : (phase == 1 ? "reverse" : "zero"), step,
					    static_cast<double>(requested(0)), static_cast<double>(requested(1)),
					    static_cast<double>(requested(2)),
					    static_cast<double>(previous[0]), static_cast<double>(previous[1]),
					    static_cast<double>(previous[2]), static_cast<double>(previous[3]),
					    static_cast<double>(report.command[0]), static_cast<double>(report.command[1]),
					    static_cast<double>(report.command[2]), static_cast<double>(report.command[3]),
					    static_cast<double>(report.achieved(0)), static_cast<double>(report.achieved(1)),
					    static_cast<double>(report.achieved(2)), static_cast<double>(report.residual(0)),
					    static_cast<double>(report.residual(1)), static_cast<double>(report.residual(2)),
					    report.objective, research_stabilized_fixture::generated::kInputSha256);
				previous = report.command;
			}
		}
	}

	return 0;
}
