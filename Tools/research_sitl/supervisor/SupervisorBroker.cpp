#include "SupervisorBroker.hpp"

#include <limits>

namespace research_sitl
{
using namespace research_acceleration;

namespace
{
constexpr uint64_t kFirst = 1;
constexpr uint64_t kMaximum = std::numeric_limits<uint64_t>::max();

class CallScope
{
public:
	explicit CallScope(bool &active) : active_(active) { active_ = true; }
	~CallScope() { active_ = false; }
private:
	bool &active_;
};

bool sameSession(const TrustedSession &a, const TrustedSession &b)
{
	return a.owner == b.owner && a.session == b.session;
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

bool knownAdminCode(AdminCode code)
{
	switch (code) {
	case AdminCode::Completed:
	case AdminCode::NotConfigured:
	case AdminCode::TrustedStateFault:
	case AdminCode::CounterExhausted:
	case AdminCode::SupervisorMissing:
	case AdminCode::SupervisorDuplicate:
	case AdminCode::SupervisorStreamFault:
	case AdminCode::MalformedPacket:
	case AdminCode::IngressRejected:
	case AdminCode::UnsupportedOperation:
	case AdminCode::PacketOperationReplay:
	case AdminCode::BindingMismatch:
	case AdminCode::EstablishmentContextRejected:
	case AdminCode::ContextRejected:
	case AdminCode::NoEstablishedSession:
	case AdminCode::ClearNotLatched:
	case AdminCode::S1ActionRejected: return true;

	case AdminCode::Observed:
	case AdminCode::ApiDomainMismatch: return false;
	}

	return false;
}

bool sameCorrelation(const AdminCorrelation &a, const AdminCorrelation &b)
{
	return a.has_packet == b.has_packet && a.packet_source == b.packet_source
	       && a.packet_session == b.packet_session && a.packet_operation_sequence == b.packet_operation_sequence
	       && a.has_supervisor_event == b.has_supervisor_event && a.supervisor_event_id == b.supervisor_event_id
	       && a.supervisor_event_consumed == b.supervisor_event_consumed;
}
}

SupervisorBroker::SupervisorBroker(const BrokerConfig &config, SupervisorOwner &owner) noexcept :
#if defined(RESEARCH_SITL_DIAGNOSTIC_OFFLINE)
	domain_(owner.domain()),
#endif
	config_(config), owner_(owner)
{
}

ConsoleResult SupervisorBroker::result(BrokerCode code, uint64_t ticket) const noexcept
{
	return {quarantined_ ? ConsoleDisposition::Quarantined :
		(code == BrokerCode::Submitted ? ConsoleDisposition::Submitted : ConsoleDisposition::NotSubmitted),
		code, ticket, false, AdminCode::Observed, false};
}

ConsoleResult SupervisorBroker::quarantine(BrokerCode code, uint64_t ticket) noexcept
{
	quarantined_ = true;
	pending_ = false;
#if defined(RESEARCH_SITL_DIAGNOSTIC_OFFLINE)

	if (domain_ == DiagnosticDomain::SitlDisarmedDiagnostic) {
		DiagnosticResult closed{};
		const bool was_in_call = in_call_;
		in_call_ = true;
		closeDiagnosticAuthority(DiagnosticCloseReason::OperatorLost, closed);
		in_call_ = was_in_call;
		attached_ = false;
	}

#endif
	return result(code, ticket);
}

bool SupervisorBroker::attach(uint64_t operator_instance) noexcept
{
	if (in_call_ || config_.maximum_confirmation_age_us == 0 || quarantined_ || attached_
	    || operator_instance == 0 || operator_instance <= operator_floor_) { return false; }

#if defined(RESEARCH_SITL_DIAGNOSTIC_OFFLINE)

	if (domain_ == DiagnosticDomain::SitlDisarmedDiagnostic) {
		CallScope scope(in_call_);

		if (!owner_.attachDiagnosticOperator(operator_instance)) { return false; }
	}

#endif
	operator_floor_ = operator_instance;
	attached_ = true;
	return true;
}

bool SupervisorBroker::detach() noexcept
{
	if (in_call_) { return false; }

#if defined(RESEARCH_SITL_DIAGNOSTIC_OFFLINE)

	if (domain_ == DiagnosticDomain::SitlDisarmedDiagnostic) {
		return detachDiagnosticOperator().broker_code == BrokerCode::Submitted;
	}

#endif
	attached_ = false;
	pending_ = false;
	return true;
}

BrokerCode SupervisorBroker::readView(OwnerView &view) noexcept
{
	if (!owner_.snapshot(view)) { return BrokerCode::OwnerUnavailable; }

	if (view.terminal) {
		quarantined_ = true;
		pending_ = false;
		return BrokerCode::AuthorityDiscontinuity;
	}

	if (!view.ready || view.controller_instance_id == 0 || view.state_generation == 0
	    || view.transition_epoch == 0 || view.clock_mapping_epoch == 0
	    || (view.has_session_history ? (view.last_session.owner == 0 || view.last_session.session == 0)
		: (view.last_session.owner != 0 || view.last_session.session != 0))
	    || (view.has_supervisor_floor ? view.supervisor_floor == 0 : view.supervisor_floor != 0)) {
		return BrokerCode::OwnerNotReady;
	}

	if ((controller_binding_ != 0 && view.controller_instance_id != controller_binding_)
	    || view.has_supervisor_floor != (event_floor_ != 0) || view.supervisor_floor != event_floor_
	    || (has_time_ && view.validator_time_us < time_floor_)) {
		quarantined_ = true;
		pending_ = false;
		return BrokerCode::AuthorityDiscontinuity;
	}

	controller_binding_ = view.controller_instance_id;
	has_time_ = true;
	time_floor_ = view.validator_time_us;
	return BrokerCode::Ready;
}

bool SupervisorBroker::sameContext(const OwnerView &a, const OwnerView &b) noexcept
{
	return a.controller_instance_id == b.controller_instance_id && a.state_generation == b.state_generation
	       && a.transition_epoch == b.transition_epoch && a.clock_mapping_epoch == b.clock_mapping_epoch
	       && a.has_session_history == b.has_session_history && sameSession(a.last_session, b.last_session)
	       && a.has_supervisor_floor == b.has_supervisor_floor && a.supervisor_floor == b.supervisor_floor;
}

ConsoleResult SupervisorBroker::prepare(Intent intent, const TrustedSession &session) noexcept
{
#if defined(RESEARCH_SITL_DIAGNOSTIC_OFFLINE)

	if (domain_ != DiagnosticDomain::Flight) { return result(BrokerCode::ContextChanged); }

#endif

	if (in_call_) { return result(BrokerCode::Busy); }

	if (config_.maximum_confirmation_age_us == 0) { return result(BrokerCode::NotConfigured); }

	if (quarantined_) { return result(BrokerCode::Quarantined); }

	if (!attached_) { return result(BrokerCode::NotAttached); }

	if (!validIntent(intent, session)) { return result(BrokerCode::InvalidIntent); }

	if (pending_) { return result(BrokerCode::Busy); }

	if (ticket_floor_ == kMaximum) { return quarantine(BrokerCode::CounterExhausted, 0); }

	CallScope scope(in_call_);
	OwnerView view{};
	const BrokerCode code = readView(view);

	if (code != BrokerCode::Ready) { return result(code); }

	pending_ = true;
	pending_ticket_ = ++ticket_floor_;
	pending_intent_ = intent;
	pending_session_ = session;
	pending_view_ = view;
	return result(BrokerCode::Ready, pending_ticket_);
}

bool SupervisorBroker::validResult(const Submission &s, const AdminResult &r) noexcept
{
	const bool consumed = r.correlation.supervisor_event_consumed;
	const bool success = r.code == AdminCode::Completed;
	const auto &v = s.expected_view;

	switch (r.session.gate) {
	case SessionGate::NoTrustedSession:
	case SessionGate::Established:
	case SessionGate::Revoked:
	case SessionGate::EstablishmentRequired: break;

	default: return false;
	}

	if (!knownAdminCode(r.code) || !r.correlation.has_supervisor_event
	    || r.correlation.supervisor_event_id != s.event.event_id
	    || r.report_count == 0 || r.report_count > 3 || !r.snapshot.initialized
	    || r.snapshot.state_generation != v.state_generation
	    || (consumed && r.snapshot.terminal)) { return false; }

	if (s.intent == Intent::ClearLatch) {
		if (r.correlation.has_packet || r.correlation.packet_source != 0 || r.correlation.packet_session != 0
		    || r.correlation.packet_operation_sequence != 0) { return false; }

	} else if (!r.correlation.has_packet || r.correlation.packet_source != s.requested_session.owner
		   || r.correlation.packet_session != s.requested_session.session) { return false; }

	// Operation sequence zero is valid. The owner checks exact ingress receipt.
	const uint64_t expected_floor = consumed ? s.event.event_id : v.supervisor_floor;

	if (r.session.has_supervisor_floor != (expected_floor != 0) || r.session.supervisor_floor != expected_floor) {
		return false;
	}

	if (success && (!consumed || v.transition_epoch == kMaximum || !r.action_performed || !r.admission_closed)) {
		return false;
	}

	const uint64_t expected_epoch = success ? v.transition_epoch + kFirst : v.transition_epoch;

	if (r.snapshot.transition_epoch != expected_epoch || r.report_count != (success ? 2 : 1)) { return false; }

	const AdminReport &last = r.reports[r.report_count - 1];

	if (last.code != r.code || !sameCorrelation(last.correlation, r.correlation)
	    || last.report_sequence != r.snapshot.last_report_sequence
	    || last.transition_epoch != r.snapshot.transition_epoch
	    || last.kind != (r.snapshot.terminal ? AdminReportKind::Terminal : AdminReportKind::Operation)) { return false; }

	uint64_t previous = 0;

	for (uint8_t index = 0; index < r.report_count; ++index) {
		const auto &report = r.reports[index];

		if (report.report_sequence == 0 || report.report_sequence <= previous
		    || (!r.snapshot.terminal && report.report_sequence == kMaximum)
		    || report.transition_epoch != expected_epoch || !report.evidence.captured
		    || report.evidence.controller_instance_id != v.controller_instance_id
		    || report.validator_time_us != v.validator_time_us) { return false; }

		previous = report.report_sequence;

		if (index + 1 < r.report_count && (report.kind != AdminReportKind::AdministrativeBarrier
						   || report.code != AdminCode::Observed || !sameCorrelation(report.correlation, AdminCorrelation{}))) { return false; }
	}

	if (success) {
		AdminActionKind action = AdminActionKind::ClearLatch;
		SessionGate gate = SessionGate::EstablishmentRequired;

		if (s.intent == Intent::Establish) { action = AdminActionKind::EstablishTrustedSession; gate = SessionGate::Established; }

		if (s.intent == Intent::Revoke) { action = AdminActionKind::Revoke; gate = SessionGate::Revoked; }

		if (r.action_kind != action || r.session.gate != gate) { return false; }
	}

	const bool established_now = success && s.intent == Intent::Establish;
	const bool expected_history = established_now || v.has_session_history;
	const TrustedSession expected_session = established_now ? s.requested_session : v.last_session;
	return r.session.has_last_successful_session == expected_history
	       && sameSession(r.session.last_successful_session, expected_session);
}

ConsoleResult SupervisorBroker::confirm(uint64_t ticket) noexcept
{
#if defined(RESEARCH_SITL_DIAGNOSTIC_OFFLINE)

	if (domain_ != DiagnosticDomain::Flight) {
		if (in_call_) { return result(BrokerCode::Busy, ticket); }

		if (pending_ && ticket != 0 && ticket == pending_ticket_) { pending_ = false; }

		return result(BrokerCode::ContextChanged, ticket);
	}

#endif

	if (in_call_) { return result(BrokerCode::Busy, ticket); }

	if (config_.maximum_confirmation_age_us == 0) { return result(BrokerCode::NotConfigured, ticket); }

	if (quarantined_) { return result(BrokerCode::Quarantined, ticket); }

	if (!attached_) { return result(BrokerCode::NotAttached, ticket); }

	if (!pending_ || ticket == 0 || ticket != pending_ticket_) { return result(BrokerCode::InvalidTicket, ticket); }

	CallScope scope(in_call_);
	pending_ = false; // Matching confirmations are one-use even on later failure.
	OwnerView view{};
	const BrokerCode code = readView(view);

	if (code != BrokerCode::Ready) { return result(code, ticket); }

	if (!sameContext(view, pending_view_)) { return result(BrokerCode::ContextChanged, ticket); }

	// readView proves monotonicity; no deadline addition can overflow.
	if (view.validator_time_us - pending_view_.validator_time_us > config_.maximum_confirmation_age_us) {
		return result(BrokerCode::Expired, ticket);
	}

	if (event_floor_ == kMaximum) { return quarantine(BrokerCode::CounterExhausted, ticket); }

	Submission submission{};
	submission.intent = pending_intent_;
	submission.requested_session = pending_session_;
	submission.expected_view = view;
	auto &event = submission.event;
	event.event_id = event_floor_ + kFirst;
	event.expected_state_generation = view.state_generation;
	event.expected_transition_epoch = view.transition_epoch;
	event.expected_controller_instance_id = view.controller_instance_id;
	event.action = pending_intent_ == Intent::Establish ? SupervisorAction::Establish :
		       (pending_intent_ == Intent::Revoke ? SupervisorAction::Revoke : SupervisorAction::ClearLatchedFault);
	event.has_session_binding = pending_intent_ != Intent::ClearLatch || view.has_session_history;
	event.session = pending_intent_ == Intent::ClearLatch ? view.last_session : pending_session_;

	AdminResult response{};
	const OwnerOutcome outcome = owner_.submit(submission, response);

	if (outcome == OwnerOutcome::NotSubmitted) { return result(BrokerCode::OwnerDeclined, ticket); }

	if (outcome != OwnerOutcome::Completed) { return quarantine(BrokerCode::OutcomeUnknown, ticket); }

	last_audit_ = response;
	has_audit_ = true;

	if (!validResult(submission, response)) { return quarantine(BrokerCode::InvalidOwnerResult, ticket); }

	if (!response.correlation.supervisor_event_consumed) {
		auto rejected = quarantine(BrokerCode::AdapterUnconsumed, ticket);
		rejected.has_adapter_code = true;
		rejected.adapter_code = response.code;
		return rejected;
	}

	event_floor_ = event.event_id;
	auto submitted = result(BrokerCode::Submitted, ticket);
	submitted.has_adapter_code = true;
	submitted.adapter_code = response.code;
	submitted.event_consumed = true;
	return submitted;
}

bool SupervisorBroker::copyLastAdminResult(AdminResult &output) const noexcept
{
	if (in_call_ || !has_audit_) { return false; }

	output = last_audit_;
	return true;
}
} // namespace research_sitl
