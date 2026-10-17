#pragma once

#if !defined(RESEARCH_P2G_SELECTOR_STATE) || !defined(RESEARCH_P2G_MULTI_SELECTOR) \
	|| !defined(RESEARCH_P2H_SELECTOR_DISPOSITION) || !defined(RESEARCH_P2I_SELECTED_ATTITUDE) \
	|| !defined(__PX4_POSIX) || !defined(__linux__) || defined(__PX4_NUTTX) \
	|| defined(ENABLE_LOCKSTEP_SCHEDULER) || defined(CONFIG_MUORB_APPS_SYNC_TIMESTAMP)
#error "P2I recorded system state requires the private P2H non-lockstep POSIX profile"
#endif

#include <cstdint>

bool research_recorded_state_load(const char *path) noexcept;
uint64_t research_recorded_state_first_imu_us() noexcept;
uint64_t research_recorded_state_step_us() noexcept;
bool research_recorded_state_publish_through(uint64_t source_us) noexcept;
bool research_recorded_state_complete() noexcept;
bool research_recorded_state_finish(const char *path) noexcept;
