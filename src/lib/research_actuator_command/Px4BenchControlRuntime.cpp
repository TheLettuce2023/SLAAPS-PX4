#include "Px4BenchControlRuntime.hpp"

#include <drivers/drv_hrt.h>

namespace research_actuator_command
{

Px4BenchControlRuntime::Px4BenchControlRuntime(const char *name, const px4::wq_config_t &work_queue) :
	ScheduledWorkItem(name, work_queue)
{
}

Px4BenchControlRuntime::~Px4BenchControlRuntime()
{
	disable();
}

BenchStatus Px4BenchControlRuntime::configure(const BenchConfiguration &configuration)
{
	return _owner.configure(configuration);
}

ManualControlSample Px4BenchControlRuntime::readSample()
{
	manual_control_setpoint_s message{};

	if (!_manual_sub.copy(&message)) {
		return {};
	}

	return manualControlSampleFromMessage(message);
}

BenchStatus Px4BenchControlRuntime::enable()
{
	const auto sample = readSample();
	const auto status = _owner.enable(hrt_absolute_time(), sample);

	if (status == BenchStatus::Ok) {
		ScheduleOnInterval(kBenchIntervalUs);
	}

	return status;
}

void Px4BenchControlRuntime::disable()
{
	ScheduleClear();
	_owner.disable();
}

void Px4BenchControlRuntime::reset()
{
	ScheduleClear();
	_owner.reset();
}

void Px4BenchControlRuntime::Run()
{
	const auto sample = readSample();
	BenchReport report{};
	(void)_owner.cycle(hrt_absolute_time(), sample, report);

	if (_owner.state() != BenchState::Active) {
		ScheduleClear();
	}

	// Candidate intentionally has no publication/output destination.
}

} // namespace research_actuator_command
