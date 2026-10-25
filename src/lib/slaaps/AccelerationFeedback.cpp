#include "AccelerationFeedback.hpp"

#include <lib/research_stabilized_manual/StabilizedInputValidation.hpp>
#include <cmath>
#include <cstring>
#include <float.h>

namespace slaaps
{
namespace
{
bool sameFloat(float left, float right)
{
	return std::memcmp(&left, &right, sizeof(float)) == 0;
}

bool sameVector(const research_stabilized_manual::Vector3f &left,
		const research_stabilized_manual::Vector3f &right)
{
	for (unsigned axis = 0; axis < kAxes; ++axis) {
		if (!sameFloat(left(axis), right(axis))) { return false; }
	}

	return true;
}

bool fresh(uint64_t now, uint64_t observed, uint64_t timestamp, uint64_t age, uint64_t &deadline)
{
	if (!timestamp || timestamp > observed || timestamp > now || now - timestamp > age
	    || age == UINT64_MAX || timestamp > UINT64_MAX - age - 1) { return false; }

	const uint64_t expiry = timestamp + age + 1;
	deadline = expiry < deadline ? expiry : deadline;
	return true;
}

void cross(const double (&left)[3], const double (&right)[3], double (&result)[3])
{
	result[0] = left[1] * right[2] - left[2] * right[1];
	result[1] = left[2] * right[0] - left[0] * right[2];
	result[2] = left[0] * right[1] - left[1] * right[0];
}
}

bool AccelerationFeedback::configure(const VehicleConfiguration &configuration)
{
	stop();

	if (configuration.feedback.enabled != 1 || !validateAccelerationFeedbackConfiguration(configuration).valid()
	    || !std::isfinite(configuration.feedback.cutoff_hz) || configuration.feedback.cutoff_hz < 0.F) { return false; }

	for (float offset : configuration.feedback.imu_position) {
		if (!std::isfinite(offset)) { return false; }
	}

	_configuration = configuration.feedback;
	_numerical.maximum_state_age_us = static_cast<uint64_t>(configuration.state_age_ms) * 1000;
	_numerical.maximum_state_skew_us = static_cast<uint64_t>(configuration.state_skew_ms) * 1000;
	_numerical.quaternion_norm_tolerance = configuration.quaternion_tolerance;

	for (unsigned axis = 0; axis < kAxes; ++axis) {
		_numerical.maximum_measured_rate_rad_s(axis) = configuration.axis[axis].measured_rate_limit;
		_numerical.maximum_measured_accel_rad_s2(axis) = configuration.axis[axis].measured_accel_limit;
	}

	_configured = true;
	return true;
}

AccelerationFeedback::Source AccelerationFeedback::source(const AccelerationFeedbackInput &input)
{
	Source result{};
	result.generation = input.source_generation;
	result.publisher = input.publisher_revision;
	result.device = input.device_id;
	result.accel = input.selected_accel_device_id;
	result.gyro = input.selected_gyro_device_id;
	result.bias = input.bias_publisher_revision;
	result.raw = input.raw_publisher_revision;
	result.state_epoch = input.state.source_epoch;
	result.bias_instance = input.bias_instance;
	result.raw_instance = input.raw_instance;
	result.attitude_reset = input.state.attitude_reset_counter;
	result.cutoff = input.filter_cutoff_hz;
	result.sample_rate = input.filter_sample_rate_hz;
	return result;
}

bool AccelerationFeedback::sameSource(const Source &left, const Source &right)
{
	return left.generation == right.generation && left.publisher == right.publisher && left.device == right.device
	       && left.accel == right.accel && left.gyro == right.gyro && left.bias == right.bias && left.raw == right.raw
	       && left.state_epoch == right.state_epoch && left.bias_instance == right.bias_instance
	       && left.raw_instance == right.raw_instance && left.attitude_reset == right.attitude_reset
	       && sameFloat(left.cutoff, right.cutoff) && sameFloat(left.sample_rate, right.sample_rate);
}

AccelerationFeedbackResult AccelerationFeedback::reject(AccelerationFeedbackResult result)
{
	_settling_started = 0;
	return result;
}

AccelerationFeedbackResult AccelerationFeedback::evaluate(uint64_t now, const AccelerationFeedbackInput &input,
		AccelerationFeedbackSample &destination)
{
	if (!_configured) { return AccelerationFeedbackResult::Disabled; }

	if (!now || now < _clock) { return reject(AccelerationFeedbackResult::InvalidInput); }

	_clock = now;
	const Source current = source(input);

	if (_source_bound && !sameSource(current, _source)) {
		_source_bound = false;
		_have_previous = false;
		return reject(AccelerationFeedbackResult::SourceChanged);
	}

	if (!input.source_generation || !input.publisher_revision || !input.raw_publisher_revision
	    || !input.raw_publisher_current || input.raw_instance >= 4 || !input.device_id
	    || input.device_id != input.selected_accel_device_id || !input.selected_gyro_device_id || !input.calibrated
	    || !input.bias_valid || !input.bias_publisher_current || !input.bias_publisher_revision || input.bias_instance != 0) {
		return reject(AccelerationFeedbackResult::InvalidMetadata);
	}

	if (!std::isfinite(input.filter_cutoff_hz) || !std::isfinite(input.filter_sample_rate_hz)) {
		return reject(AccelerationFeedbackResult::InvalidMetadata);
	}

	if (_configuration.cutoff_hz <= 0.F) {
		if (!sameFloat(input.filter_cutoff_hz, 0.F) || !sameFloat(input.filter_sample_rate_hz, 0.F)) {
			return reject(AccelerationFeedbackResult::InvalidMetadata);
		}

	} else {
		// Match LowPassFilter2p's applied lower cutoff clamp, not parameter intent alone.
		const float minimum_cutoff = input.filter_sample_rate_hz * 0.001F;
		const float expected = _configuration.cutoff_hz > minimum_cutoff ? _configuration.cutoff_hz : minimum_cutoff;

		if (input.filter_sample_rate_hz <= 0.F || input.filter_cutoff_hz <= 0.F
		    || input.filter_cutoff_hz >= input.filter_sample_rate_hz * 0.5F || !sameFloat(input.filter_cutoff_hz, expected)) {
			return reject(AccelerationFeedbackResult::InvalidMetadata);
		}
	}

	if (!input.observed_us || input.observed_us > now || input.timestamp_sample_us > input.timestamp_us
	    || input.bias_timestamp_sample > input.bias_timestamp
	    || (_have_previous && (input.observed_us < _previous.observed_us || input.timestamp_us < _previous.timestamp_us
				   || input.timestamp_sample_us < _previous.timestamp_sample_us
				   || input.bias_timestamp < _previous.bias_timestamp || input.bias_timestamp_sample < _previous.bias_timestamp_sample))) {
		return reject(AccelerationFeedbackResult::Stale);
	}

	uint64_t deadline = UINT64_MAX;
	const uint64_t age = static_cast<uint64_t>(_configuration.maximum_age_us - _configuration.latency_bound_us);
	const uint64_t bias_age = static_cast<uint64_t>(_configuration.maximum_bias_age_us);

	if (!fresh(now, input.observed_us, input.timestamp_us, age, deadline)
	    || !fresh(now, input.observed_us, input.timestamp_sample_us, age, deadline)
	    || !fresh(now, input.observed_us, input.bias_timestamp, bias_age, deadline)
	    || !fresh(now, input.observed_us, input.bias_timestamp_sample, bias_age, deadline)) {
		return reject(AccelerationFeedbackResult::Stale);
	}

	research_stabilized_manual::Quaternion attitude{};

	if (research_stabilized_manual::validateStabilizedState(now, input.state, _numerical,
			_have_previous ? &_previous.state : nullptr, attitude) != research_stabilized_manual::Status::Ok) {
		return reject(AccelerationFeedbackResult::StateRejected);
	}

	const uint64_t state_timestamps[] {input.state.attitude_timestamp_us, input.state.attitude_sample_us,
					   input.state.rate_timestamp_us, input.state.rate_sample_us
					  };

	for (uint64_t timestamp : state_timestamps) {
		if (!fresh(now, input.observed_us, timestamp, _numerical.maximum_state_age_us, deadline)) {
			return reject(AccelerationFeedbackResult::StateRejected);
		}
	}

	uint64_t earliest = input.timestamp_sample_us, latest = earliest;

	const uint64_t samples[] {input.state.attitude_sample_us, input.state.rate_sample_us};

	for (uint64_t timestamp : samples) {
		earliest = timestamp < earliest ? timestamp : earliest;
		latest = timestamp > latest ? timestamp : latest;
	}

	const uint64_t latency = static_cast<uint64_t>(_configuration.latency_bound_us);

	if (latest - earliest > _numerical.maximum_state_skew_us
	    || latency > _numerical.maximum_state_skew_us - (latest - earliest)) {
		return reject(AccelerationFeedbackResult::Skew);
	}

	if (!input.specific_force_frd_m_s2.isAllFinite()
	    || (_have_previous && input.timestamp_sample_us == _previous.timestamp_sample_us
		&& !sameVector(input.specific_force_frd_m_s2, _previous.specific_force_frd_m_s2))) {
		return reject(AccelerationFeedbackResult::InvalidInput);
	}

	double force_squared = 0.0;

	for (unsigned axis = 0; axis < kAxes; ++axis) {
		const double force = input.specific_force_frd_m_s2(axis);
		force_squared += force * force;
	}

	const double force_limit = _configuration.maximum_specific_force_m_s2;

	if (force_squared > force_limit * force_limit) { return reject(AccelerationFeedbackResult::LimitExceeded); }

	const auto gravity = attitude.rotateVectorInverse(research_stabilized_manual::Vector3f(0.F, 0.F, _configuration.gravity_m_s2));
	double rate[3] {}, alpha[3] {}, offset[3] {}, rate_cross_offset[3] {}, tangential[3] {}, centripetal[3] {};

	for (unsigned axis = 0; axis < kAxes; ++axis) {
		rate[axis] = input.state.rate_rad_s(axis);
		alpha[axis] = input.state.angular_accel_rad_s2(axis);
		offset[axis] = _configuration.imu_position[axis];
	}

	cross(rate, offset, rate_cross_offset);
	cross(alpha, offset, tangential);
	cross(rate, rate_cross_offset, centripetal);
	AccelerationFeedbackSample candidate{};
	double acceleration_squared = 0.0, reported_squared = 0.0;

	for (unsigned axis = 0; axis < kAxes; ++axis) {
		const double value = static_cast<double>(input.specific_force_frd_m_s2(axis)) + static_cast<double>(gravity(axis))
				     - tangential[axis] - centripetal[axis];

		if (!std::isfinite(value) || std::fabs(value) > static_cast<double>(FLT_MAX)) { return reject(AccelerationFeedbackResult::ArithmeticFailure); }

		acceleration_squared += value * value;
		candidate.acceleration_frd_m_s2(axis) = static_cast<float>(value);
		const double reported = candidate.acceleration_frd_m_s2(axis);
		reported_squared += reported * reported;
	}

	const double acceleration_limit = _configuration.maximum_acceleration_m_s2;

	if (acceleration_squared > acceleration_limit * acceleration_limit || reported_squared > acceleration_limit * acceleration_limit) {
		return reject(AccelerationFeedbackResult::LimitExceeded);
	}

	if (!_source_bound) { _source = current; _source_bound = true; }

	if (!_settling_started) { _settling_started = now; }

	const uint64_t settling = static_cast<uint64_t>(_configuration.settling_us);

	if (_settling_started > UINT64_MAX - settling) { return reject(AccelerationFeedbackResult::ArithmeticFailure); }

	_previous = input;
	_have_previous = true;

	// Waiting alone never promotes a retained pre-settling measurement.
	if (now < _settling_started + settling || input.timestamp_sample_us < _settling_started + settling) {
		return AccelerationFeedbackResult::Settling;
	}

	candidate.observed_us = input.observed_us;
	candidate.timestamp_sample_us = input.timestamp_sample_us;
	candidate.deadline_us = deadline;
	candidate.source_generation = input.source_generation;
	candidate.publisher_revision = input.publisher_revision;
	candidate.device_id = input.device_id;
	candidate.state_source_epoch = input.state.source_epoch;
	candidate.attitude_reset_counter = input.state.attitude_reset_counter;
	destination = candidate;
	return AccelerationFeedbackResult::Ready;
}

void AccelerationFeedback::stop()
{
	_configuration = {};
	_numerical = {};
	_previous = {};
	_source = {};
	_clock = _settling_started = 0;
	_configured = _source_bound = _have_previous = false;
}

} // namespace slaaps
