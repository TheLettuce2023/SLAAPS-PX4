#include "slaapsCheck.hpp"
#include <lib/slaaps/Airframe.hpp>

void SlaapsChecks::checkAndReport(const Context &context, Report &reporter)
{
	int32_t autostart{};

	if (_autostart != PARAM_INVALID && param_get(_autostart, &autostart) == 0 && autostart == slaaps::kAutostartId) {
		// Changing SYS_AUTOSTART during this boot cannot remove SLAAPS gates
		// while its controllers and driver mappings remain installed.
		_slaaps_airframe = true;
	}

	if (!_slaaps_airframe) { return; }

	slaaps_status_s status{};
	const hrt_abstime now = hrt_absolute_time();
	constexpr hrt_abstime maximum_age_us = 500000;
	const bool fresh = _status.copy(&status) && status.timestamp != 0 && now >= status.timestamp
			   && now - status.timestamp <= maximum_age_us;
	uint32_t known_modes{};

	if (fresh && status.direct_actuator_modes_configured) {
		for (int8_t id : status.mode_id) {
			if (id >= vehicle_status_s::NAVIGATION_STATE_EXTERNAL1 && id <= vehicle_status_s::NAVIGATION_STATE_EXTERNAL8) {
				known_modes |= 1U << id;
			}
		}
	}

	const uint32_t can_run = fresh ? status.can_run_mask & known_modes : 0;
	uint32_t can_arm = fresh ? status.can_arm_mask & can_run : 0;
	constexpr unsigned bench_index = 2;

	if (status.mode_id[bench_index] >= vehicle_status_s::NAVIGATION_STATE_EXTERNAL1
	    && status.mode_id[bench_index] <= vehicle_status_s::NAVIGATION_STATE_EXTERNAL8) {
		can_arm &= ~(1U << status.mode_id[bench_index]);

		if (context.isArmed()) {
			reporter.clearCanRunBits(static_cast<NavModes>(1U << status.mode_id[bench_index]));
		}
	}

	reporter.clearCanRunBits(static_cast<NavModes>(~can_run));
	reporter.clearArmingBits(static_cast<NavModes>(~can_arm));

	if ((can_arm & (1U << context.status().nav_state)) == 0) {
		const NavModes unavailable_modes = static_cast<NavModes>(~can_arm);
		/* EVENT
		 * @description
		 * SLAAPS requires a running supervisor, an explicitly applied vehicle
		 * configuration and an implemented output/admission path for this mode.
		 * Servo bench mode never permits arming. Stock flight modes are not
		 * substitutes for a SLAAPS controller.
		 */
		reporter.armingCheckFailure(unavailable_modes, health_component_t::system,
					    events::ID("check_slaaps_not_ready"), events::Log::Error,
					    "SLAAPS mode is not ready for arming");
	}
}
