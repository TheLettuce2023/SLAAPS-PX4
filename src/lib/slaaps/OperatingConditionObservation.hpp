#pragma once

#include "OperatingCondition.hpp"
#include "VehicleConfiguration.hpp"
#include <drivers/drv_hrt.h>
#include <uORB/ResearchSubscription.hpp>
#include <uORB/topics/airspeed.h>
#include <uORB/topics/battery_status.h>
#include <uORB/topics/vehicle_air_data.h>
#include <cmath>

namespace slaaps
{

// Serialized sensor adapter. The worker binds source identity per session and
// checks every capture, including while a handled-command ACK is pending.
class OperatingConditionObservation
{
public:
	OperatingConditionObservation() = default;
	OperatingConditionObservation(const OperatingConditionObservation &) = delete;
	OperatingConditionObservation &operator=(const OperatingConditionObservation &) = delete;

	// Failure preserves destination. Disabled scheduling performs no subscription
	// or sensor reads. No alternate airspeed, density or voltage is substituted.
	bool capture(const ControlScheduleConfiguration &configuration, OperatingConditionSample &destination)
	{
		if (configuration.enabled != 1 || configuration.maximum_age_us <= 0
		    || configuration.maximum_age_us > kMaximumAgeUs || configuration.battery_id <= 0
		    || configuration.battery_id > UINT8_MAX
		    || !select(_airspeed, ORB_ID::airspeed, configuration.airspeed_instance)
		    || !select(_air_data, ORB_ID::vehicle_air_data, configuration.air_data_instance)
		    || !select(_battery, ORB_ID::battery_status, configuration.battery_instance)) { return false; }

		airspeed_s airspeed{};
		vehicle_air_data_s air_data{};
		battery_status_s battery{};
		unsigned airspeed_revision{}, air_data_revision{}, battery_revision{};
		const bool have = _airspeed.copyBound(&airspeed, airspeed_revision)
				  && _air_data.copyBound(&air_data, air_data_revision) && _battery.copyBound(&battery, battery_revision);
		// Each payload/revision pair is atomic. Recheck all lifetimes after the
		// sequential copies; this does not assert simultaneous measurements.
		const bool stable = have && airspeed_revision && air_data_revision && battery_revision
				    && airspeed_revision == _airspeed.revision() && air_data_revision == _air_data.revision()
				    && battery_revision == _battery.revision();
		const uint64_t now = hrt_absolute_time();

		if (!stable || !std::isfinite(airspeed.indicated_airspeed_m_s) || airspeed.indicated_airspeed_m_s < 0.F
		    || !std::isfinite(airspeed.confidence) || airspeed.confidence <= 0.F || airspeed.confidence > 1.F
		    || !std::isfinite(air_data.rho) || air_data.rho <= 0.F || !air_data.baro_device_id
		    || air_data.temperature_source > kMaximumTemperatureSource
		    || !battery.connected || battery.id != configuration.battery_id
		    || (battery.source != battery_status_s::SOURCE_POWER_MODULE && battery.source != battery_status_s::SOURCE_EXTERNAL
			&& battery.source != battery_status_s::SOURCE_ESCS)
		    || !std::isfinite(battery.voltage_v) || battery.voltage_v <= 0.F) { return false; }

		OperatingConditionSample candidate{};
		candidate.deadline_us = UINT64_MAX;
		const uint64_t maximum_age = static_cast<uint64_t>(configuration.maximum_age_us);

		if (!measured(now, airspeed.timestamp, airspeed.timestamp_sample, maximum_age, candidate.deadline_us)
		    || !measured(now, air_data.timestamp, air_data.timestamp_sample, maximum_age, candidate.deadline_us)
		    || !fresh(now, battery.timestamp, maximum_age, candidate.deadline_us)) { return false; }

		candidate.observed_us = now;
		candidate.airspeed_timestamp_us = airspeed.timestamp;
		candidate.airspeed_sample_us = airspeed.timestamp_sample;
		candidate.air_data_timestamp_us = air_data.timestamp;
		candidate.air_data_sample_us = air_data.timestamp_sample;
		candidate.battery_timestamp_us = battery.timestamp;
		candidate.indicated_airspeed_m_s = airspeed.indicated_airspeed_m_s;
		candidate.density_kg_m3 = air_data.rho;
		candidate.voltage_v = battery.voltage_v;
		candidate.source.airspeed_revision = airspeed_revision;
		candidate.source.air_data_revision = air_data_revision;
		candidate.source.battery_revision = battery_revision;
		candidate.source.baro_device_id = air_data.baro_device_id;
		candidate.source.airspeed_instance = _airspeed.get_instance();
		candidate.source.air_data_instance = _air_data.get_instance();
		candidate.source.battery_instance = _battery.get_instance();
		candidate.source.baro_calibration_count = air_data.calibration_count;
		candidate.source.temperature_source = air_data.temperature_source;
		candidate.source.battery_id = battery.id;
		candidate.source.battery_source = battery.source;
		destination = candidate;
		return true;
	}

private:
	static constexpr int32_t kMaximumAgeUs{1000000};
	// Defined by VehicleAirData: default, external barometer, or airspeed.
	// Default is permitted as reported; it confers no density qualification.
	static constexpr uint8_t kMaximumTemperatureSource{2};

	static bool select(uORB::ResearchSubscription &subscription, ORB_ID topic, int32_t instance)
	{
		if (instance < 0 || instance >= ORB_MULTI_MAX_INSTANCES) { return false; }

		const auto selected = static_cast<uint8_t>(instance);

		if (subscription.orb_id() != topic) {
			subscription = uORB::ResearchSubscription(topic, selected);
		}

		return subscription.get_instance() == selected || subscription.ChangeInstance(selected);
	}

	static bool fresh(uint64_t now, uint64_t timestamp, uint64_t maximum_age, uint64_t &deadline)
	{
		const uint64_t span = maximum_age + 1; // Inclusive age, exclusive deadline.

		if (!timestamp || timestamp > now || now - timestamp > maximum_age || timestamp > UINT64_MAX - span) { return false; }

		const uint64_t expiry = timestamp + span;
		deadline = deadline < expiry ? deadline : expiry;
		return true;
	}

	static bool measured(uint64_t now, uint64_t timestamp, uint64_t sample, uint64_t maximum_age, uint64_t &deadline)
	{
		return sample <= timestamp && fresh(now, timestamp, maximum_age, deadline) && fresh(now, sample, maximum_age, deadline);
	}

	// Subscription(topic) subscribes eagerly. INVALID keeps disabled profiles
	// independent of all three sensor publishers until the first enabled capture.
	uORB::ResearchSubscription _airspeed{ORB_ID::INVALID};
	uORB::ResearchSubscription _air_data{ORB_ID::INVALID};
	uORB::ResearchSubscription _battery{ORB_ID::INVALID};
};

} // namespace slaaps
