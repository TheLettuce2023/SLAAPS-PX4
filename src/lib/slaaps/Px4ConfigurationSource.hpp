#pragma once

#include "VehicleConfiguration.hpp"
#include <parameters/param.h>

namespace slaaps
{

struct ParameterSnapshot {
	VehicleConfiguration configuration{};
	int32_t apply_counter{};
	int32_t use{};
	int32_t saved_apply{};
	int32_t saved_use{};
	int32_t saved_identity[2] {};
};

// A complete read under the same lock used by the parameter storage layers.
// Resolve/mark handles before capture; no allocation, parsing or validation
// belongs inside the NuttX interrupt critical section.
class Px4ConfigurationSource
{
public:
	bool initialize();
	bool capture(ParameterSnapshot &snapshot, uint32_t &duration_us) const;
	// Caller-owned scratch avoids a full snapshot on constrained task stacks.
	// Scratch must not alias the destination or be shared by concurrent callers.
	// A failed read may modify scratch, but never the destination.
	bool capture(ParameterSnapshot &snapshot, uint32_t &duration_us, ParameterSnapshot &scratch) const;
	bool remember(const ParameterSnapshot &snapshot) const;
	static bool remembered(const ParameterSnapshot &snapshot);
private:
	static constexpr unsigned kParameterCount = 0
#define SLAAPS_FLOAT(field, name, group, description, unit, minimum, maximum, initial) + 1
#define SLAAPS_INT SLAAPS_FLOAT
#include <lib/slaaps/VehicleParameters.def>
#undef SLAAPS_INT
#undef SLAAPS_FLOAT
			;
	param_t _handles[kParameterCount] {};
	param_t _imu_position[kAxes] {PARAM_INVALID, PARAM_INVALID, PARAM_INVALID};
	param_t _accel_cutoff{PARAM_INVALID};
	param_t _apply{PARAM_INVALID};
	param_t _use{PARAM_INVALID};
	param_t _saved_apply{PARAM_INVALID};
	param_t _saved_use{PARAM_INVALID};
	param_t _saved_identity[2] {PARAM_INVALID, PARAM_INVALID};
	bool _initialized{};
};

} // namespace slaaps
