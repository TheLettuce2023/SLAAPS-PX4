#pragma once

#include "ReportedRouting.hpp"
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
	OutputRoutingObservation()
	{
		for (unsigned i = 0; i < ORB_MULTI_MAX_INSTANCES; ++i) {
			_reports[i] = uORB::ResearchSubscription{ORB_ID::slaaps_output_bank, static_cast<uint8_t>(i)};
		}
	}

	InventoryFault capture(uint64_t now, ReportedRoutingSnapshot &snapshot)
	{
		slaaps_output_bank_s reports[2] {};
		unsigned revisions[ORB_MULTI_MAX_INSTANCES] {};
		unsigned generations[ORB_MULTI_MAX_INSTANCES] {};
		uint8_t instances[2] {};
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

		ReportedRoutingSnapshot candidate{};
		const auto result = assembleOutputInventory(now, reports, count, 3, candidate.inventory);

		if (result != InventoryFault::None) { return result; }

		for (unsigned i = 0; i < ORB_MULTI_MAX_INSTANCES; ++i) {
			auto &subscription = _reports[i];

			if (!revisions[i]) {
				if (subscription.revision()) { return InventoryFault::UnstablePublisher; }

				continue;
			}

			slaaps_output_bank_s current{};
			unsigned revision{};

			if (!subscription.copyBound(&current, revision) || revision != revisions[i]
			    || subscription.get_last_generation() != generations[i]) { return InventoryFault::UnstablePublisher; }
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
private:
	uORB::ResearchSubscription _reports[ORB_MULTI_MAX_INSTANCES];
};

} // namespace slaaps
