#include "ObserverProfile.hpp"

#include <cerrno>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <float.h>

namespace research_stabilized_manual
{
namespace
{
bool token(FILE *stream, char (&text)[64])
{
	int c{};
	do { c = fgetc(stream); } while (c != EOF && std::isspace(c));

	unsigned length = 0;

	while (c != EOF && !std::isspace(c)) {
		if (length + 1 >= sizeof(text) || c == 0) { return false; }

		text[length++] = static_cast<char>(c);
		c = fgetc(stream);
	}

	text[length] = '\0';
	return length != 0 && !ferror(stream);
}

bool scalar(FILE *stream, float &value)
{
	char text[64] {};

	if (!token(stream, text)) { return false; }

	char *end = nullptr;
	errno = 0;
	const double parsed = strtod(text, &end);

	if (errno != 0 || end == text || *end != '\0' || !std::isfinite(parsed)
	    || std::fabs(parsed) > static_cast<double>(FLT_MAX)) { return false; }

	value = static_cast<float>(parsed);
	return true;
}

bool integer(FILE *stream, TimeUs &value, TimeUs maximum)
{
	char text[64] {};

	if (!token(stream, text)) { return false; }

	TimeUs parsed = 0;

	for (const char *p = text; *p; ++p) {
		if (*p < '0' || *p > '9') { return false; }

		const unsigned digit = static_cast<unsigned>(*p - '0');

		if (digit > maximum || parsed > (maximum - digit) / 10) { return false; }

		parsed = parsed * 10 + digit;
	}

	value = parsed;
	return true;
}
}

bool readObserverProfile(FILE *stream, ObserverProfile &profile)
{
	if (!stream) { return false; }

	char header[64] {};
	ObserverProfile candidate{};
	auto &c = candidate.control;
	TimeUs instance{}, system{}, component{};

	if (!token(stream, header) || strcmp(header, "SLAAPS_STABILIZED_OBSERVER_V1") != 0
	    || !integer(stream, instance, 5) || !integer(stream, system, 255) || !integer(stream, component, 255)) { return false; }

	c.mavlink_instance = instance;
	c.source_system_id = system;
	c.source_component_id = component;
	TimeUs *times[] = {&c.maximum_manual_age_us, &c.maximum_state_age_us, &c.maximum_state_skew_us,
			   &c.minimum_cycle_us, &c.maximum_cycle_us, &candidate.maximum_status_age_us, &candidate.maximum_permission_age_us
			  };

	for (auto *value : times) {
		if (!integer(stream, *value, UINT32_MAX) || *value == 0) { return false; }
	}

	if (!scalar(stream, c.quaternion_norm_tolerance) || !scalar(stream, c.centered_enter)
	    || !scalar(stream, c.centered_exit)) { return false; }

	Vector3f *vectors[] = {&c.stick_sign, &c.maximum_rate_rad_s, &c.maximum_rate_setpoint_accel_rad_s2,
			       &c.maximum_measured_rate_rad_s, &c.maximum_measured_accel_rad_s2, &c.attitude_gain,
			       &c.rate_p, &c.rate_i, &c.rate_d, &c.integrator_limit, &c.maximum_normalized_moment
			      };

	for (auto *value : vectors) {
		for (unsigned axis = 0; axis < 3; ++axis) { if (!scalar(stream, (*value)(axis))) { return false; } }
	}

	for (auto &row : c.allocation.effectiveness) {
		for (float &value : row) { if (!scalar(stream, value)) { return false; } }
	}

	CanardVector *surfaces[] = {&c.allocation.trim, &c.allocation.minimum, &c.allocation.maximum,
				    &c.allocation.positive_slew_per_s, &c.allocation.negative_slew_per_s
				   };

	for (auto *value : surfaces) {
		for (float &entry : *value) { if (!scalar(stream, entry)) { return false; } }
	}

	for (unsigned axis = 0; axis < 3; ++axis) {
		if (!scalar(stream, c.allocation.axis_weight(axis))) { return false; }
	}

	if (!scalar(stream, c.allocation.regularization) || !scalar(stream, c.allocation.minimum_row_norm)
	    || !scalar(stream, c.allocation.minimum_independence) || !scalar(stream, c.allocation.saturation_tolerance)) { return false; }

	int extra{};
	do { extra = fgetc(stream); } while (extra != EOF && std::isspace(extra));

	if (extra != EOF || ferror(stream)) { return false; }

	// A wind-only/observation configuration has no propulsion demand. The core
	// permits zero simulated thrust; this adapter never emits a motor command.
	c.fixed_simulated_thrust_n = 0.0F;
	StabilizedManualControl validator;

	if (validator.configure(c) != Status::Ok) { return false; }

	profile = candidate;
	return true;
}
} // namespace research_stabilized_manual
