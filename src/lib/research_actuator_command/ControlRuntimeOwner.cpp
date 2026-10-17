#include "ControlRuntimeOwner.hpp"

#include <cmath>

namespace research_actuator_command
{
namespace
{

constexpr double kSecondsPerMicrosecond{1.0e-6};

RuntimeResult runtimeResult(RuntimeStatus status,
			    PipelineStatus pipeline_status = PipelineStatus::Ok)
{
	return {status, pipeline_status};
}

} // namespace

RuntimeResult ControlRuntimeOwner::configure(const RuntimeConfiguration &configuration)
{
	if (_state == RuntimeState::Running) {
		return runtimeResult(RuntimeStatus::AlreadyRunning);
	}

	if (configuration.schedule_interval_us == 0U) {
		return runtimeResult(RuntimeStatus::InvalidSchedule);
	}

	const float maximum_dt_s = configuration.pipeline.limiter.maximum_dt_s;

	if (!std::isfinite(maximum_dt_s) || maximum_dt_s <= 0.0F) {
		return runtimeResult(RuntimeStatus::PipelineFailure, PipelineStatus::InvalidConfiguration);
	}

	const double schedule_interval_s =
		static_cast<double>(configuration.schedule_interval_us) * kSecondsPerMicrosecond;

	if (schedule_interval_s > static_cast<double>(maximum_dt_s)) {
		return runtimeResult(RuntimeStatus::InvalidSchedule);
	}

	const PipelineStatus pipeline_status = _pipeline.configure(configuration.pipeline);

	if (pipeline_status != PipelineStatus::Ok) {
		return runtimeResult(RuntimeStatus::PipelineFailure, pipeline_status);
	}

	_schedule_interval_us = configuration.schedule_interval_us;
	_last_cycle_time_us = 0U;
	_primed = false;
	_state = RuntimeState::Ready;
	return runtimeResult(RuntimeStatus::Ok);
}

RuntimeStatus ControlRuntimeOwner::start()
{
	if (_state == RuntimeState::Unconfigured) {
		return RuntimeStatus::NotConfigured;
	}

	if (_state == RuntimeState::Running) {
		return RuntimeStatus::AlreadyRunning;
	}

	_last_cycle_time_us = 0U;
	_primed = false;
	_state = RuntimeState::Running;
	return RuntimeStatus::Ok;
}

RuntimeStatus ControlRuntimeOwner::prime(RuntimeTimeUs timestamp_us)
{
	if (_state == RuntimeState::Unconfigured) {
		return RuntimeStatus::NotConfigured;
	}

	if (_state != RuntimeState::Running) {
		return RuntimeStatus::NotRunning;
	}

	if (_primed) {
		return RuntimeStatus::AlreadyPrimed;
	}

	_last_cycle_time_us = timestamp_us;
	_primed = true;
	return RuntimeStatus::Primed;
}

RuntimeResult ControlRuntimeOwner::runCycle(RuntimeTimeUs timestamp_us,
		const RuntimeInput &input,
		RuntimeReport &report)
{
	if (_state == RuntimeState::Unconfigured) {
		return runtimeResult(RuntimeStatus::NotConfigured);
	}

	if (_state != RuntimeState::Running) {
		return runtimeResult(RuntimeStatus::NotRunning);
	}

	if (!_primed) {
		return runtimeResult(RuntimeStatus::NotPrimed);
	}

	if (timestamp_us <= _last_cycle_time_us) {
		return runtimeResult(RuntimeStatus::InvalidTime);
	}

	const RuntimeTimeUs delta_us = timestamp_us - _last_cycle_time_us;
	const float dt_s = static_cast<float>(static_cast<double>(delta_us) * kSecondsPerMicrosecond);
	RuntimeReport candidate{};
	candidate.timestamp_us = timestamp_us;
	candidate.delta_us = delta_us;
	const PipelineStatus pipeline_status = _pipeline.evaluate(input.demand,
					       input.desired_command,
					       dt_s,
					       candidate.pipeline);

	if (pipeline_status != PipelineStatus::Ok) {
		return runtimeResult(RuntimeStatus::PipelineFailure, pipeline_status);
	}

	_last_cycle_time_us = timestamp_us;
	report = candidate;
	return runtimeResult(RuntimeStatus::Ok);
}

void ControlRuntimeOwner::stop()
{
	_state = RuntimeState::Unconfigured;
	_schedule_interval_us = 0U;
	_last_cycle_time_us = 0U;
	_primed = false;
}

} // namespace research_actuator_command
