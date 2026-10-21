#include "ReportedRouting.hpp"

namespace slaaps
{
InventoryFault assembleOutputInventory(uint64_t now, const slaaps_output_bank_s *reports, unsigned count,
				       uint8_t required_banks, OutputInventory &inventory)
{
	if (now == 0 || required_banks == 0 || (required_banks & ~3U) || count > 2 || (count && !reports)) {
		return InventoryFault::InvalidReport;
	}

	OutputInventory candidate{};
	uint8_t banks{};

	for (unsigned i = 0; i < count; ++i) {
		const auto &report = reports[i];

		if (report.bank > 1 || report.driver_instance == 0 || report.channel_count == 0
		    || report.channel_count > kMaximumChannelsPerBank) { return InventoryFault::InvalidReport; }

		if (banks & (1U << report.bank)) { return InventoryFault::DuplicateBank; }

		banks |= 1U << report.bank;

		if (report.timestamp == 0 || report.timestamp > now || now - report.timestamp >= kMaximumOutputReportAgeUs) {
			return InventoryFault::Stale;
		}

		if (!report.ready) { return InventoryFault::DriverNotReady; }

		const uint32_t channel_mask = (1U << report.channel_count) - 1U;

		if ((report.hardware_mask & ~channel_mask) || (report.reversed_mask & ~channel_mask)) {
			return InventoryFault::InvalidReport;
		}

		for (unsigned channel = 0; channel < report.channel_count; ++channel) {
			auto &out = candidate.channels[candidate.count++];
			out.bank = report.bank == 0 ? OutputBank::Main : OutputBank::Aux;
			out.channel = channel;
			out.function = report.function[channel];
			out.protocol = (report.hardware_mask & (1U << channel)) && report.rate_hz[channel] > 0
				       ? OutputProtocol::Pwm : OutputProtocol::Unsupported;
			out.rate_hz = report.rate_hz[channel];
			out.minimum_us = report.minimum_us[channel];
			out.center_us = report.center_us[channel];
			out.maximum_us = report.maximum_us[channel];
			out.disarmed_us = report.disarmed_us[channel];
			out.failsafe_us = report.failsafe_us[channel];
			out.reversed = report.reversed_mask & (1U << channel);
		}
	}

	if ((banks & required_banks) != required_banks) { return InventoryFault::MissingBank; }

	inventory = candidate;
	return InventoryFault::None;
}
} // namespace slaaps
