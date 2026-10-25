#pragma once

#include "ReportedRouting.hpp"
#include <drivers/drv_hrt.h>
#include <uORB/ResearchSubscription.hpp>

namespace slaaps
{

// Enumerate every topic instance, atomically copy each payload with its publisher
// lifetime, then recheck the entire enumeration and publication generations.
// This rejects a changing observation pass; it is not an atomic physical-output
// transaction. Drivers still revalidate routing and permission at final handling.
class OutputRoutingObservation
{
public:
	// Driver queues can supply heap-owned scratch space instead of placing the
	// complete report/inventory capture on a small physical-driver stack.
	struct Workspace {
		slaaps_output_bank_s reports[2] {};
		slaaps_output_bank_s current{};
		ReportedRoutingSnapshot candidate{};
		unsigned revisions[ORB_MULTI_MAX_INSTANCES] {};
		unsigned generations[ORB_MULTI_MAX_INSTANCES] {};
		unsigned baseline_revisions[ORB_MULTI_MAX_INSTANCES] {};
		uint8_t instances[2] {};
		bool baseline_valid{};
		bool retryable{};
#if defined(CONFIG_SLAAPS_ROUTING_TEST)
		unsigned attempt {};
#endif
	};

	OutputRoutingObservation()
	{
		for (unsigned i = 0; i < ORB_MULTI_MAX_INSTANCES; ++i) {
			_reports[i] = uORB::ResearchSubscription{ORB_ID::slaaps_output_bank, static_cast<uint8_t>(i)};
		}
	}

	InventoryFault capture(uint64_t now, ReportedRoutingSnapshot &snapshot)
	{
		Workspace workspace{};
		return capture(now, snapshot, workspace);
	}

	InventoryFault capture(uint64_t now, ReportedRoutingSnapshot &snapshot, Workspace &workspace)
	{
		workspace.baseline_valid = false;
		workspace.retryable = false;
		return captureImpl(now, snapshot, workspace, false);
	}

	InventoryFault captureLive(uint64_t &now, ReportedRoutingSnapshot &snapshot)
	{
		Workspace workspace{};
		return captureLive(now, snapshot, workspace);
	}

	// Both report copies and the generation/lifetime recheck precede the live
	// freshness decision. Return the final observation time for caller guards.
	InventoryFault captureLive(uint64_t &now, ReportedRoutingSnapshot &snapshot, Workspace &workspace)
	{
		workspace.baseline_valid = false;
		workspace.retryable = false;
		constexpr unsigned kMaximumAttempts = 3;
		InventoryFault result{InventoryFault::UnstablePublisher};

		for (unsigned attempt = 0; attempt < kMaximumAttempts; ++attempt) {
#if defined(CONFIG_SLAAPS_ROUTING_TEST)
			workspace.attempt = attempt;
#endif
			now = hrt_absolute_time();
			result = captureImpl(now, snapshot, workspace, true);
			now = hrt_absolute_time();

			if (result != InventoryFault::UnstablePublisher || !workspace.retryable) { return result; }
		}

		return result;
	}

#if defined(CONFIG_SLAAPS_ROUTING_TEST)
	// Deterministic native fixture boundary; no authority or check override.
	using CapturePassHook = void (*)(void *context, unsigned attempt);
	void setCapturePassHook(CapturePassHook hook, void *context)
	{
		_capture_hook = hook;
		_capture_context = context;
	}
#endif
private:
	InventoryFault captureImpl(uint64_t &now, ReportedRoutingSnapshot &snapshot, Workspace &workspace, bool live)
	{
		auto &reports = workspace.reports;
		auto &revisions = workspace.revisions;
		auto &generations = workspace.generations;
		auto &instances = workspace.instances;
		auto &candidate = workspace.candidate;
		workspace.retryable = false;

		// Reset only values whose absence is interpreted below. Every used
		// report/current payload and the complete inventory are overwritten.
		for (auto &revision : revisions) { revision = 0; }

		unsigned count{};

		for (unsigned i = 0; i < ORB_MULTI_MAX_INSTANCES; ++i) {
			auto &subscription = _reports[i];
			const unsigned before = subscription.revision();

			if (!before) { continue; }

			if (count == 2) { return InventoryFault::DuplicateBank; }

			if (!subscription.copyBound(&reports[count], revisions[i])) { return InventoryFault::DriverNotReady; }

			if (revisions[i] != before) { return InventoryFault::UnstablePublisher; }

			generations[i] = subscription.get_last_generation();
			instances[count++] = i;
		}

		if (live) {
			for (unsigned i = 0; i < ORB_MULTI_MAX_INSTANCES; ++i) {
				if (workspace.baseline_valid && revisions[i] != workspace.baseline_revisions[i]) {
					return InventoryFault::UnstablePublisher;
				}
			}

			if (!workspace.baseline_valid) {
				for (unsigned i = 0; i < ORB_MULTI_MAX_INSTANCES; ++i) { workspace.baseline_revisions[i] = revisions[i]; }

				workspace.baseline_valid = true;
			}

#if defined(CONFIG_SLAAPS_ROUTING_TEST)

			if (_capture_hook) { _capture_hook(_capture_context, workspace.attempt); }

#endif
		}

		if (!live) {
			const auto result = assembleOutputInventory(now, reports, count, 3, candidate.inventory);

			if (result != InventoryFault::None) { return result; }
		}

		bool generation_changed = false;

		for (unsigned i = 0; i < ORB_MULTI_MAX_INSTANCES; ++i) {
			auto &subscription = _reports[i];

			if (!revisions[i]) {
				if (subscription.revision()) { return InventoryFault::UnstablePublisher; }

				continue;
			}

			auto &current = workspace.current;
			unsigned revision{};

			if (!subscription.copyBound(&current, revision) || revision != revisions[i]) {
				return InventoryFault::UnstablePublisher;
			}

			if (subscription.get_last_generation() != generations[i]) {
				if (!live) { return InventoryFault::UnstablePublisher; }

				generation_changed = true;
			}
		}

		if (live) {
			now = hrt_absolute_time();
			const auto result = assembleOutputInventory(now, reports, count, 3, candidate.inventory);

			if (result != InventoryFault::None) { return result; }
		}

		// A normal report update may race a pass. Retry only after every
		// publisher/enumeration check and the original reports' freshness and
		// validity checks pass. The next full pass must keep the first lifetimes.
		if (generation_changed) {
			workspace.retryable = true;
			return InventoryFault::UnstablePublisher;
		}

		candidate.captured_us = now;

		for (unsigned i = 0; i < count; ++i) {
			const unsigned bank = reports[i].bank;
			candidate.driver[bank] = reports[i].driver_instance;
			candidate.reported_us[bank] = reports[i].timestamp;
			candidate.publisher_revision[bank] = revisions[instances[i]];
			candidate.topic_instance[bank] = instances[i];
		}

		snapshot = candidate;
		return InventoryFault::None;
	}
	uORB::ResearchSubscription _reports[ORB_MULTI_MAX_INSTANCES];
#if defined(CONFIG_SLAAPS_ROUTING_TEST)
	CapturePassHook _capture_hook {};
	void *_capture_context{};
#endif
};

} // namespace slaaps
