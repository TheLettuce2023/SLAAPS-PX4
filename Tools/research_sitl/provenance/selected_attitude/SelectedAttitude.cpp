#include "SelectedAttitude.hpp"

#include <drivers/drv_hrt.h>
#include <px4_platform_common/posix.h>

#include <array>
#include <atomic>
#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

namespace
{
constexpr size_t ObservationCapacity = 2048; // Existing finite P2 bound, not a vehicle budget.
constexpr unsigned FreezeWaitSteps = 1000; // Bounded evidence cleanup, not an operational deadline.

enum class Fault : unsigned {
	None,
	AdministrationOrder,
	ConcurrentOwner,
	Profile,
	SelectorStart,
	Output,
	SourceEnd,
	Capacity,
	Freeze,
	Sink,
	ConcurrentWriter
};

struct TimedRecord {
	uint64_t sequence{};
	uint64_t observed_us{};
	SelectedAttitudeObservation value{};
};

std::array<TimedRecord, ObservationCapacity> observations{};
std::atomic<bool> recording{false};
std::atomic<unsigned> in_flight{0};
std::atomic<unsigned> first_fault{0};
std::atomic<uintptr_t> selector_source{0};
std::atomic<uintptr_t> expected_selector_stop{0};
std::atomic<uint64_t> selector_start_us{0};
std::atomic<uint64_t> stop_intent_us{0};
std::atomic<uint64_t> selector_end_us{0};
std::atomic_flag writer_busy = ATOMIC_FLAG_INIT;
std::atomic_flag owner_busy = ATOMIC_FLAG_INIT;
uint64_t run_identity{};
size_t observation_count{};
bool bootstrapped{};
bool finished{};

static_assert(std::atomic<uint64_t>::is_always_lock_free
	      &&std::atomic<uintptr_t>::is_always_lock_free,
	      "P2I selected-attitude bookkeeping must be lock-free");

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

bool begin_write()
{
	if (writer_busy.test_and_set()) {
		fail(Fault::ConcurrentWriter);
		return false;
	}

	return true;
}

bool selector_matches(const void *selector)
{
	return selector && reinterpret_cast<uintptr_t>(selector) == selector_source.load();
}

bool finite(const float (&value)[4])
{
	return std::isfinite(value[0]) && std::isfinite(value[1])
	       && std::isfinite(value[2]) && std::isfinite(value[3]);
}

uint32_t float_bits(float value)
{
	uint32_t result{};
	std::memcpy(&result, &value, sizeof(result));
	return result;
}

bool positive(const char *text, uint64_t &value)
{
	if (!text || *text < '1' || *text > '9') {
		return false;
	}

	value = 0;

	for (const char *character = text; *character; ++character) {
		if (*character < '0' || *character > '9'
		    || value > (UINT64_MAX - unsigned(*character - '0')) / 10) {
			return false;
		}

		value = value * 10 + unsigned(*character - '0');
	}

	return true;
}
}

bool research_selected_attitude_begin(uint64_t run_id) noexcept
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

void research_selected_attitude_selector_start(const void *selector) noexcept
{
	Hook hook;

	if (!hook.active || !begin_write()) {
		return;
	}

	uintptr_t none = 0;
	const uintptr_t address = reinterpret_cast<uintptr_t>(selector);
	const uint64_t now = hrt_absolute_time();

	if (!address || !now || !selector_source.compare_exchange_strong(none, address)) {
		fail(Fault::SelectorStart);

	} else {
		selector_start_us.store(now);
	}

	writer_busy.clear();
}

void research_selected_attitude_output(const void *selector,
				       const SelectedAttitudeObservation &observation) noexcept
{
	Hook hook;

	if (!hook.active || !begin_write()) {
		return;
	}

	const uint64_t now = hrt_absolute_time();
	const bool monotonic = !observation_count
			       || (observation.sample_us > observations[observation_count - 1].value.sample_us
				   && observation.selector_publication_us
				   >= observations[observation_count - 1].value.selector_publication_us);

	if (!selector_matches(selector) || !observation.publication_succeeded
	    || observation.selected_instance != 0 || observation.source_instance != 0
	    || !observation.sample_us || observation.sample_us > observation.estimator_publication_us
	    || observation.estimator_publication_us > observation.selector_publication_us
	    || observation.selector_publication_us > now || !monotonic
	    || !finite(observation.source_quaternion) || !finite(observation.selected_quaternion)
	    || !finite(observation.source_delta_quaternion)
	    || !finite(observation.selected_delta_quaternion)) {
		fail(Fault::Output);

	} else if (observation_count == ObservationCapacity) {
		fail(Fault::Capacity);

	} else {
		observations[observation_count] = {observation_count + 1, now, observation};
		++observation_count;
	}

	writer_busy.clear();
}

bool research_selected_attitude_prepare_stop() noexcept
{
	Administration administration;

	if (!administration.held) {
		return false;
	}

	const uintptr_t selector = selector_source.load();

	if (!bootstrapped || finished || !selector || !selector_start_us.load()
	    || expected_selector_stop.load() || selector_end_us.load()) {
		fail(Fault::AdministrationOrder);
		return false;
	}

	stop_intent_us.store(hrt_absolute_time());
	expected_selector_stop.store(selector);
	return first_fault.load() == 0;
}

void research_selected_attitude_selector_end(const void *selector) noexcept
{
	Hook hook;

	if (!hook.active || !begin_write()) {
		return;
	}

	const uintptr_t address = reinterpret_cast<uintptr_t>(selector);
	const uint64_t now = hrt_absolute_time();
	uint64_t none = 0;

	if (!address || address != selector_source.load() || address != expected_selector_stop.load()
	    || !stop_intent_us.load() || now < stop_intent_us.load()
	    || !selector_end_us.compare_exchange_strong(none, now)) {
		fail(Fault::SourceEnd);
	}

	writer_busy.clear();
}

bool research_selected_attitude_finish(const char *path) noexcept
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

	if (!observation_count || !selector_end_us.load()
	    || expected_selector_stop.load() != selector_source.load()) {
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
			  "{\"schema\":\"p2i-selected-attitude/v1\",\"run_id\":\"%" PRIu64 "\","
			  "\"fault\":%u,\"count\":%zu,\"lifecycle\":{\"selector_start_us\":%" PRIu64 ","
			  "\"stop_intent_us\":%" PRIu64 ",\"selector_end_us\":%" PRIu64 "},"
			  "\"binding\":[0,0],\"health_policy_configured\":false,"
			  "\"health_qualified\":false,\"failover_qualified\":false,"
			  "\"positive_admission\":false,\"eligible_for_control\":false,"
			  "\"actuator_publications\":false,\"records\":[\n",
			  run_identity, first_fault.load(), observation_count, selector_start_us.load(),
			  stop_intent_us.load(), selector_end_us.load()) > 0;

	for (size_t i = 0; ok && i < observation_count; ++i) {
		const auto &record = observations[i];
		const auto &value = record.value;
		ok = fprintf(file,
			     "%s{\"sequence\":%" PRIu64 ",\"observed_us\":%" PRIu64 ","
			     "\"sample_us\":%" PRIu64 ",\"estimator_publication_us\":%" PRIu64 ","
			     "\"selector_publication_us\":%" PRIu64 ",\"selected_instance\":%u,"
			     "\"source_instance\":%u,\"source_quaternion_bits\":[%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%" PRIu32 "],"
			     "\"selected_quaternion_bits\":[%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%" PRIu32 "],"
			     "\"source_delta_quaternion_bits\":[%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%" PRIu32 "],"
			     "\"selected_delta_quaternion_bits\":[%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%" PRIu32 "],"
			     "\"source_reset_counter\":%u,\"selected_reset_counter\":%u,"
			     "\"publication_succeeded\":%s}",
			     i ? ",\n" : "", record.sequence, record.observed_us, value.sample_us,
			     value.estimator_publication_us, value.selector_publication_us,
			     value.selected_instance, value.source_instance,
			     float_bits(value.source_quaternion[0]), float_bits(value.source_quaternion[1]),
			     float_bits(value.source_quaternion[2]), float_bits(value.source_quaternion[3]),
			     float_bits(value.selected_quaternion[0]), float_bits(value.selected_quaternion[1]),
			     float_bits(value.selected_quaternion[2]), float_bits(value.selected_quaternion[3]),
			     float_bits(value.source_delta_quaternion[0]), float_bits(value.source_delta_quaternion[1]),
			     float_bits(value.source_delta_quaternion[2]), float_bits(value.source_delta_quaternion[3]),
			     float_bits(value.selected_delta_quaternion[0]), float_bits(value.selected_delta_quaternion[1]),
			     float_bits(value.selected_delta_quaternion[2]), float_bits(value.selected_delta_quaternion[3]),
			     value.source_reset_counter, value.selected_reset_counter,
			     value.publication_succeeded ? "true" : "false") > 0;
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

extern "C" __EXPORT int research_selected_attitude_main(int argc, char *argv[])
{
	if (argc == 3 && strcmp(argv[1], "begin") == 0) {
		uint64_t run_id{};
		return positive(argv[2], run_id) && research_selected_attitude_begin(run_id) ? 0 : -1;
	}

	if (argc == 2 && strcmp(argv[1], "prepare") == 0) {
		return research_selected_attitude_prepare_stop() ? 0 : -1;
	}

	if (argc == 3 && strcmp(argv[1], "finish") == 0) {
		return research_selected_attitude_finish(argv[2]) ? 0 : -1;
	}

	return -1;
}
