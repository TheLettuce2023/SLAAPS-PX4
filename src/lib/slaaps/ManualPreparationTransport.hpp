#pragma once

#include "ManualBankPreparation.hpp"
#include <uORB/topics/slaaps_manual_preparation.h>

namespace slaaps
{

enum class PreparationStatus : uint8_t { Inactive, Waiting, Ready, Rejected };

// Syntax and proposal matching only. Callers must bind each topic's actual
// publisher lifetime, reject stale/replayed traffic, and independently admit
// activation. Readiness is not output handling, flight permission or authority.
inline bool sameManualPreparation(const ManualPreparationRequest &a, const ManualPreparationRequest &b)
{
	return a.timestamp_us == b.timestamp_us && a.deadline_us == b.deadline_us && a.session == b.session
	       && a.source == b.source && a.configuration == b.configuration && a.command_revision == b.command_revision
	       && a.acknowledgment_revisions[0] == b.acknowledgment_revisions[0]
	       && a.acknowledgment_revisions[1] == b.acknowledgment_revisions[1] && sameManualOutputBinding(a.binding, b.binding);
}

inline bool validPreparationReply(unsigned bank, uint64_t prepared, uint64_t now, const ManualPreparationRequest &request)
{
	if (bank == slaaps_manual_preparation_s::REQUEST_BANK) { return prepared == 0; }

	return bank < 2 && (request.binding.banks & (1U << bank)) && prepared >= request.timestamp_us && prepared <= now;
}

inline bool encodeManualPreparation(uint64_t now, const ManualPreparationRequest &request, unsigned bank,
				    uint64_t prepared, slaaps_manual_preparation_s &message)
{
	if (!ManualBankPreparation::validRequest(now, request) || !validPreparationReply(bank, prepared, now, request)) { return false; }

	slaaps_manual_preparation_s candidate{};
	candidate.timestamp = now;
	candidate.timestamp_sample = request.timestamp_us;
	candidate.deadline = request.deadline_us;
	candidate.session = request.session;
	candidate.source = request.source;
	candidate.configuration = request.configuration;
	candidate.command_revision = request.command_revision;
	candidate.routing = request.binding.routing;
	candidate.bank_mask = request.binding.banks;
	candidate.bank = bank;
	candidate.prepared = prepared;

	for (unsigned i = 0; i < 2; ++i) {
		candidate.driver[i] = request.binding.driver[i];
		candidate.acknowledgment_revisions[i] = request.acknowledgment_revisions[i];
	}

	message = candidate;
	return true;
}

inline bool decodeManualPreparation(uint64_t now, unsigned expected_bank, const slaaps_manual_preparation_s &message,
				    ManualPreparationRequest &request, uint64_t &prepared)
{
	if (message.bank != expected_bank || message.timestamp < message.timestamp_sample || message.timestamp > now
	    || message.prepared > message.timestamp) { return false; }

	ManualPreparationRequest candidate{};
	candidate.timestamp_us = message.timestamp_sample;
	candidate.deadline_us = message.deadline;
	candidate.session = message.session;
	candidate.source = message.source;
	candidate.configuration = message.configuration;
	candidate.command_revision = message.command_revision;
	candidate.binding.routing = message.routing;
	candidate.binding.banks = message.bank_mask;

	for (unsigned i = 0; i < 2; ++i) {
		candidate.binding.driver[i] = message.driver[i];
		candidate.acknowledgment_revisions[i] = message.acknowledgment_revisions[i];
	}

	if (!ManualBankPreparation::validRequest(now, candidate)
	    || !validPreparationReply(expected_bank, message.prepared, now, candidate)) { return false; }

	request = candidate;
	prepared = message.prepared;
	return true;
}

} // namespace slaaps
