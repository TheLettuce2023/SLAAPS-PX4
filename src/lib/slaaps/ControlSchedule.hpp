#pragma once

#include "OperatingCondition.hpp"
#include "VehicleConfiguration.hpp"
#include <cmath>

namespace slaaps
{

enum class ControlScheduleStatus : uint8_t {
	Inactive, Ready, InvalidModel, Stale, SourceChanged, OutsideEnvelope, Transient, TransitionRejected
};

// Empirical IAS/EDF-command table, not an inferred canard-wash model. This
// numerical envelope never supplies measured-condition or flight admission.
// observe() also runs while handling is pending; only commit() moves the filter.
class ControlSchedule
{
public:
	struct Step {
		uint64_t timestamp_us{};
		float airspeed{}, edf{};
		research_stabilized_manual::ScheduledControlPoint control{};
		WrenchModel wrench{}; // Physical affine model at the identical filtered coordinates.
		bool wrench_valid{};
	};

	bool begin(uint64_t now, const VehicleConfiguration &configuration,
		   const OperatingConditionSample &sample, float edf, Step &step)
	{
		stop();

		if (configuration.schedule.enabled != 1 || !validateControlSchedule(configuration).valid()) {
			return fail(ControlScheduleStatus::InvalidModel);
		}

		_configuration = configuration.schedule;
		// Incomplete acceleration calibration cannot remove Manual authority.
		// The acceleration consumer must require wrench_valid independently.
		_wrench_enabled = configuration.wrench.enabled == 1 && validateWrenchConfiguration(configuration).valid();

		if (_wrench_enabled) {
			for (unsigned i = 0; i < kScheduleKnots; ++i) {
				for (unsigned j = 0; j < kScheduleKnots; ++j) {
					if (!makeWrenchCornerModel(configuration, i, j, _wrench_corner[i][j])) {
						_wrench_enabled = false;
					}
				}
			}
		}

		const auto &source = sample.source;

		if (!source.airspeed_revision || !source.air_data_revision || !source.battery_revision || !source.baro_device_id
		    || source.airspeed_instance != _configuration.airspeed_instance
		    || source.air_data_instance != _configuration.air_data_instance
		    || source.battery_instance != _configuration.battery_instance || source.battery_id != _configuration.battery_id) {
			return fail(ControlScheduleStatus::SourceChanged);
		}

		for (unsigned axis = 0; axis < kAxes; ++axis) { _scale[axis] = configuration.axis[axis].moment_scale; }

		for (unsigned canard = 0; canard < kCanards; ++canard) { _slope[canard] = configuration.canard[canard].deflection_slope; }

		_source = sample.source;
		_active = true;

		if (!observe(now, sample, edf)) { return false; }

		Step candidate{};
		candidate.timestamp_us = now;
		candidate.airspeed = sample.indicated_airspeed_m_s;
		candidate.edf = edf;

		if (!interpolate(candidate)) { return fail(ControlScheduleStatus::InvalidModel); }

		commit(candidate);
		step = candidate;
		return true;
	}

	bool observe(uint64_t now, const OperatingConditionSample &sample, float edf)
	{
		if (!_active) { return false; }

		if (!sameOperatingConditionSource(_source, sample.source)) { return fail(ControlScheduleStatus::SourceChanged); }

		const uint64_t age = static_cast<uint64_t>(_configuration.maximum_age_us);
		const uint64_t timestamps[] {sample.airspeed_timestamp_us, sample.airspeed_sample_us,
					     sample.air_data_timestamp_us, sample.air_data_sample_us, sample.battery_timestamp_us
					    };
		uint64_t deadline = UINT64_MAX;

		if (!sample.observed_us || sample.observed_us > now || now < _observed_us
		    || sample.airspeed_sample_us > sample.airspeed_timestamp_us
		    || sample.air_data_sample_us > sample.air_data_timestamp_us) { return fail(ControlScheduleStatus::Stale); }

		for (uint64_t timestamp : timestamps) {
			if (!timestamp || timestamp > sample.observed_us || timestamp > now || now - timestamp > age
			    || timestamp > UINT64_MAX - age - 1) { return fail(ControlScheduleStatus::Stale); }

			const uint64_t expiry = timestamp + age + 1;
			deadline = expiry < deadline ? expiry : deadline;
		}

		if (sample.deadline_us != deadline || now >= deadline) { return fail(ControlScheduleStatus::Stale); }

		const auto &c = _configuration;

		if (!within(sample.indicated_airspeed_m_s, c.indicated_airspeed_m_s[0], c.indicated_airspeed_m_s[1])
		    || !within(edf, c.edf_command[0], c.edf_command[1])
		    || !within(sample.density_kg_m3, c.density_min, c.density_max)
		    || !within(sample.voltage_v, c.voltage_min, c.voltage_max)) { return fail(ControlScheduleStatus::OutsideEnvelope); }

		if (_observed_us) {
			if (sample.observed_us < _last.observed_us || sample.airspeed_timestamp_us < _last.airspeed_timestamp_us
			    || sample.air_data_timestamp_us < _last.air_data_timestamp_us || sample.battery_timestamp_us < _last.battery_timestamp_us
			    || sample.airspeed_sample_us < _last.airspeed_sample_us || sample.air_data_sample_us < _last.air_data_sample_us
			    || (sample.airspeed_sample_us == _last.airspeed_sample_us && !sameFiniteValue(sample.indicated_airspeed_m_s, _last.indicated_airspeed_m_s))
			    || (sample.air_data_sample_us == _last.air_data_sample_us && !sameFiniteValue(sample.density_kg_m3, _last.density_kg_m3))
			    || (sample.battery_timestamp_us == _last.battery_timestamp_us && !sameFiniteValue(sample.voltage_v, _last.voltage_v))) {
				return fail(ControlScheduleStatus::SourceChanged);
			}

			if (!rateBounded(sample.indicated_airspeed_m_s - _last.indicated_airspeed_m_s,
					 sample.airspeed_sample_us - _last.airspeed_sample_us, c.airspeed_transient_rate)
			    || !edfRateBounded(_raw_edf, edf, now - _observed_us, c.edf_transient_rate)) {
				return fail(ControlScheduleStatus::Transient);
			}
		}

		_last = sample;
		_raw_edf = edf;
		_observed_us = now;
		_deadline_us = deadline;
		_status = ControlScheduleStatus::Ready;
		return true;
	}

	bool evaluate(uint64_t now, Step &step)
	{
		if (!_active || now < _filter_us || now < _observed_us || now >= _deadline_us) { return fail(ControlScheduleStatus::Stale); }

		const float dt = static_cast<float>(now - _filter_us) * 1e-6F;
		Step candidate{};
		candidate.timestamp_us = now;
		candidate.airspeed = filtered(_airspeed, _last.indicated_airspeed_m_s, dt, _configuration.airspeed_filter_rate);
		candidate.edf = filtered(_edf, _raw_edf, dt, _configuration.edf_filter_rate);

		if (!interpolate(candidate)) { return fail(ControlScheduleStatus::InvalidModel); }

		step = candidate;
		return true;
	}

	void commit(const Step &step)
	{
		_filter_us = step.timestamp_us;
		_airspeed = step.airspeed;
		_edf = step.edf;
	}
	void stop() { _active = false; _observed_us = _filter_us = _deadline_us = 0; _status = ControlScheduleStatus::Inactive; }
	uint64_t deadline() const { return _deadline_us; }
	ControlScheduleStatus status() const { return _status; }

private:
	// Exact numeric equality for finite observations, including either signed zero.
	static bool sameFiniteValue(float a, float b) { return a <= b && a >= b; }
	static bool within(float value, float low, float high) { return std::isfinite(value) && value >= low && value <= high; }
	static bool rateBounded(float delta, uint64_t elapsed, float limit)
	{
		return std::isfinite(delta) && (elapsed ? std::fabs(delta) <= limit * (static_cast<float>(elapsed) * 1e-6F) : sameFiniteValue(delta, 0.F));
	}
	static bool edfRateBounded(float previous, float current, uint64_t elapsed, float limit)
	{
		if (!std::isfinite(previous) || !std::isfinite(current) || !std::isfinite(limit) || limit <= 0.F) { return false; }

		if (!elapsed) { return sameFiniteValue(previous, current); }

		// The owner emits a binary32 command after adding its bounded step.
		// Subtracting those two rounded commands can exceed the original step.
		// Use the same reachable encoded endpoints, without an epsilon or an
		// enlarged rate. IAS retains its independent measured-delta check above.
		const float step = limit * (static_cast<float>(elapsed) * 1e-6F);
		return std::isfinite(step) && current >= previous - step && current <= previous + step;
	}
	float filtered(float previous, float target, float dt, float rate) const
	{
		const float delta = (target - previous) * dt / (_configuration.filter_time_s + dt);
		const float bound = rate * dt;
		return previous + (delta > bound ? bound : delta < -bound ? -bound : delta);
	}
	bool interpolate(Step &step) const
	{
		const auto &c = _configuration;
		const float x = (step.airspeed - c.indicated_airspeed_m_s[0]) / (c.indicated_airspeed_m_s[1] - c.indicated_airspeed_m_s[0]);
		const float y = (step.edf - c.edf_command[0]) / (c.edf_command[1] - c.edf_command[0]);

		if (!within(x, 0.F, 1.F) || !within(y, 0.F, 1.F)) { return false; }

		for (unsigned i = 0; i < 2; ++i) {
			for (unsigned j = 0; j < 2; ++j) {
				const float weight = (i ? x : 1.F - x) * (j ? y : 1.F - y);
				const auto &corner = c.corner[i][j];

				if (_wrench_enabled) {
					const auto &physical = _wrench_corner[i][j];

					for (unsigned row = 0; row < kWrenchAxes; ++row) {
						step.wrench.offset[row] += weight * physical.offset[row];

						for (unsigned actuator = 0; actuator < kWrenchInputs; ++actuator) {
							step.wrench.effectiveness[row][actuator] += weight * physical.effectiveness[row][actuator];
						}
					}
				}

				for (unsigned axis = 0; axis < kAxes; ++axis) {
					step.control.rate_p(axis) += weight * corner.rate_p[axis];
					step.control.rate_i(axis) += weight * corner.rate_i[axis];
					step.control.rate_d(axis) += weight * corner.rate_d[axis];

					for (unsigned canard = 0; canard < kCanards; ++canard) {
						step.control.effectiveness[axis][canard] += weight * corner.moment_derivative[axis][canard]
								* _slope[canard] / _scale[axis];
					}
				}
			}
		}

		step.wrench_valid = _wrench_enabled;
		return true; // Controller validates the interpolated gains and matrix before committing.
	}
	bool fail(ControlScheduleStatus status) { _status = status; _active = false; return false; }
	ControlScheduleConfiguration _configuration{};
	WrenchModel _wrench_corner[kScheduleKnots][kScheduleKnots] {};
	OperatingConditionSourceIdentity _source{};
	OperatingConditionSample _last{};
	float _scale[kAxes] {}, _slope[kCanards] {};
	float _airspeed{}, _edf{}, _raw_edf{};
	uint64_t _observed_us{}, _filter_us{}, _deadline_us{};
	ControlScheduleStatus _status{ControlScheduleStatus::Inactive};
	bool _active{};
	bool _wrench_enabled{};
};

} // namespace slaaps
