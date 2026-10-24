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
	constexpr hrt_abstime maximum_age_us = 500000;
	const bool copied = _status.copy(&status);
	const hrt_abstime now = hrt_absolute_time();
	const bool fresh = copied && status.timestamp != 0 && now >= status.timestamp
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
	const bool bench_mode_valid = status.mode_id[bench_index] >= vehicle_status_s::NAVIGATION_STATE_EXTERNAL1
				      && status.mode_id[bench_index] <= vehicle_status_s::NAVIGATION_STATE_EXTERNAL8;
	const bool commissioning_session = fresh && status.direct_actuator_modes_configured && bench_mode_valid
					   && status.configuration_valid && status.configuration_use == 2 && status.routing_snapshot_valid
					   && status.power_required && status.power_ready && status.power_source_bound && now < status.power_deadline
					   && status.bench_target == 5 && status.bench_edf_commissioning && !status.bench_disarm_pending
					   && status.manual_preparation_session && now < status.bench_session_deadline
					   && (status.bench_entry_status == slaaps_status_s::BENCH_ENTRY_WAIT_ARM
					       || status.bench_entry_status == slaaps_status_s::BENCH_ENTRY_ACTIVATING
					       || status.bench_entry_status == slaaps_status_s::BENCH_ENTRY_FIRST_COMMAND
					       || status.bench_entry_status == slaaps_status_s::BENCH_ENTRY_EDF_RUNNING);
	const bool commissioning_ready = commissioning_session && status.bench_commissioning_ready
					 && status.bench_entry_status == slaaps_status_s::BENCH_ENTRY_WAIT_ARM
					 && now < status.bench_commissioning_ready_deadline && now < status.manual_preparation_deadline
					 && !context.isArmed() && context.status().nav_state == status.mode_id[bench_index]
					 && context.status().nav_state_user_intention == status.mode_id[bench_index];

	if (bench_mode_valid) {
		if (!commissioning_ready) { can_arm &= ~(1U << status.mode_id[bench_index]); }

		if (context.isArmed() && !commissioning_session) {
			reporter.clearCanRunBits(static_cast<NavModes>(1U << status.mode_id[bench_index]));
		}
	}

	reporter.clearCanRunBits(static_cast<NavModes>(~can_run));
	reporter.clearArmingBits(static_cast<NavModes>(~can_arm));

	if (commissioning_ready || (commissioning_session && context.isArmed())) {
		const NavModesMessageFail advisory{static_cast<NavModes>(1U << status.mode_id[bench_index]), NavModes::None};

		if (commissioning_ready) {
			/* EVENT
			 * @description
			 * Ground EDF commissioning is explicitly enabled and both output banks are prepared. Use the normal operator arming action; all ordinary PX4 health checks still apply. Keep throttle low until the first zero command is handled. The original SLA_BENCH_SEC deadline continues while waiting.
			 */
			reporter.armingCheckFailure(advisory, health_component_t::system,
						    events::ID("check_slaaps_edf_arm"), events::Log::Info,
						    "EDF commissioning prepared: operator arm required");

		} else {
			/* EVENT
			 * @description
			 * This is bounded ground EDF commissioning, not flight admission. Canards remain parked at physical zero and the accepted SLA_EDF_BMAX ceiling limits throttle. Set SLA_BENCH_EN=0 to revoke output and request ordinary disarm.
			 */
			reporter.armingCheckFailure(advisory, health_component_t::system,
						    events::ID("check_slaaps_edf_active"), events::Log::Info,
						    "Armed ground EDF commissioning: bounded throttle");
		}

		return;
	}

	if ((can_arm & (1U << context.status().nav_state)) == 0) {
		const NavModes unavailable_modes = static_cast<NavModes>(~can_arm);
		auto report_power_failure = [&]() {
			/* EVENT
			 * @description
			 * Manual and EDF commissioning require the battery selected by SLA_PWR_INST, SLA_PWR_ID and SLA_PWR_SRC. Configure SLA_PWR_AGE and explicit SLA_PWR_VMIN/VMAX bounds; check fresh connected battery telemetry, warning/fault and power-off indications. A publisher replacement or changed held report rejects an active session. Resolve the fault, reapply if required, then leave and reselect the mode before deliberate enable. These checks do not qualify flight or infer thrust from voltage.
			 */
			reporter.armingCheckFailure(unavailable_modes, health_component_t::system,
						    events::ID("check_slaaps_power"), events::Log::Error,
						    "Check SLAAPS selected propulsion power");
		};

		const bool bench_selected = fresh && status.direct_actuator_modes_configured
					    && status.mode_id[bench_index] >= vehicle_status_s::NAVIGATION_STATE_EXTERNAL1
					    && status.mode_id[bench_index] <= vehicle_status_s::NAVIGATION_STATE_EXTERNAL8
					    && context.status().nav_state_user_intention == status.mode_id[bench_index];

		if (bench_selected) {
			if (status.configuration_use == 2 && status.power_required && !status.bench_disarm_pending
			    && (!status.power_ready || now >= status.power_deadline)) {
				report_power_failure();
				return;
			}

			switch (status.bench_entry_status) {
			case slaaps_status_s::BENCH_ENTRY_WAIT_DISARM:
			case slaaps_status_s::BENCH_ENTRY_DISARM_FAILED:
				/* EVENT
				 * @description
				 * Commissioning output has been revoked. Ordinary disarm commands are bounded and never forced. Completion requires both actual arming publications to confirm disarm; a command acknowledgment alone is insufficient. If disarm is declined, resolve the PX4 condition and use the normal operator disarm action.
				 */
				reporter.armingCheckFailure(unavailable_modes, health_component_t::system,
							    events::ID("check_slaaps_edf_disarm"), events::Log::Error,
							    "EDF commissioning stopped: confirm actual disarm");
				return;

			case slaaps_status_s::BENCH_ENTRY_WAIT_THROTTLE:
				/* EVENT
				 * @description
				 * The raw selected throttle must be at its calibrated low endpoint within SLA_EDF_ENTRY. This check occurs before applying the commissioning ceiling; a small SLA_EDF_BMAX cannot make high throttle safe for entry.
				 */
				reporter.armingCheckFailure(unavailable_modes, health_component_t::system,
							    events::ID("check_slaaps_edf_throttle"), events::Log::Info,
							    "Lower calibrated throttle for EDF commissioning");
				return;

			case slaaps_status_s::BENCH_ENTRY_WAIT_CONFIGURATION:
				/* EVENT
				 * @description
				 * Servo commissioning uses SLA_CFG_KIND=0 and SLA_BENCH_CH=0..4. Armed EDF commissioning requires accepted kind 2, channel 5, a positive SLA_EDF_BMAX ceiling, calibrated throttle endpoints and slew, explicit timing and a finite SLA_BENCH_SEC. No flight calibration is inferred.
				 */
				reporter.armingCheckFailure(unavailable_modes, health_component_t::system,
							    events::ID("check_slaaps_bench_configuration"), events::Log::Error,
							    "Apply SLAAPS servo profile and bench duration");
				return;

			case slaaps_status_s::BENCH_ENTRY_WAIT_ROUTING:
				/* EVENT
				 * @description
				 * Both supported PWM banks must report matching logical functions and calibration. Check MAIN/AUX function, endpoint, reversal and rate settings. An unassigned EDF is supported for servo-only commissioning.
				 */
				reporter.armingCheckFailure(unavailable_modes, health_component_t::system,
							    events::ID("check_slaaps_bench_routing"), events::Log::Error,
							    "Check SLAAPS bench MAIN/AUX routing and calibration");
				return;

			case slaaps_status_s::BENCH_ENTRY_WAIT_PREARM:
				/* EVENT
				 * @description
				 * Servo commissioning requires actual PX4 prearm permission while remaining disarmed. Check the safety switch and COM_PREARM_MODE. Do not arm to bypass this requirement; EDF remains inhibited.
				 */
				reporter.armingCheckFailure(unavailable_modes, health_component_t::system,
							    events::ID("check_slaaps_bench_prearm"), events::Log::Info,
							    "SLAAPS bench needs disarmed servo prearm permission");
				return;

			case slaaps_status_s::BENCH_ENTRY_WAIT_INPUT:
			case slaaps_status_s::BENCH_ENTRY_WAIT_CENTER:
				/* EVENT
				 * @description
				 * Provide fresh centered attitude-stick input from the configured RC or pinned MAVLink source after selecting Bench. Throttle cannot command EDF in this servo-only path.
				 */
				reporter.armingCheckFailure(unavailable_modes, health_component_t::system,
							    events::ID("check_slaaps_bench_input"), events::Log::Info,
							    "Center fresh configured input for SLAAPS bench");
				return;

			case slaaps_status_s::BENCH_ENTRY_READY:
				/* EVENT
				 * @description
				 * Fresh centered input is ready for deliberate SLA_BENCH_EN=1. Servo channels remain disarmed and prearmed; EDF channel 5 first prepares without output, then requires a separate normal operator arming action. SLA_BENCH_EN=0 stops either path and the original configured duration is never extended.
				 */
				reporter.armingCheckFailure(unavailable_modes, health_component_t::system,
							    events::ID("check_slaaps_bench_enable"), events::Log::Info,
							    "SLAAPS bench ready: set SLA_BENCH_EN to 1");
				return;

			case slaaps_status_s::BENCH_ENTRY_REJECTED:
				/* EVENT
				 * @description
				 * A configuration, input, authority or output fault stopped commissioning. Resolve the reported fault, reapply the profile if invalidated, then leave and reselect Bench before a new deliberate enable. Returning input cannot restart outputs.
				 */
				reporter.armingCheckFailure(unavailable_modes, health_component_t::system,
							    events::ID("check_slaaps_bench_rejected"), events::Log::Error,
							    "Resolve SLAAPS bench fault, then leave and reselect");
				return;

			case slaaps_status_s::BENCH_ENTRY_COMPLETE:
				/* EVENT
				 * @description
				 * The bounded servo session ended or was deliberately stopped. Leave and reselect Bench with fresh centered input before enabling another session. Saved parameters cannot restart it.
				 */
				reporter.armingCheckFailure(unavailable_modes, health_component_t::system,
							    events::ID("check_slaaps_bench_complete"), events::Log::Info,
							    "SLAAPS bench stopped: leave and reselect to enable");
				return;

			default:
				/* EVENT
				 * @description
				 * This Bench path supports disarmed, prearmed servo commissioning only. SLA_BENCH_EN=0 stops the session. EDF commissioning and flight admission are separate capabilities and are not enabled here.
				 */
				reporter.armingCheckFailure(unavailable_modes, health_component_t::system,
							    events::ID("check_slaaps_bench_servo_only"), events::Log::Info,
							    "SLAAPS servo bench: arming and EDF inhibited");
				return;
			}
		}

		const bool manual_selected = fresh && status.direct_actuator_modes_configured
					     && status.mode_id[0] >= vehicle_status_s::NAVIGATION_STATE_EXTERNAL1
					     && status.mode_id[0] <= vehicle_status_s::NAVIGATION_STATE_EXTERNAL8
					     && context.status().nav_state_user_intention == status.mode_id[0];

		if (manual_selected) {
			if (status.power_required && (!status.power_ready || now >= status.power_deadline)) {
				report_power_failure();
				return;
			}

			if (status.schedule_enabled && !status.schedule_inputs_valid) {
				/* EVENT
				 * @description
				 * The selected empirical schedule needs fresh raw indicated airspeed, air-density and connected-battery voltage observations. Check SLA_SCH_AS_INST, SLA_SCH_AD_INST, SLA_SCH_BAT_INST, SLA_SCH_BAT_ID and SLA_SCH_AGE. Ground speed and missing sensors are never substituted. Valid observations alone do not qualify local canard flow or grant flight admission.
				 */
				reporter.armingCheckFailure(unavailable_modes, health_component_t::system,
							    events::ID("check_slaaps_schedule_input"), events::Log::Error,
							    "SLAAPS schedule: check selected airflow and power inputs");
				return;
			}

			if (status.schedule_enabled && status.manual_entry_status == slaaps_status_s::MANUAL_ENTRY_REJECTED
			    && status.schedule_status >= 2) {
				/* EVENT
				 * @description
				 * Scheduling rejected its source identity, freshness, calibrated numerical envelope, transient bound, allocation matrix or feasible bumpless gain transition. Inspect slaaps_status.schedule_status and the accepted table. Resolve the condition while disarmed, reapply the profile when required, then deliberately leave and reselect. Returning data cannot revive the rejected session.
				 */
				reporter.armingCheckFailure(unavailable_modes, health_component_t::system,
							    events::ID("check_slaaps_schedule_fault"), events::Log::Error,
							    "SLAAPS schedule rejected: inspect condition and table");
				return;
			}

			switch (status.manual_entry_status) {
			case slaaps_status_s::MANUAL_ENTRY_WAIT_SCHEDULE:
				/* EVENT
				 * @description
				 * The current indicated airspeed, density, battery voltage and initial zero EDF command must lie inside the accepted schedule, with a valid interpolated control-effectiveness matrix. Check the selected observations and measured table. Entry does not run the EDF to reach a scheduled condition or infer a ground spool-up policy.
				 */
				reporter.armingCheckFailure(unavailable_modes, health_component_t::system,
							    events::ID("check_slaaps_entry_schedule"), events::Log::Error,
							    "SLAAPS initial condition outside usable schedule");
				return;

			case slaaps_status_s::MANUAL_ENTRY_WAIT_CONFIGURATION:
				/* EVENT
				 * @description
				 * Select the stabilized-manual configuration scope and explicitly apply a complete valid profile while disarmed.
				 */
				reporter.armingCheckFailure(unavailable_modes, health_component_t::system,
							    events::ID("check_slaaps_entry_configuration"), events::Log::Error,
							    "Apply a valid SLAAPS manual configuration");
				return;

			case slaaps_status_s::MANUAL_ENTRY_WAIT_ROUTING:
				/* EVENT
				 * @description
				 * Both supported PWM banks must report fresh matching logical functions, routing and calibration before preparation.
				 */
				reporter.armingCheckFailure(unavailable_modes, health_component_t::system,
							    events::ID("check_slaaps_entry_routing"), events::Log::Error,
							    "Check SLAAPS MAIN/AUX output configuration");
				return;

			case slaaps_status_s::MANUAL_ENTRY_WAIT_INPUT:
				/* EVENT
				 * @description
				 * Use the configured RC or pinned MAVLink source. A new sample after manual-mode selection is required; retained samples cannot start a new entry.
				 */
				reporter.armingCheckFailure(unavailable_modes, health_component_t::system,
							    events::ID("check_slaaps_entry_input"), events::Log::Error,
							    "SLAAPS needs fresh configured pilot input");
				return;

			case slaaps_status_s::MANUAL_ENTRY_WAIT_CENTER:
				/* EVENT
				 * @description
				 * All three configured attitude axes must be inside the configured center-entry threshold before preparing outputs.
				 */
				reporter.armingCheckFailure(unavailable_modes, health_component_t::system,
							    events::ID("check_slaaps_entry_center"), events::Log::Error,
							    "Center attitude sticks for SLAAPS entry");
				return;

			case slaaps_status_s::MANUAL_ENTRY_WAIT_THROTTLE:
				/* EVENT
				 * @description
				 * Use the configured low-throttle endpoint and entry limit. Reversed endpoints are respected; values outside the calibrated span are rejected.
				 */
				reporter.armingCheckFailure(unavailable_modes, health_component_t::system,
							    events::ID("check_slaaps_entry_throttle"), events::Log::Error,
							    "Lower throttle for SLAAPS entry");
				return;

			case slaaps_status_s::MANUAL_ENTRY_WAIT_STATE:
				/* EVENT
				 * @description
				 * Selected estimator metadata and numerical attitude/rate data must be fresh and consistent. No output permission is granted by this check.
				 */
				reporter.armingCheckFailure(unavailable_modes, health_component_t::system,
							    events::ID("check_slaaps_entry_state"), events::Log::Error,
							    "SLAAPS needs valid attitude and rate estimates");
				return;

			case slaaps_status_s::MANUAL_ENTRY_REJECTED:
				/* EVENT
				 * @description
				 * A preparation or bound-input fault cancelled this entry. Resolve the reported configuration, input or routing issue, then deliberately leave and reselect Manual while disarmed.
				 */
				reporter.armingCheckFailure(unavailable_modes, health_component_t::system,
							    events::ID("check_slaaps_entry_rejected"), events::Log::Error,
							    "Leave and reselect SLAAPS Manual after resolving fault");
				return;

			case slaaps_status_s::MANUAL_ENTRY_PREPARING:
				/* EVENT
				 * @description
				 * Output-bank preparation is non-actuating and has an immutable deadline. It does not authorize arming or propulsion.
				 */
				reporter.armingCheckFailure(unavailable_modes, health_component_t::system,
							    events::ID("check_slaaps_entry_preparing"), events::Log::Error,
							    "SLAAPS output preparation is in progress");
				return;

			case slaaps_status_s::MANUAL_ENTRY_PREPARED:
				/* EVENT
				 * @description
				 * Prepared software endpoints do not establish measured control authority, recovery policy, vehicle qualification or armed output permission.
				 */
				reporter.armingCheckFailure(unavailable_modes, health_component_t::system,
							    events::ID("check_slaaps_admission_unavailable"), events::Log::Error,
							    "SLAAPS flight admission remains unavailable");
				return;

			case slaaps_status_s::MANUAL_ENTRY_INACTIVE:
				/* EVENT
				 * @description
				 * Deliberate disarmed manual-mode entry is required before checking selected pilot input and preparing the output banks.
				 */
				reporter.armingCheckFailure(unavailable_modes, health_component_t::system,
							    events::ID("check_slaaps_entry_inactive"), events::Log::Error,
							    "Select SLAAPS Manual while disarmed");
				return;

			default:
				break;
			}
		}

		/* EVENT
		 * @description
		 * SLAAPS requires a running supervisor, an explicitly applied vehicle
		 * configuration and an implemented output/admission path for this mode.
		 * The current servo-bench path has no armed admission. Stock flight modes are not
		 * substitutes for a SLAAPS controller.
		 */
		reporter.armingCheckFailure(unavailable_modes, health_component_t::system,
					    events::ID("check_slaaps_not_ready"), events::Log::Error,
					    "SLAAPS mode is not ready for arming");
	}
}
