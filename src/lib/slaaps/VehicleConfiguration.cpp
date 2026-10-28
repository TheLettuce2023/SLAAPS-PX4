#include "VehicleConfiguration.hpp"
#include "WrenchAllocator.hpp"
#include <lib/research_acceleration/TrustedTimeMap.hpp>

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
// EKF2 republishes unchanged estimator_status_flags only once per second, so its age bound must exceed 1 s.
constexpr int32_t kMaximumStatusAgeUs = 2000000;
constexpr int32_t kMaximumCycleUs = 500000;
constexpr int32_t kMaximumManualReadyWaitS = 60; // Software operator-wait bound, not timing qualification.
constexpr int32_t kMaximumRelativeDriftPpm = 100000;
constexpr int32_t kMaximumMapIntervalUs = 60000000;
constexpr float kMaximumCompanionAcceleration = 1000.F; // Software metadata range, not vehicle authority.

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
	const char *imu_parameters[kAxes] {"EKF2_IMU_POS_X", "EKF2_IMU_POS_Y", "EKF2_IMU_POS_Z"};

	for (unsigned axis = 0; axis < kAxes; ++axis) {
		if (!std::isfinite(c.feedback.imu_position[axis])) { return {ConfigurationFault::ParameterRange, imu_parameters[axis]}; }
	}

	// Native metadata has no position range. The cutoff has an explicit native
	// range, including zero for disabled filtering; never manufacture an offset.
	if (!std::isfinite(c.feedback.cutoff_hz) || c.feedback.cutoff_hz < 0.F || c.feedback.cutoff_hz > 1000.F) {
		return {ConfigurationFault::ParameterRange, "IMU_ACCEL_CUTOFF"};
	}

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
	if (c.selected_status_age_us <= 0 || c.selected_status_age_us > kMaximumStatusAgeUs) {
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

	uint64_t reserve{};

	if (!activationReserveUs(c, reserve)) { return {ConfigurationFault::InvalidTiming, "SLA_ACT_ACK_US"}; }

	// Admission reserves the complete declared reply window, not only a lucky
	// early reply. Original live sample expiry still needs an independent check.
	if (c.output_lease_ms <= 0 || static_cast<uint64_t>(c.output_lease_ms) * kUsPerMs <= reserve) {
		return {ConfigurationFault::InvalidTiming, "SLA_OUT_LEASE"};
	}

	// Commander age is inclusive; the permission deadline is exclusive.
	if (static_cast<uint64_t>(c.actuator_permission_age_us) + 1 <= reserve) {
		return {ConfigurationFault::InvalidTiming, "SLA_ARM_AGE"};
	}

	if (c.manual_age_ms <= 0 || static_cast<uint64_t>(c.manual_age_ms) * kUsPerMs <= reserve) {
		return {ConfigurationFault::InvalidTiming, "SLA_IN_AGE"};
	}

	return {};
}

ConfigurationResult propulsionActivationTiming(const VehicleConfiguration &c)
{
	uint64_t reserve{};

	if (!activationReserveUs(c, reserve)) { return {ConfigurationFault::InvalidTiming, "SLA_ACT_ACK_US"}; }

	// The original selected battery report bounds every propulsion permission.
	if (c.power.maximum_age_us <= 0 || static_cast<uint64_t>(c.power.maximum_age_us) + 1 <= reserve) {
		return {ConfigurationFault::InvalidTiming, "SLA_PWR_AGE"};
	}

	return {};
}

ConfigurationResult manualActivationTiming(const VehicleConfiguration &c)
{
	const auto timing = activationTiming(c, c.cycle_min_us > 0 ? static_cast<uint64_t>(c.cycle_min_us) : 0,
					     c.cycle_max_us > 0 ? static_cast<uint64_t>(c.cycle_max_us) : 0, "SLA_CYCLE_MAX");

	if (!timing.valid()) { return timing; }

	if (c.manual_ready_wait_s <= 0 || c.manual_ready_wait_s > kMaximumManualReadyWaitS) {
		return {ConfigurationFault::InvalidTiming, "SLA_MAN_WAIT"};
	}

	return {};
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

bool buildWrenchModel(const VehicleConfiguration &c, const ControlScheduleCorner *corner,
		      float reference_thrust, WrenchModel &model)
{
	WrenchModel candidate{};
	const auto &wrench = c.wrench;
	bool edf_force_present = false;

	for (unsigned axis = 0; axis < kAxes; ++axis) {
		const float force = corner ? corner->edf_force_per_thrust[axis] : wrench.edf_force_per_thrust[axis];
		const float moment = corner ? corner->edf_moment_per_thrust[axis] : wrench.edf_moment_per_thrust[axis];
		edf_force_present = edf_force_present || std::fabs(force) > 0.F;
		candidate.effectiveness[axis][kCanards] = force;
		candidate.effectiveness[kAxes + axis][kCanards] = moment;
		candidate.offset[axis] = (corner ? corner->trim_force[axis] : wrench.trim_force[axis]) - force * reference_thrust;
		candidate.offset[kAxes + axis] = (corner ? corner->trim_moment[axis] : wrench.trim_moment[axis])
						 - moment * reference_thrust;

		for (unsigned canard = 0; canard < kCanards; ++canard) {
			const auto &surface = c.canard[canard];
			const float force_derivative = corner ? corner->force_derivative[axis][canard] : surface.force_derivative[axis];
			const float moment_derivative = corner ? corner->moment_derivative[axis][canard] : surface.moment_derivative[axis];
			candidate.effectiveness[axis][canard] = force_derivative * surface.deflection_slope;
			candidate.effectiveness[kAxes + axis][canard] = moment_derivative * surface.deflection_slope;
			candidate.offset[axis] -= candidate.effectiveness[axis][canard] * surface.trim;
			candidate.offset[kAxes + axis] -= candidate.effectiveness[kAxes + axis][canard] * surface.trim;
		}
	}

	if (!edf_force_present) { return false; }

	for (unsigned row = 0; row < kWrenchAxes; ++row) {
		if (!std::isfinite(candidate.offset[row])) { return false; }

		for (float coefficient : candidate.effectiveness[row]) {
			if (!std::isfinite(coefficient)) { return false; }
		}
	}

	model = candidate;
	return true;
}
}

ConfigurationResult validateWrenchConfiguration(const VehicleConfiguration &c)
{
	const auto scalar = scalarRanges(c);

	if (!scalar.valid() || !c.wrench.enabled) { return scalar; }

	const char *force_parameters[kAxes] {"SLA_WR_FT_X", "SLA_WR_FT_Y", "SLA_WR_FT_Z"};
	const char *moment_parameters[kAxes] {"SLA_WR_MT_X", "SLA_WR_MT_Y", "SLA_WR_MT_Z"};

	for (unsigned axis = 0; axis < kAxes; ++axis) {
		if (!positive(c.wrench.force_residual_tolerance[axis])) {
			return {ConfigurationFault::InvalidWrench, force_parameters[axis]};
		}

		if (!positive(c.wrench.moment_residual_tolerance[axis])) {
			return {ConfigurationFault::InvalidWrench, moment_parameters[axis]};
		}
	}

	for (const auto &canard : c.canard) {
		if (!validServo(canard)) { return {ConfigurationFault::InvalidServo, nullptr}; }
	}

	if (!validThrustCurve(c)) { return {ConfigurationFault::InvalidThrustCurve, "SLA_EDF_T4"}; }

	research_stabilized_manual::AllocationConfiguration allocation{};
	WrenchModel model{};

	if (c.schedule.enabled) {
		const auto schedule = controlScheduleConfiguration(c);

		if (!schedule.valid()) { return schedule; }

		for (unsigned i = 0; i < kScheduleKnots; ++i) {
			for (unsigned j = 0; j < kScheduleKnots; ++j) {
				float reference{};

				if (!commandToThrust(c, c.schedule.edf_command[j], reference)
				    || !buildWrenchModel(c, &c.schedule.corner[i][j], reference, model)) {
					return {ConfigurationFault::InvalidWrench, nullptr};
				}
			}
		}

	} else {
		buildAllocationConfiguration(c, allocation, nullptr);

		if (!research_stabilized_manual::validAllocationConfiguration(allocation)) {
			return {ConfigurationFault::InvalidEffectiveness, nullptr};
		}

		if (c.condition_thrust < c.edf_thrust[0] || c.condition_thrust > c.edf_thrust[kThrustPoints - 1]) {
			return {ConfigurationFault::InvalidWrench, "SLA_TEST_THR"};
		}

		if (!buildWrenchModel(c, nullptr, c.condition_thrust, model)) {
			return {ConfigurationFault::InvalidWrench, nullptr};
		}
	}

	return {};
}

bool makeWrenchModel(const VehicleConfiguration &c, WrenchModel &model)
{
	return c.wrench.enabled == 1 && !c.schedule.enabled && validateWrenchConfiguration(c).valid()
	       && buildWrenchModel(c, nullptr, c.condition_thrust, model);
}

bool makeWrenchCornerModel(const VehicleConfiguration &c, unsigned i, unsigned j, WrenchModel &model)
{
	if (c.wrench.enabled != 1 || c.schedule.enabled != 1 || i >= kScheduleKnots || j >= kScheduleKnots
	    || !validateWrenchConfiguration(c).valid()) { return false; }

	float reference{};
	return commandToThrust(c, c.schedule.edf_command[j], reference)
	       && buildWrenchModel(c, &c.schedule.corner[i][j], reference, model);
}

ConfigurationResult validateAccelerationFeedbackConfiguration(const VehicleConfiguration &c)
{
	const auto scalar = scalarRanges(c);

	if (!scalar.valid() || !c.feedback.enabled) { return scalar; }

	const auto &feedback = c.feedback;

	if (feedback.latency_bound_us <= 0) { return {ConfigurationFault::InvalidFeedback, "SLA_FB_LAT_US"}; }

	if (feedback.maximum_age_us <= feedback.latency_bound_us) { return {ConfigurationFault::InvalidFeedback, "SLA_FB_AGE"}; }

	if (feedback.maximum_bias_age_us <= 0) { return {ConfigurationFault::InvalidFeedback, "SLA_FB_BIAS_AGE"}; }

	if (feedback.settling_us < feedback.latency_bound_us) { return {ConfigurationFault::InvalidFeedback, "SLA_FB_SETTLE_US"}; }

	if (!positive(feedback.maximum_specific_force_m_s2)) { return {ConfigurationFault::InvalidFeedback, "SLA_FB_F_MAX"}; }

	if (!positive(feedback.maximum_acceleration_m_s2)) { return {ConfigurationFault::InvalidFeedback, "SLA_FB_A_MAX"}; }

	if (!positive(feedback.gravity_m_s2)) { return {ConfigurationFault::InvalidFeedback, "SLA_FB_GRAV"}; }

	if (c.state_age_ms <= 0) { return {ConfigurationFault::InvalidFeedback, "SLA_STATE_AGE"}; }

	if (c.selected_status_age_us <= 0) { return {ConfigurationFault::InvalidFeedback, "SLA_STATUS_AGE"}; }

	if (c.state_skew_ms <= 0 || static_cast<uint64_t>(c.state_skew_ms) * kUsPerMs <= static_cast<uint64_t>(feedback.latency_bound_us)) {
		return {ConfigurationFault::InvalidFeedback, "SLA_STATE_SKEW"};
	}

	if (!positive(c.quaternion_tolerance) || c.quaternion_tolerance >= 1.F) {
		return {ConfigurationFault::InvalidFeedback, "SLA_QUAT_TOL"};
	}

	const char *rate_parameters[kAxes] {"SLA_R_MEAS_R", "SLA_P_MEAS_R", "SLA_Y_MEAS_R"};
	const char *acceleration_parameters[kAxes] {"SLA_R_MEAS_A", "SLA_P_MEAS_A", "SLA_Y_MEAS_A"};

	for (unsigned axis = 0; axis < kAxes; ++axis) {
		if (!positive(c.axis[axis].measured_rate_limit)) { return {ConfigurationFault::InvalidFeedback, rate_parameters[axis]}; }

		if (!positive(c.axis[axis].measured_accel_limit)) { return {ConfigurationFault::InvalidFeedback, acceleration_parameters[axis]}; }
	}

	return {};
}

bool companionClockRenewalConfigured(const CompanionConfiguration &c, uint64_t &lead_us, uint64_t *observation_slack_us)
{
	if (c.map_interval_us <= 0 || c.map_interval_us > 60000000 || c.maximum_round_trip_us <= 0
	    || c.maximum_round_trip_us > 1000000 || c.timestamp_uncertainty_us <= 0 || c.timestamp_uncertainty_us > 1000000
	    || c.maximum_relative_drift_ppm <= 0 || c.maximum_relative_drift_ppm > 100000
	    || c.maximum_clock_uncertainty_us <= 0 || c.maximum_clock_uncertainty_us > 1000000) { return false; }

	const auto interval = static_cast<uint64_t>(c.map_interval_us);
	const auto rtt = static_cast<uint64_t>(c.maximum_round_trip_us);
	const auto stamp = static_cast<uint64_t>(c.timestamp_uncertainty_us);
	const auto drift = static_cast<uint64_t>(c.maximum_relative_drift_ppm);
	constexpr uint64_t ppm_scale = 1000000;

	if (interval > UINT64_MAX / drift) { return false; }

	const uint64_t product = interval * drift;
	const uint64_t drift_uncertainty = product / ppm_scale + (product % ppm_scale ? 1 : 0);
	const uint64_t lead = interval / 2 > rtt ? interval / 2 : rtt;
	const uint64_t uncertainty = rtt / 2 + (rtt & 1U) + stamp + drift_uncertainty;
	const uint64_t spent = rtt + (rtt & 1U) + stamp + drift_uncertainty;

	if (lead >= interval || uncertainty > static_cast<uint64_t>(c.maximum_clock_uncertainty_us)
	    || spent >= interval || interval - spent <= lead) { return false; }

	lead_us = lead;

	if (observation_slack_us) { *observation_slack_us = interval - spent - lead; }

	return true;
}

ConfigurationResult validateRecoveryConfiguration(const VehicleConfiguration &c)
{
	const auto scalar = scalarRanges(c);

	if (!scalar.valid()) { return scalar; }

	if (!positive(c.recovery.hold_s)) { return {ConfigurationFault::InvalidRecovery, "SLA_REC_HOLD"}; }

	// The owner's own fall slew must be able to follow the decided ramp.
	if (!positive(c.recovery.ramp) || c.recovery.ramp > c.edf_fall) { return {ConfigurationFault::InvalidRecovery, "SLA_REC_RAMP"}; }

	// Worst case starts at full EDF: hold, ramp to the recovery command, and the
	// final ramp from it to off together take hold + 1/ramp.
	const double end_s = static_cast<double>(c.recovery.hold_s) + 1.0 / static_cast<double>(c.recovery.ramp);

	if (!(end_s <= static_cast<double>(kRecoveryLimitUs) * 1.e-6)) { return {ConfigurationFault::InvalidRecovery, "SLA_REC_HOLD"}; }

	return {};
}

ConfigurationResult validateFlightConfiguration(const VehicleConfiguration &c)
{
	const auto scalar = scalarRanges(c);

	if (!scalar.valid()) { return scalar; }

	const auto stage = static_cast<FlightStage>(c.launch.stage);

	if (stage != FlightStage::Captive && stage != FlightStage::Launcher) { return {ConfigurationFault::InvalidFlight, "SLA_FLT_STAGE"}; }

	if (c.data_assessment < static_cast<int32_t>(DataAssessment::Calibrated) || c.data_record_id <= 0) {
		return {ConfigurationFault::InvalidFlight, "SLA_DATA_STATE"};
	}

	const auto recovery = validateRecoveryConfiguration(c);

	if (!recovery.valid()) { return recovery; }

	if (stage == FlightStage::Launcher) {
		if (c.launch.airspeed_instance < 0) { return {ConfigurationFault::InvalidFlight, "SLA_LAUNCH_AS_I"}; }

		if (!positive(c.launch.minimum_airspeed_m_s)) { return {ConfigurationFault::InvalidFlight, "SLA_LAUNCH_AS"}; }

		if (c.launch.airspeed_age_ms <= 0) { return {ConfigurationFault::InvalidFlight, "SLA_LAUNCH_AGE"}; }
	}

	return {};
}

ConfigurationResult validateAccelerationControlConfiguration(const VehicleConfiguration &c)
{
	const auto scalar = scalarRanges(c);

	if (!scalar.valid()) { return scalar; }

	if (c.acceleration_control.enabled != 1) { return {ConfigurationFault::InvalidAccelerationControl, "SLA_ACC_CTL_EN"}; }

	const char *gain_parameters[kAxes] {"SLA_ACC_P_X", "SLA_ACC_P_Y", "SLA_ACC_P_Z"};
	const char *limit_parameters[kAxes] {"SLA_ACC_LIM_X", "SLA_ACC_LIM_Y", "SLA_ACC_LIM_Z"};

	for (unsigned axis = 0; axis < kAxes; ++axis) {
		if (!positive(c.acceleration_control.acceleration_feedback_p[axis])) { return {ConfigurationFault::InvalidAccelerationControl, gain_parameters[axis]}; }

		if (!positive(c.acceleration_control.correction_limit[axis])) { return {ConfigurationFault::InvalidAccelerationControl, limit_parameters[axis]}; }
	}

	if (!positive(c.mass)) { return {ConfigurationFault::InvalidAccelerationControl, "SLA_MASS"}; }

	if (c.feedback.enabled != 1) { return {ConfigurationFault::InvalidAccelerationControl, "SLA_FB_EN"}; }

	const auto feedback = validateAccelerationFeedbackConfiguration(c);

	if (!feedback.valid()) { return feedback; }

	if (c.schedule.enabled != 1) { return {ConfigurationFault::InvalidAccelerationControl, "SLA_SCH_EN"}; }

	const auto schedule = validateControlSchedule(c);

	if (!schedule.valid()) { return schedule; }

	if (c.wrench.enabled != 1) { return {ConfigurationFault::InvalidAccelerationControl, "SLA_WR_EN"}; }

	const auto wrench = validateWrenchConfiguration(c);

	if (!wrench.valid()) { return wrench; }

	// Shared weights/scales are fixed throughout interpolation. Squared
	// weighted column norms are convex, so validating every corner bounds the
	// allocator's numerical scaling over the entire supported rectangle.
	WrenchModel model{};

	for (unsigned i = 0; i < kScheduleKnots; ++i) {
		for (unsigned j = 0; j < kScheduleKnots; ++j) {
			float reference{};

			if (!commandToThrust(c, c.schedule.edf_command[j], reference)
			    || !buildWrenchModel(c, &c.schedule.corner[i][j], reference, model)) { return {ConfigurationFault::InvalidWrench, nullptr}; }

			const auto scaling = validateVehicleWrenchAllocationScaling(c, model);

			if (!scaling.valid()) { return scaling; }
		}
	}

	const auto power = validatePowerConfiguration(c, true);

	if (!power.valid()) { return power; }

	if (c.schedule.battery_instance != c.power.battery_instance) { return {ConfigurationFault::InvalidAccelerationControl, "SLA_SCH_BAT_INST"}; }

	if (c.schedule.battery_id != c.power.battery_id) { return {ConfigurationFault::InvalidAccelerationControl, "SLA_SCH_BAT_ID"}; }

	if (!companionAdmissionConfigured(c)) { return {ConfigurationFault::InvalidAccelerationControl, "SLA_CMP_ADMIT"}; }

	uint64_t lead{};

	if (!companionClockRenewalConfigured(c.companion, lead)) { return {ConfigurationFault::InvalidAccelerationControl, "SLA_CMP_MAP_US"}; }

	if (c.cycle_min_us <= 0 || c.cycle_max_us <= c.cycle_min_us) { return {ConfigurationFault::InvalidAccelerationControl, "SLA_CYCLE_MAX"}; }

	if (!positive(c.edf_rise)) { return {ConfigurationFault::InvalidAccelerationControl, "SLA_EDF_RISE"}; }

	if (!positive(c.edf_fall)) { return {ConfigurationFault::InvalidAccelerationControl, "SLA_EDF_FALL"}; }

	for (const auto &axis : c.axis) {
		if (!positive(axis.attitude_gain) || !positive(axis.rate_limit) || !positive(axis.setpoint_accel_limit)
		    || !positive(axis.integrator_limit) || !positive(axis.moment_limit) || !positive(axis.moment_scale)) {
			return {ConfigurationFault::InvalidAccelerationControl, nullptr};
		}
	}

	return {};
}

bool activationReserveUs(const VehicleConfiguration &c, uint64_t &reserve_us)
{
	if (c.activation_ack_timeout_us <= 0 || c.first_command_budget_us <= 0) { return false; }

	const auto activation = static_cast<uint64_t>(c.activation_ack_timeout_us);
	const auto first_command = static_cast<uint64_t>(c.first_command_budget_us);

	if (activation > UINT64_MAX - first_command) { return false; }

	reserve_us = activation + first_command;
	return true;
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

	if (companion.admission_enabled < 0 || companion.admission_enabled > 1) {
		return {ConfigurationFault::ParameterRange, "SLA_CMP_ADMIT"};
	}

	if (companion.disarmed_policy < 0 || companion.disarmed_policy > 2) {
		return {ConfigurationFault::ParameterRange, "SLA_CMP_DISARM"};
	}

	const bool admission = companion.admission_enabled == 1;
	static constexpr const char *minimum_parameters[kAxes] {"SLA_CMP_AX_MIN", "SLA_CMP_AY_MIN", "SLA_CMP_AZ_MIN"};
	static constexpr const char *maximum_parameters[kAxes] {"SLA_CMP_AX_MAX", "SLA_CMP_AY_MAX", "SLA_CMP_AZ_MAX"};

	for (unsigned axis = 0; axis < kAxes; ++axis) {
		const float minimum = companion.acceleration_minimum[axis], maximum = companion.acceleration_maximum[axis];

		if (!std::isfinite(minimum) || minimum < -kMaximumCompanionAcceleration || minimum > kMaximumCompanionAcceleration) {
			return {ConfigurationFault::ParameterRange, minimum_parameters[axis]};
		}

		if (!std::isfinite(maximum) || maximum < -kMaximumCompanionAcceleration || maximum > kMaximumCompanionAcceleration) {
			return {ConfigurationFault::ParameterRange, maximum_parameters[axis]};
		}

		if (admission && minimum > maximum) { return {ConfigurationFault::InvalidController, maximum_parameters[axis]}; }
	}

	if (!std::isfinite(companion.acceleration_norm_max) || companion.acceleration_norm_max < 0.F
	    || companion.acceleration_norm_max > kMaximumCompanionAcceleration) {
		return {ConfigurationFault::ParameterRange, "SLA_CMP_A_NORM"};
	}

	if (companion.minimum_validity_us < (admission ? 1 : 0) || companion.minimum_validity_us > kMaximumMetadataAgeUs) {
		return {ConfigurationFault::InvalidTiming, "SLA_CMP_V_MIN"};
	}

	if (companion.maximum_validity_us < (admission ? 1 : 0) || companion.maximum_validity_us > kMaximumMetadataAgeUs
	    || (admission && companion.maximum_validity_us < companion.minimum_validity_us)) {
		return {ConfigurationFault::InvalidTiming, "SLA_CMP_V_MAX"};
	}

	if (companion.maximum_command_age_us < (admission ? 1 : 0) || companion.maximum_command_age_us > kMaximumMetadataAgeUs) {
		return {ConfigurationFault::InvalidTiming, "SLA_CMP_CMD_AGE"};
	}

	if (companion.maximum_future_skew_us < (admission ? 1 : 0) || companion.maximum_future_skew_us > kMaximumMetadataAgeUs) {
		return {ConfigurationFault::InvalidTiming, "SLA_CMP_FUTURE"};
	}

	if (companion.maximum_clock_uncertainty_us < (admission ? 1 : 0)
	    || companion.maximum_clock_uncertainty_us > kMaximumMetadataAgeUs) {
		return {ConfigurationFault::InvalidTiming, "SLA_CMP_CLOCK_U"};
	}

	if (companion.maximum_relative_drift_ppm < (admission ? 1 : 0) || companion.maximum_relative_drift_ppm > kMaximumRelativeDriftPpm) {
		return {ConfigurationFault::ParameterRange, "SLA_CMP_DRIFT"};
	}

	if (companion.timestamp_uncertainty_us < (admission ? 1 : 0) || companion.timestamp_uncertainty_us > kMaximumMetadataAgeUs) {
		return {ConfigurationFault::InvalidTiming, "SLA_CMP_STAMP_U"};
	}

	if (companion.maximum_round_trip_us < (admission ? 1 : 0) || companion.maximum_round_trip_us > kMaximumMetadataAgeUs) {
		return {ConfigurationFault::InvalidTiming, "SLA_CMP_RTT"};
	}

	if (companion.map_interval_us < (admission ? 1 : 0) || companion.map_interval_us > kMaximumMapIntervalUs) {
		return {ConfigurationFault::InvalidTiming, "SLA_CMP_MAP_US"};
	}

	if (!admission) { return {}; }

	if (!enabled) { return {ConfigurationFault::ParameterRange, "SLA_CMP_EN"}; }

	if (!companion.disarmed_policy) { return {ConfigurationFault::ParameterRange, "SLA_CMP_DISARM"}; }

	if (!positive(companion.acceleration_norm_max)) { return {ConfigurationFault::InvalidController, "SLA_CMP_A_NORM"}; }

	if (!positive(c.quaternion_tolerance) || c.quaternion_tolerance >= 1.F) {
		return {ConfigurationFault::InvalidController, "SLA_QUAT_TOL"};
	}

	// Match RequestGuard's nonempty box/sphere intersection. A zero-width axis
	// is permitted; no axis or norm limit is inferred from the vehicle model.
	double closest_norm_squared = 0.;

	for (unsigned axis = 0; axis < kAxes; ++axis) {
		const float minimum = companion.acceleration_minimum[axis], maximum = companion.acceleration_maximum[axis];
		const double closest = minimum > 0.F ? minimum : (maximum < 0.F ? maximum : 0.F);
		closest_norm_squared += closest * closest;
	}

	if (!std::isfinite(closest_norm_squared)
	    || std::sqrt(closest_norm_squared) > static_cast<double>(companion.acceleration_norm_max)) {
		return {ConfigurationFault::InvalidController, "SLA_CMP_A_NORM"};
	}

	return {};
}

bool companionAdmissionConfigured(const VehicleConfiguration &c)
{
	return c.companion.admission_enabled == 1 && validateCompanionConfiguration(c).valid();
}

bool makeCompanionContractConfiguration(const VehicleConfiguration &c, research_acceleration::ContractConfig &contract)
{
	if (!companionAdmissionConfigured(c)) { return false; }

	const auto &companion = c.companion;
	research_acceleration::ContractConfig candidate{};
	candidate.protocol_version = research_acceleration::kWireProtocolVersionV1;
	candidate.permitted_source = static_cast<uint64_t>(companion.source_id);
	candidate.disarmed_policy = companion.disarmed_policy == 1 ? research_acceleration::DisarmedPolicy::Reject :
				    research_acceleration::DisarmedPolicy::ValidateOnly;

	for (unsigned axis = 0; axis < kAxes; ++axis) {
		candidate.acceleration_bounds.minimum_m_s2(axis) = companion.acceleration_minimum[axis];
		candidate.acceleration_bounds.maximum_m_s2(axis) = companion.acceleration_maximum[axis];
	}

	candidate.acceleration_bounds.maximum_norm_m_s2 = companion.acceleration_norm_max;
	candidate.timing.minimum_validity_us = static_cast<uint64_t>(companion.minimum_validity_us);
	candidate.timing.maximum_validity_us = static_cast<uint64_t>(companion.maximum_validity_us);
	candidate.timing.maximum_command_age_us = static_cast<uint64_t>(companion.maximum_command_age_us);
	candidate.timing.maximum_future_skew_us = static_cast<uint64_t>(companion.maximum_future_skew_us);
	candidate.timing.maximum_clock_uncertainty_us = static_cast<uint64_t>(companion.maximum_clock_uncertainty_us);
	candidate.quaternion_norm_tolerance = c.quaternion_tolerance;
	contract = candidate;
	return true;
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

	if (c.data_assessment > static_cast<int32_t>(DataAssessment::Incomplete) && c.data_record_id <= 0) {
		return {ConfigurationFault::MissingRecord, "SLA_DATA_REC"};
	}

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

		const auto timing = benchActivationTiming(c);
		return timing.valid() ? propulsionActivationTiming(c) : timing;
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

	const bool unconfigured_timing = !c.manual_ready_wait_s && !c.selected_status_age_us && !c.actuator_permission_age_us
					 && !c.activation_ack_timeout_us && !c.first_command_budget_us;

	if (!unconfigured_timing) {
		const auto timing = manualActivationTiming(c);

		if (!timing.valid()) { return timing; }

		const auto propulsion_timing = propulsionActivationTiming(c);

		if (!propulsion_timing.valid()) { return propulsion_timing; }
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
	return std::memcmp(left.feedback.imu_position, right.feedback.imu_position, sizeof(left.feedback.imu_position)) == 0
	       && std::memcmp(&left.feedback.cutoff_hz, &right.feedback.cutoff_hz, sizeof(left.feedback.cutoff_hz)) == 0;
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

	for (unsigned axis = 0; axis < kAxes; ++axis) { append(&c.feedback.imu_position[axis]); }

	append(&c.feedback.cutoff_hz);
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

bool commandToThrust(const VehicleConfiguration &c, float command, float &thrust_n)
{
	if (!validThrustCurve(c) || !std::isfinite(command) || command < 0.F || command > 1.F) { return false; }

	const float coordinate = command * static_cast<float>(kThrustPoints - 1);
	const unsigned low = static_cast<unsigned>(coordinate);
	const float candidate = low >= kThrustPoints - 1 ? c.edf_thrust[kThrustPoints - 1]
				: c.edf_thrust[low] + (coordinate - static_cast<float>(low)) * (c.edf_thrust[low + 1] - c.edf_thrust[low]);

	if (!std::isfinite(candidate)) { return false; }

	thrust_n = candidate;
	return true;
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
