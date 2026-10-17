#pragma once

#include "NativeClosedDiagnosticOwner.hpp"

#if !defined(RESEARCH_SITL_DIAGNOSTIC_NATIVE) || RESEARCH_SITL_DIAGNOSTIC_NATIVE != 1
#error "The Commander source bridge requires actual native PX4 provenance"
#endif

namespace research_sitl
{

// One immutable process-lifetime archive. No owner handle or admission API.
class NativeClosedOwnerSourceBridge final
{
public:
	NativeClosedOwnerSourceBridge() noexcept;
	~NativeClosedOwnerSourceBridge();
	NativeClosedOwnerSourceBridge(const NativeClosedOwnerSourceBridge &) = delete;
	NativeClosedOwnerSourceBridge &operator=(const NativeClosedOwnerSourceBridge &) = delete;
	void initial(RawState state) noexcept;
	void transition(RawState before, RawState after) noexcept;
	void sample(RawState state) noexcept;
	void finishSource(RawState state) noexcept;

	// Control-thread entry points; never called by source/destructor.
	static int bootstrap(const NativeClosedOwnerConfig &config) noexcept;
	static int rejectBootstrap() noexcept;
	static int waitWindow(uint64_t window_us) noexcept;
	static int stopExport(const char *path);
	static int exportClosed(const char *path);

private:
	bool sourceReady() noexcept;
	const bool claimed_;
};

} // namespace research_sitl
