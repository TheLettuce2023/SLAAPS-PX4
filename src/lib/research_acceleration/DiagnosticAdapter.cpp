#include "WireAdapter.hpp"
#include "DiagnosticTypes.hpp"
#include "WireAdapterChecks.hpp"

#include <limits>
#include <type_traits>

namespace research_acceleration
{
namespace
{
using namespace wire_adapter_detail;

template<typename T>
uint64_t diagnosticRunId(const T &value)
{
#if defined(RESEARCH_SITL_DIAGNOSTIC_OFFLINE)
	return value.fixture_run_id;
#else
	return value.native_run_id;
#endif
}

template<typename T>
bool diagnosticOriginValid(const T &value)
{
#if defined(RESEARCH_SITL_DIAGNOSTIC_OFFLINE)
	(void)value;
	return true;
#else
	return value.origin == kDiagnosticExecutionOrigin;
#endif
}

bool validCloseReason(DiagnosticCloseReason reason)
{
	if (reason == DiagnosticCloseReason::OperatorDetach || reason == DiagnosticCloseReason::OperatorLost) { return true; }

#if !defined(RESEARCH_SITL_DIAGNOSTIC_OFFLINE)
	return reason == DiagnosticCloseReason::OwnerStop || reason == DiagnosticCloseReason::SourceContinuityLost
	       || reason == DiagnosticCloseReason::OwnerEvidenceExhausted || reason == DiagnosticCloseReason::OwnerConfigurationInvalid
#if defined(RESEARCH_C1E_NEGATIVE_OWNER_TARGET) || defined(RESEARCH_C1E_NEGATIVE_CONTRACT_TEST)
	       || reason == DiagnosticCloseReason::TransportFault || reason == DiagnosticCloseReason::WorkerResourceExhausted
	       || reason == DiagnosticCloseReason::AdapterCapacityExhausted
#endif
	       ;
#else
	return false;
#endif
}

DiagnosticCause guardCause(const DiagnosticGuardResult &result)
{
	return std::visit([](auto cause) -> DiagnosticCause { return cause; }, result.cause);
}

DiagnosticOperationKind operationKind(const DiagnosticTurn &turn)
{
	// Variant alternatives are all nothrow value types; none owns external state.
	return std::visit([](const auto & operation) {
		using T = std::decay_t<decltype(operation)>;

		if constexpr(std::is_same_v<T, DiagObserve>) { return DiagnosticOperationKind::Observe; }

		else if constexpr(std::is_same_v<T, DiagPoll>) { return DiagnosticOperationKind::Poll; }

		else if constexpr(std::is_same_v<T, DiagAccess>) { return DiagnosticOperationKind::Access; }

		else if constexpr(std::is_same_v<T, DiagEnable>) { return DiagnosticOperationKind::Enable; }

		else if constexpr(std::is_same_v<T, DiagDisable>) { return DiagnosticOperationKind::Disable; }

		else if constexpr(std::is_same_v<T, DiagEstablish>) { return DiagnosticOperationKind::Establish; }

		else if constexpr(std::is_same_v<T, DiagRevoke>) { return DiagnosticOperationKind::Revoke; }

		else if constexpr(std::is_same_v<T, DiagClear>) { return DiagnosticOperationKind::Clear; }

		else if constexpr(std::is_same_v<T, DiagAcknowledge>) { return DiagnosticOperationKind::Acknowledge; }

		else { return DiagnosticOperationKind::Request; }
	}, turn);
}

DiagnosticCause prerequisiteCause(const TrustedStateSample &sample, const ContractConfig &configuration)
{
	const auto &context = sample.request_guard_context;

	if (!context.clock_mapping_valid) { return DecisionCode::ClockMappingInvalid; }

	if (!context.transport_connected) { return DecisionCode::Disconnected; }

	if (!context.required_state_valid) { return DecisionCode::RequiredStateInvalid; }

	if (context.arm_state == ArmState::Unknown) { return DecisionCode::ArmStateUnknown; }

	if (context.mode_state == ResearchModeState::Unknown) { return DecisionCode::ModeStateUnknown; }

	if (context.arm_state != ArmState::Disarmed) { return DiagnosticLocalReason::NotDisarmed; }

	if (context.mode_state != ResearchModeState::NotSelected) { return DiagnosticLocalReason::FlightModeConflict; }

	if (sample.map.source != configuration.permitted_source) { return DiagnosticLocalReason::BindingMismatch; }

	if (sample.map.uncertainty_us > configuration.timing.maximum_clock_uncertainty_us) {
		return DecisionCode::ClockUncertaintyExceeded;
	}

	return DiagnosticLocalReason::None;
}

bool noCause(const DiagnosticCause &cause)
{
	const auto *local = std::get_if<DiagnosticLocalReason>(&cause);
	return local != nullptr && *local == DiagnosticLocalReason::None;
}

bool validBindingShape(const DiagnosticProfileBinding &binding)
{
	return binding.owner_lifetime_id != 0 && diagnosticRunId(binding) != 0 && diagnosticOriginValid(binding)
	       && binding.controller_instance_id != 0
	       && binding.operator_instance != 0 && binding.state_generation != 0 && binding.transition_epoch != 0
	       && binding.selector_epoch != 0;
}
} // namespace

#if defined(RESEARCH_SITL_DIAGNOSTIC_OFFLINE)
WireAdapter::WireAdapter(const ContractConfig &configuration, const AdapterIdentity &identity,
			 DiagnosticOfflineTag, const DiagnosticIdentity &diagnostic_identity) :
	WireAdapter(configuration, identity, DiagnosticConstructionTag{}, diagnostic_identity) {}
#else
WireAdapter::WireAdapter(const ContractConfig &configuration, const AdapterIdentity &identity,
			 DiagnosticNativeSitlTag, const NativeDiagnosticIdentity &diagnostic_identity) :
	WireAdapter(configuration, identity, DiagnosticConstructionTag{}, diagnostic_identity) {}
#endif

WireAdapter::WireAdapter(const ContractConfig &configuration, const AdapterIdentity &identity,
			 DiagnosticConstructionTag, const DiagnosticProfileIdentity &diagnostic_identity) :
	configuration_(configuration), guard_(configuration_, RequestGuard::DiagnosticConstructionTag{}), identity_(identity),
	configured_(identity.controller_instance_id != 0 && configuration.protocol_version == kWireProtocolVersionV1
		    && diagnostic_identity.owner_lifetime_id != 0 && diagnosticRunId(diagnostic_identity) != 0
		    && diagnosticOriginValid(diagnostic_identity) && guard_.configured()),
	diagnostic_domain_(true), diagnostic_identity_(diagnostic_identity)
{
}

DiagnosticSnapshot WireAdapter::diagnosticSnapshot() const noexcept
{
	DiagnosticSnapshot result{};
	result.domain = diagnostic_domain_ ? DiagnosticDomain::SitlDisarmedDiagnostic : DiagnosticDomain::Flight;
	result.identity = diagnostic_identity_;
	result.controller_instance_id = identity_.controller_instance_id;
	result.configured = configured_;
	result.initialized = snapshot_.initialized;
	result.terminal = snapshot_.terminal;
	result.selection = diagnostic_selection_;
	result.selector_epoch = diagnostic_selector_epoch_;
	result.selection_reason = diagnostic_selection_reason_;
	result.operator_attached = diagnostic_operator_attached_;
	result.operator_instance = diagnostic_operator_instance_;
	result.adapter = snapshot_;
	result.session = session_snapshot_;
	result.admission = admission_snapshot_;
	result.latched = diagnostic_latched_;
	result.first_latched_code = diagnostic_first_latched_code_;
	result.rejection_count = diagnostic_rejection_count_;
	result.has_last_accepted_sequence = has_last_accepted_sequence_;
	result.last_accepted_sequence = last_accepted_sequence_;
	result.last_accepted_authenticated = last_accepted_authenticated_;
	return result;
}

#if defined(RESEARCH_C1E_NEGATIVE_OWNER_TARGET) || defined(RESEARCH_C1E_NEGATIVE_CONTRACT_TEST)
bool WireAdapter::nativeNegativeTwoTurnCapacity() const noexcept
{
	// Disabled N1 needs at most one physical barrier and two operation reports.
	// The source owner reserves its own archive separately before either turn.
	return diagnostic_domain_ && configured_ && !snapshot_.terminal
	       && hasNormalReportCapacity(3) && snapshot_.transition_epoch < kTerminalReportSequence;
}
#endif

bool WireAdapter::attachDiagnosticOperator(uint64_t operator_instance) noexcept
{
	if (!diagnostic_domain_ || !configured_ || snapshot_.terminal || diagnostic_operator_attached_
	    || operator_instance == 0 || operator_instance <= diagnostic_operator_instance_
	    || diagnostic_selection_ != DiagnosticSelectionState::Disabled || session_snapshot_.gate == SessionGate::Established) {
		return false;
	}

	diagnostic_operator_instance_ = operator_instance;
	diagnostic_operator_attached_ = true;
	return true;
}

DiagnosticResult WireAdapter::diagnosticInitialResult(DiagnosticCode code) const
{
	DiagnosticResult result{};
	result.code = code;
	result.snapshot = diagnosticSnapshot();
	return result;
}

void WireAdapter::observeDiagnosticGuard(const DiagnosticGuardResult &result)
{
	// Local rejection reasons do not carry a fabricated common Decision. Read
	// the single guard's retained bookkeeping after either result kind.
	(void)result;
	has_diagnostic_decision_ = true;
	diagnostic_latched_ = guard_.latched_;
	diagnostic_first_latched_code_ = guard_.first_latched_code_;
	diagnostic_rejection_count_ = guard_.rejection_count_;
}

bool WireAdapter::matchesDiagnosticBinding(const DiagnosticProfileBinding &binding) const
{
	return binding.owner_lifetime_id == diagnostic_identity_.owner_lifetime_id
	       && diagnosticRunId(binding) == diagnosticRunId(diagnostic_identity_) && diagnosticOriginValid(binding)
	       && binding.controller_instance_id == identity_.controller_instance_id
	       && diagnostic_operator_attached_ && binding.operator_instance == diagnostic_operator_instance_
	       && binding.state_generation == snapshot_.state_generation && binding.transition_epoch == snapshot_.transition_epoch
	       && binding.selector_epoch == diagnostic_selector_epoch_ && sameMap(binding.prospective_map, last_sample_.map);
}

bool WireAdapter::diagnosticIngressIdentity(const DiagnosticProfileIngress &ingress) const
{
	return ingress.owner_lifetime_id == diagnostic_identity_.owner_lifetime_id
	       && diagnosticRunId(ingress) == diagnosticRunId(diagnostic_identity_) && diagnosticOriginValid(ingress);
}

void WireAdapter::appendDiagnosticReport(DiagnosticResult &result, DiagnosticReportKind kind, DiagnosticCode code,
		DiagnosticCause cause, const DiagnosticCorrelation &correlation, const std::optional<Decision> &decision)
{
	// processDiagnostic reserves the whole worst-case turn before any mutation.
	if (kind == DiagnosticReportKind::PhysicalBarrier || kind == DiagnosticReportKind::SelectorBarrier
	    || kind == DiagnosticReportKind::SessionBarrier) {
		++snapshot_.transition_epoch;
		snapshot_.barrier_report_sequence = snapshot_.last_report_sequence + 1U;
		snapshot_.barrier_validator_time_us = last_sample_.request_guard_context.validator_receive_time_us;
	}

	++snapshot_.last_report_sequence;
	DiagnosticReport &record = result.records[result.record_count];
	record.domain = DiagnosticDomain::SitlDisarmedDiagnostic;
	record.identity = diagnostic_identity_;
	record.report_sequence = snapshot_.last_report_sequence;
	record.transition_epoch = snapshot_.transition_epoch;
	record.selector_epoch = diagnostic_selector_epoch_;
	record.kind = kind;
	record.code = code;
	record.cause = cause;
	record.correlation = correlation;
	record.state_reference_valid = snapshot_.initialized;

	if (snapshot_.initialized) { record.physical = last_sample_; }

	record.snapshot = diagnosticSnapshot();
	record.common_decision = decision;
	record.eligible_for_control = false;

	if (kind == DiagnosticReportKind::Operation) {
		result.has_operation_record = true;
		result.operation_record_index = result.record_count;
	}

	++result.record_count;
	result.snapshot = record.snapshot;
}

void WireAdapter::changeDiagnosticSelection(DiagnosticResult &result, DiagnosticSelectionState selection,
		DiagnosticCause cause, const DiagnosticCorrelation &correlation)
{
	if (selection == diagnostic_selection_) { return; }

	guard_.invalidateDiagnosticSession();
	clearAcknowledgement();
	session_snapshot_.gate = SessionGate::EstablishmentRequired;
	diagnostic_selection_ = selection;
	diagnostic_selection_reason_ = cause;
	++diagnostic_selector_epoch_;
	appendDiagnosticReport(result, DiagnosticReportKind::SelectorBarrier,
			       selection == DiagnosticSelectionState::SelectedForValidation ? DiagnosticCode::SelectionEnabled : DiagnosticCode::SelectionDisabled,
			       cause, correlation);
}

DiagnosticResult WireAdapter::diagnosticTerminal(DiagnosticResult result, const DiagnosticCorrelation &correlation,
		DiagnosticCause cause, bool force_sentinel)
{
	guard_.invalidateDiagnosticSession();
	clearAcknowledgement();
	session_snapshot_.gate = SessionGate::Revoked;
	diagnostic_selection_ = DiagnosticSelectionState::Faulted;
	diagnostic_selection_reason_ = cause;
	diagnostic_operator_attached_ = false;
	snapshot_.terminal = true;
	terminal_code_ = force_sentinel ? AdapterCode::CounterExhausted : AdapterCode::TrustedStateFault;
	admin_terminal_code_ = force_sentinel ? AdminCode::CounterExhausted : AdminCode::TrustedStateFault;

	if (force_sentinel || !hasNormalReportCapacity(1)) {
		// Sentinel is deliberately outside the normal stream; do not wrap.
		snapshot_.last_report_sequence = std::numeric_limits<uint64_t>::max() - 1U;
	}

	appendDiagnosticReport(result, DiagnosticReportKind::Terminal, DiagnosticCode::Terminal, cause, correlation);
	result.code = DiagnosticCode::Terminal;
	result.action_performed = false;
	return result;
}

DiagnosticResult WireAdapter::diagnosticTurn(const TrustedStateSample &physical, const DiagnosticTurn &operation) noexcept
{
	return processDiagnostic(&physical, &operation, std::nullopt);
}

DiagnosticResult WireAdapter::closeDiagnosticLifecycle(const std::optional<TrustedStateSample> &receipt,
		DiagnosticCloseReason reason) noexcept
{
	return processDiagnostic(receipt ? &*receipt : nullptr, nullptr, reason);
}

DiagnosticResult WireAdapter::processDiagnostic(const TrustedStateSample *physical, const DiagnosticTurn *operation,
		const std::optional<DiagnosticCloseReason> &close_reason) noexcept
{
	if (!diagnostic_domain_) { return diagnosticInitialResult(DiagnosticCode::WrongDomain); }

	if (!configured_) { return diagnosticInitialResult(DiagnosticCode::NotConfigured); }

	if (snapshot_.terminal) { return diagnosticInitialResult(DiagnosticCode::Terminal); }

	DiagnosticResult result{};
	DiagnosticCorrelation correlation{};
	correlation.operation = operation != nullptr ? operationKind(*operation) : DiagnosticOperationKind::LifecycleClose;
	correlation.close_reason = close_reason;
	correlation.operator_instance = diagnostic_operator_instance_;
	const DiagnosticSelectionGrant *selection_grant = nullptr;
	const DiagnosticAdminGrant *admin_grant = nullptr;
	const DecodedSessionV1 *packet = nullptr;
	const DiagRequest *request = nullptr;
	const DiagnosticProfileIngress *ingress = nullptr;

	if (operation != nullptr) {
		if (const auto *enable = std::get_if<DiagEnable>(operation)) { selection_grant = &enable->grant; }

		else if (const auto *disable = std::get_if<DiagDisable>(operation)) { selection_grant = &disable->grant; }

		else if (const auto *establish = std::get_if<DiagEstablish>(operation)) { admin_grant = &establish->grant; packet = &establish->packet; ingress = &establish->ingress; }

		else if (const auto *revoke = std::get_if<DiagRevoke>(operation)) { admin_grant = &revoke->grant; packet = &revoke->packet; ingress = &revoke->ingress; }

		else if (const auto *clear = std::get_if<DiagClear>(operation)) { admin_grant = &clear->grant; }

		else if (const auto *acknowledge = std::get_if<DiagAcknowledge>(operation)) { packet = &acknowledge->packet; ingress = &acknowledge->ingress; }

		else if (const auto *request_operation = std::get_if<DiagRequest>(operation)) {
			request = request_operation; ingress = &request_operation->ingress;
			correlation.ingress = {IngressPacketKind::Request, request_operation->packet.wire.source_id, request_operation->packet.wire.session_id,
					       request_operation->packet.wire.controller_instance_id, request_operation->packet.wire.transition_epoch,
					       request_operation->packet.wire.clock_mapping_epoch, request_operation->packet.wire.sequence, 0
					      };
		}
	}

	if (packet != nullptr) {
		if (admin_grant != nullptr) {
			correlation.admin.has_packet = true;
			correlation.admin.packet_source = packet->source;
			correlation.admin.packet_session = packet->session;
			correlation.admin.packet_operation_sequence = packet->operation_sequence;

		} else {
			correlation.ingress = {IngressPacketKind::Acknowledgement, packet->source, packet->session,
					       packet->controller_instance_id, packet->transition_epoch, packet->clock_mapping_epoch,
					       packet->operation_sequence, packet->acknowledged_status_sequence
					      };
		}
	}

	if (ingress != nullptr) { correlation.authentication = ingress->authentication; }

	const DiagnosticProfileBinding *binding = selection_grant != nullptr ? &selection_grant->expected
			: (admin_grant != nullptr ? &admin_grant->expected : nullptr);
	const uint64_t event_id = selection_grant != nullptr ? selection_grant->event_id
				  : (admin_grant != nullptr ? admin_grant->event.event_id : 0);

	if (binding != nullptr) {
		correlation.operator_instance = binding->operator_instance;
		correlation.admin.has_supervisor_event = true;
		correlation.admin.supervisor_event_id = event_id;
	}

#if !defined(RESEARCH_SITL_DIAGNOSTIC_OFFLINE)

	if (close_reason && (*close_reason == DiagnosticCloseReason::SourceContinuityLost
			     || *close_reason == DiagnosticCloseReason::OwnerEvidenceExhausted
			     || *close_reason == DiagnosticCloseReason::OwnerConfigurationInvalid
#if defined(RESEARCH_C1E_NEGATIVE_OWNER_TARGET) || defined(RESEARCH_C1E_NEGATIVE_CONTRACT_TEST)
			     || *close_reason == DiagnosticCloseReason::TransportFault
			     || *close_reason == DiagnosticCloseReason::WorkerResourceExhausted
			     || *close_reason == DiagnosticCloseReason::AdapterCapacityExhausted
#endif
			    )) {
		return diagnosticTerminal(result, correlation, DiagnosticLocalReason::TrustedStateFault, false);
	}

#endif

	if (physical == nullptr || !structurallyValid(*physical) || (close_reason && !validCloseReason(*close_reason))) {
		return diagnosticTerminal(result, correlation, DiagnosticLocalReason::TrustedStateFault, false);
	}

	const bool first = !snapshot_.initialized;
	const bool physical_barrier = first || physical->generation != snapshot_.state_generation;

	if (!first && ((!physical_barrier && !matchesRetainedState(*physical))
		       || (physical_barrier && (snapshot_.state_generation == kTerminalReportSequence
						|| physical->generation != snapshot_.state_generation + 1U)))) {
		return diagnosticTerminal(result, correlation, DiagnosticLocalReason::TrustedStateFault, false);
	}

	// Reserve the whole worst-case turn before poll/evaluate consumes history.
	const bool may_select = diagnostic_selection_ == DiagnosticSelectionState::SelectedForValidation
				|| correlation.operation == DiagnosticOperationKind::Enable;
	const uint8_t barriers = static_cast<uint8_t>(physical_barrier) + static_cast<uint8_t>(may_select)
				 + static_cast<uint8_t>(admin_grant != nullptr);
	const uint8_t records = 1U + barriers;

	if (!hasNormalReportCapacity(records) || snapshot_.transition_epoch > kTerminalReportSequence - barriers
	    || (may_select && diagnostic_selector_epoch_ == kTerminalReportSequence)
	    || (binding != nullptr && session_snapshot_.has_supervisor_floor
		&& session_snapshot_.supervisor_floor == kTerminalReportSequence)) {
		return diagnosticTerminal(result, correlation, DiagnosticLocalReason::CounterExhausted, true);
	}

	const bool map_changed = !first && !sameMap(physical->map, last_sample_.map);
	last_sample_ = *physical;

	if (physical_barrier) {
		clearAcknowledgement();
		snapshot_.initialized = true;
		snapshot_.state_generation = physical->generation;

		if (first) { diagnostic_selector_epoch_ = 1; }

		appendDiagnosticReport(result, DiagnosticReportKind::PhysicalBarrier, DiagnosticCode::Observed,
				       DiagnosticLocalReason::None, correlation);
	}

	bool preclosed = false;
	DiagnosticCause safety = prerequisiteCause(*physical, configuration_);

	if (diagnostic_latched_) { safety = DecisionCode::Latched; }

	else if (map_changed) { safety = DiagnosticLocalReason::BindingMismatch; }

	else if (!diagnostic_operator_attached_ && diagnostic_selection_ == DiagnosticSelectionState::SelectedForValidation) {
		safety = DiagnosticLocalReason::GrantInvalid;
	}

	if (diagnostic_selection_ == DiagnosticSelectionState::SelectedForValidation && !noCause(safety)) {
		changeDiagnosticSelection(result, DiagnosticSelectionState::Disabled, safety, correlation);
		preclosed = true;
	}

	const DiagnosticGuardResult prestate = guard_.pollDiagnostic(physical->request_guard_context, diagnostic_selection_);
	observeDiagnosticGuard(prestate);

	if (diagnostic_latched_ && diagnostic_selection_ == DiagnosticSelectionState::SelectedForValidation) {
		changeDiagnosticSelection(result, DiagnosticSelectionState::Disabled, guardCause(prestate), correlation);
		preclosed = true;
	}

	DiagnosticCode code = DiagnosticCode::Rejected;
	DiagnosticCause cause = DiagnosticLocalReason::None;
	std::optional<Decision> operation_decision;
	bool ready = true;
	bool close_after = false;

	if (binding != nullptr) {
		const bool selection_shape = selection_grant == nullptr
					     || selection_grant->action == DiagnosticAction::Enable || selection_grant->action == DiagnosticAction::Disable;

		if (event_id == 0 || !validBindingShape(*binding) || !selection_shape
		    || (admin_grant != nullptr && !validSupervisorShape(admin_grant->event))
		    || (!session_snapshot_.has_supervisor_floor && event_id != 1)
		    || (session_snapshot_.has_supervisor_floor && session_snapshot_.supervisor_floor != kTerminalReportSequence
			&& event_id > session_snapshot_.supervisor_floor + 1U)) {
			return diagnosticTerminal(result, correlation, AdminCode::SupervisorStreamFault, false);
		}

		const bool selection_action = selection_grant == nullptr
					      || (correlation.operation == DiagnosticOperationKind::Enable && selection_grant->action == DiagnosticAction::Enable)
					      || (correlation.operation == DiagnosticOperationKind::Disable && selection_grant->action == DiagnosticAction::Disable);
		const bool admin_action = admin_grant == nullptr
					  || (correlation.operation == DiagnosticOperationKind::Establish && admin_grant->event.action == SupervisorAction::Establish)
					  || (correlation.operation == DiagnosticOperationKind::Revoke && admin_grant->event.action == SupervisorAction::Revoke)
					  || (correlation.operation == DiagnosticOperationKind::Clear && admin_grant->event.action == SupervisorAction::ClearLatchedFault);

		if (session_snapshot_.has_supervisor_floor && event_id <= session_snapshot_.supervisor_floor) {
			cause = admin_grant != nullptr ? DiagnosticCause{AdminCode::SupervisorDuplicate}
				: DiagnosticCause{DiagnosticLocalReason::GrantReplay};
			ready = false;

		} else if (!matchesDiagnosticBinding(*binding) || !selection_action || !admin_action
			   || (admin_grant != nullptr && (admin_grant->event.expected_controller_instance_id != identity_.controller_instance_id
					   || admin_grant->event.expected_state_generation != snapshot_.state_generation
					   || admin_grant->event.expected_transition_epoch != snapshot_.transition_epoch))) {
			cause = DiagnosticLocalReason::BindingMismatch;
			ready = false;

		} else {
			session_snapshot_.has_supervisor_floor = true;
			session_snapshot_.supervisor_floor = event_id;
			correlation.admin.supervisor_event_consumed = true;
			result.grant_consumed = true;
		}
	}

	if (ready && selection_grant != nullptr) {
		if (correlation.operation == DiagnosticOperationKind::Disable) {
			const bool already = diagnostic_selection_ == DiagnosticSelectionState::Disabled;
			changeDiagnosticSelection(result, DiagnosticSelectionState::Disabled, DiagnosticLocalReason::None, correlation);
			code = already ? DiagnosticCode::AlreadyDisabled : DiagnosticCode::SelectionDisabled;
			result.action_performed = !already;

		} else if (preclosed || diagnostic_latched_ || !noCause(prerequisiteCause(*physical, configuration_))) {
			cause = preclosed ? DiagnosticCause{DiagnosticLocalReason::ContextChanged}
				: (diagnostic_latched_ ? DiagnosticCause{DecisionCode::Latched} : prerequisiteCause(*physical, configuration_));

		} else if (diagnostic_selection_ != DiagnosticSelectionState::Disabled) {
			cause = DiagnosticLocalReason::ContextChanged;

		} else {
			changeDiagnosticSelection(result, DiagnosticSelectionState::SelectedForValidation, DiagnosticLocalReason::None, correlation);
			code = DiagnosticCode::SelectionEnabled;
			result.action_performed = true;
		}

	} else if (ready && admin_grant != nullptr) {
		const SupervisorEvent &event = admin_grant->event;

		if (packet != nullptr) {
			// Diagnostic administration fixes unused ACK fields to zero and
			// requires a nonzero operation ID; ordinary Flight parsing is unchanged.
			if (!validSessionPacket(*packet) || packet->operation_sequence == 0
			    || packet->producer_acknowledgement_monotonic_us != 0 || packet->acknowledged_status_sequence != 0) {
				cause = AdminCode::MalformedPacket;

			} else if (ingress == nullptr || !diagnosticIngressIdentity(*ingress) || !validIngress(ingress->authentication)) {
				cause = AdminCode::IngressRejected;

			} else if ((correlation.operation == DiagnosticOperationKind::Establish && packet->operation != kSessionOperationEstablish)
				   || (correlation.operation == DiagnosticOperationKind::Revoke && packet->operation != kSessionOperationRevoke)) {
				cause = AdminCode::UnsupportedOperation;

			} else if (session_snapshot_.has_wire_operation_floor && packet->operation_sequence <= session_snapshot_.wire_operation_floor) {
				cause = AdminCode::PacketOperationReplay;

			} else if (!event.has_session_binding || event.session.owner != packet->source || event.session.session != packet->session
				   || packet->controller_instance_id != identity_.controller_instance_id
				   || packet->transition_epoch != snapshot_.transition_epoch || packet->clock_mapping_epoch != physical->map.map_epoch
				   || packet->source != configuration_.permitted_source || physical->map.source != packet->source
				   || physical->map.session != packet->session) {
				cause = AdminCode::BindingMismatch;

			} else if (correlation.operation == DiagnosticOperationKind::Establish) {
				if (diagnostic_selection_ != DiagnosticSelectionState::SelectedForValidation || diagnostic_latched_
				    || !noCause(prerequisiteCause(*physical, configuration_))
				    || (session_snapshot_.has_last_successful_session && packet->session == session_snapshot_.last_successful_session.session)) {
					cause = AdminCode::EstablishmentContextRejected;

				} else {
					const auto action = guard_.establishDiagnosticSession({packet->source, packet->session}, true,
							    physical->request_guard_context, diagnostic_selection_);
					observeDiagnosticGuard(action);
					operation_decision = action.common_decision;
					cause = guardCause(action);
					code = action.code;

					if (code == DiagnosticCode::SessionEstablished) {
						session_snapshot_.gate = SessionGate::Established;
						session_snapshot_.has_last_successful_session = true;
						session_snapshot_.last_successful_session = {packet->source, packet->session};
						session_snapshot_.has_wire_operation_floor = true;
						session_snapshot_.wire_operation_floor = packet->operation_sequence;
						result.action_performed = true;
					}
				}

			} else if (session_snapshot_.gate != SessionGate::Established) { cause = AdminCode::NoEstablishedSession; }

			else if (!session_snapshot_.has_last_successful_session
				 || !sameSession({packet->source, packet->session}, session_snapshot_.last_successful_session)) {
				cause = AdminCode::BindingMismatch;

			} else if (physical->request_guard_context.arm_state != ArmState::Disarmed) { cause = AdminCode::ContextRejected; }

			else {
				guard_.invalidateDiagnosticSession();
				session_snapshot_.gate = SessionGate::Revoked;
				session_snapshot_.has_wire_operation_floor = true;
				session_snapshot_.wire_operation_floor = packet->operation_sequence;
				code = DiagnosticCode::SessionRevoked;
				cause = AdminCode::Completed;
				result.action_performed = true;
				close_after = true;
			}

		} else {
			const bool session_binding = session_snapshot_.has_last_successful_session
						     ? event.has_session_binding && sameSession(event.session, session_snapshot_.last_successful_session)
						     : !event.has_session_binding;

			if (!session_binding) { cause = AdminCode::BindingMismatch; }

			else if (physical->request_guard_context.arm_state != ArmState::Disarmed) { cause = AdminCode::ContextRejected; }

			else if (!diagnostic_latched_) { cause = AdminCode::ClearNotLatched; }

			else {
				const auto action = guard_.clearDiagnosticLatch(true, physical->request_guard_context.arm_state);
				observeDiagnosticGuard(action);
				operation_decision = action.common_decision;
				cause = guardCause(action);
				code = action.code;

				if (code == DiagnosticCode::LatchCleared) {
					session_snapshot_.gate = SessionGate::EstablishmentRequired;
					result.action_performed = true;
					close_after = true;
				}
			}
		}

		if (result.action_performed) {
			clearAcknowledgement();
			appendDiagnosticReport(result, DiagnosticReportKind::SessionBarrier, DiagnosticCode::Observed,
					       DiagnosticLocalReason::None, correlation);
		}

	} else if (ready && close_reason) {
		const bool already = diagnostic_selection_ == DiagnosticSelectionState::Disabled;
		changeDiagnosticSelection(result, DiagnosticSelectionState::Disabled, DiagnosticLocalReason::None, correlation);
		guard_.invalidateDiagnosticSession();
		clearAcknowledgement();
		session_snapshot_.gate = SessionGate::EstablishmentRequired;
		diagnostic_operator_attached_ = false;
		code = already ? DiagnosticCode::AlreadyDisabled : DiagnosticCode::SelectionDisabled;
		result.action_performed = true;

	} else if (ready && (packet != nullptr || request != nullptr)) {
		if (preclosed || diagnostic_selection_ != DiagnosticSelectionState::SelectedForValidation
		    || diagnostic_latched_ || !noCause(prerequisiteCause(*physical, configuration_))) {
			cause = preclosed ? DiagnosticCause{DiagnosticLocalReason::ContextChanged}
				: (diagnostic_latched_ ? DiagnosticCause{DecisionCode::Latched}
				   : DiagnosticCause{DiagnosticLocalReason::SelectionRequired});

		} else if (ingress == nullptr || !diagnosticIngressIdentity(*ingress)) { cause = IngressCode::IngressRejected; }

		else if (packet != nullptr) {
			const IngressCode admitted = admitAcknowledgement(*physical, *packet, ingress->authentication);
			cause = admitted;
			code = admitted == IngressCode::Completed ? DiagnosticCode::Acknowledged : DiagnosticCode::Rejected;
			result.action_performed = admitted == IngressCode::Completed;

		} else {
			CanonicalRequest canonical{};
			const IngressCode prepared = prepareRequest(*physical, request->packet, ingress->authentication, canonical);
			cause = prepared;

			if (prepared == IngressCode::Completed) {
				const auto evaluated = guard_.evaluateDiagnostic(canonical, physical->request_guard_context, diagnostic_selection_);
				observeDiagnosticGuard(evaluated);
				code = evaluated.code;
				cause = guardCause(evaluated);
				operation_decision = evaluated.common_decision;
				result.action_performed = true; // Actual evaluation, even if rejected.
				close_after = diagnostic_latched_;
			}
		}

	} else if (ready) {
		// Healthy Disabled state is observable, but never request readiness.
		const auto *local = std::get_if<DiagnosticLocalReason>(&prestate.cause);
		const bool disabled_only = local != nullptr && *local == DiagnosticLocalReason::SelectionRequired;

		if (prestate.code == DiagnosticCode::NoEligibleRequest || disabled_only) {
			code = correlation.operation == DiagnosticOperationKind::Observe ? DiagnosticCode::Observed : DiagnosticCode::NoEligibleRequest;
		}

		cause = guardCause(prestate);
		operation_decision = prestate.common_decision;
	}

	appendDiagnosticReport(result, DiagnosticReportKind::Operation, code, cause, correlation, operation_decision);

	if (close_after) { changeDiagnosticSelection(result, DiagnosticSelectionState::Disabled, cause, correlation); }

	result.code = code;
	result.snapshot = diagnosticSnapshot();
	return result;
}

} // namespace research_acceleration
