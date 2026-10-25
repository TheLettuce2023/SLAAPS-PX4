#pragma once

#include "ManualWorkerTransport.hpp"

namespace slaaps
{

constexpr uint32_t kSupervisorIdleIntervalUs = 100000;

// A configured companion session also needs observation while outputs are
// inactive. Leave time for the next dispatch before its original leases end;
// this requested cadence is not proof of runtime scheduling performance.
inline uint32_t companionScheduleInterval(const VehicleConfiguration &configuration)
{
	if (!companionAdmissionConfigured(configuration) || configuration.selected_status_age_us <= 0
	    || configuration.actuator_permission_age_us <= 0) { return kSupervisorIdleIntervalUs; }

	const uint32_t shortest = math::min(static_cast<uint32_t>(configuration.companion.maximum_age_us),
					    math::min(static_cast<uint32_t>(configuration.selected_status_age_us),
							    static_cast<uint32_t>(configuration.actuator_permission_age_us)));
	uint32_t interval = math::min(kSupervisorIdleIntervalUs, math::max(uint32_t{1}, shortest / 2U));
	uint64_t renewal_lead{};
	uint64_t observation_slack{};

	if (companionClockRenewalConfigured(configuration.companion, renewal_lead, &observation_slack)) {
		// Leave dispatch opportunities within the declared renewal window even
		// when the clock map is shorter than the other observation leases.
		interval = math::min(interval, math::max(uint32_t{1}, static_cast<uint32_t>(renewal_lead / 2U)));
		interval = math::min(interval, math::max(uint32_t{1}, static_cast<uint32_t>(observation_slack / 2U)));
	}

	return interval;
}

// The worker still enforces minimum/maximum integration times and output expiry.
// This is a wake-up request, not proof that the work queue meets its deadline.
inline uint32_t manualScheduleInterval(const VehicleConfiguration &configuration, bool active)
{
	if (!active) { return kSupervisorIdleIntervalUs; }

	if (configuration.cycle_min_us <= 0 || configuration.cycle_max_us <= configuration.cycle_min_us) { return 0; }

	return math::min(static_cast<uint32_t>(configuration.cycle_min_us), kSupervisorIdleIntervalUs);
}

inline bool supervisorMaintenanceDue(uint64_t now, uint64_t previous)
{
	return !previous || now < previous || now - previous >= kSupervisorIdleIntervalUs;
}

// Called on the same serialized queue that owns configuration and admission.
// Permission is supplied by admission; this function never refreshes its expiry.
inline ManualWorkerStatus executeManual(ManualWorkerTransport &worker, uint64_t now,
					bool configuration_eligible, bool manual_mode_selected, const EdfPermission &permission,
					const SelectedManualInput::Snapshot &snapshot, const ManualOutputBinding &binding)
{
	if (!worker.active()) { return ManualWorkerStatus::Inactive; }

	if (!configuration_eligible || !manual_mode_selected || !worker.commandRevision()) {
		worker.stop();
		return ManualWorkerStatus::ControlRejected;
	}

	return worker.cycle(now, permission, snapshot, binding);
}

} // namespace slaaps
