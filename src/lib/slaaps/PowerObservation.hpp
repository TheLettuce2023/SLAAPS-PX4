#pragma once

#include "VehicleConfiguration.hpp"
#include <drivers/drv_hrt.h>
#include <uORB/ResearchSubscription.hpp>
#include <uORB/topics/battery_status.h>
#include <cmath>

namespace slaaps
{

enum class PowerObservationResult : uint8_t {
	InvalidConfiguration = 0, Valid = 1, Unavailable = 2, PublisherChanged = 3,
	IdentityMismatch = 4, Disconnected = 5, Warning = 6, Fault = 7,
	PoweringOff = 8, InvalidTimestamp = 9, InvalidVoltage = 10, VoltageOutOfRange = 11
};

struct PowerSourceIdentity {
	uint32_t publisher_revision{};
	uint8_t instance{};
	uint8_t id{};
	uint8_t source{};
};

inline bool samePowerSource(const PowerSourceIdentity &left, const PowerSourceIdentity &right)
{
	return left.publisher_revision == right.publisher_revision && left.instance == right.instance
	       && left.id == right.id && left.source == right.source;
}

struct PowerSample {
	uint64_t observed_us{};
	uint64_t timestamp_us{}; // BatteryStatus has no separate measurement timestamp.
	uint64_t deadline_us{}; // Exclusive original-report expiry, never renewed by polling.
	float voltage_v{};
	PowerSourceIdentity source{};
};

// One serialized battery observation. The caller binds identity and detects
// held-payload mutation per session; this adapter supplies no output authority.
class PowerObservation
{
public:
	PowerObservation() = default;
	PowerObservation(const PowerObservation &) = delete;
	PowerObservation &operator=(const PowerObservation &) = delete;

	// Failure preserves destination. A stable replacement is returned with its
	// new identity; only the session owner may decide whether to admit it.
	PowerObservationResult capture(const PowerConfiguration &configuration, PowerSample &destination)
	{
		if (!validatePowerConfiguration(configuration, true).valid()) { return PowerObservationResult::InvalidConfiguration; }

		const auto instance = static_cast<uint8_t>(configuration.battery_instance);

		if (_battery.orb_id() != ORB_ID::battery_status) {
			_battery = uORB::ResearchSubscription(ORB_ID::battery_status, instance);
		}

		if (_battery.get_instance() != instance && !_battery.ChangeInstance(instance)) { return PowerObservationResult::Unavailable; }

		battery_status_s battery{};
		unsigned revision{};

		if (!_battery.copyBound(&battery, revision) || !revision) { return PowerObservationResult::Unavailable; }

		const bool stable = revision == _battery.revision();
		const uint64_t now = hrt_absolute_time();

		if (!stable) { return PowerObservationResult::PublisherChanged; }

		if (battery.id != configuration.battery_id || battery.source != configuration.battery_source) {
			return PowerObservationResult::IdentityMismatch;
		}

		if (!battery.connected) { return PowerObservationResult::Disconnected; }

		if (battery.warning != battery_status_s::WARNING_NONE) { return PowerObservationResult::Warning; }

		if (battery.faults) { return PowerObservationResult::Fault; }

		if (battery.is_powering_off) { return PowerObservationResult::PoweringOff; }

		const auto maximum_age = static_cast<uint64_t>(configuration.maximum_age_us);
		const uint64_t span = maximum_age + 1; // Inclusive age, exclusive deadline.

		if (!battery.timestamp || battery.timestamp > now || now - battery.timestamp > maximum_age
		    || battery.timestamp > UINT64_MAX - span) { return PowerObservationResult::InvalidTimestamp; }

		if (!std::isfinite(battery.voltage_v) || battery.voltage_v <= 0.F) { return PowerObservationResult::InvalidVoltage; }

		if (battery.voltage_v < configuration.voltage_min || battery.voltage_v > configuration.voltage_max) {
			return PowerObservationResult::VoltageOutOfRange;
		}

		PowerSample candidate{};
		candidate.observed_us = now;
		candidate.timestamp_us = battery.timestamp;
		candidate.deadline_us = battery.timestamp + span;
		candidate.voltage_v = battery.voltage_v;
		candidate.source.publisher_revision = revision;
		candidate.source.instance = instance;
		candidate.source.id = battery.id;
		candidate.source.source = battery.source;
		destination = candidate;
		return PowerObservationResult::Valid;
	}

private:
	static_assert(kPowerBatteryInstances == battery_status_s::MAX_INSTANCES, "Battery instance configuration bound changed");
	static_assert(battery_status_s::SOURCE_POWER_MODULE == 0 && battery_status_s::SOURCE_EXTERNAL == 1
		      &&battery_status_s::SOURCE_ESCS == 2, "Battery source configuration values changed");

	// INVALID keeps unconfigured profiles independent of battery publishers.
	uORB::ResearchSubscription _battery{ORB_ID::INVALID};
};

} // namespace slaaps
