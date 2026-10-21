#include "StabilizedManualSession.hpp"

namespace research_stabilized_manual
{

SessionUpdate StabilizedManualSession::reject(OutputStatus reason, Status control_status)
{
	_core.disable();
	_producer.disable(); // Keeps protocol clock/session high-water marks.
	_active = false;
	_fault = reason;
	_control_status = control_status;
	return {reason, control_status, false};
}

OutputStatus StabilizedManualSession::begin(TimeUs now, TimeUs session, const Configuration &control,
		const OutputConfiguration &output, const ManualSample &manual, const StateSample &sample, bool permitted,
		uint32_t expected_ack_revision, uint32_t current_ack_revision)
{
	if (_active || _fault != OutputStatus::Ok) { return OutputStatus::InvalidState; }

	if (!expected_ack_revision || current_ack_revision != expected_ack_revision) {
		_source_fault = true;
		return reject(OutputStatus::InvalidAcknowledgement).status;
	}

	if (!validOutputConfiguration(output)) { return reject(OutputStatus::InvalidConfiguration).status; }

	for (unsigned i = 0; i < output.servos.size(); ++i) {
		if (output.minimum[i] > control.allocation.minimum[i] || output.maximum[i] < control.allocation.maximum[i]) {
			return reject(OutputStatus::InvalidConfiguration).status;
		}
	}

	const Status configured = _core.configure(control);

	if (configured != Status::Ok) { return reject(OutputStatus::InvalidConfiguration, configured).status; }

	if (!permitted) { return reject(OutputStatus::PermissionLost).status; }

	const Status enabled = _core.enable(now, manual, sample);

	if (enabled != Status::Ok) { return reject(OutputStatus::InvalidCommand, enabled).status; }

	const auto granted = _producer.begin(now, session, output, permitted);

	if (granted != OutputStatus::Ok) { return reject(granted).status; }

	_ack_revision = expected_ack_revision;
	_last_calculation = now;
	_control_status = Status::Ok;
	_active = true;
	_source_fault = false;
	return OutputStatus::Ok;
}

SessionUpdate StabilizedManualSession::update(TimeUs now, bool permitted, const ManualSample &manual,
		const StateSample &sample, uint32_t current_ack_revision, const BoundOutputAcknowledgement *ack,
		Report &report, OutputFrame &frame)
{
	if (!_active) { return {_fault == OutputStatus::Ok ? OutputStatus::InvalidState : _fault, _control_status, false}; }

	if (!current_ack_revision || current_ack_revision != _ack_revision || (ack && ack->source_revision != _ack_revision)) {
		_source_fault = true;
		return reject(OutputStatus::InvalidAcknowledgement);
	}

	auto ready = _producer.poll(now, permitted);

	if (ready != OutputStatus::Ok && ready != OutputStatus::Waiting) { return reject(ready); }

	const Status health = _core.monitor(now, manual, sample);

	if (health != Status::Ok) { return reject(OutputStatus::InvalidCommand, health); }

	bool acknowledged = false;

	if (ack) {
		ready = _producer.acknowledge(now, permitted, ack->value);

		if (ready != OutputStatus::Ok) { return reject(ready); }

		acknowledged = true;
	}

	if (ready == OutputStatus::Waiting) { return {OutputStatus::Waiting, Status::Ok, false}; }

	// A zero-latency ack may arrive at the calculation's clock value. Accept
	// it without running a second integration at the same instant.
	if (now <= _last_calculation) { return {OutputStatus::NoCommand, Status::NotDue, acknowledged}; }

	Report candidate{};
	OutputFrame next{};
	const Status calculated = _core.cycle(now, manual, sample, candidate);
	const auto submitted = _producer.submit(now, permitted, calculated, candidate, next);

	if (submitted == OutputStatus::NoCommand) { return {submitted, calculated, acknowledged}; }

	if (submitted != OutputStatus::Ok) { return reject(submitted, calculated); }

	_last_calculation = now;
	report = candidate;
	frame = next;
	return {OutputStatus::Ok, calculated, acknowledged};
}

void StabilizedManualSession::disable()
{
	_core.disable();
	_producer.disable();
	_active = false;
	_ack_revision = 0;
	_fault = OutputStatus::Ok;
	_control_status = Status::Ok;
	_source_fault = false;
}

} // namespace research_stabilized_manual
