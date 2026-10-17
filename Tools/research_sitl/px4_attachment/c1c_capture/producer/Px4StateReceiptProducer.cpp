#include "Px4StateReceiptProducer.hpp"

#include <drivers/drv_hrt.h>
#include <atomic>

namespace research_sitl
{
namespace state_capture
{
namespace
{
Bootstrap bootstrap;
std::atomic<bool> production_finished{false};
}

Bootstrap &captureBootstrap() noexcept { return bootstrap; }
bool captureProductionFinished() noexcept { return production_finished.load(std::memory_order_acquire); }

void Px4StateReceiptProducer::publish(const std::optional<Receipt> &receipt) noexcept
{
	if (!receipt) { return; }

	research_state_receipt_s message{};
	message.timestamp = receipt->producer_time_us;
	message.producer_lifetime_id = receipt->producer_lifetime_id;
	message.clock_epoch = receipt->clock_epoch;
	message.sequence = receipt->sequence;
	message.transition_epoch = receipt->transition_epoch;
	message.producer_time_us = receipt->producer_time_us;
	message.kind = static_cast<uint8_t>(receipt->kind);
	message.before_arming_state = receipt->before.arming_state;
	message.before_nav_state = receipt->before.nav_state;
	message.after_arming_state = receipt->after.arming_state;
	message.after_nav_state = receipt->after.nav_state;
	producer_.completePublication(receipts_.publish(message));
}

void Px4StateReceiptProducer::initial(uint8_t arm, uint8_t nav) noexcept
{
	publish(producer_.initial({arm, nav}, hrt_absolute_time()));
}

void Px4StateReceiptProducer::transition(State before, uint8_t arm, uint8_t nav) noexcept
{
	publish(producer_.transition(before, {arm, nav}, hrt_absolute_time()));
}

void Px4StateReceiptProducer::sample(uint8_t arm, uint8_t nav) noexcept
{
	publish(producer_.sample({arm, nav}, hrt_absolute_time()));
}

void Px4StateReceiptProducer::finish(uint8_t arm, uint8_t nav) noexcept
{
	if (finished_) { return; }

	finished_ = true;
	const auto result = producer_.finish({arm, nav}, hrt_absolute_time());
	research_state_summary_s message{};
	message.timestamp = result.final_time_us;
	message.producer_lifetime_id = result.identities.producer_lifetime_id;
	message.observer_lifetime_id = result.identities.observer_lifetime_id;
	message.clock_epoch = result.identities.clock_epoch;
	message.attempted_sequence = result.attempted_sequence;
	message.emitted_sequence = result.emitted_sequence;
	message.transition_epoch = result.transition_epoch;
	message.producer_time_us = result.producer_time_us;
	message.final_time_us = result.final_time_us;
	message.retained_arming_state = result.retained_state.arming_state;
	message.retained_nav_state = result.retained_state.nav_state;
	message.emitted_arming_state = result.emitted_state.arming_state;
	message.emitted_nav_state = result.emitted_state.nav_state;
	message.emitted_transition_epoch = result.emitted_transition_epoch;
	message.emitted_producer_time_us = result.emitted_producer_time_us;
	message.actual_arming_state = result.actual_state.arming_state;
	message.actual_nav_state = result.actual_state.nav_state;
	message.has_state = result.has_state;
	message.finished = result.finished;
	message.first_fault = static_cast<uint8_t>(result.first_fault);
	// Failure is detected by required-summary reconciliation, never replaced by
	// a fabricated healthy receipt. No exporter or checker runs here.
	summary_.publish(message);
	production_finished.store(true, std::memory_order_release);
}

} // namespace state_capture
} // namespace research_sitl
