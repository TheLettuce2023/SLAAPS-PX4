#pragma once

#if !defined(RESEARCH_P2G_SELECTOR_STATE) || !defined(RESEARCH_P2G_MULTI_SELECTOR) \
	|| !defined(__PX4_POSIX) || !defined(__linux__) || defined(__PX4_NUTTX) \
	|| defined(ENABLE_LOCKSTEP_SCHEDULER) || defined(CONFIG_MUORB_APPS_SYNC_TIMESTAMP)
#error "P2G selector-state observations require the private multi-EKF non-lockstep POSIX profile"
#endif

#include <cstdint>

struct SelectorSelectionObservation {
	uint64_t selection_us{};
	uint64_t last_instance_change_us{};
	uint32_t instance_changed_count{};
	uint32_t accel_device_id{};
	uint32_t gyro_device_id{};
	uint8_t old_instance{};
	uint8_t selected_instance{};
	uint8_t instances_available{};
	bool publication_succeeded{};
};

struct SelectorStatusObservation {
	uint64_t publication_us{};
	uint64_t last_instance_change_us{};
	uint32_t instance_changed_count{};
	uint32_t accel_device_id{};
	uint32_t gyro_device_id{};
	uint8_t selected_instance{};
	uint8_t instances_available{};
	bool raw_selected_healthy{};
	bool publication_succeeded{};
};

struct SelectorAttitudeObservation {
	uint64_t sample_us{};
	uint64_t estimator_publication_us{};
	uint64_t selector_publication_us{};
	uint8_t selected_instance{};
	uint8_t source_instance{};
	uint8_t source_reset_counter{};
	uint8_t selected_reset_counter{};
	bool publication_succeeded{};
};

struct SelectorLocalPositionObservation {
	uint64_t sample_us{};
	uint64_t estimator_publication_us{};
	uint64_t selector_publication_us{};
	float source_acceleration[3] {};
	float selected_acceleration[3] {};
	uint8_t selected_instance{};
	uint8_t source_instance{};
	uint8_t source_xy_reset_counter{};
	uint8_t source_z_reset_counter{};
	uint8_t source_vxy_reset_counter{};
	uint8_t source_vz_reset_counter{};
	uint8_t selected_xy_reset_counter{};
	uint8_t selected_z_reset_counter{};
	uint8_t selected_vxy_reset_counter{};
	uint8_t selected_vz_reset_counter{};
	bool publication_succeeded{};
};

bool research_selector_begin(uint64_t run_id) noexcept;
void research_selector_start(const void *selector) noexcept;
void research_selector_estimator_start(const void *estimator, int32_t estimator_instance,
				       int32_t attitude_publication_instance,
				       int32_t local_position_publication_instance,
				       int32_t status_publication_instance,
				       int32_t vehicle_imu_instance, bool replay, bool multi) noexcept;
void research_selector_selection(const void *selector,
				 const SelectorSelectionObservation &observation) noexcept;
void research_selector_status(const void *selector,
			      const SelectorStatusObservation &observation) noexcept;
void research_selector_attitude(const void *selector,
				const SelectorAttitudeObservation &observation) noexcept;
void research_selector_local_position(const void *selector,
				      const SelectorLocalPositionObservation &observation) noexcept;
bool research_selector_prepare_stop() noexcept;
void research_selector_end(const void *selector) noexcept;
void research_selector_estimator_end(const void *estimator) noexcept;
bool research_selector_finish(const char *path) noexcept;
