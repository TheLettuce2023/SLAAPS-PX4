#include "StabilizedManualControl.hpp"

#include <cmath>
#include <cstdio>
#include <limits>

using namespace research_stabilized_manual;

namespace
{
unsigned checks{}, failures{};
constexpr TimeUs kTickUs{4000};
constexpr float kDt{0.004F};
constexpr float kPi{3.14159265358979323846F};

void check(bool condition, const char *description)
{
	++checks;

	if (!condition) { ++failures; std::printf("FAIL: %s\n", description); }
}

bool near(float left, float right, float tolerance = 1.0e-5F) { return std::fabs(left - right) <= tolerance; }

Configuration fixtureConfiguration()
{
	// Complete synthetic host fixture. None of these values is a vehicle default.
	Configuration result{};
	result.mavlink_instance = 0;
	result.source_system_id = 255;
	result.source_component_id = 190;
	result.maximum_manual_age_us = 100000;
	result.maximum_state_age_us = 20000;
	result.maximum_state_skew_us = 8000;
	result.minimum_cycle_us = kTickUs;
	result.maximum_cycle_us = 12000;
	result.quaternion_norm_tolerance = 0.01F;
	result.centered_enter = 0.04F;
	result.centered_exit = 0.06F;
	result.stick_sign = Vector3f(1.0F, -1.0F, 1.0F);
	result.maximum_rate_rad_s = Vector3f(1.0F, 1.0F, 1.0F);
	result.maximum_rate_setpoint_accel_rad_s2 = Vector3f(20.0F, 20.0F, 20.0F);
	result.maximum_measured_rate_rad_s = Vector3f(20.0F, 20.0F, 20.0F);
	result.maximum_measured_accel_rad_s2 = Vector3f(100.0F, 100.0F, 100.0F);
	result.attitude_gain = Vector3f(4.0F, 4.0F, 4.0F);
	result.rate_p = Vector3f(0.4F, 0.8F, 0.8F);
	result.rate_i = Vector3f(0.05F, 0.1F, 0.1F);
	result.rate_d = Vector3f(0.003F, 0.006F, 0.006F);
	result.integrator_limit = Vector3f(0.2F, 0.2F, 0.2F);
	result.maximum_normalized_moment = Vector3f(1.0F, 1.0F, 1.0F);
	result.fixed_simulated_thrust_n = 10.0F;
	const float signs[3][4] = {{1.0F, -1.0F, 1.0F, -1.0F}, {1.0F, 1.0F, -1.0F, -1.0F}, {1.0F, -1.0F, -1.0F, 1.0F}};

	for (unsigned axis = 0; axis < 3; ++axis) {
		for (unsigned channel = 0; channel < 4; ++channel) { result.allocation.effectiveness[axis][channel] = 0.25F * signs[axis][channel]; }
	}

	for (unsigned channel = 0; channel < 4; ++channel) {
		result.allocation.minimum[channel] = -1.0F;
		result.allocation.maximum[channel] = 1.0F;
		result.allocation.positive_slew_per_s[channel] = 5.0F;
		result.allocation.negative_slew_per_s[channel] = 5.0F;
	}

	result.allocation.axis_weight = Vector3f(1.0F, 1.0F, 1.0F);
	result.allocation.regularization = 1.0e-5F;
	result.allocation.minimum_row_norm = 0.01F;
	result.allocation.minimum_independence = 0.01F;
	result.allocation.saturation_tolerance = 0.001F;
	return result;
}

struct Fixture {
	Configuration configuration{fixtureConfiguration()};
	StabilizedManualControl control{};
	ManualSample manual{};
	StateSample sample{};
	Report report{};
	TimeUs now{1000000};

	Fixture()
	{
		manual.source = research_actuator_command::ManualControlSource::Mavlink;
		manual.mavlink_instance = configuration.mavlink_instance;
		manual.source_system_id = configuration.source_system_id;
		manual.source_component_id = configuration.source_component_id;
		manual.valid = true;
		sample.valid = true;
		sample.source_epoch = 1;
		stamp();
	}

	void stamp()
	{
		manual.timestamp_us = manual.timestamp_sample_us = now;
		sample.attitude_timestamp_us = sample.attitude_sample_us = now;
		sample.rate_timestamp_us = sample.rate_sample_us = now;
	}
	void enable()
	{
		check(control.configure(configuration) == Status::Ok, "complete fixture configures");
		check(control.enable(now, manual, sample) == Status::Ok, "centered fixture enables");
	}
	Status tick()
	{
		now += kTickUs;
		stamp();
		return control.cycle(now, manual, sample, report);
	}
};

void handledEntryTests()
{
	Fixture f;
	CanardVector handled{{.4F, -.4F, .3F, -.3F}};
	const Vector3f rate(.5F, -.5F, .25F);
	check(f.control.configure(f.configuration) == Status::Ok, "handled entry configures");
	check(f.control.enableFromHandled(f.now, f.manual, f.sample, f.now - kTickUs, handled, rate) == Status::Ok,
	      "centered manual transfer accepts bounded handled history");
	check(f.control.cycle(f.now, f.manual, f.sample, f.report) == Status::Ok, "first transfer calculation uses elapsed handled time");

	for (unsigned i = 0; i < handled.size(); ++i) {
		check(std::fabs(f.report.allocation.command[i] - handled[i]) <= 5.F * kDt + 1.e-5F,
		      "first manual output slews from actual handled command rather than trim");
	}

	for (unsigned axis = 0; axis < 3; ++axis) {
		check(std::fabs(f.report.rate_setpoint_rad_s(axis) - rate(axis)) <= 20.F * kDt + 1.e-5F,
		      "manual rate target slews from outgoing handled target");
	}

	for (unsigned scenario = 0; scenario < 5; ++scenario) {
		Fixture rejected;
		auto canards = handled;
		TimeUs time = rejected.now - kTickUs;
		auto target = rate;

		if (scenario == 0) { time = 0; }

		if (scenario == 1) { time = rejected.now + 1; }

		if (scenario == 2) { time = rejected.now - rejected.configuration.maximum_cycle_us - 1; }

		if (scenario == 3) { canards[0] = 1.1F; }

		if (scenario == 4) { target(0) = std::numeric_limits<float>::quiet_NaN(); }

		check(rejected.control.configure(rejected.configuration) == Status::Ok, "rejected handled entry configures");
		check(rejected.control.enableFromHandled(rejected.now, rejected.manual, rejected.sample, time, canards, target) != Status::Ok
		      && !rejected.control.eligible(), "invalid handled history cannot enable manual control");
	}

	Fixture immediate;
	check(immediate.control.configure(immediate.configuration) == Status::Ok
	      && immediate.control.enableFromHandled(immediate.now, immediate.manual, immediate.sample,
			      immediate.now, handled, rate) == Status::Ok,
	      "an acknowledgment at the current instant is valid handled history");
	check(immediate.control.cycle(immediate.now, immediate.manual, immediate.sample, immediate.report) == Status::NotDue
	      && immediate.control.eligible(), "same-instant handled entry waits without calculation or fault");
	check(immediate.tick() == Status::Ok, "handled entry calculates after the minimum cycle interval");
}

void allocationTests()
{
	auto configuration = fixtureConfiguration().allocation;
	AllocationReport report{};
	CanardVector previous{};
	check(validAllocationConfiguration(configuration), "full-rank fixture matrix admitted");
	check(allocateCanards(configuration, Vector3f(0.2F, 0.0F, 0.0F), previous, 1.0F, report), "unconstrained request allocates");
	check(near(report.command[0], 0.2F / (1.0F + 4.0F * configuration.regularization)), "analytic interior optimum");
	check(near(report.command[1], -report.command[0]), "roll differential signs");
	check(allocateCanards(configuration, Vector3f(2.0F, 0.0F, 0.0F), previous, 1.0F, report), "infeasible request bounded");
	check(near(report.achieved(0), 1.0F) && report.saturation_positive(0), "magnitude saturation reports actual unavailable roll");
	const float sentinel = report.command[0];
	check(!allocateCanards(configuration, Vector3f(std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F), previous, 1.0F, report),
	      "nonfinite allocation rejects");
	check(report.command[0] == sentinel, "allocation failure preserves caller report");
	auto invalid = configuration;
	invalid.effectiveness[2] = invalid.effectiveness[1];
	check(!validAllocationConfiguration(invalid), "dependent moment axes rejected");
	invalid = configuration;
	invalid.positive_slew_per_s[0] = 0.0F;
	check(!validAllocationConfiguration(invalid), "missing slew rejects");
	invalid = configuration;
	invalid.regularization = 1.0e-20F;
	check(!validAllocationConfiguration(invalid), "numerically unresolved regularizer rejects instead of choosing bound-only null motion");

	// KKT stationarity/complementarity independently characterize the convex
	// optimum, including moving slew bounds and weighted coupled demands.
	uint32_t random = 7;
	auto uniform = [&random]() { random = 1664525U * random + 1013904223U; return static_cast<float>(random >> 8) / 16777216.0F; };
	configuration.axis_weight = Vector3f(3.0F, 2.0F, 1.0F);

	for (unsigned trial = 0; trial < 250; ++trial) {
		for (float &value : previous) { value = 1.6F * uniform() - 0.8F; }

		const Vector3f requested(4.0F * uniform() - 2.0F, 4.0F * uniform() - 2.0F, 4.0F * uniform() - 2.0F);
		const float dt = 0.01F + 0.15F * uniform();
		check(allocateCanards(configuration, requested, previous, dt, report), "random allocation succeeds");

		for (unsigned channel = 0; channel < 4; ++channel) {
			const float lower = std::fmax(configuration.minimum[channel], previous[channel] - dt * configuration.negative_slew_per_s[channel]);
			const float upper = std::fmin(configuration.maximum[channel], previous[channel] + dt * configuration.positive_slew_per_s[channel]);
			check(report.command[channel] >= lower - 2.0e-7F && report.command[channel] <= upper + 2.0e-7F, "magnitude and slew bound respected");
			double gradient = configuration.regularization * (report.command[channel] - configuration.trim[channel]);

			for (unsigned axis = 0; axis < 3; ++axis) {
				gradient -= static_cast<double>(configuration.effectiveness[axis][channel]) * configuration.axis_weight(axis) * report.residual(axis);
			}

			const bool at_lower = near(report.command[channel], lower, 2.0e-6F);
			const bool at_upper = near(report.command[channel], upper, 2.0e-6F);
			check((at_lower && gradient >= -1.0e-5) || (at_upper && gradient <= 1.0e-5)
			      || (!at_lower && !at_upper && std::fabs(gradient) <= 1.0e-5), "independent KKT optimum check");
		}
	}
}

void referenceTests()
{
	Fixture fixture;
	fixture.manual.axes[3] = std::numeric_limits<float>::quiet_NaN();
	fixture.sample.attitude = Quaternion(matrix::AxisAnglef(0.0F, -kPi * 0.5F, 0.0F));
	fixture.enable();
	check(!StabilizedManualControl::outputsEnabled(), "core cannot publish outputs");
	check(fixture.tick() == Status::Ok && fixture.report.state == State::Hold, "nose-up centered hold admitted");
	const Quaternion initial = fixture.report.hold_reference;
	fixture.manual.axes[0] = 0.05F;
	check(fixture.tick() == Status::Ok && fixture.report.state == State::Hold, "centering hysteresis retains hold");
	fixture.manual.axes[0] = 0.5F;
	check(fixture.tick() == Status::Ok && fixture.report.state == State::Rate
	      && fixture.report.rate_setpoint_rad_s(0) > 0.0F, "roll stick commands body roll rate");
	check(fixture.report.rate_setpoint_rad_s(0) <= 20.0F * kDt + 1.0e-6F, "rate-reference acceleration bounded");
	fixture.manual.axes[0] = 0.0F;
	fixture.manual.axes[1] = 0.5F;
	check(fixture.tick() == Status::Ok && fixture.report.rate_setpoint_rad_s(1) < 0.0F, "forward pitch stick commands negative FRD pitch");
	fixture.manual.axes[1] = 0.0F;
	fixture.sample.attitude = initial * Quaternion(matrix::AxisAnglef(0.0F, 0.0F, 0.2F));
	check(fixture.tick() == Status::Ok && fixture.report.state == State::Hold, "stick release captures measured full attitude");
	const Quaternion captured = fixture.report.hold_reference;
	fixture.sample.attitude = captured * Quaternion(matrix::AxisAnglef(0.0F, 0.0F, 0.1F));
	check(fixture.tick() == Status::Ok && fixture.report.rate_setpoint_rad_s(2) < 0.0F, "hold rejects body-yaw disturbance at nose up");
	check(near(fixture.report.hold_reference(0), captured(0)), "disturbance does not recapture reference");

	for (unsigned axis = 0; axis < 3; ++axis) {
		Fixture positive, negative;
		positive.enable(); negative.enable();
		Vector3f rotation{};
		rotation(axis) = kPi - 0.0001F;
		positive.sample.attitude = Quaternion(matrix::AxisAnglef(rotation));
		negative.sample.attitude = positive.sample.attitude * -1.0F;
		check(positive.tick() == Status::Ok && negative.tick() == Status::Ok, "near-pi quaternion error remains finite");
		check(near(positive.report.rate_setpoint_rad_s(axis), negative.report.rate_setpoint_rad_s(axis)), "q and negative-q equivalent command");
		check(positive.report.rate_setpoint_rad_s(axis) < 0.0F, "all body axes have restoring feedback");
	}
}

void faultTests()
{
	for (unsigned fault = 0; fault < 12; ++fault) {
		Fixture fixture;
		fixture.enable();
		fixture.now += kTickUs;
		fixture.stamp();
		fixture.report.timestamp_us = 55;

		switch (fault) {
		case 0: fixture.manual.valid = false; break;

		case 1: fixture.manual.source_system_id = 17; break;

		case 2: fixture.manual.timestamp_us = fixture.manual.timestamp_sample_us = fixture.now + 1; break;

		case 3: fixture.manual.timestamp_sample_us = fixture.now - 100001; break;

		case 4: fixture.manual.axes[0] = std::numeric_limits<float>::quiet_NaN(); break;

		case 5: fixture.sample.valid = false; break;

		case 6: fixture.sample.attitude = Quaternion(0.0F, 0.0F, 0.0F, 0.0F); break;

		case 7: fixture.sample.attitude_sample_us = fixture.now - 20001; break;

		case 8: fixture.sample.rate_sample_us = fixture.now - 8001; break;

		case 9: fixture.sample.attitude_reset_counter = 1; break;

		case 10: fixture.sample.source_epoch = 2; break;

		case 11: fixture.now += 12001; break;
		}

		check(fixture.control.cycle(fixture.now, fixture.manual, fixture.sample, fixture.report) != Status::Ok,
		      "invalid input/state/timing rejects");
		check(fixture.control.state() == State::Fault && !fixture.control.eligible(), "fault revokes eligibility");
		check(fixture.report.timestamp_us == 55, "failed cycle preserves caller report");
		check(fixture.control.enable(fixture.now, fixture.manual, fixture.sample) == Status::InvalidState, "fault cannot auto-enable");
		fixture.control.disable();
		check(fixture.control.state() == State::Disabled, "explicit disable clears fault eligibility");
	}

	Fixture mutation;
	mutation.enable();
	mutation.now += kTickUs;
	mutation.manual.timestamp_us = mutation.now;
	mutation.sample.rate_rad_s(0) = 0.1F;
	check(mutation.control.cycle(mutation.now, mutation.manual, mutation.sample, mutation.report) == Status::StateRejected,
	      "same-sample changed state rejects");
	Fixture noncentered;
	noncentered.manual.axes[0] = 0.1F;
	check(noncentered.control.configure(noncentered.configuration) == Status::Ok, "noncentered setup configures");
	check(noncentered.control.enable(noncentered.now, noncentered.manual, noncentered.sample) == Status::NotCentered,
	      "enable requires raw centered sticks");
	StabilizedManualControl unconfigured;
	check(unconfigured.configure(Configuration{}) == Status::InvalidConfiguration, "empty profile rejects");
	Fixture saturation;

	for (auto &row : saturation.configuration.allocation.effectiveness) { for (float &value : row) { value *= 0.1F; } }

	saturation.enable();
	saturation.manual.axes[0] = 1.0F;

	for (unsigned i = 0; i < 300; ++i) {
		check(saturation.tick() == Status::Ok, "saturated controller cycle succeeds");
		check(std::fabs(saturation.report.integral(0)) <= saturation.configuration.integrator_limit(0),
		      "integrator bounded under sustained saturation");
	}

	check(saturation.report.allocation.saturation_positive(0), "unavailable torque feeds directional saturation");
	check(std::fabs(saturation.report.integral(0)) < 0.01F, "saturation feedback prevents persistent positive windup");
	check(saturation.report.fixed_simulated_thrust_n == 10.0F, "throttle does not change fixed simulated thrust");

	for (unsigned fault = 0; fault < 5; ++fault) {
		Fixture fixture;
		fixture.enable();
		fixture.now += kTickUs;
		fixture.stamp();

		switch (fault) {
		case 0:
			fixture.manual.timestamp_sample_us -= kTickUs;
			fixture.manual.axes[0] = 0.1F;
			break;

		case 1: fixture.manual.timestamp_sample_us -= kTickUs + 1; break;

		case 2: fixture.sample.attitude_timestamp_us = fixture.sample.attitude_sample_us = fixture.now + 1; break;

		case 3: fixture.sample.rate_timestamp_us = fixture.sample.rate_sample_us = fixture.now + 1; break;

		case 4: fixture.sample.angular_accel_rad_s2(0) = std::numeric_limits<float>::infinity(); break;
		}

		check(fixture.control.cycle(fixture.now, fixture.manual, fixture.sample, fixture.report) != Status::Ok
		      && !fixture.control.eligible(), "replay/conflict/future-state/nonfinite acceleration revokes");
	}

	Fixture recovery;
	recovery.enable();
	recovery.manual.valid = false;
	check(recovery.tick() == Status::InputRejected, "recovery scenario faults");
	recovery.control.disable();
	recovery.manual.valid = true;
	recovery.now += kTickUs;
	recovery.stamp();
	check(recovery.control.configure(recovery.configuration) == Status::Ok
	      && recovery.control.enable(recovery.now, recovery.manual, recovery.sample) == Status::Ok, "explicit reset sequence re-enables");
	check(recovery.tick() == Status::Ok && recovery.report.integral.norm() == 0.0F, "reset clears integral and captures neutral hold");

	Fixture cadence;
	cadence.enable();
	cadence.now += 1;
	cadence.stamp();
	cadence.report.timestamp_us = 55;
	check(cadence.control.cycle(cadence.now, cadence.manual, cadence.sample, cadence.report) == Status::NotDue
	      && cadence.control.eligible() && cadence.report.timestamp_us == 55, "early cycle preserves report and remains eligible");
}

float attitudeError(const Quaternion &actual, const Quaternion &reference)
{
	const Quaternion error = (actual.inversed() * reference).canonical();
	return 2.0F * std::atan2(error.imag().norm(), std::fabs(error(0)));
}

void feedbackTests()
{
	// Host truth-feedback numerical fixture, deliberately not estimated-state SITL.
	// Prescribed body-local airflow is 20 m/s; E is frozen, synthetic and full rank.
	// 10 N ideal CG-aligned EDF force is reacted by the rotational fixture; no
	// rotor torque or EDF wash is assumed. J, damping, torque scale and servo lag
	// below are synthetic. This never loads or corrects the supplied vehicle SDF.
	const Vector3f inertia(0.03F, 0.08F, 0.08F);
	const Vector3f damping(0.03F, 0.04F, 0.04F);
	constexpr float torque_scale_nm{0.4F};
	constexpr float servo_tau_s{0.05F};

	for (unsigned campaign = 0; campaign < 2; ++campaign) {
		for (unsigned orientation = 0; orientation < 2; ++orientation) {
			for (unsigned axis = 0; axis < 3; ++axis) {
				Fixture fixture;
				fixture.sample.attitude = orientation == 0 ? Quaternion{} : Quaternion(matrix::AxisAnglef(0.0F, -kPi * 0.5F, 0.0F));
				fixture.enable();
				Quaternion reference = fixture.sample.attitude;
				CanardVector actual_servo{};
				Vector3f angular_accel{};
				float peak_error = 0.0F;

				for (unsigned step = 0; step < 2000; ++step) {
					if (campaign == 1 && step == 125) { fixture.manual.axes[axis] = 0.4F * fixture.configuration.stick_sign(axis); }

					if (campaign == 1 && step == 375) { fixture.manual.axes[axis] = 0.0F; }

					if (step == (campaign == 0 ? 250U : 625U)) { fixture.sample.rate_rad_s(axis) += 0.8F; }

					fixture.sample.angular_accel_rad_s2 = angular_accel;
					check(fixture.tick() == Status::Ok, "truth-feedback fixture control cycle succeeds");

					if (campaign == 1 && step == 374) {
						check(fixture.report.state == State::Rate && std::fabs(fixture.sample.rate_rad_s(axis) - 0.375F) < 0.08F,
						      "pilot-commanded body rate tracks within declared synthetic tolerance");
					}

					if (campaign == 1 && step == 375) {
						reference = fixture.report.hold_reference;
						check(fixture.report.state == State::Hold, "pilot release enters attitude hold in feedback loop");
					}

					Vector3f moment{};

					for (unsigned channel = 0; channel < 4; ++channel) {
						actual_servo[channel] += (1.0F - std::exp(-kDt / servo_tau_s))
									 * (fixture.report.allocation.command[channel] - actual_servo[channel]);
						check(std::fabs(actual_servo[channel]) <= 1.0F, "actual synthetic servo remains bounded");

						for (unsigned row = 0; row < 3; ++row) { moment(row) += torque_scale_nm * fixture.configuration.allocation.effectiveness[row][channel] * actual_servo[channel]; }
					}

					const Vector3f gyroscopic = fixture.sample.rate_rad_s.cross(inertia.emult(fixture.sample.rate_rad_s));

					for (unsigned row = 0; row < 3; ++row) { angular_accel(row) = (moment(row) - damping(row) * fixture.sample.rate_rad_s(row) - gyroscopic(row)) / inertia(row); }

					fixture.sample.rate_rad_s += angular_accel * kDt;
					fixture.sample.attitude = fixture.sample.attitude * Quaternion(matrix::AxisAnglef(fixture.sample.rate_rad_s * kDt));
					fixture.sample.attitude.normalize();
					peak_error = std::fmax(peak_error, attitudeError(fixture.sample.attitude, reference));
				}

				const float final_error = attitudeError(fixture.sample.attitude, reference);
				const float final_rate = fixture.sample.rate_rad_s.norm();
				check(peak_error < 0.5F, "synthetic impulse peak stays below declared 0.5 rad limit");
				check(final_error < 0.02F, "synthetic fixture returns within 0.02 rad after impulse");
				check(final_rate < 0.02F, "synthetic fixture settles below 0.02 rad/s");
				std::printf("fixture campaign=%u orientation=%u axis=%u peak_rad=%.6f final_rad=%.6f final_rate=%.6f\n",
					    campaign, orientation, axis, static_cast<double>(peak_error), static_cast<double>(final_error), static_cast<double>(final_rate));
			}
		}
	}
}
}

int main()
{
	handledEntryTests(); allocationTests(); referenceTests(); faultTests(); feedbackTests();
	std::printf("checks=%u failures=%u scope=host_truth_feedback_only\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
