#include "SelectorState.hpp"

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
constexpr size_t OutputCapacity = 2048; // Existing finite P2 observation bound, not a vehicle budget.
constexpr size_t StatusCapacity = 64; // Bounded selector-status evidence, not a publication-rate requirement.
constexpr unsigned FreezeWaitSteps = 1000; // Bounded diagnostic cleanup, not an operational deadline.
constexpr uint8_t InvalidInstance = UINT8_MAX;

enum class Fault : unsigned {
	None,
	AdministrationOrder,
	ConcurrentOwner,
	Profile,
	SelectorStart,
	EstimatorStart,
	Selection,
	Status,
	Attitude,
	LocalPosition,
	SourceEnd,
	Capacity,
	Freeze,
	Sink,
	ConcurrentWriter
};

struct EstimatorBinding {
	int32_t estimator_instance{};
	int32_t attitude_publication_instance{};
	int32_t local_position_publication_instance{};
	int32_t status_publication_instance{};
	int32_t vehicle_imu_instance{};
};

template<typename T>
struct TimedRecord {
	uint64_t sequence{};
	uint64_t observed_us{};
	T value{};
};

std::array<TimedRecord<SelectorStatusObservation>, StatusCapacity> statuses{};
std::array<TimedRecord<SelectorAttitudeObservation>, OutputCapacity> attitudes{};
std::array<TimedRecord<SelectorLocalPositionObservation>, OutputCapacity> local_positions{};
SelectorSelectionObservation selection{};
EstimatorBinding estimator_binding{};
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
std::atomic_flag writer_busy = ATOMIC_FLAG_INIT;
std::atomic_flag owner_busy = ATOMIC_FLAG_INIT;
uint64_t run_identity{};
size_t status_count{};
size_t attitude_count{};
size_t local_position_count{};
bool bootstrapped{};
bool have_selection{};
bool finished{};

static_assert(std::atomic<uint64_t>::is_always_lock_free &&std::atomic<uintptr_t>::is_always_lock_free,
	      "P2G source bookkeeping must be lock-free");

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

bool estimator_matches(const void *estimator)
{
	return estimator && reinterpret_cast<uintptr_t>(estimator) == estimator_source.load();
}

bool valid_output_time(uint64_t sample_us, uint64_t estimator_publication_us,
		       uint64_t selector_publication_us, uint64_t observed_us)
{
	return sample_us && sample_us <= estimator_publication_us
	       && estimator_publication_us <= selector_publication_us
	       && selector_publication_us <= observed_us;
}
}

bool research_selector_begin(uint64_t run_id) noexcept
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

void research_selector_start(const void *selector) noexcept
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

void research_selector_estimator_start(const void *estimator, int32_t estimator_instance,
				       int32_t attitude_publication_instance,
				       int32_t local_position_publication_instance,
				       int32_t status_publication_instance,
				       int32_t vehicle_imu_instance, bool replay, bool multi) noexcept
{
	Hook hook;

	if (!hook.active || !begin_write()) {
		return;
	}

	uintptr_t none = 0;
	const uintptr_t address = reinterpret_cast<uintptr_t>(estimator);
	const uint64_t now = hrt_absolute_time();

	if (!selector_source.load() || !address || !now || replay || !multi || estimator_instance != 0
	    || attitude_publication_instance != 0 || local_position_publication_instance != 0
	    || status_publication_instance != 0 || vehicle_imu_instance != 0
	    || !estimator_source.compare_exchange_strong(none, address)) {
		fail(Fault::EstimatorStart);

	} else {
		estimator_binding = {estimator_instance, attitude_publication_instance,
				     local_position_publication_instance, status_publication_instance,
				     vehicle_imu_instance
				    };
		estimator_start_us.store(now);
	}

	writer_busy.clear();
}

void research_selector_selection(const void *selector,
				 const SelectorSelectionObservation &observation) noexcept
{
	Hook hook;

	if (!hook.active || !begin_write()) {
		return;
	}

	const uint64_t now = hrt_absolute_time();

	if (!selector_matches(selector) || !estimator_source.load() || have_selection
	    || observation.old_instance != InvalidInstance || observation.selected_instance != 0
	    || observation.instances_available != 1 || observation.instance_changed_count != 1
	    || !observation.selection_us || observation.last_instance_change_us != observation.selection_us
	    || observation.selection_us < estimator_start_us.load() || observation.selection_us > now
	    || !observation.accel_device_id || !observation.gyro_device_id
	    || !observation.publication_succeeded) {
		fail(Fault::Selection);

	} else {
		selection = observation;
		have_selection = true;
	}

	writer_busy.clear();
}

void research_selector_status(const void *selector,
			      const SelectorStatusObservation &observation) noexcept
{
	Hook hook;

	if (!hook.active || !begin_write()) {
		return;
	}

	const uint64_t now = hrt_absolute_time();

	if (!selector_matches(selector) || !have_selection || !observation.publication_succeeded
	    || observation.selected_instance != selection.selected_instance
	    || observation.instances_available != selection.instances_available
	    || observation.instance_changed_count != selection.instance_changed_count
	    || observation.last_instance_change_us != selection.last_instance_change_us
	    || observation.accel_device_id != selection.accel_device_id
	    || observation.gyro_device_id != selection.gyro_device_id
	    || observation.publication_us < selection.selection_us || observation.publication_us > now) {
		fail(Fault::Status);

	} else if (status_count == StatusCapacity) {
		fail(Fault::Capacity);

	} else {
		statuses[status_count] = {status_count + 1, now, observation};
		++status_count;
	}

	writer_busy.clear();
}

void research_selector_attitude(const void *selector,
				const SelectorAttitudeObservation &observation) noexcept
{
	Hook hook;

	if (!hook.active || !begin_write()) {
		return;
	}

	const uint64_t now = hrt_absolute_time();
	const bool monotonic = !attitude_count
			       || (observation.sample_us > attitudes[attitude_count - 1].value.sample_us
				   && observation.selector_publication_us
				   >= attitudes[attitude_count - 1].value.selector_publication_us);

	if (!selector_matches(selector) || !have_selection || !observation.publication_succeeded
	    || observation.selected_instance != selection.selected_instance || observation.source_instance != 0
	    || !valid_output_time(observation.sample_us, observation.estimator_publication_us,
				  observation.selector_publication_us, now) || !monotonic) {
		fail(Fault::Attitude);

	} else if (attitude_count == OutputCapacity) {
		fail(Fault::Capacity);

	} else {
		attitudes[attitude_count] = {attitude_count + 1, now, observation};
		++attitude_count;
	}

	writer_busy.clear();
}

void research_selector_local_position(const void *selector,
				      const SelectorLocalPositionObservation &observation) noexcept
{
	Hook hook;

	if (!hook.active || !begin_write()) {
		return;
	}

	const uint64_t now = hrt_absolute_time();
	const bool monotonic = !local_position_count
			       || (observation.sample_us > local_positions[local_position_count - 1].value.sample_us
				   && observation.selector_publication_us
				   >= local_positions[local_position_count - 1].value.selector_publication_us);
	bool acceleration_matches = true;
	bool acceleration_finite = true;

	for (unsigned axis = 0; axis < 3; ++axis) {
		acceleration_matches = acceleration_matches
				       && std::memcmp(&observation.source_acceleration[axis],
						      &observation.selected_acceleration[axis], sizeof(float)) == 0;
		acceleration_finite = acceleration_finite && std::isfinite(observation.source_acceleration[axis]);
	}

	if (!selector_matches(selector) || !have_selection || !observation.publication_succeeded
	    || observation.selected_instance != selection.selected_instance || observation.source_instance != 0
	    || !valid_output_time(observation.sample_us, observation.estimator_publication_us,
				  observation.selector_publication_us, now) || !monotonic
	    || !acceleration_matches || !acceleration_finite) {
		fail(Fault::LocalPosition);

	} else if (local_position_count == OutputCapacity) {
		fail(Fault::Capacity);

	} else {
		local_positions[local_position_count] = {local_position_count + 1, now, observation};
		++local_position_count;
	}

	writer_busy.clear();
}

bool research_selector_prepare_stop() noexcept
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

void research_selector_end(const void *selector) noexcept
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

void research_selector_estimator_end(const void *estimator) noexcept
{
	Hook hook;

	if (!hook.active || !begin_write()) {
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

	writer_busy.clear();
}

bool research_selector_finish(const char *path) noexcept
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

	if (!have_selection || !status_count || !local_position_count
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
			  "{\"schema\":\"p2g-selector-state/v1\",\"run_id\":\"%" PRIu64 "\","
			  "\"fault\":%u,\"counts\":{\"status\":%zu,\"attitude\":%zu,\"local_position\":%zu},"
			  "\"lifecycle\":{\"selector_start_us\":%" PRIu64 ",\"estimator_start_us\":%" PRIu64 ","
			  "\"stop_intent_us\":%" PRIu64 ",\"selector_end_us\":%" PRIu64 ","
			  "\"estimator_end_us\":%" PRIu64 "},"
			  "\"binding\":[%" PRIi32 ",%" PRIi32 ",%" PRIi32 ",%" PRIi32 ",%" PRIi32 "],"
			  "\"health_policy_configured\":false,\"health_qualified\":false,"
			  "\"failover_qualified\":false,\"positive_admission\":false,"
			  "\"eligible_for_control\":false,\"selection\":{"
			  "\"old_instance\":%u,\"selected_instance\":%u,\"instances_available\":%u,"
			  "\"instance_changed_count\":%" PRIu32 ",\"selection_us\":%" PRIu64 ","
			  "\"last_instance_change_us\":%" PRIu64 ",\"accel_device_id\":%" PRIu32 ","
			  "\"gyro_device_id\":%" PRIu32 ",\"publication_succeeded\":%s},\"statuses\":[\n",
			  run_identity, first_fault.load(), status_count, attitude_count, local_position_count,
			  selector_start_us.load(), estimator_start_us.load(), stop_intent_us.load(),
			  selector_end_us.load(), estimator_end_us.load(), estimator_binding.estimator_instance,
			  estimator_binding.attitude_publication_instance, estimator_binding.local_position_publication_instance,
			  estimator_binding.status_publication_instance, estimator_binding.vehicle_imu_instance,
			  selection.old_instance, selection.selected_instance, selection.instances_available,
			  selection.instance_changed_count, selection.selection_us, selection.last_instance_change_us,
			  selection.accel_device_id, selection.gyro_device_id,
			  selection.publication_succeeded ? "true" : "false") > 0;

	for (size_t i = 0; ok && i < status_count; ++i) {
		const auto &record = statuses[i];
		const auto &value = record.value;
		ok = fprintf(file,
			     "%s{\"sequence\":%" PRIu64 ",\"observed_us\":%" PRIu64 ","
			     "\"publication_us\":%" PRIu64 ",\"selected_instance\":%u,"
			     "\"instances_available\":%u,\"instance_changed_count\":%" PRIu32 ","
			     "\"last_instance_change_us\":%" PRIu64 ",\"accel_device_id\":%" PRIu32 ","
			     "\"gyro_device_id\":%" PRIu32 ",\"raw_selected_healthy\":%s,"
			     "\"publication_succeeded\":%s}",
			     i ? ",\n" : "", record.sequence, record.observed_us, value.publication_us,
			     value.selected_instance, value.instances_available, value.instance_changed_count,
			     value.last_instance_change_us, value.accel_device_id, value.gyro_device_id,
			     value.raw_selected_healthy ? "true" : "false",
			     value.publication_succeeded ? "true" : "false") > 0;
	}

	ok = ok && fprintf(file, "\n],\"attitudes\":[\n") > 0;

	for (size_t i = 0; ok && i < attitude_count; ++i) {
		const auto &record = attitudes[i];
		const auto &value = record.value;
		ok = fprintf(file,
			     "%s{\"sequence\":%" PRIu64 ",\"observed_us\":%" PRIu64 ","
			     "\"sample_us\":%" PRIu64 ",\"estimator_publication_us\":%" PRIu64 ","
			     "\"selector_publication_us\":%" PRIu64 ",\"selected_instance\":%u,"
			     "\"source_instance\":%u,\"source_reset_counter\":%u,"
			     "\"selected_reset_counter\":%u,\"publication_succeeded\":%s}",
			     i ? ",\n" : "", record.sequence, record.observed_us, value.sample_us,
			     value.estimator_publication_us, value.selector_publication_us, value.selected_instance,
			     value.source_instance, value.source_reset_counter, value.selected_reset_counter,
			     value.publication_succeeded ? "true" : "false") > 0;
	}

	ok = ok && fprintf(file, "\n],\"local_positions\":[\n") > 0;

	for (size_t i = 0; ok && i < local_position_count; ++i) {
		const auto &record = local_positions[i];
		const auto &value = record.value;
		uint32_t source_acceleration_bits[3] {};
		uint32_t selected_acceleration_bits[3] {};

		for (unsigned axis = 0; axis < 3; ++axis) {
			std::memcpy(&source_acceleration_bits[axis], &value.source_acceleration[axis], sizeof(uint32_t));
			std::memcpy(&selected_acceleration_bits[axis], &value.selected_acceleration[axis], sizeof(uint32_t));
		}

		ok = fprintf(file,
			     "%s{\"sequence\":%" PRIu64 ",\"observed_us\":%" PRIu64 ","
			     "\"sample_us\":%" PRIu64 ",\"estimator_publication_us\":%" PRIu64 ","
			     "\"selector_publication_us\":%" PRIu64 ",\"selected_instance\":%u,"
			     "\"source_instance\":%u,\"source_acceleration_bits\":[%" PRIu32 ",%" PRIu32 ",%" PRIu32 "],"
			     "\"selected_acceleration_bits\":[%" PRIu32 ",%" PRIu32 ",%" PRIu32 "],"
			     "\"source_reset_counters\":[%u,%u,%u,%u],"
			     "\"selected_reset_counters\":[%u,%u,%u,%u],\"publication_succeeded\":%s}",
			     i ? ",\n" : "", record.sequence, record.observed_us, value.sample_us,
			     value.estimator_publication_us, value.selector_publication_us, value.selected_instance,
			     value.source_instance, source_acceleration_bits[0], source_acceleration_bits[1],
			     source_acceleration_bits[2], selected_acceleration_bits[0],
			     selected_acceleration_bits[1], selected_acceleration_bits[2], value.source_xy_reset_counter,
			     value.source_z_reset_counter, value.source_vxy_reset_counter, value.source_vz_reset_counter,
			     value.selected_xy_reset_counter, value.selected_z_reset_counter,
			     value.selected_vxy_reset_counter, value.selected_vz_reset_counter,
			     value.publication_succeeded ? "true" : "false") > 0;
	}

	ok = ok && fprintf(file, "\n]}\n") > 0;

	if (fclose(file) != 0) {
		ok = false;
	}

	if (!ok) {
		fail(Fault::Sink);
		return false;
	}

	return first_fault.load() == 0;
}
