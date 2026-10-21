#pragma once

#include "ManualOutput.hpp"
#include <uORB/topics/slaaps_manual_command.h>
#include <uORB/topics/slaaps_manual_ack.h>
#include <cmath>

namespace slaaps
{

// Wire validation only. No publisher authentication, session admission, sequence
// tracking or timeout renewal. Callers must bind publisher lifetimes atomically
// and pass decoded data to the existing worker/consumer checks. Failures preserve
// caller output. These messages must never be treated as supervisor grants.
inline bool validManualFrame(uint64_t now, const ManualWorkerOutput &frame)
{
	const auto &edf = frame.edf;

	if (!frame.timestamp_us || frame.timestamp_us > now || frame.deadline_us <= now || !frame.sequence
	    || !validManualOutputBinding(frame.binding) || edf.disposition != EdfDisposition::Command
	    || !edf.session || !edf.source || !edf.configuration || !edf.sequence || !edf.request_timestamp_us
	    || edf.request_timestamp_us > frame.timestamp_us || frame.deadline_us > edf.deadline_us
	    || !std::isfinite(edf.normalized) || edf.normalized < 0.F || edf.normalized > 1.F) { return false; }

	for (float value : frame.canards) {
		if (!std::isfinite(value) || value < -1.F || value > 1.F) { return false; }
	}

	return true;
}

inline bool encodeManualCommand(uint64_t now, const ManualWorkerOutput &frame, slaaps_manual_command_s &message)
{
	if (!validManualFrame(now, frame)) { return false; }

	slaaps_manual_command_s candidate{};
	candidate.timestamp = now;
	candidate.timestamp_sample = frame.timestamp_us;
	candidate.deadline = frame.deadline_us;
	candidate.sequence = frame.sequence;
	candidate.session = frame.edf.session;
	candidate.source = frame.edf.source;
	candidate.configuration = frame.edf.configuration;
	candidate.routing = frame.binding.routing;

	for (unsigned i = 0; i < 2; ++i) { candidate.driver[i] = frame.binding.driver[i]; }

	candidate.edf_sequence = frame.edf.sequence;
	candidate.edf_request_timestamp = frame.edf.request_timestamp_us;
	candidate.edf_deadline = frame.edf.deadline_us;
	candidate.edf = frame.edf.normalized;
	candidate.bank_mask = frame.binding.banks;

	for (unsigned i = 0; i < kCanards; ++i) { candidate.canards[i] = frame.canards[i]; }

	message = candidate;
	return true;
}

inline bool decodeManualCommand(uint64_t now, const slaaps_manual_command_s &message, ManualWorkerOutput &frame)
{
	if (message.timestamp < message.timestamp_sample || message.timestamp > now) { return false; }

	ManualWorkerOutput candidate{};
	candidate.timestamp_us = message.timestamp_sample;
	candidate.deadline_us = message.deadline;
	candidate.sequence = message.sequence;
	candidate.edf.disposition = EdfDisposition::Command;
	candidate.edf.session = message.session;
	candidate.edf.source = message.source;
	candidate.edf.configuration = message.configuration;
	candidate.edf.sequence = message.edf_sequence;
	candidate.edf.request_timestamp_us = message.edf_request_timestamp;
	candidate.edf.deadline_us = message.edf_deadline;
	candidate.edf.normalized = message.edf;
	candidate.binding.routing = message.routing;
	candidate.binding.banks = message.bank_mask;

	for (unsigned i = 0; i < 2; ++i) { candidate.binding.driver[i] = message.driver[i]; }

	for (unsigned i = 0; i < kCanards; ++i) { candidate.canards[i] = message.canards[i]; }

	if (!validManualFrame(now, candidate)) { return false; }

	frame = candidate;
	return true;
}

inline bool validManualAck(uint64_t now, unsigned bank, const ManualOutputAck &ack)
{
	return bank < 2 && ack.bank == bank && ack.session && ack.source && ack.configuration && ack.routing && ack.driver
	       && ack.sequence && ack.command_timestamp_us && ack.command_timestamp_us <= ack.accepted_us && ack.accepted_us <= now;
}

inline bool encodeManualAck(uint64_t now, unsigned bank, const ManualOutputAck &ack, slaaps_manual_ack_s &message)
{
	if (!validManualAck(now, bank, ack)) { return false; }

	slaaps_manual_ack_s candidate{};
	candidate.timestamp = now;
	candidate.timestamp_sample = ack.command_timestamp_us;
	candidate.accepted = ack.accepted_us;
	candidate.session = ack.session;
	candidate.source = ack.source;
	candidate.configuration = ack.configuration;
	candidate.routing = ack.routing;
	candidate.driver = ack.driver;
	candidate.sequence = ack.sequence;
	candidate.bank = ack.bank;
	message = candidate;
	return true;
}

inline bool decodeManualAck(uint64_t now, unsigned bank, const slaaps_manual_ack_s &message, ManualOutputAck &ack)
{
	if (message.timestamp < message.accepted || message.timestamp > now) { return false; }

	ManualOutputAck candidate{message.session, message.source, message.configuration, message.routing, message.driver,
				  message.sequence, message.timestamp_sample, message.accepted, message.bank};

	if (!validManualAck(now, bank, candidate)) { return false; }

	ack = candidate;
	return true;
}

} // namespace slaaps
