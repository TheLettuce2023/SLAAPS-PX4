#pragma once

#include "ControlPipeline.hpp"

#include <cstdint>

namespace research_actuator_command
{

using RuntimeTimeUs = uint64_t;

enum class RuntimeState : uint8_t {
	Unconfigured,
	Ready,
	Running
};

enum class RuntimeStatus : uint8_t {
	Ok,
	Primed,
	NotConfigured,
	AlreadyRunning,
	NotRunning,
	NotPrimed,
	AlreadyPrimed,
	InvalidSchedule,
	InvalidTime,
	PipelineFailure
};

struct RuntimeConfiguration {
	PipelineConfiguration pipeline;
	uint32_t schedule_interval_us;
};

struct RuntimeInput {
	ControlVector demand;
	Command desired_command;
};

struct RuntimeReport {
	RuntimeTimeUs timestamp_us;
	RuntimeTimeUs delta_us;
	PipelineReport pipeline;
};

struct RuntimeResult {
	RuntimeStatus status;
	// Meaningful only when status is PipelineFailure.
	PipelineStatus pipeline_status;
};

// Single owner for the H4 pipeline and its previous-command state. Construction
// is inert. A caller must provide complete configuration, explicitly start the
// owner, and prime it from the scheduling clock before any cycle can evaluate.
//
// The schedule interval is caller policy, not a default or timing guarantee.
// Actual elapsed time is derived from successful cycle timestamps. A failed
// cycle preserves the timestamp, pipeline state and caller report, so no stale
// or synthetic command becomes a new candidate. stop() revokes configuration;
// another successful configure() is required before restart.
class ControlRuntimeOwner
{
public:
	ControlRuntimeOwner() = default;
	ControlRuntimeOwner(const ControlRuntimeOwner &) = delete;
	ControlRuntimeOwner &operator=(const ControlRuntimeOwner &) = delete;
	ControlRuntimeOwner(ControlRuntimeOwner &&) = delete;
	ControlRuntimeOwner &operator=(ControlRuntimeOwner &&) = delete;

	RuntimeResult configure(const RuntimeConfiguration &configuration);
	RuntimeStatus start();
	RuntimeStatus prime(RuntimeTimeUs timestamp_us);
	RuntimeResult runCycle(RuntimeTimeUs timestamp_us,
			       const RuntimeInput &input,
			       RuntimeReport &report);
	void stop();

	RuntimeState state() const { return _state; }
	bool primed() const { return _primed; }
	uint32_t scheduleIntervalUs() const { return _schedule_interval_us; }
	RuntimeTimeUs lastCycleTimeUs() const { return _last_cycle_time_us; }
	const ControlPipeline &pipeline() const { return _pipeline; }

private:
	ControlPipeline _pipeline{};
	RuntimeState _state{RuntimeState::Unconfigured};
	uint32_t _schedule_interval_us{0U};
	RuntimeTimeUs _last_cycle_time_us{0U};
	bool _primed{false};
};

} // namespace research_actuator_command
