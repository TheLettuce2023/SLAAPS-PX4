#include "Px4ManualControlInput.hpp"

namespace research_actuator_command
{
ManualControlSample manualControlSampleFromMessage(const manual_control_setpoint_s &message)
{
	ManualControlSample sample{};
	sample.timestamp_us = message.timestamp;
	sample.timestamp_sample_us = message.timestamp_sample;
	sample.source_system_id = message.source_system_id;
	sample.source_component_id = message.source_component_id;
	sample.axes[static_cast<std::size_t>(ManualAxis::Roll)] = message.roll;
	sample.axes[static_cast<std::size_t>(ManualAxis::Pitch)] = message.pitch;
	sample.axes[static_cast<std::size_t>(ManualAxis::Yaw)] = message.yaw;
	sample.axes[static_cast<std::size_t>(ManualAxis::Throttle)] = message.throttle;
	sample.buttons = message.buttons;
	sample.valid = message.valid;
	sample.sticks_moving = message.sticks_moving;

	if (message.data_source == manual_control_setpoint_s::SOURCE_RC) {
		sample.source = ManualControlSource::Rc;

	} else if (message.data_source >= manual_control_setpoint_s::SOURCE_MAVLINK_0
		   && message.data_source <= manual_control_setpoint_s::SOURCE_MAVLINK_5) {
		sample.source = ManualControlSource::Mavlink;
		sample.mavlink_instance = message.data_source - manual_control_setpoint_s::SOURCE_MAVLINK_0;
	}

	return sample;
}

ManualControlStatus Px4ManualControlInputSource::configure(const ManualControlConfiguration &configuration)
{
	_last_status = _boundary.configure(configuration);
	return _last_status;
}

void Px4ManualControlInputSource::reset()
{
	_boundary.reset();
	_last_report = {};
	_last_status = ManualControlStatus::NotConfigured;
}

bool Px4ManualControlInputSource::read(RuntimeTimeUs timestamp_us, RuntimeInput &input)
{
	manual_control_setpoint_s message{};

	if (!_manual_control_setpoint_sub.copy(&message)) {
		_last_status = ManualControlStatus::NoSample;
		return false;
	}

	RuntimeInput candidate_input{};
	ManualControlReport candidate_report{};
	_last_status = _boundary.evaluate(timestamp_us, manualControlSampleFromMessage(message), candidate_input, candidate_report);

	if (_last_status != ManualControlStatus::Ok) {
		return false;
	}

	input = candidate_input;
	_last_report = candidate_report;
	return true;
}

} // namespace research_actuator_command
