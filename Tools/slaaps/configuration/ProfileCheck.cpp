// Check a QGC .params file against the real SLAAPS profile validators, without hardware.
// Usage: slaaps_profile_check <file.params>
// Missing SLAAPS parameters take their schema defaults. A pass is profile completeness only:
// it is not runtime health, timing qualification or flight acceptance.

#include <lib/slaaps/VehicleConfiguration.hpp>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

using namespace slaaps;

namespace
{
const char *faultName(ConfigurationFault fault)
{
	switch (fault) {
	case ConfigurationFault::None: return "valid";
	case ConfigurationFault::ParameterRange: return "parameter range";
	case ConfigurationFault::MissingRecord: return "missing record";
	case ConfigurationFault::InvalidInertia: return "inertia";
	case ConfigurationFault::InvalidServo: return "servo";
	case ConfigurationFault::InvalidTiming: return "timing";
	case ConfigurationFault::InvalidEffectiveness: return "effectiveness";
	case ConfigurationFault::InvalidController: return "controller";
	case ConfigurationFault::InvalidThrustCurve: return "thrust curve";
	case ConfigurationFault::Active: return "active";
	case ConfigurationFault::NoCandidate: return "no candidate";
	case ConfigurationFault::InvalidWrench: return "wrench";
	case ConfigurationFault::InvalidFeedback: return "feedback";
	case ConfigurationFault::InvalidAccelerationControl: return "acceleration control";
	case ConfigurationFault::InvalidRecovery: return "recovery";
	case ConfigurationFault::InvalidFlight: return "flight admission";
	}

	return "unknown";
}

bool report(const char *name, const ConfigurationResult &result)
{
	std::printf("%-28s %s%s%s\n", name, faultName(result.fault), result.parameter ? " at " : "",
		    result.parameter ? result.parameter : "");
	return result.valid();
}
}

int main(int argc, char **argv)
{
	if (argc != 2) {
		std::fprintf(stderr, "usage: %s <file.params>\n", argv[0]);
		return 2;
	}

	std::ifstream file(argv[1]);

	if (!file) {
		std::fprintf(stderr, "cannot read %s\n", argv[1]);
		return 2;
	}

	// QGC format: vehicle-id component-id name value type, tab separated; '#' starts a comment.
	std::map<std::string, std::string> values;
	std::string line;

	while (std::getline(file, line)) {
		if (line.empty() || line[0] == '#') { continue; }

		std::istringstream fields(line);
		std::string vehicle, component, name, value;

		if (fields >> vehicle >> component >> name >> value) { values[name] = value; }
	}

	VehicleConfiguration c{};
	unsigned found{}, defaulted{};
#define SLAAPS_FLOAT(field, name, group, description, unit, minimum, maximum, initial) \
	if (values.count(#name)) { c.field = std::strtof(values[#name].c_str(), nullptr); ++found; } \
	else { c.field = initial; ++defaulted; }
#define SLAAPS_INT(field, name, group, description, unit, minimum, maximum, initial) \
	if (values.count(#name)) { c.field = static_cast<int32_t>(std::strtol(values[#name].c_str(), nullptr, 10)); ++found; } \
	else { c.field = initial; ++defaulted; }
#include <lib/slaaps/VehicleParameters.def>
#undef SLAAPS_INT
#undef SLAAPS_FLOAT
	const char *imu[kAxes] {"EKF2_IMU_POS_X", "EKF2_IMU_POS_Y", "EKF2_IMU_POS_Z"};

	for (unsigned axis = 0; axis < kAxes; ++axis) {
		c.feedback.imu_position[axis] = values.count(imu[axis]) ? std::strtof(values[imu[axis]].c_str(), nullptr) : 0.F;
	}

	// PX4 default IMU_ACCEL_CUTOFF when the file does not carry it.
	c.feedback.cutoff_hz = values.count("IMU_ACCEL_CUTOFF") ? std::strtof(values["IMU_ACCEL_CUTOFF"].c_str(), nullptr) : 30.F;
	std::printf("%u SLAAPS parameters from the file, %u schema defaults\n", found, defaulted);
	bool manual = report("Servo bench", validate(c, ConfigurationUse::Bench));
	manual = report("Stabilized manual", validate(c, ConfigurationUse::StabilizedManual)) && manual;
	report("EDF commissioning", validate(c, ConfigurationUse::EdfCommissioning));
	report("Power", validatePowerConfiguration(c, true));
	const bool recovery = report("Recovery (D1)", validateRecoveryConfiguration(c));
	const bool flight = report("Flight admission profile", validateFlightConfiguration(c));
	report("Companion source", validateCompanionConfiguration(c));
	report("Control schedule", validateControlSchedule(c));
	report("Wrench model", validateWrenchConfiguration(c));
	report("Acceleration feedback", validateAccelerationFeedbackConfiguration(c));
	const bool acceleration = report("Acceleration control", validateAccelerationControlConfiguration(c));
	std::printf("SLAAPS Manual flight profile: %s\nSLAAPS Acceleration flight profile: %s\n",
		    manual && recovery && flight ? "complete" : "incomplete",
		    manual && recovery && flight && acceleration ? "complete" : "incomplete");
	return manual && recovery && flight ? 0 : 1;
}
