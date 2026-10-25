/****************************************************************************
 *
 *   Copyright (c) 2019 PX4 Development Team. All rights reserved.
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

#pragma once

#include <lib/sensor_calibration/Accelerometer.hpp>
#include <lib/mathlib/math/Limits.hpp>
#include <lib/matrix/matrix/math.hpp>
#include <lib/mathlib/math/filter/LowPassFilter2p.hpp>
#include <px4_platform_common/log.h>
#include <px4_platform_common/module_params.h>
#include <px4_platform_common/px4_config.h>
#include <px4_platform_common/px4_work_queue/ScheduledWorkItem.hpp>
#include <uORB/Publication.hpp>
#include <uORB/Subscription.hpp>
#include <uORB/SubscriptionCallback.hpp>
#if defined(CONFIG_MODULES_SLAAPS)
#include <uORB/ResearchSubscription.hpp>
#endif
#include <uORB/topics/estimator_selector_status.h>
#include <uORB/topics/estimator_sensor_bias.h>
#include <uORB/topics/parameter_update.h>
#include <uORB/topics/sensor_accel.h>
#include <uORB/topics/sensor_selection.h>
#include <uORB/topics/vehicle_acceleration.h>

using namespace time_literals;

namespace sensors
{

class VehicleAcceleration : public ModuleParams, public px4::ScheduledWorkItem
{
public:
	VehicleAcceleration();
	~VehicleAcceleration() override;

	bool Start();
	void Stop();

	void PrintStatus();

private:
	void Run() override;

	void CheckAndUpdateFilters();
	void ParametersUpdate(bool force = false);
	void SensorBiasUpdate(bool force = false);
	bool SensorSelectionUpdate(bool force = false);
	void UpdateSourceProvenance();
	void AdvanceSourceGeneration();
	void ClearBiasProvenance();
#if defined(CONFIG_MODULES_SLAAPS)
	void UpdateRawSourceProvenance(unsigned revision);

	// Keep the stock publication-count callback gate. Its composed subscription
	// is registration-only; this bound cursor is the sole numerical queue reader.
	class RawSubscription : public uORB::SubscriptionCallbackWorkItem
	{
	public:
		RawSubscription(px4::WorkItem *work_item, const orb_metadata *meta) :
			SubscriptionCallbackWorkItem(work_item, meta), _payload(meta) {}

		bool ChangeInstance(uint8_t instance)
		{
			return SubscriptionCallbackWorkItem::ChangeInstance(instance) && _payload.ChangeInstance(instance);
		}

		bool updated()
		{
			hrt_abstime last_update = _last_update.load();
			return _payload.advertised() && hrt_elapsed_time(&last_update) >= _interval_us.load() && _payload.updated();
		}

		bool copyQueuedBound(sensor_accel_s &sample, unsigned &revision)
		{
			revision = 0;

			if (!updated() || !_payload.copyQueuedBound(&sample, revision)) { return false; }

			// Preserve SubscriptionInterval's successful-copy interval accounting.
			const hrt_abstime now = hrt_absolute_time();
			const uint32_t interval_us = _interval_us.load();
			_last_update.store(now > interval_us ? math::constrain(_last_update.load() + interval_us,
					   now - interval_us, now) : now);
			return true;
		}

		unsigned revision() { return _payload.revision(); }

	private:
		uORB::ResearchSubscription _payload;
	};
#endif

	static constexpr int MAX_SENSOR_COUNT = 4;

	uORB::Publication<vehicle_acceleration_s> _vehicle_acceleration_pub{ORB_ID(vehicle_acceleration)};

	uORB::Subscription _estimator_selector_status_sub{ORB_ID(estimator_selector_status)};
#if defined(CONFIG_MODULES_SLAAPS)
	uORB::ResearchSubscription _estimator_sensor_bias_sub {ORB_ID(estimator_sensor_bias)};
#else
	uORB::Subscription _estimator_sensor_bias_sub {ORB_ID(estimator_sensor_bias)};
#endif

	uORB::SubscriptionInterval _parameter_update_sub{ORB_ID(parameter_update), 1_s};

	uORB::SubscriptionCallbackWorkItem _sensor_selection_sub{this, ORB_ID(sensor_selection)};
#if defined(CONFIG_MODULES_SLAAPS)
	RawSubscription _sensor_sub {this, ORB_ID(sensor_accel)};
	unsigned _raw_source_revision{};
	uint8_t _raw_source_instance{UINT8_MAX};
#else
	uORB::SubscriptionCallbackWorkItem _sensor_sub {this, ORB_ID(sensor_accel)};
#endif

	calibration::Accelerometer _calibration{};

	matrix::Vector3f _bias{};
	uint64_t _bias_timestamp{};
	uint64_t _bias_timestamp_sample{};
	unsigned _bias_publisher_revision{};
	unsigned _bias_source_revision{};
	uint8_t _bias_instance{UINT8_MAX};
	uint8_t _bias_source_instance{};
	bool _bias_valid{};
	bool _bias_source_available{};
	bool _bias_selection_valid{true};

	uint64_t _source_generation{1}; // Exhaustion sticks at zero; never reuse a generation.
	uint32_t _source_device_id{};
	matrix::Vector3f _source_offset{};
	matrix::Vector3f _source_scale{};
	matrix::Dcmf _source_rotation{};
	uint8_t _source_calibration_count{};
	bool _source_calibrated{};
	bool _source_initialized{};

	matrix::Vector3f _acceleration_prev{};

	float _filter_sample_rate{NAN};

	math::LowPassFilter2p<matrix::Vector3f> _lp_filter{};

	DEFINE_PARAMETERS(
		(ParamFloat<px4::params::IMU_ACCEL_CUTOFF>) _param_imu_accel_cutoff,
		(ParamInt<px4::params::IMU_INTEG_RATE>) _param_imu_integ_rate
	)
};

} // namespace sensors
