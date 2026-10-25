#pragma once

#include "PublisherBinding.hpp"
#include "StabilizedManualControl.hpp"
#include <lib/research_actuator_command/Px4ManualControlInput.hpp>
#include <parameters/param.h>
#include <drivers/drv_hrt.h>
#include <uORB/topics/actuator_armed.h>
#include <uORB/topics/vehicle_attitude.h>
#include <uORB/topics/vehicle_angular_velocity.h>
#include <uORB/topics/vehicle_acceleration.h>
#include <uORB/topics/estimator_sensor_bias.h>
#include <uORB/topics/sensor_accel.h>
#include <uORB/topics/sensor_selection.h>
#include <uORB/topics/estimator_status.h>
#include <uORB/topics/estimator_status_flags.h>

namespace research_stabilized_manual
{

struct SelectedStateObservationSnapshot {
	ManualSample manual{};
	StateSample state{};
	TimeUs now{}, estimator_time{}, flags_time{};
	uint32_t gyro{}, accel{};
	unsigned revisions[PublisherBinding::kTopics] {};
	// Metadata/permission prerequisite only. Core numerical/manual checks,
	// operator grant, calibration acceptance and ownership remain separate.
	actuator_armed_s permission{};
	bool permission_fresh{};
	unsigned required_topics{PublisherBinding::kAllTopicsMask};
};

struct SelectedAccelerationObservationSnapshot {
	SelectedStateObservationSnapshot selected{};
	vehicle_acceleration_s acceleration{};
	unsigned acceleration_revision{};
	bool acceleration_stable{};
	bool bias_publisher_current{};
	bool raw_publisher_current{};
};

// Single-estimator observation only, in armed or disarmed operation.
// Metadata validity is not numerical validation, flight health or output permission.
// Calls belong to one serialized owner; publisher epochs must be bound at admission.
class SelectedStateObservation
{
public:
	SelectedStateObservation() = default;
	SelectedStateObservation(const SelectedStateObservation &) = delete;
	SelectedStateObservation &operator=(const SelectedStateObservation &) = delete;
	SelectedStateObservationSnapshot capture(TimeUs maximum_status_age_us, TimeUs maximum_permission_age_us)
	{
		return captureImpl(maximum_status_age_us, maximum_permission_age_us, true);
	}
	// Same selected estimator/permission observation without subscribing to,
	// copying or fabricating a pilot-input publisher. Manual capture is unchanged.
	SelectedStateObservationSnapshot captureState(TimeUs maximum_status_age_us, TimeUs maximum_permission_age_us)
	{
		return captureImpl(maximum_status_age_us, maximum_permission_age_us, false);
	}
	// Optional acceleration consumer. Existing Manual and state-only captures
	// neither subscribe to nor require this topic. Recheck all retained source
	// lifetimes after the extra copy, then evaluate metadata age at final HRT.
	SelectedAccelerationObservationSnapshot captureAccelerationState(TimeUs maximum_status_age_us,
			TimeUs maximum_permission_age_us)
	{
		if (_acceleration.orb_id() != ORB_ID::vehicle_acceleration) {
			_acceleration = PublisherSubscription(ORB_ID::vehicle_acceleration);
		}

		SelectedAccelerationObservationSnapshot result{};
		result.selected = captureImpl(maximum_status_age_us, maximum_permission_age_us, false);
		const bool copied = _acceleration.copyBound(&result.acceleration, result.acceleration_revision);

		// The supported observation uses one estimator (instance zero). Check
		// the applied bias lifetime independently: an old acceleration sample
		// must not survive a bias publisher disappearing or being replaced.
		if (_acceleration_bias.orb_id() != ORB_ID::estimator_sensor_bias) {
			_acceleration_bias = PublisherSubscription(ORB_ID::estimator_sensor_bias);
		}

		result.bias_publisher_current = copied && result.acceleration.bias_instance == 0
						&& result.acceleration.bias_publisher_revision
						&& result.acceleration.bias_publisher_revision == _acceleration_bias.revision();

		if (_acceleration_raw.orb_id() != ORB_ID::sensor_accel) {
			_acceleration_raw = PublisherSubscription(ORB_ID::sensor_accel);
		}

		// VehicleAcceleration supports four physical accelerometer instances.
		result.raw_publisher_current = copied && result.acceleration.raw_instance < 4
					       && _acceleration_raw.ChangeInstance(result.acceleration.raw_instance)
					       && result.acceleration.raw_publisher_revision
					       && result.acceleration.raw_publisher_revision == _acceleration_raw.revision();
		const auto &revision = result.selected.revisions;
		const bool stable = copied && result.acceleration_revision
				    && result.acceleration_revision == _acceleration.revision()
				    && revision[0] == _attitude.revision() && revision[1] == _rates.revision()
				    && revision[2] == _estimator.revision() && revision[3] == _flags.revision()
				    && revision[5] == _selection.revision() && revision[6] == _armed.revision();
		result.selected.now = hrt_absolute_time();
		result.selected.state.valid = result.selected.state.valid && stable
					      && fresh(result.selected.now, result.selected.estimator_time, maximum_status_age_us)
					      && fresh(result.selected.now, result.selected.flags_time, maximum_status_age_us);
		result.selected.permission_fresh = result.selected.permission_fresh && stable
						   && fresh(result.selected.now, result.selected.permission.timestamp, maximum_permission_age_us);
		result.acceleration_stable = stable;
		return result;
	}
private:
	SelectedStateObservationSnapshot captureImpl(TimeUs maximum_status_age_us, TimeUs maximum_permission_age_us,
			bool include_manual)
	{
		if (include_manual && _manual.orb_id() != ORB_ID::manual_control_setpoint) {
			_manual = PublisherSubscription(ORB_ID::manual_control_setpoint);
		}

		manual_control_setpoint_s manual{};
		vehicle_attitude_s attitude{};
		vehicle_angular_velocity_s rates{};
		sensor_selection_s selection{};
		estimator_status_s estimator{};
		estimator_status_flags_s flags{};
		actuator_armed_s armed{};
		SelectedStateObservationSnapshot result{};
		result.required_topics = include_manual ? PublisherBinding::kAllTopicsMask : PublisherBinding::kStateTopicsMask;
		auto &revisions = result.revisions;
		const bool have = _attitude.copyBound(&attitude, revisions[0]) && _rates.copyBound(&rates, revisions[1])
				  && _estimator.copyBound(&estimator, revisions[2]) && _flags.copyBound(&flags, revisions[3])
				  && (!include_manual || _manual.copyBound(&manual, revisions[4])) && _selection.copyBound(&selection, revisions[5])
				  && _armed.copyBound(&armed, revisions[6]);
		// Detect lifecycle changes across the sequential snapshot. This is not
		// a simultaneous multi-topic transaction or publisher authentication.
		const bool stable = have && revisions[0] == _attitude.revision() && revisions[1] == _rates.revision()
				    && revisions[2] == _estimator.revision() && revisions[3] == _flags.revision()
				    && (!include_manual || revisions[4] == _manual.revision()) && revisions[5] == _selection.revision()
				    && revisions[6] == _armed.revision();
		const uint32_t epoch = stable ? _binding.observe(revisions, result.required_topics) : 0;
		const bool supported = singleEstimator();
		result.now = hrt_absolute_time(); // H10: evaluate only after copying inputs.

		if (include_manual) { result.manual = research_actuator_command::manualControlSampleFromMessage(manual); }

		result.gyro = selection.gyro_device_id;
		result.accel = selection.accel_device_id;
		result.estimator_time = estimator.timestamp;
		result.flags_time = flags.timestamp;
		auto &sample = result.state;
		sample.attitude_timestamp_us = attitude.timestamp;
		sample.attitude_sample_us = attitude.timestamp_sample;
		sample.rate_timestamp_us = rates.timestamp;
		sample.rate_sample_us = rates.timestamp_sample;
		sample.attitude = Quaternion(attitude.q);
		sample.rate_rad_s = Vector3f(rates.xyz);
		sample.angular_accel_rad_s2 = Vector3f(rates.xyz_derivative);
		sample.attitude_reset_counter = attitude.quat_reset_counter;
		sample.source_epoch = epoch;
		sample.valid = have && epoch && supported && flags.cs_tilt_align && flags.cs_yaw_align
			       && !flags.fs_bad_hdg && !flags.fs_bad_acc_vertical
			       && result.gyro && result.accel && result.gyro == estimator.gyro_device_id && result.accel == estimator.accel_device_id
			       && maximum_status_age_us
			       && fresh(result.now, estimator.timestamp, maximum_status_age_us)
			       && fresh(result.now, flags.timestamp, maximum_status_age_us);
		result.permission = armed;
		result.permission_fresh = have && stable && maximum_permission_age_us
					  && fresh(result.now, armed.timestamp, maximum_permission_age_us);
		return result;
	}
	static bool fresh(TimeUs now, TimeUs timestamp, TimeUs maximum)
	{
		return timestamp && timestamp <= now && now - timestamp <= maximum;
	}
	static bool singleEstimator()
	{
		const char *names[] = {"EKF2_MULTI_IMU", "EKF2_MULTI_MAG", "SENS_IMU_MODE", "SENS_MAG_MODE"};
		const int32_t expected[] = {0, 0, 1, 1};

		for (unsigned i = 0; i < 4; ++i) {
			int32_t value{};
			const param_t handle = param_find(names[i]);

			if (handle == PARAM_INVALID || param_get(handle, &value) != 0 || value != expected[i]) { return false; }
		}

		return true;
	}
	PublisherSubscription _manual{ORB_ID::INVALID};
	PublisherSubscription _acceleration{ORB_ID::INVALID};
	PublisherSubscription _acceleration_bias{ORB_ID::INVALID};
	PublisherSubscription _acceleration_raw{ORB_ID::INVALID};
	PublisherSubscription _attitude{ORB_ID(vehicle_attitude)};
	PublisherSubscription _rates{ORB_ID(vehicle_angular_velocity)};
	PublisherSubscription _selection{ORB_ID(sensor_selection)};
	PublisherSubscription _estimator{ORB_ID(estimator_status)};
	PublisherSubscription _flags{ORB_ID(estimator_status_flags)};
	PublisherSubscription _armed{ORB_ID(actuator_armed)};
	PublisherBinding _binding{};
};

} // namespace research_stabilized_manual
