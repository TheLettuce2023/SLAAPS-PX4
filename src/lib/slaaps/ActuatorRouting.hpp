#pragma once

#include "VehicleConfiguration.hpp"

namespace slaaps
{
// Reserved logical functions; physical channel numbers are never mode IDs.
// Keep in sync with mixer_module/output_functions.yaml.
constexpr uint16_t kCanardFunctionFirst = 10100;
constexpr uint16_t kEdfFunction = 10104;
constexpr unsigned kMaximumOutputChannels = 32;
constexpr unsigned kMaximumChannelsPerBank = 16;
// DShot values use the driver's units (1..1999 maps to DShot 48..2047); 0 is the disarm frame.
constexpr uint16_t kDshot600Kbit = 600;
constexpr uint16_t kDshotMaximumValue = 1999;

enum class OutputBank : uint8_t { Main, Aux };
enum class OutputProtocol : uint8_t { Pwm, Unsupported, Dshot600 };

struct OutputChannel {
	OutputBank bank{OutputBank::Main};
	uint8_t channel{}; // zero based within the physical bank
	uint16_t function{};
	OutputProtocol protocol{OutputProtocol::Pwm};
	uint16_t rate_hz{};
	uint16_t minimum_us{};
	uint16_t maximum_us{};
	uint16_t center_us{};
	uint16_t disarmed_us{};
	uint16_t failsafe_us{};
	bool reversed{};
};

struct OutputInventory {
	OutputChannel channels[kMaximumOutputChannels] {};
	unsigned count{};
};

enum class RoutingFault : uint8_t {
	None, InvalidInventory, DuplicatePin, UnsupportedFunction, UnsupportedProtocol,
	DuplicateFunction, MissingCanard, MissingEdf, InvalidCalibration, InvalidOffValue
};

struct RoutingResult {
	RoutingFault fault{RoutingFault::None};
	int channel{-1}; // inventory index for diagnosis, not an actuator number
	bool valid() const { return fault == RoutingFault::None; }
};

struct RoutingPlan {
	OutputChannel canard[kCanards] {};
	OutputChannel edf{};
	uint8_t bank_mask{}; // every participating driver must acknowledge handoff
	bool edf_assigned{};
};

inline bool sameOutputChannel(const OutputChannel &a, const OutputChannel &b)
{
	return a.bank == b.bank && a.channel == b.channel && a.function == b.function && a.protocol == b.protocol
	       && a.rate_hz == b.rate_hz && a.minimum_us == b.minimum_us && a.maximum_us == b.maximum_us
	       && a.center_us == b.center_us && a.disarmed_us == b.disarmed_us && a.failsafe_us == b.failsafe_us
	       && a.reversed == b.reversed;
}

inline bool sameRoutingPlan(const RoutingPlan &a, const RoutingPlan &b)
{
	if (a.bank_mask != b.bank_mask || a.edf_assigned != b.edf_assigned || !sameOutputChannel(a.edf, b.edf)) { return false; }

	for (unsigned i = 0; i < kCanards; ++i) {
		if (!sameOutputChannel(a.canard[i], b.canard[i])) { return false; }
	}

	return true;
}

// The native adapter must enumerate every enabled physical output, including
// unsupported protocols. Omitting a driver would invalidate exclusivity claims.
// Flight requires one EDF; servo-only bench may leave it unassigned.
RoutingResult makeRoutingPlan(const VehicleConfiguration &configuration, const OutputInventory &inventory,
			      bool flight, RoutingPlan &plan);

} // namespace slaaps
