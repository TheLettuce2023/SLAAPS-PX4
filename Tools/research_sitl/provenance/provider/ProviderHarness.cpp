#include "CheckedProvider.hpp"

#include <cerrno>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <stdexcept>
#include <time.h>
#include <type_traits>

namespace research_sitl::clock_observation
{
struct ProviderTestPeer {
	static bool begin(CheckedProvider &p, Binding b, Interval i, Reading r) { return p.beginRead(b, i, r); }
	static bool poll(CheckedProvider &p, Reading r, bool retire = false) { return p.pollRead(r, retire); }
	static bool replace(CheckedProvider &p, ReplacementKey key, Binding b, Interval i, Reading r) { return p.replaceRead(key, b, i, r); }
};
}
using namespace research_sitl::clock_observation;
namespace
{
void require(bool ok, const char *reason) { if (!ok) { throw std::runtime_error(reason); } }
Reading at(uint64_t us) { return convert(0, 0, us / 1000000, (us % 1000000) * 1000); }
Binding binding() { return {1, 2, 3, 4, 5, 6, 7, 1}; }
void tests()
{
	static_assert(!std::is_copy_constructible_v<CheckedProvider>);
	static_assert(!std::is_copy_assignable_v<CheckedProvider>);
	static_assert(!std::is_move_constructible_v<CheckedProvider>);
	static_assert(!std::is_move_assignable_v<CheckedProvider>);
	require(convert(-1, EIO, 0, 0).code == ReadCode::SystemError && convert(-1, EIO, 0, 0).error == EIO, "syscall failure");
	require(convert(0, 0, -1, 0).code == ReadCode::InvalidTimespec &&
		convert(0, 0, 1, -1).code == ReadCode::InvalidTimespec &&
		convert(0, 0, 1, 1000000000).code == ReadCode::InvalidTimespec, "invalid timespec");
	require(convert(0, 0, INT64_MAX, 0).code == ReadCode::Overflow && convert(0, 0, 0, 999).code == ReadCode::Zero, "finite representation");
	require(convert(0, 0, 1, 999999999).microseconds == 1999999, "floor conversion");
	{
		CheckedProvider p;
		require(ProviderTestPeer::begin(p, binding(), {1000000, 1000100}, convert(0, 0, 1, 10001)), "submicro setup");
		require(!ProviderTestPeer::poll(p, convert(0, 0, 1, 10000)) && p.terminal().fault == Fault::Regression, "submicro regression");
	}
	{
		CheckedProvider p;
		require(ProviderTestPeer::begin(p, binding(), {10, 20}, at(10)), "inclusive first");
		require(ProviderTestPeer::poll(p, at(20)) && p.state() == State::Active, "inclusive last");
		require(!ProviderTestPeer::poll(p, at(21)) && p.state() == State::Expired, "expiry without ingress");
		const auto key = p.replacementKey();
		Binding next = binding(); next.epoch = 2; next.session = 8;
		require(ProviderTestPeer::replace(p, key, next, {21, 30}, at(21)), "bound replacement");
		Event event{}; require(p.copy(3, event) && event.predecessor_epoch == 1 && event.predecessor_closure == 3, "replacement evidence");
		require(ProviderTestPeer::poll(p, at(25), true) && p.state() == State::Retired, "explicit retirement");
		require(!ProviderTestPeer::replace(p, key, next, {25, 30}, at(25)) && p.terminal().fault == Fault::Order, "stale predecessor");
	}
	{
		CheckedProvider before, after, arithmetic;
		require(!ProviderTestPeer::begin(before, binding(), {10, 20}, at(9)) && before.terminal().fault == Fault::BeforeInterval, "before first");
		require(!ProviderTestPeer::begin(after, binding(), {10, 20}, at(21)) && after.terminal().fault == Fault::AfterInterval, "after last");
		require(!ProviderTestPeer::begin(arithmetic, binding(), {10, UINT64_MAX}, at(10))
			&& arithmetic.terminal().fault == Fault::Interval, "upper uncertainty endpoint");
	}
	{
		CheckedProvider p;
		require(ProviderTestPeer::begin(p, binding(), {10, 100}, at(10)), "read loss setup");
		require(!ProviderTestPeer::poll(p, convert(-1, EIO, 0, 0)) && p.terminal().fault == Fault::Read
			&& p.terminal().reading.error == EIO, "read loss");
		require(!ProviderTestPeer::poll(p, at(11)) && p.count() == 1 && p.terminal().fault == Fault::Read, "sticky loss");
	}
	{
		CheckedProvider p;
		require(ProviderTestPeer::begin(p, binding(), {10, 100}, at(10)), "capacity setup");

		for (size_t i = 1; i < EventCapacity; ++i) { require(ProviderTestPeer::poll(p, at(10)), "equal readings allowed"); }

		require(!ProviderTestPeer::poll(p, at(11), true) && p.count() == EventCapacity &&
			p.terminal().fault == Fault::Capacity && p.terminal().previous_state == State::Active, "reserved terminal capacity");
	}
	{
		CheckedProvider p;
		require(ProviderTestPeer::begin(p, binding(), {10, 20}, at(10)), "epoch setup");
		require(ProviderTestPeer::poll(p, at(11), true), "epoch retire");
		require(!ProviderTestPeer::replace(p, p.replacementKey(), binding(), {11, 20}, at(11))
			&& p.terminal().fault == Fault::Binding, "epoch reuse");
	}
	puts("P3B_CLOSED_CONTRACT_PASS");
}

void observation(uint64_t run)
{
	// Finite experiment duration, not an operational age/lease/uncertainty limit.
	constexpr uint64_t ExperimentIntervalUs = 40000;
	constexpr unsigned PollAttempts = 20;
	CheckedProvider p;
	Binding id = binding();
	id.run = run;
	Reading first = readMonotonic();
	require(first.code == ReadCode::Ok && first.microseconds < UINT64_MAX - ExperimentIntervalUs - 1, "bootstrap clock");
	require(p.begin(id, {first.microseconds, first.microseconds + ExperimentIntervalUs}), "begin real interval");

	for (unsigned i = 0; i < PollAttempts && p.state() == State::Active; ++i) {
		timespec delay{0, 5000000};

		while (nanosleep(&delay, &delay) != 0) { require(errno == EINTR, "experiment sleep"); }

		p.poll();
	}

	require(p.state() == State::Expired, "real interval did not expire");
	const auto previous = p.replacementKey();
	Reading next = readMonotonic();
	require(next.code == ReadCode::Ok && next.microseconds < UINT64_MAX - ExperimentIntervalUs - 1, "replacement clock");
	id.epoch = 2; id.session = 8;
	require(p.replace(previous, id, {next.microseconds, next.microseconds + ExperimentIntervalUs}), "real replacement");
	require(p.poll() && p.retire() && p.terminal().fault == Fault::None, "real retirement");
	printf("{\"schema\":\"p3b-host-clock/v1\",\"map_qualified\":false,\"positive_admission\":false,\"state\":%u,\"fault\":0,\"events\":[\n",
	       static_cast<unsigned>(p.state()));

	for (size_t i = 0; i < p.count(); ++i) {
		Event e{}; require(p.copy(i, e), "event missing");
		printf("%s{\"sequence\":%" PRIu64 ",\"kind\":%u,\"binding\":[%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%"
		       PRIu64 ",%" PRIu64 "],"
		       "\"interval\":[%" PRIu64 ",%" PRIu64 "],\"read\":[%u,%d,%" PRId64 ",%" PRId64 ",%" PRIu64 "],\"predecessor\":[%" PRIu64 ",%" PRIu64 "]}",
		       i ? ",\n" : "", e.sequence, static_cast<unsigned>(e.kind), e.binding.run, e.binding.owner, e.binding.controller,
		       e.binding.producer, e.binding.provider, e.binding.clock, e.binding.session, e.binding.epoch,
		       e.interval.first, e.interval.last, static_cast<unsigned>(e.reading.code), e.reading.error, e.reading.seconds,
		       e.reading.nanoseconds, e.reading.microseconds, e.predecessor_epoch, e.predecessor_closure);
	}

	puts("\n]}");
}
}
int main(int argc, char **argv)
{
	try {
		if (argc == 2 && strcmp(argv[1], "--self-test") == 0) { tests(); }

		else {
			require(argc == 3 && strcmp(argv[1], "--observe") == 0, "usage");
			char *end = nullptr;
			errno = 0;
			const auto run = strtoull(argv[2], &end, 10);
			require(argv[2][0] >= '1' && argv[2][0] <= '9' && end && !*end && !errno && run, "run identity");
			observation(run);
		}

		return 0;

	} catch (const std::exception &error) { fprintf(stderr, "%s\n", error.what()); return 1; }
}
