#include "ReceiptMerge.hpp"

#include <limits>

namespace research_sitl::provenance
{
namespace
{
constexpr unsigned SlotShift = 8;
constexpr uint32_t CauseMask = 0xff;

bool equal(const Identity &a, const Identity &b)
{
	return a.run == b.run && a.owner == b.owner && a.controller == b.controller
	       && a.publisher == b.publisher && a.lifetime == b.lifetime && a.clock == b.clock;
}

bool valid(const Identity &value)
{
	return value.run && value.owner && value.controller && value.publisher && value.lifetime && value.clock;
}
}

ReceiptMerge::ReceiptMerge(const Config &config) noexcept : config_(config)
{
	bool ok = config.source_count > 0 && config.source_count <= SourceCapacity;

	for (size_t i = 0; ok && i < config.source_count; ++i) {
		const auto &id = config.sources[i];
		ok = valid(id) && id.run == config.sources[0].run && id.owner == config.sources[0].owner
		     && id.controller == config.sources[0].controller;

		for (size_t j = 0; j < i; ++j) { ok = ok && id.publisher != config.sources[j].publisher; }
	}

	if (!ok) { fault(Fault::InvalidConfiguration, 0); close(0); }
}

void ReceiptMerge::fault(Fault cause, size_t slot) noexcept
{
	uint32_t expected = 0;
	// Invalid caller slots are represented by the out-of-range sentinel.
	const auto bounded_slot = static_cast<uint32_t>(slot < SourceCapacity ? slot : SourceCapacity);
	const uint32_t encoded = static_cast<uint32_t>(cause) | (bounded_slot << SlotShift);
	first_fault_.compare_exchange_strong(expected, encoded, std::memory_order_acq_rel);
}

bool ReceiptMerge::publish(size_t slot, const Receipt &receipt) noexcept
{
	if (stopped_.load(std::memory_order_acquire) || first_fault_.load(std::memory_order_acquire)) { return false; }

	if (slot >= config_.source_count) { fault(Fault::InvalidSlot, slot); return false; }

	auto &lane = lanes_[slot];

	if (lane.writer.test_and_set(std::memory_order_acquire)) { fault(Fault::ConcurrentProducer, slot); return false; }

	const uint32_t write = lane.write.load(std::memory_order_relaxed);
	const uint32_t read = lane.read.load(std::memory_order_acquire);
	bool ok = false;

	if (write == std::numeric_limits<uint32_t>::max()) { fault(Fault::CounterExhausted, slot); }

	else if (write - read == QueueCapacity) { fault(Fault::QueueOverflow, slot); }

	else {
		lane.queue[write % QueueCapacity] = receipt;
		lane.write.store(write + 1, std::memory_order_release);
		ok = true;
	}

	lane.writer.clear(std::memory_order_release);
	return ok;
}

Fault ReceiptMerge::validate(size_t slot, const Receipt &receipt) const noexcept
{
	const auto &lane = lanes_[slot];

	if (!equal(receipt.identity, config_.sources[slot])) { return Fault::IdentityMismatch; }

	if (receipt.kind != Kind::Initial && receipt.kind != Kind::Sample && receipt.kind != Kind::Lost) { return Fault::InvalidKind; }

	if (!receipt.source_time_us || receipt.source_time_us < lane.last_source_time) { return Fault::SourceTime; }

	if (!lane.last_sequence && receipt.kind != Kind::Initial) { return Fault::MissingInitial; }

	if (lane.last_sequence && receipt.kind == Kind::Initial) { return Fault::RepeatedInitial; }

	if (lane.last_sequence == std::numeric_limits<uint64_t>::max()) { return Fault::CounterExhausted; }

	if (receipt.sequence != lane.last_sequence + 1) { return Fault::Sequence; }

	return receipt.kind == Kind::Lost ? Fault::SourceLost : Fault::None;
}

void ReceiptMerge::close(uint64_t time, const Receipt *attempted, size_t attempted_slot) noexcept
{
	if (closed_) { return; }

	const uint32_t encoded = first_fault_.load(std::memory_order_acquire);
	stopped_.store(true, std::memory_order_release);
	closed_ = true;
	closure_.cause = static_cast<Fault>(encoded & CauseMask);
	closure_.slot = encoded >> SlotShift;
	closure_.owner_time_us = time;
	closure_.has_attempted = attempted != nullptr;

	if (attempted) { closure_.attempted = *attempted; closure_.attempted_slot = attempted_slot; }
}

bool ReceiptMerge::step(uint64_t time) noexcept
{
	if (closed_) { return false; }

	if (first_fault_.load(std::memory_order_acquire)) { close(time); return false; }

	if (!time || time < last_owner_time_) { fault(Fault::OwnerTime, 0); close(time); return false; }

	last_owner_time_ = time;

	for (size_t offset = 0; offset < config_.source_count; ++offset) {
		const size_t slot = (cursor_ + offset) % config_.source_count;
		auto &lane = lanes_[slot];
		const uint32_t read = lane.read.load(std::memory_order_relaxed);

		if (read == lane.write.load(std::memory_order_acquire)) { continue; }

		const Receipt candidate = lane.queue[read % QueueCapacity];
		const Fault error = count_ == ArchiveCapacity ? Fault::EvidenceExhausted : validate(slot, candidate);

		if (error != Fault::None) { fault(error, slot); }

		// Final acquisition is the consumption linearization point. A later
		// producer fault closes on the next step, preserving this prior record.
		if (first_fault_.load(std::memory_order_acquire)) { close(time, &candidate, slot); return false; }

		archive_[count_] = {count_ + 1, time, slot, candidate};
		++count_;
		lane.last_sequence = candidate.sequence;
		lane.last_source_time = candidate.source_time_us;
		lane.read.store(read + 1, std::memory_order_release);
		cursor_ = (slot + 1) % config_.source_count;
		return true;
	}

	return false;
}

void ReceiptMerge::stop(uint64_t time) noexcept
{
	if (closed_) { return; }

	fault(!time || time < last_owner_time_ ? Fault::OwnerTime : Fault::OwnerStop, 0);
	close(time);
}

bool ReceiptMerge::copy(size_t index, Record &out) const noexcept
{
	if (index >= count_) { return false; }

	out = archive_[index];
	return true;
}
}
