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
	_saved_apply = param_find("SLA_CFG_SAVED");
	_saved_use = param_find("SLA_CFG_SCOPE");
	_saved_identity[0] = param_find("SLA_CFG_ID_LO");
	_saved_identity[1] = param_find("SLA_CFG_ID_HI");
	_initialized = valid && _apply != PARAM_INVALID && _use != PARAM_INVALID
		       && _saved_apply != PARAM_INVALID && _saved_use != PARAM_INVALID
		       && _saved_identity[0] != PARAM_INVALID && _saved_identity[1] != PARAM_INVALID;
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
		valid = (param_get(_saved_apply, &candidate.saved_apply) == 0) && valid;
		valid = (param_get(_saved_use, &candidate.saved_use) == 0) && valid;
		valid = (param_get(_saved_identity[0], &candidate.saved_identity[0]) == 0) && valid;
		valid = (param_get(_saved_identity[1], &candidate.saved_identity[1]) == 0) && valid;
		elapsed = hrt_absolute_time() - start;
	}

	duration_us = elapsed <= UINT32_MAX ? static_cast<uint32_t>(elapsed) : UINT32_MAX;

	if (!valid) { return false; }

	snapshot = candidate;
	return true;
}

bool Px4ConfigurationSource::remembered(const ParameterSnapshot &snapshot)
{
	const uint64_t identity = static_cast<uint32_t>(snapshot.saved_identity[0])
				  | (static_cast<uint64_t>(static_cast<uint32_t>(snapshot.saved_identity[1])) << 32);
	return snapshot.use >= 0 && snapshot.use <= static_cast<int32_t>(ConfigurationUse::EdfCommissioning)
	       && snapshot.saved_apply > 0 && snapshot.saved_apply == snapshot.apply_counter
	       && snapshot.saved_use == snapshot.use && identity == fingerprint(snapshot.configuration);
}

bool Px4ConfigurationSource::remember(const ParameterSnapshot &snapshot) const
{
	if (!_initialized || snapshot.apply_counter <= 0 || snapshot.use < 0
	    || snapshot.use > static_cast<int32_t>(ConfigurationUse::EdfCommissioning)) { return false; }

	const uint64_t identity = fingerprint(snapshot.configuration);
	const int32_t low = static_cast<int32_t>(identity & UINT32_MAX);
	const int32_t high = static_cast<int32_t>(identity >> 32);
	const int32_t invalid = 0;
	// Clear the commit marker first and write it last. Restoration requires the
	// complete fingerprint/scope/counter match, including after interrupted writes.
	// Normal PX4 autosave supplies durability; success here is not a media flush.
	// These markers confer neither output permission nor vehicle qualification.
	return param_set(_saved_apply, &invalid) == 0
	       && param_set(_saved_identity[0], &low) == 0
	       && param_set(_saved_identity[1], &high) == 0
	       && param_set(_saved_use, &snapshot.use) == 0
	       && param_set(_saved_apply, &snapshot.apply_counter) == 0;
}
} // namespace slaaps
