#pragma once

#include "AuthorityAnalysis.hpp"

#include <cstdint>

namespace research_actuator_command
{

enum class PipelineStatus : uint8_t {
	Ok,
	NotConfigured,
	InvalidConfiguration,
	InsufficientAuthority,
	InvalidDemand,
	InvalidDesiredCommand,
	InvalidTimestep,
	InvalidState,
	ArithmeticFailure
};

struct PipelineConfiguration {
	LimiterConfiguration limiter;
	EffectivenessMatrix effectiveness;
	Command trim;
	AuthorityPolicy authority;
	uint8_t required_axis_mask;
};

struct PipelineReport {
	Command candidate_command;
	LimitReport limit;
	EffectivenessReport effectiveness;
	AuthorityReport authority;
};

// Stateful, allocation-free composition of H1 command limiting, H2 normalized
// effectiveness evaluation and H3 structural authority analysis. Configuration
// supplies every matrix value, trim, bound, rate, priority, threshold and
// required axis. No operational defaults are provided.
//
// A successful configure validates all three contracts and initializes the
// sole previous-command state to the caller's trim. A successful evaluate
// advances that state once and returns the limited normalized candidate plus
// diagnostics. Any failure preserves both state and the caller's report.
// This class has no clock, controller, allocator, I/O, topic or actuator output;
// candidate_command is diagnostic library output for a future reviewed owner.
class ControlPipeline
{
public:
	ControlPipeline() = default;
	ControlPipeline(const ControlPipeline &) = delete;
	ControlPipeline &operator=(const ControlPipeline &) = delete;
	ControlPipeline(ControlPipeline &&) = delete;
	ControlPipeline &operator=(ControlPipeline &&) = delete;

	PipelineStatus configure(const PipelineConfiguration &configuration);
	PipelineStatus evaluate(const ControlVector &demand,
				const Command &desired_command,
				float dt_s,
				PipelineReport &report);

	bool configured() const { return _configured; }
	const Command &previousCommand() const { return _previous_command; }
	const AuthorityReport &authorityReport() const { return _authority_report; }

private:
	PipelineConfiguration _configuration{};
	Command _previous_command{};
	AuthorityReport _authority_report{};
	bool _configured{false};
};

} // namespace research_actuator_command
