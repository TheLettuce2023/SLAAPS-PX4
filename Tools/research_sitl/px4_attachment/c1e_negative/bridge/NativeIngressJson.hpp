#pragma once

#include "NativeIngressWorker.hpp"

#include <cstdint>

namespace research_sitl::native_ingress_json
{

struct Lifecycle {
	uint32_t bootstrap_count{};
	uint32_t claim_count{};
	uint64_t stack_size_bytes{};
	uint64_t stack_guard_bytes{};
	uint64_t stack_marker_offset_bytes{};
	uint64_t observation_window_us{};
	int commander_stop_result{-1};
	uint8_t bridge_fault{};
	bool source_done{};
	bool worker_joined{};
	bool stack_measured{};
	bool waited{};
	bool wait_completed{};
};

// Called only after Commander completion and acquired worker retirement.
// Returns false on any incomplete archive or sink failure.
bool writeArchive(const char *path, const NativeIngressDiagnosticOwner &owner,
		  const NativeIngressWorker &worker, const NativeIngressOwnerConfig &config,
		  const Lifecycle &lifecycle);

} // namespace research_sitl::native_ingress_json
