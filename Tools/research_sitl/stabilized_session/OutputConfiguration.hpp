#pragma once
#include <lib/research_stabilized_manual/StabilizedOutput.hpp>
#include "../stabilized_manual/FixtureConstants.hpp"
namespace research_stabilized_session_fixture
{
inline research_stabilized_manual::OutputConfiguration outputConfiguration()
{
	using namespace research_stabilized_manual;
	OutputConfiguration config{};
	config.lease_us = research_stabilized_fixture::kLeaseUs;
	config.acknowledgement_us = 50000; // Synthetic asynchronous transport budget.
	config.on_fault = OutputFallback::Disarmed;

	for (unsigned i = 0; i < 4; ++i) {
		config.servos[i] = {1000.0F, 1500.0F, 2000.0F, 1000, 1500, 2000};
		config.minimum[i] = -1.0F;
		config.maximum[i] = 1.0F;
	}

	return config;
}
} // namespace research_stabilized_session_fixture
