#include <lib/slaaps/VehicleConfiguration.hpp>
#include <lib/research_stabilized_manual/StabilizedInputValidation.hpp>
#include <lib/slaaps/ActuatorRouting.hpp>
#include <lib/slaaps/EdfOwner.hpp>
#include <lib/slaaps/ManualEdfInput.hpp>
#include <lib/slaaps/ManualWorker.hpp>
#include <lib/slaaps/ManualOutputBank.hpp>
#include <lib/slaaps/ManualBankPreparation.hpp>
#include <lib/slaaps/ManualActivationBarrier.hpp>
#include <lib/slaaps/ManualActivationTransport.hpp>
#include <lib/slaaps/ObservedRoutingBinding.hpp>
#include <lib/slaaps/ManualPreparationTransport.hpp>
#include <lib/slaaps/ManualPermissionTransport.hpp>
#include <lib/slaaps/ManualTransport.hpp>
#include <lib/slaaps/AccelerationControllerPort.hpp>
#include <lib/slaaps/ReportedRouting.hpp>
#include <lib/slaaps/ControlSchedule.hpp>
#include <lib/rate_control/rate_control.hpp>
#include <output_functions.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

using namespace slaaps;
static_assert(static_cast<unsigned>(OutputFunction::SLAAPS_Canard1) == kCanardFunctionFirst,
	      "QGC canard function identity");
static_assert(static_cast<unsigned>(OutputFunction::SLAAPS_EDF) == kEdfFunction, "QGC EDF function identity");
static unsigned checks;

static void check(bool condition, const char *message)
{
	++checks;

	if (!condition) {
		std::fprintf(stderr, "FAIL: %s\n", message);
		std::exit(1);
	}
}

static bool sameFloatBits(float a, float b) { return std::memcmp(&a, &b, sizeof(float)) == 0; }

static bool near(float a, float b) { return std::fabs(a - b) < 1.e-5F; }

// Explicit synthetic fixture, never installed as firmware parameter defaults.
static VehicleConfiguration fixture()
{
	VehicleConfiguration c{};
	c.data_revision = 1;
	c.mass = 1.F;
	c.inertia[0] = .03F;
	c.inertia[1] = c.inertia[2] = .08F;
	c.airflow = 20.F;
	c.condition_thrust = 10.F;
	c.manual_source = 2;
	c.manual_instance = 2;
	c.manual_system = 255;
	c.manual_component = 190;
	c.manual_throttle_low = -1.F;
	c.manual_throttle_high = 1.F;
	c.manual_age_ms = 100;
	c.state_age_ms = 20;
	c.state_skew_ms = 10;
	c.cycle_min_us = 4000;
	c.cycle_max_us = 12000;
	c.selected_status_age_us = 100000;
	c.actuator_permission_age_us = 100000;
	c.activation_ack_timeout_us = 2000;
	c.first_command_budget_us = 6000;
	c.bench_gap_ms = 100;
	c.output_lease_ms = 100;
	c.centered_enter = .04F;
	c.centered_exit = .06F;
	c.bench_deadzone = .03F;
	c.quaternion_tolerance = .01F;
	c.regularization = 1.e-5F;
	c.minimum_row_norm = .01F;
	c.minimum_independence = .01F;
	c.saturation_tolerance = .001F;
	c.edf_voltage = 22.2F;
	c.edf_rise = 1.F;
	c.edf_fall = 2.F;
	c.edf_entry_max = .02F;
	c.power.battery_instance = 0;
	c.power.battery_id = 1;
	c.power.battery_source = 0;
	c.power.maximum_age_us = 100000;
	c.power.voltage_min = 20.F;
	c.power.voltage_max = 26.F;

	for (unsigned i = 0; i < kThrustPoints; ++i) { c.edf_thrust[i] = 5.F * static_cast<float>(i); }

	const float signs[3][4] = {{1.F, -1.F, 1.F, -1.F}, {1.F, 1.F, -1.F, -1.F}, {1.F, -1.F, -1.F, 1.F}};

	for (unsigned a = 0; a < kAxes; ++a) {
		auto &axis = c.axis[a];
		axis.input_axis = a;
		axis.moment_scale = .4F;
		axis.sign = a == 1 ? -1.F : 1.F;
		axis.rate_limit = 1.F;
		axis.setpoint_accel_limit = 20.F;
		axis.measured_rate_limit = 20.F;
		axis.measured_accel_limit = 100.F;
		axis.attitude_gain = 4.F;
		axis.rate_p = .4F;
		axis.rate_i = .05F;
		axis.rate_d = .003F;
		axis.integrator_limit = .2F;
		axis.moment_limit = 1.F;
		axis.allocation_weight = 1.F;
	}

	for (unsigned i = 0; i < kCanards; ++i) {
		auto &canard = c.canard[i];
		canard.deflection_slope = .226892802759F;
		canard.slope_uncertainty = .001F;
		canard.minimum = -1.F;
		canard.maximum = 1.F;
		canard.slew_positive = 5.F;
		canard.slew_negative = 4.F;
		canard.pwm_negative = 1100.F;
		canard.pwm_neutral = 1500.F;
		canard.pwm_positive = 1900.F;
		canard.maximum_deflection_deg = 13.F;
		canard.lag_s = .05F;
		canard.latency_s = .01F;
		canard.local_airflow = 20.F;

		for (unsigned a = 0; a < kAxes; ++a) {
			canard.moment_derivative[a] = signs[a][i] * .25F * c.axis[a].moment_scale / canard.deflection_slope;
			canard.derivative_uncertainty[a] = .001F;
			canard.bench_mix[a] = signs[a][i];
		}
	}

	return c;
}

static void manualTimingConfigurationTests()
{
	const auto c = fixture();
	check(manualActivationTimingConfigured(c), "explicit synthetic manual timing is configured");
	auto staged = c;
	staged.selected_status_age_us = staged.actuator_permission_age_us = 0;
	staged.activation_ack_timeout_us = staged.first_command_budget_us = 0;
	check(validate(staged, ConfigurationUse::StabilizedManual).valid()
	      && !manualActivationTimingConfigured(staged), "all-zero timing can stage without activation readiness");

	VehicleConfiguration timing_only{};
	timing_only.selected_status_age_us = c.selected_status_age_us;
	timing_only.actuator_permission_age_us = c.actuator_permission_age_us;
	timing_only.activation_ack_timeout_us = c.activation_ack_timeout_us;
	timing_only.first_command_budget_us = c.first_command_budget_us;
	timing_only.cycle_min_us = c.cycle_min_us;
	timing_only.cycle_max_us = c.cycle_max_us;
	check(manualActivationTimingConfigured(timing_only)
	      && !validate(timing_only, ConfigurationUse::StabilizedManual).valid(),
	      "timing helper is independent of vehicle-model validity and grants no profile acceptance");

	int32_t VehicleConfiguration::*const timing_fields[] = {
		&VehicleConfiguration::selected_status_age_us,
		&VehicleConfiguration::actuator_permission_age_us,
		&VehicleConfiguration::activation_ack_timeout_us,
		&VehicleConfiguration::first_command_budget_us
	};

	for (const auto field : timing_fields) {
		auto partial = c;
		partial.*field = 0;
		check(!manualActivationTimingConfigured(partial)
		      && validate(partial, ConfigurationUse::StabilizedManual).fault == ConfigurationFault::InvalidTiming,
		      "each missing timing bound rejects partially configured manual activation");
		check(validate(partial, ConfigurationUse::Bench).valid(), "servo-only bench does not require flight activation timing");
		auto changed = c;
		++(changed.*field);
		check(!equal(c, changed) && fingerprint(c) != fingerprint(changed),
		      "each timing field belongs to accepted-profile equality and restart identity");
		ConfigurationStore store;
		check(store.commit(c, ConfigurationUse::StabilizedManual).valid() && store.activate(),
		      "synthetic timing profile activates only through the existing store transaction");
		store.observe(changed);
		store.observe(c);
		check(!store.eligible(), "observed timing edit invalidates the immutable session even after reverting");
		changed.*field = std::numeric_limits<int32_t>::max();
		check(!manualActivationTimingConfigured(changed)
		      && validate(changed, ConfigurationUse::StabilizedManual).fault == ConfigurationFault::ParameterRange,
		      "unbounded timing metadata rejects without signed arithmetic overflow");
		changed.*field = -1;
		check(!manualActivationTimingConfigured(changed), "negative timing is never configured");
	}

	auto boundary = c;
	boundary.first_command_budget_us = boundary.cycle_min_us;
	check(!manualActivationTimingConfigured(boundary)
	      && validate(boundary, ConfigurationUse::StabilizedManual).fault == ConfigurationFault::InvalidTiming,
	      "first-command budget must exceed the minimum controller interval");
	boundary.first_command_budget_us = boundary.cycle_max_us;
	check(!manualActivationTimingConfigured(boundary), "first-command budget cannot consume the complete cycle");
	boundary = c;
	boundary.activation_ack_timeout_us = boundary.cycle_max_us - boundary.first_command_budget_us;
	check(!manualActivationTimingConfigured(boundary)
	      && validate(boundary, ConfigurationUse::StabilizedManual).fault == ConfigurationFault::InvalidTiming,
	      "activation reply timeout must be strictly below the remaining first-command interval");
	--boundary.activation_ack_timeout_us;
	check(manualActivationTimingConfigured(boundary) && validate(boundary, ConfigurationUse::StabilizedManual).valid(),
	      "one microsecond below the activation timeout boundary remains structurally configurable");
	boundary.selected_status_age_us = boundary.actuator_permission_age_us = 1000000;
	check(manualActivationTimingConfigured(boundary), "declared metadata age upper bounds are inclusive");
	boundary.selected_status_age_us = 1000001;
	check(!manualActivationTimingConfigured(boundary), "status metadata age above its declared maximum rejects");
	boundary = c;
	boundary.actuator_permission_age_us = 1000001;
	check(!manualActivationTimingConfigured(boundary), "arming metadata age above its declared maximum rejects");
	boundary = c;
	boundary.cycle_min_us = std::numeric_limits<int32_t>::min();
	check(!manualActivationTimingConfigured(boundary), "negative cycle minimum rejects before timing subtraction");
	boundary = c;
	boundary.cycle_max_us = std::numeric_limits<int32_t>::max();
	check(!manualActivationTimingConfigured(boundary), "cycle maximum must fit its existing configured bound");
}

static void powerConfigurationTests()
{
	auto c = fixture();
	check(validatePowerConfiguration(c, true).valid(), "explicit synthetic propulsion battery configuration is valid");
	c.power = {};
	check(validatePowerConfiguration(c).valid() && validate(c, ConfigurationUse::Bench).valid()
	      && !validate(c, ConfigurationUse::StabilizedManual).valid(),
	      "servo-only commissioning stays independent while manual propulsion requires explicit battery selection");
	c.edf_commission_max = .25F;
	c.activation_ack_timeout_us = 10000;
	c.first_command_budget_us = 40000;
	check(!validate(c, ConfigurationUse::EdfCommissioning).valid(), "armed EDF commissioning cannot use an unconfigured power gate");
	c.power = fixture().power;
	check(validate(c, ConfigurationUse::EdfCommissioning).valid(), "EDF commissioning accepts the same explicit power policy as manual");

	for (unsigned fault = 0; fault < 10; ++fault) {
		auto bad = c.power;

		switch (fault) {
		case 0: bad.battery_instance = -1; break;

		case 1: bad.battery_instance = 3; break;

		case 2: bad.battery_id = 0; break;

		case 3: bad.battery_source = -1; break;

		case 4: bad.battery_source = 3; break;

		case 5: bad.maximum_age_us = 0; break;

		case 6: bad.maximum_age_us = 1000001; break;

		case 7: bad.voltage_min = 0.F; break;

		case 8: bad.voltage_max = bad.voltage_min; break;

		case 9: bad.voltage_max = std::numeric_limits<float>::quiet_NaN(); break;
		}

		check(!validatePowerConfiguration(bad, true).valid(), "incomplete, invalid or nonfinite required power policy is rejected");
	}
}

static void profileTests()
{
	const auto c = fixture();
	check(!validate({}, ConfigurationUse::Bench).valid(), "blank bench configuration rejected");
	check(!validate({}, ConfigurationUse::StabilizedManual).valid(), "blank manual configuration rejected");
	check(validate(c, ConfigurationUse::Bench).valid(), "explicit synthetic bench configuration accepted");
	check(validate(c, ConfigurationUse::StabilizedManual).valid(), "explicit synthetic physical profile accepted structurally");
	research_actuator_command::BenchOutputProfile bench{};
	check(makeBenchProfile(c, bench), "convert bench profile");
	check(near(bench.control.servos[0].rise_deg_per_s, 65.F), "normalized slew converted to physical deflection rate");
	check(bench.lease_us == 100000 && bench.control.maximum_cycle_gap_us == 100000, "milliseconds converted exactly");
	research_stabilized_manual::Configuration manual{};
	check(makeManualConfiguration(c, manual), "convert manual configuration");
	check(near(manual.allocation.effectiveness[0][0], .25F), "physical moment scaling");
	check(near(manual.allocation.effectiveness[0][1], -.25F), "physical moment sign");
	check(sameFloatBits(manual.fixed_simulated_thrust_n, 0.F), "legacy fixture thrust never becomes propulsion");

	auto bad = c;
	bad.cycle_max_us = bad.cycle_min_us;
	check(validate(bad, ConfigurationUse::StabilizedManual).fault == ConfigurationFault::InvalidTiming,
	      "cycle expiry must extend beyond the earliest integration tick");
	bad = c;
	bad.output_lease_ms = bad.cycle_min_us / 1000;
	check(validate(bad, ConfigurationUse::StabilizedManual).fault == ConfigurationFault::InvalidTiming,
	      "initial output lease cannot expire at the first integration tick");
	bad = c;
	bad.manual_age_ms = bad.cycle_min_us / 1000;
	check(validate(bad, ConfigurationUse::StabilizedManual).fault == ConfigurationFault::InvalidTiming,
	      "initial throttle request must survive until the first integration tick");
	bad = c;
	bad.inertia[3] = .1F;
	check(validate(bad, ConfigurationUse::StabilizedManual).fault == ConfigurationFault::InvalidInertia,
	      "indefinite full inertia rejected");
	bad = c;
	bad.inertia[0] = 1.F;
	check(validate(bad, ConfigurationUse::StabilizedManual).fault == ConfigurationFault::InvalidInertia,
	      "positive definite but impossible rigid-body inertia rejected");
	bad = c;
	bad.canard[0].pwm_neutral = 1000.F;
	check(!validate(bad, ConfigurationUse::Bench).valid(), "unordered calibration rejected");
	bad = c;
	bad.canard[0].pwm_neutral = 1500.5F;
	check(!validate(bad, ConfigurationUse::Bench).valid(), "fractional PWM calibration rejected");
	bad = c;
	bad.canard[0].maximum_deflection_deg = 14.F;
	check(!validate(bad, ConfigurationUse::Bench).valid(), "preserve existing bench travel limit");
	bad = c;
	bad.output_lease_ms = 99;
	check(!validate(bad, ConfigurationUse::Bench).valid(), "lease shorter than admitted bench gap rejected");
	bad = c;
	bad.canard[0].pwm_negative = 1900.F;
	bad.canard[0].pwm_positive = 1100.F;
	check(validate(bad, ConfigurationUse::Bench).valid(), "reversed servo calibration accepted without extra reversal");
	bad = c;
	bad.canard[0].slope_uncertainty = .3F;
	check(!validate(bad, ConfigurationUse::StabilizedManual).valid(), "uncertain slope sign rejected");
	bad = c;

	for (auto &canard : bad.canard) { canard.moment_derivative[2] = canard.moment_derivative[1]; }

	check(!validate(bad, ConfigurationUse::StabilizedManual).valid(), "rank deficient physical allocation rejected by actual allocator");
	const auto sentinel = manual;
	check(!makeManualConfiguration(bad, manual), "bad adapter input rejected");
	check(sameFloatBits(manual.allocation.effectiveness[0][0], sentinel.allocation.effectiveness[0][0]),
	      "failed conversion preserves caller output");
	bad = c;
	bad.mass = 0.F;
	check(validate(bad, ConfigurationUse::Bench).valid(), "servo bench does not require a flight plant");
	check(!validate(bad, ConfigurationUse::StabilizedManual).valid(), "manual flight requires plant data");
}

static void benchTravelBoundsTests()
{
	using namespace research_actuator_command;
	const BenchServoCalibration legacy{1100.F, 1500.F, 1900.F, 13.F, 65.F, 52.F};
	check(near(legacy.minimum_normalized, -1.F) && near(legacy.maximum_normalized, 1.F),
	      "legacy six-field servo calibration retains full normalized travel");
	auto c = fixture();

	for (auto &canard : c.canard) {
		canard.minimum = -.25F;
		canard.maximum = .5F;
		canard.trim = .2F;
		canard.slew_positive = canard.slew_negative = 100.F; // Explicit synthetic bounds test, not a measured rate.
	}

	c.canard[1].pwm_negative = 1900.F;
	c.canard[1].pwm_positive = 1100.F;
	BenchOutputProfile profile{};
	check(makeBenchProfile(c, profile), "shared profile admits asymmetric travel containing physical zero");
	BenchControlOwner owner;
	check(owner.configure(profile.control) == BenchStatus::Ok, "actual bench owner configures shared travel limits");
	BenchReport report{};
	check(owner.neutralCandidate(report), "bounded bench retains physical inspection neutral");

	for (unsigned i = 0; i < kCanards; ++i) {
		check(near(profile.control.servos[i].minimum_normalized, -.25F)
		      && near(profile.control.servos[i].maximum_normalized, .5F), "shared factory retains authoritative canard bounds");
		check(near(report.normalized[i + 1], 0.F) && near(report.pwm_us[i], 1500.F),
		      "bench physical zero remains calibrated neutral independently of nonzero flight trim");
	}

	ManualControlSample sample{};
	sample.timestamp_us = sample.timestamp_sample_us = 1000000;
	sample.source = ManualControlSource::Mavlink;
	sample.mavlink_instance = 2;
	sample.source_system_id = 255;
	sample.source_component_id = 190;
	sample.valid = true;
	check(owner.enable(1000000, sample) == BenchStatus::Ok, "bounded bench enables from raw centered input");

	for (unsigned direction = 0; direction < 2; ++direction) {
		const uint64_t now = 1020000 + direction * 20000;
		sample.timestamp_us = sample.timestamp_sample_us = now;
		sample.axes[0] = direction == 0 ? 1.F : -1.F;
		check(owner.cycle(now, sample, report) == BenchStatus::Ok, "actual bench limits both travel directions");

		for (unsigned i = 0; i < kCanards; ++i) {
			const float command = c.canard[i].bench_mix[0] * sample.axes[0] > 0.F ? .5F : -.25F;
			const float calibration_sign = i == 1 ? -1.F : 1.F;
			check(near(report.normalized[i + 1], command)
			      && near(report.pwm_us[i], 1500.F + 400.F * calibration_sign * command),
			      "travel clamps use the original PWM calibration and reversal without rescaling the endpoints");
		}
	}

	for (unsigned fault = 0; fault < 7; ++fault) {
		auto invalid = profile.control;

		switch (fault) {
		case 0: invalid.servos[0].minimum_normalized = .1F; break;

		case 1: invalid.servos[0].maximum_normalized = -.1F; break;

		case 2: invalid.servos[0].minimum_normalized = invalid.servos[0].maximum_normalized = 0.F; break;

		case 3: invalid.servos[0].minimum_normalized = -1.1F; break;

		case 4: invalid.servos[0].maximum_normalized = 1.1F; break;

		case 5: invalid.servos[0].minimum_normalized = std::numeric_limits<float>::quiet_NaN(); break;

		case 6: invalid.servos[0].maximum_normalized = std::numeric_limits<float>::infinity(); break;
		}

		BenchControlOwner rejected;
		check(rejected.configure(invalid) == BenchStatus::InvalidConfiguration,
		      "bench rejects nonfinite, unordered, out-of-domain or physical-zero-excluding travel");
	}

	c.canard[0].minimum = .1F;
	check(!makeBenchProfile(c, profile), "shared bench rejects a travel interval excluding physical zero");
	c.canard[0].minimum = 0.F;
	check(makeBenchProfile(c, profile), "physical zero may be an endpoint of one-sided commissioning travel");
}

static void companionConfigurationTests()
{
	VehicleConfiguration empty{};
	check(empty.companion.enabled == 0 && empty.companion.mavlink_instance == -1
	      && empty.companion.source_system == 0 && empty.companion.source_component == 0
	      && empty.companion.source_id == 0 && empty.companion.maximum_age_us == 0
	      && validateCompanionConfiguration(empty).valid(),
	      "companion source defaults disabled and independently stages without a vehicle model or output authority");
	auto c = fixture();
	c.cycle_max_us = 100000;
	c.first_command_budget_us = 30000;
	c.activation_ack_timeout_us = 20000;
	c.edf_commission_max = .1F;
	const ConfigurationUse scopes[] = {ConfigurationUse::Bench, ConfigurationUse::StabilizedManual, ConfigurationUse::EdfCommissioning};
	c.companion = {0, 3, 255, 255, std::numeric_limits<int32_t>::max(), 1000000};
	check(validateCompanionConfiguration(c).valid(), "disabled companion fields may stage their full declared scalar ranges");

	for (const auto use : scopes) {
		check(validate(c, use).valid(), "disabled staged companion settings do not require a session or alter mode-specific validation");
	}

	c.companion = {1, 0, 1, 1, 1, 1};
	check(validateCompanionConfiguration(c).valid(), "enabled companion source accepts complete explicit minimum identity and age");

	for (const auto use : scopes) {
		check(validate(c, use).valid(), "complete transport source configuration validates in every scope without granting control");
	}

	for (unsigned fault = 0; fault < 14; ++fault) {
		auto bad = c;
		const char *parameter = nullptr;

		switch (fault) {
		case 0: bad.companion.enabled = 2; parameter = "SLA_CMP_EN"; break;

		case 1: bad.companion.enabled = -1; parameter = "SLA_CMP_EN"; break;

		case 2: bad.companion.mavlink_instance = -2; parameter = "SLA_CMP_INST"; break;

		case 3: bad.companion.mavlink_instance = 4; parameter = "SLA_CMP_INST"; break;

		case 4: bad.companion.mavlink_instance = -1; parameter = "SLA_CMP_INST"; break;

		case 5: bad.companion.source_system = 0; parameter = "SLA_CMP_SYS"; break;

		case 6: bad.companion.source_system = 256; parameter = "SLA_CMP_SYS"; break;

		case 7: bad.companion.source_component = 0; parameter = "SLA_CMP_COMP"; break;

		case 8: bad.companion.source_component = 256; parameter = "SLA_CMP_COMP"; break;

		case 9: bad.companion.source_id = 0; parameter = "SLA_CMP_SOURCE"; break;

		case 10: bad.companion.source_id = -1; parameter = "SLA_CMP_SOURCE"; break;

		case 11: bad.companion.maximum_age_us = 0; parameter = "SLA_CMP_AGE"; break;

		case 12: bad.companion.maximum_age_us = -1; parameter = "SLA_CMP_AGE"; break;

		case 13: bad.companion.maximum_age_us = 1000001; parameter = "SLA_CMP_AGE"; break;
		}

		const auto direct = validateCompanionConfiguration(bad);
		check(!direct.valid() && direct.parameter && std::strcmp(direct.parameter, parameter) == 0,
		      "independent companion validation rejects incomplete enabled identity or invalid scalar values with the exact parameter");

		for (const auto use : scopes) {
			const auto result = validate(bad, use);
			check(!result.valid() && result.parameter && std::strcmp(result.parameter, parameter) == 0,
			      "invalid enabled companion settings cannot apply through any shared mode scope");
		}
	}

	c.companion = {1, 3, 255, 255, std::numeric_limits<int32_t>::max(), 1000000};
	check(validateCompanionConfiguration(c).valid(),
	      "companion source accepts exact positive 31-bit identity and explicit software age ceiling");
	c.companion.source_id = 16777216;
	auto adjacent = c;
	++adjacent.companion.source_id;
	check(validateCompanionConfiguration(adjacent).valid() && !equal(c, adjacent)
	      && fingerprint(c) != fingerprint(adjacent),
	      "adjacent payload identities above float integer precision remain distinct in shared equality and fingerprint");
	ConfigurationStore store;
	check(store.commit(c, ConfigurationUse::Bench).valid() && store.activate(), "complete companion source binds to the accepted profile");
	store.observe(adjacent);
	check(!store.eligible(), "changing companion payload identity invalidates the active accepted profile");
	store.observe(c);
	check(!store.eligible(), "restoring the old companion source cannot revive an invalidated profile");
}

static void scalarTests()
{
	const auto c = fixture();
	const auto identity = fingerprint(c);
#define SLAAPS_FLOAT(field, name, group, description, unit, minimum, maximum, initial) \
	{ auto bad = c; bad.field = std::numeric_limits<float>::quiet_NaN(); \
		check(!validate(bad, ConfigurationUse::Bench).valid(), #name " NaN rejected"); \
		bad.field = std::numeric_limits<float>::infinity(); \
		check(!validate(bad, ConfigurationUse::StabilizedManual).valid(), #name " infinity rejected"); \
		bad.field = std::nextafter(static_cast<float>(maximum), std::numeric_limits<float>::infinity()); \
		check(!validate(bad, ConfigurationUse::Bench).valid(), #name " upper bound enforced"); \
		bad = c; bad.field = std::nextafter(c.field, std::numeric_limits<float>::infinity()); \
		check(!equal(c, bad) && fingerprint(bad) != identity, #name " participates in configuration identity"); }
#define SLAAPS_INT(field, name, group, description, unit, minimum, maximum, initial) \
	{ auto bad = c; bad.field = minimum - 1; \
		check(!validate(bad, ConfigurationUse::Bench).valid(), #name " lower bound enforced"); \
		bad = c; ++bad.field; \
		check(!equal(c, bad) && fingerprint(bad) != identity, #name " participates in configuration identity"); }
#include <lib/slaaps/VehicleParameters.def>
#undef SLAAPS_INT
#undef SLAAPS_FLOAT
}

static void storeTests()
{
	const auto c = fixture();
	ConfigurationStore store;
	check(!store.activate(), "boot has no candidate");
	check(store.commit(c, ConfigurationUse::Bench).valid(), "explicit complete candidate committed");
	check(store.activate(), "validated candidate activated");
	check(store.eligible() && equal(store.configuration(), c), "active snapshot equals committed values");
	check(store.use() == ConfigurationUse::Bench, "snapshot retains its admitted mode class");
	auto changed = c;
	changed.canard[2].pwm_neutral = 1501.F;
	check(store.commit(changed, ConfigurationUse::Bench).fault == ConfigurationFault::Active,
	      "cannot replace an active snapshot");
	check(equal(store.configuration(), c), "active edits do not retune snapshot");
	store.observe(changed);
	check(!store.eligible(), "observed edit invalidates eligibility");
	store.observe(c);
	check(!store.eligible(), "edit reversal cannot unlatch invalidation");
	store.disable();
	check(!store.activate(), "disable requires deliberate recommit");
	check(store.commit(changed, ConfigurationUse::Bench).valid() && store.activate(), "explicit re-entry uses new calibration");
	store.disable();
	check(store.commit(c, ConfigurationUse::Bench).valid(), "stage another complete candidate");
	check(!store.commit({}, ConfigurationUse::Bench).valid() && !store.activate(),
	      "invalid replacement cannot leave an older candidate activatable");
}

static void thrustTests()
{
	auto c = fixture();
	float command = -1.F;

	for (unsigned i = 0; i < kThrustPoints; ++i) {
		check(thrustToCommand(c, c.edf_thrust[i], command) && near(command, .25F * i), "thrust curve knot");
	}

	check(thrustToCommand(c, 7.5F, command) && near(command, .375F), "thrust curve interpolation");
	check(!thrustToCommand(c, 21.F, command) && near(command, .375F), "out-of-range demand rejects without clamping");
	check(!thrustToCommand(c, -1.F, command), "negative thrust rejects");
	check(!thrustToCommand(c, std::numeric_limits<float>::quiet_NaN(), command), "NaN demand rejects");
	c.edf_thrust[2] = c.edf_thrust[1];
	check(!thrustToCommand(c, 7.5F, command), "non-invertible thrust curve rejects");
	c = fixture();
	c.edf_thrust[0] = 1.F;
	check(!thrustToCommand(c, 7.5F, command), "nonzero stopped-thrust curve rejects");
}

static void actualControllers()
{
	const auto c = fixture();
	research_actuator_command::BenchOutputProfile bench{};
	check(makeBenchProfile(c, bench), "bench adapter");
	research_actuator_command::BenchControlOwner owner;
	check(owner.configure(bench.control) == research_actuator_command::BenchStatus::Ok, "actual bench configures");
	research_actuator_command::ManualControlSample sample{};
	sample.timestamp_us = sample.timestamp_sample_us = 1000000;
	sample.source = research_actuator_command::ManualControlSource::Mavlink;
	sample.mavlink_instance = 2;
	sample.source_system_id = 255;
	sample.source_component_id = 190;
	sample.valid = true;
	check(owner.enable(1000000, sample) == research_actuator_command::BenchStatus::Ok, "actual bench centered entry");
	sample.timestamp_us = sample.timestamp_sample_us = 1020000;
	sample.axes[0] = .5F;
	research_actuator_command::BenchReport report{};
	check(owner.cycle(1020000, sample, report) == research_actuator_command::BenchStatus::Ok, "actual bench executes mapped profile");
	check(report.pwm_us[0] > 1500.F && report.pwm_us[1] < 1500.F, "actual bench roll directions");
	check(sameFloatBits(report.normalized[0], 0.F), "actual bench has no EDF demand");

	research_stabilized_manual::Configuration control{};
	check(makeManualConfiguration(c, control), "manual adapter");
	research_stabilized_manual::StabilizedManualControl manual;
	check(manual.configure(control) == research_stabilized_manual::Status::Ok, "actual manual configures");
	research_stabilized_manual::StateSample state{};
	state.valid = true;
	state.source_epoch = 1;
	state.attitude_timestamp_us = state.attitude_sample_us = state.rate_timestamp_us = state.rate_sample_us = 1020000;
	sample.axes[0] = 0.F;
	check(manual.enable(1020000, sample, state) == research_stabilized_manual::Status::Ok, "actual manual holds on centered entry");
	sample.timestamp_us = sample.timestamp_sample_us = 1024000;
	state.attitude_timestamp_us = state.attitude_sample_us = state.rate_timestamp_us = state.rate_sample_us = 1024000;
	sample.axes[0] = .5F;
	research_stabilized_manual::Report manual_report{};
	check(manual.cycle(1024000, sample, state, manual_report) == research_stabilized_manual::Status::Ok, "actual manual cycle");
	check(manual_report.allocation.command[0] > 0.F && manual_report.allocation.command[1] < 0.F, "actual allocator roll response");
}

static void inputMappingTests()
{
	using namespace research_actuator_command;
	auto c = fixture();

	for (unsigned fault = 0; fault < 12; ++fault) {
		auto bad = c;

		switch (fault) {
		case 0: bad.manual_source = 0; break;

		case 1: bad.manual_source = 3; break;

		case 2: bad.manual_instance = -1; break;

		case 3: bad.manual_system = 0; break;

		case 4: bad.manual_component = 0; break;

		case 5: bad.manual_source = 1; break;

		case 6: bad.axis[0].input_axis = -1; break;

		case 7: bad.axis[0].input_axis = 3; break;

		case 8: bad.axis[1].input_axis = bad.axis[0].input_axis; break;

		case 9: bad.axis[2].input_axis = bad.axis[0].input_axis; break;

		case 10: bad.axis[0].sign = 0.F; break;

		case 11: bad.axis[1].sign = .5F; break;
		}

		check(!validate(bad, ConfigurationUse::Bench).valid()
		      && !validate(bad, ConfigurationUse::StabilizedManual).valid(),
		      "both shared modes reject missing source identity or invalid attitude permutation/sign");
	}

	c.manual_source = 1;
	c.manual_instance = -1;
	c.manual_system = c.manual_component = 0;
	c.axis[0].input_axis = 2;
	c.axis[1].input_axis = 0;
	c.axis[2].input_axis = 1;
	check(validate(c, ConfigurationUse::Bench).valid() && validate(c, ConfigurationUse::StabilizedManual).valid(),
	      "explicit RC source and nonidentity body-axis permutation accepted");

	for (unsigned identity = 0; identity < 3; ++identity) {
		auto bad = c;

		if (identity == 0) { bad.manual_instance = 0; }

		if (identity == 1) { bad.manual_system = 1; }

		if (identity == 2) { bad.manual_component = 1; }

		check(!validate(bad, ConfigurationUse::Bench).valid()
		      && !validate(bad, ConfigurationUse::StabilizedManual).valid(), "RC source rejects hidden MAVLink identity");
	}

	research_actuator_command::BenchOutputProfile bench{};
	research_stabilized_manual::Configuration control{};
	check(makeBenchProfile(c, bench) && makeManualConfiguration(c, control), "both adapters capture shared RC mapping");
	check(bench.control.source_policy == ManualSourcePolicy::RcOnly && control.source_policy == ManualSourcePolicy::RcOnly,
	      "bench and flight adapters retain the same exclusive selected source");
	check(bench.control.maximum_manual_age_us == static_cast<uint64_t>(c.manual_age_ms) * 1000
	      && BenchConfiguration{}.maximum_manual_age_us == kBenchSampleAgeUs,
	      "shared bench profile captures configured freshness while legacy standalone defaults stay explicit");

	for (unsigned axis = 0; axis < kAxes; ++axis) {
		check(bench.control.stick_axis[axis] == c.axis[axis].input_axis
		      && control.stick_axis[axis] == c.axis[axis].input_axis
		      && near(bench.control.stick_sign[axis], c.axis[axis].sign)
		      && near(control.stick_sign(axis), c.axis[axis].sign), "both adapters preserve body-axis map and sign exactly once");
		BenchControlOwner bench_owner;
		research_stabilized_manual::StabilizedManualControl manual_owner;
		check(bench_owner.configure(bench.control) == BenchStatus::Ok
		      && manual_owner.configure(control) == research_stabilized_manual::Status::Ok, "actual RC mapped controllers configure");
		ManualControlSample sample{};
		sample.timestamp_us = sample.timestamp_sample_us = 1000000;
		sample.source = ManualControlSource::Rc;
		sample.valid = true;
		research_stabilized_manual::StateSample state{};
		state.valid = true;
		state.source_epoch = 1;
		state.attitude_timestamp_us = state.attitude_sample_us = state.rate_timestamp_us = state.rate_sample_us = 1000000;
		check(bench_owner.enable(1000000, sample) == BenchStatus::Ok
		      && manual_owner.enable(1000000, sample, state) == research_stabilized_manual::Status::Ok,
		      "RC mapped owners admit centered raw input");
		sample.axes[c.axis[axis].input_axis] = .08F;
		sample.timestamp_us = sample.timestamp_sample_us = 1004000;
		state.attitude_timestamp_us = state.attitude_sample_us = state.rate_timestamp_us = state.rate_sample_us = 1004000;
		research_stabilized_manual::Report manual_report{};
		check(manual_owner.cycle(1004000, sample, state, manual_report) == research_stabilized_manual::Status::Ok,
		      "actual flight controller consumes permuted raw RC input");

		for (unsigned body = 0; body < kAxes; ++body) {
			const float expected = body == axis ? c.axis[axis].sign * (.08F - c.centered_enter) / (1.F - c.centered_enter) : 0.F;
			check(near(manual_report.rate_setpoint_rad_s(body), expected), "permutation and sign affect the intended body rate once");
		}

		sample.timestamp_us = sample.timestamp_sample_us = 1020000;
		BenchReport bench_report{};
		check(bench_owner.cycle(1020000, sample, bench_report) == BenchStatus::Ok, "actual bench consumes same permuted raw RC input");

		for (unsigned canard = 0; canard < kCanards; ++canard) {
			const float expected = .08F * c.axis[axis].sign * c.canard[canard].bench_mix[axis];
			check(near(bench_report.normalized[canard + 1], expected)
			      && bench_report.normalized[canard + 1] * manual_report.allocation.command[canard] > 0.F,
			      "bench and flight produce consistent signed canard response for each mapped body axis");
		}

		// The raw timestamp is retained: remapping must not manufacture a new
		// sample or conceal a changed raw value behind controller processing.
		sample.timestamp_us = sample.timestamp_sample_us = 1004000;
		sample.axes[c.axis[axis].input_axis] = -.08F;
		check(manual_owner.monitor(1004001, sample, state) == research_stabilized_manual::Status::InputRejected,
		      "changed raw mapped axis at a held timestamp rejects flight input");
		sample.timestamp_us = sample.timestamp_sample_us = 1020000;
		check(bench_owner.cycle(1020001, sample, bench_report) == BenchStatus::InputRejected,
		      "changed raw mapped axis at a held timestamp rejects bench input");
	}

	auto servo_only = c;
	servo_only.manual_throttle_low = servo_only.manual_throttle_high = 0.F;
	check(validate(servo_only, ConfigurationUse::Bench).valid()
	      && !validate(servo_only, ConfigurationUse::StabilizedManual).valid(),
	      "servo-only profile need not invent throttle endpoints but manual flight requires a nonzero interval");
	BenchControlOwner fresh_bench;
	ManualControlSample held{};
	held.valid = true;
	held.source = ManualControlSource::Rc;
	held.timestamp_us = held.timestamp_sample_us = 1000000;
	check(fresh_bench.configure(bench.control) == BenchStatus::Ok && fresh_bench.enable(1000000, held) == BenchStatus::Ok,
	      "configured bench freshness fixture enters centered");
	BenchReport held_report{};
	check(fresh_bench.cycle(1100000, held, held_report) == BenchStatus::Ok,
	      "unchanged bench sample remains accepted at configured age boundary");
	check(fresh_bench.cycle(1100001, held, held_report) == BenchStatus::InputRejected
	      && fresh_bench.inputStatus() == ManualControlStatus::StaleSample,
	      "shared bench rejects held input just beyond configured freshness instead of legacy half-second age");
}

static void manualReadinessInputTests()
{
	using namespace research_actuator_command;
	const float tiny = std::numeric_limits<float>::denorm_min();
	const float accepted[][4] = {
		{-1.F, -1.F, 1.F, 0.F}, {1.F, -1.F, 1.F, 1.F}, {0.F, -1.F, 1.F, .5F},
		{0.F, 0.F, 1.F, 0.F}, {.25F, 0.F, 1.F, .25F}, {1.F, 1.F, -1.F, 0.F},
		{-1.F, 1.F, -1.F, 1.F}, {0.F, 1.F, -1.F, .5F}, {.2F, -.2F, .6F, .5F},
		{tiny, 0.F, tiny, 1.F}, {0.F, tiny, 0.F, 1.F}
	};

	for (const auto &row : accepted) {
		float value = .375F;
		check(normalizeManualThrottle(row[0], row[1], row[2], value) && near(value, row[3]),
		      "readiness and propulsion share forward reversed and narrow throttle normalization");
	}

	const float rejected[][3] = {
		{NAN, -1.F, 1.F}, {0.F, NAN, 1.F}, {0.F, -1.F, NAN},
		{INFINITY, -1.F, 1.F}, {0.F, -INFINITY, 1.F}, {0.F, -1.F, INFINITY},
		{-1.01F, -1.F, 1.F}, {1.01F, -1.F, 1.F}, {0.F, -1.01F, 1.F},
		{0.F, -1.F, 1.01F}, {0.F, 0.F, 0.F}, {0.F, -0.F, 0.F},
		{-.1F, 0.F, 1.F}, {.6F, -.5F, .5F}, {-.6F, .5F, -.5F}
	};

	for (const auto &row : rejected) {
		float value = .375F;
		check(!normalizeManualThrottle(row[0], row[1], row[2], value) && sameFloatBits(value, .375F),
		      "invalid raw throttle or calibration preserves caller normalization output");
	}

	for (unsigned source = 0; source < 2; ++source) {
		auto c = fixture();

		if (source == 1) {
			c.manual_source = 1;
			c.manual_instance = -1;
			c.manual_system = c.manual_component = 0;
		}

		research_stabilized_manual::Configuration control{};
		check(makeManualConfiguration(c, control), "readiness raw validator shares immutable source configuration");
		ManualControlSample sample{};
		sample.valid = true;
		sample.source = source == 0 ? ManualControlSource::Mavlink : ManualControlSource::Rc;
		sample.mavlink_instance = static_cast<uint8_t>(c.manual_instance);
		sample.source_system_id = c.manual_system;
		sample.source_component_id = c.manual_component;
		sample.timestamp_us = sample.timestamp_sample_us = 1000000;
		sample.axes[3] = c.manual_throttle_low;
		ManualControlInput attitude, readiness;
		check(research_stabilized_manual::configureStabilizedManualInput(control, attitude) == ManualControlStatus::Ok
		      && research_stabilized_manual::configureStabilizedManualInput(control, readiness, true) == ManualControlStatus::Ok,
		      "legacy attitude validation and explicit four-axis readiness both configure");
		RuntimeInput output{};
		ManualControlReport report{};
		check(readiness.evaluate(1000000, sample, output, report) == ManualControlStatus::Ok,
		      "four-axis readiness accepts original selected raw sample without propulsion grant");
		const auto original_output = output;
		const auto original_report = report;
		sample.axes[3] = .5F;
		check(readiness.evaluate(1000001, sample, output, report) == ManualControlStatus::ConflictingSample
		      && std::memcmp(&original_output, &output, sizeof(output)) == 0
		      && std::memcmp(&original_report, &report, sizeof(report)) == 0,
		      "readiness rejects changed held raw throttle and preserves caller outputs");
		sample.axes[3] = NAN;
		check(attitude.evaluate(1000000, sample, output, report) == ManualControlStatus::Ok,
		      "legacy attitude-only validator still ignores unused throttle");

		for (unsigned fault = 0; fault < 5; ++fault) {
			ManualControlInput input;
			check(research_stabilized_manual::configureStabilizedManualInput(control, input, true) == ManualControlStatus::Ok,
			      "independent readiness rejection fixture configures");
			auto bad = sample;
			bad.axes[3] = c.manual_throttle_low;
			uint64_t now = bad.timestamp_us;

			switch (fault) {
			case 0: bad.axes[3] = NAN; break;

			case 1: bad.axes[3] = 1.01F; break;

			case 2: ++bad.source_component_id; break;

			case 3: bad.source = source == 0 ? ManualControlSource::Rc : ManualControlSource::Mavlink; break;

			case 4: now += static_cast<uint64_t>(c.manual_age_ms) * 1000 + 1; break;
			}

			const auto previous_output = output;
			const auto previous_report = report;
			check(input.evaluate(now, bad, output, report) != ManualControlStatus::Ok
			      && std::memcmp(&previous_output, &output, sizeof(output)) == 0
			      && std::memcmp(&previous_report, &report, sizeof(report)) == 0,
			      "readiness rejects invalid throttle source identity and stale data without changing outputs");
		}
	}
}

static OutputInventory routingFixture()
{
	OutputInventory inventory{};
	inventory.count = 5;

	for (unsigned i = 0; i < inventory.count; ++i) {
		auto &channel = inventory.channels[i];
		channel.bank = i % 2 ? OutputBank::Main : OutputBank::Aux;
		channel.channel = 7 - i; // deliberately not MAIN1-4
		channel.function = i < kCanards ? kCanardFunctionFirst + i : kEdfFunction;
		channel.rate_hz = 50;
		channel.minimum_us = i < kCanards ? 1100 : 1000;
		channel.maximum_us = i < kCanards ? 1900 : 2000;
		channel.center_us = 1500;
	}

	return inventory;
}

static void routingTests()
{
	const auto c = fixture();
	const auto inventory = routingFixture();
	RoutingPlan plan{};
	check(makeRoutingPlan(c, inventory, true, plan).valid(), "canards and EDF accept mixed MAIN/AUX assignments");
	check(plan.bank_mask == 3 && plan.edf_assigned, "both driver banks participate in ownership");
	const auto reference = plan;
	unsigned permutation[] = {0, 1, 2, 3, 4};

	do {
		OutputInventory reordered{};
		reordered.count = inventory.count;

		for (unsigned i = 0; i < inventory.count; ++i) { reordered.channels[i] = inventory.channels[permutation[i]]; }

		check(makeRoutingPlan(c, reordered, true, plan).valid(), "inventory order independent");

		for (unsigned logical = 0; logical < kCanards; ++logical) {
			check(plan.canard[logical].bank == reference.canard[logical].bank
			      && plan.canard[logical].channel == reference.canard[logical].channel,
			      "logical calibration follows function identity");
		}
	} while (std::next_permutation(permutation, permutation + 5));

	auto bad = inventory;
	bad.channels[0].function = bad.channels[1].function;
	check(makeRoutingPlan(c, bad, true, plan).fault == RoutingFault::DuplicateFunction, "duplicate function rejected");
	bad = inventory;
	bad.channels[0].bank = bad.channels[1].bank;
	bad.channels[0].channel = bad.channels[1].channel;
	check(makeRoutingPlan(c, bad, true, plan).fault == RoutingFault::DuplicatePin, "duplicate physical pin rejected");
	bad = inventory;
	bad.channels[0].function = 0;
	check(makeRoutingPlan(c, bad, true, plan).fault == RoutingFault::MissingCanard, "missing canard rejected");
	bad = inventory;
	bad.channels[4].function = 0;
	check(makeRoutingPlan(c, bad, true, plan).fault == RoutingFault::MissingEdf, "flight requires EDF mapping");
	check(makeRoutingPlan(c, bad, false, plan).valid() && !plan.edf_assigned, "bench permits disconnected EDF");
	bad = inventory;
	bad.channels[0].protocol = OutputProtocol::Unsupported;
	check(makeRoutingPlan(c, bad, true, plan).fault == RoutingFault::UnsupportedProtocol, "unsupported transport rejected");
	bad = inventory;
	bad.channels[4].function = 101;
	check(makeRoutingPlan(c, bad, true, plan).fault == RoutingFault::UnsupportedFunction, "ordinary motor bypass rejected");
	bad = inventory;
	bad.channels[0].failsafe_us = 1500;
	check(makeRoutingPlan(c, bad, true, plan).fault == RoutingFault::InvalidOffValue, "unowned servo pulse rejected");
	bad = inventory;
	bad.channels[4].failsafe_us = 2000;
	check(makeRoutingPlan(c, bad, true, plan).fault == RoutingFault::InvalidOffValue, "EDF high failsafe rejected");
	bad = inventory;
	bad.channels[4].reversed = true;
	check(makeRoutingPlan(c, bad, true, plan).fault == RoutingFault::InvalidOffValue, "EDF reversal rejected");
	bad = inventory;
	bad.channels[4].disarmed_us = bad.channels[4].failsafe_us = 1000;
	check(makeRoutingPlan(c, bad, true, plan).valid(), "EDF calibrated minimum off pulse supported");
	bad = inventory;
	bad.channels[0].center_us = 1501;
	check(makeRoutingPlan(c, bad, true, plan).fault == RoutingFault::InvalidCalibration, "QGC center mismatch rejected");
	bad = inventory;
	bad.channels[0].maximum_us = 2500;
	bad.channels[0].rate_hz = 400;
	check(makeRoutingPlan(c, bad, true, plan).fault == RoutingFault::InvalidCalibration, "pulse must fit inside PWM period");
	auto reversed = c;
	reversed.canard[0].pwm_negative = 1900;
	reversed.canard[0].pwm_positive = 1100;
	bad = inventory;
	bad.channels[0].reversed = true;
	check(makeRoutingPlan(reversed, bad, true, plan).valid(), "one QGC reversal matches reversed logical calibration");
	bad = inventory;
	bad.count = kMaximumOutputChannels + 1;
	check(makeRoutingPlan(c, bad, true, plan).fault == RoutingFault::InvalidInventory, "oversized inventory rejected before indexing");
	plan = reference;
	check(!makeRoutingPlan(c, bad, true, plan).valid() && plan.edf.channel == reference.edf.channel,
	      "failed routing preserves caller plan");
}

static EdfPermission permission(uint64_t now, ControlMode mode = ControlMode::Manual)
{
	EdfPermission p{};
	p.timestamp_us = now;
	p.deadline_us = now + 100000;
	p.session = 1000;
	p.source = 17;
	p.configuration = 31;
	p.mode = mode;
	p.armed = p.flight_admitted = p.healthy = p.measured_condition_valid = true;
	return p;
}

static EdfRequest request(const EdfPermission &p, uint64_t now, uint64_t sequence, float value)
{
	EdfRequest r{};
	r.session = p.session;
	r.source = p.source;
	r.configuration = p.configuration;
	r.mode = p.mode;
	r.units = p.mode == ControlMode::Manual ? EdfUnits::Normalized : EdfUnits::Newtons;
	r.timestamp_us = now;
	r.deadline_us = now + 90000;
	r.sequence = sequence;
	r.value = value;
	return r;
}

static void permissionWireTests()
{
	ManualPermissionSample input{permission(1000), 1};
	input.permission.configuration = 12;
	slaaps_manual_permission_s wire{};
	ManualPermissionSample output{};
	check(encodeManualPermission(1100, input, 100000, wire)
	      && decodeManualPermission(1200, wire, 100000, output)
	      && output.permission.timestamp_us == 1000 && output.permission.deadline_us == 101000,
	      "permission transport preserves original decision time and expiry");
	input.permission.flight_admitted = false;
	check(encodeManualPermission(1100, input, 100000, wire) && decodeManualPermission(1200, wire, 100000, output)
	      && !output.permission.flight_admitted, "denied permission remains denied after transport");

	for (unsigned scenario = 0; scenario < 7; ++scenario) {
		auto bad = wire;

		switch (scenario) {
		case 0: bad.sequence = 0; break;

		case 1: bad.timestamp = 1201; break;

		case 2: bad.deadline = 1200; break;

		case 3: bad.deadline = 101001; break;

		case 4: bad.mode = 0; break;

		case 5: bad.session = 1001; break;

		case 6: bad.timestamp_sample = 1101; break;
		}

		output.sequence = 77;
		check(!decodeManualPermission(1200, bad, 100000, output) && output.sequence == 77,
		      "invalid permission syntax cannot replace caller decision");
	}
}

static void manualEdfTests()
{
	using namespace research_actuator_command;
	const auto c = fixture();
	const auto p = permission(1000);
	ManualControlSample sample{};
	sample.timestamp_us = sample.timestamp_sample_us = 1000;
	sample.valid = true;
	sample.source = ManualControlSource::Mavlink;
	sample.mavlink_instance = c.manual_instance;
	sample.source_system_id = c.manual_system;
	sample.source_component_id = c.manual_component;
	sample.axes[3] = -1.F;
	ManualEdfInput adapter;
	EdfRequest r{};
	check(adapter.evaluate(1000, sample, r) == ManualEdfStatus::NotConfigured, "throttle needs explicit configuration");
	check(adapter.configure(c, p), "configure selected throttle adapter");
	EdfOwner owner;
	check(owner.begin(1000, c, p) == EdfFault::None, "adapter fixture admitted owner");
	check(adapter.evaluate(1000, sample, r) == ManualEdfStatus::Request && near(r.value, 0.F), "negative one means zero throttle");
	check(r.session == p.session && r.source == p.source && r.configuration == p.configuration
	      && r.timestamp_us == 1000 && r.deadline_us == 101000 && r.sequence == 1, "request preserves immutable identity and sample time");
	check(owner.accept(1000, p, r) == EdfFault::None, "real owner accepts adapter low entry");
	sample.timestamp_us = 2000;
	check(adapter.evaluate(2000, sample, r) == ManualEdfStatus::Held && r.sequence == 1 && r.deadline_us == 101000,
	      "republished held throttle cannot renew request");
	sample.timestamp_us = sample.timestamp_sample_us = 11000;
	sample.axes[3] = 1.F;
	check(adapter.evaluate(11000, sample, r) == ManualEdfStatus::Request && near(r.value, 1.F) && r.sequence == 2,
	      "fresh full throttle request");
	check(owner.accept(11000, p, r) == EdfFault::None, "real owner accepts fresh demand");
	check(near(owner.sample(21000, p).normalized, .01F), "adapter demand retains owner slew limit");
	check(owner.sample(101000, p).disposition == EdfDisposition::RecoveryRequired, "permission still expires independently");

	for (unsigned fault = 0; fault < 12; ++fault) {
		ManualEdfInput input;
		check(input.configure(c, p), "fresh rejection fixture");
		auto bad = sample;
		uint64_t now = 11000;

		switch (fault) {
		case 0: bad.source_system_id--; break;

		case 1: bad.source_component_id--; break;

		case 2: bad.mavlink_instance--; break;

		case 3: bad.source = ManualControlSource::Rc; break;

		case 4: bad.valid = false; break;

		case 5: bad.axes[3] = std::numeric_limits<float>::quiet_NaN(); break;

		case 6: bad.axes[3] = 1.01F; break;

		case 7: now = 111000; break;

		case 8: bad.timestamp_us = 12000; break;

		case 9: bad.timestamp_sample_us = 999; break;

		case 10: now = 999; break;

		case 11: now = bad.timestamp_us = bad.timestamp_sample_us = UINT64_MAX - 10; break;
		}

		r.sequence = 99;
		check(input.evaluate(now, bad, r) == ManualEdfStatus::Rejected && r.sequence == 99, "invalid input cannot replace request");
		check(input.evaluate(11000, sample, r) == ManualEdfStatus::NotConfigured, "invalid input latches until explicit configuration");
	}

	ManualEdfInput centered;
	EdfOwner centered_owner;
	check(centered.configure(c, p) && centered_owner.begin(1000, c, p) == EdfFault::None, "center throttle fixture");
	sample.timestamp_us = sample.timestamp_sample_us = 1000;
	sample.axes[3] = 0.F;
	check(centered.evaluate(1000, sample, r) == ManualEdfStatus::Request && near(r.value, .5F), "center throttle is fifty percent");
	check(centered_owner.accept(1000, p, r) == EdfFault::NotLowThrottle, "centered throttle cannot enter propulsion");
	sample.axes[3] = -.5F;
	check(centered.evaluate(1001, sample, r) == ManualEdfStatus::Rejected, "changed throttle at same sample timestamp rejected");
	check(!centered.configure({}, p), "missing vehicle data cannot configure propulsion adapter");
	check(!centered.configure(c, permission(1000, ControlMode::ServoBench)), "bench cannot configure propulsion adapter");

	// A calibrated descending raw interval reverses throttle once. These are
	// explicit synthetic endpoints, never an installed vehicle default.
	auto reversed = c;
	reversed.manual_source = 1;
	reversed.manual_instance = -1;
	reversed.manual_system = reversed.manual_component = 0;
	reversed.manual_throttle_low = .75F;
	reversed.manual_throttle_high = -.5F;
	auto rc_permission = p;
	rc_permission.configuration = fingerprint(reversed);
	ManualControlSample rc{};
	rc.valid = true;
	rc.source = ManualControlSource::Rc;
	rc.timestamp_us = rc.timestamp_sample_us = 1000;
	rc.axes[3] = reversed.manual_throttle_low;
	ManualEdfInput reversed_input;
	EdfOwner reversed_owner;
	check(reversed_input.configure(reversed, rc_permission)
	      && reversed_owner.begin(1000, reversed, rc_permission) == EdfFault::None, "reversed RC throttle configures through actual EDF owner");
	check(reversed_input.evaluate(1000, rc, r) == ManualEdfStatus::Request && near(r.value, 0.F)
	      && reversed_owner.accept(1000, rc_permission, r) == EdfFault::None, "configured raw low endpoint admits zero propulsion");
	rc.timestamp_us = 1001;
	check(reversed_input.evaluate(1001, rc, r) == ManualEdfStatus::Held && r.sequence == 1 && r.timestamp_us == 1000,
	      "held reversed throttle preserves request identity");
	rc.timestamp_us = rc.timestamp_sample_us = 5000;
	rc.axes[3] = reversed.manual_throttle_high;
	check(reversed_input.evaluate(5000, rc, r) == ManualEdfStatus::Request && near(r.value, 1.F)
	      && reversed_owner.accept(5000, rc_permission, r) == EdfFault::None, "descending calibrated high endpoint requests full forward thrust");
	check(near(reversed_owner.sample(9000, rc_permission).normalized, .004F), "reversed raw mapping retains shared owner slew");
	rc.timestamp_us = rc.timestamp_sample_us = 9000;
	rc.axes[3] = .5F * (reversed.manual_throttle_low + reversed.manual_throttle_high);
	check(reversed_input.evaluate(9000, rc, r) == ManualEdfStatus::Request && near(r.value, .5F),
	      "calibrated reversed midpoint maps to half forward demand");

	for (unsigned fault = 0; fault < 5; ++fault) {
		ManualEdfInput input;
		check(input.configure(reversed, rc_permission), "fresh reversed endpoint rejection fixture");
		rc.timestamp_us = rc.timestamp_sample_us = 1000;
		rc.axes[3] = reversed.manual_throttle_low;
		check(input.evaluate(1000, rc, r) == ManualEdfStatus::Request, "accept baseline raw reversed low endpoint");
		auto bad = rc;
		bad.timestamp_us = bad.timestamp_sample_us = 2000;

		switch (fault) {
		case 0: bad.axes[3] = .751F; break;

		case 1: bad.axes[3] = -.501F; break;

		case 2: bad.timestamp_sample_us = 1000; bad.axes[3] = .5F; break;

		case 3: bad.source = ManualControlSource::Mavlink; bad.mavlink_instance = 2; bad.source_system_id = 255; bad.source_component_id = 190;
			break;

		case 4: bad.buttons = 1; bad.timestamp_sample_us = 1000; break;
		}

		r.sequence = 99;
		check(input.evaluate(2000, bad, r) == ManualEdfStatus::Rejected && r.sequence == 99,
		      "out-of-calibration, wrong-source or mutated held RC input cannot replace throttle request");
	}

	ManualEdfInput reversed_high;
	EdfOwner high_owner;
	rc.timestamp_us = rc.timestamp_sample_us = 1000;
	rc.axes[3] = reversed.manual_throttle_high;
	check(reversed_high.configure(reversed, rc_permission) && high_owner.begin(1000, reversed, rc_permission) == EdfFault::None
	      && reversed_high.evaluate(1000, rc, r) == ManualEdfStatus::Request
	      && high_owner.accept(1000, rc_permission, r) == EdfFault::NotLowThrottle,
	      "reversed configured high endpoint cannot pass low-throttle entry");
}

static void benchWorkerExpiryTests()
{
	using namespace research_actuator_command;
	auto c = fixture();
	c.mass = c.airflow = 0.F;
	c.activation_ack_timeout_us = 10000;
	c.first_command_budget_us = 40000;
	const ManualOutputBinding binding{42, {51, 52}, 3};
	auto p = permission(1000, ControlMode::ServoBench);
	p.configuration = fingerprint(c);
	p.armed = p.flight_admitted = p.measured_condition_valid = false;
	p.prearmed = true;
	ManualControlSample manual{};
	manual.valid = true;
	manual.source = ManualControlSource::Mavlink;
	manual.mavlink_instance = c.manual_instance;
	manual.source_system_id = c.manual_system;
	manual.source_component_id = c.manual_component;
	manual.timestamp_us = manual.timestamp_sample_us = 1000;
	EdfOwner edf;
	ManualWorker worker(edf);
	ManualWorkerOutput frame{};
	check(worker.beginBench(1000, c, p, manual, binding) == ManualWorkerStatus::Ready,
	      "servo worker enters with shared calibration and no flight plant");

	for (uint64_t now = 21000; now <= 61000; now += 20000) {
		p.timestamp_us = now;
		p.deadline_us = now + 100000;
		check(worker.cycleBench(now, p, manual, frame, binding) == ManualWorkerStatus::Command
		      && frame.deadline_us == 101000 && frame.edf.request_timestamp_us == 1000 && frame.edf.sequence == 1,
		      "fresh permission never renews a held raw bench sample lease");
		check(frame.mode == ControlMode::ServoBench && frame.edf.disposition == EdfDisposition::Inhibited
		      && near(frame.edf.normalized, 0.F), "servo worker carries explicit inhibited propulsion metadata");

		for (unsigned bank = 0; bank < 2; ++bank) {
			ManualOutputAck ack{p.session, p.source, p.configuration, binding.routing, binding.driver[bank],
					    frame.sequence, frame.timestamp_us, now, static_cast<uint8_t>(bank)};
			check(worker.cycleBench(now, p, manual, frame, binding, &ack)
			      == (bank ? ManualWorkerStatus::NotDue : ManualWorkerStatus::Waiting),
			      "held-input servo cycles still require every exact bank acknowledgment");
		}
	}

	p.timestamp_us = 101000;
	p.deadline_us = 201000;
	check(worker.cycleBench(101000, p, manual, frame, binding) == ManualWorkerStatus::OutputRejected,
	      "raw bench input expiry stops output despite renewed permission");
	manual.timestamp_us = manual.timestamp_sample_us = 101000;
	check(worker.beginBench(101000, c, p, manual, binding) != ManualWorkerStatus::Ready,
	      "fresh input cannot replay a consumed bench session");
}

static void edfCommissioningTests()
{
	using namespace research_actuator_command;
	auto c = fixture();
	c.mass = c.airflow = 0.F;

	for (float &inertia : c.inertia) { inertia = 0.F; }

	for (float &thrust : c.edf_thrust) { thrust = 0.F; }

	c.edf_voltage = 0.F;
	c.edf_commission_max = .05F;
	c.activation_ack_timeout_us = 10000;
	c.first_command_budget_us = 40000;
	check(validate(c, ConfigurationUse::EdfCommissioning).valid()
	      && !validate(c, ConfigurationUse::StabilizedManual).valid(),
	      "explicit EDF ground profile needs servo calibration, input, ceiling and slew but no flight plant or thrust curve");

	for (unsigned fault = 0; fault < 6; ++fault) {
		auto bad = c;

		switch (fault) {
		case 0: bad.edf_commission_max = 0.F; break;

		case 1: bad.edf_commission_max = 1.01F; break;

		case 2: bad.edf_rise = 0.F; break;

		case 3: bad.manual_throttle_high = bad.manual_throttle_low; break;

		case 4: bad.first_command_budget_us = 20000; break;

		case 5: bad.edf_fall = 0.F; break;
		}

		check(!validate(bad, ConfigurationUse::EdfCommissioning).valid(), "EDF commissioning rejects absent cap, slew, endpoints or timing");
	}

	const ManualOutputBinding binding{42, {51, 52}, 3};
	auto p = permission(1000, ControlMode::EdfBench);
	p.configuration = fingerprint(c);
	p.flight_admitted = p.measured_condition_valid = false;
	p.commissioning_admitted = true;
	ManualPermissionSample permission_sample{p, 1}, decoded{};
	slaaps_manual_permission_s wire{};
	check(encodeManualPermission(1000, permission_sample, 100000, wire)
	      && decodeManualPermission(1000, wire, 100000, decoded) && decoded.permission.commissioning_admitted
	      && decoded.permission.mode == ControlMode::EdfBench && !decoded.permission.flight_admitted,
	      "commissioning mode and its distinct nonflight permission flag survive the actual wire codec");

	for (unsigned mode = 0; mode < 3; ++mode) {
		auto bad = p;
		bad.mode = mode == 0 ? ControlMode::Manual : mode == 1 ? ControlMode::ServoBench : ControlMode::EdfBench;
		bad.commissioning_admitted = mode != 2;
		check(!ManualBankPreparation::permitted(bad, bad.mode), "flight, servo and EDF commissioning permissions cannot impersonate each other");
	}

	ManualControlSample manual{};
	manual.valid = true;
	manual.source = ManualControlSource::Mavlink;
	manual.mavlink_instance = c.manual_instance;
	manual.source_system_id = c.manual_system;
	manual.source_component_id = c.manual_component;
	manual.timestamp_us = manual.timestamp_sample_us = 1000;
	manual.axes[3] = 1.F;
	ManualEdfInput high;
	EdfRequest target{};
	check(high.configure(c, p) && high.evaluate(1000, manual, target) == ManualEdfStatus::Rejected,
	      "a small commissioning ceiling cannot turn unscaled high throttle into low-entry admission");
	manual.axes[3] = -1.F;
	EdfOwner owner;
	ManualWorker worker(owner);
	ManualWorkerOutput frame{};
	check(worker.beginEdfBench(1000, c, p, manual, binding) == ManualWorkerStatus::Ready,
	      "armed ground commissioning begins through shared EDF owner with centered low raw input");
	check(worker.cycleEdfBench(21000, p, manual, frame, binding) == ManualWorkerStatus::Command
	      && frame.mode == ControlMode::EdfBench && frame.edf.disposition == EdfDisposition::Command && near(frame.edf.normalized, 0.F),
	      "the first armed commissioning frame explicitly commands zero EDF");
	manual.timestamp_us = manual.timestamp_sample_us = 22000;
	manual.axes[3] = 1.F;
	check(worker.cycleEdfBench(22000, p, manual, frame, binding) == ManualWorkerStatus::Waiting,
	      "fresh early high throttle cannot bypass pending first handled acknowledgment");
	auto acknowledge = [&](uint64_t now) {
		for (unsigned bank = 0; bank < 2; ++bank) {
			ManualOutputAck ack{p.session, p.source, p.configuration, binding.routing, binding.driver[bank],
					    frame.sequence, frame.timestamp_us, now, static_cast<uint8_t>(bank)};
			const auto status = worker.cycleEdfBench(now, p, manual, frame, binding, &ack);
			check(status == (bank ? ManualWorkerStatus::NotDue : ManualWorkerStatus::Waiting),
			      "commissioning requires every exact bank ACK before another command");
		}
	};
	acknowledge(22000);
	check(worker.handledSequence() == 1, "both actual output portions complete the first zero command");
	check(worker.cycleEdfBench(41000, p, manual, frame, binding) == ManualWorkerStatus::Command && near(frame.edf.normalized, 0.F),
	      "held pre-first-ACK raw throttle cannot later increase commissioned EDF");
	acknowledge(41000);

	for (uint64_t now = 61000; now <= 161000; now += 20000) {
		manual.timestamp_us = manual.timestamp_sample_us = now;
		p.timestamp_us = now;
		p.deadline_us = now + 100000;
		check(worker.cycleEdfBench(now, p, manual, frame, binding) == ManualWorkerStatus::Command
		      && frame.edf.normalized <= c.edf_commission_max && frame.deadline_us <= p.deadline_us,
		      "genuinely new post-ACK throttle produces only accepted capped, leased EDF commands");

		for (float canard : frame.canards) { check(near(canard, 0.F), "armed EDF commissioning parks every calibrated canard at physical zero"); }

		acknowledge(now);
	}

	check(near(frame.edf.normalized, c.edf_commission_max), "fresh full raw throttle settles at the accepted commissioning ceiling");
	p.commissioning_admitted = false;
	check(worker.cycleEdfBench(162000, p, manual, frame, binding) == ManualWorkerStatus::PropulsionRejected
	      && owner.sample(162000, p).disposition == EdfDisposition::Inhibited,
	      "commissioning permission loss revokes EDF without inventing an airborne Manual recovery policy");
	check(worker.cycleEdfBench(163000, p, manual, frame, binding) == ManualWorkerStatus::Inactive,
	      "commissioning faults cannot revive without a new deliberate session");
}

static void scheduledControlTests()
{
	using research_stabilized_manual::Vector3f;
	using research_stabilized_manual::ScheduledControlPoint;
	using research_stabilized_manual::StabilizedManualControl;
	using research_stabilized_manual::StateSample;
	using research_stabilized_manual::Report;
	using research_stabilized_manual::Status;
	using research_actuator_command::ManualControlSample;
	using research_actuator_command::ManualControlSource;
	const Vector3f old_p(.4F, .5F, .6F), old_i(.1F, .2F, .3F), old_d(.01F, .02F, .03F);
	const Vector3f new_p(.5F, .45F, .55F), new_i(.12F, .15F, .2F), new_d(.015F, .01F, .04F);
	const Vector3f rate(.1F, -.1F, .2F), setpoint(.3F, -.2F, .5F), acceleration(.4F, -.3F, .2F);
	RateControl original{};
	original.setPidGains(old_p, old_i, old_d);
	original.setIntegratorLimit(Vector3f(.5F, .5F, .5F));
	original.setFeedForwardGain(Vector3f{});
	(void)original.update(rate, setpoint, acceleration, .1F, false);
	auto before = original;
	auto transitioned = original;
	const auto expected_torque = before.update(rate, setpoint, acceleration, .01F, true);
	check(transitioned.setPidGainsBumpless(new_p, new_i, new_d, setpoint - rate, acceleration),
	      "scheduled gains compensate retained integral output for both P and D contributions");
	const auto transitioned_torque = transitioned.update(rate, setpoint, acceleration, .01F, true);

	for (unsigned axis = 0; axis < kAxes; ++axis) {
		check(near(transitioned_torque(axis), expected_torque(axis)),
		      "actual old and transitioned PID produce the same torque at identical current error and acceleration");
	}

	for (unsigned fault = 0; fault < 6; ++fault) {
		auto rejected = original;
		auto gains_p = new_p, gains_i = new_i;

		if (fault == 0) { gains_p(2) = std::numeric_limits<float>::quiet_NaN(); }

		if (fault == 1) { gains_i(2) = -1.F; }

		if (fault == 2) { gains_p(2) = 100.F; }

		if (fault == 3) { rejected.setIntegratorLimit(Vector3f(.5F, .5F, std::numeric_limits<float>::infinity())); }

		if (fault == 4) { rejected.setIntegratorLimit(Vector3f(.5F, .5F, -1.F)); }

		if (fault == 5) { rejected.setIntegratorLimit(Vector3f(.5F, .5F, .001F)); }

		rate_ctrl_status_s initial_integral{}, final_integral{};
		rejected.getRateControlStatus(initial_integral);
		check(!rejected.setPidGainsBumpless(gains_p, gains_i, new_d, setpoint - rate, acceleration),
		      "invalid late axis, infeasible compensation or invalid integral bounds reject the entire gain transition");
		rejected.getRateControlStatus(final_integral);
		check(near(initial_integral.rollspeed_integ, final_integral.rollspeed_integ)
		      && near(initial_integral.pitchspeed_integ, final_integral.pitchspeed_integ)
		      && near(initial_integral.yawspeed_integ, final_integral.yawspeed_integ),
		      "late-axis rejection cannot partially mutate any integral output");
		const auto rejected_torque = rejected.update(rate, setpoint, acceleration, .01F, true);

		for (unsigned axis = 0; axis < kAxes; ++axis) {
			check(near(rejected_torque(axis), expected_torque(axis)), "failed scheduled gain update leaves every old gain observable unchanged");
		}
	}

	const auto base = fixture();
	research_stabilized_manual::Configuration control{};
	check(makeManualConfiguration(base, control), "scheduled controller tests reuse complete shared manual calibration");
	ManualControlSample manual{};
	manual.valid = true;
	manual.source = ManualControlSource::Mavlink;
	manual.mavlink_instance = base.manual_instance;
	manual.source_system_id = base.manual_system;
	manual.source_component_id = base.manual_component;
	manual.axes[3] = -1.F;
	StateSample state{};
	state.valid = true;
	state.source_epoch = 1;
	state.rate_rad_s = Vector3f(.01F, -.005F, .002F);
	state.angular_accel_rad_s2 = Vector3f(.02F, -.01F, .005F);
	auto stamp = [&](uint64_t now) {
		manual.timestamp_us = manual.timestamp_sample_us = now;
		state.attitude_timestamp_us = state.attitude_sample_us = state.rate_timestamp_us = state.rate_sample_us = now;
	};
	ScheduledControlPoint point{control.rate_p * .9F, control.rate_i * 1.1F, control.rate_d * .8F, control.allocation.effectiveness};

	for (auto &row : point.effectiveness) { for (float &value : row) { value *= 2.F; } }

	StabilizedManualControl fixed, scheduled, not_due;
	stamp(1000);

	for (auto *owner : {&fixed, &scheduled, &not_due}) {
		check(owner->configure(control) == Status::Ok && owner->enable(1000, manual, state) == Status::Ok,
		      "real scheduled-control comparison starts from the same accepted controller history");
	}
	Report early{};
	early.timestamp_us = 77;
	stamp(2000);
	check(not_due.cycle(2000, manual, state, early, point) == Status::NotDue && early.timestamp_us == 77,
	      "a scheduled point cannot mutate the caller report or commit gains before a due control cycle");
	Report fixed_report{}, scheduled_report{}, not_due_report{};
	stamp(5000);
	check(fixed.cycle(5000, manual, state, fixed_report) == Status::Ok
	      && scheduled.cycle(5000, manual, state, scheduled_report, point) == Status::Ok
	      && not_due.cycle(5000, manual, state, not_due_report) == Status::Ok,
	      "existing and scheduled overloads execute the actual PID and allocator");

	for (unsigned axis = 0; axis < kAxes; ++axis) {
		check(near(scheduled_report.requested_moment(axis), fixed_report.requested_moment(axis)),
		      "scheduled P/D compensation preserves the old controller request at the same state and setpoint");
		check(near(not_due_report.requested_moment(axis), fixed_report.requested_moment(axis)),
		      "a prior NotDue scheduled call leaves old gains in force");
		float achieved{};

		for (unsigned canard = 0; canard < kCanards; ++canard) { achieved += point.effectiveness[axis][canard] * scheduled_report.allocation.command[canard]; }

		check(near(achieved, scheduled_report.allocation.achieved(axis)),
		      "the same successfully transitioned cycle reports authority from its new effectiveness matrix");
	}

	check(std::fabs(scheduled_report.allocation.command[0]) < std::fabs(fixed_report.allocation.command[0]) * .6F,
	      "stronger scheduled effectiveness changes actual canard allocation while requested moment stays continuous");
	check(scheduled_report.state == research_stabilized_manual::State::Hold
	      && near(scheduled_report.hold_reference(0), fixed_report.hold_reference(0)), "scheduled control retains quaternion hold history");
	const auto accepted_command = scheduled_report.allocation.command;
	stamp(9000);
	manual.axes[0] = .5F;
	check(scheduled.cycle(9000, manual, state, scheduled_report, point) == Status::Ok,
	      "scheduled controller continues stick-rate operation with accepted history");

	for (unsigned canard = 0; canard < kCanards; ++canard) {
		const float delta = scheduled_report.allocation.command[canard] - accepted_command[canard];
		check(delta <= base.canard[canard].slew_positive * .004F + 1.e-6F
		      && delta >= -base.canard[canard].slew_negative * .004F - 1.e-6F,
		      "paired scheduled gain/effectiveness transition cannot discard accepted servo slew history");
	}

	manual.axes[0] = 0.F;

	for (unsigned fault = 0; fault < 4; ++fault) {
		StabilizedManualControl rejected;
		auto bad = point;
		state.rate_rad_s(2) = fault == 3 ? 1.F : .002F;
		stamp(1000);
		check(rejected.configure(control) == Status::Ok && rejected.enable(1000, manual, state) == Status::Ok,
		      "scheduled rejection fixture has an eligible real controller");

		if (fault == 0) { bad.rate_p(2) = std::numeric_limits<float>::quiet_NaN(); }

		if (fault == 1) { bad.effectiveness[2] = bad.effectiveness[1]; }

		if (fault == 2) { bad.effectiveness[2][3] = std::numeric_limits<float>::infinity(); }

		if (fault == 3) { bad.rate_p(2) = 100.F; }

		Report report{};
		report.timestamp_us = 77;
		unsigned char untouched[sizeof(report)];
		std::memcpy(untouched, &report, sizeof(report));
		stamp(5000);
		const auto result = rejected.cycle(5000, manual, state, report, bad);
		const bool report_unchanged = std::memcmp(&report, untouched, sizeof(report)) == 0;

		if (result != (fault == 3 ? Status::GainTransitionRejected : Status::InvalidSchedule)
		    || !report_unchanged || rejected.eligible()) {
			std::fprintf(stderr, "schedule rejection case=%u status=%u report_unchanged=%u eligible=%u\n", fault,
				     static_cast<unsigned>(result), static_cast<unsigned>(report_unchanged), static_cast<unsigned>(rejected.eligible()));
		}

		check(result == (fault == 3 ? Status::GainTransitionRejected : Status::InvalidSchedule)
		      && report_unchanged && !rejected.eligible(),
		      "bad gains, nonfinite/interior-rank matrix or infeasible compensation fault before publishing a replacement report");
	}

	state.rate_rad_s(2) = .002F;

	// Complete synthetic empirical table, never vehicle defaults or a wash law.
	auto c = base;
	auto &table = c.schedule;
	table.enabled = 1;
	table.airspeed_instance = table.air_data_instance = table.battery_instance = 0;
	table.battery_id = 1;
	table.maximum_age_us = 6000;
	table.indicated_airspeed_m_s[0] = 10.F;
	table.indicated_airspeed_m_s[1] = 30.F;
	table.edf_command[0] = 0.F;
	table.edf_command[1] = 1.F;
	table.density_min = 1.F;
	table.density_max = 1.4F;
	table.voltage_min = 20.F;
	table.voltage_max = 25.F;
	table.filter_time_s = .02F;
	table.airspeed_filter_rate = 100.F;
	table.edf_filter_rate = 10.F;
	table.airspeed_transient_rate = 1000.F;
	table.edf_transient_rate = 10.F;

	for (unsigned i = 0; i < 2; ++i) {
		for (unsigned j = 0; j < 2; ++j) {
			auto &corner = table.corner[i][j];
			const float gain = .5F + .1F * static_cast<float>(i) + .05F * static_cast<float>(j);
			const float authority = 2.F + static_cast<float>(i) + .5F * static_cast<float>(j);

			for (unsigned axis = 0; axis < kAxes; ++axis) {
				corner.rate_p[axis] = c.axis[axis].rate_p * gain;
				corner.rate_i[axis] = c.axis[axis].rate_i * gain;
				corner.rate_d[axis] = c.axis[axis].rate_d * gain;

				for (unsigned canard = 0; canard < kCanards; ++canard) {
					corner.moment_derivative[axis][canard] = c.canard[canard].moment_derivative[axis] * authority;
				}
			}
		}
	}

	check(validate(c, ConfigurationUse::StabilizedManual).valid(),
	      "explicit paired four-corner schedule validates through the shared manual profile");

	for (bool change_instance : {false, true}) {
		auto mismatched = c;

		if (change_instance) { ++mismatched.schedule.battery_instance; }

		else { ++mismatched.schedule.battery_id; }

		check(!validate(mismatched, ConfigurationUse::StabilizedManual).valid(),
		      "enabled scheduling and propulsion must observe the same selected battery instance and ID");
	}

	auto condition = [](const VehicleConfiguration & profile, uint64_t observed, uint64_t sampled, float airspeed) {
		OperatingConditionSample result{};
		result.observed_us = observed;
		result.airspeed_timestamp_us = result.airspeed_sample_us = result.air_data_timestamp_us = result.air_data_sample_us
					       = result.battery_timestamp_us = sampled;
		result.deadline_us = sampled + static_cast<uint64_t>(profile.schedule.maximum_age_us) + 1;
		result.indicated_airspeed_m_s = airspeed;
		result.density_kg_m3 = 1.2F;
		result.voltage_v = 22.F;
		result.source.airspeed_revision = 31;
		result.source.air_data_revision = 32;
		result.source.battery_revision = 33;
		result.source.baro_device_id = 456;
		result.source.baro_calibration_count = 2;
		result.source.battery_id = 1;
		return result;
	};
	ControlSchedule interpolation;
	ControlSchedule::Step middle{};
	auto observed = condition(c, 1000, 1000, 20.F);
	check(interpolation.begin(1000, c, observed, .5F, middle)
	      && near(middle.control.rate_p(0), base.axis[0].rate_p * .575F)
	      && near(middle.control.effectiveness[0][0], control.allocation.effectiveness[0][0] * 2.75F),
	      "one bilinear midpoint selects matching independently specified PID and physical derivative data");
	const ManualOutputBinding binding{42, {51, 52}, 3};
	auto p = permission(1000);
	p.configuration = fingerprint(c);
	observed = condition(c, 1000, 1000, 10.F);
	stamp(1000);
	EdfOwner expiry_owner;
	ManualWorker expiry_worker(expiry_owner);
	ManualWorkerOutput frame{};
	check(expiry_worker.begin(1000, c, p, manual, state, binding, &observed) == ManualWorkerStatus::Ready,
	      "scheduled worker enters from actual low EDF and explicitly bound numerical condition");
	stamp(5000);
	check(expiry_worker.cycle(5000, p, manual, state, frame, binding, nullptr, &observed) == ManualWorkerStatus::Command
	      && frame.deadline_us == 7001, "actual combined command expiry is capped by the oldest original condition sample");
	stamp(6000);
	observed.observed_us = 6000;
	check(expiry_worker.cycle(6000, p, manual, state, frame, binding, nullptr, &observed) == ManualWorkerStatus::Waiting
	      && frame.deadline_us == 7001 && frame.sequence == 1,
	      "pending exact bank ACKs monitor held conditions without renewing original condition or command expiry");
	stamp(7001);
	check(expiry_worker.cycle(7001, p, manual, state, frame, binding, nullptr, &observed) == ManualWorkerStatus::OutputRejected,
	      "condition-capped output expiry stops the real worker even with fresh pilot and state samples");

	for (unsigned fault = 0; fault < 11; ++fault) {
		auto profile = c;

		if (fault == 9) { profile.schedule.edf_transient_rate = .1F; }

		auto grant = permission(1000);
		grant.configuration = fingerprint(profile);
		auto input = condition(profile, 1000, 1000, 10.F);
		stamp(1000);
		manual.axes[3] = -1.F;
		EdfOwner edf;
		ManualWorker worker(edf);
		ManualWorkerOutput output{};
		check(worker.begin(1000, profile, grant, manual, state, binding, &input) == ManualWorkerStatus::Ready,
		      "pending-condition rejection fixture starts one real scheduled worker");
		stamp(5000);

		if (fault == 9) { manual.axes[3] = 1.F; }

		check(worker.cycle(5000, grant, manual, state, output, binding, nullptr, &input) == ManualWorkerStatus::Command,
		      "condition rejection is exercised while actual bank handling remains pending");
		stamp(6000);
		input = condition(profile, 6000, 6000, 10.F);
		auto expected = ControlScheduleStatus::OutsideEnvelope;
		const OperatingConditionSample *selected_condition = &input;

		switch (fault) {
		case 0: selected_condition = nullptr; expected = ControlScheduleStatus::Stale; break;

		case 1: input.source.battery_revision++; expected = ControlScheduleStatus::SourceChanged; break;

		case 2: input.indicated_airspeed_m_s = 31.F; break;

		case 3: input.density_kg_m3 = .9F; break;

		case 4: input.voltage_v = 26.F; break;

		case 5: input.indicated_airspeed_m_s = 20.F; expected = ControlScheduleStatus::Transient; break;

		case 6: input.observed_us = 6001; expected = ControlScheduleStatus::Stale; break;

		case 7: input.airspeed_sample_us = 1000; input.deadline_us = 7001; input.indicated_airspeed_m_s = 10.1F;
			expected = ControlScheduleStatus::SourceChanged; break;

		case 8: input.deadline_us++; expected = ControlScheduleStatus::Stale; break;

		case 9: expected = ControlScheduleStatus::Transient; break;

		case 10: input.source.temperature_source++; expected = ControlScheduleStatus::SourceChanged; break;
		}

		output.sequence = 99;
		check(worker.cycle(6000, grant, manual, state, output, binding, nullptr, selected_condition) == ManualWorkerStatus::ConditionRejected
		      && worker.scheduleStatus() == expected && output.sequence == 99,
		      "source replacement, numerical envelope or sensor/owner-command transient faults reject while ACKs are still pending");
		check(edf.sample(6000, grant).disposition != EdfDisposition::Command
		      && worker.cycle(6001, grant, manual, state, output, binding, nullptr, &input) == ManualWorkerStatus::Inactive,
		      "scheduled condition failure revokes shared EDF and cannot resume on restored data");
	}

	manual.axes[3] = -1.F;
	auto moving = c;
	moving.schedule.maximum_age_us = 20000;
	auto moving_permission = permission(1000);
	moving_permission.configuration = fingerprint(moving);
	EdfOwner moving_edf;
	ManualWorker moving_worker(moving_edf);
	observed = condition(moving, 1000, 1000, 10.F);
	stamp(1000);
	check(moving_worker.begin(1000, moving, moving_permission, manual, state, binding, &observed) == ManualWorkerStatus::Ready,
	      "moving empirical schedule begins with accepted corner data and zero owned EDF");

	for (uint64_t now = 5000; now <= 29000; now += 4000) {
		stamp(now);
		manual.axes[3] = now == 5000 ? -1.F : 1.F;
		moving_permission.timestamp_us = now;
		moving_permission.deadline_us = now + 100000;
		observed = condition(moving, now, now, 10.F + .1F * static_cast<float>((now - 5000) / 4000));
		check(moving_worker.cycle(now, moving_permission, manual, state, frame, binding, nullptr, &observed) == ManualWorkerStatus::Command
		      && frame.deadline_us <= observed.deadline_us && moving_worker.scheduleStatus() == ControlScheduleStatus::Ready,
		      "real scheduled worker tracks changing IAS and slewed EDF with paired valid control and original condition deadlines");

		for (unsigned bank = 0; bank < 2; ++bank) {
			const ManualOutputAck ack{moving_permission.session, moving_permission.source, moving_permission.configuration,
						  binding.routing, binding.driver[bank], frame.sequence, frame.timestamp_us, now, static_cast<uint8_t>(bank)};
			check(moving_worker.cycle(now, moving_permission, manual, state, frame, binding, &ack, &observed)
			      == (bank ? ManualWorkerStatus::NotDue : ManualWorkerStatus::Waiting),
			      "each scheduled combined frame still needs both exact bank acknowledgments before the next calculation");
		}
	}

	check(frame.edf.normalized > 0.F && frame.sequence == 7 && moving_worker.handledSequence() == 7,
	      "fresh commanded propulsion and multiple empirical schedule transitions run through complete two-bank ACK cycles");
}

static void manualWorkerTests()
{
	using namespace research_actuator_command;
	using research_stabilized_manual::StateSample;
	const auto c = fixture();
	const ManualOutputBinding binding{42, {51, 52}, 3};
	auto p = permission(1000);
	p.configuration = fingerprint(c);
	ManualControlSample manual{};
	manual.valid = true;
	manual.source = ManualControlSource::Mavlink;
	manual.mavlink_instance = c.manual_instance;
	manual.source_system_id = c.manual_system;
	manual.source_component_id = c.manual_component;
	manual.axes[3] = -1.F;
	StateSample state{};
	state.valid = true;
	state.source_epoch = 1;
	auto stamp = [&](uint64_t now) {
		manual.timestamp_us = manual.timestamp_sample_us = now;
		state.attitude_timestamp_us = state.attitude_sample_us = state.rate_timestamp_us = state.rate_sample_us = now;
	};
	stamp(1000);
	EdfOwner owner;
	ManualWorker worker(owner);
	ManualWorkerOutput output{};
	check(worker.cycle(1000, p, manual, state, output, binding) == ManualWorkerStatus::Inactive
	      && worker.handledSequence() == 0, "worker starts inactive without handled output");
	check(worker.begin(1000, c, p, manual, state, binding) == ManualWorkerStatus::Ready
	      && worker.handledSequence() == 0, "combined low centered entry has no handled output");
	stamp(2000);
	check(worker.cycle(2000, p, manual, state, output, binding) == ManualWorkerStatus::NotDue && output.sequence == 0,
	      "early polls do not produce combined output");
	stamp(5000);
	manual.axes[0] = .5F;
	manual.axes[3] = 1.F;
	check(worker.cycle(5000, p, manual, state, output, binding) == ManualWorkerStatus::Command, "combined attitude propulsion cycle");
	check(output.canards[0] > 0.F && output.canards[1] < 0.F && near(output.edf.normalized, 0.F),
	      "real allocation and EDF initial slew agree");
	check(output.edf.configuration == fingerprint(c) && output.deadline_us == 17000 && output.sequence == 1,
	      "combined cycle expiry bounded by scheduler gap");
	ManualOutputAck ack{p.session, p.source, p.configuration, binding.routing, binding.driver[0],
			    output.sequence, output.timestamp_us, 5000, 0};
	check(worker.cycle(5000, p, manual, state, output, binding, &ack) == ManualWorkerStatus::Waiting
	      && worker.handledSequence() == 0,
	      "MAIN acknowledgment alone cannot release combined worker");
	ack.bank = 1;
	ack.driver = binding.driver[1];
	check(worker.cycle(5000, p, manual, state, output, binding, &ack) == ManualWorkerStatus::NotDue
	      && worker.handledSequence() == 1,
	      "both banks acknowledged at command time without reintegration");
	stamp(9000);
	manual.axes[0] = 0.F;
	check(worker.cycle(9000, p, manual, state, output, binding) == ManualWorkerStatus::Command && near(output.edf.normalized, .004F),
	      "stick release holds attitude while actual EDF owner slews");
	worker.stop();
	check(worker.handledSequence() == 0 && owner.sample(9000, p).disposition == EdfDisposition::RecoveryRequired,
	      "stop clears handled output and revokes propulsion without inventing airborne fallback");
	check(worker.begin(9000, c, p, manual, state, binding) == ManualWorkerStatus::PropulsionRejected, "worker cannot reuse a session");

	for (unsigned fault = 0; fault < 12; ++fault) {
		EdfOwner edf;
		ManualWorker w(edf);
		manual.axes[0] = 0.F;
		manual.axes[3] = -1.F;
		state.source_epoch = 1;
		state.attitude_reset_counter = 0;
		state.valid = true;
		stamp(1000);
		check(w.begin(1000, c, p, manual, state, binding) == ManualWorkerStatus::Ready, "worker fault fixture entry");
		stamp(5000);
		auto permission_now = p;
		auto expected = ManualWorkerStatus::ControlRejected;

		switch (fault) {
		case 0: manual.axes[0] = std::numeric_limits<float>::quiet_NaN(); break;

		case 1: manual.axes[3] = std::numeric_limits<float>::quiet_NaN(); expected = ManualWorkerStatus::ThrottleRejected; break;

		case 2: state.source_epoch++; break;

		case 3: state.attitude_reset_counter++; break;

		case 4: state.valid = false; break;

		case 5: permission_now.healthy = false; expected = ManualWorkerStatus::PropulsionRejected; break;

		case 6: permission_now.armed = false; expected = ManualWorkerStatus::PropulsionRejected; break;

		case 7: permission_now.kill = true; expected = ManualWorkerStatus::PropulsionRejected; break;

		case 8: permission_now.configuration++; expected = ManualWorkerStatus::PropulsionRejected; break;

		case 9: permission_now.mode = ControlMode::ServoBench; expected = ManualWorkerStatus::PropulsionRejected; break;

		case 10: permission_now.measured_condition_valid = false; expected = ManualWorkerStatus::PropulsionRejected; break;

		case 11: permission_now.deadline_us = 5000; expected = ManualWorkerStatus::PropulsionRejected; break;
		}

		output.sequence = 99;
		check(w.cycle(5000, permission_now, manual, state, output, binding) == expected && output.sequence == 99,
		      "either subsystem failure prevents combined output");
		check(edf.sample(5000, p).disposition != EdfDisposition::Command, "attitude or propulsion failure revokes shared owner");
		check(w.cycle(5001, p, manual, state, output, binding) == ManualWorkerStatus::Inactive, "worker failure latches");
	}

	for (unsigned fault = 0; fault < 17; ++fault) {
		EdfOwner edf;
		ManualWorker w(edf);
		manual.axes[0] = 0.F;
		manual.axes[3] = -1.F;
		state.source_epoch = 1;
		state.attitude_reset_counter = 0;
		state.valid = true;
		stamp(1000);
		check(w.begin(1000, c, p, manual, state, binding) == ManualWorkerStatus::Ready, "ack fixture entry");
		stamp(5000);
		check(w.cycle(5000, p, manual, state, output, binding) == ManualWorkerStatus::Command, "ack fixture pending frame");
		ManualOutputAck bad{p.session, p.source, p.configuration, binding.routing, binding.driver[0],
				    output.sequence, output.timestamp_us, 5000, 0};
		auto live_binding = binding;
		auto live_permission = p;
		auto expected = ManualWorkerStatus::OutputRejected;
		uint64_t now = 6000;

		switch (fault) {
		case 0: bad.session++; break;

		case 1: bad.source++; break;

		case 2: bad.configuration++; break;

		case 3: bad.routing++; break;

		case 4: bad.driver++; break;

		case 5: bad.sequence++; break;

		case 6: bad.command_timestamp_us++; break;

		case 7: bad.accepted_us = 6001; break;

		case 8: bad.accepted_us = 4999; break;

		case 9: bad.bank = 2; break;

		case 10:
			check(w.cycle(5000, p, manual, state, output, binding, &bad) == ManualWorkerStatus::Waiting, "first bank accepted once");
			break;

		case 11: live_binding.driver[1]++; break;

		case 12: live_binding.routing++; break;

		case 13: now = 17000; break;

		case 14: manual.axes[0] = std::numeric_limits<float>::quiet_NaN(); expected = ManualWorkerStatus::ControlRejected; break;

		case 15: manual.axes[3] = std::numeric_limits<float>::quiet_NaN(); expected = ManualWorkerStatus::ThrottleRejected; break;

		case 16: live_permission.kill = true; expected = ManualWorkerStatus::PropulsionRejected; break;
		}

		stamp(now);
		const auto sequence = output.sequence;
		check(w.cycle(now, live_permission, manual, state, output, live_binding, &bad) == expected
		      && output.sequence == sequence && w.handledSequence() == 0,
		      "ack or live health fault cannot produce a replacement or handled sequence");
		check(edf.sample(now, p).disposition != EdfDisposition::Command, "pending-output fault revokes EDF");
	}

	// A fresh throttle sample during the wait must survive a later held poll;
	// it cannot advance the target until every bank accepts the preceding frame.
	EdfOwner waiting_edf;
	ManualWorker waiting(waiting_edf);
	manual.axes[0] = 0.F;
	manual.axes[3] = -1.F;
	stamp(1000);
	check(waiting.begin(1000, c, p, manual, state, binding) == ManualWorkerStatus::Ready, "held throttle waiting fixture");
	stamp(5000);
	check(waiting.cycle(5000, p, manual, state, output, binding) == ManualWorkerStatus::Command, "initial pending combined command");
	stamp(6000);
	manual.axes[3] = 1.F;
	check(waiting.cycle(6000, p, manual, state, output, binding) == ManualWorkerStatus::Waiting
	      && output.sequence == 1 && near(waiting_edf.sample(6000, p).normalized, 0.F), "waiting preserves control frame and EDF target");
	ManualOutputAck first{p.session, p.source, p.configuration, binding.routing, binding.driver[0], 1, 5000, 6000, 0};
	check(waiting.cycle(6000, p, manual, state, output, binding, &first) == ManualWorkerStatus::Waiting, "partial ack keeps waiting");
	first.bank = 1;
	first.driver = binding.driver[1];
	first.accepted_us = 9000;
	check(waiting.cycle(9000, p, manual, state, output, binding, &first) == ManualWorkerStatus::Command
	      && output.sequence == 2 && output.edf.request_timestamp_us == 6000, "held pending throttle retains original timestamp after ack");
	check(near(waiting_edf.sample(13000, p).normalized, .004F), "queued throttle target starts slew only after complete ack");

}

static void asynchronousManualEntryTests()
{
	using namespace research_actuator_command;
	const auto c = fixture();
	const ManualOutputBinding binding{42, {51, 52}, 3};
	constexpr uint64_t entry = 1000000, sample_time = 1002000, decision = 1003000;
	const uint64_t raw_deadline = sample_time + static_cast<uint64_t>(c.output_lease_ms) * 1000;
	auto grant = permission(1002500);
	grant.session = entry;
	grant.configuration = fingerprint(c);
	ManualControlSample manual{};
	manual.valid = true;
	manual.source = ManualControlSource::Mavlink;
	manual.mavlink_instance = c.manual_instance;
	manual.source_system_id = c.manual_system;
	manual.source_component_id = c.manual_component;
	manual.timestamp_sample_us = sample_time;
	manual.timestamp_us = 1002100;
	manual.axes[3] = c.manual_throttle_low;
	ManualEdfInput input;
	EdfOwner owner;
	EdfRequest command{};
	check(input.configure(c, grant) && owner.begin(decision, c, grant) == EdfFault::None,
	      "asynchronous throttle entry keeps distinct session sample permission and decision clocks");
	check(input.evaluate(decision, manual, command) == ManualEdfStatus::Request
	      && command.timestamp_us == sample_time && command.deadline_us == raw_deadline
	      && owner.accept(decision, grant, command) == EdfFault::None,
	      "fresh post-entry sample preceding permission publication and begin decision is admitted unchanged");
	manual.timestamp_us = 1004000;
	check(input.evaluate(1005000, manual, command) == ManualEdfStatus::Held
	      && command.timestamp_us == sample_time && command.deadline_us == raw_deadline,
	      "republishing the asynchronous first sample never renews its original raw deadline");
	auto fresh_permission = grant;
	fresh_permission.timestamp_us = 1100000;
	fresh_permission.deadline_us = 1200000;
	const auto held = owner.sample(raw_deadline - 1, fresh_permission);
	check(held.disposition == EdfDisposition::Command && held.request_timestamp_us == sample_time
	      && held.deadline_us == raw_deadline, "fresh permission cannot redate or extend the first asynchronous request");
	manual.timestamp_us = raw_deadline;
	command.sequence = 99;
	check(input.evaluate(raw_deadline, manual, command) == ManualEdfStatus::Rejected && command.sequence == 99,
	      "asynchronous held input expires at its raw deadline while preserving caller output");
	check(owner.sample(raw_deadline, fresh_permission).disposition == EdfDisposition::RecoveryRequired
	      && owner.fault() == EdfFault::Expired, "asynchronous request expires independently of refreshed permission");
	check(owner.accept(raw_deadline + 1, fresh_permission, request(fresh_permission, raw_deadline + 1, 2, 0.F)) == EdfFault::Expired,
	      "new traffic cannot recover the expired owner without a deliberate new session");

	EdfOwner waiting;
	check(waiting.begin(decision, c, grant) == EdfFault::None, "no-request timeout starts at actual owner begin");
	check(waiting.sample(decision + 99999, fresh_permission).disposition == EdfDisposition::RecoveryRequired
	      && waiting.fault() == EdfFault::None, "permission publication age does not prematurely consume no-request allowance");
	check(waiting.sample(decision + 100000, fresh_permission).disposition == EdfDisposition::RecoveryRequired
	      && waiting.fault() == EdfFault::Expired, "no-request allowance still expires at its original decision-relative deadline");

	research_stabilized_manual::StateSample state{};
	state.valid = true;
	state.source_epoch = 1;
	state.attitude_timestamp_us = state.attitude_sample_us = state.rate_timestamp_us = state.rate_sample_us = 1002200;
	manual.timestamp_us = 1002100;
	EdfOwner worker_owner;
	ManualWorker worker(worker_owner);
	check(worker.begin(decision, c, grant, manual, state, binding) == ManualWorkerStatus::Ready,
	      "actual worker admits a selected sample captured before its separate begin decision");
	manual.timestamp_us = 1006500;
	state.attitude_timestamp_us = state.attitude_sample_us = state.rate_timestamp_us = state.rate_sample_us = 1006500;
	ManualWorkerOutput output{};
	check(worker.cycle(1007000, grant, manual, state, output, binding) == ManualWorkerStatus::Command
	      && output.timestamp_us == 1007000 && output.edf.request_timestamp_us == sample_time
	      && output.edf.deadline_us == raw_deadline && output.deadline_us <= raw_deadline,
	      "first combined command retains the asynchronous raw throttle identity and independent expiry");
	const auto first = output;
	const auto inventory = routingFixture();
	ManualOutputAck handled[2] {};

	for (unsigned bank = 0; bank < 2; ++bank) {
		ManualOutputBank consumer;
		ManualActivationAck activation{};
		check(consumer.begin(1002800, bank, c, inventory, binding, grant) == ManualBankFault::None
		      && consumer.activationAck(1002900, grant, inventory, binding, activation)
		      && activation.activated_us == 1002800 && activation.first_command_deadline_us == 1014800,
		      "activation acknowledgment retains actual bank entry separately from earlier permission and raw sample times");
		check(consumer.accept(1007000, grant, inventory, binding, first) == ManualBankFault::None,
		      "both consumers admit post-entry raw EDF input captured before bank activation");
		const auto candidate = consumer.sample(1007000, grant, inventory, binding);
		check(candidate.disposition == EdfDisposition::Command
		      && consumer.confirmHandled(1007000, grant, inventory, binding, candidate, true, handled[bank]) == ManualBankFault::None,
		      "both consumers release exact handled acknowledgments for the asynchronous first command");
		ManualOutputBank before_activation;
		auto old_frame = first;
		old_frame.timestamp_us = 1002799;
		old_frame.deadline_us = old_frame.timestamp_us + c.cycle_max_us;
		check(before_activation.begin(1002800, bank, c, inventory, binding, grant) == ManualBankFault::None
		      && before_activation.accept(1007000, grant, inventory, binding, old_frame) == ManualBankFault::InvalidFrame,
		      "a command computed before actual bank activation remains inadmissible");
		ManualOutputBank before_entry;
		old_frame = first;
		old_frame.edf.request_timestamp_us = entry - 1;
		old_frame.edf.deadline_us = entry - 1 + static_cast<uint64_t>(c.output_lease_ms) * 1000;
		check(before_entry.begin(1002800, bank, c, inventory, binding, grant) == ManualBankFault::None
		      && before_entry.accept(1007000, grant, inventory, binding, old_frame) == ManualBankFault::InvalidFrame,
		      "both consumers independently reject raw EDF samples predating deliberate entry");
	}

	check(worker.cycle(1007001, grant, manual, state, output, binding) == ManualWorkerStatus::Waiting
	      && output.sequence == first.sequence && output.deadline_us == first.deadline_us,
	      "held first command cannot bypass all-bank handling or renew the pending frame");
	auto ack = handled[0];
	check(worker.cycle(1007001, grant, manual, state, output, binding, &ack) == ManualWorkerStatus::Waiting,
	      "asynchronous first command still waits for the second bank");
	ack = handled[1];
	check(worker.cycle(1007001, grant, manual, state, output, binding, &ack) == ManualWorkerStatus::NotDue
	      && output.sequence == first.sequence && output.edf.request_timestamp_us == sample_time,
	      "all-bank handling does not manufacture a new sample or immediate replacement command");
	check(worker.cycle(first.deadline_us, grant, manual, state, output, binding) == ManualWorkerStatus::OutputRejected
	      && output.sequence == first.sequence && output.deadline_us == first.deadline_us,
	      "first asynchronous combined frame expires without replacing retained caller output");
	check(worker.cycle(first.deadline_us + 1, grant, manual, state, output, binding) == ManualWorkerStatus::Inactive,
	      "expired asynchronous worker cannot reuse its old command");

	manual.timestamp_sample_us = entry - 1;
	manual.timestamp_us = 1002100;
	ManualEdfInput previous;
	EdfOwner previous_owner;
	check(previous.configure(c, grant) && previous_owner.begin(decision, c, grant) == EdfFault::None,
	      "pre-entry raw sample fixture has fresh permission and a later publication time");
	command.sequence = 99;
	check(previous.evaluate(decision, manual, command) == ManualEdfStatus::Rejected && command.sequence == 99,
	      "republishing pre-session raw input cannot meet the new entry boundary");
	check(previous_owner.accept(decision, grant, request(grant, entry - 1, 1, 0.F)) == EdfFault::InvalidRequest,
	      "owner independently rejects a request captured before the deliberate entry");
	state.attitude_timestamp_us = state.attitude_sample_us = state.rate_timestamp_us = state.rate_sample_us = 1002200;
	EdfOwner rejected_owner;
	ManualWorker rejected(rejected_owner);
	check(rejected.begin(decision, c, grant, manual, state, binding) == ManualWorkerStatus::ThrottleRejected,
	      "real worker rejects otherwise fresh republished input from before the session");
	manual.timestamp_sample_us = sample_time;
	check(rejected.begin(decision + 1, c, grant, manual, state, binding) == ManualWorkerStatus::PropulsionRejected,
	      "failed asynchronous entry consumes the session instead of allowing replay with repaired input");
}

static ManualWorkerOutput transportFrame(uint64_t now)
{
	ManualWorkerOutput frame{};
	frame.timestamp_us = now;
	frame.deadline_us = now + 12000;
	frame.sequence = 7;
	frame.binding = {42, {51, 52}, 3};
	frame.edf = {EdfDisposition::Command, .25F, 1000, 17, 31, 3, now, now + 100000};

	for (unsigned i = 0; i < kCanards; ++i) { frame.canards[i] = .1F * static_cast<float>(i); }

	return frame;
}

static void transportTests()
{
	const auto frame = transportFrame(5000);
	slaaps_manual_command_s wire{};
	ManualWorkerOutput decoded{};
	check(encodeManualCommand(5001, frame, wire) && decodeManualCommand(5002, wire, decoded), "command wire round trip");
	check(decoded.timestamp_us == frame.timestamp_us && decoded.deadline_us == frame.deadline_us
	      && decoded.sequence == frame.sequence && sameManualOutputBinding(decoded.binding, frame.binding)
	      && decoded.edf.session == frame.edf.session && decoded.edf.source == frame.edf.source
	      && decoded.edf.configuration == frame.edf.configuration && decoded.edf.sequence == frame.edf.sequence
	      && decoded.edf.request_timestamp_us == frame.edf.request_timestamp_us && decoded.edf.deadline_us == frame.edf.deadline_us
	      && sameFloatBits(decoded.edf.normalized, frame.edf.normalized), "all command identities times and thrust survive wire format");

	for (unsigned i = 0; i < kCanards; ++i) { check(sameFloatBits(decoded.canards[i], frame.canards[i]), "canard payload preserved exactly"); }

	check(encodeManualCommand(6000, decoded, wire) && wire.timestamp_sample == 5000 && wire.deadline == 17000,
	      "republishing cannot freshen calculation or expiry");

	for (unsigned fault = 0; fault < 20; ++fault) {
		auto bad = wire;

		switch (fault) {
		case 0: bad.timestamp = 4999; break;

		case 1: bad.timestamp = 7000; break;

		case 2: bad.timestamp_sample = 0; break;

		case 3: bad.deadline = 6000; break;

		case 4: bad.sequence = 0; break;

		case 5: bad.session = 0; break;

		case 6: bad.source = 0; break;

		case 7: bad.configuration = 0; break;

		case 8: bad.routing = 0; break;

		case 9: bad.driver[0] = 0; break;

		case 10: bad.bank_mask = 4; break;

		case 11: bad.bank_mask = 1; break;

		case 12: bad.edf_sequence = 0; break;

		case 13: bad.edf_request_timestamp = 5001; break;

		case 14: bad.edf_deadline = bad.deadline - 1; break;

		case 15: bad.edf = std::numeric_limits<float>::quiet_NaN(); break;

		case 16: bad.edf = -0.01F; break;

		case 17: bad.edf = 1.01F; break;

		case 18: bad.canards[2] = std::numeric_limits<float>::quiet_NaN(); break;

		case 19: bad.canards[3] = -1.01F; break;
		}

		decoded.sequence = 99;
		check(!decodeManualCommand(6000, bad, decoded) && decoded.sequence == 99, "malformed command preserves decoder output");
	}

	for (unsigned bank = 0; bank < 2; ++bank) {
		const ManualOutputAck ack{1000, 17, 31, 42, 51 + bank, 7, 5000, 5100, static_cast<uint8_t>(bank)};
		slaaps_manual_ack_s encoded{};
		ManualOutputAck result{};
		check(encodeManualAck(5200, bank, ack, encoded) && decodeManualAck(5300, bank, encoded, result), "ack wire round trip");
		check(result.session == ack.session && result.source == ack.source && result.configuration == ack.configuration
		      && result.routing == ack.routing && result.driver == ack.driver && result.sequence == ack.sequence
		      && result.command_timestamp_us == ack.command_timestamp_us && result.accepted_us == ack.accepted_us
		      && result.bank == ack.bank, "every acknowledgment field survives wire format");
		check(!decodeManualAck(5300, 1 - bank, encoded, result), "ack cannot cross bank topic");

		for (unsigned fault = 0; fault < 12; ++fault) {
			auto bad = encoded;

			switch (fault) {
			case 0: bad.timestamp = 5099; break;

			case 1: bad.timestamp = 5400; break;

			case 2: bad.timestamp_sample = 0; break;

			case 3: bad.accepted = 4999; break;

			case 4: bad.session = 0; break;

			case 5: bad.source = 0; break;

			case 6: bad.configuration = 0; break;

			case 7: bad.routing = 0; break;

			case 8: bad.driver = 0; break;

			case 9: bad.sequence = 0; break;

			case 10: bad.bank = 2; break;

			case 11: bad.timestamp_sample = 5101; break;
			}

			result.sequence = 99;
			check(!decodeManualAck(5300, bank, bad, result) && result.sequence == 99, "invalid acknowledgment preserves decoder output");
		}
	}

	wire.sequence = 99;
	auto invalid = frame;
	invalid.edf.disposition = EdfDisposition::RecoveryRequired;
	check(!encodeManualCommand(5000, invalid, wire) && wire.sequence == 99, "fault disposition cannot be encoded as a command");
}

// Memory-only stand-in for a successful driver handling operation. Tests below
// also exercise preparation without handling and failed/mismatched confirmation.
static ManualBankFault acceptAndHandle(ManualOutputBank &bank, uint64_t now, const EdfPermission &permission,
				       const OutputInventory &inventory, const ManualOutputBinding &binding,
				       const ManualWorkerOutput &frame, ManualOutputAck &ack)
{
	slaaps_manual_command_s wire{};
	ManualWorkerOutput decoded{};

	if (!encodeManualCommand(now, frame, wire) || !decodeManualCommand(now, wire, decoded)) {
		// Invalid input still reaches the consumer fault latch in this fixture.
		return bank.accept(now, permission, inventory, binding, frame);
	}

	const auto result = bank.accept(now, permission, inventory, binding, decoded);

	if (result != ManualBankFault::None) { return result; }

	const auto handled = bank.sample(now, permission, inventory, binding);
	return bank.confirmHandled(now, permission, inventory, binding, handled, true, ack);
}

static void manualBankTests()
{
	const auto c = fixture();
	const auto inventory = routingFixture();
	const ManualOutputBinding binding{42, {51, 52}, 3};
	auto p = permission(1000);
	p.configuration = fingerprint(c);
	research_stabilized_manual::ManualSample manual{};
	manual.valid = true;
	manual.source = research_actuator_command::ManualControlSource::Mavlink;
	manual.mavlink_instance = c.manual_instance;
	manual.source_system_id = c.manual_system;
	manual.source_component_id = c.manual_component;
	manual.axes[3] = -1.F;
	research_stabilized_manual::StateSample state{};
	state.valid = true;
	state.source_epoch = 1;
	auto stamp = [&](uint64_t now) {
		manual.timestamp_us = manual.timestamp_sample_us = now;
		state.attitude_timestamp_us = state.attitude_sample_us = state.rate_timestamp_us = state.rate_sample_us = now;
	};
	stamp(1000);
	EdfOwner owner;
	ManualWorker worker(owner);
	ManualOutputBank main, aux;
	check(worker.begin(1000, c, p, manual, state, binding) == ManualWorkerStatus::Ready, "bank integration worker entry");
	check(main.begin(1000, 0, c, inventory, binding, p) == ManualBankFault::None
	      && aux.begin(1000, 1, c, inventory, binding, p) == ManualBankFault::None, "independent bank grants");
	check(main.sample(1000, p, inventory, binding).disposition == EdfDisposition::RecoveryRequired, "grant alone cannot output PWM");
	stamp(5000);
	manual.axes[0] = .5F;
	ManualWorkerOutput frame{};
	check(worker.cycle(5000, p, manual, state, frame, binding) == ManualWorkerStatus::Command, "actual worker produces bank frame");
	ManualOutputAck main_ack{}, aux_ack{};
	check(acceptAndHandle(main, 5000, p, inventory, binding, frame, main_ack) == ManualBankFault::None
	      && acceptAndHandle(aux, 5000, p, inventory, binding, frame, aux_ack) == ManualBankFault::None, "both banks validate actual worker output");
	const auto main_output = main.sample(5000, p, inventory, binding);
	const auto aux_output = aux.sample(5000, p, inventory, binding);
	check(main_output.channel_mask == ((1U << 6) | (1U << 4))
	      && aux_output.channel_mask == ((1U << 7) | (1U << 5) | (1U << 3)), "mixed arbitrary MAIN AUX channel mapping");
	check(main_output.pwm_us[6] < 1500.F && aux_output.pwm_us[7] > 1500.F && near(aux_output.pwm_us[3], 1000.F),
	      "mapped actual allocator response and stopped EDF pulse");
	check(worker.cycle(5000, p, manual, state, frame, binding, &main_ack) == ManualWorkerStatus::Waiting
	      && worker.cycle(5000, p, manual, state, frame, binding, &aux_ack) == ManualWorkerStatus::NotDue,
	      "real bank acknowledgments complete producer handoff");
	ManualWorkerOutput next{};
	stamp(9000);
	manual.axes[0] = 0.F;
	manual.axes[3] = 1.F;
	check(worker.cycle(9000, p, manual, state, next, binding) == ManualWorkerStatus::Command
	      && acceptAndHandle(main, 9000, p, inventory, binding, next, main_ack) == ManualBankFault::None
	      && acceptAndHandle(aux, 9000, p, inventory, binding, next, aux_ack) == ManualBankFault::None,
	      "fresh throttle request reaches both consumers");
	check(worker.cycle(9000, p, manual, state, next, binding, &main_ack) == ManualWorkerStatus::Waiting
	      && worker.cycle(9000, p, manual, state, next, binding, &aux_ack) == ManualWorkerStatus::NotDue,
	      "second frame completes both acknowledgments");
	stamp(13000);
	check(worker.cycle(13000, p, manual, state, next, binding) == ManualWorkerStatus::Command
	      && acceptAndHandle(main, 13000, p, inventory, binding, next, main_ack) == ManualBankFault::None
	      && acceptAndHandle(aux, 13000, p, inventory, binding, next, aux_ack) == ManualBankFault::None,
	      "slewed propulsion and hold cycle reaches both banks");
	check(near(aux.sample(13000, p, inventory, binding).pwm_us[3], 1004.F),
	      "actual EDF slew maps to nonzero forward propulsion pulse");
	check(aux.sample(24999, p, inventory, binding).disposition == EdfDisposition::Command
	      && aux.sample(25000, p, inventory, binding).disposition == EdfDisposition::RecoveryRequired,
	      "independent consumer expires when producer stops");
	check(acceptAndHandle(aux, 25000, p, inventory, binding, frame, aux_ack) == ManualBankFault::Expired,
	      "late traffic cannot revive expired bank");
	check(aux.begin(25000, 1, c, inventory, binding, p) == ManualBankFault::InvalidGrant, "bank session replay blocked");

	for (unsigned fault = 0; fault < 18; ++fault) {
		ManualOutputBank bank;
		check(bank.begin(1000, 1, c, inventory, binding, p) == ManualBankFault::None, "bank fault fixture");
		auto bad = frame;
		auto live = inventory;
		auto current_binding = binding;
		auto current_permission = p;
		uint64_t now = 5000;
		ManualBankFault expected = ManualBankFault::InvalidFrame;

		switch (fault) {
		case 0: bad.canards[0] = std::numeric_limits<float>::quiet_NaN(); break;

		case 1: bad.canards[1] = 1.01F; break;

		case 2: bad.edf.normalized = std::numeric_limits<float>::quiet_NaN(); break;

		case 3: bad.edf.normalized = .5F; break;

		case 4: bad.edf.session++; break;

		case 5: bad.edf.configuration++; break;

		case 6: bad.edf.deadline_us = bad.deadline_us - 1; break;

		case 7: bad.deadline_us++; break;

		case 8: bad.timestamp_us++; break;

		case 9: bad.sequence++; break;

		case 10: bad.binding.routing++; break;

		case 11: current_binding.driver[1]++; expected = ManualBankFault::RoutingChanged; break;

		case 12: live.channels[0].rate_hz = 60; expected = ManualBankFault::RoutingChanged; break;

		case 13: current_permission.kill = true; expected = ManualBankFault::PermissionLost; break;

		case 14: current_permission.armed = false; expected = ManualBankFault::PermissionLost; break;

		case 15: now = 999; expected = ManualBankFault::InvalidTime; break;

		case 16: now = 13000; expected = ManualBankFault::Expired; break;

		case 17: current_permission.measured_condition_valid = false; expected = ManualBankFault::PermissionLost; break;
		}

		ManualOutputAck unchanged{};
		unchanged.sequence = 99;
		check(acceptAndHandle(bank, now, current_permission, live, current_binding, bad, unchanged) == expected
		      && unchanged.sequence == 99, "bank rejects malformed frame or changed live admission without ack");
		check(bank.sample(5000, p, inventory, binding).disposition != EdfDisposition::Command, "bank fault latches without output");
	}

	ManualOutputBank duplicate;
	check(duplicate.begin(1000, 0, c, inventory, binding, p) == ManualBankFault::None
	      && acceptAndHandle(duplicate, 5000, p, inventory, binding, frame, main_ack) == ManualBankFault::None,
	      "duplicate fixture first accepted frame");
	check(acceptAndHandle(duplicate, 5000, p, inventory, binding, frame, main_ack) == ManualBankFault::InvalidFrame,
	      "duplicate frame cannot renew consumer expiry");
	auto reversed = c;
	std::swap(reversed.canard[0].pwm_negative, reversed.canard[0].pwm_positive);
	auto reversed_inventory = inventory;
	reversed_inventory.channels[0].reversed = true;
	auto reversed_permission = p;
	reversed_permission.configuration = fingerprint(reversed);
	auto reversed_frame = frame;
	reversed_frame.edf.configuration = reversed_permission.configuration;
	ManualOutputBank reverse_bank;
	check(reverse_bank.begin(1000, 1, reversed, reversed_inventory, binding, reversed_permission) == ManualBankFault::None
	      && acceptAndHandle(reverse_bank, 5000, reversed_permission, reversed_inventory, binding, reversed_frame, aux_ack) == ManualBankFault::None,
	      "reversed calibration accepted");
	check(reverse_bank.sample(5000, reversed_permission, reversed_inventory, binding).pwm_us[7] < 1500.F,
	      "physical pulse applies servo reversal once");
	ManualOutputBank prepared;
	check(prepared.begin(1000, 1, c, inventory, binding, p) == ManualBankFault::None
	      && prepared.accept(5000, p, inventory, binding, frame) == ManualBankFault::None
	      && prepared.awaitingHandling(), "preparation requires separate driver handling");
	const auto ticket = prepared.sample(5000, p, inventory, binding);
	ManualOutputAck handled_ack{};
	check(prepared.confirmHandled(6000, p, inventory, binding, ticket, true, handled_ack) == ManualBankFault::None
	      && handled_ack.accepted_us == 6000 && !prepared.awaitingHandling(), "ack records handling time rather than preparation time");
	check(prepared.confirmHandled(6000, p, inventory, binding, ticket, true, handled_ack) == ManualBankFault::HandlingRejected,
	      "same handling confirmation cannot acknowledge twice");

	for (unsigned fault = 0; fault < 17; ++fault) {
		ManualOutputBank bank;
		check(bank.begin(1000, 1, c, inventory, binding, p) == ManualBankFault::None
		      && bank.accept(5000, p, inventory, binding, frame) == ManualBankFault::None, "handling fault preparation");
		auto bad = bank.sample(5000, p, inventory, binding);
		auto live = inventory;
		auto admission = p;
		uint64_t now = 6000;
		bool success = true;
		auto expected = ManualBankFault::HandlingRejected;

		switch (fault) {
		case 0: bad.disposition = EdfDisposition::Inhibited; break;

		case 1: bad.channel_mask ^= 1U; break;

		case 2: bad.sequence++; break;

		case 3: bad.session++; break;

		case 4: bad.source++; break;

		case 5: bad.configuration++; break;

		case 6: bad.command_timestamp_us++; break;

		case 7: bad.binding.routing++; break;

		case 8: bad.binding.driver[1]++; break;

		case 9: bad.deadline_us = now; break;

		case 10: bad.deadline_us++; break;

		case 11: bad.pwm_us[3] += 1.F; break;

		case 12: bad.pwm_us[7] = std::numeric_limits<float>::quiet_NaN(); break;

		case 13: success = false; break;

		case 14: admission.kill = true; expected = ManualBankFault::PermissionLost; break;

		case 15: live.channels[0].rate_hz = 60; expected = ManualBankFault::RoutingChanged; break;

		case 16: now = 17000; expected = ManualBankFault::Expired; break;
		}

		handled_ack.sequence = 99;
		check(bank.confirmHandled(now, admission, live, binding, bad, success, handled_ack) == expected
		      && handled_ack.sequence == 99 && !bank.awaitingHandling(), "handling fault releases no acknowledgment and latches");
		check(bank.sample(now, p, inventory, binding).disposition != EdfDisposition::Command,
		      "handling fault revokes the prepared output");
	}

	ManualOutputBank premature;
	check(premature.begin(1000, 1, c, inventory, binding, p) == ManualBankFault::None
	      && premature.confirmHandled(1000, p, inventory, binding, ticket, true, handled_ack) == ManualBankFault::HandlingRejected,
	      "handling cannot precede preparation");
	ManualOutputBank replacement;
	check(replacement.begin(1000, 1, c, inventory, binding, p) == ManualBankFault::None
	      && replacement.accept(5000, p, inventory, binding, frame) == ManualBankFault::None, "pending replacement fixture");
	auto another = frame;
	another.sequence = 2;
	another.timestamp_us = 9000;
	another.deadline_us = 21000;
	check(replacement.accept(9000, p, inventory, binding, another) == ManualBankFault::InvalidFrame,
	      "new frame cannot replace an unhandled candidate");

	ManualOutputBank lifecycle;
	check(lifecycle.begin(1000, 1, c, inventory, binding, p) == ManualBankFault::None
	      && lifecycle.accept(5000, p, inventory, binding, frame) == ManualBankFault::None, "old ticket lifecycle fixture");
	const auto old_ticket = lifecycle.sample(5000, p, inventory, binding);
	lifecycle.revoke();
	auto renewed = permission(20000);
	renewed.session = 20000;
	renewed.configuration = p.configuration;
	another = frame;
	another.timestamp_us = another.edf.request_timestamp_us = 24000;
	another.deadline_us = 36000;
	another.edf.deadline_us = 124000;
	another.edf.session = renewed.session;
	check(lifecycle.begin(20000, 1, c, inventory, binding, renewed) == ManualBankFault::None
	      && lifecycle.accept(24000, renewed, inventory, binding, another) == ManualBankFault::None, "explicit new session prepared");
	check(lifecycle.confirmHandled(24000, renewed, inventory, binding, old_ticket, true, handled_ack) == ManualBankFault::HandlingRejected,
	      "old session ticket cannot confirm identical PWM in a new session");

}

static void edfTests()
{
	const auto c = fixture();
	AccelerationControllerPort development_port;
	AccelerationControlOutput unchanged{};
	unchanged.edf_thrust_n = 7.F;
	check(!development_port.available(), "future acceleration port cannot advertise an implemented controller");
	check(development_port.configure(c) == AccelerationPortStatus::Unavailable, "configuration cannot enable placeholder controller");
	check(development_port.cycle({}, unchanged) == AccelerationPortStatus::Unavailable && near(unchanged.edf_thrust_n, 7.F),
	      "unimplemented acceleration port produces no replacement output");
	EdfOwner owner;
	auto p = permission(1000);
	check(owner.begin(1000, c, p) == EdfFault::None, "explicit admitted EDF grant");
	check(owner.sample(1000, p).disposition == EdfDisposition::RecoveryRequired, "grant alone cannot supply propulsion");
	check(owner.accept(1000, p, request(p, 1000, 1, 0.F)) == EdfFault::None, "low throttle first request");
	check(near(owner.sample(1000, p).normalized, 0.F), "first EDF command starts at zero");
	check(owner.accept(11000, p, request(p, 11000, 2, 1.F)) == EdfFault::None, "full manual demand accepted");
	check(near(owner.sample(11000, p).normalized, 0.F), "new demand cannot use old elapsed slew budget");
	auto output = owner.sample(21000, p);
	check(output.disposition == EdfDisposition::Command && near(output.normalized, .01F), "measured elapsed rise limit");
	check(output.deadline_us == 101000 && output.sequence == 2 && output.request_timestamp_us == 11000,
	      "held samples retain original request identity and deadline");
	check(owner.accept(31000, p, request(p, 31000, 3, 0.F)) == EdfFault::None, "fall request accepted");
	check(near(owner.sample(36000, p).normalized, .01F), "old target advances then asymmetric fall applies");
	check(owner.sample(101000, p).disposition == EdfDisposition::RecoveryRequired, "permission expiry never silently holds thrust");
	p = permission(102000);
	check(owner.accept(102000, p, request(p, 102000, 4, 0.F)) == EdfFault::PermissionLost,
	      "permission expiry latches across fresh permission");
	check(owner.begin(102000, c, p) == EdfFault::InvalidGrant, "old session cannot reacquire EDF");
	p.session = 102000;
	check(owner.begin(102000, c, p) == EdfFault::None, "new session permits deliberate reacquisition");
	check(owner.accept(102000, p, request(p, 102000, 1, .5F)) == EdfFault::NotLowThrottle,
	      "high throttle cannot enter after handoff");

	for (unsigned fault = 0; fault < 15; ++fault) {
		EdfOwner subject;
		auto admission = permission(1000);
		check(subject.begin(1000, c, admission) == EdfFault::None, "fault test grant");
		check(subject.accept(1000, admission, request(admission, 1000, 1, 0.F)) == EdfFault::None, "fault test entry");
		auto bad = request(admission, 2000, 2, .5F);

		switch (fault) {
		case 0: ++bad.session; break;

		case 1: ++bad.source; break;

		case 2: ++bad.configuration; break;

		case 3: bad.mode = ControlMode::ServoBench; break;

		case 4: bad.units = EdfUnits::Newtons; break;

		case 5: bad.sequence = 1; break;

		case 6: bad.timestamp_us = 2001; break;

		case 7: bad.timestamp_us = 1000; break;

		case 8: bad.deadline_us = 2000; break;

		case 9: bad.deadline_us = 102001; break;

		case 10: bad.value = std::numeric_limits<float>::quiet_NaN(); break;

		case 11: bad.value = std::numeric_limits<float>::infinity(); break;

		case 12: bad.value = -.01F; break;

		case 13: bad.value = 1.01F; break;

		case 14: bad.sequence = 0; break;
		}

		check(subject.accept(2000, admission, bad) != EdfFault::None, "malformed or foreign EDF request rejected");
		check(subject.sample(2000, admission).disposition == EdfDisposition::RecoveryRequired, "request fault revokes command");
	}

	for (unsigned fault = 0; fault < 12; ++fault) {
		EdfOwner subject;
		auto admission = permission(1000);
		check(subject.begin(1000, c, admission) == EdfFault::None, "permission test grant");
		check(subject.accept(1000, admission, request(admission, 1000, 1, 0.F)) == EdfFault::None, "permission test entry");

		switch (fault) {
		case 0: admission.armed = false; break;

		case 1: admission.mode = ControlMode::ServoBench; break;

		case 2: admission.kill = true; break;

		case 3: admission.lockdown = true; break;

		case 4: admission.termination = true; break;

		case 5: admission.flight_admitted = false; break;

		case 6: admission.healthy = false; break;

		case 7: admission.measured_condition_valid = false; break;

		case 8: ++admission.configuration; break;

		case 9: ++admission.source; break;

		case 10: ++admission.session; break;

		case 11: admission.timestamp_us = 3000; break;
		}

		const auto sampled = subject.sample(2000, admission);
		check(sampled.disposition == (fault < 5 ? EdfDisposition::Inhibited : EdfDisposition::RecoveryRequired),
		      "independent permission sampling handles immediate inhibition and recovery separately");
	}

	EdfOwner acceleration;
	p = permission(1000, ControlMode::Acceleration);
	check(acceleration.begin(1000, c, p) == EdfFault::None, "acceleration has common EDF owner");
	check(acceleration.accept(1000, p, request(p, 1000, 1, 0.F)) == EdfFault::None, "acceleration low-thrust entry");
	check(acceleration.accept(2000, p, request(p, 2000, 2, 10.F)) == EdfFault::None, "measured newton demand conversion");
	check(near(acceleration.sample(52000, p).normalized, .05F), "acceleration uses same physical slew limit");
	check(acceleration.sample(92000, p).disposition == EdfDisposition::RecoveryRequired, "request expires before permission");
	EdfOwner bench;
	p = permission(1000, ControlMode::ServoBench);
	check(bench.begin(1000, c, p) == EdfFault::InvalidGrant, "bench never acquires EDF");
	check(bench.sample(1000, p).disposition == EdfDisposition::Inhibited, "bench unconditionally inhibits EDF");
	EdfOwner empty;
	p = permission(1000);
	check(empty.begin(1000, {}, p) == EdfFault::InvalidConfiguration, "unmeasured defaults cannot acquire EDF");
	EdfOwner revoked;
	check(revoked.begin(1000, c, p) == EdfFault::None, "revoke test grant");
	revoked.revoke();
	check(revoked.sample(1000, p).disposition == EdfDisposition::RecoveryRequired, "explicit revoke persists");
}

static void reportedRoutingTests()
{
	slaaps_output_bank_s banks[2] {};

	for (unsigned bank = 0; bank < 2; ++bank) {
		auto &b = banks[bank];
		b.timestamp = 1000;
		b.driver_instance = bank + 1;
		b.bank = bank;
		b.channel_count = 8;
		b.hardware_mask = 255;
		b.ready = true;

		for (unsigned channel = 0; channel < 8; ++channel) {
			b.rate_hz[channel] = 50;
			b.minimum_us[channel] = 1100;
			b.center_us[channel] = 1500;
			b.maximum_us[channel] = 1900;
		}
	}

	// Intentionally interleave and permute physical channels across banks.
	banks[0].function[7] = kCanardFunctionFirst;
	banks[1].function[2] = kCanardFunctionFirst + 1;
	banks[0].function[4] = kCanardFunctionFirst + 2;
	banks[1].function[0] = kCanardFunctionFirst + 3;
	banks[1].function[6] = kEdfFunction;
	OutputInventory inventory{};
	RoutingPlan plan{};
	check(assembleOutputInventory(2000, banks, 2, 3, inventory) == InventoryFault::None && inventory.count == 16,
	      "all reported PWM channels collected including unused pins");
	check(makeRoutingPlan(fixture(), inventory, true, plan).valid(), "interleaved actual bank assignments accepted");
	check(plan.canard[0].channel == 7 && plan.canard[1].bank == OutputBank::Aux && plan.edf.channel == 6,
	      "logical identities preserve QGC physical mapping");
	const auto original = inventory;
	check(assembleOutputInventory(2000, banks, 1, 3, inventory) == InventoryFault::MissingBank,
	      "missing physical bank cannot be inferred from parameters");
	check(inventory.count == original.count && inventory.channels[15].channel == original.channels[15].channel,
	      "failed inventory preserves caller snapshot");
	check(assembleOutputInventory(1001000, banks, 2, 3, inventory) == InventoryFault::Stale, "driver heartbeat expires at boundary");
	check(assembleOutputInventory(999, banks, 2, 3, inventory) == InventoryFault::Stale, "future driver report rejected");
	check(assembleOutputInventory(2000, nullptr, 2, 3, inventory) == InventoryFault::InvalidReport, "null report array rejected");
	check(assembleOutputInventory(2000, banks, 3, 3, inventory) == InventoryFault::InvalidReport, "excess bank count bounded before access");
	check(assembleOutputInventory(2000, banks, 2, 4, inventory) == InventoryFault::InvalidReport, "unsupported required bank rejected");

	for (unsigned fault = 0; fault < 8; ++fault) {
		auto bad = banks[1];

		switch (fault) {
		case 0: bad.bank = 0; break;

		case 1: bad.bank = 2; break;

		case 2: bad.ready = false; break;

		case 3: bad.driver_instance = 0; break;

		case 4: bad.channel_count = 17; break;

		case 5: bad.channel_count = 0; break;

		case 6: bad.hardware_mask |= 1U << 8; break;

		case 7: bad.reversed_mask |= 1U << 8; break;
		}

		slaaps_output_bank_s pair[2] {banks[0], bad};
		check(assembleOutputInventory(2000, pair, 2, 3, inventory) != InventoryFault::None,
		      "invalid or duplicate physical bank cannot produce routing snapshot");
	}

	for (unsigned fault = 0; fault < 6; ++fault) {
		slaaps_output_bank_s pair[2] {banks[0], banks[1]};

		switch (fault) {
		case 0: pair[1].hardware_mask &= ~(1U << 2); break;

		case 1: pair[1].rate_hz[2] = 0; break;

		case 2: pair[1].rate_hz[2] = 401; break;

		case 3: pair[1].failsafe_us[2] = UINT16_MAX; break;

		case 4: pair[1].center_us[2] = 1501; break;

		case 5: pair[1].function[7] = kCanardFunctionFirst; break;
		}

		check(assembleOutputInventory(2000, pair, 2, 3, inventory) == InventoryFault::None,
		      "raw driver state retained for route diagnosis");
		check(!makeRoutingPlan(fixture(), inventory, true, plan).valid(),
		      "hardware availability rate calibration and duplication enforced end to end");
	}
}

static void bankPreparationTests()
{
	const auto c = fixture();
	const auto inventory = routingFixture();
	const ManualOutputBinding binding{42, {51, 52}, 3};
	auto p = permission(1000);
	p.configuration = fingerprint(c);
	ManualPreparationRequest request{1000, 20000, 1000, p.source, p.configuration, 77, {88, 99}, binding, 66};
	ManualBankPreparation preparation;
	check(preparation.prepare(1000, 0, c, inventory, request), "prepare valid routing without actuator permission");
	check(preparation.prepared(19999) && !preparation.prepared(20000), "preparation expires at original deadline");
	check(preparation.consume(2000, c, inventory, binding, p, 77, 88), "matching activation consumes proposal");
	check(!preparation.consume(2000, c, inventory, binding, p, 77, 88), "preparation cannot be consumed twice");
	check(!preparation.prepare(2000, 0, c, inventory, request), "preparation token cannot replay");
	request.session = request.timestamp_us = 2001;
	check(preparation.prepare(2001, 1, c, inventory, request), "new token prepares AUX independently");
	preparation.cancel();
	p.session = request.session;
	check(!preparation.consume(2001, c, inventory, binding, p, 77, 99), "cancel prevents activation");

	ManualBankPreparation aux;
	check(aux.prepare(2001, 1, c, inventory, request) && aux.consume(3000, c, inventory, binding, p, 77, 99),
	      "AUX activation binds its own acknowledgment lifetime");

	for (unsigned scenario = 0; scenario < 11; ++scenario) {
		ManualBankPreparation bank;
		auto candidate = c;
		auto live = inventory;
		auto route = binding;
		auto grant = p;
		auto proposal = request;
		unsigned revision = 77;
		unsigned acknowledgment_revision = 88;
		uint64_t now = 3000;
		check(bank.prepare(2001, 0, c, inventory, proposal), "mutation fixture prepares");

		switch (scenario) {
		case 0: ++grant.session; break;

		case 1: ++grant.source; break;

		case 2: ++grant.configuration; break;

		case 3: candidate.mass += .1F; break;

		case 4: ++route.routing; break;

		case 5: ++route.driver[0]; break;

		case 6: ++live.channels[0].rate_hz; break;

		case 7: ++revision; break;

		case 8: now = proposal.deadline_us; break;

		case 9: now = 2000; break;

		case 10: ++acknowledgment_revision; break;
		}

		check(!bank.consume(now, candidate, live, route, grant, revision, acknowledgment_revision),
		      "changed or expired preparation cannot activate");
		check(!bank.consume(3000, c, inventory, binding, p, 77, 88), "repair cannot recover consumed preparation");
	}

	for (unsigned scenario = 0; scenario < 7; ++scenario) {
		ManualBankPreparation bank;
		auto proposal = request;
		unsigned bank_id = 0;

		switch (scenario) {
		case 0: proposal.deadline_us = proposal.timestamp_us + ManualBankPreparation::kMaximumPreparationUs + 1; break;

		case 1: proposal.timestamp_us = 3000; break;

		case 2: proposal.command_revision = 0; break;

		case 3: proposal.source = 0; break;

		case 4: proposal.binding.banks = 1; proposal.binding.driver[1] = 0; break;

		case 5: bank_id = 2; break;

		case 6: proposal.acknowledgment_revisions[1] = 0; break;
		}

		check(!bank.prepare(2001, bank_id, c, inventory, proposal), "invalid preparation metadata and incomplete bank set rejected");
	}

	ManualBankPreparation failed;
	auto invalid = c;
	invalid.mass = 0.F;
	check(!failed.prepare(2001, 0, invalid, inventory, request), "invalid configuration cannot prepare");
	check(!failed.prepare(2001, 0, c, inventory, request), "failed valid-time token cannot replay after configuration repair");
}

static void preparationWireTests()
{
	const ManualPreparationRequest request{1000, 20000, 1000, 11, 12, 77, {88, 99}, {42, {51, 52}, 3}, 66};
	const unsigned request_bank = slaaps_manual_preparation_s::REQUEST_BANK;
	slaaps_manual_preparation_s wire{};
	ManualPreparationRequest decoded{};
	uint64_t prepared = 123;
	check(encodeManualPreparation(1100, request, request_bank, 0, wire)
	      && decodeManualPreparation(1200, request_bank, wire, decoded, prepared)
	      && sameManualPreparation(request, decoded) && prepared == 0, "proposal roundtrip preserves original identity and expiry");

	for (unsigned bank = 0; bank < 2; ++bank) {
		check(encodeManualPreparation(1300, request, bank, 1200, wire)
		      && decodeManualPreparation(1400, bank, wire, decoded, prepared)
		      && sameManualPreparation(request, decoded) && prepared == 1200, "readiness roundtrip echoes entire proposal");
		check(!decodeManualPreparation(1400, 1 - bank, wire, decoded, prepared), "readiness cannot cross bank topics");
		check(!decodeManualPreparation(1400, request_bank, wire, decoded, prepared), "readiness cannot become a preparation request");
	}

	for (unsigned scenario = 0; scenario < 16; ++scenario) {
		encodeManualPreparation(1300, request, 0, 1200, wire);

		switch (scenario) {
		case 0: wire.timestamp = 1401; break;

		case 1: wire.timestamp = 999; break;

		case 2: wire.timestamp_sample = 0; break;

		case 3: wire.deadline = 1400; break;

		case 4: wire.deadline = 1001001; break;

		case 5: wire.session = 0; break;

		case 6: wire.session = 1001; break;

		case 7: wire.source = 0; break;

		case 8: wire.configuration = 0; break;

		case 9: wire.command_revision = 0; break;

		case 10: wire.acknowledgment_revisions[1] = 0; break;

		case 11: wire.driver[0] = 0; break;

		case 12: wire.bank_mask = 4; break;

		case 13: wire.prepared = 999; break;

		case 14: wire.prepared = 1301; break;

		case 15: wire.bank = 2; break;
		}

		decoded = request;
		prepared = 456;
		check(!decodeManualPreparation(1400, 0, wire, decoded, prepared)
		      && sameManualPreparation(decoded, request) && prepared == 456, "malformed readiness rejected without changing outputs");
	}

	check(!encodeManualPreparation(1200, request, request_bank, 1100, wire), "proposal cannot claim bank preparation");
	check(!encodeManualPreparation(20000, request, 0, 1200, wire), "republication cannot renew preparation expiry");
	auto changed = request;
	changed.acknowledgment_revisions[1]++;
	check(!sameManualPreparation(changed, request), "readiness identity includes peer reply lifetime");
	changed = request;
	changed.deadline_us++;
	check(!sameManualPreparation(changed, request), "readiness identity includes original deadline");
}

static ReportedRoutingSnapshot routingSnapshotFixture()
{
	ReportedRoutingSnapshot snapshot{};
	snapshot.captured_us = 1000;
	snapshot.inventory = routingFixture();

	for (unsigned bank = 0; bank < 2; ++bank) {
		snapshot.driver[bank] = 51 + bank;
		snapshot.reported_us[bank] = 1000;
		snapshot.publisher_revision[bank] = 11 + bank;
		snapshot.topic_instance[bank] = bank;
	}

	return snapshot;
}

static void observedRoutingBindingTests()
{
	const auto c = fixture();
	const auto original = routingSnapshotFixture();
	const ManualOutputBinding binding{42, {51, 52}, 3};
	const ReportedRoutingPolicy policy{1000, 2000};
	ObservedRoutingBinding positive;
	auto fresh = original;
	fresh.captured_us = 1500;
	fresh.reported_us[0] = fresh.reported_us[1] = 1400;
	check(positive.begin(1000, c, original, InventoryFault::None, binding, policy)
	      && positive.check(1500, c, fresh, InventoryFault::None), "routing binding accepts fresh heartbeat with stable identities");
	positive.cancel();
	check(!positive.check(1500, c, fresh, InventoryFault::None), "cancelled routing binding cannot recover through a fresh observation");

	for (unsigned scenario = 0; scenario < 16; ++scenario) {
		ObservedRoutingBinding guard;
		check(guard.begin(1000, c, original, InventoryFault::None, binding, policy), "bind trusted report identities and routing");
		auto candidate = fresh;
		auto configuration = c;
		auto status = InventoryFault::None;
		uint64_t now = 1600;

		switch (scenario) {
		case 0: configuration.mass += .1F; break;

		case 1: ++candidate.inventory.channels[0].rate_hz; break;

		case 2: status = InventoryFault::UnstablePublisher; break;

		case 3: ++candidate.driver[0]; break;

		case 4: ++candidate.publisher_revision[1]; break;

		case 5: candidate.topic_instance[0] = 1; candidate.topic_instance[1] = 0; break;

		case 6: candidate.captured_us = 1601; break;

		case 7: candidate.captured_us = 0; break;

		case 8: candidate.reported_us[0] = 0; break;

		case 9: candidate.reported_us[0] = 1501; break;

		case 10: candidate.reported_us[0] = 999; break;

		case 11: candidate.captured_us = 999; candidate.reported_us[0] = candidate.reported_us[1] = 900; break;

		case 12: now = 3000; break;

		case 13: now = 999; break;

		case 14: candidate.publisher_revision[0] = 0; break;

		case 15: now = 3000; candidate.captured_us = 3000; candidate.reported_us[0] = 1000; break;
		}

		check(!guard.check(now, configuration, candidate, status) && !guard.check(1600, c, fresh, InventoryFault::None),
		      "routing identity time configuration or capture fault latches through restored observations");
	}

	for (unsigned scenario = 0; scenario < 3; ++scenario) {
		auto bad_policy = policy;

		if (scenario == 0) { bad_policy.maximum_capture_age_us = 0; }

		if (scenario == 1) { bad_policy.maximum_report_age_us = 0; }

		if (scenario == 2) { bad_policy.maximum_report_age_us = kMaximumOutputReportAgeUs + 1; }

		ObservedRoutingBinding guard;
		check(!guard.begin(1000, c, original, InventoryFault::None, binding, bad_policy), "routing timing policy must be explicit and bounded");
	}

	auto wrong = binding;
	++wrong.driver[0];
	ObservedRoutingBinding guard;
	check(!guard.begin(1000, c, original, InventoryFault::None, wrong, policy), "proposed device identity must match actual reported device");
}

static void activationBarrierTests()
{
	const auto c = fixture();
	const ManualOutputBinding binding{42, {51, 52}, 3};
	const ManualPreparationRequest request{1000, 20000, 1000, 17, fingerprint(c), 77, {88, 99}, binding, 66};
	const ManualActivationAck main{1000, 17, request.configuration, 42, 51, 2100, 14100, 77, 88, 0};
	const ManualActivationAck aux{1000, 17, request.configuration, 42, 52, 2200, 14200, 77, 99, 1};
	ManualActivationBarrier barrier;
	check(barrier.begin(2000, c, request, 6000, 5000), "activation barrier requires explicit bounded first-command allowance");
	check(barrier.accept(3000, 0, main) == ActivationStatus::Waiting
	      && barrier.accept(3500, 0, main) == ActivationStatus::Waiting, "duplicate activation reply cannot stand in for missing peer");
	check(barrier.accept(4000, 1, aux) == ActivationStatus::Ready, "all banks with exact activation identities complete barrier");
	uint64_t deadline = 123;
	check(barrier.consume(4500, deadline) && deadline == 14100 && !barrier.consume(4500, deadline),
	      "activation completion is one-use and preserves earliest original bank deadline");
	check(barrier.requiredBanks() == 3, "all participating banks remain known for later entry failure cancellation");

	for (unsigned scenario = 0; scenario < 14; ++scenario) {
		ManualActivationBarrier test;
		check(test.begin(2000, c, request, 6000, 5000) && test.accept(3000, 0, main) == ActivationStatus::Waiting,
		      "partial bank activation remains pending");
		auto bad = aux;
		unsigned bank = 1;

		switch (scenario) {
		case 0: bad.bank = 0; break;

		case 1: ++bad.session; break;

		case 2: ++bad.source; break;

		case 3: ++bad.configuration; break;

		case 4: ++bad.routing; break;

		case 5: ++bad.driver; break;

		case 6: ++bad.command_revision; break;

		case 7: ++bad.acknowledgment_revision; break;

		case 8: bad.activated_us = 1999; break;

		case 9: bad.activated_us = 4001; break;

		case 10: bad.first_command_deadline_us = 4000; break;

		case 11: ++bad.first_command_deadline_us; break;

		case 12: bad.first_command_deadline_us = 9000; break;

		case 13: bad = main; bank = 0; --bad.first_command_deadline_us; break;
		}

		check(test.accept(4000, bank, bad) == ActivationStatus::Rejected && test.requiredBanks() == 3
		      && test.accept(4000, 1, aux) == ActivationStatus::Rejected,
		      "invalid or mutated activation reply latches rejection and retains cancellation set");
	}

	for (unsigned scenario = 0; scenario < 3; ++scenario) {
		ManualActivationBarrier test;
		auto first = main;

		if (scenario == 2) { first.first_command_deadline_us = 8501; }

		check(test.begin(2000, c, request, 6000, 5000) && test.accept(3000, 0, first) == ActivationStatus::Waiting,
		      "initial activation reply has remaining first-command allowance");
		const uint64_t now = scenario == 0 ? 6000 : (scenario == 1 ? 2999 : 3501);
		deadline = 123;
		check(test.status(now) == ActivationStatus::Rejected && !test.consume(now, deadline) && deadline == 123
		      && test.requiredBanks() == 3, "timeout backward time or exhausted delivery allowance cancels all required banks");
	}

	for (unsigned scenario = 0; scenario < 6; ++scenario) {
		ManualActivationBarrier test;
		uint64_t budget = 5000, end = 6000;

		switch (scenario) {
		case 0: budget = 0; break;

		case 1: budget = c.cycle_min_us; break;

		case 2: budget = c.cycle_max_us; break;

		case 3: end = 2000; break;

		case 4: end = request.deadline_us + 1; break;

		case 5: end = 9000; break;
		}

		check(!test.begin(2000, c, request, end, budget) && !test.begin(2000, c, request, 6000, 5000),
		      "invalid activation timing consumes token and cannot be repaired in place");
	}

	ManualOutputBank bank;
	auto permission_now = permission(1000);
	permission_now.configuration = request.configuration;
	ManualActivationAck observed{};
	check(!bank.activationAck(2000, permission_now, routingFixture(), binding, observed), "inactive bank cannot confirm activation");
	check(bank.begin(2100, 0, c, routingFixture(), binding, permission_now) == ManualBankFault::None
	      && bank.activationAck(3000, permission_now, routingFixture(), binding, observed)
	      && observed.activated_us == 2100 && observed.first_command_deadline_us == 14100,
	      "actual bank reports its original initial-command deadline");
	check(bank.activationAck(4000, permission_now, routingFixture(), binding, observed)
	      && observed.activated_us == 2100 && observed.first_command_deadline_us == 14100,
	      "polling activation confirmation never renews bank expiry");
	permission_now.flight_admitted = false;
	check(!bank.activationAck(4000, permission_now, routingFixture(), binding, observed),
	      "activation confirmation requires current independent permission");
}

static void activationWireTests()
{
	const ManualActivationAck original{1000, 17, 12, 42, 51, 2100, 14100, 77, 88, 0};
	slaaps_manual_activation_s wire{};
	ManualActivationAck decoded{};
	check(encodeManualActivation(3000, 0, original, wire)
	      && decodeManualActivation(4000, 0, wire, decoded) && sameManualActivation(original, decoded),
	      "activation wire preserves every transaction identity and original timing");
	check(encodeManualActivation(5000, 0, original, wire) && wire.timestamp == 5000
	      && wire.timestamp_sample == 2100 && wire.first_command_deadline == 14100,
	      "republished activation confirmation preserves original expiry");

	for (unsigned scenario = 0; scenario < 15; ++scenario) {
		auto bad = wire;
		unsigned bank = 0;

		switch (scenario) {
		case 0: bad.session = 0; break;

		case 1: bad.source = 0; break;

		case 2: bad.configuration = 0; break;

		case 3: bad.routing = 0; break;

		case 4: bad.driver = 0; break;

		case 5: bad.command_revision = 0; break;

		case 6: bad.acknowledgment_revision = 0; break;

		case 7: bad.timestamp_sample = 0; break;

		case 8: bad.timestamp_sample = bad.session - 1; break;

		case 9: bad.first_command_deadline = 6000; break;

		case 10: bad.timestamp = 6001; break;

		case 11: bad.timestamp = bad.timestamp_sample - 1; break;

		case 12: bad.bank = 1; break;

		case 13: bank = 2; break;

		case 14: bad.timestamp_sample = UINT64_MAX; break;
		}

		decoded = original;
		check(!decodeManualActivation(6000, bank, bad, decoded) && sameManualActivation(decoded, original),
		      "invalid activation payload or wrong topic role preserves caller output");
	}

	for (unsigned bank : {1U, 2U}) {
		const auto before = wire;
		check(!encodeManualActivation(6000, bank, original, wire)
		      && std::memcmp(&wire, &before, sizeof(wire)) == 0, "wrong-role activation encode preserves output");
	}
	check(!encodeManualActivation(14100, 0, original, wire)
	      && !decodeManualActivation(14100, 0, wire, decoded), "activation wire expires at original first-command deadline");
}

static int runConfigurationTests()
{
	checks = 0;
	manualTimingConfigurationTests();
	powerConfigurationTests();
	profileTests();
	benchTravelBoundsTests();
	companionConfigurationTests();
	scalarTests();
	storeTests();
	thrustTests();
	actualControllers();
	inputMappingTests();
	manualReadinessInputTests();
	routingTests();
	manualEdfTests();
	permissionWireTests();
	manualWorkerTests();
	benchWorkerExpiryTests();
	edfCommissioningTests();
	scheduledControlTests();
	asynchronousManualEntryTests();
	manualBankTests();
	bankPreparationTests();
	preparationWireTests();
	observedRoutingBindingTests();
	activationBarrierTests();
	activationWireTests();
	transportTests();
	edfTests();
	reportedRoutingTests();
	std::printf("PASS %u checks; synthetic configuration only, no output publication\n", checks);
	return 0;
}

#ifdef SLAAPS_NATIVE_CONTROL_TEST
#include <px4_platform_common/px4_config.h>
#include <px4_platform_common/log.h>
#include <uORB/Publication.hpp>
#include <lib/slaaps/BoundSubscription.hpp>
#include <uORB/topics/slaaps_status.h>
#include <uORB/topics/slaaps_manual_permission.h>
#include <lib/slaaps/ManualBankTransport.hpp>
#include <lib/slaaps/ManualPreparationEndpoint.hpp>
#include <lib/slaaps/ManualPreparationCoordinator.hpp>
#include <lib/slaaps/OutputRoutingObservation.hpp>
#include <lib/slaaps/OperatingConditionObservation.hpp>
#include <lib/slaaps/PowerObservation.hpp>
#include <uORB/PublicationMulti.hpp>
#include <lib/slaaps/ManualExecution.hpp>
#include <uORB/Subscription.hpp>
#include <memory>
#include <new>
#if !defined(CONFIG_SLAAPS_ROUTING_TEST) || !defined(__PX4_POSIX) || defined(ENABLE_LOCKSTEP_SCHEDULER)
#error SLAAPS control fixture requires the explicit native wall-clock test target
#endif
#include <modules/research_stabilized_manual/SelectedStateInput.hpp>

// Advertisement alone supplies identity for preparation, never permission.
struct NativePermissionPublisher {
	uORB::Publication<slaaps_manual_permission_s> publication{ORB_ID(slaaps_manual_permission)};
	uORB::ResearchSubscription lifetime{ORB_ID(slaaps_manual_permission)};
	NativePermissionPublisher() { check(publication.advertise(), "advertise independent synthetic permission endpoint"); }
	unsigned revision() { return lifetime.revision(); }
};

static void nativePermissionTests()
{
	for (unsigned scenario = 0; scenario < 6; ++scenario) {
		NativePermissionPublisher publisher;
		uORB::Publication<slaaps_manual_permission_s> replacement{ORB_ID(slaaps_manual_permission)};
		auto p = permission(1000);
		p.configuration = 12;
		const ManualPreparationRequest request{1000, 20000, 1000, p.source, p.configuration, 77, {88, 99},
			{42, {51, 52}, 3}, publisher.revision()};
		ManualPermissionSubscription subscriber;
		EdfPermission received{};
		check(subscriber.begin(1000, request, 100000) && subscriber.read(1000, received) == BoundRead::Waiting,
		      "bound permission subscription waits without inventing authority");
		slaaps_manual_permission_s wire{};
		check(encodeManualPermission(1000, {p, 1}, 100000, wire) && publisher.publication.publish(wire)
		      && subscriber.read(1100, received) == BoundRead::Data && received.deadline_us == 101000,
		      "exact admitted publisher supplies original permission deadline");
		check(subscriber.read(1200, received) == BoundRead::Data && received.deadline_us == 101000,
		      "held permission cannot renew decision expiry");
		uint64_t now = 2000;

		switch (scenario) {
		case 0: ++wire.timestamp; break;

		case 1: ++wire.sequence; break;

		case 2: ++wire.source; break;

		case 3: now = 101000; break;

		case 4: check(replacement.advertise(), "replace permission publisher with identical payload"); break;

		case 5:
			p.timestamp_us = 2000;
			p.deadline_us = 102000;
			check(encodeManualPermission(2000, {p, 2}, 100000, wire), "encode fresh sequenced permission");
			break;
		}

		check(publisher.publication.publish(wire), "publish permission lifecycle scenario");

		if (scenario == 5) {
			check(subscriber.read(now, received) == BoundRead::Data && received.deadline_us == 102000,
			      "new decision timestamp and sequence advance permission");

		} else {
			check(subscriber.read(now, received) == BoundRead::Rejected
			      && subscriber.read(now, received) == BoundRead::Rejected, "permission lifecycle failure latches rejection");
		}
	}

	for (unsigned scenario = 0; scenario < 4; ++scenario) {
		NativePermissionPublisher publisher;
		auto previous = permission(1000);
		previous.configuration = 12;
		slaaps_manual_permission_s retained{};

		if (scenario != 1) {
			if (scenario == 3) { previous = permission(200000); previous.session = 200000; previous.configuration = 12; }

			check(encodeManualPermission(previous.timestamp_us, {previous, 1}, 100000, retained), "encode retained permission fixture");
		}

		check(publisher.publication.publish(retained), "retain prior permission or exact zero tombstone");
		const ManualPreparationRequest request{200000, 220000, 200000, previous.source, 12, 77, {88, 99},
			{42, {51, 52}, 3}, publisher.revision()};
		ManualPermissionSubscription subscriber;
		EdfPermission received{};
		check(subscriber.begin(200000, request, 100000), "new permission session binds retained publisher without reusing old authority");

		if (scenario == 3) {
			check(subscriber.read(200001, received) == BoundRead::Data && received.session == request.session,
			      "already published current-session permission is not discarded as baseline");
			continue;
		}

		check(subscriber.read(200001, received) == BoundRead::Waiting,
		      "expired older permission and zero tombstone remain nonauthoritative baseline");

		if (scenario == 2) {
			++retained.timestamp;
			check(publisher.publication.publish(retained) && subscriber.read(200002, received) == BoundRead::Rejected,
			      "mutating retained old permission cannot bypass current-session checks");
			continue;
		}

		auto current = permission(200002);
		current.session = request.session;
		current.configuration = request.configuration;
		slaaps_manual_permission_s fresh{};
		check(encodeManualPermission(200002, {current, 1}, 100000, fresh) && publisher.publication.publish(fresh)
		      && subscriber.read(200002, received) == BoundRead::Data && received.session == request.session,
		      "fresh new-session permission replaces retained baseline exactly once");
	}

}

static void nativeAwaitGrantTests()
{
	for (unsigned scenario = 0; scenario < 17; ++scenario) {
		NativePermissionPublisher publisher;
		uORB::Publication<slaaps_manual_permission_s> replacement{ORB_ID(slaaps_manual_permission)};
		slaaps_manual_permission_s baseline{};
		baseline.timestamp = 1000;
		check(publisher.publication.publish(baseline), "retain nonauthoritative first-grant baseline");
		const ManualPreparationRequest request{1000, 201000, 1000, 17, 12, 77, {88, 99},
			{42, {51, 52}, 3}, publisher.revision()};
		ManualPermissionSubscription subscriber;
		EdfPermission received{};
		received.session = 999;
		check(subscriber.begin(1000, request, 100000) && subscriber.awaitGrant(1001, received) == BoundRead::Waiting
		      && received.session == 999, "first-grant waiting preserves caller output without conferring authority");
		auto denied = permission(1002);
		denied.configuration = request.configuration;
		denied.armed = denied.flight_admitted = denied.healthy = denied.measured_condition_valid = false;
		slaaps_manual_permission_s wire{};
		check(encodeManualPermission(1002, {denied, 1}, 100000, wire) && publisher.publication.publish(wire)
		      && subscriber.awaitGrant(1003, received) == BoundRead::Waiting && received.session == 999,
		      "explicit disarmed all-denied permission waits only before the first usable grant");

		if (scenario == 0) {
			auto granted = permission(2000);
			granted.configuration = request.configuration;
			check(encodeManualPermission(2000, {granted, 2}, 100000, wire) && publisher.publication.publish(wire)
			      && subscriber.awaitGrant(2000, received, false) == BoundRead::Waiting && received.session == 999,
			      "fresh positive permission cannot be accepted before independent local arming agrees");
			check(subscriber.awaitGrant(2001, received, false) == BoundRead::Waiting
			      && subscriber.awaitGrant(2002, received) == BoundRead::Data && received.armed && received.flight_admitted,
			      "fresh usable grant completes waiting without changing its permission meaning");
			denied.timestamp_us = 2003;
			denied.deadline_us = 102003;
			check(encodeManualPermission(2003, {denied, 3}, 100000, wire) && publisher.publication.publish(wire)
			      && subscriber.awaitGrant(2003, received) == BoundRead::Rejected,
			      "a granted subscription can never return to pending disarmed denial");
			continue;
		}

		if (scenario == 1) {
			check(subscriber.awaitGrant(150000, received) == BoundRead::Waiting && received.session == 999,
			      "expired retained prearm denial may wait without renewing or granting its original permission");
			check(subscriber.awaitGrant(request.deadline_us, received) == BoundRead::Rejected,
			      "retained prearm denial expires at the original preparation deadline itself");
			continue;
		}

		if (scenario == 2) {
			check(subscriber.read(1004, received) == BoundRead::Data && !received.armed && !received.flight_admitted,
			      "strict permission read continues to report a fresh denial as denied data");
			check(subscriber.read(denied.deadline_us, received) == BoundRead::Rejected,
			      "strict permission read never accepts the expired denial exception");
			continue;
		}

		uint64_t now = 1004;

		if ((scenario >= 3 && scenario <= 8) || scenario >= 13) {
			wire.sequence = 2;
			wire.timestamp = wire.timestamp_sample = now;
			wire.deadline = now + 100000;
		}

		switch (scenario) {
		case 3: wire.armed = true; break;

		case 4: wire.kill = true; break;

		case 5: wire.lockdown = true; break;

		case 6: wire.termination = true; break;

		case 7: wire.healthy = true; break;

		case 8:
			wire.armed = wire.flight_admitted = wire.healthy = wire.measured_condition_valid = true;
			now = 150000;
			break;

		case 9: check(replacement.advertise(), "replace bound publisher while waiting for first grant"); break;

		case 10: ++wire.timestamp; break;

		case 11: wire.sequence = 0; break;

		case 12: wire.timestamp = now + 1; break;

		case 13: ++wire.session; break;

		case 14: ++wire.source; break;

		case 15: ++wire.configuration; break;

		case 16: --wire.session; break;
		}

		check(publisher.publication.publish(wire) && subscriber.awaitGrant(now, received) == BoundRead::Rejected,
		      "armed denial inhibition stale grant identity mutation or publisher replacement rejects pending authority");
		check(subscriber.awaitGrant(now, received) == BoundRead::Rejected,
		      "first-grant rejection remains latched without fresh authority");
	}
}

// Run before commander/sensors start. All values and source identities below
// are synthetic; observing them grants neither flight nor output permission.
static void nativePowerObservationTests()
{
	PowerConfiguration policy{};
	PowerObservation observation;
	PowerSample sample{};
	sample.observed_us = 77;
	auto rejected = [&](PowerObservationResult expected, const char *message) {
		unsigned char original[sizeof(sample)];
		std::memcpy(original, &sample, sizeof(sample));
		check(observation.capture(policy, sample) == expected && std::memcmp(original, &sample, sizeof(sample)) == 0, message);
	};
	rejected(PowerObservationResult::InvalidConfiguration, "unconfigured power observation needs no publisher and preserves destination");
	policy = fixture().power;
	policy.battery_instance = 1;
	rejected(PowerObservationResult::Unavailable, "missing selected propulsion battery cannot fall back to another instance");
	using BatteryPublication = uORB::PublicationMulti<battery_status_s>;
	BatteryPublication distractor{ORB_ID(battery_status)}, selected{ORB_ID(battery_status)};
	check(distractor.advertise() && selected.advertise() && distractor.get_instance() == 0 && selected.get_instance() == 1,
	      "synthetic power test reserves a distractor and a different explicitly selected instance");
	rejected(PowerObservationResult::Unavailable, "battery advertisement alone supplies no power report");
	battery_status_s battery{};
	auto refresh = [&]() {
		battery = {};
		battery.timestamp = hrt_absolute_time();
		battery.connected = true;
		battery.id = 1;
		battery.source = battery_status_s::SOURCE_POWER_MODULE;
		battery.voltage_v = 24.F;
	};
	auto publish = [&]() { check(selected.publish(battery), "publish explicitly synthetic selected propulsion battery"); };
	refresh();
	auto other = battery;
	other.id = 9;
	other.voltage_v = 6.F;
	check(distractor.publish(other), "publish distinguishable unhealthy power distractor on instance zero");
	publish();
	check(observation.capture(policy, sample) == PowerObservationResult::Valid && near(sample.voltage_v, 24.F)
	      && sample.source.publisher_revision && sample.source.instance == 1 && sample.source.id == 1
	      && sample.source.source == battery_status_s::SOURCE_POWER_MODULE,
	      "power observation uses the configured battery and its actual publisher identity");
	check(sample.timestamp_us == battery.timestamp && sample.observed_us >= battery.timestamp
	      && sample.deadline_us == battery.timestamp + static_cast<uint64_t>(policy.maximum_age_us) + 1,
	      "power deadline derives from the original report using inclusive age and exclusive expiry");
	const auto retained = sample;
	check(observation.capture(policy, sample) == PowerObservationResult::Valid && sample.deadline_us == retained.deadline_us
	      && samePowerSource(sample.source, retained.source), "polling retained battery data cannot renew its expiry or identity");

	for (float voltage : {policy.voltage_min, policy.voltage_max}) {
		refresh();
		battery.voltage_v = voltage;
		publish();
		check(observation.capture(policy, sample) == PowerObservationResult::Valid, "power voltage band includes its explicit endpoints");
	}

	for (unsigned scenario = 0; scenario < 13; ++scenario) {
		policy.maximum_age_us = 100000;
		refresh();
		auto expected = PowerObservationResult::IdentityMismatch;

		switch (scenario) {
		case 0: battery.id = 2; break;

		case 1: battery.source = battery_status_s::SOURCE_EXTERNAL; break;

		case 2: battery.source = UINT8_MAX; break;

		case 3: battery.connected = false; expected = PowerObservationResult::Disconnected; break;

		case 4: battery.warning = battery_status_s::WARNING_LOW; expected = PowerObservationResult::Warning; break;

		case 5: battery.warning = UINT8_MAX; expected = PowerObservationResult::Warning; break;

		case 6: battery.faults = 1U << battery_status_s::FAULT_OVER_CURRENT; expected = PowerObservationResult::Fault; break;

		case 7: battery.is_powering_off = true; expected = PowerObservationResult::PoweringOff; break;

		case 8: battery.timestamp = 0; expected = PowerObservationResult::InvalidTimestamp; break;

		case 9: battery.timestamp = UINT64_MAX; expected = PowerObservationResult::InvalidTimestamp; break;

		case 10:
			check(battery.timestamp > 2, "native power clock supports a strictly expired original report");
			policy.maximum_age_us = 1;
			battery.timestamp -= 2;
			expected = PowerObservationResult::InvalidTimestamp;
			break;

		case 11: battery.voltage_v = policy.voltage_min - 1.F; expected = PowerObservationResult::VoltageOutOfRange; break;

		case 12: battery.voltage_v = policy.voltage_max + 1.F; expected = PowerObservationResult::VoltageOutOfRange; break;
		}

		publish();
		rejected(expected, "identity, connection, warning, fault, power-off, timestamp and voltage failures preserve caller sample");
	}

	for (float voltage : {0.F, -1.F, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()}) {
		refresh();
		battery.voltage_v = voltage;
		publish();
		rejected(PowerObservationResult::InvalidVoltage, "nonpositive or nonfinite battery voltage cannot authorize propulsion");
	}

	for (unsigned field = 0; field < 4; ++field) {
		auto changed = retained.source;

		switch (field) {
		case 0: ++changed.publisher_revision; break;

		case 1: ++changed.instance; break;

		case 2: ++changed.id; break;

		case 3: ++changed.source; break;
		}

		check(!samePowerSource(changed, retained.source), "every battery identity field is part of the session binding");
	}

	selected.~BatteryPublication();
	rejected(PowerObservationResult::Unavailable, "selected battery publisher loss cannot consume the remaining distractor");
	new (&selected) BatteryPublication{ORB_ID(battery_status)};
	check(selected.advertise() && selected.get_instance() == 1, "replacement battery explicitly reclaims the same selected instance");
	rejected(PowerObservationResult::Unavailable, "replacement advertisement cannot inherit retained old battery data");
	refresh();
	publish();
	check(observation.capture(policy, sample) == PowerObservationResult::Valid
	      && sample.source.publisher_revision != retained.source.publisher_revision && sample.source.instance == retained.source.instance
	      && sample.source.id == retained.source.id && sample.source.source == retained.source.source
	      && !samePowerSource(sample.source, retained.source),
	      "stable replacement can be observed but cannot reuse the old propulsion session identity");
}

static void nativeOperatingConditionObservationTests()
{
	constexpr int32_t maximum_age_us = 1000000;
	ControlScheduleConfiguration policy{};
	OperatingConditionObservation observation;
	OperatingConditionSample sample{};
	sample.observed_us = 77;
	auto rejected = [&](const char *message) {
		unsigned char original[sizeof(sample)];
		std::memcpy(original, &sample, sizeof(sample));
		check(!observation.capture(policy, sample) && std::memcmp(original, &sample, sizeof(sample)) == 0, message);
	};
	rejected("disabled condition observation needs no publishers and preserves its destination");
	policy.enabled = 1;
	policy.airspeed_instance = policy.air_data_instance = policy.battery_instance = 1;
	policy.battery_id = 2;
	policy.maximum_age_us = maximum_age_us;
	rejected("missing explicitly selected condition publishers cannot fall back to another source");
	using AirspeedPublication = uORB::PublicationMulti<airspeed_s>;
	AirspeedPublication airspeed_zero{ORB_ID(airspeed)}, airspeed_selected{ORB_ID(airspeed)};
	uORB::PublicationMulti<vehicle_air_data_s> air_data_zero{ORB_ID(vehicle_air_data)}, air_data_selected{ORB_ID(vehicle_air_data)};
	uORB::PublicationMulti<battery_status_s> battery_zero{ORB_ID(battery_status)}, battery_selected{ORB_ID(battery_status)};
	check(airspeed_zero.advertise() && airspeed_selected.advertise()
	      && air_data_zero.advertise() && air_data_selected.advertise()
	      && battery_zero.advertise() && battery_selected.advertise()
	      && airspeed_zero.get_instance() == 0 && airspeed_selected.get_instance() == 1
	      && air_data_zero.get_instance() == 0 && air_data_selected.get_instance() == 1
	      && battery_zero.get_instance() == 0 && battery_selected.get_instance() == 1,
	      "native condition fixture reserves distractor zero and explicitly selected nonzero instances");
	rejected("advertisement alone supplies no operating-condition measurements");
	airspeed_s airspeed{};
	vehicle_air_data_s air_data{};
	battery_status_s battery{};
	auto refresh = [&]() {
		const auto now = hrt_absolute_time();
		airspeed = {};
		air_data = {};
		battery = {};
		airspeed.timestamp = airspeed.timestamp_sample = now;
		airspeed.indicated_airspeed_m_s = 20.F;
		airspeed.confidence = 1.F;
		air_data.timestamp = air_data.timestamp_sample = now;
		air_data.rho = 1.2F;
		air_data.baro_device_id = 456;
		air_data.calibration_count = 2;
		air_data.temperature_source = 1;
		battery.timestamp = now;
		battery.connected = true;
		battery.id = 2;
		battery.source = battery_status_s::SOURCE_POWER_MODULE;
		battery.voltage_v = 22.F;
	};
	auto publish = [&]() {
		check(airspeed_selected.publish(airspeed) && air_data_selected.publish(air_data) && battery_selected.publish(battery),
		      "publish explicitly synthetic selected condition measurements");
	};
	refresh();
	const auto original_sample = airspeed.timestamp_sample;
	auto distractor_airspeed = airspeed;
	auto distractor_air_data = air_data;
	auto distractor_battery = battery;
	distractor_airspeed.indicated_airspeed_m_s = 99.F;
	distractor_airspeed.confidence = 0.F;
	distractor_air_data.rho = .4F;
	distractor_battery.id = 9;
	distractor_battery.voltage_v = 6.F;
	check(airspeed_zero.publish(distractor_airspeed) && air_data_zero.publish(distractor_air_data)
	      && battery_zero.publish(distractor_battery), "publish distinguishable condition distractors on instance zero");
	publish();
	check(observation.capture(policy, sample) && near(sample.indicated_airspeed_m_s, 20.F)
	      && near(sample.density_kg_m3, 1.2F) && near(sample.voltage_v, 22.F)
	      && sample.source.airspeed_instance == 1 && sample.source.air_data_instance == 1 && sample.source.battery_instance == 1
	      && sample.source.airspeed_revision && sample.source.air_data_revision && sample.source.battery_revision
	      && sample.source.baro_device_id == 456 && sample.source.baro_calibration_count == 2
	      && sample.source.temperature_source == 1 && sample.source.battery_id == 2
	      && sample.source.battery_source == battery_status_s::SOURCE_POWER_MODULE,
	      "native adapter uses selected raw topics and reports their actual source identities");
	check(sample.airspeed_timestamp_us == original_sample && sample.airspeed_sample_us == original_sample
	      && sample.air_data_timestamp_us == original_sample && sample.air_data_sample_us == original_sample
	      && sample.battery_timestamp_us == original_sample && sample.observed_us >= original_sample
	      && sample.deadline_us == original_sample + static_cast<uint64_t>(maximum_age_us) + 1,
	      "condition observation retains every original clock and derives its exclusive expiry");
	const auto original_deadline = sample.deadline_us;
	airspeed.timestamp = air_data.timestamp = battery.timestamp = hrt_absolute_time();
	publish();
	check(observation.capture(policy, sample) && sample.deadline_us == original_deadline,
	      "republishing held physical sensor samples cannot renew the original condition deadline");

	for (float confidence : {0.F, -.1F, 1.1F, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()}) {
		refresh();
		airspeed.confidence = confidence;
		publish();
		rejected("zero invalid or nonfinite sensor confidence cannot become a usable condition observation");
	}

	for (uint8_t temperature_source : {0, 1, 2}) {
		const auto before = sample.source;
		refresh();
		air_data.temperature_source = temperature_source;
		publish();
		check(observation.capture(policy, sample) && sample.source.temperature_source == temperature_source
		      && !sameOperatingConditionSource(before, sample.source),
		      "defined upstream temperature categories remain visible as immutable session source identity");
	}

	refresh();
	air_data.temperature_source = 3;
	publish();
	rejected("undefined density temperature source cannot replace a condition observation");

	for (unsigned fault = 0; fault < 8; ++fault) {
		refresh();

		switch (fault) {
		case 0: battery.connected = false; break;

		case 1: battery.id = 3; break;

		case 2: battery.source = UINT8_MAX; break;

		case 3: battery.voltage_v = 0.F; break;

		case 4: battery.voltage_v = std::numeric_limits<float>::quiet_NaN(); break;

		case 5: air_data.rho = 0.F; break;

		case 6: air_data.rho = std::numeric_limits<float>::quiet_NaN(); break;

		case 7: air_data.baro_device_id = 0; break;
		}

		publish();
		rejected("battery connection identity source and finite positive density voltage remain required");
	}

	// Wait for an explicit clock condition, not an assumed sleep duration. This
	// makes timestamp 1 an expired physical sample under the generous 1 s policy.
	const auto clock_deadline = hrt_absolute_time() + static_cast<uint64_t>(maximum_age_us) + 200000;

	while (hrt_absolute_time() <= static_cast<uint64_t>(maximum_age_us) + 1 && hrt_absolute_time() < clock_deadline) {
		px4_usleep(1000);
	}

	check(hrt_absolute_time() > static_cast<uint64_t>(maximum_age_us) + 1, "native clock reaches the bounded stale-sample fixture condition");

	for (unsigned fault = 0; fault < 5; ++fault) {
		refresh();

		switch (fault) {
		case 0: airspeed.timestamp_sample = 1; break;

		case 1: air_data.timestamp_sample = 1; break;

		case 2: airspeed.timestamp_sample = 0; break;

		case 3: airspeed.timestamp_sample = airspeed.timestamp + 1; break;

		case 4: battery.timestamp = UINT64_MAX; break;
		}

		publish();
		rejected("fresh publication cannot repair stale zero reversed or future original condition clocks");
	}

	refresh();
	publish();
	check(observation.capture(policy, sample), "valid native condition measurements remain observable after rejected input");
	auto before = sample.source;
	++air_data.calibration_count;
	publish();
	check(observation.capture(policy, sample) && !sameOperatingConditionSource(before, sample.source),
	      "barometer recalibration changes operating-condition session identity");
	before = sample.source;
	battery.source = battery_status_s::SOURCE_EXTERNAL;
	publish();
	check(observation.capture(policy, sample) && !sameOperatingConditionSource(before, sample.source),
	      "changing the selected battery source changes operating-condition session identity");
	before = sample.source;
	airspeed_selected.~AirspeedPublication();
	rejected("loss of the selected airspeed publisher cannot consume the distractor instance");
	new (&airspeed_selected) AirspeedPublication{ORB_ID(airspeed)};
	const auto replaced_at = hrt_absolute_time();
	airspeed.timestamp = airspeed.timestamp_sample = air_data.timestamp = air_data.timestamp_sample = battery.timestamp = replaced_at;
	publish();
	check(airspeed_selected.get_instance() == policy.airspeed_instance && observation.capture(policy, sample)
	      && sample.source.airspeed_revision != before.airspeed_revision && sample.source.air_data_revision == before.air_data_revision
	      && sample.source.battery_revision == before.battery_revision && sample.source.baro_calibration_count == before.baro_calibration_count
	      && sample.source.battery_source == before.battery_source && !sameOperatingConditionSource(before, sample.source),
	      "replacement publisher retains its explicit instance but cannot reuse the old session identity");
}

// Run before commander/sensors start: owns these synthetic publishers exclusively.
static void nativeSelectedInputTests()
{
	using namespace research_stabilized_manual;
	const char *names[] = {"EKF2_MULTI_IMU", "EKF2_MULTI_MAG", "SENS_IMU_MODE", "SENS_MAG_MODE"};
	const int32_t expected[] = {0, 0, 1, 1};
	int32_t saved[4] {};
	param_t handles[4] {};

	for (unsigned i = 0; i < 4; ++i) {
		handles[i] = param_find(names[i]);
		check(handles[i] != PARAM_INVALID && param_get(handles[i], &saved[i]) == 0
		      && param_set_no_notification(handles[i], &expected[i]) == 0, "single estimator fixture parameters");
	}

	uORB::Publication<vehicle_attitude_s> attitude_pub{ORB_ID(vehicle_attitude)};
	uORB::Publication<vehicle_angular_velocity_s> rates_pub{ORB_ID(vehicle_angular_velocity)};
	uORB::Publication<estimator_status_s> estimator_pub{ORB_ID(estimator_status)};
	uORB::Publication<estimator_status_flags_s> flags_pub{ORB_ID(estimator_status_flags)};
	uORB::Publication<manual_control_setpoint_s> manual_pub{ORB_ID(manual_control_setpoint)};
	uORB::Publication<sensor_selection_s> selection_pub{ORB_ID(sensor_selection)};
	uORB::Publication<actuator_armed_s> armed_pub{ORB_ID(actuator_armed)};
	vehicle_attitude_s attitude{};
	vehicle_angular_velocity_s rates{};
	estimator_status_s estimator{};
	estimator_status_flags_s flags{};
	manual_control_setpoint_s manual{};
	sensor_selection_s selection{};
	actuator_armed_s armed{};
	const auto now = hrt_absolute_time();
	attitude.timestamp = attitude.timestamp_sample = now;
	attitude.q[0] = 1.f;
	rates.timestamp = rates.timestamp_sample = now;
	estimator.timestamp = flags.timestamp = armed.timestamp = now;
	estimator.gyro_device_id = selection.gyro_device_id = 123;
	estimator.accel_device_id = selection.accel_device_id = 456;
	flags.cs_tilt_align = flags.cs_yaw_align = true;
	armed.prearmed = true;
	manual.timestamp = manual.timestamp_sample = now;
	check(attitude_pub.publish(attitude) && rates_pub.publish(rates) && estimator_pub.publish(estimator)
	      && flags_pub.publish(flags) && manual_pub.publish(manual) && selection_pub.publish(selection)
	      && armed_pub.publish(armed), "publish complete selected input fixture");
	SelectedStateObservation observation;
	SelectedStateInput bench;
	ObserverProfile profile{};
	profile.maximum_status_age_us = profile.maximum_permission_age_us = 1000000;
	auto capture = [&]() { return observation.capture(1000000, 1000000); };
	auto snapshot = capture();
	check(snapshot.state.valid && snapshot.permission_fresh && snapshot.state.source_epoch,
	      "fresh single estimator metadata observed");
	check(snapshot.state.attitude_sample_us == now && snapshot.manual.timestamp_sample_us == now,
	      "original sensor and pilot sample times retained");
	check(bench.capture(profile).prearmed_servo_condition, "bench accepts disarmed prearmed metadata");
	armed.armed = true;
	check(armed_pub.publish(armed), "publish armed observation");
	snapshot = capture();
	check(snapshot.state.valid && snapshot.permission_fresh && snapshot.permission.armed,
	      "armed state remains observable without granting authority");
	check(!bench.capture(profile).state.valid, "bench still rejects armed metadata");
	armed.armed = false;
	bool *inhibits[] = {&armed.kill, &armed.lockdown, &armed.termination, &armed.in_esc_calibration_mode};

	for (auto inhibit : inhibits) {
		*inhibit = true;
		check(armed_pub.publish(armed) && !bench.capture(profile).state.valid, "bench retains each permission inhibit");
		*inhibit = false;
	}

	armed.timestamp = UINT64_MAX;
	check(armed_pub.publish(armed) && !capture().permission_fresh && !bench.capture(profile).state.valid,
	      "future permission cannot qualify bench");
	armed.timestamp = 0;
	check(armed_pub.publish(armed) && !capture().permission_fresh, "missing permission timestamp rejected");
	armed.timestamp = now;
	check(armed_pub.publish(armed), "restore permission");
	check(!observation.capture(0, 1000000).state.valid && !observation.capture(1000000, 0).permission_fresh,
	      "zero freshness budgets reject");
	selection.gyro_device_id++;
	check(selection_pub.publish(selection) && !capture().state.valid, "selected gyro mismatch rejected");
	selection.gyro_device_id--;
	check(selection_pub.publish(selection), "restore selected gyro");
	flags.fs_bad_hdg = true;
	check(flags_pub.publish(flags) && !capture().state.valid, "bad heading metadata rejected");
	flags.fs_bad_hdg = false;
	flags.timestamp = UINT64_MAX;
	check(flags_pub.publish(flags) && !capture().state.valid, "future estimator metadata rejected");
	flags.timestamp = now;
	check(flags_pub.publish(flags), "restore flags");
	const int32_t unsupported = 1;
	check(param_set_no_notification(handles[0], &unsupported) == 0 && !capture().state.valid,
	      "multi estimator configuration rejected");
	check(param_set_no_notification(handles[0], &expected[0]) == 0, "restore single estimator");
	const auto epoch = capture().state.source_epoch;
	check(attitude_pub.unadvertise() && !capture().state.valid, "missing attitude publisher invalidates observation");
	uORB::Publication<vehicle_attitude_s> replacement_attitude{ORB_ID(vehicle_attitude)};
	check(replacement_attitude.advertise() && !capture().state.valid, "new lifetime cannot inherit retained attitude");
	check(replacement_attitude.publish(attitude), "replacement publishes identical attitude");
	snapshot = capture();
	check(snapshot.state.valid && snapshot.state.source_epoch != epoch, "identical replacement changes source epoch");

	for (unsigned i = 0; i < 4; ++i) {
		check(param_set_no_notification(handles[i], &saved[i]) == 0, "restore original estimator parameter");
	}

	PX4_INFO("SLAAPS_INPUT_TEST_PASS checks=%u", checks);
}

static void nativeActivationWireTests()
{
	for (unsigned bank = 0; bank < 2; ++bank) {
		const auto topic = bank == 0 ? ORB_ID::slaaps_manual_active_main : ORB_ID::slaaps_manual_active_aux;
		uORB::Publication<slaaps_manual_activation_s> publisher{topic};
		uORB::Publication<slaaps_manual_activation_s> replacement{topic};
		uORB::ResearchSubscription lifetime{topic};
		BoundSubscription<slaaps_manual_activation_s> subscriber{topic};
		const ManualActivationAck original{1000, 17, 12, 42, 51 + bank, 2100, 14100, 77, 88 + bank, static_cast<uint8_t>(bank)};
		slaaps_manual_activation_s wire{}, received{};
		ManualActivationAck decoded{};
		check(encodeManualActivation(3000, bank, original, wire) && publisher.publish(wire)
		      && subscriber.bind(lifetime.revision()), "activation reply binds actual publisher lifetime");
		check(subscriber.read(received) == BoundRead::Data && decodeManualActivation(4000, bank, received, decoded)
		      && sameManualActivation(decoded, original), "native activation topic roundtrip preserves confirmation");
		check(!decodeManualActivation(4000, 1 - bank, received, decoded), "native activation reply cannot cross bank roles");
		publisher.unadvertise();
		check(replacement.publish(wire) && subscriber.read(received) == BoundRead::Rejected,
		      "identical activation payload from replacement publisher cannot inherit binding");
		check(subscriber.read(received) == BoundRead::Rejected, "activation publisher replacement rejection remains latched");
	}
}

static void nativePreparationWireTests()
{
	NativePermissionPublisher permission_publisher;
	const ManualPreparationRequest request{1000, 20000, 1000, 11, 12, 77, {88, 99}, {42, {51, 52}, 3}, permission_publisher.revision()};
	uORB::Publication<slaaps_manual_preparation_s> proposal{ORB_ID(slaaps_manual_prepare)};
	uORB::Publication<slaaps_manual_preparation_s> main{ORB_ID(slaaps_manual_ready_main)};
	uORB::Publication<slaaps_manual_preparation_s> aux{ORB_ID(slaaps_manual_ready_aux)};
	auto roundtrip = [&](auto & publisher, ORB_ID topic, unsigned bank, uint64_t prepared) {
		slaaps_manual_preparation_s wire{}, received{};
		check(encodeManualPreparation(1300, request, bank, prepared, wire) && publisher.publish(wire),
		      "native preparation topic publishes generated schema");
		uORB::ResearchSubscription lifetime{topic};
		BoundSubscription<slaaps_manual_preparation_s> subscriber{topic};
		ManualPreparationRequest decoded{};
		uint64_t ready_time = 0;
		check(subscriber.bind(lifetime.revision()) && subscriber.read(received) == BoundRead::Data
		      && decodeManualPreparation(1400, bank, received, decoded, ready_time)
		      && sameManualPreparation(request, decoded) && ready_time == prepared,
		      "native preparation topics preserve proposal with atomic publisher binding");
	};
	roundtrip(proposal, ORB_ID::slaaps_manual_prepare, slaaps_manual_preparation_s::REQUEST_BANK, 0);
	roundtrip(main, ORB_ID::slaaps_manual_ready_main, 0, 1200);
	roundtrip(aux, ORB_ID::slaaps_manual_ready_aux, 1, 1250);
}

static void nativeTransportTests()
{
	const auto frame = transportFrame(5000); // Synthetic time; serialization test only.
	slaaps_manual_command_s wire{}, copied{};
	check(encodeManualCommand(5000, frame, wire), "native command encode");
	uORB::Publication<slaaps_manual_command_s> publisher{ORB_ID(slaaps_manual_command)};
	uORB::Subscription subscriber{ORB_ID(slaaps_manual_command)};
	check(publisher.publish(wire) && subscriber.update(&copied), "actual uORB command publish and copy");
	ManualWorkerOutput decoded{};
	check(decodeManualCommand(5000, copied, decoded) && decoded.sequence == frame.sequence
	      && decoded.edf.request_timestamp_us == frame.edf.request_timestamp_us, "native command decode preserves identity");
	uORB::Publication<slaaps_manual_ack_s> main_pub{ORB_ID(slaaps_manual_ack_main)};
	uORB::Publication<slaaps_manual_ack_s> aux_pub{ORB_ID(slaaps_manual_ack_aux)};
	uORB::Subscription main_sub{ORB_ID(slaaps_manual_ack_main)};
	uORB::Subscription aux_sub{ORB_ID(slaaps_manual_ack_aux)};
	slaaps_manual_ack_s main_wire{}, aux_wire{}, main_copy{}, aux_copy{};
	const ManualOutputAck main_ack{1000, 17, 31, 42, 51, 7, 5000, 5100, 0};
	const ManualOutputAck aux_ack{1000, 17, 31, 42, 52, 7, 5000, 5200, 1};
	check(encodeManualAck(5300, 0, main_ack, main_wire) && encodeManualAck(5300, 1, aux_ack, aux_wire), "native bank ack encode");
	check(main_pub.publish(main_wire) && aux_pub.publish(aux_wire)
	      && main_sub.update(&main_copy) && aux_sub.update(&aux_copy), "separate uORB bank replies both survive publication");
	ManualOutputAck decoded_main{}, decoded_aux{};
	check(decodeManualAck(5300, 0, main_copy, decoded_main) && decodeManualAck(5300, 1, aux_copy, decoded_aux)
	      && decoded_main.driver == 51 && decoded_aux.driver == 52, "native bank topics preserve driver identity");
	check(!decodeManualAck(5300, 0, aux_copy, decoded_main), "native wrong-bank reply rejected");
	uORB::ResearchSubscription lifetime{ORB_ID(slaaps_manual_command)};
	const unsigned original_revision = lifetime.revision();
	BoundSubscription<slaaps_manual_command_s> bound{ORB_ID::slaaps_manual_command};
	check(!bound.bind(0) && !bound.copy(copied), "zero lifetime never binds");
	BoundSubscription<uint8_t> wrong_size{ORB_ID::slaaps_manual_command};
	check(!wrong_size.bind(original_revision), "wrong payload size cannot bind or overrun copy storage");
	check(bound.bind(original_revision) && bound.copy(copied), "explicit live lifetime admits atomic copy");
	check(publisher.unadvertise(), "command publisher stops");
	const auto preserved = copied;
	check(!bound.copy(copied) && !bound.valid() && memcmp(&preserved, &copied, sizeof(copied)) == 0,
	      "unadvertisement latches rejection and preserves destination");
	uORB::Publication<slaaps_manual_command_s> replacement{ORB_ID(slaaps_manual_command)};
	check(replacement.advertise(), "replacement advertises before first payload");
	const unsigned replacement_revision = lifetime.revision();
	check(replacement_revision && replacement_revision != original_revision, "replacement has distinct lifetime");
	check(!bound.bind(original_revision), "old lifetime cannot bind replacement");
	check(bound.bind(replacement_revision) && !bound.copy(copied), "advertisement cannot inherit old payload");
	check(replacement.publish(wire) && !bound.copy(copied), "new payload does not clear latched rejection");
	check(bound.bind(replacement_revision) && bound.copy(copied), "explicit readmission accepts new lifetime");
	check(replacement.unadvertise(), "replacement stops between consumer reads");
	uORB::Publication<slaaps_manual_command_s> hidden_replacement{ORB_ID(slaaps_manual_command)};
	check(hidden_replacement.publish(wire) && !bound.copy(copied), "identical payload hidden replacement rejected");
	bound.revoke();
	check(!bound.copy(copied), "explicit revoke prevents copy");

}
static void nativeBankTransportTests()
{
	const auto c = fixture();
	const auto inventory = routingFixture();
	const ManualOutputBinding binding{42, {51, 52}, 3};
	auto permission_now = permission(1000);
	permission_now.configuration = fingerprint(c);

	for (unsigned scenario = 0; scenario < 9; ++scenario) {
		uORB::Publication<slaaps_manual_command_s> publisher{ORB_ID(slaaps_manual_command)};
		uORB::ResearchSubscription lifetime{ORB_ID(slaaps_manual_command)};
		ManualBankTransport<0> main;
		ManualBankTransport<1> aux;
		check(publisher.advertise() && main.advertise() && aux.advertise(), "native endpoint advertisements");
		check(main.begin(1000, c, inventory, binding, permission_now, lifetime.revision())
		      && aux.begin(1000, c, inventory, binding, permission_now, lifetime.revision()), "native bank transport explicit entry");
		check(main.dispatch(1000, permission_now, inventory, binding).disposition == EdfDisposition::RecoveryRequired
		      && main.fault() == ManualBankFault::None, "initial silence waits without inventing output");

		if (scenario == 8) {
			check(main.dispatch(1000 + c.cycle_max_us, permission_now, inventory, binding).disposition == EdfDisposition::RecoveryRequired
			      && main.fault() == ManualBankFault::Expired, "initial silence expires independently");
			continue;
		}

		auto frame = transportFrame(5000);
		frame.sequence = frame.edf.sequence = 1;
		frame.edf.configuration = permission_now.configuration;
		frame.edf.normalized = 0.F;
		slaaps_manual_command_s wire{};
		check(encodeManualCommand(5000, frame, wire) && publisher.publish(wire), "native combined frame publication");
		const auto main_output = main.dispatch(5000, permission_now, inventory, binding);
		const auto aux_output = aux.dispatch(5000, permission_now, inventory, binding);
		check(main_output.disposition == EdfDisposition::Command && aux_output.disposition == EdfDisposition::Command
		      && main.awaitingHandling() && aux.awaitingHandling(), "both bank endpoints prepare commands");
		uORB::ResearchSubscription main_reply{ORB_ID(slaaps_manual_ack_main)};
		uORB::ResearchSubscription aux_reply{ORB_ID(slaaps_manual_ack_aux)};
		slaaps_manual_ack_s reply{};
		unsigned revision{};
		check(!main_reply.copyBound(&reply, revision)
		      && !aux_reply.copyBound(&reply, revision), "preparation releases no reply or inherited reply");
		check(main.dispatch(5100, permission_now, inventory, binding).sequence == 1 && main.fault() == ManualBankFault::None,
		      "rereading held sample does not duplicate acceptance");

		if (scenario == 0) {
			check(main.confirmHandled(5200, permission_now, inventory, binding, main_output, true)
			      && aux.confirmHandled(5200, permission_now, inventory, binding, aux_output, true), "exact handled tickets release native replies");
			ManualOutputAck ack{};
			check(main_reply.copyBound(&reply, revision) && decodeManualAck(5200, 0, reply, ack) && ack.sequence == 1
			      && ack.accepted_us == 5200, "MAIN handled acknowledgment crosses uORB");
			check(aux_reply.copyBound(&reply, revision) && decodeManualAck(5200, 1, reply, ack) && ack.driver == 52,
			      "AUX handled acknowledgment crosses independent uORB topic");
			check(main.dispatch(frame.deadline_us, permission_now, inventory, binding).disposition == EdfDisposition::RecoveryRequired
			      && main.fault() == ManualBankFault::Expired, "held traffic never extends deadline");
			continue;
		}

		if (scenario == 1) { wire.timestamp = 5100; check(publisher.publish(wire), "mutated held frame published"); }

		if (scenario == 2) { wire.sequence = 2; check(publisher.publish(wire), "premature next frame published"); }

		if (scenario == 3) { wire.edf = NAN; check(publisher.publish(wire), "invalid wire published"); }

		if (scenario == 4) {
			check(publisher.unadvertise(), "publisher withdrawn before handling");
			uORB::Publication<slaaps_manual_command_s> replacement{ORB_ID(slaaps_manual_command)};
			check(replacement.publish(wire) && !main.confirmHandled(5200, permission_now, inventory, binding, main_output, true),
			      "hidden identical replacement blocks handling acknowledgment");

		} else if (scenario == 5) {
			uORB::Publication<slaaps_manual_ack_s> replacement{ORB_ID(slaaps_manual_ack_main)};
			check(replacement.advertise() && !main.confirmHandled(5200, permission_now, inventory, binding, main_output, true),
			      "reply advertisement replacement revokes endpoint");

		} else if (scenario == 6) {
			auto altered = main_output;
			altered.pwm_us[6] += 1.F;
			check(!main.confirmHandled(5200, permission_now, inventory, binding, altered, true), "altered ticket cannot acknowledge");

		} else {
			check(!main.confirmHandled(5200, permission_now, inventory, binding, main_output, scenario != 7),
			      "invalid traffic or failed handling cannot acknowledge");
		}

		check(main.dispatch(5300, permission_now, inventory, binding).disposition == EdfDisposition::RecoveryRequired
		      && !main_reply.copyBound(&reply, revision), "failed endpoint stays revoked without reply");
	}
}

static void nativePreparedBankTests()
{
	NativePermissionPublisher permission_publisher;
	const auto c = fixture();
	const auto inventory = routingFixture();
	const ManualOutputBinding binding{42, {51, 52}, 3};

	for (unsigned scenario = 0; scenario < 6; ++scenario) {
		uORB::Publication<slaaps_manual_command_s> publisher{ORB_ID(slaaps_manual_command)};
		uORB::ResearchSubscription lifetime{ORB_ID(slaaps_manual_command)};
		ManualBankTransport<0> main;
		uORB::Publication<slaaps_manual_command_s> replacement_command{ORB_ID(slaaps_manual_command)};
		uORB::Publication<slaaps_manual_ack_s> replacement_reply{ORB_ID(slaaps_manual_ack_main)};
		check(publisher.advertise() && main.advertise(), "preparation endpoint advertisements");
		auto p = permission(1000);
		p.configuration = fingerprint(c);
		const unsigned revision = lifetime.revision();
		ManualPreparationRequest proposal{1000, 20000, 1000, p.source, p.configuration, revision, {main.acknowledgmentRevision(), 99}, binding, permission_publisher.revision()};
		check(main.prepare(1000, c, inventory, proposal), "native bank prepares without activating consumer");
		const auto inactive = main.dispatch(1000, p, inventory, binding);
		check(inactive.disposition != EdfDisposition::Command && !inactive.channel_mask && !main.awaitingHandling(),
		      "prepared bank cannot output even with an armed permission argument");
		BoundSubscription<slaaps_manual_ack_s> ack{ORB_ID::slaaps_manual_ack_main};
		slaaps_manual_ack_s reply{};
		check(ack.bind(main.acknowledgmentRevision()) && ack.read(reply) == BoundRead::Waiting,
		      "preparation never produces a handled-command acknowledgment");
		auto candidate = c;
		auto live = inventory;

		if (scenario == 1) { p.flight_admitted = false; }

		if (scenario == 2) { candidate.mass += .1F; }

		if (scenario == 3) { ++live.channels[0].rate_hz; }

		if (scenario == 4) { check(replacement_command.advertise(), "replace command publisher after preparation"); }

		if (scenario == 5) {
			check(replacement_reply.advertise(), "replace reply publisher after preparation");
			check(!main.acknowledgmentRevision() && !main.advertise(), "bank cannot silently adopt replacement reply lifetime");
			ManualBankTransport<0> restarted;
			check(restarted.advertise() && !restarted.prepare(2000, c, inventory, proposal),
			      "new bank instance rejects retained proposal addressed to the old reply lifetime");
		}

		if (scenario) {
			check(!main.beginPrepared(2000, candidate, live, binding, p, revision),
			      "prepared activation rejects changed ownership routing or permission");
			check(main.dispatch(2000, p, inventory, binding).disposition != EdfDisposition::Command,
			      "failed activation never creates output");
			continue;
		}

		check(main.beginPrepared(2000, c, inventory, binding, p, revision), "prepared activation passes independent armed grant checks");
		proposal.session = proposal.timestamp_us = 2000;
		check(!main.prepare(2000, c, inventory, proposal) && main.fault() == ManualBankFault::None,
		      "new preparation cannot disturb an active bank");
		auto frame = transportFrame(5000);
		frame.sequence = frame.edf.sequence = 1;
		frame.edf.configuration = p.configuration;
		frame.edf.normalized = 0.F;
		slaaps_manual_command_s wire{};
		check(encodeManualCommand(5000, frame, wire) && publisher.publish(wire), "publish first command after prepared activation");
		const auto output = main.dispatch(5000, p, inventory, binding);
		check(output.disposition == EdfDisposition::Command && output.channel_mask, "only activated bank produces a handling candidate");
		check(main.confirmHandled(5000, p, inventory, binding, output, true), "activated bank confirms actual handling contract");
		main.revoke();
		check(!main.beginPrepared(5000, c, inventory, binding, p, revision), "consumed preparation cannot reactivate after revoke");
	}
}

template<unsigned Bank>
static void nativePreparationEndpointTests(unsigned scenarios)
{
	NativePermissionPublisher permission_publisher;
	const auto c = fixture();
	const auto inventory = routingFixture();
	const ManualOutputBinding binding{42, {51, 52}, 3};
	constexpr ORB_ID ready_topic = Bank == 0 ? ORB_ID::slaaps_manual_ready_main : ORB_ID::slaaps_manual_ready_aux;
	constexpr ORB_ID ack_topic = Bank == 0 ? ORB_ID::slaaps_manual_ack_main : ORB_ID::slaaps_manual_ack_aux;
	constexpr ORB_ID active_topic = Bank == 0 ? ORB_ID::slaaps_manual_active_main : ORB_ID::slaaps_manual_active_aux;

	for (unsigned scenario = 0; scenario < scenarios; ++scenario) {
		uORB::Publication<slaaps_manual_command_s> commands{ORB_ID(slaaps_manual_command)};
		uORB::ResearchSubscription command_lifetime{ORB_ID(slaaps_manual_command)};
		uORB::Publication<slaaps_manual_preparation_s> proposals{ORB_ID(slaaps_manual_prepare)};
		uORB::ResearchSubscription proposal_lifetime{ORB_ID(slaaps_manual_prepare)};
		ManualBankTransport<Bank> bank;
		ManualPreparationEndpoint<Bank> endpoint{bank};
		uORB::Publication<slaaps_manual_command_s> replacement_command{ORB_ID(slaaps_manual_command)};
		uORB::Publication<slaaps_manual_ack_s> replacement_ack{ack_topic};
		uORB::Publication<slaaps_manual_preparation_s> replacement_ready{ready_topic};
		uORB::Publication<slaaps_manual_preparation_s> replacement_proposal{ORB_ID(slaaps_manual_prepare)};
		check(commands.advertise() && proposals.advertise() && bank.advertise() && endpoint.advertise(),
		      "preparation wire endpoints advertise without authority");
		uORB::ResearchSubscription activation_lifetime{active_topic};
		BoundSubscription<slaaps_manual_activation_s> activation{active_topic};
		slaaps_manual_activation_s activation_wire{};
		const auto activation_revision = endpoint.activationRevision();
		check(activation_revision && activation_revision == activation_lifetime.revision()
		      && activation.bind(activation_revision) && activation.read(activation_wire) == BoundRead::Waiting,
		      "activation reply identity exists before any proposal or transition binding without an activation payload");
		check(endpoint.advertise() && endpoint.activationRevision() == activation_revision
		      && activation.read(activation_wire) == BoundRead::Waiting,
		      "repeated endpoint advertisement preserves the same non-actuating activation lifetime");
		check(endpoint.listen(900, proposal_lifetime.revision())
		      && endpoint.poll(900, c, inventory) == PreparationStatus::Waiting, "preparation endpoint waits for first proposal");
		auto permission_now = permission(1000);
		permission_now.configuration = fingerprint(c);
		ManualPreparationRequest request{1000, 20000, 1000, permission_now.source, permission_now.configuration,
						 command_lifetime.revision(), {88, 99}, binding, permission_publisher.revision()};
		request.acknowledgment_revisions[Bank] = bank.acknowledgmentRevision();
		slaaps_manual_preparation_s wire{}, reply{};
		check(encodeManualPreparation(1000, request, slaaps_manual_preparation_s::REQUEST_BANK, 0, wire)
		      && proposals.publish(wire) && endpoint.poll(1100, c, inventory) == PreparationStatus::Ready,
		      "wire proposal prepares bank and publishes readiness");
		BoundSubscription<slaaps_manual_preparation_s> ready{ready_topic};
		ManualPreparationRequest decoded{};
		uint64_t prepared{};
		check(ready.bind(endpoint.revision()) && ready.read(reply) == BoundRead::Data
		      && decodeManualPreparation(1100, Bank, reply, decoded, prepared) && sameManualPreparation(decoded, request)
		      && prepared == 1100, "readiness is bound and echoes successfully prepared proposal");
		check(encodeManualPreparation(1200, request, slaaps_manual_preparation_s::REQUEST_BANK, 0, wire)
		      && proposals.publish(wire) && endpoint.poll(1400, c, inventory) == PreparationStatus::Ready
		      && ready.read(reply) == BoundRead::Data && reply.timestamp == 1100 && reply.prepared == 1100,
		      "republished duplicate preserves original preparation and readiness times");
		check(!bank.active() && bank.dispatch(1400, permission_now, inventory, binding).disposition != EdfDisposition::Command,
		      "wire readiness cannot activate output with an armed argument");

		if (scenario == 0) {
			check(endpoint.activate(1500, c, inventory, binding, permission_now, request.command_revision) && bank.active(),
			      "trusted activation consumes wire preparation once");
			check(ready.read(reply) == BoundRead::Data && !decodeManualPreparation(1500, Bank, reply, decoded, prepared),
			      "activation invalidates retained readiness");
			check(!endpoint.listen(1600, proposal_lifetime.revision())
			      && endpoint.poll(1600, c, inventory) == PreparationStatus::Inactive && bank.active(),
			      "preparation traffic cannot disturb active bank");
			endpoint.cancel(1600);
			check(bank.active(), "preparation cancellation cannot revoke active flight");
			bank.revoke();
			check(!endpoint.activate(1700, c, inventory, binding, permission_now, request.command_revision),
			      "consumed wire preparation cannot reactivate");
			continue;
		}

		auto candidate = c;
		auto live = inventory;
		uint64_t now = 1500;

		switch (scenario) {
		case 1: candidate.mass += .1F; break;

		case 2: ++live.channels[0].rate_hz; break;

		case 3: now = 20000; break;

		case 4: now = 1300; break;

		case 5: --wire.deadline; check(proposals.publish(wire), "mutated same-session proposal publishes"); break;

		case 6: check(replacement_command.advertise(), "replace preparation command endpoint"); break;

		case 7: check(replacement_ack.advertise(), "replace preparation handled-reply endpoint"); break;

		case 8: check(replacement_ready.advertise(), "replace preparation readiness endpoint"); break;

		case 9: check(replacement_proposal.advertise(), "replace preparation proposal endpoint"); break;

		case 10: wire.bank = Bank; check(proposals.publish(wire), "wrong-role preparation publishes"); break;

		case 11: permission_now.flight_admitted = false; break;

		case 12: endpoint.cancel(now); break;
		}

		if (scenario != 11 && scenario != 12) {
			check(endpoint.poll(now, candidate, live) == PreparationStatus::Rejected, "live preparation fault cancels readiness");
		}

		check(!endpoint.activate(now, candidate, live, binding, permission_now, request.command_revision) && !bank.active(),
		      "failed or cancelled preparation never activates bank");
		check(ready.read(reply) != BoundRead::Data || !decodeManualPreparation(now, Bank, reply, decoded, prepared),
		      "fault leaves no decodable positive readiness on bound topic");
	}

	ManualBankTransport<Bank> bank;
	ManualPreparationEndpoint<Bank> endpoint{bank};
	check(bank.advertise() && endpoint.advertise(), "advertise endpoint for activation lifetime replacement regression");
	uORB::Publication<slaaps_manual_activation_s> replacement{active_topic};
	check(replacement.advertise() && !endpoint.activationRevision() && !endpoint.advertise(),
	      "activation publisher replacement invalidates owned endpoint advertisement and cannot silently rebind");
}

static void nativeRoutingObservationTests()
{
	OutputRoutingObservation observation;
	ReportedRoutingSnapshot snapshot{};
	check(observation.capture(2000, snapshot) == InventoryFault::MissingBank && !snapshot.captured_us,
	      "routing observation requires both physical banks");
	slaaps_output_bank_s banks[2] {};

	for (unsigned bank = 0; bank < 2; ++bank) {
		auto &report = banks[bank];
		report.timestamp = 1000;
		report.driver_instance = bank + 1;
		report.bank = bank;
		report.channel_count = 8;
		report.hardware_mask = 255;
		report.ready = true;

		for (unsigned channel = 0; channel < 8; ++channel) {
			report.rate_hz[channel] = 50;
			report.minimum_us[channel] = 1100;
			report.center_us[channel] = 1500;
			report.maximum_us[channel] = 1900;
		}
	}

	banks[0].function[7] = kCanardFunctionFirst;
	banks[1].function[2] = kCanardFunctionFirst + 1;
	banks[0].function[4] = kCanardFunctionFirst + 2;
	banks[1].function[0] = kCanardFunctionFirst + 3;
	banks[1].function[6] = kEdfFunction;
	uORB::PublicationMulti<slaaps_output_bank_s> aux{ORB_ID::slaaps_output_bank};
	uORB::PublicationMulti<slaaps_output_bank_s> main{ORB_ID::slaaps_output_bank};
	check(aux.advertise() && observation.capture(2000, snapshot) == InventoryFault::DriverNotReady,
	      "advertisement without report cannot form routing snapshot");
	check(aux.publish(banks[1]) && main.publish(banks[0])
	      && observation.capture(2000, snapshot) == InventoryFault::None,
	      "atomic report copies produce complete routing observation");
	check(snapshot.driver[0] == 1 && snapshot.driver[1] == 2 && snapshot.publisher_revision[0] && snapshot.publisher_revision[1]
	      && snapshot.topic_instance[0] == main.get_instance() && snapshot.topic_instance[1] == aux.get_instance()
	      && snapshot.topic_instance[0] != snapshot.topic_instance[1], "physical bank identity is independent of instance ordering");
	RoutingPlan plan{};
	check(makeRoutingPlan(fixture(), snapshot.inventory, true, plan).valid() && snapshot.inventory.count == 16,
	      "retained observation includes complete effective routing");
	const auto original = snapshot;
	banks[0].timestamp = banks[1].timestamp = 1500;
	check(main.publish(banks[0]) && aux.publish(banks[1]) && observation.capture(2000, snapshot) == InventoryFault::None
	      && sameReportedDrivers(original, snapshot) && snapshot.reported_us[0] == 1500,
	      "heartbeat generations refresh time without changing driver identity");
	++banks[0].driver_instance;
	check(main.publish(banks[0]) && observation.capture(2000, snapshot) == InventoryFault::None
	      && !sameReportedDrivers(original, snapshot), "changed device lifetime invalidates old driver identity");
	--banks[0].driver_instance;
	check(main.publish(banks[0]) && observation.capture(2000, snapshot) == InventoryFault::None
	      && sameReportedDrivers(original, snapshot), "fixture restores original report before publisher restart");
	check(main.unadvertise() && observation.capture(2000, snapshot) == InventoryFault::MissingBank,
	      "removed publisher invalidates routing capture");
	uORB::PublicationMulti<slaaps_output_bank_s> restarted{ORB_ID::slaaps_output_bank};
	check(restarted.publish(banks[0]) && observation.capture(2000, snapshot) == InventoryFault::None
	      && !sameReportedDrivers(original, snapshot) && snapshot.driver[0] == original.driver[0],
	      "identical restarted payload has a distinct publisher identity");
	const auto restarted_snapshot = snapshot;
	uORB::PublicationMulti<slaaps_output_bank_s> extra{ORB_ID::slaaps_output_bank};
	check(extra.publish(banks[0]) && observation.capture(2000, snapshot) == InventoryFault::DuplicateBank,
	      "extra advertised instance cannot be silently omitted");
	check(extra.unadvertise() && observation.capture(2000, snapshot) == InventoryFault::None
	      && sameReportedDrivers(restarted_snapshot, snapshot), "complete enumeration recovers after extra publisher disappears");
	snapshot.captured_us = 777;
	check(observation.capture(1001500, snapshot) == InventoryFault::Stale && snapshot.captured_us == 777,
	      "stale capture leaves last snapshot untouched and returns explicit failure");
	banks[1].ready = false;
	check(aux.publish(banks[1]) && observation.capture(2000, snapshot) == InventoryFault::DriverNotReady
	      && snapshot.captured_us == 777, "unready driver cannot replace retained snapshot");
	banks[1].ready = true;
	banks[1].bank = 0;
	check(aux.publish(banks[1]) && observation.capture(2000, snapshot) == InventoryFault::DuplicateBank,
	      "two instances claiming MAIN cannot stand in for MAIN and AUX");
	banks[1].bank = 1;
	banks[1].timestamp = 2001;
	check(aux.publish(banks[1]) && observation.capture(2000, snapshot) == InventoryFault::Stale,
	      "future report timestamp rejected");
	banks[1].timestamp = 1500;
	check(aux.publish(banks[1]) && observation.capture(2000, snapshot) == InventoryFault::None,
	      "fresh complete observations restore diagnostic snapshot");
	auto changed = snapshot;
	++changed.topic_instance[0];
	check(!sameReportedDrivers(snapshot, changed) && !sameReportedDrivers(snapshot, ReportedRoutingSnapshot{}),
	      "driver identity requires matching instance and successful capture metadata");
}

static void nativePreparationCoordinatorTests()
{
	NativePermissionPublisher permission_publisher;
	const auto c = fixture();
	const auto inventory = routingFixture();
	const ManualOutputBinding binding{42, {51, 52}, 3};

	for (unsigned scenario = 0; scenario < 15; ++scenario) {
		uORB::Publication<slaaps_manual_command_s> commands{ORB_ID(slaaps_manual_command)};
		uORB::ResearchSubscription command_lifetime{ORB_ID(slaaps_manual_command)};
		ManualBankTransport<0> main;
		ManualBankTransport<1> aux;
		ManualPreparationEndpoint<0> main_endpoint{main};
		ManualPreparationEndpoint<1> aux_endpoint{aux};
		ManualPreparationCoordinator coordinator;
		const bool inject_payload = scenario >= 10 && scenario <= 12;
		uORB::Publication<slaaps_manual_preparation_s> payload_ready{ORB_ID(slaaps_manual_ready_aux)};
		uORB::ResearchSubscription payload_lifetime{ORB_ID(slaaps_manual_ready_aux)};
		uORB::Publication<slaaps_manual_preparation_s> replacement_proposal{ORB_ID(slaaps_manual_prepare)};
		uORB::Publication<slaaps_manual_preparation_s> replacement_ready{ORB_ID(slaaps_manual_ready_aux)};
		uORB::Publication<slaaps_manual_command_s> replacement_command{ORB_ID(slaaps_manual_command)};
		uORB::Publication<slaaps_manual_ack_s> replacement_ack{ORB_ID(slaaps_manual_ack_aux)};
		check(commands.advertise() && main.advertise() && aux.advertise()
		      && main_endpoint.advertise() && aux_endpoint.advertise() && coordinator.advertise()
		      && (!inject_payload || payload_ready.advertise()),
		      "all preparation peers advertise before supervisor transaction");
		check(main_endpoint.listen(900, coordinator.revision()) && (inject_payload || aux_endpoint.listen(900, coordinator.revision())),
		      "banks bind coordinator lifetime before proposal");
		auto p = permission(1000);
		p.configuration = fingerprint(c);
		ManualPreparationRequest request{1000, 20000, 1000, p.source, p.configuration, command_lifetime.revision(),
			{main.acknowledgmentRevision(), aux.acknowledgmentRevision()}, binding, permission_publisher.revision()};
		unsigned revisions[2] {main_endpoint.revision(), inject_payload ? payload_lifetime.revision() : aux_endpoint.revision()};

		if (scenario == 14) { revisions[1] = 0; }

		if (scenario == 14) {
			check(!coordinator.begin(1000, c, inventory, request, revisions), "incomplete readiness publisher set rejects begin");
			revisions[1] = aux_endpoint.revision();
			check(!coordinator.begin(1000, c, inventory, request, revisions), "failed coordinator entry consumes session token");
			continue;
		}

		check(coordinator.begin(1000, c, inventory, request, revisions)
		      && coordinator.poll(1000, c, inventory) == PreparationStatus::Waiting,
		      "retained pre-proposal replies never satisfy readiness");
		check(main_endpoint.poll(1100, c, inventory) == PreparationStatus::Ready
		      && coordinator.poll(1200, c, inventory) == PreparationStatus::Waiting,
		      "one ready bank cannot complete a two-bank transaction");

		if (scenario == 13) {
			check(coordinator.poll(20000, c, inventory) == PreparationStatus::Rejected,
			      "silent required bank expires the whole preparation");
			check(main_endpoint.poll(20000, c, inventory) == PreparationStatus::Rejected && !main.active() && !aux.active(),
			      "expired coordinator cancels peer preparation without output");
			continue;
		}

		slaaps_manual_preparation_s injected{};
		const bool aux_ready = inject_payload
				       ? encodeManualPreparation(1300, request, 1, 1300, injected) && payload_ready.publish(injected)
				       : aux_endpoint.poll(1300, c, inventory) == PreparationStatus::Ready;
		check(aux_ready
		      && coordinator.poll(1400, c, inventory) == PreparationStatus::Ready
		      && coordinator.poll(1500, c, inventory) == PreparationStatus::Ready,
		      "both exact replies complete preparation and remain revalidated");
		check(!main.active() && !aux.active(), "coordinated readiness never grants activation");

		if (scenario == 0) {
			check(main_endpoint.activate(1600, c, inventory, binding, p, request.command_revision)
			      && aux_endpoint.activate(1600, c, inventory, binding, p, request.command_revision),
			      "independent trusted activation consumes both coordinated preparations");
			coordinator.cancel(1700);
			check(main.active() && aux.active() && coordinator.poll(1700, c, inventory) == PreparationStatus::Inactive,
			      "ending preparation does not revoke activated banks");
			continue;
		}

		auto candidate = c;
		auto live = inventory;
		uint64_t now = 1600;

		switch (scenario) {
		case 1: candidate.mass += .1F; break;

		case 2: ++live.channels[0].rate_hz; break;

		case 3: now = 20000; break;

		case 4: now = 1499; break;

		case 5: aux_endpoint.cancel(now); break;

		case 6: check(replacement_proposal.advertise(), "replace coordinator publisher"); break;

		case 7: check(replacement_ready.advertise(), "replace coordinated readiness publisher"); break;

		case 8: check(replacement_command.advertise(), "replace coordinated command publisher"); break;

		case 9: check(replacement_ack.advertise(), "replace coordinated handled-reply publisher"); break;

		default: {
				// Publish through the existing handle to test payload mutation separately
				// from lifetime replacement (publisher binding is not authentication).
				uORB::Subscription ready_sub{ORB_ID(slaaps_manual_ready_aux)};
				slaaps_manual_preparation_s wire{};
				check(ready_sub.copy(&wire), "capture ready payload for mutation");

				if (scenario == 10) { ++wire.source; }

				if (scenario == 11) { ++wire.prepared; ++wire.timestamp; }

				if (scenario == 12) { wire.bank = 0; }

				check(payload_ready.publish(wire), "same publisher mutates readiness payload");
				break;
			}
		}

		check(coordinator.poll(now, candidate, live) == PreparationStatus::Rejected,
		      "coordinator rejects changed readiness identity time routing or lifetime");
		check(coordinator.poll(now, c, inventory) == PreparationStatus::Inactive && !main.active() && !aux.active(),
		      "preparation rejection remains inactive without granting output");
	}
}

static void nativeInitialPreparationUnreadyTests()
{
	NativePermissionPublisher permission_publisher;
	auto c = fixture();
	c.edf_commission_max = .25F;
	c.activation_ack_timeout_us = 10000;
	c.first_command_budget_us = 30000;
	const auto snapshot = routingSnapshotFixture();
	const ManualOutputBinding binding{42, {51, 52}, 3};
	const ReportedRoutingPolicy policy{5000, 5000};

	for (unsigned scenario = 0; scenario < 5; ++scenario) {
		uORB::Publication<slaaps_manual_command_s> command{ORB_ID(slaaps_manual_command)};
		uORB::ResearchSubscription command_lifetime{ORB_ID::slaaps_manual_command};
		ManualBankTransport<0> main;
		ManualBankTransport<1> aux;
		ManualPreparationEndpoint<0> main_endpoint{main};
		ManualPreparationEndpoint<1> aux_endpoint{aux};
		ManualPreparationCoordinator coordinator;
		uORB::Publication<slaaps_manual_preparation_s> replacement{ORB_ID(slaaps_manual_ready_aux)};
		check(command.advertise() && main.advertise() && aux.advertise()
		      && main_endpoint.advertise() && aux_endpoint.advertise() && coordinator.advertise(),
		      "initial-unready regression advertises real endpoints before starting their listeners");
		auto p = permission(1000);
		ManualPreparationRequest request{1000, 20000, 1000, p.source, fingerprint(c), command_lifetime.revision(),
			{main.acknowledgmentRevision(), aux.acknowledgmentRevision()}, binding, permission_publisher.revision(), ControlMode::EdfBench};
		const unsigned revisions[2] {main_endpoint.revision(), aux_endpoint.revision()};
		check(coordinator.beginObserved(1000, c, snapshot, InventoryFault::None, policy, request, revisions),
		      "EDF coordinator captures baselines and publishes before either driver listens");
		check(main_endpoint.listen(1100, coordinator.revision())
		      && coordinator.pollObserved(1200, c, snapshot, InventoryFault::None) == PreparationStatus::Waiting,
		      "MAIN initialization tombstone after proposal remains non-actuating while its first reply is pending");
		check(aux_endpoint.listen(1300, coordinator.revision())
		      && coordinator.pollObserved(1400, c, snapshot, InventoryFault::None) == PreparationStatus::Waiting,
		      "AUX initialization tombstone also waits without accepting readiness or renewing the original request");

		if (scenario == 1) {
			auto fresh = snapshot;
			fresh.captured_us = fresh.reported_us[0] = fresh.reported_us[1] = 20000;
			check(coordinator.pollObserved(20000, c, fresh, InventoryFault::None) == PreparationStatus::Rejected,
			      "initial unready payloads cannot extend the original exclusive preparation deadline");

		} else if (scenario == 2) {
			check(replacement.advertise()
			      && coordinator.pollObserved(1500, c, snapshot, InventoryFault::None) == PreparationStatus::Rejected,
			      "readiness publisher replacement rejects even before its first positive reply");

		} else if (scenario == 3) {
			check(coordinator.pollObserved(6000, c, snapshot, InventoryFault::None) == PreparationStatus::Rejected,
			      "initial unready waiting retains the original routing-capture freshness bound");

		} else {
			check(main_endpoint.poll(1500, c, snapshot.inventory) == PreparationStatus::Ready
			      && coordinator.pollObserved(1600, c, snapshot, InventoryFault::None) == PreparationStatus::Waiting,
			      "one positive current-bank reply is remembered while the other bank is still unready");

			if (scenario == 0) {
				check(aux_endpoint.poll(1700, c, snapshot.inventory) == PreparationStatus::Ready
				      && coordinator.pollObserved(1800, c, snapshot, InventoryFault::None) == PreparationStatus::Ready,
				      "both later exact positive replies complete the unchanged EDF preparation");
			}

			main_endpoint.cancel(1900);
			check(coordinator.pollObserved(1950, c, snapshot, InventoryFault::None) == PreparationStatus::Rejected,
			      "a tombstone after any positive current-bank readiness remains immediate revocation");
		}

		check(!main.active() && !aux.active(), "unready observation and all readiness failures never activate either bank");
	}

	for (unsigned mutation = 0; mutation < 22; ++mutation) {
		uORB::Publication<slaaps_manual_command_s> command{ORB_ID(slaaps_manual_command)};
		uORB::ResearchSubscription command_lifetime{ORB_ID::slaaps_manual_command};
		ManualBankTransport<0> main;
		ManualBankTransport<1> aux;
		ManualPreparationEndpoint<0> main_endpoint{main};
		uORB::Publication<slaaps_manual_preparation_s> injected{ORB_ID(slaaps_manual_ready_aux)};
		uORB::ResearchSubscription injected_lifetime{ORB_ID::slaaps_manual_ready_aux};
		ManualPreparationCoordinator coordinator;
		check(command.advertise() && main.advertise() && aux.advertise() && main_endpoint.advertise()
		      && injected.advertise() && coordinator.advertise(), "canonical-unready mutation peers own explicit publisher lifetimes");
		auto p = permission(1000);
		ManualPreparationRequest request{1000, 20000, 1000, p.source, fingerprint(c), command_lifetime.revision(),
			{main.acknowledgmentRevision(), aux.acknowledgmentRevision()}, binding, permission_publisher.revision(), ControlMode::EdfBench};
		const unsigned revisions[2] {main_endpoint.revision(), injected_lifetime.revision()};
		check(coordinator.begin(1000, c, snapshot.inventory, request, revisions), "capture baseline before injected unready payload");
		slaaps_manual_preparation_s wire{};
		wire.timestamp = 1100;
		wire.bank = 1;

		switch (mutation) {
		case 0: break;

		case 1: wire.timestamp = 0; break;

		case 2: wire.timestamp = 999; break;

		case 3: wire.timestamp = 1201; break;

		case 4: wire.timestamp_sample = 1; break;

		case 5: wire.deadline = 1; break;

		case 6: wire.session = 1; break;

		case 7: wire.source = 1; break;

		case 8: wire.configuration = 1; break;

		case 9: wire.mode = static_cast<uint8_t>(ControlMode::EdfBench); break;

		case 10: wire.routing = 1; break;

		case 11: wire.driver[0] = 1; break;

		case 12: wire.driver[1] = 1; break;

		case 13: wire.prepared = 1; break;

		case 14: wire.command_revision = 1; break;

		case 15: wire.permission_revision = 1; break;

		case 16: wire.acknowledgment_revisions[0] = 1; break;

		case 17: wire.acknowledgment_revisions[1] = 1; break;

		case 18: wire.operation = slaaps_manual_preparation_s::ACTIVATE; break;

		case 19: wire.bank_mask = 1; break;

		case 20: wire.bank = 0; break;

		default: wire.bank = slaaps_manual_preparation_s::REQUEST_BANK; break;
		}

		check(injected.publish(wire)
		      && coordinator.poll(1200, c, snapshot.inventory) == (mutation ? PreparationStatus::Rejected : PreparationStatus::Waiting),
		      "only the canonical same-lifetime initial unready payload within this request interval may wait");
		check(!main.active() && !aux.active(), "malformed readiness cannot activate either bank");
	}
}

static void nativeActivationTransactionTests()
{
	NativePermissionPublisher permission_publisher;
	const auto c = fixture();
	const auto inventory = routingFixture();
	const ManualOutputBinding binding{42, {51, 52}, 3};

	for (unsigned scenario = 0; scenario < 6; ++scenario) {
		uORB::Publication<slaaps_manual_command_s> commands{ORB_ID(slaaps_manual_command)};
		uORB::ResearchSubscription lifetime{ORB_ID(slaaps_manual_command)};
		uORB::Publication<slaaps_manual_preparation_s> replacement{ORB_ID(slaaps_manual_transition)};
		ManualBankTransport<0> main;
		ManualBankTransport<1> aux;
		ManualPreparationEndpoint<0> main_endpoint{main};
		ManualPreparationEndpoint<1> aux_endpoint{aux};
		ManualPreparationCoordinator coordinator;
		check(commands.advertise() && main.advertise() && aux.advertise()
		      && main_endpoint.advertise() && aux_endpoint.advertise() && coordinator.advertise()
		      && coordinator.advertiseTransitions(), "transaction peers advertise");
		check(main_endpoint.listen(900, coordinator.revision()) && aux_endpoint.listen(900, coordinator.revision())
		      && main_endpoint.bindTransitions(coordinator.transitionRevision())
		      && aux_endpoint.bindTransitions(coordinator.transitionRevision()), "bind both transaction lifetimes");
		auto p = permission(1000);
		p.configuration = fingerprint(c);
		ManualPreparationRequest request{1000, 20000, 1000, p.source, p.configuration, lifetime.revision(),
			{main.acknowledgmentRevision(), aux.acknowledgmentRevision()}, binding, permission_publisher.revision()};
		const unsigned ready[2] {main_endpoint.revision(), aux_endpoint.revision()};
		const unsigned activated[2] {main_endpoint.activationRevision(), aux_endpoint.activationRevision()};
		check(coordinator.begin(1000, c, inventory, request, ready)
		      && main_endpoint.poll(1100, c, inventory) == PreparationStatus::Ready
		      && aux_endpoint.poll(1200, c, inventory) == PreparationStatus::Ready,
		      "both banks prepare before activation request");
		check(coordinator.beginActivation(1300, c, inventory, activated, 4000, 5000), "publish activation after all-bank readiness");
		check(main_endpoint.processTransition(1400, c, inventory, p) == PreparationStatus::Ready
		      && main.active() && coordinator.pollActivation(1500, c, inventory) == ActivationStatus::Waiting,
		      "first bank activation does not invalidate consumed readiness or complete transaction");

		if (scenario == 1) {
			coordinator.cancelActivation(1600);

		} else if (scenario == 2) {
			check(coordinator.pollActivation(4000, c, inventory) == ActivationStatus::Rejected, "silent second activation cancels all peers");

		} else if (scenario == 3) {
			check(replacement.advertise(), "replace transition publisher after partial activation");

		} else if (scenario == 5) {
			auto denied = p;
			denied.flight_admitted = false;
			check(aux_endpoint.processTransition(1600, c, inventory, denied) == PreparationStatus::Rejected && !aux.active(),
			      "activation request cannot replace independent flight admission");
			check(coordinator.pollActivation(1700, c, inventory) == ActivationStatus::Rejected,
			      "independent bank denial cancels transaction including activated peer");

		} else {
			check(aux_endpoint.processTransition(1600, c, inventory, p) == PreparationStatus::Ready
			      && coordinator.pollActivation(1700, c, inventory) == ActivationStatus::Ready,
			      "both exact activation replies complete transport barrier");
			uint64_t deadline{};
			check(coordinator.consumeActivation(1800, deadline) && deadline > 1800, "consume earliest bank first-command deadline");

			if (scenario == 4) {
				check(main_endpoint.processTransition(21000, c, inventory, p) == PreparationStatus::Ready,
				      "retained activation never becomes expired proposal after consumption");
				check(coordinator.pollActivation(21000, c, inventory) == ActivationStatus::Consumed,
				      "consumed activation deadline does not become an ongoing output lease");
			}

			coordinator.cancelActivation(22000);
		}

		check(main_endpoint.processTransition(23000, c, inventory, p) == PreparationStatus::Rejected
		      && aux_endpoint.processTransition(23000, c, inventory, p) == PreparationStatus::Rejected
		      && !main.active() && !aux.active(), "cancel or replaced publisher revokes every peer including partial activation");
		check(main_endpoint.processTransition(23001, c, inventory, p) == PreparationStatus::Rejected && !main.active(),
		      "cancelled transition cannot replay activation");
	}
}

template<unsigned Bank>
static void nativeDeferredTransitionTests()
{
	NativePermissionPublisher permission_publisher;
	const auto c = fixture();
	const auto inventory = routingFixture();
	const ManualOutputBinding binding{42, {51, 52}, 3};

	for (unsigned scenario = 0; scenario < 7; ++scenario) {
		uORB::Publication<slaaps_manual_command_s> command{ORB_ID(slaaps_manual_command)};
		uORB::ResearchSubscription command_lifetime{ORB_ID(slaaps_manual_command)};
		uORB::Publication<slaaps_manual_preparation_s> proposal{ORB_ID(slaaps_manual_prepare)};
		uORB::ResearchSubscription proposal_lifetime{ORB_ID(slaaps_manual_prepare)};
		uORB::Publication<slaaps_manual_preparation_s> transition{ORB_ID(slaaps_manual_transition)};
		uORB::ResearchSubscription transition_lifetime{ORB_ID(slaaps_manual_transition)};
		uORB::Publication<slaaps_manual_preparation_s> replacement{ORB_ID(slaaps_manual_transition)};
		ManualBankTransport<Bank> bank;
		ManualPreparationEndpoint<Bank> endpoint{bank};
		check(command.advertise() && proposal.advertise() && transition.advertise() && bank.advertise()
		      && endpoint.advertise() && endpoint.listen(900, proposal_lifetime.revision())
		      && endpoint.bindTransitions(transition_lifetime.revision()), "deferred transaction binds existing real endpoint lifetimes");
		auto p = permission(1000);
		p.configuration = fingerprint(c);
		ManualPreparationRequest request{1000, 20000, 1000, p.source, p.configuration, command_lifetime.revision(),
			{88, 99}, binding, permission_publisher.revision()};
		request.acknowledgment_revisions[Bank] = bank.acknowledgmentRevision();
		slaaps_manual_preparation_s wire{};
		check(encodeManualPreparation(1000, request, slaaps_manual_preparation_s::REQUEST_BANK, 0, wire)
		      && proposal.publish(wire) && endpoint.poll(1100, c, inventory) == PreparationStatus::Ready,
		      "deferred transaction starts from an actual non-actuating prepared endpoint");
		check(encodeManualPreparation(1200, request, slaaps_manual_preparation_s::REQUEST_BANK, 0, wire),
		      "encode exact current-session activation proposal");
		wire.operation = slaaps_manual_preparation_s::ACTIVATE;
		const auto original = wire;
		check(transition.publish(wire)
		      && endpoint.processTransition(1300, c, inventory, p, false) == PreparationStatus::Waiting
		      && endpoint.processTransition(1400, c, inventory, p, false) == PreparationStatus::Waiting && !bank.active(),
		      "ACTIVATE remains exact and unconsumed while first permission is pending");

		if (scenario == 0) {
			check(endpoint.processTransition(1500, c, inventory, p) == PreparationStatus::Ready && bank.active(),
			      "same deferred activation may consume preparation once fresh independent permission arrives");
			continue;
		}

		uint64_t now = 1500;

		switch (scenario) {
		case 1: ++wire.timestamp; break;

		case 2: {
				auto older = request;
				older.timestamp_us = older.session = 999;
				check(encodeManualPreparation(1200, older, slaaps_manual_preparation_s::REQUEST_BANK, 0, wire),
				      "encode structurally valid prior-session transition on unchanged publisher");
				wire.operation = slaaps_manual_preparation_s::ACTIVATE;
				break;
			}

		case 3: wire.operation = slaaps_manual_preparation_s::CANCEL; wire.timestamp = now; break;

		case 4: check(replacement.advertise(), "replace publisher after retaining deferred current ACTIVATE"); break;

		case 5: now = request.deadline_us; break;

		case 6: ++wire.source; break;
		}

		check(transition.publish(wire)
		      && endpoint.processTransition(now, c, inventory, p, false) == PreparationStatus::Rejected && !bank.active(),
		      "deferred activation rejects timestamp mutation older-session substitution cancel replacement expiry and identity mutation");
		check(transition.publish(original)
		      && endpoint.processTransition(now + 1, c, inventory, p) == PreparationStatus::Rejected && !bank.active(),
		      "restoring exact activation and fresh permission cannot revive the rejected prepared endpoint");
	}
}

static void nativeObservedCoordinatorTests()
{
	NativePermissionPublisher permission_publisher;
	const auto c = fixture();
	const auto snapshot = routingSnapshotFixture();
	const ManualOutputBinding binding{42, {51, 52}, 3};
	const ReportedRoutingPolicy policy{1000, 2000};

	for (unsigned scenario = 0; scenario < 5; ++scenario) {
		uORB::Publication<slaaps_manual_command_s> command{ORB_ID::slaaps_manual_command};
		uORB::ResearchSubscription command_lifetime{ORB_ID::slaaps_manual_command};
		ManualBankTransport<0> main;
		ManualBankTransport<1> aux;
		ManualPreparationEndpoint<0> main_endpoint{main};
		ManualPreparationEndpoint<1> aux_endpoint{aux};
		ManualPreparationCoordinator coordinator;
		check(command.advertise() && main.advertise() && aux.advertise() && main_endpoint.advertise()
		      && aux_endpoint.advertise() && coordinator.advertise(), "observed coordinator peers advertise");
		check(main_endpoint.listen(900, coordinator.revision()) && aux_endpoint.listen(900, coordinator.revision()),
		      "observed coordinator banks bind proposal publisher");
		auto p = permission(1000);
		p.configuration = fingerprint(c);
		ManualPreparationRequest request{1000, 20000, 1000, p.source, p.configuration, command_lifetime.revision(),
			{main.acknowledgmentRevision(), aux.acknowledgmentRevision()}, binding, permission_publisher.revision()};
		const unsigned revisions[2] {main_endpoint.revision(), aux_endpoint.revision()};

		if (scenario == 3) {
			check(!coordinator.beginObserved(1000, c, snapshot, InventoryFault::Stale, policy, request, revisions),
			      "failed capture rejects observed coordinator entry");
			check(!coordinator.beginObserved(1000, c, snapshot, InventoryFault::None, policy, request, revisions),
			      "failed observed entry consumes session token even after capture recovers");
			continue;
		}

		check(coordinator.beginObserved(1000, c, snapshot, InventoryFault::None, policy, request, revisions)
		      && coordinator.pollObserved(1100, c, snapshot, InventoryFault::None) == PreparationStatus::Waiting,
		      "observed preparation binds routing before publication and waits for banks");
		check(main_endpoint.poll(1200, c, snapshot.inventory) == PreparationStatus::Ready
		      && aux_endpoint.poll(1200, c, snapshot.inventory) == PreparationStatus::Ready
		      && coordinator.pollObserved(1300, c, snapshot, InventoryFault::None) == PreparationStatus::Ready,
		      "both bank replies plus current driver observations satisfy readiness");
		auto changed = snapshot;
		auto status = InventoryFault::None;

		if (scenario == 1) { status = InventoryFault::MissingBank; }

		if (scenario == 2) { ++changed.publisher_revision[1]; }

		if (scenario == 4) { changed.captured_us = 1501; }

		const auto result = scenario == 0 ? coordinator.poll(1500, c, snapshot.inventory)
				    : coordinator.pollObserved(1500, c, changed, status);
		check(result == PreparationStatus::Rejected && !main.active() && !aux.active(),
		      "observation failure replacement or raw-poll bypass cancels preparation without authority");
		check(coordinator.pollObserved(1500, c, snapshot, InventoryFault::None) == PreparationStatus::Inactive,
		      "restored observed data cannot revive rejected preparation");
	}
}

static void nativeActivationCompletionTests()
{
	NativePermissionPublisher permission_publisher;
	const auto c = fixture();
	const auto inventory = routingFixture();
	const ManualOutputBinding binding{42, {51, 52}, 3};

	for (unsigned scenario = 0; scenario < 3; ++scenario) {
		uORB::Publication<slaaps_manual_command_s> commands{ORB_ID::slaaps_manual_command};
		uORB::ResearchSubscription command_lifetime{ORB_ID::slaaps_manual_command};
		ManualBankTransport<0> main;
		ManualBankTransport<1> aux;
		check(commands.advertise() && main.advertise() && aux.advertise(), "activation confirmation transports advertise");
		auto p = permission(1000);
		p.configuration = fingerprint(c);
		const ManualPreparationRequest request{1000, 20000, 1000, p.source, p.configuration, command_lifetime.revision(),
			{main.acknowledgmentRevision(), aux.acknowledgmentRevision()}, binding, permission_publisher.revision()};
		ManualActivationBarrier barrier;
		check(main.prepare(1000, c, inventory, request) && aux.prepare(1000, c, inventory, request)
		      && barrier.begin(2000, c, request, 6000, 5000), "both banks prepare before activation completion starts");
		check(main.beginPrepared(2100, c, inventory, binding, p, request.command_revision)
		      && aux.beginPrepared(2200, c, inventory, binding, p, request.command_revision),
		      "independently permitted banks activate without command output");
		ManualActivationAck main_ack{}, aux_ack{};
		check(main.activationAck(3000, p, inventory, binding, main_ack)
		      && main_ack.command_revision == request.command_revision
		      && main_ack.acknowledgment_revision == request.acknowledgment_revisions[0]
		      && barrier.accept(3000, 0, main_ack) == ActivationStatus::Waiting,
		      "native bank activation identity is bound to actual command and handled-reply lifetimes");

		if (scenario == 1) {
			check(barrier.status(6000) == ActivationStatus::Rejected && barrier.requiredBanks() == 3,
			      "lost activation reply requires cancellation of both banks including unacknowledged peer");
			main.revoke();
			aux.revoke();
			check(!main.active() && !aux.active() && main.dispatch(6000, p, inventory, binding).disposition != EdfDisposition::Command
			      && aux.dispatch(6000, p, inventory, binding).disposition != EdfDisposition::Command,
			      "caller cancellation removes partial activation without selecting a physical recovery action");
			continue;
		}

		if (scenario == 2) { p.healthy = false; }

		if (scenario == 2) {
			check(!aux.activationAck(4000, p, inventory, binding, aux_ack), "permission loss prevents native activation confirmation");
			barrier.cancel();
			main.revoke();
			aux.revoke();
			check(barrier.status(4000) == ActivationStatus::Rejected && !main.active() && !aux.active(),
			      "permission failure aborts activation completion and both banks");
			continue;
		}

		check(aux.activationAck(4000, p, inventory, binding, aux_ack)
		      && barrier.accept(4000, 1, aux_ack) == ActivationStatus::Ready, "both native activation confirmations satisfy completion");
		uint64_t deadline{};
		check(barrier.consume(4000, deadline) && deadline == 14100, "native completion preserves earliest bank command expiry");
		auto frame = transportFrame(8000);
		frame.sequence = frame.edf.sequence = 1;
		frame.edf.configuration = p.configuration;
		frame.edf.normalized = 0.F;
		slaaps_manual_command_s wire{};
		check(encodeManualCommand(8000, frame, wire) && commands.publish(wire), "first command arrives within explicit handoff allowance");
		const auto main_output = main.dispatch(8000, p, inventory, binding);
		const auto aux_output = aux.dispatch(8000, p, inventory, binding);
		check(main_output.disposition == EdfDisposition::Command && aux_output.disposition == EdfDisposition::Command
		      && main.confirmHandled(8000, p, inventory, binding, main_output, true)
		      && aux.confirmHandled(8000, p, inventory, binding, aux_output, true), "completed activation proceeds to exact-ticket bank handling");
		check(!main.activationAck(8000, p, inventory, binding, main_ack)
		      && !aux.activationAck(8000, p, inventory, binding, aux_ack), "command handling is distinct from initial activation confirmation");
	}
}

static void nativeWorkerTransportTests()
{
	const auto c = fixture();
	const auto inventory = routingFixture();
	const ManualOutputBinding binding{42, {51, 52}, 3};

	for (unsigned scenario = 0; scenario < 5; ++scenario) {
		auto p = permission(1000);
		p.configuration = fingerprint(c);
		research_stabilized_manual::ManualSample manual{};
		manual.valid = true;
		manual.source = research_actuator_command::ManualControlSource::Mavlink;
		manual.mavlink_instance = c.manual_instance;
		manual.source_system_id = c.manual_system;
		manual.source_component_id = c.manual_component;
		manual.axes[3] = -1.F;
		research_stabilized_manual::StateSample state{};
		state.valid = true;
		state.source_epoch = 1;
		auto stamp = [&](uint64_t now) {
			manual.timestamp_us = manual.timestamp_sample_us = now;
			state.attitude_timestamp_us = state.attitude_sample_us = state.rate_timestamp_us = state.rate_sample_us = now;
		};
		stamp(1000);
		EdfOwner owner;
		ManualWorkerTransport worker(owner);
		check(worker.handledSequence() == 0, "inactive worker has no handled sequence");
		ManualBankTransport<0> main;
		ManualBankTransport<1> aux;
		uORB::ResearchSubscription commands{ORB_ID(slaaps_manual_command)};
		uORB::ResearchSubscription main_reply{ORB_ID(slaaps_manual_ack_main)};
		uORB::ResearchSubscription aux_reply{ORB_ID(slaaps_manual_ack_aux)};
		check(worker.advertise() && main.advertise() && aux.advertise(), "closed-loop native endpoint advertisements");
		const unsigned revisions[2] {main_reply.revision(), aux_reply.revision()};
		check(worker.begin(1000, c, p, manual, state, binding, commands.revision(), revisions) == ManualWorkerStatus::Ready
		      && main.begin(1000, c, inventory, binding, p, commands.revision())
		      && aux.begin(1000, c, inventory, binding, p, commands.revision()), "closed-loop explicit transport and controller grants");
		stamp(5000);
		manual.axes[0] = .5F;
		check(worker.cycle(5000, p, manual, state, binding) == ManualWorkerStatus::Command, "real controller publishes first combined command");
		slaaps_manual_command_s wire{};
		unsigned revision{};
		check(commands.copyBound(&wire, revision) && wire.sequence == 1 && wire.timestamp_sample == 5000,
		      "actual native worker frame retains calculation identity");
		const auto main_output = main.dispatch(5000, p, inventory, binding);
		const auto aux_output = aux.dispatch(5000, p, inventory, binding);
		check(main_output.disposition == EdfDisposition::Command && aux_output.disposition == EdfDisposition::Command,
		      "actual worker frame reaches both native consumers");

		if (scenario == 0) {
			check(main.confirmHandled(5000, p, inventory, binding, main_output, true)
			      && worker.cycle(5000, p, manual, state, binding) == ManualWorkerStatus::Waiting
			      && worker.handledSequence() == 0, "MAIN alone cannot advance handled sequence");
			check(aux.confirmHandled(5000, p, inventory, binding, aux_output, true)
			      && worker.cycle(5000, p, manual, state, binding) == ManualWorkerStatus::NotDue
			      && worker.handledSequence() == 1, "both native replies complete exact first handled sequence");
			check(worker.cycle(5001, p, manual, state, binding) == ManualWorkerStatus::NotDue,
			      "held replies do not replay into controller");
			stamp(9000);
			manual.axes[0] = 0.F;
			manual.axes[3] = 1.F;
			check(worker.cycle(9000, p, manual, state, binding) == ManualWorkerStatus::Command
			      && commands.copyBound(&wire, revision) && wire.sequence == 2, "second rate/hold and propulsion frame crosses native transport");
			const auto second_main = main.dispatch(9000, p, inventory, binding);
			const auto second_aux = aux.dispatch(9000, p, inventory, binding);
			check(main.confirmHandled(9000, p, inventory, binding, second_main, true)
			      && aux.confirmHandled(9000, p, inventory, binding, second_aux, true)
			      && worker.cycle(9000, p, manual, state, binding) == ManualWorkerStatus::NotDue, "simultaneous bank replies drain once");
			stamp(13000);
			check(worker.cycle(13000, p, manual, state, binding) == ManualWorkerStatus::Command
			      && commands.copyBound(&wire, revision) && wire.sequence == 3 && wire.edf > 0.F,
			      "EDF slew produces nonzero propulsion after held-stick release");
			worker.stop();
			check(worker.handledSequence() == 0, "stopped transport exposes no prior handled sequence");
			p = permission(30000);
			p.session = 30000;
			p.configuration = fingerprint(c);
			stamp(30000);
			manual.axes[3] = -1.F;
			check(worker.begin(30000, c, p, manual, state, binding, commands.revision(), revisions) == ManualWorkerStatus::Ready
			      && main.begin(30000, c, inventory, binding, p, commands.revision())
			      && aux.begin(30000, c, inventory, binding, p, commands.revision()), "same-publisher new session ignores expired prior traffic at entry");
			check(main.dispatch(30000, p, inventory, binding).disposition == EdfDisposition::RecoveryRequired
			      && main.fault() == ManualBankFault::None, "retained old command cannot drive newly admitted bank");
			stamp(34000);
			check(worker.cycle(34000, p, manual, state, binding) == ManualWorkerStatus::Command
			      && commands.copyBound(&wire, revision) && wire.session == 30000 && wire.sequence == 1,
			      "new session starts a fresh sequence without replaying old replies");
			const auto reentry_main = main.dispatch(34000, p, inventory, binding);
			const auto reentry_aux = aux.dispatch(34000, p, inventory, binding);
			check(main.confirmHandled(34000, p, inventory, binding, reentry_main, true)
			      && aux.confirmHandled(34000, p, inventory, binding, reentry_aux, true)
			      && worker.cycle(34000, p, manual, state, binding) == ManualWorkerStatus::NotDue, "reentered transport completes handling handshake");
			worker.stop();
			check(worker.cycle(34000, p, manual, state, binding) == ManualWorkerStatus::Inactive,
			      "explicit native stop prevents further publication");
			continue;
		}

		if (scenario == 1) {
			uORB::Publication<slaaps_manual_command_s> replacement{ORB_ID(slaaps_manual_command)};
			check(replacement.publish(wire) && worker.cycle(5000, p, manual, state, binding) == ManualWorkerStatus::OutputRejected,
			      "producer command lifetime replacement revokes worker");

		} else if (scenario == 2) {
			uORB::Publication<slaaps_manual_ack_s> replacement{ORB_ID(slaaps_manual_ack_aux)};
			check(replacement.advertise() && worker.cycle(5000, p, manual, state, binding) == ManualWorkerStatus::OutputRejected,
			      "reply publisher replacement revokes worker before publication");

		} else {
			check(main.confirmHandled(5000, p, inventory, binding, main_output, true), "one bank handling succeeds before delay");
			stamp(17000);

			if (scenario == 3) { check(!aux.confirmHandled(17000, p, inventory, binding, aux_output, true), "late bank handling cannot acknowledge"); }

			check(worker.cycle(17000, p, manual, state, binding) == ManualWorkerStatus::OutputRejected,
			      "missing or late bank reply expires worker independently");
		}

		check(owner.sample(scenario >= 3 ? 17000 : 5000, p).disposition == EdfDisposition::RecoveryRequired,
		      "transport loss revokes shared propulsion owner");
	}

	EdfOwner owner;
	auto p = permission(1000);
	p.configuration = fingerprint(c);
	check(owner.begin(1000, c, p) == EdfFault::None && owner.accept(1000, p, request(p, 1000, 1, 0.F)) == EdfFault::None,
	      "shared owner command before endpoint destruction");
	{
		ManualWorkerTransport worker(owner);
	}
	check(owner.sample(1000, p).disposition == EdfDisposition::Command,
	      "unadmitted endpoint destruction does not revoke another owner");
	EdfOwner active_owner;
	ManualBankTransport<0> main;
	ManualBankTransport<1> aux;
	research_stabilized_manual::ManualSample manual{};
	manual.valid = true;
	manual.source = research_actuator_command::ManualControlSource::Mavlink;
	manual.mavlink_instance = c.manual_instance;
	manual.source_system_id = c.manual_system;
	manual.source_component_id = c.manual_component;
	manual.axes[3] = -1.F;
	manual.timestamp_us = manual.timestamp_sample_us = 1000;
	research_stabilized_manual::StateSample state{};
	state.valid = true;
	state.source_epoch = 1;
	state.attitude_timestamp_us = state.attitude_sample_us = state.rate_timestamp_us = state.rate_sample_us = 1000;
	{
		ManualWorkerTransport worker(active_owner);
		check(worker.advertise() && main.advertise() && aux.advertise(), "destruction fixture endpoint advertisements");
		uORB::ResearchSubscription commands{ORB_ID(slaaps_manual_command)};
		uORB::ResearchSubscription main_reply{ORB_ID(slaaps_manual_ack_main)};
		uORB::ResearchSubscription aux_reply{ORB_ID(slaaps_manual_ack_aux)};
		const unsigned revisions[2] {main_reply.revision(), aux_reply.revision()};
		check(worker.begin(1000, c, p, manual, state, binding, commands.revision(), revisions) == ManualWorkerStatus::Ready,
		      "admitted endpoint owns propulsion before destruction");
	}
	check(active_owner.sample(1000, p).disposition == EdfDisposition::RecoveryRequired,
	      "active endpoint destruction revokes shared propulsion ownership");
}

static void nativeLiveReadTimingTests()
{
	auto c = fixture();
	c.cycle_max_us = 200000;
	c.activation_ack_timeout_us = 40000;
	c.first_command_budget_us = 30000;
	const auto inventory = routingFixture();
	const ManualOutputBinding binding{42, {51, 52}, 3};

	for (unsigned scenario = 0; scenario < 3; ++scenario) {
		NativePermissionPublisher publisher;
		ManualPermissionSubscription reader;
		const auto entry = hrt_absolute_time() - (scenario == 2 ? 200000 : 1000);
		auto p = permission(entry);
		p.session = entry;
		p.configuration = fingerprint(c);
		ManualPreparationRequest request{entry, entry + 500000, entry, p.source, p.configuration, 1, {2, 3}, binding, publisher.revision()};
		uint64_t now = hrt_absolute_time();
		check(reader.beginLive(now, request, 100000), "live permission reader binds the original prepared transaction");
		const auto published = scenario == 2 ? entry : hrt_absolute_time();
		p.timestamp_us = published;
		p.deadline_us = published + 100000;
		slaaps_manual_permission_s wire{};
		check(encodeManualPermission(published, {p, 1}, 100000, wire) && publisher.publication.publish(wire),
		      "publish exact current or already-expired permission on the bound lifetime");
		now = published - 1;
		EdfPermission observed{};

		if (scenario == 0) {
			check(reader.read(now, observed) == BoundRead::Rejected,
			      "fixed-time permission read rejects a packet newer than the retained decision time");

		} else if (scenario == 1) {
			check(reader.readLive(now, observed) == BoundRead::Data && now >= published
			      && observed.timestamp_us == p.timestamp_us && observed.deadline_us == p.deadline_us,
			      "live permission read uses postcopy HRT without changing decision time or lease");
			check(reader.readLive(now, observed) == BoundRead::Data && observed.deadline_us == p.deadline_us,
			      "retained live permission cannot renew its original lease");

		} else {
			observed.source = 99;
			check(reader.readLive(now, observed) == BoundRead::Rejected && observed.source == 99,
			      "live permission copying cannot revive an expired original lease or replace output on rejection");
		}
	}

	for (unsigned scenario = 0; scenario < 3; ++scenario) {
		uORB::Publication<slaaps_manual_command_s> commands{ORB_ID(slaaps_manual_command)};
		uORB::ResearchSubscription command_lifetime{ORB_ID(slaaps_manual_command)};
		ManualBankTransport<0> bank;
		check(commands.advertise() && bank.advertise(), "live command fixture owns exact producer and handled-reply lifetimes");
		const auto calculated = hrt_absolute_time() - (scenario == 2 ? 60000 : 0);
		const auto entry = calculated - 1000;
		auto p = permission(entry);
		p.session = entry;
		p.configuration = fingerprint(c);
		check(bank.begin(entry, c, inventory, binding, p, command_lifetime.revision()),
		      "command consumer enters before the synthetic calculated frame");
		ManualWorkerOutput frame{};
		frame.timestamp_us = calculated;
		frame.deadline_us = calculated + 50000;
		frame.sequence = 1;
		frame.binding = binding;
		frame.edf.disposition = EdfDisposition::Command;
		frame.edf.session = entry;
		frame.edf.source = p.source;
		frame.edf.configuration = p.configuration;
		frame.edf.sequence = 1;
		frame.edf.request_timestamp_us = calculated;
		frame.edf.deadline_us = calculated + 100000;
		frame.edf.normalized = 0.F;

		for (unsigned i = 0; i < kCanards; ++i) { frame.canards[i] = 0.F; }

		slaaps_manual_command_s wire{};
		check(encodeManualCommand(calculated, frame, wire) && commands.publish(wire),
		      "publish exact current or expired frame without altering its original clocks");
		uint64_t now = calculated - 1;

		if (scenario == 0) {
			check(bank.dispatch(now, p, inventory, binding).disposition == EdfDisposition::RecoveryRequired,
			      "fixed-time command dispatch rejects a frame newer than the supplied decision time");

		} else if (scenario == 1) {
			const auto output = bank.dispatchLive(now, p, inventory, binding);
			check(output.disposition == EdfDisposition::Command && now >= wire.timestamp
			      && output.command_timestamp_us == calculated && output.deadline_us == frame.deadline_us,
			      "live command copying accepts a current packet with exact original calculation and output expiry");
			check(bank.confirmHandledLive(now, p, inventory, binding, output, true),
			      "live exact-ticket handling publishes an acknowledgment only after current validation");
			const auto retained = bank.dispatchLive(now, p, inventory, binding);
			check(!bank.awaitingHandling() && retained.deadline_us == frame.deadline_us,
			      "retained live command neither requests another write nor extends expiry");
			check(bank.dispatch(frame.deadline_us, p, inventory, binding).disposition == EdfDisposition::RecoveryRequired,
			      "successful live handling leaves the original output deadline authoritative");

		} else {
			check(bank.dispatchLive(now, p, inventory, binding).disposition == EdfDisposition::RecoveryRequired,
			      "live command copying cannot revive a frame already past its original expiry");
		}
	}
}

static void nativeLiveHandshakeTimingTests()
{
	auto c = fixture();
	c.cycle_max_us = 200000;
	c.activation_ack_timeout_us = 40000;
	c.first_command_budget_us = 30000;
	const ManualOutputBinding binding{42, {51, 52}, 3};
	const ReportedRoutingPolicy policy{500000, 500000};

	for (unsigned scenario = 0; scenario < 2; ++scenario) {
		NativePermissionPublisher permission_publisher;
		uORB::Publication<slaaps_manual_command_s> commands{ORB_ID(slaaps_manual_command)};
		uORB::ResearchSubscription command_lifetime{ORB_ID(slaaps_manual_command)};
		ManualBankTransport<0> main;
		ManualBankTransport<1> aux;
		ManualPreparationEndpoint<0> main_endpoint{main};
		ManualPreparationEndpoint<1> aux_endpoint{aux};
		ManualPreparationCoordinator coordinator;
		check(commands.advertise() && main.advertise() && aux.advertise() && main_endpoint.advertise()
		      && aux_endpoint.advertise() && coordinator.advertise() && coordinator.advertiseTransitions(),
		      "live handshake owns every original preparation and activation endpoint");
		const auto listening = hrt_absolute_time();
		check(main_endpoint.listen(listening, coordinator.revision()) && aux_endpoint.listen(listening, coordinator.revision())
		      && main_endpoint.bindTransitions(coordinator.transitionRevision()) && aux_endpoint.bindTransitions(coordinator.transitionRevision()),
		      "real bank endpoints bind both supervisor transaction lifetimes");
		const auto entry = hrt_absolute_time();
		auto p = permission(entry);
		p.session = entry;
		p.configuration = fingerprint(c);
		auto snapshot = routingSnapshotFixture();
		snapshot.captured_us = snapshot.reported_us[0] = snapshot.reported_us[1] = entry;
		ManualPreparationRequest request{entry, entry + 500000, entry, p.source, p.configuration, command_lifetime.revision(),
			{main.acknowledgmentRevision(), aux.acknowledgmentRevision()}, binding, permission_publisher.revision()};
		const unsigned ready_revisions[2] {main_endpoint.revision(), aux_endpoint.revision()};
		check(coordinator.beginObserved(entry, c, snapshot, InventoryFault::None, policy, request, ready_revisions),
		      "real coordinator publishes an unchanged observed preparation request");
		uint64_t now = entry - 1;

		if (scenario == 0) {
			check(main_endpoint.poll(now, c, snapshot.inventory) == PreparationStatus::Rejected,
			      "fixed-time endpoint rejects a proposal newer than the supplied decision time");
			continue;
		}

		check(main_endpoint.pollLive(now, c, snapshot.inventory) == PreparationStatus::Ready && now >= entry
		      && aux_endpoint.pollLive(now, c, snapshot.inventory) == PreparationStatus::Ready,
		      "both live endpoints prepare after actual proposal copies using current HRT");
		check(coordinator.pollObservedLive(now, c, snapshot, InventoryFault::None) == PreparationStatus::Ready,
		      "live coordinator consumes both exact current readiness replies");
		const unsigned activation_revisions[2] {main_endpoint.activationRevision(), aux_endpoint.activationRevision()};
		const auto activation_deadline = now + 40000;
		check(coordinator.beginActivationObservedLive(now, c, snapshot, InventoryFault::None, activation_revisions,
				activation_deadline, 30000), "live activation start preserves its caller-supplied absolute deadline");
		check(main_endpoint.processTransitionLive(now, c, snapshot.inventory, p) == PreparationStatus::Ready
		      && aux_endpoint.processTransitionLive(now, c, snapshot.inventory, p) == PreparationStatus::Ready,
		      "both live transition reads activate exact prepared banks with independent permission");
		check(coordinator.pollActivationObservedLive(now, c, snapshot, InventoryFault::None) == ActivationStatus::Ready,
		      "live coordinator validates actual activation replies after both copies");
		uint64_t first_deadline{};
		check(coordinator.consumeActivation(now, first_deadline) && first_deadline == p.deadline_us
		      && request.deadline_us == entry + 500000,
		      "live handshake consumes the original earliest bank deadline without renewing preparation");
		slaaps_manual_command_s wire{};
		unsigned revision{};
		check(!command_lifetime.copyBound(&wire, revision), "live handshake and permission alone publish no actuator command");
		coordinator.cancelActivation(hrt_absolute_time());
		check(main_endpoint.processTransitionLive(now, c, snapshot.inventory, p) == PreparationStatus::Rejected
		      && aux_endpoint.processTransitionLive(now, c, snapshot.inventory, p) == PreparationStatus::Rejected
		      && !main.active() && !aux.active(), "live cancellation still revokes both activated peers");
	}
}

static void nativeObservedWorkerTimingTests()
{
	auto c = fixture();
	c.cycle_max_us = 200000;
	c.state_age_ms = 100;
	c.activation_ack_timeout_us = 40000;
	c.first_command_budget_us = 30000;
	const auto inventory = routingFixture();
	const ManualOutputBinding binding{42, {51, 52}, 3};
	const SelectedInputPolicy policy{100000, 100000};

	for (unsigned scenario = 0; scenario < 4; ++scenario) {
		EdfOwner owner;
		ManualWorkerTransport worker(owner);
		ManualBankTransport<0> main;
		ManualBankTransport<1> aux;
		uORB::ResearchSubscription commands{ORB_ID(slaaps_manual_command)};
		uORB::ResearchSubscription main_reply{ORB_ID(slaaps_manual_ack_main)}, aux_reply{ORB_ID(slaaps_manual_ack_aux)};
		check(worker.advertise() && main.advertise() && aux.advertise(), "observed-clock worker and genuine bank reply endpoints advertise");
		const unsigned revisions[2] {main_reply.revision(), aux_reply.revision()};
		SelectedManualInput::Snapshot snapshot{};
		snapshot.manual.valid = true;
		snapshot.manual.source = research_actuator_command::ManualControlSource::Mavlink;
		snapshot.manual.mavlink_instance = c.manual_instance;
		snapshot.manual.source_system_id = c.manual_system;
		snapshot.manual.source_component_id = c.manual_component;
		snapshot.manual.axes[3] = -1.F;
		snapshot.state.valid = true;
		snapshot.state.source_epoch = 1;
		snapshot.gyro = 123;
		snapshot.accel = 456;
		snapshot.permission_fresh = true;
		snapshot.permission.armed = true;

		for (auto &revision : snapshot.revisions) { revision = 1; }

		auto stamp = [&](uint64_t now) {
			snapshot.now = snapshot.estimator_time = snapshot.flags_time = snapshot.permission.timestamp = now;
			snapshot.manual.timestamp_us = snapshot.manual.timestamp_sample_us = now;
			snapshot.state.attitude_timestamp_us = snapshot.state.attitude_sample_us = now;
			snapshot.state.rate_timestamp_us = snapshot.state.rate_sample_us = now;
		};
		const uint64_t entry = hrt_absolute_time();
		auto p = permission(entry);
		p.session = entry;
		p.configuration = fingerprint(c);
		stamp(entry);
		check(worker.begin(entry, c, p, snapshot, policy, binding, commands.revision(), revisions) == ManualWorkerStatus::Ready
		      && main.begin(hrt_absolute_time(), c, inventory, binding, p, commands.revision())
		      && aux.begin(hrt_absolute_time(), c, inventory, binding, p, commands.revision()),
		      "real-clock selected worker and both bank consumers enter the same deliberate session");
		ManualActivationAck main_activation{}, aux_activation{};
		check(main.activationAck(hrt_absolute_time(), p, inventory, binding, main_activation)
		      && aux.activationAck(hrt_absolute_time(), p, inventory, binding, aux_activation),
		      "real banks retain their original first-command deadlines");
		const auto first_deadline = std::min(main_activation.first_command_deadline_us, aux_activation.first_command_deadline_us);
		px4_usleep(c.cycle_min_us);
		const auto calculated = hrt_absolute_time();
		stamp(calculated);
		p.timestamp_us = calculated;
		p.deadline_us = calculated + static_cast<uint64_t>(c.output_lease_ms) * 1000;
		check(worker.cycle(calculated, p, snapshot, binding) == ManualWorkerStatus::Command,
		      "real-clock selected worker publishes its first command with original raw expiry");
		slaaps_manual_command_s first{}, after{};
		unsigned revision{};
		check(commands.copyBound(&first, revision) && first.sequence == 1 && first.timestamp_sample == calculated
		      && first.edf_request_timestamp == calculated && first.edf_deadline == p.deadline_us,
		      "first combined wire preserves actual calculation and raw sample clocks");
		const auto main_output = main.dispatch(hrt_absolute_time(), p, inventory, binding);
		const auto aux_output = aux.dispatch(hrt_absolute_time(), p, inventory, binding);
		check(main_output.disposition == EdfDisposition::Command && aux_output.disposition == EdfDisposition::Command,
		      "both genuine bank consumers retain the exact first handling tickets");
		const auto earlier_decision = hrt_absolute_time();
		px4_usleep(1000);
		check(main.confirmHandled(hrt_absolute_time(), p, inventory, binding, main_output, true),
		      "MAIN genuinely handles after the earlier supervisor decision");
		const auto aux_handled = hrt_absolute_time() + (scenario == 3 ? 50000 : 0);
		check(aux.confirmHandled(aux_handled, p, inventory, binding, aux_output, true),
		      "AUX publishes the exact handled ticket on its retained publisher");
		slaaps_manual_ack_s main_wire{}, aux_wire{};
		check(main_reply.copyBound(&main_wire, revision) && aux_reply.copyBound(&aux_wire, revision)
		      && main_wire.timestamp > earlier_decision && aux_wire.timestamp > earlier_decision
		      && main_wire.sequence == 1 && aux_wire.sequence == 1,
		      "genuine ACK publications postdate the retained earlier decision without changing identity or sequence");

		if (scenario == 0) {
			check(worker.cycle(earlier_decision, p, snapshot, binding) == ManualWorkerStatus::OutputRejected,
			      "explicit-time API still strictly rejects ACKs newer than its supplied decision clock");

		} else if (scenario == 1) {
			const auto observed = worker.cycleObserved(p, snapshot, binding, first_deadline);
			check((observed == ManualWorkerStatus::NotDue || observed == ManualWorkerStatus::Command)
			      && worker.handledSequence() == 1, "live observation accepts both genuine ACKs after earlier state capture");
			check(commands.copyBound(&after, revision) && after.edf_request_timestamp == first.edf_request_timestamp
			      && after.edf_deadline == first.edf_deadline && after.deadline <= first.edf_deadline,
			      "live observation cannot renew held raw request time or expiry");
			const auto original_expiry = first.edf_deadline;
			p.timestamp_us = original_expiry;
			p.deadline_us = original_expiry + static_cast<uint64_t>(c.output_lease_ms) * 1000;
			check(worker.cycle(original_expiry, p, snapshot, binding) == ManualWorkerStatus::OutputRejected,
			      "fresh permission cannot extend the original raw and output expiry after observed ACK handling");
			check(commands.copyBound(&first, revision) && memcmp(&first, &after, sizeof(first)) == 0,
			      "raw expiry cannot republish an old or replacement command");

		} else if (scenario == 2) {
			const auto before_wait = hrt_absolute_time();
			const auto until_deadline = before_wait < first_deadline ? first_deadline - before_wait : 0;
			check(until_deadline < 100000, "first-command expiry wait remains within its original bounded lease");
			px4_usleep(static_cast<useconds_t>(until_deadline + 1));
			const auto current = hrt_absolute_time();
			stamp(current);
			p.timestamp_us = current;
			p.deadline_us = current + static_cast<uint64_t>(c.output_lease_ms) * 1000;
			check(worker.cycleObserved(p, snapshot, binding, first_deadline) == ManualWorkerStatus::OutputRejected,
			      "fresh state input and permission cannot extend the original bank first-command deadline");

		} else {
			check(aux_wire.timestamp == aux_handled && aux_handled > hrt_absolute_time()
			      && aux_handled < first.deadline,
			      "negative synthetic bank clock creates a genuinely future ACK within the unchanged output lease");
			check(worker.cycleObserved(p, snapshot, binding, first_deadline) == ManualWorkerStatus::OutputRejected,
			      "live observation still rejects an ACK genuinely newer than its actual read clock");
		}

		if (scenario != 1) {
			check(commands.copyBound(&after, revision) && memcmp(&first, &after, sizeof(first)) == 0,
			      "strict future or first-deadline rejection cannot publish another command");
		}

		check(worker.handledSequence() == 0 && worker.cycleObserved(p, snapshot, binding) == ManualWorkerStatus::Inactive,
		      "timing rejection revokes the session and cannot reuse a stale command");
	}
}

static void nativeSelectedWorkerTests()
{
	const auto c = fixture();
	const ManualOutputBinding binding{42, {51, 52}, 3};
	const SelectedInputPolicy policy{10000, 10000};

	for (unsigned scenario = 0; scenario < 16; ++scenario) {
		auto p = permission(1000);
		p.configuration = fingerprint(c);
		SelectedManualInput::Snapshot snapshot{};
		snapshot.manual.valid = true;
		snapshot.manual.source = research_actuator_command::ManualControlSource::Mavlink;
		snapshot.manual.mavlink_instance = c.manual_instance;
		snapshot.manual.source_system_id = c.manual_system;
		snapshot.manual.source_component_id = c.manual_component;
		snapshot.manual.axes[3] = -1.F;
		snapshot.state.valid = true;
		snapshot.state.source_epoch = 1;
		snapshot.gyro = 123;
		snapshot.accel = 456;
		snapshot.permission_fresh = true;
		snapshot.permission.armed = true;

		for (auto &revision : snapshot.revisions) { revision = 1; }

		auto stamp = [&](uint64_t now) {
			snapshot.now = snapshot.estimator_time = snapshot.flags_time = snapshot.permission.timestamp = now;
			snapshot.manual.timestamp_us = snapshot.manual.timestamp_sample_us = now;
			snapshot.state.attitude_timestamp_us = snapshot.state.attitude_sample_us = now;
			snapshot.state.rate_timestamp_us = snapshot.state.rate_sample_us = now;
		};
		stamp(1000);
		EdfOwner owner;
		ManualWorkerTransport worker(owner);
		uORB::Publication<slaaps_manual_ack_s> main{ORB_ID(slaaps_manual_ack_main)};
		uORB::Publication<slaaps_manual_ack_s> aux{ORB_ID(slaaps_manual_ack_aux)};
		uORB::ResearchSubscription commands{ORB_ID(slaaps_manual_command)};
		uORB::ResearchSubscription main_reply{ORB_ID(slaaps_manual_ack_main)};
		uORB::ResearchSubscription aux_reply{ORB_ID(slaaps_manual_ack_aux)};
		check(worker.advertise() && main.advertise() && aux.advertise(), "selected worker endpoint advertisements");
		const unsigned revisions[2] {main_reply.revision(), aux_reply.revision()};

		if (scenario == 13) {
			snapshot.gyro = 0;
			check(worker.begin(1000, c, p, snapshot, policy, binding, commands.revision(), revisions)
			      == ManualWorkerStatus::ControlRejected, "invalid selected metadata rejects entry");
			snapshot.gyro = 123;
			check(worker.begin(1000, c, p, snapshot, policy, binding, commands.revision(), revisions)
			      == ManualWorkerStatus::PropulsionRejected, "failed selected entry consumes owner session");
			continue;
		}

		check(worker.begin(1000, c, p, snapshot, policy, binding, commands.revision(), revisions)
		      == ManualWorkerStatus::Ready, "selected snapshot admitted through real controller");
		stamp(5000);
		check(executeManual(worker, 5000, true, true, p, snapshot, binding) == ManualWorkerStatus::Command,
		      "selected snapshot produces native combined command");
		check(executeManual(worker, 5000, true, true, p, snapshot, binding) == ManualWorkerStatus::Waiting,
		      "unchanged valid selected input accepted while waiting for banks");
		slaaps_manual_command_s before{}, after{};
		unsigned revision{};
		check(commands.copyBound(&before, revision) && before.sequence == 1, "selected command captured before input fault");
		stamp(5001);
		const auto valid = snapshot;

		switch (scenario) {
		case 0: ++snapshot.gyro; break;

		case 1: ++snapshot.accel; break;

		case 2: ++snapshot.revisions[4]; break;

		case 3: snapshot.estimator_time = 4999; break;

		case 4: snapshot.flags_time = 4999; break;

		case 5: snapshot.permission.timestamp = 4999; break;

		case 6: snapshot.permission.armed = false; break;

		case 7: snapshot.permission.kill = true; break;

		case 8: snapshot.permission.in_esc_calibration_mode = true; break;

		case 9: snapshot.flags_time = UINT64_MAX; break;

		case 10: snapshot.permission_fresh = false; break;

		case 11: snapshot.now = 4999; break;

		default: break;
		}

		const auto result = scenario == 12 ? worker.cycle(5001, p, snapshot.manual, snapshot.state, binding)
				    : executeManual(worker, 5001, scenario != 14, scenario != 15, p, snapshot, binding);
		check(result == ManualWorkerStatus::ControlRejected,
		      "selected input change or raw-cycle bypass revokes waiting worker");
		check(owner.sample(5001, p).disposition == EdfDisposition::RecoveryRequired,
		      "selected input fault revokes shared propulsion");
		check(commands.copyBound(&after, revision) && memcmp(&before, &after, sizeof(before)) == 0,
		      "input rejection never publishes a replacement or extends command deadline");
		check(worker.cycle(5001, p, valid, binding) == ManualWorkerStatus::Inactive,
		      "restored metadata cannot recover faulted session");
	}

	check(manualScheduleInterval(c, true) == static_cast<uint32_t>(c.cycle_min_us), "active schedule uses configured fast interval");
	check(manualScheduleInterval(c, false) == kSupervisorIdleIntervalUs, "inactive schedule returns to slow supervisor interval");
	auto invalid_timing = c;
	invalid_timing.cycle_min_us = 0;
	check(manualScheduleInterval(invalid_timing, true) == 0, "invalid active timing requests stop instead of zero-period scheduling");
	check(supervisorMaintenanceDue(1000, 0) && !supervisorMaintenanceDue(5000, 1000)
	      && supervisorMaintenanceDue(101000, 1000) && supervisorMaintenanceDue(999, 1000),
	      "maintenance cadence is independent of fast control ticks and handles clock discontinuity");
	// Independent metadata expiry re-evaluates age at consumption, not capture.
	SelectedManualInput::Snapshot snapshot{};
	snapshot.now = snapshot.estimator_time = snapshot.flags_time = snapshot.permission.timestamp = 1000;
	snapshot.state.valid = snapshot.permission_fresh = snapshot.permission.armed = true;
	snapshot.state.source_epoch = snapshot.gyro = snapshot.accel = 1;

	for (auto &revision : snapshot.revisions) { revision = 1; }

	SelectedManualInput guard;
	check(!guard.begin(1000, snapshot, SelectedInputPolicy{}), "zero selected metadata age budgets reject entry");
	check(guard.begin(1000, snapshot, policy) && !guard.check(11001, snapshot), "retained metadata expires at consumption");
	check(!guard.check(1000, snapshot), "metadata expiry latches until deliberate entry");
}

static void nativeDisarmedInputTests()
{
	const SelectedInputPolicy policy{10000, 10000};
	SelectedManualInput::Snapshot initial{};
	initial.now = initial.estimator_time = initial.flags_time = initial.permission.timestamp = 1000;
	initial.state.valid = initial.permission_fresh = true;
	initial.state.source_epoch = 1;
	initial.gyro = 123;
	initial.accel = 456;

	for (auto &revision : initial.revisions) { revision = 1; }

	SelectedManualInput independent;
	check(independent.beginDisarmed(1000, initial, policy) && !initial.permission.prearmed,
	      "disarmed metadata binding does not invent a prearm or flight requirement");
	check(independent.checkDisarmed(11000, initial) && !independent.checkDisarmed(11001, initial),
	      "disarmed metadata expires at consumption with the same bounded age policy");
	check(!independent.checkDisarmed(1000, initial), "disarmed expiry remains latched on clock restoration");
	check(!independent.begin(1000, initial, policy), "armed entry cannot consume disarmed metadata");
	auto armed = initial;
	armed.permission.armed = true;
	check(!independent.beginDisarmed(1000, armed, policy), "disarmed entry cannot consume armed metadata");
	check(independent.begin(1000, armed, policy) && !independent.checkDisarmed(1000, armed)
	      && !independent.check(1000, armed), "armed binding cannot be crossed through the disarmed polling API");
	auto missing = initial;
	missing.revisions[0] = 0;
	check(!independent.beginDisarmed(1000, missing, policy), "disarmed entry requires all original publisher lifetimes");

	for (unsigned fault = 0; fault < 29; ++fault) {
		SelectedManualInput guard;
		check(guard.beginDisarmed(1000, initial, policy), "deliberate disarmed metadata session binds");
		auto snapshot = initial;
		snapshot.now = snapshot.estimator_time = snapshot.flags_time = snapshot.permission.timestamp = 2000;
		const auto valid = snapshot;
		uint64_t now = 2000;

		if (fault < 7) {
			++snapshot.revisions[fault];

		} else {
			switch (fault) {
			case 7: ++snapshot.gyro; break;

			case 8: ++snapshot.accel; break;

			case 9: ++snapshot.state.source_epoch; break;

			case 10: snapshot.state.valid = false; break;

			case 11: snapshot.permission_fresh = false; break;

			case 12: snapshot.permission.armed = true; break;

			case 13: snapshot.permission.kill = true; break;

			case 14: snapshot.permission.lockdown = true; break;

			case 15: snapshot.permission.termination = true; break;

			case 16: snapshot.permission.in_esc_calibration_mode = true; break;

			case 17: snapshot.estimator_time = 999; break;

			case 18: snapshot.flags_time = 999; break;

			case 19: snapshot.permission.timestamp = 999; break;

			case 20: snapshot.now = 999; break;

			case 21: snapshot.now = 2001; break;

			case 22: snapshot.estimator_time = 2001; break;

			case 23: snapshot.flags_time = 2001; break;

			case 24: snapshot.permission.timestamp = 2001; break;

			case 25: now = 12001; break;

			case 26: break; // Cross the explicitly bound observation mode below.

			case 27: now = 999; break;

			case 28: snapshot.permission.timestamp = 0; break;
			}
		}

		check(!(fault == 26 ? guard.check(now, snapshot) : guard.checkDisarmed(now, snapshot)) && !guard.active(),
		      "disarmed source replacement stale state arming or inhibition cancels the bound session");
		check(!guard.checkDisarmed(2001, valid), "fresh restored metadata cannot recover a canceled disarmed binding");
		check(guard.beginDisarmed(2001, valid, policy) && guard.checkDisarmed(2002, valid),
		      "a deliberate new disarmed bind can recover with current complete metadata");
	}
}

// Native fake hardware boundary only: actual MixingOutput and supervisor code
// perform discovery/preparation. No PWM device or actuator is opened here.
#include <lib/mixer_module/mixer_module.hpp>
#include <lib/slaaps/Px4ConfigurationSource.hpp>
#include <uORB/topics/actuator_test.h>
#include <uORB/topics/vehicle_status.h>
#include <uORB/topics/vehicle_command_ack.h>
#include <atomic>
#include <new>

// Synthetic sensor/pilot producer for the running production supervisor. The
// real Commander exclusively owns actuator_armed and vehicle_status here.
class NativeSlaapsSelectedInput final : public px4::ScheduledWorkItem
{
public:
	NativeSlaapsSelectedInput() : ScheduledWorkItem("slaaps_selected_fixture", px4::wq_configurations::test1) {}
	void start() { ScheduleOnInterval(4000); }
	bool request(unsigned operation)
	{
		const auto deadline = hrt_absolute_time() + 100000;

		do {
			unsigned empty{};

			if (_request.compare_exchange_strong(empty, operation)) { return true; }

			px4_usleep(1000);
		} while (hrt_absolute_time() < deadline);

		return false;
	}
	void printStatus() const
	{
		PX4_INFO("SLAAPS_SELECTED_STATUS running=%u scenario=%u samples=%u failures=%u power_running=%u power_scenario=%u power_samples=%u synthetic=1 authority_publishers=0",
			 _publishing.load() ? 1U : 0U, _scenario.load(), _samples.load(), _failures.load(),
			 _power_publishing.load() ? 1U : 0U, _power_scenario.load(), _power_samples.load());
	}
private:
	void Run() override
	{
		const unsigned request = _request.exchange(0);

		if (request == 1) { _publishing.store(false); }

		if (request == 2) { _publishing.store(true); }

		if (request >= 3 && request <= 7) { _scenario.store(request - 3); }

		if (request == 8) {
			// unadvertise() retains PublicationBase's handle. Reconstruct the
			// publisher so advertise establishes a genuinely new lifetime.
			const unsigned previous = _attitude_lifetime.revision();
			using AttitudePublication = uORB::Publication<vehicle_attitude_s>;
			_attitude.~AttitudePublication();
			new (&_attitude) AttitudePublication{ORB_ID(vehicle_attitude)};

			if (!_attitude.advertise() || !_attitude_lifetime.revision()
			    || _attitude_lifetime.revision() == previous) { ++_failures; }
		}

		if (request == 9) { _power_publishing.store(true); _power_scenario.store(0); }

		if (request == 10) { _power_publishing.store(false); }

		if (request >= 11 && request <= 18) { _power_scenario.store(request - 10); _power_publishing.store(true); }

		if (request == 19) {
			const unsigned previous = _power_lifetime.revision();
			using BatteryPublication = uORB::Publication<battery_status_s>;
			_power.~BatteryPublication();
			new (&_power) BatteryPublication{ORB_ID(battery_status)};

			if (!_power.advertise() || !_power_lifetime.revision()
			    || _power_lifetime.revision() == previous) { ++_failures; }
		}

		if (!_publishing.load()) { return; }

		vehicle_attitude_s attitude{};
		vehicle_angular_velocity_s rates{};
		estimator_status_s estimator{};
		estimator_status_flags_s flags{};
		manual_control_setpoint_s manual{};
		sensor_selection_s selection{};
		const auto now = hrt_absolute_time();
		const unsigned scenario = _scenario.load();

		if (_power_publishing.load()) {
			// Explicit synthetic battery, confined to this capture-only fixture.
			battery_status_s power{};
			const unsigned power_scenario = _power_scenario.load();
			power.timestamp = now;
			power.connected = power_scenario != 1;
			power.id = power_scenario == 7 ? 2 : 1;
			power.source = power_scenario == 8 ? battery_status_s::SOURCE_EXTERNAL : battery_status_s::SOURCE_POWER_MODULE;
			power.voltage_v = power_scenario == 5 ? 19.F : (power_scenario == 6 ? 27.F : 24.F);
			power.warning = power_scenario == 2 ? battery_status_s::WARNING_LOW : battery_status_s::WARNING_NONE;
			power.faults = power_scenario == 3 ? 1U << battery_status_s::FAULT_OVER_CURRENT : 0;
			power.is_powering_off = power_scenario == 4;

			if (_power.publish(power)) { ++_power_samples; }

			else { ++_failures; }
		}

		attitude.timestamp = attitude.timestamp_sample = now;
		attitude.q[0] = scenario == 3 ? 0.F : 1.F;
		rates.timestamp = rates.timestamp_sample = now;
		estimator.timestamp = flags.timestamp = selection.timestamp = now;
		estimator.gyro_device_id = selection.gyro_device_id = 123;
		estimator.accel_device_id = selection.accel_device_id = 456;
		flags.cs_tilt_align = flags.cs_yaw_align = true;
		manual.timestamp = manual.timestamp_sample = now;
		manual.valid = true;
		manual.data_source = manual_control_setpoint_s::SOURCE_MAVLINK_0 + 2;
		manual.source_system_id = 255;
		manual.source_component_id = scenario == 4 ? 191 : 190;
		manual.roll = scenario == 1 ? .3F : 0.F;
		manual.throttle = scenario == 2 ? 1.F : -1.F;
		manual.sticks_moving = scenario == 1;
		const bool published = _attitude.publish(attitude) && _rates.publish(rates) && _estimator.publish(estimator)
				       && _flags.publish(flags) && _manual.publish(manual) && _selection.publish(selection)
				       && _attitude_lifetime.revision();

		if (published) { ++_samples; }

		else { ++_failures; }
	}
	uORB::Publication<vehicle_attitude_s> _attitude{ORB_ID(vehicle_attitude)};
	uORB::ResearchSubscription _attitude_lifetime{ORB_ID(vehicle_attitude)};
	uORB::Publication<vehicle_angular_velocity_s> _rates{ORB_ID(vehicle_angular_velocity)};
	uORB::Publication<estimator_status_s> _estimator{ORB_ID(estimator_status)};
	uORB::Publication<estimator_status_flags_s> _flags{ORB_ID(estimator_status_flags)};
	uORB::Publication<manual_control_setpoint_s> _manual{ORB_ID(manual_control_setpoint)};
	uORB::Publication<sensor_selection_s> _selection{ORB_ID(sensor_selection)};
	uORB::Publication<battery_status_s> _power{ORB_ID(battery_status)};
	uORB::ResearchSubscription _power_lifetime{ORB_ID(battery_status)};
	std::atomic<unsigned> _request{0}, _scenario{0}, _samples{0}, _failures{0};
	std::atomic<unsigned> _power_scenario{0}, _power_samples{0};
	std::atomic<bool> _publishing{true};
	std::atomic<bool> _power_publishing{true};
};

static int nativeSelectedCommand(const char *operation)
{
	// Retain the object for process lifetime, including paused producer states.
	static NativeSlaapsSelectedInput *producer{};

	if (!strcmp(operation, "start")) {
		if (producer) { return PX4_ERROR; }

		const char *names[] = {"EKF2_MULTI_IMU", "EKF2_MULTI_MAG", "SENS_IMU_MODE", "SENS_MAG_MODE"};
		const int32_t expected[] = {0, 0, 1, 1};

		for (unsigned i = 0; i < 4; ++i) {
			check(param_set(param_find(names[i]), &expected[i]) == 0, "select labeled synthetic single-estimator fixture");
		}

		producer = new NativeSlaapsSelectedInput;

		if (!producer) { return PX4_ERROR; }

		producer->start();
		PX4_INFO("SLAAPS_SELECTED_STARTED synthetic=1 authority_publishers=0 physical_outputs=0");
		return 0;
	}

	if (!producer) { return PX4_ERROR; }

	if (!strcmp(operation, "status")) { producer->printStatus(); return 0; }

	const char *operations[] = {"stop", "resume", "neutral", "offset", "throttle-high", "invalid-state", "wrong-source", "replace",
				    "power-valid", "power-drop", "power-disconnected", "power-warning", "power-fault", "power-off",
				    "power-low", "power-high", "power-id", "power-source", "power-replace"
				   };

	for (unsigned i = 0; i < sizeof(operations) / sizeof(operations[0]); ++i) {
		if (!strcmp(operation, operations[i])) { return producer->request(i + 1) ? 0 : PX4_ERROR; }
	}

	return PX4_ERROR;
}

class NativeSlaapsOutput final : public OutputModuleInterface
{
public:
	explicit NativeSlaapsOutput(const char *prefix) :
		OutputModuleInterface("slaaps_fake_output", px4::wq_configurations::hp_default),
		_mixing(prefix, 8, *this, MixingOutput::SchedulingPolicy::Disabled, false, false),
		_ack(strcmp(prefix, "PWM_MAIN") == 0 ? ORB_ID(slaaps_manual_ack_main) : ORB_ID(slaaps_manual_ack_aux)) {}
	void start() { _running.store(true); ScheduleOnInterval(20000); }
	void stop() { _running.store(false); ScheduleClear(); }
	unsigned writes() const { return _writes.load(); }
	unsigned nonzeroWrites() const { return _nonzero.load(); }
	void Run() override
	{
		if (!_running.load()) { return; }

		pump();
	}
	void pump()
	{
		updateParams();
		_mixing.update();
		uint16_t rates[16] {};

		for (unsigned i = 0; i < 8; ++i) { rates[i] = 50; }

		if (_mixing.slaapsInventoryDue()) { _mixing.reportSlaapsInventory(8, 255, rates, true); }

		_mixing.updateSubscriptions(false);
	}
	void expectAcknowledgment(uint64_t session, uint64_t sequence) { _expected_session = session; _expected_sequence = sequence; _trace_pending = true; }
	void failNextWrite() { _fail_next = true; }
	void revokeDuringNextWrite(uORB::Publication<slaaps_manual_permission_s> &publisher, const slaaps_manual_permission_s &denied)
	{
		_revoke_publisher = &publisher;
		_denied_permission = denied;
	}
	float lastOutput(unsigned channel) const { return _last_output[channel]; }
	unsigned lastCount() const { return _last_count; }
	uint64_t lastWriteTime() const { return _last_write_time; }
	bool prematureAcknowledgment() const { return _premature_ack; }
	MixingOutput &mixing() { return _mixing; }
	bool updateOutputs(float outputs[MAX_ACTUATORS], unsigned count, unsigned) override
	{
		++_writes;
		_last_count = count;
		_last_write_time = hrt_absolute_time();
		memcpy(_last_output, outputs, sizeof(_last_output));
		slaaps_manual_ack_s previous{};

		if (_trace_pending && _expected_session && _ack.copy(&previous) && previous.session == _expected_session
		    && previous.sequence == _expected_sequence) { _premature_ack = true; }

		_trace_pending = false;

		for (unsigned i = 0; i < count; ++i) {
			if (std::isfinite(outputs[i]) && (outputs[i] < 0.F || outputs[i] > 0.F)) { ++_nonzero; break; }
		}

		if (_revoke_publisher) {
			check(_revoke_publisher->publish(_denied_permission), "revoke permission inside capture-only physical callback");
			_revoke_publisher = nullptr;
		}

		const bool success = !_fail_next;
		_fail_next = false;
		return success;
	}
private:
	MixingOutput _mixing;
	uORB::Subscription _ack;
	uORB::Publication<slaaps_manual_permission_s> *_revoke_publisher{};
	slaaps_manual_permission_s _denied_permission{};
	float _last_output[MAX_ACTUATORS] {};
	uint64_t _last_write_time{}, _expected_session{}, _expected_sequence{};
	unsigned _last_count{};
	bool _premature_ack{}, _fail_next{}, _trace_pending{};
	std::atomic<bool> _running{false};
	std::atomic<unsigned> _writes{0}, _nonzero{0};
};

static void installNativeMixerConfiguration(const VehicleConfiguration &c)
{
#define SLAAPS_FLOAT(field, name, group, description, unit, minimum, maximum, initial) \
	check(param_set(param_find(#name), &c.field) == 0, "install labeled synthetic native configuration");
#define SLAAPS_INT SLAAPS_FLOAT
#include <lib/slaaps/VehicleParameters.def>
#undef SLAAPS_INT
#undef SLAAPS_FLOAT
	const auto inventory = routingFixture();

	for (unsigned bank = 0; bank < 2; ++bank) {
		const char *prefix = bank == 0 ? "PWM_MAIN" : "PWM_AUX";

		for (unsigned channel = 0; channel < 8; ++channel) {
			int32_t function = 0, minimum = 1100, maximum = 1900, center = 1500, zero = 0;

			for (unsigned i = 0; i < inventory.count; ++i) {
				const auto &item = inventory.channels[i];

				if (static_cast<unsigned>(item.bank) == bank && item.channel == channel) {
					function = item.function; minimum = item.minimum_us; maximum = item.maximum_us;
				}
			}

			const char *suffixes[] {"FUNC", "MIN", "MAX", "CENT", "DIS", "FAIL"};
			const int32_t values[] {function, minimum, maximum, center, zero, zero};

			for (unsigned i = 0; i < 6; ++i) {
				char name[17];
				snprintf(name, sizeof(name), "%s_%s%u", prefix, suffixes[i], channel + 1);
				check(param_set(param_find(name), &values[i]) == 0, "configure actual mixer function and calibration params");
			}
		}
	}

}

// Recompile the actual supervisor only in this explicit native test target.
// The normal modules__slaaps object is compiled separately without this seam.
// Synthetic admission is supplied at its permission-publication boundary;
// startup, registration, preparation, activation, worker and stop remain real.
#if !defined(SLAAPS_SUPERVISOR_TEST_ACCESS)
#error The isolated supervisor fixture requires its target-private test seam
#endif
struct SlaapsNativeTest {
	static std::atomic<bool> admitted;
	static void companionTransportTests();
	static void supplyPermission(hrt_abstime, slaaps::EdfPermission &permission)
	{
		const bool allow = admitted.load() && permission.armed && !permission.kill
				   && !permission.lockdown && !permission.termination;
		permission.flight_admitted = permission.healthy = permission.measured_condition_valid = allow;
	}
};
std::atomic<bool> SlaapsNativeTest::admitted{false};
#define Slaaps SlaapsNativeTestSubject
#define slaaps_main slaaps_native_test_subject_main
#include "../../../src/modules/slaaps/Slaaps.cpp"
#undef slaaps_main
#undef Slaaps

void SlaapsNativeTest::companionTransportTests()
{
	// The exact production method runs on one unscheduled subject. Synthetic
	// uORB authentication evidence does not test the MAVLink signature verifier.
	auto subject = std::make_unique<SlaapsNativeTestSubject>();
	auto c = fixture();
	c.companion = {1, 3, 42, 191, 16777217, 500000};
	using Publication = uORB::Publication<slaaps_companion_packet_s>;
	{
		// Exercise the queued API directly before the supervisor monitor. This
		// isolated instance is not the selected receiver or its distractor.
		auto publication = std::make_unique<Publication>(ORB_ID(slaaps_companion_packet_0));
		check(publication->advertise(), "advertise a queued lifetime without an initial payload");
		uORB::ResearchSubscription ordered{ORB_ID::slaaps_companion_packet_0};
		const unsigned first_revision = ordered.revision();
		slaaps_companion_packet_s packet{}, copied{};
		copied.sequence = 999;
		unsigned revision{};
		check(first_revision && !ordered.copyBound(&copied, revision) && !revision && copied.sequence == 999,
		      "queue-one copyBound remains strict and cannot silently consume queued packets");
		check(!ordered.copyQueuedBound(&copied, revision) && !revision && copied.sequence == 999,
		      "queued advertisement without a current-lifetime payload has no bound sample");

		for (uint64_t sequence = 1; sequence <= 3; ++sequence) {
			packet.sequence = sequence;
			check(publication->publish(packet), "publish ordered same-lifetime queued samples");
		}

		for (uint64_t sequence = 1; sequence <= 3; ++sequence) {
			check(ordered.updated() && ordered.copyQueuedBound(&copied, revision)
			      && copied.sequence == sequence && revision == first_revision,
			      "queued bound reads drain every unread same-lifetime packet in publication order");
		}

		check(!ordered.updated() && !ordered.copyQueuedBound(&copied, revision) && !revision && copied.sequence == 3,
		      "no-new-data queued read returns false without repeating or changing the last payload");
		const uint64_t last_sequence = 3 + slaaps_companion_packet_s::ORB_QUEUE_LENGTH + 3;

		for (uint64_t sequence = 4; sequence <= last_sequence; ++sequence) {
			packet.sequence = sequence;
			check(publication->publish(packet), "overrun the bounded queue while a bound reader is held");
		}

		for (uint64_t sequence = last_sequence - slaaps_companion_packet_s::ORB_QUEUE_LENGTH + 1; sequence <= last_sequence; ++sequence) {
			check(ordered.copyQueuedBound(&copied, revision) && copied.sequence == sequence && revision == first_revision,
			      "overwritten packets remain visibly skipped and retained packets drain in order under the original publisher revision");
		}

		check(copied.sequence == last_sequence && !ordered.copyQueuedBound(&copied, revision),
		      "queue overrun exposes its sequence gap instead of manufacturing missing events");

		for (uint64_t sequence = last_sequence + 1; sequence <= last_sequence + 2; ++sequence) {
			packet.sequence = sequence;
			check(publication->publish(packet), "leave old-advertiser packets queued before replacement");
		}

		publication.reset();
		publication = std::make_unique<Publication>(ORB_ID(slaaps_companion_packet_0));
		check(publication->advertise(), "replace the publisher without publishing any replacement payload");
		const unsigned replacement_revision = ordered.revision();
		uORB::ResearchSubscription fresh{ORB_ID::slaaps_companion_packet_0};
		copied.sequence = 999;
		check(replacement_revision && replacement_revision != first_revision
		      && !ordered.copyQueuedBound(&copied, revision) && !revision && copied.sequence == 999,
		      "existing queued reader cannot relabel old buffered packets with the replacement publisher lifetime");
		check(!fresh.copyQueuedBound(&copied, revision) && !revision && copied.sequence == 999,
		      "fresh unbound subscriber cannot receive queued old-advertiser bytes as a new-lifetime sample");
		packet.sequence = 1000;
		check(publication->publish(packet), "publish the first actual payload of the replacement lifetime");
		check(ordered.copyQueuedBound(&copied, revision) && copied.sequence == 1000 && revision == replacement_revision,
		      "lagging queued reader skips all earlier-advertiser slots and binds the first replacement sample atomically");
		check(fresh.copyQueuedBound(&copied, revision) && copied.sequence == 1000 && revision == replacement_revision,
		      "fresh subscriber receives only replacement data and its matching atomic publisher revision");
		check(!fresh.copyQueuedBound(&copied, revision) && !revision && copied.sequence == 1000,
		      "replacement payload is consumed once without no-data replay");
	}

	auto selected = std::make_unique<Publication>(ORB_ID(slaaps_companion_packet_3));
	Publication distractor{ORB_ID(slaaps_companion_packet_2)};
	uint64_t event_sequence = 100;
	auto frame = [&](bool session = false, uint64_t source = 16777217, uint8_t target_system = 1, uint8_t target_component = 1) {
		slaaps_companion_packet_s packet{};
		packet.mavlink_instance = 3;
		packet.source_system = 42;
		packet.source_component = 191;
		packet.receiver_system = packet.receiver_component = 1;
		packet.signing_generation = 7;
		packet.event = slaaps_companion_packet_s::EVENT_FRAME;
		packet.mavlink2 = packet.signing_active = packet.signature_verified = true;
		mavlink_message_t message{};

		if (session) {
			mavlink_research_acceleration_session_t payload{};
			payload.source_id = source;
			payload.session_id = payload.controller_instance_id = payload.transition_epoch = payload.clock_mapping_epoch = 1;
			payload.operation_sequence = payload.acknowledged_status_sequence = payload.producer_acknowledgement_monotonic_us = 1;
			payload.protocol_version = 0x00010000;
			payload.target_system = target_system;
			payload.target_component = target_component;
			payload.operation = 1;
			mavlink_msg_research_acceleration_session_encode(42, 191, &message, &payload);

		} else {
			mavlink_research_acceleration_request_t payload{};
			payload.source_id = source;
			payload.session_id = payload.controller_instance_id = payload.transition_epoch = payload.clock_mapping_epoch = 1;
			payload.sequence = payload.producer_monotonic_us = 1;
			payload.producer_expiry_monotonic_us = 2;
			payload.protocol_version = 0x00010000;
			payload.target_system = target_system;
			payload.target_component = target_component;
			payload.frame = payload.quantity = payload.reference_point = 1;
			mavlink_msg_research_acceleration_request_encode(42, 191, &message, &payload);
		}

		packet.message_id = message.msgid;
		packet.payload_length = message.len;
		std::memcpy(packet.payload, _MAV_PAYLOAD(&message), message.len);
		return packet;
	};
	auto apply = [&]() {
		check(subject->_configuration.commit(c, ConfigurationUse::Bench).valid(),
		      "companion method uses a real accepted shared profile rather than a fabricated source grant");
		++subject->_last_apply;
		subject->updateCompanion(hrt_absolute_time());
		check(subject->_companion_status == slaaps_status_s::COMPANION_WAIT_FRAME && !subject->_companion_fault
		      && !subject->_companion_deadline, "new accepted apply resets only the receipt monitor and requires a post-apply frame");
	};
	auto send = [&](slaaps_companion_packet_s packet) {
		// Separate real HRT receipt from the apply boundary without inventing a
		// producer timestamp or renewing a retained packet's original deadline.
		do { packet.timestamp = hrt_absolute_time(); } while (packet.timestamp <= subject->_companion_started);

		packet.sequence = ++event_sequence;
		check(selected->publish(packet), "publish synthetic receiver-owned evidence on the selected companion topic");
		subject->updateCompanion(hrt_absolute_time());
		return packet.timestamp;
	};
	auto queued = frame();
	queued.timestamp = hrt_absolute_time();
	queued.sequence = event_sequence;
	check(selected->publish(queued), "queue a frame before accepted companion configuration is observed");
	apply();
	check(!subject->_companion_revision && !subject->_companion_sequence,
	      "configuration apply ignores pre-apply queued frames without binding their publisher or sequence");
	auto other = frame();
	other.timestamp = hrt_absolute_time();
	other.sequence = 1;
	other.mavlink_instance = 2;
	check(distractor.publish(other), "publish an otherwise matching frame on an unselected companion receiver");
	subject->updateCompanion(hrt_absolute_time());
	check(subject->_companion_status == slaaps_status_s::COMPANION_WAIT_FRAME && !subject->_companion_revision,
	      "only configured instance three is consumed; instance two cannot establish receipt readiness");
	const auto receipt = send(frame());
	const auto original_deadline = receipt + static_cast<uint64_t>(c.companion.maximum_age_us);
	check(subject->_companion_status == slaaps_status_s::COMPANION_AUTHENTICATED_FRAME
	      && subject->_companion_revision && subject->_companion_sequence == event_sequence
	      && subject->_companion_deadline == original_deadline,
	      "matching authenticated request records the actual source and original local receipt expiry only");
	subject->updateCompanion(original_deadline - 1);
	check(subject->_companion_deadline == original_deadline, "empty-queue polling never renews the immutable receipt deadline");
	subject->updateCompanion(original_deadline);
	check(subject->_companion_status == slaaps_status_s::COMPANION_STALE && !subject->_companion_deadline,
	      "receipt expires at its original exclusive deadline without extending producer command validity");
	(void)send(frame(true));
	check(subject->_companion_status == slaaps_status_s::COMPANION_AUTHENTICATED_FRAME,
	      "fresh matching session payload can restore receipt evidence after ordinary expiry without creating a trusted session");

	for (unsigned fault = 0; fault < 14; ++fault) {
		auto packet = frame();
		uint8_t expected = slaaps_status_s::COMPANION_INVALID_FRAME;

		switch (fault) {
		case 0: packet.signature_verified = false; expected = slaaps_status_s::COMPANION_AUTHENTICATION_REQUIRED; break;

		case 1: packet.signing_active = false; expected = slaaps_status_s::COMPANION_AUTHENTICATION_REQUIRED; break;

		case 2: packet.mavlink2 = false; expected = slaaps_status_s::COMPANION_AUTHENTICATION_REQUIRED; break;

		case 3: ++packet.source_system; expected = slaaps_status_s::COMPANION_SOURCE_MISMATCH; break;

		case 4: ++packet.source_component; expected = slaaps_status_s::COMPANION_SOURCE_MISMATCH; break;

		case 5: packet = frame(false, 16777218); expected = slaaps_status_s::COMPANION_SOURCE_MISMATCH; break;

		case 6: packet = frame(false, 16777217, 2); expected = slaaps_status_s::COMPANION_TARGET_MISMATCH; break;

		case 7: packet = frame(false, 16777217, 1, 0); expected = slaaps_status_s::COMPANION_TARGET_MISMATCH; break;

		case 8: packet.payload_length = 84; break;

		case 9: packet.payload_length = MAVLINK_MSG_ID_RESEARCH_ACCELERATION_REQUEST_LEN + 1; break;

		case 10: packet.event = slaaps_companion_packet_s::EVENT_FRAME + 1; break;

		case 11: packet.mavlink_instance = 2; break;

		case 12: packet.message_id = MAVLINK_MSG_ID_HEARTBEAT; break;

		case 13: packet = frame(true); packet.payload_length = 70; break;
		}

		(void)send(packet);
		check(subject->_companion_status == expected && !subject->_companion_deadline && !subject->_companion_fault,
		      "unauthenticated, foreign source/target or malformed receipt revokes current evidence without granting command admission");
		(void)send(frame(fault % 2 != 0));
		check(subject->_companion_status == slaaps_status_s::COMPANION_AUTHENTICATED_FRAME,
		      "a later intact contiguous frame may recover ordinary per-frame rejection, preserving receiver lifetime");
	}

	for (unsigned fault = 0; fault < 5; ++fault) {
		auto packet = frame();

		if (fault == 0) { ++event_sequence; }

		if (fault == 1) {
			selected.reset();
			selected = std::make_unique<Publication>(ORB_ID(slaaps_companion_packet_3));
		}

		if (fault == 2) { ++packet.signing_generation; }

		if (fault == 3) { ++packet.receiver_system; }

		if (fault == 4) { ++packet.receiver_component; }

		(void)send(packet);
		const uint8_t expected = fault == 0 ? slaaps_status_s::COMPANION_SEQUENCE_GAP : slaaps_status_s::COMPANION_LIFETIME_CHANGED;
		check(subject->_companion_fault && subject->_companion_status == expected && !subject->_companion_deadline,
		      "event loss, publisher replacement, signing replacement or local-identity replacement latches receipt failure");
		(void)send(frame());
		check(subject->_companion_fault && subject->_companion_status == expected && !subject->_companion_deadline,
		      "restored traffic cannot revive a faulted receiver lifetime");
		subject->_configuration.disable();
		subject->updateCompanion(hrt_absolute_time());
		check(subject->_configuration.commit(c, ConfigurationUse::Bench).valid(), "recommit the unchanged profile with the same apply counter");
		subject->updateCompanion(hrt_absolute_time());
		check(subject->_companion_fault && subject->_companion_status == expected,
		      "temporary candidate loss and unchanged-profile restoration cannot masquerade as deliberate reapply");
		apply();
		(void)send(frame());
		check(subject->_companion_status == slaaps_status_s::COMPANION_AUTHENTICATED_FRAME && !subject->_companion_fault,
		      "only a new accepted apply followed by fresh evidence binds the replacement receiver lifecycle");
	}

	subject->_bench_target_parameter = param_find("SLA_BENCH_CH");
	subject->publishStatus(hrt_absolute_time());
	uORB::Subscription status_input{ORB_ID(slaaps_status)};
	slaaps_status_s status{};
	check(status_input.copy(&status) && status.companion_transport_status == slaaps_status_s::COMPANION_AUTHENTICATED_FRAME
	      && status.companion_receiver_revision == subject->_companion_revision
	      && status.companion_event_sequence == event_sequence && status.companion_frame_deadline == subject->_companion_deadline
	      && !status.can_arm_mask && !status.can_run_mask && !status.manual_worker_active && !status.bench_worker_active,
	      "actual supervisor status exposes receipt evidence while all control availability and workers remain denied");
	check(!subject->_preparation_pending && !subject->_manual_activation_owned && !subject->_prepared_request.session
	      && subject->_manual_permission.mode == ControlMode::None && !subject->_manual_permission.armed,
	      "authenticated receipt never starts preparation, mints output ownership or manufactures armed permission");
	c.companion.enabled = 0;
	check(subject->_configuration.commit(c, ConfigurationUse::Bench).valid(), "accept deliberate companion transport disable");
	++subject->_last_apply;
	(void)send(frame());
	subject->publishStatus(hrt_absolute_time());
	check(status_input.copy(&status) && status.companion_transport_status == slaaps_status_s::COMPANION_DISABLED
	      && !status.companion_frame_deadline && !status.can_arm_mask && !status.can_run_mask,
	      "disabled transport ignores later frames and clears advertised receipt expiry without changing output admission");
	PX4_INFO("SLAAPS_COMPANION_METHOD_TEST_PASS synthetic_uORB=1 actual_method=1 authentication_verified=0 command_admission=0 physical_outputs=0");
}

// No Commander or normal supervisor runs during this isolated command. Topic
// authority and registration replies are explicitly synthetic; both PWM-bank
// endpoints use the real mixer and capture-only updateOutputs callback above.
static int nativeSupervisorOrchestrationTests()
{
	checks = 0;
	auto c = fixture();
	c.cycle_min_us = 10000;
	c.cycle_max_us = 200000;
	c.manual_age_ms = c.state_age_ms = 100;
	c.selected_status_age_us = c.actuator_permission_age_us = 100000;
	c.activation_ack_timeout_us = 40000;
	c.first_command_budget_us = 30000;
	installNativeMixerConfiguration(c);
	const char *estimator_names[] = {"EKF2_MULTI_IMU", "EKF2_MULTI_MAG", "SENS_IMU_MODE", "SENS_MAG_MODE"};
	const int32_t estimator_values[] = {0, 0, 1, 1};

	for (unsigned i = 0; i < 4; ++i) {
		check(param_set(param_find(estimator_names[i]), &estimator_values[i]) == 0,
		      "isolated supervisor selects synthetic single-estimator metadata");
	}

	const int32_t scope = 1, apply = 300;
	check(param_set(param_find("SLA_CFG_KIND"), &scope) == 0
	      && param_set(param_find("SLA_CFG_APPLY"), &apply) == 0,
	      "isolated supervisor installs explicit synthetic manual profile");
	Px4ConfigurationSource parameters;
	ParameterSnapshot profile{};
	uint32_t capture_duration{};
	check(parameters.initialize() && parameters.capture(profile, capture_duration) && parameters.remember(profile),
	      "isolated supervisor uses actual accepted-profile restore path");
	NativeSlaapsOutput main("PWM_MAIN"), aux("PWM_AUX");
	uORB::Publication<vehicle_status_s> vehicle_pub{ORB_ID(vehicle_status)};
	uORB::Publication<actuator_armed_s> armed_pub{ORB_ID(actuator_armed)};
	uORB::Publication<vehicle_attitude_s> attitude_pub{ORB_ID(vehicle_attitude)};
	uORB::Publication<vehicle_angular_velocity_s> rates_pub{ORB_ID(vehicle_angular_velocity)};
	uORB::Publication<estimator_status_s> estimator_pub{ORB_ID(estimator_status)};
	uORB::Publication<estimator_status_flags_s> flags_pub{ORB_ID(estimator_status_flags)};
	uORB::Publication<manual_control_setpoint_s> manual_pub{ORB_ID(manual_control_setpoint)};
	uORB::Publication<sensor_selection_s> selection_pub{ORB_ID(sensor_selection)};
	uORB::Publication<register_ext_component_reply_s> registration_pub{ORB_ID(register_ext_component_reply)};
	uORB::Publication<airspeed_s> condition_airspeed_pub{ORB_ID(airspeed)};
	uORB::Publication<vehicle_air_data_s> condition_air_data_pub{ORB_ID(vehicle_air_data)};
	uORB::Publication<battery_status_s> power_pub{ORB_ID(battery_status)};
	uORB::Publication<setpoint_config_reply_s> setpoint_pub{ORB_ID(setpoint_config_reply)};
	uORB::Subscription registration{ORB_ID(register_ext_component_request)};
	uORB::Subscription setpoint{ORB_ID(setpoint_config)};
	uORB::Subscription status_sub{ORB_ID(slaaps_status)};
	uORB::Subscription command_sub{ORB_ID(slaaps_manual_command)};
	uORB::Subscription main_ack_sub{ORB_ID(slaaps_manual_ack_main)}, aux_ack_sub{ORB_ID(slaaps_manual_ack_aux)};
	uORB::Subscription aux_activation_sub{ORB_ID(slaaps_manual_active_aux)};
	uORB::Subscription disarm_sub{ORB_ID(vehicle_command)};
	uORB::Subscription scheduled_permission_sub{ORB_ID(slaaps_manual_permission)};
	uORB::Publication<vehicle_command_ack_s> disarm_ack_pub{ORB_ID(vehicle_command_ack)};
	unsigned disarm_requests{};
	uint64_t last_disarm_request{};
	vehicle_status_s vehicle{};
	actuator_armed_s armed{};
	vehicle.nav_state = vehicle.nav_state_user_intention = vehicle_status_s::NAVIGATION_STATE_MANUAL;
	bool publish_inputs = true, publish_estimates = true, pump_aux = true, vehicle_armed = false;
	bool publish_conditions = false;
	bool publish_power = true;
	unsigned power_scenario{};
	float condition_airspeed = 20.F;
	float roll = 0.F, throttle = -1.F;
	slaaps_status_s status{};
	const uint64_t started = hrt_absolute_time();
	SlaapsNativeTest::admitted.store(true);
	auto pump = [&]() {
		const auto now = hrt_absolute_time();
		vehicle.timestamp = armed.timestamp = now;
		vehicle.arming_state = vehicle_armed ? vehicle_status_s::ARMING_STATE_ARMED : vehicle_status_s::ARMING_STATE_DISARMED;
		bool published = vehicle_pub.publish(vehicle) && armed_pub.publish(armed);

		if (publish_power) {
			// One synthetic battery supplies both propulsion and optional schedule observations.
			battery_status_s battery{};
			battery.timestamp = now;
			battery.connected = true;
			battery.id = 1;
			battery.source = battery_status_s::SOURCE_POWER_MODULE;
			battery.voltage_v = 24.F;
			battery.faults = power_scenario == 1 ? 1U << battery_status_s::FAULT_OVER_CURRENT : 0;
			published &= power_pub.publish(battery);
		}

		if (publish_conditions) {
			airspeed_s airspeed{};
			vehicle_air_data_s air_data{};
			airspeed.timestamp = airspeed.timestamp_sample = now;
			airspeed.indicated_airspeed_m_s = condition_airspeed;
			airspeed.confidence = 1.F;
			air_data.timestamp = air_data.timestamp_sample = now;
			air_data.rho = 1.2F;
			air_data.baro_device_id = 456;
			air_data.calibration_count = 2;
			air_data.temperature_source = 1;
			published &= condition_airspeed_pub.publish(airspeed) && condition_air_data_pub.publish(air_data);
		}

		if (publish_inputs) {
			vehicle_attitude_s attitude{};
			vehicle_angular_velocity_s rates{};
			estimator_status_s estimator{};
			estimator_status_flags_s flags{};
			manual_control_setpoint_s manual{};
			sensor_selection_s selection{};
			attitude.timestamp = attitude.timestamp_sample = rates.timestamp = rates.timestamp_sample = now;
			attitude.q[0] = 1.F;
			estimator.timestamp = flags.timestamp = selection.timestamp = now;
			estimator.gyro_device_id = selection.gyro_device_id = 123;
			estimator.accel_device_id = selection.accel_device_id = 456;
			flags.cs_tilt_align = flags.cs_yaw_align = true;
			manual.timestamp = manual.timestamp_sample = now;
			manual.valid = true;
			manual.data_source = manual_control_setpoint_s::SOURCE_MAVLINK_0 + 2;
			manual.source_system_id = 255;
			manual.source_component_id = 190;
			manual.roll = roll;
			manual.throttle = throttle;
			manual.sticks_moving = roll > 0.F;
			published &= manual_pub.publish(manual);

			if (publish_estimates) {
				published &= attitude_pub.publish(attitude) && rates_pub.publish(rates) && estimator_pub.publish(estimator)
					     && flags_pub.publish(flags) && selection_pub.publish(selection);
			}
		}

		register_ext_component_request_s request{};

		while (registration.update(&request)) {
			register_ext_component_reply_s reply{};
			reply.timestamp = now;
			reply.request_id = request.request_id;
			reply.success = true;
			reply.mode_id = reply.arming_check_id = -1;
			reply.mode_executor_id = -1;

			for (unsigned i = 0; i < kModeCount; ++i) {
				if (!strcmp(request.name, kModeNames[i])) {
					reply.mode_id = vehicle_status_s::NAVIGATION_STATE_EXTERNAL1 + i;
					reply.arming_check_id = i;
				}
			}

			if (reply.mode_id < 0) { published = false; }

			published &= registration_pub.publish(reply);
		}

		setpoint_config_s configuration{};

		while (setpoint.update(&configuration)) {
			setpoint_config_reply_s reply{};
			reply.timestamp = now;
			reply.source_id = configuration.source_id;
			reply.type = configuration.type;
			reply.result = setpoint_config_reply_s::RESULT_SUCCESS;
			published &= setpoint_pub.publish(reply);
		}

		if (!published) { check(false, "isolated supervisor synthetic topic publication succeeds"); }

		vehicle_command_s command{};

		while (disarm_sub.update(&command)) {
			if (command.command == vehicle_command_s::VEHICLE_CMD_COMPONENT_ARM_DISARM
			    && near(command.param1, static_cast<float>(vehicle_command_s::ARMING_ACTION_DISARM))) {
				check(near(command.param2, 0.F) && !command.from_external,
				      "ground commissioning requests ordinary Commander disarm without force or synthetic arming state");
				++disarm_requests;
				last_disarm_request = command.timestamp;
			}
		}

		main.pump();

		if (pump_aux) { aux.pump(); }

		status_sub.copy(&status);
	};
	auto wait = [&](auto predicate, uint64_t timeout, const char *message) {
		const auto deadline = hrt_absolute_time() + timeout;
		bool complete = false;

		do {
			pump();
			complete = predicate();

			if (!complete) { px4_usleep(1000); }

		} while (!complete && hrt_absolute_time() < deadline);

		if (!complete) {
			PX4_ERR("supervisor fixture entry=%u worker=%u active=%u preparation=%u session=%llu",
				status.manual_entry_status, status.manual_worker_status, status.manual_worker_active,
				status.manual_preparation_status, static_cast<unsigned long long>(status.manual_preparation_session));
			PX4_ERR("supervisor fixture bench entry=%u active=%u target=%u deadline=%llu",
				status.bench_entry_status, status.bench_worker_active, status.bench_target,
				static_cast<unsigned long long>(status.bench_session_deadline));
			PX4_ERR("supervisor fixture routing=%u inventory_fault=%u routing_fault=%u configured=%u",
				status.routing_snapshot_valid, status.inventory_fault, status.routing_fault, status.direct_actuator_modes_configured);
			const ORB_ID topics[] {ORB_ID::slaaps_manual_ready_main, ORB_ID::slaaps_manual_ready_aux,
					       ORB_ID::slaaps_manual_ack_main, ORB_ID::slaaps_manual_ack_aux,
					       ORB_ID::slaaps_manual_active_main, ORB_ID::slaaps_manual_active_aux
					      };
			unsigned revisions[6] {};

			for (unsigned i = 0; i < 6; ++i) {
				uORB::ResearchSubscription lifetime{topics[i]};
				revisions[i] = lifetime.revision();
			}

			PX4_ERR("supervisor fixture reply revisions ready=%u/%u handled=%u/%u activation=%u/%u",
				revisions[0], revisions[1], revisions[2], revisions[3], revisions[4], revisions[5]);
		}

		check(complete, message);
	};
	pump();
	check(SlaapsNativeTestSubject::task_spawn(1, nullptr) == PX4_OK,
	      "isolated exact-source supervisor starts through actual task_spawn");
	wait([&]() {
		return status.timestamp >= started && status.direct_actuator_modes_configured
		       && status.configuration_valid && status.configuration_identity == fingerprint(c)
		       && status.manual_entry_status == slaaps_status_s::MANUAL_ENTRY_INACTIVE;
	}, 3000000,
	"actual supervisor registers all modes and restores synthetic profile without entering");
	check(!status.can_run_mask && !status.can_arm_mask && !status.manual_worker_active,
	      "isolated synthetic authority does not change production mode availability masks");
	uint64_t previous_session{};

	for (unsigned scenario = 0; scenario < 4; ++scenario) {
		vehicle_armed = armed.armed = false;
		vehicle.nav_state = vehicle.nav_state_user_intention = vehicle_status_s::NAVIGATION_STATE_MANUAL;
		publish_inputs = pump_aux = true;
		roll = 0.F;
		throttle = -1.F;
		SlaapsNativeTest::admitted.store(true);
		wait([&]() { return status.manual_entry_status == slaaps_status_s::MANUAL_ENTRY_INACTIVE; }, 1000000,
		"actual disarmed leave enables a deliberate new supervisor entry");
		vehicle.nav_state = vehicle.nav_state_user_intention = status.mode_id[0];

		if (!scenario) {
			power_scenario = 1;
			wait([&]() { return status.manual_entry_status == slaaps_status_s::MANUAL_ENTRY_WAIT_POWER; }, 1000000,
			"manual readiness waits for a healthy selected propulsion battery before preparing any output");
			check(status.power_required && !status.power_ready && status.power_status == slaaps_status_s::POWER_FAULT
			      && !status.manual_preparation_session && !status.manual_worker_active && !status.can_arm_mask,
			      "battery fault cannot mint manual preparation, flight permission or propulsion output");
			power_scenario = 0;
		}

		wait([&]() { return status.manual_entry_status == slaaps_status_s::MANUAL_ENTRY_PREPARED; }, 1000000,
		"actual supervisor observes centered low-throttle entry and prepares both mixers");
		const auto session = status.manual_preparation_session;
		check(session > previous_session && status.manual_preparation_banks == 3 && !status.manual_worker_active,
		      "deliberate supervisor reentry creates a fresh non-actuating two-bank session");
		check(status.power_ready && status.power_source_bound && status.power_publisher_revision
		      && status.power_deadline > status.timestamp, "manual preparation pins the healthy source and its original report expiry");
		previous_session = session;
		const auto before = main.nonzeroWrites() + aux.nonzeroWrites();
		slaaps_manual_activation_s activation{};

		if (scenario == 0 || scenario == 2) {
			vehicle_armed = scenario == 0;
			armed.armed = scenario == 2;
			wait([&]() { return status.manual_entry_status == slaaps_status_s::MANUAL_ENTRY_ACTIVATING; }, 500000,
			"actual supervisor waits through either staggered arming-publication order");
			check(main.nonzeroWrites() + aux.nonzeroWrites() == before
			      && (!aux_activation_sub.copy(&activation) || activation.session != session),
			      "one armed source cannot activate either bank or produce PWM");
		}

		vehicle_armed = armed.armed = true;
		wait([&]() { return aux_activation_sub.copy(&activation) && activation.session == session; }, 500000,
		"actual supervisor ACTIVATE reaches both real mixer banks");
		pump_aux = false;
		slaaps_manual_command_s first{};
		slaaps_manual_ack_s main_ack{}, aux_ack{};
		wait([&]() {
			return command_sub.copy(&first) && first.session == session && first.sequence == 1
			       && main_ack_sub.copy(&main_ack) && main_ack.session == session && main_ack.sequence == 1
			       && status.manual_entry_status == slaaps_status_s::MANUAL_ENTRY_FIRST_COMMAND;
		}, 100000,
		"actual supervisor keeps FirstCommand after MAIN alone handles its first output");
		check(!aux_ack_sub.copy(&aux_ack) || aux_ack.session != session,
		      "withheld AUX callback cannot supply first handled acknowledgment");
		check(first.timestamp_sample >= activation.timestamp_sample && first.edf_request_timestamp >= session,
		      "supervisor first calculation postdates bank activation and retains post-entry raw sample");

		if (scenario == 1) {
			wait([&]() {
				return status.manual_entry_status == slaaps_status_s::MANUAL_ENTRY_REJECTED
				       && !status.manual_worker_active;
			}, 300000,
			"missing exact AUX handling expires actual supervisor first-command phase");
			pump_aux = true;
			pump();
			check(!aux_ack_sub.copy(&aux_ack) || aux_ack.session != session,
			      "late AUX dispatch cannot acknowledge expired first command");

		} else {
			pump_aux = true;
			wait([&]() {
				return status.manual_entry_status == slaaps_status_s::MANUAL_ENTRY_RUNNING
				       && status.manual_worker_active;
			}, 100000,
			"both exact handled replies advance actual supervisor to Running");
			roll = .3F;
			throttle = 1.F;
			slaaps_manual_command_s running{};
			wait([&]() {
				return command_sub.copy(&running) && running.session == session && running.sequence >= 4
				       && running.edf > 0.F && main_ack_sub.copy(&main_ack) && aux_ack_sub.copy(&aux_ack)
				       && main_ack.session == session && aux_ack.session == session
				       && main_ack.sequence >= 3 && aux_ack.sequence >= 3;
			}, 200000,
			"actual supervisor sustains body-rate and EDF commands through both exact mixer callbacks");
			check(main.nonzeroWrites() + aux.nonzeroWrites() > before && !main.prematureAcknowledgment()
			      && !aux.prematureAcknowledgment(), "supervisor output uses capture callbacks before handled replies");

			if (scenario == 0) { publish_inputs = false; }

			else if (scenario == 3) { vehicle_armed = armed.armed = false; }

			else { SlaapsNativeTest::admitted.store(false); }

			wait([&]() {
				return status.manual_entry_status == slaaps_status_s::MANUAL_ENTRY_REJECTED
				       && !status.manual_worker_active;
			}, 300000,
			"actual supervisor stops on ordinary disarm or cancels on current input or admission loss");
		}

		publish_inputs = true;
		roll = 0.F;
		throttle = -1.F;
		SlaapsNativeTest::admitted.store(true);
		const auto stopped_writes = main.nonzeroWrites() + aux.nonzeroWrites();
		const auto settle = hrt_absolute_time() + 120000;

		while (hrt_absolute_time() < settle) { pump(); px4_usleep(1000); }

		check(status.manual_entry_status == slaaps_status_s::MANUAL_ENTRY_REJECTED && !status.manual_worker_active
		      && main.nonzeroWrites() + aux.nonzeroWrites() == stopped_writes,
		      "restored authority and fresh inputs cannot revive rejected supervisor session");
		vehicle_armed = armed.armed = false;

		if (scenario != 3) {
			pump();
			const int32_t reapply = 301 + scenario;
			check(param_set(param_find("SLA_CFG_APPLY"), &reapply) == 0,
			      "armed supervisor fault requires actual deliberate configuration reapply after disarm");
			wait([&]() { return status.configuration_valid && status.apply_counter == reapply; }, 1000000,
			"actual supervisor accepts deliberate postfault disarmed apply");
			check(status.manual_entry_status == slaaps_status_s::MANUAL_ENTRY_REJECTED && !status.manual_worker_active,
			      "postfault apply alone cannot revive the still-selected supervisor entry");

		} else {
			wait([&]() { return status.configuration_valid; }, 1000000,
			"ordinary disarm retains exact unchanged accepted profile through fresh disarmed revalidation");
		}

		vehicle.nav_state = vehicle.nav_state_user_intention = vehicle_status_s::NAVIGATION_STATE_MANUAL;
		wait([&]() { return status.manual_entry_status == slaaps_status_s::MANUAL_ENTRY_INACTIVE; }, 1000000,
		"actual supervisor disarmed mode exit cancels and clears the session");
	}

	vehicle.nav_state = vehicle.nav_state_user_intention = status.mode_id[0];
	wait([&]() { return status.manual_entry_status == slaaps_status_s::MANUAL_ENTRY_PREPARED; }, 1000000,
	"ordinary disarm profile retention supports deliberate reentry without another apply");
	check(status.manual_preparation_session > previous_session && status.apply_counter == 303,
	      "clean-stop reentry uses a new session and the same accepted apply revision");
	vehicle.nav_state = vehicle.nav_state_user_intention = vehicle_status_s::NAVIGATION_STATE_MANUAL;
	wait([&]() { return status.manual_entry_status == slaaps_status_s::MANUAL_ENTRY_INACTIVE; }, 1000000,
	"isolated supervisor leaves its final non-actuating preparation before stop");

	// A separate disarmed numerical-readiness scenario uses real topic copies,
	// but never supplies synthetic flight admission or an armed state. Opposite
	// full-rank IAS corners intentionally cancel at the table's midpoint.
	auto scheduled = c;
	auto &table = scheduled.schedule;
	table.enabled = 1;
	table.airspeed_instance = table.air_data_instance = table.battery_instance = 0;
	table.battery_id = 1;
	table.maximum_age_us = 1000000;
	table.indicated_airspeed_m_s[0] = 10.F;
	table.indicated_airspeed_m_s[1] = 30.F;
	table.edf_command[0] = 0.F;
	table.edf_command[1] = 1.F;
	table.density_min = 1.F;
	table.density_max = 1.4F;
	table.voltage_min = 20.F;
	table.voltage_max = 25.F;
	table.filter_time_s = .02F;
	table.airspeed_filter_rate = 100.F;
	table.edf_filter_rate = 10.F;
	table.airspeed_transient_rate = 1000.F;
	table.edf_transient_rate = 10.F;

	for (unsigned i = 0; i < kScheduleKnots; ++i) {
		for (unsigned j = 0; j < kScheduleKnots; ++j) {
			auto &corner = table.corner[i][j];

			for (unsigned axis = 0; axis < kAxes; ++axis) {
				corner.rate_p[axis] = c.axis[axis].rate_p;
				corner.rate_i[axis] = c.axis[axis].rate_i;
				corner.rate_d[axis] = c.axis[axis].rate_d;

				for (unsigned canard = 0; canard < kCanards; ++canard) {
					corner.moment_derivative[axis][canard] = (i ? -1.F : 1.F) * c.canard[canard].moment_derivative[axis];
				}
			}
		}
	}

	check(validate(scheduled, ConfigurationUse::StabilizedManual).valid(),
	      "explicit synthetic scheduled prearm fixture has four individually valid corners");
	SlaapsNativeTest::admitted.store(false);
	installNativeMixerConfiguration(scheduled);
	const int32_t scheduled_apply = 304;
	check(param_set(param_find("SLA_CFG_APPLY"), &scheduled_apply) == 0,
	      "deliberately apply scheduled readiness profile while genuinely disarmed");
	publish_conditions = true;
	condition_airspeed = 40.F;
	wait([&]() {
		return status.configuration_valid && status.configuration_identity == fingerprint(scheduled)
		       && status.apply_counter == scheduled_apply;
	}, 1000000,
	"actual supervisor accepts complete scheduled profile through its parameter path");
	const unsigned scheduled_writes = main.nonzeroWrites() + aux.nonzeroWrites();
	const uint64_t scheduled_started = hrt_absolute_time();
	vehicle.nav_state = vehicle.nav_state_user_intention = status.mode_id[0];
	wait([&]() { return status.manual_entry_status == slaaps_status_s::MANUAL_ENTRY_WAIT_SCHEDULE; }, 1000000,
	"disarmed scheduling readiness rejects current IAS outside its calibrated table");
	const auto outside_at = status.timestamp;
	condition_airspeed = 20.F;
	uint64_t rank_observed_at = outside_at;
	unsigned rank_observations{};
	wait([&]() {
		if (status.timestamp > rank_observed_at && status.manual_entry_status == slaaps_status_s::MANUAL_ENTRY_WAIT_SCHEDULE) {
			rank_observed_at = status.timestamp;
			++rank_observations;
		}

		return rank_observations >= 2;
	}, 1000000,
	"disarmed scheduling readiness rejects an interior rank loss despite valid corner matrices");
	condition_airspeed = 10.F;
	roll = .3F;
	wait([&]() { return status.manual_entry_status == slaaps_status_s::MANUAL_ENTRY_WAIT_CENTER; }, 1000000,
	"valid scheduled condition still requires centered selected manual input");
	roll = 0.F;
	wait([&]() { return status.manual_entry_status == slaaps_status_s::MANUAL_ENTRY_PREPARED; }, 1000000,
	"centering the same scheduled source reaches real two-bank disarmed preparation");
	slaaps_manual_permission_s scheduled_permission{};
	slaaps_manual_command_s scheduled_command{};
	check(status.manual_preparation_session > previous_session && status.manual_preparation_banks == 3
	      && !status.can_run_mask && !status.can_arm_mask && !status.manual_worker_active
	      && main.nonzeroWrites() + aux.nonzeroWrites() == scheduled_writes
	      && (!command_sub.copy(&scheduled_command) || scheduled_command.timestamp < scheduled_started)
	      && scheduled_permission_sub.copy(&scheduled_permission) && scheduled_permission.session == status.manual_preparation_session
	      && !scheduled_permission.armed && !scheduled_permission.flight_admitted && !scheduled_permission.healthy
	      && !scheduled_permission.measured_condition_valid && !scheduled_permission.commissioning_admitted,
	      "scheduled prearm success publishes no command and confers no flight or propulsion admission");
	vehicle.nav_state = vehicle.nav_state_user_intention = vehicle_status_s::NAVIGATION_STATE_MANUAL;
	wait([&]() { return status.manual_entry_status == slaaps_status_s::MANUAL_ENTRY_INACTIVE; }, 1000000,
	"scheduled readiness scenario deliberately leaves before supervisor shutdown");
	publish_conditions = false;

	SlaapsNativeTest::admitted.store(false);
	auto *subject = ModuleBase::get_instance<SlaapsNativeTestSubject>(SlaapsNativeTestSubject::desc);
	check(subject != nullptr, "isolated supervisor retains its actual module descriptor");
	subject->request_stop();
	wait([&]() { return SlaapsNativeTestSubject::desc.object.load() == nullptr; }, 1000000,
	"actual supervisor unregisters and destroys its isolated source-linked instance");

	// This second real module instance exercises the native QGC parameter path.
	// Flight admission stays false, estimator publications stop, and the accepted
	// profile contains only servo/input/timing data. No test seam grants Bench.
	VehicleConfiguration bench{};
	publish_power = false;
	bench.data_revision = 2;
	bench.manual_source = c.manual_source;
	bench.manual_instance = c.manual_instance;
	bench.manual_system = c.manual_system;
	bench.manual_component = c.manual_component;
	bench.manual_age_ms = 500;
	bench.selected_status_age_us = bench.actuator_permission_age_us = 1000000;
	bench.activation_ack_timeout_us = 60000;
	bench.first_command_budget_us = 80000;
	bench.bench_gap_ms = bench.output_lease_ms = 200;
	bench.centered_enter = c.centered_enter;
	bench.centered_exit = c.centered_exit;
	bench.bench_deadzone = c.bench_deadzone;

	for (unsigned axis = 0; axis < kAxes; ++axis) {
		bench.axis[axis].input_axis = c.axis[axis].input_axis;
		bench.axis[axis].sign = c.axis[axis].sign;
	}

	for (unsigned i = 0; i < kCanards; ++i) {
		auto &servo = bench.canard[i];
		servo.minimum = c.canard[i].minimum;
		servo.maximum = c.canard[i].maximum;
		servo.slew_positive = c.canard[i].slew_positive;
		servo.slew_negative = c.canard[i].slew_negative;
		servo.pwm_negative = c.canard[i].pwm_negative;
		servo.pwm_neutral = c.canard[i].pwm_neutral;
		servo.pwm_positive = c.canard[i].pwm_positive;
		servo.maximum_deflection_deg = c.canard[i].maximum_deflection_deg;

		for (unsigned axis = 0; axis < kAxes; ++axis) { servo.bench_mix[axis] = c.canard[i].bench_mix[axis]; }
	}

	check(validate(bench, ConfigurationUse::Bench).valid()
	      && !validate(bench, ConfigurationUse::StabilizedManual).valid(),
	      "servo-only supervisor fixture needs no flight plant, controller or thrust data");
	installNativeMixerConfiguration(bench);
	const int32_t bench_scope = 0, bench_apply = 400, disabled = 0, enabled = 1, two_seconds = 2;
	const auto enable_parameter = param_find("SLA_BENCH_EN");
	const auto target_parameter = param_find("SLA_BENCH_CH");
	const auto seconds_parameter = param_find("SLA_BENCH_SEC");
	check(param_set(param_find("SLA_CFG_KIND"), &bench_scope) == 0
	      && param_set(param_find("SLA_CFG_APPLY"), &bench_apply) == 0
	      && param_set(enable_parameter, &enabled) == 0 && param_set(target_parameter, &disabled) == 0
	      && param_set(seconds_parameter, &disabled) == 0,
	      "stage native Bench controls including a deliberately retained enabled value");
	check(parameters.capture(profile, capture_duration) && parameters.remember(profile),
	      "servo-only supervisor fixture uses actual coherent accepted Bench profile");
	vehicle_armed = armed.armed = armed.prearmed = false;
	publish_estimates = false;
	roll = 0.F;
	throttle = 1.F; // Throttle is irrelevant to servo commissioning and must never acquire EDF.
	vehicle.nav_state = vehicle.nav_state_user_intention = vehicle_status_s::NAVIGATION_STATE_MANUAL;
	pump();
	const uint64_t bench_started = hrt_absolute_time();
	check(SlaapsNativeTestSubject::task_spawn(1, nullptr) == PX4_OK,
	      "actual supervisor restarts with servo-only accepted profile and no synthetic admission");
	wait([&]() {
		int32_t value = -1;
		return status.timestamp >= bench_started && status.direct_actuator_modes_configured
		       && status.configuration_valid && status.configuration_identity == fingerprint(bench)
		       && status.can_run_mask == (1U << status.mode_id[kBenchMode])
		       && param_get(enable_parameter, &value) == 0 && value == 0;
	}, 3000000, "Bench becomes selectable before prearm, input entry, duration or enable; startup clears retained enable");
	check(!status.can_arm_mask && status.unavailable_reason == slaaps_status_s::AVAILABLE_SERVO_BENCH
	      && !status.bench_worker_active && !status.manual_worker_active && !status.input_metadata_valid
	      && status.manual_preparation_session == 0 && !SlaapsNativeTest::admitted.load(),
	      "Bench availability never admits arming, flight, estimator state or automatic startup output");
	check(param_set(seconds_parameter, &two_seconds) == 0 && param_set(enable_parameter, &enabled) == 0,
	      "operator requests enable before selected Bench readiness");
	vehicle.nav_state = vehicle.nav_state_user_intention = status.mode_id[kBenchMode];
	wait([&]() { return status.bench_entry_status == slaaps_status_s::BENCH_ENTRY_REJECTED; }, 1000000,
	"premature GUI enable rejects entry instead of becoming a deferred output request");
	int32_t enable_value = -1;
	check(param_get(enable_parameter, &enable_value) == 0 && enable_value == 0 && !status.bench_worker_active
	      && !status.manual_preparation_session, "rejected early Bench enable resets the native command and creates no transaction");
	vehicle.nav_state = vehicle.nav_state_user_intention = vehicle_status_s::NAVIGATION_STATE_MANUAL;
	wait([&]() { return status.bench_entry_status == slaaps_status_s::BENCH_ENTRY_INACTIVE; }, 1000000,
	"fresh disarmed Bench exit permits a new deliberate selection");
	vehicle.nav_state = vehicle.nav_state_user_intention = status.mode_id[kBenchMode];
	wait([&]() { return status.bench_entry_status == slaaps_status_s::BENCH_ENTRY_WAIT_PREARM; }, 1000000,
	"selected servo Bench remains non-actuating until actual prearmed state");
	check(!status.manual_preparation_session && !status.bench_worker_active, "selection and prearm wait cannot mint a Bench output session");
	armed.prearmed = true;
	wait([&]() { return status.bench_entry_status == slaaps_status_s::BENCH_ENTRY_READY; }, 1000000,
	"early-stop fixture first reaches actual centered Bench readiness");
	pump_aux = false;
	const unsigned before_early_prepare = main.writes() + aux.writes();
	check(param_set(enable_parameter, &enabled) == 0, "operator enables a Bench preparation with AUX readiness withheld");
	wait([&]() {
		return status.bench_entry_status == slaaps_status_s::BENCH_ENTRY_PREPARING && status.manual_preparation_session;
	}, 500000, "Bench preparation waits non-actuating for withheld AUX readiness");
	const auto early_session = status.manual_preparation_session;
	check(main.writes() + aux.writes() == before_early_prepare && !status.bench_worker_active,
	      "pending Bench readiness produces no physical callback before explicit activation");
	check(param_set(enable_parameter, &disabled) == 0, "operator may stop Bench before activation is consumed");
	wait([&]() { return status.bench_entry_status == slaaps_status_s::BENCH_ENTRY_COMPLETE && !status.bench_worker_active; },
	1000000, "GUI disable safely cancels unconsumed Bench preparation");
	check(status.configuration_valid && status.apply_counter == bench_apply,
	      "early Bench stop preserves the unchanged inactive profile without requiring consumed activation");
	pump_aux = true;
	vehicle.nav_state = vehicle.nav_state_user_intention = vehicle_status_s::NAVIGATION_STATE_MANUAL;
	wait([&]() { return status.bench_entry_status == slaaps_status_s::BENCH_ENTRY_INACTIVE; }, 1000000,
	"stopped preparation requires deliberate disarmed exit before another enable");
	vehicle.nav_state = vehicle.nav_state_user_intention = status.mode_id[kBenchMode];

	uint64_t bench_previous_session = early_session;
	uORB::Subscription permission_sub{ORB_ID(slaaps_manual_permission)};
	uORB::Subscription prepare_sub{ORB_ID(slaaps_manual_prepare)};

	for (unsigned scenario = 0; scenario < 3; ++scenario) {
		if (scenario) {
			vehicle.nav_state = vehicle.nav_state_user_intention = vehicle_status_s::NAVIGATION_STATE_MANUAL;
			roll = 0.F;
			publish_inputs = true;
			wait([&]() { return status.bench_entry_status == slaaps_status_s::BENCH_ENTRY_INACTIVE; }, 1000000,
			"Bench clean-stop reentry first observes a fresh disarmed mode exit");
			const int32_t target = scenario == 1 ? 2 : 0, seconds = scenario == 1 ? 1 : 2;
			check(param_set(target_parameter, &target) == 0 && param_set(seconds_parameter, &seconds) == 0,
			      "operator selects immutable Bench target and bounded duration while stopped");
			vehicle.nav_state = vehicle.nav_state_user_intention = status.mode_id[kBenchMode];
		}

		wait([&]() { return status.bench_entry_status == slaaps_status_s::BENCH_ENTRY_READY; }, 1000000,
		"post-entry centered configured raw input makes servo Bench ready with enable still zero");
		const auto ready_time = status.timestamp;
		check(!status.manual_preparation_session && !status.bench_worker_active && !status.input_metadata_valid
		      && status.apply_counter == bench_apply, "Bench readiness uses no estimator or output transaction and retains accepted profile");
		check(param_set(enable_parameter, &enabled) == 0, "fresh post-ready GUI zero-to-one enable starts one bounded Bench session");
		slaaps_manual_activation_s bench_activation{};
		wait([&]() {
			return status.manual_preparation_session > bench_previous_session
			       && aux_activation_sub.copy(&bench_activation) && bench_activation.session == status.manual_preparation_session;
		}, 500000, "native Bench enable drives actual two-bank preparation and disarmed prearmed activation");
		const auto session = status.manual_preparation_session;
		const auto total_deadline = status.bench_session_deadline;
		bench_previous_session = session;
		pump_aux = false;
		slaaps_manual_command_s command{};
		slaaps_manual_ack_s handled_main{}, handled_aux{};
		wait([&]() {
			return command_sub.copy(&command) && command.session == session && command.sequence == 1
			       && main_ack_sub.copy(&handled_main) && handled_main.session == session
			       && status.bench_entry_status == slaaps_status_s::BENCH_ENTRY_FIRST_COMMAND;
		}, 100000, "Bench FirstCommand remains pending until every actual mixer handles the first command");
		check(!aux_ack_sub.copy(&handled_aux) || handled_aux.session != session,
		      "MAIN alone cannot report Bench Running or forge AUX handling");
		slaaps_manual_preparation_s proposal{};
		slaaps_manual_permission_s permission{};
		check(prepare_sub.copy(&proposal) && permission_sub.copy(&permission)
		      && proposal.session == session && proposal.mode == static_cast<uint8_t>(ControlMode::ServoBench)
		      && proposal.timestamp >= ready_time && session <= proposal.timestamp
		      && permission.mode == static_cast<uint8_t>(ControlMode::ServoBench) && permission.prearmed
		      && !permission.armed && permission.healthy && !permission.flight_admitted && !permission.measured_condition_valid
		      && permission.deadline <= total_deadline && !SlaapsNativeTest::admitted.load(),
		      "real Bench permission derives only from disarmed prearmed authority and preserves immutable duration without synthetic admission");
		check(command.mode == static_cast<uint8_t>(ControlMode::ServoBench)
		      && command.edf_disposition == static_cast<uint8_t>(EdfDisposition::Inhibited) && sameFloatBits(command.edf, 0.F)
		      && command.deadline <= total_deadline && !status.can_arm_mask,
		      "shared Bench transaction explicitly inhibits EDF and never permits arming");
		pump_aux = true;
		wait([&]() { return status.bench_entry_status == slaaps_status_s::BENCH_ENTRY_RUNNING && status.bench_worker_active; },
		100000, "all exact handled ACKs advance the real disarmed Bench supervisor to Running");
		roll = .4F;
		wait([&]() {
			return command_sub.copy(&command) && command.session == session && command.sequence >= 5
			       && command.canards[scenario == 1 ? 1 : 0] > .1F;
		}, 250000, "GUI-selected combined or individual Bench target reaches calibrated servo command");
		check(status.bench_session_deadline == total_deadline && command.deadline <= total_deadline
		      && command.edf_disposition == static_cast<uint8_t>(EdfDisposition::Inhibited) && sameFloatBits(command.edf, 0.F)
		      && near(aux.lastOutput(3), 0.F) && !status.manual_worker_active && !status.input_metadata_valid,
		      "sustained servo Bench preserves total deadline, exact EDF off pulse, flight denial and estimator independence");

		if (scenario == 1) {
			check(near(command.canards[0], 0.F) && near(command.canards[2], 0.F) && near(command.canards[3], 0.F),
			      "individual GUI target leaves every other canard at physical zero");

		} else {
			check(command.canards[0] > 0.F && command.canards[1] < 0.F && command.canards[2] > 0.F && command.canards[3] < 0.F,
			      "combined GUI target preserves calibrated roll-mix canard directions");
		}

		if (scenario == 0) { check(param_set(enable_parameter, &disabled) == 0, "operator GUI stop clears active Bench enable"); }

		else if (scenario == 2) { publish_inputs = false; }

		wait([&]() {
			return !status.bench_worker_active && status.bench_entry_status == (scenario == 2 ?
					slaaps_status_s::BENCH_ENTRY_REJECTED : slaaps_status_s::BENCH_ENTRY_COMPLETE);
		}, scenario == 1 ? 1200000 : 500000,
		"real Bench supervisor stops on GUI disable, immutable total duration or raw-input loss");
		check(param_get(enable_parameter, &enable_value) == 0 && enable_value == 0,
		      "every completed or faulted Bench session resets the native enable parameter");

		if (scenario != 2) {
			check(status.configuration_valid && status.apply_counter == bench_apply,
			      "ordinary GUI disable and duration stop preserve the exact accepted Bench profile");
		}

		publish_inputs = true;
		roll = 0.F;
		slaaps_manual_command_s stopped_command{};
		check(command_sub.copy(&stopped_command), "retain the terminal Bench command before attempting re-enable");
		const auto reenable_time = hrt_absolute_time();
		check(param_set(enable_parameter, &enabled) == 0, "attempt same-selection Bench enable after terminal stop");
		// Idle maintenance can skip a nominal tick because its previous timestamp
		// is taken after Run's entry time. Observe the reset instead of assuming
		// a 150 ms host scheduling deadline; output must stay revoked throughout.
		wait([&]() {
			if (status.bench_worker_active || status.manual_preparation_session != session) {
				check(false, "same-selection re-enable never revives a stopped Bench worker or replaces its session");
			}

			return status.timestamp >= reenable_time && param_get(enable_parameter, &enable_value) == 0 && enable_value == 0;
		}, 1000000, "terminal Bench owner observes and clears renewed enable without deliberate leave and reentry");
		slaaps_manual_command_s after_reenable{};
		check(command_sub.copy(&after_reenable) && after_reenable.session == stopped_command.session
		      && after_reenable.sequence == stopped_command.sequence,
		      "fresh input and another enable cannot publish another stopped Bench command");
	}

	vehicle.nav_state = vehicle.nav_state_user_intention = vehicle_status_s::NAVIGATION_STATE_MANUAL;
	wait([&]() { return status.bench_entry_status == slaaps_status_s::BENCH_ENTRY_INACTIVE; }, 1000000,
	"faulted servo Bench session clears on disarmed mode exit without fallback");
	subject = ModuleBase::get_instance<SlaapsNativeTestSubject>(SlaapsNativeTestSubject::desc);
	check(subject != nullptr, "servo Bench uses the actual scheduled module descriptor");
	subject->request_stop();
	wait([&]() { return SlaapsNativeTestSubject::desc.object.load() == nullptr; }, 1000000,
	"actual Bench supervisor unregisters and stops with native enable cleared");
	PX4_INFO("SLAAPS_SUPERVISOR_BENCH_TEST_PASS production_predicate=1 synthetic_flight_admission=0 estimator_updates=0 physical_outputs=0");

	// Armed EDF commissioning is a different accepted scope and an immutable
	// ground session. The fixture alone supplies actual arming observations;
	// production never arms and the test admission seam remains disabled.
	bench.edf_commission_max = .05F;
	bench.manual_throttle_low = -1.F;
	bench.manual_throttle_high = 1.F;
	bench.edf_entry_max = .02F;
	bench.edf_rise = 1.F;
	bench.edf_fall = 2.F;
	bench.power = c.power;
	publish_power = true;
	power_scenario = 0;
	installNativeMixerConfiguration(bench);
	const int32_t edf_scope = 2, edf_apply = 500, edf_target = 5, three_seconds = 3;
	check(param_set(param_find("SLA_CFG_KIND"), &edf_scope) == 0
	      && param_set(param_find("SLA_CFG_APPLY"), &edf_apply) == 0 && param_set(target_parameter, &edf_target) == 0
	      && param_set(seconds_parameter, &three_seconds) == 0 && param_set(enable_parameter, &disabled) == 0,
	      "operator selects explicit accepted EDF commissioning scope, channel and bounded duration");
	check(parameters.capture(profile, capture_duration) && parameters.remember(profile)
	      && parameters.capture(profile, capture_duration) && Px4ConfigurationSource::remembered(profile)
	      && profile.use == edf_scope,
	      "scope2 persistence uses coherent saved kind/apply/fingerprint markers");
	vehicle.system_id = vehicle.component_id = 1;
	vehicle_armed = armed.armed = armed.prearmed = false;
	publish_inputs = pump_aux = true;
	roll = 0.F;
	throttle = -1.F;
	pump();
	const auto edf_started = hrt_absolute_time();
	check(SlaapsNativeTestSubject::task_spawn(1, nullptr) == PX4_OK,
	      "actual source supervisor starts accepted EDF commissioning without synthetic admission or estimator");
	wait([&]() {
		return status.timestamp >= edf_started && status.configuration_valid && status.configuration_use == edf_scope
		       && status.configuration_identity == fingerprint(bench) && status.direct_actuator_modes_configured
		       && status.can_run_mask == (1U << status.mode_id[kBenchMode]);
	}, 3000000, "EDF ground profile restores and is selectable while disarmed without admitting arming");
	check(!status.can_arm_mask && !status.bench_worker_active, "restored scope2 and channel5 alone cannot authorize armed output");
	const unsigned before_flight_entry = disarm_requests;
	vehicle_armed = armed.armed = true;
	vehicle.nav_state = vehicle.nav_state_user_intention = status.mode_id[kBenchMode];
	wait([&]() { return status.bench_entry_status == slaaps_status_s::BENCH_ENTRY_REJECTED; }, 1000000,
	"Bench selection from an already armed flight state is rejected");
	check(disarm_requests == before_flight_entry && !status.manual_preparation_session && !status.bench_disarm_pending,
	      "an unowned armed entry cannot acquire commissioning disarm authority or issue airborne disarm");
	vehicle_armed = armed.armed = false;
	vehicle.nav_state = vehicle.nav_state_user_intention = vehicle_status_s::NAVIGATION_STATE_MANUAL;
	pump();
	const int32_t edf_reapply = 501;
	check(param_set(param_find("SLA_CFG_APPLY"), &edf_reapply) == 0,
	      "operator deliberately reapplies ground profile after rejected armed entry");
	wait([&]() {
		return status.configuration_valid && status.apply_counter == edf_reapply
		       && status.bench_entry_status == slaaps_status_s::BENCH_ENTRY_INACTIVE;
	}, 1000000, "fresh disarmed exit and coherent reapply permit an intentional ground commissioning entry");
	uint64_t edf_previous_session{};

	for (unsigned scenario = 0; scenario < 5; ++scenario) {
		if (scenario) {
			vehicle.nav_state = vehicle.nav_state_user_intention = vehicle_status_s::NAVIGATION_STATE_MANUAL;
			throttle = -1.F;
			wait([&]() { return status.bench_entry_status == slaaps_status_s::BENCH_ENTRY_INACTIVE; }, 1000000,
			"EDF commissioning requires fresh observed disarmed leave before another session");
			const int32_t seconds = scenario == 2 ? 1 : 2;
			check(param_set(seconds_parameter, &seconds) == 0, "operator supplies the next immutable EDF commissioning duration");
		}

		vehicle.nav_state = vehicle.nav_state_user_intention = status.mode_id[kBenchMode];

		if (!scenario) {
			publish_power = false;
			wait([&]() { return status.bench_entry_status == slaaps_status_s::BENCH_ENTRY_WAIT_POWER; }, 1000000,
			"EDF selection loses readiness when the selected original battery report expires");
			check(!status.can_run_mask && !status.can_arm_mask && !status.manual_preparation_session
			      && !status.bench_worker_active && !status.power_ready,
			      "stale power cannot authorize preparation, arming or any commissioned EDF output");
			publish_power = true;
			throttle = 1.F;
			wait([&]() { return status.bench_entry_status == slaaps_status_s::BENCH_ENTRY_WAIT_THROTTLE; }, 1000000,
			"small accepted EDF ceiling cannot hide unscaled high raw throttle during supervisor entry");
			check(!status.can_arm_mask && !status.manual_preparation_session,
			      "high throttle blocks commissioning before any preparation or arming authority");
			throttle = -1.F;
		}

		wait([&]() { return status.bench_entry_status == slaaps_status_s::BENCH_ENTRY_READY; }, 1000000,
		"fresh centered low raw source makes EDF commissioning ready without requiring armed permission or prearm");
		check(!status.can_arm_mask && !status.bench_commissioning_ready && !status.bench_worker_active,
		      "low-input readiness alone still requires deliberate enable and exact bank preparation");
		check(param_set(enable_parameter, &enabled) == 0, "fresh GUI enable mints one ground-only EDF commissioning session");
		wait([&]() {
			return status.bench_entry_status == slaaps_status_s::BENCH_ENTRY_WAIT_ARM && status.bench_commissioning_ready
			       && status.manual_preparation_session > edf_previous_session
			       && status.can_arm_mask == (1U << status.mode_id[kBenchMode]);
		}, 1000000, "disarmed prepared EDF session explicitly permits only a normal operator arming action");
		const uint64_t session = status.manual_preparation_session, duration_deadline = status.bench_session_deadline;
		edf_previous_session = session;
		const auto before_arming_writes = main.writes() + aux.writes();
		slaaps_manual_permission_s edf_permission{};
		check(permission_sub.copy(&edf_permission) && edf_permission.session == session && !edf_permission.armed
		      && !edf_permission.healthy && !edf_permission.commissioning_admitted && !edf_permission.flight_admitted
		      && !status.bench_worker_active && status.bench_commissioning_ready_deadline > status.timestamp,
		      "preparation and arming readiness carry denied output permission with original input and authority expiry");
		check(status.power_ready && status.power_source_bound
		      && status.bench_commissioning_ready_deadline <= status.power_deadline,
		      "EDF arming readiness is capped by the pinned battery's original report expiry");

		if (!scenario) {
			const auto human_wait = hrt_absolute_time() + 1100000;

			while (hrt_absolute_time() < human_wait) { pump(); px4_usleep(1000); }

			check(status.bench_entry_status == slaaps_status_s::BENCH_ENTRY_WAIT_ARM && status.bench_commissioning_ready
			      && status.bench_session_deadline == duration_deadline && main.writes() + aux.writes() == before_arming_writes,
			      "operator arming may take more than one second without renewing original duration or producing any callback");
		}

		vehicle_armed = scenario != 1;
		armed.armed = scenario == 1;
		const auto staggered = hrt_absolute_time() + 60000;

		while (hrt_absolute_time() < staggered) { pump(); px4_usleep(1000); }

		check(status.bench_entry_status == slaaps_status_s::BENCH_ENTRY_WAIT_ARM && !status.bench_worker_active
		      && !status.can_arm_mask && main.writes() + aux.writes() == before_arming_writes,
		      "either staggered fresh arming order waits non-actuating until both actual publications agree");
		vehicle_armed = armed.armed = true;
		slaaps_manual_activation_s edf_activation{};
		wait([&]() { return aux_activation_sub.copy(&edf_activation) && edf_activation.session == session; }, 500000,
		"both actual armed observations permit the original prepared EDF activation");
		pump_aux = false;
		slaaps_manual_command_s commissioned{};
		wait([&]() {
			return command_sub.copy(&commissioned) && commissioned.session == session && commissioned.sequence == 1
			       && status.bench_entry_status == slaaps_status_s::BENCH_ENTRY_FIRST_COMMAND;
		}, 100000, "armed EDF commissioning begins with its exact first command pending all-bank handling");
		check(near(commissioned.edf, 0.F) && commissioned.mode == static_cast<uint8_t>(ControlMode::EdfBench)
		      && commissioned.edf_disposition == static_cast<uint8_t>(EdfDisposition::Command),
		      "the first commissioned EDF command is explicitly zero before any operator throttle increase");
		throttle = 1.F;
		pump_aux = true;
		wait([&]() { return status.bench_entry_status == slaaps_status_s::BENCH_ENTRY_EDF_RUNNING && status.bench_worker_active; },
		100000, "exact all-bank first-zero handling advances genuine commissioning supervisor to armed Running");
		wait([&]() {
			return command_sub.copy(&commissioned) && commissioned.session == session && commissioned.sequence >= 5
			       && commissioned.edf > 0.F;
		}, 250000, "new post-first-ACK raw throttle drives bounded commissioned EDF through actual callbacks");
		check(commissioned.edf <= bench.edf_commission_max && commissioned.deadline <= duration_deadline
		      && status.bench_session_deadline == duration_deadline && !status.can_arm_mask
		      && status.can_run_mask == (1U << status.mode_id[kBenchMode]) && !status.manual_worker_active
		      && !status.input_metadata_valid && permission_sub.copy(&edf_permission) && edf_permission.commissioning_admitted
		      && edf_permission.armed && !edf_permission.flight_admitted && !edf_permission.measured_condition_valid,
		      "only live ground commissioning permission permits capped EDF; flight modes and estimator admission remain absent");

		for (float canard : commissioned.canards) { check(near(canard, 0.F), "armed commissioned EDF parks all four canards at physical zero"); }

		const unsigned disarms_before_stop = disarm_requests;

		if (!scenario || scenario == 4) { check(param_set(enable_parameter, &disabled) == 0, "operator stops visibly armed EDF through native GUI enable zero"); }

		else if (scenario == 1) { publish_inputs = false; }

		else if (scenario == 3) { power_scenario = 1; }

		wait([&]() { return status.bench_disarm_pending && !status.bench_worker_active && disarm_requests > disarms_before_stop; },
		scenario == 2 ? 1200000 : 500000, "stop, original duration, input loss or power fault revokes EDF and requests ordinary Commander disarm");

		if (scenario == 3) {
			check(status.power_status == slaaps_status_s::POWER_FAULT && !status.power_ready && !status.power_deadline,
			      "active power fault remains attributable after commissioned output is canceled");
		}

		vehicle_command_ack_s accepted_ack{};
		accepted_ack.timestamp = hrt_absolute_time();
		accepted_ack.command = vehicle_command_s::VEHICLE_CMD_COMPONENT_ARM_DISARM;
		accepted_ack.result = vehicle_command_ack_s::VEHICLE_CMD_RESULT_ACCEPTED;
		check(disarm_ack_pub.publish(accepted_ack), "fixture acknowledges disarm without changing either actual arming observation");
		publish_inputs = true;
		power_scenario = 0;
		throttle = -1.F;
		const auto pending_check = hrt_absolute_time() + (scenario ? 60000 : 1200000);

		while (hrt_absolute_time() < pending_check) { pump(); px4_usleep(1000); }

		check(status.bench_disarm_pending && !status.bench_worker_active && !status.can_arm_mask && !status.can_run_mask
		      && near(aux.lastOutput(3), 0.F) && last_disarm_request >= commissioned.timestamp,
		      "ACK alone cannot complete disarm; expired or denied requests leave positive EDF inhibited");

		if (!scenario) {
			std::fprintf(stderr, "EDF_DISARM_RETRY_OBSERVATION status=%u requests=%u initial=%u last_request=%llu status_time=%llu observed=%llu\n",
				     static_cast<unsigned>(status.bench_entry_status), disarm_requests, disarms_before_stop,
				     static_cast<unsigned long long>(last_disarm_request), static_cast<unsigned long long>(status.timestamp),
				     static_cast<unsigned long long>(hrt_absolute_time()));
			check(status.bench_entry_status == slaaps_status_s::BENCH_ENTRY_DISARM_FAILED
			      && disarm_requests == disarms_before_stop + kDisarmRequestAttempts,
			      "ordinary disarm retries are bounded and remain visibly unresolved when actual arming does not change");
		}

		vehicle_armed = armed.armed = false;
		wait([&]() {
			return !status.bench_disarm_pending && status.bench_entry_status == (scenario == 1 || scenario == 3 ?
					slaaps_status_s::BENCH_ENTRY_REJECTED : slaaps_status_s::BENCH_ENTRY_COMPLETE);
		}, 1000000, "only both fresh actual disarmed observations finish the ground commissioning stop");
		check(param_get(enable_parameter, &enable_value) == 0 && enable_value == 0,
		      "commissioning completion never retains an enabled GUI command");

		if (scenario == 1 || scenario == 3) {
			const int32_t after_fault_apply = scenario == 1 ? 502 : 503;
			check(param_set(param_find("SLA_CFG_APPLY"), &after_fault_apply) == 0,
			      "input or power fault requires explicit disarmed ground-profile reapply");
			wait([&]() { return status.configuration_valid && status.apply_counter == after_fault_apply; }, 1000000,
			"coherent scope2 reapply restores profile without reviving selected faulted commissioning");
			check(status.bench_entry_status == slaaps_status_s::BENCH_ENTRY_REJECTED && !status.bench_worker_active,
			      "reapply alone cannot reset commissioning fault or deliberate reentry latch");

		} else {
			check(status.configuration_valid, "normal EDF disable or duration stop retains only exact profile after observed disarm");
		}
	}

	vehicle.nav_state = vehicle.nav_state_user_intention = vehicle_status_s::NAVIGATION_STATE_MANUAL;
	wait([&]() { return status.bench_entry_status == slaaps_status_s::BENCH_ENTRY_INACTIVE; }, 1000000,
	"completed EDF commissioning exits deliberately with no armed fallback or automatic reentry");
	subject = ModuleBase::get_instance<SlaapsNativeTestSubject>(SlaapsNativeTestSubject::desc);
	check(subject != nullptr, "commissioning uses the genuine scheduled supervisor object");
	subject->request_stop();
	wait([&]() { return SlaapsNativeTestSubject::desc.object.load() == nullptr; }, 1000000,
	"supervisor stops only after positive output revoked and genuine disarm observed");
	PX4_INFO("SLAAPS_SUPERVISOR_EDF_TEST_PASS production_predicate=1 synthetic_flight_admission=0 ordinary_disarm=1 physical_outputs=0");
	PX4_INFO("SLAAPS_SUPERVISOR_TEST_PASS checks=%u actual_source=1 synthetic_authority=1 physical_outputs=0", checks);
	return 0;
}

// Isolated native command: no Commander/supervisor or physical driver is
// running. Only authority/state inputs and the final hardware callback are fake;
// preparation, permission transport, controller, allocation and mixer are real.
static int nativeMixingOutputTests()
{
	checks = 0;
	auto c = fixture();
	c.manual_source = 1;
	c.manual_instance = -1;
	c.manual_system = c.manual_component = 0;
	c.axis[0].input_axis = 2;
	c.axis[1].input_axis = 0;
	c.axis[2].input_axis = 1;
	c.manual_throttle_low = .75F;
	c.manual_throttle_high = -.5F;
	// The actual worker/transport/mixer boundary uses a complete synthetic
	// schedule. These observations are injected test data, not real sensors.
	auto &table = c.schedule;
	table.enabled = 1;
	table.airspeed_instance = table.air_data_instance = table.battery_instance = 0;
	table.battery_id = 1;
	table.maximum_age_us = 1000000;
	table.indicated_airspeed_m_s[0] = 10.F;
	table.indicated_airspeed_m_s[1] = 30.F;
	table.edf_command[0] = 0.F;
	table.edf_command[1] = 1.F;
	table.density_min = 1.F;
	table.density_max = 1.4F;
	table.voltage_min = 20.F;
	table.voltage_max = 25.F;
	table.filter_time_s = .02F;
	table.airspeed_filter_rate = 100.F;
	table.edf_filter_rate = 10.F;
	table.airspeed_transient_rate = 1000.F;
	table.edf_transient_rate = 10.F;

	for (unsigned i = 0; i < kScheduleKnots; ++i) {
		for (unsigned j = 0; j < kScheduleKnots; ++j) {
			auto &corner = table.corner[i][j];
			const float gain = 1.F + .1F * static_cast<float>(i) + .05F * static_cast<float>(j);
			const float authority = 1.F + .25F * static_cast<float>(i) + .1F * static_cast<float>(j);

			for (unsigned axis = 0; axis < kAxes; ++axis) {
				corner.rate_p[axis] = c.axis[axis].rate_p * gain;
				corner.rate_i[axis] = c.axis[axis].rate_i * gain;
				corner.rate_d[axis] = c.axis[axis].rate_d * gain;

				for (unsigned canard = 0; canard < kCanards; ++canard) {
					corner.moment_derivative[axis][canard] = c.canard[canard].moment_derivative[axis] * authority;
				}
			}
		}
	}

	check(validate(c, ConfigurationUse::StabilizedManual).valid(), "actual mixer fixture has an explicit non-cancelling control schedule");
	installNativeMixerConfiguration(c);
	const int32_t manual_scope = 1, apply = 200;
	check(param_set(param_find("SLA_CFG_KIND"), &manual_scope) == 0
	      && param_set(param_find("SLA_CFG_APPLY"), &apply) == 0, "select explicit synthetic manual profile");
	Px4ConfigurationSource parameters;
	ParameterSnapshot profile{};
	uint32_t capture_duration{};
	check(parameters.initialize() && parameters.capture(profile, capture_duration) && parameters.remember(profile),
	      "actual configuration source accepts synthetic output-test profile");
	// A stock provider makes the ordinary mixer output path live even though
	// this mixed routing is forbidden by SLAAPS. Exercise the bypass before the
	// first driver inventory/endpoint and again while that endpoint is inactive.
	const int32_t mixed_function = actuator_test_s::FUNCTION_SERVO1;
	check(param_set(param_find("PWM_MAIN_FUNC8"), &mixed_function) == 0,
	      "stage a forbidden mixed stock-servo and SLAAPS-function output map");
	NativeSlaapsOutput main("PWM_MAIN"), aux("PWM_AUX");
	NativePermissionPublisher authority;
	uORB::Publication<vehicle_status_s> vehicle_pub{ORB_ID(vehicle_status)};
	uORB::Publication<actuator_armed_s> armed_pub{ORB_ID(actuator_armed)};
	uORB::Publication<slaaps_status_s> supervisor_pub{ORB_ID(slaaps_status)};
	uORB::Subscription main_ack{ORB_ID(slaaps_manual_ack_main)}, aux_ack{ORB_ID(slaaps_manual_ack_aux)};
	uORB::ResearchSubscription main_ready{ORB_ID(slaaps_manual_ready_main)}, aux_ready{ORB_ID(slaaps_manual_ready_aux)};
	uORB::ResearchSubscription main_handled{ORB_ID(slaaps_manual_ack_main)}, aux_handled{ORB_ID(slaaps_manual_ack_aux)};
	uORB::ResearchSubscription main_activated{ORB_ID(slaaps_manual_active_main)}, aux_activated{ORB_ID(slaaps_manual_active_aux)};
	uORB::Subscription command_sub{ORB_ID(slaaps_manual_command)};
	EdfOwner edf;
	ManualWorkerTransport worker(edf);
	ManualPreparationCoordinator coordinator;
	check(worker.advertise() && coordinator.advertise()
	      && coordinator.advertiseTransitions(), "output test advertises real transaction endpoints");
	vehicle_status_s vehicle{};
	vehicle.nav_state = vehicle_status_s::NAVIGATION_STATE_EXTERNAL1;
	actuator_armed_s armed{};
	slaaps_status_s supervisor{};
	supervisor.configuration_valid = true;
	supervisor.configuration_use = 1;
	supervisor.configuration_identity = fingerprint(c);
	supervisor.direct_actuator_modes_configured = true;
	supervisor.mode_id[0] = vehicle.nav_state;
	supervisor.manual_command_revision = worker.commandRevision();
	supervisor.manual_permission_revision = authority.revision();
	auto publishState = [&](bool is_armed) {
		const uint64_t now = hrt_absolute_time();
		vehicle.timestamp = armed.timestamp = supervisor.timestamp = now;
		vehicle.arming_state = is_armed ? vehicle_status_s::ARMING_STATE_ARMED : vehicle_status_s::ARMING_STATE_DISARMED;
		armed.armed = is_armed;
		check(vehicle_pub.publish(vehicle) && armed_pub.publish(armed) && supervisor_pub.publish(supervisor),
		      "publish coherent labeled synthetic local authority state");
	};
	publishState(false);
	main.mixing().updateSubscriptions(false);
	uORB::Publication<actuator_test_s> actuator_test_pub{ORB_ID(actuator_test)};
	actuator_test_s test{};
	test.timestamp = hrt_absolute_time();
	test.function = actuator_test_s::FUNCTION_SERVO1;
	test.action = actuator_test_s::ACTION_DO_CONTROL;
	test.value = .8F;
	test.timeout_ms = 1000;
	check(actuator_test_pub.publish(test), "publish synthetic stock test targeting the forbidden mixed-map servo");
	const unsigned unauthorized_main_writes = main.writes(), unauthorized_aux_writes = aux.writes();
	main.pump(); aux.pump();
	check(main.writes() == unauthorized_main_writes && aux.writes() == unauthorized_aux_writes,
	      "dedicated SLAAPS banks reject stock actuator-test fallback before their first inventory report");

	for (unsigned i = 0; i < 8; ++i) { main.pump(); aux.pump(); px4_usleep(20000); }

	OutputRoutingObservation observation;
	ReportedRoutingSnapshot snapshot{};
	uint64_t observed = hrt_absolute_time();
	RoutingPlan invalid_plan{};
	check(observation.captureLive(observed, snapshot) == InventoryFault::None
	      && makeRoutingPlan(c, snapshot.inventory, true, invalid_plan).fault == RoutingFault::UnsupportedFunction,
	      "actual inventory exposes the forbidden stock function rather than hiding the mixed mapping");
	test.timestamp = hrt_absolute_time();
	test.function = kCanardFunctionFirst;
	check(actuator_test_pub.publish(test), "repeat stock actuator test after bank endpoint advertisement");
	main.pump(); aux.pump();
	check(main.mixing().slaapsOutputStatus() == SlaapsOutputStatus::Inactive
	      && aux.mixing().slaapsOutputStatus() == SlaapsOutputStatus::Inactive
	      && main.writes() == unauthorized_main_writes && aux.writes() == unauthorized_aux_writes,
	      "inactive SLAAPS endpoints retain exclusive ownership and make no unauthorised PWM callback");
	test.timestamp = hrt_absolute_time();
	test.action = actuator_test_s::ACTION_RELEASE_CONTROL;
	const int32_t disabled_function = 0;
	check(actuator_test_pub.publish(test) && param_set(param_find("PWM_MAIN_FUNC8"), &disabled_function) == 0,
	      "release synthetic stock test and restore the accepted SLAAPS-only function mapping");

	for (unsigned i = 0; i < 8; ++i) { main.pump(); aux.pump(); px4_usleep(20000); }

	const ReportedRoutingPolicy policy{200000, kMaximumOutputReportAgeUs};
	research_stabilized_manual::ManualSample manual{};
	manual.valid = true;
	manual.source = research_actuator_command::ManualControlSource::Rc;
	research_stabilized_manual::StateSample state{};
	state.valid = true;
	state.source_epoch = 1;
	OperatingConditionSample condition{};
	float condition_airspeed = 10.F;
	auto stamp_condition = [&](uint64_t now) {
		condition.observed_us = condition.airspeed_timestamp_us = condition.airspeed_sample_us
					= condition.air_data_timestamp_us = condition.air_data_sample_us = condition.battery_timestamp_us = now;
		condition.deadline_us = now + static_cast<uint64_t>(c.schedule.maximum_age_us) + 1;
		condition.indicated_airspeed_m_s = condition_airspeed;
		condition.density_kg_m3 = 1.2F;
		condition.voltage_v = 22.F;
		condition.source.airspeed_revision = 31;
		condition.source.air_data_revision = 32;
		condition.source.battery_revision = 33;
		condition.source.baro_device_id = 456;
		condition.source.baro_calibration_count = 2;
		condition.source.temperature_source = 1;
		condition.source.battery_id = 1;
		condition.source.battery_source = battery_status_s::SOURCE_POWER_MODULE;
	};
	auto stamp = [&](uint64_t now) {
		manual.timestamp_us = manual.timestamp_sample_us = now;
		state.attitude_timestamp_us = state.attitude_sample_us = state.rate_timestamp_us = state.rate_sample_us = now;
		stamp_condition(now);
	};

	for (unsigned session_index = 0; session_index < 10; ++session_index) {
		publishState(false);
		main.pump(); aux.pump();
		const uint64_t start = hrt_absolute_time();
		check(observation.capture(start, snapshot) == InventoryFault::None, "capture actual mixer inventory for output transaction");
		ManualOutputBinding binding{start, {snapshot.driver[0], snapshot.driver[1]}, 3};
		ManualPreparationRequest request{start, start + (session_index == 3 ? 20000 : 1000000), start, 17, fingerprint(c), worker.commandRevision(),
			{main_handled.revision(), aux_handled.revision()}, binding, authority.revision()};
		const unsigned ready_revisions[2] {main_ready.revision(), aux_ready.revision()};
		check(coordinator.beginObserved(start, c, snapshot, InventoryFault::None, policy, request, ready_revisions),
		      "real mixer output session starts with observed disarmed preparation");
		PreparationStatus prepared = PreparationStatus::Waiting;

		for (unsigned attempt = 0; attempt < 30 && prepared == PreparationStatus::Waiting; ++attempt) {
			main.pump(); aux.pump();
			const uint64_t now = hrt_absolute_time();
			const auto captured = observation.capture(now, snapshot);
			prepared = coordinator.pollObserved(now, c, snapshot, captured);

			if (prepared == PreparationStatus::Waiting) { px4_usleep(2000); }
		}

		check(prepared == PreparationStatus::Ready, "actual MAIN and AUX mixers complete preparation");
		manual.axes[0] = manual.axes[1] = manual.axes[2] = 0.F;
		manual.axes[3] = c.manual_throttle_low;
		condition_airspeed = 10.F;
		uint64_t first_sample = hrt_absolute_time();
		stamp(first_sample);
		const unsigned grant_sequence = session_index == 2 ? 2 : 1;

		if (session_index == 2) {
			auto denied = permission(first_sample);
			denied.session = request.session;
			denied.configuration = request.configuration;
			denied.armed = denied.flight_admitted = denied.healthy = denied.measured_condition_valid = false;
			slaaps_manual_permission_s denied_wire{};
			check(encodeManualPermission(first_sample, {denied, 1}, 100000, denied_wire)
			      && authority.publication.publish(denied_wire), "retain explicit disarmed denial before local arming");
		}

		px4_usleep(1000);

		if (session_index < 2) {
			const uint64_t now = hrt_absolute_time();
			vehicle.timestamp = armed.timestamp = supervisor.timestamp = now;
			vehicle.arming_state = session_index == 0 ? vehicle_status_s::ARMING_STATE_ARMED : vehicle_status_s::ARMING_STATE_DISARMED;
			armed.armed = session_index == 1;
			const bool published = session_index == 0 ? vehicle_pub.publish(vehicle) : armed_pub.publish(armed);
			check(published && supervisor_pub.publish(supervisor),
			      "publish only one local armed message while its independently fresh peer still reports disarmed");

		} else {
			publishState(true);
		}

		auto pending_without_output = [&]() {
			const unsigned main_writes = main.writes(), aux_writes = aux.writes();
			main.pump(); aux.pump();
			check(main.mixing().slaapsOutputStatus() == SlaapsOutputStatus::Waiting
			      && aux.mixing().slaapsOutputStatus() == SlaapsOutputStatus::Waiting
			      && main.writes() == main_writes && aux.writes() == aux_writes,
			      "armed prepared banks wait for first usable grant without any physical callback");
			slaaps_manual_ack_s handled{};
			check((!main_ack.copy(&handled) || handled.session != request.session)
			      && (!aux_ack.copy(&handled) || handled.session != request.session),
			      "waiting for first grant cannot acknowledge PWM handling");
			slaaps_manual_activation_s activated{};
			unsigned revision{};
			check((!main_activated.copyBound(&activated, revision) || activated.session != request.session)
			      && (!aux_activated.copyBound(&activated, revision) || activated.session != request.session),
			      "first-grant waiting cannot acknowledge activation prematurely");
		};
		pending_without_output();

		if (session_index == 2) {
			px4_usleep(101000);
			pending_without_output();
			first_sample = hrt_absolute_time();
			stamp(first_sample);
			px4_usleep(1000);
		}

		uint64_t activation_start = hrt_absolute_time();
		const unsigned active_revisions[2] {main_activated.revision(), aux_activated.revision()};
		const bool deferred_activation = session_index == 1 || session_index >= 4;

		if (deferred_activation) {
			check(observation.capture(activation_start, snapshot) == InventoryFault::None
			      && coordinator.beginActivationObserved(activation_start, c, snapshot, InventoryFault::None, active_revisions,
					      activation_start + 6000, 5000), "publish exact ACTIVATE before first usable permission");
			pending_without_output();
			px4_usleep(500);
			pending_without_output();
		}

		if (session_index >= 3) {
			if (session_index == 3) { px4_usleep(22000); }

			if (session_index == 4) {
				using PermissionPublication = uORB::Publication<slaaps_manual_permission_s>;
				authority.publication.~PermissionPublication();
				new (&authority.publication) PermissionPublication{ORB_ID(slaaps_manual_permission)};
				check(authority.publication.advertise(), "replace first-grant permission publisher with a new real lifetime");
				supervisor.manual_permission_revision = authority.revision();
			}

			if (session_index == 5) { coordinator.cancelActivation(hrt_absolute_time()); }

			if (session_index >= 6 && session_index <= 8) {
				armed.kill = session_index == 6;
				armed.lockdown = session_index == 7;
				armed.termination = session_index == 8;
				armed.timestamp = hrt_absolute_time();
				check(armed_pub.publish(armed), "publish actual local inhibit during first-grant waiting");
			}

			if (session_index == 9) {
				auto denied = permission(hrt_absolute_time());
				denied.session = request.session;
				denied.configuration = request.configuration;
				denied.flight_admitted = denied.healthy = denied.measured_condition_valid = false;
				slaaps_manual_permission_s denied_wire{};
				check(encodeManualPermission(denied.timestamp_us, {denied, 1}, 100000, denied_wire)
				      && authority.publication.publish(denied_wire), "armed denial is terminal instead of a pending prearm sample");
			}

			main.pump(); aux.pump();
			const bool inhibited = session_index >= 6 && session_index <= 8;
			check(main.mixing().slaapsOutputStatus() == (inhibited ? SlaapsOutputStatus::Inhibited : SlaapsOutputStatus::RecoveryRequired)
			      && aux.mixing().slaapsOutputStatus() == (inhibited ? SlaapsOutputStatus::Inhibited : SlaapsOutputStatus::RecoveryRequired),
			      "expiry publisher replacement cancellation actual inhibition or armed denial rejects first-grant wait");
			armed.kill = armed.lockdown = armed.termination = false;
			publishState(true);
			auto late = permission(hrt_absolute_time());
			late.session = request.session;
			late.configuration = request.configuration;
			slaaps_manual_permission_s late_wire{};
			check(encodeManualPermission(late.timestamp_us, {late, session_index == 9 ? 2U : 1U}, 100000, late_wire)
			      && authority.publication.publish(late_wire), "publish positive grant after rejected first-grant wait");
			const unsigned main_writes = main.writes(), aux_writes = aux.writes();
			main.pump(); aux.pump();
			check(main.mixing().slaapsOutputStatus() == SlaapsOutputStatus::RecoveryRequired
			      && aux.mixing().slaapsOutputStatus() == SlaapsOutputStatus::RecoveryRequired
			      && main.writes() == main_writes && aux.writes() == aux_writes,
			      "late first grant and retained ACTIVATE cannot revive a rejected or inhibited session");
			slaaps_manual_ack_s untouched{};
			check((!main_ack.copy(&untouched) || untouched.session != request.session)
			      && (!aux_ack.copy(&untouched) || untouched.session != request.session),
			      "failed first-grant waiting never produces a handled acknowledgment");
			coordinator.cancelActivation(hrt_absolute_time());
			publishState(false);
			main.pump(); aux.pump();
			continue;
		}

		activation_start = hrt_absolute_time();
		auto grant = permission(activation_start);
		grant.session = request.session;
		grant.configuration = request.configuration;
		slaaps_manual_permission_s permission_wire{};
		check(encodeManualPermission(activation_start, {grant, grant_sequence}, 100000, permission_wire)
		      && authority.publication.publish(permission_wire), "publish independently bound synthetic flight permission");

		if (!deferred_activation) {
			check(observation.capture(activation_start, snapshot) == InventoryFault::None
			      && coordinator.beginActivationObserved(activation_start, c, snapshot, InventoryFault::None, active_revisions,
					      activation_start + 6000, 5000), "request actual mixer activation after preparation");
		}

		if (session_index < 2) {
			pending_without_output();
			publishState(true);
		}

		main.pump(); aux.pump();
		uint64_t now = hrt_absolute_time(), first_deadline{};
		check(observation.capture(now, snapshot) == InventoryFault::None
		      && coordinator.pollActivationObserved(now, c, snapshot, InventoryFault::None) == ActivationStatus::Ready
		      && coordinator.consumeActivation(now, first_deadline), "both actual mixers activate before first command");
		slaaps_manual_ack_s ack{};
		check((!main_ack.copy(&ack) || ack.session != request.session)
		      && (!aux_ack.copy(&ack) || ack.session != request.session), "activation alone never acknowledges PWM handling");
		state.attitude_timestamp_us = state.attitude_sample_us = state.rate_timestamp_us = state.rate_sample_us = now;
		stamp_condition(now); // Preserve the earlier asynchronous raw manual sample.
		const unsigned handled_revisions[2] {request.acknowledgment_revisions[0], request.acknowledgment_revisions[1]};
		check(request.session <= first_sample && first_sample < grant.timestamp_us && grant.timestamp_us < now
		      && worker.begin(now, c, grant, manual, state, binding, request.command_revision, handled_revisions,
				      &condition) == ManualWorkerStatus::Ready,
		      "real worker admits a post-session raw sample captured before permission bank activation and entry decision");

		for (unsigned cycle = 1; cycle <= 3; ++cycle) {
			px4_usleep(c.cycle_min_us + 200);
			now = hrt_absolute_time();
			state.attitude_timestamp_us = state.attitude_sample_us = state.rate_timestamp_us = state.rate_sample_us = now;
			condition_airspeed = 10.F + static_cast<float>(cycle);
			stamp_condition(now);

			if (cycle > 1) {
				manual.timestamp_us = manual.timestamp_sample_us = now;
				manual.axes[2] = .3F; // Raw yaw is the configured body-roll input.
				manual.axes[3] = c.manual_throttle_low + .1F * (c.manual_throttle_high - c.manual_throttle_low);
			}

			check(worker.cycle(now, grant, manual, state, binding, &condition) == ManualWorkerStatus::Command
			      && worker.scheduleStatus() == ControlScheduleStatus::Ready,
			      "scheduled controller allocator and shared EDF owner publish changing-IAS combined output");
			slaaps_manual_command_s wire{};
			ManualWorkerOutput frame{};
			check(command_sub.copy(&wire) && decodeManualCommand(now, wire, frame) && frame.sequence == cycle
			      && frame.deadline_us <= condition.deadline_us,
			      "inspect real combined command for physical channel comparison");
			check(cycle == 1 ? near(frame.canards[0], 0.F) && near(frame.canards[1], 0.F)
			      && near(frame.canards[2], 0.F) && near(frame.canards[3], 0.F)
			      : frame.canards[0] > 0.F && frame.canards[1] < 0.F && frame.canards[2] > 0.F && frame.canards[3] < 0.F,
			      "permuted RC yaw produces the expected body-roll allocator response through actual worker");

			if (cycle == 1) {
				check(frame.edf.request_timestamp_us == first_sample
				      && frame.edf.deadline_us == first_sample + static_cast<uint64_t>(c.output_lease_ms) * 1000,
				      "both actual mixer banks handle the first command with its original pre-activation raw timestamp and expiry");
			}

			check(cycle < 3 ? near(frame.edf.normalized, 0.F) : frame.edf.normalized > 0.F,
			      "reversed RC low endpoint starts stopped and later requests forward slewed EDF thrust");
			main.expectAcknowledgment(request.session, cycle); aux.expectAcknowledgment(request.session, cycle);
			const bool fail_aux = session_index == 0 && cycle == 3;
			const bool revoke_main = session_index == 2 && cycle == 3;

			if (revoke_main) {
				auto denied = grant;
				denied.timestamp_us = hrt_absolute_time();
				denied.deadline_us = denied.timestamp_us + 100000;
				denied.flight_admitted = false;
				slaaps_manual_permission_s denied_wire{};
				check(encodeManualPermission(denied.timestamp_us, {denied, grant_sequence + 1}, 100000, denied_wire),
				      "encode callback-time authority revocation");
				main.revokeDuringNextWrite(authority.publication, denied_wire);
			}

			if (fail_aux) { aux.failNextWrite(); }

			main.pump(); aux.pump();
			check(!main.prematureAcknowledgment() && !aux.prematureAcknowledgment(),
			      "handled replies are absent inside physical callback before success return");

			if (revoke_main) {
				check(!main_ack.copy(&ack) || ack.session != request.session || ack.sequence != cycle,
				      "callback-time permission revocation prevents successful MAIN acknowledgment");

			} else {
				check(main_ack.copy(&ack) && ack.session == request.session && ack.sequence == cycle
				      && ack.accepted >= main.lastWriteTime(), "MAIN replies only after actual callback success");
			}

			if (fail_aux || revoke_main) {
				check(!aux_ack.copy(&ack) || ack.session != request.session || ack.sequence != cycle,
				      "partial bank write failure produces no false AUX handled reply");

			} else {
				check(aux_ack.copy(&ack) && ack.session == request.session && ack.sequence == cycle
				      && ack.accepted >= aux.lastWriteTime(), "AUX replies only after actual callback success");
			}

			for (unsigned logical = 0; logical < kCanards; ++logical) {
				if (revoke_main && logical % 2 == 0) { continue; }

				const auto &servo = c.canard[logical];
				const float value = frame.canards[logical];
				const float pulse = servo.pwm_neutral + std::fabs(value) * ((value < 0.F ? servo.pwm_negative : servo.pwm_positive) - servo.pwm_neutral);
				const auto &captured = logical % 2 ? main : aux;
				check(near(captured.lastOutput(7 - logical), pulse), "actual callback receives calibrated mapped canard PWM");
			}

			if (!revoke_main) {
				check(near(aux.lastOutput(3), 1000.F + frame.edf.normalized * 1000.F), "actual callback receives shared EDF owner's mapped PWM");
			}

			if (!fail_aux && !revoke_main) {
				const unsigned main_writes = main.writes(), aux_writes = aux.writes();
				main.pump(); aux.pump();
				check(main.writes() == main_writes && aux.writes() == aux_writes, "retained handled command is not written or acknowledged twice");
			}
		}

		if (session_index == 1) {
			const unsigned main_before = main.writes(), aux_before = aux.writes();
			armed.armed = false;
			armed.timestamp = hrt_absolute_time();
			check(armed_pub.publish(armed), "publish armed disagreement after an independently accepted positive grant");
			main.pump(); aux.pump();
			check(main.mixing().slaapsOutputStatus() == SlaapsOutputStatus::RecoveryRequired
			      && aux.mixing().slaapsOutputStatus() == SlaapsOutputStatus::RecoveryRequired
			      && main.writes() == main_before && aux.writes() == aux_before,
			      "post-grant arming disagreement is terminal and grants no invented stop or replacement command");
			publishState(true);
			main.pump(); aux.pump();
			check(main.writes() == main_before && aux.writes() == aux_before,
			      "restoring armed agreement cannot revive a faulted output session");
		}

		const unsigned main_writes = main.writes(), aux_writes = aux.writes();
		px4_usleep(c.cycle_max_us + 1000);
		now = hrt_absolute_time();
		main.pump(); aux.pump();
		check(main.writes() == main_writes && aux.writes() == aux_writes, "expired armed output authorizes no further physical writes");
		stamp(now);
		check(worker.cycle(now, grant, manual, state, binding, &condition) != ManualWorkerStatus::Command && !worker.active(),
		      "missing/expired bank completion prevents next combined command");
		coordinator.cancelActivation(now);
		worker.stop();
		publishState(false);

		if (session_index == 2) {
			slaaps_manual_ack_s before_stop{};
			const bool had_ack = main_ack.copy(&before_stop);
			main.failNextWrite();
			main.pump(); aux.pump();
			check(main.writes() == main_writes + 1
			      && main.mixing().slaapsOutputStatus() == SlaapsOutputStatus::HandlingRejected,
			      "failed actual disarm callback reports rejection and retains pending stop");
			// A retained pending stop is not authority to write with invalid state time.
			vehicle.timestamp = armed.timestamp = hrt_absolute_time() + 2000000;
			check(vehicle_pub.publish(vehicle) && armed_pub.publish(armed), "publish invalid future stop authority");
			main.pump();
			check(main.writes() == main_writes + 1, "pending stop never retries using invalid authority timestamp");
			publishState(false);
			main.pump();
			check(main.writes() == main_writes + 2 && near(main.lastOutput(6), 0.F)
			      && near(main.lastOutput(4), 0.F), "fresh disarm retries failed physical stop with disarmed PWM");
			check(main_ack.copy(&ack) == had_ack
			      && (!had_ack || (ack.session == before_stop.session && ack.sequence == before_stop.sequence
					       && ack.accepted == before_stop.accepted)), "stop handling never acknowledges a flight command");
			main.pump();
			check(main.writes() == main_writes + 2, "successfully handled disarm is not repeatedly written");
			publishState(true);
			main.pump();
			check(main.writes() == main_writes + 2, "arming again without deliberate new preparation cannot resume output");
			publishState(false);

		} else {
			main.pump(); aux.pump();
		}
	}


	// Shared servo commissioning uses the same calibrated physical routes, with
	// no aerodynamic/flight admission and no positive EDF command.
	c.mass = 0.F;
	c.airflow = 0.F;

	for (float &inertia : c.inertia) { inertia = 0.F; }

	c.activation_ack_timeout_us = 10000;
	c.first_command_budget_us = 40000;
	c.canard[0].minimum = -.2F;
	c.canard[0].maximum = .35F;
	installNativeMixerConfiguration(c);
	const int32_t bench_scope = 0, bench_apply = 201;
	check(param_set(param_find("SLA_CFG_KIND"), &bench_scope) == 0
	      && param_set(param_find("SLA_CFG_APPLY"), &bench_apply) == 0
	      && parameters.capture(profile, capture_duration) && parameters.remember(profile),
	      "apply shared servo calibration without invented mass or aerodynamic data");
	supervisor.configuration_use = 0;
	supervisor.configuration_identity = fingerprint(c);
	vehicle.nav_state = vehicle_status_s::NAVIGATION_STATE_EXTERNAL3;
	supervisor.mode_id[2] = vehicle.nav_state;
	armed.prearmed = true;
	publishState(false);

	for (unsigned i = 0; i < 8; ++i) { main.pump(); aux.pump(); px4_usleep(20000); }

	for (unsigned target = 0; target <= kCanards; ++target) {
		armed.prearmed = true;
		publishState(false);
		main.pump(); aux.pump();

		if (target == kCanards) {
			check(param_set(param_find("PWM_AUX_FUNC4"), &disabled_function) == 0,
			      "servo-only commissioning also supports an unassigned EDF output");

			for (unsigned i = 0; i < 8; ++i) { main.pump(); aux.pump(); px4_usleep(20000); }
		}

		uint64_t now = hrt_absolute_time();
		check(observation.captureLive(now, snapshot) == InventoryFault::None, "capture current shared bench routes");
		ManualOutputBinding binding{now, {snapshot.driver[0], snapshot.driver[1]}, 3};
		ManualPreparationRequest request{now, now + 1000000, now, 17, fingerprint(c), worker.commandRevision(),
			{main_handled.revision(), aux_handled.revision()}, binding, authority.revision(), ControlMode::ServoBench};
		const unsigned ready_revisions[2] {main_ready.revision(), aux_ready.revision()};
		check(coordinator.beginObserved(now, c, snapshot, InventoryFault::None, policy, request, ready_revisions),
		      "prepare actual bank endpoints in explicitly bound servo mode");
		PreparationStatus prepared = PreparationStatus::Waiting;

		for (unsigned attempt = 0; attempt < 30 && prepared == PreparationStatus::Waiting; ++attempt) {
			main.pump(); aux.pump();
			const auto captured = observation.captureLive(now, snapshot);
			prepared = coordinator.pollObservedLive(now, c, snapshot, captured);

			if (prepared == PreparationStatus::Waiting) { px4_usleep(1000); }
		}

		check(prepared == PreparationStatus::Ready, "both actual banks prepare without arming or flight data");
		unsigned grant_sequence = 0;
		auto grant = permission(hrt_absolute_time());
		grant.session = request.session;
		grant.configuration = request.configuration;
		grant.mode = ControlMode::ServoBench;
		grant.armed = grant.flight_admitted = grant.measured_condition_valid = false;
		grant.prearmed = true;
		auto publishGrant = [&]() {
			grant.timestamp_us = hrt_absolute_time();
			grant.deadline_us = grant.timestamp_us + 100000;
			slaaps_manual_permission_s wire{};
			check(encodeManualPermission(grant.timestamp_us, {grant, ++grant_sequence}, 100000, wire)
			      && authority.publication.publish(wire), "publish explicit disarmed servo grant with EDF inhibited");
		};
		publishState(false);
		publishGrant();
		const unsigned activated_revisions[2] {main_activated.revision(), aux_activated.revision()};
		check(observation.captureLive(now, snapshot) == InventoryFault::None
		      && coordinator.beginActivationObservedLive(now, c, snapshot, InventoryFault::None, activated_revisions,
				      now + 10000, 40000), "activate shared banks with bench-specific timing");
		main.pump(); aux.pump();
		uint64_t first_deadline{};
		check(observation.captureLive(now, snapshot) == InventoryFault::None
		      && coordinator.pollActivationObservedLive(now, c, snapshot, InventoryFault::None) == ActivationStatus::Ready
		      && coordinator.consumeActivation(now, first_deadline), "prearmed banks activate without a propulsion grant");
		manual.axes[0] = manual.axes[1] = manual.axes[2] = 0.F;
		stamp(now);
		check(worker.beginBench(now, c, grant, manual, binding, request.command_revision,
					request.acknowledgment_revisions, target) == ManualWorkerStatus::Ready,
		      "shared worker enables centered individual or combined commissioning");

		for (unsigned cycle = 1; cycle <= 2; ++cycle) {
			px4_usleep(21000);
			publishState(false);
			publishGrant();
			stamp(hrt_absolute_time());
			manual.axes[c.axis[0].input_axis] = .5F;
			check(worker.cycleBenchObserved(grant, manual, binding, cycle == 1 ? first_deadline : 0) == ManualWorkerStatus::Command,
			      "shared worker publishes a bounded calibrated bench frame");
			slaaps_manual_command_s wire{};
			ManualWorkerOutput frame{};
			check(command_sub.copy(&wire) && decodeManualCommand(hrt_absolute_time(), wire, frame)
			      && frame.mode == ControlMode::ServoBench && frame.edf.disposition == EdfDisposition::Inhibited
			      && near(frame.edf.normalized, 0.F), "bench wire explicitly inhibits EDF instead of disguising a flight command");
			main.expectAcknowledgment(request.session, cycle); aux.expectAcknowledgment(request.session, cycle);

			if (target == 3 && cycle == 2) { aux.failNextWrite(); }

			main.pump(); aux.pump();

			for (unsigned logical = 0; logical < kCanards; ++logical) {
				const auto &servo = c.canard[logical];
				const float value = frame.canards[logical];
				const float pulse = servo.pwm_neutral + std::fabs(value)
						    * ((value < 0.F ? servo.pwm_negative : servo.pwm_positive) - servo.pwm_neutral);
				check(value >= servo.minimum && value <= servo.maximum
				      && near((logical % 2 ? main : aux).lastOutput(7 - logical), pulse),
				      "bench physical callback uses the same authoritative routing travel and PWM conversion");

				if (target && logical + 1 != target) { check(near(value, 0.F), "individual inspection leaves other canards at physical neutral"); }
			}

			check(near(aux.lastOutput(3), 0.F), "routed or absent EDF receives no propulsion during servo commissioning");
			slaaps_manual_ack_s ack{};
			check(main_ack.copy(&ack) && ack.session == request.session && ack.sequence == cycle,
			      "MAIN acknowledges exact bench handling after callback");

			if (target == 3 && cycle == 2) {
				check(!aux_ack.copy(&ack) || ack.session != request.session || ack.sequence != cycle,
				      "partial bench callback failure never acknowledges success");

			} else {
				check(aux_ack.copy(&ack) && ack.session == request.session && ack.sequence == cycle,
				      "AUX acknowledges exact bench handling after callback");
			}
		}

		const unsigned before_stop_main = main.writes(), before_stop_aux = aux.writes();

		if (target == 0) { armed.prearmed = false; publishState(false); }

		else if (target == 1) { publishState(true); }

		else if (target == 2) { px4_usleep(101000); publishState(false); }

		else if (target == 4) { grant.armed = true; publishGrant(); }

		else { coordinator.cancelActivation(hrt_absolute_time()); }

		main.pump(); aux.pump();
		check(main.writes() == before_stop_main + 1 && aux.writes() == before_stop_aux + 1
		      && near(main.lastOutput(6), 0.F) && near(aux.lastOutput(7), 0.F) && near(aux.lastOutput(3), 0.F),
		      "prearm loss unexpected arming expiry authority denial or partial failure stops shared bench output");
		worker.stop(); coordinator.cancelActivation(hrt_absolute_time());
		armed.prearmed = true; publishState(false);
		const unsigned stopped_main = main.writes(), stopped_aux = aux.writes();
		main.pump(); aux.pump();
		check(main.writes() == stopped_main && aux.writes() == stopped_aux,
		      "restored prearm and retained bench commands cannot restart the ended session");
	}

	// Armed EDF commissioning shares these actual endpoints, but never borrows
	// flight admission or a measured thrust curve. All callbacks remain captures.
	c.edf_commission_max = .25F;
	installNativeMixerConfiguration(c);
	const int32_t edf_scope = 2, edf_apply = 202;
	check(param_set(param_find("SLA_CFG_KIND"), &edf_scope) == 0
	      && param_set(param_find("SLA_CFG_APPLY"), &edf_apply) == 0
	      && parameters.capture(profile, capture_duration) && parameters.remember(profile),
	      "accept explicit synthetic EDF commissioning scope with the shared parked-servo profile");
	supervisor.configuration_use = edf_scope;
	supervisor.configuration_identity = fingerprint(c);
	armed.prearmed = false;
	publishState(false);

	for (unsigned i = 0; i < 8; ++i) { main.pump(); aux.pump(); px4_usleep(20000); }

	for (unsigned scenario = 0; scenario < 6; ++scenario) {
		publishState(false);
		main.pump(); aux.pump();
		uint64_t now = hrt_absolute_time();
		check(observation.captureLive(now, snapshot) == InventoryFault::None, "capture EDF commissioning's mandatory propulsion route");
		ManualOutputBinding binding{now, {snapshot.driver[0], snapshot.driver[1]}, 3};
		ManualPreparationRequest request{now, now + 2000000, now, 17, fingerprint(c), worker.commandRevision(),
			{main_handled.revision(), aux_handled.revision()}, binding, authority.revision(), ControlMode::EdfBench};
		const unsigned ready_revisions[2] {main_ready.revision(), aux_ready.revision()};
		const unsigned before_prepare_main = main.writes(), before_prepare_aux = aux.writes();
		check(coordinator.beginObserved(now, c, snapshot, InventoryFault::None, policy, request, ready_revisions),
		      "prepare EDF banks while genuinely disarmed with a bounded operator arming window");
		PreparationStatus prepared = PreparationStatus::Waiting;

		for (unsigned attempt = 0; attempt < 30 && prepared == PreparationStatus::Waiting; ++attempt) {
			main.pump(); aux.pump();
			const auto captured = observation.captureLive(now, snapshot);
			prepared = coordinator.pollObservedLive(now, c, snapshot, captured);

			if (prepared == PreparationStatus::Waiting) { px4_usleep(1000); }
		}

		check(prepared == PreparationStatus::Ready && main.writes() == before_prepare_main && aux.writes() == before_prepare_aux,
		      "EDF preparation establishes readiness without writing any actuator while disarmed");
		unsigned grant_sequence = 0;
		auto grant = permission(hrt_absolute_time());
		grant.session = request.session;
		grant.configuration = request.configuration;
		grant.mode = ControlMode::EdfBench;
		grant.flight_admitted = grant.measured_condition_valid = grant.prearmed = false;
		grant.armed = grant.commissioning_admitted = true;
		auto publishGrant = [&]() {
			grant.timestamp_us = hrt_absolute_time();
			grant.deadline_us = grant.timestamp_us + 100000;
			slaaps_manual_permission_s wire{};
			check(encodeManualPermission(grant.timestamp_us, {grant, ++grant_sequence}, 100000, wire)
			      && authority.publication.publish(wire), "publish exact explicit armed commissioning permission, never flight permission");
		};
		publishState(true);
		publishGrant();
		const unsigned activated_revisions[2] {main_activated.revision(), aux_activated.revision()};
		check(observation.captureLive(now, snapshot) == InventoryFault::None
		      && coordinator.beginActivationObservedLive(now, c, snapshot, InventoryFault::None, activated_revisions,
				      now + 10000, 40000), "activate EDF banks only after both actual arming publications agree");
		main.pump(); aux.pump();
		uint64_t first_deadline{};
		check(observation.captureLive(now, snapshot) == InventoryFault::None
		      && coordinator.pollActivationObservedLive(now, c, snapshot, InventoryFault::None) == ActivationStatus::Ready
		      && coordinator.consumeActivation(now, first_deadline), "both physical endpoints acknowledge armed EDF activation");
		manual.axes[0] = manual.axes[1] = manual.axes[2] = 0.F;
		manual.axes[3] = c.manual_throttle_low;
		stamp(now);
		check(worker.beginEdfBench(now, c, grant, manual, binding, request.command_revision,
					   request.acknowledgment_revisions) == ManualWorkerStatus::Ready,
		      "shared EDF worker starts at unscaled low throttle without estimator or flight model");

		for (unsigned cycle = 1; cycle <= 4; ++cycle) {
			px4_usleep(21000);
			publishState(true);
			publishGrant();
			stamp(hrt_absolute_time());
			manual.axes[3] = cycle > 1 ? c.manual_throttle_high : c.manual_throttle_low;
			check(worker.cycleEdfBenchObserved(grant, manual, binding, cycle == 1 ? first_deadline : 0) == ManualWorkerStatus::Command,
			      "armed commissioning uses the same exact command and all-bank acknowledgment loop");
			slaaps_manual_command_s wire{};
			ManualWorkerOutput frame{};
			check(command_sub.copy(&wire) && decodeManualCommand(hrt_absolute_time(), wire, frame)
			      && frame.mode == ControlMode::EdfBench && frame.edf.disposition == EdfDisposition::Command
			      && frame.edf.normalized >= 0.F && frame.edf.normalized <= c.edf_commission_max,
			      "commissioning command remains inside the immutable normalized ceiling");

			if (cycle == 1) { check(near(frame.edf.normalized, 0.F), "first EDF frame is zero before any handling acknowledgment"); }

			if (cycle == 4) { check(frame.edf.normalized > 0.F, "new post-ACK throttle reaches the bounded EDF output after its own slew interval"); }

			for (float value : frame.canards) { check(near(value, 0.F), "EDF commissioning parks every canard at physical zero"); }

			main.expectAcknowledgment(request.session, cycle); aux.expectAcknowledgment(request.session, cycle);

			if (scenario == 5 && cycle == 4) { aux.failNextWrite(); }

			main.pump(); aux.pump();
			check(near(aux.lastOutput(3), 1000.F + frame.edf.normalized * 1000.F)
			      && near(main.lastOutput(6), 1500.F) && near(main.lastOutput(4), 1500.F)
			      && near(aux.lastOutput(7), 1500.F) && near(aux.lastOutput(5), 1500.F),
			      "actual callbacks use shared EDF PWM conversion and calibrated canard neutrals");
			slaaps_manual_ack_s ack{};
			check(main_ack.copy(&ack) && ack.session == request.session && ack.sequence == cycle,
			      "MAIN acknowledges only the exact commissioned frame after handling");

			if (scenario == 5 && cycle == 4) {
				check(!aux_ack.copy(&ack) || ack.session != request.session || ack.sequence != cycle,
				      "failed EDF physical callback cannot acknowledge handling");

			} else {
				check(aux_ack.copy(&ack) && ack.session == request.session && ack.sequence == cycle,
				      "AUX acknowledges only the exact commissioned frame after handling");
			}
		}

		const unsigned before_stop_main = main.writes(), before_stop_aux = aux.writes();

		if (scenario == 0) {
			armed.timestamp = 1; // Lost actuator authority must not strand the EDF.
			check(armed_pub.publish(armed), "publish expired actual actuator authority while still armed");
			aux.failNextWrite();

		} else if (scenario == 1) { px4_usleep(101000); publishState(true); }

		else if (scenario == 3) { grant.commissioning_admitted = false; publishGrant(); }

		else if (scenario == 4) {
			const float changed_ceiling = .2F;
			check(param_set(param_find("SLA_EDF_BMAX"), &changed_ceiling) == 0, "edit commissioning ceiling while armed without retuning");

		} else { coordinator.cancelActivation(hrt_absolute_time()); }

		main.pump(); aux.pump();
		check(main.writes() == before_stop_main + 1 && aux.writes() == before_stop_aux + 1
		      && near(main.lastOutput(6), 0.F) && near(aux.lastOutput(7), 0.F) && near(aux.lastOutput(3), 0.F),
		      "expired authority, lease, cancellation, denied permission, edit or partial failure dispatches the retained ground off frame");
		slaaps_manual_ack_s before_stop_ack{}, after_stop_ack{};
		check(aux_ack.copy(&before_stop_ack), "retain the last handled command acknowledgment for stop comparison");
		main.pump(); aux.pump();
		check(aux_ack.copy(&after_stop_ack) && memcmp(&before_stop_ack, &after_stop_ack, sizeof(before_stop_ack)) == 0,
		      "retained stop and failed-stop retry never acknowledge a positive command");

		if (scenario == 0) {
			check(aux.writes() == before_stop_aux + 2 && near(aux.lastOutput(3), 0.F),
			      "failed stop handling retries its exact off ticket even while actuator authority remains stale");
		}

		worker.stop(); coordinator.cancelActivation(hrt_absolute_time());
		check(param_set(param_find("SLA_EDF_BMAX"), &c.edf_commission_max) == 0, "restore staged ceiling only for subsequent disarmed reentry");
		publishState(true); publishGrant();
		const unsigned stopped_main = main.writes(), stopped_aux = aux.writes();
		main.pump(); aux.pump();
		check(main.writes() == stopped_main && aux.writes() == stopped_aux,
		      "restored authority and retained commissioned commands cannot revive a stopped armed session");
		publishState(false);
		main.pump(); aux.pump();
	}

	PX4_INFO("SLAAPS_MIXING_OUTPUT_TEST_PASS checks=%u synthetic_authority=1 physical_outputs=0", checks);
	return 0;
}

static int nativeMixingCommand(const char *operation)
{
	// Intentionally retained for process lifetime so stopping a scheduled fixture
	// cannot destroy an object while its queue callback is finishing.
	static NativeSlaapsOutput *main{}, *aux{};

	if (!strcmp(operation, "start")) {
		if (main || aux) { return PX4_ERROR; }

		installNativeMixerConfiguration(fixture());

		main = new NativeSlaapsOutput("PWM_MAIN");
		aux = new NativeSlaapsOutput("PWM_AUX");
		main->start(); aux->start();
		const int32_t manual = 1, apply = 100;
		check(param_set(param_find("SLA_CFG_KIND"), &manual) == 0 && param_set(param_find("SLA_CFG_APPLY"), &apply) == 0,
		      "request explicit synthetic manual configuration apply");
		PX4_INFO("SLAAPS_MIXING_STARTED synthetic=1 physical_outputs=0");
		return 0;
	}

	if (!main || !aux) { return PX4_ERROR; }

	if (!strcmp(operation, "stop")) { main->stop(); aux->stop(); return 0; }

	if (!strcmp(operation, "status")) {
		PX4_INFO("SLAAPS_MIXING_STATUS main_writes=%u aux_writes=%u nonzero=%u", main->writes(), aux->writes(),
			 main->nonzeroWrites() + aux->nonzeroWrites());
		return 0;
	}

	return PX4_ERROR;
}

// Read-only inspection of the actual supervisor endpoint; no synthetic publisher.
static int nativeSupervisorEndpointTest()
{
	uORB::Subscription status_sub{ORB_ID(slaaps_status)};
	slaaps_status_s status{};
	checks = 0;
	check(status_sub.copy(&status) && status.manual_command_revision != 0, "supervisor owns live command endpoint");
	check(!status.manual_worker_active && !status.can_run_mask && !status.can_arm_mask,
	      "endpoint advertisement does not admit worker or modes");
	check(status.schedule_interval_us == kSupervisorIdleIntervalUs, "unadmitted supervisor stays at idle cadence");
	BoundSubscription<slaaps_manual_command_s> commands{ORB_ID::slaaps_manual_command};
	check(commands.bind(status.manual_command_revision), "supervisor reports actual advertisement revision");
	slaaps_manual_command_s wire{};
	check(commands.read(wire) == BoundRead::Waiting, "unadmitted supervisor cannot inherit retained command payload");
	PX4_INFO("SLAAPS_ENDPOINT_TEST_PASS revision=%u checks=%u", status.manual_command_revision, checks);
	return 0;
}

extern "C" __EXPORT int slaaps_control_test_main(int argc, char *argv[]);
int slaaps_control_test_main(int argc, char *argv[])
{
	if (argc == 3 && strcmp(argv[1], "selected") == 0) { return nativeSelectedCommand(argv[2]); }

	if (argc == 3 && strcmp(argv[1], "mixing") == 0) { return nativeMixingCommand(argv[2]); }

	if (argc == 2 && strcmp(argv[1], "output") == 0) { return nativeMixingOutputTests(); }

	if (argc == 2 && strcmp(argv[1], "supervisor") == 0) { return nativeSupervisorOrchestrationTests(); }

	if (argc == 2 && strcmp(argv[1], "endpoint") == 0) { return nativeSupervisorEndpointTest(); }

	if (argc == 2 && strcmp(argv[1], "inputs") == 0) { nativeSelectedInputTests(); return 0; }

	const int result = runConfigurationTests();
	SlaapsNativeTest::companionTransportTests();
	nativePermissionTests();
	nativeAwaitGrantTests();
	nativePowerObservationTests();
	nativeOperatingConditionObservationTests();
	nativeTransportTests();
	nativePreparationWireTests();
	nativeActivationWireTests();
	nativeBankTransportTests();
	nativePreparedBankTests();
	nativePreparationEndpointTests<0>(13);
	nativePreparationEndpointTests<1>(1);
	nativePreparationCoordinatorTests();
	nativeInitialPreparationUnreadyTests();
	nativeRoutingObservationTests();
	nativeObservedCoordinatorTests();
	nativeActivationTransactionTests();
	nativeDeferredTransitionTests<0>();
	nativeDeferredTransitionTests<1>();
	nativeActivationCompletionTests();
	nativeWorkerTransportTests();
	nativeSelectedWorkerTests();
	nativeObservedWorkerTimingTests();
	nativeLiveReadTimingTests();
	nativeLiveHandshakeTimingTests();
	nativeDisarmedInputTests();
	PX4_INFO("SLAAPS_CONTROL_TEST_PASS checks=%u", checks);
	return result;
}
#else
int main()
{
	return runConfigurationTests();
}
#endif
