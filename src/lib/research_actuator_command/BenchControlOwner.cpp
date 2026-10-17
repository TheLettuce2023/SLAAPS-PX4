#include "BenchControlOwner.hpp"

#include <cmath>

namespace research_actuator_command
{
namespace
{
constexpr float kServoMinimumPulseUs{500.0F};
constexpr float kServoMaximumPulseUs{2500.0F};
constexpr double kMicrosecondsPerSecond{1000000.0};

bool validPulse(float pulse)
{
	return std::isfinite(pulse) && pulse >= kServoMinimumPulseUs && pulse <= kServoMaximumPulseUs;
}

bool validConfiguration(const BenchConfiguration &configuration)
{
	if (!std::isfinite(configuration.neutral_entry_tolerance)
	    || configuration.neutral_entry_tolerance < 0.0F || configuration.neutral_entry_tolerance >= 1.0F
	    || configuration.maximum_cycle_gap_us < kBenchIntervalUs
	    || configuration.maximum_cycle_gap_us > kBenchSampleAgeUs) {
		return false;
	}

	for (std::size_t i = 0; i < kBenchCanardCount; ++i) {
		const auto &servo = configuration.servos[i];
		const bool increasing = servo.negative_pwm_us < servo.neutral_pwm_us && servo.neutral_pwm_us < servo.positive_pwm_us;
		const bool decreasing = servo.negative_pwm_us > servo.neutral_pwm_us && servo.neutral_pwm_us > servo.positive_pwm_us;

		if (!validPulse(servo.negative_pwm_us) || !validPulse(servo.neutral_pwm_us) || !validPulse(servo.positive_pwm_us)
		    || (!increasing && !decreasing) || !std::isfinite(servo.maximum_deflection_deg)
		    || servo.maximum_deflection_deg <= 0.0F || servo.maximum_deflection_deg > kBenchMaximumDeflectionDeg
		    || !std::isfinite(servo.rise_deg_per_s) || servo.rise_deg_per_s <= 0.0F
		    || !std::isfinite(servo.fall_deg_per_s) || servo.fall_deg_per_s <= 0.0F
		    || !std::isfinite(servo.rise_deg_per_s / servo.maximum_deflection_deg)
		    || !std::isfinite(servo.fall_deg_per_s / servo.maximum_deflection_deg)) {
			return false;
		}

		bool used = false;

		for (float coefficient : configuration.mix[i]) {
			if (!std::isfinite(coefficient)) {
				return false;
			}

			used = used || coefficient < 0.0F || coefficient > 0.0F;
		}

		if (!used) {
			return false;
		}
	}

	return true;
}
} // namespace

BenchStatus BenchControlOwner::configure(const BenchConfiguration &configuration)
{
	if (_state != BenchState::Disabled) {
		return BenchStatus::InvalidState;
	}

	if (!validConfiguration(configuration)) {
		return BenchStatus::InvalidConfiguration;
	}

	ManualControlConfiguration input{};
	input.required_axis_mask = kAttitudeManualAxesMask;
	input.source_policy = ManualSourcePolicy::MavlinkOnly;
	input.maximum_sample_age_us = kBenchSampleAgeUs;
	input.mavlink_instance = configuration.mavlink_instance;
	input.mavlink_system_id = configuration.source_system_id;
	input.mavlink_component_id = configuration.source_component_id;
	LimiterConfiguration limits{};
	limits.maximum_dt_s = static_cast<float>(configuration.maximum_cycle_gap_us / kMicrosecondsPerSecond);
	limits.channels[kEdfChannelIndex] = {0.0F, 0.0F, 0.0F, 0.0F};

	for (std::size_t i = 0; i < kBenchCanardCount; ++i) {
		for (std::size_t axis = 0; axis < kBenchAxisCount; ++axis) {
			input.command_map[i + 1U][axis] = configuration.mix[i][axis];
		}

		const auto &servo = configuration.servos[i];
		limits.channels[i + 1U] = { -1.0F, 1.0F, servo.rise_deg_per_s / servo.maximum_deflection_deg,
					    servo.fall_deg_per_s / servo.maximum_deflection_deg
					  };
	}

	if (_input.configure(input) != ManualControlStatus::Ok) {
		return BenchStatus::InvalidConfiguration;
	}

	_configuration = configuration;
	_limits = limits;
	_state = BenchState::Ready;
	_fault = BenchStatus::Ok;
	_last_report = makeReport(0U, {}, 1.0F, true);
	return BenchStatus::Ok;
}

BenchStatus BenchControlOwner::enable(RuntimeTimeUs now_us, const ManualControlSample &sample)
{
	if (_state != BenchState::Ready) {
		return BenchStatus::InvalidState;
	}

	if (now_us == 0U || now_us <= _last_cycle_us) {
		return BenchStatus::InvalidTime;
	}

	for (std::size_t axis = 0; axis < kBenchAxisCount; ++axis) {
		if (!std::isfinite(sample.axes[axis]) || std::fabs(sample.axes[axis]) > _configuration.neutral_entry_tolerance) {
			return BenchStatus::NotCentered;
		}
	}

	RuntimeInput input{};
	ManualControlReport report{};
	_input_status = _input.evaluate(now_us, sample, input, report);

	if (_input_status != ManualControlStatus::Ok) {
		return BenchStatus::InputRejected;
	}

	_last_cycle_us = now_us;
	_last_report = makeReport(now_us, {}, 1.0F, false);
	_state = BenchState::Active;
	return BenchStatus::Ok;
}

BenchStatus BenchControlOwner::latch(BenchStatus fault)
{
	_state = BenchState::Fault;
	_fault = fault;
	return fault;
}

BenchStatus BenchControlOwner::cycle(RuntimeTimeUs now_us, const ManualControlSample &sample, BenchReport &report)
{
	if (_state != BenchState::Active) {
		return BenchStatus::InvalidState;
	}

	if (now_us <= _last_cycle_us) {
		return latch(BenchStatus::InvalidTime);
	}

	const RuntimeTimeUs elapsed = now_us - _last_cycle_us;

	if (elapsed > _configuration.maximum_cycle_gap_us) {
		return latch(BenchStatus::CycleGap);
	}

	// Validate every observation, even before the next scheduled command cycle.
	RuntimeInput input{};
	ManualControlReport input_report{};
	_input_status = _input.evaluate(now_us, sample, input, input_report);

	if (_input_status != ManualControlStatus::Ok) {
		return latch(BenchStatus::InputRejected);
	}

	if (elapsed < kBenchIntervalUs) {
		return BenchStatus::NotDue;
	}

	float magnitude = 1.0F;

	for (float value : input.desired_command) {
		const float absolute = std::fabs(value);
		magnitude = absolute > magnitude ? absolute : magnitude;
	}

	const float scale = 1.0F / magnitude;

	for (float &value : input.desired_command) {
		value *= scale;
	}

	Command limited{};
	LimitReport limits{};
	const auto status = limitCommand(_limits, input.desired_command, _last_report.normalized,
					 static_cast<float>(elapsed / kMicrosecondsPerSecond), limited, limits);

	if (status != LimitStatus::Ok) {
		return latch(BenchStatus::LimiterFailure);
	}

	const BenchReport candidate = makeReport(now_us, limited, scale, false);
	_last_cycle_us = now_us;
	_last_report = candidate;
	report = candidate;
	return BenchStatus::Ok;
}

BenchReport BenchControlOwner::makeReport(RuntimeTimeUs now_us, const Command &command, float scale, bool neutral) const
{
	BenchReport result{};
	result.timestamp_us = now_us;
	result.normalized = command;
	result.mix_scale = scale;
	result.neutral_requested = neutral;

	for (std::size_t i = 0; i < kBenchCanardCount; ++i) {
		const auto &servo = _configuration.servos[i];
		const float value = command[i + 1U];
		result.deflection_deg[i] = value * servo.maximum_deflection_deg;
		const float endpoint = value < 0.0F ? servo.negative_pwm_us : servo.positive_pwm_us;
		result.pwm_us[i] = servo.neutral_pwm_us + std::fabs(value) * (endpoint - servo.neutral_pwm_us);
	}

	return result;
}

bool BenchControlOwner::neutralCandidate(BenchReport &report) const
{
	if (_state == BenchState::Disabled) {
		return false;
	}

	report = makeReport(_last_cycle_us, {}, 1.0F, true);
	return true;
}

void BenchControlOwner::disable()
{
	if (_state == BenchState::Active) {
		_state = BenchState::Ready;
	}
}

void BenchControlOwner::reset()
{
	_input.reset();
	_configuration = {};
	_limits = {};
	_last_report = {};
	_last_cycle_us = 0U;
	_input_status = ManualControlStatus::NotConfigured;
	_fault = BenchStatus::Ok;
	_state = BenchState::Disabled;
}

} // namespace research_actuator_command
