#include "RequestGuard.hpp"

// Shared predicates: compiled only into an explicitly selected diagnostic profile.
#include "DiagnosticTypes.hpp"
#include <limits>

namespace research_acceleration
{

DiagnosticGuardResult RequestGuard::diagnosticCommon(DiagnosticGuardOperation operation, DiagnosticCode success,
		const Decision &result) const
{
	const bool succeeded = result.code == DecisionCode::Accepted || result.code == DecisionCode::ValidatedDisarmed
			       || result.code == DecisionCode::NoEligibleRequest;
	const DiagnosticCode code = result.code == DecisionCode::NotConfigured ? DiagnosticCode::NotConfigured
				    : (succeeded ? success : DiagnosticCode::Rejected);
	return {operation, code, result.code, result};
}

DiagnosticGuardResult RequestGuard::diagnosticLocal(DiagnosticGuardOperation operation, DiagnosticLocalReason reason)
{
	if (rejection_count_ != std::numeric_limits<uint64_t>::max()) {
		++rejection_count_;
	}

	return {operation, DiagnosticCode::Rejected, reason, std::nullopt};
}

DiagnosticGuardResult RequestGuard::diagnosticContext(const EvaluationContext &context,
		DiagnosticSelectionState selection, DiagnosticGuardOperation operation)
{
	// Never substitute an Armed/Selected observation or retain a control demand.
	has_active_request_ = false;
	const Decision prerequisites = contextPrerequisiteDecision(context);

	if (prerequisites.code != DecisionCode::Accepted) {
		return diagnosticCommon(operation, DiagnosticCode::Observed, prerequisites);
	}

	if (context.arm_state != ArmState::Disarmed) {
		return diagnosticLocal(operation, DiagnosticLocalReason::NotDisarmed);
	}

	if (context.mode_state != ResearchModeState::NotSelected) {
		return diagnosticLocal(operation, DiagnosticLocalReason::FlightModeConflict);
	}

	if (selection != DiagnosticSelectionState::Disabled && selection != DiagnosticSelectionState::SelectedForValidation
	    && selection != DiagnosticSelectionState::Faulted) {
		return diagnosticLocal(operation, DiagnosticLocalReason::SelectorInvalid);
	}

	if (selection == DiagnosticSelectionState::Faulted) {
		return diagnosticLocal(operation, DiagnosticLocalReason::SelectorFaulted);
	}

	if (selection != DiagnosticSelectionState::SelectedForValidation) {
		return diagnosticLocal(operation, DiagnosticLocalReason::SelectionRequired);
	}

	// Internal predicate success is not a request evaluation or a common Decision.
	return {operation, DiagnosticCode::Observed, DiagnosticLocalReason::None, std::nullopt};
}

DiagnosticGuardResult RequestGuard::evaluateDiagnostic(const CanonicalRequest &request,
		const EvaluationContext &context, DiagnosticSelectionState selection)
{
	constexpr auto operation = DiagnosticGuardOperation::Request;

	if (!diagnostic_domain_) {
		return {operation, DiagnosticCode::WrongDomain, DiagnosticLocalReason::None, std::nullopt};
	}

	if (!configured_) {
		return diagnosticCommon(operation, DiagnosticCode::RequestValidatedDisarmed, decision(DecisionCode::NotConfigured));
	}

	if (!observeValidatorTime(context.validator_receive_time_us)) {
		return diagnosticCommon(operation, DiagnosticCode::RequestValidatedDisarmed, decision(DecisionCode::ValidatorTimeRegressed));
	}

	if (latched_) {
		return diagnosticCommon(operation, DiagnosticCode::RequestValidatedDisarmed, reject(DecisionCode::Latched));
	}

	if (!trusted_session_established_ || session_establishment_required_) {
		return diagnosticCommon(operation, DiagnosticCode::RequestValidatedDisarmed,
					reject(session_establishment_required_ ? DecisionCode::SessionEstablishmentRequired : DecisionCode::NoSession));
	}

	const DiagnosticGuardResult prerequisites = diagnosticContext(context, selection, operation);

	if (prerequisites.code != DiagnosticCode::Observed) {
		return prerequisites;
	}

	// The shared payload body advances replay floors but cannot assign a cache.
	// Only the ordinary Flight evaluate wrapper contains that assignment.
	return diagnosticCommon(operation, DiagnosticCode::RequestValidatedDisarmed, evaluatePayload(request, context));
}

DiagnosticGuardResult RequestGuard::pollDiagnostic(const EvaluationContext &context, DiagnosticSelectionState selection)
{
	constexpr auto operation = DiagnosticGuardOperation::Poll;

	if (!diagnostic_domain_) {
		return {operation, DiagnosticCode::WrongDomain, DiagnosticLocalReason::None, std::nullopt};
	}

	if (!configured_) {
		return diagnosticCommon(operation, DiagnosticCode::NoEligibleRequest, decision(DecisionCode::NotConfigured));
	}

	if (!observeValidatorTime(context.validator_receive_time_us)) {
		return diagnosticCommon(operation, DiagnosticCode::NoEligibleRequest, decision(DecisionCode::ValidatorTimeRegressed));
	}

	if (latched_) {
		return diagnosticCommon(operation, DiagnosticCode::NoEligibleRequest, reject(DecisionCode::Latched));
	}

	const DiagnosticGuardResult prerequisites = diagnosticContext(context, selection, operation);

	if (prerequisites.code != DiagnosticCode::Observed) {
		return prerequisites;
	}

	return diagnosticCommon(operation, DiagnosticCode::NoEligibleRequest, decision(DecisionCode::NoEligibleRequest));
}

DiagnosticGuardResult RequestGuard::establishDiagnosticSession(const TrustedSession &session, bool authorized,
		const EvaluationContext &context, DiagnosticSelectionState selection)
{
	constexpr auto operation = DiagnosticGuardOperation::Establish;

	if (!diagnostic_domain_) {
		return {operation, DiagnosticCode::WrongDomain, DiagnosticLocalReason::None, std::nullopt};
	}

	if (!configured_) {
		return diagnosticCommon(operation, DiagnosticCode::SessionEstablished, decision(DecisionCode::NotConfigured));
	}

	// No nested poll: the future serialized adapter observes its prefix once.
	// Rechecking an equal timestamp is legal and does not reset the clock floor.
	if (!observeValidatorTime(context.validator_receive_time_us)) {
		return diagnosticCommon(operation, DiagnosticCode::SessionEstablished, decision(DecisionCode::ValidatorTimeRegressed));
	}

	if (latched_) {
		return diagnosticCommon(operation, DiagnosticCode::SessionEstablished, reject(DecisionCode::Latched));
	}

	const Decision administration = establishSessionChecks(session, authorized, context.arm_state);

	if (administration.code != DecisionCode::Accepted) {
		return diagnosticCommon(operation, DiagnosticCode::SessionEstablished, administration);
	}

	const DiagnosticGuardResult prerequisites = diagnosticContext(context, selection, operation);

	if (prerequisites.code != DiagnosticCode::Observed) {
		return prerequisites;
	}

	return diagnosticCommon(operation, DiagnosticCode::SessionEstablished, commitSession(session));
}

DiagnosticGuardResult RequestGuard::clearDiagnosticLatch(bool authorized, ArmState arm_state)
{
	constexpr auto operation = DiagnosticGuardOperation::Clear;

	if (!diagnostic_domain_) {
		return {operation, DiagnosticCode::WrongDomain, DiagnosticLocalReason::None, std::nullopt};
	}

	return diagnosticCommon(operation, DiagnosticCode::LatchCleared, clearLatchImpl(authorized, arm_state));
}

void RequestGuard::invalidateDiagnosticSession()
{
	if (!diagnostic_domain_ || !configured_) {
		return;
	}

	has_active_request_ = false;
	session_establishment_required_ = true;
	// Retain the successful session identity and every replay/time floor.
}

} // namespace research_acceleration
