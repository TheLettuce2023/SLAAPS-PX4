#include "Px4ConfigurationSource.hpp"

#include <drivers/drv_hrt.h>
#include <lib/parameters/atomic_transaction.h>

#if defined(__PX4_NUTTX) && !defined(CONFIG_BUILD_FLAT)
#error SLAAPS parameter snapshots currently require a flat NuttX build
#endif

namespace slaaps
{
bool Px4ConfigurationSource::initialize()
{
	unsigned index = 0;
	bool valid = true;
#define SLAAPS_FLOAT(field, name, group, description, unit, minimum, maximum, initial) \
	_handles[index] = param_find(#name); \
	valid = valid && _handles[index] != PARAM_INVALID && param_type(_handles[index]) == PARAM_TYPE_FLOAT; \
	++index;
#define SLAAPS_INT(field, name, group, description, unit, minimum, maximum, initial) \
	_handles[index] = param_find(#name); \
	valid = valid && _handles[index] != PARAM_INVALID && param_type(_handles[index]) == PARAM_TYPE_INT32; \
	++index;
#include <lib/slaaps/VehicleParameters.def>
#undef SLAAPS_INT
#undef SLAAPS_FLOAT
	_apply = param_find("SLA_CFG_APPLY");
	_use = param_find("SLA_CFG_KIND");
	_initialized = valid && _apply != PARAM_INVALID && _use != PARAM_INVALID;
	return _initialized;
}

bool Px4ConfigurationSource::capture(ParameterSnapshot &snapshot, uint32_t &duration_us) const
{
	if (!_initialized) { return false; }

	ParameterSnapshot candidate{};
	unsigned index = 0;
	bool valid = true;
	hrt_abstime elapsed{};

	{
		const AtomicTransaction transaction;
		const hrt_abstime start = hrt_absolute_time();
#define SLAAPS_FLOAT(field, name, group, description, unit, minimum, maximum, initial) \
	valid = (param_get(_handles[index++], &candidate.configuration.field) == 0) && valid;
#define SLAAPS_INT SLAAPS_FLOAT
#include <lib/slaaps/VehicleParameters.def>
#undef SLAAPS_INT
#undef SLAAPS_FLOAT
		valid = (param_get(_apply, &candidate.apply_counter) == 0) && valid;
		valid = (param_get(_use, &candidate.use) == 0) && valid;
		elapsed = hrt_absolute_time() - start;
	}

	duration_us = elapsed <= UINT32_MAX ? static_cast<uint32_t>(elapsed) : UINT32_MAX;

	if (!valid) { return false; }

	snapshot = candidate;
	return true;
}
} // namespace slaaps
