#pragma once

#include "ActuatorRouting.hpp"
#include <uORB/topics/slaaps_output_bank.h>

namespace slaaps
{
constexpr uint64_t kMaximumOutputReportAgeUs = 1000000;
enum class InventoryFault : uint8_t { None, MissingBank, InvalidReport, Stale, DuplicateBank, DriverNotReady, UnstablePublisher };

// Successful capture metadata, indexed by physical bank, not uORB instance.
// Failure leaves the caller's previous snapshot untouched; always check the
// capture result before using it. These observations grant no output authority.
struct ReportedRoutingSnapshot {
	uint64_t captured_us{};
	OutputInventory inventory{};
	uint64_t driver[2] {};
	uint64_t reported_us[2] {};
	unsigned publisher_revision[2] {};
	uint8_t topic_instance[2] {};
};

inline bool sameReportedDrivers(const ReportedRoutingSnapshot &a, const ReportedRoutingSnapshot &b)
{
	if (!a.captured_us || !b.captured_us) { return false; }

	for (unsigned bank = 0; bank < 2; ++bank) {
		if (!a.driver[bank] || !a.publisher_revision[bank] || a.driver[bank] != b.driver[bank]
		    || a.publisher_revision[bank] != b.publisher_revision[bank] || a.topic_instance[bank] != b.topic_instance[bank]) { return false; }
	}

	return true;
}

// Reports are driver observations, not a cross-bank output transaction. The
// final driver write must independently revalidate the admitted routing.
// The fmu-v6x path requires MAIN and AUX (mask 3); an absent/replaced PWM driver
// cannot be inferred from saved function parameters or silently omitted.
InventoryFault assembleOutputInventory(uint64_t now, const slaaps_output_bank_s *reports, unsigned count,
				       uint8_t required_banks, OutputInventory &inventory);
} // namespace slaaps
