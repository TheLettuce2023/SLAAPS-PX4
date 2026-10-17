#pragma once

#include "ControlRuntimeOwner.hpp"

#include <px4_platform_common/px4_work_queue/ScheduledWorkItem.hpp>

namespace research_actuator_command
{

class RuntimeInputSource
{
public:
	virtual ~RuntimeInputSource() = default;
	virtual bool read(RuntimeTimeUs timestamp_us, RuntimeInput &input) = 0;
};

// PX4 work-queue boundary for the single runtime owner. This is an inert
// library type: there is no ModuleBase entry point, Kconfig/board selection,
// startup registration, parameter, uORB publication, or actuator output.
// Construction and configuration do not schedule work; start() is explicit.
// The future enclosing owner must serialize configure/start/stop against Run();
// this seam intentionally adds no cross-thread command or synchronization path.
class Px4ControlRuntime final : public px4::ScheduledWorkItem
{
public:
	Px4ControlRuntime(const char *name,
			  const px4::wq_config_t &work_queue,
			  RuntimeInputSource &input_source);
	~Px4ControlRuntime() override;

	Px4ControlRuntime(const Px4ControlRuntime &) = delete;
	Px4ControlRuntime &operator=(const Px4ControlRuntime &) = delete;
	Px4ControlRuntime(Px4ControlRuntime &&) = delete;
	Px4ControlRuntime &operator=(Px4ControlRuntime &&) = delete;

	RuntimeResult configure(const RuntimeConfiguration &configuration);
	RuntimeStatus start();
	void stop();

	RuntimeState state() const { return _owner.state(); }

private:
	void Run() override;

	RuntimeInputSource &_input_source;
	ControlRuntimeOwner _owner{};
};

} // namespace research_actuator_command
