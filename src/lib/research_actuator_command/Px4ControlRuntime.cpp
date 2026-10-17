#include "Px4ControlRuntime.hpp"

#include <drivers/drv_hrt.h>

namespace research_actuator_command
{

Px4ControlRuntime::Px4ControlRuntime(const char *name,
				     const px4::wq_config_t &work_queue,
				     RuntimeInputSource &input_source) :
	ScheduledWorkItem(name, work_queue),
	_input_source(input_source)
{
}

Px4ControlRuntime::~Px4ControlRuntime()
{
	stop();
}

RuntimeResult Px4ControlRuntime::configure(const RuntimeConfiguration &configuration)
{
	return _owner.configure(configuration);
}

RuntimeStatus Px4ControlRuntime::start()
{
	const RuntimeStatus status = _owner.start();

	if (status == RuntimeStatus::Ok) {
		ScheduleOnInterval(_owner.scheduleIntervalUs());
	}

	return status;
}

void Px4ControlRuntime::stop()
{
	ScheduleClear();
	_owner.stop();
}

void Px4ControlRuntime::Run()
{
	const RuntimeTimeUs timestamp_us = hrt_absolute_time();

	if (!_owner.primed()) {
		(void)_owner.prime(timestamp_us);
		return;
	}

	RuntimeInput input{};

	if (!_input_source.read(timestamp_us, input)) {
		return;
	}

	// The report is intentionally local and discarded. This boundary has no
	// publication or output path; it only exercises ownership and scheduling.
	RuntimeReport report{};
	(void)_owner.runCycle(timestamp_us, input, report);
}

} // namespace research_actuator_command
