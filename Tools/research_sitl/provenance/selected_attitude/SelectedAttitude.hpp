#pragma once

#if !defined(RESEARCH_P2G_SELECTOR_STATE) || !defined(RESEARCH_P2G_MULTI_SELECTOR) \
	|| !defined(RESEARCH_P2H_SELECTOR_DISPOSITION) || !defined(RESEARCH_P2I_SELECTED_ATTITUDE) \
	|| !defined(__PX4_POSIX) || !defined(__linux__) || defined(__PX4_NUTTX) \
	|| defined(ENABLE_LOCKSTEP_SCHEDULER) || defined(CONFIG_MUORB_APPS_SYNC_TIMESTAMP)
#error "P2I selected-attitude observations require the private P2H non-lockstep POSIX profile"
#endif

#include <cstdint>

struct SelectedAttitudeObservation {
	uint64_t sample_us{};
	uint64_t estimator_publication_us{};
	uint64_t selector_publication_us{};
	float source_quaternion[4] {};
	float selected_quaternion[4] {};
	float source_delta_quaternion[4] {};
	float selected_delta_quaternion[4] {};
	uint8_t selected_instance{};
	uint8_t source_instance{};
	uint8_t source_reset_counter{};
	uint8_t selected_reset_counter{};
	bool publication_succeeded{};
};

bool research_selected_attitude_begin(uint64_t run_id) noexcept;
void research_selected_attitude_selector_start(const void *selector) noexcept;
void research_selected_attitude_output(const void *selector,
				       const SelectedAttitudeObservation &observation) noexcept;
bool research_selected_attitude_prepare_stop() noexcept;
void research_selected_attitude_selector_end(const void *selector) noexcept;
bool research_selected_attitude_finish(const char *path) noexcept;
