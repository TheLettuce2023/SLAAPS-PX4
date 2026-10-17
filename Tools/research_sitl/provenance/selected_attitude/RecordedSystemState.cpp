#include "RecordedSystemState.hpp"

#include <drivers/drv_hrt.h>
#include <uORB/Publication.hpp>
#include <uORB/topics/vehicle_land_detected.h>
#include <uORB/topics/vehicle_status.h>

#include <array>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

namespace
{
constexpr size_t StateCapacity = 32; // Exact closed-fixture bound, not an operational budget.
constexpr size_t StatusFieldCount = 40;
constexpr size_t LandFieldCount = 12;
constexpr size_t MaximumTokens = StatusFieldCount + 2;
constexpr size_t LineCapacity = 1024;

enum class Kind : uint8_t { Status, Land };
enum class Fault : unsigned { None, AdministrationOrder, Input, Publication, Sink };

struct StateInput {
	Kind kind{};
	uint64_t source_us{};
	uint64_t publication_us{};
	std::array<uint64_t, StatusFieldCount> fields{};
};

std::array<StateInput, StateCapacity> inputs{};
uORB::Publication<vehicle_status_s> vehicle_status_publication{ORB_ID(vehicle_status)};
uORB::Publication<vehicle_land_detected_s> land_detected_publication{ORB_ID(vehicle_land_detected)};
size_t input_count{};
size_t status_count{};
size_t land_count{};
size_t next_input{};
uint64_t first_imu_us{};
uint64_t last_imu_us{};
uint64_t step_us{};
uint64_t last_limit_us{};
unsigned first_fault{};
bool bootstrapped{};
bool finished{};

void fail(Fault fault)
{
	if (!first_fault) {
		first_fault = static_cast<unsigned>(fault);
	}
}

bool unsigned_value(const char *text, uint64_t &value)
{
	if (!text || !*text) {
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

size_t tokens(char *line, std::array<char *, MaximumTokens> &result)
{
	size_t count = 0;
	char *context = nullptr;

	for (char *token = strtok_r(line, " \t\r\n", &context); token;
	     token = strtok_r(nullptr, " \t\r\n", &context)) {
		if (count == result.size()) {
			return result.size() + 1;
		}

		result[count++] = token;
	}

	return count;
}

bool boolean_fields(const std::array<uint64_t, StatusFieldCount> &fields, Kind kind)
{
	if (kind == Kind::Land) {
		for (size_t index = 0; index < LandFieldCount; ++index) {
			if (fields[index] > 1) {
				return false;
			}
		}

		return true;
	}

	constexpr size_t BooleanIndexes[] {10, 15, 16, 18, 20, 21, 22, 23, 24, 28, 29,
					   30, 31, 32, 33, 34, 35, 36, 37, 38, 39
					  };

	for (size_t index : BooleanIndexes) {
		if (fields[index] > 1) {
			return false;
		}
	}

	for (size_t index = 2; index < StatusFieldCount; ++index) {
		if (index != 5 && index != 11 && index != 12 && fields[index] > UINT8_MAX) {
			return false;
		}
	}

	return fields[11] <= UINT32_MAX && fields[12] <= UINT32_MAX;
}

bool parse_header(char *line, size_t &expected_status, size_t &expected_land, size_t &expected_total)
{
	std::array<char *, MaximumTokens> fields{};
	const size_t count = tokens(line, fields);
	uint64_t values[7] {};

	if (count != 8 || strcmp(fields[0], "M") != 0) {
		return false;
	}

	for (size_t index = 0; index < 7; ++index) {
		if (!unsigned_value(fields[index + 1], values[index])) {
			return false;
		}
	}

	first_imu_us = values[0];
	last_imu_us = values[1];
	step_us = values[2];
	const uint64_t imu_count = values[3];
	expected_status = values[4];
	expected_land = values[5];
	expected_total = values[6];
	return first_imu_us && last_imu_us >= first_imu_us && step_us
	       && imu_count == 1200 && last_imu_us == first_imu_us + (imu_count - 1) * step_us
	       && expected_total <= StateCapacity && expected_total == expected_status + expected_land
	       && expected_status && expected_land;
}

bool parse_input(char *line, uint64_t previous_source)
{
	std::array<char *, MaximumTokens> tokens_found{};
	const size_t count = tokens(line, tokens_found);
	const bool status = count && strcmp(tokens_found[0], "S") == 0;
	const bool land = count && strcmp(tokens_found[0], "L") == 0;
	const size_t field_count = status ? StatusFieldCount : LandFieldCount;

	if ((!status && !land) || count != field_count + 2 || input_count == StateCapacity) {
		return false;
	}

	StateInput value{};
	value.kind = status ? Kind::Status : Kind::Land;

	if (!unsigned_value(tokens_found[1], value.source_us) || !value.source_us
	    || value.source_us < previous_source || value.source_us > last_imu_us) {
		return false;
	}

	for (size_t index = 0; index < field_count; ++index) {
		if (!unsigned_value(tokens_found[index + 2], value.fields[index])) {
			return false;
		}
	}

	if (!boolean_fields(value.fields, value.kind)) {
		return false;
	}

	inputs[input_count++] = value;
	status_count += status;
	land_count += land;
	return true;
}

bool publish_status(StateInput &input)
{
	const auto &field = input.fields;
	vehicle_status_s value{};
	value.timestamp = hrt_absolute_time();
	value.armed_time = field[0];
	value.takeoff_time = field[1];
	value.arming_state = field[2];
	value.latest_arming_reason = field[3];
	value.latest_disarming_reason = field[4];
	value.nav_state_timestamp = field[5];
	value.nav_state_user_intention = field[6];
	value.nav_state = field[7];
	value.executor_in_charge = field[8];
	value.nav_state_display = field[9];
	value.accepts_offboard_setpoints = field[10];
	value.valid_nav_states_mask = field[11];
	value.can_set_nav_states_mask = field[12];
	value.hil_state = field[13];
	value.vehicle_type = field[14];
	value.failsafe = field[15];
	value.failsafe_and_user_took_over = field[16];
	value.failsafe_defer_state = field[17];
	value.gcs_connection_lost = field[18];
	value.gcs_connection_lost_counter = field[19];
	value.high_latency_data_link_lost = field[20];
	value.is_vtol = field[21];
	value.is_vtol_tailsitter = field[22];
	value.in_transition_mode = field[23];
	value.in_transition_to_fw = field[24];
	value.system_type = field[25];
	value.system_id = field[26];
	value.component_id = field[27];
	value.safety_button_available = field[28];
	value.safety_off = field[29];
	value.power_input_valid = field[30];
	value.usb_connected = field[31];
	value.open_drone_id_system_present = field[32];
	value.open_drone_id_system_healthy = field[33];
	value.parachute_system_present = field[34];
	value.parachute_system_healthy = field[35];
	value.traffic_avoidance_system_present = field[36];
	value.rc_calibration_in_progress = field[37];
	value.calibration_enabled = field[38];
	value.pre_flight_checks_pass = field[39];
	input.publication_us = value.timestamp;
	return vehicle_status_publication.publish(value);
}

bool publish_land(StateInput &input)
{
	const auto &field = input.fields;
	vehicle_land_detected_s value{};
	value.timestamp = hrt_absolute_time();
	value.freefall = field[0];
	value.ground_contact = field[1];
	value.maybe_landed = field[2];
	value.landed = field[3];
	value.in_ground_effect = field[4];
	value.in_descend = field[5];
	value.has_low_throttle = field[6];
	value.vertical_movement = field[7];
	value.horizontal_movement = field[8];
	value.rotational_movement = field[9];
	value.close_to_ground_or_skipped_check = field[10];
	value.at_rest = field[11];
	input.publication_us = value.timestamp;
	return land_detected_publication.publish(value);
}
}

bool research_recorded_state_load(const char *path) noexcept
{
	if (bootstrapped || !path) {
		fail(Fault::AdministrationOrder);
		return false;
	}

	bootstrapped = true; // A failed bootstrap cannot be retried in this process.
	FILE *file = fopen(path, "r");

	if (!file) {
		fail(Fault::Input);
		return false;
	}

	char line[LineCapacity] {};
	size_t expected_status{};
	size_t expected_land{};
	size_t expected_total{};
	bool ok = fgets(line, sizeof(line), file)
		  && parse_header(line, expected_status, expected_land, expected_total);
	uint64_t previous_source{};

	while (ok && fgets(line, sizeof(line), file)) {
		ok = strchr(line, '\n') && parse_input(line, previous_source);

		if (ok) {
			previous_source = inputs[input_count - 1].source_us;
		}
	}

	const bool read_ok = ok && !ferror(file);
	const bool close_ok = fclose(file) == 0;
	ok = read_ok && close_ok && input_count == expected_total
	     && status_count == expected_status && land_count == expected_land
	     && inputs[0].source_us <= first_imu_us;

	if (!ok) {
		fail(Fault::Input);
	}

	return ok;
}

uint64_t research_recorded_state_first_imu_us() noexcept
{
	return bootstrapped && !first_fault ? first_imu_us : 0;
}

uint64_t research_recorded_state_step_us() noexcept
{
	return bootstrapped && !first_fault ? step_us : 0;
}

bool research_recorded_state_publish_through(uint64_t source_us) noexcept
{
	if (!bootstrapped || finished || first_fault || source_us < last_limit_us
	    || source_us < first_imu_us || source_us > last_imu_us) {
		fail(Fault::AdministrationOrder);
		return false;
	}

	last_limit_us = source_us;

	while (next_input < input_count && inputs[next_input].source_us <= source_us) {
		StateInput &input = inputs[next_input];
		const bool published = input.kind == Kind::Status ? publish_status(input) : publish_land(input);

		if (!published || !input.publication_us) {
			fail(Fault::Publication);
			return false;
		}

		++next_input;
	}

	return true;
}

bool research_recorded_state_complete() noexcept
{
	return bootstrapped && !finished && !first_fault && next_input == input_count
	       && last_limit_us == last_imu_us;
}

bool research_recorded_state_finish(const char *path) noexcept
{
	if (!bootstrapped || finished || !path) {
		fail(Fault::AdministrationOrder);
		return false;
	}

	finished = true;

	if (next_input != input_count || last_limit_us != last_imu_us) {
		fail(Fault::AdministrationOrder);
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
			  "{\"schema\":\"p2i-recorded-system-state/v1\",\"fault\":%u,"
			  "\"first_imu_source_us\":%" PRIu64 ",\"last_imu_source_us\":%" PRIu64 ","
			  "\"step_us\":%" PRIu64 ",\"counts\":{\"total\":%zu,\"status\":%zu,\"land\":%zu},"
			  "\"published_count\":%zu,\"actuator_publications\":false,\"events\":[\n",
			  first_fault, first_imu_us, last_imu_us, step_us, input_count,
			  status_count, land_count, next_input) > 0;

	for (size_t index = 0; ok && index < input_count; ++index) {
		const StateInput &input = inputs[index];
		ok = fprintf(file,
			     "%s{\"sequence\":%zu,\"kind\":\"%s\",\"source_us\":%" PRIu64 ","
			     "\"publication_us\":%" PRIu64 "}",
			     index ? ",\n" : "", index + 1,
			     input.kind == Kind::Status ? "vehicle_status" : "vehicle_land_detected",
			     input.source_us, input.publication_us) > 0;
	}

	ok = ok && fprintf(file, "\n]}\n") > 0;

	if (fclose(file) != 0) {
		ok = false;
	}

	if (!ok) {
		fail(Fault::Sink);
	}

	return ok && !first_fault;
}
