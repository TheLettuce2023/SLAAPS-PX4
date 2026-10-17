#include "StateReceiptChecker.hpp"

#include <limits>

namespace research_sitl
{
namespace state_receipts
{
namespace
{
// Pinned VehicleStatus.msg values; this host slice has no PX4 dependency.
constexpr uint8_t kDisarmed = 1;
constexpr uint8_t kArmed = 2;
constexpr uint8_t kNavStateLimit = 31;
constexpr uint32_t kReservedNavMask = (uint32_t{1} << 9) | (uint32_t{1} << 11)
				      | (uint32_t{1} << 16) | (uint32_t{1} << kNavStateLimit);
constexpr uint64_t kFirstCounter = 1;
constexpr uint64_t kMaximumCounter = std::numeric_limits<uint64_t>::max();

bool sameState(const State &a, const State &b) noexcept
{
	return a.arming_state == b.arming_state && a.nav_state == b.nav_state;
}

bool knownKind(ReceiptKind kind) noexcept
{
	return kind == ReceiptKind::Initial || kind == ReceiptKind::Transition || kind == ReceiptKind::Sample;
}
} // namespace

ReceiptChecker::ReceiptChecker(const Config &config) noexcept : config_(config)
{
	if (config_.expected_producer_lifetime_id == 0 || config_.expected_observer_lifetime_id == 0
	    || config_.expected_clock_epoch == 0 || config_.max_age_us == 0 || config_.allowed_nav_mask == 0
	    || (config_.allowed_nav_mask & kReservedNavMask) != 0) {
		fail(FaultReason::InvalidConfig);
	}
}

bool ReceiptChecker::validState(const State &state) const noexcept
{
	return (state.arming_state == kDisarmed || state.arming_state == kArmed)
	       && state.nav_state < kNavStateLimit
	       && (config_.allowed_nav_mask & (uint32_t{1} << state.nav_state)) != 0;
}

FaultReason ReceiptChecker::localTimeFault(uint64_t now_us) const noexcept
{
	if (now_us == 0) { return FaultReason::TimeInvalid; }

	if (now_us < snapshot_.local_time_us) { return FaultReason::TimeRegressed; }

	return FaultReason::None;
}

Snapshot ReceiptChecker::fail(FaultReason reason) noexcept
{
	if (snapshot_.code != Code::Faulted) {
		snapshot_.code = Code::Faulted;
		snapshot_.first_fault = reason;
		snapshot_.age_valid = false;
		snapshot_.age_us = 0;
	}

	return snapshot_;
}

Snapshot ReceiptChecker::commitTime(uint64_t now_us) noexcept
{
	snapshot_.local_time_us = now_us;
	snapshot_.age_valid = snapshot_.has_state;
	snapshot_.age_us = snapshot_.has_state ? now_us - snapshot_.producer_time_us : 0;
	snapshot_.code = !snapshot_.has_state ? Code::AwaitingInitial
			 : snapshot_.age_us <= config_.max_age_us ? Code::Fresh : Code::Stale;
	return snapshot_;
}

Snapshot ReceiptChecker::observe(const Observation &observation) noexcept
{
	if (snapshot_.code == Code::Faulted) { return snapshot_; }

	if (observation.observer_lifetime_id != config_.expected_observer_lifetime_id) {
		return fail(FaultReason::ObserverLifetimeMismatch);
	}

	if (!observation.clock_qualified) { return fail(FaultReason::ClockUnqualified); }

	const auto time_fault = localTimeFault(observation.receive_time_us);

	if (time_fault != FaultReason::None) { return fail(time_fault); }

	const auto &receipt = observation.receipt;

	if (receipt.producer_lifetime_id != config_.expected_producer_lifetime_id) {
		return fail(FaultReason::ProducerLifetimeMismatch);
	}

	if (receipt.clock_epoch != config_.expected_clock_epoch) { return fail(FaultReason::ClockEpochMismatch); }

	if (!knownKind(receipt.kind)) { return fail(FaultReason::InvalidKind); }

	if (!validState(receipt.before) || !validState(receipt.after)) { return fail(FaultReason::InvalidState); }

	if (receipt.producer_time_us == 0 || receipt.producer_time_us > observation.receive_time_us) {
		return fail(FaultReason::TimeInvalid);
	}

	if (snapshot_.has_state && receipt.producer_time_us < snapshot_.producer_time_us) {
		return fail(FaultReason::TimeRegressed);
	}

	if (!snapshot_.has_state) {
		if (receipt.kind != ReceiptKind::Initial) { return fail(FaultReason::MissingInitial); }

		if (receipt.sequence != kFirstCounter) { return fail(FaultReason::ReceiptContinuity); }

		if (receipt.transition_epoch != kFirstCounter) { return fail(FaultReason::TransitionEpochContradiction); }

		if (!sameState(receipt.before, receipt.after)) { return fail(FaultReason::StateContradiction); }

	} else {
		if (receipt.kind == ReceiptKind::Initial) { return fail(FaultReason::RepeatedInitial); }

		if (snapshot_.receipt_sequence == kMaximumCounter) { return fail(FaultReason::CounterExhausted); }

		if (receipt.sequence != snapshot_.receipt_sequence + kFirstCounter) { return fail(FaultReason::ReceiptContinuity); }

		if (receipt.kind == ReceiptKind::Transition) {
			if (!sameState(receipt.before, snapshot_.state) || sameState(receipt.before, receipt.after)) {
				return fail(FaultReason::StateContradiction);
			}

			if (snapshot_.transition_epoch == kMaximumCounter) { return fail(FaultReason::CounterExhausted); }

			if (receipt.transition_epoch != snapshot_.transition_epoch + kFirstCounter) {
				return fail(FaultReason::TransitionEpochContradiction);
			}

		} else {
			if (!sameState(receipt.before, receipt.after) || !sameState(receipt.after, snapshot_.state)) {
				return fail(FaultReason::StateContradiction);
			}

			if (receipt.transition_epoch != snapshot_.transition_epoch) {
				return fail(FaultReason::TransitionEpochContradiction);
			}
		}
	}

	// No rejected receipt can partially advance the retained observation.
	snapshot_.has_state = true;
	snapshot_.receipt_sequence = receipt.sequence;
	snapshot_.transition_epoch = receipt.transition_epoch;
	snapshot_.state = receipt.after;
	snapshot_.producer_time_us = receipt.producer_time_us;
	return commitTime(observation.receive_time_us);
}

Snapshot ReceiptChecker::poll(uint64_t observer_lifetime_id, uint64_t clock_epoch,
			      uint64_t now_us, bool clock_qualified) noexcept
{
	if (snapshot_.code == Code::Faulted) { return snapshot_; }

	if (observer_lifetime_id != config_.expected_observer_lifetime_id) {
		return fail(FaultReason::ObserverLifetimeMismatch);
	}

	if (clock_epoch != config_.expected_clock_epoch) { return fail(FaultReason::ClockEpochMismatch); }

	if (!clock_qualified) { return fail(FaultReason::ClockUnqualified); }

	const auto time_fault = localTimeFault(now_us);

	if (time_fault != FaultReason::None) { return fail(time_fault); }

	// Accepted receipt time already bounded producer_time_us. Monotone local
	// time therefore proves subtraction is safe without changing source state.
	return commitTime(now_us);
}

} // namespace state_receipts
} // namespace research_sitl
