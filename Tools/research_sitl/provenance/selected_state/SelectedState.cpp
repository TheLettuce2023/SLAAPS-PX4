#include "SelectedState.hpp"

#include <drivers/drv_hrt.h>
#include <px4_platform_common/posix.h>

#include <array>
#include <atomic>
#include <cinttypes>
#include <cstdio>
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
	StatusContract,
	MutationContract,
	Freeze,
	Sink
};

struct SourceBinding {
	int32_t estimator_instance{};
	int32_t attitude_publication_instance{};
	int32_t local_position_publication_instance{};
	int32_t status_publication_instance{};
};

struct Record {
	uint64_t sequence{};
	uint64_t source_time_us{};
	SelectedEstimatorIterationObservation iteration{};
	bool has_status{};
	EstimatorStatusPublicationObservation status{};
};

std::array<Record, RecordCapacity> records{};
EstimatorStatusPublicationObservation pending_status{};
SourceBinding binding{};
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
uint32_t previous_control_changes{};
uint32_t previous_fault_changes{};
bool has_pending_status{};
bool have_previous{};
bool bootstrapped{};
bool finished{};

static_assert(std::atomic<uint64_t>::is_always_lock_free &&std::atomic<uintptr_t>::is_always_lock_free,
	      "P2F source bookkeeping must be lock-free");

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

bool counter_step(uint32_t previous, uint32_t current)
{
	return current >= previous && current - previous <= 1;
}
}

bool research_selected_begin(uint64_t run_id) noexcept
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

void research_selected_source_start(const void *instance, int32_t estimator_instance,
				    int32_t attitude_publication_instance,
				    int32_t local_position_publication_instance,
				    int32_t status_publication_instance, bool replay, bool multi) noexcept
{
	Hook hook;

	if (!hook.active) {
		return;
	}

	if (producer_busy.test_and_set()) {
		fail(Fault::ConcurrentSource);
		return;
	}

	uintptr_t none = 0;
	const uintptr_t address = reinterpret_cast<uintptr_t>(instance);

	if (!address || replay || multi || estimator_instance != 0 || attitude_publication_instance != 0
	    || local_position_publication_instance != 0 || status_publication_instance != 0
	    || !producer.compare_exchange_strong(none, address)) {
		fail(Fault::SourceStart);

	} else {
		binding = {estimator_instance, attitude_publication_instance,
			   local_position_publication_instance, status_publication_instance
			  };
		const uint64_t now = hrt_absolute_time();

		if (!now) {
			fail(Fault::SourceClock);

		} else {
			start_time.store(now);
		}
	}

	producer_busy.clear();
}

void research_selected_status(const void *instance,
			      const EstimatorStatusPublicationObservation &value) noexcept
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

	if (!source_matches(instance) || !value.sample_us || !value.publication_us
	    || value.sample_us > value.publication_us || value.publication_us > now
	    || value.estimator_instance != binding.estimator_instance
	    || value.publication_instance != binding.status_publication_instance
	    || !value.publication_succeeded || has_pending_status) {
		fail(Fault::StatusContract);

	} else {
		pending_status = value;
		has_pending_status = true;
	}

	producer_busy.clear();
}

void research_selected_iteration(const void *instance,
				 const SelectedEstimatorIterationObservation &value) noexcept
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
	const bool counters_valid = !have_previous
				    || (counter_step(previous_control_changes, value.control_status_changes)
					&& counter_step(previous_fault_changes, value.fault_status_changes));
	const bool counters_changed = have_previous
				      && (previous_control_changes != value.control_status_changes
					  || previous_fault_changes != value.fault_status_changes);
	const bool status_matches = !has_pending_status
				    || (pending_status.sample_us <= value.sample_us
					&& pending_status.publication_us <= now
					&& pending_status.control_flags == value.published_control_flags
					&& pending_status.fault_flags == value.published_fault_flags
					&& pending_status.control_status_changes == value.control_status_changes
					&& pending_status.fault_status_changes == value.fault_status_changes
					&& pending_status.estimator_instance == value.estimator_instance);

	if (!source_matches(instance) || !value.sample_us || value.sample_us > now || value.replay || value.multi
	    || value.estimator_instance != binding.estimator_instance) {
		fail(Fault::Profile);

	} else if (!counters_valid || (counters_changed && !has_pending_status)
		   || (!value.update_committed && has_pending_status)
		   || (value.update_committed
		       && (value.control_flags != value.published_control_flags
			   || value.fault_flags != value.published_fault_flags))) {
		fail(Fault::MutationContract);

	} else if (!status_matches) {
		fail(Fault::StatusContract);

	} else if (count == RecordCapacity) {
		fail(Fault::Capacity);

	} else {
		Record &record = records[count];
		record.sequence = count + 1;
		record.source_time_us = now;
		record.iteration = value;
		record.has_status = has_pending_status;

		if (has_pending_status) {
			record.status = pending_status;
		}

		count++;
		previous_control_changes = value.control_status_changes;
		previous_fault_changes = value.fault_status_changes;
		have_previous = true;
		pending_status = {};
		has_pending_status = false;
	}

	producer_busy.clear();
}

bool research_selected_prepare_stop() noexcept
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

void research_selected_source_end(const void *instance) noexcept
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
	    || !end_time.compare_exchange_strong(none, now) || has_pending_status) {
		fail(Fault::SourceEnd);
	}

	producer_busy.clear();
}

bool research_selected_finish(const char *path) noexcept
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
	    || !count || has_pending_status) {
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
			  "{\"schema\":\"p2f-selected-state/v1\",\"run_id\":\"%" PRIu64 "\","
			  "\"fault\":%u,\"count\":%zu,\"start_us\":%" PRIu64 ","
			  "\"stop_intent_us\":%" PRIu64 ",\"end_us\":%" PRIu64 ","
			  "\"binding\":[%" PRIi32 ",%" PRIi32 ",%" PRIi32 ",%" PRIi32 "],"
			  "\"health_policy_configured\":false,\"health_qualified\":false,"
			  "\"positive_admission\":false,\"eligible_for_control\":false,\"records\":[\n",
			  run_identity, first_fault.load(), count, start_time.load(), stop_intent_time.load(),
			  end_time.load(), binding.estimator_instance, binding.attitude_publication_instance,
			  binding.local_position_publication_instance, binding.status_publication_instance) > 0;

	for (size_t i = 0; ok && i < count; ++i) {
		const Record &record = records[i];
		const SelectedEstimatorIterationObservation &iteration = record.iteration;
		ok = fprintf(file,
			     "%s{\"sequence\":%" PRIu64 ",\"source_us\":%" PRIu64 ","
			     "\"sample_us\":%" PRIu64 ",\"update_committed\":%s,\"instance\":%" PRIi32 ","
			     "\"control_flags\":%" PRIu64 ",\"fault_flags\":%" PRIu32 ","
			     "\"published_control_flags\":%" PRIu64 ",\"published_fault_flags\":%" PRIu32 ","
			     "\"control_status_changes\":%" PRIu32 ",\"fault_status_changes\":%" PRIu32 ","
			     "\"status\":",
			     i ? ",\n" : "", record.sequence, record.source_time_us, iteration.sample_us,
			     iteration.update_committed ? "true" : "false", iteration.estimator_instance,
			     iteration.control_flags, iteration.fault_flags, iteration.published_control_flags,
			     iteration.published_fault_flags, iteration.control_status_changes,
			     iteration.fault_status_changes) > 0;

		if (ok && record.has_status) {
			const EstimatorStatusPublicationObservation &status = record.status;
			ok = fprintf(file,
				     "{\"sample_us\":%" PRIu64 ",\"publication_us\":%" PRIu64 ","
				     "\"control_flags\":%" PRIu64 ",\"fault_flags\":%" PRIu32 ","
				     "\"control_status_changes\":%" PRIu32 ",\"fault_status_changes\":%" PRIu32 ","
				     "\"instance\":%" PRIi32 ",\"publication_instance\":%" PRIi32 "}",
				     status.sample_us, status.publication_us, status.control_flags, status.fault_flags,
				     status.control_status_changes, status.fault_status_changes,
				     status.estimator_instance, status.publication_instance) > 0;

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
