#include "NativeMerge.hpp"
#include "ReceiptMerge.hpp"

#include <drivers/drv_hrt.h>
#include <px4_platform_common/posix.h>
#include <atomic>
#include <cinttypes>
#include <cstdio>
#include <optional>
#include <fcntl.h>
#include <unistd.h>

namespace
{
using namespace research_sitl::provenance;
constexpr unsigned PumpBudget = 4; // Finite observer service, not a timing policy.
constexpr unsigned FreezeWaitSteps = 1000;
enum class BridgeFault : unsigned {
	None, AdminOrder, ConcurrentOwner, Profile, SourceStart, SourceEnd,
	SourceClock, ConcurrentSource, QueueRejected, Freeze, OwnerClose, Sink
};
std::optional<ReceiptMerge> owner{}; // Fixed storage; constructed before source release.
std::array<uint64_t, ArchiveCapacity> completion_times{}; // Owner-only completion brackets.
Identity identity{};
std::atomic<bool> recording{false};
std::atomic<unsigned> in_flight{0}, first_fault{0};
std::atomic<uintptr_t> producer{0}, expected_stop{0};
std::atomic<uint64_t> start_time{0}, end_time{0}, stop_intent_time{0};
std::atomic_flag producer_busy = ATOMIC_FLAG_INIT;
std::atomic_flag owner_busy = ATOMIC_FLAG_INIT;
uint64_t source_sequence{}; // Capture writer only; finish reads after quiescence.
bool bootstrapped{}, finished{}; // Serialized administration only.
static_assert(std::atomic<uint64_t>::is_always_lock_free &&std::atomic<uintptr_t>::is_always_lock_free);

void fail(BridgeFault code)
{
	unsigned none = 0;
	first_fault.compare_exchange_strong(none, static_cast<unsigned>(code));
	recording.store(false);
}

struct Administration {
	bool held{!owner_busy.test_and_set()};
	Administration() { if (!held) { fail(BridgeFault::ConcurrentOwner); } }
	~Administration() { if (held) { owner_busy.clear(); } }
};

struct Hook {
	bool active{};
	Hook() { in_flight.fetch_add(1); active = recording.load(); }
	~Hook() { in_flight.fetch_sub(1); }
};

bool consumeOne()
{
	const uint64_t before = hrt_absolute_time();

	if (!owner->step(before)) { return false; }

	const uint64_t after = hrt_absolute_time();
	Record row{};
	owner->copy(owner->count() - 1, row);
	completion_times[owner->count() - 1] = after;

	if (!before || after < before || after < row.receipt.source_time_us) { fail(BridgeFault::SourceClock); return false; }

	return true;
}
}

bool research_merge_begin(uint64_t run_id) noexcept
{
	Administration admin;

	if (!admin.held) { return false; }

	if (bootstrapped || !run_id) { fail(BridgeFault::AdminOrder); return false; }

	bootstrapped = true;
	identity = {run_id, run_id, run_id, 1, 1, 1}; // Namespaced by fresh native run.
	Config config{};
	config.source_count = 1;
	config.sources[0] = identity;
	owner.emplace(config);
	recording.store(true);
	return true;
}

void research_merge_source_start(const void *instance, bool replay, bool multi) noexcept
{
	Hook hook;

	if (!hook.active) { return; }

	uintptr_t none = 0;
	const uintptr_t address = reinterpret_cast<uintptr_t>(instance);

	if (!address || replay || multi || !producer.compare_exchange_strong(none, address)) {
		fail(BridgeFault::SourceStart); return;
	}

	const uint64_t now = hrt_absolute_time();

	if (!now) { fail(BridgeFault::SourceClock); return; }

	start_time.store(now);
}

void research_merge_source_end(const void *instance) noexcept
{
	Hook hook;

	if (!hook.active) { return; }

	const uintptr_t address = reinterpret_cast<uintptr_t>(instance);
	const uint64_t now = hrt_absolute_time();
	uint64_t none = 0;

	if (!address || address != producer.load() || address != expected_stop.load()
	    || !stop_intent_time.load() || now < stop_intent_time.load() || !end_time.compare_exchange_strong(none, now)) {
		fail(BridgeFault::SourceEnd);
	}
}

void research_merge_capture(const void *instance, const EstimatorObservation &value) noexcept
{
	Hook hook;

	if (!hook.active) { return; }

	if (producer_busy.test_and_set()) { fail(BridgeFault::ConcurrentSource); return; }

	const uint64_t now = hrt_absolute_time();

	if (!instance || reinterpret_cast<uintptr_t>(instance) != producer.load() || !start_time.load() || end_time.load()
	    || value.replay || value.multi) { fail(BridgeFault::Profile); }

	else if (!value.sample_us || value.sample_us > now || now < start_time.load() || source_sequence == UINT64_MAX) {
		fail(BridgeFault::SourceClock);

	} else {
		Receipt receipt{};
		receipt.identity = identity;
		receipt.sequence = ++source_sequence;
		receipt.source_time_us = now;
		receipt.kind = source_sequence == 1 ? Kind::Initial : Kind::Sample;
		receipt.observation = {value.sample_us, value.control_flags, value.fault_flags, value.attitude_valid,
				       value.horizontal_valid, value.vertical_position_valid, value.vertical_velocity_valid
				      };

		if (!owner->publish(0, receipt)) { fail(BridgeFault::QueueRejected); }
	}

	producer_busy.clear();
}

bool research_merge_pump() noexcept
{
	Administration admin;

	if (!admin.held) { return false; }

	if (!bootstrapped || finished) { fail(BridgeFault::AdminOrder); return false; }

	if (first_fault.load()) { return false; }

	for (unsigned i = 0; i < PumpBudget; ++i) {
		if (!consumeOne()) { break; }
	}

	if (owner->closed()) { fail(BridgeFault::OwnerClose); }

	return first_fault.load() == 0;
}

bool research_merge_prepare_stop() noexcept
{
	Administration admin;

	if (!admin.held) { return false; }

	const uintptr_t bound = producer.load();

	if (!bootstrapped || finished || !bound || !start_time.load() || expected_stop.load() || end_time.load()) {
		fail(BridgeFault::AdminOrder); return false;
	}

	stop_intent_time.store(hrt_absolute_time());
	expected_stop.store(bound); // Does not permit any subsequent constructor.
	return first_fault.load() == 0;
}

bool research_merge_finish(const char *path) noexcept
{
	Administration admin;

	if (!admin.held) { return false; }

	if (!bootstrapped || finished) { fail(BridgeFault::AdminOrder); return false; }

	finished = true;
	recording.store(false);
	unsigned waits = 0;

	while (in_flight.load() && waits++ < FreezeWaitSteps) { px4_usleep(1000); }

	if (in_flight.load()) { fail(BridgeFault::Freeze); return false; }

	if (!start_time.load() || !end_time.load() || expected_stop.load() != producer.load()) { fail(BridgeFault::SourceEnd); }

	for (uint32_t i = 0; i < QueueCapacity; ++i) {
		if (!consumeOne()) { break; }
	}

	owner->stop(hrt_absolute_time());
	const auto closure = owner->closure();

	if (closure.cause != Fault::OwnerStop || owner->count() != source_sequence || !source_sequence) { fail(BridgeFault::OwnerClose); }

	const int fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);

	if (fd < 0) { fail(BridgeFault::Sink); return false; }

	FILE *file = fdopen(fd, "w");

	if (!file) { close(fd); fail(BridgeFault::Sink); return false; }

	bool ok = fprintf(file, "{\"schema\":\"p2d-native-merge/v1\",\"run_id\":\"%" PRIu64 "\","
			  "\"bridge_fault\":%u,\"owner_fault\":%u,\"source_count\":%" PRIu64 ","
			  "\"start_us\":%" PRIu64 ",\"stop_intent_us\":%" PRIu64 ",\"end_us\":%" PRIu64 ","
			  "\"close_us\":%" PRIu64 ",\"positive_admission\":false,\"clock_map_qualified\":false,"
			  "\"records\":[\n", identity.run, first_fault.load(), static_cast<unsigned>(closure.cause),
			  source_sequence, start_time.load(), stop_intent_time.load(), end_time.load(), closure.owner_time_us) > 0;

	for (size_t i = 0; ok && i < owner->count(); ++i) {
		Record row{};
		owner->copy(i, row);
		const auto &v = row.receipt.observation;
		ok = fprintf(file, "%s{\"owner_order\":%" PRIu64 ",\"owner_begin_us\":%" PRIu64 ",\"owner_end_us\":%" PRIu64 ",\"slot\":%zu,"
			     "\"sequence\":%" PRIu64 ",\"source_us\":%" PRIu64 ",\"kind\":%u,"
			     "\"identity\":[%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 "],"
			     "\"sample_us\":%" PRIu64 ",\"control_flags\":%" PRIu64 ",\"fault_flags\":%" PRIu64 ","
			     "\"validity\":[%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 "]}",
			     i ? ",\n" : "", row.owner_order, row.owner_time_us, completion_times[i], row.slot, row.receipt.sequence,
			     row.receipt.source_time_us, static_cast<unsigned>(row.receipt.kind),
			     row.receipt.identity.run, row.receipt.identity.owner, row.receipt.identity.controller,
			     row.receipt.identity.publisher, row.receipt.identity.lifetime, row.receipt.identity.clock,
			     v[0], v[1], v[2], v[3], v[4], v[5], v[6]) > 0;
	}

	ok = ok && fprintf(file, "\n]}\n") > 0;

	if (fclose(file) != 0) { ok = false; }

	if (!ok) { fail(BridgeFault::Sink); }

	return ok && first_fault.load() == 0;
}
