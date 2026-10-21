#pragma once

#include <lib/research_stabilized_manual/StabilizedOutput.hpp>
#include <gz/msgs/double_v.pb.h>
#include <cmath>
#include <cfloat>
#include <cstdint>

namespace research_stabilized_session_fixture
{
using namespace research_stabilized_manual;

// Private, versioned Gazebo transport. These records convey data only: neither
// decode nor receipt grants ownership or confirms physical application. The
// independently scheduled consumer must validate the explicit grant, selected
// input health, immutable profile, lease and pending sequence before acting.
// Double_V represents all integer fields exactly only through 2^53 - 1.
constexpr uint64_t kMaximumWireInteger = (uint64_t{1} << 53) - 1;
constexpr unsigned kCommandFields = 12;
constexpr unsigned kAcknowledgementFields = 7;
constexpr uint64_t kVersion = 1;
constexpr uint64_t kCommandKind = 1;
constexpr uint64_t kAcknowledgementKind = 2;

struct Command {
	uint32_t configuration{};
	OutputFrame frame{};
	TimeUs lease_us{};
	float thrust_n{};
};
struct Acknowledgement {
	uint32_t configuration{};
	OutputAcknowledgement value{};
};

inline bool integer(double value, uint64_t &result)
{
	if (!std::isfinite(value) || value < 0.0 || value > static_cast<double>(kMaximumWireInteger)
	    || std::floor(value) < value) { return false; }

	result = static_cast<uint64_t>(value);
	return true;
}

inline bool prefix(const gz::msgs::Double_V &wire, unsigned count, uint64_t kind, uint32_t &configuration)
{
	if (wire.data_size() != static_cast<int>(count)) { return false; }

	uint64_t version{}, type{}, identity{};

	if (!integer(wire.data(0), version) || version != kVersion || !integer(wire.data(1), type) || type != kind
	    || !integer(wire.data(2), identity) || !identity || identity > UINT32_MAX) { return false; }

	configuration = static_cast<uint32_t>(identity);
	return true;
}

inline bool decode(const gz::msgs::Double_V &wire, Command &result)
{
	Command candidate{};
	auto &frame = candidate.frame;

	if (!prefix(wire, kCommandFields, kCommandKind, candidate.configuration)
	    || !integer(wire.data(3), frame.session) || !frame.session
	    || !integer(wire.data(4), frame.timestamp_us) || frame.timestamp_us < frame.session
	    || !integer(wire.data(5), frame.sequence) || !frame.sequence
	    || !integer(wire.data(6), candidate.lease_us) || !candidate.lease_us
	    || candidate.lease_us > kMaximumWireInteger - frame.timestamp_us) { return false; }

	const double thrust = wire.data(7);

	if (!std::isfinite(thrust) || thrust < 0.0 || thrust > static_cast<double>(FLT_MAX)) { return false; }

	candidate.thrust_n = static_cast<float>(thrust);

	for (unsigned i = 0; i < 4; ++i) {
		const double value = wire.data(8 + i);

		if (!std::isfinite(value) || value < -1.0 || value > 1.0) { return false; }

		frame.command[i] = static_cast<float>(value);
	}

	result = candidate;
	return true;
}

inline bool decode(const gz::msgs::Double_V &wire, Acknowledgement &result)
{
	Acknowledgement candidate{};
	auto &ack = candidate.value;

	if (!prefix(wire, kAcknowledgementFields, kAcknowledgementKind, candidate.configuration)
	    || !integer(wire.data(3), ack.session) || !ack.session
	    || !integer(wire.data(4), ack.command_timestamp_us) || ack.command_timestamp_us < ack.session
	    || !integer(wire.data(5), ack.sequence) || !ack.sequence
	    || !integer(wire.data(6), ack.accepted_us) || ack.accepted_us < ack.command_timestamp_us) { return false; }

	result = candidate;
	return true;
}

inline bool encode(const Command &source, gz::msgs::Double_V &result)
{
	// Check before conversion: an out-of-range uint64_t can round to an
	// apparently valid double. Decode alone cannot detect that information loss.
	if (source.frame.session > kMaximumWireInteger || source.frame.timestamp_us > kMaximumWireInteger
	    || source.frame.sequence > kMaximumWireInteger || source.lease_us > kMaximumWireInteger) { return false; }

	gz::msgs::Double_V wire;

	for (uint64_t value : {
		     kVersion, kCommandKind, uint64_t{source.configuration}, source.frame.session,
		     source.frame.timestamp_us, source.frame.sequence, source.lease_us
	     }) { wire.add_data(static_cast<double>(value)); }

	wire.add_data(static_cast<double>(source.thrust_n));

	for (unsigned i = 0; i < 4; ++i) { wire.add_data(static_cast<double>(source.frame.command[i])); }

	Command validated{};

	if (!decode(wire, validated)) { return false; }

	result.Swap(&wire);
	return true;
}

inline bool encode(const Acknowledgement &source, gz::msgs::Double_V &result)
{
	const auto &ack = source.value;

	if (ack.session > kMaximumWireInteger || ack.command_timestamp_us > kMaximumWireInteger
	    || ack.sequence > kMaximumWireInteger || ack.accepted_us > kMaximumWireInteger) { return false; }

	gz::msgs::Double_V wire;

	for (uint64_t value : {
		     kVersion, kAcknowledgementKind, uint64_t{source.configuration}, ack.session,
		     ack.command_timestamp_us, ack.sequence, ack.accepted_us
	     }) { wire.add_data(static_cast<double>(value)); }

	Acknowledgement validated{};

	if (!decode(wire, validated)) { return false; }

	result.Swap(&wire);
	return true;
}
} // namespace research_stabilized_session_fixture
