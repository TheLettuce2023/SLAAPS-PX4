#include <lib/slaaps/AccelerationControllerPort.hpp>
#include <lib/slaaps/EdfOwner.hpp>

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <limits>

namespace
{
using namespace slaaps;
using namespace research_stabilized_manual;
bool near(float a, float b, float tolerance = 0.001F) { return std::fabs(a - b) <= tolerance; }

// Explicit synthetic model only: three canard moment columns and one fixed
// synthetic EDF force column. No independent lateral force authority is invented.
VehicleConfiguration configuration()
{
	VehicleConfiguration c{};
	c.data_revision = 1;
	c.mass = 1.F;
	c.inertia[0] = c.inertia[1] = c.inertia[2] = 1.F;
	c.manual_source = 2; c.manual_instance = 0; c.manual_system = 255; c.manual_component = 190;
	c.manual_throttle_low = -1.F; c.manual_throttle_high = 1.F;
	c.manual_age_ms = c.state_age_ms = c.output_lease_ms = c.bench_gap_ms = 100;
	c.state_skew_ms = 10; c.manual_ready_wait_s = 5;
	c.selected_status_age_us = c.actuator_permission_age_us = 100000;
	c.activation_ack_timeout_us = 40000; c.first_command_budget_us = 30000;
	c.cycle_min_us = 10000; c.cycle_max_us = 200000;
	c.centered_enter = 0.05F; c.centered_exit = 0.1F; c.bench_deadzone = 0.05F;
	c.quaternion_tolerance = 0.01F; c.regularization = 0.00001F;
	c.minimum_row_norm = c.minimum_independence = 0.01F; c.saturation_tolerance = 0.001F;
	c.edf_voltage = 24.F; c.edf_rise = c.edf_fall = 100.F; c.edf_entry_max = 0.1F;

	for (unsigned i = 0; i < kThrustPoints; ++i) { c.edf_thrust[i] = 3.F * static_cast<float>(i); }

	c.power = {0, 1, 0, 100000, 20.F, 26.F};
	c.acceleration_control.enabled = 1;
	c.feedback.enabled = 1; c.feedback.maximum_age_us = c.feedback.maximum_bias_age_us = 100000;
	c.feedback.latency_bound_us = c.feedback.settling_us = 1000;
	c.feedback.maximum_specific_force_m_s2 = c.feedback.maximum_acceleration_m_s2 = 100.F;
	c.feedback.gravity_m_s2 = 9.F;
	auto &schedule = c.schedule;
	schedule.enabled = 1; schedule.airspeed_instance = schedule.air_data_instance = schedule.battery_instance = 0;
	schedule.battery_id = 1; schedule.maximum_age_us = 100000;
	schedule.indicated_airspeed_m_s[0] = 0.F; schedule.indicated_airspeed_m_s[1] = 20.F;
	schedule.edf_command[0] = 0.F; schedule.edf_command[1] = 1.F;
	schedule.density_min = 1.F; schedule.density_max = 2.F;
	schedule.voltage_min = 20.F; schedule.voltage_max = 26.F;
	schedule.filter_time_s = 0.05F;
	schedule.airspeed_filter_rate = schedule.airspeed_transient_rate = 1000.F;
	schedule.edf_filter_rate = schedule.edf_transient_rate = 100.F;
	c.wrench.enabled = 1;

	for (unsigned axis = 0; axis < kAxes; ++axis) {
		auto &a = c.axis[axis];
		a.moment_scale = 1.F; a.input_axis = static_cast<int32_t>(axis); a.sign = 1.F;
		a.rate_limit = a.measured_rate_limit = 10.F;
		a.setpoint_accel_limit = a.measured_accel_limit = 100.F;
		a.attitude_gain = 1.F; a.rate_p = 0.2F; a.rate_i = 0.1F;
		a.integrator_limit = 0.5F; a.moment_limit = a.allocation_weight = 1.F;
		c.acceleration_control.acceleration_feedback_p[axis] = 1.F;
		c.acceleration_control.correction_limit[axis] = 0.25F;
		c.wrench.force_residual_tolerance[axis] = c.wrench.moment_residual_tolerance[axis] = 0.05F;
	}

	for (unsigned i = 0; i < kCanards; ++i) {
		auto &servo = c.canard[i];
		servo.deflection_slope = 0.1F; servo.minimum = -1.F; servo.maximum = 1.F;
		servo.slope_uncertainty = 0.001F;
		servo.pwm_negative = 1000.F; servo.pwm_neutral = 1500.F; servo.pwm_positive = 2000.F;
		servo.maximum_deflection_deg = 13.F; servo.slew_positive = servo.slew_negative = 100.F;
		servo.bench_mix[i % kAxes] = 1.F;
	}

	for (unsigned i = 0; i < 2; ++i) {
		for (unsigned j = 0; j < 2; ++j) {
			auto &corner = schedule.corner[i][j];
			corner.edf_force_per_thrust[2] = -1.F;
			corner.trim_force[2] = -12.F * static_cast<float>(j);

			for (unsigned axis = 0; axis < kAxes; ++axis) {
				corner.moment_derivative[axis][axis] = 10.F;
				corner.rate_p[axis] = 0.2F + 0.1F * static_cast<float>(i + j);
				corner.rate_i[axis] = 0.1F;
			}
		}
	}

	auto &p = c.companion;
	p.enabled = p.admission_enabled = 1; p.mavlink_instance = 0; p.source_system = 42; p.source_component = 191;
	p.source_id = 1; p.maximum_age_us = 100000; p.disarmed_policy = 2;

	for (unsigned axis = 0; axis < kAxes; ++axis) { p.acceleration_minimum[axis] = -100.F; p.acceleration_maximum[axis] = 100.F; }

	p.acceleration_norm_max = 100.F; p.minimum_validity_us = 1000; p.maximum_validity_us = p.maximum_command_age_us = 500000;
	p.maximum_future_skew_us = 1000; p.maximum_clock_uncertainty_us = 10000;
	p.maximum_relative_drift_ppm = 50; p.timestamp_uncertainty_us = 1000; p.maximum_round_trip_us = 10000; p.map_interval_us = 500000;
	return c;
}

void refresh(const VehicleConfiguration &c, AccelerationControlInput &in, uint64_t now)
{
	if (in.now_us && now > in.now_us) { ++in.request.sequence; }

	in.now_us = now;
	in.context = {now, research_acceleration::ArmState::Armed, research_acceleration::ResearchModeState::Selected, true, true, true};
	in.request.producer_monotonic_us = in.request.producer_time_in_validator_domain_us = now - 1000;
	in.request.producer_expiry_monotonic_us = in.request.expiry_time_in_validator_domain_us = now + 199000;
	in.selected_state.attitude_timestamp_us = in.selected_state.attitude_sample_us = now;
	in.selected_state.rate_timestamp_us = in.selected_state.rate_sample_us = now;
	in.feedback.observed_us = in.feedback.timestamp_sample_us = now;
	in.feedback.deadline_us = now + 99001;

	if (!in.actual_edf_deadline_us) { in.actual_edf_deadline_us = now + 50000; }

	// Observation alone never renews an original owner/map authority lease.
	if (!in.evidence_deadline_us) { in.evidence_deadline_us = now + 150000; }

	auto &condition = in.operating_condition;
	condition.observed_us = condition.airspeed_timestamp_us = condition.airspeed_sample_us = now;
	condition.air_data_timestamp_us = condition.air_data_sample_us = condition.battery_timestamp_us = now;
	condition.deadline_us = now + 100001;
	in.configuration = fingerprint(c);
}

AccelerationControlInput input(const VehicleConfiguration &c)
{
	AccelerationControlInput in{};
	in.previous_handled_us = 980000;
	in.request.protocol_version = research_acceleration::kRequestProtocolVersionV2;
	in.request.command_kind = research_acceleration::CommandKind::AccelerationAndAttitude;
	in.request.source = 1; in.request.session = 7; in.request.sequence = 1; in.request.clock_mapping_uncertainty_us = 10;
	in.request.desired_attitude_ned_from_body = Quaternion(1.F, 0.F, 0.F, 0.F);
	in.selected_state.valid = true; in.selected_state.source_epoch = 1;
	in.selected_state.attitude = Quaternion(1.F, 0.F, 0.F, 0.F);
	in.feedback.source_generation = in.feedback.publisher_revision = 1; in.feedback.device_id = 456;
	in.feedback.state_source_epoch = 1;
	in.operating_condition.source = {1, 1, 1, 123, 0, 0, 0, 0, 0, 1, 0};
	in.operating_condition.indicated_airspeed_m_s = 10.F;
	in.operating_condition.density_kg_m3 = 1.5F; in.operating_condition.voltage_v = 24.F;
	in.actual_edf_command = 0.75F; commandToThrust(c, in.actual_edf_command, in.actual_edf_thrust_n);
	refresh(c, in, 1000000);
	return in;
}

template<typename T> std::array<unsigned char, sizeof(T)> bytes(const T &value)
{
	std::array<unsigned char, sizeof(T)> result{}; std::memcpy(result.data(), &value, sizeof(T)); return result;
}
}

bool slaapsAccelerationControllerTests(unsigned &checks)
{
	using namespace slaaps;
	bool passed = true;
	auto check = [&](bool good, const char *name) { ++checks; if (!good) { std::fprintf(stderr, "FAILED: acceleration controller %s\n", name); passed = false; } };
	const auto c = configuration();
	check(validateAccelerationControlConfiguration(c).valid(), "explicit synthetic completeness");
	auto under_regularized = c;

	for (unsigned i = 0; i < kThrustPoints; ++i) { under_regularized.edf_thrust[i] = 5.F * static_cast<float>(i); }

	for (unsigned i = 0; i < 2; ++i) { under_regularized.schedule.corner[i][1].trim_force[2] = -20.F; }

	const auto insufficient = validateAccelerationControlConfiguration(under_regularized);
	check(validateWrenchConfiguration(under_regularized).valid() && !insufficient.valid()
	      && insufficient.parameter && std::strcmp(insufficient.parameter, "SLA_ALLOC_REG") == 0,
	      "accepted physical model rejects regularization unresolved by the actual full-wrench scaling");
	under_regularized.regularization = 0.0001F;
	check(validateAccelerationControlConfiguration(under_regularized).valid(),
	      "explicit resolvable regularization passes the same allocator criterion");
	auto strong_corner = c;
	strong_corner.schedule.corner[1][1].edf_force_per_thrust[2] = -3.F;
	strong_corner.schedule.corner[1][1].trim_force[2] = -36.F;
	const auto corner_scaling = validateAccelerationControlConfiguration(strong_corner);
	check(!corner_scaling.valid() && corner_scaling.parameter && std::strcmp(corner_scaling.parameter, "SLA_ALLOC_REG") == 0,
	      "all schedule corners constrain numerical allocation validity");
	auto tiny_tolerance = c;
	tiny_tolerance.wrench.force_residual_tolerance[0] = std::numeric_limits<float>::min();
	const auto tolerance_scaling = validateAccelerationControlConfiguration(tiny_tolerance);
	check(!tolerance_scaling.valid() && tolerance_scaling.parameter && std::strcmp(tolerance_scaling.parameter, "SLA_WR_FT_X") == 0,
	      "nonrepresentable physical residual weight identifies its actual tolerance parameter");
	auto incomplete = c;
	incomplete.acceleration_control.acceleration_feedback_p[1] = 0.F;
	check(!validateAccelerationControlConfiguration(incomplete).valid()
	      && validateAccelerationControlConfiguration(incomplete).parameter != nullptr
	      && std::strcmp(validateAccelerationControlConfiguration(incomplete).parameter, "SLA_ACC_P_Y") == 0,
	      "exact missing outer gain parameter");
	check(validate(incomplete, ConfigurationUse::StabilizedManual).valid()
	      && validate(incomplete, ConfigurationUse::Bench).valid(), "incomplete acceleration leaves Manual and Bench valid");
	check(!equal(c, incomplete) && fingerprint(c) != fingerprint(incomplete), "outer gains bind shared profile identity");
	uint64_t lead = 123;
	check(companionClockRenewalConfigured(c.companion, lead) && lead == 250000, "declared clock policy renewal window");
	auto impossible_clock = c.companion; impossible_clock.maximum_round_trip_us = impossible_clock.map_interval_us;
	lead = 123;
	check(!companionClockRenewalConfigured(impossible_clock, lead) && lead == 123, "impossible renewal preserves output");

	auto short_age = c; short_age.companion.maximum_command_age_us = 2000;
	auto aged = std::make_unique<AccelerationControllerPort>(); aged->configure(short_age);
	auto aged_input = input(short_age);
	AccelerationControlOutput aged_output{};
	check(aged->cycle(aged_input, aged_output) == AccelerationPortStatus::Calculated
	      && aged_output.deadline_us == aged_input.request.producer_time_in_validator_domain_us
	      - aged_input.request.clock_mapping_uncertainty_us + 2001,
	      "long producer validity never extends conservative command maximum age");
	auto short_evidence = std::make_unique<AccelerationControllerPort>(); short_evidence->configure(c);
	auto evidence_input = input(c); evidence_input.evidence_deadline_us = evidence_input.now_us + 100;
	check(short_evidence->cycle(evidence_input, aged_output) == AccelerationPortStatus::Calculated
	      && aged_output.deadline_us == evidence_input.evidence_deadline_us,
	      "original map link witness and state evidence bounds the calculation");

	auto controller = std::make_unique<AccelerationControllerPort>();
	check(controller->configure(c) == AccelerationPortStatus::Ready, "configured numerical component");
	auto in = input(c);
	AccelerationControlOutput out{};
	check(controller->cycle(in, out) == AccelerationPortStatus::Calculated && controller->pending(), "feasible current wrench stages output");
	check(near(out.current_edf_thrust_n, 9.F) && near(out.achieved_wrench[2], -9.F)
	      && near(out.edf_thrust_n, 9.F) && out.deadline_us > in.actual_edf_deadline_us
	      && out.current_deadline_us == in.actual_edf_deadline_us, "new target lease distinct from current owner evidence");
	const auto before = bytes(out);
	refresh(c, in, in.now_us + 1);
	check(controller->cycle(in, out) == AccelerationPortStatus::Pending && bytes(out) == before,
	      "pending cycle never reintegrates or replaces output");
	check(!controller->commitHandled(in.now_us + 1) && !controller->commitHandled(999999)
	      && controller->commitHandled(in.now_us) && !controller->commitHandled(in.now_us),
	      "one exact monitored handling timestamp commits once");
	in.previous_handled_us = in.now_us; refresh(c, in, in.now_us + 1); ++in.feedback.source_generation;
	check(controller->monitor(in) == AccelerationPortStatus::Rejected
	      && controller->fault() == AccelerationControlFault::StateChanged, "feedback source replacement revokes committed history");

	auto handling = std::make_unique<AccelerationControllerPort>(); handling->configure(c);
	auto handled_input = input(c);
	AccelerationControlOutput handled_output{};
	check(handling->cycle(handled_input, handled_output) == AccelerationPortStatus::Calculated,
	      "same-time ACK fixture first calculation");
	refresh(c, handled_input, 1020000);
	check(handling->monitor(handled_input) == AccelerationPortStatus::Ready && handling->commitHandled(handled_input.now_us),
	      "actual ACK observed at a later valid control time");
	handled_input.previous_handled_us = handled_input.now_us;

	for (unsigned i = 0; i < kCanards; ++i) { handled_input.previous_handled_canard[i] = handled_output.canard[i]; }

	const auto handled_before = bytes(handled_output);
	check(handling->cycle(handled_input, handled_output) == AccelerationPortStatus::NotDue
	      && bytes(handled_output) == handled_before && handling->available() && !handling->pending(),
	      "same-time handled ACK then cycle monitors without fault zero-dt or integration");
	refresh(c, handled_input, 1020001);
	check(handling->cycle(handled_input, handled_output) == AccelerationPortStatus::NotDue
	      && bytes(handled_output) == handled_before,
	      "tiny actuator elapsed time cannot spend the older PID calculation interval as servo slew");
	refresh(c, handled_input, 1030000);
	check(handling->cycle(handled_input, handled_output) == AccelerationPortStatus::Calculated,
	      "fresh calculation resumes when both real handled and PID intervals permit it");

	auto handed_config = c; handed_config.axis[0].setpoint_accel_limit = 10.F;
	auto handed_rate = std::make_unique<AccelerationControllerPort>(); handed_rate->configure(handed_config);
	auto handed_input = input(handed_config);
	handed_input.previous_handled_rate_setpoint(0) = 4.F;
	AccelerationControlOutput handed_output{};
	check(handed_rate->cycle(handed_input, handed_output) == AccelerationPortStatus::Calculated
	      && near(handed_output.rate_setpoint_rad_s(0), 3.8F),
	      "first acceleration target slews from the genuine handled outgoing body-rate target");
	check(handed_rate->commitHandled(handed_input.now_us), "nonzero rate handoff commits only after handling");
	const auto rate_before = bytes(handed_output);

	for (unsigned i = 0; i < kCanards; ++i) { handed_input.previous_handled_canard[i] = handed_output.canard[i]; }

	handed_input.previous_handled_us = handed_input.now_us;
	refresh(handed_config, handed_input, handed_input.now_us + 20000);
	check(handed_rate->cycle(handed_input, handed_output) == AccelerationPortStatus::Rejected && bytes(handed_output) == rate_before,
	      "subsequent rate-history proof must match the last all-bank handled target");

	for (unsigned fault = 0; fault < 8; ++fault) {
		auto failing = std::make_unique<AccelerationControllerPort>(); failing->configure(c);
		auto bad = input(c);

		if (fault == 0) { bad.actual_edf_command = bad.actual_edf_thrust_n = 0.F; }

		if (fault == 1) { bad.request.acceleration_frd_m_s2(0) = bad.feedback.acceleration_frd_m_s2(0) = 1.F; }

		if (fault == 2) { ++bad.feedback.state_source_epoch; }

		if (fault == 3) { bad.request.desired_attitude_ned_from_body = Quaternion(0.F, 0.F, 0.F, 0.F); }

		if (fault == 4) { bad.request.protocol_version = research_acceleration::kRequestProtocolVersionV1; bad.request.command_kind = research_acceleration::CommandKind::AccelerationOnly; }

		if (fault == 5) { bad.previous_handled_us = bad.now_us + 1; }

		if (fault == 6) { bad.previous_handled_rate_setpoint(0) = std::numeric_limits<float>::quiet_NaN(); }

		if (fault == 7) { bad.previous_handled_rate_setpoint(0) = c.axis[0].rate_limit + 1.F; }

		const auto saved = bytes(out);
		check(failing->cycle(bad, out) == AccelerationPortStatus::Rejected && bytes(out) == saved && !failing->pending(),
		      "future-only thrust, impossible force, epoch, attitude, v1 and future ACK time reject unchanged");
	}

	auto correction = std::make_unique<AccelerationControllerPort>(); correction->configure(c);
	auto correction_input = input(c);
	correction_input.feedback.acceleration_frd_m_s2(2) = -10.F;
	correction_input.actual_edf_command = 8.75F / 12.F;
	commandToThrust(c, correction_input.actual_edf_command, correction_input.actual_edf_thrust_n);
	check(correction->cycle(correction_input, out) == AccelerationPortStatus::Calculated
	      && near(out.acceleration_correction(2), 0.25F) && near(out.requested_wrench[2], -8.75F),
	      "bounded outer acceleration P and gravity at CG");
	correction->discard();
	check(!correction->available() && !correction->commitHandled(correction_input.now_us), "discard cannot commit an unhandled output");

	auto limited_config = c; limited_config.axis[0].moment_limit = 0.02F;
	auto limited = std::make_unique<AccelerationControllerPort>(); limited->configure(limited_config);
	auto turn = input(limited_config);
	turn.request.desired_attitude_ned_from_body = Quaternion(0.F, 1.F, 0.F, 0.F);
	check(limited->cycle(turn, out) == AccelerationPortStatus::Calculated && out.saturation_positive(0)
	      && near(out.requested_normalized_moment(0), 0.65F), "scheduled rotational P and actual moment saturation");
	const auto first_turn = out;
	refresh(limited_config, turn, 1001000);
	check(limited->monitor(turn) == AccelerationPortStatus::Ready && limited->commitHandled(1001000),
	      "worker monitors final ACK observation before commit");
	refresh(limited_config, turn, 1020000); turn.previous_handled_us = 1001000;

	for (unsigned i = 0; i < kCanards; ++i) { turn.previous_handled_canard[i] = first_turn.canard[i]; }

	turn.previous_handled_rate_setpoint = first_turn.rate_setpoint_rad_s;

	check(limited->cycle(turn, out) == AccelerationPortStatus::Calculated && near(out.integral(0), first_turn.integral(0)),
	      "current achieved-moment antiwindup stops further positive integration");

	auto opposite = std::make_unique<AccelerationControllerPort>(); opposite->configure(limited_config);
	auto negative = input(limited_config); negative.request.desired_attitude_ned_from_body = Quaternion(0.F, -1.F, 0.F, 0.F);
	negative.actual_edf_deadline_us = negative.now_us + 200000; // Outlast pending output, isolating its immutable expiry.
	check(opposite->cycle(negative, out) == AccelerationPortStatus::Calculated && near(out.canard[0], first_turn.canard[0])
	      && near(out.rate_setpoint_rad_s(0), first_turn.rate_setpoint_rad_s(0)), "absolute desired q and minus q equivalent");
	const auto pending_deadline = out.deadline_us;
	refresh(limited_config, negative, pending_deadline);
	check(opposite->monitor(negative) == AccelerationPortStatus::Rejected && !opposite->commitHandled(negative.now_us),
	      "new observations cannot renew original pending output expiry");

	auto plateau = c;
	plateau.edf_thrust[0] = 0.F; plateau.edf_thrust[1] = 1.F;
	plateau.edf_thrust[2] = std::nextafter(1.F, 2.F); plateau.edf_thrust[3] = 3.F; plateau.edf_thrust[4] = 4.F;
	plateau.edf_rise = plateau.edf_fall = 0.01F;

	for (unsigned i = 0; i < 2; ++i) { plateau.schedule.corner[i][1].trim_force[2] = -4.F; }

	WrenchModel model{};
	check(makeWrenchCornerModel(plateau, 0, 0, model), "plateau model fixture");
	const float requested[kWrenchAxes] {0.F, 0.F, -1.F, 0.F, 0.F, 0.F};
	float previous[kWrenchInputs] {};
	commandToThrust(plateau, 0.3F, previous[kCanards]);
	WrenchAllocationReport fixed{};
	check(allocateVehicleWrenchAtCurrentThrust(plateau, model, requested, previous, 0.3F, 0.01F, fixed)
	      == WrenchAllocationResult::Success && near(fixed.realized_edf_thrust_n, previous[kCanards]),
	      "fixed-current solve keeps actual owner command at a float thrust plateau");
	const auto fixed_before = bytes(fixed);
	check(allocateVehicleWrench(plateau, model, requested, previous, 0.3F, 0.01F, fixed)
	      == WrenchAllocationResult::ArithmeticFailure && bytes(fixed) == fixed_before,
	      "future target inverse cannot silently spend old current-command slew");

	// Actual producer/consumer regression, not a fabricated rounded EDF value.
	// Every permission/model value is synthetic and grants no hardware authority.
	auto rate_owner = std::make_unique<EdfOwner>();
	auto rate_schedule = std::make_unique<ControlSchedule>();
	EdfPermission rate_permission{};
	rate_permission.timestamp_us = 1000; rate_permission.deadline_us = 100000;
	rate_permission.session = 1; rate_permission.source = 42; rate_permission.configuration = fingerprint(c);
	rate_permission.mode = ControlMode::Manual;
	rate_permission.armed = rate_permission.healthy = rate_permission.flight_admitted = rate_permission.measured_condition_valid = true;
	EdfRequest rate_request{};
	rate_request.session = rate_permission.session; rate_request.source = rate_permission.source;
	rate_request.configuration = rate_permission.configuration; rate_request.mode = rate_permission.mode;
	rate_request.units = EdfUnits::Normalized; rate_request.sequence = 1;
	rate_request.timestamp_us = 1000; rate_request.deadline_us = rate_permission.deadline_us;
	auto rate_condition = input(c).operating_condition;
	auto sample_condition = [&](uint64_t now) {
		rate_condition.observed_us = rate_condition.airspeed_timestamp_us = rate_condition.airspeed_sample_us = now;
		rate_condition.air_data_timestamp_us = rate_condition.air_data_sample_us = rate_condition.battery_timestamp_us = now;
		rate_condition.deadline_us = now + static_cast<uint64_t>(c.schedule.maximum_age_us) + 1;
	};
	sample_condition(1000);
	ControlSchedule::Step rate_step{};
	check(rate_owner->begin(1000, c, rate_permission) == EdfFault::None
	      && rate_owner->accept(1000, rate_permission, rate_request) == EdfFault::None
	      && rate_schedule->begin(1000, c, rate_condition, rate_owner->sample(1000, rate_permission).normalized, rate_step),
	      "actual EDF owner and empirical schedule share the initial zero command");
	rate_request.timestamp_us = 1001; rate_request.sequence = 2; rate_request.value = 0.25F;
	check(rate_owner->accept(1001, rate_permission, rate_request) == EdfFault::None, "actual EDF owner accepts a new bounded target");
	sample_condition(1001);
	check(rate_schedule->observe(1001, rate_condition, rate_owner->sample(1001, rate_permission).normalized),
	      "target acceptance spends no prior slew time");
	const auto rise_from = rate_owner->sample(2001, rate_permission);
	sample_condition(2001);
	check(rate_schedule->observe(2001, rate_condition, rise_from.normalized), "first actual rate-limited EDF step is observed");
	auto outside_rise = std::make_unique<ControlSchedule>(*rate_schedule);
	const auto rise_to = rate_owner->sample(2038, rate_permission);
	const float encoded_step = c.schedule.edf_transient_rate * (37.F * 1e-6F);
	sample_condition(2038);
	check(rise_to.normalized - rise_from.normalized > encoded_step
	      && rate_schedule->observe(2038, rate_condition, rise_to.normalized),
	      "actual short owner poll defeats delta subtraction but stays inside reachable binary32 endpoints");
	check(!outside_rise->observe(2038, rate_condition, std::nextafter(rise_to.normalized, 1.F))
	      && outside_rise->status() == ControlScheduleStatus::Transient,
	      "one representable command above the exact rising endpoint remains rejected");
	check(rate_schedule->observe(2038, rate_condition, rise_to.normalized), "zero elapsed requires the same encoded EDF value");
	auto zero_mutation = std::make_unique<ControlSchedule>(*rate_schedule);
	check(!zero_mutation->observe(2038, rate_condition, std::nextafter(rise_to.normalized, 1.F)),
	      "zero elapsed cannot move even one representable EDF step");
	auto nonfinite_edf = std::make_unique<ControlSchedule>(*rate_schedule);
	check(!nonfinite_edf->observe(2038, rate_condition, std::numeric_limits<float>::quiet_NaN()),
	      "nonfinite EDF commands never enter the reachable-endpoint check");
	rate_request.timestamp_us = 2039; rate_request.sequence = 3; rate_request.value = 0.F;
	check(rate_owner->accept(2039, rate_permission, rate_request) == EdfFault::None,
	      "actual EDF owner starts a falling target at the current time");
	const auto fall_from = rate_owner->sample(2039, rate_permission);
	sample_condition(2039);
	check(rate_schedule->observe(2039, rate_condition, fall_from.normalized),
	      "outgoing target slew is observed before the falling request takes effect");
	auto outside_fall = std::make_unique<ControlSchedule>(*rate_schedule);
	const auto fall_to = rate_owner->sample(2076, rate_permission);
	sample_condition(2076);
	check(fall_from.normalized - fall_to.normalized > encoded_step
	      && rate_schedule->observe(2076, rate_condition, fall_to.normalized),
	      "actual falling owner step also respects its encoded reachable endpoint");
	check(!outside_fall->observe(2076, rate_condition, std::nextafter(fall_to.normalized, 0.F))
	      && outside_fall->status() == ControlScheduleStatus::Transient,
	      "one representable command below the exact falling endpoint remains rejected");
	return passed;
}
