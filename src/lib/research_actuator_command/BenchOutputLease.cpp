#include "BenchOutputLease.hpp"

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <float.h>

namespace research_actuator_command
{
namespace
{
bool number(FILE *stream, double &result)
{
	char token[48] {};

	if (fscanf(stream, "%47s", token) != 1) { return false; }

	char *end = nullptr;
	errno = 0;
	result = strtod(token, &end);
	return errno == 0 && end != token && *end == '\0' && std::isfinite(result);
}

bool scalar(FILE *stream, float &value)
{
	double parsed{};

	if (!number(stream, parsed) || std::fabs(parsed) > static_cast<double>(FLT_MAX)) { return false; }

	value = static_cast<float>(parsed);
	return true;
}

bool integer(FILE *stream, unsigned &value, unsigned maximum)
{
	double parsed{};

	if (!number(stream, parsed) || parsed < 0 || parsed > maximum || std::floor(parsed) < parsed) { return false; }

	value = static_cast<unsigned>(parsed);
	return true;
}
}

bool readBenchOutputProfile(FILE *stream, BenchOutputProfile &profile)
{
	if (!stream) { return false; }

	BenchOutputProfile candidate{};
	unsigned version{}, instance{}, system{}, component{}, gap{}, lease{};

	if (!integer(stream, version, 1U) || version != 1U
	    || !integer(stream, instance, 5U) || !integer(stream, system, 255U)
	    || !integer(stream, component, 255U) || !integer(stream, gap, kBenchSampleAgeUs)
	    || !scalar(stream, candidate.control.neutral_entry_tolerance)
	    || !scalar(stream, candidate.deadzone) || !integer(stream, lease, kBenchSampleAgeUs)
	    || candidate.deadzone < 0.0F || candidate.deadzone >= 1.0F || lease < gap) { return false; }

	candidate.control.mavlink_instance = instance;
	candidate.control.source_system_id = system;
	candidate.control.source_component_id = component;
	candidate.control.maximum_cycle_gap_us = gap;
	candidate.lease_us = lease;

	for (std::size_t i = 0; i < kBenchCanardCount; ++i) {
		auto &servo = candidate.control.servos[i];

		if (!scalar(stream, servo.negative_pwm_us) || !scalar(stream, servo.neutral_pwm_us)
		    || !scalar(stream, servo.positive_pwm_us) || !scalar(stream, servo.maximum_deflection_deg)
		    || !scalar(stream, servo.rise_deg_per_s) || !scalar(stream, servo.fall_deg_per_s)) { return false; }

		for (float &coefficient : candidate.control.mix[i]) {
			if (!scalar(stream, coefficient)) { return false; }
		}
	}

	char extra[2] {};

	if (fscanf(stream, "%1s", extra) != EOF || ferror(stream)) { return false; }

	BenchControlOwner validator;

	if (validator.configure(candidate.control) != BenchStatus::Ok) { return false; }

	profile = candidate;
	return true;
}

float benchDeadzone(float value, float deadzone)
{
	if (!std::isfinite(value) || !std::isfinite(deadzone) || deadzone < 0.0F || deadzone >= 1.0F
	    || std::fabs(value) > 1.0F) { return NAN; }

	if (std::fabs(value) <= deadzone) { return 0.0F; }

	const float magnitude = (std::fabs(value) - deadzone) / (1.0F - deadzone);
	return value < 0.0F ? -magnitude : magnitude;
}

bool benchPwmToControl(const BenchVector &pwm, const BenchConfiguration &configuration, BenchVector &control)
{
	BenchVector candidate{};

	for (std::size_t i = 0; i < kBenchCanardCount; ++i) {
		const float center = configuration.servos[i].neutral_pwm_us;

		if (!std::isfinite(center) || center <= 500.0F || center >= 2500.0F
		    || !std::isfinite(pwm[i]) || pwm[i] < 500.0F || pwm[i] > 2500.0F) { return false; }

		const float delta = pwm[i] - center;
		candidate[i] = delta / (delta < 0.0F ? center - 500.0F : 2500.0F - center);
	}

	control = candidate;
	return true;
}

void BenchOutputLease::update(RuntimeTimeUs now, const BenchOutputFrame &frame)
{
	// A late producer cannot renew an expired session before value() polls it.
	if (_ever_enabled && (now < _frame.timestamp || now - _frame.timestamp >= _frame.lease_us)) { _fault = true; }

	if (frame.session < _session) { _fault = true; return; }

	const bool new_session = frame.session > _session;

	if (new_session) { _session = frame.session; _timestamp = 0; }

	bool valid = frame.session > 0 && frame.timestamp >= frame.session && frame.timestamp > _timestamp
		     && now >= frame.timestamp && frame.lease_us >= kBenchIntervalUs
		     && frame.lease_us <= kBenchSampleAgeUs && now - frame.timestamp < frame.lease_us;

	for (float control : frame.control) { valid &= std::isfinite(control) && std::fabs(control) <= 1.0F; }

	if (!valid || !frame.enabled || (_fault && !new_session)) { _fault = true; return; }

	_frame = frame;
	_timestamp = frame.timestamp;
	_ever_enabled = true;
	_fault = false;
}

float BenchOutputLease::value(RuntimeTimeUs now, std::size_t channel)
{
	if (!_ever_enabled || channel >= kBenchCanardCount) { return NAN; }

	if (now < _frame.timestamp || now - _frame.timestamp >= _frame.lease_us) { _fault = true; }

	return _fault ? 0.0F : _frame.control[channel];
}

} // namespace research_actuator_command
