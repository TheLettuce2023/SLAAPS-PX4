#include "SelectorHandoff.hpp"

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
constexpr uint8_t EstimatorCount = 2;
constexpr uint8_t InvalidInstance = UINT8_MAX;
constexpr size_t SelectionCapacity = 8;
constexpr size_t StatusCapacity = 128;
constexpr size_t OutputCapacity = 2048; // Existing finite P2 evidence bound, not a vehicle budget.
constexpr unsigned FreezeWaitSteps = 1000; // Bounded cleanup, not an operational deadline.

enum class Fault : unsigned {
	None,
	AdministrationOrder,
	ConcurrentOwner,
	Profile,
	SelectorStart,
	EstimatorStart,
	EstimatorEnd,
	Selection,
	Status,
	Attitude,
	LocalPosition,
	Capacity,
	Freeze,
	Sink,
	ConcurrentWriter,
	Lifecycle
};

template<typename T>
struct TimedRecord {
	uint64_t sequence{};
	uint64_t observed_us{};
	T value{};
};

struct EstimatorLifecycle {
	std::atomic<uintptr_t> source{0};
	std::atomic<uint64_t> start_us{0};
	std::atomic<uint64_t> end_us{0};
	HandoffEstimatorBinding binding{};
};

std::array<EstimatorLifecycle, EstimatorCount> estimators{};
std::array<TimedRecord<HandoffSelectionObservation>, SelectionCapacity> selections{};
std::array<TimedRecord<HandoffStatusObservation>, StatusCapacity> statuses{};
std::array<TimedRecord<HandoffAttitudeObservation>, OutputCapacity> attitudes{};
std::array<TimedRecord<HandoffLocalPositionObservation>, OutputCapacity> local_positions{};
std::atomic<bool> accepting{false};
std::atomic<unsigned> selector_in_flight{0};
std::atomic<unsigned> first_fault{0};
std::atomic<uintptr_t> selector_source{0};
std::atomic<uint64_t> selector_start_us{0};
std::atomic<uint64_t> selector_end_us{0};
std::atomic<uint64_t> stop_intent_us{0};
std::atomic_flag selector_writer = ATOMIC_FLAG_INIT;
std::atomic_flag owner_busy = ATOMIC_FLAG_INIT;
uint64_t run_identity{};
size_t selection_count{};
size_t status_count{};
size_t attitude_count{};
size_t local_position_count{};
bool bootstrapped{};
bool stop_prepared{};
bool finished{};

static_assert(std::atomic<uint64_t>::is_always_lock_free
	      &&std::atomic<uintptr_t>::is_always_lock_free,
	      "P2J lifecycle bookkeeping must be lock-free");

void fail(Fault fault)
{
	unsigned none = 0;
	first_fault.compare_exchange_strong(none, static_cast<unsigned>(fault));
	accepting.store(false);
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

struct SelectorHook {
	bool active{};

	SelectorHook()
	{
		selector_in_flight.fetch_add(1);
		active = accepting.load();
	}

	~SelectorHook()
	{
		selector_in_flight.fetch_sub(1);
	}
};

bool begin_selector_write()
{
	if (selector_writer.test_and_set()) {
		fail(Fault::ConcurrentWriter);
		return false;
	}

	return true;
}

bool selector_matches(const void *selector)
{
	return selector && reinterpret_cast<uintptr_t>(selector) == selector_source.load();
}

template<size_t Size>
bool finite(const float (&values)[Size])
{
	for (float value : values) {
		if (!std::isfinite(value)) {
			return false;
		}
	}

	return true;
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

bool output_time_valid(uint64_t sample_us, uint64_t estimator_us, uint64_t selector_us,
		       uint64_t now, uint64_t previous_sample, uint64_t previous_selector)
{
	return sample_us > previous_sample && sample_us <= estimator_us
	       && estimator_us <= selector_us && selector_us <= now
	       && selector_us >= previous_selector;
}

bool close_file(FILE *file)
{
	return fclose(file) == 0;
}
}

bool research_handoff_begin(uint64_t run_id) noexcept
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
	accepting.store(true);
	return true;
}

void research_handoff_selector_start(const void *selector) noexcept
{
	const uintptr_t address = reinterpret_cast<uintptr_t>(selector);
	uintptr_t none = 0;
	const uint64_t now = hrt_absolute_time();

	if (!bootstrapped || !address || !now
	    || !selector_source.compare_exchange_strong(none, address)) {
		fail(Fault::SelectorStart);
		return;
	}

	selector_start_us.store(now);
}

void research_handoff_selector_end(const void *selector) noexcept
{
	const uintptr_t address = reinterpret_cast<uintptr_t>(selector);
	uint64_t none = 0;
	const uint64_t now = hrt_absolute_time();

	if (!stop_prepared || !address || address != selector_source.load() || !now
	    || !selector_end_us.compare_exchange_strong(none, now)) {
		fail(Fault::Lifecycle);
	}
}

void research_handoff_estimator_start(const void *estimator,
				      const HandoffEstimatorBinding &binding) noexcept
{
	const uintptr_t address = reinterpret_cast<uintptr_t>(estimator);
	const int instance = binding.estimator_instance;

	if (!bootstrapped || !address || instance < 0 || instance >= EstimatorCount
	    || binding.attitude_instance != instance || binding.local_position_instance != instance
	    || binding.status_instance != instance || binding.imu_instance != instance
	    || binding.replay_mode || !binding.multi_mode) {
		fail(Fault::Profile);
		return;
	}

	EstimatorLifecycle &lifecycle = estimators[instance];
	uintptr_t none = 0;

	if (lifecycle.start_us.load() || lifecycle.end_us.load()
	    || !lifecycle.source.compare_exchange_strong(none, address)) {
		fail(Fault::EstimatorStart);
		return;
	}

	lifecycle.binding = binding;
	const uint64_t now = hrt_absolute_time();

	if (!now) {
		fail(Fault::EstimatorStart);
		return;
	}

	lifecycle.start_us.store(now);
}

void research_handoff_estimator_end(const void *estimator) noexcept
{
	const uintptr_t address = reinterpret_cast<uintptr_t>(estimator);
	const uint64_t now = hrt_absolute_time();

	for (EstimatorLifecycle &lifecycle : estimators) {
		if (address && lifecycle.source.load() == address) {
			uint64_t none = 0;

			if (!now || !lifecycle.start_us.load()
			    || !lifecycle.end_us.compare_exchange_strong(none, now)) {
				fail(Fault::EstimatorEnd);
			}

			return;
		}
	}

	fail(Fault::EstimatorEnd);
}

void research_handoff_selection(const void *selector,
				const HandoffSelectionObservation &observation) noexcept
{
	SelectorHook hook;

	if (!hook.active || !begin_selector_write()) {
		return;
	}

	const uint64_t now = hrt_absolute_time();
	const bool old_valid = observation.old_instance < EstimatorCount;
	const bool profile = observation.new_instance < EstimatorCount
			     && (old_valid || observation.old_instance == InvalidInstance)
			     && observation.available_instances >= observation.new_instance + 1
			     && observation.available_instances <= EstimatorCount
			     && observation.accelerometer_device_id
			     && observation.gyroscope_device_id;
	const bool monotonic = !selection_count
			       || (observation.selection_us > selections[selection_count - 1].value.selection_us
				   && observation.instance_changed_count
				   == selections[selection_count - 1].value.instance_changed_count + 1);

	if (!selector_matches(selector) || !profile || !observation.selection_us
	    || observation.selection_us > now || !observation.instance_changed_count || !monotonic
	    || !observation.publication_succeeded || selection_count == SelectionCapacity) {
		fail(selection_count == SelectionCapacity ? Fault::Capacity : Fault::Selection);

	} else {
		selections[selection_count] = {selection_count + 1, now, observation};
		selection_count++;
	}

	selector_writer.clear();
}

void research_handoff_status(const void *selector,
			     const HandoffStatusObservation &observation) noexcept
{
	if (observation.available_instances < EstimatorCount) {
		return;
	}

	SelectorHook hook;

	if (!hook.active || !begin_selector_write()) {
		return;
	}

	const uint64_t now = hrt_absolute_time();
	const bool monotonic = !status_count
			       || observation.publication_us > statuses[status_count - 1].value.publication_us;

	if (!selector_matches(selector) || observation.primary_instance >= EstimatorCount
	    || observation.available_instances != EstimatorCount
	    || !observation.publication_us || observation.publication_us > now
	    || !observation.last_instance_change_us
	    || observation.last_instance_change_us > observation.publication_us
	    || !observation.instance_changed_count || !observation.accelerometer_device_id
	    || !observation.gyroscope_device_id || !finite(observation.combined_test_ratio)
	    || !monotonic || !observation.publication_succeeded || status_count == StatusCapacity) {
		fail(status_count == StatusCapacity ? Fault::Capacity : Fault::Status);

	} else {
		statuses[status_count] = {status_count + 1, now, observation};
		status_count++;
	}

	selector_writer.clear();
}

void research_handoff_attitude(const void *selector,
			       const HandoffAttitudeObservation &observation) noexcept
{
	SelectorHook hook;

	if (!hook.active || !begin_selector_write()) {
		return;
	}

	const uint64_t now = hrt_absolute_time();
	const uint64_t previous_sample = attitude_count ? attitudes[attitude_count - 1].value.sample_us : 0;
	const uint64_t previous_selector = attitude_count
					   ? attitudes[attitude_count - 1].value.selector_publication_us : 0;

	if (!selector_matches(selector) || observation.selected_instance >= EstimatorCount
	    || observation.source_instance != observation.selected_instance
	    || !output_time_valid(observation.sample_us, observation.estimator_publication_us,
				  observation.selector_publication_us, now,
				  previous_sample, previous_selector)
	    || !finite(observation.source_quaternion) || !finite(observation.selected_quaternion)
	    || !finite(observation.source_delta_quaternion)
	    || !finite(observation.selected_delta_quaternion)
	    || !observation.publication_succeeded || attitude_count == OutputCapacity) {
		fail(attitude_count == OutputCapacity ? Fault::Capacity : Fault::Attitude);

	} else {
		attitudes[attitude_count] = {attitude_count + 1, now, observation};
		attitude_count++;
	}

	selector_writer.clear();
}

void research_handoff_local_position(const void *selector,
				     const HandoffLocalPositionObservation &observation) noexcept
{
	SelectorHook hook;

	if (!hook.active || !begin_selector_write()) {
		return;
	}

	const uint64_t now = hrt_absolute_time();
	const uint64_t previous_sample = local_position_count
					 ? local_positions[local_position_count - 1].value.sample_us : 0;
	const uint64_t previous_selector = local_position_count
					   ? local_positions[local_position_count - 1].value.selector_publication_us : 0;

	if (!selector_matches(selector) || observation.selected_instance >= EstimatorCount
	    || observation.source_instance != observation.selected_instance
	    || !output_time_valid(observation.sample_us, observation.estimator_publication_us,
				  observation.selector_publication_us, now,
				  previous_sample, previous_selector)
	    || !finite(observation.source_acceleration) || !finite(observation.selected_acceleration)
	    || !observation.publication_succeeded || local_position_count == OutputCapacity) {
		fail(local_position_count == OutputCapacity ? Fault::Capacity : Fault::LocalPosition);

	} else {
		local_positions[local_position_count] = {local_position_count + 1, now, observation};
		local_position_count++;
	}

	selector_writer.clear();
}

bool research_handoff_prepare_stop() noexcept
{
	Administration administration;

	if (!administration.held) {
		return false;
	}

	bool found_initial = false;
	bool found_automatic_handoff = false;
	bool found_attitude_one = false;
	bool found_local_position_one = false;

	for (size_t index = 0; index < selection_count; ++index) {
		const HandoffSelectionObservation &value = selections[index].value;
		found_initial = found_initial
				|| (value.old_instance == InvalidInstance && value.new_instance == 0);
		found_automatic_handoff = found_automatic_handoff
					  || (value.old_instance == 0 && value.new_instance == 1
					      && value.requested_instance == InvalidInstance
					      && !value.old_healthy && value.old_timeout
					      && value.new_healthy && !value.new_timeout);
	}

	for (size_t index = 0; index < attitude_count; ++index) {
		found_attitude_one = found_attitude_one || attitudes[index].value.selected_instance == 1;
	}

	for (size_t index = 0; index < local_position_count; ++index) {
		found_local_position_one = found_local_position_one
					   || local_positions[index].value.selected_instance == 1;
	}

	const uint64_t estimator_zero_end = estimators[0].end_us.load();
	bool handoff_after_end = false;

	for (size_t index = 0; index < selection_count; ++index) {
		if (selections[index].value.old_instance == 0 && selections[index].value.new_instance == 1) {
			handoff_after_end = estimator_zero_end
					    && estimator_zero_end <= selections[index].value.selection_us;
		}
	}

	if (!bootstrapped || stop_prepared || first_fault.load() || !selector_source.load()
	    || !selector_start_us.load() || !estimators[0].start_us.load()
	    || !estimators[1].start_us.load() || !estimator_zero_end || estimators[1].end_us.load()
	    || !found_initial || !found_automatic_handoff || !handoff_after_end || !status_count
	    || !found_attitude_one || !found_local_position_one) {
		fail(Fault::AdministrationOrder);
		return false;
	}

	stop_prepared = true;
	stop_intent_us.store(hrt_absolute_time());
	accepting.store(false);

	for (unsigned step = 0; step < FreezeWaitSteps; ++step) {
		if (!selector_in_flight.load()) {
			return true;
		}

		px4_usleep(1);
	}

	fail(Fault::Freeze);
	return false;
}

bool research_handoff_finish(const char *path) noexcept
{
	Administration administration;

	if (!administration.held) {
		return false;
	}

	if (!stop_prepared || finished || !path) {
		fail(Fault::AdministrationOrder);
		return false;
	}

	finished = true;

	for (unsigned step = 0; step < FreezeWaitSteps && selector_in_flight.load(); ++step) {
		px4_usleep(1);
	}

	if (selector_in_flight.load()) {
		fail(Fault::Freeze);
	}

	const uint64_t selector_end = selector_end_us.load();
	const uint64_t estimator_zero_end = estimators[0].end_us.load();
	const uint64_t estimator_one_end = estimators[1].end_us.load();

	if (!selector_end || !estimator_zero_end || !estimator_one_end
	    || selector_end < stop_intent_us.load() || estimator_one_end < selector_end) {
		fail(Fault::Lifecycle);
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
			  "{\"schema\":\"p2j-selector-handoff/v1\",\"run_id\":\"%" PRIu64 "\","
			  "\"fault\":%u,\"counts\":{\"selection\":%zu,\"status\":%zu,"
			  "\"attitude\":%zu,\"local_position\":%zu},"
			  "\"lifecycle\":{\"selector_start_us\":%" PRIu64 ",\"stop_intent_us\":%" PRIu64 ","
			  "\"selector_end_us\":%" PRIu64 ",\"estimator_start_us\":[%" PRIu64 ",%" PRIu64 "],"
			  "\"estimator_end_us\":[%" PRIu64 ",%" PRIu64 "]},"
			  "\"bindings\":[[%d,%d,%d,%d,%d],[%d,%d,%d,%d,%d]],"
			  "\"health_policy_configured\":false,\"health_qualified\":false,"
			  "\"failover_qualified\":false,\"complete_mutation_coverage\":false,"
			  "\"deployment_sensors_qualified\":false,\"positive_admission\":false,"
			  "\"eligible_for_control\":false,\"actuator_publications\":false,"
			  "\"selections\":[",
			  run_identity, first_fault.load(), selection_count, status_count,
			  attitude_count, local_position_count, selector_start_us.load(),
			  stop_intent_us.load(), selector_end,
			  estimators[0].start_us.load(), estimators[1].start_us.load(),
			  estimator_zero_end, estimator_one_end,
			  estimators[0].binding.estimator_instance, estimators[0].binding.attitude_instance,
			  estimators[0].binding.local_position_instance, estimators[0].binding.status_instance,
			  estimators[0].binding.imu_instance, estimators[1].binding.estimator_instance,
			  estimators[1].binding.attitude_instance, estimators[1].binding.local_position_instance,
			  estimators[1].binding.status_instance, estimators[1].binding.imu_instance) > 0;

	for (size_t index = 0; ok && index < selection_count; ++index) {
		const auto &record = selections[index];
		const auto &value = record.value;
		ok = fprintf(file,
			     "%s{\"sequence\":%" PRIu64 ",\"observed_us\":%" PRIu64 ","
			     "\"selection_us\":%" PRIu64 ",\"instance_changed_count\":%" PRIu32 ","
			     "\"accelerometer_device_id\":%" PRIu32 ",\"gyroscope_device_id\":%" PRIu32 ","
			     "\"old_instance\":%u,\"new_instance\":%u,\"available_instances\":%u,"
			     "\"requested_instance\":%u,\"old_healthy\":%s,\"old_timeout\":%s,"
			     "\"new_healthy\":%s,\"new_timeout\":%s,\"publication_succeeded\":%s}",
			     index ? "," : "", record.sequence, record.observed_us, value.selection_us,
			     value.instance_changed_count, value.accelerometer_device_id,
			     value.gyroscope_device_id, value.old_instance, value.new_instance,
			     value.available_instances, value.requested_instance,
			     value.old_healthy ? "true" : "false", value.old_timeout ? "true" : "false",
			     value.new_healthy ? "true" : "false", value.new_timeout ? "true" : "false",
			     value.publication_succeeded ? "true" : "false") > 0;
	}

	ok = ok && fprintf(file, "],\"statuses\":[") > 0;

	for (size_t index = 0; ok && index < status_count; ++index) {
		const auto &record = statuses[index];
		const auto &value = record.value;
		ok = fprintf(file,
			     "%s{\"sequence\":%" PRIu64 ",\"observed_us\":%" PRIu64 ","
			     "\"publication_us\":%" PRIu64 ",\"last_instance_change_us\":%" PRIu64 ","
			     "\"instance_changed_count\":%" PRIu32 ",\"accelerometer_device_id\":%" PRIu32 ","
			     "\"gyroscope_device_id\":%" PRIu32 ",\"combined_test_ratio_bits\":[%" PRIu32 ",%" PRIu32 "],"
			     "\"primary_instance\":%u,\"available_instances\":%u,\"healthy\":[%s,%s],"
			     "\"publication_succeeded\":%s}",
			     index ? "," : "", record.sequence, record.observed_us, value.publication_us,
			     value.last_instance_change_us, value.instance_changed_count,
			     value.accelerometer_device_id, value.gyroscope_device_id,
			     float_bits(value.combined_test_ratio[0]), float_bits(value.combined_test_ratio[1]),
			     value.primary_instance, value.available_instances,
			     value.healthy[0] ? "true" : "false", value.healthy[1] ? "true" : "false",
			     value.publication_succeeded ? "true" : "false") > 0;
	}

	ok = ok && fprintf(file, "],\"attitudes\":[") > 0;

	for (size_t index = 0; ok && index < attitude_count; ++index) {
		const auto &record = attitudes[index];
		const auto &value = record.value;
		ok = fprintf(file,
			     "%s{\"sequence\":%" PRIu64 ",\"observed_us\":%" PRIu64 ","
			     "\"sample_us\":%" PRIu64 ",\"estimator_publication_us\":%" PRIu64 ","
			     "\"selector_publication_us\":%" PRIu64 ",\"source_quaternion_bits\":[%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%" PRIu32 "],"
			     "\"selected_quaternion_bits\":[%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%" PRIu32 "],"
			     "\"source_delta_quaternion_bits\":[%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%" PRIu32 "],"
			     "\"selected_delta_quaternion_bits\":[%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%" PRIu32 "],"
			     "\"selected_instance\":%u,\"source_instance\":%u,"
			     "\"source_reset_counter\":%u,\"selected_reset_counter\":%u,"
			     "\"publication_succeeded\":%s}",
			     index ? "," : "", record.sequence, record.observed_us, value.sample_us,
			     value.estimator_publication_us, value.selector_publication_us,
			     float_bits(value.source_quaternion[0]), float_bits(value.source_quaternion[1]),
			     float_bits(value.source_quaternion[2]), float_bits(value.source_quaternion[3]),
			     float_bits(value.selected_quaternion[0]), float_bits(value.selected_quaternion[1]),
			     float_bits(value.selected_quaternion[2]), float_bits(value.selected_quaternion[3]),
			     float_bits(value.source_delta_quaternion[0]), float_bits(value.source_delta_quaternion[1]),
			     float_bits(value.source_delta_quaternion[2]), float_bits(value.source_delta_quaternion[3]),
			     float_bits(value.selected_delta_quaternion[0]), float_bits(value.selected_delta_quaternion[1]),
			     float_bits(value.selected_delta_quaternion[2]), float_bits(value.selected_delta_quaternion[3]),
			     value.selected_instance, value.source_instance, value.source_reset_counter,
			     value.selected_reset_counter, value.publication_succeeded ? "true" : "false") > 0;
	}

	ok = ok && fprintf(file, "],\"local_positions\":[") > 0;

	for (size_t index = 0; ok && index < local_position_count; ++index) {
		const auto &record = local_positions[index];
		const auto &value = record.value;
		ok = fprintf(file,
			     "%s{\"sequence\":%" PRIu64 ",\"observed_us\":%" PRIu64 ","
			     "\"sample_us\":%" PRIu64 ",\"estimator_publication_us\":%" PRIu64 ","
			     "\"selector_publication_us\":%" PRIu64 ",\"source_acceleration_bits\":[%" PRIu32 ",%" PRIu32 ",%" PRIu32 "],"
			     "\"selected_acceleration_bits\":[%" PRIu32 ",%" PRIu32 ",%" PRIu32 "],"
			     "\"source_reset_counters\":[%u,%u,%u,%u,%u,%u],"
			     "\"selected_reset_counters\":[%u,%u,%u,%u,%u,%u],"
			     "\"selected_instance\":%u,\"source_instance\":%u,\"publication_succeeded\":%s}",
			     index ? "," : "", record.sequence, record.observed_us, value.sample_us,
			     value.estimator_publication_us, value.selector_publication_us,
			     float_bits(value.source_acceleration[0]), float_bits(value.source_acceleration[1]),
			     float_bits(value.source_acceleration[2]), float_bits(value.selected_acceleration[0]),
			     float_bits(value.selected_acceleration[1]), float_bits(value.selected_acceleration[2]),
			     value.source_reset_counters[0], value.source_reset_counters[1],
			     value.source_reset_counters[2], value.source_reset_counters[3],
			     value.source_reset_counters[4], value.source_reset_counters[5],
			     value.selected_reset_counters[0], value.selected_reset_counters[1],
			     value.selected_reset_counters[2], value.selected_reset_counters[3],
			     value.selected_reset_counters[4], value.selected_reset_counters[5],
			     value.selected_instance, value.source_instance,
			     value.publication_succeeded ? "true" : "false") > 0;
	}

	ok = ok && fprintf(file, "]}\n") > 0;
	ok = close_file(file) && ok;

	if (!ok) {
		fail(Fault::Sink);
	}

	return ok && first_fault.load() == 0;
}

extern "C" __EXPORT int research_selector_handoff_main(int argc, char *argv[])
{
	if (argc == 3 && strcmp(argv[1], "begin") == 0) {
		uint64_t run_id{};
		return positive(argv[2], run_id) && research_handoff_begin(run_id) ? 0 : -1;
	}

	if (argc == 2 && strcmp(argv[1], "prepare") == 0) {
		return research_handoff_prepare_stop() ? 0 : -1;
	}

	if (argc == 3 && strcmp(argv[1], "finish") == 0) {
		return research_handoff_finish(argv[2]) ? 0 : -1;
	}

	return -1;
}
