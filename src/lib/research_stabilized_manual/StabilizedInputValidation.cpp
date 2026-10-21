#include "StabilizedInputValidation.hpp"

#include <cmath>
#include <cstring>

namespace research_stabilized_manual
{
namespace
{
bool boundedVector(const Vector3f &value, const Vector3f &limit)
{
	for (unsigned axis = 0; axis < 3; ++axis) {
		if (!std::isfinite(value(axis)) || std::fabs(value(axis)) > limit(axis)) { return false; }
	}

	return true;
}

bool fresh(TimeUs now, TimeUs publication, TimeUs sample, TimeUs age)
{
	return sample != 0 && publication >= sample && now >= publication && now - sample <= age;
}

template<typename T>
bool same(const T &left, const T &right, unsigned size)
{
	for (unsigned i = 0; i < size; ++i) {
		const float left_value = left(i), right_value = right(i);

		if (std::memcmp(&left_value, &right_value, sizeof(float)) != 0) { return false; }
	}

	return true;
}
}

research_actuator_command::ManualControlStatus configureStabilizedManualInput(const Configuration &configuration,
		research_actuator_command::ManualControlInput &input)
{
	research_actuator_command::ManualControlConfiguration mapping{};
	mapping.required_axis_mask = research_actuator_command::kAttitudeManualAxesMask;
	mapping.source_policy = research_actuator_command::ManualSourcePolicy::MavlinkOnly;
	mapping.maximum_sample_age_us = configuration.maximum_manual_age_us;
	mapping.mavlink_instance = configuration.mavlink_instance;
	mapping.mavlink_system_id = configuration.source_system_id;
	mapping.mavlink_component_id = configuration.source_component_id;

	// Unity maps reuse the existing raw-input validator only. Its mapped output
	// is discarded; it is not interpreted as a flight wrench or actuator demand.
	for (unsigned axis = 0; axis < 3; ++axis) { mapping.demand_map[axis][axis] = 1.0F; }

	return input.configure(mapping);
}

Status validateStabilizedState(TimeUs now, const StateSample &sample, const Configuration &configuration,
			       const StateSample *previous, Quaternion &attitude)
{
	if (!sample.valid || sample.source_epoch == 0
	    || !fresh(now, sample.attitude_timestamp_us, sample.attitude_sample_us, configuration.maximum_state_age_us)
	    || !fresh(now, sample.rate_timestamp_us, sample.rate_sample_us, configuration.maximum_state_age_us)) { return Status::StateRejected; }

	const auto skew = sample.attitude_sample_us > sample.rate_sample_us ? sample.attitude_sample_us - sample.rate_sample_us
			  : sample.rate_sample_us - sample.attitude_sample_us;

	if (skew > configuration.maximum_state_skew_us
	    || !boundedVector(sample.rate_rad_s, configuration.maximum_measured_rate_rad_s)
	    || !boundedVector(sample.angular_accel_rad_s2, configuration.maximum_measured_accel_rad_s2)) { return Status::StateRejected; }

	double norm_squared = 0.0;

	for (unsigned i = 0; i < 4; ++i) {
		if (!std::isfinite(sample.attitude(i))) { return Status::StateRejected; }

		norm_squared += static_cast<double>(sample.attitude(i)) * static_cast<double>(sample.attitude(i));
	}

	const double norm = std::sqrt(norm_squared);

	if (!std::isfinite(norm) || std::fabs(norm - 1.0) > static_cast<double>(configuration.quaternion_norm_tolerance)) { return Status::StateRejected; }

	if (previous != nullptr) {
		if (sample.source_epoch != previous->source_epoch || sample.attitude_reset_counter != previous->attitude_reset_counter) { return Status::StateChanged; }

		if (sample.attitude_sample_us < previous->attitude_sample_us || sample.rate_sample_us < previous->rate_sample_us
		    || sample.attitude_timestamp_us < previous->attitude_timestamp_us || sample.rate_timestamp_us < previous->rate_timestamp_us
		    || (sample.attitude_sample_us == previous->attitude_sample_us && !same(sample.attitude, previous->attitude, 4))
		    || (sample.rate_sample_us == previous->rate_sample_us && (!same(sample.rate_rad_s, previous->rate_rad_s, 3)
				    || !same(sample.angular_accel_rad_s2, previous->angular_accel_rad_s2, 3)))) { return Status::StateRejected; }
	}

	for (unsigned i = 0; i < 4; ++i) { attitude(i) = static_cast<float>(static_cast<double>(sample.attitude(i)) / norm); }

	return Status::Ok;
}

} // namespace research_stabilized_manual
