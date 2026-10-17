#pragma once

#if !defined(RESEARCH_P2F_SELECTED_STATE) || !defined(RESEARCH_P2E_COMMITTED_STATE) \
	|| !defined(RESEARCH_P2D_NATIVE) || !defined(__PX4_POSIX) || !defined(__linux__) \
	|| defined(__PX4_NUTTX) || defined(ENABLE_LOCKSTEP_SCHEDULER) \
	|| defined(CONFIG_MUORB_APPS_SYNC_TIMESTAMP)
#error "P2F selected-state observations require the private P2E non-lockstep POSIX profile"
#endif

#include <cstdint>

struct EstimatorStatusPublicationObservation {
	uint64_t sample_us{};
	uint64_t publication_us{};
	uint64_t control_flags{};
	uint32_t fault_flags{};
	uint32_t control_status_changes{};
	uint32_t fault_status_changes{};
	int32_t estimator_instance{};
	int32_t publication_instance{};
	bool publication_succeeded{};
};

struct SelectedEstimatorIterationObservation {
	uint64_t sample_us{};
	uint64_t control_flags{};
	uint64_t published_control_flags{};
	uint32_t fault_flags{};
	uint32_t published_fault_flags{};
	uint32_t control_status_changes{};
	uint32_t fault_status_changes{};
	int32_t estimator_instance{};
	bool update_committed{};
	bool replay{};
	bool multi{};
};

bool research_selected_begin(uint64_t run_id) noexcept;
void research_selected_source_start(const void *instance, int32_t estimator_instance,
				    int32_t attitude_publication_instance,
				    int32_t local_position_publication_instance,
				    int32_t status_publication_instance, bool replay, bool multi) noexcept;
void research_selected_status(const void *instance,
			      const EstimatorStatusPublicationObservation &value) noexcept;
void research_selected_iteration(const void *instance,
				 const SelectedEstimatorIterationObservation &value) noexcept;
bool research_selected_prepare_stop() noexcept;
void research_selected_source_end(const void *instance) noexcept;
bool research_selected_finish(const char *path) noexcept;
