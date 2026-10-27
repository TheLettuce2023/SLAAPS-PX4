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
#include <lib/slaaps/WrenchAllocator.hpp>
#include <lib/research_acceleration/TrustedTimeMap.hpp>
#include <lib/rate_control/rate_control.hpp>
#include <output_functions.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

using namespace slaaps;
bool slaapsAccelerationFeedbackTests(unsigned &checks);
bool slaapsAccelerationControllerTests(unsigned &checks);
bool slaapsEdfOwnerTransferTests(unsigned &checks, const slaaps::VehicleConfiguration &configuration);
bool slaapsEdfOwnerRecoveryTests(unsigned &checks, const slaaps::VehicleConfiguration &configuration);
slaaps::VehicleConfiguration slaapsFlightTransferConfiguration(const slaaps::VehicleConfiguration &base);
bool slaapsFlightTransferTests(unsigned &checks, const slaaps::VehicleConfiguration &configuration,
			       const slaaps::OutputInventory &inventory);
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
	c.manual_ready_wait_s = 5; // Explicit synthetic operator window, never a firmware default.
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

// Entirely synthetic physical wrench data, never vehicle measurements/defaults.
static VehicleConfiguration wrenchFixture()
{
	auto c = fixture();
	c.wrench.enabled = 1;
	c.wrench.edf_force_per_thrust[0] = 1.2F; // Installed measured gain need not have unit norm.
	c.wrench.edf_moment_per_thrust[2] = .03F;
	c.wrench.trim_force[0] = 14.F;
	c.wrench.trim_force[1] = 2.F;
	c.wrench.trim_moment[2] = .5F;

	for (unsigned axis = 0; axis < kAxes; ++axis) {
		c.wrench.force_residual_tolerance[axis] = .01F;
		c.wrench.moment_residual_tolerance[axis] = .001F;
	}

	for (unsigned canard = 0; canard < kCanards; ++canard) {
		c.canard[canard].trim = .1F * (static_cast<float>(canard) - 1.F);
		c.canard[canard].force_derivative[0] = .5F * static_cast<float>(canard + 1);
		c.canard[canard].force_derivative[1] = -.25F;
	}

	c.canard[0].deflection_slope = -c.canard[0].deflection_slope;
	return c;
}

static float wrenchComponent(const WrenchModel &model, unsigned row, const float (&command)[kWrenchInputs])
{
	float result = model.offset[row];

	for (unsigned actuator = 0; actuator < kWrenchInputs; ++actuator) {
		result += model.effectiveness[row][actuator] * command[actuator];
	}

	return result;
}

static void wrenchConfigurationTests()
{
	auto c = fixture();
	WrenchModel output{};
	output.offset[0] = 123.F;
	unsigned char sentinel[sizeof(output)];
	std::memcpy(sentinel, &output, sizeof(output));
	check(validateWrenchConfiguration(c).valid() && !makeWrenchModel(c, output)
	      && std::memcmp(sentinel, &output, sizeof(output)) == 0,
	      "disabled wrench may stage but cannot convert or mutate a caller model");
	c.wrench.enabled = 1;
	check(!validateWrenchConfiguration(c).valid() && !makeWrenchModel(c, output)
	      && validate(c, ConfigurationUse::Bench).valid() && validate(c, ConfigurationUse::StabilizedManual).valid(),
	      "incomplete enabled acceleration data cannot disable existing Manual or Bench profiles");
	c = wrenchFixture();
	check(validateWrenchConfiguration(c).valid() && makeWrenchModel(c, output),
	      "explicit physical force/moment model permits zero measured rows and non-unit installed EDF gain");
	float reference[kWrenchInputs] {};

	for (unsigned canard = 0; canard < kCanards; ++canard) { reference[canard] = c.canard[canard].trim; }

	reference[kCanards] = c.condition_thrust;

	for (unsigned axis = 0; axis < kAxes; ++axis) {
		check(near(wrenchComponent(output, axis, reference), c.wrench.trim_force[axis])
		      && near(wrenchComponent(output, axis + kAxes, reference), c.wrench.trim_moment[axis]),
		      "absolute affine model reconstructs total measured trim wrench at the declared reference thrust");

		for (unsigned canard = 0; canard < kCanards; ++canard) {
			check(near(output.effectiveness[axis][canard], c.canard[canard].force_derivative[axis] * c.canard[canard].deflection_slope)
			      && near(output.effectiveness[axis + kAxes][canard], c.canard[canard].moment_derivative[axis] * c.canard[canard].deflection_slope),
			      "one shared signed servo slope converts physical force and existing moment derivatives exactly once");
		}
	}

	for (unsigned fault = 0; fault < 6; ++fault) {
		auto bad = c;

		if (fault == 0) { bad.wrench.enabled = 2; }

		if (fault == 1) { bad.wrench.force_residual_tolerance[0] = 0.F; }

		if (fault == 2) { bad.wrench.moment_residual_tolerance[2] = 0.F; }

		if (fault == 3) { bad.wrench.edf_force_per_thrust[0] = 0.F; }

		if (fault == 4) { bad.condition_thrust = c.edf_thrust[kThrustPoints - 1] + 1.F; }

		if (fault == 5) { bad.canard[0].force_derivative[0] = std::numeric_limits<float>::quiet_NaN(); }

		const auto rejected = validateWrenchConfiguration(bad);
		const char *parameter[] {"SLA_WR_EN", "SLA_WR_FT_X", "SLA_WR_MT_Z", nullptr, "SLA_TEST_THR", "SLA_C1_DFX"};
		check(!rejected.valid() && (!parameter[fault] || (rejected.parameter && std::strcmp(rejected.parameter, parameter[fault]) == 0)),
		      "invalid wrench declarations, residuals, EDF column, reference and finite data reject with available exact parameter");
		std::memcpy(sentinel, &output, sizeof(output));
		check(!makeWrenchModel(bad, output) && std::memcmp(sentinel, &output, sizeof(output)) == 0,
		      "failed physical conversion preserves the complete destination model");
	}

	for (unsigned field = 0; field < 5; ++field) {
		auto changed = c;

		if (field == 0) { changed.canard[0].force_derivative[0] += .1F; }

		if (field == 1) { changed.wrench.trim_force[0] += .1F; }

		if (field == 2) { changed.wrench.edf_moment_per_thrust[0] += .1F; }

		if (field == 3) { changed.wrench.force_residual_tolerance[0] += .1F; }

		if (field == 4) { changed.schedule.corner[1][1].trim_force[2] += .1F; }

		check(!equal(c, changed) && fingerprint(c) != fingerprint(changed),
		      "shared profile identity binds fixed and scheduled wrench data even while a table is staged");
	}

	const float nonlinear[kThrustPoints] {0.F, 1.F, 4.F, 10.F, 20.F};

	for (unsigned i = 0; i < kThrustPoints; ++i) { c.edf_thrust[i] = nonlinear[i]; }

	float thrust = 123.F;
	check(commandToThrust(c, .625F, thrust) && near(thrust, 7.F), "forward conversion uses the actual nonlinear EDF knot segment");
	check(!commandToThrust(c, 1.1F, thrust) && near(thrust, 7.F), "invalid forward command preserves caller thrust");
	auto &table = c.schedule;
	table.enabled = 1;
	table.airspeed_instance = table.air_data_instance = table.battery_instance = 0;
	table.battery_id = 1;
	table.maximum_age_us = 100000;
	table.indicated_airspeed_m_s[0] = 10.F;
	table.indicated_airspeed_m_s[1] = 30.F;
	table.edf_command[0] = .25F;
	table.edf_command[1] = .75F;
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
			corner.trim_force[0] = 20.F + 4.F * static_cast<float>(i) + 8.F * static_cast<float>(j);
			corner.edf_force_per_thrust[0] = 1.F + .2F * static_cast<float>(i) + 2.F * static_cast<float>(j);
			corner.edf_moment_per_thrust[2] = .01F * static_cast<float>(i + j);

			for (unsigned axis = 0; axis < kAxes; ++axis) {
				corner.rate_p[axis] = c.axis[axis].rate_p;
				corner.rate_i[axis] = c.axis[axis].rate_i;
				corner.rate_d[axis] = c.axis[axis].rate_d;

				for (unsigned canard = 0; canard < kCanards; ++canard) {
					corner.moment_derivative[axis][canard] = c.canard[canard].moment_derivative[axis];
					corner.force_derivative[axis][canard] = c.canard[canard].force_derivative[axis];
				}
			}
		}
	}

	OperatingConditionSample observed{};
	observed.observed_us = observed.airspeed_timestamp_us = observed.airspeed_sample_us = observed.air_data_timestamp_us
			       = observed.air_data_sample_us = observed.battery_timestamp_us = 1000;
	observed.deadline_us = 101001;
	observed.density_kg_m3 = 1.2F;
	observed.voltage_v = 22.F;
	observed.source.airspeed_revision = observed.source.air_data_revision = observed.source.battery_revision = 1;
	observed.source.baro_device_id = 456;
	observed.source.battery_id = 1;
	observed.indicated_airspeed_m_s = 10.F;
	WrenchModel corner{};
	check(!makeWrenchModel(c, output) && makeWrenchCornerModel(c, 0, 0, corner)
	      && !makeWrenchCornerModel(c, 2, 0, output), "scheduled models cannot silently fall back to fixed data or an invalid knot");
	ControlSchedule exact;
	ControlSchedule::Step point{};
	check(exact.begin(1000, c, observed, .25F, point) && point.wrench_valid,
	      "shared schedule exposes its physical affine model at the same exact measured corner");

	for (unsigned row = 0; row < kWrenchAxes; ++row) {
		check(near(point.wrench.offset[row], corner.offset[row]), "exact schedule corner retains formed affine intercept");

		for (unsigned actuator = 0; actuator < kWrenchInputs; ++actuator) {
			check(near(point.wrench.effectiveness[row][actuator], corner.effectiveness[row][actuator]),
			      "exact corner shares physical coefficients with validated profile conversion");
		}
	}

	observed.indicated_airspeed_m_s = 20.F;
	ControlSchedule middle;
	check(middle.begin(1000, c, observed, .5F, point) && point.wrench_valid, "physical and PID schedule use one midpoint");
	float trim_contribution{};

	for (unsigned canard = 0; canard < kCanards; ++canard) {
		trim_contribution += c.canard[canard].force_derivative[0] * c.canard[canard].deflection_slope * c.canard[canard].trim;
	}

	check(near(point.wrench.effectiveness[0][kCanards], 2.1F)
	      && near(point.wrench.offset[0], 26.F - trim_contribution - 16.05F)
	      && !near(point.wrench.offset[0], 26.F - trim_contribution - 2.1F * 5.5F),
	      "midpoint interpolates formed affine offsets rather than the product of averaged varying EDF columns and reference thrusts");
	auto bounded = c;
	// This synthetic physical matrix has a scaled normal diagonal of 1.768e7;
	// stay above the solver's 0.001768 numerical-resolution floor.
	bounded.regularization = .01F;
	bounded.edf_rise = bounded.edf_fall = 100.F;
	float previous[kWrenchInputs] {}, target[kWrenchInputs] {}, requested[kWrenchAxes] {};

	for (unsigned canard = 0; canard < kCanards; ++canard) {
		previous[canard] = target[canard] = c.canard[canard].trim;
	}

	check(commandToThrust(bounded, .5F, previous[kCanards]) && commandToThrust(bounded, 1.F, target[kCanards]),
	      "schedule-bound allocation fixture distinguishes an in-envelope history from an excessive thrust request");

	for (unsigned row = 0; row < kWrenchAxes; ++row) { requested[row] = wrenchComponent(point.wrench, row, target); }

	WrenchAllocationReport limited{};
	limited.command[0] = 77.F;
	unsigned char report_sentinel[sizeof(limited)];
	std::memcpy(report_sentinel, &limited, sizeof(limited));
	float previous_command{};
	check(thrustToCommand(bounded, previous[kCanards], previous_command),
	      "scheduled allocation supplies an explicit canonical previous owner command");
	const auto bounded_result = allocateVehicleWrench(bounded, point.wrench, requested, previous, previous_command, .1F, limited);
	float allocated{};
	check((bounded_result == WrenchAllocationResult::Success
	       && thrustToCommand(bounded, limited.command[kCanards], allocated)
	       && allocated >= bounded.schedule.edf_command[0] && allocated <= bounded.schedule.edf_command[1])
	      || (bounded_result == WrenchAllocationResult::ResidualExceeded
		  && std::memcmp(report_sentinel, &limited, sizeof(limited)) == 0),
	      "high permitted EDF slew cannot allocate beyond a narrower calibrated schedule envelope or leak a rejected candidate");

	for (float outside : {.2F, .8F}) {
		check(commandToThrust(bounded, outside, previous[kCanards]), "outside-history test still uses the shared valid EDF curve");
		std::memcpy(report_sentinel, &limited, sizeof(limited));
		check(allocateVehicleWrench(bounded, point.wrench, requested, previous, outside, .1F, limited) == WrenchAllocationResult::InvalidInput
		      && std::memcmp(report_sentinel, &limited, sizeof(limited)) == 0,
		      "an accepted EDF history outside either calibrated schedule knot is rejected without report mutation");
	}

	auto broken_corner = c;
	broken_corner.schedule.corner[1][1].edf_force_per_thrust[0] = 0.F;
	std::memcpy(sentinel, &output, sizeof(output));
	check(!validateWrenchConfiguration(broken_corner).valid()
	      && !makeWrenchCornerModel(broken_corner, 0, 0, output)
	      && std::memcmp(sentinel, &output, sizeof(output)) == 0,
	      "a missing installed EDF column at any scheduled corner rejects the whole wrench model without fixed-model fallback");

	for (bool enabled : {false, true}) {
		auto staged = c;
		staged.wrench.enabled = enabled;
		staged.wrench.force_residual_tolerance[0] = 0.F;
		ControlSchedule manual;
		check(validate(staged, ConfigurationUse::StabilizedManual).valid()
		      && manual.begin(1000, staged, observed, .5F, point) && !point.wrench_valid,
		      "disabled or incomplete enabled wrench leaves Manual schedule usable but acceleration model unavailable");
	}
}

static void vehicleWrenchAllocationTests()
{
	auto c = wrenchFixture();
	c.regularization = .001F;
	WrenchModel model{};
	check(makeWrenchModel(c, model), "physical coupled allocation uses the accepted shared wrench conversion");
	float previous[kWrenchInputs] {}, requested[kWrenchAxes] {};

	for (unsigned canard = 0; canard < kCanards; ++canard) { previous[canard] = c.canard[canard].trim; }

	previous[kCanards] = c.condition_thrust;
	float previous_command{};
	check(thrustToCommand(c, previous[kCanards], previous_command), "shared allocation supplies the actual previous normalized EDF command");

	for (unsigned row = 0; row < kWrenchAxes; ++row) { requested[row] = wrenchComponent(model, row, previous); }

	WrenchAllocationReport report{};
	check(allocateVehicleWrench(c, model, requested, previous, previous_command, .1F, report) == WrenchAllocationResult::Success,
	      "coupled allocator and existing calibrated limits reproduce a physically feasible trim wrench");

	float realized[kWrenchInputs] {};
	std::memcpy(realized, report.command, sizeof(realized));
	realized[kCanards] = report.realized_edf_thrust_n;

	for (unsigned row = 0; row < kWrenchAxes; ++row) {
		check(near(report.achieved[row], wrenchComponent(model, row, realized))
		      && near(report.achieved[row], requested[row]),
		      "reported physical wrench is reconstructed from calibrated realized EDF thrust and the returned canard commands");
	}

	unsigned char sentinel[sizeof(report)];
	std::memcpy(sentinel, &report, sizeof(report));
	requested[2] = 1.F; // This synthetic model has no body-Z force authority at all.
	check(allocateVehicleWrench(c, model, requested, previous, previous_command, .1F, report) == WrenchAllocationResult::ResidualExceeded
	      && std::memcmp(sentinel, &report, sizeof(report)) == 0,
	      "unavailable force direction is rejected without pretending the six wrench rows are independently controllable");
	check(allocateVehicleWrench(c, model, requested, previous, previous_command, 0.F, report) == WrenchAllocationResult::InvalidInput
	      && std::memcmp(sentinel, &report, sizeof(report)) == 0, "invalid cycle interval preserves complete caller report");
	const float nonlinear[kThrustPoints] {0.F, 1.F, 4.F, 10.F, 20.F};

	for (unsigned knot = 0; knot < kThrustPoints; ++knot) { c.edf_thrust[knot] = nonlinear[knot]; }

	c.condition_thrust = 4.F;

	for (unsigned axis = 0; axis < kAxes; ++axis) {
		c.wrench.trim_force[axis] = axis ? 0.F : c.condition_thrust;
		c.wrench.trim_moment[axis] = 0.F;
		c.wrench.edf_force_per_thrust[axis] = axis ? 0.F : 1.F;
		c.wrench.edf_moment_per_thrust[axis] = 0.F;

		for (unsigned canard = 0; canard < kCanards; ++canard) { c.canard[canard].force_derivative[axis] = 0.F; }
	}

	check(makeWrenchModel(c, model) && commandToThrust(c, .45F, previous[kCanards]),
	      "nonlinear EDF slew fixture starts below the shared half-command curve knot");
	check(thrustToCommand(c, previous[kCanards], previous_command), "nonlinear fixture retains its explicit previous normalized command");
	float upper_thrust{};
	check(commandToThrust(c, .55F, upper_thrust), "shared calibration converts the true command-rate endpoint across a curve knot");

	for (unsigned row = 0; row < kWrenchAxes; ++row) { requested[row] = wrenchComponent(model, row, previous); }

	requested[0] = upper_thrust;
	check(allocateVehicleWrench(c, model, requested, previous, previous_command, .1F, report) == WrenchAllocationResult::Success,
	      "EDF command-slew interval crossing a nonlinear thrust knot remains physically allocatable");
	float command{};
	check(thrustToCommand(c, report.command[kCanards], command) && command > .45F && command <= .55F
	      && report.command[kCanards] <= upper_thrust,
	      "allocated thrust respects the exact nonlinear shared-curve command slew, not a single N-per-second approximation");
	std::memcpy(sentinel, &report, sizeof(report));
	requested[0] = 15.F;
	check(allocateVehicleWrench(c, model, requested, previous, previous_command, .1F, report) == WrenchAllocationResult::ResidualExceeded
	      && std::memcmp(sentinel, &report, sizeof(report)) == 0,
	      "a force beyond this same EDF command-rate window rejects without leaking an oversized target or partial report");

	// Adjacent thrust knots can quantize a valid interval of owner commands to
	// the same float N. Recovering prior command from N would fabricate .25.
	c.edf_thrust[1] = 1.F;
	c.edf_thrust[2] = std::nextafter(1.F, std::numeric_limits<float>::infinity());
	c.condition_thrust = c.wrench.trim_force[0] = 1.F;
	c.edf_rise = c.edf_fall = 1.F;
	previous_command = .30F;
	check(makeWrenchModel(c, model) && commandToThrust(c, previous_command, previous[kCanards])
	      && sameFloatBits(previous[kCanards], 1.F), "explicit owner command .30 legitimately realizes one N between adjacent float thrust knots");

	for (unsigned row = 0; row < kWrenchAxes; ++row) { requested[row] = wrenchComponent(model, row, previous); }

	float canonical{};
	check(thrustToCommand(c, previous[kCanards], canonical) && near(canonical, .25F),
	      "the plateau fixture proves thrust alone cannot recover the actual prior owner command");
	std::memcpy(sentinel, &report, sizeof(report));
	check(allocateVehicleWrench(c, model, requested, previous, previous_command, .01F, report) == WrenchAllocationResult::ArithmeticFailure
	      && std::memcmp(sentinel, &report, sizeof(report)) == 0,
	      "unrepresentable narrow owner-command slew rejects instead of silently moving .30 to inverse .25 or mutating the report");
}

static void accelerationFeedbackConfigurationTests()
{
	auto c = fixture();
	check(!c.feedback.enabled && validateAccelerationFeedbackConfiguration(c).valid(),
	      "disabled feedback has no invented gravity, latency or sample bounds");
	c.feedback.enabled = 1;
	check(!validateAccelerationFeedbackConfiguration(c).valid()
	      && validate(c, ConfigurationUse::Bench).valid() && validate(c, ConfigurationUse::StabilizedManual).valid(),
	      "incomplete enabled feedback cannot remove existing Manual and Bench model validity");
	c.feedback.maximum_age_us = 20000;
	c.feedback.maximum_bias_age_us = 50000;
	c.feedback.latency_bound_us = 1000;
	c.feedback.settling_us = 2000;
	c.feedback.maximum_specific_force_m_s2 = 50.F;
	c.feedback.maximum_acceleration_m_s2 = 20.F;
	c.feedback.gravity_m_s2 = 9.8F; // Explicit synthetic value, not a production default.
	c.feedback.imu_position[0] = .2F;
	c.feedback.imu_position[1] = -.1F;
	c.feedback.imu_position[2] = .05F;
	check(validateAccelerationFeedbackConfiguration(c).valid(),
	      "explicit feedback policy accepts native zero cutoff as disabled filtering, without declaring it applied");
	c.feedback.cutoff_hz = 30.F;
	check(validateAccelerationFeedbackConfiguration(c).valid(), "finite native filter intent is valid configuration, not producer metadata");

	for (unsigned fault = 0; fault < 19; ++fault) {
		auto bad = c;
		const char *parameter{};

		switch (fault) {
		case 0: bad.feedback.enabled = 2; parameter = "SLA_FB_EN"; break;

		case 1: bad.feedback.maximum_age_us = bad.feedback.latency_bound_us; parameter = "SLA_FB_AGE"; break;

		case 2: bad.feedback.maximum_bias_age_us = 0; parameter = "SLA_FB_BIAS_AGE"; break;

		case 3: bad.feedback.latency_bound_us = 0; parameter = "SLA_FB_LAT_US"; break;

		case 4: bad.feedback.settling_us = bad.feedback.latency_bound_us - 1; parameter = "SLA_FB_SETTLE_US"; break;

		case 5: bad.feedback.maximum_specific_force_m_s2 = 0.F; parameter = "SLA_FB_F_MAX"; break;

		case 6: bad.feedback.maximum_acceleration_m_s2 = 0.F; parameter = "SLA_FB_A_MAX"; break;

		case 7: bad.feedback.gravity_m_s2 = 0.F; parameter = "SLA_FB_GRAV"; break;

		case 8: bad.state_age_ms = 0; parameter = "SLA_STATE_AGE"; break;

		case 9: bad.state_skew_ms = 1; parameter = "SLA_STATE_SKEW"; break;

		case 10: bad.quaternion_tolerance = 0.F; parameter = "SLA_QUAT_TOL"; break;

		case 11: bad.axis[0].measured_rate_limit = 0.F; parameter = "SLA_R_MEAS_R"; break;

		case 12: bad.axis[2].measured_accel_limit = 0.F; parameter = "SLA_Y_MEAS_A"; break;

		case 13: bad.feedback.imu_position[1] = std::numeric_limits<float>::quiet_NaN(); parameter = "EKF2_IMU_POS_Y"; break;

		case 14: bad.feedback.cutoff_hz = -1.F; parameter = "IMU_ACCEL_CUTOFF"; break;

		case 15: bad.feedback.cutoff_hz = 1001.F; parameter = "IMU_ACCEL_CUTOFF"; break;

		case 16: bad.feedback.maximum_age_us = 1000001; parameter = "SLA_FB_AGE"; break;

		case 17: bad.feedback.cutoff_hz = std::numeric_limits<float>::infinity(); parameter = "IMU_ACCEL_CUTOFF"; break;

		default: bad.selected_status_age_us = 0; parameter = "SLA_STATUS_AGE"; break;
		}

		const auto result = validateAccelerationFeedbackConfiguration(bad);
		check(!result.valid() && result.parameter && std::strcmp(result.parameter, parameter) == 0,
		      "feedback timing, numerical and native-intent failures retain the exact actionable parameter");
	}

	auto boundary = c;
	boundary.feedback.settling_us = boundary.feedback.latency_bound_us;
	check(validateAccelerationFeedbackConfiguration(boundary).valid(),
	      "declared settling may equal latency while original sample age remains strictly larger");

	for (unsigned field = 0; field < 5; ++field) {
		auto changed = c;

		if (field < kAxes) { changed.feedback.imu_position[field] += .01F; }

		if (field == 3) { changed.feedback.cutoff_hz += 1.F; }

		if (field == 4) { changed.feedback.settling_us += 1; }

		check(!equal(c, changed) && fingerprint(c) != fingerprint(changed),
		      "native external position/filter intents and feedback policy participate in immutable profile identity");
	}

	auto signed_zero = fixture();
	auto negative_zero = signed_zero;
	negative_zero.feedback.imu_position[0] = -0.F;
	check(!equal(signed_zero, negative_zero) && fingerprint(signed_zero) != fingerprint(negative_zero),
	      "external native scalar identity preserves signed-zero edits without struct-padding dependence");
	negative_zero.feedback.imu_position[0] = std::numeric_limits<float>::infinity();
	check(!validate(negative_zero, ConfigurationUse::Bench).valid(),
	      "disabled feedback still rejects nonfinite native parameter intent");
	ConfigurationStore store;
	check(store.commit(signed_zero, ConfigurationUse::Bench).valid() && store.activate() && store.eligible(),
	      "external-intent invalidation test starts an eligible accepted bench calibration");
	auto edited = signed_zero;
	edited.feedback.cutoff_hz = 30.F;
	store.observe(edited);
	check(!store.eligible(), "native filter-intent edits cannot retain an accepted configuration fingerprint");
}

static void dataProvenanceTests()
{
	const VehicleConfiguration defaults{};
	check(defaults.data_schema_version == 1 && !defaults.data_record_id && !defaults.data_revision
	      && defaults.data_assessment == static_cast<int32_t>(DataAssessment::Incomplete),
	      "provenance defaults name SLAAPS profile schema1 but claim no external record or assessment");
	const auto incomplete = fixture();
	research_actuator_command::BenchOutputProfile bench{};
	check(validate(incomplete, ConfigurationUse::Bench).valid() && makeBenchProfile(incomplete, bench),
	      "incomplete assessment and unassigned record ID preserve explicitly calibrated servo commissioning");

	for (const auto use : {ConfigurationUse::Bench, ConfigurationUse::StabilizedManual, ConfigurationUse::EdfCommissioning}) {
		auto c = incomplete;

		if (use == ConfigurationUse::EdfCommissioning) {
			c.edf_commission_max = .25F;
			c.activation_ack_timeout_us = 10000;
			c.first_command_budget_us = 40000;
		}

		for (const auto state : {DataAssessment::Calibrated, DataAssessment::Qualified}) {
			c.data_record_id = 42;
			c.data_revision = 7;
			c.data_assessment = static_cast<int32_t>(state);
			check(validate(c, use).valid(), "operator-declared assessment with explicit record and revision can be accepted");
			auto missing = c;
			missing.data_record_id = 0;
			auto result = validate(missing, use);
			check(result.fault == ConfigurationFault::MissingRecord && result.parameter
			      && std::strcmp(result.parameter, "SLA_DATA_REC") == 0,
			      "calibrated or qualified assessment requires the actionable external record ID");
			missing = c;
			missing.data_revision = 0;
			result = validate(missing, use);
			check(result.fault == ConfigurationFault::MissingRecord && result.parameter
			      && std::strcmp(result.parameter, "SLA_DATA_REV") == 0,
			      "operator assessment cannot omit its external record revision");
		}

		for (unsigned invalid = 0; invalid < 6; ++invalid) {
			auto bad = c;
			const char *parameter{};

			switch (invalid) {
			case 0: bad.data_schema_version = 0; parameter = "SLA_DATA_VER"; break;

			case 1: bad.data_schema_version = 2; parameter = "SLA_DATA_VER"; break;

			case 2: bad.data_record_id = -1; parameter = "SLA_DATA_REC"; break;

			case 3: bad.data_assessment = -1; parameter = "SLA_DATA_STATE"; break;

			case 4: bad.data_assessment = 3; parameter = "SLA_DATA_STATE"; break;

			default: bad.data_revision = -1; parameter = "SLA_DATA_REV"; break;
			}

			const auto result = validate(bad, use);
			check(result.fault == ConfigurationFault::ParameterRange && result.parameter
			      && std::strcmp(result.parameter, parameter) == 0,
			      "unsupported schema, record or assessment returns its exact parameter");
		}
	}

	int32_t VehicleConfiguration::*const fields[] = {
		&VehicleConfiguration::data_schema_version, &VehicleConfiguration::data_record_id,
		&VehicleConfiguration::data_revision, &VehicleConfiguration::data_assessment
	};

	for (const auto field : fields) {
		auto changed = incomplete;
		++(changed.*field);
		check(!equal(incomplete, changed) && fingerprint(incomplete) != fingerprint(changed),
		      "each record reference and assessment field belongs to canonical profile identity");
		ConfigurationStore store;
		check(store.commit(incomplete, ConfigurationUse::Bench).valid() && store.activate(), "bind synthetic provenance profile");
		store.observe(changed);
		store.observe(incomplete);
		check(!store.eligible(), "provenance mutation invalidates a bound profile even after reversion");
	}

	auto qualified = incomplete;
	qualified.data_record_id = 42;
	qualified.data_assessment = static_cast<int32_t>(DataAssessment::Qualified);
	EdfPermission denied{};
	denied.mode = ControlMode::Manual;
	denied.armed = denied.healthy = true;
	AccelerationControllerPort acceleration;
	check(validate(qualified, ConfigurationUse::StabilizedManual).valid()
	      && !ManualBankPreparation::permitted(denied, ControlMode::Manual)
	      && !denied.flight_admitted && !denied.measured_condition_valid && !denied.commissioning_admitted
	      && !acceleration.available() && acceleration.configure(qualified) == AccelerationPortStatus::Unavailable,
	      "operator-declared qualified record cannot grant flight permission or create an acceleration controller");
}

static void manualTimingConfigurationTests()
{
	const auto c = fixture();
	check(manualActivationTimingConfigured(c), "explicit synthetic manual timing is configured");
	auto staged = c;
	staged.selected_status_age_us = staged.actuator_permission_age_us = 0;
	staged.activation_ack_timeout_us = staged.first_command_budget_us = 0;
	staged.manual_ready_wait_s = 0;
	check(validate(staged, ConfigurationUse::StabilizedManual).valid()
	      && !manualActivationTimingConfigured(staged), "all-zero timing can stage without activation readiness");

	VehicleConfiguration timing_only{};
	timing_only.selected_status_age_us = c.selected_status_age_us;
	timing_only.actuator_permission_age_us = c.actuator_permission_age_us;
	timing_only.activation_ack_timeout_us = c.activation_ack_timeout_us;
	timing_only.first_command_budget_us = c.first_command_budget_us;
	timing_only.cycle_min_us = c.cycle_min_us;
	timing_only.cycle_max_us = c.cycle_max_us;
	timing_only.output_lease_ms = c.output_lease_ms;
	timing_only.manual_age_ms = c.manual_age_ms;
	timing_only.manual_ready_wait_s = c.manual_ready_wait_s;
	check(manualActivationTimingConfigured(timing_only)
	      && !validate(timing_only, ConfigurationUse::StabilizedManual).valid(),
	      "timing helper is independent of vehicle-model validity and grants no profile acceptance");

	int32_t VehicleConfiguration::*const timing_fields[] = {
		&VehicleConfiguration::selected_status_age_us,
		&VehicleConfiguration::actuator_permission_age_us,
		&VehicleConfiguration::activation_ack_timeout_us,
		&VehicleConfiguration::first_command_budget_us,
		&VehicleConfiguration::manual_ready_wait_s
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

	for (int32_t seconds : {1, 60}) {
		boundary = c;
		boundary.manual_ready_wait_s = seconds;
		check(manualActivationTimingConfigured(boundary) && validate(boundary, ConfigurationUse::StabilizedManual).valid(),
		      "explicit manual operator wait accepts both software bounds without changing activation budgets");
	}

	for (int32_t seconds : {-1, 0, 61}) {
		boundary = c;
		boundary.manual_ready_wait_s = seconds;
		const auto result = validate(boundary, ConfigurationUse::StabilizedManual);
		check(!manualActivationTimingConfigured(boundary) && !result.valid() && result.parameter
		      && std::strcmp(result.parameter, "SLA_MAN_WAIT") == 0,
		      "missing or out-of-range manual operator wait reports its exact parameter");
	}

	auto bench_only = c;
	bench_only.manual_ready_wait_s = 0;
	bench_only.activation_ack_timeout_us = 10000;
	bench_only.first_command_budget_us = 40000;
	bench_only.edf_commission_max = .25F;
	check(validate(bench_only, ConfigurationUse::Bench).valid() && benchActivationTimingConfigured(bench_only)
	      && validate(bench_only, ConfigurationUse::EdfCommissioning).valid(),
	      "servo and EDF commissioning do not require a manual operator wait");
	staged.manual_ready_wait_s = 5;
	check(!validate(staged, ConfigurationUse::StabilizedManual).valid(),
	      "an operator wait alone cannot make otherwise absent manual timing configured");
}

static void activationReserveConfigurationTests()
{
	auto c = fixture();
	uint64_t reserve = 17;
	check(activationReserveUs(c, reserve) && reserve == 8000,
	      "activation reserve is the complete declared reply window plus first-command budget");

	for (unsigned invalid = 0; invalid < 4; ++invalid) {
		auto bad = c;

		if (invalid < 2) { bad.activation_ack_timeout_us = invalid ? -1 : 0; }

		else { bad.first_command_budget_us = invalid == 2 ? 0 : -1; }

		check(!activationReserveUs(bad, reserve) && reserve == 8000,
		      "missing or negative reserve bounds preserve the caller's destination");
	}

	auto wide = c;
	wide.activation_ack_timeout_us = wide.first_command_budget_us = std::numeric_limits<int32_t>::max();
	check(activationReserveUs(wide, reserve)
	      && reserve == 2 * static_cast<uint64_t>(std::numeric_limits<int32_t>::max()),
	      "reserve arithmetic widens before addition without claiming metadata validity");

	// Explicit synthetic values cover a full 60ms activation window and 80ms
	// first-command reserve. They do not establish measured timing qualification.
	c.activation_ack_timeout_us = 60000;
	c.first_command_budget_us = 80000;
	c.cycle_max_us = 200000;
	c.bench_gap_ms = c.output_lease_ms = c.manual_age_ms = 200;
	c.actuator_permission_age_us = c.power.maximum_age_us = 200000;
	c.edf_commission_max = .25F;

	for (const auto use : {ConfigurationUse::StabilizedManual, ConfigurationUse::EdfCommissioning}) {
		check(validate(c, use).valid(), "synthetic propulsion timing supports the full declared activation reserve");

		for (unsigned field = 0; field < 4; ++field) {
			// Bench already requires output lease >= cycle maximum > reserve.
			if (use == ConfigurationUse::EdfCommissioning && field == 0) { continue; }

			auto bad = c;
			const char *parameter{};

			switch (field) {
			case 0: bad.output_lease_ms = 140; parameter = "SLA_OUT_LEASE"; break;

			case 1: bad.actuator_permission_age_us = 139999; parameter = "SLA_ARM_AGE"; break;

			case 2: bad.manual_age_ms = 140; parameter = "SLA_IN_AGE"; break;

			default: bad.power.maximum_age_us = 139999; parameter = "SLA_PWR_AGE"; break;
			}

			const auto result = validate(bad, use);
			check(result.fault == ConfigurationFault::InvalidTiming && result.parameter
			      && std::strcmp(result.parameter, parameter) == 0,
			      "every insufficient original lease rejects with its actionable exact parameter");
		}

		auto boundary = c;
		boundary.bench_gap_ms = 141;
		boundary.output_lease_ms = 141;
		boundary.manual_age_ms = 141;
		boundary.actuator_permission_age_us = boundary.power.maximum_age_us = 140000;
		check(validate(boundary, use).valid(),
		      "inclusive authority ages allow one microsecond beyond reserve while output and raw leases remain exclusive");
		boundary.power.maximum_age_us = 100000;
		check(validate(boundary, use).fault == ConfigurationFault::InvalidTiming,
		      "100ms battery lifetime cannot cover the declared 60ms activation and 80ms first-command windows");
	}

	c.power = {};
	check(validate(c, ConfigurationUse::Bench).valid() && benchActivationTimingConfigured(c),
	      "servo-only bench activation remains independent of propulsion battery configuration");
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
	const auto indefinite = validate(bad, ConfigurationUse::StabilizedManual);
	check(indefinite.fault == ConfigurationFault::InvalidInertia && indefinite.parameter == nullptr,
	      "indefinite full inertia rejects without incorrectly attributing the tensor error to mass");
	bad = c;
	bad.inertia[0] = 1.F;
	const auto impossible = validate(bad, ConfigurationUse::StabilizedManual);
	check(impossible.fault == ConfigurationFault::InvalidInertia && impossible.parameter == nullptr,
	      "impossible rigid-body inertia rejects without naming an unrelated scalar parameter");
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
	const auto missing_mass = validate(bad, ConfigurationUse::StabilizedManual);
	check(missing_mass.fault == ConfigurationFault::InvalidInertia && missing_mass.parameter
	      && std::strcmp(missing_mass.parameter, "SLA_MASS") == 0, "missing manual mass names the actual rejected scalar");
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

static void companionAdmissionConfigurationTests()
{
	using research_acceleration::ContractConfig;
	using research_acceleration::DisarmedPolicy;
	VehicleConfiguration c{};
	const auto &defaults = c.companion;
	check(defaults.admission_enabled == 0 && defaults.disarmed_policy == 0 && near(defaults.acceleration_norm_max, 0.F)
	      && defaults.minimum_validity_us == 0 && defaults.maximum_validity_us == 0
	      && defaults.maximum_command_age_us == 0 && defaults.maximum_future_skew_us == 0
	      && defaults.maximum_clock_uncertainty_us == 0 && defaults.maximum_relative_drift_ppm == 0
	      && defaults.timestamp_uncertainty_us == 0 && defaults.maximum_round_trip_us == 0 && defaults.map_interval_us == 0,
	      "companion admission and every required timing or clock assumption default absent, not operational");

	for (unsigned axis = 0; axis < kAxes; ++axis) {
		check(near(defaults.acceleration_minimum[axis], 0.F) && near(defaults.acceleration_maximum[axis], 0.F),
		      "companion acceleration bounds have no fabricated physical defaults");
	}

	ContractConfig destination{};
	destination.protocol_version = 0x12345678;
	destination.permitted_source = 99;
	destination.acceleration_bounds.maximum_norm_m_s2 = 19.F;
	unsigned char original[sizeof(destination)];
	std::memcpy(original, &destination, sizeof(destination));
	check(validateCompanionConfiguration(c).valid() && !companionAdmissionConfigured(c)
	      && !makeCompanionContractConfiguration(c, destination)
	      && std::memcmp(original, &destination, sizeof(destination)) == 0,
	      "disabled admission stages but cannot convert or modify a caller's existing contract");

	// Explicit synthetic transport/admission limits. This conversion requires
	// neither a vehicle plant nor a session, trusted map or output owner.
	c.companion = {1, 3, 42, 191, 16777217, 500000};
	c.companion.admission_enabled = 1;
	c.companion.disarmed_policy = 1;
	c.companion.acceleration_minimum[0] = -7.F;
	c.companion.acceleration_minimum[1] = -5.F;
	c.companion.acceleration_minimum[2] = -3.F;
	c.companion.acceleration_maximum[0] = 11.F;
	c.companion.acceleration_maximum[1] = 9.F;
	c.companion.acceleration_maximum[2] = 6.F;
	c.companion.acceleration_norm_max = 13.F;
	c.companion.minimum_validity_us = 1100;
	c.companion.maximum_validity_us = 43000;
	c.companion.maximum_command_age_us = 17000;
	c.companion.maximum_future_skew_us = 1900;
	c.companion.maximum_clock_uncertainty_us = 2300;
	c.companion.maximum_relative_drift_ppm = 51;
	c.companion.timestamp_uncertainty_us = 700;
	c.companion.maximum_round_trip_us = 3100;
	c.companion.map_interval_us = 500000;
	c.quaternion_tolerance = .015F;
	check(validateCompanionConfiguration(c).valid() && companionAdmissionConfigured(c)
	      && makeCompanionContractConfiguration(c, destination),
	      "explicit companion admission configuration converts independently of a vehicle model");
	check(destination.protocol_version == research_acceleration::kWireProtocolVersionV1
	      && destination.permitted_source == 16777217 && destination.disarmed_policy == DisarmedPolicy::Reject
	      && near(destination.acceleration_bounds.maximum_norm_m_s2, 13.F)
	      && near(destination.quaternion_norm_tolerance, .015F),
	      "contract conversion retains exact payload identity, v1 semantics, configured norm and quaternion tolerance");

	for (unsigned axis = 0; axis < kAxes; ++axis) {
		check(sameFloatBits(destination.acceleration_bounds.minimum_m_s2(axis), c.companion.acceleration_minimum[axis])
		      && sameFloatBits(destination.acceleration_bounds.maximum_m_s2(axis), c.companion.acceleration_maximum[axis]),
		      "conversion preserves signed inertial acceleration FRD components without scaling or axis remapping");
	}

	check(destination.timing.minimum_validity_us == 1100 && destination.timing.maximum_validity_us == 43000
	      && destination.timing.maximum_command_age_us == 17000 && destination.timing.maximum_future_skew_us == 1900
	      && destination.timing.maximum_clock_uncertainty_us == 2300,
	      "contract conversion copies independent microsecond timing bounds exactly without borrowing local receipt age");
	c.companion.disarmed_policy = 2;
	check(makeCompanionContractConfiguration(c, destination) && destination.disarmed_policy == DisarmedPolicy::ValidateOnly,
	      "explicit disarmed validate-only policy maps to its existing guard enum without session authority");

	auto vehicle = fixture();
	vehicle.companion = c.companion;
	vehicle.cycle_max_us = 100000;
	vehicle.first_command_budget_us = 30000;
	vehicle.activation_ack_timeout_us = 20000;
	vehicle.edf_commission_max = .1F;
	const ConfigurationUse scopes[] = {ConfigurationUse::Bench, ConfigurationUse::StabilizedManual, ConfigurationUse::EdfCommissioning};

	for (const auto use : scopes) {
		check(validate(vehicle, use).valid(), "valid companion admission limits may be accepted without changing the vehicle's mode scope");
	}

	for (unsigned fault = 0; fault < 20; ++fault) {
		auto bad = c;
		const char *parameter = nullptr;

		switch (fault) {
		case 0: bad.companion.enabled = 0; parameter = "SLA_CMP_EN"; break;

		case 1: bad.companion.admission_enabled = 2; parameter = "SLA_CMP_ADMIT"; break;

		case 2: bad.companion.disarmed_policy = 0; parameter = "SLA_CMP_DISARM"; break;

		case 3: bad.companion.disarmed_policy = 3; parameter = "SLA_CMP_DISARM"; break;

		case 4: bad.companion.acceleration_minimum[1] = 10.F; parameter = "SLA_CMP_AY_MAX"; break;

		case 5: bad.companion.acceleration_norm_max = 0.F; parameter = "SLA_CMP_A_NORM"; break;

		case 6: bad.companion.acceleration_norm_max = std::numeric_limits<float>::quiet_NaN(); parameter = "SLA_CMP_A_NORM"; break;

		case 7: bad.companion.minimum_validity_us = 0; parameter = "SLA_CMP_V_MIN"; break;

		case 8: bad.companion.maximum_validity_us = 0; parameter = "SLA_CMP_V_MAX"; break;

		case 9: bad.companion.maximum_validity_us = bad.companion.minimum_validity_us - 1; parameter = "SLA_CMP_V_MAX"; break;

		case 10: bad.companion.maximum_command_age_us = 0; parameter = "SLA_CMP_CMD_AGE"; break;

		case 11: bad.companion.maximum_future_skew_us = 0; parameter = "SLA_CMP_FUTURE"; break;

		case 12: bad.companion.maximum_clock_uncertainty_us = 0; parameter = "SLA_CMP_CLOCK_U"; break;

		case 13: bad.companion.maximum_relative_drift_ppm = 0; parameter = "SLA_CMP_DRIFT"; break;

		case 14: bad.companion.timestamp_uncertainty_us = 0; parameter = "SLA_CMP_STAMP_U"; break;

		case 15: bad.companion.maximum_round_trip_us = 0; parameter = "SLA_CMP_RTT"; break;

		case 16: bad.companion.map_interval_us = 0; parameter = "SLA_CMP_MAP_US"; break;

		case 17: bad.quaternion_tolerance = 0.F; parameter = "SLA_QUAT_TOL"; break;

		case 18: bad.quaternion_tolerance = 1.F; parameter = "SLA_QUAT_TOL"; break;

		case 19: bad.companion.source_id = 0; parameter = "SLA_CMP_SOURCE"; break;
		}

		const auto result = validateCompanionConfiguration(bad);
		check(!result.valid() && result.parameter && std::strcmp(result.parameter, parameter) == 0
		      && !companionAdmissionConfigured(bad),
		      "incomplete admission, timing or explicit clock assumptions reject with the actionable exact parameter");
		std::memcpy(original, &destination, sizeof(destination));
		check(!makeCompanionContractConfiguration(bad, destination)
		      && std::memcmp(original, &destination, sizeof(destination)) == 0,
		      "failed admission conversion leaves the whole existing destination byte-for-byte unchanged");
		vehicle.companion = bad.companion;
		vehicle.quaternion_tolerance = bad.quaternion_tolerance;

		for (const auto use : scopes) {
			check(!validate(vehicle, use).valid(), "every mode scope rejects an invalid enabled companion admission policy");
		}
	}

	auto box = c;
	box.companion.acceleration_minimum[0] = 3.F;
	box.companion.acceleration_maximum[0] = 4.F;
	box.companion.acceleration_minimum[1] = 4.F;
	box.companion.acceleration_maximum[1] = 5.F;
	box.companion.acceleration_minimum[2] = 0.F;
	box.companion.acceleration_maximum[2] = 0.F;
	box.companion.acceleration_norm_max = 5.F;
	check(makeCompanionContractConfiguration(box, destination),
	      "a box touching the norm sphere at its closest point and a zero-width axis remain feasible");
	box.companion.acceleration_norm_max = std::nextafter(5.F, 0.F);
	std::memcpy(original, &destination, sizeof(destination));
	const auto infeasible = validateCompanionConfiguration(box);
	check(!infeasible.valid() && infeasible.parameter && std::strcmp(infeasible.parameter, "SLA_CMP_A_NORM") == 0
	      && !makeCompanionContractConfiguration(box, destination)
	      && std::memcmp(original, &destination, sizeof(destination)) == 0,
	      "a box wholly outside the sphere rejects instead of silently widening acceleration bounds");

	box.companion.admission_enabled = 0;
	box.companion.maximum_relative_drift_ppm = 0;
	box.companion.timestamp_uncertainty_us = 0;
	box.companion.maximum_round_trip_us = 0;
	box.companion.map_interval_us = 0;
	check(validateCompanionConfiguration(box).valid() && !companionAdmissionConfigured(box)
	      && !makeCompanionContractConfiguration(box, destination)
	      && std::memcmp(original, &destination, sizeof(destination)) == 0,
	      "source-only staging may retain incomplete admission geometry or clock assumptions without enabling conversion");
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
	{
		auto dshot = inventory;
		auto &edf = dshot.channels[4];
		edf.protocol = OutputProtocol::Dshot600;
		edf.rate_hz = kDshot600Kbit;
		edf.minimum_us = 1;
		edf.center_us = 1000;
		edf.maximum_us = kDshotMaximumValue;
		edf.disarmed_us = edf.failsafe_us = 0;
		check(makeRoutingPlan(c, dshot, true, plan).valid() && plan.edf.protocol == OutputProtocol::Dshot600,
		      "EDF on DShot600 with disarm frame 0 accepted");
		auto reject = dshot;
		reject.channels[4].rate_hz = 300;
		check(makeRoutingPlan(c, reject, true, plan).fault == RoutingFault::UnsupportedProtocol, "DShot300 rejected");
		reject = dshot;
		reject.channels[0].protocol = OutputProtocol::Dshot600;
		reject.channels[0].rate_hz = kDshot600Kbit;
		check(makeRoutingPlan(c, reject, true, plan).fault == RoutingFault::UnsupportedProtocol, "servo on DShot rejected");
		reject = dshot;
		reject.channels[4].maximum_us = 2000;
		check(makeRoutingPlan(c, reject, true, plan).fault == RoutingFault::InvalidCalibration, "DShot above driver range rejected");
		reject = dshot;
		reject.channels[4].minimum_us = 0;
		check(makeRoutingPlan(c, reject, true, plan).fault == RoutingFault::InvalidCalibration, "DShot zero minimum rejected");
		reject = dshot;
		reject.channels[4].disarmed_us = reject.channels[4].failsafe_us = 1;
		check(makeRoutingPlan(c, reject, true, plan).fault == RoutingFault::InvalidOffValue, "DShot minimum throttle as off value rejected");
		reject = dshot;
		reject.channels[4].failsafe_us = 1000;
		check(makeRoutingPlan(c, reject, true, plan).fault == RoutingFault::InvalidOffValue, "DShot high failsafe rejected");
	}
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

// D3/D4 recovery through the real worker and both banks: canards hold, EDF ramps off, nothing revives the grant.
static void recoveryBankTests()
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
	check(worker.begin(1000, c, p, manual, state, binding) == ManualWorkerStatus::Ready
	      && main.begin(1000, 0, c, inventory, binding, p) == ManualBankFault::None
	      && aux.begin(1000, 1, c, inventory, binding, p) == ManualBankFault::None, "recovery fixture entry");
	ManualWorkerOutput frame{};
	ManualOutputAck main_ack{}, aux_ack{};
	bool flying = true;
	ManualWorkerOutput first_frame{};
	auto same = [](float a, float b) { return std::memcmp(&a, &b, sizeof(a)) == 0; };

	for (uint64_t now : {5000, 9000, 13000}) {
		stamp(now);
		manual.axes[0] = .5F;
		manual.axes[3] = now == 5000 ? -1.F : 1.F;
		flying &= worker.cycle(now, p, manual, state, frame, binding) == ManualWorkerStatus::Command
			  && acceptAndHandle(main, now, p, inventory, binding, frame, main_ack) == ManualBankFault::None
			  && acceptAndHandle(aux, now, p, inventory, binding, frame, aux_ack) == ManualBankFault::None
			  && worker.cycle(now, p, manual, state, frame, binding, &main_ack) == ManualWorkerStatus::Waiting
			  && worker.cycle(now, p, manual, state, frame, binding, &aux_ack) == ManualWorkerStatus::NotDue;

		if (now == 5000) { first_frame = frame; }
	}

	check(flying, "recovery fixture has three fully handled flight frames");
	const auto handled_canards = frame.canards;
	const float handled_edf = frame.edf.normalized;
	const auto pre_main = main.sample(13000, p, inventory, binding);
	const auto pre_aux = aux.sample(13000, p, inventory, binding);
	check(handled_edf > 0.F, "fixture loses authority with nonzero EDF");

	// The supervisor stops renewing a healthy permission; the vehicle remains armed.
	auto lost = p;
	lost.healthy = false;
	stamp(14000);
	check(worker.cycle(14000, lost, manual, state, frame, binding) == ManualWorkerStatus::PropulsionRejected
	      && worker.recovering() && !worker.outstanding(), "permission loss starts worker recovery after a handled frame");
	check(main.sample(14000, lost, inventory, binding).disposition == EdfDisposition::RecoveryRequired
	      && main.recovering() && aux.sample(14000, lost, inventory, binding).disposition == EdfDisposition::RecoveryRequired
	      && aux.recovering(), "both banks enter recovery without any output until a recovery frame is admitted");
	ManualWorkerOutput normal_frame = frame;
	normal_frame.sequence = 99;
	check(main.accept(14500, lost, inventory, binding, normal_frame) != ManualBankFault::None
	      && main.recovering(), "a normal frame cannot revive the lost session");

	auto deliver = [&](ManualOutputBank & bank, uint64_t now, const EdfPermission & perm, const ManualWorkerOutput & f,
	ManualOutputAck & ack) {
		slaaps_manual_command_s wire{};
		ManualWorkerOutput decoded{};

		if (!encodeManualCommand(now, f, wire) || !decodeManualCommand(now, wire, decoded)
		    || bank.acceptRecovery(now, perm, inventory, binding, decoded) != ManualBankFault::None
		    || !bank.awaitingHandling()) { return false; }

		const auto out = bank.sample(now, perm, inventory, binding);
		return bank.confirmHandled(now, perm, inventory, binding, out, true, ack) == ManualBankFault::None;
	};
	ManualWorkerOutput recovery{};
	check(worker.recoverCycle(15000, lost, binding, recovery) == ManualWorkerStatus::Command
	      && recovery.edf.disposition == EdfDisposition::Recovery && recovery.edf.normalized < handled_edf
	      && recovery.sequence > frame.sequence && recovery.deadline_us == 15000 + 100000
	      && std::memcmp(&recovery.canards, &handled_canards, sizeof(handled_canards)) == 0,
	      "recovery frame holds the last handled canards and lowers EDF under one fresh lease");
	check(deliver(main, 15000, lost, recovery, main_ack) && deliver(aux, 15000, lost, recovery, aux_ack),
	      "both banks admit and handle the recovery frame through the wire codec");
	const auto rec_main = main.sample(15000, lost, inventory, binding);
	const auto rec_aux = aux.sample(15000, lost, inventory, binding);
	check(rec_main.disposition == EdfDisposition::Recovery && same(rec_main.pwm_us[6], pre_main.pwm_us[6])
	      && same(rec_main.pwm_us[4], pre_main.pwm_us[4]) && same(rec_aux.pwm_us[7], pre_aux.pwm_us[7])
	      && same(rec_aux.pwm_us[5], pre_aux.pwm_us[5]) && rec_aux.pwm_us[3] <= pre_aux.pwm_us[3],
	      "D3: every canard pulse is exactly the last handled pulse and EDF never rises");
	check(worker.recoverCycle(15000, lost, binding, recovery, &main_ack) == ManualWorkerStatus::Waiting
	      && worker.recoverCycle(15000, lost, binding, recovery, &aux_ack) == ManualWorkerStatus::NotDue,
	      "recovery waits for both exact acknowledgments");

	float last_edf = recovery.edf.normalized;
	uint64_t now = 15000;
	bool ramp_ok = true, canards_ok = true;

	for (unsigned step = 0; step < 6 && worker.recovering(); ++step) {
		now += 3000;
		ManualWorkerOutput next{};
		const auto status = worker.recoverCycle(now, lost, binding, next);

		if (status != ManualWorkerStatus::Command) { ramp_ok = false; break; }

		ramp_ok &= next.edf.normalized <= last_edf;
		last_edf = next.edf.normalized;
		ramp_ok &= deliver(main, now, lost, next, main_ack) && deliver(aux, now, lost, next, aux_ack);
		canards_ok &= same(main.sample(now, lost, inventory, binding).pwm_us[6], pre_main.pwm_us[6]);
		ramp_ok &= worker.recoverCycle(now, lost, binding, next, &main_ack) == ManualWorkerStatus::Waiting
			   && worker.recoverCycle(now, lost, binding, next, &aux_ack) == ManualWorkerStatus::NotDue;
	}

	check(ramp_ok && canards_ok && last_edf <= 0.F, "recovery ramps EDF monotonically to off while canards stay frozen");
	const auto off = aux.sample(now, lost, inventory, binding);
	check((off.channel_mask & (1U << 3)) && off.pwm_us[3] <= 1000.F && off.disposition == EdfDisposition::Recovery,
	      "EDF off is still an explicit handled channel value, at or below the minimum pulse");

	// D4: the bank enforces the ceiling independently of the producer.
	check(main.sample(14000 + ManualOutputBank::kRecoveryCeilingUs - 1, lost, inventory, binding).disposition
	      == EdfDisposition::RecoveryRequired
	      && main.recovering() && main.sample(14000 + ManualOutputBank::kRecoveryCeilingUs, lost, inventory, binding).disposition
	      == EdfDisposition::RecoveryRequired && !main.recovering(), "D4: bank recovery ends permanently at the 60 s ceiling");

	// An invalid recovery frame ends recovery; it is never repaired by a later frame.
	auto bad = recovery;
	bad.sequence += 100;
	bad.timestamp_us = now + 1;
	bad.deadline_us = now + 1000;
	bad.edf.deadline_us = bad.deadline_us;
	bad.edf.disposition = EdfDisposition::Command;
	slaaps_manual_command_s bad_wire{};
	ManualWorkerOutput bad_decoded{};
	check(encodeManualCommand(now + 1, bad, bad_wire) && decodeManualCommand(now + 1, bad_wire, bad_decoded)
	      && aux.acceptRecovery(now + 1, lost, inventory, binding, bad_decoded) != ManualBankFault::None
	      && !aux.recovering(), "a command-disposition frame is not a recovery frame and ends recovery");

	// Kill, lockdown, disarm and termination never start recovery.
	for (unsigned reason = 0; reason < 4; ++reason) {
		ManualOutputBank third;
		ManualOutputAck ack{};
		auto denied = p;
		denied.kill = reason == 0;
		denied.lockdown = reason == 1;
		denied.armed = reason != 2;
		denied.termination = reason == 3;
		check(third.begin(1000, 0, c, inventory, binding, p) == ManualBankFault::None
		      && acceptAndHandle(third, 5000, p, inventory, binding, first_frame, ack) == ManualBankFault::None
		      && third.sample(6000, denied, inventory, binding).disposition == EdfDisposition::Inhibited
		      && !third.recovering(), "kill, lockdown, disarm and termination never enter recovery");
	}

	// Worker side: deliberate stop and new entry end recovery.
	check(worker.recovering(), "worker is still in recovery before a deliberate stop");
	worker.stop();
	check(!worker.recovering() && !owner.recovering() && worker.recoverCycle(now + 1, lost, binding, recovery)
	      == ManualWorkerStatus::Inactive, "deliberate stop ends worker and owner recovery");
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
	check(!development_port.available(), "disabled acceleration calculation is unavailable");
	check(development_port.configure(c) == AccelerationPortStatus::Unavailable,
	      "default incomplete profile cannot enable acceleration calculation");
	check(development_port.cycle({}, unchanged) == AccelerationPortStatus::Unavailable && near(unchanged.edf_thrust_n, 7.F),
	      "disabled acceleration calculation produces no replacement output");
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

	{
		auto dshot = banks[1];
		dshot.protocol = 1;

		for (unsigned i = 0; i < 16; ++i) {
			dshot.rate_hz[i] = kDshot600Kbit;
			dshot.minimum_us[i] = 1;
			dshot.center_us[i] = 1000;
			dshot.maximum_us[i] = kDshotMaximumValue;
			dshot.disarmed_us[i] = dshot.failsafe_us[i] = 0;
		}

		slaaps_output_bank_s mixed[2] {banks[0], dshot};
		check(assembleOutputInventory(2000, mixed, 2, 3, inventory) == InventoryFault::None
		      && inventory.channels[16 - 1].protocol == OutputProtocol::Dshot600, "DShot600 bank reported as such");
		check(makeRoutingPlan(fixture(), inventory, true, plan).fault == RoutingFault::UnsupportedProtocol,
		      "servo on the DShot bank rejected");
		mixed[0].function[1] = kCanardFunctionFirst + 1;
		mixed[0].function[2] = kCanardFunctionFirst + 3;
		mixed[1].function[2] = 0;
		mixed[1].function[0] = 0;
		check(assembleOutputInventory(2000, mixed, 2, 3, inventory) == InventoryFault::None
		      && makeRoutingPlan(fixture(), inventory, true, plan).valid()
		      && plan.edf.protocol == OutputProtocol::Dshot600 && plan.edf.bank == OutputBank::Aux && plan.edf.channel == 6,
		      "servos on PWM bank plus EDF on DShot600 bank accepted");
		mixed[1].rate_hz[6] = 300;
		check(assembleOutputInventory(2000, mixed, 2, 3, inventory) == InventoryFault::None
		      && makeRoutingPlan(fixture(), inventory, true, plan).fault == RoutingFault::UnsupportedProtocol,
		      "DShot300 EDF channel rejected");
	}
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

	auto operator_wait = request;
	operator_wait.deadline_us = operator_wait.timestamp_us + 2000000;
	ManualBankPreparation waiting;
	const uint64_t after_one_second = operator_wait.timestamp_us + 1000001;
	check(waiting.prepare(operator_wait.timestamp_us, 0, c, inventory, operator_wait)
	      && waiting.prepared(after_one_second) && waiting.prepared(operator_wait.deadline_us - 1)
	      && !waiting.prepared(operator_wait.deadline_us),
	      "configured non-actuating Manual preparation survives one second and expires at its exact original deadline");
	auto too_long = request;
	too_long.deadline_us = too_long.timestamp_us + static_cast<uint64_t>(c.manual_ready_wait_s) * 1000000 + 1;
	ManualBankPreparation bounded;
	check(ManualBankPreparation::validRequest(too_long.timestamp_us, too_long)
	      && !bounded.prepare(too_long.timestamp_us, 0, c, inventory, too_long),
	      "a proposal within the absolute Manual ceiling cannot exceed its accepted five-second operator wait");
	check(!bounded.prepare(operator_wait.timestamp_us, 0, c, inventory, operator_wait),
	      "shortening a rejected operator window cannot replay its consumed session token");

	auto delayed = request;
	delayed.timestamp_us += 1000000;
	delayed.deadline_us = delayed.timestamp_us + static_cast<uint64_t>(c.manual_ready_wait_s) * 1000000;
	ManualBankPreparation delayed_bank;
	check(ManualBankPreparation::validRequest(delayed.timestamp_us, delayed)
	      && !delayed_bank.prepare(delayed.timestamp_us, 0, c, inventory, delayed),
	      "delaying a proposal cannot restart the five-second wait from its later publication time");
	delayed.deadline_us = delayed.session + static_cast<uint64_t>(c.manual_ready_wait_s) * 1000000;
	ManualBankPreparation exact_entry_window;
	check(exact_entry_window.prepare(delayed.timestamp_us, 0, c, inventory, delayed)
	      && exact_entry_window.prepared(delayed.deadline_us - 1) && !exact_entry_window.prepared(delayed.deadline_us),
	      "a delayed proposal may use only the remaining part of the exact original entry window");

	auto denied = p;
	denied.timestamp_us = after_one_second;
	denied.deadline_us = after_one_second + 100000;
	denied.armed = denied.healthy = denied.flight_admitted = denied.measured_condition_valid = false;
	ManualBankPreparation consumed;
	check(consumed.prepare(operator_wait.timestamp_us, 0, c, inventory, operator_wait)
	      && consumed.consume(after_one_second, c, inventory, binding, denied, 77, 88),
	      "non-actuating preparation consumption validates transaction identity without manufacturing permission");
	ManualOutputBank output;
	check(output.begin(after_one_second, 0, c, inventory, binding, denied) != ManualBankFault::None && !output.active()
	      && !denied.armed && !denied.healthy && !denied.flight_admitted && !denied.measured_condition_valid
	      && denied.timestamp_us == after_one_second && denied.deadline_us == after_one_second + 100000,
	      "long disarmed readiness leaves the actual output consumer denied and cannot renew authority");

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
		case 0: proposal.deadline_us = proposal.timestamp_us + ManualBankPreparation::maximumPreparationUs(proposal.mode) + 1; break;

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

		case 4: wire.deadline = request.timestamp_us + ManualBankPreparation::maximumPreparationUs(request.mode) + 1; break;

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
	wrenchConfigurationTests();
	accelerationFeedbackConfigurationTests();
	check(slaapsAccelerationFeedbackTests(checks), "selected acceleration feedback numerical, source and expiry regressions");
	check(slaapsAccelerationControllerTests(checks), "scheduled acceleration controller coupled current-output regressions");
	check(slaapsEdfOwnerTransferTests(checks, fixture()), "same-owner EDF transfer preserves applied output and original authority");
	check(slaapsEdfOwnerRecoveryTests(checks, fixture()), "EDF owner D3/D4 recovery ramps off and respects the 60 s ceiling");
	check(slaapsFlightTransferTests(checks, fixture(), routingFixture()), "shared worker and physical-bank flight transfer boundaries");
	vehicleWrenchAllocationTests();
	dataProvenanceTests();
	activationReserveConfigurationTests();
	powerConfigurationTests();
	profileTests();
	benchTravelBoundsTests();
	companionConfigurationTests();
	companionAdmissionConfigurationTests();
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
	recoveryBankTests();
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
#include <uORB/topics/mavlink_log.h>
#include <lib/slaaps/ManualBankTransport.hpp>
#include <lib/slaaps/ManualPreparationEndpoint.hpp>
#include <lib/slaaps/ManualPreparationCoordinator.hpp>
#include <lib/slaaps/OutputRoutingObservation.hpp>
#include <lib/slaaps/OperatingConditionObservation.hpp>
#include <lib/slaaps/PowerObservation.hpp>
#include <uORB/PublicationMulti.hpp>
#include <uORB/topics/sensor_accel.h>
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
	{
		using BiasPublication = uORB::PublicationMulti<estimator_sensor_bias_s>;
		using RawPublication = uORB::PublicationMulti<sensor_accel_s>;
		BiasPublication bias_pub{ORB_ID(estimator_sensor_bias)};
		RawPublication raw_pub{ORB_ID(sensor_accel)};
		uORB::ResearchSubscription bias_lifetime{ORB_ID::estimator_sensor_bias};
		uORB::ResearchSubscription raw_lifetime{ORB_ID::sensor_accel};
		uORB::Publication<vehicle_acceleration_s> acceleration_pub{ORB_ID(vehicle_acceleration)};
		sensor_accel_s raw{};
		raw.timestamp = raw.timestamp_sample = now;
		raw.device_id = selection.accel_device_id;
		raw.z = -9.8F;
		check(raw_pub.publish(raw) && raw_pub.get_instance() == 0 && raw_lifetime.revision(),
		      "optional acceleration fixture binds an actual advertised raw accelerometer lifetime");
		estimator_sensor_bias_s bias{};
		bias.timestamp = bias.timestamp_sample = now;
		bias.accel_device_id = selection.accel_device_id;
		bias.accel_bias_valid = true;
		check(bias_pub.publish(bias) && bias_pub.get_instance() == 0 && bias_lifetime.revision(),
		      "optional acceleration fixture owns the supported single-estimator bias instance zero");
		vehicle_acceleration_s acceleration{};
		acceleration.timestamp = acceleration.timestamp_sample = now;
		acceleration.xyz[2] = -9.8F;
		acceleration.device_id = selection.accel_device_id;
		acceleration.calibrated = true;
		acceleration.source_generation = 7;
		acceleration.filter_cutoff_hz = 30.F;
		acceleration.filter_sample_rate_hz = 1000.F;
		acceleration.bias_timestamp = acceleration.bias_timestamp_sample = now;
		acceleration.bias_valid = true;
		acceleration.bias_publisher_revision = bias_lifetime.revision();
		acceleration.bias_instance = 0;
		acceleration.raw_publisher_revision = raw_lifetime.revision();
		acceleration.raw_instance = 0;
		check(acceleration_pub.publish(acceleration), "publish one original acceleration payload with applied bias and filter metadata");
		SelectedStateObservation acceleration_observation;
		auto selected = acceleration_observation.captureAccelerationState(1000000, 1000000);
		check(selected.selected.state.valid && selected.acceleration_stable && selected.bias_publisher_current && selected.raw_publisher_current
		      && selected.selected.required_topics == PublisherBinding::kStateTopicsMask
		      && !selected.selected.manual.timestamp_us && selected.acceleration_revision,
		      "optional acceleration observation retains selected state without inventing or requiring manual input");
		check(selected.acceleration.timestamp == now && selected.acceleration.timestamp_sample == now
		      && selected.acceleration.device_id == acceleration.device_id && selected.acceleration.calibrated
		      && selected.acceleration.source_generation == acceleration.source_generation
		      && sameFloatBits(selected.acceleration.filter_cutoff_hz, acceleration.filter_cutoff_hz)
		      && sameFloatBits(selected.acceleration.filter_sample_rate_hz, acceleration.filter_sample_rate_hz)
		      && selected.acceleration.bias_timestamp == now && selected.acceleration.bias_timestamp_sample == now
		      && selected.acceleration.bias_valid && selected.acceleration.bias_instance == 0
		      && selected.acceleration.bias_publisher_revision == acceleration.bias_publisher_revision
		      && selected.acceleration.raw_publisher_revision == acceleration.raw_publisher_revision && selected.acceleration.raw_instance == 0
		      && sameFloatBits(selected.acceleration.xyz[2], acceleration.xyz[2]),
		      "observer copies original raw timestamps, applied filter/source and bias provenance without renewing them");
		const unsigned original_bias_revision = acceleration.bias_publisher_revision;
		bias_pub.~BiasPublication();
		selected = acceleration_observation.captureAccelerationState(1000000, 1000000);
		check(selected.acceleration_stable && selected.acceleration.bias_valid && !selected.bias_publisher_current
		      && selected.acceleration.bias_publisher_revision == original_bias_revision,
		      "a held acceleration payload cannot keep its bias publisher current after actual publisher loss");
		new (&bias_pub) BiasPublication{ORB_ID(estimator_sensor_bias)};
		check(bias_pub.advertise() && bias_pub.get_instance() == 0 && bias_lifetime.revision()
		      && bias_lifetime.revision() != original_bias_revision, "replacement bias publisher has a distinct lifetime at the same instance");
		selected = acceleration_observation.captureAccelerationState(1000000, 1000000);
		check(selected.acceleration_stable && selected.acceleration.bias_valid && !selected.bias_publisher_current,
		      "replacement advertisement cannot inherit held acceleration bias provenance");
		check(bias_pub.publish(bias), "replacement publishes the identical old bias payload");
		selected = acceleration_observation.captureAccelerationState(1000000, 1000000);
		check(selected.acceleration_stable && selected.acceleration.bias_valid && !selected.bias_publisher_current,
		      "identical replacement bias data cannot retroactively change the applied lifetime in an old acceleration sample");
		acceleration.bias_publisher_revision = bias_lifetime.revision();
		++acceleration.source_generation;
		check(acceleration_pub.publish(acceleration), "publish explicit replacement applied-source metadata without renewing original timestamps");
		selected = acceleration_observation.captureAccelerationState(1000000, 1000000);
		check(selected.acceleration_stable && selected.bias_publisher_current && selected.acceleration.timestamp_sample == now
		      && selected.acceleration.source_generation == acceleration.source_generation,
		      "only an acceleration sample carrying the replacement applied lifetime can observe it as current");
		const unsigned original_raw_revision = acceleration.raw_publisher_revision;
		// Queued uORB publishers deliberately do not unadvertise in their
		// destructor. Match PX4Accelerometer's explicit driver teardown.
		check(raw_pub.unadvertise(), "explicitly retire queued raw accelerometer publisher");
		raw_pub.~RawPublication();
		new (&raw_pub) RawPublication{ORB_ID(sensor_accel)};
		check(raw_pub.publish(raw) && raw_pub.get_instance() == 0 && raw_lifetime.revision()
		      && raw_lifetime.revision() != original_raw_revision, "raw publisher replacement keeps device ID but changes actual lifetime");
		selected = acceleration_observation.captureAccelerationState(1000000, 1000000);
		check(selected.acceleration_stable && selected.bias_publisher_current && !selected.raw_publisher_current,
		      "held applied acceleration cannot inherit a restarted raw publisher even with identical sensor ID and payload");
		acceleration.raw_publisher_revision = raw_lifetime.revision();
		++acceleration.source_generation;
		check(acceleration_pub.publish(acceleration), "publish explicit replacement raw-source metadata");
		selected = acceleration_observation.captureAccelerationState(1000000, 1000000);
		check(selected.acceleration_stable && selected.bias_publisher_current && selected.raw_publisher_current,
		      "new applied raw lifetime is observable only from corresponding acceleration metadata");
		check(raw_pub.unadvertise(), "release queued raw fixture instance for later integration scenarios");
	}
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

	// Deterministic live-copy interleavings; no sleeps or writer threads. The
	// callback runs after initial copies and cannot override capture's checks.
	using BankPublication = uORB::PublicationMulti<slaaps_output_bank_s>;
	struct Interleave {
		BankPublication *main{}, *aux{}, *extra{};
		slaaps_output_bank_s *banks{};
		unsigned calls{}, publish_attempts{}, change_attempt{};
		bool replace{}, enumerate{};
	};
	const auto interleave = [](void *opaque, unsigned attempt) {
		auto &context = *static_cast<Interleave *>(opaque);
		check(attempt == context.calls, "live capture callback identifies contiguous zero-based attempts");
		++context.calls;

		if (attempt == context.change_attempt && context.replace) {
			context.main->~BankPublication();
			new (context.main) BankPublication{ORB_ID::slaaps_output_bank};
			context.banks[0].timestamp = hrt_absolute_time();
			check(context.main->publish(context.banks[0]), "replace a routing publisher between initial copy and lifetime recheck");
			return;
		}

		if (attempt == context.change_attempt && context.enumerate) {
			check(context.extra->publish(context.banks[0]), "add an advertised routing instance between initial copy and enumeration recheck");
			return;
		}

		if (attempt < context.publish_attempts) {
			context.banks[0].timestamp = context.banks[1].timestamp = hrt_absolute_time();
			check(context.main->publish(context.banks[0]) && context.aux->publish(context.banks[1]),
			      "same-lifetime report heartbeats deterministically change both copied generations");
		}
	};
	OutputRoutingObservation::Workspace workspace{};
	uint64_t observed{};
	banks[0].timestamp = banks[1].timestamp = hrt_absolute_time();
	check(restarted.publish(banks[0]) && aux.publish(banks[1])
	      && observation.captureLive(observed, snapshot, workspace) == InventoryFault::None,
	      "live retry fixture starts from a complete fresh unchanged routing snapshot");
	const auto live_identity = snapshot;
	Interleave heartbeat{&restarted, &aux, nullptr, banks, 0, 2};
	observation.setCapturePassHook(interleave, &heartbeat);
	check(observation.captureLive(observed, snapshot, workspace) == InventoryFault::None && heartbeat.calls == 3
	      && sameReportedDrivers(live_identity, snapshot) && snapshot.reported_us[0] == banks[0].timestamp
	      && snapshot.reported_us[1] == banks[1].timestamp && snapshot.captured_us <= observed,
	      "two concurrent heartbeat generations retry full copies and a stable third pass retains exact publisher identities");
	const unsigned before_strict = heartbeat.calls;
	check(observation.capture(hrt_absolute_time(), snapshot, workspace) == InventoryFault::None && heartbeat.calls == before_strict,
	      "explicit-time capture remains one strict pass and never invokes the live test callback");
	unsigned char retained[sizeof(snapshot)];
	std::memcpy(retained, &snapshot, sizeof(snapshot));
	heartbeat.calls = 0;
	heartbeat.publish_attempts = 3;
	check(observation.captureLive(observed, snapshot, workspace) == InventoryFault::UnstablePublisher
	      && heartbeat.calls == 3 && std::memcmp(retained, &snapshot, sizeof(snapshot)) == 0,
	      "three changing live generations exhaust the bound without modifying any byte of the previous snapshot");
	observation.setCapturePassHook(nullptr, nullptr);
	check(observation.captureLive(observed, snapshot, workspace) == InventoryFault::None,
	      "a later independent capture may observe stability after the bounded failed call");

	for (bool replace : {true, false}) {
		for (unsigned change_attempt : {0U, 1U}) {
			BankPublication added{ORB_ID::slaaps_output_bank};
			Interleave lifetime{&restarted, &aux, &added, banks, 0, change_attempt, change_attempt, replace, !replace};
			std::memcpy(retained, &snapshot, sizeof(snapshot));
			observation.setCapturePassHook(interleave, &lifetime);
			check(observation.captureLive(observed, snapshot, workspace) == InventoryFault::UnstablePublisher
			      && lifetime.calls == change_attempt + 1 && std::memcmp(retained, &snapshot, sizeof(snapshot)) == 0,
			      "publisher replacement or absent-to-present enumeration is terminal even after an earlier permitted heartbeat retry");
			observation.setCapturePassHook(nullptr, nullptr);

			if (!replace) { check(added.unadvertise(), "remove deliberately added routing publisher"); }

			check(observation.captureLive(observed, snapshot, workspace) == InventoryFault::None,
			      "new independent capture can bind the current complete enumeration after terminal lifetime rejection");
		}
	}

	for (unsigned fault = 0; fault < 3; ++fault) {
		auto bad = banks[0];
		InventoryFault expected{};

		if (fault == 0) { bad.channel_count = kMaximumChannelsPerBank + 1; expected = InventoryFault::InvalidReport; }

		if (fault == 1) { bad.timestamp = 1; expected = InventoryFault::Stale; }

		if (fault == 2) { bad.ready = false; expected = InventoryFault::DriverNotReady; }

		check(restarted.publish(bad), "publish original malformed, stale or unready routing report");
		Interleave corrected{&restarted, &aux, nullptr, banks, 0, 3};
		std::memcpy(retained, &snapshot, sizeof(snapshot));
		observation.setCapturePassHook(interleave, &corrected);
		check(observation.captureLive(observed, snapshot, workspace) == expected && corrected.calls == 1
		      && std::memcmp(retained, &snapshot, sizeof(snapshot)) == 0,
		      "generation change cannot retry away an originally malformed, stale or unready report even when the callback publishes a valid correction");
		observation.setCapturePassHook(nullptr, nullptr);
		check(observation.captureLive(observed, snapshot, workspace) == InventoryFault::None,
		      "corrected reports become usable only on a new independent capture call");
	}
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
	auto companion_timing = slaapsFlightTransferConfiguration(c);
	check(companionScheduleInterval(companion_timing) == 50000,
	      "inactive companion observation precedes the original 100ms control lease");
	companion_timing.actuator_permission_age_us = 20000;
	check(companionScheduleInterval(companion_timing) == 10000,
	      "companion monitoring respects the shortest configured authority lifetime");
	companion_timing.companion.map_interval_us = 10000;
	companion_timing.companion.maximum_round_trip_us = 1000;
	companion_timing.companion.timestamp_uncertainty_us = 1;
	companion_timing.companion.maximum_relative_drift_ppm = 1;
	uint64_t renewal_lead{};
	check(companionClockRenewalConfigured(companion_timing.companion, renewal_lead) && renewal_lead == 5000
	      && companionScheduleInterval(companion_timing) == 1999,
	      "short valid clock map requests dispatches within its renewal window");
	companion_timing.companion.map_interval_us = 500000;
	companion_timing.companion.maximum_round_trip_us = 240000;
	companion_timing.companion.maximum_clock_uncertainty_us = 300000;
	uint64_t observation_slack{};
	check(companionClockRenewalConfigured(companion_timing.companion, renewal_lead, &observation_slack)
	      && renewal_lead == 250000 && observation_slack == 9998
	      && companionScheduleInterval(companion_timing) == 4999,
	      "large declared round trip retains dispatch opportunities before renewal slack is spent");
	companion_timing.companion.admission_enabled = 0;
	check(companionScheduleInterval(companion_timing) == kSupervisorIdleIntervalUs,
	      "disabled companion admission leaves ordinary inactive cadence unchanged");
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
#include <uORB/topics/failsafe_flags.h>
#include <uORB/topics/vehicle_command_ack.h>
#include <uORB/topics/sensor_baro.h>
#include <uORB/topics/sensor_gyro.h>
#include <uORB/topics/sensor_mag.h>
#include <uORB/topics/system_power.h>
#include <atomic>
#include <new>

// Synthetic sensor/pilot producer for the running production supervisor. The
// real Commander exclusively owns actuator_armed and vehicle_status here.
class NativeSlaapsSelectedInput final : public px4::ScheduledWorkItem
{
public:
	// Raw device identities for the synthetic health inputs. Gyro and accel
	// match the estimator fixture below, so Commander sees one coherent IMU.
	static constexpr int32_t kHealthGyroId = 123, kHealthAccelId = 456, kHealthBaroId = 789, kHealthMagId = 1011;
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
		const uint64_t gap = _power_maximum_gap.load();
		const uint64_t gap_start = _power_gap_start.load(), gap_end = _power_gap_end.load();
		const bool coherent_gap = gap == _power_maximum_gap.load() && (!gap || (gap_end >= gap_start && gap_end - gap_start == gap));
		PX4_INFO("SLAAPS_POWER_TIMING window=%llu last=%llu", static_cast<unsigned long long>(_power_window.load()),
			 static_cast<unsigned long long>(_power_last.load()));
		PX4_INFO("SLAAPS_POWER_PUBLICATION previous=%llu published=%llu", static_cast<unsigned long long>(_power_previous.load()),
			 static_cast<unsigned long long>(_power_published.load()));
		PX4_INFO("SLAAPS_POWER_GAP gap=%llu begin=%llu end=%llu", static_cast<unsigned long long>(gap),
			 static_cast<unsigned long long>(gap_start), static_cast<unsigned long long>(gap_end));
		PX4_INFO("SLAAPS_POWER_GAP_STATE coherent=%d regressions=%u first_fault=%llu", coherent_gap,
			 _power_regressions.load(), static_cast<unsigned long long>(_edf_first_terminal.load()));
		PX4_INFO("SLAAPS_ESTIMATOR_STATUS only=%u running=%u samples=%u synthetic=1 authority_publishers=0",
			 _estimator_only.load() ? 1U : 0U, _publishing.load() ? 1U : 0U, _samples.load());
		PX4_INFO("SLAAPS_FEEDBACK_STATUS enabled=%u holding=%u generation=%llu last_sample=%llu",
			 _feedback_enabled.load() ? 1U : 0U, _feedback_hold.load() ? 1U : 0U,
			 static_cast<unsigned long long>(_feedback_generation.load()),
			 static_cast<unsigned long long>(_feedback_last_sample.load()));
		PX4_INFO("SLAAPS_FEEDBACK_SOURCES bias_revision=%u raw_revision=%u synthetic=1 authority_publishers=0",
			 _feedback_bias_revision.load(), _feedback_raw_revision.load());
		PX4_INFO("SLAAPS_HEALTH_STATUS running=%u samples=%u synthetic=1 measured=0",
			 _health_publishing.load() ? 1U : 0U, _health_samples.load());
		PX4_INFO("SLAAPS_SELECTED_STATUS running=%u scenario=%u samples=%u failures=%u power_running=%u power_scenario=%u power_samples=%u synthetic=1 authority_publishers=0",
			 _publishing.load() ? 1U : 0U, _scenario.load(), _samples.load(), _failures.load(),
			 _power_publishing.load() ? 1U : 0U, _power_scenario.load(), _power_samples.load());
	}
private:
	void Run() override
	{
		const unsigned request = _request.exchange(0);

		if (request == 1) { _publishing.store(false); }

		if (request == 2) { _estimator_only.store(false); _publishing.store(true); }

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

		if (request == 9) {
			// A new explicit valid-power command starts a diagnostic window;
			// earlier intentional drop tests cannot inflate this window's gap.
			_power_window.store(hrt_absolute_time());
			_power_last.store(0); _power_previous.store(0); _power_published.store(0);
			_power_maximum_gap.store(0); _power_gap_start.store(0); _power_gap_end.store(0);
			_edf_first_terminal.store(0); _power_regressions.store(0);
			_edf_watched_session = _power_last_status_time = 0;
			_power_last_status_entry = _power_last_status_power = 0;
			_power_publishing.store(true); _power_scenario.store(0);
		}

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

		if (request == 20) {
			// Companion protocol exercises need only estimator observations.
			// Commander remains the sole publisher of its actual state witness.
			_estimator_only.store(true);
			_power_publishing.store(false);
			_scenario.store(0);
			_publishing.store(true);
		}

		if (request == 21) {
			_estimator_only.store(true);
			_power_publishing.store(false);
			const bool first_feedback = !_feedback_enabled.exchange(true);
			_feedback_hold.store(false);
			_publishing.store(true);

			if (first_feedback) { _feedback_generation.store(1); }
		}

		if (request == 22) { _feedback_hold.store(true); }

		if (request == 25) { _health_publishing.store(true); }

		if (request == 26) { _health_publishing.store(false); }

		if (request == 23) {
			_feedback_hold.store(true);
			const unsigned previous = _feedback_bias_lifetime.revision();
			using BiasPublication = uORB::PublicationMulti<estimator_sensor_bias_s>;
			_feedback_bias.~BiasPublication();
			new (&_feedback_bias) BiasPublication{ORB_ID(estimator_sensor_bias)};

			if (!_feedback_enabled.load() || !_feedback_bias.advertise() || _feedback_bias.get_instance() != 0
			    || !_feedback_bias_lifetime.revision() || _feedback_bias_lifetime.revision() == previous) { ++_failures; }

			const uint64_t generation = _feedback_generation.load();
			_feedback_generation.store(generation == UINT64_MAX ? 0 : generation + 1);
		}

		if (request == 24) {
			_feedback_hold.store(true);
			const unsigned previous = _feedback_raw_lifetime.revision();
			using RawPublication = uORB::PublicationMulti<sensor_accel_s>;

			if (!_feedback_raw.unadvertise()) { ++_failures; }

			_feedback_raw.~RawPublication();
			new (&_feedback_raw) RawPublication{ORB_ID(sensor_accel)};

			if (!_feedback_enabled.load() || !_feedback_raw.advertise() || _feedback_raw.get_instance() != 0
			    || !_feedback_raw_lifetime.revision() || _feedback_raw_lifetime.revision() == previous) { ++_failures; }

			const uint64_t generation = _feedback_generation.load();
			_feedback_generation.store(generation == UINT64_MAX ? 0 : generation + 1);
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

			if (_power.publish(power)) {
				const uint64_t previous = _power_last.load();

				if (!_power_window.load()) { _power_window.store(now); }

				if (previous && now < previous) { ++_power_regressions; }

				if (previous && now >= previous && now - previous > _power_maximum_gap.load()) {
					_power_gap_start.store(previous); _power_gap_end.store(now);
					_power_maximum_gap.store(now - previous);
				}

				_power_previous.store(previous); _power_last.store(now); _power_published.store(hrt_absolute_time());
				++_power_samples;
			}

			else { ++_failures; }
		}

		if (_health_publishing.load()) {
			if (publishHealth(now)) { ++_health_samples; }

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
		const bool feedback_published = publishFeedback(now);
		const bool published = _attitude.publish(attitude) && _rates.publish(rates) && _estimator.publish(estimator)
				       && _flags.publish(flags) && (_estimator_only.load() || _manual.publish(manual)) && _selection.publish(selection)
				       && _attitude_lifetime.revision() && feedback_published;

		if (published) { ++_samples; }

		else { ++_failures; }

		captureFirstEdfTerminal();
	}
	bool publishHealth(uint64_t now)
	{
		// Explicit synthetic raw sensors and avionics rail so Commander's
		// unmodified ordinary checks have inputs. Stationary values only; these
		// are not vehicle measurements and qualify no physical health.
		bool accel_published = true;

		if (!_feedback_enabled.load()) {
			// Share the feedback publisher so raw accel keeps instance 0.
			sensor_accel_s raw{};
			raw.timestamp = raw.timestamp_sample = now;
			raw.device_id = kHealthAccelId;
			raw.z = -9.8F;
			accel_published = _feedback_raw.publish(raw) && _feedback_raw.get_instance() == 0;
		}

		sensor_gyro_s gyro{};
		gyro.timestamp = gyro.timestamp_sample = now;
		gyro.device_id = kHealthGyroId;
		gyro.temperature = 25.F;
		gyro.samples = 1;
		sensor_baro_s baro{};
		baro.timestamp = baro.timestamp_sample = now;
		baro.device_id = kHealthBaroId;
		baro.pressure = 101325.F;
		baro.temperature = 25.F;
		sensor_mag_s mag{};
		mag.timestamp = mag.timestamp_sample = now;
		mag.device_id = kHealthMagId;
		mag.x = .2F;
		mag.z = .4F;
		mag.temperature = 25.F;
		system_power_s rail{};
		rail.timestamp = now;
		rail.voltage5v_v = 5.F;
		rail.brick_valid = 1;
		return accel_published && _health_gyro.publish(gyro) && _health_gyro.get_instance() == 0
		       && _health_baro.publish(baro) && _health_baro.get_instance() == 0
		       && _health_mag.publish(mag) && _health_mag.get_instance() == 0 && _health_rail.publish(rail);
	}
	bool publishFeedback(uint64_t now)
	{
		if (!_feedback_enabled.load()) { return true; }

		// Explicit synthetic specific force/bias only. Commander remains the sole
		// authority witness; this fixture never creates an acceleration setpoint.
		sensor_accel_s raw{};
		raw.timestamp = raw.timestamp_sample = now;
		raw.device_id = 456;
		raw.z = -9.8F;
		const bool raw_published = _feedback_raw.publish(raw) && _feedback_raw.get_instance() == 0;
		const unsigned raw_revision = _feedback_raw_lifetime.revision();
		_feedback_raw_revision.store(raw_revision);
		estimator_sensor_bias_s bias{};
		bias.timestamp = bias.timestamp_sample = now;
		bias.accel_device_id = 456;
		bias.accel_bias_valid = true;
		const bool bias_published = _feedback_bias.publish(bias) && _feedback_bias.get_instance() == 0;
		const unsigned revision = _feedback_bias_lifetime.revision();
		_feedback_bias_revision.store(revision);

		if (!raw_published || !raw_revision || !bias_published || !revision) { return false; }

		if (_feedback_hold.load()) { return true; }

		vehicle_acceleration_s acceleration{};
		acceleration.timestamp = acceleration.timestamp_sample = now;
		acceleration.xyz[2] = -9.8F;
		acceleration.device_id = 456;
		acceleration.calibrated = true;
		acceleration.source_generation = _feedback_generation.load();
		// Matches the explicit native IMU_ACCEL_CUTOFF=0 synthetic profile.
		acceleration.filter_cutoff_hz = acceleration.filter_sample_rate_hz = 0.F;
		acceleration.bias_timestamp = acceleration.bias_timestamp_sample = now;
		acceleration.bias_valid = true;
		acceleration.bias_publisher_revision = revision;
		acceleration.bias_instance = 0;
		acceleration.raw_publisher_revision = raw_revision;
		acceleration.raw_instance = 0;
		const bool published = _feedback_acceleration.publish(acceleration);

		if (published) { _feedback_last_sample.store(now); }

		return published;
	}
	void captureFirstEdfTerminal()
	{
		if (_edf_first_terminal.load()) { return; }

		slaaps_status_s status{};

		if (!_observed_supervisor.copy(&status) || status.timestamp < _power_window.load()) { return; }

		const uint64_t previous_status = _power_last_status_time;
		const unsigned previous_entry = _power_last_status_entry, previous_power = _power_last_status_power;
		_power_last_status_time = status.timestamp;
		_power_last_status_entry = status.bench_entry_status;
		_power_last_status_power = status.power_status;

		if (status.bench_edf_commissioning && status.manual_preparation_session && status.power_ready
		    && (status.bench_entry_status == slaaps_status_s::BENCH_ENTRY_WAIT_ARM
			|| status.bench_entry_status == slaaps_status_s::BENCH_ENTRY_ACTIVATING
			|| status.bench_entry_status == slaaps_status_s::BENCH_ENTRY_FIRST_COMMAND
			|| status.bench_entry_status == slaaps_status_s::BENCH_ENTRY_EDF_RUNNING)) {
			_edf_watched_session = status.manual_preparation_session;
		}

		const bool terminal = status.bench_entry_status == slaaps_status_s::BENCH_ENTRY_REJECTED
				      || status.bench_entry_status == slaaps_status_s::BENCH_ENTRY_WAIT_DISARM
				      || status.bench_entry_status == slaaps_status_s::BENCH_ENTRY_DISARM_FAILED
				      || status.bench_entry_status == slaaps_status_s::BENCH_ENTRY_COMPLETE;
		const bool session_lost = status.manual_preparation_session != _edf_watched_session;

		if (!_edf_watched_session || (!terminal && !session_lost)) { return; }

		const uint64_t started = hrt_absolute_time();
		vehicle_status_s vehicle{};
		actuator_armed_s armed{};
		failsafe_flags_s failsafe{};
		battery_status_s battery{};
		slaaps_manual_permission_s permission{};
		slaaps_manual_command_s command{};
		slaaps_manual_preparation_s ready[2] {}, transition{};
		slaaps_manual_activation_s activation[2] {};
		slaaps_manual_ack_s handled[2] {};
		slaaps_output_bank_s bank_report[2] {};
		_observed_vehicle.copy(&vehicle); _observed_armed.copy(&armed); _observed_failsafe.copy(&failsafe);
		_observed_power.copy(&battery); _observed_permission.copy(&permission); _observed_command.copy(&command);
		_observed_transition.copy(&transition);

		for (unsigned bank = 0; bank < 2; ++bank) {
			_observed_ready[bank].copy(&ready[bank]); _observed_activation[bank].copy(&activation[bank]);
			_observed_handled[bank].copy(&handled[bank]); _observed_bank[bank].copy(&bank_report[bank]);
		}

		const uint64_t observed = hrt_absolute_time();
		_edf_first_terminal.store(observed);
		const auto t = [](uint64_t value) { return static_cast<unsigned long long>(value); };
		// First sampled terminal/session-loss evidence, not the exact causal
		// read or a proven ordering against Commander failsafe. In particular,
		// power9 with a retained deadline may be later status-expiry synthesis.
		std::fprintf(stderr,
			     "EDF_TERMINAL_FIRST sampled_only=1 window=%llu copy_start=%llu observed=%llu status=%llu session=%llu entry=%u worker=%u power=%u retained_deadline=%llu\n",
			     t(_power_window.load()), t(started), t(observed), t(status.timestamp), t(status.manual_preparation_session),
			     status.bench_entry_status, status.manual_worker_status, status.power_status, t(status.power_deadline));
		std::fprintf(stderr, "EDF_TERMINAL_FIRST previous_sampled_status=%llu entry=%u power=%u\n", t(previous_status), previous_entry,
			     previous_power);
		std::fprintf(stderr, "EDF_TERMINAL_FIRST watched_session=%llu terminal=%d session_lost=%d\n", t(_edf_watched_session), terminal,
			     session_lost);
		std::fprintf(stderr,
			     "EDF_TERMINAL_FIRST producer previous=%llu last=%llu published=%llu max_gap=%llu gap_start=%llu gap_end=%llu regressions=%u\n",
			     t(_power_previous.load()), t(_power_last.load()), t(_power_published.load()), t(_power_maximum_gap.load()),
			     t(_power_gap_start.load()), t(_power_gap_end.load()), _power_regressions.load());
		std::fprintf(stderr, "EDF_TERMINAL_FIRST battery timestamp=%llu voltage=%.6f connected=%d id=%u source=%u warning=%u faults=%u\n",
			     t(battery.timestamp), static_cast<double>(battery.voltage_v), battery.connected, battery.id, battery.source, battery.warning,
			     battery.faults);
		std::fprintf(stderr,
			     "EDF_TERMINAL_FIRST authority vehicle=%llu state=%u nav=%u intent=%u failsafe=%d actuator=%llu armed=%d prearmed=%d inhibits=%d/%d/%d masks=%u/%u\n",
			     t(vehicle.timestamp), vehicle.arming_state, vehicle.nav_state, vehicle.nav_state_user_intention, vehicle.failsafe,
			     t(armed.timestamp), armed.armed, armed.prearmed, armed.kill, armed.lockdown, armed.termination, status.can_arm_mask, status.can_run_mask);
		std::fprintf(stderr,
			     "EDF_TERMINAL_FIRST failsafe time=%llu other=%u prevent_arming=%u manual_required=%u manual_lost=%d gcs_lost=%d battery_unhealthy=%d attitude_invalid=%d rates_invalid=%d fd=%d/%d\n",
			     t(failsafe.timestamp), failsafe.mode_req_other, failsafe.mode_req_prevent_arming, failsafe.mode_req_manual_control,
			     failsafe.manual_control_signal_lost, failsafe.gcs_connection_lost, failsafe.battery_unhealthy,
			     failsafe.attitude_invalid, failsafe.angular_velocity_invalid, failsafe.fd_critical_failure, failsafe.fd_esc_arming_failure);
		std::fprintf(stderr,
			     "EDF_TERMINAL_FIRST permission=%llu/%llu session=%llu armed=%d healthy=%d commission=%d command=%llu/%llu/%llu deadline=%llu edf=%.6f\n",
			     t(permission.timestamp), t(permission.deadline), t(permission.session), permission.armed, permission.healthy,
			     permission.commissioning_admitted,
			     t(command.session), t(command.sequence), t(command.timestamp_sample), t(command.deadline), static_cast<double>(command.edf));
		std::fprintf(stderr,
			     "EDF_TERMINAL_FIRST transition time=%llu session=%llu operation=%u deadline=%llu routing=%llu status_prep=%u/%llu total_deadline=%llu routing=%d/%u/%u\n",
			     t(transition.timestamp), t(transition.session), transition.operation, t(transition.deadline), t(transition.routing),
			     status.manual_preparation_status, t(status.manual_preparation_deadline), t(status.bench_session_deadline),
			     status.routing_snapshot_valid, status.inventory_fault, status.routing_fault);

		for (unsigned bank = 0; bank < 2; ++bank) {
			std::fprintf(stderr,
				     "EDF_TERMINAL_FIRST bank=%u ready=%llu/%llu/%llu/%llu activation=%llu/%llu/%llu ack=%llu/%llu/%llu/%llu/%llu report=%llu/%d/%llu\n",
				     bank, t(ready[bank].timestamp), t(ready[bank].session), t(ready[bank].prepared), t(ready[bank].deadline),
				     t(activation[bank].session), t(activation[bank].timestamp_sample), t(activation[bank].first_command_deadline),
				     t(handled[bank].session), t(handled[bank].sequence), t(handled[bank].timestamp), t(handled[bank].timestamp_sample),
				     t(handled[bank].accepted),
				     t(bank_report[bank].timestamp), bank_report[bank].ready, t(bank_report[bank].driver_instance));
		}
	}
	uORB::Publication<vehicle_attitude_s> _attitude{ORB_ID(vehicle_attitude)};
	uORB::ResearchSubscription _attitude_lifetime{ORB_ID(vehicle_attitude)};
	uORB::Publication<vehicle_angular_velocity_s> _rates{ORB_ID(vehicle_angular_velocity)};
	uORB::Publication<estimator_status_s> _estimator{ORB_ID(estimator_status)};
	uORB::Publication<estimator_status_flags_s> _flags{ORB_ID(estimator_status_flags)};
	uORB::Publication<manual_control_setpoint_s> _manual{ORB_ID(manual_control_setpoint)};
	uORB::Publication<sensor_selection_s> _selection{ORB_ID(sensor_selection)};
	uORB::Publication<battery_status_s> _power{ORB_ID(battery_status)};
	uORB::Publication<vehicle_acceleration_s> _feedback_acceleration{ORB_ID(vehicle_acceleration)};
	uORB::PublicationMulti<estimator_sensor_bias_s> _feedback_bias{ORB_ID(estimator_sensor_bias)};
	uORB::ResearchSubscription _feedback_bias_lifetime{ORB_ID(estimator_sensor_bias)};
	uORB::PublicationMulti<sensor_accel_s> _feedback_raw{ORB_ID(sensor_accel)};
	uORB::ResearchSubscription _feedback_raw_lifetime{ORB_ID(sensor_accel)};
	uORB::ResearchSubscription _power_lifetime{ORB_ID(battery_status)};
	uORB::PublicationMulti<sensor_gyro_s> _health_gyro{ORB_ID(sensor_gyro)};
	uORB::PublicationMulti<sensor_baro_s> _health_baro{ORB_ID(sensor_baro)};
	uORB::PublicationMulti<sensor_mag_s> _health_mag{ORB_ID(sensor_mag)};
	uORB::Publication<system_power_s> _health_rail{ORB_ID(system_power)};
	uORB::Subscription _observed_supervisor{ORB_ID(slaaps_status)}, _observed_vehicle{ORB_ID(vehicle_status)};
	uORB::Subscription _observed_armed{ORB_ID(actuator_armed)}, _observed_failsafe{ORB_ID(failsafe_flags)};
	uORB::Subscription _observed_power{ORB_ID(battery_status)}, _observed_permission{ORB_ID(slaaps_manual_permission)};
	uORB::Subscription _observed_command{ORB_ID(slaaps_manual_command)};
	uORB::Subscription _observed_transition{ORB_ID(slaaps_manual_transition)};
	uORB::Subscription _observed_ready[2] {{ORB_ID(slaaps_manual_ready_main)}, {ORB_ID(slaaps_manual_ready_aux)}};
	uORB::Subscription _observed_activation[2] {{ORB_ID(slaaps_manual_active_main)}, {ORB_ID(slaaps_manual_active_aux)}};
	uORB::Subscription _observed_handled[2] {{ORB_ID(slaaps_manual_ack_main)}, {ORB_ID(slaaps_manual_ack_aux)}};
	uORB::Subscription _observed_bank[2] {{ORB_ID(slaaps_output_bank), 0}, {ORB_ID(slaaps_output_bank), 1}};
	std::atomic<unsigned> _request{0}, _scenario{0}, _samples{0}, _failures{0};
	std::atomic<unsigned> _power_scenario{0}, _power_samples{0};
	std::atomic<bool> _publishing{true};
	std::atomic<bool> _estimator_only{false};
	std::atomic<bool> _feedback_enabled{false}, _feedback_hold{false};
	std::atomic<uint64_t> _feedback_generation{0}, _feedback_last_sample{0};
	std::atomic<unsigned> _feedback_bias_revision{0}, _feedback_raw_revision{0};
	std::atomic<bool> _power_publishing{true};
	std::atomic<bool> _health_publishing{false};
	std::atomic<unsigned> _health_samples{0};
	std::atomic<uint64_t> _power_window{0}, _power_last{0}, _power_previous{0}, _power_published{0};
	std::atomic<uint64_t> _power_maximum_gap{0}, _power_gap_start{0}, _power_gap_end{0}, _edf_first_terminal{0};
	std::atomic<unsigned> _power_regressions{0};
	uint64_t _edf_watched_session{}, _power_last_status_time{}; // Run queue only.
	unsigned _power_last_status_entry{}, _power_last_status_power{};
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

	if (!strcmp(operation, "health-valid")) {
		// Bind calibration slots to the synthetic device IDs only. Offsets and
		// scales keep their defaults; no measured calibration is claimed.
		const char *names[] = {"CAL_ACC0_ID", "CAL_GYRO0_ID", "CAL_MAG0_ID", "CAL_MAG0_PRIO"};
		const int32_t values[] = {NativeSlaapsSelectedInput::kHealthAccelId, NativeSlaapsSelectedInput::kHealthGyroId,
					  NativeSlaapsSelectedInput::kHealthMagId, 50
					 };

		for (unsigned i = 0; i < 4; ++i) {
			check(param_set(param_find(names[i]), &values[i]) == 0, "bind labeled synthetic calibration slots");
		}
	}

	const char *operations[] = {"stop", "resume", "neutral", "offset", "throttle-high", "invalid-state", "wrong-source", "replace",
				    "power-valid", "power-drop", "power-disconnected", "power-warning", "power-fault", "power-off",
				    "power-low", "power-high", "power-id", "power-source", "power-replace", "estimator-only",
				    "feedback-valid", "feedback-hold", "feedback-bias-replace", "feedback-raw-replace",
				    "health-valid", "health-stop"
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
	unsigned handledWrites() const { return _handled_writes.load(); }
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
	float lastHandledOutput(unsigned channel) const { return _last_handled_output[channel]; }
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

		if (success) {
			memcpy(_last_handled_output, outputs, sizeof(_last_handled_output));
			++_handled_writes;
		}

		return success;
	}
private:
	MixingOutput _mixing;
	uORB::Subscription _ack;
	uORB::Publication<slaaps_manual_permission_s> *_revoke_publisher{};
	slaaps_manual_permission_s _denied_permission{};
	float _last_output[MAX_ACTUATORS] {};
	float _last_handled_output[MAX_ACTUATORS] {};
	uint64_t _last_write_time{}, _expected_session{}, _expected_sequence{};
	unsigned _last_count{};
	bool _premature_ack{}, _fail_next{}, _trace_pending{};
	std::atomic<bool> _running{false};
	std::atomic<unsigned> _writes{0}, _handled_writes{0}, _nonzero{0};
};

static void installNativeMixerConfiguration(const VehicleConfiguration &c)
{
#define SLAAPS_FLOAT(field, name, group, description, unit, minimum, maximum, initial) \
	check(param_set(param_find(#name), &c.field) == 0, "install labeled synthetic native configuration");
#define SLAAPS_INT SLAAPS_FLOAT
#include <lib/slaaps/VehicleParameters.def>
#undef SLAAPS_INT
#undef SLAAPS_FLOAT
	const char *imu_parameters[kAxes] {"EKF2_IMU_POS_X", "EKF2_IMU_POS_Y", "EKF2_IMU_POS_Z"};

	for (unsigned axis = 0; axis < kAxes; ++axis) {
		check(param_set(param_find(imu_parameters[axis]), &c.feedback.imu_position[axis]) == 0,
		      "install explicit synthetic native CG-to-IMU position intent");
	}

	check(param_set(param_find("IMU_ACCEL_CUTOFF"), &c.feedback.cutoff_hz) == 0,
	      "install explicit synthetic native filter intent rather than assume its default");
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
class SlaapsNativeTestSubject;
struct NativeBenchCancelRecord {
	uint64_t now{}, session{}, first_deadline{}, total_deadline{}, handled{}, routing_time{}, configuration{};
	uint64_t reported[2] {}, driver[2] {};
	unsigned scenario{}, line{}, requested{}, entry{}, worker{}, preparation{}, inventory{}, routing{}, channels{};
	unsigned publisher[2] {};
	int32_t apply{}, use{};
	bool normal_stop{}, candidate{}, eligible{}, active{}, activation_owned{}, pending{}, power_valid{}, power_bound{};
	bool arming_authorized{}, edf_granted{}, input_bound{};
	slaaps::EdfPermission permission{};
	slaaps::PowerSample power{};
	research_stabilized_manual::ManualSample raw{};
	slaaps::ManualPreparationRequest request{};
};
// One supervisor-queue writer. Published entries are immutable; the fixture
// thread reads only the prefix exposed by the release/acquire count.
static NativeBenchCancelRecord native_bench_cancels[16] {};
static std::atomic<unsigned> native_bench_cancel_count{0}, native_bench_cancel_overflow{0};
static std::atomic<unsigned> native_bench_cancel_scenario{UINT32_MAX};
struct NativeManualDisarmInjection {
	uORB::Publication<vehicle_status_s> *vehicle{};
	uORB::Publication<actuator_armed_s> *armed{};
	std::atomic<unsigned> requested{0}, completed{0}; // 1 vehicle first, 2 actuator first.
	std::atomic<bool> at_capture{false};
	uint64_t injected_us{};
	bool original_both_armed{}, published{};
};
// Installed before scheduling and cleared after final subject destruction.
// Only the request/result handshake changes during the scheduled lifetime.
static NativeManualDisarmInjection *native_manual_disarm{};
struct NativeAccelerationCapture {
	uint64_t source{}, session{}, sequence{}, configuration{}, captured_us{}, observed_us{};
	uint64_t producer_created{}, producer_expiry{}, mapped_created{}, mapped_expiry{}, evidence_deadline{};
};
// One supervisor-queue writer; published records never change. The fixture
// reads only the prefix released through the count, never mutable owner data.
static NativeAccelerationCapture native_acceleration_captures[4] {};
static std::atomic<unsigned> native_acceleration_capture_count{0}, native_acceleration_capture_overflow{0};
struct SlaapsNativeTest {
	static std::atomic<bool> admitted;
	static void companionTransportTests();
	static void companionEntryTests();
	static void beforeBenchCancel(const SlaapsNativeTestSubject &, hrt_abstime, uint8_t, bool, unsigned);
	static void printBenchCancels();
	static void beforeManualPermission();
	static void beforeAccelerationCapture();
	static void afterAccelerationCapture(const slaaps::AccelerationControlInput &input);
	static void injectOrdinaryDisarm(bool at_capture);
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

void SlaapsNativeTest::beforeManualPermission()
{
	injectOrdinaryDisarm(false);
}

void SlaapsNativeTest::beforeAccelerationCapture()
{
	injectOrdinaryDisarm(true);
}

void SlaapsNativeTest::afterAccelerationCapture(const slaaps::AccelerationControlInput &input)
{
	if (input.request.session != 7303 || input.request.acceleration_frd_m_s2(1) <= .5F) { return; }

	const unsigned index = native_acceleration_capture_count.load(std::memory_order_relaxed);

	if (index >= sizeof(native_acceleration_captures) / sizeof(native_acceleration_captures[0])) {
		native_acceleration_capture_overflow.fetch_add(1, std::memory_order_relaxed);
		return;
	}

	auto &record = native_acceleration_captures[index];
	record.source = input.request.source;
	record.session = input.request.session;
	record.sequence = input.request.sequence;
	record.configuration = input.configuration;
	record.captured_us = input.now_us;
	record.observed_us = input.context.validator_receive_time_us;
	record.producer_created = input.request.producer_monotonic_us;
	record.producer_expiry = input.request.producer_expiry_monotonic_us;
	record.mapped_created = input.request.producer_time_in_validator_domain_us;
	record.mapped_expiry = input.request.expiry_time_in_validator_domain_us;
	record.evidence_deadline = input.evidence_deadline_us;
	native_acceleration_capture_count.store(index + 1, std::memory_order_release);
}

void SlaapsNativeTest::injectOrdinaryDisarm(bool at_capture)
{
	auto *injection = native_manual_disarm;

	if (!injection) { return; }

	const unsigned requested = injection->requested.load(std::memory_order_acquire);

	if (!requested || injection->completed.load(std::memory_order_acquire)
	    || injection->at_capture.load(std::memory_order_acquire) != at_capture) { return; }

	uORB::Subscription vehicle_sub{ORB_ID(vehicle_status)}, armed_sub{ORB_ID(actuator_armed)};
	vehicle_status_s vehicle{};
	actuator_armed_s armed{};
	const bool copied = vehicle_sub.copy(&vehicle) && armed_sub.copy(&armed);
	injection->original_both_armed = copied && vehicle.arming_state == vehicle_status_s::ARMING_STATE_ARMED && armed.armed;
	injection->injected_us = hrt_absolute_time();
	vehicle.timestamp = armed.timestamp = injection->injected_us;

	if (requested == 1) { vehicle.arming_state = vehicle_status_s::ARMING_STATE_DISARMED; }

	else { armed.armed = false; }

	injection->published = copied && injection->vehicle->publish(vehicle) && injection->armed->publish(armed);
	// Pump publishes no authority while this is pending. Once released it can
	// publish only this same partial-disarm tuple until the fixture completes it.
	injection->completed.store(requested, std::memory_order_release);
}

void SlaapsNativeTest::beforeBenchCancel(const SlaapsNativeTestSubject &subject, hrt_abstime now, uint8_t requested,
		bool normal_stop, unsigned line)
{
	if (subject._prepared_request.mode != slaaps::ControlMode::EdfBench || !subject._preparation_session) { return; }

	const unsigned index = native_bench_cancel_count.load(std::memory_order_relaxed);

	if (index && native_bench_cancels[index - 1].session == subject._preparation_session) { return; }

	if (index >= sizeof(native_bench_cancels) / sizeof(native_bench_cancels[0])) { ++native_bench_cancel_overflow; return; }

	auto &record = native_bench_cancels[index];
	record.now = now; record.session = subject._preparation_session; record.line = line; record.requested = requested;
	record.scenario = native_bench_cancel_scenario.load(std::memory_order_acquire);
	record.normal_stop = normal_stop; record.entry = subject._bench_entry_status;
	record.worker = static_cast<unsigned>(subject._manual_status); record.preparation = static_cast<unsigned>(subject._preparation_status);
	record.candidate = subject._configuration.hasCandidate(); record.eligible = subject._configuration.eligible();
	record.configuration = subject._configuration.identity(); record.apply = subject._last_apply; record.use = subject._selected_use;
	record.active = subject._manual.active(); record.handled = subject._manual.handledSequence();
	record.activation_owned = subject._manual_activation_owned; record.pending = subject._preparation_pending;
	record.arming_authorized = subject._bench_arming_authorized; record.edf_granted = subject._bench_edf_granted;
	record.input_bound = subject._bench_input_bound;
	record.first_deadline = subject._manual_first_command_deadline; record.total_deadline = subject._bench_deadline;
	record.inventory = static_cast<unsigned>(subject._inventory_fault); record.routing = static_cast<unsigned>(subject._routing_fault);
	record.routing_time = subject._routing_snapshot.captured_us; record.channels = subject._routing_snapshot.inventory.count;
	record.permission = subject._manual_permission; record.power = subject._power_sample;
	record.power_valid = subject._power_valid; record.power_bound = subject._power_bound;
	record.raw = subject._bench_sample; record.request = subject._prepared_request;

	for (unsigned bank = 0; bank < 2; ++bank) {
		record.reported[bank] = subject._routing_snapshot.reported_us[bank]; record.driver[bank] = subject._routing_snapshot.driver[bank];
		record.publisher[bank] = subject._routing_snapshot.publisher_revision[bank];
	}

	native_bench_cancel_count.store(index + 1, std::memory_order_release);
}

void SlaapsNativeTest::printBenchCancels()
{
	const unsigned count = native_bench_cancel_count.load(std::memory_order_acquire);
	const auto t = [](uint64_t value) { return static_cast<unsigned long long>(value); };
	std::fprintf(stderr, "EDF_PRE_CANCEL records=%u overflow=%u supervisor_queue_before_cleanup=1\n", count,
		     native_bench_cancel_overflow.load());

	for (unsigned i = 0; i < count; ++i) {
		const auto &r = native_bench_cancels[i];
		std::fprintf(stderr,
			     "EDF_PRE_CANCEL line=%u scenario=%u now=%llu session=%llu requested=%u normal_stop=%d entry=%u worker=%u active=%d handled=%llu\n",
			     r.line, r.scenario, t(r.now), t(r.session), r.requested, r.normal_stop, r.entry, r.worker, r.active, t(r.handled));
		std::fprintf(stderr,
			     "EDF_PRE_CANCEL candidate=%d eligible=%d identity=%llu use=%d apply=%d preparation=%u pending=%d activation=%d authorized=%d granted=%d input_bound=%d first_deadline=%llu total=%llu\n",
			     r.candidate, r.eligible, t(r.configuration), r.use, r.apply, r.preparation, r.pending, r.activation_owned,
			     r.arming_authorized, r.edf_granted, r.input_bound, t(r.first_deadline), t(r.total_deadline));
		std::fprintf(stderr,
			     "EDF_PRE_CANCEL inventory=%u routing=%u captured=%llu channels=%u reports=%llu/%llu drivers=%llu/%llu publishers=%u/%u\n",
			     r.inventory, r.routing, t(r.routing_time), r.channels, t(r.reported[0]), t(r.reported[1]), t(r.driver[0]), t(r.driver[1]), r.publisher[0],
			     r.publisher[1]);
		std::fprintf(stderr,
			     "EDF_PRE_CANCEL permission=%llu/%llu armed=%d healthy=%d commissioning=%d power_valid=%d bound=%d power=%llu/%llu/%.6f raw=%llu/%llu/%.6f request=%llu/%llu\n",
			     t(r.permission.timestamp_us), t(r.permission.deadline_us), r.permission.armed, r.permission.healthy, r.permission.commissioning_admitted,
			     r.power_valid, r.power_bound, t(r.power.timestamp_us), t(r.power.deadline_us), static_cast<double>(r.power.voltage_v),
			     t(r.raw.timestamp_us), t(r.raw.timestamp_sample_us), static_cast<double>(r.raw.axes[3]), t(r.request.timestamp_us),
			     t(r.request.deadline_us));
	}
}

void SlaapsNativeTest::companionEntryTests()
{
	// This runs only in the isolated synthetic-authority supervisor process.
	// The actual-Commander loopback process never publishes this fixture witness.
	using namespace research_acceleration;
	struct InputBytes { unsigned char value[sizeof(AccelerationControlInput)]; };
	auto objectBytes = [](const AccelerationControlInput & input) {
		InputBytes bytes{};
		std::memcpy(bytes.value, &input, sizeof(input));
		return bytes;
	};
	uORB::Publication<slaaps_vehicle_state_s> witness_pub{ORB_ID(slaaps_vehicle_state)};
	uORB::Publication<vehicle_attitude_s> attitude_pub{ORB_ID(vehicle_attitude)};
	uORB::Publication<vehicle_angular_velocity_s> rates_pub{ORB_ID(vehicle_angular_velocity)};
	uORB::Publication<estimator_status_s> estimator_pub{ORB_ID(estimator_status)};
	uORB::Publication<estimator_status_flags_s> flags_pub{ORB_ID(estimator_status_flags)};
	uORB::Publication<sensor_selection_s> selection_pub{ORB_ID(sensor_selection)};
	uORB::Publication<actuator_armed_s> armed_pub{ORB_ID(actuator_armed)};
	uORB::PublicationMulti<sensor_accel_s> raw_pub{ORB_ID(sensor_accel)};
	uORB::PublicationMulti<estimator_sensor_bias_s> bias_pub{ORB_ID(estimator_sensor_bias)};
	uORB::Publication<vehicle_acceleration_s> acceleration_pub{ORB_ID(vehicle_acceleration)};
	uORB::ResearchSubscription raw_lifetime{ORB_ID(sensor_accel)};
	uORB::ResearchSubscription bias_lifetime{ORB_ID(estimator_sensor_bias)};
	auto c = fixture();
	c.state_age_ms = 500;
	c.selected_status_age_us = c.actuator_permission_age_us = 1000000;
	c.companion = {1, 3, 42, 191, 16777217, 500000};
	c.companion.admission_enabled = 1;
	c.companion.disarmed_policy = 2;
	c.companion.acceleration_norm_max = 8.F;

	for (unsigned axis = 0; axis < kAxes; ++axis) {
		c.companion.acceleration_minimum[axis] = -5.F;
		c.companion.acceleration_maximum[axis] = 5.F;
	}

	c.companion.minimum_validity_us = 1000;
	c.companion.maximum_validity_us = c.companion.maximum_command_age_us = 1000000;
	c.companion.maximum_future_skew_us = 500000;
	c.companion.maximum_clock_uncertainty_us = 10000;
	c.companion.maximum_relative_drift_ppm = 1;
	c.companion.timestamp_uncertainty_us = 1;
	c.companion.maximum_round_trip_us = 10000;
	c.companion.map_interval_us = 500000; // Explicit short synthetic renewal interval.
	const SessionIngressVerdict ingress{true, true, true, true};

	for (unsigned scenario = 0; scenario < 26; ++scenario) {
		const bool feedback_case = scenario >= 12;

		if (feedback_case) {
			c.feedback.enabled = 1;
			c.feedback.maximum_age_us = c.feedback.maximum_bias_age_us = 100000;
			c.feedback.latency_bound_us = c.feedback.settling_us = 1000;
			c.feedback.maximum_specific_force_m_s2 = 50.F;
			c.feedback.maximum_acceleration_m_s2 = 20.F;
			c.feedback.gravity_m_s2 = 9.8F;
			c.state_skew_ms = 500; // Isolate original feedback expiry from combined raw/state skew.
		}

		bool feedback_hold = false;
		auto subject = std::make_unique<SlaapsNativeTestSubject>();
		check(subject->_configuration.commit(c, ConfigurationUse::StabilizedManual).valid(),
		      "isolated companion entry uses an accepted flight-scope synthetic profile");
		subject->_selected_use = 1;
		subject->_last_apply = 1;
		subject->_modes[0].id = vehicle_status_s::NAVIGATION_STATE_EXTERNAL1;
		subject->_modes[1].id = vehicle_status_s::NAVIGATION_STATE_EXTERNAL2;
		subject->_modes[0].configured = true;
		subject->_modes[1].configured = scenario != 2;
		auto &owner = subject->_companion_protocol;
		check(owner.initialize(1000 + scenario), "isolated owner has a distinct synthetic controller lifetime");
		slaaps_vehicle_state_s witness{};
		witness.transition = 1;
		witness.arming_state = vehicle_status_s::ARMING_STATE_DISARMED;
		witness.nav_state = subject->_modes[scenario == 0 || scenario == 10 || scenario == 13 ? 1 : 0].id;
		vehicle_attitude_s attitude{};
		attitude.q[0] = 1.F;
		vehicle_angular_velocity_s rates{};
		estimator_status_s estimator{};
		estimator_status_flags_s flags{};
		sensor_selection_s selection{};
		actuator_armed_s armed{};
		estimator.gyro_device_id = selection.gyro_device_id = 123;
		estimator.accel_device_id = selection.accel_device_id = 456;
		flags.cs_tilt_align = flags.cs_yaw_align = true;
		auto publish_state = [&]() {
			const uint64_t now = hrt_absolute_time();

			if (feedback_case) {
				// Real native topic lifetimes and applied metadata; explicitly
				// synthetic sensor values, never a feedback-ready boolean bypass.
				sensor_accel_s raw{};
				raw.timestamp = raw.timestamp_sample = now;
				raw.device_id = 456; raw.z = -9.8F;
				estimator_sensor_bias_s bias{};
				bias.timestamp = bias.timestamp_sample = now;
				bias.accel_device_id = 456; bias.accel_bias_valid = true;
				check(raw_pub.publish(raw) && bias_pub.publish(bias)
				      && raw_pub.get_instance() == 0 && bias_pub.get_instance() == 0
				      && raw_lifetime.revision() && bias_lifetime.revision(), "capture fixture binds actual raw and bias publishers");

				if (!feedback_hold) {
					vehicle_acceleration_s acceleration{};
					acceleration.timestamp = acceleration.timestamp_sample = now;
					acceleration.xyz[2] = -9.8F;
					acceleration.device_id = 456; acceleration.calibrated = true; acceleration.source_generation = 1;
					acceleration.bias_valid = true;
					acceleration.bias_timestamp = acceleration.bias_timestamp_sample = now;
					acceleration.bias_publisher_revision = bias_lifetime.revision();
					acceleration.raw_publisher_revision = raw_lifetime.revision();
					check(acceleration_pub.publish(acceleration), "publish original selected applied acceleration metadata");
				}
			}

			witness.timestamp = attitude.timestamp = attitude.timestamp_sample = rates.timestamp = rates.timestamp_sample = now;
			estimator.timestamp = flags.timestamp = selection.timestamp = armed.timestamp = now;
			armed.armed = witness.armed;
			armed.prearmed = witness.prearmed;
			check(witness_pub.publish(witness) && attitude_pub.publish(attitude) && rates_pub.publish(rates)
			      && estimator_pub.publish(estimator) && flags_pub.publish(flags) && selection_pub.publish(selection)
			      && armed_pub.publish(armed), "publish coherent isolated state without a pilot or actuator command");
		};
		publish_state();
		subject->updateCompanion(hrt_absolute_time());
		check(owner._configured && owner._allow_entry_transitions == (scenario != 2),
		      "production caller explicitly permits protocol entry only with accepted profile and registered modes");

		if (feedback_case) {
			const uint64_t settle_end = hrt_absolute_time() + static_cast<uint64_t>(c.feedback.maximum_age_us);

			while (owner._feedback_result != AccelerationFeedbackResult::Ready && hrt_absolute_time() < settle_end) {
				px4_usleep(1000);
				publish_state();
				uint64_t observation = hrt_absolute_time();
				(void)owner.observe(observation);
			}

			check(owner._feedback_required && owner._feedback_result == AccelerationFeedbackResult::Ready,
			      "actual selected feedback reaches Ready only after configured settling and fresh samples");
		}

		auto observe = [&]() {
			publish_state();
			uint64_t now = hrt_absolute_time();
			check(owner.observe(now) && owner._state.request_guard_context.required_state_valid,
			      "actual owner observes the current isolated Commander and selected-state publications");
		};
		owner._connection_deadline = hrt_absolute_time() + 1000000;
		observe();
		uint64_t now = hrt_absolute_time();
		// Trusted clock/session staging is synthetic here; signed ingress and
		// matched TIMESYNC are independently covered by the existing wire test.
		owner._prepared = true;
		owner._prepared_session = 7000 + scenario;
		owner._state.map = {true, static_cast<uint64_t>(c.companion.source_id), owner._prepared_session,
				    owner._identity, 1, 0, 10, now - 1000, now + (scenario == 9 ? 50000 : c.companion.map_interval_us)
				   };
		owner._map_epoch = owner._state.map.map_epoch;
		owner._map_deadline = owner._state.map.valid_until_producer_us - owner._state.map.uncertainty_us;
		owner._signing_generation = 7;
		observe();
		owner._proposal = {owner._identity, owner._snapshot.transition_epoch, owner._state.map.map_epoch,
				   owner._state.map.source, owner._state.map.session, 1, 0, 0, kWireProtocolVersionV1, 1, 1, 1, 0, {}
				  };
		owner._proposal_ingress = ingress;
		owner._proposal_deadline = hrt_absolute_time() + 1000000;
		now = hrt_absolute_time();
		owner.action(2, now);
		check(owner._status.action_result == 1 && owner._adapter->sessionSnapshot().gate == SessionGate::Established
		      && owner._entry_authorized == (scenario != 2), "actual disarmed Grant binds one-use entry preauthorization");
		const auto original_map = owner._state.map;
		const uint64_t original_deadline = owner._map_deadline;
		uint64_t wire_operation = 1;

		if (scenario == 0) {
			// Exercise the actual owner publishers without a timing/sleep seam:
			// ordinary correlated report traffic overruns its eight slots while
			// the separately published exact barrier must remain available.
			uORB::ResearchSubscription priority{ORB_ID::slaaps_companion_barrier}, ordinary{ORB_ID::slaaps_companion_tx};
			uint8_t expected_payload[106] {};
			check(owner._have_barrier && encodeStatusV1Payload(owner._barrier, expected_payload) == StatusEncodeCode::Ok
			      && owner.send(owner._barrier, hrt_absolute_time(), true), "publish the exact retained core barrier on its isolated alias");
			const uint64_t first_operation = wire_operation + 1;

			for (unsigned report = 0; report < slaaps_companion_tx_s::ORB_QUEUE_LENGTH + 3; ++report) {
				auto repeated = owner._proposal;
				repeated.transition_epoch = owner._snapshot.transition_epoch;
				repeated.operation_sequence = ++wire_operation;
				const auto result = owner._adapter->processSession(owner._state, repeated, ingress, nullptr);
				check(result.code != AdminCode::Completed && result.report_count,
				      "ordinary burst uses genuine correlated rejected SESSION reports");
				owner.reports(result, hrt_absolute_time());
			}

			slaaps_companion_tx_s barrier_envelope{}, ordinary_envelope{};
			unsigned barrier_revision{}, ordinary_revision{}, barriers{}, operations{};

			// A failed read clears its revision output by contract. Keep only the revision of the last
			// successful read, exactly as the MAVLink barrier drain does.
			for (unsigned report = 0; report < slaaps_companion_tx_s::ORB_QUEUE_LENGTH; ++report) {
				unsigned revision{};

				if (!priority.copyQueuedBound(&barrier_envelope, revision)) { break; }

				barrier_revision = revision;
				++barriers;
			}

			check(barriers && barrier_revision == priority.revision()
			      && std::memcmp(barrier_envelope.payload, expected_payload, sizeof(expected_payload)) == 0,
			      "ordinary queue overrun cannot overwrite or reconstruct the exact retained barrier payload");

			for (unsigned report = 0; report < slaaps_companion_tx_s::ORB_QUEUE_LENGTH; ++report) {
				unsigned revision{};

				if (!ordinary.copyQueuedBound(&ordinary_envelope, revision)) { break; }

				ordinary_revision = revision;
				mavlink_message_t encoded{};
				encoded.msgid = MAVLINK_MSG_ID_RESEARCH_ACCELERATION_STATUS;
				encoded.len = sizeof(ordinary_envelope.payload);
				std::memcpy(_MAV_PAYLOAD_NON_CONST(&encoded), ordinary_envelope.payload, sizeof(ordinary_envelope.payload));
				const auto correlation = mavlink_msg_research_acceleration_status_get_correlation_sequence(&encoded);
				check(mavlink_msg_research_acceleration_status_get_status_class(&encoded) == 2
				      && correlation >= first_operation && correlation <= wire_operation,
				      "isolating barriers preserves the bounded ordinary correlated-rejection queue");
				++operations;
			}

			check(operations == slaaps_companion_tx_s::ORB_QUEUE_LENGTH && ordinary_revision == ordinary.revision(),
			      "the last eight correlated operation reports remain available independently of the priority alias");
		}

		DecodedSessionV1 old_ack{};
		auto acknowledge = [&]() {
			uint64_t producer{};

			do { producer = hrt_absolute_time(); } while (producer <= owner._snapshot.barrier_validator_time_us + 2 * owner._state.map.uncertainty_us);

			observe();
			DecodedSessionV1 ack{owner._identity, owner._snapshot.transition_epoch, owner._state.map.map_epoch,
					     owner._state.map.source, owner._state.map.session, ++wire_operation, producer,
					     owner._snapshot.barrier_report_sequence, kWireProtocolVersionV1, 1, 1, 3, 0, {}};
			const auto result = owner._adapter->processAcknowledgement(owner._state, ack, ingress);
			owner.reports(result, owner._state.request_guard_context.validator_receive_time_us);
			check(result.code == IngressCode::Completed && result.admission.acknowledgement_current,
			      "same real adapter accepts only a fresh exact current-epoch ACK");
			old_ack = ack;
		};
		acknowledge();
		auto preserved = [&](uint64_t previous_epoch) {
			check(owner._state.map.valid && owner._map_deadline == original_deadline
			      && owner._state.map.source == original_map.source && owner._state.map.session == original_map.session
			      && owner._state.map.map_epoch == original_map.map_epoch && owner._state.map.valid_until_producer_us == original_map.valid_until_producer_us
			      && owner._snapshot.transition_epoch == previous_epoch + 1
			      && owner._adapter->sessionSnapshot().gate == SessionGate::Established
			      && !owner._status.acknowledgement_current,
			      "expected entry preserves original session/map deadline but invalidates the prior epoch ACK");
			const auto replay = owner._adapter->processAcknowledgement(owner._state, old_ack, ingress);
			check(replay.code != IngressCode::Completed && !replay.admission.acknowledgement_current,
			      "prior epoch ACK cannot reopen entry after a genuine Commander transition");

			if (scenario == 13 && witness.armed && witness.nav_state == subject->_modes[1].id) {
				AccelerationControlInput sentinel{};
				sentinel.previous_handled_us = 123;
				const auto before = objectBytes(sentinel);
				check(owner._feedback_result == AccelerationFeedbackResult::Ready
				      && !owner.captureControlInput(fingerprint(c), sentinel)
				      && owner.awaitingControlInput()
				      && std::memcmp(&sentinel, before.value, sizeof(sentinel)) == 0,
				      "old ACK cannot capture a full command despite selected mode and actual ready feedback");
			}
		};

		if (scenario == 1) {
			const uint64_t epoch = owner._snapshot.transition_epoch;
			witness.prearmed = true;
			++witness.transition;
			observe();
			preserved(epoch);
			acknowledge();
		}

		uint64_t epoch = owner._snapshot.transition_epoch;
		witness.armed = true;
		witness.arming_state = vehicle_status_s::ARMING_STATE_ARMED;
		++witness.transition;

		if (scenario == 4) { witness.kill = true; }

		if (scenario == 5) { ++witness.transition; }

		if (scenario == 6) { witness.nav_state = vehicle_status_s::NAVIGATION_STATE_MANUAL; }

		if (scenario == 7) { ++attitude.quat_reset_counter; }

		if (scenario == 8) {
			subject->_modes[1].configured = false;
			publish_state();
			subject->updateCompanion(hrt_absolute_time());

		} else if (scenario == 9) {
			while (hrt_absolute_time() < original_deadline) { px4_usleep(100); }
		}

		publish_state();
		now = hrt_absolute_time();
		(void)owner.observe(now);

		if (scenario == 3) {
			preserved(epoch);
			witness.armed = false;
			witness.arming_state = vehicle_status_s::ARMING_STATE_DISARMED;
			++witness.transition;
			observe();
		}

		if (scenario >= 2 && scenario <= 9) {
			check(!owner._state.map.valid && !owner._entry_authorized && !owner._prepared
			      && owner._adapter->sessionSnapshot().gate != SessionGate::Established,
			      "unauthorized, unhealthy, skipped, exited, replaced or expired entry revokes rather than reviving");
			continue;
		}

		preserved(epoch);
		acknowledge();

		if (scenario == 11) {
			const uint64_t administration = owner._admin_sequence;
			now = hrt_absolute_time();
			owner.action(2, now);
			check(owner._status.action_result == 2 && owner._admin_sequence == administration,
			      "armed Establish remains rejected without consuming a fresh administrative event");
		}

		if (witness.nav_state != subject->_modes[1].id) {
			epoch = owner._snapshot.transition_epoch;
			witness.nav_state = subject->_modes[1].id;
			++witness.transition;
			observe();
			preserved(epoch);
			acknowledge();
		}

		uint64_t creation{};
		do { creation = hrt_absolute_time(); } while (creation <= old_ack.producer_acknowledgement_monotonic_us + 2 *
				owner._state.map.uncertainty_us);

		observe();
		DecodedRequestV1 request{};
		request.target_system = request.target_component = 1;
		request.wire = {original_map.source, original_map.session, 1, creation, creation + 375000,
				owner._identity, owner._snapshot.transition_epoch, original_map.map_epoch,
				0.F, 0.F, 0.F, kWireProtocolVersionV1, kWireFrameFrdV1, kWireQuantityInertialAccelerationV1, kWireReferencePointCgV1
			       };

		if (feedback_case) {
			request.wire.protocol_version = kWireProtocolVersionV2;
			request.flags = kWireFullCommandFlagsV2;
			request.wire.desired_attitude_ned_from_body[0] = 1.F;
		}

		const auto accepted = owner._adapter->processRequest(owner._state, request, ingress);
		CanonicalRequest eligible{};
		const auto accessed = owner._adapter->getEligibleRequest(owner._state, eligible);
		check(accepted.code == IngressCode::Completed && accessed.code == AdapterCode::EligibleRequest
		      && eligible.session == original_map.session && eligible.producer_monotonic_us == creation,
		      "only the new post-entry ACK and request become eligible without any output grant");

		if (feedback_case) {
			AccelerationControlInput captured{};
			check(owner.captureControlInput(fingerprint(c), captured)
			      && !owner.awaitingControlInput()
			      && captured.request.protocol_version == kWireProtocolVersionV2
			      && captured.request.command_kind == CommandKind::AccelerationAndAttitude
			      && near(captured.request.desired_attitude_ned_from_body(0), 1.F)
			      && captured.feedback.state_source_epoch == captured.selected_state.source_epoch
			      && captured.feedback.attitude_reset_counter == captured.selected_state.attitude_reset_counter
			      && captured.feedback.publisher_revision && captured.feedback.source_generation
			      && captured.evidence_deadline_us > captured.now_us
			      && captured.evidence_deadline_us <= original_deadline
			      && captured.previous_handled_us == 0 && captured.actual_edf_deadline_us == 0,
			      "production full-v2 capture copies actual coherent selected feedback without inventing worker history");

			if (scenario == 12) {
				using Binding = research_stabilized_manual::PublisherBinding;
				research_stabilized_manual::SelectedStateObservation independent;
				publish_state();
				auto expected = independent.captureState(static_cast<uint64_t>(c.selected_status_age_us),
						static_cast<uint64_t>(c.actuator_permission_age_us));
				check(expected.state.valid && expected.permission_fresh
				      && (expected.required_topics & Binding::kStateTopicsMask) == Binding::kStateTopicsMask,
				      "second real state-only observer captures the same selected native publishers");
				// Observer-local counters need not have the same observation history.
				// Alter only this local counter; every real revision/device/reset stays intact.
				expected.state.source_epoch = owner._previous_state.source_epoch == UINT32_MAX
							      ? 1U : owner._previous_state.source_epoch + 1U;
				check(expected.state.source_epoch != owner._previous_state.source_epoch
				      && owner.captureControlInput(fingerprint(c), expected, captured)
				      && !owner.awaitingControlInput()
				      && captured.selected_state.source_epoch == owner._previous_state.source_epoch
				      && captured.feedback.state_source_epoch == captured.selected_state.source_epoch,
				      "coherent publisher binding accepts unequal observer-local epochs and returns owner-local provenance");

				for (unsigned mismatch = 0; mismatch < Binding::kTopics + 3; ++mismatch) {
					if (mismatch < Binding::kTopics && !(Binding::kStateTopicsMask & (1U << mismatch))) { continue; }

					auto wrong = expected;

					if (mismatch < Binding::kTopics) {
						wrong.revisions[mismatch] = wrong.revisions[mismatch] == std::numeric_limits<unsigned>::max()
									    ? wrong.revisions[mismatch] - 1U : wrong.revisions[mismatch] + 1U;

					} else if (mismatch == Binding::kTopics) { wrong.gyro ^= 1U; }

					else if (mismatch == Binding::kTopics + 1) { wrong.accel ^= 1U; }

					else { wrong.state.attitude_reset_counter ^= 1U; }

					publish_state();
					const auto before = objectBytes(captured);
					check(!owner.captureControlInput(fingerprint(c), wrong, captured)
					      && !owner.awaitingControlInput()
					      && std::memcmp(&captured, before.value, sizeof(captured)) == 0,
					      "inconsistent real publisher revision device or reset binding rejects capture without changing output");
				}

				publish_state();
				check(owner.captureControlInput(fingerprint(c), expected, captured) && !owner.awaitingControlInput(),
				      "rejected cross-observer bindings never replace or consume the valid full command");
			}

			if (scenario == 14) {
				witness.nav_state = subject->_modes[0].id; ++witness.transition;
				publish_state();
				const auto before = objectBytes(captured);
				check(!owner.captureControlInput(fingerprint(c), captured) && std::memcmp(&captured, before.value, sizeof(captured)) == 0
				      && !owner.awaitingControlInput()
				      && owner._state.request_guard_context.mode_state == ResearchModeState::NotSelected,
				      "actual mode exit rejects full capture and preserves caller output");
				continue;
			}

			if (scenario == 15) {
				const uint64_t feedback_deadline = owner._feedback_sample.deadline_us;
				check(original_deadline > feedback_deadline
				      && request.wire.producer_expiry_monotonic_us - owner._state.map.uncertainty_us > feedback_deadline,
				      "original map and request outlast the isolated applied-feedback expiry");
				feedback_hold = true;

				while (hrt_absolute_time() < feedback_deadline) { px4_usleep(1000); publish_state(); }

				publish_state();
				const auto before = objectBytes(captured);
				check(!owner.captureControlInput(fingerprint(c), captured) && std::memcmp(&captured, before.value, sizeof(captured)) == 0
				      && !owner.awaitingControlInput()
				      && owner._feedback_result == AccelerationFeedbackResult::Stale
				      && owner._state.request_guard_context.mode_state == ResearchModeState::Selected
				      && hrt_absolute_time() < original_deadline,
				      "held applied feedback expires while actual raw bias state and mode stay fresh");
				continue;
			}

			if (scenario >= 16) {
				uORB::Publication<slaaps_companion_packet_s> receiver{ORB_ID(slaaps_companion_packet_3)};
				check(receiver.advertise(), "ordinary-disarm capture pins a real receiver lifetime");
				owner._receiver_revision = owner._input[3].revision();
				owner._receiver_system = owner._receiver_component = 1;
				using Binding = research_stabilized_manual::PublisherBinding;
				research_stabilized_manual::SelectedStateObservation independent;
				bool supplied_new_source = false;

				if (scenario == 17 || scenario == 20 || scenario == 25) {
					if (scenario == 25) { witness.nav_state = subject->_modes[0].id; ++witness.transition; }

					publish_state();
					armed.armed = false; // Actuator publication arrives before the atomic witness.
					armed.kill = scenario == 20;
					check(armed_pub.publish(armed), "publish first actual actuator disarm independently of the held Commander witness");

				} else {
					witness.arming_state = vehicle_status_s::ARMING_STATE_DISARMED;

					if (scenario != 16) { witness.armed = false; }

					++witness.transition;
					publish_state();
				}

				if (scenario == 19) {
					witness.timestamp = hrt_absolute_time() - static_cast<uint64_t>(c.actuator_permission_age_us) - 1;
					check(witness_pub.publish(witness), "publish stale disarm authority alongside fresh selected state");
				}

				if (scenario == 21) {
					using AttitudePublication = uORB::Publication<vehicle_attitude_s>;
					attitude_pub.~AttitudePublication();
					new (&attitude_pub) AttitudePublication{ORB_ID(vehicle_attitude)};
					attitude.timestamp = attitude.timestamp_sample = hrt_absolute_time();
					check(attitude_pub.publish(attitude), "replace the attitude publisher concurrently with ordinary disarm");
					supplied_new_source = true;
				}

				auto selected = independent.captureState(c.selected_status_age_us, c.actuator_permission_age_us);
				check(selected.state.valid && selected.permission_fresh
				      && (!supplied_new_source || selected.revisions[0] != owner._state_revisions[0])
				      && (selected.required_topics & Binding::kStateTopicsMask) == Binding::kStateTopicsMask,
				      "disarm capture receives a fresh actual selected snapshot, including the replacement source when requested");

				if (scenario == 22) { owner._connection_deadline = hrt_absolute_time(); }

				if (scenario == 24) { owner._continuity_fault = true; }

				const auto before = objectBytes(captured);
				const uint64_t configuration = scenario == 23 ? fingerprint(c) ^ 1U : fingerprint(c);
				const bool capture_accepted = owner.captureControlInput(configuration, selected, captured);
				const bool unchanged = std::memcmp(&captured, before.value, sizeof(captured)) == 0;
				const bool pending = owner.awaitingControlInput();

				if (capture_accepted || !unchanged || pending) {
					const auto &diagnostic = owner.status();
					std::fprintf(stderr,
						     "CAPTURE_DISARM_DIAGNOSTIC scenario=%u accepted=%d unchanged=%d pending=%d ordinary=%d phase=%u decision=%u map=%d session=%d state=%d feedback=%d continuity=%d\n",
						     scenario, capture_accepted, unchanged, pending, owner.observedOrdinaryDisarm(hrt_absolute_time()),
						     static_cast<unsigned>(diagnostic.phase), static_cast<unsigned>(diagnostic.decision_code), diagnostic.map_valid,
						     diagnostic.session_established, diagnostic.required_state_valid, diagnostic.feedback_valid, diagnostic.continuity_fault);
				}

				check(!capture_accepted && unchanged && !pending,
				      "same-observation disarm or fault rejects control capture without returning a reusable command");
				check(owner.observedOrdinaryDisarm(hrt_absolute_time()) == (scenario <= 18),
				      "only fresh source-continuous ordinary disarm is a stop outcome; stale, inhibit, source, expiry, config and prior faults remain faults");

				if (scenario <= 18) {
					check(!owner._state.map.valid && !owner._entry_authorized && !owner.observedOrdinaryDisarm(original_deadline),
					      "ordinary-disarm capture revokes its map and cannot extend the original evidence deadline");
					check(!owner.captureControlInput(fingerprint(c) ^ 1U, captured) && !owner.observedOrdinaryDisarm(hrt_absolute_time()),
					      "another failed capture cannot reuse the preceding ordinary-disarm outcome");
				}

				continue;
			}
		}

		if (scenario == 0) {
			uORB::Publication<slaaps_companion_packet_s> receiver{ORB_ID(slaaps_companion_packet_3)};
			check(receiver.advertise(), "advertise isolated receiver lifetime for matched authenticated clock evidence");
			owner._receiver_revision = owner._input[3].revision();
			owner._receiver_system = owner._receiver_component = 1;
			// Explicit synthetic post-parser evidence; signature verification and
			// the challenge ledger are exercised by the signed wire scenario.
			auto clock_reply = [&](uint64_t remote_override = 0, bool wrong_key = false) {
				slaaps_companion_packet_s packet{};
				packet.clock_sent_us = hrt_absolute_time();
				do { packet.timestamp = hrt_absolute_time(); } while (packet.timestamp <= packet.clock_sent_us);

				const uint64_t midpoint = packet.clock_sent_us + (packet.timestamp - packet.clock_sent_us) / 2;
				mavlink_timesync_t payload{};
				payload.ts1 = static_cast<int64_t>(packet.clock_sent_us * 1000);
				payload.tc1 = static_cast<int64_t>((remote_override ? remote_override : midpoint) * 1000);
				payload.target_system = payload.target_component = 1;
				mavlink_message_t message{};
				mavlink_msg_timesync_encode(42, 191, &message, &payload);
				packet.message_id = message.msgid;
				packet.payload_length = message.len;
				std::memcpy(packet.payload, _MAV_PAYLOAD(&message), message.len);
				packet.event = slaaps_companion_packet_s::EVENT_CLOCK_SAMPLE;
				packet.control_binding_id = owner._binding;
				packet.controller_instance_id = owner._identity;
				packet.configuration_identity = owner._configuration_identity;
				packet.signing_generation = wrong_key ? 8 : 7;
				packet.mavlink_instance = 3;
				packet.source_system = 42;
				packet.source_component = 191;
				packet.receiver_system = packet.receiver_component = 1;
				packet.mavlink2 = packet.signing_active = packet.signature_verified = true;
				return owner.freezeMap(packet, hrt_absolute_time());
			};
			const uint64_t before_renewal_epoch = owner._snapshot.transition_epoch;
			check(!clock_reply() && owner._map_deadline == original_deadline && owner._state.map.map_epoch == original_map.map_epoch,
			      "authenticated clock replies before the bounded renewal window cannot repeatedly invalidate demand");
			const uint64_t renewal_start = original_deadline - static_cast<uint64_t>(c.companion.map_interval_us) / 2;

			while (hrt_absolute_time() <= renewal_start + 1000) { px4_usleep(100); }

			observe();
			check(!clock_reply(original_map.valid_from_producer_us - 1) && !clock_reply(hrt_absolute_time() - 100000)
			      && !clock_reply(0, true) && owner._map_deadline == original_deadline
			      && owner._state.map.map_epoch == original_map.map_epoch && owner._snapshot.transition_epoch == before_renewal_epoch,
			      "backward, nonoverlapping or mismatched-key evidence cannot replace the retained map");
			check(clock_reply(), "same-session authenticated matched clock renews before the original map expires");
			observe();
			const uint64_t renewed_deadline = owner._map_deadline;
			check(renewed_deadline > original_deadline && owner._state.map.map_epoch == original_map.map_epoch + 1
			      && owner._state.map.source == original_map.source && owner._state.map.session == original_map.session
			      && owner._snapshot.transition_epoch == before_renewal_epoch + 1
			      && owner._adapter->sessionSnapshot().gate == SessionGate::Established && !owner._status.acknowledgement_current
			      && owner._adapter->getEligibleRequest(owner._state, eligible).code != AdapterCode::EligibleRequest
			      && request.wire.producer_expiry_monotonic_us == creation + 375000,
			      "renewal advances only the map/barrier while revoking the still-fresh old request and ACK");
			check(!clock_reply() && owner._map_deadline == renewed_deadline,
			      "the renewed map starts a new bounded interval and cannot renew again immediately");
			acknowledge();
			do { creation = hrt_absolute_time(); } while (creation <= old_ack.producer_acknowledgement_monotonic_us + 2 *
					owner._state.map.uncertainty_us);

			observe();
			request.wire.transition_epoch = owner._snapshot.transition_epoch;
			request.wire.clock_mapping_epoch = owner._state.map.map_epoch;
			request.wire.producer_monotonic_us = creation;
			request.wire.producer_expiry_monotonic_us = creation + 20000;
			const auto duplicate = owner._adapter->processRequest(owner._state, request, ingress);
			check(duplicate.code == IngressCode::S1Rejected && duplicate.operation_s1_decision.code == DecisionCode::DuplicateSequence,
			      "map renewal retains the original request replay floor after a fresh ACK");
			request.wire.sequence = 2;
			check(owner._adapter->processRequest(owner._state, request, ingress).code == IngressCode::Completed
			      && owner._adapter->getEligibleRequest(owner._state, eligible).code == AdapterCode::EligibleRequest,
			      "a genuinely new post-renewal ACK/request pair restores eligibility under the same session");

			while (hrt_absolute_time() <= original_deadline + 1000) { px4_usleep(100); }

			observe();
			creation = owner._state.request_guard_context.validator_receive_time_us - 2 * owner._state.map.uncertainty_us;
			request.wire.sequence = 3;
			request.wire.producer_monotonic_us = creation;
			request.wire.producer_expiry_monotonic_us = creation + 20000;
			check(owner._adapter->processRequest(owner._state, request, ingress).code == IngressCode::Completed
			      && owner._adapter->getEligibleRequest(owner._state, eligible).code == AdapterCode::EligibleRequest
			      && owner._map_deadline == renewed_deadline,
			      "fresh demand remains eligible beyond the prior map expiry without re-establishment or output admission");

			while (hrt_absolute_time() < renewed_deadline) { px4_usleep(100); }

			observe();
			check(!owner._state.map.valid && !clock_reply() && !owner._entry_authorized
			      && owner._adapter->sessionSnapshot().gate != SessionGate::Established,
			      "missing the renewed original deadline closes authority and a late authentic reply cannot revive it");
		}

		if (scenario == 0 || scenario == 1) {
			witness.armed = false;
			witness.arming_state = vehicle_status_s::ARMING_STATE_DISARMED;

		} else { witness.nav_state = subject->_modes[0].id; }

		++witness.transition;
		observe();
		check(!owner._state.map.valid && !owner._entry_authorized
		      && owner._adapter->sessionSnapshot().gate != SessionGate::Established,
		      "ordinary disarm or reverse Acceleration-to-Manual exit revokes the preauthorized session");
	}

	const bool raw_retired = raw_pub.unadvertise();
	const bool bias_retired = bias_pub.unadvertise();
	check(raw_retired && bias_retired, "explicitly retire queued feedback source lifetimes");
}

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
static int nativeSupervisorOrchestrationTests(bool signed_flight = false)
{
	checks = 0;
	native_acceleration_capture_count.store(0, std::memory_order_relaxed);
	native_acceleration_capture_overflow.store(0, std::memory_order_relaxed);
	{
		vehicle_status_s vehicle{};
		actuator_armed_s armed{};
		vehicle.timestamp = armed.timestamp = 1000;
		vehicle.arming_state = vehicle_status_s::ARMING_STATE_DISARMED;
		armed.armed = true;
		check(ordinaryFlightDisarm(1000, 100, vehicle, armed), "fresh vehicle-first disarm is an ordinary output stop");
		vehicle.arming_state = vehicle_status_s::ARMING_STATE_ARMED;
		armed.armed = false;
		check(ordinaryFlightDisarm(1000, 100, vehicle, armed), "fresh actuator-first disarm is an ordinary output stop");

		for (unsigned fault = 0; fault < 10; ++fault) {
			auto bad_vehicle = vehicle;
			auto bad_armed = armed;

			switch (fault) {
			case 0: bad_armed.kill = true; break;

			case 1: bad_armed.lockdown = true; break;

			case 2: bad_armed.termination = true; break;

			case 3: bad_armed.in_esc_calibration_mode = true; break;

			case 4: bad_vehicle.timestamp = 0; break;

			case 5: bad_armed.timestamp = 0; break;

			case 6: bad_vehicle.timestamp = 1001; break;

			case 7: bad_vehicle.timestamp = 899; break;

			case 8: bad_armed.timestamp = 899; break;

			default: bad_armed.armed = true; break;
			}

			check(!ordinaryFlightDisarm(1000, 100, bad_vehicle, bad_armed),
			      "inhibited, absent, stale, future or still-armed authority cannot be classified as ordinary disarm");
		}
	}
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

	SlaapsNativeTest::companionEntryTests();

	const int32_t scope = 1, apply = 300;
	check(param_set(param_find("SLA_CFG_KIND"), &scope) == 0
	      && param_set(param_find("SLA_CFG_APPLY"), &apply) == 0,
	      "isolated supervisor installs explicit synthetic manual profile");
	Px4ConfigurationSource parameters;
	ParameterSnapshot profile{};
	uint32_t capture_duration{};
	check(parameters.initialize() && parameters.capture(profile, capture_duration) && parameters.remember(profile),
	      "isolated supervisor uses actual accepted-profile restore path");
	{
		ParameterSnapshot captured{}, scratch{};
		check(parameters.capture(captured, capture_duration, scratch)
		      && equal(captured.configuration, profile.configuration) && captured.apply_counter == profile.apply_counter
		      && captured.use == profile.use && Px4ConfigurationSource::remembered(captured),
		      "caller-owned workspace captures the complete coherent accepted profile");
		const ParameterSnapshot before = captured;
		check(!parameters.capture(captured, capture_duration, captured)
		      && std::memcmp(&before, &captured, sizeof(captured)) == 0,
		      "aliasing capture workspace is rejected without modifying the destination");
		Px4ConfigurationSource uninitialized;
		check(!uninitialized.capture(captured, capture_duration, scratch)
		      && std::memcmp(&before, &captured, sizeof(captured)) == 0,
		      "failed caller-workspace capture preserves the complete previous destination");
		const char *imu_parameters[kAxes] {"EKF2_IMU_POS_X", "EKF2_IMU_POS_Y", "EKF2_IMU_POS_Z"};
		const float native_position[kAxes] {.12F, -.03F, .04F};
		const float native_cutoff = 42.F;
		const float separate_cg = .7F;

		for (unsigned axis = 0; axis < kAxes; ++axis) {
			check(param_set(param_find(imu_parameters[axis]), &native_position[axis]) == 0,
			      "set exact native IMU offset for atomic typed snapshot regression");
		}

		check(param_set(param_find("SLA_CG_X"), &separate_cg) == 0
		      && param_set(param_find("IMU_ACCEL_CUTOFF"), &native_cutoff) == 0
		      && parameters.capture(captured, capture_duration, scratch), "real atomic source captures changed native filter/offset intent");
		check(sameFloatBits(captured.configuration.cg[0], separate_cg), "external offset test uses a distinct nonzero SLAAPS CG coordinate");

		for (unsigned axis = 0; axis < kAxes; ++axis) {
			check(sameFloatBits(captured.configuration.feedback.imu_position[axis], native_position[axis]),
			      "native CG-to-IMU coordinates are copied verbatim, without subtracting a second CG reference");
			check(param_set(param_find(imu_parameters[axis]), &profile.configuration.feedback.imu_position[axis]) == 0,
			      "restore exact native position intent after focused capture regression");
		}

		check(sameFloatBits(captured.configuration.feedback.cutoff_hz, native_cutoff)
		      && !equal(captured.configuration, profile.configuration) && !Px4ConfigurationSource::remembered(captured),
		      "native intent mutation changes typed identity and cannot inherit saved acceptance markers");
		check(param_set(param_find("SLA_CG_X"), &profile.configuration.cg[0]) == 0
		      && param_set(param_find("IMU_ACCEL_CUTOFF"), &profile.configuration.feedback.cutoff_hz) == 0
		      && parameters.capture(captured, capture_duration, scratch) && equal(captured.configuration, profile.configuration)
		      && Px4ConfigurationSource::remembered(captured),
		      "restoring exact external intent restores original identity without inventing producer-applied state");
	}
	NativeSlaapsOutput main("PWM_MAIN"), aux("PWM_AUX");
	uORB::Publication<vehicle_status_s> vehicle_pub{ORB_ID(vehicle_status)};
	uORB::Publication<actuator_armed_s> armed_pub{ORB_ID(actuator_armed)};
	NativeManualDisarmInjection disarm_injection{};
	disarm_injection.vehicle = &vehicle_pub;
	disarm_injection.armed = &armed_pub;
	native_manual_disarm = &disarm_injection;
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
	uORB::Publication<slaaps_vehicle_state_s> flight_witness_pub{ORB_ID(slaaps_vehicle_state)};
	uORB::PublicationMulti<sensor_accel_s> flight_raw_pub{ORB_ID(sensor_accel)};
	uORB::PublicationMulti<estimator_sensor_bias_s> flight_bias_pub{ORB_ID(estimator_sensor_bias)};
	uORB::Publication<vehicle_acceleration_s> flight_acceleration_pub{ORB_ID(vehicle_acceleration)};
	uORB::Publication<slaaps_companion_packet_s> flight_packet_pub{ORB_ID(slaaps_companion_packet_3)};
	uORB::ResearchSubscription flight_signed_packets{ORB_ID::slaaps_companion_packet_0};
	uORB::ResearchSubscription flight_injected_lifetime{ORB_ID::slaaps_companion_packet_3};
	uORB::ResearchSubscription flight_raw_lifetime{ORB_ID::sensor_accel}, flight_bias_lifetime{ORB_ID::estimator_sensor_bias};
	uORB::Subscription flight_control_sub{ORB_ID(slaaps_companion_control)}, flight_protocol_sub{ORB_ID(slaaps_companion_status)};
	uORB::Subscription control_detail_sub{ORB_ID(slaaps_acceleration_status)};
	slaaps_acceleration_status_s control_detail{};
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
	bool publish_inputs = true, publish_estimates = true, publish_manual = true, pump_aux = true, vehicle_armed = false;
	bool publish_conditions = false;
	bool publish_power = true;
	bool publish_flight = false, flight_keepalive = false, flight_stream_requests = false, flight_renew_clock = false;
	bool flight_expect_session = false, flight_auto_ack = false;
	bool flight_single_request{};
	float flight_lateral_request{};
	uint64_t flight_rejected_request{}, flight_rejected_receipt{};
	uint64_t flight_companion_session{7301};
	VehicleConfiguration flight_profile = slaapsFlightTransferConfiguration(c);
	slaaps_vehicle_state_s flight_witness{};
	slaaps_companion_control_s flight_control{};
	slaaps_companion_status_s flight_protocol{};
	uint64_t flight_event_sequence{}, flight_operation{}, flight_request_sequence{}, flight_last_request{}, flight_last_session{},
		 flight_ack_producer{};
	uint64_t flight_ack_epoch{}, flight_last_clock{}, flight_last_clock_event{}, flight_renewal_lead{};
	bool flight_clock_pending{};
	unsigned flight_receiver_revision{}, flight_signing_generation{}, flight_verified_clocks{}, flight_verified_requests{};
	unsigned flight_injected_revision{}, flight_injected_generation{};
	auto flight_injection_unchanged = [&]() {
		return !flight_packet_pub.advertised() && flight_injected_lifetime.revision() == flight_injected_revision
		       && flight_injected_lifetime.get_last_generation() == flight_injected_generation && !flight_injected_lifetime.updated();
	};
	struct FlightSessionDiagnostic {
		uint64_t observed{}, protocol_time{}, witness_time{}, witness_transition{}, observed_transition{}, epoch{};
		uint64_t map_epoch{}, map_deadline{}, connection_deadline{}, barrier_time{};
		uint8_t nav{}, arming{}, phase{}, entry{}, worker{}, feedback{};
		uint16_t decision{};
		bool vehicle_armed{}, actuator_armed{}, witness_armed{}, session{}, map{}, state{}, acknowledgement{}, continuity{};
	};
	// Fixture-thread-only sampled evidence: prior healthy protocol publication
	// and first observed loss. These topics are not one atomic transaction.
	auto flight_diagnostics = std::make_unique<FlightSessionDiagnostic[]>(2);
	// A single bounded/flushed line asks the independent Python UDP producer to
	// act. These local values select a test transaction; they never substitute
	// for a producer timestamp, a verifier verdict or an injected packet.
	auto flight_instruction = [&](const char *kind, unsigned operation = 0) {
		char line[768] {};
		const int prefix = std::snprintf(line, sizeof(line),
						 "SLAAPS_SIGNED_FLIGHT kind=%s controller=%llu binding=%llu epoch=%llu map=%llu session=%llu offset=%lld uncertainty=%llu deadline=%llu",
						 kind, static_cast<unsigned long long>(flight_protocol.controller_instance_id),
						 static_cast<unsigned long long>(flight_control.binding_id), static_cast<unsigned long long>(flight_protocol.transition_epoch),
						 static_cast<unsigned long long>(flight_protocol.map_epoch), static_cast<unsigned long long>(flight_companion_session),
						 static_cast<long long>(flight_protocol.map_offset_us), static_cast<unsigned long long>(flight_protocol.map_uncertainty_us),
						 static_cast<unsigned long long>(flight_protocol.map_deadline));
		check(prefix > 0 && static_cast<size_t>(prefix) < sizeof(line), "signed-flight instruction prefix fits its fixed buffer");
		int suffix{};

		if (std::strcmp(kind, "session") == 0) {
			suffix = std::snprintf(line + prefix, sizeof(line) - prefix, " operation=%u barrier=%llu barrier_time=%llu\n", operation,
					       static_cast<unsigned long long>(flight_protocol.barrier_sequence), static_cast<unsigned long long>(flight_protocol.barrier_time));

		} else if (std::strcmp(kind, "request") == 0) {
			suffix = std::snprintf(line + prefix, sizeof(line) - prefix, " ax=5 ay=%.9g az=%.9g minimum_validity=%d maximum_validity=%d\n",
					       static_cast<double>(flight_lateral_request), static_cast<double>(flight_profile.feedback.gravity_m_s2),
					       flight_profile.companion.minimum_validity_us,
					       flight_profile.companion.maximum_validity_us);

		} else {
			suffix = std::snprintf(line + prefix, sizeof(line) - prefix, "\n");
		}

		check(suffix > 0 && static_cast<size_t>(suffix) < sizeof(line) - prefix,
		      "signed-flight instruction suffix fits its fixed buffer");
		check(std::fputs(line, stdout) >= 0 && std::fflush(stdout) == 0, "flush one complete signed-flight instruction");
	};
	auto flight_packet = [&](const mavlink_message_t &message, uint8_t event, uint64_t sent = 0, uint64_t receipt = 0) {
		check(!signed_flight, "the signed active path cannot inject a companion packet topic");
		slaaps_companion_packet_s packet{};
		packet.timestamp = receipt ? receipt : hrt_absolute_time();
		packet.sequence = ++flight_event_sequence;
		packet.control_binding_id = flight_control.binding_id;
		packet.controller_instance_id = flight_control.controller_instance_id;
		packet.configuration_identity = flight_control.configuration_identity;
		packet.clock_sent_us = sent;
		packet.signing_generation = 7;
		packet.message_id = message.msgid;
		packet.mavlink_instance = 3;
		packet.source_system = 42;
		packet.source_component = 191;
		packet.receiver_system = packet.receiver_component = 1;
		packet.payload_length = message.len;
		packet.event = event;
		packet.mavlink2 = packet.signing_active = packet.signature_verified = true;
		std::memcpy(packet.payload, _MAV_PAYLOAD(&message), message.len);
		check(flight_packet_pub.publish(packet), "isolated post-parser companion evidence publication succeeds (not signature qualification)");
	};
	auto flight_session = [&](bool acknowledgement) {
		if (signed_flight) {
			flight_instruction("session", acknowledgement ? 3 : 1);
			flight_last_session = hrt_absolute_time();

			if (acknowledgement) {
				// This is local instruction time only. Python owns the original
				// remote ACK time and waits for the real signed wire barrier.
				flight_ack_producer = flight_last_session;
				flight_ack_epoch = flight_protocol.transition_epoch;
			}

			return;
		}

		mavlink_research_acceleration_session_t payload{};
		payload.source_id = static_cast<uint64_t>(flight_profile.companion.source_id);
		payload.session_id = flight_companion_session;
		payload.controller_instance_id = flight_protocol.controller_instance_id;
		payload.transition_epoch = flight_protocol.transition_epoch;
		payload.clock_mapping_epoch = flight_protocol.map_epoch ? flight_protocol.map_epoch : 1;
		payload.operation_sequence = ++flight_operation;
		payload.protocol_version = research_acceleration::kWireProtocolVersionV1;
		payload.target_system = payload.target_component = 1;
		payload.operation = acknowledgement ? 3 : 1;

		if (acknowledgement) {
			flight_ack_producer = hrt_absolute_time();
			flight_ack_epoch = flight_protocol.transition_epoch;
			payload.acknowledged_status_sequence = flight_protocol.barrier_sequence;
			payload.producer_acknowledgement_monotonic_us = flight_ack_producer;
		}

		mavlink_message_t message{};
		mavlink_msg_research_acceleration_session_encode(42, 191, &message, &payload);
		flight_packet(message, slaaps_companion_packet_s::EVENT_FRAME);
		flight_last_session = hrt_absolute_time();
	};
	auto flight_clock = [&]() {
		if (signed_flight) {
			flight_instruction("clock");
			flight_clock_pending = true;
			flight_last_clock = hrt_absolute_time();
			return;
		}

		const uint64_t receipt = hrt_absolute_time(), sent = receipt - 2000;
		mavlink_timesync_t clock{};
		clock.tc1 = static_cast<int64_t>((sent + (receipt - sent) / 2) * 1000);
		clock.ts1 = static_cast<int64_t>(sent * 1000);
		clock.target_system = clock.target_component = 1;
		mavlink_message_t message{};
		mavlink_msg_timesync_encode(42, 191, &message, &clock);
		flight_packet(message, slaaps_companion_packet_s::EVENT_CLOCK_SAMPLE, sent, receipt);
		flight_last_clock = receipt;
		flight_last_clock_event = flight_event_sequence;
	};
	auto flight_request = [&]() {
		const uint64_t now = hrt_absolute_time();
		const uint64_t remaining = flight_protocol.map_deadline > now ? flight_protocol.map_deadline - now : 0;

		if (remaining < static_cast<uint64_t>(flight_profile.companion.minimum_validity_us)) { return; }

		if (signed_flight) {
			flight_instruction("request");
			flight_last_request = now;

			if (flight_single_request) { flight_stream_requests = false; }

			return;
		}

		mavlink_research_acceleration_request_t payload{};
		payload.source_id = static_cast<uint64_t>(flight_profile.companion.source_id);
		payload.session_id = flight_companion_session;
		payload.sequence = ++flight_request_sequence;
		payload.producer_monotonic_us = now;
		payload.producer_expiry_monotonic_us = now + math::min(remaining, static_cast<uint64_t>(flight_profile.companion.maximum_validity_us));
		payload.controller_instance_id = flight_protocol.controller_instance_id;
		payload.transition_epoch = flight_protocol.transition_epoch;
		payload.clock_mapping_epoch = flight_protocol.map_epoch;
		payload.acceleration_x_frd_m_s2 = 5.F;
		payload.acceleration_y_frd_m_s2 = flight_lateral_request;
		payload.acceleration_z_frd_m_s2 = flight_profile.feedback.gravity_m_s2;
		payload.desired_attitude_ned_from_body[0] = 1.F;
		payload.protocol_version = research_acceleration::kWireProtocolVersionV2;
		payload.target_system = payload.target_component = 1;
		payload.frame = payload.quantity = payload.reference_point = 1;
		payload.flags = 1;
		mavlink_message_t message{};
		mavlink_msg_research_acceleration_request_encode(42, 191, &message, &payload);
		flight_packet(message, slaaps_companion_packet_s::EVENT_FRAME);
		flight_last_request = now;

		if (flight_single_request) {
			flight_stream_requests = false;
			flight_rejected_request = payload.sequence;
			flight_rejected_receipt = now;
		}
	};
	unsigned power_scenario{};
	float condition_airspeed = 20.F;
	float roll = 0.F, throttle = -1.F;
	slaaps_status_s status{};
	// Keep the first observable anomaly and preceding good evidence in this
	// native fixture. No supervisor-private mutable state is read here, and no
	// diagnostic output delays the active scenario unless an assertion fails.
	struct EdfDiagnosticSample {
		uint64_t observed{}, pump_started{}, previous_pump{}, maximum_gap{}, session{}, phase_started{};
		unsigned scenario{}, phase{}, reasons{}, writes[2] {}, driver_status[2] {};
		bool expected_vehicle_armed{}, expected_actuator_armed{}, pump_aux{}, publish_inputs{}, publish_power{};
		float expected_throttle{};
		slaaps_status_s status{};
		vehicle_status_s vehicle{};
		actuator_armed_s armed{};
		manual_control_setpoint_s raw{};
		battery_status_s power{};
		slaaps_manual_permission_s permission{};
		slaaps_manual_command_s command{};
		slaaps_manual_ack_s handled[2] {};
		slaaps_manual_preparation_s ready[2] {};
		slaaps_manual_activation_s activation[2] {};
		slaaps_output_bank_s bank[2] {};
	};
	auto edf_diagnostics = std::make_unique<EdfDiagnosticSample[]>(3); // Prior good, first anomaly, latest.
	uORB::Subscription diagnostic_vehicle{ORB_ID(vehicle_status)}, diagnostic_armed{ORB_ID(actuator_armed)};
	uORB::Subscription diagnostic_raw{ORB_ID(manual_control_setpoint)}, diagnostic_power{ORB_ID(battery_status)};
	uORB::Subscription diagnostic_permission{ORB_ID(slaaps_manual_permission)}, diagnostic_command{ORB_ID(slaaps_manual_command)};
	uORB::Subscription diagnostic_handled[2] {{ORB_ID(slaaps_manual_ack_main)}, {ORB_ID(slaaps_manual_ack_aux)}};
	uORB::Subscription diagnostic_ready[2] {{ORB_ID(slaaps_manual_ready_main)}, {ORB_ID(slaaps_manual_ready_aux)}};
	uORB::Subscription diagnostic_activation[2] {{ORB_ID(slaaps_manual_active_main)}, {ORB_ID(slaaps_manual_active_aux)}};
	uORB::Subscription diagnostic_bank[2] {{ORB_ID(slaaps_output_bank), 0}, {ORB_ID(slaaps_output_bank), 1}};
	unsigned diagnostic_phase{}, diagnostic_scenario{}, diagnostic_baseline_writes{};
	uint64_t diagnostic_session{}, diagnostic_phase_started{}, diagnostic_previous_pump{}, diagnostic_maximum_gap{};
	bool diagnostic_latched{};
	auto capture_edf_diagnostic = [&](uint64_t pump_started, uint64_t previous_pump) {
		auto &sample = edf_diagnostics[2];
		sample = {};
		sample.pump_started = pump_started;
		sample.previous_pump = previous_pump;
		sample.maximum_gap = diagnostic_maximum_gap;
		sample.session = diagnostic_session;
		sample.phase_started = diagnostic_phase_started;
		sample.scenario = diagnostic_scenario;
		sample.phase = diagnostic_phase;
		sample.expected_vehicle_armed = vehicle_armed;
		sample.expected_actuator_armed = armed.armed;
		sample.expected_throttle = throttle;
		sample.pump_aux = pump_aux;
		sample.publish_inputs = publish_inputs;
		sample.publish_power = publish_power;
		sample.status = status;
		diagnostic_vehicle.copy(&sample.vehicle);
		diagnostic_armed.copy(&sample.armed);
		diagnostic_raw.copy(&sample.raw);
		diagnostic_power.copy(&sample.power);
		diagnostic_permission.copy(&sample.permission);
		diagnostic_command.copy(&sample.command);

		for (unsigned bank = 0; bank < 2; ++bank) {
			diagnostic_handled[bank].copy(&sample.handled[bank]);
			diagnostic_ready[bank].copy(&sample.ready[bank]);
			diagnostic_activation[bank].copy(&sample.activation[bank]);
			diagnostic_bank[bank].copy(&sample.bank[bank]);
		}

		sample.writes[0] = main.writes(); sample.writes[1] = aux.writes();
		sample.driver_status[0] = static_cast<unsigned>(main.mixing().slaapsOutputStatus());
		sample.driver_status[1] = static_cast<unsigned>(aux.mixing().slaapsOutputStatus());
		sample.observed = hrt_absolute_time();
		const bool fresh_status = status.timestamp >= diagnostic_phase_started;
		const bool waiting = diagnostic_phase <= 2;
		const bool running = diagnostic_phase == 4;
		sample.reasons = fresh_status && status.manual_preparation_session != diagnostic_session ? 1U : 0U;
		const bool terminal = status.bench_entry_status == slaaps_status_s::BENCH_ENTRY_REJECTED
				      || status.bench_entry_status == slaaps_status_s::BENCH_ENTRY_WAIT_DISARM
				      || status.bench_entry_status == slaaps_status_s::BENCH_ENTRY_DISARM_FAILED;

		if (fresh_status && (terminal || (waiting && status.bench_entry_status != slaaps_status_s::BENCH_ENTRY_WAIT_ARM)
				     || (running && status.bench_entry_status != slaaps_status_s::BENCH_ENTRY_EDF_RUNNING))) { sample.reasons |= 2U; }

		if (fresh_status && ((waiting && status.bench_worker_active) || (running && !status.bench_worker_active))) { sample.reasons |= 4U; }

		if (fresh_status && (!status.configuration_valid || !status.routing_snapshot_valid)) { sample.reasons |= 8U; }

		if (waiting && sample.writes[0] + sample.writes[1] != diagnostic_baseline_writes) { sample.reasons |= 16U; }

		for (unsigned bank = 0; bank < 2; ++bank) {
			if (sample.driver_status[bank] >= static_cast<unsigned>(SlaapsOutputStatus::Inhibited)) { sample.reasons |= 32U << bank; }
		}

		if (sample.reasons && !diagnostic_latched) { edf_diagnostics[1] = sample; diagnostic_latched = true; }

		if (!sample.reasons && !diagnostic_latched) { edf_diagnostics[0] = sample; }
	};
	auto print_edf_diagnostic = [&](const char *assertion) {
		SlaapsNativeTest::printBenchCancels();

		if (!diagnostic_phase) { return; }

		capture_edf_diagnostic(hrt_absolute_time(), diagnostic_previous_pump);
		std::fprintf(stderr,
			     "EDF_CAUSAL assertion=%s first_retained=%u phases=1disarmed/2partial/3activation/4running reasons=1session/2entry/4worker/8config-routing/16writes/32main/64aux\n",
			     assertion, diagnostic_latched ? 1U : 0U);
		const char *labels[] {"prior_good", "first_anomaly", "failure"};
		const auto t = [](uint64_t value) { return static_cast<unsigned long long>(value); };

		for (unsigned i = 0; i < 3; ++i) {
			const auto &s = edf_diagnostics[i];
			std::fprintf(stderr,
				     "EDF_CAUSAL %s scenario=%u phase=%u reasons=%u now=%llu start=%llu pump=%llu previous=%llu maxgap=%llu expected_session=%llu\n",
				     labels[i], s.scenario, s.phase, s.reasons, t(s.observed), t(s.phase_started), t(s.pump_started), t(s.previous_pump), t(s.maximum_gap),
				     t(s.session));
			std::fprintf(stderr,
				     "EDF_CAUSAL status time=%llu entry=%u active=%d worker=%u prep=%u session=%llu prep_deadline=%llu total=%llu ready_deadline=%llu masks=%u/%u power=%u/%llu routing=%d/%u/%u\n",
				     t(s.status.timestamp), s.status.bench_entry_status, s.status.bench_worker_active, s.status.manual_worker_status,
				     s.status.manual_preparation_status, t(s.status.manual_preparation_session), t(s.status.manual_preparation_deadline),
				     t(s.status.bench_session_deadline), t(s.status.bench_commissioning_ready_deadline), s.status.can_arm_mask, s.status.can_run_mask,
				     s.status.power_status, t(s.status.power_deadline), s.status.routing_snapshot_valid, s.status.inventory_fault, s.status.routing_fault);
			std::fprintf(stderr,
				     "EDF_CAUSAL authority expected=%d/%d actual=%u/%d timestamps=%llu/%llu nav=%u/%u prearm=%d inhibits=%d/%d/%d fixture_inputs=%d power=%d aux=%d throttle=%.6f\n",
				     s.expected_vehicle_armed, s.expected_actuator_armed, s.vehicle.arming_state, s.armed.armed, t(s.vehicle.timestamp), t(s.armed.timestamp),
				     s.vehicle.nav_state, s.vehicle.nav_state_user_intention, s.armed.prearmed, s.armed.kill, s.armed.lockdown, s.armed.termination,
				     s.publish_inputs, s.publish_power, s.pump_aux, static_cast<double>(s.expected_throttle));
			std::fprintf(stderr,
				     "EDF_CAUSAL permission time=%llu original=%llu deadline=%llu session=%llu seq=%llu armed=%d healthy=%d commission=%d mode=%u raw=%llu/%llu/%.6f power=%llu/%.6f/%u\n",
				     t(s.permission.timestamp), t(s.permission.timestamp_sample), t(s.permission.deadline), t(s.permission.session), t(s.permission.sequence),
				     s.permission.armed, s.permission.healthy, s.permission.commissioning_admitted, s.permission.mode,
				     t(s.raw.timestamp), t(s.raw.timestamp_sample), static_cast<double>(s.raw.throttle), t(s.power.timestamp),
				     static_cast<double>(s.power.voltage_v), s.power.faults);
			std::fprintf(stderr,
				     "EDF_CAUSAL command session=%llu seq=%llu published=%llu calculated=%llu deadline=%llu edf=%.6f raw_seq=%llu raw_time=%llu raw_deadline=%llu mode=%u disposition=%u\n",
				     t(s.command.session), t(s.command.sequence), t(s.command.timestamp), t(s.command.timestamp_sample), t(s.command.deadline),
				     static_cast<double>(s.command.edf),
				     t(s.command.edf_sequence), t(s.command.edf_request_timestamp), t(s.command.edf_deadline), s.command.mode, s.command.edf_disposition);

			for (unsigned bank = 0; bank < 2; ++bank) {
				std::fprintf(stderr,
					     "EDF_CAUSAL bank=%u state=%u writes=%u report=%llu/%d/%llu ready=%llu/%llu/%llu/%llu activation=%llu/%llu/%llu ack=%llu/%llu/%llu/%llu/%llu\n",
					     bank, s.driver_status[bank], s.writes[bank], t(s.bank[bank].timestamp), s.bank[bank].ready, t(s.bank[bank].driver_instance),
					     t(s.ready[bank].timestamp), t(s.ready[bank].session), t(s.ready[bank].prepared), t(s.ready[bank].deadline),
					     t(s.activation[bank].session), t(s.activation[bank].timestamp_sample), t(s.activation[bank].first_command_deadline),
					     t(s.handled[bank].session), t(s.handled[bank].sequence), t(s.handled[bank].timestamp), t(s.handled[bank].timestamp_sample),
					     t(s.handled[bank].accepted));
			}
		}
	};
	const uint64_t started = hrt_absolute_time();
	SlaapsNativeTest::admitted.store(true);
	auto pump = [&]() {
		const auto now = hrt_absolute_time();
		const auto previous_pump = diagnostic_previous_pump;
		diagnostic_previous_pump = now;

		if (diagnostic_phase && previous_pump && now - previous_pump > diagnostic_maximum_gap) { diagnostic_maximum_gap = now - previous_pump; }

		vehicle.timestamp = armed.timestamp = now;
		vehicle.arming_state = vehicle_armed ? vehicle_status_s::ARMING_STATE_ARMED : vehicle_status_s::ARMING_STATE_DISARMED;

		if (publish_flight && (!flight_witness.transition || flight_witness.nav_state != vehicle.nav_state)) {
			vehicle.nav_state_timestamp = now;
		}

		const unsigned requested_disarm = disarm_injection.requested.load(std::memory_order_acquire);
		const unsigned completed_disarm = disarm_injection.completed.load(std::memory_order_acquire);
		bool published = true;

		if (!requested_disarm) { published = vehicle_pub.publish(vehicle) && armed_pub.publish(armed); }

		else if (completed_disarm) {
			auto disarmed_vehicle = vehicle;
			auto disarmed_actuator = armed;

			if (requested_disarm == 1) { disarmed_vehicle.arming_state = vehicle_status_s::ARMING_STATE_DISARMED; }

			else { disarmed_actuator.armed = false; }

			published = vehicle_pub.publish(disarmed_vehicle) && armed_pub.publish(disarmed_actuator);
		}

		if (publish_flight) {
			const bool changed = !flight_witness.transition || flight_witness.nav_state != vehicle.nav_state
					     || flight_witness.arming_state != vehicle.arming_state || flight_witness.armed != armed.armed
					     || flight_witness.prearmed != armed.prearmed || flight_witness.kill != armed.kill
					     || flight_witness.lockdown != armed.lockdown || flight_witness.termination != armed.termination;

			if (changed) { ++flight_witness.transition; }

			flight_witness.timestamp = hrt_absolute_time();
			flight_witness.nav_state = vehicle.nav_state;
			flight_witness.arming_state = vehicle.arming_state;
			flight_witness.armed = armed.armed;
			flight_witness.prearmed = armed.prearmed;
			flight_witness.kill = armed.kill;
			flight_witness.lockdown = armed.lockdown;
			flight_witness.termination = armed.termination;

			// Mirror Commander: VehicleStatus is published before its atomic
			// witness. This process is explicitly synthetic authority only.
			// The capture-disarm injection holds the last actual witness while
			// the first actuator disarm publication is being consumed.
			if (!requested_disarm) { published &= flight_witness_pub.publish(flight_witness); }

			const float pulse = aux.lastOutput(3);
			const float normalized = pulse >= 1000.F && pulse <= 2000.F ? (pulse - 1000.F) * .001F : 0.F;
			float modeled_thrust{};
			published &= commandToThrust(flight_profile, normalized, modeled_thrust);
			sensor_accel_s raw{};
			raw.timestamp = raw.timestamp_sample = now;
			raw.device_id = 456;
			raw.x = modeled_thrust;
			estimator_sensor_bias_s bias{};
			bias.timestamp = bias.timestamp_sample = now;
			bias.accel_device_id = 456;
			bias.accel_bias_valid = true;
			published &= flight_raw_pub.publish(raw) && flight_bias_pub.publish(bias);
			vehicle_acceleration_s acceleration{};
			acceleration.timestamp = acceleration.timestamp_sample = now;
			acceleration.device_id = 456;
			acceleration.source_generation = 1;
			acceleration.calibrated = acceleration.bias_valid = true;
			acceleration.raw_publisher_revision = flight_raw_lifetime.revision();
			acceleration.bias_publisher_revision = flight_bias_lifetime.revision();
			acceleration.bias_timestamp = acceleration.bias_timestamp_sample = now;
			acceleration.xyz[0] = modeled_thrust;
			// Ideal test feedback follows the capture callback's shared EDF
			// curve. It is not measured thrust or calibration qualification.
			published &= flight_acceleration_pub.publish(acceleration);
		}

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
			published &= !publish_manual || manual_pub.publish(manual);

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
		control_detail_sub.copy(&control_detail);

		if (publish_flight) {
			flight_control_sub.copy(&flight_control);
			flight_protocol_sub.copy(&flight_protocol);

			if (signed_flight) {
				for (unsigned unread = 0; unread < slaaps_companion_packet_s::ORB_QUEUE_LENGTH; ++unread) {
					slaaps_companion_packet_s packet{};
					unsigned revision{};

					if (!flight_signed_packets.copyQueuedBound(&packet, revision)) { break; }

					if (packet.event == slaaps_companion_packet_s::EVENT_LINK_STATE) { continue; }

					check(revision && packet.mavlink_instance == 0 && packet.source_system == 42 && packet.source_component == 191
					      && packet.receiver_system == 1 && packet.receiver_component == 1 && packet.mavlink2
					      && packet.signing_active && packet.signature_verified && packet.signing_generation,
					      "actual selected receiver supplies verifier-backed signed source evidence");

					if (!flight_receiver_revision) {
						flight_receiver_revision = revision;
						flight_signing_generation = packet.signing_generation;
					}

					check(revision == flight_receiver_revision && packet.signing_generation == flight_signing_generation,
					      "signed active ingress retains one actual receiver and signing-key lifetime");

					if (packet.event == slaaps_companion_packet_s::EVENT_CLOCK_SAMPLE
					    && packet.control_binding_id == flight_control.binding_id
					    && packet.controller_instance_id == flight_control.controller_instance_id
					    && packet.configuration_identity == flight_control.configuration_identity) {
						check(packet.clock_sent_us && packet.timestamp > packet.clock_sent_us,
						      "signed map evidence retains the real matching challenge send and receive times");
						++flight_verified_clocks;
						flight_last_clock = packet.timestamp;
						flight_last_clock_event = packet.sequence;

						if (flight_clock_pending) {
							flight_clock_pending = false;
							flight_instruction("clock_done");
						}
					}

					if (packet.message_id == MAVLINK_MSG_ID_RESEARCH_ACCELERATION_REQUEST) {
						mavlink_message_t encoded{};
						check(packet.payload_length >= MAVLINK_MSG_ID_RESEARCH_ACCELERATION_REQUEST_MIN_LEN
						      && packet.payload_length <= MAVLINK_MSG_ID_RESEARCH_ACCELERATION_REQUEST_LEN,
						      "signed full command preserves its actual bounded MAVLink extension length");
						encoded.len = packet.payload_length;
						std::memcpy(_MAV_PAYLOAD_NON_CONST(&encoded), packet.payload, packet.payload_length);
						check(mavlink_msg_research_acceleration_request_get_protocol_version(&encoded) == research_acceleration::kWireProtocolVersionV2
						      && mavlink_msg_research_acceleration_request_get_flags(&encoded) == 1
						      && mavlink_msg_research_acceleration_request_get_source_id(&encoded) == 1,
						      "actual signed receiver payload is the explicit full-v2 logical source command");
						++flight_verified_requests;

						if (mavlink_msg_research_acceleration_request_get_session_id(&encoded) == 7303
						    && mavlink_msg_research_acceleration_request_get_acceleration_y_frd_m_s2(&encoded) > .5F) {
							check(!flight_rejected_request, "lateral allocation case sends exactly one signed request");
							flight_rejected_request = mavlink_msg_research_acceleration_request_get_sequence(&encoded);
							flight_rejected_receipt = packet.timestamp;
						}
					}
				}
			}

			if (flight_expect_session && !flight_diagnostics[1].observed) {
				const bool healthy = flight_protocol.session_established && flight_protocol.map_valid
						     && flight_protocol.required_state_valid && flight_protocol.feedback_valid && !flight_protocol.continuity_fault;
				auto &sample = flight_diagnostics[healthy ? 0 : 1];

				if (healthy || !sample.observed) {
					sample.observed = hrt_absolute_time();
					sample.protocol_time = flight_protocol.timestamp;
					sample.witness_time = flight_witness.timestamp;
					sample.witness_transition = flight_witness.transition;
					sample.observed_transition = flight_protocol.commander_transition;
					sample.epoch = flight_protocol.transition_epoch;
					sample.map_epoch = flight_protocol.map_epoch;
					sample.map_deadline = flight_protocol.map_deadline;
					sample.connection_deadline = flight_protocol.connection_deadline;
					sample.barrier_time = flight_protocol.barrier_time;
					sample.nav = flight_witness.nav_state;
					sample.arming = flight_witness.arming_state;
					sample.phase = flight_protocol.phase;
					sample.entry = status.manual_entry_status;
					sample.worker = status.manual_worker_status;
					sample.feedback = flight_protocol.feedback_result;
					sample.decision = flight_protocol.decision_code;
					sample.vehicle_armed = vehicle_armed;
					sample.actuator_armed = armed.armed;
					sample.witness_armed = flight_witness.armed;
					sample.session = flight_protocol.session_established;
					sample.map = flight_protocol.map_valid;
					sample.state = flight_protocol.required_state_valid;
					sample.acknowledgement = flight_protocol.acknowledgement_current;
					sample.continuity = flight_protocol.continuity_fault;
				}
			}

			if (flight_keepalive && flight_protocol.controller_instance_id && now - flight_last_session >= 20000) { flight_session(false); }

			if (flight_renew_clock && flight_protocol.session_established) {
				if (flight_protocol.map_valid && flight_protocol.map_deadline > now
				    && flight_protocol.map_deadline - now <= flight_renewal_lead && now - flight_last_clock >= 20000) { flight_clock(); }
			}

			if (flight_auto_ack && flight_protocol.session_established && !flight_protocol.acknowledgement_current
			    && flight_protocol.transition_epoch != flight_ack_epoch
			    && now > flight_protocol.barrier_time + 2 * flight_protocol.map_uncertainty_us) { flight_session(true); }

			if (flight_stream_requests && flight_protocol.acknowledgement_current
			    && now > flight_ack_producer + 2 * flight_protocol.map_uncertainty_us
			    && now - flight_last_request >= 20000) { flight_request(); }
		}

		if (diagnostic_phase) {
			const auto &last = edf_diagnostics[2];

			if (last.phase != diagnostic_phase || last.status.timestamp != status.timestamp
			    || last.driver_status[0] != static_cast<unsigned>(main.mixing().slaapsOutputStatus())
			    || last.driver_status[1] != static_cast<unsigned>(aux.mixing().slaapsOutputStatus())
			    || last.writes[0] != main.writes() || last.writes[1] != aux.writes()) { capture_edf_diagnostic(now, previous_pump); }
		}
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
			print_edf_diagnostic(message);
			PX4_ERR("supervisor fixture entry=%u worker=%u active=%u preparation=%u session=%llu",
				status.manual_entry_status, status.manual_worker_status, status.manual_worker_active,
				status.manual_preparation_status, static_cast<unsigned long long>(status.manual_preparation_session));
			PX4_ERR("supervisor fixture bench entry=%u active=%u target=%u deadline=%llu",
				status.bench_entry_status, status.bench_worker_active, status.bench_target,
				static_cast<unsigned long long>(status.bench_session_deadline));
			PX4_ERR("supervisor fixture routing=%u inventory_fault=%u routing_fault=%u configured=%u",
				status.routing_snapshot_valid, status.inventory_fault, status.routing_fault, status.direct_actuator_modes_configured);
			PX4_ERR("supervisor fixture schedule enabled=%u inputs=%u status=%u",
				status.schedule_enabled, status.schedule_inputs_valid, status.schedule_status);
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

			if (publish_flight) {
				for (unsigned i = 0; i < 2; ++i) {
					const auto &s = flight_diagnostics[i];
					std::fprintf(stderr,
						     "FLIGHT_SESSION_SAMPLE index=%u sampled=%llu protocol=%llu witness=%llu transition=%llu/%llu nav=%u arming=%u armed=%d/%d/%d\n",
						     i, static_cast<unsigned long long>(s.observed), static_cast<unsigned long long>(s.protocol_time),
						     static_cast<unsigned long long>(s.witness_time), static_cast<unsigned long long>(s.observed_transition),
						     static_cast<unsigned long long>(s.witness_transition), s.nav, s.arming, s.vehicle_armed, s.actuator_armed, s.witness_armed);
					std::fprintf(stderr,
						     "FLIGHT_SESSION_SAMPLE index=%u epoch=%llu map_epoch=%llu map_deadline=%llu connection=%llu barrier=%llu phase=%u entry=%u worker=%u feedback=%u decision=%u session=%d map=%d state=%d ack=%d continuity=%d\n",
						     i, static_cast<unsigned long long>(s.epoch), static_cast<unsigned long long>(s.map_epoch),
						     static_cast<unsigned long long>(s.map_deadline), static_cast<unsigned long long>(s.connection_deadline),
						     static_cast<unsigned long long>(s.barrier_time), s.phase, s.entry, s.worker, s.feedback, s.decision,
						     s.session, s.map, s.state, s.acknowledgement, s.continuity);
				}

				PX4_ERR("flight fixture phase=%u action=%u result=%u epoch=%llu map=%llu session=%d ack=%d state=%d feedback=%u code=%u",
					flight_protocol.phase, flight_protocol.action, flight_protocol.action_result,
					static_cast<unsigned long long>(flight_protocol.transition_epoch), static_cast<unsigned long long>(flight_protocol.map_epoch),
					flight_protocol.session_established, flight_protocol.acknowledgement_current, flight_protocol.required_state_valid,
					flight_protocol.feedback_result, flight_protocol.decision_code);
			}
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

	for (unsigned scenario = 0; scenario < 5; ++scenario) {
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

			else if (scenario >= 3) {
				// Stop fixture-thread authority publications, then let the actual
				// permission call publish either first disarm after its caller has
				// already observed both armed. No supervisor state is fabricated.
				disarm_injection.requested.store(scenario - 2, std::memory_order_release);
			}

			else { SlaapsNativeTest::admitted.store(false); }

			wait([&]() {
				return status.manual_entry_status == slaaps_status_s::MANUAL_ENTRY_REJECTED
				       && !status.manual_worker_active;
			}, 300000,
			"actual supervisor stops on ordinary disarm or cancels on current input or admission loss");

			if (scenario >= 3) {
				check(disarm_injection.completed.load(std::memory_order_acquire) == scenario - 2
				      && disarm_injection.original_both_armed && disarm_injection.published && disarm_injection.injected_us,
				      "real authority publication between armed caller observation and permission copies exercises either disarm order");
			}
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
		disarm_injection.requested.store(0, std::memory_order_release);
		disarm_injection.completed.store(0, std::memory_order_release);

		if (scenario < 3) {
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

	// Actual apply results must reach operator status, including replacement of
	// an earlier rejection and clearing after a corrected coherent profile.
	const param_t voltage_parameter = param_find("SLA_PWR_VMAX"), battery_id_parameter = param_find("SLA_PWR_ID");
	const float rejected_voltage = 19.F;
	const int32_t rejected_id = 0, accepted_id = 1;
	uORB::Subscription diagnostic_logs{ORB_ID(mavlink_log)};
	uint64_t diagnostic_log_start = hrt_absolute_time();
	uint64_t aux8_log_time{}, aux6_log_time{};
	unsigned voltage_log_count{}, identity_log_count{}, aux8_log_count{}, aux6_log_count{};
	auto collect_diagnostics = [&]() {
		mavlink_log_s log{};

		for (unsigned i = 0; i < mavlink_log_s::ORB_QUEUE_LENGTH && diagnostic_logs.update(&log); ++i) {
			if (log.timestamp < diagnostic_log_start) { continue; }

			if (std::strcmp(log.text, "SLAAPS: check SLA_PWR_VMAX") == 0) { ++voltage_log_count; }

			if (std::strcmp(log.text, "SLAAPS: check SLA_PWR_ID") == 0) { ++identity_log_count; }

			if (std::strcmp(log.text, "SLAAPS: check AUX channel 8 (routing 8)") == 0) { ++aux8_log_count; aux8_log_time = log.timestamp; }

			if (std::strcmp(log.text, "SLAAPS: check AUX channel 6 (routing 8)") == 0) { ++aux6_log_count; aux6_log_time = log.timestamp; }
		}
	};
	int32_t diagnostic_apply = 304;
	check(param_set(voltage_parameter, &rejected_voltage) == 0
	      && param_set(param_find("SLA_CFG_APPLY"), &diagnostic_apply) == 0,
	      "deliberately apply an invalid power voltage band through actual parameters");
	wait([&]() {
		collect_diagnostics();
		return !status.configuration_valid && status.apply_counter == diagnostic_apply
		       && status.configuration_fault == static_cast<uint8_t>(ConfigurationFault::ParameterRange)
		       && std::strcmp(status.cfg_param, "SLA_PWR_VMAX") == 0 && voltage_log_count == 1;
	}, 1000000, "actual supervisor reports and emits the precise parameter that rejected the current apply");
	check(!status.can_run_mask && !status.can_arm_mask && !status.manual_worker_active
	      && !status.manual_preparation_session, "a detailed apply rejection cannot admit any stale candidate or output session");
	const auto rejected_status_time = status.timestamp;
	param_notify_changes();
	wait([&]() {
		collect_diagnostics();
		return status.timestamp > rejected_status_time
		       && status.apply_counter == diagnostic_apply && !status.configuration_valid
		       && status.configuration_fault == static_cast<uint8_t>(ConfigurationFault::ParameterRange)
		       && std::strcmp(status.cfg_param, "SLA_PWR_VMAX") == 0 && voltage_log_count == 1;
	}, 1000000, "unchanged parameter notification and later supervisor cycle retain the exact rejected-apply detail");
	check(param_set(voltage_parameter, &c.power.voltage_max) == 0, "mutate the rejected profile without approving a new apply");
	wait([&]() {
		collect_diagnostics();
		return !status.configuration_valid && status.apply_counter == diagnostic_apply
		       && status.configuration_fault == static_cast<uint8_t>(ConfigurationFault::NoCandidate)
		       && status.cfg_param[0] == '\0';
	}, 1000000, "unattempted profile mutation clears the obsolete parameter label without admitting the repaired profile");
	check(voltage_log_count == 1 && !identity_log_count, "notification and unattempted repair cannot repeat the prior apply's status text");
	diagnostic_apply = 305;
	diagnostic_log_start = hrt_absolute_time();
	check(param_set(battery_id_parameter, &rejected_id) == 0
	      && param_set(param_find("SLA_CFG_APPLY"), &diagnostic_apply) == 0,
	      "submit a different invalid apply without retaining the prior voltage error");
	wait([&]() {
		collect_diagnostics();
		return !status.configuration_valid && status.apply_counter == diagnostic_apply
		       && status.configuration_fault == static_cast<uint8_t>(ConfigurationFault::ParameterRange)
		       && std::strcmp(status.cfg_param, "SLA_PWR_ID") == 0 && identity_log_count == 1;
	}, 1000000, "same-fault new rejected apply supersedes the parameter detail and emits the new exact parameter");
	diagnostic_apply = 306;
	check(param_set(battery_id_parameter, &accepted_id) == 0
	      && param_set(param_find("SLA_CFG_APPLY"), &diagnostic_apply) == 0, "explicitly apply the corrected coherent power profile");
	wait([&]() {
		return status.configuration_valid && status.apply_counter == diagnostic_apply
		       && status.configuration_fault == static_cast<uint8_t>(ConfigurationFault::None)
		       && status.cfg_param[0] == '\0' && status.routing_snapshot_valid;
	}, 1000000, "successful actual apply clears parameter detail and restores valid routing without entering a mode");
	const param_t center_parameter = param_find("PWM_AUX_CENT8");
	const param_t second_center_parameter = param_find("PWM_AUX_CENT6");
	const int32_t wrong_center = 1501, correct_center = 1500;
	diagnostic_log_start = hrt_absolute_time();
	check(param_set(center_parameter, &wrong_center) == 0, "change actual AUX8 calibration while the supervisor is inactive");
	wait([&]() {
		collect_diagnostics();
		return !status.routing_snapshot_valid && status.inventory_fault == static_cast<uint8_t>(InventoryFault::None)
		       && status.routing_fault == static_cast<uint8_t>(RoutingFault::InvalidCalibration)
		       && status.io_index == 15 && status.io_bank == 1 && status.io_channel == 8
		       && aux8_log_count == 1;
	}, 1500000, "actual mixer reports expose and emit the rejected inventory index and physical AUX8 channel");
	check(param_set(center_parameter, &correct_center) == 0 && param_set(second_center_parameter, &wrong_center) == 0,
	      "replace the known AUX8 error with an AUX6 error of the same routing fault class");
	wait([&]() {
		collect_diagnostics();
		return !status.routing_snapshot_valid && status.inventory_fault == static_cast<uint8_t>(InventoryFault::None)
		       && status.routing_fault == static_cast<uint8_t>(RoutingFault::InvalidCalibration)
		       && status.io_index == 13 && status.io_bank == 1 && status.io_channel == 6
		       && aux6_log_count == 1;
	}, 1500000, "latest same-fault physical channel emits after the existing bounded routing-report interval");

	if (!(aux8_log_count == 1 && aux6_log_time >= aux8_log_time + 1000000)) {
		const bool ordered = aux6_log_time >= aux8_log_time;
		const uint64_t delta = ordered ? aux6_log_time - aux8_log_time : aux8_log_time - aux6_log_time;
		std::fprintf(stderr,
			     "ROUTING_DIAGNOSTIC_FAILURE aux8_count=%u aux6_count=%u aux8_time=%llu aux6_time=%llu delta_us=%c%llu minimum_us=1000000\n",
			     aux8_log_count, aux6_log_count, static_cast<unsigned long long>(aux8_log_time),
			     static_cast<unsigned long long>(aux6_log_time), ordered ? '+' : '-', static_cast<unsigned long long>(delta));
	}

	check(aux8_log_count == 1 && aux6_log_time >= aux8_log_time + 1000000,
	      "routing message channel updates neither repeat the old pin nor bypass the one-second reporting bound");
	check(param_set(second_center_parameter, &correct_center) == 0, "restore actual AUX6 calibration without a new ownership session");
	wait([&]() {
		return status.routing_snapshot_valid && status.routing_fault == static_cast<uint8_t>(RoutingFault::None)
		       && status.io_index == -1 && status.io_bank == UINT8_MAX && status.io_channel == 0;
	}, 1000000, "fresh successful routing validation clears stale index and physical channel detail");
	PX4_INFO("SLAAPS_OPERATOR_DIAGNOSTIC_TEST_PASS actual_source=1 emitted_mavlink_log=1 actual_mixer_routing=1 qgc_display=0");

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
	const int32_t scheduled_apply = 307;
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

	// Synthetic operator declarations exercise the same inactive apply and
	// accepted markers; neither this record ID nor its assessment is evidence
	// of a physical measurement, qualification review or flight permission.
	const param_t record_parameter = param_find("SLA_DATA_REC"), assessment_parameter = param_find("SLA_DATA_STATE");
	const int32_t missing_record = 0, calibrated = 1, qualified = 2, synthetic_record = 4242, calibrated_revision = 17;
	int32_t provenance_apply = 308;
	check(param_set(record_parameter, &missing_record) == 0 && param_set(assessment_parameter, &calibrated) == 0
	      && param_set(param_find("SLA_CFG_APPLY"), &provenance_apply) == 0,
	      "submit an operator calibrated declaration without a measurement-record ID through native parameters");
	wait([&]() {
		return !status.configuration_valid && status.apply_counter == provenance_apply
		       && status.configuration_fault == static_cast<uint8_t>(ConfigurationFault::MissingRecord)
		       && std::strcmp(status.cfg_param, "SLA_DATA_REC") == 0;
	}, 1000000, "actual inactive apply rejects missing provenance with the exact actionable record parameter");
	auto declared_configuration = scheduled;
	declared_configuration.data_schema_version = 1;
	declared_configuration.data_record_id = synthetic_record;
	declared_configuration.data_revision = calibrated_revision;
	declared_configuration.data_assessment = calibrated;
	provenance_apply = 309;
	check(param_set(record_parameter, &synthetic_record) == 0
	      && param_set(param_find("SLA_DATA_REV"), &calibrated_revision) == 0
	      && param_set(param_find("SLA_CFG_APPLY"), &provenance_apply) == 0,
	      "explicitly apply a labeled synthetic calibrated record and revision while inactive");
	wait([&]() {
		return status.configuration_valid && status.apply_counter == provenance_apply
		       && status.configuration_identity == fingerprint(declared_configuration);
	}, 1000000, "accepted identity binds numerical configuration and the operator-declared provenance tuple together");
	check(parameters.capture(profile, capture_duration) && Px4ConfigurationSource::remembered(profile)
	      && profile.configuration.data_schema_version == 1 && profile.configuration.data_record_id == synthetic_record
	      && profile.configuration.data_revision == calibrated_revision && profile.configuration.data_assessment == calibrated,
	      "actual coherent capture and saved acceptance markers retain schema, record, revision and declared assessment");
	const int32_t edited_record = synthetic_record + 1;
	check(param_set(record_parameter, &edited_record) == 0, "edit provenance without a new apply");
	wait([&]() { return !status.configuration_valid; }, 1000000, "a provenance edit revokes the previously accepted claim");
	const uint64_t invalidated_claim = status.timestamp;
	check(param_set(record_parameter, &synthetic_record) == 0, "restore the old record ID without approving it again");
	wait([&]() { return status.timestamp > invalidated_claim && !status.configuration_valid; }, 1000000,
	"restoring a record ID cannot automatically restore an invalidated accepted declaration");
	const int32_t qualified_revision = calibrated_revision + 1;
	const uint64_t qualified_started = hrt_absolute_time();
	declared_configuration.data_record_id = edited_record;
	declared_configuration.data_revision = qualified_revision;
	declared_configuration.data_assessment = qualified;
	provenance_apply = 310;
	check(param_set(record_parameter, &edited_record) == 0 && param_set(assessment_parameter, &qualified) == 0
	      && param_set(param_find("SLA_DATA_REV"), &qualified_revision) == 0
	      && param_set(param_find("SLA_CFG_APPLY"), &provenance_apply) == 0,
	      "explicitly apply a synthetic operator Qualified declaration without a controller or output grant");
	wait([&]() {
		return status.configuration_valid && status.apply_counter == provenance_apply
		       && status.configuration_identity == fingerprint(declared_configuration);
	}, 1000000, "operator Qualified is recorded as accepted profile data independently of mode admission");
	slaaps_manual_command_s provenance_command{};
	check(!status.can_run_mask && !status.can_arm_mask && !status.manual_worker_active && !status.bench_worker_active
	      && !status.manual_preparation_session
	      && (!command_sub.copy(&provenance_command) || provenance_command.timestamp < qualified_started),
	      "a Qualified declaration cannot select a flight mode, begin preparation, start a worker or publish a new output command");
	PX4_INFO("SLAAPS_PROVENANCE_TEST_PASS native_apply=1 synthetic_declared_record=1 flight_permission=0");

	SlaapsNativeTest::admitted.store(false);
	auto *subject = ModuleBase::get_instance<SlaapsNativeTestSubject>(SlaapsNativeTestSubject::desc);
	check(subject != nullptr, "isolated supervisor retains its actual module descriptor");
	subject->request_stop();
	wait([&]() { return SlaapsNativeTestSubject::desc.object.load() == nullptr; }, 1000000,
	"actual supervisor unregisters and destroys its isolated source-linked instance");

	// A separate actual Run-queue instance exercises the connected flight caller.
	// Its authority witness, model and post-parser evidence are synthetic; both
	// real mixer callbacks still capture only, and no ordinary health/flight
	// qualification follows from this isolated process.
	flight_profile.companion.mavlink_instance = signed_flight ? 0 : 3;

	if (signed_flight) {
		// PublicationBase deliberately retains queued advertisements on
		// destruction. Earlier isolated packet_3 fixtures have finished, but
		// their node/history can remain. Consume at most the retained queue;
		// these discarded ordinary copies are never authentication evidence.
		for (unsigned old = 0; old < slaaps_companion_packet_s::ORB_QUEUE_LENGTH && flight_injected_lifetime.updated(); ++old) {
			slaaps_companion_packet_s historical{};
			check(flight_injected_lifetime.update(&historical), "drain only retained pre-flight packet history");
		}

		flight_injected_revision = flight_injected_lifetime.revision();
		flight_injected_generation = flight_injected_lifetime.get_last_generation();
		check(flight_injection_unchanged(), "signed active scenario pins old packet history without advertising its injection publisher");
	}

	installNativeMixerConfiguration(flight_profile);
	const int32_t flight_apply = 311, no_action = 0;
	const float no_offset = 0.F;
	const char *flight_positions[kAxes] {"EKF2_IMU_POS_X", "EKF2_IMU_POS_Y", "EKF2_IMU_POS_Z"};

	for (const char *position : flight_positions) {
		check(param_set(param_find(position), &no_offset) == 0, "synthetic flight feedback uses native zero CG-to-IMU offset");
	}

	check(companionClockRenewalConfigured(flight_profile.companion, flight_renewal_lead)
	      && param_set(param_find("IMU_ACCEL_CUTOFF"), &no_offset) == 0
	      && param_set(param_find("SLA_CFG_KIND"), &scope) == 0
	      && param_set(param_find("SLA_CFG_APPLY"), &flight_apply) == 0
	      && param_set(param_find("SLA_CMP_ACT"), &no_action) == 0
	      && parameters.capture(profile, capture_duration) && parameters.remember(profile)
	      && parameters.capture(profile, capture_duration) && Px4ConfigurationSource::remembered(profile)
	      && equal(profile.configuration, flight_profile),
	      "scheduled flight-transfer fixture binds its explicit shared synthetic model through coherent accepted parameters");
	check(flight_raw_pub.advertise() && flight_bias_pub.advertise()
	      && flight_raw_pub.get_instance() == 0 && flight_bias_pub.get_instance() == 0,
	      "flight-transfer feedback uses actual selected raw and bias instance-zero publisher lifetimes");
	publish_flight = publish_conditions = publish_inputs = publish_estimates = publish_manual = publish_power = true;
	pump_aux = true;
	vehicle_armed = armed.armed = armed.prearmed = false;
	vehicle.nav_state = vehicle.nav_state_user_intention = vehicle_status_s::NAVIGATION_STATE_MANUAL;
	roll = 0.F;
	throttle = -1.F;
	pump();
	const auto flight_started = hrt_absolute_time();
	check(SlaapsNativeTestSubject::task_spawn(1, nullptr) == PX4_OK,
	      "isolated scheduled supervisor starts the real flight caller without direct private owner mutation");
	wait([&]() {
		return status.timestamp >= flight_started && status.configuration_valid
		       && status.configuration_identity == fingerprint(flight_profile) && status.direct_actuator_modes_configured
		       && flight_control.enabled && flight_protocol.configuration_identity == status.configuration_identity
		       && flight_protocol.required_state_valid && flight_protocol.feedback_valid;
	}, 3000000, "scheduled companion owner observes fresh synthetic authority and actual selected feedback provenance");
	auto flight_establish_session = [&]() {
		flight_keepalive = true;
		wait([&]() {
			return flight_protocol.transport_connected && flight_protocol.proposal_session == flight_companion_session
			       && flight_protocol.proposal_deadline > flight_protocol.timestamp;
		}, 500000, "configured session proposal proves the selected test ingress without granting an output owner");
		const int32_t prepare_action = 1, grant_action = 2;
		check(param_set(param_find("SLA_CMP_ACT"), &prepare_action) == 0,
		      "operator deliberately prepares the scheduled companion owner while disarmed");
		wait([&]() { return flight_protocol.action == 1 && flight_protocol.action_result == 1 && flight_protocol.prepared; }, 500000,
		"actual serialized Prepare establishes only the mapping transaction");
		flight_clock();
		wait([&]() { return flight_protocol.map_valid && flight_protocol.map_session == flight_companion_session; }, 500000,
		"matched synthetic clock evidence freezes an original bounded map through the actual owner");
		const auto initial_flight_map_epoch = flight_protocol.map_epoch;
		const auto pre_map_proposal_deadline = flight_protocol.proposal_deadline;
		flight_session(false);
		wait([&]() {
			return flight_protocol.proposal_session == flight_companion_session && flight_protocol.proposal_deadline > pre_map_proposal_deadline
			       && flight_protocol.proposal_deadline > flight_protocol.timestamp
			       && flight_protocol.transition_epoch && flight_protocol.map_epoch == initial_flight_map_epoch;
		}, 500000, "fresh post-map proposal reaches the scheduled owner");
		check(param_set(param_find("SLA_CMP_ACT"), &grant_action) == 0, "operator explicitly grants the same disarmed mapped session");
		wait([&]() {
			return flight_protocol.action == 2 && flight_protocol.action_result == 1 && flight_protocol.session_established
			       && !flight_protocol.acknowledgement_current;
		}, 500000, "scheduled Grant authorizes protocol entry without active outputs or a reused acknowledgement");
		flight_expect_session = true;
		// The same established owner must renew while the real banks prepare too;
		// preparation is not guaranteed to fit inside the first clock-map interval.
		// ACKs remain explicit until the post-arm barrier is deliberately accepted.
		flight_renew_clock = true;
	};
	flight_establish_session();
	SlaapsNativeTest::admitted.store(true); // Existing isolated synthetic-authority seam only.
	vehicle.nav_state = vehicle.nav_state_user_intention = status.mode_id[kManualMode];
	wait([&]() {
		return status.manual_entry_status == slaaps_status_s::MANUAL_ENTRY_PREPARED
		       && flight_protocol.commander_transition == flight_witness.transition
		       && flight_protocol.timestamp >= vehicle.nav_state_timestamp && flight_protocol.session_established
		       && flight_protocol.map_valid && flight_protocol.required_state_valid && flight_protocol.feedback_valid;
	}, 500000, "actual Manual preparation and protocol both observe the disarmed entry before synthetic arming");
	const auto flight_session_id = status.manual_preparation_session;
	vehicle_armed = armed.armed = true;
	wait([&]() {
		return status.manual_worker_active && status.manual_entry_status == slaaps_status_s::MANUAL_ENTRY_RUNNING
		       && flight_protocol.session_established && flight_protocol.required_state_valid
		       && flight_protocol.commander_transition == flight_witness.transition;
	}, 500000, "Manual first command reaches exact handling on both real callback adapters");
	flight_renew_clock = false;
	wait([&]() {
		return !flight_clock_pending && status.companion_event_sequence >= flight_last_clock_event
		       && flight_protocol.timestamp >= flight_last_clock;
	}, 100000, "the owner consumes any queued renewal before the explicit post-arm acknowledgement");
	wait([&]() { return hrt_absolute_time() > flight_protocol.barrier_time + 2 * flight_protocol.map_uncertainty_us; }, 100000,
	"new armed protocol barrier precedes its producer acknowledgement");
	flight_session(true);
	wait([&]() { return flight_protocol.acknowledgement_current; }, 100000,
	"fresh exact post-arm acknowledgement belongs to the established session");
	flight_renew_clock = flight_auto_ack = true;
	flight_stream_requests = true; // Manual requests remain ModeNotSelected, but prove link liveness.
	throttle = -.5F;
	slaaps_manual_command_s outgoing{};
	slaaps_manual_ack_s flight_ack[2] {};
	auto flight_handled = [&](const slaaps_manual_command_s & frame) {
		return main_ack_sub.copy(&flight_ack[0]) && aux_ack_sub.copy(&flight_ack[1])
		       && flight_ack[0].session == frame.session && flight_ack[1].session == frame.session
		       && flight_ack[0].sequence == frame.sequence && flight_ack[1].sequence == frame.sequence;
	};
	wait([&]() {
		return command_sub.copy(&outgoing) && outgoing.session == flight_session_id && outgoing.mode == slaaps_manual_command_s::MANUAL
		       && std::fabs(outgoing.edf - .25F) < .001F && flight_handled(outgoing);
	}, 200000, "actual Manual calculation and both callbacks reach the synthetic five-newton EDF operating point");
	flight_renew_clock = false;
	wait([&]() {
		return !flight_clock_pending && status.companion_event_sequence >= flight_last_clock_event && flight_protocol.timestamp >= flight_last_clock
		       && flight_protocol.acknowledgement_current;
	}, 100000, "Manual drains its last renewal and exact ACK before the deliberate forward handoff");
	flight_auto_ack = false;
	const auto flight_map_epoch = flight_protocol.map_epoch, flight_map_deadline = flight_protocol.map_deadline;
	const auto before_flight_transition = outgoing;
	flight_stream_requests = false;
	vehicle.nav_state = vehicle.nav_state_user_intention = status.mode_id[kAccelerationMode];
	pump();
	const auto forward_edge = vehicle.nav_state_timestamp;
	wait([&]() {
		return status.manual_worker_active && status.manual_entry_status == slaaps_status_s::MANUAL_ENTRY_ACTIVATING
		       && flight_protocol.session_established && !flight_protocol.acknowledgement_current
		       && flight_protocol.commander_transition == flight_witness.transition;
	}, 50000, "actual Manual-to-Acceleration edge waits for the new epoch acknowledgement and request");
	slaaps_manual_command_s waiting_command{};
	check(command_sub.copy(&waiting_command) && waiting_command.mode == slaaps_manual_command_s::MANUAL
	      && waiting_command.timestamp_sample <= forward_edge && waiting_command.deadline >= hrt_absolute_time()
	      && flight_protocol.map_epoch == flight_map_epoch && flight_protocol.map_deadline == flight_map_deadline,
	      "handoff wait emits no post-edge Manual calculation and does not renew the original map");
	const auto waiting_sequence = waiting_command.sequence, waiting_deadline = waiting_command.deadline;
	const uint64_t waiting_until = hrt_absolute_time() + 10000;

	while (hrt_absolute_time() < waiting_until) { pump(); px4_usleep(1000); }

	check(command_sub.copy(&waiting_command) && waiting_command.sequence == waiting_sequence && waiting_command.deadline == waiting_deadline
	      && status.manual_worker_active && status.manual_entry_status == slaaps_status_s::MANUAL_ENTRY_ACTIVATING,
	      "withholding target evidence preserves the exact outgoing command and its expiry without another calculation");
	wait([&]() { return hrt_absolute_time() > flight_protocol.barrier_time + 2 * flight_protocol.map_uncertainty_us; }, 50000,
	"target-mode epoch barrier precedes the new exact acknowledgement");
	flight_session(true);
	wait([&]() { return flight_protocol.acknowledgement_current; }, 50000,
	"Acceleration entry accepts only the fresh target-epoch acknowledgement");
	pump_aux = false;
	flight_stream_requests = true;
	slaaps_manual_command_s acceleration_command{};
	const uint64_t acceleration_handling_deadline = hrt_absolute_time() + 50000;
	wait([&]() {
		return command_sub.copy(&acceleration_command) && acceleration_command.session == flight_session_id
		       && acceleration_command.mode == slaaps_manual_command_s::ACCELERATION && acceleration_command.transition == forward_edge
		       && acceleration_command.previous_handled_sequence && control_detail.calculated_sequence == acceleration_command.sequence
		       && control_detail.state == slaaps_acceleration_status_s::CALCULATED;
	}, 50000, "calculated Acceleration status is visible before the withheld AUX callback");
	const auto pending_control_detail = control_detail;
	check(!pending_control_detail.handled_sequence && pending_control_detail.calculated_deadline == acceleration_command.deadline
	      && pending_control_detail.active && !flight_handled(acceleration_command),
	      "a calculated command and one bank cannot be presented as all-bank handled saturation");
	pump_aux = true;
	const auto handling_now = hrt_absolute_time();
	wait([&]() {
		return flight_handled(acceleration_command) && control_detail.handled_sequence == acceleration_command.sequence
		       && control_detail.handled_us >= acceleration_command.timestamp_sample
		       && control_detail.handled_deadline == acceleration_command.deadline;
	}, handling_now < acceleration_handling_deadline ? acceleration_handling_deadline - handling_now : 0,
	"all required callbacks promote handled diagnostics inside the unchanged first50ms observation budget");
	check(control_detail.handled_moment_mask == pending_control_detail.calculated_moment_mask
	      && control_detail.handled_canard_mask == pending_control_detail.calculated_canard_mask
	      && hrt_absolute_time() < acceleration_handling_deadline,
	      "handled modeled limits are copied from the exact all-bank acknowledged calculation");

	if (signed_flight) {
		check(flight_receiver_revision && flight_signing_generation && flight_verified_clocks && flight_verified_requests
		      && flight_protocol.receiver_revision == flight_receiver_revision && flight_protocol.signing_generation == flight_signing_generation
		      && flight_signed_packets.revision() == flight_receiver_revision && flight_injection_unchanged(),
		      "active Acceleration and both handled banks bind the actual signed receiver without packet-topic injection");
	}

	check(acceleration_command.configuration == before_flight_transition.configuration
	      && acceleration_command.source == before_flight_transition.source && acceleration_command.routing == before_flight_transition.routing
	      && acceleration_command.driver[0] == before_flight_transition.driver[0]
	      && acceleration_command.driver[1] == before_flight_transition.driver[1]
	      && acceleration_command.sequence > waiting_sequence && acceleration_command.edf_sequence > before_flight_transition.edf_sequence
	      && acceleration_command.previous_deadline == waiting_deadline && acceleration_command.deadline <= waiting_deadline
	      && std::fabs(acceleration_command.edf - .25F) < .001F,
	      "forward transfer preserves exact same-session ownership, handled boundary, current EDF and original outgoing lease");
	wait([&]() { return status.manual_entry_status == slaaps_status_s::MANUAL_ENTRY_RUNNING; }, 50000,
	"Acceleration is Running only after both target-mode commands are exactly handled");
	// Keep the factory's original 500 ms map. Ongoing control renews only
	// through clock evidence and a fresh exact barrier ACK/request.
	flight_renew_clock = flight_auto_ack = true;
	const auto acceleration_sequence = acceleration_command.sequence;
	publish_manual = false;
	const auto pilot_hold_started = hrt_absolute_time();
	const auto pilot_hold_until = pilot_hold_started + static_cast<uint64_t>(flight_profile.manual_age_ms) * 1000 + 10000;

	while (hrt_absolute_time() < pilot_hold_until) { pump(); px4_usleep(1000); }

	const bool have_pilot_expiry_command = command_sub.copy(&acceleration_command)
					       && acceleration_command.mode == slaaps_manual_command_s::ACCELERATION;
	const auto pilot_expiry_sequence = acceleration_command.sequence;
	const auto pilot_expiry_deadline = acceleration_command.deadline;
	bool ongoing_acceleration = false;

	// Renewal invalidates the prior ACK/request and can leave the real caller
	// ACTIVATING at this arbitrary observation boundary. Require fresh post-pilot
	// expiry handling within the captured ORIGINAL command lease, never a new
	// timeout or success based solely on that intermediate state.
	while (have_pilot_expiry_command && hrt_absolute_time() < pilot_expiry_deadline) {
		pump();

		if (!status.manual_worker_active || !flight_protocol.session_established || !flight_protocol.map_valid
		    || !flight_protocol.required_state_valid || !flight_protocol.feedback_valid
		    || (status.manual_entry_status != slaaps_status_s::MANUAL_ENTRY_RUNNING
			&& status.manual_entry_status != slaaps_status_s::MANUAL_ENTRY_ACTIVATING)) { break; }

		if (command_sub.copy(&acceleration_command) && acceleration_command.mode == slaaps_manual_command_s::ACCELERATION
		    && acceleration_command.sequence > pilot_expiry_sequence && acceleration_command.sequence > acceleration_sequence
		    && acceleration_command.timestamp_sample >= pilot_hold_until && flight_handled(acceleration_command)
		    && control_detail.handled_sequence == acceleration_command.sequence
		    && status.manual_entry_status == slaaps_status_s::MANUAL_ENTRY_RUNNING && hrt_absolute_time() < pilot_expiry_deadline) {
			ongoing_acceleration = true;
			break;
		}

		px4_usleep(1000);
	}

	if (!ongoing_acceleration) {
		const auto observed = hrt_absolute_time();
		uORB::Subscription acceleration_detail_sub{ORB_ID(slaaps_acceleration_status)};
		slaaps_acceleration_status_s detail{};
		const bool have_detail = acceleration_detail_sub.copy(&detail);
		std::fprintf(stderr,
			     "ACCEL_ONGOING_FAILURE now=%llu status=%llu entry=%u worker=%u active=%d config=%d original_deadline=%llu\n",
			     static_cast<unsigned long long>(observed), static_cast<unsigned long long>(status.timestamp),
			     static_cast<unsigned>(status.manual_entry_status), static_cast<unsigned>(status.manual_worker_status),
			     status.manual_worker_active, status.configuration_valid, static_cast<unsigned long long>(pilot_expiry_deadline));
		std::fprintf(stderr,
			     "ACCEL_ONGOING_FAILURE protocol=%llu epoch=%llu map=%llu map_deadline=%llu link_deadline=%llu state=%d feedback=%d feedback_deadline=%llu ack=%d decision=%u\n",
			     static_cast<unsigned long long>(flight_protocol.timestamp), static_cast<unsigned long long>(flight_protocol.transition_epoch),
			     static_cast<unsigned long long>(flight_protocol.map_epoch), static_cast<unsigned long long>(flight_protocol.map_deadline),
			     static_cast<unsigned long long>(flight_protocol.connection_deadline), flight_protocol.required_state_valid,
			     flight_protocol.feedback_valid, static_cast<unsigned long long>(flight_protocol.feedback_deadline),
			     flight_protocol.acknowledgement_current, static_cast<unsigned>(flight_protocol.decision_code));
		std::fprintf(stderr,
			     "ACCEL_ONGOING_FAILURE command session=%llu seq=%llu mode=%u sample=%llu deadline=%llu\n",
			     static_cast<unsigned long long>(acceleration_command.session), static_cast<unsigned long long>(acceleration_command.sequence),
			     static_cast<unsigned>(acceleration_command.mode), static_cast<unsigned long long>(acceleration_command.timestamp_sample),
			     static_cast<unsigned long long>(acceleration_command.deadline));
		std::fprintf(stderr,
			     "ACCEL_ONGOING_FAILURE detail=%d at=%llu state=%u fault=%u stage=%u solver=%u worker=%u fault_time=%llu request=%llu/%llu/%llu\n",
			     have_detail, static_cast<unsigned long long>(detail.timestamp), static_cast<unsigned>(detail.state),
			     static_cast<unsigned>(detail.control_fault), static_cast<unsigned>(detail.allocation_stage),
			     static_cast<unsigned>(detail.allocation_result), static_cast<unsigned>(detail.worker_status),
			     static_cast<unsigned long long>(detail.fault_observed_us), static_cast<unsigned long long>(detail.request_source),
			     static_cast<unsigned long long>(detail.request_session), static_cast<unsigned long long>(detail.request_sequence));
		std::fprintf(stderr,
			     "ACCEL_ONGOING_FAILURE calculated=%llu deadline=%llu handled=%llu at=%llu deadline=%llu\n",
			     static_cast<unsigned long long>(detail.calculated_sequence), static_cast<unsigned long long>(detail.calculated_deadline),
			     static_cast<unsigned long long>(detail.handled_sequence), static_cast<unsigned long long>(detail.handled_us),
			     static_cast<unsigned long long>(detail.handled_deadline));
	}

	check(ongoing_acceleration,
	      "ongoing Acceleration uses fresh selected state and companion evidence after the untouched pilot sample expires");
	flight_stream_requests = flight_renew_clock = flight_auto_ack = false;
	flight_expect_session = false; // Reverse intentionally revokes companion authority.
	vehicle.nav_state = vehicle.nav_state_user_intention = status.mode_id[kManualMode];
	pump();
	const auto reverse_edge = vehicle.nav_state_timestamp;
	wait([&]() { return status.manual_worker_active && status.manual_entry_status == slaaps_status_s::MANUAL_ENTRY_ACTIVATING; }, 50000,
	"reverse flight edge waits without reusing the pre-edge pilot sample");
	slaaps_manual_command_s reverse_wait{};
	check(command_sub.copy(&reverse_wait) && reverse_wait.mode == slaaps_manual_command_s::ACCELERATION
	      && reverse_wait.timestamp_sample <= reverse_edge,
	      "reverse handoff cannot calculate a new Manual command before genuine post-edge pilot input");
	publish_manual = true;
	slaaps_manual_command_s reverse_command{};
	wait([&]() {
		return command_sub.copy(&reverse_command) && reverse_command.mode == slaaps_manual_command_s::MANUAL
		       && reverse_command.transition == reverse_edge && reverse_command.previous_handled_sequence
		       && flight_handled(reverse_command);
	}, 50000, "fresh raw pilot input transfers the same worker and both banks back to Manual");
	check(reverse_command.session == flight_session_id && reverse_command.configuration == before_flight_transition.configuration
	      && reverse_command.source == before_flight_transition.source && reverse_command.routing == before_flight_transition.routing
	      && reverse_command.sequence > reverse_wait.sequence && reverse_command.edf_sequence > acceleration_command.edf_sequence
	      && reverse_command.previous_deadline == reverse_wait.deadline && reverse_command.deadline <= reverse_wait.deadline
	      && std::fabs(reverse_command.edf - acceleration_command.edf) < .001F
	      && !main.prematureAcknowledgment() && !aux.prematureAcknowledgment(),
	      "reverse transfer retains continuous EDF/output history and exact prior handled expiry without a new owner");
	wait([&]() { return status.manual_entry_status == slaaps_status_s::MANUAL_ENTRY_RUNNING && !flight_protocol.session_established; }, 50000,
	"reverse Manual operation needs exact handling and revokes the old companion session");
	vehicle_armed = armed.armed = false;
	wait([&]() { return !status.manual_worker_active && status.configuration_valid; }, 500000,
	"ordinary disarm stops the shared flight worker and revalidates the unchanged accepted profile");

	// A new deliberate session proves the remaining capture boundary, after
	// preserving the full bidirectional handoff above. The first actuator disarm
	// is published on the supervisor queue after its armed checks but before the
	// protocol copies authority; the atomic witness is still the actual prior one.
	flight_keepalive = false;
	vehicle.nav_state = vehicle.nav_state_user_intention = vehicle_status_s::NAVIGATION_STATE_MANUAL;
	throttle = -1.F;
	wait([&]() {
		return status.manual_entry_status == slaaps_status_s::MANUAL_ENTRY_INACTIVE && status.configuration_valid
		       && flight_protocol.required_state_valid && flight_protocol.feedback_valid && !flight_protocol.session_established;
	}, 1000000, "capture-disarm regression deliberately leaves disarmed before creating another companion session");
	flight_companion_session = 7302;
	flight_establish_session();
	uint64_t previous_flight_output_session = flight_session_id;
	auto flight_enter_acceleration = [&]() {
		flight_single_request = false;
		flight_lateral_request = 0.F;
		vehicle.nav_state = vehicle.nav_state_user_intention = status.mode_id[kManualMode];
		wait([&]() {
			return status.manual_entry_status == slaaps_status_s::MANUAL_ENTRY_PREPARED && flight_protocol.session_established
			       && flight_protocol.commander_transition == flight_witness.transition;
		}, 500000, "new actual Manual preparation observes its own fresh protocol entry before arming");
		const auto capture_session = status.manual_preparation_session;
		check(capture_session > previous_flight_output_session, "deliberate reentry owns a genuinely new output session");
		vehicle_armed = armed.armed = true;
		wait([&]() {
			return status.manual_worker_active && status.manual_entry_status == slaaps_status_s::MANUAL_ENTRY_RUNNING
			       && flight_protocol.session_established && flight_protocol.commander_transition == flight_witness.transition;
		}, 500000, "new actual Manual worker begins only after both real callback adapters handle its initial output");
		flight_renew_clock = false;
		wait([&]() {
			return !flight_clock_pending && status.companion_event_sequence >= flight_last_clock_event && flight_protocol.timestamp >= flight_last_clock
			       && hrt_absolute_time() > flight_protocol.barrier_time + 2 * flight_protocol.map_uncertainty_us;
		}, 100000, "new explicit post-arm ACK follows consumed clock evidence and its actual epoch barrier");
		flight_session(true);
		wait([&]() { return flight_protocol.acknowledgement_current; }, 100000, "new armed session accepts its own exact current ACK");
		flight_renew_clock = flight_auto_ack = flight_stream_requests = true;
		throttle = -.5F;
		wait([&]() {
			return command_sub.copy(&outgoing) && outgoing.session == capture_session && outgoing.mode == slaaps_manual_command_s::MANUAL
			       && std::fabs(outgoing.edf - .25F) < .001F && flight_handled(outgoing);
		}, 200000, "new Manual session reaches the same explicitly feasible synthetic transfer operating point");
		flight_renew_clock = false;
		wait([&]() {
			return !flight_clock_pending && status.companion_event_sequence >= flight_last_clock_event && flight_protocol.timestamp >= flight_last_clock
			       && flight_protocol.acknowledgement_current;
		}, 100000, "new forward handoff drains clock renewal before withholding its target ACK");
		flight_auto_ack = flight_stream_requests = false;
		vehicle.nav_state = vehicle.nav_state_user_intention = status.mode_id[kAccelerationMode];
		pump();
		const auto capture_edge = vehicle.nav_state_timestamp;
		wait([&]() {
			return status.manual_worker_active && status.manual_entry_status == slaaps_status_s::MANUAL_ENTRY_ACTIVATING
			       && !flight_protocol.acknowledgement_current && flight_protocol.commander_transition == flight_witness.transition
			       && hrt_absolute_time() > flight_protocol.barrier_time + 2 * flight_protocol.map_uncertainty_us;
		}, 50000, "new forward handoff requires a new exact target-mode epoch ACK");
		slaaps_manual_command_s held_before_ack{};
		slaaps_manual_permission_s original_permission{};
		check(command_sub.copy(&held_before_ack) && held_before_ack.session == capture_session
		      && held_before_ack.mode == slaaps_manual_command_s::MANUAL && held_before_ack.timestamp_sample <= capture_edge
		      && scheduled_permission_sub.copy(&original_permission) && original_permission.session == capture_session,
		      "target ACK wait captures the real outgoing command and original permission bound");
		const uint64_t handoff_deadline = math::min(held_before_ack.deadline, math::min(original_permission.deadline,
						   math::min(flight_protocol.map_deadline, flight_protocol.connection_deadline)));
		const auto target_epoch = flight_protocol.transition_epoch, target_map = flight_protocol.map_epoch;
		auto handoff_remaining = [&]() {
			const auto current = hrt_absolute_time();
			return current < handoff_deadline ? handoff_deadline - current : 0;
		};
		check(handoff_remaining(), "target ACK and first handled output share a live immutable original lease");
		flight_session(true);
		wait([&]() {
			slaaps_manual_command_s held{};
			check(command_sub.copy(&held) && held.sequence == held_before_ack.sequence && held.deadline == held_before_ack.deadline
			      && held.timestamp_sample == held_before_ack.timestamp_sample && status.manual_worker_active
			      && flight_protocol.transition_epoch == target_epoch && flight_protocol.map_epoch == target_map,
			      "waiting for the wire ACK cannot calculate or renew the outgoing command or change the captured epoch");
			return flight_protocol.acknowledgement_current && hrt_absolute_time() < handoff_deadline;
		}, handoff_remaining(), "new Acceleration edge accepts only its own post-mode ACK inside the original lease");
		flight_stream_requests = true;
		wait([&]() {
			return command_sub.copy(&acceleration_command) && acceleration_command.session == capture_session
			       && acceleration_command.mode == slaaps_manual_command_s::ACCELERATION && acceleration_command.transition == capture_edge
			       && flight_handled(acceleration_command) && status.manual_entry_status == slaaps_status_s::MANUAL_ENTRY_RUNNING;
		}, handoff_remaining(), "actual supervisor reaches active Acceleration inside that same original lease");
		check(hrt_absolute_time() < handoff_deadline,
		      "ACK and first handled Acceleration output do not restart a second observation window");
		previous_flight_output_session = capture_session;
		return capture_session;
	};
	(void)flight_enter_acceleration();
	flight_expect_session = false;
	flight_stream_requests = false;
	disarm_injection.at_capture.store(true, std::memory_order_release);
	disarm_injection.requested.store(2, std::memory_order_release);
	wait([&]() { return !status.manual_worker_active; }, 300000,
	"fresh actuator-first disarm inside acceleration capture stops the actual shared worker");
	check(disarm_injection.completed.load(std::memory_order_acquire) == 2 && disarm_injection.original_both_armed
	      && disarm_injection.published && command_sub.copy(&outgoing) && outgoing.timestamp_sample <= disarm_injection.injected_us,
	      "capture-disarm injection occurs after armed caller checks and produces no subsequent control calculation");
	vehicle_armed = armed.armed = false;
	disarm_injection.requested.store(0, std::memory_order_release);
	disarm_injection.completed.store(0, std::memory_order_release);
	disarm_injection.at_capture.store(false, std::memory_order_release);
	wait([&]() { return !status.manual_worker_active && status.configuration_valid; }, 1000000,
	"capture-observed ordinary disarm retains the exact accepted profile only after both actual authorities are freshly disarmed");
	const auto capture_stopped_sequence = outgoing.sequence;
	const auto no_revival_until = hrt_absolute_time() + 120000;

	while (hrt_absolute_time() < no_revival_until) { pump(); px4_usleep(1000); }

	check(!status.manual_worker_active && status.configuration_identity == fingerprint(flight_profile) && status.apply_counter == flight_apply
	      && command_sub.copy(&outgoing) && outgoing.sequence == capture_stopped_sequence && !flight_protocol.session_established,
	      "fresh restored sources and the old signed-session proposal cannot revive a disarmed Acceleration owner or renew its command");

	// A final deliberate session preserves both successful lifecycles above.
	// This synthetic model has no Y-force column or Y-force offset at any
	// scheduled corner. A within-contract lateral demand therefore reaches the
	// numerical allocator and exceeds its configured residual tolerance.
	flight_keepalive = flight_renew_clock = flight_auto_ack = false;
	vehicle.nav_state = vehicle.nav_state_user_intention = vehicle_status_s::NAVIGATION_STATE_MANUAL;
	throttle = -1.F;
	wait([&]() {
		return status.manual_entry_status == slaaps_status_s::MANUAL_ENTRY_INACTIVE && status.configuration_valid
		       && flight_protocol.required_state_valid && flight_protocol.feedback_valid && !flight_protocol.session_established;
	}, 1000000, "allocator diagnostic requires a fresh deliberate disarmed reentry");
	flight_companion_session = 7303;
	flight_establish_session();
	const auto rejection_session = flight_enter_acceleration();
	check(control_detail.active && control_detail.control_fault == slaaps_acceleration_status_s::FAULT_NONE,
	      "third session starts from real active Acceleration without a retained numerical rejection");
	check(flight_profile.companion.acceleration_minimum[1] <= 1.F && flight_profile.companion.acceleration_maximum[1] >= 1.F
	      && std::sqrt(26.F + flight_profile.feedback.gravity_m_s2 * flight_profile.feedback.gravity_m_s2)
	      <= flight_profile.companion.acceleration_norm_max, "lateral diagnostic request lies inside the unchanged typed wire bounds");

	for (unsigned speed = 0; speed < 2; ++speed) {
		for (unsigned edf = 0; edf < 2; ++edf) {
			const auto &corner = flight_profile.schedule.corner[speed][edf];
			check(std::fabs(corner.trim_force[1]) < 1.e-6F && std::fabs(corner.edf_force_per_thrust[1]) < 1.e-6F,
			      "synthetic scheduled model has no lateral EDF force or trim force");

			for (unsigned canard = 0; canard < kCanards; ++canard) {
				check(std::fabs(corner.force_derivative[1][canard]) < 1.e-6F,
				      "synthetic canard columns cannot provide the requested lateral force");
			}
		}
	}

	flight_single_request = true;
	flight_lateral_request = 1.F;
	diagnostic_log_start = hrt_absolute_time();
	unsigned allocation_log_count{};
	auto collect_allocation_log = [&]() {
		mavlink_log_s log{};

		for (unsigned i = 0; i < mavlink_log_s::ORB_QUEUE_LENGTH && diagnostic_logs.update(&log); ++i) {
			if (log.timestamp >= diagnostic_log_start && std::strcmp(log.text, "SLAAPS accel: target allocation rejected") == 0) {
				++allocation_log_count;
			}
		}
	};
	flight_stream_requests = true;
	wait([&]() {
		collect_allocation_log();
		return !status.manual_worker_active && control_detail.state == slaaps_acceleration_status_s::REJECTED
		       && control_detail.request_session == flight_companion_session && flight_rejected_request
		       && control_detail.request_sequence == flight_rejected_request;
	}, 100000, "one fresh within-contract request reaches the allocator and reports its exact retained rejection");
	const auto rejected_detail = control_detail;
	const unsigned captures = native_acceleration_capture_count.load(std::memory_order_acquire);
	check(captures && !native_acceleration_capture_overflow.load(std::memory_order_relaxed),
	      "the real caller observes successful guarded capture before numerical rejection");
	const auto &captured_rejection = native_acceleration_captures[captures - 1];
	check(captured_rejection.source == rejected_detail.request_source && captured_rejection.session == rejected_detail.request_session
	      && captured_rejection.sequence == rejected_detail.request_sequence && captured_rejection.configuration == rejected_detail.configuration_identity
	      && captured_rejection.observed_us == rejected_detail.request_observed_us && captured_rejection.captured_us <= rejected_detail.fault_observed_us
	      && captured_rejection.producer_created < captured_rejection.producer_expiry
	      && captured_rejection.mapped_created < captured_rejection.mapped_expiry
	      && captured_rejection.captured_us < captured_rejection.evidence_deadline,
	      "successful capture and retained allocator fault refer to the same original validated canonical request and evidence");
	slaaps_manual_command_s rejected_command{};
	check(!control_detail.active && control_detail.control_fault == slaaps_acceleration_status_s::FAULT_ALLOCATION
	      && control_detail.allocation_stage == slaaps_acceleration_status_s::ALLOCATION_FUTURE_TARGET
	      && control_detail.allocation_result == slaaps_acceleration_status_s::SOLVER_RESIDUAL
	      && control_detail.worker_status == static_cast<uint8_t>(ManualWorkerStatus::AccelerationRejected)
	      && control_detail.output_session == rejection_session && control_detail.request_source == 1
	      && control_detail.configuration_identity == fingerprint(flight_profile)
	      && control_detail.fault_observed_us >= flight_rejected_receipt && control_detail.request_observed_us
	      && control_detail.request_observed_us <= control_detail.fault_observed_us,
	      "precise allocation stage and original request correlation survive worker and supervisor cleanup");
	// The rejected calculation itself produced nothing: the last calculated frame is the last handled one.
	check(rejected_detail.calculated_sequence == rejected_detail.handled_sequence
	      && rejected_detail.calculated_deadline == rejected_detail.handled_deadline,
	      "rejected calculation creates no output sequence or handling proof and preserves the last successful command expiry");
	// D3/D4: the fault after a handled frame starts recovery. Only recovery frames follow: same session, a later
	// sequence created after the fault, one lease at most, never a calculated command.
	const uint64_t recovery_lease = static_cast<uint64_t>(flight_profile.output_lease_ms) * 1000;
	wait([&]() {
		return command_sub.copy(&rejected_command) && rejected_command.edf_disposition == slaaps_manual_command_s::EDF_RECOVERY;
	}, 100000, "the supervisor publishes a recovery frame after the rejected calculation");
	check(rejected_command.session == rejection_session && rejected_command.sequence > rejected_detail.handled_sequence
	      && rejected_command.timestamp_sample >= rejected_detail.fault_observed_us
	      && rejected_command.deadline > rejected_command.timestamp_sample
	      && rejected_command.deadline - rejected_command.timestamp_sample <= recovery_lease
	      && rejected_command.edf >= 0.F && rejected_command.edf <= 1.F,
	      "recovery frame is a bounded same-session frame created after the fault, not the rejected request");
	wait([&]() { collect_allocation_log(); return allocation_log_count == 1; }, 1500000,
	"actual mavlink_log publishes the rate-bounded actionable allocator rejection");
	const auto retained_until = hrt_absolute_time() + 120000;

	while (hrt_absolute_time() < retained_until) { pump(); collect_allocation_log(); px4_usleep(1000); }

	check(command_sub.copy(&outgoing) && outgoing.session == rejected_command.session && outgoing.sequence >= rejected_command.sequence
	      && outgoing.edf_disposition == slaaps_manual_command_s::EDF_RECOVERY && outgoing.edf <= rejected_command.edf
	      && std::memcmp(outgoing.canards, rejected_command.canards, sizeof(outgoing.canards)) == 0
	      && !status.manual_worker_active && !status.can_run_mask && !status.can_arm_mask && allocation_log_count == 1
	      && control_detail.fault_observed_us == rejected_detail.fault_observed_us
	      && control_detail.handled_sequence == rejected_detail.handled_sequence && control_detail.handled_us == rejected_detail.handled_us
	      && control_detail.handled_deadline == rejected_detail.handled_deadline,
	      "recovery holds the canards and never raises EDF; retained diagnostics are not renewed or repeated");
	check(std::printf("SLAAPS_ACCEL_DIAGNOSTIC_TEST_PASS source=1 session=7303 request=%llu stage=1 solver=5 qgc_log=1 modeled_only=1 guarded_capture=1\n",
			  static_cast<unsigned long long>(flight_rejected_request)) > 0 && std::fflush(stdout) == 0,
	      "emit complete exact request correlation for the signed allocator diagnostic");
	check(std::printf("SLAAPS_GUARDED_CAPTURE source=%llu session=%llu request=%llu config=%llu capture=%llu observed=%llu producer=%llu expiry=%llu\n",
			  static_cast<unsigned long long>(captured_rejection.source), static_cast<unsigned long long>(captured_rejection.session),
			  static_cast<unsigned long long>(captured_rejection.sequence), static_cast<unsigned long long>(captured_rejection.configuration),
			  static_cast<unsigned long long>(captured_rejection.captured_us), static_cast<unsigned long long>(captured_rejection.observed_us),
			  static_cast<unsigned long long>(captured_rejection.producer_created), static_cast<unsigned long long>(captured_rejection.producer_expiry)) > 0
	      && std::fflush(stdout) == 0, "emit original capture metadata without relying on post-revocation wire status");
	vehicle_armed = armed.armed = false; // Explicit operator disarm after the capture-only rejection.
	SlaapsNativeTest::admitted.store(false);
	{
		// Disarm ends recovery: the recovery frame stream stops and is never renewed.
		const uint64_t settle = hrt_absolute_time() + 2 * recovery_lease;

		while (hrt_absolute_time() < settle) { pump(); px4_usleep(1000); }

		slaaps_manual_command_s stopped{}, later{};
		check(command_sub.copy(&stopped), "recovery stream is observable at disarm");
		const uint64_t quiet = hrt_absolute_time() + 2 * recovery_lease;

		while (hrt_absolute_time() < quiet) { pump(); px4_usleep(1000); }

		check(command_sub.copy(&later) && later.sequence == stopped.sequence && hrt_absolute_time() >= stopped.deadline,
		      "disarm ends recovery: no further frame and the last one has expired");
	}
	flight_keepalive = flight_stream_requests = false;

	// D3 losses detected outside the core: the SLAAPS mode is left or the power source stops while still armed after
	// a handled frame. Each enters recovery: canards frozen, EDF ramps off, disarm ends it. Estimator loss (case 2) is
	// routed in source but still ends in a transport cancel in this fixture; it is excluded until that is resolved.
	for (unsigned loss = 0; loss < 2; ++loss) {
		static const char *const kLossLog[] {"SLAAPS: mode left, canards hold, EDF ramping off",
						     "SLAAPS: power lost, canards hold, EDF ramping off", nullptr};
		static const char *const kLossRecovery[] {"leaving the SLAAPS mode while armed enters recovery",
							  "losing the power source while armed enters recovery",
							  "losing the estimator while armed enters recovery"};
		flight_keepalive = flight_renew_clock = flight_auto_ack = flight_stream_requests = false;
		vehicle_armed = armed.armed = false;
		vehicle.nav_state = vehicle.nav_state_user_intention = vehicle_status_s::NAVIGATION_STATE_MANUAL;
		throttle = -1.F;
		wait([&]() {
			return status.manual_entry_status == slaaps_status_s::MANUAL_ENTRY_INACTIVE && status.configuration_valid
			       && flight_protocol.required_state_valid && flight_protocol.feedback_valid && !flight_protocol.session_established;
		}, 1000000, "each loss case starts from a fresh deliberate disarmed reentry");
		flight_companion_session = 7304 + loss;
		flight_establish_session();
		SlaapsNativeTest::admitted.store(true);
		vehicle.nav_state = vehicle.nav_state_user_intention = status.mode_id[kManualMode];
		wait([&]() {
			return status.manual_entry_status == slaaps_status_s::MANUAL_ENTRY_PREPARED && flight_protocol.session_established
			       && flight_protocol.commander_transition == flight_witness.transition;
		}, 500000, "loss case prepares a new deliberate Manual session");
		const auto loss_session = status.manual_preparation_session;
		vehicle_armed = armed.armed = true;
		wait([&]() { return status.manual_worker_active && status.manual_entry_status == slaaps_status_s::MANUAL_ENTRY_RUNNING; },
		     500000, "loss case Manual worker runs on both real callback adapters");
		flight_renew_clock = false;
		wait([&]() {
			return !flight_clock_pending && status.companion_event_sequence >= flight_last_clock_event
			       && flight_protocol.timestamp >= flight_last_clock
			       && hrt_absolute_time() > flight_protocol.barrier_time + 2 * flight_protocol.map_uncertainty_us;
		}, 100000, "loss case drains clock renewal before its post-arm acknowledgement");
		flight_session(true);
		wait([&]() { return flight_protocol.acknowledgement_current; }, 100000, "loss case accepts its own post-arm acknowledgement");
		flight_renew_clock = flight_auto_ack = true;
		throttle = -.5F;
		slaaps_manual_command_s handled_frame{};
		wait([&]() {
			return command_sub.copy(&handled_frame) && handled_frame.session == loss_session
			       && handled_frame.mode == slaaps_manual_command_s::MANUAL && std::fabs(handled_frame.edf - .25F) < .001F
			       && flight_handled(handled_frame);
		}, 200000, "loss case has a handled nonzero-EDF Manual frame before the loss");
		diagnostic_log_start = hrt_absolute_time();
		bool logged = false;

		if (loss == 0) { vehicle.nav_state = vehicle.nav_state_user_intention = vehicle_status_s::NAVIGATION_STATE_MANUAL; }

		if (loss == 1) { publish_power = false; }

		if (loss == 2) { publish_estimates = false; }

		slaaps_manual_command_s first_recovery{};
		wait([&]() {
			mavlink_log_s log{};

			for (unsigned i = 0; i < mavlink_log_s::ORB_QUEUE_LENGTH && diagnostic_logs.update(&log); ++i) {
				logged |= kLossLog[loss] && log.timestamp >= diagnostic_log_start && std::strcmp(log.text, kLossLog[loss]) == 0;
			}

			return command_sub.copy(&first_recovery) && first_recovery.session == loss_session
			       && first_recovery.edf_disposition == slaaps_manual_command_s::EDF_RECOVERY && !status.manual_worker_active;
		}, 1000000, kLossRecovery[loss]);
		check(first_recovery.sequence > handled_frame.sequence && first_recovery.edf <= handled_frame.edf
		      && first_recovery.deadline > first_recovery.timestamp_sample
		      && first_recovery.deadline - first_recovery.timestamp_sample <= static_cast<uint64_t>(flight_profile.output_lease_ms) * 1000,
		      "recovery frame follows the handled frame, never raises EDF and carries at most one lease");
		slaaps_manual_command_s off{};
		wait([&]() {
			return command_sub.copy(&off) && off.session == loss_session && off.edf_disposition == slaaps_manual_command_s::EDF_RECOVERY
			       && off.edf <= 0.F && off.sequence > first_recovery.sequence;
		}, 500000, "recovery ramps the EDF command to off");
		check(std::memcmp(off.canards, first_recovery.canards, sizeof(off.canards)) == 0,
		      "canards stay frozen at the last handled position throughout recovery");

		if (kLossLog[loss]) {
			wait([&]() {
				mavlink_log_s log{};

				for (unsigned i = 0; i < mavlink_log_s::ORB_QUEUE_LENGTH && diagnostic_logs.update(&log); ++i) {
					logged |= log.timestamp >= diagnostic_log_start && std::strcmp(log.text, kLossLog[loss]) == 0;
				}

				return logged;
			}, 500000, "the actual mavlink_log names the loss that started recovery");
		}

		publish_power = publish_estimates = true;
		vehicle_armed = armed.armed = false;
		SlaapsNativeTest::admitted.store(false);
		slaaps_manual_command_s stopped{}, later{};
		wait([&]() {
			return !status.manual_worker_active && command_sub.copy(&stopped) && hrt_absolute_time() >= stopped.deadline;
		}, 1000000, "disarm ends recovery and the last recovery frame expires");
		const uint64_t quiet = hrt_absolute_time() + 50000;

		while (hrt_absolute_time() < quiet) { pump(); px4_usleep(1000); }

		check(command_sub.copy(&later) && later.sequence == stopped.sequence, "no recovery frame follows disarm");
	}

	flight_keepalive = flight_renew_clock = flight_auto_ack = flight_stream_requests = false;
	subject = ModuleBase::get_instance<SlaapsNativeTestSubject>(SlaapsNativeTestSubject::desc);
	check(subject != nullptr, "flight-transfer supervisor retains its actual module descriptor");
	subject->request_stop();
	wait([&]() { return SlaapsNativeTestSubject::desc.object.load() == nullptr; }, 1000000,
	"flight-transfer source-linked supervisor stops through its normal module lifecycle");
	publish_flight = publish_conditions = false;
	flight_packet_pub.unadvertise();
	flight_acceleration_pub.unadvertise();
	flight_bias_pub.unadvertise();
	flight_raw_pub.unadvertise();
	flight_witness_pub.unadvertise();
	PX4_INFO("SLAAPS_FLIGHT_HANDOFF_TEST_PASS actual_Run=1 both_banks=1 synthetic_authority=1 post_parser=%u flight_qualified=0",
		 signed_flight ? 0U : 1U);

	if (signed_flight) {
		check(flight_verified_clocks >= 2 && flight_verified_requests >= 2 && flight_injection_unchanged()
		      && flight_signed_packets.revision() == flight_receiver_revision,
		      "both signed sessions retain actual receiver evidence without advertising the injected active source");
		check(std::printf("SLAAPS_SIGNED_FLIGHT_TEST_PASS actual_parser=1 packet_injection=0 both_banks=1 receiver=%u signing=%u clocks=%u requests=%u\n",
				  flight_receiver_revision, flight_signing_generation, flight_verified_clocks, flight_verified_requests) > 0
		      && std::fflush(stdout) == 0, "emit the complete signed receiver proof without PX4 log truncation");
	}

	// This subsequent real module instance exercises the native QGC parameter path.
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
	bench.power.maximum_age_us = 200000; // Covers the declared 60ms activation plus 80ms first-command reserve.
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
		native_bench_cancel_scenario.store(scenario, std::memory_order_release);

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
		diagnostic_scenario = scenario;
		diagnostic_phase = 1;
		diagnostic_session = session;
		diagnostic_phase_started = hrt_absolute_time();
		diagnostic_baseline_writes = before_arming_writes;
		diagnostic_maximum_gap = 0;
		diagnostic_latched = false;

		for (unsigned i = 0; i < 3; ++i) { edf_diagnostics[i] = {}; }

		capture_edf_diagnostic(diagnostic_phase_started, diagnostic_previous_pump);
		slaaps_manual_permission_s edf_permission{};
		check(permission_sub.copy(&edf_permission) && edf_permission.session == session && !edf_permission.armed
		      && !edf_permission.healthy && !edf_permission.commissioning_admitted && !edf_permission.flight_admitted
		      && !status.bench_worker_active && status.bench_commissioning_ready_deadline > status.timestamp,
		      "preparation and arming readiness carry denied output permission with original input and authority expiry");
		check(status.power_ready && status.power_source_bound
		      && status.bench_commissioning_ready_deadline <= status.power_deadline,
		      "EDF arming readiness is capped by the pinned battery's original report expiry");

		if (!scenario) {
			manual_control_setpoint_s held_raw{}, observed_raw{};
			check(diagnostic_raw.copy(&held_raw), "capture the exact raw pilot sample before pausing only its publisher");
			uint64_t reserve{};
			check(activationReserveUs(bench, reserve), "synthetic EDF profile supplies its explicit full activation reserve");
			const uint64_t input_age_us = static_cast<uint64_t>(bench.manual_age_ms) * 1000;
			const uint64_t raw_lease_us = std::min(input_age_us, static_cast<uint64_t>(bench.output_lease_ms) * 1000);
			check(raw_lease_us > reserve && input_age_us > raw_lease_us,
			      "held-raw regression distinguishes the shorter EDF lease from configured pilot age");
			const uint64_t raw_expiry = held_raw.timestamp_sample + raw_lease_us;
			const uint64_t readiness_expiry = raw_expiry - reserve;
			publish_manual = false;
			wait([&]() {
				return status.timestamp >= readiness_expiry && !status.can_arm_mask && !status.bench_commissioning_ready;
			}, raw_lease_us, "held pilot sample loses arming readiness at its original EDF lease minus activation reserve");
			vehicle_status_s live_vehicle{};
			actuator_armed_s live_armed{};
			battery_status_s live_power{};
			check(diagnostic_raw.copy(&observed_raw) && observed_raw.timestamp == held_raw.timestamp
			      && observed_raw.timestamp_sample == held_raw.timestamp_sample && status.timestamp < raw_expiry
			      && hrt_absolute_time() - held_raw.timestamp_sample <= input_age_us
			      && status.bench_entry_status == slaaps_status_s::BENCH_ENTRY_WAIT_ARM
			      && status.manual_preparation_session == session && status.bench_session_deadline == duration_deadline
			      && status.can_run_mask == (1U << status.mode_id[kBenchMode]) && !status.bench_worker_active
			      && status.power_ready && main.writes() + aux.writes() == before_arming_writes,
			      "reserve readiness expires before both raw expiry and pilot-age limit without canceling or activating the prepared session");
			check(diagnostic_vehicle.copy(&live_vehicle) && diagnostic_armed.copy(&live_armed) && diagnostic_power.copy(&live_power)
			      && live_vehicle.timestamp > held_raw.timestamp && live_armed.timestamp > held_raw.timestamp
			      && live_power.timestamp > held_raw.timestamp && !live_armed.armed
			      && live_vehicle.arming_state == vehicle_status_s::ARMING_STATE_DISARMED,
			      "only raw pilot publication is held while disarmed authority and selected power reports remain fresh");
			publish_manual = true;
			wait([&]() {
				return status.bench_entry_status == slaaps_status_s::BENCH_ENTRY_WAIT_ARM && status.bench_commissioning_ready
				       && status.can_arm_mask == (1U << status.mode_id[kBenchMode]);
			}, raw_lease_us, "a genuinely fresh raw sample restores disarmed reserve readiness without another enable or activation");
			check(status.manual_preparation_session == session && status.bench_session_deadline == duration_deadline
			      && !status.bench_worker_active && main.writes() + aux.writes() == before_arming_writes,
			      "fresh raw readiness preserves original session duration and performs no output callback");
			const auto human_wait = hrt_absolute_time() + 1100000;

			while (hrt_absolute_time() < human_wait) { pump(); px4_usleep(1000); }

			check(status.bench_entry_status == slaaps_status_s::BENCH_ENTRY_WAIT_ARM && status.bench_commissioning_ready
			      && status.bench_session_deadline == duration_deadline && main.writes() + aux.writes() == before_arming_writes,
			      "operator arming may take more than one second without renewing original duration or producing any callback");
		}

		diagnostic_phase = 2;
		diagnostic_phase_started = hrt_absolute_time();
		vehicle_armed = scenario != 1;
		armed.armed = scenario == 1;
		const auto staggered = hrt_absolute_time() + 60000;

		while (hrt_absolute_time() < staggered) { pump(); px4_usleep(1000); }

		const bool staggered_waited = status.bench_entry_status == slaaps_status_s::BENCH_ENTRY_WAIT_ARM && !status.bench_worker_active
					      && !status.can_arm_mask && main.writes() + aux.writes() == before_arming_writes;

		if (!staggered_waited) { print_edf_diagnostic("staggered arming"); }

		check(staggered_waited,
		      "either staggered fresh arming order waits non-actuating until both actual publications agree");
		diagnostic_phase = 3;
		diagnostic_phase_started = hrt_absolute_time();
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
		diagnostic_phase = 4;
		diagnostic_phase_started = hrt_absolute_time();
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

		diagnostic_phase = 0; // The following explicit fault/stop is intentional.
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
	native_manual_disarm = nullptr;
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

	for (unsigned session_index = 0; session_index < 11; ++session_index) {
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

		if (session_index >= 3 && session_index < 10) {
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

		if (session_index == 1 || session_index == 10) {
			const unsigned main_before = main.writes(), aux_before = aux.writes();
			const unsigned main_stop_handled = main.handledWrites(), aux_stop_handled = aux.handledWrites();
			const float previous_edf = aux.lastHandledOutput(3);
			slaaps_manual_ack_s before_main{}, before_aux{}, after_main{}, after_aux{};
			check(main_ack.copy(&before_main) && aux_ack.copy(&before_aux) && previous_edf > 1000.F,
			      "ordinary disarm fixture has an actually successful positive EDF callback and exact prior ACKs");
			const uint64_t disarm_time = hrt_absolute_time();

			if (session_index == 1) {
				armed.armed = false;
				armed.timestamp = disarm_time;
				check(armed_pub.publish(armed), "actuator disarm arrives while fresh VehicleStatus still reports armed");

			} else {
				vehicle.arming_state = vehicle_status_s::ARMING_STATE_DISARMED;
				vehicle.timestamp = disarm_time;
				check(vehicle_pub.publish(vehicle), "VehicleStatus disarm arrives while fresh actuator state still reports armed");
				aux.failNextWrite();
			}

			main.pump(); aux.pump();
			check(main.writes() == main_before + 1 && aux.writes() == aux_before + 1
			      && main.handledWrites() == main_stop_handled + 1
			      && aux.handledWrites() == aux_stop_handled + (session_index == 1 ? 1U : 0U)
			      && main.mixing().slaapsOutputStatus() == SlaapsOutputStatus::Inhibited
			      && aux.mixing().slaapsOutputStatus() == (session_index == 1 ? SlaapsOutputStatus::Inhibited : SlaapsOutputStatus::HandlingRejected),
			      "either fresh disarm publication immediately attempts the retained stop and completion requires callback success");
			check(near(main.lastOutput(6), 0.F) && near(main.lastOutput(4), 0.F)
			      && near(aux.lastOutput(7), 0.F) && near(aux.lastOutput(5), 0.F) && near(aux.lastOutput(3), 0.F)
			      && near(main.lastHandledOutput(6), 0.F) && near(main.lastHandledOutput(4), 0.F),
			      "actual MAIN and AUX callback arguments actively command the accepted disarmed values on every owned pin");

			if (session_index == 10) {
				check(near(aux.lastHandledOutput(3), previous_edf), "failed stop callback cannot claim the previous EDF output was replaced");
				px4_usleep(static_cast<unsigned>(c.output_lease_ms) * 1000 + 1000);
				vehicle.timestamp = armed.timestamp = supervisor.timestamp = 0;
				check(vehicle_pub.publish(vehicle) && armed_pub.publish(armed) && supervisor_pub.publish(supervisor),
				      "original inputs and commands expire and later authority is unavailable after explicit disarm");
				main.pump(); aux.pump();
				check(main.writes() == main_before + 1 && aux.writes() == aux_before + 2
				      && aux.handledWrites() == aux_stop_handled + 1 && near(aux.lastHandledOutput(3), 0.F)
				      && aux.mixing().slaapsOutputStatus() == SlaapsOutputStatus::Inhibited,
				      "only the failed immutable off ticket retries after input expiry without waiting for fresh positive authority");
			}

			check(near(aux.lastHandledOutput(3), 0.F) && main_ack.copy(&after_main) && aux_ack.copy(&after_aux)
			      && memcmp(&before_main, &after_main, sizeof(before_main)) == 0
			      && memcmp(&before_aux, &after_aux, sizeof(before_aux)) == 0,
			      "successful ordinary disarm actively replaces EDF PWM and never emits a flight-command ACK");
			const unsigned stopped_main = main.writes(), stopped_aux = aux.writes();
			publishState(true);
			main.pump(); aux.pump();
			check(main.writes() == stopped_main && aux.writes() == stopped_aux && near(aux.lastHandledOutput(3), 0.F),
			      "restored armed state and retained positive permission cannot revive the disarmed output session");
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
			// The observed explicit disarm minted an immutable off-only ticket.
			// Later invalid authority cannot strand the failed stop attempt.
			vehicle.timestamp = armed.timestamp = hrt_absolute_time() + 2000000;
			check(vehicle_pub.publish(vehicle) && armed_pub.publish(armed), "publish invalid future stop authority");
			main.pump();
			check(main.writes() == main_writes + 2 && near(main.lastHandledOutput(6), 0.F)
			      && near(main.lastHandledOutput(4), 0.F), "retained explicit-disarm ticket retries despite later invalid authority time");
			publishState(false);
			main.pump();
			check(main.writes() == main_writes + 2 && near(main.lastHandledOutput(6), 0.F)
			      && near(main.lastHandledOutput(4), 0.F), "fresh authority cannot repeat a successfully handled disarm ticket");
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

	if (argc == 3 && strcmp(argv[1], "supervisor") == 0 && strcmp(argv[2], "signed") == 0) { return nativeSupervisorOrchestrationTests(true); }

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
