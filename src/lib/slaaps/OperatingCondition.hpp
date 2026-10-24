#pragma once

#include <cstdint>

namespace slaaps
{

// Raw airspeed does not carry a device ID. Its explicitly selected uORB
// instance and advertisement revision are the available source identity.
struct OperatingConditionSourceIdentity {
	uint32_t airspeed_revision{};
	uint32_t air_data_revision{};
	uint32_t battery_revision{};
	uint32_t baro_device_id{};
	uint8_t airspeed_instance{};
	uint8_t air_data_instance{};
	uint8_t battery_instance{};
	uint8_t baro_calibration_count{};
	// PX4 category: 0 default temperature, 1 external baro, 2 airspeed.
	// The upstream default and computed density do not qualify measurements.
	uint8_t temperature_source{};
	uint8_t battery_id{};
	uint8_t battery_source{};
};

inline bool sameOperatingConditionSource(const OperatingConditionSourceIdentity &left,
		const OperatingConditionSourceIdentity &right)
{
	return left.airspeed_revision == right.airspeed_revision && left.air_data_revision == right.air_data_revision
	       && left.battery_revision == right.battery_revision && left.baro_device_id == right.baro_device_id
	       && left.airspeed_instance == right.airspeed_instance && left.air_data_instance == right.air_data_instance
	       && left.battery_instance == right.battery_instance && left.baro_calibration_count == right.baro_calibration_count
	       && left.temperature_source == right.temperature_source && left.battery_id == right.battery_id
	       && left.battery_source == right.battery_source;
}

// Sensor observations only: no EDF command coordinate, angle observation,
// qualified envelope, flight health or output permission is supplied here.
struct OperatingConditionSample {
	uint64_t observed_us{};
	uint64_t airspeed_timestamp_us{};
	uint64_t airspeed_sample_us{};
	uint64_t air_data_timestamp_us{};
	uint64_t air_data_sample_us{};
	uint64_t battery_timestamp_us{}; // BatteryStatus has no sample timestamp.
	uint64_t deadline_us{}; // Exclusive; bounded by every original sensor clock.
	float indicated_airspeed_m_s{};
	float density_kg_m3{};
	float voltage_v{};
	OperatingConditionSourceIdentity source{};
};

} // namespace slaaps
