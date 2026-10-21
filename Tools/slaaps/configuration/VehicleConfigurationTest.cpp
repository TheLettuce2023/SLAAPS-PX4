#include <lib/slaaps/VehicleConfiguration.hpp>
#include <lib/slaaps/ActuatorRouting.hpp>
#include <lib/slaaps/EdfOwner.hpp>
#include <lib/slaaps/ManualEdfInput.hpp>
#include <lib/slaaps/ManualWorker.hpp>
#include <lib/slaaps/ManualOutputBank.hpp>
#include <lib/slaaps/ManualBankPreparation.hpp>
#include <lib/slaaps/ManualActivationBarrier.hpp>
#include <lib/slaaps/ObservedRoutingBinding.hpp>
#include <lib/slaaps/ManualPreparationTransport.hpp>
#include <lib/slaaps/ManualTransport.hpp>
#include <lib/slaaps/AccelerationControllerPort.hpp>
#include <lib/slaaps/ReportedRouting.hpp>
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
	c.manual_instance = 2;
	c.manual_system = 255;
	c.manual_component = 190;
	c.manual_age_ms = 100;
	c.state_age_ms = 20;
	c.state_skew_ms = 10;
	c.cycle_min_us = 4000;
	c.cycle_max_us = 12000;
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

	for (unsigned i = 0; i < kThrustPoints; ++i) { c.edf_thrust[i] = 5.F * static_cast<float>(i); }

	const float signs[3][4] = {{1.F, -1.F, 1.F, -1.F}, {1.F, 1.F, -1.F, -1.F}, {1.F, -1.F, -1.F, 1.F}};

	for (unsigned a = 0; a < kAxes; ++a) {
		auto &axis = c.axis[a];
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
	check(worker.cycle(1000, p, manual, state, output, binding) == ManualWorkerStatus::Inactive, "worker starts inactive");
	check(worker.begin(1000, c, p, manual, state, binding) == ManualWorkerStatus::Ready, "combined low centered entry");
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
	check(worker.cycle(5000, p, manual, state, output, binding, &ack) == ManualWorkerStatus::Waiting,
	      "MAIN acknowledgment alone cannot release combined worker");
	ack.bank = 1;
	ack.driver = binding.driver[1];
	check(worker.cycle(5000, p, manual, state, output, binding, &ack) == ManualWorkerStatus::NotDue,
	      "both banks acknowledged at command time without reintegration");
	stamp(9000);
	manual.axes[0] = 0.F;
	check(worker.cycle(9000, p, manual, state, output, binding) == ManualWorkerStatus::Command && near(output.edf.normalized, .004F),
	      "stick release holds attitude while actual EDF owner slews");
	worker.stop();
	check(owner.sample(9000, p).disposition == EdfDisposition::RecoveryRequired, "stop revokes propulsion without inventing airborne fallback");
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
		      && output.sequence == sequence, "ack or live health fault cannot produce a replacement");
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
	ManualPreparationRequest request{1000, 20000, 1000, p.source, p.configuration, 77, {88, 99}, binding};
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
	const ManualPreparationRequest request{1000, 20000, 1000, 11, 12, 77, {88, 99}, {42, {51, 52}, 3}};
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
	const ManualPreparationRequest request{1000, 20000, 1000, 17, fingerprint(c), 77, {88, 99}, binding};
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

static int runConfigurationTests()
{
	checks = 0;
	profileTests();
	scalarTests();
	storeTests();
	thrustTests();
	actualControllers();
	routingTests();
	manualEdfTests();
	manualWorkerTests();
	manualBankTests();
	bankPreparationTests();
	preparationWireTests();
	observedRoutingBindingTests();
	activationBarrierTests();
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
#include <lib/slaaps/ManualBankTransport.hpp>
#include <lib/slaaps/ManualPreparationEndpoint.hpp>
#include <lib/slaaps/ManualPreparationCoordinator.hpp>
#include <lib/slaaps/OutputRoutingObservation.hpp>
#include <uORB/PublicationMulti.hpp>
#include <lib/slaaps/ManualExecution.hpp>
#include <uORB/Subscription.hpp>
#if !defined(CONFIG_SLAAPS_ROUTING_TEST) || !defined(__PX4_POSIX) || defined(ENABLE_LOCKSTEP_SCHEDULER)
#error SLAAPS control fixture requires the explicit native wall-clock test target
#endif
#include <modules/research_stabilized_manual/SelectedStateInput.hpp>

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

static void nativePreparationWireTests()
{
	const ManualPreparationRequest request{1000, 20000, 1000, 11, 12, 77, {88, 99}, {42, {51, 52}, 3}};
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
		ManualPreparationRequest proposal{1000, 20000, 1000, p.source, p.configuration, revision, {main.acknowledgmentRevision(), 99}, binding};
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
	const auto c = fixture();
	const auto inventory = routingFixture();
	const ManualOutputBinding binding{42, {51, 52}, 3};
	constexpr ORB_ID ready_topic = Bank == 0 ? ORB_ID::slaaps_manual_ready_main : ORB_ID::slaaps_manual_ready_aux;
	constexpr ORB_ID ack_topic = Bank == 0 ? ORB_ID::slaaps_manual_ack_main : ORB_ID::slaaps_manual_ack_aux;

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
		check(endpoint.listen(900, proposal_lifetime.revision())
		      && endpoint.poll(900, c, inventory) == PreparationStatus::Waiting, "preparation endpoint waits for first proposal");
		auto permission_now = permission(1000);
		permission_now.configuration = fingerprint(c);
		ManualPreparationRequest request{1000, 20000, 1000, permission_now.source, permission_now.configuration,
						 command_lifetime.revision(), {88, 99}, binding};
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
			{main.acknowledgmentRevision(), aux.acknowledgmentRevision()}, binding};
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

static void nativeObservedCoordinatorTests()
{
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
			{main.acknowledgmentRevision(), aux.acknowledgmentRevision()}, binding};
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
			{main.acknowledgmentRevision(), aux.acknowledgmentRevision()}, binding};
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
			      && worker.cycle(5000, p, manual, state, binding) == ManualWorkerStatus::Waiting, "MAIN alone cannot advance worker");
			check(aux.confirmHandled(5000, p, inventory, binding, aux_output, true)
			      && worker.cycle(5000, p, manual, state, binding) == ManualWorkerStatus::NotDue, "both native replies complete first frame");
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
	if (argc == 2 && strcmp(argv[1], "endpoint") == 0) { return nativeSupervisorEndpointTest(); }

	if (argc == 2 && strcmp(argv[1], "inputs") == 0) { nativeSelectedInputTests(); return 0; }

	const int result = runConfigurationTests();
	nativeTransportTests();
	nativePreparationWireTests();
	nativeBankTransportTests();
	nativePreparedBankTests();
	nativePreparationEndpointTests<0>(13);
	nativePreparationEndpointTests<1>(1);
	nativePreparationCoordinatorTests();
	nativeRoutingObservationTests();
	nativeObservedCoordinatorTests();
	nativeActivationCompletionTests();
	nativeWorkerTransportTests();
	nativeSelectedWorkerTests();
	PX4_INFO("SLAAPS_CONTROL_TEST_PASS checks=%u", checks);
	return result;
}
#else
int main()
{
	return runConfigurationTests();
}
#endif
