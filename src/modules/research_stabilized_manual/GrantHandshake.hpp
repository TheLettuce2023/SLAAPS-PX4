#pragma once

#include <uORB/topics/stabilized_grant_request.h>
#include <uORB/topics/stabilized_grant_response.h>

namespace research_stabilized_manual
{

// Trusted publisher-lifetime metadata, not fields supplied by a requester.
struct GrantSources {
	unsigned request{}, response{}, command{}, acknowledgement{};
};
struct GrantHandshake {
	stabilized_grant_request_s request{};
	GrantSources sources{};
};

inline bool validGrantRequest(uint64_t now, const stabilized_grant_request_s &request,
			      uint64_t configuration, unsigned command_revision, unsigned acknowledgement_revision)
{
	return request.session && request.timestamp >= request.session && request.timestamp <= now
	       && request.deadline > request.session && now < request.deadline
	       && configuration && request.configuration == configuration
	       && command_revision && request.command_revision == command_revision
	       && acknowledgement_revision && request.acknowledgement_revision == acknowledgement_revision;
}

// Caller copies response and revision atomically, then samples current source
// lifetimes and now. This validates one startup decision, not ongoing health or
// authority. The consumer retains its independent source checks/output lease.
inline bool grantConfirmed(uint64_t now, const GrantHandshake &expected,
			   const stabilized_grant_response_s &response, unsigned copied_response_revision,
			   const GrantSources &current)
{
	const auto &sources = expected.sources;
	const auto &request = expected.request;
	return sources.request && sources.response && sources.command && sources.acknowledgement
	       && current.request == sources.request && current.response == sources.response
	       && current.command == sources.command && current.acknowledgement == sources.acknowledgement
	       && copied_response_revision == sources.response
	       && validGrantRequest(now, request, request.configuration, sources.command, sources.acknowledgement)
	       && response.accepted && response.timestamp >= request.timestamp && response.timestamp <= now
	       && response.session == request.session && response.deadline == request.deadline
	       && response.configuration == request.configuration && response.request_revision == sources.request
	       && response.command_revision == sources.command && response.acknowledgement_revision == sources.acknowledgement;
}

} // namespace research_stabilized_manual
