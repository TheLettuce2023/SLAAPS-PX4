#include "ControlPipeline.hpp"

namespace research_actuator_command
{
namespace
{

constexpr uint8_t kAllAxisMask{static_cast<uint8_t>((1U << kControlAxisCount) - 1U)};

EffectivenessConfiguration effectivenessConfiguration(const PipelineConfiguration &configuration)
{
	EffectivenessConfiguration result{};
	result.effectiveness = configuration.effectiveness;
	result.trim = configuration.trim;

	for (std::size_t actuator = 0; actuator < kChannelCount; ++actuator) {
		result.minimum[actuator] = configuration.limiter.channels[actuator].minimum;
		result.maximum[actuator] = configuration.limiter.channels[actuator].maximum;
	}

	return result;
}

PipelineStatus mapAuthorityStatus(AuthorityStatus status)
{
	switch (status) {
	case AuthorityStatus::Ok:
		return PipelineStatus::Ok;

	case AuthorityStatus::InvalidMatrix:
	case AuthorityStatus::InvalidPolicy:
		return PipelineStatus::InvalidConfiguration;

	case AuthorityStatus::ArithmeticFailure:
		return PipelineStatus::ArithmeticFailure;
	}

	return PipelineStatus::ArithmeticFailure;
}

PipelineStatus mapLimitStatus(LimitStatus status)
{
	switch (status) {
	case LimitStatus::Ok:
		return PipelineStatus::Ok;

	case LimitStatus::InvalidConfiguration:
		return PipelineStatus::InvalidConfiguration;

	case LimitStatus::InvalidInput:
		return PipelineStatus::InvalidDesiredCommand;

	case LimitStatus::InvalidTimestep:
		return PipelineStatus::InvalidTimestep;

	case LimitStatus::InvalidPreviousCommand:
		return PipelineStatus::InvalidState;

	case LimitStatus::ArithmeticFailure:
		return PipelineStatus::ArithmeticFailure;
	}

	return PipelineStatus::ArithmeticFailure;
}

PipelineStatus mapEffectivenessStatus(EffectivenessStatus status)
{
	switch (status) {
	case EffectivenessStatus::Ok:
		return PipelineStatus::Ok;

	case EffectivenessStatus::InvalidConfiguration:
		return PipelineStatus::InvalidConfiguration;

	case EffectivenessStatus::InvalidDemand:
		return PipelineStatus::InvalidDemand;

	case EffectivenessStatus::InvalidCommand:
		return PipelineStatus::InvalidState;

	case EffectivenessStatus::ArithmeticFailure:
		return PipelineStatus::ArithmeticFailure;
	}

	return PipelineStatus::ArithmeticFailure;
}

} // namespace

PipelineStatus ControlPipeline::configure(const PipelineConfiguration &configuration)
{
	if (configuration.required_axis_mask == 0U || configuration.required_axis_mask > kAllAxisMask) {
		return PipelineStatus::InvalidConfiguration;
	}

	const EffectivenessConfiguration effectiveness = effectivenessConfiguration(configuration);
	EffectivenessReport effectiveness_report{};
	const PipelineStatus effectiveness_status = mapEffectivenessStatus(
				evaluateEffectiveness(effectiveness,
						ControlVector{},
						configuration.trim,
						effectiveness_report));

	if (effectiveness_status != PipelineStatus::Ok) {
		return effectiveness_status;
	}

	Command validated_trim{};
	LimitReport limit_report{};
	const PipelineStatus limit_status = mapLimitStatus(
			limitCommand(configuration.limiter,
				     configuration.trim,
				     configuration.trim,
				     configuration.limiter.maximum_dt_s,
				     validated_trim,
				     limit_report));

	if (limit_status != PipelineStatus::Ok) {
		return limit_status;
	}

	AuthorityReport candidate_authority{};
	const PipelineStatus authority_status = mapAuthorityStatus(
			analyzeAuthority(configuration.effectiveness,
					 configuration.authority,
					 candidate_authority));

	if (authority_status != PipelineStatus::Ok) {
		return authority_status;
	}

	if ((candidate_authority.selected_axis_mask & configuration.required_axis_mask)
	    != configuration.required_axis_mask) {
		return PipelineStatus::InsufficientAuthority;
	}

	_configuration = configuration;
	_previous_command = configuration.trim;
	_authority_report = candidate_authority;
	_configured = true;
	return PipelineStatus::Ok;
}

PipelineStatus ControlPipeline::evaluate(const ControlVector &demand,
		const Command &desired_command,
		float dt_s,
		PipelineReport &report)
{
	if (!_configured) {
		return PipelineStatus::NotConfigured;
	}

	PipelineReport candidate{};
	const PipelineStatus limit_status = mapLimitStatus(
			limitCommand(_configuration.limiter,
				     desired_command,
				     _previous_command,
				     dt_s,
				     candidate.candidate_command,
				     candidate.limit));

	if (limit_status != PipelineStatus::Ok) {
		return limit_status;
	}

	const EffectivenessConfiguration effectiveness = effectivenessConfiguration(_configuration);
	const PipelineStatus effectiveness_status = mapEffectivenessStatus(
				evaluateEffectiveness(effectiveness,
						demand,
						candidate.candidate_command,
						candidate.effectiveness));

	if (effectiveness_status != PipelineStatus::Ok) {
		return effectiveness_status;
	}

	candidate.authority = _authority_report;
	_previous_command = candidate.candidate_command;
	report = candidate;
	return PipelineStatus::Ok;
}

} // namespace research_actuator_command
