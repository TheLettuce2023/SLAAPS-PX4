#include "StabilizedManualControl.hpp"
#include "StabilizedInputValidation.hpp"

#include <cmath>

namespace research_stabilized_manual
{
namespace
{
bool boundedVector(const Vector3f &value, const Vector3f &limit)
{
	for (unsigned axis = 0; axis < 3; ++axis) {
		if (!std::isfinite(value(axis)) || std::fabs(value(axis)) > limit(axis)) { return false; }
	}

	return true;
}

float clamp(float value, float lower, float upper) { return ::fmaxf(lower, ::fminf(upper, value)); }

bool centered(const ManualSample &sample, float limit)
{
	for (unsigned axis = 0; axis < 3; ++axis) {
		if (!std::isfinite(sample.axes[axis]) || std::fabs(sample.axes[axis]) > limit) { return false; }
	}

	return true;
}


// Share the exact admission checks without constructing PID, attitude-history
// and controller state on physical-driver validation stacks.
Status validateControlConfiguration(const Configuration &configuration,
				    research_actuator_command::ManualControlInput &input)
{
	if (configuration.maximum_manual_age_us == 0 || configuration.maximum_state_age_us == 0
	    || configuration.maximum_state_skew_us == 0 || configuration.minimum_cycle_us == 0
	    || configuration.maximum_cycle_us < configuration.minimum_cycle_us
	    || !std::isfinite(configuration.quaternion_norm_tolerance) || configuration.quaternion_norm_tolerance <= 0.0F
	    || configuration.quaternion_norm_tolerance >= 1.0F || !std::isfinite(configuration.centered_enter)
	    || !std::isfinite(configuration.centered_exit) || configuration.centered_enter < 0.0F
	    || configuration.centered_exit <= configuration.centered_enter || configuration.centered_exit >= 1.0F
	    || !std::isfinite(configuration.fixed_simulated_thrust_n) || configuration.fixed_simulated_thrust_n < 0.0F
	    || !validAllocationConfiguration(configuration.allocation)) { return Status::InvalidConfiguration; }

	for (unsigned axis = 0; axis < 3; ++axis) {
		if (configuration.stick_axis[axis] >= 3) { return Status::InvalidConfiguration; }

		for (unsigned previous = 0; previous < axis; ++previous) {
			if (configuration.stick_axis[axis] == configuration.stick_axis[previous]) { return Status::InvalidConfiguration; }
		}

		const float positive[] = {configuration.maximum_rate_rad_s(axis), configuration.maximum_rate_setpoint_accel_rad_s2(axis),
					  configuration.maximum_measured_rate_rad_s(axis), configuration.maximum_measured_accel_rad_s2(axis),
					  configuration.attitude_gain(axis), configuration.rate_p(axis), configuration.integrator_limit(axis),
					  configuration.maximum_normalized_moment(axis)
					 };

		for (float value : positive) { if (!std::isfinite(value) || value <= 0.0F) { return Status::InvalidConfiguration; } }

		if (!std::isfinite(configuration.stick_sign(axis)) || std::fabs(configuration.stick_sign(axis)) < 1.0F
		    || std::fabs(configuration.stick_sign(axis)) > 1.0F
		    || !std::isfinite(configuration.rate_i(axis)) || configuration.rate_i(axis) < 0.0F
		    || !std::isfinite(configuration.rate_d(axis)) || configuration.rate_d(axis) < 0.0F
		    || configuration.maximum_normalized_moment(axis) > 1.0F) { return Status::InvalidConfiguration; }
	}

	if (configureStabilizedManualInput(configuration, input) != research_actuator_command::ManualControlStatus::Ok) { return Status::InvalidConfiguration; }

	return Status::Ok;
}
}

Status StabilizedManualControl::validateConfiguration(const Configuration &configuration)
{
	research_actuator_command::ManualControlInput input{};
	return validateControlConfiguration(configuration, input);
}

Status StabilizedManualControl::configure(const Configuration &configuration)
{
	if (_state != State::Disabled) { return Status::InvalidState; }

	const auto validation = validateControlConfiguration(configuration, _manual);

	if (validation != Status::Ok) { return validation; }

	_configuration = configuration;
	_rate_control = RateControl{};
	_rate_control.setPidGains(configuration.rate_p, configuration.rate_i, configuration.rate_d);
	_rate_control.setIntegratorLimit(configuration.integrator_limit);
	_rate_control.setFeedForwardGain(Vector3f{});
	_last_command = configuration.allocation.trim;
	_last_sample = {};
	_last_cycle_us = 0;
	_last_observed_us = 0;
	_rate_setpoint.zero();
	_handled_entry = false;
	_fault = Status::Ok;
	_state = State::Ready;
	return Status::Ok;
}

Status StabilizedManualControl::validateState(TimeUs now, const StateSample &sample, Quaternion &attitude) const
{
	return validateStabilizedState(now, sample, _configuration, _last_cycle_us ? &_last_sample : nullptr, attitude);
}

Status StabilizedManualControl::reject(Status reason)
{
	_state = State::Fault;
	_fault = reason;
	return reason;
}

Status StabilizedManualControl::enable(TimeUs now, const ManualSample &manual, const StateSample &sample)
{
	if (_state != State::Ready) { return Status::InvalidState; }

	if (now == 0) { return reject(Status::InvalidTime); }

	Quaternion attitude;
	const Status status = validateState(now, sample, attitude);

	if (status != Status::Ok) { return reject(status); }

	research_actuator_command::RuntimeInput discarded{};
	research_actuator_command::ManualControlReport input_report{};

	if (_manual.evaluate(now, manual, discarded, input_report) != research_actuator_command::ManualControlStatus::Ok) { return reject(Status::InputRejected); }

	if (!centered(manual, _configuration.centered_enter)) { return reject(Status::NotCentered); }

	_reference = attitude;
	_last_sample = sample;
	_last_cycle_us = now;
	_last_observed_us = now;
	_state = State::Hold;
	return Status::Ok;
}

Status StabilizedManualControl::enableFromHandled(TimeUs now, const ManualSample &manual, const StateSample &sample,
		TimeUs handled_us, const CanardVector &canards, const Vector3f &rate_setpoint)
{
	if (_state != State::Ready) { return Status::InvalidState; }

	if (!handled_us || handled_us > now || now - handled_us > _configuration.maximum_cycle_us) { return reject(Status::InvalidTime); }

	if (!boundedVector(rate_setpoint, _configuration.maximum_rate_rad_s)) { return reject(Status::InvalidState); }

	for (unsigned i = 0; i < canards.size(); ++i) {
		if (!std::isfinite(canards[i]) || canards[i] < _configuration.allocation.minimum[i]
		    || canards[i] > _configuration.allocation.maximum[i]) { return reject(Status::InvalidState); }
	}

	const Status result = enable(now, manual, sample);

	if (result != Status::Ok) { return result; }

	_last_command = canards;
	_rate_setpoint = rate_setpoint;
	_last_cycle_us = handled_us;
	_handled_entry = true;
	return Status::Ok;
}

Status StabilizedManualControl::inspect(TimeUs now, const ManualSample &manual, const StateSample &sample,
					Quaternion &attitude)
{
	if (!eligible()) { return Status::InvalidState; }

	if (now < _last_observed_us || now - _last_cycle_us > _configuration.maximum_cycle_us) { return reject(Status::InvalidTime); }

	const Status status = validateState(now, sample, attitude);

	if (status != Status::Ok) { return reject(status); }

	research_actuator_command::RuntimeInput discarded{};
	research_actuator_command::ManualControlReport input_report{};

	if (_manual.evaluate(now, manual, discarded, input_report) != research_actuator_command::ManualControlStatus::Ok) { return reject(Status::InputRejected); }

	_last_sample = sample;
	_last_observed_us = now;
	return Status::Ok;
}

Status StabilizedManualControl::monitor(TimeUs now, const ManualSample &manual, const StateSample &sample)
{
	Quaternion attitude;
	return inspect(now, manual, sample, attitude);
}

Status StabilizedManualControl::cycle(TimeUs now, const ManualSample &manual, const StateSample &sample, Report &report)
{
	return cycleImpl(now, manual, sample, report, nullptr);
}

Status StabilizedManualControl::cycle(TimeUs now, const ManualSample &manual, const StateSample &sample, Report &report,
				      const ScheduledControlPoint &scheduled)
{
	return cycleImpl(now, manual, sample, report, &scheduled);
}

Status StabilizedManualControl::cycleImpl(TimeUs now, const ManualSample &manual, const StateSample &sample, Report &report,
		const ScheduledControlPoint *scheduled)
{
	if (!eligible()) { return Status::InvalidState; }

	if (now < _last_cycle_us || (now == _last_cycle_us && !_handled_entry)) { return reject(Status::InvalidTime); }

	Quaternion attitude;
	const Status status = inspect(now, manual, sample, attitude);

	if (status != Status::Ok) { return status; }

	if (now - _last_cycle_us < _configuration.minimum_cycle_us) { return Status::NotDue; }

	const float dt_s = static_cast<float>(static_cast<double>(now - _last_cycle_us) * 1.0e-6);

	if (!std::isfinite(dt_s) || dt_s <= 0.0F) { return reject(Status::ArithmeticFailure); }

	State next = _state;
	Quaternion reference = _reference;

	if (_state == State::Hold && !centered(manual, _configuration.centered_exit)) { next = State::Rate; }

	if (_state == State::Rate && centered(manual, _configuration.centered_enter)) { next = State::Hold; reference = attitude; }

	Vector3f rate_setpoint{};

	if (next == State::Hold) {
		const Quaternion error = (attitude.inversed() * reference).canonical();
		rate_setpoint = _configuration.attitude_gain.emult(2.0F * error.imag());

	} else {
		for (unsigned axis = 0; axis < 3; ++axis) {
			const float stick = manual.axes[_configuration.stick_axis[axis]];
			const float magnitude = ::fmaxf(0.0F, (std::fabs(stick) - _configuration.centered_enter)
							/ (1.0F - _configuration.centered_enter));
			rate_setpoint(axis) = (stick < 0.0F ? -magnitude : magnitude)
					      * _configuration.stick_sign(axis) * _configuration.maximum_rate_rad_s(axis);
		}
	}

	for (unsigned axis = 0; axis < 3; ++axis) {
		if (!std::isfinite(rate_setpoint(axis))) { return reject(Status::ArithmeticFailure); }

		const float limit = _configuration.maximum_rate_rad_s(axis);
		const float slew = _configuration.maximum_rate_setpoint_accel_rad_s2(axis) * dt_s;

		if (!std::isfinite(slew)) { return reject(Status::ArithmeticFailure); }

		rate_setpoint(axis) = clamp(clamp(rate_setpoint(axis), -limit, limit), _rate_setpoint(axis) - slew, _rate_setpoint(axis) + slew);
	}

	RateControl rate_control = _rate_control;
	AllocationConfiguration allocation = _configuration.allocation;

	if (scheduled) {
		for (unsigned axis = 0; axis < 3; ++axis) {
			if (!std::isfinite(scheduled->rate_p(axis)) || scheduled->rate_p(axis) <= 0.0F
			    || !std::isfinite(scheduled->rate_i(axis)) || scheduled->rate_i(axis) < 0.0F
			    || !std::isfinite(scheduled->rate_d(axis)) || scheduled->rate_d(axis) < 0.0F) { return reject(Status::InvalidSchedule); }
		}

		allocation.effectiveness = scheduled->effectiveness;

		if (!validAllocationConfiguration(allocation)) { return reject(Status::InvalidSchedule); }

		if (!rate_control.setPidGainsBumpless(scheduled->rate_p, scheduled->rate_i, scheduled->rate_d,
						      rate_setpoint - sample.rate_rad_s, sample.angular_accel_rad_s2)) { return reject(Status::GainTransitionRejected); }
	}

	const Vector3f requested = rate_control.update(sample.rate_rad_s, rate_setpoint, sample.angular_accel_rad_s2, dt_s, false);
	Vector3f bounded{};

	for (unsigned axis = 0; axis < 3; ++axis) {
		if (!std::isfinite(requested(axis))) { return reject(Status::ArithmeticFailure); }

		bounded(axis) = clamp(requested(axis), -_configuration.maximum_normalized_moment(axis), _configuration.maximum_normalized_moment(axis));
	}

	Report candidate{};

	if (!allocateCanards(allocation, bounded, _last_command, dt_s, candidate.allocation)) { return reject(Status::AllocationFailure); }

	for (unsigned axis = 0; axis < 3; ++axis) {
		candidate.allocation.residual(axis) = requested(axis) - candidate.allocation.achieved(axis);

		if (!std::isfinite(candidate.allocation.residual(axis))) { return reject(Status::ArithmeticFailure); }

		candidate.allocation.saturation_positive(axis) = candidate.allocation.residual(axis) > _configuration.allocation.saturation_tolerance;
		candidate.allocation.saturation_negative(axis) = candidate.allocation.residual(axis) < -_configuration.allocation.saturation_tolerance;
	}

	rate_control.setSaturationStatus(candidate.allocation.saturation_positive, candidate.allocation.saturation_negative);
	rate_ctrl_status_s integral{};
	rate_control.getRateControlStatus(integral);
	candidate.integral = Vector3f(integral.rollspeed_integ, integral.pitchspeed_integ, integral.yawspeed_integ);

	if (!boundedVector(candidate.integral, _configuration.integrator_limit)) { return reject(Status::ArithmeticFailure); }

	candidate.timestamp_us = now;
	candidate.state = next;
	candidate.hold_reference = reference;
	candidate.rate_setpoint_rad_s = rate_setpoint;
	candidate.requested_moment = requested;
	candidate.bounded_moment = bounded;
	candidate.fixed_simulated_thrust_n = _configuration.fixed_simulated_thrust_n;
	_rate_control = rate_control;

	if (scheduled) {
		_configuration.rate_p = scheduled->rate_p;
		_configuration.rate_i = scheduled->rate_i;
		_configuration.rate_d = scheduled->rate_d;
		_configuration.allocation.effectiveness = scheduled->effectiveness;
	}

	_last_command = candidate.allocation.command;
	_handled_entry = false;
	_rate_setpoint = rate_setpoint;
	_reference = reference;
	_last_sample = sample;
	_last_cycle_us = now;
	_state = next;
	report = candidate;
	return Status::Ok;
}

void StabilizedManualControl::disable()
{
	_manual.reset();
	_rate_control.resetIntegral();
	_last_cycle_us = 0;
	_last_observed_us = 0;
	_state = State::Disabled;
	_handled_entry = false;
	_fault = Status::Ok;
}

} // namespace research_stabilized_manual
