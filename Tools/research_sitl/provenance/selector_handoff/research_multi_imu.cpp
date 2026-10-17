// Closed two-instance replay for P2J selector lifecycle evidence; no actuator path.
#include "SelectorHandoff.hpp"
#include "RecordedSystemState.hpp"

#include <drivers/drv_hrt.h>
#include <px4_platform_common/posix.h>
#include <uORB/PublicationMulti.hpp>
#include <uORB/Subscription.hpp>
#include <uORB/topics/estimator_selector_status.h>
#include <uORB/topics/vehicle_imu.h>

#include <array>
#include <atomic>
#include <cmath>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

namespace
{
constexpr size_t InputCapacity = 1200;
constexpr uint8_t PublisherCount = 2;
constexpr unsigned FixtureStepUs = 4000; // Recorded 250 Hz fixture, not an operational rate.

struct ImuInput {
	float gyro[3] {};
	float accel[3] {};
	unsigned gyro_dt{};
	unsigned accel_dt{};
	unsigned accel_clip{};
	unsigned gyro_clip{};
	unsigned accel_cal{};
	unsigned gyro_cal{};
};

enum class Fault : unsigned { None, AdministrationOrder, Input, Publication, Pair, Handoff, Sink };

std::array<ImuInput, InputCapacity> inputs{};
std::array<std::array<uint64_t, InputCapacity>, PublisherCount> publication_us{};
uORB::PublicationMulti<vehicle_imu_s> vehicle_imu_publication_zero{ORB_ID(vehicle_imu)};
uORB::PublicationMulti<vehicle_imu_s> vehicle_imu_publication_one{ORB_ID(vehicle_imu)};
std::array<uORB::PublicationMulti<vehicle_imu_s> *, PublisherCount> vehicle_imu_publications {
	&vehicle_imu_publication_zero, &vehicle_imu_publication_one
};
uORB::Subscription selector_status_subscription{ORB_ID(estimator_selector_status)};
std::atomic<unsigned> first_fault{0};
estimator_selector_status_s pair_status{};
estimator_selector_status_s handoff_status{};
size_t input_count{};
size_t next_input{};
size_t split_index{};
uint64_t run_identity{};
uint32_t accelerometer_device_id{};
uint32_t gyroscope_device_id{};
bool bootstrapped{};
bool sensors_bound{};
bool primed{};
bool pair_observed{};
bool fed{};
bool handoff_observed{};
bool stop_prepared{};
bool finished{};

void fail(Fault fault)
{
	unsigned none = 0;
	first_fault.compare_exchange_strong(none, static_cast<unsigned>(fault));
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

int load_fixture(const char *path, const char *state_path, const char *identity)
{
	if (bootstrapped || !positive(identity, run_identity)) {
		fail(Fault::AdministrationOrder);
		return -1;
	}

	bootstrapped = true;

	if (!research_recorded_state_load(state_path)) {
		fail(Fault::Input);
		return -1;
	}

	FILE *file = fopen(path, "r");

	if (!file) {
		fail(Fault::Input);
		return -1;
	}

	int result = 0;

	while (true) {
		ImuInput value{};
		const int fields = fscanf(file, "%u %u %f %f %f %f %f %f %u %u %u %u",
					  &value.gyro_dt, &value.accel_dt, &value.gyro[0], &value.gyro[1],
					  &value.gyro[2], &value.accel[0], &value.accel[1], &value.accel[2],
					  &value.accel_clip, &value.gyro_clip, &value.accel_cal, &value.gyro_cal);

		if (fields == EOF) {
			if (ferror(file)) {
				result = -1;
			}

			break;
		}

		if (fields != 12 || input_count == InputCapacity || value.gyro_dt != FixtureStepUs
		    || value.accel_dt != FixtureStepUs || value.accel_clip > 7 || value.gyro_clip > 7
		    || value.accel_cal > UINT8_MAX || value.gyro_cal > UINT8_MAX) {
			result = -1;
			break;
		}

		for (unsigned axis = 0; axis < 3; ++axis) {
			if (!std::isfinite(value.gyro[axis]) || !std::isfinite(value.accel[axis])) {
				result = -1;
			}
		}

		if (result) {
			break;
		}

		inputs[input_count++] = value;
	}

	const bool close_ok = fclose(file) == 0;

	if (!close_ok || result || input_count != InputCapacity) {
		fail(Fault::Input);
		return -1;
	}

	return 0;
}

int bind_sensors(const char *accelerometer, const char *gyroscope)
{
	uint64_t accel{};
	uint64_t gyro{};

	if (!bootstrapped || sensors_bound || primed || !positive(accelerometer, accel)
	    || !positive(gyroscope, gyro) || accel >= (uint64_t{1} << 32)
	    || gyro >= (uint64_t{1} << 32)) {
		fail(Fault::AdministrationOrder);
		return -1;
	}

	accelerometer_device_id = static_cast<uint32_t>(accel);
	gyroscope_device_id = static_cast<uint32_t>(gyro);
	sensors_bound = true;
	return 0;
}

bool publish_input(size_t index)
{
	if (index >= input_count) {
		return false;
	}

	const ImuInput &input = inputs[index];

	for (uint8_t publisher = 0; publisher < PublisherCount; ++publisher) {
		vehicle_imu_s value{};
		value.timestamp = hrt_absolute_time();
		value.timestamp_sample = value.timestamp;
		value.accel_device_id = accelerometer_device_id;
		value.gyro_device_id = gyroscope_device_id;
		value.delta_angle_dt = input.gyro_dt;
		value.delta_velocity_dt = input.accel_dt;
		value.delta_angle_clipping = input.gyro_clip;
		value.delta_velocity_clipping = input.accel_clip;
		value.accel_calibration_count = input.accel_cal;
		value.gyro_calibration_count = input.gyro_cal;
		const float gyro_seconds = input.gyro_dt * 1e-6f;
		const float accel_seconds = input.accel_dt * 1e-6f;

		for (unsigned axis = 0; axis < 3; ++axis) {
			value.delta_angle[axis] = input.gyro[axis] * gyro_seconds;
			value.delta_velocity[axis] = input.accel[axis] * accel_seconds;
		}

		if (!vehicle_imu_publications[publisher]->publish(value)
		    || vehicle_imu_publications[publisher]->get_instance() != publisher) {
			return false;
		}

		publication_us[publisher][index] = value.timestamp;
	}

	return true;
}

bool publish_next()
{
	const uint64_t first_source_us = research_recorded_state_first_imu_us();
	const uint64_t step_us = research_recorded_state_step_us();
	const uint64_t source_us = first_source_us + next_input * step_us;

	if (!first_source_us || !step_us || !research_recorded_state_publish_through(source_us)
	    || !publish_input(next_input)) {
		return false;
	}

	next_input++;
	return true;
}

void observe_selector_status()
{
	estimator_selector_status_s status{};

	if (!selector_status_subscription.update(&status)) {
		return;
	}

	if (!pair_observed && status.primary_instance == 0 && status.instances_available >= PublisherCount
	    && status.healthy[0] && status.healthy[1]) {
		pair_status = status;
		pair_observed = true;
		split_index = next_input;
	}

	if (pair_observed && !handoff_observed && status.primary_instance == 1
	    && status.instances_available >= PublisherCount && status.instance_changed_count >= 2) {
		handoff_status = status;
		handoff_observed = true;
	}
}

int prime()
{
	if (!bootstrapped || !sensors_bound || primed || fed || first_fault.load()
	    || !publish_next()) {
		fail(Fault::Publication);
		return -1;
	}

	primed = true;
	return 0;
}

int feed_until_pair()
{
	if (!primed || pair_observed || fed || stop_prepared || first_fault.load()) {
		fail(Fault::AdministrationOrder);
		return -1;
	}

	while (next_input < input_count && !pair_observed) {
		if (!publish_next()) {
			fail(Fault::Publication);
			return -1;
		}

		px4_usleep(FixtureStepUs);
		observe_selector_status();
	}

	if (!pair_observed || split_index <= 1 || split_index >= input_count) {
		fail(Fault::Pair);
		return -1;
	}

	return 0;
}

int feed_after_stop()
{
	if (!pair_observed || fed || stop_prepared || first_fault.load()) {
		fail(Fault::AdministrationOrder);
		return -1;
	}

	while (next_input < input_count) {
		if (!publish_next()) {
			fail(Fault::Publication);
			return -1;
		}

		px4_usleep(FixtureStepUs);
		observe_selector_status();
	}

	fed = true;

	if (!handoff_observed || !research_recorded_state_complete()) {
		fail(Fault::Handoff);
		return -1;
	}

	return 0;
}

int prepare_stop()
{
	if (!fed || stop_prepared || next_input != input_count || first_fault.load()) {
		fail(Fault::AdministrationOrder);
		return -1;
	}

	stop_prepared = true;
	return 0;
}

bool export_feeder(const char *path)
{
	const int fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);

	if (fd < 0) {
		return false;
	}

	FILE *file = fdopen(fd, "w");

	if (!file) {
		close(fd);
		return false;
	}

	bool ok = fprintf(file,
			  "{\"schema\":\"p2j-multi-imu-feeder/v1\",\"run_id\":\"%" PRIu64 "\","
			  "\"fault\":%u,\"input_count\":%zu,\"published_count\":[%zu,%zu],"
			  "\"vehicle_imu_instances\":[%d,%d],\"split_index\":%zu,"
			  "\"accelerometer_device_ids\":[%" PRIu32 ",%" PRIu32 "],"
			  "\"gyroscope_device_ids\":[%" PRIu32 ",%" PRIu32 "],"
			  "\"duplicated_recorded_sensor_payload\":true,\"sensor_diversity\":false,"
			  "\"deployment_sensors_qualified\":false,\"pair_status\":{"
			  "\"timestamp\":%" PRIu64 ",\"primary_instance\":%u,\"instances_available\":%u,"
			  "\"instance_changed_count\":%" PRIu32 ",\"healthy\":[%s,%s]},"
			  "\"handoff_status\":{\"timestamp\":%" PRIu64 ",\"primary_instance\":%u,"
			  "\"instances_available\":%u,\"instance_changed_count\":%" PRIu32 ","
			  "\"healthy\":[%s,%s]},\"actuator_publications\":false,"
			  "\"publication_timestamps\":[[",
			  run_identity, first_fault.load(), input_count, next_input, next_input,
			  vehicle_imu_publications[0]->get_instance(), vehicle_imu_publications[1]->get_instance(),
			  split_index, accelerometer_device_id, accelerometer_device_id,
			  gyroscope_device_id, gyroscope_device_id, pair_status.timestamp,
			  pair_status.primary_instance, pair_status.instances_available,
			  pair_status.instance_changed_count, pair_status.healthy[0] ? "true" : "false",
			  pair_status.healthy[1] ? "true" : "false", handoff_status.timestamp,
			  handoff_status.primary_instance, handoff_status.instances_available,
			  handoff_status.instance_changed_count, handoff_status.healthy[0] ? "true" : "false",
			  handoff_status.healthy[1] ? "true" : "false") > 0;

	for (size_t index = 0; ok && index < input_count; ++index) {
		ok = fprintf(file, "%s%" PRIu64, index ? "," : "", publication_us[0][index]) > 0;
	}

	ok = ok && fprintf(file, "],[") > 0;

	for (size_t index = 0; ok && index < input_count; ++index) {
		ok = fprintf(file, "%s%" PRIu64, index ? "," : "", publication_us[1][index]) > 0;
	}

	ok = ok && fprintf(file, "]]}\n") > 0;

	if (fclose(file) != 0) {
		ok = false;
	}

	return ok;
}

int finish(const char *feeder_path, const char *state_path)
{
	if (!stop_prepared || finished) {
		fail(Fault::AdministrationOrder);
		return -1;
	}

	finished = true;
	const bool feeder_ok = export_feeder(feeder_path);
	const bool state_ok = research_recorded_state_finish(state_path);

	if (!feeder_ok || !state_ok) {
		fail(Fault::Sink);
	}

	return feeder_ok && state_ok && first_fault.load() == 0 ? 0 : -1;
}
}

extern "C" __EXPORT int research_multi_imu_main(int argc, char *argv[])
{
	if (argc == 5 && strcmp(argv[1], "load") == 0) {
		return load_fixture(argv[2], argv[3], argv[4]);
	}

	if (argc == 4 && strcmp(argv[1], "sensors") == 0) {
		return bind_sensors(argv[2], argv[3]);
	}

	if (argc == 2 && strcmp(argv[1], "prime") == 0) {
		return prime();
	}

	if (argc == 2 && strcmp(argv[1], "feed_until_pair") == 0) {
		return feed_until_pair();
	}

	if (argc == 2 && strcmp(argv[1], "feed_after_stop") == 0) {
		return feed_after_stop();
	}

	if (argc == 2 && strcmp(argv[1], "prepare") == 0) {
		return prepare_stop();
	}

	if (argc == 4 && strcmp(argv[1], "finish") == 0) {
		return finish(argv[2], argv[3]);
	}

	return -1;
}
