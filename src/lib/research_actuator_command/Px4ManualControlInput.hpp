#pragma once

#include "ManualControlInput.hpp"
#include "Px4ControlRuntime.hpp"

#include <uORB/Subscription.hpp>
#include <uORB/topics/manual_control_setpoint.h>

namespace research_actuator_command
{

ManualControlSample manualControlSampleFromMessage(const manual_control_setpoint_s &message);

// PX4-specific source for the portable H6 manual-input boundary. It consumes
// only PX4's already selected manual_control_setpoint, so RC/GCS selection stays
// under the existing manual_control module and COM_RC_IN_MODE policy. This type
// adds no parameter, mode, arming action, publication, actuator output or
// default instantiation.
class Px4ManualControlInputSource final : public RuntimeInputSource
{
public:
	Px4ManualControlInputSource() = default;
	~Px4ManualControlInputSource() override = default;

	Px4ManualControlInputSource(const Px4ManualControlInputSource &) = delete;
	Px4ManualControlInputSource &operator=(const Px4ManualControlInputSource &) = delete;
	Px4ManualControlInputSource(Px4ManualControlInputSource &&) = delete;
	Px4ManualControlInputSource &operator=(Px4ManualControlInputSource &&) = delete;

	ManualControlStatus configure(const ManualControlConfiguration &configuration);
	void reset();
	bool read(RuntimeTimeUs timestamp_us, RuntimeInput &input) override;

	ManualControlStatus lastStatus() const { return _last_status; }
	const ManualControlReport &lastReport() const { return _last_report; }
	const ManualControlInput &boundary() const { return _boundary; }

private:
	ManualControlInput _boundary{};
	uORB::Subscription _manual_control_setpoint_sub{ORB_ID(manual_control_setpoint)};
	ManualControlReport _last_report{};
	ManualControlStatus _last_status{ManualControlStatus::NotConfigured};
};

} // namespace research_actuator_command
