#include "Px4SessionConsumer.hpp"
#include <px4_platform_common/module.h>
#include <cstdlib>
#include <cstring>
using namespace research_stabilized_session_fixture;
namespace { Px4SessionConsumer *consumer{}; }
extern "C" __EXPORT int research_session_consumer_main(int argc, char **argv)
{
	if (argc == 2 && std::strcmp(argv[1], "status") == 0) {
		if (!consumer) { return PX4_ERROR; }

		PX4_INFO("phase=%u acknowledgements=%u faults=%u private SITL only",
			 static_cast<unsigned>(consumer->phase()), consumer->acknowledgements(), consumer->faults());
		return PX4_OK;
	}

	if (argc == 2 && consumer && std::strcmp(argv[1], "stop") == 0) { consumer->stop(); return PX4_OK; }

	if (argc == 2 && consumer && std::strcmp(argv[1], "revoke") == 0) { consumer->revoke(); return PX4_OK; }

	if (argc != 4 || consumer || std::strcmp(argv[1], "start") != 0) {
		PX4_WARN("start <observer-profile> <unique-plant-namespace>; status|revoke|stop");
		return PX4_ERROR;
	}

	FILE *file = std::fopen(argv[2], "r");

	if (!file) { return PX4_ERROR; }

	ObserverProfile profile{};
	const bool valid = readObserverProfile(file, profile);
	std::fclose(file);

	if (!valid) { return PX4_ERROR; }

	// Observer files remain output-disabled; only this private synthetic mode supplies thrust.
	profile.control.fixed_simulated_thrust_n = 10.0F; // Declared fixed simulated EDF thrust, in newtons.

	uORB::ResearchSubscription supervisor{ORB_ID(stabilized_grant_request)};
	auto *candidate = new Px4SessionConsumer(profile, supervisor.revision());

	if (!candidate || !candidate->start(argv[3])) { delete candidate; return PX4_ERROR; }

	// Retain the owner until process exit; stop is asynchronous and may wait
	// for remote disarm. No unsafe deletion on a stopped/blocked producer.
	consumer = candidate;
	return PX4_OK;
}
