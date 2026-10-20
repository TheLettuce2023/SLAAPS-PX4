#include "StabilizedOutput.hpp"

#include <cmath>

namespace research_stabilized_manual
{
namespace
{
bool unit(float value)
{
	return std::isfinite(value) && value >= -1.0F && value <= 1.0F;
}

bool pulse(float value, const OutputServoCalibration &servo)
{
	return std::isfinite(value) && value >= servo.driver_minimum_us && value <= servo.driver_maximum_us;
}

bool live(OutputState state)
{
	return state == OutputState::Awaiting || state == OutputState::Active;
}
}

bool validOutputConfiguration(const OutputConfiguration &configuration)
{
	if (configuration.lease_us == 0 || configuration.lease_us > UINT32_MAX
	    || configuration.acknowledgement_us == 0 || configuration.acknowledgement_us >= configuration.lease_us
	    || (configuration.on_fault != OutputFallback::Disarmed && configuration.on_fault != OutputFallback::Calibrated)) {
		return false;
	}

	for (unsigned i = 0; i < 4; ++i) {
		const auto &servo = configuration.servos[i];
		const bool increasing = servo.negative_pwm_us < servo.zero_pwm_us && servo.zero_pwm_us < servo.positive_pwm_us;
		const bool decreasing = servo.negative_pwm_us > servo.zero_pwm_us && servo.zero_pwm_us > servo.positive_pwm_us;

		if (servo.driver_minimum_us == 0 || servo.driver_minimum_us >= servo.driver_center_us
		    || servo.driver_center_us >= servo.driver_maximum_us || (!increasing && !decreasing)
		    || !pulse(servo.negative_pwm_us, servo) || !pulse(servo.zero_pwm_us, servo) || !pulse(servo.positive_pwm_us, servo)
		    || !unit(configuration.minimum[i]) || !unit(configuration.maximum[i])
		    || configuration.minimum[i] >= configuration.maximum[i] || !unit(configuration.fallback[i])
		    || configuration.fallback[i] < configuration.minimum[i] || configuration.fallback[i] > configuration.maximum[i]) {
			return false;
		}
	}

	return true;
}

bool mapStabilizedOutput(const OutputConfiguration &configuration, const CanardVector &command, OutputCandidate &candidate)
{
	if (!validOutputConfiguration(configuration)) { return false; }

	OutputCandidate mapped{};
	mapped.disposition = OutputDisposition::Command;

	for (unsigned i = 0; i < 4; ++i) {
		if (!unit(command[i]) || command[i] < configuration.minimum[i] || command[i] > configuration.maximum[i]) { return false; }

		const auto &servo = configuration.servos[i];
		const float endpoint = command[i] < 0.0F ? servo.negative_pwm_us : servo.positive_pwm_us;
		const float pwm = servo.zero_pwm_us + std::fabs(command[i]) * (endpoint - servo.zero_pwm_us);
		const float delta = pwm - servo.driver_center_us;
		const float span = delta < 0.0F ? servo.driver_center_us - servo.driver_minimum_us
				   : servo.driver_maximum_us - servo.driver_center_us;
		const float control = delta / span;

		if (!pulse(pwm, servo) || !unit(control)) { return false; }

		mapped.pwm_us[i] = pwm;
		mapped.driver_control[i] = control;
	}

	candidate = mapped;
	return true;
}

OutputStatus StabilizedOutputProducer::reject(OutputStatus status)
{
	_state = OutputState::Fault;
	_fault = status;
	return status;
}

OutputStatus StabilizedOutputProducer::begin(TimeUs now, TimeUs session, const OutputConfiguration &configuration,
		bool permitted)
{
	if (_state != OutputState::Disabled) { return reject(OutputStatus::InvalidState); }

	if (!validOutputConfiguration(configuration)) { return reject(OutputStatus::InvalidConfiguration); }

	if (now == 0 || now < _clock || session == 0 || session <= _session || session > now) { return reject(OutputStatus::InvalidTime); }

	if (!permitted) { return reject(OutputStatus::PermissionLost); }

	_configuration = configuration;
	_session = session;
	_clock = now;
	_last_command = now; // Bounds initial silence as well as producer disappearance.
	_sequence = 0;
	_waiting = false;
	_pending = {};
	_state = OutputState::Awaiting;
	_fault = OutputStatus::Ok;
	return OutputStatus::Ok;
}

OutputStatus StabilizedOutputProducer::poll(TimeUs now, bool permitted)
{
	if (_state == OutputState::Fault) { return _fault; }

	if (!live(_state)) { return OutputStatus::InvalidState; }

	if (now < _clock) { return reject(OutputStatus::InvalidTime); }

	_clock = now;

	if (!permitted) { return reject(OutputStatus::PermissionLost); }

	if (now - _last_command >= _configuration.lease_us
	    || (_waiting && now - _pending.timestamp_us >= _configuration.acknowledgement_us)) {
		return reject(OutputStatus::Expired);
	}

	return _waiting ? OutputStatus::Waiting : OutputStatus::Ok;
}

OutputStatus StabilizedOutputProducer::submit(TimeUs now, bool permitted, Status calculation_status, const Report &report,
		OutputFrame &frame)
{
	const OutputStatus status = poll(now, permitted);

	if (status != OutputStatus::Ok && status != OutputStatus::Waiting) { return status; }

	// A control fault always wins over waiting for the preceding acknowledgement.
	if (calculation_status != Status::Ok && calculation_status != Status::NotDue) { return reject(OutputStatus::InvalidCommand); }

	if (calculation_status == Status::NotDue) { return OutputStatus::NoCommand; }

	if (_waiting) { return OutputStatus::Waiting; }

	OutputCandidate discarded{};

	if (report.timestamp_us <= _last_command || report.timestamp_us > now
	    || now - report.timestamp_us >= _configuration.acknowledgement_us
	    || (report.state != State::Rate && report.state != State::Hold)
	    || _sequence == UINT64_MAX
	    || !mapStabilizedOutput(_configuration, report.allocation.command, discarded)) {
		return reject(OutputStatus::InvalidCommand);
	}

	_pending.session = _session;
	_pending.timestamp_us = report.timestamp_us;
	_pending.sequence = ++_sequence;
	_pending.command = report.allocation.command;
	_last_command = report.timestamp_us;
	_waiting = true;
	frame = _pending;
	return OutputStatus::Ok;
}

OutputStatus StabilizedOutputProducer::acknowledge(TimeUs now, bool permitted, const OutputAcknowledgement &acknowledgement)
{
	const OutputStatus status = poll(now, permitted);

	if (status != OutputStatus::Ok && status != OutputStatus::Waiting) { return status; }

	if (!_waiting || acknowledgement.session != _session || acknowledgement.sequence != _pending.sequence
	    || acknowledgement.command_timestamp_us != _pending.timestamp_us
	    || acknowledgement.accepted_us < _pending.timestamp_us || acknowledgement.accepted_us > now) {
		return reject(OutputStatus::InvalidAcknowledgement);
	}

	_waiting = false;
	_state = OutputState::Active;
	return OutputStatus::Ok;
}

void StabilizedOutputProducer::disable()
{
	_state = OutputState::Disabled;
	_waiting = false;
	_pending = {};
	_fault = OutputStatus::Ok;
	// Preserve clock and session high-water marks across explicit recovery.
}

OutputStatus StabilizedOutputConsumer::reject(OutputStatus status)
{
	_state = OutputState::Fault;
	_fault = status;
	return status;
}

OutputStatus StabilizedOutputConsumer::begin(TimeUs now, TimeUs session, const OutputConfiguration &configuration,
		bool permitted)
{
	if (_state != OutputState::Disabled) { return reject(OutputStatus::InvalidState); }

	if (!validOutputConfiguration(configuration)) { return reject(OutputStatus::InvalidConfiguration); }

	if (now == 0 || now < _clock || session == 0 || session <= _session || session > now) { return reject(OutputStatus::InvalidTime); }

	if (!permitted) { return reject(OutputStatus::PermissionLost); }

	_configuration = configuration;
	_session = session;
	_clock = now;
	_last_command = now;
	_sequence = 0;
	_candidate = {};
	_inhibited = false;
	_state = OutputState::Awaiting;
	_fault = OutputStatus::Ok;
	return OutputStatus::Ok;
}

OutputStatus StabilizedOutputConsumer::poll(TimeUs now, bool permitted)
{
	// A later permission loss must also suppress fallback after an earlier fault.
	if (!permitted) { _inhibited = true; }

	if (_state == OutputState::Fault) { return _fault; }

	if (!live(_state)) { return OutputStatus::InvalidState; }

	if (now < _clock) { return reject(OutputStatus::InvalidTime); }

	_clock = now;

	if (!permitted) { return reject(OutputStatus::PermissionLost); }

	if (now - _last_command >= _configuration.lease_us) { return reject(OutputStatus::Expired); }

	return OutputStatus::Ok;
}

OutputStatus StabilizedOutputConsumer::accept(TimeUs now, bool permitted, const OutputFrame &frame,
		OutputAcknowledgement &acknowledgement)
{
	const OutputStatus status = poll(now, permitted);

	if (status != OutputStatus::Ok) { return status; }

	OutputCandidate mapped{};

	if (frame.session != _session || _sequence == UINT64_MAX
	    || frame.sequence != _sequence + 1 || frame.timestamp_us <= _last_command || frame.timestamp_us > now
	    || now - frame.timestamp_us >= _configuration.acknowledgement_us
	    || !mapStabilizedOutput(_configuration, frame.command, mapped)) {
		return reject(OutputStatus::InvalidCommand);
	}

	_candidate = mapped;
	_last_command = frame.timestamp_us;
	_sequence = frame.sequence;
	_state = OutputState::Active;
	acknowledgement = {_session, frame.timestamp_us, now, frame.sequence};
	return OutputStatus::Ok;
}

OutputCandidate StabilizedOutputConsumer::sample(TimeUs now, bool permitted)
{
	poll(now, permitted);

	if (_state == OutputState::Active) { return _candidate; }

	OutputCandidate fallback{};

	// No valid command yet means disarmed, including failure during begin().
	if (_state == OutputState::Fault && !_inhibited && _sequence > 0 && _configuration.on_fault == OutputFallback::Calibrated
	    && mapStabilizedOutput(_configuration, _configuration.fallback, fallback)) {
		fallback.disposition = OutputDisposition::Fallback;
	}

	return fallback;
}

void StabilizedOutputConsumer::disable()
{
	_state = OutputState::Disabled;
	_candidate = {};
	_sequence = 0;
	_inhibited = true;
	_fault = OutputStatus::Ok;
}

} // namespace research_stabilized_manual
