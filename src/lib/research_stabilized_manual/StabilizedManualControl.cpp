#include "StabilizedManualControl.hpp"

#include <cmath>
#include <cstring>

namespace research_stabilized_manual
{
namespace
{
float clamp(float value, float lower, float upper) { return ::fmaxf(lower, ::fminf(upper, value)); }

bool boundedVector(const Vector3f &value, const Vector3f &limit)
{
	for (unsigned axis = 0; axis < 3; ++axis) {
		if (!std::isfinite(value(axis)) || std::fabs(value(axis)) > limit(axis)) { return false; }
	}

	return true;
}

bool centered(const ManualSample &sample, float limit)
{
	for (unsigned axis = 0; axis < 3; ++axis) {
		if (!std::isfinite(sample.axes[axis]) || std::fabs(sample.axes[axis]) > limit) { return false; }
	}

	return true;
}

bool fresh(TimeUs now, TimeUs publication, TimeUs sample, TimeUs age)
{
	return sample != 0 && publication >= sample && now >= publication && now - sample <= age;
}

template<typename T>
bool same(const T &left, const T &right, unsigned size)
{
	for (unsigned i = 0; i < size; ++i) {
		const float left_value = left(i), right_value = right(i);

		if (std::memcmp(&left_value, &right_value, sizeof(float)) != 0) { return false; }
	}

	return true;
}
}

Status StabilizedManualControl::configure(const Configuration &configuration)
{
	if (_state != State::Disabled) { return Status::InvalidState; }

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

	research_actuator_command::ManualControlConfiguration input{};
	input.required_axis_mask = research_actuator_command::kAttitudeManualAxesMask;
	input.source_policy = research_actuator_command::ManualSourcePolicy::MavlinkOnly;
	input.maximum_sample_age_us = configuration.maximum_manual_age_us;
	input.mavlink_instance = configuration.mavlink_instance;
	input.mavlink_system_id = configuration.source_system_id;
	input.mavlink_component_id = configuration.source_component_id;

	// Unity maps reuse the existing raw-input validator only. Its mapped output
	// is discarded; it is not interpreted as a flight wrench or actuator demand.
	for (unsigned axis = 0; axis < 3; ++axis) { input.demand_map[axis][axis] = 1.0F; }

	if (_manual.configure(input) != research_actuator_command::ManualControlStatus::Ok) { return Status::InvalidConfiguration; }

	_configuration = configuration;
	_rate_control = RateControl{};
	_rate_control.setPidGains(configuration.rate_p, configuration.rate_i, configuration.rate_d);
	_rate_control.setIntegratorLimit(configuration.integrator_limit);
	_rate_control.setFeedForwardGain(Vector3f{});
	_last_command = configuration.allocation.trim;
	_last_sample = {};
	_last_cycle_us = 0;
	_rate_setpoint.zero();
	_fault = Status::Ok;
	_state = State::Ready;
	return Status::Ok;
}

Status StabilizedManualControl::validateState(TimeUs now, const StateSample &sample, Quaternion &attitude) const
{
	if (!sample.valid || sample.source_epoch == 0
	    || !fresh(now, sample.attitude_timestamp_us, sample.attitude_sample_us, _configuration.maximum_state_age_us)
	    || !fresh(now, sample.rate_timestamp_us, sample.rate_sample_us, _configuration.maximum_state_age_us)) { return Status::StateRejected; }

	const auto skew = sample.attitude_sample_us > sample.rate_sample_us ? sample.attitude_sample_us - sample.rate_sample_us
			  : sample.rate_sample_us - sample.attitude_sample_us;

	if (skew > _configuration.maximum_state_skew_us
	    || !boundedVector(sample.rate_rad_s, _configuration.maximum_measured_rate_rad_s)
	    || !boundedVector(sample.angular_accel_rad_s2, _configuration.maximum_measured_accel_rad_s2)) { return Status::StateRejected; }

	double norm_squared = 0.0;

	for (unsigned i = 0; i < 4; ++i) {
		if (!std::isfinite(sample.attitude(i))) { return Status::StateRejected; }

		norm_squared += static_cast<double>(sample.attitude(i)) * static_cast<double>(sample.attitude(i));
	}

	const double norm = std::sqrt(norm_squared);

	if (!std::isfinite(norm) || std::fabs(norm - 1.0) > static_cast<double>(_configuration.quaternion_norm_tolerance)) { return Status::StateRejected; }

	if (_last_cycle_us != 0) {
		if (sample.source_epoch != _last_sample.source_epoch || sample.attitude_reset_counter != _last_sample.attitude_reset_counter) { return Status::StateChanged; }

		if (sample.attitude_sample_us < _last_sample.attitude_sample_us || sample.rate_sample_us < _last_sample.rate_sample_us
		    || sample.attitude_timestamp_us < _last_sample.attitude_timestamp_us || sample.rate_timestamp_us < _last_sample.rate_timestamp_us
		    || (sample.attitude_sample_us == _last_sample.attitude_sample_us && !same(sample.attitude, _last_sample.attitude, 4))
		    || (sample.rate_sample_us == _last_sample.rate_sample_us && (!same(sample.rate_rad_s, _last_sample.rate_rad_s, 3)
				    || !same(sample.angular_accel_rad_s2, _last_sample.angular_accel_rad_s2, 3)))) { return Status::StateRejected; }
	}

	for (unsigned i = 0; i < 4; ++i) { attitude(i) = static_cast<float>(static_cast<double>(sample.attitude(i)) / norm); }

	return Status::Ok;
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
	_state = State::Hold;
	return Status::Ok;
}

Status StabilizedManualControl::cycle(TimeUs now, const ManualSample &manual, const StateSample &sample, Report &report)
{
	if (!eligible()) { return Status::InvalidState; }

	if (now <= _last_cycle_us || now - _last_cycle_us > _configuration.maximum_cycle_us) { return reject(Status::InvalidTime); }

	Quaternion attitude;
	const Status status = validateState(now, sample, attitude);

	if (status != Status::Ok) { return reject(status); }

	research_actuator_command::RuntimeInput discarded{};
	research_actuator_command::ManualControlReport input_report{};

	if (_manual.evaluate(now, manual, discarded, input_report) != research_actuator_command::ManualControlStatus::Ok) { return reject(Status::InputRejected); }

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
			const float magnitude = ::fmaxf(0.0F, (std::fabs(manual.axes[axis]) - _configuration.centered_enter)
							/ (1.0F - _configuration.centered_enter));
			rate_setpoint(axis) = (manual.axes[axis] < 0.0F ? -magnitude : magnitude)
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
	const Vector3f requested = rate_control.update(sample.rate_rad_s, rate_setpoint, sample.angular_accel_rad_s2, dt_s, false);
	Vector3f bounded{};

	for (unsigned axis = 0; axis < 3; ++axis) {
		if (!std::isfinite(requested(axis))) { return reject(Status::ArithmeticFailure); }

		bounded(axis) = clamp(requested(axis), -_configuration.maximum_normalized_moment(axis), _configuration.maximum_normalized_moment(axis));
	}

	Report candidate{};

	if (!allocateCanards(_configuration.allocation, bounded, _last_command, dt_s, candidate.allocation)) { return reject(Status::AllocationFailure); }

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
	_last_command = candidate.allocation.command;
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
	_state = State::Disabled;
	_fault = Status::Ok;
}

} // namespace research_stabilized_manual
