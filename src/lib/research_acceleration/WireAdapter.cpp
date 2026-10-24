#include "WireAdapter.hpp"
#include "WireAdapterChecks.hpp"

#include <stdint.h>

namespace research_acceleration
{
using namespace wire_adapter_detail;

WireAdapter::WireAdapter(const ContractConfig &configuration, const AdapterIdentity &identity) :
	configuration_(configuration),
	guard_(configuration_),
	identity_(identity),
	configured_(identity_.controller_instance_id != 0 && configuration_.protocol_version == kWireProtocolVersionV1
		    && guard_.configured())
{
}

bool WireAdapter::configured() const
{
	return configured_;
}

bool WireAdapter::acknowledgementCurrent() const
{
	return snapshot_.initialized && !snapshot_.terminal && admission_snapshot_.acknowledgement_current
	       && session_snapshot_.gate == SessionGate::Established && session_snapshot_.has_last_successful_session
	       && admission_snapshot_.source == configuration_.permitted_source
	       && admission_snapshot_.source == last_sample_.map.source
	       && admission_snapshot_.source == session_snapshot_.last_successful_session.owner
	       && admission_snapshot_.session == session_snapshot_.last_successful_session.session
	       && admission_snapshot_.session == last_sample_.map.session
	       && admission_snapshot_.controller_instance_id == identity_.controller_instance_id
	       && admission_snapshot_.controller_instance_id == last_sample_.map.controller_instance_id
	       && admission_snapshot_.transition_epoch == snapshot_.transition_epoch
	       && admission_snapshot_.map_epoch == last_sample_.map.map_epoch
	       && sameMap(admission_snapshot_.map, last_sample_.map);
}

void WireAdapter::clearAcknowledgement()
{
	admission_snapshot_ = {};
}

void WireAdapter::observeDecisionEvidence(const Decision &decision)
{
	has_diagnostic_decision_ = true;
	diagnostic_latched_ = decision.latched;
	diagnostic_first_latched_code_ = decision.first_latched_code;
	diagnostic_rejection_count_ = decision.rejection_count;
}

ReportEvidence WireAdapter::captureEvidence(const TrustedStateSample *sample, bool state_reference_valid,
		StatusDecisionOrigin origin, uint16_t local_wire_decision_code,
		const Decision *operation_decision, const SessionIngressVerdict *ingress) const
{
	ReportEvidence evidence{};
	evidence.captured = true;
	evidence.controller_instance_id = identity_.controller_instance_id;
	evidence.state_reference_valid = state_reference_valid;
	evidence.session_gate = session_snapshot_.gate;
	evidence.trusted_source = session_snapshot_.has_last_successful_session ? session_snapshot_.last_successful_session.owner : 0;
	evidence.trusted_session = session_snapshot_.has_last_successful_session ? session_snapshot_.last_successful_session.session : 0;
	evidence.acknowledgement_current = acknowledgementCurrent();
	evidence.has_last_accepted_sequence = has_last_accepted_sequence_;
	evidence.last_accepted_sequence = has_last_accepted_sequence_ ? last_accepted_sequence_ : 0;
	evidence.last_accepted_authenticated = last_accepted_authenticated_;
	evidence.latched = diagnostic_latched_;
	evidence.first_latched_code = diagnostic_latched_ ? diagnostic_first_latched_code_ : DecisionCode::Accepted;
	evidence.rejection_count = has_diagnostic_decision_ ? diagnostic_rejection_count_ : 0;
	evidence.decision_origin = origin;
	evidence.local_wire_decision_code = local_wire_decision_code;

	if (sample != nullptr && state_reference_valid) {
		evidence.clock_mapping_epoch = sample->map.valid ? sample->map.map_epoch : 0;
		evidence.arm_state = sample->request_guard_context.arm_state;
		evidence.mode_state = sample->request_guard_context.mode_state;
		evidence.transport_connected = sample->request_guard_context.transport_connected;
		evidence.clock_mapping_valid = sample->request_guard_context.clock_mapping_valid;

	} else {
		evidence.clock_mapping_epoch = 0;
		evidence.arm_state = ArmState::Unknown;
		evidence.mode_state = ResearchModeState::Unknown;
		evidence.transport_connected = false;
		evidence.clock_mapping_valid = false;
	}

	if (ingress != nullptr) {
		evidence.packet_mavlink2 = ingress->mavlink2;
		evidence.packet_parser_authenticated = ingress->parser_authenticated;
		evidence.packet_header_source_bound = ingress->configured_header_channel_source_binding;
		evidence.packet_target_bound = ingress->configured_target_binding;
	}

	if (origin == StatusDecisionOrigin::S1 && operation_decision != nullptr) {
		evidence.syntactically_valid = operation_decision->syntactically_valid;
		evidence.conservative_age_valid = operation_decision->conservative_age_valid;
		evidence.conservative_age_us = operation_decision->conservative_age_valid
					       ? operation_decision->conservative_age_us : 0;
	}

	return evidence;
}

uint16_t WireAdapter::adapterLocalWireCode(AdapterCode code) const
{
	switch (code) {
	case AdapterCode::TrustedStateFault:
		return 79;

	case AdapterCode::CounterExhausted:
		return 78;

	case AdapterCode::NotConfigured:
		return 3;

	default:
		return 0;
	}
}

uint16_t WireAdapter::adminLocalWireCode(AdminCode code, const TrustedStateSample &sample,
		const DecodedSessionV1 *packet, const SupervisorEvent *grant,
		const SessionIngressVerdict *ingress) const
{
	if (code == AdminCode::IngressRejected && ingress != nullptr) {
		if (!ingress->mavlink2) { return 64; }

		if (!ingress->parser_authenticated) { return 65; }

		if (!ingress->configured_header_channel_source_binding) { return 5; }

		return !ingress->configured_target_binding ? 66 : 0;
	}

	if (code == AdminCode::BindingMismatch) {
		if (grant != nullptr) {
			if (grant->expected_controller_instance_id != identity_.controller_instance_id) { return 68; }

			if (grant->expected_state_generation != snapshot_.state_generation) { return 79; }

			if (grant->expected_transition_epoch != snapshot_.transition_epoch) { return 69; }

			if (packet != nullptr && (!grant->has_session_binding
						  || (packet->operation == kSessionOperationEstablish
						      && grant->action != SupervisorAction::Establish)
						  || (packet->operation == kSessionOperationRevoke
						      && grant->action != SupervisorAction::Revoke))) { return 8; }

			if (packet != nullptr) {
				if (grant->session.owner != packet->source) { return 5; }

				if (grant->session.session != packet->session) { return 7; }

			} else {
				if (grant->action != SupervisorAction::ClearLatchedFault) { return 8; }

				if (!session_snapshot_.has_last_successful_session) {
					if (grant->has_session_binding) { return 10; }

				} else {
					if (!grant->has_session_binding) { return 8; }

					if (grant->session.owner != session_snapshot_.last_successful_session.owner) { return 5; }

					if (grant->session.session != session_snapshot_.last_successful_session.session) { return 7; }
				}
			}
		}

		if (packet != nullptr) {
			if (packet->controller_instance_id != identity_.controller_instance_id) { return 68; }

			if (packet->transition_epoch != snapshot_.transition_epoch) { return 69; }

			if (packet->clock_mapping_epoch != sample.map.map_epoch) { return 72; }

			if (packet->source != configuration_.permitted_source) { return 5; }

			if (session_snapshot_.has_last_successful_session
			    && !sameSession({packet->source, packet->session}, session_snapshot_.last_successful_session)) { return 7; }

			if (sample.map.source != packet->source || sample.map.session != packet->session
			    || sample.map.controller_instance_id != identity_.controller_instance_id) { return 81; }
		}

		return 0;
	}

	if (code == AdminCode::EstablishmentContextRejected && packet != nullptr) {
		if (session_snapshot_.has_last_successful_session
		    && packet->session == session_snapshot_.last_successful_session.session) { return 7; }

		if (sample.request_guard_context.arm_state == ArmState::Unknown) { return 33; }

		if (sample.request_guard_context.arm_state == ArmState::Armed) { return 9; }

		if (!sample.map.valid) { return 15; }

		if (!sample.request_guard_context.transport_connected) { return 35; }

		if (!sample.request_guard_context.required_state_valid) { return 24; }

		if (sample.request_guard_context.mode_state == ResearchModeState::Unknown) { return 34; }

		return diagnostic_latched_ ? 37 : 0;
	}

	if (code == AdminCode::ContextRejected) {
		return sample.request_guard_context.arm_state == ArmState::Unknown ? 33 : 9;
	}

	if (code == AdminCode::NoEstablishedSession) {
		switch (session_snapshot_.gate) {
		case SessionGate::NoTrustedSession: return 6;

		case SessionGate::Revoked: return 76;

		case SessionGate::EstablishmentRequired: return 11;

		case SessionGate::Established: break;
		}
	}

	switch (code) {
	case AdminCode::Completed:
		return 1;

	case AdminCode::NotConfigured:
		return 3;

	case AdminCode::TrustedStateFault:
	case AdminCode::SupervisorStreamFault:
		return 79;

	case AdminCode::CounterExhausted:
		return 78;

	case AdminCode::SupervisorMissing:
		return 8;

	case AdminCode::SupervisorDuplicate:
	case AdminCode::PacketOperationReplay:
		return 75;

	case AdminCode::MalformedPacket:
		return 74;

	case AdminCode::UnsupportedOperation:
		return 73;

	case AdminCode::ClearNotLatched:
		return 0;

	default:
		return 0;
	}
}

uint16_t WireAdapter::ingressLocalWireCode(IngressCode code, const TrustedStateSample &sample,
		const DecodedSessionV1 *acknowledgement, const DecodedRequestV1 *request,
		const SessionIngressVerdict &ingress) const
{
	switch (code) {
	case IngressCode::Completed:
		return 1;

	case IngressCode::NotConfigured:
		return 3;

	case IngressCode::TrustedStateFault:
		return 79;

	case IngressCode::CounterExhausted:
		return 78;

	case IngressCode::MalformedPacket:
		return 74;

	case IngressCode::UnsupportedSessionOperation:
		return 73;

	case IngressCode::WireOperationReplay:
		return 75;

	case IngressCode::AcknowledgementRequired:
		return 70;

	case IngressCode::AcknowledgementFrozen:
	case IngressCode::AcknowledgementOrdering:
		return 71;

	case IngressCode::AcknowledgementStatusMismatch:
		return 80;

	case IngressCode::MapInvalid:
		return 15;

	case IngressCode::MapIntervalInvalid:
		return 77;

	case IngressCode::MapArithmeticInvalid:
		return 82;

	case IngressCode::WireVersionInvalid:
		return 4;

	case IngressCode::WireNonFinite:
		return 28;

	case IngressCode::IngressRejected:
		if (!ingress.mavlink2) { return 64; }

		if (!ingress.parser_authenticated) { return 65; }

		if (!ingress.configured_header_channel_source_binding) { return 5; }

		return !ingress.configured_target_binding ? 66 : 0;

	case IngressCode::SessionGateRejected:
		switch (session_snapshot_.gate) {
		case SessionGate::NoTrustedSession: return 6;

		case SessionGate::Revoked: return 76;

		case SessionGate::EstablishmentRequired: return 11;

		case SessionGate::Established: return 0;
		}

		break;

	case IngressCode::BindingMismatch: {
			const uint64_t controller = acknowledgement != nullptr ? acknowledgement->controller_instance_id
						    : request->wire.controller_instance_id;
			const uint64_t epoch = acknowledgement != nullptr ? acknowledgement->transition_epoch : request->wire.transition_epoch;
			const uint64_t map_epoch = acknowledgement != nullptr ? acknowledgement->clock_mapping_epoch
						   : request->wire.clock_mapping_epoch;
			const SourceId source = acknowledgement != nullptr ? acknowledgement->source : request->wire.source_id;
			const SessionId session = acknowledgement != nullptr ? acknowledgement->session : request->wire.session_id;

			if (controller != identity_.controller_instance_id) { return 68; }

			if (epoch != snapshot_.transition_epoch) { return 69; }

			if (map_epoch != sample.map.map_epoch) { return 72; }

			if (source != configuration_.permitted_source) { return 5; }

			if (session_snapshot_.has_last_successful_session
			    && !sameSession({source, session}, session_snapshot_.last_successful_session)) { return 7; }

			return 81;
		}

	case IngressCode::AcknowledgementTimingInvalid: {
			TimeUs mapped{};
			TimeUs earliest{};
			TimeUs latest{};
			TimeUs future_limit{};
			TimeUs age{};

			if (acknowledgement == nullptr
			    || !addSignedOffset(acknowledgement->producer_acknowledgement_monotonic_us,
						sample.map.validator_minus_producer_us, mapped)
			    || !checkedSubtract(mapped, sample.map.uncertainty_us, earliest)
			    || !checkedAdd(mapped, sample.map.uncertainty_us, latest)
			    || !checkedAdd(sample.request_guard_context.validator_receive_time_us,
					   configuration_.timing.maximum_future_skew_us, future_limit)
			    || !checkedSubtract(sample.request_guard_context.validator_receive_time_us, earliest, age)) { return 23; }

			if (sample.map.uncertainty_us > configuration_.timing.maximum_clock_uncertainty_us) { return 16; }

			if (earliest > sample.request_guard_context.validator_receive_time_us) { return 17; }

			if (age > configuration_.timing.maximum_command_age_us) { return 20; }

			return latest > future_limit ? 17 : 0;
		}

	case IngressCode::WireEnumInvalid:
		if (request == nullptr) { return 0; }

		if (request->wire.frame != 1) { return 25; }

		if (request->wire.quantity != 1) { return 26; }

		return request->wire.reference_point != 1 ? 27 : 0;

	case IngressCode::WireTimeInvalid:
		return request != nullptr && request->wire.producer_expiry_monotonic_us
		       <= request->wire.producer_monotonic_us ? 21 : 23;

	default:
		return 0;
	}

	return 0;
}

AdapterResult WireAdapter::initialResult(AdapterCode code) const
{
	AdapterResult result{};
	result.code = code;
	result.admission_closed = !acknowledgementCurrent();
	result.snapshot = snapshot_;
	return result;
}

bool WireAdapter::structurallyValid(const TrustedStateSample &sample) const
{
	if (sample.generation == 0 || !sample.observer_continuous
	    || !validArmState(sample.request_guard_context.arm_state)
	    || !validModeState(sample.request_guard_context.mode_state)
	    || sample.request_guard_context.clock_mapping_valid != sample.map.valid
	    || sample.map.controller_instance_id != identity_.controller_instance_id) {
		return false;
	}

	if (!sample.map.valid) {
		return true;
	}

	return sample.map.source != 0 && sample.map.session != 0 && sample.map.map_epoch != 0
	       && sample.map.uncertainty_us != 0
	       && sample.map.valid_from_producer_us <= sample.map.valid_until_producer_us;
}

bool WireAdapter::matchesRetainedState(const TrustedStateSample &sample) const
{
	return sameContextExceptValidatorTime(sample.request_guard_context, last_sample_.request_guard_context)
	       && sameMap(sample.map, last_sample_.map);
}

bool WireAdapter::hasNormalReportCapacity(uint8_t count) const
{
	return snapshot_.last_report_sequence <= kMaximumNormalReportSequence
	       && count <= kMaximumNormalReportSequence - snapshot_.last_report_sequence;
}

AdapterResult WireAdapter::terminalResult(AdapterCode code, TimeUs validator_time_us, bool allow_normal_report)
{
	guard_.revoke();
	clearAcknowledgement();
	snapshot_.terminal = true;
	terminal_code_ = code;

	AdapterResult result{};
	result.admission_closed = true;
	AdapterReport &report = result.reports[0];
	report.transition_epoch = snapshot_.transition_epoch;
	report.validator_time_us = validator_time_us;
	report.kind = AdapterReportKind::Terminal;
	report.has_s1_decision = false;

	if (allow_normal_report && hasNormalReportCapacity(kOperationReportCount)) {
		report.report_sequence = snapshot_.last_report_sequence + kFirstUsableCounterValue;
		report.code = code;
		snapshot_.last_report_sequence = report.report_sequence;
		result.code = code;

	} else {
		report.report_sequence = kTerminalReportSequence;
		report.code = AdapterCode::CounterExhausted;
		snapshot_.last_report_sequence = kTerminalReportSequence;
		terminal_code_ = AdapterCode::CounterExhausted;
		result.code = AdapterCode::CounterExhausted;
	}

	report.evidence = captureEvidence(nullptr, false, StatusDecisionOrigin::Local,
					  adapterLocalWireCode(report.code), nullptr, nullptr);

	result.report_count = kOperationReportCount;
	result.snapshot = snapshot_;
	return result;
}

AdapterResult WireAdapter::process(const TrustedStateSample &sample, OperationKind operation, CanonicalRequest *request)
{
#if RESEARCH_SITL_DIAGNOSTIC_CAPABILITY

	if (diagnostic_domain_) {
		AdapterResult result = initialResult(AdapterCode::ApiDomainMismatch);
		result.admission_closed = true;
		return result;
	}

#endif

	if (!configured_) {
		return initialResult(AdapterCode::NotConfigured);
	}

	if (snapshot_.terminal) {
		return initialResult(terminal_code_);
	}

	if (!structurallyValid(sample)) {
		return terminalResult(AdapterCode::TrustedStateFault,
				      sample.request_guard_context.validator_receive_time_us, true);
	}

	const bool first_observation = !snapshot_.initialized;
	bool generation_transition = false;

	if (!first_observation) {
		if (sample.generation == snapshot_.state_generation) {
			if (!matchesRetainedState(sample)) {
				return terminalResult(AdapterCode::TrustedStateFault,
						      sample.request_guard_context.validator_receive_time_us, true);
			}

		} else if (snapshot_.state_generation == UINT64_MAX
			   || sample.generation != snapshot_.state_generation + kFirstUsableCounterValue) {
			return terminalResult(AdapterCode::TrustedStateFault,
					      sample.request_guard_context.validator_receive_time_us, true);

		} else {
			generation_transition = true;
		}
	}

	const bool requires_barrier = first_observation || generation_transition;
	const uint8_t required_reports = requires_barrier ? kTransitionReportCount : kOperationReportCount;

	if ((requires_barrier && !first_observation
	     && snapshot_.transition_epoch == UINT64_MAX)
	    || !hasNormalReportCapacity(required_reports)) {
		return terminalResult(AdapterCode::CounterExhausted,
				      sample.request_guard_context.validator_receive_time_us, false);
	}

	AdapterResult result{};

	if (requires_barrier) {
		guard_.revoke();
		clearAcknowledgement();

		if (!first_observation
		    && (sample.map.source != last_sample_.map.source || sample.map.session != last_sample_.map.session)
		    && session_snapshot_.gate == SessionGate::Established) {
			session_snapshot_.gate = SessionGate::Revoked;
		}

		last_sample_ = sample;
		snapshot_.initialized = true;
		snapshot_.state_generation = sample.generation;
		snapshot_.transition_epoch = first_observation ? kFirstUsableCounterValue
					     : snapshot_.transition_epoch + kFirstUsableCounterValue;
		snapshot_.barrier_report_sequence = snapshot_.last_report_sequence + kFirstUsableCounterValue;
		snapshot_.barrier_validator_time_us = sample.request_guard_context.validator_receive_time_us;

		AdapterReport &barrier = result.reports[0];
		barrier.report_sequence = snapshot_.barrier_report_sequence;
		barrier.transition_epoch = snapshot_.transition_epoch;
		barrier.validator_time_us = snapshot_.barrier_validator_time_us;
		barrier.kind = AdapterReportKind::Barrier;
		barrier.code = AdapterCode::Observed;
		barrier.has_s1_decision = false;
		barrier.evidence = captureEvidence(&sample, true, StatusDecisionOrigin::Local, 70, nullptr, nullptr);
		snapshot_.last_report_sequence = barrier.report_sequence;
		result.report_count = kOperationReportCount;

	} else {
		last_sample_ = sample;
	}

	Decision decision{};
	bool copied_request = false;

	if (operation == OperationKind::Access) {
		CanonicalRequest candidate{};
		copied_request = guard_.getEligibleRequest(sample.request_guard_context, candidate, decision);

		if (copied_request && acknowledgementCurrent() && request != nullptr) {
			*request = candidate;

		} else {
			copied_request = false;
		}

	} else {
		decision = guard_.poll(sample.request_guard_context);
	}

	observeDecisionEvidence(decision);

	if (decision.latched) {
		clearAcknowledgement();
	}

	AdapterCode operation_code = AdapterCode::GuardRejected;

	if (operation == OperationKind::Observation && normalPollDecision(decision.code)) {
		operation_code = AdapterCode::Observed;

	} else if (operation == OperationKind::Poll && decision.code == DecisionCode::Accepted) {
		operation_code = AdapterCode::Observed;

	} else if (operation == OperationKind::Poll
		   && (decision.code == DecisionCode::NoEligibleRequest || decision.code == DecisionCode::ValidatedDisarmed)) {
		operation_code = AdapterCode::NoEligibleRequest;

	} else if (operation == OperationKind::Access && copied_request) {
		operation_code = AdapterCode::EligibleRequest;

	} else if (operation == OperationKind::Access
		   && (decision.code == DecisionCode::Accepted || decision.code == DecisionCode::NoEligibleRequest
		       || decision.code == DecisionCode::ValidatedDisarmed)) {
		operation_code = AdapterCode::NoEligibleRequest;
	}

	const uint8_t report_index = result.report_count;
	AdapterReport &operation_report = result.reports[report_index];
	operation_report.report_sequence = snapshot_.last_report_sequence + kFirstUsableCounterValue;
	operation_report.transition_epoch = snapshot_.transition_epoch;
	operation_report.validator_time_us = sample.request_guard_context.validator_receive_time_us;
	operation_report.kind = operation == OperationKind::Observation ? AdapterReportKind::Observation
				: (operation == OperationKind::Poll ? AdapterReportKind::Poll : AdapterReportKind::Access);
	operation_report.code = operation_code;
	operation_report.has_s1_decision = true;
	operation_report.s1_decision = decision;
	operation_report.evidence = captureEvidence(&sample, true, StatusDecisionOrigin::S1, 0, &decision, nullptr);
	snapshot_.last_report_sequence = operation_report.report_sequence;
	++result.report_count;
	result.code = operation_code;
	result.admission_closed = !acknowledgementCurrent();
	result.snapshot = snapshot_;
	return result;
}

AdapterResult WireAdapter::observeTrustedState(const TrustedStateSample &sample)
{
	return process(sample, OperationKind::Observation);
}

AdapterResult WireAdapter::poll(const TrustedStateSample &sample)
{
	return process(sample, OperationKind::Poll);
}

AdapterResult WireAdapter::getEligibleRequest(const TrustedStateSample &sample, CanonicalRequest &request)
{
	return process(sample, OperationKind::Access, &request);
}

SessionSnapshot WireAdapter::sessionSnapshot() const
{
	return session_snapshot_;
}

AdminResult WireAdapter::adminInitialResult(AdminCode code, const AdminCorrelation &correlation) const
{
	AdminResult result{};
	result.code = code;
	result.admission_closed = !acknowledgementCurrent();
	result.snapshot = snapshot_;
	result.session = session_snapshot_;
	result.correlation = correlation;
	return result;
}

AdminResult WireAdapter::adminTerminalResult(AdminCode code, TimeUs validator_time_us,
		const AdminCorrelation &correlation, bool allow_normal_report)
{
	guard_.revoke();
	clearAcknowledgement();
	snapshot_.terminal = true;
	terminal_code_ = code == AdminCode::CounterExhausted ? AdapterCode::CounterExhausted : AdapterCode::TrustedStateFault;
	admin_terminal_code_ = code;

	AdminResult result{};
	result.admission_closed = true;
	result.correlation = correlation;
	AdminReport &report = result.reports[0];
	report.transition_epoch = snapshot_.transition_epoch;
	report.validator_time_us = validator_time_us;
	report.kind = AdminReportKind::Terminal;
	report.correlation = correlation;

	if (allow_normal_report && hasNormalReportCapacity(kOperationReportCount)) {
		report.report_sequence = snapshot_.last_report_sequence + kFirstUsableCounterValue;
		report.code = code;
		snapshot_.last_report_sequence = report.report_sequence;

	} else {
		report.report_sequence = kTerminalReportSequence;
		report.code = AdminCode::CounterExhausted;
		snapshot_.last_report_sequence = kTerminalReportSequence;
		terminal_code_ = AdapterCode::CounterExhausted;
		admin_terminal_code_ = AdminCode::CounterExhausted;
	}

	report.has_s1_decision = false;
	report.evidence = captureEvidence(nullptr, false, StatusDecisionOrigin::Local,
					  adminLocalWireCode(report.code, TrustedStateSample{}, nullptr, nullptr, nullptr), nullptr, nullptr);

	result.code = report.code;
	result.report_count = kOperationReportCount;
	result.snapshot = snapshot_;
	result.session = session_snapshot_;
	return result;
}

AdminResult WireAdapter::processAdmin(const TrustedStateSample &sample, const DecodedSessionV1 *packet,
				      const SessionIngressVerdict *ingress, const SupervisorEvent *grant)
{
#if RESEARCH_SITL_DIAGNOSTIC_CAPABILITY

	if (diagnostic_domain_) {
		AdminResult result = adminInitialResult(AdminCode::ApiDomainMismatch, {});
		result.admission_closed = true;
		return result;
	}

#endif
	AdminCorrelation correlation {};

	if (packet != nullptr) {
		correlation.has_packet = true;
		correlation.packet_source = packet->source;
		correlation.packet_session = packet->session;
		correlation.packet_operation_sequence = packet->operation_sequence;
	}

	if (grant != nullptr) {
		correlation.has_supervisor_event = true;
		correlation.supervisor_event_id = grant->event_id;
	}

	if (!configured_) {
		return adminInitialResult(AdminCode::NotConfigured, correlation);
	}

	if (snapshot_.terminal) {
		const AdminCode code = terminal_code_ == AdapterCode::CounterExhausted ? AdminCode::CounterExhausted
				       : admin_terminal_code_;
		return adminInitialResult(code, correlation);
	}

	if (!structurallyValid(sample)) {
		return adminTerminalResult(AdminCode::TrustedStateFault,
					   sample.request_guard_context.validator_receive_time_us, correlation, true);
	}

	const bool first_observation = !snapshot_.initialized;
	bool generation_transition = false;

	if (!first_observation) {
		if (sample.generation == snapshot_.state_generation) {
			if (!matchesRetainedState(sample)) {
				return adminTerminalResult(AdminCode::TrustedStateFault,
							   sample.request_guard_context.validator_receive_time_us, correlation, true);
			}

		} else if (snapshot_.state_generation == UINT64_MAX
			   || sample.generation != snapshot_.state_generation + kFirstUsableCounterValue) {
			return adminTerminalResult(AdminCode::TrustedStateFault,
						   sample.request_guard_context.validator_receive_time_us, correlation, true);

		} else {
			generation_transition = true;
		}
	}

	bool duplicate_grant = false;

	if (grant != nullptr) {
		if (grant->event_id == 0 || !validSupervisorShape(*grant)
		    || (!session_snapshot_.has_supervisor_floor && grant->event_id != kFirstUsableCounterValue)
		    || (session_snapshot_.has_supervisor_floor
			&& (session_snapshot_.supervisor_floor == kTerminalReportSequence
			    || grant->event_id > session_snapshot_.supervisor_floor + kFirstUsableCounterValue))) {
			return adminTerminalResult(AdminCode::SupervisorStreamFault,
						   sample.request_guard_context.validator_receive_time_us, correlation, true);
		}

		duplicate_grant = session_snapshot_.has_supervisor_floor
				  && grant->event_id <= session_snapshot_.supervisor_floor;
	}

	const bool requires_exogenous_barrier = first_observation || generation_transition;
	const uint8_t required_reports = requires_exogenous_barrier ? kAdminTransitionReportCount : kAdminDirectReportCount;
	const uint64_t required_epochs = requires_exogenous_barrier ? kAdminTransitionEpochAdvances
					 : kAdminDirectEpochAdvances;

	if (!hasNormalReportCapacity(required_reports)
	    || snapshot_.transition_epoch > kTerminalReportSequence - required_epochs) {
		return adminTerminalResult(AdminCode::CounterExhausted,
					   sample.request_guard_context.validator_receive_time_us, correlation, false);
	}

	AdminResult result{};
	result.correlation = correlation;

	if (requires_exogenous_barrier) {
		guard_.revoke();
		clearAcknowledgement();

		if (!first_observation
		    && (sample.map.source != last_sample_.map.source || sample.map.session != last_sample_.map.session)
		    && session_snapshot_.gate == SessionGate::Established) {
			session_snapshot_.gate = SessionGate::Revoked;
		}

		last_sample_ = sample;
		snapshot_.initialized = true;
		snapshot_.state_generation = sample.generation;
		snapshot_.transition_epoch = first_observation ? kFirstUsableCounterValue
					     : snapshot_.transition_epoch + kFirstUsableCounterValue;
		snapshot_.barrier_report_sequence = snapshot_.last_report_sequence + kFirstUsableCounterValue;
		snapshot_.barrier_validator_time_us = sample.request_guard_context.validator_receive_time_us;

		AdminReport &barrier = result.reports[0];
		barrier.report_sequence = snapshot_.barrier_report_sequence;
		barrier.transition_epoch = snapshot_.transition_epoch;
		barrier.validator_time_us = snapshot_.barrier_validator_time_us;
		barrier.kind = AdminReportKind::ExogenousBarrier;
		barrier.code = AdminCode::Observed;
		barrier.has_s1_decision = false;
		barrier.evidence = captureEvidence(&sample, true, StatusDecisionOrigin::Local, 70, nullptr, nullptr);
		snapshot_.last_report_sequence = barrier.report_sequence;
		result.report_count = kOperationReportCount;

	} else {
		last_sample_ = sample;
	}

	if (grant != nullptr && !duplicate_grant) {
		session_snapshot_.has_supervisor_floor = true;
		session_snapshot_.supervisor_floor = grant->event_id;
		correlation.supervisor_event_consumed = true;
		result.correlation = correlation;
	}

	const Decision prestate = guard_.poll(sample.request_guard_context);
	observeDecisionEvidence(prestate);
	result.has_prestate_s1_decision = true;
	result.prestate_s1_decision = prestate;

	if (prestate.latched) {
		clearAcknowledgement();
	}

	AdminCode code = AdminCode::Observed;
	bool action_success = false;

	if (packet != nullptr) {
		if (!validSessionPacket(*packet)) {
			code = AdminCode::MalformedPacket;

		} else if (ingress == nullptr || !validIngress(*ingress)) {
			code = AdminCode::IngressRejected;

		} else if (packet->operation == kSessionOperationAcknowledge) {
			code = AdminCode::UnsupportedOperation;

		} else if (session_snapshot_.has_wire_operation_floor
			   && packet->operation_sequence <= session_snapshot_.wire_operation_floor) {
			code = AdminCode::PacketOperationReplay;

		} else if (duplicate_grant) {
			code = AdminCode::SupervisorDuplicate;

		} else if (grant == nullptr) {
			code = AdminCode::SupervisorMissing;

		} else if (grant->expected_controller_instance_id != identity_.controller_instance_id
			   || grant->expected_state_generation != snapshot_.state_generation
			   || grant->expected_transition_epoch != snapshot_.transition_epoch
			   || !grant->has_session_binding || grant->session.owner != packet->source
			   || grant->session.session != packet->session
			   || (packet->operation == kSessionOperationEstablish && grant->action != SupervisorAction::Establish)
			   || (packet->operation == kSessionOperationRevoke && grant->action != SupervisorAction::Revoke)
			   || packet->controller_instance_id != identity_.controller_instance_id
			   || packet->transition_epoch != snapshot_.transition_epoch) {
			code = AdminCode::BindingMismatch;

		} else if (packet->operation == kSessionOperationEstablish) {
			const bool map_binding_matches = packet->source == configuration_.permitted_source
							 && packet->clock_mapping_epoch == sample.map.map_epoch
							 && sample.map.source == packet->source && sample.map.session == packet->session
							 && sample.map.controller_instance_id == identity_.controller_instance_id;
			const bool same_last_session = session_snapshot_.has_last_successful_session
						       && packet->session == session_snapshot_.last_successful_session.session;
			const bool allowed_context = sample.request_guard_context.arm_state == ArmState::Disarmed
						     && sample.map.valid && sample.request_guard_context.transport_connected
						     && sample.request_guard_context.required_state_valid
						     && sample.request_guard_context.mode_state != ResearchModeState::Unknown && !prestate.latched;

			if (!map_binding_matches) {
				code = AdminCode::BindingMismatch;

			} else if (same_last_session || !allowed_context) {
				code = AdminCode::EstablishmentContextRejected;

			} else {
				result.action_performed = true;
				result.action_kind = AdminActionKind::EstablishTrustedSession;
				const Decision action = guard_.establishTrustedSession({packet->source, packet->session}, true,
							sample.request_guard_context);
				result.has_action_s1_decision = true;
				result.action_s1_decision = action;

				if (action.code == DecisionCode::Accepted) {
					session_snapshot_.gate = SessionGate::Established;
					session_snapshot_.has_last_successful_session = true;
					session_snapshot_.last_successful_session = {packet->source, packet->session};
					has_last_accepted_sequence_ = false;
					last_accepted_sequence_ = 0;
					last_accepted_authenticated_ = false;
					session_snapshot_.has_wire_operation_floor = true;
					session_snapshot_.wire_operation_floor = packet->operation_sequence;
					action_success = true;
					code = AdminCode::Completed;

				} else {
					code = AdminCode::S1ActionRejected;
				}
			}

		} else {
			if (session_snapshot_.gate != SessionGate::Established) {
				code = AdminCode::NoEstablishedSession;

			} else if (!session_snapshot_.has_last_successful_session
				   || packet->source != configuration_.permitted_source
				   || !sameSession({packet->source, packet->session}, session_snapshot_.last_successful_session)
				   || sample.map.map_epoch == 0 || packet->clock_mapping_epoch != sample.map.map_epoch
				   || sample.map.source != packet->source || sample.map.session != packet->session) {
				code = AdminCode::BindingMismatch;

			} else if (sample.request_guard_context.arm_state != ArmState::Disarmed) {
				code = AdminCode::ContextRejected;

			} else {
				guard_.revoke();
				result.action_performed = true;
				result.action_kind = AdminActionKind::Revoke;
				session_snapshot_.gate = SessionGate::Revoked;
				session_snapshot_.has_wire_operation_floor = true;
				session_snapshot_.wire_operation_floor = packet->operation_sequence;
				action_success = true;
				code = AdminCode::Completed;
			}
		}

	} else {
		if (duplicate_grant) {
			code = AdminCode::SupervisorDuplicate;

		} else if (grant == nullptr || grant->action != SupervisorAction::ClearLatchedFault
			   || grant->expected_controller_instance_id != identity_.controller_instance_id
			   || grant->expected_state_generation != snapshot_.state_generation
			   || grant->expected_transition_epoch != snapshot_.transition_epoch
			   || (!session_snapshot_.has_last_successful_session
			       && (grant == nullptr || grant->has_session_binding))
			   || (session_snapshot_.has_last_successful_session
			       && (grant == nullptr || !grant->has_session_binding
				   || !sameSession(grant->session, session_snapshot_.last_successful_session)))) {
			code = grant == nullptr ? AdminCode::SupervisorMissing : AdminCode::BindingMismatch;

		} else if (sample.request_guard_context.arm_state != ArmState::Disarmed) {
			code = AdminCode::ContextRejected;

		} else if (!prestate.latched) {
			code = AdminCode::ClearNotLatched;

		} else {
			result.action_performed = true;
			result.action_kind = AdminActionKind::ClearLatch;
			const Decision action = guard_.clearLatch(true, ArmState::Disarmed);
			result.has_action_s1_decision = true;
			result.action_s1_decision = action;

			if (action.code == DecisionCode::Accepted) {
				session_snapshot_.gate = SessionGate::EstablishmentRequired;
				action_success = true;
				code = AdminCode::Completed;

			} else {
				code = AdminCode::S1ActionRejected;
			}
		}
	}

	if (result.has_action_s1_decision) {
		observeDecisionEvidence(result.action_s1_decision);
	}

	if (action_success) {
		clearAcknowledgement();
		snapshot_.transition_epoch += kFirstUsableCounterValue;
		snapshot_.barrier_report_sequence = snapshot_.last_report_sequence + kFirstUsableCounterValue;
		snapshot_.barrier_validator_time_us = sample.request_guard_context.validator_receive_time_us;
		AdminReport &barrier = result.reports[result.report_count];
		barrier.report_sequence = snapshot_.barrier_report_sequence;
		barrier.transition_epoch = snapshot_.transition_epoch;
		barrier.validator_time_us = snapshot_.barrier_validator_time_us;
		barrier.kind = AdminReportKind::AdministrativeBarrier;
		barrier.code = AdminCode::Observed;
		barrier.has_s1_decision = false;
		barrier.evidence = captureEvidence(&sample, true, StatusDecisionOrigin::Local, 70, nullptr, nullptr);
		snapshot_.last_report_sequence = barrier.report_sequence;
		++result.report_count;
	}

	AdminReport &operation = result.reports[result.report_count];
	operation.report_sequence = snapshot_.last_report_sequence + kFirstUsableCounterValue;
	operation.transition_epoch = snapshot_.transition_epoch;
	operation.validator_time_us = sample.request_guard_context.validator_receive_time_us;
	operation.kind = AdminReportKind::Operation;
	operation.code = code;
	operation.correlation = correlation;

	if (result.has_action_s1_decision) {
		operation.has_s1_decision = true;
		operation.s1_decision = result.action_s1_decision;

	} else {
		operation.has_s1_decision = true;
		operation.s1_decision = prestate;
	}

	const bool action_is_s1 = result.has_action_s1_decision;
	operation.evidence = captureEvidence(&sample, true,
					     action_is_s1 ? StatusDecisionOrigin::S1 : StatusDecisionOrigin::Local,
					     action_is_s1 ? 0 : adminLocalWireCode(code, sample, packet, grant, ingress),
					     action_is_s1 ? &result.action_s1_decision : nullptr, ingress);

	snapshot_.last_report_sequence = operation.report_sequence;
	++result.report_count;
	result.code = code;
	result.admission_closed = !acknowledgementCurrent();
	result.snapshot = snapshot_;
	result.session = session_snapshot_;
	result.correlation = correlation;
	return result;
}

AdminResult WireAdapter::processSession(const TrustedStateSample &sample, const DecodedSessionV1 &packet,
					const SessionIngressVerdict &ingress, const SupervisorEvent *grant)
{
	return processAdmin(sample, &packet, &ingress, grant);
}

AdminResult WireAdapter::clearLatchedFault(const TrustedStateSample &sample, const SupervisorEvent &grant)
{
	return processAdmin(sample, nullptr, nullptr, &grant);
}

IngressResult WireAdapter::ingressInitialResult(IngressCode code) const
{
	IngressResult result{};
	result.code = code;
	result.admission_closed = !acknowledgementCurrent();
	result.snapshot = snapshot_;
	result.session = session_snapshot_;
	result.admission = admission_snapshot_;
	return result;
}

IngressResult WireAdapter::ingressTerminalResult(IngressCode code, TimeUs validator_time_us,
		const IngressCorrelation &correlation, bool allow_normal_report)
{
	guard_.revoke();
	clearAcknowledgement();
	snapshot_.terminal = true;
	terminal_code_ = code == IngressCode::CounterExhausted ? AdapterCode::CounterExhausted : AdapterCode::TrustedStateFault;
	admin_terminal_code_ = code == IngressCode::CounterExhausted ? AdminCode::CounterExhausted : AdminCode::TrustedStateFault;

	IngressResult result{};
	IngressReport &report = result.reports[0];
	report.transition_epoch = snapshot_.transition_epoch;
	report.validator_time_us = validator_time_us;
	report.kind = IngressReportKind::Terminal;
	report.correlation = correlation;

	if (allow_normal_report && hasNormalReportCapacity(kOperationReportCount)) {
		report.report_sequence = snapshot_.last_report_sequence + kFirstUsableCounterValue;
		report.code = code;
		snapshot_.last_report_sequence = report.report_sequence;

	} else {
		report.report_sequence = kTerminalReportSequence;
		report.code = IngressCode::CounterExhausted;
		snapshot_.last_report_sequence = kTerminalReportSequence;
		terminal_code_ = AdapterCode::CounterExhausted;
		admin_terminal_code_ = AdminCode::CounterExhausted;
	}

	report.has_s1_decision = false;
	report.evidence = captureEvidence(nullptr, false, StatusDecisionOrigin::Local,
					  ingressLocalWireCode(report.code, TrustedStateSample{}, nullptr, nullptr,
							  SessionIngressVerdict{}), nullptr, nullptr);

	result.code = report.code;
	result.admission_closed = true;
	result.snapshot = snapshot_;
	result.session = session_snapshot_;
	result.admission = admission_snapshot_;
	result.report_count = kOperationReportCount;
	return result;
}

IngressCode WireAdapter::admitAcknowledgement(const TrustedStateSample &sample, const DecodedSessionV1 &packet,
		const SessionIngressVerdict &ingress)
{
	IngressCode code = IngressCode::Observed;

	if (!validSessionPacket(packet)) {
		code = IngressCode::MalformedPacket;

	} else if (!validIngress(ingress)) {
		code = IngressCode::IngressRejected;

	} else if (packet.operation != kSessionOperationAcknowledge) {
		code = IngressCode::UnsupportedSessionOperation;

	} else if (session_snapshot_.has_wire_operation_floor
		   && packet.operation_sequence <= session_snapshot_.wire_operation_floor) {
		code = IngressCode::WireOperationReplay;

	} else if (session_snapshot_.gate != SessionGate::Established) {
		code = IngressCode::SessionGateRejected;

	} else if (!session_snapshot_.has_last_successful_session
		   || packet.source != configuration_.permitted_source
		   || !sameSession({packet.source, packet.session}, session_snapshot_.last_successful_session)
		   || packet.controller_instance_id != identity_.controller_instance_id
		   || packet.transition_epoch != snapshot_.transition_epoch
		   || packet.clock_mapping_epoch != sample.map.map_epoch || sample.map.source != packet.source
		   || sample.map.session != packet.session || sample.map.controller_instance_id != identity_.controller_instance_id) {
		code = IngressCode::BindingMismatch;

	} else if (!validMap(sample.map)) {
		code = IngressCode::MapInvalid;

	} else if (sample.map.valid_from_producer_us > sample.map.valid_until_producer_us) {
		code = IngressCode::MapIntervalInvalid;

	} else if (acknowledgementCurrent()) {
		code = IngressCode::AcknowledgementFrozen;

	} else if (packet.acknowledged_status_sequence != snapshot_.barrier_report_sequence) {
		code = IngressCode::AcknowledgementStatusMismatch;

	} else if (packet.producer_acknowledgement_monotonic_us < sample.map.valid_from_producer_us
		   || packet.producer_acknowledgement_monotonic_us > sample.map.valid_until_producer_us) {
		code = IngressCode::MapIntervalInvalid;

	} else {
		TimeUs mapped{};
		TimeUs earliest{};
		TimeUs latest{};
		TimeUs future_limit{};
		TimeUs age{};

		if (!addSignedOffset(packet.producer_acknowledgement_monotonic_us,
				     sample.map.validator_minus_producer_us, mapped)) {
			code = IngressCode::MapArithmeticInvalid;

		} else if (!checkedSubtract(mapped, sample.map.uncertainty_us, earliest)
			   || !checkedAdd(mapped, sample.map.uncertainty_us, latest)
			   || !checkedAdd(sample.request_guard_context.validator_receive_time_us,
					  configuration_.timing.maximum_future_skew_us, future_limit)
			   || !checkedSubtract(sample.request_guard_context.validator_receive_time_us, earliest, age)) {
			code = IngressCode::AcknowledgementTimingInvalid;

		} else if (sample.map.uncertainty_us > configuration_.timing.maximum_clock_uncertainty_us
			   || earliest > sample.request_guard_context.validator_receive_time_us
			   || age > configuration_.timing.maximum_command_age_us || latest > future_limit) {
			code = IngressCode::AcknowledgementTimingInvalid;

		} else if (earliest <= snapshot_.barrier_validator_time_us) {
			code = IngressCode::AcknowledgementOrdering;

		} else {
			admission_snapshot_.acknowledgement_current = true;
			admission_snapshot_.acknowledgement_operation_sequence = packet.operation_sequence;
			admission_snapshot_.acknowledged_status_sequence = packet.acknowledged_status_sequence;
			admission_snapshot_.acknowledgement_raw_producer_time_us = packet.producer_acknowledgement_monotonic_us;
			admission_snapshot_.mapped_acknowledgement_time_us = mapped;
			admission_snapshot_.earliest_acknowledgement_time_us = earliest;
			admission_snapshot_.latest_acknowledgement_time_us = latest;
			admission_snapshot_.source = packet.source;
			admission_snapshot_.session = packet.session;
			admission_snapshot_.controller_instance_id = packet.controller_instance_id;
			admission_snapshot_.transition_epoch = packet.transition_epoch;
			admission_snapshot_.map_epoch = packet.clock_mapping_epoch;
			admission_snapshot_.map = sample.map;
			session_snapshot_.has_wire_operation_floor = true;
			session_snapshot_.wire_operation_floor = packet.operation_sequence;
			code = IngressCode::Completed;
		}
	}

	return code;
}

IngressCode WireAdapter::prepareRequest(const TrustedStateSample &sample, const DecodedRequestV1 &packet,
					const SessionIngressVerdict &ingress, CanonicalRequest &canonical) const
{
	IngressCode code = IngressCode::Observed;

	if (!validRequestPacket(packet)) {
		code = IngressCode::MalformedPacket;

	} else if (!validIngress(ingress)) {
		code = IngressCode::IngressRejected;

	} else if (session_snapshot_.gate != SessionGate::Established) {
		code = IngressCode::SessionGateRejected;

	} else if (!session_snapshot_.has_last_successful_session
		   || packet.wire.source_id != configuration_.permitted_source
		   || !sameSession({packet.wire.source_id, packet.wire.session_id}, session_snapshot_.last_successful_session)
		   || packet.wire.controller_instance_id != identity_.controller_instance_id
		   || packet.wire.transition_epoch != snapshot_.transition_epoch
		   || packet.wire.clock_mapping_epoch != sample.map.map_epoch
		   || sample.map.source != packet.wire.source_id || sample.map.session != packet.wire.session_id
		   || sample.map.controller_instance_id != identity_.controller_instance_id) {
		code = IngressCode::BindingMismatch;

	} else if (!acknowledgementCurrent()) {
		code = IngressCode::AcknowledgementRequired;

	} else {
		const CanonicalizeCode conversion = canonicalizeRequestV1(packet.wire, sample.map, canonical);

		if (conversion != CanonicalizeCode::Ok) {
			code = canonicalizeIngressCode(conversion);

		} else if (packet.wire.producer_monotonic_us
			   <= admission_snapshot_.acknowledgement_raw_producer_time_us) {
			code = IngressCode::AcknowledgementOrdering;

		} else {
			TimeUs earliest_creation{};

			if (!checkedSubtract(canonical.producer_time_in_validator_domain_us,
					     canonical.clock_mapping_uncertainty_us, earliest_creation)) {
				code = IngressCode::MapArithmeticInvalid;

			} else if (earliest_creation <= admission_snapshot_.latest_acknowledgement_time_us) {
				code = IngressCode::AcknowledgementOrdering;

			} else {
				code = IngressCode::Completed;
			}
		}
	}

	return code;
}

IngressResult WireAdapter::processIngress(const TrustedStateSample &sample, IngressPacketKind kind,
		const DecodedSessionV1 *acknowledgement, const DecodedRequestV1 *request,
		const SessionIngressVerdict &ingress)
{
#if RESEARCH_SITL_DIAGNOSTIC_CAPABILITY

	if (diagnostic_domain_) {
		IngressResult result = ingressInitialResult(IngressCode::ApiDomainMismatch);
		result.admission_closed = true;
		return result;
	}

#endif
	IngressCorrelation correlation {};
	correlation.packet_kind = kind;

	if (acknowledgement != nullptr) {
		correlation.source = acknowledgement->source;
		correlation.session = acknowledgement->session;
		correlation.controller_instance_id = acknowledgement->controller_instance_id;
		correlation.transition_epoch = acknowledgement->transition_epoch;
		correlation.map_epoch = acknowledgement->clock_mapping_epoch;
		correlation.packet_sequence = acknowledgement->operation_sequence;
		correlation.acknowledged_status_sequence = acknowledgement->acknowledged_status_sequence;

	} else if (request != nullptr) {
		correlation.source = request->wire.source_id;
		correlation.session = request->wire.session_id;
		correlation.controller_instance_id = request->wire.controller_instance_id;
		correlation.transition_epoch = request->wire.transition_epoch;
		correlation.map_epoch = request->wire.clock_mapping_epoch;
		correlation.packet_sequence = request->wire.sequence;
	}

	if (!configured_) {
		return ingressInitialResult(IngressCode::NotConfigured);
	}

	if (snapshot_.terminal) {
		return ingressInitialResult(terminal_code_ == AdapterCode::CounterExhausted ? IngressCode::CounterExhausted
					    : IngressCode::TrustedStateFault);
	}

	if (!structurallyValid(sample)) {
		return ingressTerminalResult(IngressCode::TrustedStateFault,
					     sample.request_guard_context.validator_receive_time_us, correlation, true);
	}

	const bool first_observation = !snapshot_.initialized;
	bool generation_transition = false;

	if (!first_observation) {
		if (sample.generation == snapshot_.state_generation) {
			if (!matchesRetainedState(sample)) {
				return ingressTerminalResult(IngressCode::TrustedStateFault,
							     sample.request_guard_context.validator_receive_time_us, correlation, true);
			}

		} else if (snapshot_.state_generation == UINT64_MAX
			   || sample.generation != snapshot_.state_generation + kFirstUsableCounterValue) {
			return ingressTerminalResult(IngressCode::TrustedStateFault,
						     sample.request_guard_context.validator_receive_time_us, correlation, true);

		} else {
			generation_transition = true;
		}
	}

	const bool requires_barrier = first_observation || generation_transition;
	const uint8_t required_reports = requires_barrier ? kTransitionReportCount : kOperationReportCount;

	if ((requires_barrier && !first_observation
	     && snapshot_.transition_epoch == UINT64_MAX)
	    || !hasNormalReportCapacity(required_reports)) {
		return ingressTerminalResult(IngressCode::CounterExhausted,
					     sample.request_guard_context.validator_receive_time_us, correlation, false);
	}

	IngressResult result{};

	if (requires_barrier) {
		guard_.revoke();
		clearAcknowledgement();

		if (!first_observation
		    && (sample.map.source != last_sample_.map.source || sample.map.session != last_sample_.map.session)
		    && session_snapshot_.gate == SessionGate::Established) {
			session_snapshot_.gate = SessionGate::Revoked;
		}

		last_sample_ = sample;
		snapshot_.initialized = true;
		snapshot_.state_generation = sample.generation;
		snapshot_.transition_epoch = first_observation ? kFirstUsableCounterValue
					     : snapshot_.transition_epoch + kFirstUsableCounterValue;
		snapshot_.barrier_report_sequence = snapshot_.last_report_sequence + kFirstUsableCounterValue;
		snapshot_.barrier_validator_time_us = sample.request_guard_context.validator_receive_time_us;

		IngressReport &barrier = result.reports[0];
		barrier.report_sequence = snapshot_.barrier_report_sequence;
		barrier.transition_epoch = snapshot_.transition_epoch;
		barrier.validator_time_us = snapshot_.barrier_validator_time_us;
		barrier.kind = IngressReportKind::ExogenousBarrier;
		barrier.code = IngressCode::Observed;
		barrier.correlation.packet_kind = IngressPacketKind::None;
		barrier.has_s1_decision = false;
		barrier.evidence = captureEvidence(&sample, true, StatusDecisionOrigin::Local, 70, nullptr, nullptr);
		snapshot_.last_report_sequence = barrier.report_sequence;
		result.report_count = kOperationReportCount;

	} else {
		last_sample_ = sample;
	}

	const Decision prestate = guard_.poll(sample.request_guard_context);
	observeDecisionEvidence(prestate);
	result.has_prestate_s1_decision = true;
	result.prestate_s1_decision = prestate;
	IngressCode code = IngressCode::Observed;
	Decision operation_decision = prestate;
	bool has_operation_decision = false;

	if (prestate.latched || !prestateAllowsIngress(prestate.code)) {
		if (prestate.latched || prestate.code == DecisionCode::ValidatorTimeRegressed
		    || prestate.code == DecisionCode::ClockMappingInvalid || prestate.code == DecisionCode::RequiredStateInvalid
		    || prestate.code == DecisionCode::ArmStateUnknown || prestate.code == DecisionCode::ModeStateUnknown
		    || prestate.code == DecisionCode::Disconnected) {
			clearAcknowledgement();
		}

		code = IngressCode::PrestateRejected;

	} else if (kind == IngressPacketKind::Acknowledgement) {
		code = admitAcknowledgement(sample, *acknowledgement, ingress);

	} else {
		const DecodedRequestV1 &packet = *request;
		CanonicalRequest canonical{};
		code = prepareRequest(sample, packet, ingress, canonical);

		if (code == IngressCode::Completed) {
			operation_decision = guard_.evaluate(canonical, sample.request_guard_context);
			observeDecisionEvidence(operation_decision);
			has_operation_decision = true;
			code = operation_decision.code == DecisionCode::Accepted ? IngressCode::Completed : IngressCode::S1Rejected;

			if (operation_decision.code == DecisionCode::Accepted) {
				has_last_accepted_sequence_ = true;
				last_accepted_sequence_ = packet.wire.sequence;
				last_accepted_authenticated_ = ingress.mavlink2 && ingress.parser_authenticated;
			}

			if (operation_decision.latched) {
				clearAcknowledgement();
			}
		}
	}

	IngressReport &operation = result.reports[result.report_count];
	operation.report_sequence = snapshot_.last_report_sequence + kFirstUsableCounterValue;
	operation.transition_epoch = snapshot_.transition_epoch;
	operation.validator_time_us = sample.request_guard_context.validator_receive_time_us;
	operation.kind = IngressReportKind::Operation;
	operation.code = code;
	operation.has_s1_decision = true;
	operation.s1_decision = has_operation_decision ? operation_decision : prestate;
	operation.correlation = correlation;
	const bool operation_is_s1 = has_operation_decision || code == IngressCode::PrestateRejected;
	operation.evidence = captureEvidence(&sample, true,
					     operation_is_s1 ? StatusDecisionOrigin::S1 : StatusDecisionOrigin::Local,
					     operation_is_s1 ? 0 : ingressLocalWireCode(code, sample, acknowledgement, request, ingress),
					     operation_is_s1 ? (has_operation_decision ? &operation_decision : &prestate) : nullptr,
					     &ingress);
	snapshot_.last_report_sequence = operation.report_sequence;
	++result.report_count;
	result.code = code;
	result.admission_closed = !acknowledgementCurrent();
	result.snapshot = snapshot_;
	result.session = session_snapshot_;
	result.admission = admission_snapshot_;
	result.has_operation_s1_decision = has_operation_decision;

	if (has_operation_decision) {
		result.operation_s1_decision = operation_decision;
	}

	return result;
}

IngressResult WireAdapter::processAcknowledgement(const TrustedStateSample &sample, const DecodedSessionV1 &packet,
		const SessionIngressVerdict &ingress)
{
	return processIngress(sample, IngressPacketKind::Acknowledgement, &packet, nullptr, ingress);
}

IngressResult WireAdapter::processRequest(const TrustedStateSample &sample, const DecodedRequestV1 &packet,
		const SessionIngressVerdict &ingress)
{
	return processIngress(sample, IngressPacketKind::Request, nullptr, &packet, ingress);
}

} // namespace research_acceleration
