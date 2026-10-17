#include "CommittedState.hpp"

#include <drivers/drv_hrt.h>
#include <px4_platform_common/posix.h>

#include <array>
#include <atomic>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

namespace
{
constexpr size_t RecordCapacity = 2048; // Existing finite P2B observation bound, not a vehicle budget.
constexpr unsigned FreezeWaitSteps = 1000; // Bounded diagnostic cleanup, not an operational deadline.

enum class Fault : unsigned {
	None,
	AdministrationOrder,
	ConcurrentOwner,
	Profile,
	SourceStart,
	SourceEnd,
	SourceClock,
	ConcurrentSource,
	Capacity,
	OutputContract,
	Freeze,
	Sink
};

struct Record {
	uint64_t sequence{};
	uint64_t source_time_us{};
	EstimatorCommitObservation commit{};
	bool has_attitude{};
	bool has_local_position{};
	CommittedAttitudeObservation attitude{};
	CommittedLocalPositionObservation local_position{};
};

std::array<Record, RecordCapacity> records{};
Record pending{};
std::atomic<bool> recording{false};
std::atomic<unsigned> in_flight{0};
std::atomic<unsigned> first_fault{0};
std::atomic<uintptr_t> producer{0};
std::atomic<uintptr_t> expected_stop{0};
std::atomic<uint64_t> start_time{0};
std::atomic<uint64_t> stop_intent_time{0};
std::atomic<uint64_t> end_time{0};
std::atomic_flag producer_busy = ATOMIC_FLAG_INIT;
std::atomic_flag owner_busy = ATOMIC_FLAG_INIT;
uint64_t run_identity{};
size_t count{};
bool bootstrapped{};
bool finished{};

static_assert(std::atomic<uint64_t>::is_always_lock_free &&std::atomic<uintptr_t>::is_always_lock_free,
	      "P2E source bookkeeping must be lock-free");

void fail(Fault fault)
{
	unsigned none = 0;
	first_fault.compare_exchange_strong(none, static_cast<unsigned>(fault));
	recording.store(false);
}

struct Administration {
	bool held{!owner_busy.test_and_set()};

	Administration()
	{
		if (!held) {
			fail(Fault::ConcurrentOwner);
		}
	}

	~Administration()
	{
		if (held) {
			owner_busy.clear();
		}
	}
};

struct Hook {
	bool active{};

	Hook()
	{
		in_flight.fetch_add(1);
		active = recording.load();
	}

	~Hook()
	{
		in_flight.fetch_sub(1);
	}
};

bool source_matches(const void *instance)
{
	return instance && reinterpret_cast<uintptr_t>(instance) == producer.load();
}

bool local_matches(const EstimatorCommitObservation &commit,
		   const CommittedLocalPositionObservation &local)
{
	return local.sample_us == commit.sample_us
	       && local.horizontal_position_valid == commit.horizontal_position_valid
	       && local.horizontal_velocity_valid == commit.horizontal_velocity_valid
	       && local.vertical_position_valid == commit.vertical_position_valid
	       && local.vertical_velocity_valid == commit.vertical_velocity_valid
	       && local.horizontal_position_reset_count == commit.horizontal_position_reset_count
	       && local.horizontal_velocity_reset_count == commit.horizontal_velocity_reset_count
	       && local.vertical_position_reset_count == commit.vertical_position_reset_count
	       && local.vertical_velocity_reset_count == commit.vertical_velocity_reset_count
	       && local.heading_reset_count == commit.quaternion_reset_count;
}

uint32_t float_bits(float value)
{
	uint32_t bits{};
	static_assert(sizeof(bits) == sizeof(value));
	std::memcpy(&bits, &value, sizeof(bits));
	return bits;
}

bool print_float_bits(FILE *file, const float *values, size_t size)
{
	if (fprintf(file, "[") <= 0) {
		return false;
	}

	for (size_t i = 0; i < size; ++i) {
		if (fprintf(file, "%s%" PRIu32, i ? "," : "", float_bits(values[i])) <= 0) {
			return false;
		}
	}

	return fprintf(file, "]") > 0;
}
}

bool research_committed_begin(uint64_t run_id) noexcept
{
	Administration administration;

	if (!administration.held) {
		return false;
	}

	if (bootstrapped || !run_id) {
		fail(Fault::AdministrationOrder);
		return false;
	}

	bootstrapped = true;
	run_identity = run_id;
	recording.store(true);
	return true;
}

void research_committed_source_start(const void *instance, bool replay, bool multi) noexcept
{
	Hook hook;

	if (!hook.active) {
		return;
	}

	uintptr_t none = 0;
	const uintptr_t address = reinterpret_cast<uintptr_t>(instance);

	if (!address || replay || multi || !producer.compare_exchange_strong(none, address)) {
		fail(Fault::SourceStart);
		return;
	}

	const uint64_t now = hrt_absolute_time();

	if (!now) {
		fail(Fault::SourceClock);
		return;
	}

	start_time.store(now);
}

void research_committed_source_end(const void *instance) noexcept
{
	Hook hook;

	if (!hook.active) {
		return;
	}

	if (producer_busy.test_and_set()) {
		fail(Fault::ConcurrentSource);
		return;
	}

	const uintptr_t address = reinterpret_cast<uintptr_t>(instance);
	const uint64_t now = hrt_absolute_time();
	uint64_t none = 0;

	if (!address || address != producer.load() || address != expected_stop.load()
	    || !stop_intent_time.load() || now < stop_intent_time.load()
	    || !end_time.compare_exchange_strong(none, now) || pending.has_attitude
	    || pending.has_local_position) {
		fail(Fault::SourceEnd);
	}

	producer_busy.clear();
}

void research_committed_attitude(const void *instance, const CommittedAttitudeObservation &value) noexcept
{
	Hook hook;

	if (!hook.active) {
		return;
	}

	if (producer_busy.test_and_set()) {
		fail(Fault::ConcurrentSource);
		return;
	}

	if (!source_matches(instance) || !value.sample_us || pending.has_attitude) {
		fail(Fault::OutputContract);

	} else {
		pending.has_attitude = true;
		pending.attitude = value;
	}

	producer_busy.clear();
}

void research_committed_local_position(const void *instance,
				       const CommittedLocalPositionObservation &value) noexcept
{
	Hook hook;

	if (!hook.active) {
		return;
	}

	if (producer_busy.test_and_set()) {
		fail(Fault::ConcurrentSource);
		return;
	}

	if (!source_matches(instance) || !value.sample_us || pending.has_local_position) {
		fail(Fault::OutputContract);

	} else {
		pending.has_local_position = true;
		pending.local_position = value;
	}

	producer_busy.clear();
}

void research_committed_iteration(const void *instance, const EstimatorCommitObservation &value) noexcept
{
	Hook hook;

	if (!hook.active) {
		return;
	}

	if (producer_busy.test_and_set()) {
		fail(Fault::ConcurrentSource);
		return;
	}

	const uint64_t now = hrt_absolute_time();
	const bool attitude_contract = pending.has_attitude == value.attitude_valid
				       && (!pending.has_attitude
					   || (pending.attitude.sample_us == value.sample_us
					       && pending.attitude.quaternion_reset_count == value.quaternion_reset_count));
	const bool local_contract = pending.has_local_position == value.update_committed
				    && (!pending.has_local_position || local_matches(value, pending.local_position));

	if (!source_matches(instance) || !value.sample_us || value.sample_us > now
	    || value.replay || value.multi || !value.accelerometer_device_id || !value.gyroscope_device_id) {
		fail(Fault::Profile);

	} else if (!attitude_contract || !local_contract) {
		fail(Fault::OutputContract);

	} else if (count == RecordCapacity) {
		fail(Fault::Capacity);

	} else {
		pending.sequence = count + 1;
		pending.source_time_us = now;
		pending.commit = value;
		records[count++] = pending;
		pending = {};
	}

	producer_busy.clear();
}

bool research_committed_prepare_stop() noexcept
{
	Administration administration;

	if (!administration.held) {
		return false;
	}

	const uintptr_t bound = producer.load();

	if (!bootstrapped || finished || !bound || !start_time.load() || expected_stop.load() || end_time.load()) {
		fail(Fault::AdministrationOrder);
		return false;
	}

	stop_intent_time.store(hrt_absolute_time());
	expected_stop.store(bound);
	return first_fault.load() == 0;
}

bool research_committed_finish(const char *path) noexcept
{
	Administration administration;

	if (!administration.held) {
		return false;
	}

	if (!bootstrapped || finished) {
		fail(Fault::AdministrationOrder);
		return false;
	}

	finished = true;
	recording.store(false);
	unsigned waits = 0;

	while (in_flight.load() && waits++ < FreezeWaitSteps) {
		px4_usleep(1000);
	}

	if (in_flight.load()) {
		fail(Fault::Freeze);
		return false;
	}

	if (!start_time.load() || !end_time.load() || expected_stop.load() != producer.load()
	    || !count || pending.has_attitude || pending.has_local_position) {
		fail(Fault::SourceEnd);
	}

	const int fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);

	if (fd < 0) {
		fail(Fault::Sink);
		return false;
	}

	FILE *file = fdopen(fd, "w");

	if (!file) {
		close(fd);
		fail(Fault::Sink);
		return false;
	}

	bool ok = fprintf(file,
			  "{\"schema\":\"p2e-committed-state/v1\",\"run_id\":\"%" PRIu64 "\","
			  "\"fault\":%u,\"count\":%zu,\"start_us\":%" PRIu64 ","
			  "\"stop_intent_us\":%" PRIu64 ",\"end_us\":%" PRIu64 ","
			  "\"health_qualified\":false,\"positive_admission\":false,"
			  "\"eligible_for_control\":false,\"records\":[\n",
			  run_identity, first_fault.load(), count, start_time.load(),
			  stop_intent_time.load(), end_time.load()) > 0;

	for (size_t i = 0; ok && i < count; ++i) {
		const Record &record = records[i];
		const EstimatorCommitObservation &commit = record.commit;
		ok = fprintf(file,
			     "%s{\"sequence\":%" PRIu64 ",\"source_us\":%" PRIu64 ","
			     "\"sample_us\":%" PRIu64 ",\"update_committed\":%s,"
			     "\"sensor\":[%" PRIu32 ",%" PRIu32 ",%u,%u],"
			     "\"control_flags\":%" PRIu64 ",\"fault_flags\":%" PRIu32 ","
			     "\"validity\":[%u,%u,%u,%u,%u],\"resets\":[%u,%u,%u,%u,%u],"
			     "\"attitude\":",
			     i ? ",\n" : "", record.sequence, record.source_time_us, commit.sample_us,
			     commit.update_committed ? "true" : "false", commit.accelerometer_device_id,
			     commit.gyroscope_device_id, commit.accelerometer_calibration_count,
			     commit.gyroscope_calibration_count, commit.control_flags, commit.fault_flags,
			     commit.attitude_valid, commit.horizontal_position_valid,
			     commit.horizontal_velocity_valid, commit.vertical_position_valid,
			     commit.vertical_velocity_valid,
			     commit.quaternion_reset_count, commit.horizontal_position_reset_count,
			     commit.horizontal_velocity_reset_count, commit.vertical_position_reset_count,
			     commit.vertical_velocity_reset_count) > 0;

		if (ok && record.has_attitude) {
			ok = fprintf(file, "{\"quaternion_bits\":") > 0
			     && print_float_bits(file, record.attitude.quaternion, 4)
			     && fprintf(file, ",\"reset\":%u}", record.attitude.quaternion_reset_count) > 0;

		} else if (ok) {
			ok = fprintf(file, "null") > 0;
		}

		ok = ok && fprintf(file, ",\"local_position\":") > 0;

		if (ok && record.has_local_position) {
			const CommittedLocalPositionObservation &local = record.local_position;
			ok = fprintf(file, "{\"position_bits\":") > 0
			     && print_float_bits(file, local.position_ned_m, 3)
			     && fprintf(file, ",\"velocity_bits\":") > 0
			     && print_float_bits(file, local.velocity_ned_m_s, 3)
			     && fprintf(file, ",\"acceleration_bits\":") > 0
			     && print_float_bits(file, local.acceleration_ned_m_s2, 3)
			     && fprintf(file, ",\"validity\":[%u,%u,%u,%u],\"resets\":[%u,%u,%u,%u,%u]}",
					local.horizontal_position_valid, local.horizontal_velocity_valid,
					local.vertical_position_valid, local.vertical_velocity_valid,
					local.heading_reset_count, local.horizontal_position_reset_count,
					local.horizontal_velocity_reset_count, local.vertical_position_reset_count,
					local.vertical_velocity_reset_count) > 0;

		} else if (ok) {
			ok = fprintf(file, "null") > 0;
		}

		ok = ok && fprintf(file, "}") > 0;
	}

	ok = ok && fprintf(file, "\n]}\n") > 0;

	if (fclose(file) != 0) {
		ok = false;
	}

	if (!ok) {
		fail(Fault::Sink);
	}

	return ok && first_fault.load() == 0;
}
