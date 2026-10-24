#include "ManualControlInput.hpp"

#include <cmath>
#include <cstring>

namespace research_actuator_command
{
namespace
{

constexpr float kMinimumManualAxis{-1.0F};
constexpr float kMaximumManualAxis{1.0F};

bool mavlinkAllowed(ManualSourcePolicy policy)
{
	return policy == ManualSourcePolicy::MavlinkOnly || policy == ManualSourcePolicy::RcOrMavlink;
}

bool rcAllowed(ManualSourcePolicy policy)
{
	return policy == ManualSourcePolicy::RcOnly || policy == ManualSourcePolicy::RcOrMavlink;
}

bool validPolicy(ManualSourcePolicy policy)
{
	return policy == ManualSourcePolicy::RcOnly || policy == ManualSourcePolicy::MavlinkOnly
	       || policy == ManualSourcePolicy::RcOrMavlink;
}

bool finiteConfiguration(const ManualControlConfiguration &configuration)
{
	for (std::size_t control_axis = 0; control_axis < kControlAxisCount; ++control_axis) {
		if (!std::isfinite(configuration.demand_offset[control_axis])) {
			return false;
		}

		for (std::size_t manual_axis = 0; manual_axis < kManualAxisCount; ++manual_axis) {
			if (!std::isfinite(configuration.demand_map[control_axis][manual_axis])) {
				return false;
			}
		}
	}

	for (std::size_t channel = 0; channel < kChannelCount; ++channel) {
		if (!std::isfinite(configuration.command_offset[channel])) {
			return false;
		}

		for (std::size_t manual_axis = 0; manual_axis < kManualAxisCount; ++manual_axis) {
			if (!std::isfinite(configuration.command_map[channel][manual_axis])) {
				return false;
			}
		}
	}

	return true;
}

bool everyManualAxisMapped(const ManualControlConfiguration &configuration)
{
	for (std::size_t manual_axis = 0; manual_axis < kManualAxisCount; ++manual_axis) {
		bool mapped = false;

		for (std::size_t control_axis = 0; control_axis < kControlAxisCount; ++control_axis) {
			const float value = configuration.demand_map[control_axis][manual_axis];
			mapped = mapped || value < 0.0F || value > 0.0F;
		}

		for (std::size_t channel = 0; channel < kChannelCount; ++channel) {
			const float value = configuration.command_map[channel][manual_axis];
			mapped = mapped || value < 0.0F || value > 0.0F;
		}

		const bool required = (configuration.required_axis_mask & (1U << manual_axis)) != 0U;

		if (mapped != required) {
			return false;
		}
	}

	return true;
}

bool sameRawSample(const ManualControlSample &left, const ManualControlSample &right, uint8_t axis_mask)
{
	for (std::size_t axis = 0; axis < kManualAxisCount; ++axis) {
		if ((axis_mask & (1U << axis)) != 0U
		    && std::memcmp(&left.axes[axis], &right.axes[axis], sizeof(float)) != 0) {
			return false;
		}
	}

	return left.source == right.source
	       && left.mavlink_instance == right.mavlink_instance
	       && left.source_system_id == right.source_system_id
	       && left.source_component_id == right.source_component_id
	       && left.buttons == right.buttons;
}

ManualControlStatus validateSource(const ManualControlConfiguration &configuration,
				   const ManualControlSample &sample)
{
	switch (sample.source) {
	case ManualControlSource::Rc:
		if (!rcAllowed(configuration.source_policy)) {
			return ManualControlStatus::SourceRejected;
		}

		if (sample.mavlink_instance != kNoMavlinkManualInstance
		    || sample.source_system_id != 0U || sample.source_component_id != 0U) {
			return ManualControlStatus::IdentityRejected;
		}

		return ManualControlStatus::Ok;

	case ManualControlSource::Mavlink:
		if (!mavlinkAllowed(configuration.source_policy)) {
			return ManualControlStatus::SourceRejected;
		}

		if (sample.mavlink_instance != configuration.mavlink_instance
		    || sample.source_system_id != configuration.mavlink_system_id
		    || sample.source_component_id != configuration.mavlink_component_id) {
			return ManualControlStatus::IdentityRejected;
		}

		return ManualControlStatus::Ok;

	case ManualControlSource::Unknown:
	default:
		return ManualControlStatus::InvalidSource;
	}
}

ManualControlStatus mapInput(const ManualControlConfiguration &configuration,
			     const ManualControlSample &sample,
			     RuntimeInput &input)
{
	RuntimeInput candidate{};

	for (std::size_t control_axis = 0; control_axis < kControlAxisCount; ++control_axis) {
		candidate.demand[control_axis] = configuration.demand_offset[control_axis];

		for (std::size_t manual_axis = 0; manual_axis < kManualAxisCount; ++manual_axis) {
			if ((configuration.required_axis_mask & (1U << manual_axis)) == 0U) {
				continue;
			}

			candidate.demand[control_axis] +=
				configuration.demand_map[control_axis][manual_axis] * sample.axes[manual_axis];
		}

		if (!std::isfinite(candidate.demand[control_axis])) {
			return ManualControlStatus::ArithmeticFailure;
		}
	}

	for (std::size_t channel = 0; channel < kChannelCount; ++channel) {
		candidate.desired_command[channel] = configuration.command_offset[channel];

		for (std::size_t manual_axis = 0; manual_axis < kManualAxisCount; ++manual_axis) {
			if ((configuration.required_axis_mask & (1U << manual_axis)) == 0U) {
				continue;
			}

			candidate.desired_command[channel] +=
				configuration.command_map[channel][manual_axis] * sample.axes[manual_axis];
		}

		if (!std::isfinite(candidate.desired_command[channel])) {
			return ManualControlStatus::ArithmeticFailure;
		}
	}

	input = candidate;
	return ManualControlStatus::Ok;
}

} // namespace

ManualControlStatus ManualControlInput::validateConfiguration(const ManualControlConfiguration &configuration)
{
	if (configuration.required_axis_mask == 0U
	    || (configuration.required_axis_mask & ~kAllManualAxesMask) != 0U
	    || !validPolicy(configuration.source_policy) || configuration.maximum_sample_age_us == 0U
	    || !finiteConfiguration(configuration) || !everyManualAxisMapped(configuration)) {
		return ManualControlStatus::InvalidConfiguration;
	}

	if (mavlinkAllowed(configuration.source_policy)) {
		if (configuration.mavlink_instance > kMaximumMavlinkManualInstance
		    || configuration.mavlink_system_id == 0U || configuration.mavlink_component_id == 0U) {
			return ManualControlStatus::InvalidConfiguration;
		}

	} else if (configuration.mavlink_instance != kNoMavlinkManualInstance
		   || configuration.mavlink_system_id != 0U || configuration.mavlink_component_id != 0U) {
		return ManualControlStatus::InvalidConfiguration;
	}

	return ManualControlStatus::Ok;
}

ManualControlStatus ManualControlInput::configure(const ManualControlConfiguration &configuration)
{
	const auto status = validateConfiguration(configuration);

	if (status != ManualControlStatus::Ok) { return status; }

	_configuration = configuration;
	_last_sample = {};
	_have_last_sample = false;
	_configured = true;
	return ManualControlStatus::Ok;
}

ManualControlStatus ManualControlInput::evaluate(RuntimeTimeUs now_us,
		const ManualControlSample &sample,
		RuntimeInput &input,
		ManualControlReport &report)
{
	if (!_configured) {
		return ManualControlStatus::NotConfigured;
	}

	if (!sample.valid) {
		return ManualControlStatus::InvalidSample;
	}

	if (sample.timestamp_sample_us == 0U || sample.timestamp_us < sample.timestamp_sample_us
	    || now_us < sample.timestamp_us) {
		return ManualControlStatus::InvalidTime;
	}

	const RuntimeTimeUs age_us = now_us - sample.timestamp_sample_us;

	if (age_us > _configuration.maximum_sample_age_us) {
		return ManualControlStatus::StaleSample;
	}

	const ManualControlStatus source_status = validateSource(_configuration, sample);

	if (source_status != ManualControlStatus::Ok) {
		return source_status;
	}

	for (std::size_t axis = 0; axis < kManualAxisCount; ++axis) {
		if ((_configuration.required_axis_mask & (1U << axis)) == 0U) {
			continue;
		}

		if (!std::isfinite(sample.axes[axis])) {
			return ManualControlStatus::NonFinite;
		}

		if (sample.axes[axis] < kMinimumManualAxis || sample.axes[axis] > kMaximumManualAxis) {
			return ManualControlStatus::OutOfRange;
		}
	}

	bool new_sample = true;

	if (_have_last_sample) {
		if (sample.timestamp_sample_us < _last_sample.timestamp_sample_us
		    || (sample.timestamp_sample_us == _last_sample.timestamp_sample_us
			&& sample.timestamp_us < _last_sample.timestamp_us)) {
			return ManualControlStatus::RegressedSample;
		}

		if (sample.timestamp_sample_us == _last_sample.timestamp_sample_us) {
			if (!sameRawSample(sample, _last_sample, _configuration.required_axis_mask)) {
				return ManualControlStatus::ConflictingSample;
			}

			new_sample = false;
		}
	}

	RuntimeInput candidate_input{};
	const ManualControlStatus mapping_status = mapInput(_configuration, sample, candidate_input);

	if (mapping_status != ManualControlStatus::Ok) {
		return mapping_status;
	}

	ManualControlReport candidate_report{};
	candidate_report.timestamp_us = sample.timestamp_us;
	candidate_report.timestamp_sample_us = sample.timestamp_sample_us;
	candidate_report.sample_age_us = age_us;
	candidate_report.source = sample.source;
	candidate_report.mavlink_instance = sample.mavlink_instance;
	candidate_report.source_system_id = sample.source_system_id;
	candidate_report.source_component_id = sample.source_component_id;
	candidate_report.buttons = sample.buttons;
	candidate_report.new_sample = new_sample;
	candidate_report.sticks_moving = sample.sticks_moving;

	_last_sample = sample;
	_have_last_sample = true;
	input = candidate_input;
	report = candidate_report;
	return ManualControlStatus::Ok;
}

void ManualControlInput::reset()
{
	_configuration = {};
	_last_sample = {};
	_configured = false;
	_have_last_sample = false;
}

} // namespace research_actuator_command
