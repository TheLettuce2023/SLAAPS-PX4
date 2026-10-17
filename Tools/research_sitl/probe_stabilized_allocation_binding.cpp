#include "SyntheticConfiguration.hpp"

#include <cstdio>

int main()
{
	using namespace research_stabilized_manual;
	const Configuration configuration = research_stabilized_fixture::configuration();

	if (!validAllocationConfiguration(configuration.allocation)) { return 2; }

	CanardVector previous{};

	for (unsigned channel = 0; channel < 4; ++channel) {
		previous[channel] = configuration.allocation.trim[channel];
	}

	AllocationReport allocation{};
	const Vector3f requested(0.1F, -0.08F, 0.06F);

	if (!allocateCanards(configuration.allocation, requested, previous, 1.0F, allocation)) { return 3; }

	const Vector3f positive(1.0F, 1.0F, 1.0F);
	const Vector3f negative(-1.0F, -1.0F, -1.0F);
	AllocationReport positive_bound{};
	AllocationReport negative_bound{};
	AllocationReport positive_slew{};
	AllocationReport negative_slew{};

	if (!allocateCanards(configuration.allocation, positive, previous, 1.0F, positive_bound)
	    || !allocateCanards(configuration.allocation, negative, previous, 1.0F, negative_bound)
	    || !allocateCanards(configuration.allocation, positive, previous, 0.01F, positive_slew)
	    || !allocateCanards(configuration.allocation, negative, previous, 0.01F, negative_slew)) { return 4; }

	std::printf("{\"profile\":\"%s\",\"a00\":%.9g,\"trim0\":%.9g,\"minimum0\":%.9g,"
		    "\"maximum0\":%.9g,\"positive_slew0\":%.9g,\"negative_slew0\":%.9g,"
		    "\"commands\":[%.9g,%.9g,%.9g,%.9g],\"positive_bound_u0\":%.9g,"
		    "\"negative_bound_u0\":%.9g,\"positive_slew_u0\":%.9g,\"negative_slew_u0\":%.9g}\n",
		    research_stabilized_fixture::generated::kInputSha256,
		    static_cast<double>(configuration.allocation.effectiveness[0][0]),
		    static_cast<double>(configuration.allocation.trim[0]),
		    static_cast<double>(configuration.allocation.minimum[0]),
		    static_cast<double>(configuration.allocation.maximum[0]),
		    static_cast<double>(configuration.allocation.positive_slew_per_s[0]),
		    static_cast<double>(configuration.allocation.negative_slew_per_s[0]),
		    static_cast<double>(allocation.command[0]),
		    static_cast<double>(allocation.command[1]),
		    static_cast<double>(allocation.command[2]),
		    static_cast<double>(allocation.command[3]),
		    static_cast<double>(positive_bound.command[0]),
		    static_cast<double>(negative_bound.command[0]),
		    static_cast<double>(positive_slew.command[0]),
		    static_cast<double>(negative_slew.command[0]));
	return 0;
}
