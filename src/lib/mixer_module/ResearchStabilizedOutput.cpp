#include "ResearchStabilizedOutput.hpp"

#include <cmath>

using namespace research_stabilized_manual;

bool ResearchStabilizedOutput::attach()
{
	if (_attached || !_mixer.attachResearchTransaction(*this)) { return false; }

	_channels = _mixer.maxNumOutputs();
	_attached = true;
	return true;
}

bool ResearchStabilizedOutput::bankMatches(const OutputConfiguration &configuration) const
{
	if (_mixer.maxNumOutputs() != _channels || _mixer.reversedOutputs() != 0) { return false; }

	for (unsigned i = 0; i < _channels; ++i) {
		if (_mixer.disarmedValue(i) != 0 || std::fabs(_mixer.actualFailsafeValue(i)) > kPwmMatchToleranceUs
		    || !std::isfinite(_mixer.actualFailsafeValue(i))) { return false; }

		if (i < configuration.servos.size()) {
			const auto &servo = configuration.servos[i];

			if (_mixer.outputFunction(i) != static_cast<OutputFunction>(static_cast<int>(OutputFunction::Research_Stabilized1) + i)
			    || _mixer.minValue(i) != servo.driver_minimum_us
			    || _mixer.centerValue(i) != servo.driver_center_us
			    || _mixer.maxValue(i) != servo.driver_maximum_us) { return false; }

		} else if (_mixer.outputFunction(i) != OutputFunction::Disabled) { return false; }
	}

	return true;
}

OutputStatus ResearchStabilizedOutput::begin(hrt_abstime now, const ResearchOutputGrant &request,
		const OutputConfiguration &configuration)
{
	if (!_attached || _pending || _result_ready || _driver.state() != DriverState::Disabled
	    || !request.source_revision || _source.sourceRevision() != request.source_revision) { return OutputStatus::InvalidState; }

	if (!request.session || request.session > now || request.deadline_us <= request.session) { return OutputStatus::InvalidTime; }

	if (now >= request.deadline_us) { return OutputStatus::Expired; }

	if (!validOutputConfiguration(configuration) || !bankMatches(configuration)) { return OutputStatus::InvalidConfiguration; }

	const auto status = _driver.begin(now, request.session, configuration, _permission.outputPermitted(now));

	if (status == OutputStatus::Ok) {
		_configuration = configuration;
		_source_revision = request.source_revision;
		_source_fault = false;
	}

	return status;
}

bool ResearchStabilizedOutput::prepare(hrt_abstime now, const OutputFrame *frame, unsigned frame_revision,
				       OutputCandidate &candidate)
{
	if (!_attached || _pending || _result_ready) { return false; }

	if (_source_revision && frame && frame_revision != _source_revision) { invalidateSource(); }

	const bool source_valid = sourceMatches();

	if (!_driver.prepare(now, source_valid && _permission.outputPermitted(now), source_valid ? frame : nullptr,
			     _write)) { return false; }

	_pending = true;
	_validated = false;
	_new_command = source_valid && frame && _write.candidate.disposition == OutputDisposition::Command;

	if (_new_command) { _last_command = frame->timestamp_us; }

	candidate = _write.candidate;
	return true;
}

float ResearchStabilizedOutput::control(unsigned channel) const
{
	if (!_pending || !_driver.pending(_write.ticket) || _write.candidate.disposition == OutputDisposition::Disarmed
	    || channel >= _write.candidate.driver_control.size()) { return NAN; }

	return _write.candidate.driver_control[channel];
}

bool ResearchStabilizedOutput::validate(const float *outputs, unsigned count, const actuator_armed_s &armed)
{
	_validated = false;
	const bool source_valid = sourceMatches();

	if (!_attached || !_pending || !_driver.pending(_write.ticket) || count != _channels || !outputs) { return false; }

	const bool disarmed = _write.candidate.disposition == OutputDisposition::Disarmed;

	if (!disarmed) {
		const auto now = hrt_absolute_time();

		// A queued candidate may expire before the mixer reaches its driver.
		// Check the calculation time, never preparation or receipt time.
		if (_write.candidate.disposition == OutputDisposition::Command
		    && (now < _last_command || now - _last_command >= _configuration.lease_us
			|| (_new_command && now - _last_command >= _configuration.acknowledgement_us))) { return false; }

		if (!source_valid || !_permission.outputPermitted(now) || !bankMatches(_configuration)
		    || !armed.timestamp || armed.timestamp > now || now - armed.timestamp >= _configuration.acknowledgement_us
		    || !(armed.armed || armed.prearmed) || armed.kill || armed.lockdown || armed.termination
		    || armed.in_esc_calibration_mode || _mixer.isActuatorTestRunning()) { return false; }
	}

	for (unsigned i = 0; i < count; ++i) {
		const float expected = !disarmed && i < _write.candidate.pwm_us.size() ? _write.candidate.pwm_us[i] : 0.0F;

		if (!std::isfinite(outputs[i]) || std::fabs(outputs[i] - expected) > kPwmMatchToleranceUs) { return false; }
	}

	_validated = true;
	return true;
}

void ResearchStabilizedOutput::complete(bool handled, hrt_abstime completed)
{
	if (!_pending) { return; }

	const bool permitted = sourceMatches() && _permission.outputPermitted(completed);
	_result.ticket = _write.ticket;
	_result.candidate_valid = _validated;
	const bool accepted = handled && _validated;
	_result.dispatch = _driver.complete(_write.ticket, {completed, accepted},
					    permitted);
	_pending = false;
	_result_ready = true;
}

bool ResearchStabilizedOutput::takeResult(ResearchMixerResult &result)
{
	if (!_result_ready) { return false; }

	(void)sourceMatches();
	result = _result;
	_result = {};
	_result_ready = false;
	return true;
}

bool ResearchStabilizedOutput::cancel()
{
	if (!_pending || !_driver.cancel(_write.ticket)) { return false; }

	_pending = false;
	_validated = false;
	return true;
}

void ResearchStabilizedOutput::revoke()
{
	_source_revision = 0;
	_driver.revoke();
	// A completed result waiting for collection cannot grant authority after
	// this newer revoke. Its historical handled flag remains diagnostic only.
	_result.dispatch.command_acknowledged = false;
	_result.dispatch.revoke_acknowledged = false;
	_result.dispatch.acknowledgement = {};
}

void ResearchStabilizedOutput::invalidateSource()
{
	_source_fault = true;
	revoke();
}

bool ResearchStabilizedOutput::sourceMatches()
{
	if (!_source_revision) { return false; }

	if (_source.sourceRevision() != _source_revision) {
		invalidateSource();
		return false;
	}

	return true;
}
