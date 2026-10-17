#include "NativeClock.hpp"
#include "CheckedProvider.hpp"
#include <px4_platform_common/time.h>
#include <px4_platform_common/posix.h>
#include <array>
#include <atomic>
#include <cerrno>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

namespace
{
using research_sitl::clock_observation::Reading;
using research_sitl::clock_observation::ReadCode;
constexpr size_t LaneCount = 3; // owner, EKF work-queue source, lifecycle CLI
constexpr size_t ReadCapacity = 4096;
constexpr uint64_t InjectedSourceSequence = 26;
constexpr unsigned FreezeWaitSteps = 1000;
enum class Fault : unsigned { None, Order, ConcurrentLane, Capacity, Read, Regression, Freeze, Sink };
struct Record {
	uint64_t sequence{};
	ClockPurpose purpose{};
	Reading reading{};
	bool injected{};
};
struct Lane {
	std::array<Record, ReadCapacity> records{};
	size_t count{};
	Reading previous{};
	bool has_previous{};
	std::atomic_flag busy = ATOMIC_FLAG_INIT;
};
std::array<Lane, LaneCount> lanes{};
std::atomic<unsigned> first_fault{0}, in_flight{0};
std::atomic<bool> active{false};
uint64_t run_id{};
bool started{}, finished{}, inject{}; // sequential CLI administration
size_t fault_lane{};
Record fault_record{}; // unique first-fault writer; export after hook quiescence
static_assert(std::atomic<unsigned>::is_always_lock_free &&std::atomic<bool>::is_always_lock_free);

void fail(Fault fault, size_t lane = 0, Record record = {})
{
	unsigned none = 0;

	if (first_fault.compare_exchange_strong(none, static_cast<unsigned>(fault))) {
		fault_lane = lane; fault_record = record;
	}
}
struct Hook {
	bool held{};
	Hook() { in_flight.fetch_add(1); held = active.load(); }
	~Hook() { in_flight.fetch_sub(1); }
};
size_t laneFor(ClockPurpose purpose)
{
	if (purpose == ClockPurpose::SourceObservation) { return 1; }

	if (purpose == ClockPurpose::SourceStart || purpose == ClockPurpose::SourceEnd) { return 2; }

	return 0;
}
bool writeRecord(FILE *file, const Record &r)
{
	return fprintf(file, "{\"sequence\":%" PRIu64 ",\"purpose\":%u,\"code\":%u,\"error\":%d,"
		       "\"seconds\":%" PRId64 ",\"nanoseconds\":%" PRId64 ",\"microseconds\":%" PRIu64 ",\"injected\":%s}",
		       r.sequence, static_cast<unsigned>(r.purpose), static_cast<unsigned>(r.reading.code), r.reading.error,
		       r.reading.seconds, r.reading.nanoseconds, r.reading.microseconds, r.injected ? "true" : "false") > 0;
}
}

bool research_clock_begin(uint64_t run) noexcept
{
	if (started || !run) { fail(Fault::Order); return false; }

	started = true;
	const char *mode = getenv("P3C_CASE");

	if (!mode || (strcmp(mode, "normal") != 0 && strcmp(mode, "read_error") != 0)) { fail(Fault::Order); return false; }

	inject = strcmp(mode, "read_error") == 0;
	run_id = run;
	active.store(true); // publishes immutable configuration before source scheduling
	return true;
}

uint64_t research_clock_read(ClockPurpose purpose) noexcept
{
	Hook hook;

	if (!hook.held) { return 0; }

	const size_t index = laneFor(purpose);
	Lane &lane = lanes[index];

	if (lane.busy.test_and_set()) { fail(Fault::ConcurrentLane, index); return 0; }

	Record record{};
	record.sequence = lane.count + 1;
	record.purpose = purpose;

	if (lane.count == ReadCapacity) {
		fail(Fault::Capacity, index, record); lane.busy.clear(); return 0;
	}

	record.injected = inject && index == 1 && record.sequence == InjectedSourceSequence;

	if (record.injected) {
		// Deliberately synthetic read error. Never label this an observed OS failure.
		record.reading = research_sitl::clock_observation::convert(-1, EIO, 0, 0);

	} else {
		timespec value{};
		errno = 0;
		const int result = px4_clock_gettime(CLOCK_MONOTONIC, &value);
		const int error = errno;
		record.reading = research_sitl::clock_observation::convert(result, result == 0 ? 0 : error, value.tv_sec, value.tv_nsec);
	}

	const auto &r = record.reading;
	bool ok = r.code == ReadCode::Ok;

	if (!ok) { fail(Fault::Read, index, record); }

	else if (lane.has_previous && (r.seconds < lane.previous.seconds ||
				       (r.seconds == lane.previous.seconds && r.nanoseconds < lane.previous.nanoseconds))) {
		fail(Fault::Regression, index, record); ok = false;
	}

	lane.records[lane.count++] = record;

	if (ok) { lane.previous = r; lane.has_previous = true; }

	lane.busy.clear();
	return ok ? r.microseconds : 0;
}

bool research_clock_faulted() noexcept { return first_fault.load() != 0; }

bool research_clock_finish(const char *path) noexcept
{
	if (!started || finished) { fail(Fault::Order); return false; }

	finished = true;
	active.store(false);
	unsigned waits = 0;

	while (in_flight.load() && waits++ < FreezeWaitSteps) { px4_usleep(1000); }

	if (in_flight.load()) { fail(Fault::Freeze); return false; }

	const int fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);

	if (fd < 0) { fail(Fault::Sink); return false; }

	FILE *file = fdopen(fd, "w");

	if (!file) { close(fd); fail(Fault::Sink); return false; }

	bool ok = fprintf(file, "{\"schema\":\"p3c-native-clock/v1\",\"run_id\":\"%" PRIu64 "\",\"injection_configured\":%s,"
			  "\"fault\":%u,\"fault_lane\":%zu,\"map_qualified\":false,\"positive_admission\":false,\"fault_record\":",
			  run_id, inject ? "true" : "false", first_fault.load(), fault_lane) > 0;
	ok = ok && writeRecord(file, fault_record) && fprintf(file, ",\"lanes\":[") > 0;

	for (size_t lane = 0; ok && lane < LaneCount; ++lane) {
		ok = fprintf(file, "%s[", lane ? "," : "") > 0;

		for (size_t i = 0; ok && i < lanes[lane].count; ++i) {
			ok = (!i || fprintf(file, ",") > 0) && writeRecord(file, lanes[lane].records[i]);
		}

		ok = ok && fprintf(file, "]") > 0;
	}

	ok = ok && fprintf(file, "]}\n") > 0;

	if (fclose(file) != 0) { ok = false; }

	if (!ok) { fail(Fault::Sink); }

	return ok && first_fault.load() == 0;
}
