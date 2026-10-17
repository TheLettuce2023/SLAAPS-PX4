#include "SelectorDisposition.hpp"

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
	EstimatorStart,
	EkfAttitude,
	AttitudeDisposition,
	LocalPositionDisposition,
	SourceEnd,
	Capacity,
	Freeze,
	Sink
};

template<typename T>
struct TimedRecord {
	uint64_t sequence{};
	uint64_t observed_us{};
	T value{};
};

std::array<TimedRecord<DispositionEkfAttitudeObservation>, ObservationCapacity> ekf_attitudes{};
std::array<TimedRecord<DispositionSelectorOutputObservation>, ObservationCapacity> attitude_dispositions{};
std::array<TimedRecord<DispositionSelectorOutputObservation>, ObservationCapacity> local_position_dispositions{};
std::atomic<size_t> ekf_attitude_count{0};
std::atomic<size_t> attitude_disposition_count{0};
std::atomic<size_t> local_position_disposition_count{0};
std::atomic<bool> recording{false};
std::atomic<unsigned> in_flight{0};
std::atomic<unsigned> first_fault{0};
std::atomic<uintptr_t> selector_source{0};
std::atomic<uintptr_t> estimator_source{0};
std::atomic<uintptr_t> expected_selector_stop{0};
std::atomic<uintptr_t> expected_estimator_stop{0};
std::atomic<uint64_t> selector_start_us{0};
std::atomic<uint64_t> estimator_start_us{0};
std::atomic<uint64_t> stop_intent_us{0};
std::atomic<uint64_t> selector_end_us{0};
std::atomic<uint64_t> estimator_end_us{0};
std::atomic_flag owner_busy = ATOMIC_FLAG_INIT;
uint64_t run_identity{};
bool bootstrapped{};
bool finished{};

static_assert(std::atomic<uint64_t>::is_always_lock_free
	      &&std::atomic<uintptr_t>::is_always_lock_free
	      &&std::atomic<size_t>::is_always_lock_free,
	      "P2H disposition bookkeeping must be lock-free");

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

size_t reserve(std::atomic<size_t> &count)
{
	size_t value = count.load();

	while (value < ObservationCapacity) {
		if (count.compare_exchange_weak(value, value + 1)) {
			return value;
		}
	}

	fail(Fault::Capacity);
	return ObservationCapacity;
}

bool selector_matches(const void *selector)
{
	return selector && reinterpret_cast<uintptr_t>(selector) == selector_source.load();
}

bool estimator_matches(const void *estimator)
{
	return estimator && reinterpret_cast<uintptr_t>(estimator) == estimator_source.load();
}

bool zero_acceleration(const float (&acceleration)[3])
{
	const float zero[3] {};
	return std::memcmp(acceleration, zero, sizeof(zero)) == 0;
}

bool empty_output(const DispositionSelectorOutputObservation &observation)
{
	return observation.sample_us == 0 && observation.source_publication_us == 0
	       && observation.previous_sample_us == 0 && observation.age_us == 0
	       && zero_acceleration(observation.source_acceleration)
	       && !observation.monotonic && !observation.fresh && !observation.publish_requested;
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

bool research_disposition_begin(uint64_t run_id) noexcept
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

void research_disposition_selector_start(const void *selector) noexcept
{
	Hook hook;

	if (!hook.active) {
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
}

void research_disposition_estimator_start(const void *estimator, int32_t estimator_instance,
		bool replay, bool multi) noexcept
{
	Hook hook;

	if (!hook.active) {
		return;
	}

	uintptr_t none = 0;
	const uintptr_t address = reinterpret_cast<uintptr_t>(estimator);
	const uint64_t now = hrt_absolute_time();

	if (!selector_source.load() || !address || !now || replay || !multi || estimator_instance != 0
	    || !estimator_source.compare_exchange_strong(none, address)) {
		fail(Fault::EstimatorStart);

	} else {
		estimator_start_us.store(now);
	}
}

void research_disposition_ekf_attitude(const void *estimator,
				       const DispositionEkfAttitudeObservation &observation) noexcept
{
	Hook hook;

	if (!hook.active) {
		return;
	}

	const uint64_t now = hrt_absolute_time();

	if (!estimator_matches(estimator) || observation.estimator_instance != 0
	    || !observation.sample_us || observation.sample_us > now
	    || (observation.publication_succeeded && !observation.publication_attempted)
	    || (observation.publication_attempted && !observation.attitude_valid)) {
		fail(Fault::EkfAttitude);
		return;
	}

	const size_t index = reserve(ekf_attitude_count);

	if (index < ObservationCapacity) {
		ekf_attitudes[index] = {index + 1, now, observation};
	}
}

void research_disposition_attitude(const void *selector,
				   const DispositionSelectorOutputObservation &observation) noexcept
{
	Hook hook;

	if (!hook.active) {
		return;
	}

	const uint64_t now = hrt_absolute_time();
	bool valid = selector_matches(selector) && estimator_source.load()
		     && observation.selected_instance == 0 && observation.source_instance == 0;

	if (observation.candidate_available) {
		valid = valid && observation.sample_us && observation.source_publication_us
			&& observation.sample_us <= observation.source_publication_us
			&& observation.source_publication_us <= now
			&& observation.monotonic == (observation.sample_us > observation.previous_sample_us)
			&& observation.publish_requested == (observation.monotonic && observation.fresh)
			&& zero_acceleration(observation.source_acceleration);

	} else {
		valid = valid && empty_output(observation);
	}

	if (!valid) {
		fail(Fault::AttitudeDisposition);
		return;
	}

	const size_t index = reserve(attitude_disposition_count);

	if (index < ObservationCapacity) {
		attitude_dispositions[index] = {index + 1, now, observation};
	}
}

void research_disposition_local_position(const void *selector,
		const DispositionSelectorOutputObservation &observation) noexcept
{
	Hook hook;

	if (!hook.active) {
		return;
	}

	const uint64_t now = hrt_absolute_time();
	bool valid = selector_matches(selector) && estimator_source.load()
		     && observation.selected_instance == 0 && observation.source_instance == 0;

	if (observation.candidate_available) {
		valid = valid && observation.sample_us && observation.source_publication_us
			&& observation.sample_us <= observation.source_publication_us
			&& observation.source_publication_us <= now
			&& observation.monotonic == (observation.sample_us > observation.previous_sample_us)
			&& observation.publish_requested == (observation.monotonic && observation.fresh);

		for (unsigned axis = 0; axis < 3; ++axis) {
			valid = valid && std::isfinite(observation.source_acceleration[axis]);
		}

	} else {
		valid = valid && empty_output(observation);
	}

	if (!valid) {
		fail(Fault::LocalPositionDisposition);
		return;
	}

	const size_t index = reserve(local_position_disposition_count);

	if (index < ObservationCapacity) {
		local_position_dispositions[index] = {index + 1, now, observation};
	}
}

bool research_disposition_prepare_stop() noexcept
{
	Administration administration;

	if (!administration.held) {
		return false;
	}

	const uintptr_t selector = selector_source.load();
	const uintptr_t estimator = estimator_source.load();

	if (!bootstrapped || finished || !selector || !estimator || !selector_start_us.load()
	    || !estimator_start_us.load() || expected_selector_stop.load() || expected_estimator_stop.load()
	    || selector_end_us.load() || estimator_end_us.load()) {
		fail(Fault::AdministrationOrder);
		return false;
	}

	stop_intent_us.store(hrt_absolute_time());
	expected_selector_stop.store(selector);
	expected_estimator_stop.store(estimator);
	return first_fault.load() == 0;
}

void research_disposition_selector_end(const void *selector) noexcept
{
	Hook hook;

	if (!hook.active) {
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
}

void research_disposition_estimator_end(const void *estimator) noexcept
{
	Hook hook;

	if (!hook.active) {
		return;
	}

	const uintptr_t address = reinterpret_cast<uintptr_t>(estimator);
	const uint64_t now = hrt_absolute_time();
	uint64_t none = 0;

	if (!estimator_matches(estimator) || address != expected_estimator_stop.load()
	    || !selector_end_us.load() || now < selector_end_us.load()
	    || !estimator_end_us.compare_exchange_strong(none, now)) {
		fail(Fault::SourceEnd);
	}
}

bool research_disposition_finish(const char *path) noexcept
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

	const size_t ekf_count = ekf_attitude_count.load();
	const size_t attitude_count = attitude_disposition_count.load();
	const size_t local_count = local_position_disposition_count.load();

	if (!ekf_count || !attitude_count || !local_count
	    || !selector_end_us.load() || !estimator_end_us.load()
	    || expected_selector_stop.load() != selector_source.load()
	    || expected_estimator_stop.load() != estimator_source.load()) {
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
			  "{\"schema\":\"p2h-selector-disposition/v1\",\"run_id\":\"%" PRIu64 "\","
			  "\"fault\":%u,\"counts\":{\"ekf_attitude\":%zu,\"attitude\":%zu,\"local_position\":%zu},"
			  "\"lifecycle\":{\"selector_start_us\":%" PRIu64 ",\"estimator_start_us\":%" PRIu64 ","
			  "\"stop_intent_us\":%" PRIu64 ",\"selector_end_us\":%" PRIu64 ","
			  "\"estimator_end_us\":%" PRIu64 "},\"binding\":[0,0],"
			  "\"health_policy_configured\":false,\"health_qualified\":false,"
			  "\"failover_qualified\":false,\"positive_admission\":false,"
			  "\"eligible_for_control\":false,\"ekf_attitudes\":[\n",
			  run_identity, first_fault.load(), ekf_count, attitude_count, local_count,
			  selector_start_us.load(), estimator_start_us.load(), stop_intent_us.load(),
			  selector_end_us.load(), estimator_end_us.load()) > 0;

	for (size_t i = 0; ok && i < ekf_count && i < ObservationCapacity; ++i) {
		const auto &record = ekf_attitudes[i];
		const auto &value = record.value;
		ok = fprintf(file,
			     "%s{\"sequence\":%" PRIu64 ",\"observed_us\":%" PRIu64 ","
			     "\"sample_us\":%" PRIu64 ",\"estimator_instance\":%" PRIi32 ","
			     "\"attitude_valid\":%s,\"publication_attempted\":%s,"
			     "\"publication_succeeded\":%s}",
			     i ? ",\n" : "", record.sequence, record.observed_us, value.sample_us,
			     value.estimator_instance, value.attitude_valid ? "true" : "false",
			     value.publication_attempted ? "true" : "false",
			     value.publication_succeeded ? "true" : "false") > 0;
	}

	ok = ok && fprintf(file, "\n],\"attitude_dispositions\":[\n") > 0;

	for (size_t i = 0; ok && i < attitude_count && i < ObservationCapacity; ++i) {
		const auto &record = attitude_dispositions[i];
		const auto &value = record.value;
		ok = fprintf(file,
			     "%s{\"sequence\":%" PRIu64 ",\"observed_us\":%" PRIu64 ","
			     "\"sample_us\":%" PRIu64 ",\"source_publication_us\":%" PRIu64 ","
			     "\"previous_sample_us\":%" PRIu64 ",\"age_us\":%" PRIu64 ","
			     "\"selected_instance\":%u,\"source_instance\":%u,"
			     "\"candidate_available\":%s,\"monotonic\":%s,\"fresh\":%s,"
			     "\"publish_requested\":%s}",
			     i ? ",\n" : "", record.sequence, record.observed_us, value.sample_us,
			     value.source_publication_us, value.previous_sample_us, value.age_us,
			     value.selected_instance, value.source_instance,
			     value.candidate_available ? "true" : "false", value.monotonic ? "true" : "false",
			     value.fresh ? "true" : "false", value.publish_requested ? "true" : "false") > 0;
	}

	ok = ok && fprintf(file, "\n],\"local_position_dispositions\":[\n") > 0;

	for (size_t i = 0; ok && i < local_count && i < ObservationCapacity; ++i) {
		const auto &record = local_position_dispositions[i];
		const auto &value = record.value;
		ok = fprintf(file,
			     "%s{\"sequence\":%" PRIu64 ",\"observed_us\":%" PRIu64 ","
			     "\"sample_us\":%" PRIu64 ",\"source_publication_us\":%" PRIu64 ","
			     "\"previous_sample_us\":%" PRIu64 ",\"age_us\":%" PRIu64 ","
			     "\"source_acceleration_bits\":[%" PRIu32 ",%" PRIu32 ",%" PRIu32 "],"
			     "\"selected_instance\":%u,\"source_instance\":%u,"
			     "\"candidate_available\":%s,\"monotonic\":%s,\"fresh\":%s,"
			     "\"publish_requested\":%s}",
			     i ? ",\n" : "", record.sequence, record.observed_us, value.sample_us,
			     value.source_publication_us, value.previous_sample_us, value.age_us,
			     float_bits(value.source_acceleration[0]), float_bits(value.source_acceleration[1]),
			     float_bits(value.source_acceleration[2]), value.selected_instance, value.source_instance,
			     value.candidate_available ? "true" : "false", value.monotonic ? "true" : "false",
			     value.fresh ? "true" : "false", value.publish_requested ? "true" : "false") > 0;
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

extern "C" __EXPORT int research_selector_disposition_main(int argc, char *argv[])
{
	if (argc == 3 && strcmp(argv[1], "begin") == 0) {
		uint64_t run_id{};
		return positive(argv[2], run_id) && research_disposition_begin(run_id) ? 0 : -1;
	}

	if (argc == 2 && strcmp(argv[1], "prepare") == 0) {
		return research_disposition_prepare_stop() ? 0 : -1;
	}

	if (argc == 3 && strcmp(argv[1], "finish") == 0) {
		return research_disposition_finish(argv[2]) ? 0 : -1;
	}

	return -1;
}
