#include "StabilizedOutputDriver.hpp"

namespace research_stabilized_manual
{

OutputStatus StabilizedOutputDriver::begin(TimeUs now, TimeUs session, const OutputConfiguration &configuration,
		bool permitted)
{
	// A competing grant cannot replace or reset an existing session.
	if (_state != DriverState::Disabled || !_disarmed_confirmed || _pending_ticket || _exhausted) {
		return OutputStatus::InvalidState;
	}

	if (now < _clock) { return OutputStatus::InvalidTime; }

	const OutputStatus result = _consumer.begin(now, session, configuration, permitted);

	if (result != OutputStatus::Ok) { _state = DriverState::Fault; return result; }

	_configuration = configuration;
	_clock = now;
	_last_frame = now;
	_disarmed_confirmed = false;
	_sink_fault = false;
	_state = DriverState::Active;
	return OutputStatus::Ok;
}

void StabilizedOutputDriver::revoke()
{
	_consumer.disable();
	_state = DriverState::Revoking;
	_disarmed_confirmed = false;
	_invalidated = _pending_ticket != 0;
	// A successful disarmed dispatch clears this; a request alone cannot.
}

bool StabilizedOutputDriver::prepare(TimeUs now, bool permitted, const OutputFrame *frame, DriverWrite &write)
{
	write = {};

	if (_pending_ticket) { return false; }

	_prepared = {};
	_accepted = false;
	_invalidated = false;

	if (_issued_ticket == UINT64_MAX) {
		// Exhaustion permanently forbids authoritative acknowledgments. Keep
		// retrying disarm with the terminal token, never wrap into an old ID.
		_exhausted = true;
		_consumer.disable();
		_state = DriverState::Fault;
		_sink_fault = true;
		_disarmed_confirmed = false;

	} else { ++_issued_ticket; }

	if (now < _clock) {
		_consumer.disable();
		_state = DriverState::Fault;
		_sink_fault = true;
		_disarmed_confirmed = false;
	}

	if (now >= _clock) { _clock = now; }

	if ((_state == DriverState::Active || _state == DriverState::Fault) && !_sink_fault) {
		if (frame && _state == DriverState::Active) {
			_accepted = _consumer.accept(now, permitted, *frame, _prepared.acknowledgement) == OutputStatus::Ok;

			if (_accepted) { _last_frame = frame->timestamp_us; }
		}

		_prepared.candidate = _consumer.sample(now, permitted);

		if (_consumer.state() == OutputState::Fault) { _state = DriverState::Fault; }
	}

	_pending_ticket = _issued_ticket;
	write = {_pending_ticket, _prepared.candidate};
	return true;
}

bool StabilizedOutputDriver::cancel(uint64_t ticket)
{
	if (!ticket || ticket != _pending_ticket) { return false; }

	_pending_ticket = 0;
	_consumer.disable();
	_sink_fault = true;
	_disarmed_confirmed = false;

	if (_state != DriverState::Revoking) { _state = DriverState::Fault; }

	return true;
}

DriverDispatch StabilizedOutputDriver::complete(uint64_t ticket, const OutputWriteResult &write, bool permitted)
{
	// A delayed/duplicate callback cannot complete a newer prepared write or
	// consume its acknowledgment. The caller must cancel a matching abandoned
	// write before preparing another.
	if (!ticket || ticket != _pending_ticket) { return {}; }

	DriverDispatch result = _prepared;
	_pending_ticket = 0;
	const bool clock_valid = write.completed_us >= _clock;
	result.handled = write.handled && clock_valid;

	if (clock_valid) { _clock = write.completed_us; }

	if (!result.handled || _invalidated || _exhausted
	    || (!permitted && result.candidate.disposition != OutputDisposition::Disarmed)) {
		_consumer.disable();
		_sink_fault = true;
		_disarmed_confirmed = false;

		// Continue trying disarmed writes after rejection, but require explicit
		// revoke before any new session. No successful command ack escapes.
		if (_state != DriverState::Revoking) { _state = DriverState::Fault; }

		result.acknowledgement = {};
		return result;
	}

	if (_state == DriverState::Revoking) {
		_state = DriverState::Disabled;
		_disarmed_confirmed = true;
		_sink_fault = false;
		result.revoke_acknowledged = true;

	} else if (_state == DriverState::Active) {
		// Recheck after a potentially slow driver. No late command acceptance
		// may extend a lease or masquerade as a timely acknowledgement.
		const auto after = _consumer.sample(write.completed_us, permitted);

		if (after.disposition != result.candidate.disposition
		    || (_accepted && write.completed_us - _last_frame >= _configuration.acknowledgement_us)) {
			_consumer.disable();
			_state = DriverState::Fault;
			_sink_fault = true; // Next independent dispatch must request disarm.

		} else if (_accepted) {
			result.command_acknowledged = true;
			result.acknowledgement.accepted_us = write.completed_us;
		}
	}

	if (!result.command_acknowledged) { result.acknowledgement = {}; }

	return result;
}

DriverDispatch StabilizedOutputDriver::dispatch(TimeUs now, bool permitted, const OutputFrame *frame,
		StabilizedOutputSink &sink)
{
	DriverWrite write{};

	if (!prepare(now, permitted, frame, write)) { return {}; }

	return complete(write.ticket, sink.write(write.candidate), permitted);
}

} // namespace research_stabilized_manual
