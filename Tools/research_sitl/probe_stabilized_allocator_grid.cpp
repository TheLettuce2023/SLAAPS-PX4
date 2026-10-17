#include "SyntheticConfiguration.hpp"

#include <cstdio>

int main()
{
	using namespace research_stabilized_manual;
	const Configuration configuration = research_stabilized_fixture::configuration();
	const AllocationConfiguration &allocation = configuration.allocation;

	if (!validAllocationConfiguration(allocation)) { return 2; }

	CanardVector previous{};

	for (unsigned channel = 0; channel < 4; ++channel) {
		previous[channel] = allocation.trim[channel];
	}

	std::puts("mode,ix,iy,iz,request_x,request_y,request_z,u0,u1,u2,u3,achieved_x,achieved_y,achieved_z,residual_x,residual_y,residual_z,objective,profile");

	for (unsigned mode = 0; mode < 2; ++mode) {
		const float dt_s = mode == 0 ? 1.0F : 0.01F;

		for (int ix = -5; ix <= 5; ++ix) {
			for (int iy = -5; iy <= 5; ++iy) {
				for (int iz = -5; iz <= 5; ++iz) {
					const Vector3f requested(static_cast<float>(ix) * 0.02F,
								 static_cast<float>(iy) * 0.02F,
								 static_cast<float>(iz) * 0.02F);
					AllocationReport report{};

					if (!allocateCanards(allocation, requested, previous, dt_s, report)) { return 3; }

					std::printf("%s,%d,%d,%d,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.17g,%s\n",
						    mode == 0 ? "static" : "slew", ix, iy, iz,
						    static_cast<double>(requested(0)), static_cast<double>(requested(1)),
						    static_cast<double>(requested(2)),
						    static_cast<double>(report.command[0]), static_cast<double>(report.command[1]),
						    static_cast<double>(report.command[2]), static_cast<double>(report.command[3]),
						    static_cast<double>(report.achieved(0)), static_cast<double>(report.achieved(1)),
						    static_cast<double>(report.achieved(2)), static_cast<double>(report.residual(0)),
						    static_cast<double>(report.residual(1)), static_cast<double>(report.residual(2)),
						    report.objective, research_stabilized_fixture::generated::kInputSha256);
				}
			}
		}
	}

	return 0;
}
