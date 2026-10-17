#include "StateCaptureProducer.hpp"

#include <limits>

namespace research_sitl
{
namespace state_capture
{
namespace
{
constexpr uint64_t MaximumCounter = std::numeric_limits<uint64_t>::max();
constexpr uint32_t ReservedNavMask = (uint32_t{1} << 9) | (uint32_t{1} << 11) | (uint32_t{1} << 16);
}

bool sameState(State a, State b) noexcept
{
	return a.arming_state == b.arming_state && a.nav_state == b.nav_state;
}

bool validState(State state) noexcept
{
	return (state.arming_state == 1 || state.arming_state == 2) && state.nav_state < 31
	       && (ReservedNavMask & (uint32_t{1} << state.nav_state)) == 0;
}

bool Bootstrap::configure(const Identities &identities) noexcept
{
	const std::lock_guard<std::mutex> lock(mutex_);

	if (configured_ || claimed_ || faulted_ || identities.producer_lifetime_id == 0
	    || identities.observer_lifetime_id == 0 || identities.clock_epoch == 0) {
		faulted_ = true;
		return false;
	}

	identities_ = identities;
	configured_ = true;
	return true;
}

bool Bootstrap::claim(Identities &identities) noexcept
{
	const std::lock_guard<std::mutex> lock(mutex_);

	if (!configured_ || claimed_ || faulted_) {
		faulted_ = true;
		return false;
	}

	claimed_ = true;
	identities = identities_;
	return true;
}

bool Bootstrap::healthy() const noexcept
{
	const std::lock_guard<std::mutex> lock(mutex_);
	return configured_ && claimed_ && !faulted_;
}

void Producer::fail(Fault fault) noexcept
{
	if (summary_.first_fault == Fault::None && !summary_.finished) { summary_.first_fault = fault; }
}

bool Producer::active() noexcept
{
	if (summary_.finished || summary_.first_fault != Fault::None) { return false; }

	if (!bootstrap_.healthy()) { fail(Fault::BootstrapUnavailable); return false; }

	if (pending_) { fail(Fault::PublicationPending); return false; }

	return true;
}

bool Producer::checkTime(uint64_t now_us) noexcept
{
	if (now_us == 0) { fail(Fault::TimeInvalid); return false; }

	if (now_us < checked_time_us_) { fail(Fault::TimeRegressed); return false; }

	checked_time_us_ = now_us;
	return true;
}

std::optional<Receipt> Producer::prepare(Kind kind, State before, State after, uint64_t now_us) noexcept
{
	if (summary_.attempted_sequence == MaximumCounter
	    || (kind == Kind::Transition && summary_.transition_epoch == MaximumCounter)) {
		fail(Fault::CounterExhausted);
		return std::nullopt;
	}

	++summary_.attempted_sequence;

	if (kind == Kind::Initial) { summary_.transition_epoch = 1; }

	else if (kind == Kind::Transition) { ++summary_.transition_epoch; }

	summary_.has_state = true;
	summary_.retained_state = after;
	summary_.actual_state = after;
	summary_.producer_time_us = now_us;
	pending_ = true;
	return Receipt{summary_.identities.producer_lifetime_id, summary_.identities.clock_epoch,
		       summary_.attempted_sequence, summary_.transition_epoch, now_us, kind, before, after};
}

std::optional<Receipt> Producer::initial(State actual, uint64_t now_us) noexcept
{
	if (summary_.finished || summary_.first_fault != Fault::None) { return std::nullopt; }

	if (started_) { fail(Fault::AlreadyStarted); return std::nullopt; }

	started_ = true;

	if (!bootstrap_.claim(summary_.identities)) { fail(Fault::BootstrapUnavailable); return std::nullopt; }

	if (!validState(actual)) { fail(Fault::InvalidState); return std::nullopt; }

	if (!checkTime(now_us)) { return std::nullopt; }

	return prepare(Kind::Initial, actual, actual, now_us);
}

std::optional<Receipt> Producer::transition(State before, State after, uint64_t now_us) noexcept
{
	if (!active()) { return std::nullopt; }

	if (!summary_.has_state) { fail(Fault::BootstrapUnavailable); return std::nullopt; }

	if (!validState(before) || !validState(after)) { fail(Fault::InvalidState); return std::nullopt; }

	if (!checkTime(now_us)) { return std::nullopt; }

	if (!sameState(before, summary_.retained_state)) { fail(Fault::StateContradiction); return std::nullopt; }

	if (sameState(before, after)) { return std::nullopt; }

	return prepare(Kind::Transition, before, after, now_us);
}

std::optional<Receipt> Producer::sample(State actual, uint64_t now_us) noexcept
{
	if (!active()) { return std::nullopt; }

	if (!summary_.has_state) { fail(Fault::BootstrapUnavailable); return std::nullopt; }

	if (!validState(actual)) { fail(Fault::InvalidState); return std::nullopt; }

	if (!checkTime(now_us)) { return std::nullopt; }

	if (!sameState(actual, summary_.retained_state)) { fail(Fault::StateContradiction); return std::nullopt; }

	return prepare(Kind::Sample, actual, actual, now_us);
}

void Producer::completePublication(bool success) noexcept
{
	if (summary_.finished) { return; }

	if (!pending_) { fail(Fault::PublicationPending); return; }

	pending_ = false;

	if (!success) { fail(Fault::PublishFailed); return; }

	summary_.emitted_sequence = summary_.attempted_sequence;
	summary_.emitted_state = summary_.retained_state;
	summary_.emitted_transition_epoch = summary_.transition_epoch;
	summary_.emitted_producer_time_us = summary_.producer_time_us;
}

Summary Producer::finish(State actual, uint64_t now_us) noexcept
{
	if (summary_.finished) { return summary_; }

	if (!bootstrap_.healthy() || !started_) { fail(Fault::BootstrapUnavailable); }

	if (pending_) { fail(Fault::PublicationPending); }

	if (!validState(actual)) { fail(Fault::InvalidState); }

	checkTime(now_us);

	if (summary_.has_state && !sameState(actual, summary_.retained_state)) { fail(Fault::StateContradiction); }

	summary_.actual_state = actual;
	summary_.final_time_us = now_us;
	summary_.finished = true;
	return summary_;
}

const char *kindName(Kind kind) noexcept
{
	switch (kind) {
	case Kind::Initial: return "Initial";

	case Kind::Transition: return "Transition";

	case Kind::Sample: return "Sample";
	}

	return nullptr;
}

const char *faultName(Fault fault) noexcept
{
	switch (fault) {
	case Fault::None: return "None";

	case Fault::InvalidIdentity: return "InvalidIdentity";

	case Fault::BootstrapUnavailable: return "BootstrapUnavailable";

	case Fault::AlreadyStarted: return "AlreadyStarted";

	case Fault::InvalidState: return "InvalidState";

	case Fault::TimeInvalid: return "TimeInvalid";

	case Fault::TimeRegressed: return "TimeRegressed";

	case Fault::StateContradiction: return "StateContradiction";

	case Fault::CounterExhausted: return "CounterExhausted";

	case Fault::PublishFailed: return "PublishFailed";

	case Fault::PublicationPending: return "PublicationPending";
	}

	return nullptr;
}

} // namespace state_capture
} // namespace research_sitl
