#pragma once

#include "ManualOutput.hpp"
#include <uORB/topics/slaaps_manual_activation.h>

namespace slaaps
{

// Syntax only. Bind this reply topic's actual publisher lifetime independently;
// neither payload revisions nor successful decoding grant output permission.
// The activation barrier checks the exact transaction and configured time budget.
inline bool validManualActivation(uint64_t now, unsigned bank, const ManualActivationAck &ack)
{
	return bank < 2 && ack.bank == bank && ack.session && ack.source && ack.configuration && ack.routing && ack.driver
	       && ack.command_revision && ack.acknowledgment_revision && ack.session <= ack.activated_us
	       && ack.activated_us <= now && ack.first_command_deadline_us > now;
}

inline bool encodeManualActivation(uint64_t now, unsigned bank, const ManualActivationAck &ack,
				   slaaps_manual_activation_s &message)
{
	if (!validManualActivation(now, bank, ack)) { return false; }

	slaaps_manual_activation_s candidate{};
	candidate.timestamp = now;
	candidate.timestamp_sample = ack.activated_us;
	candidate.first_command_deadline = ack.first_command_deadline_us;
	candidate.session = ack.session;
	candidate.source = ack.source;
	candidate.configuration = ack.configuration;
	candidate.routing = ack.routing;
	candidate.driver = ack.driver;
	candidate.command_revision = ack.command_revision;
	candidate.acknowledgment_revision = ack.acknowledgment_revision;
	candidate.bank = bank;
	message = candidate;
	return true;
}

inline bool decodeManualActivation(uint64_t now, unsigned bank, const slaaps_manual_activation_s &message,
				   ManualActivationAck &ack)
{
	if (message.timestamp < message.timestamp_sample || message.timestamp > now) { return false; }

	const ManualActivationAck candidate{message.session, message.source, message.configuration, message.routing,
					    message.driver, message.timestamp_sample, message.first_command_deadline,
					    message.command_revision, message.acknowledgment_revision, message.bank};

	if (!validManualActivation(now, bank, candidate)) { return false; }

	ack = candidate;
	return true;
}

} // namespace slaaps
