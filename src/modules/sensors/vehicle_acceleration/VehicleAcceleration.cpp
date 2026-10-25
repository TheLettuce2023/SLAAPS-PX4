/****************************************************************************
 *
 *   Copyright (c) 2019-2022 PX4 Development Team. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name PX4 nor the names of its contributors may be
 *    used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************/

#include "VehicleAcceleration.hpp"

#include <px4_platform_common/log.h>
#include <cstring>

#include <uORB/topics/vehicle_imu_status.h>

using namespace matrix;

namespace sensors
{

VehicleAcceleration::VehicleAcceleration() :
	ModuleParams(nullptr),
	ScheduledWorkItem(MODULE_NAME, px4::wq_configurations::nav_and_controllers)
{
	_vehicle_acceleration_pub.advertise();

	CheckAndUpdateFilters();
}

VehicleAcceleration::~VehicleAcceleration()
{
	Stop();
}

bool VehicleAcceleration::Start()
{
	// force initial updates
	ParametersUpdate(true);

	// sensor_selection needed to change the active sensor if the primary stops updating
	if (!_sensor_selection_sub.registerCallback()) {
		PX4_ERR("callback registration failed");
		return false;
	}

	if (!SensorSelectionUpdate(true)) {
		_sensor_sub.registerCallback();
	}

	return true;
}

void VehicleAcceleration::Stop()
{
	// clear all registered callbacks
	_sensor_sub.unregisterCallback();
	_sensor_selection_sub.unregisterCallback();

	Deinit();
}

void VehicleAcceleration::CheckAndUpdateFilters()
{
	bool sample_rate_changed = false;

	// get sample rate from vehicle_imu_status publication
	for (uint8_t i = 0; i < MAX_SENSOR_COUNT; i++) {
		uORB::SubscriptionData<vehicle_imu_status_s> imu_status{ORB_ID(vehicle_imu_status), i};

		const float sample_rate_hz = imu_status.get().accel_rate_hz;

		if (imu_status.advertised() && (imu_status.get().timestamp != 0)
		    && (imu_status.get().accel_device_id != 0) && (imu_status.get().accel_device_id == _calibration.device_id())
		    && PX4_ISFINITE(sample_rate_hz) && (sample_rate_hz > 0)) {

			// check if sample rate error is greater than 1%
			if (!PX4_ISFINITE(_filter_sample_rate) || (fabsf(sample_rate_hz - _filter_sample_rate) / _filter_sample_rate) > 0.01f) {
				PX4_DEBUG("sample rate changed: %.3f Hz -> %.3f Hz", (double)_filter_sample_rate, (double)sample_rate_hz);
				_filter_sample_rate = sample_rate_hz;
				sample_rate_changed = true;

				// determine number of sensor samples that will get closest to the desired rate
				if (_param_imu_integ_rate.get() > 0) {
					const float configured_interval_us = 1e6f / _param_imu_integ_rate.get();
					const float sample_interval_avg = 1e6f / sample_rate_hz;
					const uint8_t samples = math::constrain(roundf(configured_interval_us / sample_interval_avg), 1.f,
										(float)sensor_accel_s::ORB_QUEUE_LENGTH);

					_sensor_sub.set_required_updates(samples);

				} else {
					_sensor_sub.set_required_updates(1);
				}

				break;
			}
		}
	}

	// update software low pass filters
	if (sample_rate_changed || (fabsf(_lp_filter.get_cutoff_freq() - _param_imu_accel_cutoff.get()) > 0.1f)) {
		_lp_filter.set_cutoff_frequency(_filter_sample_rate, _param_imu_accel_cutoff.get());
		_lp_filter.reset(_acceleration_prev);
		AdvanceSourceGeneration();
	}
}

void VehicleAcceleration::SensorBiasUpdate(bool force)
{
	// find corresponding estimated sensor bias
	if (_estimator_selector_status_sub.updated()) {
		estimator_selector_status_s estimator_selector_status;

		if (_estimator_selector_status_sub.copy(&estimator_selector_status)) {
			_bias_selection_valid = _estimator_sensor_bias_sub.ChangeInstance(estimator_selector_status.primary_instance);
		}
	}

	const uint8_t instance = _estimator_sensor_bias_sub.get_instance();

	if (instance != _bias_source_instance) {
		_bias_source_instance = instance;
		AdvanceSourceGeneration();
	}

	estimator_sensor_bias_s bias{};
	unsigned revision{};
	bool copied = false;
#if defined(CONFIG_MODULES_SLAAPS)
	(void)force;
	// Copy payload and publication lifetime together, including held samples.
	// A replacement with no new payload must still invalidate provenance.
	copied = _estimator_sensor_bias_sub.copyBound(&bias, revision);
	const unsigned current_revision = _estimator_sensor_bias_sub.revision();

	if (current_revision != _bias_source_revision) {
		_bias_source_revision = current_revision;
		AdvanceSourceGeneration();
	}

	_bias_source_available = copied && revision && revision == current_revision;
#else

	if (_estimator_sensor_bias_sub.updated() || force) {
		copied = _estimator_sensor_bias_sub.copy(&bias);
	}

	_bias_source_available = _estimator_sensor_bias_sub.advertised();
#endif

	if (copied) {
		if (bias.accel_device_id == _calibration.device_id()) {
			_bias = Vector3f{bias.accel_bias};
			_bias_timestamp = bias.timestamp;
			_bias_timestamp_sample = bias.timestamp_sample;
			_bias_publisher_revision = revision;
			_bias_instance = instance;
			_bias_valid = bias.accel_bias_valid && _bias.isAllFinite();

		} else {
			_bias.zero();
			ClearBiasProvenance();
		}
	}
}

bool VehicleAcceleration::SensorSelectionUpdate(bool force)
{
	if (_sensor_selection_sub.updated() || (_calibration.device_id() == 0) || force) {
		sensor_selection_s sensor_selection{};
		_sensor_selection_sub.copy(&sensor_selection);

		if ((sensor_selection.accel_device_id != 0) && (_calibration.device_id() != sensor_selection.accel_device_id)) {
			for (uint8_t i = 0; i < MAX_SENSOR_COUNT; i++) {
				uORB::SubscriptionData<sensor_accel_s> sensor_accel_sub{ORB_ID(sensor_accel), i};

				const uint32_t device_id = sensor_accel_sub.get().device_id;

				if (sensor_accel_sub.advertised() && (sensor_accel_sub.get().timestamp != 0)
				    && (device_id != 0) && (device_id == sensor_selection.accel_device_id)) {

					if (_sensor_sub.ChangeInstance(i) && _sensor_sub.registerCallback()) {
						PX4_DEBUG("selected sensor changed %" PRIu32 " -> %" PRIu32 "", _calibration.device_id(), device_id);

						// clear bias and corrections
						_bias.zero();
						ClearBiasProvenance();

						_calibration.set_device_id(device_id, sensor_accel_sub.get().is_external);

						CheckAndUpdateFilters();

						return true;
					}
				}
			}

			PX4_ERR("unable to find or subscribe to selected sensor (%" PRIu32 ")", sensor_selection.accel_device_id);
			_calibration.set_device_id(0);
		}
	}

	return false;
}

void VehicleAcceleration::ParametersUpdate(bool force)
{
	// Check if parameters have changed
	if (_parameter_update_sub.updated() || force) {
		// clear update
		parameter_update_s param_update;
		_parameter_update_sub.copy(&param_update);

		updateParams();

		_calibration.ParametersUpdate();

		CheckAndUpdateFilters();
	}
}

void VehicleAcceleration::AdvanceSourceGeneration()
{
	if (_source_generation) {
		_source_generation = _source_generation == UINT64_MAX ? 0 : _source_generation + 1;
	}
}

void VehicleAcceleration::ClearBiasProvenance()
{
	_bias_timestamp = 0;
	_bias_timestamp_sample = 0;
	_bias_publisher_revision = 0;
	_bias_instance = UINT8_MAX;
	_bias_valid = false;
}

#if defined(CONFIG_MODULES_SLAAPS)
void VehicleAcceleration::UpdateRawSourceProvenance(unsigned revision)
{
	const uint8_t instance = _sensor_sub.get_instance();

	if (_raw_source_instance != instance || _raw_source_revision != revision) {
		_raw_source_instance = instance;
		_raw_source_revision = revision;
		AdvanceSourceGeneration();
	}
}
#endif

void VehicleAcceleration::UpdateSourceProvenance()
{
	const auto &offset = _calibration.offset();
	const auto &scale = _calibration.scale();
	const auto &rotation = _calibration.rotation();
	bool changed = _source_device_id != _calibration.device_id() || _source_calibrated != _calibration.calibrated()
		       || _source_calibration_count != _calibration.calibration_count();
	constexpr unsigned kAxes = 3;

	for (unsigned row = 0; row < kAxes; ++row) {
		changed |= std::memcmp(&_source_offset(row), &offset(row), sizeof(float)) != 0
			   || std::memcmp(&_source_scale(row), &scale(row), sizeof(float)) != 0;

		for (unsigned column = 0; column < kAxes; ++column) {
			changed |= std::memcmp(&_source_rotation(row, column), &rotation(row, column), sizeof(float)) != 0;
		}
	}

	if (_source_initialized && changed) { AdvanceSourceGeneration(); }

	_source_device_id = _calibration.device_id();
	_source_calibrated = _calibration.calibrated();
	_source_calibration_count = _calibration.calibration_count();
	_source_offset = offset;
	_source_scale = scale;
	_source_rotation = rotation;
	_source_initialized = true;
}

void VehicleAcceleration::Run()
{
	// backup schedule
	ScheduleDelayed(10_ms);

	// update corrections first to set _selected_sensor
	bool selection_updated = SensorSelectionUpdate();

	ParametersUpdate();

	_calibration.SensorCorrectionsUpdate(selection_updated);

	SensorBiasUpdate(selection_updated);

#if defined(CONFIG_MODULES_SLAAPS)
	// Observe loss even when there is no new raw payload. Never republish an old
	// numerical value with a new timestamp merely to announce a lifetime change.
	UpdateRawSourceProvenance(_sensor_sub.revision());
#endif

	// require valid sensor sample rate to run
	if (!PX4_ISFINITE(_filter_sample_rate)) {
		CheckAndUpdateFilters();

		if (!PX4_ISFINITE(_filter_sample_rate)) {
			return;
		}
	}

	UpdateSourceProvenance();

	// process all outstanding messages
	int sensor_sub_updates = 0;
	sensor_accel_s sensor_data;

	while (sensor_sub_updates < sensor_accel_s::ORB_QUEUE_LENGTH) {
#if defined(CONFIG_MODULES_SLAAPS)
		unsigned raw_revision {};

		if (!_sensor_sub.copyQueuedBound(sensor_data, raw_revision)) { break; }

		sensor_sub_updates++;
		UpdateRawSourceProvenance(raw_revision);
		const unsigned current_revision = _sensor_sub.revision();

		if (raw_revision != current_revision) {
			UpdateRawSourceProvenance(current_revision);
			continue;
		}

#else

		if (!_sensor_sub.update(&sensor_data)) { break; }

		sensor_sub_updates++;
#endif

		if (sensor_data.device_id != _calibration.device_id()) { continue; }

		const Vector3f accel_raw{sensor_data.x, sensor_data.y, sensor_data.z};

		if (accel_raw.isAllFinite()) {
			// Apply calibration and filter
			//  - calibration offsets, scale factors, and thermal scale (if available)
			//  - estimated in run bias (if available)
			//  - biquad low-pass filter
			const Vector3f accel_corrected = _calibration.Correct(accel_raw) - _bias;
			const Vector3f accel_filtered = _lp_filter.apply(accel_corrected);

			_acceleration_prev = accel_corrected;

			// publish once all new samples are processed
			if (!_sensor_sub.updated()) {
#if defined(CONFIG_MODULES_SLAAPS)
				const unsigned final_revision = _sensor_sub.revision();

				if (raw_revision != final_revision) {
					UpdateRawSourceProvenance(final_revision);
					return;
				}

#endif
				// Publish vehicle_acceleration
				vehicle_acceleration_s v_acceleration{};
				v_acceleration.device_id = sensor_data.device_id;
				v_acceleration.calibrated = _calibration.calibrated();
				v_acceleration.source_generation = _source_generation;
#if defined(CONFIG_MODULES_SLAAPS)
				v_acceleration.raw_publisher_revision = raw_revision;
				v_acceleration.raw_instance = _sensor_sub.get_instance();
#endif
				v_acceleration.filter_cutoff_hz = _lp_filter.get_cutoff_freq();
				v_acceleration.filter_sample_rate_hz = _lp_filter.get_sample_freq();
				v_acceleration.bias_timestamp = _bias_timestamp;
				v_acceleration.bias_timestamp_sample = _bias_timestamp_sample;
				v_acceleration.bias_publisher_revision = _bias_publisher_revision;
				v_acceleration.bias_instance = _bias_instance;
				v_acceleration.bias_valid = _bias_valid && _bias_selection_valid && _bias_source_available
							    && _bias_instance == _bias_source_instance;
#if defined(CONFIG_MODULES_SLAAPS)
				v_acceleration.bias_valid &= _bias_publisher_revision && _bias_publisher_revision == _bias_source_revision;
#endif
				v_acceleration.timestamp_sample = sensor_data.timestamp_sample;
				accel_filtered.copyTo(v_acceleration.xyz);
				v_acceleration.timestamp = hrt_absolute_time();
				_vehicle_acceleration_pub.publish(v_acceleration);

				return;
			}
		}
	}
}

void VehicleAcceleration::PrintStatus()
{
	PX4_INFO_RAW("[vehicle_acceleration] selected sensor: %" PRIu32 ", rate: %.1f Hz, estimated bias: [%.4f %.4f %.4f]\n",
		     _calibration.device_id(), (double)_filter_sample_rate,
		     (double)_bias(0), (double)_bias(1), (double)_bias(2));

	_calibration.PrintStatus();
}

} // namespace sensors
