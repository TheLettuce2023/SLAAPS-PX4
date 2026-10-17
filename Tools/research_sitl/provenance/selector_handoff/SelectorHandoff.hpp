#pragma once

#if !defined(RESEARCH_P2J_SELECTOR_HANDOFF) || !defined(__PX4_POSIX) || !defined(__linux__) \
	|| defined(__PX4_NUTTX) || defined(ENABLE_LOCKSTEP_SCHEDULER) \
	|| defined(CONFIG_MUORB_APPS_SYNC_TIMESTAMP)
#error "P2J selector handoff evidence requires the private non-lockstep POSIX profile"
#endif

#include <cstdint>

struct HandoffEstimatorBinding {
	int8_t estimator_instance{};
	int8_t attitude_instance{};
	int8_t local_position_instance{};
	int8_t status_instance{};
	int8_t imu_instance{};
	bool replay_mode{};
	bool multi_mode{};
};

struct HandoffSelectionObservation {
	uint64_t selection_us{};
	uint32_t instance_changed_count{};
	uint32_t accelerometer_device_id{};
	uint32_t gyroscope_device_id{};
	uint8_t old_instance{};
	uint8_t new_instance{};
	uint8_t available_instances{};
	uint8_t requested_instance{};
	bool old_healthy{};
	bool old_timeout{};
	bool new_healthy{};
	bool new_timeout{};
	bool publication_succeeded{};
};

struct HandoffStatusObservation {
	uint64_t publication_us{};
	uint64_t last_instance_change_us{};
	uint32_t instance_changed_count{};
	uint32_t accelerometer_device_id{};
	uint32_t gyroscope_device_id{};
	float combined_test_ratio[2] {};
	uint8_t primary_instance{};
	uint8_t available_instances{};
	bool healthy[2] {};
	bool publication_succeeded{};
};

struct HandoffAttitudeObservation {
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

struct HandoffLocalPositionObservation {
	uint64_t sample_us{};
	uint64_t estimator_publication_us{};
	uint64_t selector_publication_us{};
	float source_acceleration[3] {};
	float selected_acceleration[3] {};
	uint8_t source_reset_counters[6] {};
	uint8_t selected_reset_counters[6] {};
	uint8_t selected_instance{};
	uint8_t source_instance{};
	bool publication_succeeded{};
};

bool research_handoff_begin(uint64_t run_id) noexcept;
void research_handoff_selector_start(const void *selector) noexcept;
void research_handoff_selector_end(const void *selector) noexcept;
void research_handoff_estimator_start(const void *estimator,
				      const HandoffEstimatorBinding &binding) noexcept;
void research_handoff_estimator_end(const void *estimator) noexcept;
void research_handoff_selection(const void *selector,
				const HandoffSelectionObservation &observation) noexcept;
void research_handoff_status(const void *selector,
			     const HandoffStatusObservation &observation) noexcept;
void research_handoff_attitude(const void *selector,
			       const HandoffAttitudeObservation &observation) noexcept;
void research_handoff_local_position(const void *selector,
				     const HandoffLocalPositionObservation &observation) noexcept;
bool research_handoff_prepare_stop() noexcept;
bool research_handoff_finish(const char *path) noexcept;
