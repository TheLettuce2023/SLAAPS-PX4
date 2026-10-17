#pragma once

#include <lib/research_acceleration/NativeIngressDiagnosticOwner.hpp>

#if !defined(RESEARCH_C1E_NEGATIVE_OWNER_TARGET) || !defined(RESEARCH_SITL_DIAGNOSTIC_NATIVE) \
	|| !defined(__linux__) || defined(__PX4_NUTTX) || defined(ENABLE_LOCKSTEP_SCHEDULER)
#error "C1E Commander bridge requires the private native POSIX SITL target"
#endif

namespace research_sitl
{

struct DiagnosticParserConfig;

// Process-lifetime owner and worker. Commander source hooks have no socket,
// parser, sink or control-lock access.
class NativeIngressSourceBridge final
{
public:
	NativeIngressSourceBridge() noexcept;
	~NativeIngressSourceBridge();
	NativeIngressSourceBridge(const NativeIngressSourceBridge &) = delete;
	NativeIngressSourceBridge &operator=(const NativeIngressSourceBridge &) = delete;
	void initial(RawState state) noexcept;
	void transition(RawState before, RawState after) noexcept;
	void sample(RawState state) noexcept;
	void boundary(RawState current) noexcept;
	void finishSource(RawState state) noexcept;

	// Control-thread entry points. bootstrap erases parser_config.key on every path.
	static int bootstrap(const NativeIngressOwnerConfig &owner_config,
			     DiagnosticParserConfig &parser_config, uint16_t local_port) noexcept;
	static int rejectBootstrap() noexcept;
	static int printCheckpoint() noexcept;
	static int waitWindow(uint64_t window_us) noexcept;
	static int stopExport(const char *path);
	static int exportClosed(const char *path);

private:
	bool sourceReady(RawState current) noexcept;
	const bool claimed_;
};

} // namespace research_sitl
