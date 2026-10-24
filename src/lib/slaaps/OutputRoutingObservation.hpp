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
		uint8_t instances[2] {};
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
		now = hrt_absolute_time();
		const auto result = captureImpl(now, snapshot, workspace, true);
		now = hrt_absolute_time();
		return result;
	}
private:
	InventoryFault captureImpl(uint64_t &now, ReportedRoutingSnapshot &snapshot, Workspace &workspace, bool live)
	{
		auto &reports = workspace.reports;
		auto &revisions = workspace.revisions;
		auto &generations = workspace.generations;
		auto &instances = workspace.instances;
		auto &candidate = workspace.candidate;

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

		if (!live) {
			const auto result = assembleOutputInventory(now, reports, count, 3, candidate.inventory);

			if (result != InventoryFault::None) { return result; }
		}

		for (unsigned i = 0; i < ORB_MULTI_MAX_INSTANCES; ++i) {
			auto &subscription = _reports[i];

			if (!revisions[i]) {
				if (subscription.revision()) { return InventoryFault::UnstablePublisher; }

				continue;
			}

			auto &current = workspace.current;
			unsigned revision{};

			if (!subscription.copyBound(&current, revision) || revision != revisions[i]
			    || subscription.get_last_generation() != generations[i]) { return InventoryFault::UnstablePublisher; }
		}

		if (live) {
			now = hrt_absolute_time();
			const auto result = assembleOutputInventory(now, reports, count, 3, candidate.inventory);

			if (result != InventoryFault::None) { return result; }
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
};

} // namespace slaaps
