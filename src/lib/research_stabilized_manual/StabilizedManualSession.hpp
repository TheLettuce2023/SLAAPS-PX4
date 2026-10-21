#pragma once

#include "StabilizedOutput.hpp"

namespace research_stabilized_manual
{

struct BoundOutputAcknowledgement {
	OutputAcknowledgement value{};
	uint32_t source_revision{}; // Trusted metadata copied atomically with value.
};

struct SessionUpdate {
	OutputStatus status{OutputStatus::InvalidState};
	Status control_status{Status::Ok};
	bool acknowledged{};
};

// Serialized producer/controller lifecycle. No transport, scheduler or grant
// authority: begin is allowed only after the supervisor confirms consumer
// ownership with the same configuration/session and expected ack lifetime.
class StabilizedManualSession
{
public:
	StabilizedManualSession() = default;
	StabilizedManualSession(const StabilizedManualSession &) = delete;
	StabilizedManualSession &operator=(const StabilizedManualSession &) = delete;
	OutputStatus begin(TimeUs now, TimeUs session, const Configuration &control, const OutputConfiguration &output,
			   const ManualSample &manual, const StateSample &sample, bool permitted,
			   uint32_t expected_ack_revision, uint32_t current_ack_revision);
	SessionUpdate update(TimeUs now, bool permitted, const ManualSample &manual, const StateSample &sample,
			     uint32_t current_ack_revision, const BoundOutputAcknowledgement *ack,
			     Report &report, OutputFrame &frame);
	void disable();
	OutputStatus fault() const { return _fault; }
	bool acknowledgementSourceFault() const { return _source_fault; }
	bool active() const { return _active; }
	State controlState() const { return _core.state(); }

private:
	SessionUpdate reject(OutputStatus reason, Status control_status = Status::Ok);
	StabilizedManualControl _core{};
	StabilizedOutputProducer _producer{};
	TimeUs _last_calculation{};
	uint32_t _ack_revision{};
	OutputStatus _fault{OutputStatus::Ok};
	Status _control_status{Status::Ok};
	bool _active{}, _source_fault{};
};

} // namespace research_stabilized_manual
