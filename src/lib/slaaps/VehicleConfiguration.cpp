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

void buildBenchProfile(const VehicleConfiguration &c, research_actuator_command::BenchOutputProfile &profile)
{
	auto &control = profile.control;
	control.mavlink_instance = static_cast<uint8_t>(c.manual_instance);
	control.source_system_id = static_cast<uint8_t>(c.manual_system);
	control.source_component_id = static_cast<uint8_t>(c.manual_component);
	control.neutral_entry_tolerance = c.centered_enter;
	control.maximum_cycle_gap_us = static_cast<uint64_t>(c.bench_gap_ms) * kUsPerMs;
	profile.lease_us = static_cast<uint64_t>(c.output_lease_ms) * kUsPerMs;
	profile.deadzone = c.bench_deadzone;

	for (unsigned i = 0; i < kCanards; ++i) {
		const auto &canard = c.canard[i];
		control.servos[i] = {canard.pwm_negative, canard.pwm_neutral, canard.pwm_positive,
				     canard.maximum_deflection_deg, canard.slew_positive * canard.maximum_deflection_deg,
				     canard.slew_negative *canard.maximum_deflection_deg
				    };

		for (unsigned axis = 0; axis < kAxes; ++axis) { control.mix[i][axis] = canard.bench_mix[axis]; }
	}
}

void buildManualConfiguration(const VehicleConfiguration &c, research_stabilized_manual::Configuration &control)
{
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
	control.allocation.regularization = c.regularization;
	control.allocation.minimum_row_norm = c.minimum_row_norm;
	control.allocation.minimum_independence = c.minimum_independence;
	control.allocation.saturation_tolerance = c.saturation_tolerance;

	for (unsigned a = 0; a < kAxes; ++a) {
		const auto &axis = c.axis[a];
		control.stick_sign(a) = axis.sign;
		control.maximum_rate_rad_s(a) = axis.rate_limit;
		control.maximum_rate_setpoint_accel_rad_s2(a) = axis.setpoint_accel_limit;
		control.maximum_measured_rate_rad_s(a) = axis.measured_rate_limit;
		control.maximum_measured_accel_rad_s2(a) = axis.measured_accel_limit;
		control.attitude_gain(a) = axis.attitude_gain;
		control.rate_p(a) = axis.rate_p;
		control.rate_i(a) = axis.rate_i;
		control.rate_d(a) = axis.rate_d;
		control.integrator_limit(a) = axis.integrator_limit;
		control.maximum_normalized_moment(a) = axis.moment_limit;
		control.allocation.axis_weight(a) = axis.allocation_weight;

		for (unsigned i = 0; i < kCanards; ++i) {
			control.allocation.effectiveness[a][i] =
				c.canard[i].moment_derivative[a] * c.canard[i].deflection_slope / axis.moment_scale;
		}
	}

	for (unsigned i = 0; i < kCanards; ++i) {
		const auto &canard = c.canard[i];
		control.allocation.trim[i] = canard.trim;
		control.allocation.minimum[i] = canard.minimum;
		control.allocation.maximum[i] = canard.maximum;
		control.allocation.positive_slew_per_s[i] = canard.slew_positive;
		control.allocation.negative_slew_per_s[i] = canard.slew_negative;
	}
}
}

ConfigurationResult validate(const VehicleConfiguration &c, ConfigurationUse use)
{
	const auto scalar = scalarRanges(c);

	if (!scalar.valid()) { return scalar; }

	if (c.data_revision <= 0) { return {ConfigurationFault::MissingRecord, "SLA_DATA_REV"}; }

	if (c.manual_instance < 0 || c.manual_system <= 0 || c.manual_component <= 0) {
		return {ConfigurationFault::ParameterRange, "SLA_IN_INST"};
	}

	if (c.output_lease_ms <= 0) { return {ConfigurationFault::InvalidTiming, "SLA_OUT_LEASE"}; }

	for (const auto &canard : c.canard) {
		if (!validServo(canard)) { return {ConfigurationFault::InvalidServo, nullptr}; }
	}

	if (use == ConfigurationUse::Bench) {
		research_actuator_command::BenchOutputProfile profile{};
		buildBenchProfile(c, profile);
		research_actuator_command::BenchControlOwner owner;

		if (c.output_lease_ms < c.bench_gap_ms
		    || c.centered_enter < c.bench_deadzone || owner.configure(profile.control) != research_actuator_command::BenchStatus::Ok) {
			return {ConfigurationFault::InvalidController, "SLA_BENCH_GAP"};
		}

		return {};
	}

	if (!positive(c.mass) || !validInertia(c)) { return {ConfigurationFault::InvalidInertia, "SLA_MASS"}; }

	if (!positive(c.airflow)) { return {ConfigurationFault::InvalidEffectiveness, "SLA_AIRSPD"}; }

	for (const auto &canard : c.canard) {
		if (!positive(canard.slope_uncertainty) || std::fabs(canard.deflection_slope) <= canard.slope_uncertainty
		    || canard.minimum >= canard.maximum || canard.trim < canard.minimum || canard.trim > canard.maximum) {
			return {ConfigurationFault::InvalidEffectiveness, nullptr};
		}

		for (float uncertainty : canard.derivative_uncertainty) {
			if (!positive(uncertainty)) { return {ConfigurationFault::InvalidEffectiveness, nullptr}; }
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

	research_stabilized_manual::Configuration control{};
	buildManualConfiguration(c, control);
	research_stabilized_manual::StabilizedManualControl owner;

	if (owner.configure(control) != research_stabilized_manual::Status::Ok) {
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
