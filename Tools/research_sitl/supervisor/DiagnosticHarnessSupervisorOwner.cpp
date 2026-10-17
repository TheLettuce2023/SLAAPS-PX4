#include "HarnessSupervisorOwner.hpp"
#include "DiagnosticTypes.hpp"

#include <limits>

namespace research_sitl
{
using namespace research_acceleration;

HarnessSupervisorOwner::HarnessSupervisorOwner(const OwnerHarnessConfig &config, DiagnosticOfflineTag,
		uint64_t owner_lifetime_id) noexcept :
	config_(config), adapter_(config.adapter_contract, config.adapter_identity, DiagnosticOfflineTag{},
{owner_lifetime_id, config.fixture_run_id}), diagnostic_domain_(true)
{
	if (config_.fixture_run_id == 0 || config_.maximum_receipt_age_us == 0 || !adapter_.configured()) {
		administration_disabled_ = true;
	}
}

DiagnosticDomain HarnessSupervisorOwner::domain() const noexcept
{
	return diagnostic_domain_ ? DiagnosticDomain::SitlDisarmedDiagnostic : DiagnosticDomain::Flight;
}

void HarnessSupervisorOwner::retainDiagnosticResult(const DiagnosticResult &result) noexcept
{
	diagnostic_audit_ = result;
	has_diagnostic_audit_ = true;
	retainView(result.snapshot.adapter, sample_);
}

void HarnessSupervisorOwner::closeDiagnosticOnFault() noexcept
{
	// Never use the receipt whose provenance failed. Closure needs no healthy
	// estimator/map and permanently blocks further work by this owner.
	administration_disabled_ = true;
	has_receipt_ = false;
	const auto receipt = has_view_ ? std::optional<TrustedStateSample> {sample_} : std::nullopt;
	retainDiagnosticResult(adapter_.closeDiagnosticLifecycle(receipt, DiagnosticCloseReason::OperatorLost));
}

bool HarnessSupervisorOwner::attachDiagnosticOperator(uint64_t operator_instance) noexcept
{
	if (!diagnostic_domain_) { return false; }

	if (!enter()) { closeDiagnosticOnFault(); return false; }

	const bool attached = !administration_disabled_ && adapter_.attachDiagnosticOperator(operator_instance);

	if (attached) { has_receipt_ = false; }

	leave();
	return attached;
}

bool HarnessSupervisorOwner::tickDiagnostic(const FixtureStateReceipt &receipt, DiagnosticResult *output) noexcept
{
	if (!diagnostic_domain_) { return false; }

	if (!enter()) { closeDiagnosticOnFault(); return false; }

	if (administration_disabled_) { leave(); return false; }

	if (!coherentTick(receipt) || !receipt.clock.continuous) {
		closeDiagnosticOnFault();

		if (output != nullptr) { *output = diagnostic_audit_; }

		leave();
		return false;
	}

	has_stream_id_ = true;
	stream_id_ = receipt.fixture_state_stream_id;
	has_observation_id_ = true;
	observation_id_floor_ = receipt.clock.observation_id;
	const auto result = adapter_.diagnosticTurn(receipt.sample, DiagPoll{});

	// Keep the adapter's actual last valid physical reference on structural fault.
	if (result.record_count != 0 && result.records[result.record_count - 1].state_reference_valid) {
		sample_ = result.records[result.record_count - 1].physical;

	} else if (!result.snapshot.terminal) {
		sample_ = receipt.sample;
	}

	retainDiagnosticResult(result);

	if (result.snapshot.terminal || !receipt.sample.observer_continuous) {
		administration_disabled_ = true;
		has_receipt_ = false;
	}

	if (output != nullptr) { *output = result; }

	leave();
	return !administration_disabled_;
}

DiagnosticOwnerView HarnessSupervisorOwner::currentDiagnosticView() const noexcept
{
	DiagnosticOwnerView output{};
	const auto state = adapter_.diagnosticSnapshot();
	output.state = state;
	output.physical = sample_;
	output.validator_time_us = sample_.request_guard_context.validator_receive_time_us;
	output.binding = {state.identity.owner_lifetime_id, state.identity.fixture_run_id, state.controller_instance_id,
			  state.operator_instance, state.adapter.state_generation, state.adapter.transition_epoch, state.selector_epoch, sample_.map
			 };
	const auto &context = sample_.request_guard_context;
	const bool available = has_view_ && !administration_disabled_ && state.configured && state.initialized
			       && !state.terminal && state.operator_attached;
	output.can_disable = available;
	output.can_admin = available && context.arm_state == ArmState::Disarmed;
	output.can_enable = output.can_admin && state.selection == DiagnosticSelectionState::Disabled && !state.latched
			    && context.mode_state == ResearchModeState::NotSelected && context.clock_mapping_valid
			    && context.transport_connected && context.required_state_valid && sample_.map.valid
			    && sample_.map.source == config_.adapter_contract.permitted_source
			    && sample_.map.uncertainty_us <= config_.adapter_contract.timing.maximum_clock_uncertainty_us;
	return output;
}

bool HarnessSupervisorOwner::diagnosticSnapshot(DiagnosticOwnerView &output) noexcept
{
	if (!diagnostic_domain_) { return false; }

	if (!enter()) { closeDiagnosticOnFault(); return false; }

	if (!has_view_ || administration_disabled_) { leave(); return false; }

	output = currentDiagnosticView();
	leave();
	return true;
}

bool HarnessSupervisorOwner::runDiagnosticIngress(const DiagnosticTurn &turn, DiagnosticResult &output) noexcept
{
	if (!diagnostic_domain_) { return false; }

	if (!enter()) { closeDiagnosticOnFault(); return false; }

	const auto state = adapter_.diagnosticSnapshot();
	const bool packet = std::holds_alternative<DiagAcknowledge>(turn) || std::holds_alternative<DiagRequest>(turn);

	if (administration_disabled_ || !has_view_ || state.terminal || (packet && !state.operator_attached)) {
		leave();
		return false;
	}

	output = adapter_.diagnosticTurn(sample_, turn);
	retainDiagnosticResult(output);

	if (output.snapshot.terminal) { administration_disabled_ = true; has_receipt_ = false; }

	leave();
	return true;
}

bool HarnessSupervisorOwner::processDiagnosticAcknowledgement(const DecodedSessionV1 &packet,
		const DiagnosticIngress &ingress, DiagnosticResult &output) noexcept
{
	return runDiagnosticIngress(DiagAcknowledge{packet, ingress}, output);
}

bool HarnessSupervisorOwner::processDiagnosticRequest(const DecodedRequestV1 &packet,
		const DiagnosticIngress &ingress, DiagnosticResult &output) noexcept
{
	return runDiagnosticIngress(DiagRequest{packet, ingress}, output);
}

bool HarnessSupervisorOwner::accessDiagnostic(DiagnosticResult &output) noexcept
{
	return runDiagnosticIngress(DiagAccess{}, output);
}

OwnerOutcome HarnessSupervisorOwner::submitDiagnostic(const DiagnosticSubmission &submission,
		DiagnosticResult &output) noexcept
{
	if (!diagnostic_domain_) { return OwnerOutcome::NotSubmitted; }

	if (!enter()) { closeDiagnosticOnFault(); return OwnerOutcome::Unknown; }

	if (administration_disabled_ || !has_view_) { leave(); return OwnerOutcome::NotSubmitted; }

	const auto view = currentDiagnosticView();

	if (!sameDiagnosticOwnerView(view, submission.expected_view, true) || view.state.terminal
	    || !view.state.operator_attached) {
		leave();
		return OwnerOutcome::NotSubmitted;
	}

	const auto *selection = std::get_if<DiagnosticSelectionCommand>(&submission.command);
	const auto *administration = std::get_if<DiagnosticAdministrationCommand>(&submission.command);
	const uint64_t event_id = selection != nullptr ? selection->event_id : administration->event_id;

	if (view.state.session.supervisor_floor == std::numeric_limits<uint64_t>::max()
	    || event_id != view.state.session.supervisor_floor + 1U) {
		leave();
		return OwnerOutcome::NotSubmitted;
	}

	DiagnosticTurn turn{};
	std::optional<DecodedSessionV1> consumed_packet;

	if (selection != nullptr) {
		if (selection->action != DiagnosticAction::Enable && selection->action != DiagnosticAction::Disable) {
			leave(); return OwnerOutcome::NotSubmitted;
		}

		if (selection->action == DiagnosticAction::Enable) {
			if (!view.can_enable || selection->prospective.owner != view.binding.prospective_map.source
			    || selection->prospective.session != view.binding.prospective_map.session) {
				leave(); return OwnerOutcome::NotSubmitted;
			}

			turn = DiagEnable{{event_id, selection->action, view.binding}};

		} else {
			if (!view.can_disable) { leave(); return OwnerOutcome::NotSubmitted; }

			has_receipt_ = false;
			turn = DiagDisable{{event_id, selection->action, view.binding}};
		}

	} else {
		const auto intent = administration->intent;

		if (!view.can_admin || (intent != Intent::Establish && intent != Intent::Revoke && intent != Intent::ClearLatch)) {
			leave(); return OwnerOutcome::NotSubmitted;
		}

		const auto session = administration->session;
		const auto action = intent == Intent::Establish ? SupervisorAction::Establish :
				    (intent == Intent::Revoke ? SupervisorAction::Revoke : SupervisorAction::ClearLatchedFault);
		const bool has_binding = intent != Intent::ClearLatch || view.state.session.has_last_successful_session;
		const auto bound_session = intent == Intent::ClearLatch ? view.state.session.last_successful_session : session;
		const SupervisorEvent event{event_id, view.binding.state_generation, view.binding.transition_epoch,
					    view.binding.controller_instance_id, action, has_binding, bound_session};
		const DiagnosticAdminGrant grant{view.binding, event};

		if (intent == Intent::ClearLatch) {
			if (session.owner != 0 || session.session != 0) { leave(); return OwnerOutcome::NotSubmitted; }

			turn = DiagClear{grant};

		} else {
			const Submission legacy_binding{intent, session, view_, event};

			if (!validReceiptForSubmission(legacy_binding) || receipt_.packet.operation_sequence == 0) {
				has_receipt_ = false;
				leave();
				return OwnerOutcome::NotSubmitted;
			}

			const ImmutableIngressReceipt consumed = receipt_;
			consumed_packet = consumed.packet;
			has_receipt_ = false; // One use even when actual adapter rejects.
			const DiagnosticIngress ingress{view.binding.owner_lifetime_id, view.binding.fixture_run_id, consumed.verdict};

			if (intent == Intent::Establish) { turn = DiagEstablish{consumed.packet, ingress, grant}; }

			else { turn = DiagRevoke{consumed.packet, ingress, grant}; }
		}
	}

	output = adapter_.diagnosticTurn(sample_, turn);
	retainDiagnosticResult(output);

	if (consumed_packet) {
		// Only this owner has the immutable receipt; the broker must not infer
		// its operation sequence from intent/session alone.
		const bool indexed = output.has_operation_record && output.operation_record_index < output.record_count;
		const bool terminal = output.snapshot.terminal && !output.has_operation_record && output.record_count != 0;
		bool matches = output.record_count <= 4 && (indexed || terminal);

		if (matches) {
			const auto &record = output.records[indexed ? output.operation_record_index : output.record_count - 1];
			const auto expected_kind = consumed_packet->operation == 1 ? DiagnosticOperationKind::Establish : DiagnosticOperationKind::Revoke;
			matches = record.correlation.operation == expected_kind && record.correlation.admin.has_packet
				  && record.correlation.admin.packet_source == consumed_packet->source
				  && record.correlation.admin.packet_session == consumed_packet->session
				  && record.correlation.admin.packet_operation_sequence == consumed_packet->operation_sequence;
		}

		if (!matches) { closeDiagnosticOnFault(); leave(); return OwnerOutcome::Unknown; }
	}

	if (output.snapshot.terminal) { administration_disabled_ = true; has_receipt_ = false; }

	leave();
	return OwnerOutcome::Completed;
}

OwnerOutcome HarnessSupervisorOwner::closeDiagnosticOwner(DiagnosticCloseReason reason, DiagnosticResult &output) noexcept
{
	if (!diagnostic_domain_) { return OwnerOutcome::NotSubmitted; }

	if (!enter()) { closeDiagnosticOnFault(); return OwnerOutcome::Unknown; }

	has_receipt_ = false;
	const auto receipt = has_view_ ? std::optional<TrustedStateSample> {sample_} : std::nullopt;
	output = adapter_.closeDiagnosticLifecycle(receipt, reason);
	retainDiagnosticResult(output);

	if (output.snapshot.terminal) { administration_disabled_ = true; }

	leave();
	return OwnerOutcome::Completed;
}

bool HarnessSupervisorOwner::copyLastDiagnosticResult(DiagnosticResult &output) const noexcept
{
	if (!diagnostic_domain_ || in_call_ || !has_diagnostic_audit_) { return false; }

	output = diagnostic_audit_;
	return true;
}
} // namespace research_sitl
