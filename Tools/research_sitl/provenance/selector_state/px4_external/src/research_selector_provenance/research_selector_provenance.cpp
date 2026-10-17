// Isolated recorded-IMU feeder for the selector-enabled EKF provenance profile.
#include "SelectorState.hpp"

#include <drivers/drv_hrt.h>
#include <px4_platform_common/posix.h>
#include <uORB/Publication.hpp>
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
constexpr unsigned FixtureStepUs = 4000; // Recorded 250 Hz experiment only, not an operational rate.

struct ImuInput {
	uint64_t publication_us{};
	float gyro[3] {};
	float accel[3] {};
	unsigned gyro_dt{};
	unsigned accel_dt{};
	unsigned accel_clip{};
	unsigned gyro_clip{};
	unsigned accel_cal{};
	unsigned gyro_cal{};
};

enum class Fault : unsigned { None, AdministrationOrder, Input, Publication, Sink };

std::array<ImuInput, InputCapacity> inputs{};
uORB::Publication<vehicle_imu_s> vehicle_imu_publication{ORB_ID(vehicle_imu)};
std::atomic<unsigned> first_fault{0};
size_t input_count{};
size_t next_input{};
uint64_t run_identity{};
uint32_t accelerometer_device_id{};
uint32_t gyroscope_device_id{};
bool bootstrapped{};
bool sensors_bound{};
bool primed{};
bool fed{};
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

int load_fixture(const char *path, const char *identity)
{
	if (bootstrapped || !positive(identity, run_identity)) {
		fail(Fault::AdministrationOrder);
		return -1;
	}

	bootstrapped = true; // A failed bootstrap cannot be retried in this process.
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

		bool finite = true;

		for (unsigned axis = 0; axis < 3; ++axis) {
			finite = finite && std::isfinite(value.gyro[axis]) && std::isfinite(value.accel[axis]);
		}

		if (!finite) {
			result = -1;
			break;
		}

		inputs[input_count++] = value;
	}

	if (fclose(file) != 0 || input_count != InputCapacity) {
		result = -1;
	}

	if (result != 0 || !research_selector_begin(run_identity)) {
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
	    || !positive(gyroscope, gyro) || accel >= (uint64_t{1} << 32) || gyro >= (uint64_t{1} << 32)) {
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

	ImuInput &input = inputs[index];
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

	if (!vehicle_imu_publication.publish(value)) {
		return false;
	}

	input.publication_us = value.timestamp;
	return true;
}

int prime()
{
	if (!bootstrapped || !sensors_bound || primed || fed || first_fault.load() || !publish_input(0)) {
		fail(Fault::Publication);
		return -1;
	}

	primed = true;
	next_input = 1;
	return 0;
}

int feed()
{
	if (!primed || fed || stop_prepared || first_fault.load()) {
		fail(Fault::AdministrationOrder);
		return -1;
	}

	fed = true;

	for (; next_input < input_count; ++next_input) {
		if (!publish_input(next_input)) {
			fail(Fault::Publication);
			return -1;
		}

		px4_usleep(FixtureStepUs);
	}

	return 0;
}

int prepare_stop()
{
	if (!fed || stop_prepared || next_input != input_count || first_fault.load()
	    || !research_selector_prepare_stop()) {
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
			  "{\"schema\":\"p2g-vehicle-imu-feeder/v1\",\"run_id\":\"%" PRIu64 "\","
			  "\"fault\":%u,\"input_count\":%zu,\"published_count\":%zu,"
			  "\"vehicle_imu_instance\":0,\"accel_device_id\":%" PRIu32 ","
			  "\"gyro_device_id\":%" PRIu32 ",\"delta_transform\":true,"
			  "\"actuator_publications\":false,\"publication_timestamps\":[",
			  run_identity, first_fault.load(), input_count, next_input,
			  accelerometer_device_id, gyroscope_device_id) > 0;

	for (size_t index = 0; ok && index < input_count; ++index) {
		ok = fprintf(file, "%s%" PRIu64, index ? "," : "", inputs[index].publication_us) > 0;
	}

	ok = ok && fprintf(file, "]}\n") > 0;

	if (fclose(file) != 0) {
		ok = false;
	}

	return ok;
}

int finish(const char *selector_path, const char *feeder_path)
{
	if (!stop_prepared || finished) {
		fail(Fault::AdministrationOrder);
		return -1;
	}

	finished = true;
	const bool selector_ok = research_selector_finish(selector_path);
	const bool feeder_ok = export_feeder(feeder_path);

	if (!feeder_ok) {
		fail(Fault::Sink);
	}

	return selector_ok && feeder_ok && first_fault.load() == 0 ? 0 : -1;
}
}

extern "C" __EXPORT int research_selector_provenance_main(int argc, char *argv[])
{
	if (argc == 4 && strcmp(argv[1], "load") == 0) {
		return load_fixture(argv[2], argv[3]);
	}

	if (argc == 4 && strcmp(argv[1], "sensors") == 0) {
		return bind_sensors(argv[2], argv[3]);
	}

	if (argc == 2 && strcmp(argv[1], "prime") == 0) {
		return prime();
	}

	if (argc == 2 && strcmp(argv[1], "feed") == 0) {
		return feed();
	}

	if (argc == 2 && strcmp(argv[1], "prepare") == 0) {
		return prepare_stop();
	}

	if (argc == 4 && strcmp(argv[1], "finish") == 0) {
		return finish(argv[2], argv[3]);
	}

	return -1;
}
