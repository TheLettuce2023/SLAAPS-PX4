#include "SupervisorBroker.hpp"

#include <limits>

namespace research_sitl
{
using namespace research_acceleration;
namespace
{
constexpr uint64_t kMaximum = std::numeric_limits<uint64_t>::max();

class DiagnosticCallScope
{
public:
	explicit DiagnosticCallScope(bool &active) : active_(active) { active_ = true; }
	~DiagnosticCallScope() { active_ = false; }
private:
	bool &active_;
};

bool sameSession(const TrustedSession &a, const TrustedSession &b)
{
	return a.owner == b.owner && a.session == b.session;
}

bool sameMap(const TrustedClockMap &a, const TrustedClockMap &b)
{
	return a.valid == b.valid && a.source == b.source && a.session == b.session
	       && a.controller_instance_id == b.controller_instance_id && a.map_epoch == b.map_epoch
	       && a.validator_minus_producer_us == b.validator_minus_producer_us && a.uncertainty_us == b.uncertainty_us
	       && a.valid_from_producer_us == b.valid_from_producer_us && a.valid_until_producer_us == b.valid_until_producer_us;
}

bool samePhysical(const TrustedStateSample &a, const TrustedStateSample &b, bool time)
{
	const auto &x = a.request_guard_context;
	const auto &y = b.request_guard_context;
	return a.generation == b.generation && a.observer_continuous == b.observer_continuous && sameMap(a.map, b.map)
	       && x.arm_state == y.arm_state && x.mode_state == y.mode_state
	       && x.clock_mapping_valid == y.clock_mapping_valid && x.transport_connected == y.transport_connected
	       && x.required_state_valid == y.required_state_valid
	       && (!time || x.validator_receive_time_us == y.validator_receive_time_us);
}

bool sameAdmission(const AdmissionSnapshot &a, const AdmissionSnapshot &b)
{
	return a.acknowledgement_current == b.acknowledgement_current
	       && a.acknowledgement_operation_sequence == b.acknowledgement_operation_sequence
	       && a.acknowledged_status_sequence == b.acknowledged_status_sequence
	       && a.acknowledgement_raw_producer_time_us == b.acknowledgement_raw_producer_time_us
	       && a.mapped_acknowledgement_time_us == b.mapped_acknowledgement_time_us
	       && a.earliest_acknowledgement_time_us == b.earliest_acknowledgement_time_us
	       && a.latest_acknowledgement_time_us == b.latest_acknowledgement_time_us
	       && a.source == b.source && a.session == b.session && a.controller_instance_id == b.controller_instance_id
	       && a.transition_epoch == b.transition_epoch && a.map_epoch == b.map_epoch && sameMap(a.map, b.map);
}

bool sameSessionSnapshot(const SessionSnapshot &a, const SessionSnapshot &b)
{
	return a.gate == b.gate && a.has_last_successful_session == b.has_last_successful_session
	       && sameSession(a.last_successful_session, b.last_successful_session)
	       && a.has_wire_operation_floor == b.has_wire_operation_floor && a.wire_operation_floor == b.wire_operation_floor
	       && a.has_supervisor_floor == b.has_supervisor_floor && a.supervisor_floor == b.supervisor_floor;
}

bool sameSnapshot(const DiagnosticSnapshot &a, const DiagnosticSnapshot &b, bool exact)
{
	return a.domain == b.domain && a.identity.owner_lifetime_id == b.identity.owner_lifetime_id
	       && a.identity.fixture_run_id == b.identity.fixture_run_id && a.controller_instance_id == b.controller_instance_id
	       && a.configured == b.configured && a.initialized == b.initialized && a.terminal == b.terminal
	       && a.selection == b.selection && a.selector_epoch == b.selector_epoch && a.selection_reason == b.selection_reason
	       && a.operator_attached == b.operator_attached && a.operator_instance == b.operator_instance
	       && a.adapter.initialized == b.adapter.initialized && a.adapter.terminal == b.adapter.terminal
	       && a.adapter.state_generation == b.adapter.state_generation && a.adapter.transition_epoch == b.adapter.transition_epoch
	       && a.adapter.barrier_report_sequence == b.adapter.barrier_report_sequence
	       && a.adapter.barrier_validator_time_us == b.adapter.barrier_validator_time_us
	       && (!exact || a.adapter.last_report_sequence == b.adapter.last_report_sequence)
	       && sameSessionSnapshot(a.session, b.session) && sameAdmission(a.admission, b.admission)
	       && a.latched == b.latched && a.first_latched_code == b.first_latched_code
	       && (!exact || a.rejection_count == b.rejection_count)
	       && a.has_last_accepted_sequence == b.has_last_accepted_sequence
	       && a.last_accepted_sequence == b.last_accepted_sequence && a.last_accepted_authenticated == b.last_accepted_authenticated;
}

bool knownCause(const DiagnosticCause &cause)
{
	if (const auto *v = std::get_if<DiagnosticLocalReason>(&cause)) {
		return *v >= DiagnosticLocalReason::None && *v <= DiagnosticLocalReason::TrustedStateFault;
	}

	if (const auto *v = std::get_if<DecisionCode>(&cause)) {
		return *v >= DecisionCode::Accepted && *v < DecisionCode::ApiDomainMismatch;
	}

	if (const auto *v = std::get_if<AdminCode>(&cause)) {
		return *v >= AdminCode::Observed && *v < AdminCode::ApiDomainMismatch;
	}

	if (const auto *v = std::get_if<IngressCode>(&cause)) {
		return *v >= IngressCode::Observed && *v < IngressCode::ApiDomainMismatch;
	}

	return false;
}

bool validSnapshot(const DiagnosticSnapshot &s)
{
	return s.domain == DiagnosticDomain::SitlDisarmedDiagnostic && s.configured
	       && s.identity.owner_lifetime_id != 0 && s.identity.fixture_run_id != 0 && s.controller_instance_id != 0
	       && s.initialized == s.adapter.initialized && s.terminal == s.adapter.terminal
	       && s.selection >= DiagnosticSelectionState::Disabled && s.selection <= DiagnosticSelectionState::Faulted
	       && (s.selection == DiagnosticSelectionState::Faulted) == s.terminal
	       && knownCause(s.selection_reason)
	       && (s.latched ? s.first_latched_code == DecisionCode::ValidatorTimeRegressed
		   || s.first_latched_code == DecisionCode::ProducerTimeRegressed
		   : s.first_latched_code == DecisionCode::Accepted)
	       && s.session.gate >= SessionGate::NoTrustedSession && s.session.gate <= SessionGate::EstablishmentRequired
	       && s.session.has_supervisor_floor == (s.session.supervisor_floor != 0)
	       && (s.session.has_last_successful_session
		   ? s.session.last_successful_session.owner != 0 && s.session.last_successful_session.session != 0
		   : s.session.last_successful_session.owner == 0 && s.session.last_successful_session.session == 0)
	       && (s.session.has_wire_operation_floor || s.session.wire_operation_floor == 0)
	       && !s.has_last_accepted_sequence && s.last_accepted_sequence == 0 && !s.last_accepted_authenticated
	       && (!s.initialized || (s.adapter.state_generation != 0 && s.adapter.transition_epoch != 0 && s.selector_epoch != 0))
	       && (!s.terminal || (s.selection == DiagnosticSelectionState::Faulted && s.session.gate == SessionGate::Revoked
				   && !s.admission.acknowledgement_current && !s.operator_attached))
	       && (!s.admission.acknowledgement_current || (s.selection == DiagnosticSelectionState::SelectedForValidation
			       && s.session.gate == SessionGate::Established));
}

bool validIntent(Intent intent, const TrustedSession &session)
{
	switch (intent) {
	case Intent::Establish:
	case Intent::Revoke: return session.owner != 0 && session.session != 0;

	case Intent::ClearLatch: return session.owner == 0 && session.session == 0;

	default: return false;
	}
}

bool validView(const DiagnosticOwnerView &v)
{
	const auto &b = v.binding;
	const auto &s = v.state;
	return validSnapshot(s) && s.initialized && !s.terminal && s.operator_attached
	       && !(s.latched && s.selection == DiagnosticSelectionState::SelectedForValidation)
	       && b.owner_lifetime_id == s.identity.owner_lifetime_id && b.fixture_run_id == s.identity.fixture_run_id
	       && b.controller_instance_id == s.controller_instance_id && b.operator_instance == s.operator_instance
	       && b.operator_instance != 0 && b.state_generation == s.adapter.state_generation
	       && b.transition_epoch == s.adapter.transition_epoch && b.selector_epoch == s.selector_epoch
	       && v.physical.generation == b.state_generation && v.physical.observer_continuous
	       && sameMap(b.prospective_map, v.physical.map)
	       && v.validator_time_us == v.physical.request_guard_context.validator_receive_time_us;
}

bool sameBinding(const DiagnosticBinding &a, const DiagnosticBinding &b)
{
	return a.owner_lifetime_id == b.owner_lifetime_id && a.fixture_run_id == b.fixture_run_id
	       && a.controller_instance_id == b.controller_instance_id && a.operator_instance == b.operator_instance
	       && a.state_generation == b.state_generation && a.transition_epoch == b.transition_epoch
	       && a.selector_epoch == b.selector_epoch && sameMap(a.prospective_map, b.prospective_map);
}

uint64_t commandEvent(const DiagnosticSubmission &s)
{
	if (const auto *selection = std::get_if<DiagnosticSelectionCommand>(&s.command)) { return selection->event_id; }

	return std::get<DiagnosticAdministrationCommand>(s.command).event_id;
}

DiagnosticOperationKind commandKind(const DiagnosticSubmission &s)
{
	if (const auto *selection = std::get_if<DiagnosticSelectionCommand>(&s.command)) {
		return selection->action == DiagnosticAction::Enable ? DiagnosticOperationKind::Enable : DiagnosticOperationKind::Disable;
	}

	switch (std::get<DiagnosticAdministrationCommand>(s.command).intent) {
	case Intent::Establish: return DiagnosticOperationKind::Establish;

	case Intent::Revoke: return DiagnosticOperationKind::Revoke;

	case Intent::ClearLatch: return DiagnosticOperationKind::Clear;
	}

	return DiagnosticOperationKind::None;
}
} // namespace

bool sameDiagnosticOwnerView(const DiagnosticOwnerView &a, const DiagnosticOwnerView &b, bool include_time) noexcept
{
	return sameBinding(a.binding, b.binding) && (!include_time || a.validator_time_us == b.validator_time_us)
	       && sameSnapshot(a.state, b.state, include_time) && samePhysical(a.physical, b.physical, include_time)
	       && a.can_enable == b.can_enable && a.can_disable == b.can_disable && a.can_admin == b.can_admin;
}

DiagnosticConsoleResult SupervisorBroker::diagnosticResult(BrokerCode code, uint64_t ticket) const noexcept
{
	return {quarantined_ ? ConsoleDisposition::Quarantined :
		(code == BrokerCode::Submitted ? ConsoleDisposition::Submitted : ConsoleDisposition::NotSubmitted),
		code, std::nullopt, ticket, false, false};
}

DiagnosticConsoleResult SupervisorBroker::diagnosticQuarantine(BrokerCode code, uint64_t ticket) noexcept
{
	quarantine(code, ticket);
	return diagnosticResult(code, ticket);
}

BrokerCode SupervisorBroker::readDiagnosticView(DiagnosticOwnerView &view) noexcept
{
	if (!owner_.diagnosticSnapshot(view)) {
		diagnosticQuarantine(BrokerCode::OwnerUnavailable, 0);
		return BrokerCode::OwnerUnavailable;
	}

	if (view.state.terminal) {
		diagnosticQuarantine(BrokerCode::AuthorityDiscontinuity, 0);
		return BrokerCode::AuthorityDiscontinuity;
	}

	if (!validView(view)) { return BrokerCode::OwnerNotReady; }

	const auto &b = view.binding;

	if ((controller_binding_ != 0 && b.controller_instance_id != controller_binding_)
	    || (diagnostic_owner_binding_ != 0 && b.owner_lifetime_id != diagnostic_owner_binding_)
	    || (diagnostic_run_binding_ != 0 && b.fixture_run_id != diagnostic_run_binding_)
	    || b.operator_instance != operator_floor_
	    || view.state.session.has_supervisor_floor != (event_floor_ != 0)
	    || view.state.session.supervisor_floor != event_floor_
	    || (has_time_ && view.validator_time_us < time_floor_)) {
		diagnosticQuarantine(BrokerCode::AuthorityDiscontinuity, 0);
		return BrokerCode::AuthorityDiscontinuity;
	}

	controller_binding_ = b.controller_instance_id;
	diagnostic_owner_binding_ = b.owner_lifetime_id;
	diagnostic_run_binding_ = b.fixture_run_id;
	has_time_ = true;
	time_floor_ = view.validator_time_us;
	return BrokerCode::Ready;
}

DiagnosticConsoleResult SupervisorBroker::prepareDiagnostic(bool selection, Intent intent,
		const TrustedSession &session) noexcept
{
	if (in_call_) { return diagnosticResult(BrokerCode::Busy); }

	if (domain_ != DiagnosticDomain::SitlDisarmedDiagnostic) { return diagnosticResult(BrokerCode::ContextChanged); }

	if (config_.maximum_confirmation_age_us == 0) { return diagnosticResult(BrokerCode::NotConfigured); }

	if (quarantined_) { return diagnosticResult(BrokerCode::Quarantined); }

	if (!attached_) { return diagnosticResult(BrokerCode::NotAttached); }

	if (selection ? session.owner == 0 || session.session == 0 : !validIntent(intent, session)) {
		return diagnosticResult(BrokerCode::InvalidIntent);
	}

	if (pending_) { return diagnosticResult(BrokerCode::Busy); }

	if (ticket_floor_ == kMaximum) { return diagnosticQuarantine(BrokerCode::CounterExhausted, 0); }

	DiagnosticCallScope scope(in_call_);
	DiagnosticOwnerView view{};
	const auto code = readDiagnosticView(view);

	if (code != BrokerCode::Ready) { return diagnosticResult(code); }

	if (selection ? !view.can_enable : !view.can_admin) { return diagnosticResult(BrokerCode::OwnerNotReady); }

	if (selection && (view.binding.prospective_map.source != session.owner
			  || view.binding.prospective_map.session != session.session)) {
		return diagnosticResult(BrokerCode::ContextChanged);
	}

	pending_ = true;
	pending_ticket_ = ++ticket_floor_;
	pending_diagnostic_selection_ = selection;
	pending_intent_ = intent;
	pending_session_ = session;
	pending_diagnostic_view_ = view;
	return diagnosticResult(BrokerCode::Ready, pending_ticket_);
}

DiagnosticConsoleResult SupervisorBroker::prepareDiagnosticSelection(const TrustedSession &prospective) noexcept
{
	return prepareDiagnostic(true, Intent::Establish, prospective);
}

DiagnosticConsoleResult SupervisorBroker::prepareDiagnosticAdmin(Intent intent, const TrustedSession &session) noexcept
{
	return prepareDiagnostic(false, intent, session);
}

DiagnosticConsoleResult SupervisorBroker::confirmDiagnostic(uint64_t ticket, bool selection) noexcept
{
	if (in_call_) { return diagnosticResult(BrokerCode::Busy, ticket); }

	// A wrong confirmation route only cancels its matching one-shot ticket.
	if (domain_ != DiagnosticDomain::SitlDisarmedDiagnostic
	    || (pending_ && ticket != 0 && ticket == pending_ticket_ && selection != pending_diagnostic_selection_)) {
		if (pending_ && ticket != 0 && ticket == pending_ticket_) { pending_ = false; }

		return diagnosticResult(BrokerCode::ContextChanged, ticket);
	}

	if (config_.maximum_confirmation_age_us == 0) { return diagnosticResult(BrokerCode::NotConfigured, ticket); }

	if (quarantined_) { return diagnosticResult(BrokerCode::Quarantined, ticket); }

	if (!attached_) { return diagnosticResult(BrokerCode::NotAttached, ticket); }

	if (!pending_ || ticket == 0 || ticket != pending_ticket_) { return diagnosticResult(BrokerCode::InvalidTicket, ticket); }

	DiagnosticCallScope scope(in_call_);
	pending_ = false;
	DiagnosticOwnerView view{};
	const auto code = readDiagnosticView(view);

	if (code != BrokerCode::Ready) { return diagnosticResult(code, ticket); }

	if (!sameDiagnosticOwnerView(view, pending_diagnostic_view_, false)) {
		return diagnosticResult(BrokerCode::ContextChanged, ticket);
	}

	if (view.validator_time_us - pending_diagnostic_view_.validator_time_us > config_.maximum_confirmation_age_us) {
		return diagnosticResult(BrokerCode::Expired, ticket);
	}

	if (event_floor_ == kMaximum) { return diagnosticQuarantine(BrokerCode::CounterExhausted, ticket); }

	DiagnosticSubmission submission{};
	submission.expected_view = view;

	if (selection) {
		submission.command = DiagnosticSelectionCommand{DiagnosticAction::Enable, pending_session_, event_floor_ + 1U};

	} else {
		submission.command = DiagnosticAdministrationCommand{pending_intent_, pending_session_, event_floor_ + 1U};
	}

	return submitDiagnosticCommand(submission, ticket);
}

DiagnosticConsoleResult SupervisorBroker::confirmDiagnosticSelection(uint64_t ticket) noexcept
{
	return confirmDiagnostic(ticket, true);
}

DiagnosticConsoleResult SupervisorBroker::confirmDiagnosticAdmin(uint64_t ticket) noexcept
{
	return confirmDiagnostic(ticket, false);
}

DiagnosticConsoleResult SupervisorBroker::disableDiagnosticSelection() noexcept
{
	if (in_call_) { return diagnosticResult(BrokerCode::Busy); }

	if (domain_ != DiagnosticDomain::SitlDisarmedDiagnostic) { return diagnosticResult(BrokerCode::ContextChanged); }

	if (config_.maximum_confirmation_age_us == 0) { return diagnosticResult(BrokerCode::NotConfigured); }

	if (quarantined_) { return diagnosticResult(BrokerCode::Quarantined); }

	if (!attached_) { return diagnosticResult(BrokerCode::NotAttached); }

	DiagnosticCallScope scope(in_call_);
	pending_ = false;
	DiagnosticOwnerView view{};
	const auto code = readDiagnosticView(view);

	if (code != BrokerCode::Ready) { return diagnosticResult(code); }

	if (!view.can_disable) { return diagnosticResult(BrokerCode::OwnerNotReady); }

	if (event_floor_ == kMaximum) { return diagnosticQuarantine(BrokerCode::CounterExhausted, 0); }

	DiagnosticSubmission submission{};
	submission.expected_view = view;
	submission.command = DiagnosticSelectionCommand{DiagnosticAction::Disable, {}, event_floor_ + 1U};
	return submitDiagnosticCommand(submission, 0);
}

DiagnosticConsoleResult SupervisorBroker::submitDiagnosticCommand(const DiagnosticSubmission &submission, uint64_t ticket) noexcept
{
	DiagnosticResult response{};
	const auto outcome = owner_.submitDiagnostic(submission, response);

	if (outcome == OwnerOutcome::NotSubmitted) { return diagnosticResult(BrokerCode::OwnerDeclined, ticket); }

	if (outcome != OwnerOutcome::Completed) { return diagnosticQuarantine(BrokerCode::OutcomeUnknown, ticket); }

	last_diagnostic_audit_ = response;
	has_diagnostic_audit_ = true;

	if (!validDiagnosticResult(submission, response)) { return diagnosticQuarantine(BrokerCode::InvalidOwnerResult, ticket); }

	if (!response.grant_consumed) {
		auto rejected = diagnosticQuarantine(BrokerCode::AdapterUnconsumed, ticket);
		rejected.diagnostic_code = response.code;
		return rejected;
	}

	event_floor_ = commandEvent(submission);
	auto submitted = diagnosticResult(BrokerCode::Submitted, ticket);
	submitted.diagnostic_code = response.code;
	submitted.event_consumed = true;
	submitted.action_performed = response.action_performed;
	return submitted;
}

bool SupervisorBroker::copyLastDiagnosticResult(DiagnosticResult &output) const noexcept
{
	if (in_call_ || !has_diagnostic_audit_) { return false; }

	output = last_diagnostic_audit_;
	return true;
}

bool SupervisorBroker::validDiagnosticResult(const DiagnosticSubmission &submission, const DiagnosticResult &r) noexcept
{
	const auto &v = submission.expected_view;
	const auto operation = commandKind(submission);
	const uint64_t event = commandEvent(submission);
	const bool administration = std::holds_alternative<DiagnosticAdministrationCommand>(submission.command);
	const auto *admin = std::get_if<DiagnosticAdministrationCommand>(&submission.command);

	if (!validView(v) || event == 0 || v.state.session.supervisor_floor == kMaximum
	    || event != v.state.session.supervisor_floor + 1U || !validSnapshot(r.snapshot)
	    || r.record_count == 0 || r.record_count > (administration ? 4 : 3)
	    || (r.has_operation_record ? r.operation_record_index >= r.record_count : r.operation_record_index != 0)
	    || (!r.snapshot.terminal && !r.has_operation_record)
	    || (r.snapshot.terminal && (r.grant_consumed || r.action_performed))
	    || (r.action_performed && !r.grant_consumed)) { return false; }

	DiagnosticSnapshot previous = v.state;
	uint8_t operations = 0;
	uint8_t selector_barriers = 0;
	uint8_t session_barriers = 0;
	bool consumed = false;

	for (uint8_t i = 0; i < r.record_count; ++i) {
		const auto &record = r.records[i];
		const auto &s = record.snapshot;
		const auto &c = record.correlation;
		const bool terminal = record.kind == DiagnosticReportKind::Terminal;
		const bool barrier = record.kind == DiagnosticReportKind::SelectorBarrier
				     || record.kind == DiagnosticReportKind::SessionBarrier;

		if (((record.kind == DiagnosticReportKind::SelectorBarrier || terminal) && record.cause != s.selection_reason)
		    || (record.kind == DiagnosticReportKind::SessionBarrier
			&& record.cause != DiagnosticCause{DiagnosticLocalReason::None})) { return false; }

		if (!validSnapshot(s) || record.domain != DiagnosticDomain::SitlDisarmedDiagnostic
		    || record.identity.owner_lifetime_id != v.binding.owner_lifetime_id
		    || record.identity.fixture_run_id != v.binding.fixture_run_id
		    || s.identity.owner_lifetime_id != v.binding.owner_lifetime_id
		    || s.identity.fixture_run_id != v.binding.fixture_run_id
		    || s.controller_instance_id != v.binding.controller_instance_id
		    || s.operator_instance != v.binding.operator_instance
		    || record.eligible_for_control || !knownCause(record.cause)
		    || !record.state_reference_valid || !samePhysical(record.physical, v.physical, true)
		    || s.adapter.state_generation != v.binding.state_generation
		    || s.adapter.last_report_sequence != record.report_sequence
		    || s.adapter.transition_epoch != record.transition_epoch || s.selector_epoch != record.selector_epoch
		    || c.operation != operation || c.operator_instance != v.binding.operator_instance
		    || c.close_reason.has_value() || !c.admin.has_supervisor_event || c.admin.supervisor_event_id != event
		    || c.ingress.packet_kind != IngressPacketKind::None || c.ingress.source != 0 || c.ingress.session != 0
		    || c.ingress.controller_instance_id != 0 || c.ingress.transition_epoch != 0 || c.ingress.map_epoch != 0
		    || c.ingress.packet_sequence != 0 || c.ingress.acknowledged_status_sequence != 0
		    || (consumed && !c.admin.supervisor_event_consumed)) { return false; }

		if (previous.adapter.last_report_sequence == kMaximum
		    || (record.report_sequence != previous.adapter.last_report_sequence + 1U
			&& !(terminal && record.report_sequence == kMaximum))
		    || (!terminal && record.report_sequence == kMaximum)) { return false; }

		consumed = c.admin.supervisor_event_consumed;
		const uint64_t expected_floor = consumed ? event : v.state.session.supervisor_floor;

		if (s.session.has_supervisor_floor != (expected_floor != 0) || s.session.supervisor_floor != expected_floor) {
			return false;
		}

		const bool packet = administration && admin->intent != Intent::ClearLatch;

		if (c.admin.has_packet != packet
		    || (packet ? c.admin.packet_source != admin->session.owner || c.admin.packet_session != admin->session.session
			|| c.admin.packet_operation_sequence == 0 || !c.authentication.has_value()
			: c.admin.packet_source != 0 || c.admin.packet_session != 0 || c.admin.packet_operation_sequence != 0
			|| c.authentication.has_value())) { return false; }

		if (packet && (!c.authentication->mavlink2 || !c.authentication->parser_authenticated
			       || !c.authentication->configured_header_channel_source_binding
			       || !c.authentication->configured_target_binding)) { return false; }

		if (record.common_decision && (record.common_decision->eligible_for_control
					       || record.common_decision->code < DecisionCode::Accepted || record.common_decision->code >= DecisionCode::ApiDomainMismatch
					       || record.common_decision->code == DecisionCode::ValidatedDisarmed
					       || record.common_decision->syntactically_valid || record.common_decision->conservative_age_valid
					       || record.common_decision->conservative_age_us != 0)) { return false; }

		// The owner takes an exact already-observed view before submission.
		// A new physical barrier here would contradict that trusted receipt.
		if (record.kind == DiagnosticReportKind::PhysicalBarrier) { return false; }

		if (barrier) {
			if (previous.adapter.transition_epoch == kMaximum
			    || record.transition_epoch != previous.adapter.transition_epoch + 1U
			    || s.adapter.barrier_report_sequence != record.report_sequence
			    || s.adapter.barrier_validator_time_us != v.validator_time_us || record.common_decision) { return false; }

		} else if (record.transition_epoch != previous.adapter.transition_epoch
			   || s.adapter.barrier_report_sequence != previous.adapter.barrier_report_sequence
			   || s.adapter.barrier_validator_time_us != previous.adapter.barrier_validator_time_us) { return false; }

		switch (record.kind) {
		case DiagnosticReportKind::SelectorBarrier:
			if (++selector_barriers > 1 || previous.selector_epoch == kMaximum
			    || s.selector_epoch != previous.selector_epoch + 1U || s.selection == previous.selection
			    || s.selection == DiagnosticSelectionState::Faulted
			    || s.session.gate != SessionGate::EstablishmentRequired
			    || !sameAdmission(s.admission, AdmissionSnapshot{})
			    || record.code != (s.selection == DiagnosticSelectionState::SelectedForValidation
					       ? DiagnosticCode::SelectionEnabled : DiagnosticCode::SelectionDisabled)
			    || (s.selection == DiagnosticSelectionState::SelectedForValidation
				&& (operation != DiagnosticOperationKind::Enable || operations != 0 || !r.action_performed))) { return false; }

			break;

		case DiagnosticReportKind::SessionBarrier:
			if (++session_barriers > 1 || !administration || !r.action_performed || !consumed || operations != 0
			    || record.code != DiagnosticCode::Observed || s.selection != previous.selection
			    || s.selector_epoch != previous.selector_epoch || !sameAdmission(s.admission, AdmissionSnapshot{})) { return false; }

			if (s.session.gate != (operation == DiagnosticOperationKind::Establish ? SessionGate::Established
					       : operation == DiagnosticOperationKind::Revoke ? SessionGate::Revoked
					       : SessionGate::EstablishmentRequired)) { return false; }

			break;

		case DiagnosticReportKind::Operation:
			if (++operations != 1 || !r.has_operation_record || i != r.operation_record_index
			    || record.code != r.code || s.selection != previous.selection || s.selector_epoch != previous.selector_epoch
			    || c.admin.supervisor_event_consumed != r.grant_consumed) { return false; }

			// Administration mutations precede their session barrier. The
			// operation record cannot invent another session/ACK transition.
			if (s.session.gate != previous.session.gate
			    || s.session.has_last_successful_session != previous.session.has_last_successful_session
			    || !sameSession(s.session.last_successful_session, previous.session.last_successful_session)
			    || s.session.has_wire_operation_floor != previous.session.has_wire_operation_floor
			    || s.session.wire_operation_floor != previous.session.wire_operation_floor
			    || !sameAdmission(s.admission, previous.admission)) { return false; }

			if (record.common_decision) {
				const auto &decision = *record.common_decision;
				const auto *cause = std::get_if<DecisionCode>(&record.cause);

				if (cause == nullptr || *cause != decision.code || decision.latched != s.latched
				    || decision.first_latched_code != s.first_latched_code
				    || decision.rejection_count != s.rejection_count) { return false; }
			}

			if ((record.code == DiagnosticCode::SessionEstablished || record.code == DiagnosticCode::LatchCleared)
			    && (!record.common_decision || record.common_decision->code != DecisionCode::Accepted
				|| record.common_decision->latched)) { return false; }

			if ((record.code == DiagnosticCode::SelectionEnabled || record.code == DiagnosticCode::SelectionDisabled
			     || record.code == DiagnosticCode::AlreadyDisabled)
			    && (record.cause != DiagnosticCause{DiagnosticLocalReason::None} || record.common_decision)) { return false; }

			if (record.code == DiagnosticCode::SessionRevoked
			    && (record.cause != DiagnosticCause{AdminCode::Completed} || record.common_decision)) { return false; }

			if (record.code == DiagnosticCode::Rejected
			    && (record.cause == DiagnosticCause{DiagnosticLocalReason::None}
				|| record.cause == DiagnosticCause{DecisionCode::Accepted}
				|| record.cause == DiagnosticCause{DecisionCode::ValidatedDisarmed}
				|| record.cause == DiagnosticCause{DecisionCode::NoEligibleRequest}
				|| record.cause == DiagnosticCause{AdminCode::Observed}
				|| record.cause == DiagnosticCause{AdminCode::Completed}
				|| std::holds_alternative<IngressCode>(record.cause))) { return false; }

			break;

		case DiagnosticReportKind::Terminal:
			if (i + 1 != r.record_count || !s.terminal || r.code != DiagnosticCode::Terminal
			    || record.code != DiagnosticCode::Terminal || consumed || r.has_operation_record
			    || s.selector_epoch != previous.selector_epoch) { return false; }

			break;

		default: return false;
		}

		if (!terminal && (!s.operator_attached || s.terminal)) { return false; }

		// Submission reuses the owner's already-polled timestamp. Only the
		// requested successful clear may change the retained latch here.
		const bool cleared = operation == DiagnosticOperationKind::Clear && r.code == DiagnosticCode::LatchCleared
				     && r.action_performed && session_barriers != 0;

		if ((cleared && !v.state.latched) || s.latched != (cleared ? false : v.state.latched)
		    || s.first_latched_code != (cleared ? DecisionCode::Accepted : v.state.first_latched_code)) { return false; }

		const bool established = administration && admin->intent == Intent::Establish && r.action_performed
					 && session_barriers != 0;
		const bool expected_history = established || v.state.session.has_last_successful_session;
		const TrustedSession expected_session = established ? admin->session : v.state.session.last_successful_session;

		if (s.session.has_last_successful_session != expected_history
		    || !sameSession(s.session.last_successful_session, expected_session)) { return false; }

		if (!terminal && session_barriers == 0
		    && (s.session.has_wire_operation_floor != v.state.session.has_wire_operation_floor
			|| s.session.wire_operation_floor != v.state.session.wire_operation_floor)) { return false; }

		if (session_barriers != 0 && admin != nullptr) {
			const uint64_t expected_wire_floor = admin->intent == Intent::ClearLatch
							     ? v.state.session.wire_operation_floor : c.admin.packet_operation_sequence;
			const bool expected_has_wire = admin->intent != Intent::ClearLatch || v.state.session.has_wire_operation_floor;

			if (s.session.has_wire_operation_floor != expected_has_wire || s.session.wire_operation_floor != expected_wire_floor) {
				return false;
			}
		}

		previous = s;
	}

	if (!sameSnapshot(previous, r.snapshot, true) || consumed != r.grant_consumed
	    || operations != (r.has_operation_record ? 1 : 0)) { return false; }

	if (r.snapshot.terminal) { return true; }

	if (r.code == DiagnosticCode::SessionEstablished
	    && (admin == nullptr || v.state.selection != DiagnosticSelectionState::SelectedForValidation || v.state.latched
		|| admin->session.owner != v.binding.prospective_map.source
		|| admin->session.session != v.binding.prospective_map.session
		|| (v.state.session.has_last_successful_session && admin->session.session == v.state.session.last_successful_session.session))) {
		return false;
	}

	if (r.code == DiagnosticCode::SessionRevoked
	    && (admin == nullptr || v.state.session.gate != SessionGate::Established || v.state.latched
		|| !v.state.session.has_last_successful_session || !sameSession(admin->session, v.state.session.last_successful_session)
		|| admin->session.owner != v.binding.prospective_map.source
		|| admin->session.session != v.binding.prospective_map.session)) { return false; }

	if (r.code == DiagnosticCode::LatchCleared && !v.state.latched) { return false; }

	switch (r.code) {
	case DiagnosticCode::SelectionEnabled:
		return operation == DiagnosticOperationKind::Enable && r.action_performed && selector_barriers == 1
		       && session_barriers == 0 && !r.snapshot.latched
		       && r.snapshot.selection == DiagnosticSelectionState::SelectedForValidation;

	case DiagnosticCode::SelectionDisabled:
		return operation == DiagnosticOperationKind::Disable && r.action_performed && selector_barriers == 1
		       && session_barriers == 0 && r.snapshot.selection == DiagnosticSelectionState::Disabled;

	case DiagnosticCode::AlreadyDisabled:
		return operation == DiagnosticOperationKind::Disable && !r.action_performed
		       && session_barriers == 0 && r.snapshot.selection == DiagnosticSelectionState::Disabled;

	case DiagnosticCode::SessionEstablished:
		return operation == DiagnosticOperationKind::Establish && r.action_performed && session_barriers == 1
		       && selector_barriers == 0 && r.snapshot.selection == DiagnosticSelectionState::SelectedForValidation
		       && r.snapshot.session.gate == SessionGate::Established && !r.snapshot.admission.acknowledgement_current;

	case DiagnosticCode::SessionRevoked:
	case DiagnosticCode::LatchCleared:
		return operation == (r.code == DiagnosticCode::SessionRevoked ? DiagnosticOperationKind::Revoke : DiagnosticOperationKind::Clear)
		       && r.action_performed && session_barriers == 1 && r.snapshot.selection == DiagnosticSelectionState::Disabled
		       && !r.snapshot.admission.acknowledgement_current
		       && (r.snapshot.session.gate == SessionGate::EstablishmentRequired || r.snapshot.session.gate == SessionGate::Revoked)
		       && !r.snapshot.latched;

	case DiagnosticCode::Rejected:
		return !r.action_performed && session_barriers == 0;

	default: return false;
	}
}

bool SupervisorBroker::closeDiagnosticAuthority(DiagnosticCloseReason reason, DiagnosticResult &response) noexcept
{
	// Closure is unconditional: an unavailable snapshot must never gate it.
	DiagnosticOwnerView before{};
	const bool has_before = owner_.diagnosticSnapshot(before) && validSnapshot(before.state);
	const auto outcome = owner_.closeDiagnosticOwner(reason, response);

	if (outcome != OwnerOutcome::Completed || !validSnapshot(response.snapshot)
	    || response.grant_consumed || response.record_count > 3
	    || response.snapshot.operator_attached || response.snapshot.admission.acknowledgement_current
	    || (response.snapshot.selection != DiagnosticSelectionState::Disabled
		&& response.snapshot.selection != DiagnosticSelectionState::Faulted)
	    || (response.snapshot.session.gate != SessionGate::EstablishmentRequired
		&& response.snapshot.session.gate != SessionGate::Revoked)
	    || (diagnostic_owner_binding_ != 0 && response.snapshot.identity.owner_lifetime_id != diagnostic_owner_binding_)
	    || (diagnostic_run_binding_ != 0 && response.snapshot.identity.fixture_run_id != diagnostic_run_binding_)
	    || (controller_binding_ != 0 && response.snapshot.controller_instance_id != controller_binding_)
	    || (response.has_operation_record ? response.operation_record_index >= response.record_count
		: response.operation_record_index != 0)) { return false; }

	if (response.record_count == 0) {
		return response.snapshot.terminal && response.code == DiagnosticCode::Terminal
		       && !response.has_operation_record && !response.action_performed
		       && (!has_before || sameSnapshot(before.state, response.snapshot, true));
	}

	uint8_t operations = 0;
	uint8_t selectors = 0;
	uint8_t physicals = 0;
	DiagnosticSnapshot previous = has_before ? before.state : response.records[0].snapshot;

	for (uint8_t i = 0; i < response.record_count; ++i) {
		const auto &record = response.records[i];
		const auto &s = record.snapshot;
		const auto &c = record.correlation;
		const bool terminal = record.kind == DiagnosticReportKind::Terminal;
		const bool barrier = record.kind == DiagnosticReportKind::SelectorBarrier || record.kind == DiagnosticReportKind::PhysicalBarrier;

		if (((record.kind == DiagnosticReportKind::SelectorBarrier || terminal) && record.cause != s.selection_reason)
		    || (record.kind == DiagnosticReportKind::PhysicalBarrier
			&& record.cause != DiagnosticCause{DiagnosticLocalReason::None})) { return false; }

		if (!validSnapshot(s) || record.domain != DiagnosticDomain::SitlDisarmedDiagnostic
		    || record.identity.owner_lifetime_id != response.snapshot.identity.owner_lifetime_id
		    || record.identity.fixture_run_id != response.snapshot.identity.fixture_run_id
		    || s.identity.owner_lifetime_id != response.snapshot.identity.owner_lifetime_id
		    || s.identity.fixture_run_id != response.snapshot.identity.fixture_run_id
		    || s.controller_instance_id != response.snapshot.controller_instance_id
		    || s.operator_instance != response.snapshot.operator_instance
		    || record.eligible_for_control || !knownCause(record.cause)
		    || record.state_reference_valid != s.initialized
		    || (record.state_reference_valid && record.physical.generation != s.adapter.state_generation)
		    || (has_before && record.state_reference_valid && !samePhysical(record.physical, before.physical, true))
		    || (has_before && (s.latched != before.state.latched || s.first_latched_code != before.state.first_latched_code))
		    || record.report_sequence != s.adapter.last_report_sequence
		    || record.transition_epoch != s.adapter.transition_epoch || record.selector_epoch != s.selector_epoch
		    || c.operation != DiagnosticOperationKind::LifecycleClose || !c.close_reason || *c.close_reason != reason
		    || c.operator_instance != response.snapshot.operator_instance || c.admin.has_supervisor_event
		    || c.admin.supervisor_event_id != 0 || c.admin.supervisor_event_consumed || c.admin.has_packet
		    || c.admin.packet_source != 0 || c.admin.packet_session != 0 || c.admin.packet_operation_sequence != 0
		    || c.authentication || c.ingress.packet_kind != IngressPacketKind::None
		    || c.ingress.source != 0 || c.ingress.session != 0 || c.ingress.controller_instance_id != 0
		    || c.ingress.transition_epoch != 0 || c.ingress.map_epoch != 0 || c.ingress.packet_sequence != 0
		    || c.ingress.acknowledged_status_sequence != 0 || record.common_decision) { return false; }

		if (has_before || i != 0) {
			if (previous.adapter.last_report_sequence == kMaximum
			    || (record.report_sequence != previous.adapter.last_report_sequence + 1U
				&& !(terminal && record.report_sequence == kMaximum))
			    || (!terminal && record.report_sequence == kMaximum)
			    || (barrier ? previous.adapter.transition_epoch == kMaximum
				|| record.transition_epoch != previous.adapter.transition_epoch + 1U
				: record.transition_epoch != previous.adapter.transition_epoch)
			    || s.session.has_supervisor_floor != previous.session.has_supervisor_floor
			    || s.session.supervisor_floor != previous.session.supervisor_floor
			    || s.session.has_wire_operation_floor != previous.session.has_wire_operation_floor
			    || s.session.wire_operation_floor != previous.session.wire_operation_floor
			    || s.session.has_last_successful_session != previous.session.has_last_successful_session
			    || !sameSession(s.session.last_successful_session, previous.session.last_successful_session)) { return false; }

			if (record.kind == DiagnosticReportKind::SelectorBarrier) {
				if (previous.selector_epoch == kMaximum || s.selector_epoch != previous.selector_epoch + 1U
				    || s.selection == previous.selection) { return false; }

			} else if (s.selector_epoch != previous.selector_epoch) { return false; }
		}

		if (barrier && (s.adapter.barrier_report_sequence != record.report_sequence
				|| s.adapter.barrier_validator_time_us != record.physical.request_guard_context.validator_receive_time_us)) {
			return false;
		}

		switch (record.kind) {
		case DiagnosticReportKind::PhysicalBarrier:
			if (++physicals > 1 || i != 0 || record.code != DiagnosticCode::Observed) { return false; }

			break;

		case DiagnosticReportKind::SelectorBarrier:
			if (++selectors > 1 || s.selection != DiagnosticSelectionState::Disabled
			    || record.code != DiagnosticCode::SelectionDisabled || s.admission.acknowledgement_current
			    || s.session.gate != SessionGate::EstablishmentRequired) { return false; }

			break;

		case DiagnosticReportKind::Operation:
			if (++operations != 1 || !response.has_operation_record || response.operation_record_index != i
			    || record.code != response.code || i + 1 != response.record_count || s.operator_attached
			    || !response.action_performed
			    || record.cause != DiagnosticCause{DiagnosticLocalReason::None}
			    || (record.code != DiagnosticCode::SelectionDisabled && record.code != DiagnosticCode::AlreadyDisabled)) { return false; }

			break;

		case DiagnosticReportKind::Terminal:
			if (i + 1 != response.record_count || !s.terminal || response.code != DiagnosticCode::Terminal
			    || record.code != DiagnosticCode::Terminal || response.action_performed || response.has_operation_record) { return false; }

			break;

		default: return false;
		}

		previous = s;
	}

	return sameSnapshot(previous, response.snapshot, true)
	       && operations == (response.has_operation_record ? 1 : 0)
	       && (response.snapshot.terminal || response.has_operation_record);
}

DiagnosticConsoleResult SupervisorBroker::detachDiagnosticOperator() noexcept
{
	if (in_call_) { return diagnosticResult(BrokerCode::Busy); }

	if (domain_ != DiagnosticDomain::SitlDisarmedDiagnostic) { return diagnosticResult(BrokerCode::ContextChanged); }

	DiagnosticCallScope scope(in_call_);
	pending_ = false;
	DiagnosticResult response{};
	const bool closed = closeDiagnosticAuthority(DiagnosticCloseReason::OperatorDetach, response);
	attached_ = false;

	if (!closed) {
		// An uncertain close is never reported as a successful detach. Do not
		// retry through a packet/grant path or reset any retained floor.
		quarantined_ = true;
		return diagnosticResult(BrokerCode::OutcomeUnknown);
	}

	last_diagnostic_audit_ = response;
	has_diagnostic_audit_ = true;

	if (response.snapshot.terminal) {
		quarantined_ = true;
		auto terminal = diagnosticResult(BrokerCode::AuthorityDiscontinuity);
		terminal.diagnostic_code = response.code;
		return terminal;
	}

	auto result = diagnosticResult(BrokerCode::Submitted);
	result.diagnostic_code = response.code;
	result.action_performed = response.action_performed;
	return result;
}
} // namespace research_sitl
