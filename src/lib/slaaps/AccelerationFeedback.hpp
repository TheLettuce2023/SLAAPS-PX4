#pragma once

#include "VehicleConfiguration.hpp"

namespace slaaps
{

enum class AccelerationFeedbackResult : uint8_t {
	Disabled, Ready, Settling, InvalidConfiguration, InvalidMetadata, Stale, Skew,
	StateRejected, SourceChanged, InvalidInput, ArithmeticFailure, LimitExceeded
};

// Already copied by the selected-state owner. No uORB, pilot, authority or
// clock reader is hidden here. Current raw/bias booleans are independent live
// lifetime checks, not the acceleration publisher's historical assertions.
struct AccelerationFeedbackInput {
	uint64_t observed_us{}, timestamp_us{}, timestamp_sample_us{}, source_generation{};
	uint32_t publisher_revision{}, device_id{}, selected_accel_device_id{}, selected_gyro_device_id{};
	uint32_t bias_publisher_revision{}, raw_publisher_revision{};
	float filter_cutoff_hz{}, filter_sample_rate_hz{};
	uint64_t bias_timestamp{}, bias_timestamp_sample{};
	uint8_t bias_instance{}, raw_instance{};
	bool calibrated{}, bias_valid{}, bias_publisher_current{}, raw_publisher_current{};
	research_stabilized_manual::Vector3f specific_force_frd_m_s2{};
	research_stabilized_manual::StateSample state{};
};

struct AccelerationFeedbackSample {
	uint64_t observed_us{}, timestamp_sample_us{}, deadline_us{}, source_generation{};
	uint32_t publisher_revision{}, device_id{};
	uint32_t state_source_epoch{};
	uint8_t attitude_reset_counter{};
	research_stabilized_manual::Vector3f acceleration_frd_m_s2{};
};

// Filtered selected-IMU specific force -> inertial CG acceleration in body FRD.
// r is exactly feedback.imu_position (CG to IMU), never an additional cg offset.
// This is a latency-bounded estimate, not an instantaneous/qualified measurement.
// Every rejection preserves destination and resets settling. SourceChanged
// additionally discards prior-source history for the protocol owner to revoke.
// Fresh post-settling samples may recover observation; no session is restored.
class AccelerationFeedback
{
public:
	bool configure(const VehicleConfiguration &configuration);
	AccelerationFeedbackResult evaluate(uint64_t now, const AccelerationFeedbackInput &input,
					    AccelerationFeedbackSample &destination);
	void stop();
private:
	struct Source {
		uint64_t generation{};
		uint32_t publisher{}, device{}, accel{}, gyro{}, bias{}, raw{}, state_epoch{};
		uint8_t bias_instance{}, raw_instance{}, attitude_reset{};
		float cutoff{}, sample_rate{};
	};
	static Source source(const AccelerationFeedbackInput &input);
	static bool sameSource(const Source &left, const Source &right);
	AccelerationFeedbackResult reject(AccelerationFeedbackResult result);
	AccelerationFeedbackConfiguration _configuration{};
	research_stabilized_manual::Configuration _numerical{};
	AccelerationFeedbackInput _previous{};
	Source _source{};
	uint64_t _clock{}, _settling_started{};
	bool _configured{}, _source_bound{}, _have_previous{};
};

} // namespace slaaps
