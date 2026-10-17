// Isolated EKF observation and recorded IMU feeder. No control/admission path.
#include "../../../Receipt.hpp"

#include <drivers/drv_hrt.h>
#include <px4_platform_common/posix.h>
#include <uORB/Publication.hpp>
#include <uORB/topics/sensor_combined.h>

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
constexpr size_t RecordCapacity = 2048; // Finite source archive, not a vehicle budget.
constexpr size_t InputCapacity = 1200;
constexpr unsigned FixtureStepUs = 4000; // Recorded 250 Hz IMU experiment only.
constexpr unsigned FreezeWaitSteps = 1000; // Bounded export wait, outside producer.

struct Record {
	uint64_t sequence{}, sample_us{}, observed_us{}, control_flags{};
	uint32_t fault_flags{};
	bool attitude_valid{}, horizontal_valid{}, vertical_position_valid{}, vertical_velocity_valid{};
};
struct Imu {
	uint64_t publication_us{};
	float gyro[3] {}, accel[3] {};
	unsigned gyro_dt{}, accel_dt{};
	unsigned accel_clip{}, gyro_clip{}, accel_cal{}, gyro_cal{};
};
enum class Fault : unsigned { None, ConcurrentWriter, Identity, Time, Capacity, Input, Sink, Freeze };
std::array<Record, RecordCapacity> records{};
std::array<Imu, InputCapacity> inputs{};
std::atomic<bool> active{false};
std::atomic<unsigned> in_flight{0};
std::atomic<unsigned> first_fault{0};
std::atomic_flag writer = ATOMIC_FLAG_INIT;
static_assert(std::atomic<bool>::is_always_lock_free &&std::atomic<unsigned>::is_always_lock_free,
	      "Producer bookkeeping must be lock-free");
size_t count{}, input_count{};
const void *producer{};
uint64_t run_id{};
bool bootstrapped{}, fed{}, exported{};

void fail(Fault fault)
{
	unsigned none = 0;
	first_fault.compare_exchange_strong(none, static_cast<unsigned>(fault));
	active.store(false);
}

bool positive(const char *text, uint64_t &value)
{
	if (!text || *text < '1' || *text > '9') { return false; }

	value = 0;

	for (const char *p = text; *p; ++p) {
		if (*p < '0' || *p > '9' || value > (UINT64_MAX - unsigned(*p - '0')) / 10) { return false; }

		value = value * 10 + unsigned(*p - '0');
	}

	return true;
}

int load(const char *path, const char *identity)
{
	if (bootstrapped || !positive(identity, run_id)) { return -1; }

	bootstrapped = true; // A failed bootstrap cannot be retried in this process.
	FILE *file = fopen(path, "r");

	if (!file) { fail(Fault::Input); return -1; }

	int result = 0;

	while (true) {
		Imu value{};
		const int fields = fscanf(file, "%u %u %f %f %f %f %f %f %u %u %u %u", &value.gyro_dt, &value.accel_dt,
					  &value.gyro[0], &value.gyro[1], &value.gyro[2], &value.accel[0], &value.accel[1], &value.accel[2],
					  &value.accel_clip, &value.gyro_clip, &value.accel_cal, &value.gyro_cal);

		if (fields == EOF) { if (ferror(file)) { result = -1; } break; }

		if (fields != 12 || input_count == InputCapacity || value.gyro_dt != FixtureStepUs
		    || value.accel_dt != FixtureStepUs || value.accel_clip > 7 || value.gyro_clip > 7
		    || value.accel_cal > 255 || value.gyro_cal > 255) { result = -1; break; }

		bool finite = true;

		for (unsigned i = 0; i < 3; ++i) { finite = finite && std::isfinite(value.gyro[i]) && std::isfinite(value.accel[i]); }

		if (!finite) { result = -1; break; }

		inputs[input_count++] = value;
	}

	if (fclose(file) != 0 || input_count == 0) { result = -1; }

	if (result != 0) { fail(Fault::Input); return -1; }

	active.store(true);
	return 0;
}

int feed()
{
	if (!bootstrapped || fed || !active.load()) { return -1; }

	fed = true;
	uORB::Publication<sensor_combined_s> publication{ORB_ID(sensor_combined)};

	for (size_t i = 0; i < input_count; ++i) {
		if (!active.load()) { return -1; }

		sensor_combined_s value{};
		value.timestamp = hrt_absolute_time();
		inputs[i].publication_us = value.timestamp;
		value.gyro_integral_dt = inputs[i].gyro_dt;
		value.accelerometer_integral_dt = inputs[i].accel_dt;
		value.accelerometer_clipping = inputs[i].accel_clip;
		value.gyro_clipping = inputs[i].gyro_clip;
		value.accel_calibration_count = inputs[i].accel_cal;
		value.gyro_calibration_count = inputs[i].gyro_cal;

		for (unsigned axis = 0; axis < 3; ++axis) {
			value.gyro_rad[axis] = inputs[i].gyro[axis];
			value.accelerometer_m_s2[axis] = inputs[i].accel[axis];
		}

		if (!publication.publish(value)) { fail(Fault::Input); return -1; }

		px4_usleep(FixtureStepUs);
	}

	return 0;
}

int export_records(const char *path)
{
	if (!bootstrapped || !fed || exported) { return -1; }

	active.store(false);
	// No source-path wait. Export waits only for a bounded in-flight copy.
	unsigned waits = 0;

	while (in_flight.load() != 0 && waits++ < FreezeWaitSteps) { px4_usleep(1000); }

	if (in_flight.load() != 0) { fail(Fault::Freeze); return -1; }

	exported = true;
	const int fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);

	if (fd < 0) { fail(Fault::Sink); return -1; }

	FILE *file = fdopen(fd, "w");

	if (!file) { close(fd); fail(Fault::Sink); return -1; }

	bool ok = fprintf(file, "{\"schema\":\"native-ekf-observation/v1\",\"run_id\":\"%" PRIu64 "\","
			  "\"input_count\":%zu,\"fault\":%u,\"count\":%zu,\"admission_instantiated\":false,"
			  "\"clock_map_qualified\":false,\"input_timestamps\":[", run_id, input_count, first_fault.load(), count) > 0;

	for (size_t i = 0; i < input_count; ++i) {
		ok = ok && fprintf(file, "%s%" PRIu64, i ? "," : "", inputs[i].publication_us) > 0;
	}

	ok = ok && fprintf(file, "],\"records\":[\n") > 0;

	for (size_t i = 0; i < count; ++i) {
		const Record &r = records[i];
		ok = ok && fprintf(file, "%s{\"sequence\":%" PRIu64 ",\"sample_us\":%" PRIu64 ",\"observed_us\":%" PRIu64
				   ",\"control_flags\":%" PRIu64 ",\"fault_flags\":%u,\"attitude_valid\":%s,"
				   "\"horizontal_valid\":%s,\"vertical_position_valid\":%s,\"vertical_velocity_valid\":%s}",
				   i ? ",\n" : "", r.sequence, r.sample_us, r.observed_us, r.control_flags, r.fault_flags,
				   r.attitude_valid ? "true" : "false", r.horizontal_valid ? "true" : "false",
				   r.vertical_position_valid ? "true" : "false", r.vertical_velocity_valid ? "true" : "false") > 0;
	}

	ok = ok && fprintf(file, "\n]}\n") > 0;

	if (fclose(file) != 0) { ok = false; }

	if (!ok) { fail(Fault::Sink); return -1; }

	return first_fault.load() == 0 && count > 0 ? 0 : -1;
}
}

extern "C" void research_capture_ekf(const void *instance, uint64_t sample_us,
				     bool replay, bool multi, bool attitude_valid, bool horizontal_valid,
				     bool vertical_position_valid, bool vertical_velocity_valid,
				     uint64_t control_flags, uint32_t fault_flags) noexcept
{
	in_flight.fetch_add(1);

	if (!active.load()) { in_flight.fetch_sub(1); return; }

	if (writer.test_and_set()) { fail(Fault::ConcurrentWriter); in_flight.fetch_sub(1); return; }

	const uint64_t now = hrt_absolute_time();

	if (replay || multi || !instance || (producer && producer != instance)) { fail(Fault::Identity); }

	else if (!sample_us || sample_us > now || (count && (sample_us <= records[count - 1].sample_us
			|| now < records[count - 1].observed_us))) { fail(Fault::Time); }

	else if (count == RecordCapacity) { fail(Fault::Capacity); }

	else {
		producer = instance;
		records[count] = {count + 1, sample_us, now, control_flags, fault_flags, attitude_valid,
				  horizontal_valid, vertical_position_valid, vertical_velocity_valid
				 };
		++count;
	}

	writer.clear();
	in_flight.fetch_sub(1);
}

extern "C" __EXPORT int research_provenance_main(int argc, char *argv[])
{
	if (argc == 4 && strcmp(argv[1], "load") == 0) { return load(argv[2], argv[3]); }

	if (argc == 2 && strcmp(argv[1], "feed") == 0) { return feed(); }

	if (argc == 3 && strcmp(argv[1], "export") == 0) { return export_records(argv[2]); }

	return -1;
}
