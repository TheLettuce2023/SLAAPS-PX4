#include "VehicleConfiguration.hpp"

#include <cmath>
#include <cstring>

namespace slaaps
{
namespace
{
constexpr uint64_t kUsPerMs = 1000;
constexpr float kMinimumServoPulse = 500.F;
constexpr float kMaximumServoPulse = 2500.F;
constexpr int32_t kMaximumMetadataAgeUs = 1000000;
constexpr int32_t kMaximumCycleUs = 500000;

bool positive(float value) { return std::isfinite(value) && value > 0.F; }

ConfigurationResult scalarRanges(const VehicleConfiguration &c)
{
#define SLAAPS_FLOAT(field, name, group, description, unit, minimum, maximum, initial) \
	if (!std::isfinite(c.field) || c.field < minimum || c.field > maximum) { \
		return {ConfigurationFault::ParameterRange, #name}; \
	}
#define SLAAPS_INT(field, name, group, description, unit, minimum, maximum, initial) \
	if (c.field < minimum || c.field > maximum) { return {ConfigurationFault::ParameterRange, #name}; }
#include "VehicleParameters.def"
#undef SLAAPS_INT
#undef SLAAPS_FLOAT
	return {};
}

bool validInertia(const VehicleConfiguration &c)
{
	// Cholesky of the full symmetric tensor, in double precision. Positive
	// diagonal entries alone do not establish a physically admissible tensor.
	const double xx = c.inertia[0], yy = c.inertia[1], zz = c.inertia[2];
	const double xy = c.inertia[3], xz = c.inertia[4], yz = c.inertia[5];

	if (!(xx > 0. && yy > 0. && zz > 0.)) { return false; }

	const double l00 = std::sqrt(xx);
	const double l10 = xy / l00, l20 = xz / l00;
	const double d11 = yy - l10 * l10;

	if (!(d11 > 0.)) { return false; }

	const double l21 = (yz - l20 * l10) / std::sqrt(d11);
	const double d22 = zz - l20 * l20 - l21 * l21;

	// A rigid body's inertia must also satisfy the principal-moment triangle
	// inequalities: trace(I)/2 * identity - I must be positive semidefinite.
	const double a = (yy + zz - xx) * 0.5;
	const double b = (xx + zz - yy) * 0.5;
	const double d = (xx + yy - zz) * 0.5;
	const double determinant = a * b * d - a * yz * yz - b * xz * xz - d * xy * xy - 2. * xy * xz * yz;
	return std::isfinite(d22) && d22 > 0. && a >= 0. && b >= 0. && d >= 0.
	       && a * b >= xy * xy && a * d >= xz * xz && b * d >= yz * yz && determinant >= 0.;
}

bool validServo(const CanardConfiguration &c)
{
	const bool ascending = c.pwm_negative < c.pwm_neutral && c.pwm_neutral < c.pwm_positive;
	const bool descending = c.pwm_negative > c.pwm_neutral && c.pwm_neutral > c.pwm_positive;
	const float pulses[] = {c.pwm_negative, c.pwm_neutral, c.pwm_positive};

	for (float pulse : pulses) {
		if (pulse < kMinimumServoPulse || pulse > kMaximumServoPulse || std::floor(pulse) < pulse) { return false; }
	}

	return (ascending || descending) && positive(c.maximum_deflection_deg)
	       && positive(c.slew_positive) && positive(c.slew_negative);
}

bool validThrustCurve(const VehicleConfiguration &c)
{
	if (!(c.edf_thrust[0] >= 0.F && c.edf_thrust[0] <= 0.F)
	    || !positive(c.edf_voltage) || !positive(c.edf_rise) || !positive(c.edf_fall)) {
		return false;
	}

	for (unsigned i = 1; i < kThrustPoints; ++i) {
		if (!std::isfinite(c.edf_thrust[i]) || c.edf_thrust[i] <= c.edf_thrust[i - 1]) { return false; }
	}

	return true;
}

ConfigurationResult activationTiming(const VehicleConfiguration &c, uint64_t minimum, uint64_t maximum,
				     const char *cycle_parameter)
{
	if (c.selected_status_age_us <= 0 || c.selected_status_age_us > kMaximumMetadataAgeUs) {
		return {ConfigurationFault::InvalidTiming, "SLA_STATUS_AGE"};
	}

	if (c.actuator_permission_age_us <= 0 || c.actuator_permission_age_us > kMaximumMetadataAgeUs) {
		return {ConfigurationFault::InvalidTiming, "SLA_ARM_AGE"};
	}

	if (!minimum || maximum <= minimum || maximum > static_cast<uint64_t>(kMaximumCycleUs)) {
		return {ConfigurationFault::InvalidTiming, cycle_parameter};
	}

	if (c.first_command_budget_us <= 0 || static_cast<uint64_t>(c.first_command_budget_us) <= minimum
	    || static_cast<uint64_t>(c.first_command_budget_us) >= maximum) {
		return {ConfigurationFault::InvalidTiming, "SLA_FIRST_CMD_US"};
	}

	// Ordered positive bounds make the subtraction safe. Match the activation
	// barrier's strict remaining-cycle inequality without adding signed times.
	if (c.activation_ack_timeout_us <= 0
	    || static_cast<uint64_t>(c.activation_ack_timeout_us) >= maximum - static_cast<uint64_t>(c.first_command_budget_us)) {
		return {ConfigurationFault::InvalidTiming, "SLA_ACT_ACK_US"};
	}

	return {};
}

ConfigurationResult manualActivationTiming(const VehicleConfiguration &c)
{
	return activationTiming(c, c.cycle_min_us > 0 ? static_cast<uint64_t>(c.cycle_min_us) : 0,
				c.cycle_max_us > 0 ? static_cast<uint64_t>(c.cycle_max_us) : 0, "SLA_CYCLE_MAX");
}

ConfigurationResult benchActivationTiming(const VehicleConfiguration &c)
{
	return activationTiming(c, research_actuator_command::kBenchIntervalUs,
				c.bench_gap_ms > 0 ? static_cast<uint64_t>(c.bench_gap_ms) * kUsPerMs : 0, "SLA_BENCH_GAP");
}

void buildBenchProfile(const VehicleConfiguration &c, research_actuator_command::BenchOutputProfile &profile)
{
	auto &control = profile.control;
	control.source_policy = c.manual_source == static_cast<int32_t>(ManualInputSource::RcOnly) ?
				research_actuator_command::ManualSourcePolicy::RcOnly
				: research_actuator_command::ManualSourcePolicy::MavlinkOnly;
	control.mavlink_instance = static_cast<uint8_t>(c.manual_instance);
	control.source_system_id = static_cast<uint8_t>(c.manual_system);
	control.source_component_id = static_cast<uint8_t>(c.manual_component);
	control.neutral_entry_tolerance = c.centered_enter;
	control.maximum_manual_age_us = static_cast<uint64_t>(c.manual_age_ms) * kUsPerMs;
	control.maximum_cycle_gap_us = static_cast<uint64_t>(c.bench_gap_ms) * kUsPerMs;
	profile.lease_us = static_cast<uint64_t>(c.output_lease_ms) * kUsPerMs;
	profile.deadzone = c.bench_deadzone;

	for (unsigned axis = 0; axis < kAxes; ++axis) {
		control.stick_axis[axis] = static_cast<uint8_t>(c.axis[axis].input_axis);
		control.stick_sign[axis] = c.axis[axis].sign;
	}

	for (unsigned i = 0; i < kCanards; ++i) {
		const auto &canard = c.canard[i];
		control.servos[i] = {canard.pwm_negative, canard.pwm_neutral, canard.pwm_positive,
				     canard.maximum_deflection_deg, canard.slew_positive * canard.maximum_deflection_deg,
				     canard.slew_negative * canard.maximum_deflection_deg, canard.minimum, canard.maximum
				    };

		for (unsigned axis = 0; axis < kAxes; ++axis) { control.mix[i][axis] = canard.bench_mix[axis]; }
	}
}

// Keep bench-only profile storage off the flight driver's validation
// call chain. The shared work queue has a bounded stack; inlining this branch
// reserves its larger scratch frame even for StabilizedManual validation.
__attribute__((noinline)) ConfigurationResult validateBenchConfiguration(const VehicleConfiguration &c)
{
	research_actuator_command::BenchOutputProfile profile{};
	buildBenchProfile(c, profile);

	if (c.output_lease_ms < c.bench_gap_ms || c.centered_enter < c.bench_deadzone) {
		return {ConfigurationFault::InvalidController, "SLA_BENCH_GAP"};
	}

	// Bench inspects physical zero; its travel must include zero independently
	// of the flight trim. The same owner checks standalone and shared profiles.
	if (research_actuator_command::BenchControlOwner::validateConfiguration(profile.control)
	    != research_actuator_command::BenchStatus::Ok) {
		return {ConfigurationFault::InvalidController, nullptr};
	}

	return {};
}

void buildAllocationConfiguration(const VehicleConfiguration &c,
				  research_stabilized_manual::AllocationConfiguration &allocation,
				  const ControlScheduleCorner *corner)
{
	allocation.regularization = c.regularization;
	allocation.minimum_row_norm = c.minimum_row_norm;
	allocation.minimum_independence = c.minimum_independence;
	allocation.saturation_tolerance = c.saturation_tolerance;

	for (unsigned axis = 0; axis < kAxes; ++axis) {
		allocation.axis_weight(axis) = c.axis[axis].allocation_weight;

		for (unsigned canard = 0; canard < kCanards; ++canard) {
			const float derivative = corner ? corner->moment_derivative[axis][canard] : c.canard[canard].moment_derivative[axis];
			allocation.effectiveness[axis][canard] = derivative * c.canard[canard].deflection_slope / c.axis[axis].moment_scale;
		}
	}

	for (unsigned i = 0; i < kCanards; ++i) {
		const auto &canard = c.canard[i];
		allocation.trim[i] = canard.trim;
		allocation.minimum[i] = canard.minimum;
		allocation.maximum[i] = canard.maximum;
		allocation.positive_slew_per_s[i] = canard.slew_positive;
		allocation.negative_slew_per_s[i] = canard.slew_negative;
	}
}

void buildManualConfiguration(const VehicleConfiguration &c, research_stabilized_manual::Configuration &control)
{
	control.source_policy = c.manual_source == static_cast<int32_t>(ManualInputSource::RcOnly) ?
				research_actuator_command::ManualSourcePolicy::RcOnly
				: research_actuator_command::ManualSourcePolicy::MavlinkOnly;
	control.mavlink_instance = static_cast<uint8_t>(c.manual_instance);
	control.source_system_id = static_cast<uint8_t>(c.manual_system);
	control.source_component_id = static_cast<uint8_t>(c.manual_component);
	control.maximum_manual_age_us = static_cast<uint64_t>(c.manual_age_ms) * kUsPerMs;
	control.maximum_state_age_us = static_cast<uint64_t>(c.state_age_ms) * kUsPerMs;
	control.maximum_state_skew_us = static_cast<uint64_t>(c.state_skew_ms) * kUsPerMs;
	control.minimum_cycle_us = static_cast<uint64_t>(c.cycle_min_us);
	control.maximum_cycle_us = static_cast<uint64_t>(c.cycle_max_us);
	control.quaternion_norm_tolerance = c.quaternion_tolerance;
	control.centered_enter = c.centered_enter;
	control.centered_exit = c.centered_exit;
	// This legacy report field is explicitly not a hardware propulsion request.
	control.fixed_simulated_thrust_n = 0.F;
	const auto *corner = c.schedule.enabled == 1 ? &c.schedule.corner[0][0] : nullptr;
	buildAllocationConfiguration(c, control.allocation, corner);

	for (unsigned a = 0; a < kAxes; ++a) {
		const auto &axis = c.axis[a];
		control.stick_axis[a] = static_cast<uint8_t>(axis.input_axis);
		control.stick_sign(a) = axis.sign;
		control.maximum_rate_rad_s(a) = axis.rate_limit;
		control.maximum_rate_setpoint_accel_rad_s2(a) = axis.setpoint_accel_limit;
		control.maximum_measured_rate_rad_s(a) = axis.measured_rate_limit;
		control.maximum_measured_accel_rad_s2(a) = axis.measured_accel_limit;
		control.attitude_gain(a) = axis.attitude_gain;
		control.rate_p(a) = corner ? corner->rate_p[a] : axis.rate_p;
		control.rate_i(a) = corner ? corner->rate_i[a] : axis.rate_i;
		control.rate_d(a) = corner ? corner->rate_d[a] : axis.rate_d;
		control.integrator_limit(a) = axis.integrator_limit;
		control.maximum_normalized_moment(a) = axis.moment_limit;
	}
}

ConfigurationResult controlScheduleConfiguration(const VehicleConfiguration &c)
{
	const auto &schedule = c.schedule;

	if (!schedule.enabled) { return {}; }

	if (schedule.enabled != 1) { return {ConfigurationFault::ParameterRange, "SLA_SCH_EN"}; }

	if (schedule.airspeed_instance < 0) { return {ConfigurationFault::ParameterRange, "SLA_SCH_AS_INST"}; }

	if (schedule.air_data_instance < 0) { return {ConfigurationFault::ParameterRange, "SLA_SCH_AD_INST"}; }

	if (schedule.battery_instance < 0) { return {ConfigurationFault::ParameterRange, "SLA_SCH_BAT_INST"}; }

	if (schedule.battery_id <= 0) { return {ConfigurationFault::ParameterRange, "SLA_SCH_BAT_ID"}; }

	if (schedule.maximum_age_us <= 0 || schedule.maximum_age_us > kMaximumMetadataAgeUs) {
		return {ConfigurationFault::InvalidTiming, "SLA_SCH_AGE"};
	}

	if (!(schedule.indicated_airspeed_m_s[0] >= 0.F
	      && schedule.indicated_airspeed_m_s[1] > schedule.indicated_airspeed_m_s[0])) {
		return {ConfigurationFault::InvalidController, "SLA_SCH_AS_HI"};
	}

	if (!(schedule.edf_command[0] >= 0.F && schedule.edf_command[1] > schedule.edf_command[0]
	      && schedule.edf_command[1] <= 1.F)) {
		return {ConfigurationFault::InvalidController, "SLA_SCH_EDF_HI"};
	}

	if (!positive(schedule.density_min) || !(schedule.density_max > schedule.density_min)) {
		return {ConfigurationFault::InvalidController, "SLA_SCH_RHO_MAX"};
	}

	if (!positive(schedule.voltage_min) || !(schedule.voltage_max > schedule.voltage_min)) {
		return {ConfigurationFault::InvalidController, "SLA_SCH_V_MAX"};
	}

	if (!positive(schedule.filter_time_s)) { return {ConfigurationFault::InvalidController, "SLA_SCH_FILT_T"}; }

	if (!positive(schedule.airspeed_filter_rate)) { return {ConfigurationFault::InvalidController, "SLA_SCH_AS_RATE"}; }

	if (!positive(schedule.edf_filter_rate)) { return {ConfigurationFault::InvalidController, "SLA_SCH_E_RATE"}; }

	if (!positive(schedule.airspeed_transient_rate)) { return {ConfigurationFault::InvalidController, "SLA_SCH_AS_TRAN"}; }

	if (!positive(schedule.edf_transient_rate)) { return {ConfigurationFault::InvalidController, "SLA_SCH_E_TRAN"}; }

	for (const auto &axis : c.axis) {
		if (!positive(axis.moment_scale)) { return {ConfigurationFault::InvalidEffectiveness, nullptr}; }
	}

	research_stabilized_manual::AllocationConfiguration allocation{};

	for (const auto &row : schedule.corner) {
		for (const auto &corner : row) {
			for (unsigned axis = 0; axis < kAxes; ++axis) {
				if (!positive(corner.rate_p[axis]) || !std::isfinite(corner.rate_i[axis]) || corner.rate_i[axis] < 0.F
				    || !std::isfinite(corner.rate_d[axis]) || corner.rate_d[axis] < 0.F) {
					return {ConfigurationFault::InvalidController, nullptr};
				}
			}

			buildAllocationConfiguration(c, allocation, &corner);

			if (!research_stabilized_manual::validAllocationConfiguration(allocation)) {
				return {ConfigurationFault::InvalidEffectiveness, nullptr};
			}
		}
	}

	return {};
}
}

bool manualActivationTimingConfigured(const VehicleConfiguration &c)
{
	return manualActivationTiming(c).valid();
}

bool benchActivationTimingConfigured(const VehicleConfiguration &c)
{
	return benchActivationTiming(c).valid();
}

ConfigurationResult validateControlSchedule(const VehicleConfiguration &c)
{
	const auto scalar = scalarRanges(c);
	return scalar.valid() ? controlScheduleConfiguration(c) : scalar;
}

ConfigurationResult validateCompanionConfiguration(const VehicleConfiguration &c)
{
	const auto &companion = c.companion;

	if (companion.enabled < 0 || companion.enabled > 1) { return {ConfigurationFault::ParameterRange, "SLA_CMP_EN"}; }

	const bool enabled = companion.enabled == 1;

	if (companion.mavlink_instance < (enabled ? 0 : -1) || companion.mavlink_instance > 3) {
		return {ConfigurationFault::ParameterRange, "SLA_CMP_INST"};
	}

	if (companion.source_system < (enabled ? 1 : 0) || companion.source_system > 255) {
		return {ConfigurationFault::ParameterRange, "SLA_CMP_SYS"};
	}

	if (companion.source_component < (enabled ? 1 : 0) || companion.source_component > 255) {
		return {ConfigurationFault::ParameterRange, "SLA_CMP_COMP"};
	}

	if (companion.source_id < (enabled ? 1 : 0)) { return {ConfigurationFault::ParameterRange, "SLA_CMP_SOURCE"}; }

	if (companion.maximum_age_us < 0 || companion.maximum_age_us > kMaximumMetadataAgeUs) {
		return {ConfigurationFault::ParameterRange, "SLA_CMP_AGE"};
	}

	if (enabled && companion.maximum_age_us == 0) { return {ConfigurationFault::InvalidTiming, "SLA_CMP_AGE"}; }

	return {};
}

ConfigurationResult validatePowerConfiguration(const PowerConfiguration &power, bool required)
{
	if (power.battery_instance < (required ? 0 : -1) || power.battery_instance >= kPowerBatteryInstances) {
		return {ConfigurationFault::ParameterRange, "SLA_PWR_INST"};
	}

	if (power.battery_id < (required ? 1 : 0) || power.battery_id > UINT8_MAX) {
		return {ConfigurationFault::ParameterRange, "SLA_PWR_ID"};
	}

	// BatteryStatus v1 sources: power module, external, ESC telemetry.
	if (power.battery_source < (required ? 0 : -1) || power.battery_source > 2) {
		return {ConfigurationFault::ParameterRange, "SLA_PWR_SRC"};
	}

	if (power.maximum_age_us < 0 || power.maximum_age_us > kMaximumMetadataAgeUs) {
		return {ConfigurationFault::ParameterRange, "SLA_PWR_AGE"};
	}

	if (!std::isfinite(power.voltage_min) || power.voltage_min < 0.F || power.voltage_min > 100.F) {
		return {ConfigurationFault::ParameterRange, "SLA_PWR_VMIN"};
	}

	if (!std::isfinite(power.voltage_max) || power.voltage_max < 0.F || power.voltage_max > 100.F) {
		return {ConfigurationFault::ParameterRange, "SLA_PWR_VMAX"};
	}

	if (required) {
		if (!power.maximum_age_us) { return {ConfigurationFault::InvalidTiming, "SLA_PWR_AGE"}; }

		if (power.voltage_min <= 0.F) { return {ConfigurationFault::ParameterRange, "SLA_PWR_VMIN"}; }

		if (power.voltage_max <= power.voltage_min) { return {ConfigurationFault::ParameterRange, "SLA_PWR_VMAX"}; }
	}

	return {};
}

ConfigurationResult validatePowerConfiguration(const VehicleConfiguration &c, bool required)
{
	return validatePowerConfiguration(c.power, required);
}

ConfigurationResult validate(const VehicleConfiguration &c, ConfigurationUse use)
{
	if (use != ConfigurationUse::Bench && use != ConfigurationUse::StabilizedManual
	    && use != ConfigurationUse::EdfCommissioning) { return {ConfigurationFault::ParameterRange, "SLA_CFG_KIND"}; }

	const auto scalar = scalarRanges(c);

	if (!scalar.valid()) { return scalar; }

	const auto companion = validateCompanionConfiguration(c);

	if (!companion.valid()) { return companion; }

	const auto power = validatePowerConfiguration(c, use != ConfigurationUse::Bench);

	if (!power.valid()) { return power; }

	if (c.data_revision <= 0) { return {ConfigurationFault::MissingRecord, "SLA_DATA_REV"}; }

	if (c.manual_source != static_cast<int32_t>(ManualInputSource::RcOnly)
	    && c.manual_source != static_cast<int32_t>(ManualInputSource::MavlinkOnly)) {
		return {ConfigurationFault::ParameterRange, "SLA_IN_SOURCE"};
	}

	const bool rc = c.manual_source == static_cast<int32_t>(ManualInputSource::RcOnly);

	if (rc ? (c.manual_instance != -1 || c.manual_system != 0 || c.manual_component != 0)
	    : (c.manual_instance < 0 || c.manual_system <= 0 || c.manual_component <= 0)) {
		return {ConfigurationFault::ParameterRange, "SLA_IN_INST"};
	}

	for (unsigned axis = 0; axis < kAxes; ++axis) {
		if (c.axis[axis].input_axis < 0 || c.axis[axis].input_axis >= static_cast<int32_t>(kAxes)
		    || std::fabs(c.axis[axis].sign) < 1.F || std::fabs(c.axis[axis].sign) > 1.F) {
			return {ConfigurationFault::InvalidController, nullptr};
		}

		for (unsigned previous = 0; previous < axis; ++previous) {
			if (c.axis[axis].input_axis == c.axis[previous].input_axis) {
				return {ConfigurationFault::InvalidController, nullptr};
			}
		}
	}

	if (c.output_lease_ms <= 0) { return {ConfigurationFault::InvalidTiming, "SLA_OUT_LEASE"}; }

	if (c.manual_age_ms <= 0) { return {ConfigurationFault::InvalidTiming, "SLA_IN_AGE"}; }

	for (const auto &canard : c.canard) {
		if (!validServo(canard)) { return {ConfigurationFault::InvalidServo, nullptr}; }
	}

	if (use == ConfigurationUse::Bench) { return validateBenchConfiguration(c); }

	if (!(c.manual_throttle_low < c.manual_throttle_high) && !(c.manual_throttle_low > c.manual_throttle_high)) {
		return {ConfigurationFault::InvalidController, "SLA_THR_HIGH"};
	}

	if (use == ConfigurationUse::EdfCommissioning) {
		const auto bench = validateBenchConfiguration(c);

		if (!bench.valid()) { return bench; }

		if (!positive(c.edf_commission_max)) { return {ConfigurationFault::InvalidController, "SLA_EDF_BMAX"}; }

		if (!positive(c.edf_rise)) { return {ConfigurationFault::InvalidController, "SLA_EDF_RISE"}; }

		if (!positive(c.edf_fall)) { return {ConfigurationFault::InvalidController, "SLA_EDF_FALL"}; }

		return benchActivationTiming(c);
	}

	if (!positive(c.mass)) { return {ConfigurationFault::InvalidInertia, "SLA_MASS"}; }

	// Tensor consistency involves multiple entries; do not blame a valid mass.
	if (!validInertia(c)) { return {ConfigurationFault::InvalidInertia, nullptr}; }

	const bool scheduled = c.schedule.enabled == 1;

	if (scheduled && c.schedule.battery_instance != c.power.battery_instance) {
		return {ConfigurationFault::ParameterRange, "SLA_SCH_BAT_INST"};
	}

	if (scheduled && c.schedule.battery_id != c.power.battery_id) {
		return {ConfigurationFault::ParameterRange, "SLA_SCH_BAT_ID"};
	}

	if (!scheduled && !positive(c.airflow)) { return {ConfigurationFault::InvalidEffectiveness, "SLA_AIRSPD"}; }

	for (const auto &canard : c.canard) {
		if (!positive(canard.slope_uncertainty) || std::fabs(canard.deflection_slope) <= canard.slope_uncertainty
		    || canard.minimum >= canard.maximum || canard.trim < canard.minimum || canard.trim > canard.maximum) {
			return {ConfigurationFault::InvalidEffectiveness, nullptr};
		}

		if (!scheduled) {
			for (float uncertainty : canard.derivative_uncertainty) {
				if (!positive(uncertainty)) { return {ConfigurationFault::InvalidEffectiveness, nullptr}; }
			}
		}
	}

	for (const auto &axis : c.axis) {
		if (!positive(axis.moment_scale)) { return {ConfigurationFault::InvalidEffectiveness, nullptr}; }
	}

	// Commands expire at the deadline itself. Entry must leave time for at least
	// one permitted integration before the initial EDF request/cycle expires.
	if (c.cycle_min_us <= 0 || c.cycle_max_us <= c.cycle_min_us) {
		return {ConfigurationFault::InvalidTiming, "SLA_CYCLE_MAX"};
	}

	if (static_cast<uint64_t>(c.output_lease_ms) * kUsPerMs <= static_cast<uint64_t>(c.cycle_min_us)) {
		return {ConfigurationFault::InvalidTiming, "SLA_OUT_LEASE"};
	}

	if (static_cast<uint64_t>(c.manual_age_ms) * kUsPerMs <= static_cast<uint64_t>(c.cycle_min_us)) {
		return {ConfigurationFault::InvalidTiming, "SLA_IN_AGE"};
	}

	const bool unconfigured_timing = !c.selected_status_age_us && !c.actuator_permission_age_us
					 && !c.activation_ack_timeout_us && !c.first_command_budget_us;

	if (!unconfigured_timing) {
		const auto timing = manualActivationTiming(c);

		if (!timing.valid()) { return timing; }
	}

	const auto schedule = controlScheduleConfiguration(c);

	if (!schedule.valid()) { return schedule; }

	research_stabilized_manual::Configuration control{};
	buildManualConfiguration(c, control);

	if (research_stabilized_manual::StabilizedManualControl::validateConfiguration(control) != research_stabilized_manual::Status::Ok) {
		return {ConfigurationFault::InvalidController, nullptr};
	}

	if (!validThrustCurve(c) || c.condition_thrust > c.edf_thrust[kThrustPoints - 1]) {
		return {ConfigurationFault::InvalidThrustCurve, "SLA_EDF_T4"};
	}

	return {};
}

bool makeBenchProfile(const VehicleConfiguration &c, research_actuator_command::BenchOutputProfile &profile)
{
	if (!validate(c, ConfigurationUse::Bench).valid()) { return false; }

	research_actuator_command::BenchOutputProfile candidate{};
	buildBenchProfile(c, candidate);
	profile = candidate;
	return true;
}

bool makeManualConfiguration(const VehicleConfiguration &c, research_stabilized_manual::Configuration &control)
{
	if (!validate(c, ConfigurationUse::StabilizedManual).valid()) { return false; }

	research_stabilized_manual::Configuration candidate{};
	buildManualConfiguration(c, candidate);
	control = candidate;
	return true;
}

bool equal(const VehicleConfiguration &left, const VehicleConfiguration &right)
{
	// Fieldwise bytes avoid struct padding and preserve signed-zero changes.
#define SLAAPS_FLOAT(field, name, group, description, unit, minimum, maximum, initial) \
	if (std::memcmp(&left.field, &right.field, sizeof(left.field)) != 0) { return false; }
#define SLAAPS_INT SLAAPS_FLOAT
#include "VehicleParameters.def"
#undef SLAAPS_INT
#undef SLAAPS_FLOAT
	return true;
}

uint64_t fingerprint(const VehicleConfiguration &c)
{
	// Canonical little-endian FNV-1a over listed scalar bits; diagnostic identity,
	// not authentication. The store compares every field, never only this hash.
	uint64_t hash = UINT64_C(14695981039346656037);
	const auto append = [&hash](const void *value) {
		uint32_t bits{};
		std::memcpy(&bits, value, sizeof(bits));

		for (unsigned i = 0; i < sizeof(bits); ++i) {
			hash ^= (bits >> (8 * i)) & 0xffU;
			hash *= UINT64_C(1099511628211);
		}
	};
#define SLAAPS_FLOAT(field, name, group, description, unit, minimum, maximum, initial) append(&c.field);
#define SLAAPS_INT SLAAPS_FLOAT
#include "VehicleParameters.def"
#undef SLAAPS_INT
#undef SLAAPS_FLOAT
	return hash;
}

bool thrustToCommand(const VehicleConfiguration &c, float thrust_n, float &command)
{
	if (!validThrustCurve(c) || !std::isfinite(thrust_n) || thrust_n < 0.F || thrust_n > c.edf_thrust[kThrustPoints - 1]) {
		return false;
	}

	for (unsigned i = 1; i < kThrustPoints; ++i) {
		if (thrust_n <= c.edf_thrust[i]) {
			const float fraction = (thrust_n - c.edf_thrust[i - 1]) / (c.edf_thrust[i] - c.edf_thrust[i - 1]);
			command = (static_cast<float>(i - 1) + fraction) / static_cast<float>(kThrustPoints - 1);
			return true;
		}
	}

	return false;
}

bool normalizeManualThrottle(float raw, float low, float high, float &command)
{
	if (!std::isfinite(raw) || !std::isfinite(low) || !std::isfinite(high)
	    || raw < -1.F || raw > 1.F || low < -1.F || low > 1.F || high < -1.F || high > 1.F
	    || (!(low < high) && !(low > high))) { return false; }

	const float minimum = low < high ? low : high;
	const float maximum = low > high ? low : high;

	if (raw < minimum || raw > maximum) { return false; }

	command = static_cast<float>((static_cast<double>(raw) - static_cast<double>(low))
				     / (static_cast<double>(high) - static_cast<double>(low)));
	return true;
}

ConfigurationResult ConfigurationStore::commit(const VehicleConfiguration &candidate, ConfigurationUse use)
{
	if (_active) { return {ConfigurationFault::Active, nullptr}; }

	const auto result = validate(candidate, use);

	if (!result.valid()) {
		// A failed replacement cannot leave an older candidate eligible.
		_candidate = false;
		return result;
	}

	_configuration = candidate;
	_use = use;
	_identity = fingerprint(candidate);
	_candidate = true;
	_invalidated = false;
	return {};
}

bool ConfigurationStore::activate()
{
	if (!_candidate || _active || _invalidated) { return false; }

	_active = true;
	return true;
}

void ConfigurationStore::observe(const VehicleConfiguration &candidate)
{
	if (_candidate && !equal(candidate, _configuration)) { invalidate(); }
}

void ConfigurationStore::invalidate()
{
	_invalidated = true;
	_candidate = false;
}

void ConfigurationStore::disable()
{
	_active = false;
	_candidate = false;
	_invalidated = true;
}
} // namespace slaaps
