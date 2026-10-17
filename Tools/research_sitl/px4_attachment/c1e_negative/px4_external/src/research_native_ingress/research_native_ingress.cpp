#include "NativeIngressSourceBridge.hpp"
#include "NativeIngressWorker.hpp"

#include <drivers/drv_hrt.h>
#include <px4_platform_common/log.h>

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <sys/stat.h>
#include <unistd.h>

namespace
{
using namespace research_sitl;
using namespace research_acceleration;

constexpr uint8_t kDiagnosticChannel = 1;
constexpr uint64_t kSigningLimit = uint64_t{1} << 48;

bool parsePositive(const char *text, uint64_t &value) noexcept
{
	if (!text || *text < '1' || *text > '9') { return false; }

	uint64_t parsed{};

	for (const char *cursor = text; *cursor; ++cursor) {
		if (*cursor < '0' || *cursor > '9') { return false; }

		const uint64_t digit = static_cast<uint64_t>(*cursor - '0');

		if (parsed > (std::numeric_limits<uint64_t>::max() - digit) / 10) { return false; }

		parsed = parsed * 10 + digit;
	}

	value = parsed;
	return true;
}

bool parsePort(const char *text, uint16_t &port) noexcept
{
	uint64_t parsed{};

	if (!parsePositive(text, parsed) || parsed > std::numeric_limits<uint16_t>::max()) { return false; }

	port = static_cast<uint16_t>(parsed);
	return true;
}

void clearKey(uint8_t (&key)[32]) noexcept
{
	volatile uint8_t *bytes = key;

	for (size_t index = 0; index < sizeof(key); ++index) { bytes[index] = 0; }
}

bool readOneTimeKey(const char *path, uint8_t (&key)[32]) noexcept
{
	if (!path || !*path) { return false; }

	const int fd = open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);

	if (fd < 0) { return false; }

	struct stat info {};

	const bool valid = fstat(fd, &info) == 0 && S_ISREG(info.st_mode)
			   && info.st_uid == geteuid() && (info.st_mode & 0777) == 0600
			   && info.st_nlink == 1 && info.st_size == static_cast<off_t>(sizeof(key));

	const ssize_t count = valid ? read(fd, key, sizeof(key)) : -1;

	const bool closed = close(fd) == 0;

	const bool unlinked = valid && count == static_cast<ssize_t>(sizeof(key)) && unlink(path) == 0;

	const bool accepted = valid && count == static_cast<ssize_t>(sizeof(key)) && closed && unlinked;

	if (!accepted) { clearKey(key); }

	return accepted;
}

int bootstrap(int argc, char *argv[]) noexcept
{
	NativeIngressOwnerConfig owner{};
	DiagnosticParserConfig parser{};
	uint16_t local_port{};
	uint64_t source{}, controller{}, owner_id{}, run{}, producer{}, clock{}, transport{};

	if (argc != 12 || !parsePositive(argv[2], source) || !parsePositive(argv[3], controller)
	    || !parsePositive(argv[4], owner_id) || !parsePositive(argv[5], run)
	    || !parsePositive(argv[6], producer) || !parsePositive(argv[7], clock)
	    || !parsePositive(argv[8], transport) || !parsePort(argv[9], local_port)
	    || !parsePort(argv[10], owner.peer_port) || local_port == owner.peer_port
	    || !readOneTimeKey(argv[11], parser.key)) {
		clearKey(parser.key);
		return NativeIngressSourceBridge::rejectBootstrap();
	}

	const uint64_t signing_seed = hrt_absolute_time() / 10;

	if (signing_seed == 0 || signing_seed >= kSigningLimit - 1) {
		clearKey(parser.key);
		return NativeIngressSourceBridge::rejectBootstrap();
	}

	owner.contract.permitted_source = source;
	owner.contract.protocol_version = kWireProtocolVersionV1;
	owner.contract.disarmed_policy = DisarmedPolicy::ValidateOnly;
	owner.contract.acceleration_bounds.minimum_m_s2 = Vec3f{-10.F, -10.F, -10.F};
	owner.contract.acceleration_bounds.maximum_m_s2 = Vec3f{10.F, 10.F, 10.F};
	owner.contract.acceleration_bounds.maximum_norm_m_s2 = 12.F;
	owner.contract.timing = {80000, 80000, 60000, 8000, 4000};
	owner.contract.quaternion_norm_tolerance = 0.01F;
	owner.adapter_identity.controller_instance_id = controller;
	owner.identity.owner_lifetime_id = owner_id;
	owner.identity.native_run_id = run;
	owner.identity.origin = kDiagnosticExecutionOrigin;
	owner.transport_binding_id = transport;
	owner.producer_lifetime_id = producer;
	owner.source_clock_id = clock;
	owner.channel = kDiagnosticChannel;
	parser.native_run_id = run;
	parser.owner_lifetime_id = owner_id;
	parser.controller_instance_id = controller;
	parser.source_id = source;
	parser.transport_session_id = transport;
	parser.signing_seed = signing_seed;
	parser.channel = kDiagnosticChannel;
	return NativeIngressSourceBridge::bootstrap(owner, parser, local_port);
}
} // namespace

extern "C" __EXPORT int research_native_ingress_main(int argc, char *argv[])
{
	if (argc >= 2 && strcmp(argv[1], "bootstrap") == 0) { return bootstrap(argc, argv); }

	if (argc == 2 && strcmp(argv[1], "checkpoint") == 0) {
		return NativeIngressSourceBridge::printCheckpoint();
	}

	if (argc == 3 && strcmp(argv[1], "wait") == 0) {
		uint64_t window{};

		if (!parsePositive(argv[2], window)) { return 1; }

		return NativeIngressSourceBridge::waitWindow(window);
	}

	if (argc == 3 && strcmp(argv[1], "stop_export") == 0) {
		return NativeIngressSourceBridge::stopExport(argv[2]);
	}

	if (argc == 3 && strcmp(argv[1], "export") == 0) {
		return NativeIngressSourceBridge::exportClosed(argv[2]);
	}

	PX4_ERR("usage: research_native_ingress bootstrap <source> <controller> <owner> <run> <producer> <clock> <transport> <local_port> <peer_port> <key_file> | checkpoint | wait <window_us> | stop_export <file> | export <file>");
	return 1;
}
