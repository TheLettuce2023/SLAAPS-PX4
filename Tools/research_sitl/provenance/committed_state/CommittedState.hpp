#pragma once

#if !defined(RESEARCH_P2E_COMMITTED_STATE) || !defined(RESEARCH_P2D_NATIVE) \
	|| !defined(__PX4_POSIX) || !defined(__linux__) || defined(__PX4_NUTTX) \
	|| defined(ENABLE_LOCKSTEP_SCHEDULER) || defined(CONFIG_MUORB_APPS_SYNC_TIMESTAMP)
#error "P2E committed-state observations require the private P2D non-lockstep POSIX profile"
#endif

#include <cstdint>

struct CommittedAttitudeObservation {
	uint64_t sample_us{};
	float quaternion[4] {};
	uint8_t quaternion_reset_count{};
};

struct CommittedLocalPositionObservation {
	uint64_t sample_us{};
	float position_ned_m[3] {};
	float velocity_ned_m_s[3] {};
	float acceleration_ned_m_s2[3] {};
	bool horizontal_position_valid{};
	bool horizontal_velocity_valid{};
	bool vertical_position_valid{};
	bool vertical_velocity_valid{};
	uint8_t horizontal_position_reset_count{};
	uint8_t horizontal_velocity_reset_count{};
	uint8_t vertical_position_reset_count{};
	uint8_t vertical_velocity_reset_count{};
	uint8_t heading_reset_count{};
};

struct EstimatorCommitObservation {
	uint64_t sample_us{};
	uint64_t control_flags{};
	uint32_t fault_flags{};
	uint32_t accelerometer_device_id{};
	uint32_t gyroscope_device_id{};
	uint8_t accelerometer_calibration_count{};
	uint8_t gyroscope_calibration_count{};
	uint8_t quaternion_reset_count{};
	uint8_t horizontal_position_reset_count{};
	uint8_t horizontal_velocity_reset_count{};
	uint8_t vertical_position_reset_count{};
	uint8_t vertical_velocity_reset_count{};
	bool update_committed{};
	bool replay{};
	bool multi{};
	bool attitude_valid{};
	bool horizontal_position_valid{};
	bool horizontal_velocity_valid{};
	bool vertical_position_valid{};
	bool vertical_velocity_valid{};
};

void research_committed_source_start(const void *instance, bool replay, bool multi) noexcept;
void research_committed_source_end(const void *instance) noexcept;
void research_committed_attitude(const void *instance, const CommittedAttitudeObservation &value) noexcept;
void research_committed_local_position(const void *instance,
				       const CommittedLocalPositionObservation &value) noexcept;
void research_committed_iteration(const void *instance, const EstimatorCommitObservation &value) noexcept;
bool research_committed_begin(uint64_t run_id) noexcept;
bool research_committed_prepare_stop() noexcept;
bool research_committed_finish(const char *path) noexcept;
