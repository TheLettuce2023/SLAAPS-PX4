#pragma once

#if !defined(RESEARCH_P2D_NATIVE) || !defined(__PX4_POSIX) || !defined(__linux__) \
    || defined(__PX4_NUTTX) || defined(ENABLE_LOCKSTEP_SCHEDULER) || defined(CONFIG_MUORB_APPS_SYNC_TIMESTAMP)
#error "P2D typed receipts require the private non-lockstep POSIX profile"
#endif

#include <cstdint>

// Typed value-copy at the EKF output boundary; not an admission health bundle.
struct EstimatorObservation {
	uint64_t sample_us{};
	bool replay{}, multi{}, attitude_valid{}, horizontal_valid{};
	bool vertical_position_valid{}, vertical_velocity_valid{};
	uint64_t control_flags{};
	uint32_t fault_flags{};
};

void research_merge_source_start(const void *instance, bool replay, bool multi) noexcept;
void research_merge_source_end(const void *instance) noexcept;
void research_merge_capture(const void *instance, const EstimatorObservation &value) noexcept;
bool research_merge_begin(uint64_t run_id) noexcept;
bool research_merge_pump() noexcept;
bool research_merge_prepare_stop() noexcept;
bool research_merge_finish(const char *path) noexcept;
