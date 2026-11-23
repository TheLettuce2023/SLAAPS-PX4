#pragma once

#include <cmath>
#include <cstdint>

namespace slaaps
{

// Launcher-stage airspeed (SLA_FLT_STAGE 2, SLA_LAUNCH_AS_I, SLA_LAUNCH_AGE, SLA_LAUNCH_AS).
//
// A zeroed pitot reads a little either side of zero in still air (-0.69 to +0.93 m/s on the board, 2026-11-23), so a
// small negative indicated airspeed is a valid sample below the gate speed, not a fault. Below this limit it is a bad
// zero or reversed tubes, and the sample does not admit an arming.
static constexpr float kLaunchAirspeedReversedM_s = -5.F;

struct LaunchAirspeedSample {
	uint64_t timestamp_sample_us{};
	float indicated_m_s{NAN};
	float confidence{};
};

// Fresh (sample age at most SLA_LAUNCH_AGE), finite, not strongly negative, with a confidence in (0, 1].
inline bool launchAirspeedValid(const LaunchAirspeedSample &sample, uint64_t now_us, int32_t age_ms)
{
	if (age_ms <= 0 || sample.timestamp_sample_us == 0 || sample.timestamp_sample_us > now_us
	    || now_us - sample.timestamp_sample_us > static_cast<uint64_t>(age_ms) * 1000U) { return false; }

	return std::isfinite(sample.indicated_m_s) && sample.indicated_m_s >= kLaunchAirspeedReversedM_s
	       && std::isfinite(sample.confidence) && sample.confidence > 0.F && sample.confidence <= 1.F;
}

// The airspeed term of launcher-stage flight admission. A valid sample is required to admit an arming. Once that
// arming is admitted it stays admitted whatever the pitot does: withdrawing admission cancels the session, which
// stops the output stream and leaves the canards unpowered, so a stale sample (a CAN dropout) or noise must not
// do it mid-launch. The attitude gate is separate and still opens only on a valid sample at or above SLA_LAUNCH_AS.
// A disarm, or a new arming (a different armed_time), needs a valid sample again. D1 re-entry within the same
// arming keeps the admission.
class LaunchAdmission
{
public:
	// Returns whether the airspeed term admits flight. Outside the launcher stage the term does not apply.
	bool update(bool launcher, bool armed, uint64_t armed_time_us, bool sample_valid)
	{
		if (!launcher || !armed) {
			_admitted = false;
			_armed_time_us = 0;
			return !launcher;
		}

		if (_admitted && _armed_time_us != armed_time_us) { _admitted = false; }

		if (!_admitted && sample_valid) {
			_admitted = true;
			_armed_time_us = armed_time_us;
		}

		return _admitted;
	}

	bool admitted() const { return _admitted; }

private:
	bool _admitted{};
	uint64_t _armed_time_us{};
};

} // namespace slaaps
