#include <modules/research_stabilized_manual/SessionRequestOwner.hpp>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
using namespace research_stabilized_manual;
namespace
{
unsigned checks{};
void check(bool value, const char *message)
{
	++checks;

	if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
const GrantSources sources{1, 2, 3, 4};
stabilized_grant_response_s response(const SessionRequestOwner &owner)
{
	const auto &h = owner.handshake();
	stabilized_grant_response_s r{};
	r.timestamp = h.request.timestamp;
	r.session = h.request.session;
	r.deadline = h.request.deadline;
	r.configuration = h.request.configuration;
	r.command_revision = h.sources.command;
	r.acknowledgement_revision = h.sources.acknowledgement;
	r.request_revision = h.sources.request;
	r.accepted = true;
	return r;
}
void ready(SessionRequestOwner &o)
{
	check(!o.request(10, 100, 1, sources, true), "initial disarm required");
	check(!o.confirmDisarmed(1, 0, false), "unhandled disarm rejected");
	check(!o.confirmDisarmed(1, 9, true), "wrong disarm session rejected");
	check(o.confirmDisarmed(1, 0, true), "initial confirmed disarm");
}
}
int main()
{
	SessionRequestOwner o;
	ready(o);
	check(!o.request(10, 100, 1, sources, false), "admission required");
	check(!o.request(10, 10, 1, sources, true), "exclusive deadline");
	check(!o.request(10, 100, 0, sources, true), "profile identity required");
	check(o.request(10, 100, 1, sources, true), "explicit request");
	check(!o.request(11, 200, 2, sources, true), "pending profile replacement rejected");
	check(o.handshake().request.deadline == 100, "deadline not refreshed");
	auto r = response(o);
	r.session = 9;
	check(!o.accept(11, true, sources, r, 2), "stale response rejected");
	r = response(o);
	check(!o.accept(12, true, sources, r, 3), "coherent source binding required");
	check(o.accept(13, true, sources, r, 2), "matching response accepted");
	check(o.check(101, true, sources), "grant deadline not active lease");
	check(!o.confirmDisarmed(102, 10, true), "active cannot reset directly");
	check(!o.check(102, false, sources), "permission loss revokes");
	check(!o.check(103, true, sources), "permission return cannot recover");
	check(!o.confirmDisarmed(101, 10, true), "clock cannot regress");
	check(!o.confirmDisarmed(103, 9, true), "old disarm cannot reset");
	check(o.confirmDisarmed(103, 10, true), "explicit confirmed restart");
	check(!o.request(10, 200, 1, sources, true), "old session cannot replay");
	check(o.request(104, 200, 1, sources, true), "new session request");
	check(!o.check(200, true, sources), "deadline equality revokes");

	for (unsigned axis = 0; axis < 4; ++axis) {
		SessionRequestOwner p; ready(p); check(p.request(10, 100, 1, sources, true), "lifetime request");
		GrantSources changed = sources;

		if (axis == 0) { ++changed.request; }

		if (axis == 1) { ++changed.response; }

		if (axis == 2) { ++changed.command; }

		if (axis == 3) { ++changed.acknowledgement; }

		check(!p.check(11, true, changed), "publisher replacement revokes");
		check(!p.accept(12, true, sources, response(p), 2), "old source recovery rejected");
	}

	SessionRequestOwner q; ready(q);
	check(q.request(UINT64_MAX - 1, UINT64_MAX, 1, sources, true), "near clock limit request");
	check(!q.check(UINT64_MAX, true, sources), "clock limit deadline no overflow");
	check(q.confirmDisarmed(UINT64_MAX, UINT64_MAX - 1, true), "terminal disarm");
	check(!q.request(1, 100, 1, sources, true), "clock wrap cannot restart");
	std::printf("Session request owner: %u checks passed\n", checks);
	return 0;
}
