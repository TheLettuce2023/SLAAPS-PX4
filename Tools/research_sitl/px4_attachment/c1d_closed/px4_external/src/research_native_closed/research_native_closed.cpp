#include "NativeClosedOwnerSourceBridge.hpp"

#include <px4_platform_common/log.h>

#include <cstring>
#include <limits>

namespace
{
using namespace research_sitl;
using namespace research_acceleration;

bool parsePositive(const char *text, uint64_t &value) noexcept
{
	if (!text || *text < '1' || *text > '9') { return false; }

	uint64_t parsed{};

	for (const char *cursor = text; *cursor; ++cursor) {
		if (*cursor < '0' || *cursor > '9') { return false; }

		const auto digit = static_cast<uint64_t>(*cursor - '0');

		if (parsed > (std::numeric_limits<uint64_t>::max() - digit) / 10) { return false; }

		parsed = parsed * 10 + digit;
	}

	value = parsed;
	return true;
}

int bootstrap(int argc, char *argv[]) noexcept
{
	NativeClosedOwnerConfig config{};

	if (argc != 8 || !parsePositive(argv[2], config.contract.permitted_source)
	    || !parsePositive(argv[3], config.adapter_identity.controller_instance_id)
	    || !parsePositive(argv[4], config.identity.owner_lifetime_id)
	    || !parsePositive(argv[5], config.identity.native_run_id)
	    || !parsePositive(argv[6], config.producer_lifetime_id)
	    || !parsePositive(argv[7], config.clock_epoch)) {
		return NativeClosedOwnerSourceBridge::rejectBootstrap();
	}

	// Exact named closed-poll construction scaffolding; never operational defaults.
	config.contract.protocol_version = kWireProtocolVersionV1;
	config.contract.disarmed_policy = DisarmedPolicy::ValidateOnly;
	config.contract.acceleration_bounds.minimum_m_s2 = Vec3f{-10.F, -10.F, -10.F};
	config.contract.acceleration_bounds.maximum_m_s2 = Vec3f{10.F, 10.F, 10.F};
	config.contract.acceleration_bounds.maximum_norm_m_s2 = 12.F;
	config.contract.timing = {80000, 80000, 60000, 8000, 4000};
	config.contract.quaternion_norm_tolerance = 0.01F;
	return NativeClosedOwnerSourceBridge::bootstrap(config);
}
} // namespace

extern "C" __EXPORT int research_native_closed_main(int argc, char *argv[])
{
	if (argc >= 2 && strcmp(argv[1], "bootstrap") == 0) { return bootstrap(argc, argv); }

	if (argc == 3 && strcmp(argv[1], "wait") == 0) {
		uint64_t window{};

		if (!parsePositive(argv[2], window)) { return 1; }

		return NativeClosedOwnerSourceBridge::waitWindow(window);
	}

	if (argc == 3 && strcmp(argv[1], "stop_export") == 0) {
		return NativeClosedOwnerSourceBridge::stopExport(argv[2]);
	}

	if (argc == 3 && strcmp(argv[1], "export") == 0) {
		return NativeClosedOwnerSourceBridge::exportClosed(argv[2]);
	}

	PX4_ERR("usage: research_native_closed bootstrap <source> <controller> <owner> <run> <producer> <clock> | wait <window_us> | stop_export <file> | export <file>");
	return 1;
}
