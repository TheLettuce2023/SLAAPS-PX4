#include "AccelerationControllerPort.hpp"
#include <lib/research_stabilized_manual/StabilizedInputValidation.hpp>

#include <cmath>
#include <float.h>

namespace slaaps
{
using namespace research_stabilized_manual;
namespace
{
float clamp(float value, float low, float high) { return value < low ? low : value > high ? high : value; }
bool same(float left, float right) { return left <= right && left >= right; }
bool cap(uint64_t now, uint64_t timestamp, uint64_t duration, uint64_t &deadline)
{
	if (!timestamp || timestamp > now || duration > UINT64_MAX - timestamp || now >= timestamp + duration) { return false; }

	if (timestamp + duration < deadline) { deadline = timestamp + duration; }

	return true;
}
bool representable(double value, float &result)
{
	if (!std::isfinite(value) || std::fabs(value) > static_cast<double>(FLT_MAX)) { return false; }

	result = static_cast<float>(value);
	return true;
}
}

AccelerationPortStatus AccelerationControllerPort::configure(const VehicleConfiguration &configuration)
{
	disable();
	_fault = AccelerationControlFault::Disabled;
	_allocation_stage = AccelerationAllocationStage::None;
	_allocation_result = WrenchAllocationResult::Success;

	if (!configuration.acceleration_control.enabled) { return AccelerationPortStatus::Unavailable; }

	if (!validateAccelerationControlConfiguration(configuration).valid()) { return reject(AccelerationControlFault::InvalidConfiguration); }

	_configuration = configuration;
	_identity = fingerprint(configuration);
	_numerical.maximum_state_age_us = static_cast<uint64_t>(configuration.state_age_ms) * 1000U;
	_numerical.maximum_state_skew_us = static_cast<uint64_t>(configuration.state_skew_ms) * 1000U;
	_numerical.quaternion_norm_tolerance = configuration.quaternion_tolerance;
	_allocation_validation.regularization = configuration.regularization;
	_allocation_validation.minimum_row_norm = configuration.minimum_row_norm;
	_allocation_validation.minimum_independence = configuration.minimum_independence;
	_allocation_validation.saturation_tolerance = configuration.saturation_tolerance;
	Vector3f integral_limit{};

	for (unsigned axis = 0; axis < kAxes; ++axis) {
		_numerical.maximum_measured_rate_rad_s(axis) = configuration.axis[axis].measured_rate_limit;
		_numerical.maximum_measured_accel_rad_s2(axis) = configuration.axis[axis].measured_accel_limit;
		_allocation_validation.axis_weight(axis) = configuration.axis[axis].allocation_weight;
		integral_limit(axis) = configuration.axis[axis].integrator_limit;
	}

	for (unsigned i = 0; i < kCanards; ++i) {
		const auto &servo = configuration.canard[i];
		_allocation_validation.trim[i] = servo.trim;
		_allocation_validation.minimum[i] = servo.minimum;
		_allocation_validation.maximum[i] = servo.maximum;
		_allocation_validation.positive_slew_per_s[i] = servo.slew_positive;
		_allocation_validation.negative_slew_per_s[i] = servo.slew_negative;
	}

	_rate_control.setIntegratorLimit(integral_limit);
	_rate_control.setFeedForwardGain(Vector3f{});
	_configured = true;
	_failed = false;
	_fault = AccelerationControlFault::None;
	return AccelerationPortStatus::Ready;
}

void AccelerationControllerPort::disable()
{
	_configured = _failed = _pending = _schedule_started = _observed = _committed = false;
	_last_cycle_us = _last_handled_us = _pending_cycle_us = _pending_deadline_us = _identity = _source = _session = 0;
	_rate_control = RateControl{};
	_rate_setpoint.zero();
	_schedule.stop();
	// Fault/stage/result are historical diagnostics. Only an explicit new
	// configure clears them; disabling cannot erase the reason for rejection.
	// Preserve the monotonic observation floor, including across reconfigure.
}

AccelerationPortStatus AccelerationControllerPort::reject(AccelerationControlFault reason)
{
	_pending = false;
	_failed = true;
	_fault = reason;

	if (reason != AccelerationControlFault::AllocationRejected) {
		_allocation_stage = AccelerationAllocationStage::None;
		_allocation_result = WrenchAllocationResult::Success;
	}

	_schedule.stop();
	return AccelerationPortStatus::Rejected;
}

AccelerationPortStatus AccelerationControllerPort::inspect(const AccelerationControlInput &input,
		Quaternion &attitude, uint64_t &deadline)
{
	if (!_configured) { return AccelerationPortStatus::Unavailable; }

	if (_failed) { return AccelerationPortStatus::Rejected; }

	const uint64_t now = input.now_us;
	const auto &request = input.request;
	const auto &context = input.context;
	const auto &feedback = input.feedback;

	if (!now || now < _clock) { return reject(AccelerationControlFault::TimeRegression); }

	_clock = now;

	if (input.configuration != _identity || !input.previous_handled_us || input.previous_handled_us > now
	    || input.actual_edf_deadline_us <= now || input.evidence_deadline_us <= now || (_pending && now >= _pending_deadline_us)
	    || context.validator_receive_time_us == 0 || context.validator_receive_time_us > now
	    || context.arm_state != research_acceleration::ArmState::Armed
	    || context.mode_state != research_acceleration::ResearchModeState::Selected
	    || !context.clock_mapping_valid || !context.transport_connected || !context.required_state_valid
	    || request.protocol_version != research_acceleration::kRequestProtocolVersionV2
	    || request.command_kind != research_acceleration::CommandKind::AccelerationAndAttitude
	    || request.source != static_cast<uint64_t>(_configuration.companion.source_id) || !request.session
	    || request.frame != research_acceleration::Frame::Frd
	    || request.quantity != research_acceleration::Quantity::InertialAcceleration
	    || request.reference_point != research_acceleration::ReferencePoint::Cg) { return reject(AccelerationControlFault::InvalidInput); }

	Quaternion desired;

	if (research_acceleration::normaliseAttitudeQuaternion(request.desired_attitude_ned_from_body,
			_configuration.quaternion_tolerance, desired) != research_acceleration::MathStatus::Ok) { return reject(AccelerationControlFault::InvalidInput); }

	if (_observed && (request.source != _source || request.session != _session
			  || feedback.source_generation != _observed_feedback.source_generation
			  || feedback.publisher_revision != _observed_feedback.publisher_revision
			  || feedback.device_id != _observed_feedback.device_id)) { return reject(AccelerationControlFault::StateChanged); }

	if (validateStabilizedState(now, input.selected_state, _numerical, _observed ? &_observed_state : nullptr, attitude) != Status::Ok
	    || !feedback.source_generation || !feedback.publisher_revision || !feedback.device_id
	    || feedback.state_source_epoch != input.selected_state.source_epoch
	    || feedback.attitude_reset_counter != input.selected_state.attitude_reset_counter) { return reject(AccelerationControlFault::StateChanged); }

	const uint64_t uncertainty = request.clock_mapping_uncertainty_us;
	const auto &timing = _configuration.companion;

	if (request.producer_expiry_monotonic_us <= request.producer_monotonic_us
	    || request.expiry_time_in_validator_domain_us <= request.producer_time_in_validator_domain_us
	    || request.producer_expiry_monotonic_us - request.producer_monotonic_us
	    != request.expiry_time_in_validator_domain_us - request.producer_time_in_validator_domain_us
	    || request.producer_expiry_monotonic_us - request.producer_monotonic_us < static_cast<uint64_t>(timing.minimum_validity_us)
	    || request.producer_expiry_monotonic_us - request.producer_monotonic_us > static_cast<uint64_t>(timing.maximum_validity_us)
	    || uncertainty > static_cast<uint64_t>(timing.maximum_clock_uncertainty_us)
	    || request.producer_time_in_validator_domain_us < uncertainty || request.expiry_time_in_validator_domain_us < uncertainty
	    || uncertainty > UINT64_MAX - request.producer_time_in_validator_domain_us
	    || static_cast<uint64_t>(timing.maximum_future_skew_us) > UINT64_MAX - now) { return reject(AccelerationControlFault::InvalidInput); }

	const uint64_t creation = request.producer_time_in_validator_domain_us - uncertainty;
	deadline = request.expiry_time_in_validator_domain_us - uncertainty;
	const uint64_t maximum_age = static_cast<uint64_t>(timing.maximum_command_age_us);

	if (maximum_age >= UINT64_MAX - creation) { return reject(AccelerationControlFault::ArithmeticFailure); }

	const uint64_t age_deadline = creation + maximum_age + 1;

	if (age_deadline < deadline) { deadline = age_deadline; }

	if (input.evidence_deadline_us < deadline) { deadline = input.evidence_deadline_us; }

	if (creation > now
	    || request.producer_time_in_validator_domain_us + uncertainty > now + static_cast<uint64_t>(timing.maximum_future_skew_us)
	    || now - creation > static_cast<uint64_t>(timing.maximum_command_age_us) || deadline <= now
	    || !feedback.observed_us || feedback.observed_us > now || feedback.deadline_us <= now
	    || !feedback.timestamp_sample_us || feedback.timestamp_sample_us > feedback.observed_us
	    || now - feedback.timestamp_sample_us > static_cast<uint64_t>(_configuration.feedback.maximum_age_us -
			    _configuration.feedback.latency_bound_us)
	    || (_observed && (feedback.timestamp_sample_us < _observed_feedback.timestamp_sample_us
			      || feedback.observed_us < _observed_feedback.observed_us))) { return reject(AccelerationControlFault::Expired); }

	const auto &state = input.selected_state;
	uint64_t low = feedback.timestamp_sample_us, high = low;
	const uint64_t samples[] {state.attitude_sample_us, state.rate_sample_us};

	for (uint64_t timestamp : samples) { if (timestamp < low) { low = timestamp; } if (timestamp > high) { high = timestamp; } }

	if (high - low > _numerical.maximum_state_skew_us
	    || static_cast<uint64_t>(_configuration.feedback.latency_bound_us) > _numerical.maximum_state_skew_us - (high - low)) { return reject(AccelerationControlFault::InvalidInput); }

	double norm_squared = 0.0, feedback_norm_squared = 0.0;

	for (unsigned axis = 0; axis < kAxes; ++axis) {
		const float acceleration = request.acceleration_frd_m_s2(axis);

		if (!std::isfinite(acceleration) || acceleration < timing.acceleration_minimum[axis] || acceleration > timing.acceleration_maximum[axis]
		    || !std::isfinite(feedback.acceleration_frd_m_s2(axis))
		    || (_observed && feedback.timestamp_sample_us == _observed_feedback.timestamp_sample_us
			&& !same(feedback.acceleration_frd_m_s2(axis), _observed_feedback.acceleration_frd_m_s2(axis)))) { return reject(AccelerationControlFault::InvalidInput); }

		norm_squared += static_cast<double>(acceleration) * static_cast<double>(acceleration);
		feedback_norm_squared += static_cast<double>(feedback.acceleration_frd_m_s2(axis)) * static_cast<double>(feedback.acceleration_frd_m_s2(
						 axis));
	}

	if (!std::isfinite(norm_squared) || std::sqrt(norm_squared) > static_cast<double>(timing.acceleration_norm_max)
	    || !std::isfinite(feedback_norm_squared)
	    || std::sqrt(feedback_norm_squared) > static_cast<double>(_configuration.feedback.maximum_acceleration_m_s2)) { return reject(AccelerationControlFault::InvalidInput); }

	float thrust{};

	if (!commandToThrust(_configuration, input.actual_edf_command, thrust) || !same(thrust, input.actual_edf_thrust_n)) { return reject(AccelerationControlFault::InvalidInput); }

	for (unsigned i = 0; i < kCanards; ++i) {
		const float previous = input.previous_handled_canard[i];

		if (!std::isfinite(previous) || previous < _configuration.canard[i].minimum || previous > _configuration.canard[i].maximum
		    || (_committed && (!same(previous, _handled_canard[i]) || input.previous_handled_us != _last_handled_us))) { return reject(AccelerationControlFault::InvalidInput); }
	}

	for (unsigned axis = 0; axis < kAxes; ++axis) {
		const float previous = input.previous_handled_rate_setpoint(axis);

		if (!std::isfinite(previous) || std::fabs(previous) > _configuration.axis[axis].rate_limit
		    || (_committed && !same(previous, _rate_setpoint(axis)))) { return reject(AccelerationControlFault::InvalidInput); }
	}

	if (!_schedule_started) {
		if (!_schedule.begin(now, _configuration, input.operating_condition, input.actual_edf_command, _pending_step)) { return reject(AccelerationControlFault::InvalidSchedule); }

		_schedule_started = true;

	} else if (!_schedule.observe(now, input.operating_condition, input.actual_edf_command)) { return reject(AccelerationControlFault::InvalidSchedule); }

	const uint64_t state_duration = _numerical.maximum_state_age_us + 1;
	const uint64_t timestamps[] {state.attitude_timestamp_us, state.attitude_sample_us, state.rate_timestamp_us, state.rate_sample_us};

	for (uint64_t timestamp : timestamps) { if (!cap(now, timestamp, state_duration, deadline)) { return reject(AccelerationControlFault::Expired); } }

	if (feedback.deadline_us < deadline) { deadline = feedback.deadline_us; }

	if (!cap(now, feedback.timestamp_sample_us,
		 static_cast<uint64_t>(_configuration.feedback.maximum_age_us - _configuration.feedback.latency_bound_us) + 1,
		 deadline)) { return reject(AccelerationControlFault::Expired); }

	if (_schedule.deadline() < deadline) { deadline = _schedule.deadline(); }

	if (!cap(now, now, static_cast<uint64_t>(_configuration.cycle_max_us), deadline)) { return reject(AccelerationControlFault::Expired); }

	_observed_state = state;
	_observed_feedback = feedback;
	_source = request.source;
	_session = request.session;
	_observed = true;
	return AccelerationPortStatus::Ready;
}

AccelerationPortStatus AccelerationControllerPort::monitor(const AccelerationControlInput &input)
{
	Quaternion attitude;
	uint64_t deadline{};
	return inspect(input, attitude, deadline);
}

AccelerationPortStatus AccelerationControllerPort::cycle(const AccelerationControlInput &input,
		AccelerationControlOutput &output)
{
	Quaternion attitude;
	uint64_t deadline{};
	const auto inspected = inspect(input, attitude, deadline);

	if (inspected != AccelerationPortStatus::Ready) { return inspected; }

	if (_pending) { return AccelerationPortStatus::Pending; }

	const uint64_t elapsed = input.now_us - (_committed ? _last_cycle_us : input.previous_handled_us);
	const uint64_t actuator_elapsed = input.now_us - input.previous_handled_us;

	if (elapsed < static_cast<uint64_t>(_configuration.cycle_min_us)
	    || actuator_elapsed < static_cast<uint64_t>(_configuration.cycle_min_us)) { return AccelerationPortStatus::NotDue; }

	if (elapsed > static_cast<uint64_t>(_configuration.cycle_max_us) || actuator_elapsed > static_cast<uint64_t>(_configuration.cycle_max_us)) { return reject(AccelerationControlFault::Expired); }

	const float dt_s = static_cast<float>(elapsed) * 1e-6F;
	const float actuator_dt_s = static_cast<float>(actuator_elapsed) * 1e-6F;
	auto &step = _pending_step;

	if (!_schedule.evaluate(input.now_us, step) || !step.wrench_valid) { return reject(AccelerationControlFault::InvalidSchedule); }

	_allocation_validation.effectiveness = step.control.effectiveness;

	if (!validAllocationConfiguration(_allocation_validation)) { return reject(AccelerationControlFault::InvalidSchedule); }

	for (unsigned axis = 0; axis < kAxes; ++axis) {
		if (!std::isfinite(step.control.rate_p(axis)) || step.control.rate_p(axis) <= 0.F
		    || !std::isfinite(step.control.rate_i(axis)) || step.control.rate_i(axis) < 0.F
		    || !std::isfinite(step.control.rate_d(axis)) || step.control.rate_d(axis) < 0.F) { return reject(AccelerationControlFault::InvalidSchedule); }
	}

	Quaternion desired;

	if (research_acceleration::normaliseAttitudeQuaternion(input.request.desired_attitude_ned_from_body,
			_configuration.quaternion_tolerance, desired) != research_acceleration::MathStatus::Ok) { return reject(AccelerationControlFault::InvalidInput); }

	const Quaternion error = (attitude.inversed() * desired).canonical();
	Vector3f rate_setpoint{};

	for (unsigned axis = 0; axis < kAxes; ++axis) {
		const auto &limits = _configuration.axis[axis];
		const float wanted = 2.F * error.imag()(axis) * limits.attitude_gain;
		const float slew = limits.setpoint_accel_limit * dt_s;
		const float previous = _committed ? _rate_setpoint(axis) : input.previous_handled_rate_setpoint(axis);
		rate_setpoint(axis) = clamp(clamp(wanted, -limits.rate_limit, limits.rate_limit), previous - slew, previous + slew);

		if (!std::isfinite(rate_setpoint(axis))) { return reject(AccelerationControlFault::ArithmeticFailure); }
	}

	_pending_rate_control = _rate_control;
	auto &rate_control = _pending_rate_control;

	if (_committed) {
		if (!rate_control.setPidGainsBumpless(step.control.rate_p, step.control.rate_i, step.control.rate_d,
						      rate_setpoint - input.selected_state.rate_rad_s, input.selected_state.angular_accel_rad_s2)) { return reject(AccelerationControlFault::GainTransitionRejected); }

	} else { rate_control.setPidGains(step.control.rate_p, step.control.rate_i, step.control.rate_d); }

	const Vector3f moment = rate_control.update(input.selected_state.rate_rad_s, rate_setpoint,
				input.selected_state.angular_accel_rad_s2, dt_s, false);
	AccelerationControlOutput candidate{};
	Vector3f acceleration{};

	for (unsigned axis = 0; axis < kAxes; ++axis) {
		const auto &outer = _configuration.acceleration_control;
		float correction{};

		if (!representable(static_cast<double>(outer.acceleration_feedback_p[axis])
				   * (static_cast<double>(input.request.acceleration_frd_m_s2(axis)) - static_cast<double>(input.feedback.acceleration_frd_m_s2(axis))),
				   correction)) { return reject(AccelerationControlFault::ArithmeticFailure); }

		candidate.acceleration_correction(axis) = clamp(correction, -outer.correction_limit[axis], outer.correction_limit[axis]);
		acceleration(axis) = input.request.acceleration_frd_m_s2(axis) + candidate.acceleration_correction(axis);
		const auto &limits = _configuration.axis[axis];

		if (!std::isfinite(moment(axis)) || !representable(static_cast<double>(clamp(moment(axis), -limits.moment_limit, limits.moment_limit))
				* static_cast<double>(limits.moment_scale), candidate.requested_wrench[axis + kAxes])) { return reject(AccelerationControlFault::ArithmeticFailure); }
	}

	Vector3f specific_force{};

	if (research_acceleration::specificForceFrdFromInertialAccelerationFrd(attitude, _configuration.quaternion_tolerance,
			Vector3f(0.F, 0.F, _configuration.feedback.gravity_m_s2), acceleration, specific_force) != research_acceleration::MathStatus::Ok) { return reject(AccelerationControlFault::ArithmeticFailure); }

	for (unsigned axis = 0; axis < kAxes; ++axis) {
		if (!representable(static_cast<double>(_configuration.mass) * static_cast<double>(specific_force(axis)),
				   candidate.requested_wrench[axis])) { return reject(AccelerationControlFault::ArithmeticFailure); }
	}

	float previous[kWrenchInputs] {};

	for (unsigned i = 0; i < kCanards; ++i) { previous[i] = input.previous_handled_canard[i]; }

	previous[kCanards] = input.actual_edf_thrust_n;
	WrenchAllocationReport current{};

	_allocation_stage = AccelerationAllocationStage::FutureTarget;
	_allocation_result = allocateVehicleWrench(_configuration, step.wrench, candidate.requested_wrench, previous,
			     input.actual_edf_command, actuator_dt_s, current);

	if (_allocation_result != WrenchAllocationResult::Success) { return reject(AccelerationControlFault::AllocationRejected); }

	candidate.edf_thrust_n = current.command[kCanards];

	_allocation_stage = AccelerationAllocationStage::CurrentOwnerThrust;
	_allocation_result = allocateVehicleWrenchAtCurrentThrust(_configuration, step.wrench, candidate.requested_wrench, previous,
			     input.actual_edf_command, actuator_dt_s, current);

	if (_allocation_result != WrenchAllocationResult::Success) { return reject(AccelerationControlFault::AllocationRejected); }

	_allocation_stage = AccelerationAllocationStage::None;

	for (unsigned axis = 0; axis < kAxes; ++axis) {
		const float residual = moment(axis) - current.achieved[axis + kAxes] / _configuration.axis[axis].moment_scale;

		if (!std::isfinite(residual)) { return reject(AccelerationControlFault::ArithmeticFailure); }

		candidate.saturation_positive(axis) = residual > _configuration.saturation_tolerance;
		candidate.saturation_negative(axis) = residual < -_configuration.saturation_tolerance;
	}

	rate_control.setSaturationStatus(candidate.saturation_positive, candidate.saturation_negative);
	rate_ctrl_status_s integral{};
	rate_control.getRateControlStatus(integral);
	candidate.integral = Vector3f(integral.rollspeed_integ, integral.pitchspeed_integ, integral.yawspeed_integ);

	for (unsigned axis = 0; axis < kAxes; ++axis) {
		if (!std::isfinite(candidate.integral(axis)) || std::fabs(candidate.integral(axis)) > _configuration.axis[axis].integrator_limit) { return reject(AccelerationControlFault::ArithmeticFailure); }
	}

	for (unsigned i = 0; i < kCanards; ++i) {
		candidate.canard[i] = current.command[i];
		_pending_canard[i] = current.command[i];

		if (current.saturation_lower[i]) { candidate.canard_limit_mask |= 1U << i; }

		if (current.saturation_upper[i]) { candidate.canard_limit_mask |= 1U << (i + kCanards); }
	}

	for (unsigned row = 0; row < kWrenchAxes; ++row) { candidate.achieved_wrench[row] = current.achieved[row]; candidate.residual[row] = current.residual[row]; }

	candidate.current_edf_thrust_n = current.realized_edf_thrust_n;
	candidate.deadline_us = deadline;
	candidate.current_deadline_us = deadline < input.actual_edf_deadline_us ? deadline : input.actual_edf_deadline_us;
	candidate.rate_setpoint_rad_s = rate_setpoint;
	candidate.requested_normalized_moment = moment;
	_pending_rate_setpoint = rate_setpoint;
	_pending_cycle_us = input.now_us;
	_pending_deadline_us = deadline;
	_pending = true;
	output = candidate;
	return AccelerationPortStatus::Calculated;
}

bool AccelerationControllerPort::commitHandled(uint64_t handled_us)
{
	if (!available() || !_pending || _clock >= _pending_deadline_us || handled_us < _pending_cycle_us
	    || handled_us > _clock || handled_us >= _pending_deadline_us) { return false; }

	_rate_control = _pending_rate_control;
	_rate_setpoint = _pending_rate_setpoint;
	_schedule.commit(_pending_step);
	_last_cycle_us = _pending_cycle_us;
	_last_handled_us = handled_us;

	for (unsigned i = 0; i < kCanards; ++i) { _handled_canard[i] = _pending_canard[i]; }

	_committed = true;
	_pending = false;
	return true;
}
}
