#pragma once

#include <lib/slaaps/VehicleConfiguration.hpp>
#include <parameters/param.h>

namespace slaaps
{

struct ParameterSnapshot {
	VehicleConfiguration configuration{};
	int32_t apply_counter{};
	int32_t use{};
};

// A complete read under the same lock used by the parameter storage layers.
// Resolve/mark handles before capture; no allocation, parsing or validation
// belongs inside the NuttX interrupt critical section.
class Px4ConfigurationSource
{
public:
	bool initialize();
	bool capture(ParameterSnapshot &snapshot, uint32_t &duration_us) const;
private:
	static constexpr unsigned kParameterCount = 0
#define SLAAPS_FLOAT(field, name, group, description, unit, minimum, maximum, initial) + 1
#define SLAAPS_INT SLAAPS_FLOAT
#include <lib/slaaps/VehicleParameters.def>
#undef SLAAPS_INT
#undef SLAAPS_FLOAT
			;
	param_t _handles[kParameterCount] {};
	param_t _apply{PARAM_INVALID};
	param_t _use{PARAM_INVALID};
	bool _initialized{};
};

} // namespace slaaps
