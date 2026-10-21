#pragma once

#include "StabilizedOutput.hpp"

namespace research_stabilized_manual
{

struct OutputWriteResult {
	TimeUs completed_us{}; // Same monotonic clock, sampled AFTER the sink call.
	bool handled{}; // Software driver acceptance, never electrical feedback.
};

class StabilizedOutputSink
{
public:
	virtual ~StabilizedOutputSink() = default;
	virtual OutputWriteResult write(const OutputCandidate &candidate) = 0;
};

enum class DriverState : uint8_t { Revoking, Disabled, Active, Fault };
struct DriverDispatch {
	OutputCandidate candidate{};
	OutputAcknowledgement acknowledgement{};
	bool handled{};
	bool command_acknowledged{};
	bool revoke_acknowledged{};
};
struct DriverWrite {
	uint64_t ticket{};
	OutputCandidate candidate{};
};

// Single serialized consumer-side owner. No grant is accepted until the sink
// has handled a disarmed write. revoke() invalidates that confirmation and must
// itself reach the sink before another begin(). Dispatch independently of the
// producer, including after a fault. Callers must retain this owner across
// producer replacement and prevent all other writers to the selected channels.
class StabilizedOutputDriver
{
public:
	StabilizedOutputDriver() = default;
	StabilizedOutputDriver(const StabilizedOutputDriver &) = delete;
	StabilizedOutputDriver &operator=(const StabilizedOutputDriver &) = delete;
	OutputStatus begin(TimeUs now, TimeUs session, const OutputConfiguration &configuration, bool permitted);
	// Split boundary for a mixer that prepares values before calling its
	// physical driver. Only one write may be pending. All methods stay on one
	// serialized owner context; this does not add cross-thread synchronization.
	bool prepare(TimeUs now, bool permitted, const OutputFrame *frame, DriverWrite &write);
	// Validate immediately before starting a deferred write. A revoke makes a
	// prepared command invalid; an already-started write cannot be undone.
	bool pending(uint64_t ticket) const { return ticket && ticket == _pending_ticket && !_invalidated; }
	DriverDispatch complete(uint64_t ticket, const OutputWriteResult &write, bool permitted);
	bool cancel(uint64_t ticket); // No acknowledgment; next dispatch must disarm.
	DriverDispatch dispatch(TimeUs now, bool permitted, const OutputFrame *frame, StabilizedOutputSink &sink);
	void revoke();
	DriverState state() const { return _state; }
	bool disarmedConfirmed() const { return _disarmed_confirmed; }

private:
	StabilizedOutputConsumer _consumer{};
	OutputConfiguration _configuration{};
	TimeUs _clock{};
	TimeUs _last_frame{};
	DriverState _state{DriverState::Revoking};
	bool _disarmed_confirmed{};
	bool _sink_fault{};
	DriverDispatch _prepared{};
	uint64_t _issued_ticket{};
	uint64_t _pending_ticket{};
	bool _accepted{};
	bool _invalidated{};
	bool _exhausted{};
};

} // namespace research_stabilized_manual
