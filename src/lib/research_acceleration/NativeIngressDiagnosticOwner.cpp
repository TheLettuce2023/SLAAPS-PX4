#include "NativeIngressDiagnosticOwner.hpp"

#include <cmath>
#include <limits>

namespace research_sitl
{
namespace
{
using namespace research_acceleration;

bool sameState(RawState a, RawState b) noexcept
{
	return a.arming_state == b.arming_state && a.nav_state == b.nav_state;
}

bool validState(RawState state) noexcept
{
	return (state.arming_state == 1 || state.arming_state == 2) && state.nav_state < 31
	       && state.nav_state != 9 && state.nav_state != 11 && state.nav_state != 16;
}

bool exactFinite(float value, float expected) noexcept
{
	return std::isfinite(value) && value >= expected && value <= expected;
}

bool validConfiguration(const NativeIngressOwnerConfig &config) noexcept
{
	const auto &contract = config.contract;

	if (contract.protocol_version != kWireProtocolVersionV1 || contract.permitted_source == 0
	    || contract.disarmed_policy != DisarmedPolicy::ValidateOnly
	    || !exactFinite(contract.acceleration_bounds.maximum_norm_m_s2, 12.F)
	    || contract.timing.minimum_validity_us != 80000 || contract.timing.maximum_validity_us != 80000
	    || contract.timing.maximum_command_age_us != 60000 || contract.timing.maximum_future_skew_us != 8000
	    || contract.timing.maximum_clock_uncertainty_us != 4000
	    || !exactFinite(contract.quaternion_norm_tolerance, 0.01F)
	    || config.adapter_identity.controller_instance_id == 0 || config.identity.owner_lifetime_id == 0
	    || config.identity.native_run_id == 0 || config.identity.origin != kDiagnosticExecutionOrigin
	    || config.transport_binding_id == 0 || config.producer_lifetime_id == 0
	    || config.source_clock_id == 0 || config.peer_port == 0) { return false; }

	for (size_t axis = 0; axis < 3; ++axis) {
		if (!exactFinite(contract.acceleration_bounds.minimum_m_s2(axis), -10.F)
		    || !exactFinite(contract.acceleration_bounds.maximum_m_s2(axis), 10.F)) { return false; }
	}

	return true;
}

NativeIngressFault unavailable(const DiagnosticResult &result) noexcept
{
	if (result.code == DiagnosticCode::Terminal || result.snapshot.terminal) { return NativeIngressFault::AdapterTerminal; }

	if (result.code == DiagnosticCode::WrongDomain || result.code == DiagnosticCode::NotConfigured) {
		return NativeIngressFault::AdapterUnavailable;
	}

	return NativeIngressFault::None;
}

DiagnosticCloseReason adapterReason(NativeIngressCloseCause cause) noexcept
{
	switch (cause) {
	case NativeIngressCloseCause::OwnerStop: return DiagnosticCloseReason::OwnerStop;

	case NativeIngressCloseCause::SourceContinuityLost: return DiagnosticCloseReason::SourceContinuityLost;

	case NativeIngressCloseCause::OwnerEvidenceExhausted: return DiagnosticCloseReason::OwnerEvidenceExhausted;

	case NativeIngressCloseCause::OwnerConfigurationInvalid: return DiagnosticCloseReason::OwnerConfigurationInvalid;

	case NativeIngressCloseCause::TransportFault: return DiagnosticCloseReason::TransportFault;

	case NativeIngressCloseCause::WorkerResourceExhausted: return DiagnosticCloseReason::WorkerResourceExhausted;

	case NativeIngressCloseCause::AdapterCapacityExhausted: return DiagnosticCloseReason::AdapterCapacityExhausted;

	case NativeIngressCloseCause::AdapterFault:
	case NativeIngressCloseCause::InvariantViolation: return DiagnosticCloseReason::OwnerConfigurationInvalid;
	}

	return DiagnosticCloseReason::OwnerConfigurationInvalid;
}

uint16_t closureWireReason(NativeIngressCloseCause cause) noexcept
{
	switch (cause) {
	case NativeIngressCloseCause::SourceContinuityLost: return 6;

	case NativeIngressCloseCause::OwnerStop: return 7;

	case NativeIngressCloseCause::OwnerEvidenceExhausted:
	case NativeIngressCloseCause::WorkerResourceExhausted: return 8;

	default: return 0;
	}
}

bool negativeOnly(const DiagnosticResult &result) noexcept
{
	if (result.snapshot.selection != DiagnosticSelectionState::Disabled
	    || result.snapshot.operator_attached || result.snapshot.session.gate == SessionGate::Established
	    || result.snapshot.admission.acknowledgement_current
	    || result.code == DiagnosticCode::RequestValidatedDisarmed) { return false; }

	for (uint8_t index = 0; index < result.record_count; ++index) {
		if (result.records[index].eligible_for_control) { return false; }
	}

	return true;
}

bool requestSelectionRequired(const DiagnosticResult &result, const NativeIngressCandidate &candidate,
			      const NativeSourceSnapshot &source, uint64_t evaluation_hrt_us) noexcept
{
	if (result.code != DiagnosticCode::Rejected || !result.has_operation_record
	    || result.operation_record_index >= result.record_count || !negativeOnly(result)) { return false; }

	const auto &operation = result.records[result.operation_record_index];
	const auto *reason = std::get_if<DiagnosticLocalReason>(&operation.cause);
	const auto &correlation = operation.correlation.ingress;
	const auto &authentication = operation.correlation.authentication;
	return operation.kind == DiagnosticReportKind::Operation && operation.code == DiagnosticCode::Rejected
	       && operation.correlation.operation == DiagnosticOperationKind::Request
	       && reason != nullptr && *reason == DiagnosticLocalReason::SelectionRequired
	       && correlation.packet_kind == IngressPacketKind::Request
	       && correlation.source == candidate.source_id && correlation.session == candidate.transport_binding_id
	       && correlation.controller_instance_id == candidate.controller_instance_id
	       && correlation.transition_epoch == candidate.expected_transition_epoch
	       && correlation.map_epoch == 0 && correlation.packet_sequence == candidate.request_sequence
	       && authentication && authentication->mavlink2 && authentication->parser_authenticated
	       && authentication->configured_header_channel_source_binding
	       && authentication->configured_target_binding && operation.state_reference_valid
	       && operation.physical.generation == source.physical_generation
	       && operation.physical.request_guard_context.validator_receive_time_us == evaluation_hrt_us
	       && operation.physical.request_guard_context.mode_state == ResearchModeState::NotSelected
	       && !operation.eligible_for_control;
}

bool boundCandidate(const NativeIngressCandidate &candidate, const NativeIngressOwnerConfig &config) noexcept
{
	bool signature_time_matches = candidate.signature[0] == 7;

	for (unsigned byte = 0; byte < 6; ++byte) {
		signature_time_matches = signature_time_matches
					 && candidate.signature[byte + 1] == ((candidate.signing_timestamp_48 >> (8 * byte)) & 0xff);
	}

	return signature_time_matches && candidate.token != 0 && candidate.native_run_id == config.identity.native_run_id
	       && candidate.owner_lifetime_id == config.identity.owner_lifetime_id
	       && candidate.controller_instance_id == config.adapter_identity.controller_instance_id
	       && candidate.source_id == config.contract.permitted_source
	       && candidate.transport_binding_id == config.transport_binding_id
	       && candidate.channel == config.channel && candidate.peer_port == config.peer_port
	       && candidate.header_system == 42 && candidate.header_component == 191
	       && candidate.incoming_link_id == 7 && candidate.target_system == 1
	       && candidate.signing_timestamp_48 < (uint64_t{1} << 48)
	       &&candidate.payload_length >= 103 && candidate.payload_length <= 108
	       &&candidate.parser_stage == 1
	       &&candidate.target_component == 1 && candidate.protocol_version == kWireProtocolVersionV1
	       &&candidate.domain == 1 && candidate.profile == 1 && candidate.frame == kWireFrameFrdV1
	       &&candidate.quantity == kWireQuantityInertialAccelerationV1
	       &&candidate.reference_point == kWireReferencePointCgV1 &&candidate.flags == 0
	       &&candidate.reserved[0] == 0 && candidate.reserved[1] == 0
	       &&candidate.reserved[2] == 0 && candidate.reserved[3] == 0
	       &&candidate.clock_mapping_epoch == 0 && candidate.request_sequence != 0
	       &&candidate.producer_monotonic_us != 0
	       &&candidate.producer_expiry_monotonic_us > candidate.producer_monotonic_us
	       &&std::isfinite(candidate.acceleration_x_frd_m_s2)
	       &&std::isfinite(candidate.acceleration_y_frd_m_s2)
	       &&std::isfinite(candidate.acceleration_z_frd_m_s2);
}

} // namespace

NativeIngressDiagnosticOwner::NativeIngressDiagnosticOwner(const NativeIngressOwnerConfig &config,
		NativeIngressMailbox &mailbox) noexcept :
	config_(config), mailbox_(mailbox),
	adapter_(config.contract, config.adapter_identity, DiagnosticNativeSitlTag{}, config.identity)
{
	if (!validConfiguration(config)) {
		close(NativeIngressCloseCause::OwnerConfigurationInvalid, NativeIngressFault::InvalidConfiguration,
		      std::nullopt, std::nullopt, 0);
	}
}

bool NativeIngressDiagnosticOwner::closed() const noexcept
{
	return state_ == NativeOwnerState::Stopped || state_ == NativeOwnerState::Faulted;
}

NativeIngressOwnerSummary NativeIngressDiagnosticOwner::summary() const noexcept
{
	return {state_, first_fault_, source_, source_count_, transaction_count_, last_status_sequence_,
		has_closure_, evidence_complete_};
}

bool NativeIngressDiagnosticOwner::copySource(size_t index, NativeSourceRecord &out) const noexcept
{
	if (!closed() || index >= source_count_) { return false; }

	out = source_records_[index];
	return true;
}

bool NativeIngressDiagnosticOwner::copyTransaction(size_t index, NativeIngressTransactionRecord &out) const noexcept
{
	if (!closed() || index >= transaction_count_) { return false; }

	out = transactions_[index];
	return true;
}

bool NativeIngressDiagnosticOwner::copyClosure(NativeIngressClosureRecord &out) const noexcept
{
	if (!closed() || !has_closure_) { return false; }

	out = closure_;
	return true;
}

TrustedStateSample NativeIngressDiagnosticOwner::physical(const NativeSourceSnapshot &source,
		uint64_t time_us) const noexcept
{
	TrustedStateSample result{};
	result.generation = source.physical_generation;
	result.observer_continuous = true;
	result.request_guard_context.validator_receive_time_us = time_us;
	result.request_guard_context.arm_state = source.state.arming_state == 1 ? ArmState::Disarmed : ArmState::Armed;
	result.request_guard_context.mode_state = ResearchModeState::NotSelected;
	// The source knows neither estimator readiness, transport admission nor a map.
	result.map.controller_instance_id = config_.adapter_identity.controller_instance_id;
	return result;
}

void NativeIngressDiagnosticOwner::initial(RawState state, uint64_t hrt_us) noexcept
{
	observe({NativeSourceKind::Initial, state, state, hrt_us});
}

void NativeIngressDiagnosticOwner::transition(RawState before, RawState after, uint64_t hrt_us) noexcept
{
	observe({NativeSourceKind::Transition, before, after, hrt_us});
}

void NativeIngressDiagnosticOwner::sample(RawState state, uint64_t hrt_us) noexcept
{
	observe({NativeSourceKind::Sample, state, state, hrt_us});
}

void NativeIngressDiagnosticOwner::observe(const NativeSourceObservation &observation) noexcept
{
	if (closed()) { return; }

	const auto reject = [this, &observation](NativeIngressFault fault) {
		close(NativeIngressCloseCause::SourceContinuityLost, fault, observation, std::nullopt,
		      observation.source_time_us);
	};
	const bool initial_event = observation.kind == NativeSourceKind::Initial;

	if (!source_.initialized && !initial_event) { reject(NativeIngressFault::MissingInitial); return; }

	if (source_.initialized && initial_event) { reject(NativeIngressFault::RepeatedInitial); return; }

	if (!validState(observation.before) || !validState(observation.after)) {
		reject(NativeIngressFault::InvalidState); return;
	}

	if ((!initial_event && !sameState(observation.before, source_.state))
	    || (observation.kind != NativeSourceKind::Transition && !sameState(observation.before, observation.after))) {
		reject(NativeIngressFault::StateContradiction); return;
	}

	if (observation.source_time_us == 0) { reject(NativeIngressFault::TimeInvalid); return; }

	if (source_.initialized && observation.source_time_us < source_.checked_time_us) {
		reject(NativeIngressFault::TimeRegressed); return;
	}

	if (observation.kind == NativeSourceKind::Stop) {
		close(NativeIngressCloseCause::OwnerStop, NativeIngressFault::None, observation,
		      physical(source_, observation.source_time_us), observation.source_time_us);
		return;
	}

	const bool changed = observation.kind == NativeSourceKind::Transition
			     && !sameState(observation.before, observation.after);

	if (observation.kind == NativeSourceKind::Transition && !changed) {
		source_.checked_time_us = observation.source_time_us;
		return;
	}

	if (source_count_ >= source_records_.size()) {
		close(NativeIngressCloseCause::OwnerEvidenceExhausted, NativeIngressFault::OwnerEvidenceExhausted,
		      observation, std::nullopt, observation.source_time_us);
		return;
	}

	constexpr uint64_t maximum = std::numeric_limits<uint64_t>::max();

	if (!initial_event && (source_.sequence == maximum
			       || (changed && (source_.transition_epoch == maximum
					       || source_.physical_generation == maximum)))) {
		reject(NativeIngressFault::CounterExhausted);
		return;
	}

	auto &record = source_records_[source_count_];
	record.observation = observation;
	record.before = source_;
	record.after = source_;
	record.after.initialized = true;
	record.after.state = observation.after;
	record.after.sequence = initial_event ? 1 : source_.sequence + 1;
	record.after.transition_epoch = initial_event ? 1 : source_.transition_epoch + (changed ? 1 : 0);
	record.after.physical_generation = initial_event ? 1 : source_.physical_generation + (changed ? 1 : 0);
	record.after.committed_time_us = observation.source_time_us;
	record.after.checked_time_us = observation.source_time_us;
	record.adapter_input = physical(record.after, observation.source_time_us);
	record.result = adapter_.diagnosticTurn(record.adapter_input, DiagPoll{});
	const auto fault = unavailable(record.result);

	if (fault != NativeIngressFault::None || !negativeOnly(record.result)) {
		closure_.attempted = observation;
		closure_.adapter_input = record.adapter_input;
		closure_.result = record.result;
		closure_.cause = fault == NativeIngressFault::None
				 ? NativeIngressCloseCause::InvariantViolation : NativeIngressCloseCause::AdapterFault;
		closure_.fault = fault == NativeIngressFault::None ? NativeIngressFault::InvariantViolation : fault;
		closure_.retained_source = source_;
		first_fault_ = closure_.fault;
		has_closure_ = true;
		evidence_complete_ = false;
		state_ = NativeOwnerState::Faulted;
		mailbox_.terminal.store(true, std::memory_order_release);
		discardReadyOnClosure(closure_.cause, observation.source_time_us);
		return;
	}

	source_ = record.after;
	++source_count_;
	state_ = NativeOwnerState::Observing;
}

void NativeIngressDiagnosticOwner::close(NativeIngressCloseCause cause, NativeIngressFault fault,
		const std::optional<NativeSourceObservation> &attempted,
		const std::optional<TrustedStateSample> &input, uint64_t closing_hrt_us) noexcept
{
	if (closed()) { return; }

	mailbox_.terminal.store(true, std::memory_order_release);
	closure_.cause = cause;
	closure_.fault = fault;
	closure_.attempted = attempted;
	closure_.adapter_input = input;
	closure_.result = adapter_.closeDiagnosticLifecycle(input, adapterReason(cause));
	const auto adapter_fault = unavailable(closure_.result);

	if (adapter_fault != NativeIngressFault::None && fault == NativeIngressFault::None) { fault = adapter_fault; }

	if (fault == NativeIngressFault::None && attempted && cause == NativeIngressCloseCause::OwnerStop) {
		source_.checked_time_us = attempted->source_time_us;
	}

	closure_.retained_source = source_;
	closure_.fault = fault;
	first_fault_ = fault;
	has_closure_ = true;
	state_ = fault == NativeIngressFault::None ? NativeOwnerState::Stopped : NativeOwnerState::Faulted;
	discardReadyOnClosure(cause, closing_hrt_us);
}

void NativeIngressDiagnosticOwner::stopOrFault(NativeIngressCloseCause cause, RawState current,
		uint64_t hrt_us) noexcept
{
	if (closed()) { return; }

	if (cause == NativeIngressCloseCause::OwnerStop) {
		const auto async_fault = mailbox_.async_fault.load(std::memory_order_acquire);

		if (async_fault == NativeIngressAsyncFault::None) {
			observe({NativeSourceKind::Stop, current, current, hrt_us});
			return;
		}

		cause = async_fault == NativeIngressAsyncFault::TransportFault
			? NativeIngressCloseCause::TransportFault : NativeIngressCloseCause::WorkerResourceExhausted;
	}

	NativeIngressFault fault = NativeIngressFault::SourceLifetimeLost;

	if (cause == NativeIngressCloseCause::TransportFault) { fault = NativeIngressFault::TransportFault; }

	else if (cause == NativeIngressCloseCause::WorkerResourceExhausted) {
		fault = NativeIngressFault::WorkerResourceExhausted;
	}

	else if (cause == NativeIngressCloseCause::OwnerEvidenceExhausted) {
		fault = NativeIngressFault::OwnerEvidenceExhausted;
	}

	else if (cause == NativeIngressCloseCause::OwnerConfigurationInvalid) {
		fault = NativeIngressFault::InvalidConfiguration;
	}

	else if (cause == NativeIngressCloseCause::AdapterCapacityExhausted) {
		fault = NativeIngressFault::AdapterCapacityExhausted;
	}

	close(cause, fault, std::nullopt, std::nullopt, hrt_us);
}

bool NativeIngressDiagnosticOwner::projectStatus(const NativeIngressCandidate &candidate,
		const NativeSourceSnapshot &source, uint64_t evaluation_hrt_us, uint16_t reason,
		uint8_t outcome, NativeIngressStatus &status) noexcept
{
	if (!source.initialized || !validState(source.state) || candidate.token == 0
	    || candidate.arrival_native_us == 0 || candidate.arrival_native_us > evaluation_hrt_us
	    || source.checked_time_us > evaluation_hrt_us || source.sequence < source.physical_generation
	    || last_status_sequence_ == std::numeric_limits<uint64_t>::max()
	    || !((outcome == 1 && reason >= 1 && reason <= 5)
		 || (outcome == 2 && reason >= 6 && reason <= 8))) { return false; }

	NativeIngressStatus value{};
	value.native_run_id = config_.identity.native_run_id;
	value.owner_lifetime_id = config_.identity.owner_lifetime_id;
	value.controller_instance_id = config_.adapter_identity.controller_instance_id;
	value.source_id = config_.contract.permitted_source;
	value.transport_binding_id = config_.transport_binding_id;
	value.request_sequence = candidate.request_sequence;
	value.status_sequence = last_status_sequence_ + 1;
	value.transition_epoch = source.transition_epoch;
	value.source_sequence = source.sequence;
	value.state_generation = source.physical_generation;
	value.arrival_native_us = candidate.arrival_native_us;
	value.evaluation_native_us = evaluation_hrt_us;
	value.source_checked_native_us = source.checked_time_us;
	value.protocol_version = kWireProtocolVersionV1;
	value.reason = reason;
	value.domain = 1;
	value.profile = 1;
	value.execution_origin = kDiagnosticExecutionOrigin == DiagnosticExecutionOrigin::NativePosixSitl ? 2 : 1;
	value.outcome = outcome;
	value.arm_state = source.state.arming_state;
	value.raw_nav_state = source.state.nav_state;
	value.target_system = 42;
	value.target_component = 191;
	status = value;
	last_status_sequence_ = value.status_sequence;
	return true;
}

void NativeIngressDiagnosticOwner::discardReadyOnClosure(NativeIngressCloseCause cause,
		uint64_t hrt_us) noexcept
{
	auto expected = NativeIngressPhase::Ready;

	if (!mailbox_.phase.compare_exchange_strong(expected, NativeIngressPhase::Processing,
			std::memory_order_acq_rel, std::memory_order_acquire)) { return; }

	const auto &candidate = mailbox_.candidate;
	NativeIngressDisposition disposition{};
	disposition.token = candidate.token;
	disposition.kind = NativeIngressDispositionKind::DiscardedClosed;
	const uint16_t reason = closureWireReason(cause);

	if (reason != 0 && source_.initialized && boundCandidate(candidate, config_)) {
		disposition.has_status = projectStatus(candidate, source_, hrt_us, reason, 2, disposition.status);
	}

	mailbox_.disposition = disposition;
	mailbox_.phase.store(NativeIngressPhase::Resolved, std::memory_order_release);
}

void NativeIngressDiagnosticOwner::resolveReady(RawState current, uint64_t evaluation_hrt_us) noexcept
{
	if (closed()) { return; }

	const auto async_fault = mailbox_.async_fault.load(std::memory_order_acquire);

	if (async_fault != NativeIngressAsyncFault::None) {
		stopOrFault(async_fault == NativeIngressAsyncFault::TransportFault
			    ? NativeIngressCloseCause::TransportFault : NativeIngressCloseCause::WorkerResourceExhausted,
			    current, evaluation_hrt_us);
		return;
	}

	if (!source_.initialized) { return; }

	if (!sameState(current, source_.state)) {
		close(NativeIngressCloseCause::SourceContinuityLost, NativeIngressFault::StateContradiction,
		      std::nullopt, std::nullopt, evaluation_hrt_us);
		return;
	}

	if (evaluation_hrt_us == 0 || evaluation_hrt_us < source_.checked_time_us) {
		close(NativeIngressCloseCause::SourceContinuityLost,
		      evaluation_hrt_us == 0 ? NativeIngressFault::TimeInvalid : NativeIngressFault::TimeRegressed,
		      std::nullopt, std::nullopt, evaluation_hrt_us);
		return;
	}

	auto expected = NativeIngressPhase::Ready;

	if (!mailbox_.phase.compare_exchange_strong(expected, NativeIngressPhase::Processing,
			std::memory_order_acq_rel, std::memory_order_acquire)) { return; }

	const auto candidate = mailbox_.candidate;
	NativeIngressDisposition disposition{};
	disposition.token = candidate.token;
	disposition.kind = NativeIngressDispositionKind::DiscardedForeign;
	// Safety faults outrank even a malformed candidate after the Ready claim.
	const auto claimed_fault = mailbox_.async_fault.load(std::memory_order_acquire);

	if (claimed_fault != NativeIngressAsyncFault::None) {
		const auto cause = claimed_fault == NativeIngressAsyncFault::TransportFault
				   ? NativeIngressCloseCause::TransportFault
				   : NativeIngressCloseCause::WorkerResourceExhausted;
		stopOrFault(cause, current, evaluation_hrt_us);
		disposition.kind = NativeIngressDispositionKind::DiscardedClosed;

		if (cause == NativeIngressCloseCause::WorkerResourceExhausted
		    && boundCandidate(candidate, config_)) {
			disposition.has_status = projectStatus(candidate, source_, evaluation_hrt_us, 8, 2,
							       disposition.status);
		}

		mailbox_.disposition = disposition;
		mailbox_.phase.store(NativeIngressPhase::Resolved, std::memory_order_release);
		return;
	}

	if (!boundCandidate(candidate, config_)) {
		mailbox_.disposition = disposition;
		mailbox_.phase.store(NativeIngressPhase::Resolved, std::memory_order_release);
		return;
	}

	disposition.kind = NativeIngressDispositionKind::DiscardedStale;

	if (candidate.expected_transition_epoch != source_.transition_epoch || candidate.arrival_native_us == 0
	    || candidate.arrival_native_us > evaluation_hrt_us) {
		mailbox_.disposition = disposition;
		mailbox_.phase.store(NativeIngressPhase::Resolved, std::memory_order_release);
		return;
	}

	if (transaction_count_ >= transactions_.size()) {
		close(NativeIngressCloseCause::OwnerEvidenceExhausted, NativeIngressFault::OwnerEvidenceExhausted,
		      std::nullopt, std::nullopt, evaluation_hrt_us);
		// Processing is already claimed; close() cannot retire this candidate.
		disposition.kind = NativeIngressDispositionKind::DiscardedClosed;
		disposition.has_status = projectStatus(candidate, source_, evaluation_hrt_us, 8, 2, disposition.status);
		mailbox_.disposition = disposition;
		mailbox_.phase.store(NativeIngressPhase::Resolved, std::memory_order_release);
		return;
	}

	if (!adapter_.nativeNegativeTwoTurnCapacity()) {
		close(NativeIngressCloseCause::AdapterCapacityExhausted, NativeIngressFault::AdapterCapacityExhausted,
		      std::nullopt, std::nullopt, evaluation_hrt_us);
		disposition.kind = NativeIngressDispositionKind::Faulted;
		mailbox_.disposition = disposition;
		mailbox_.phase.store(NativeIngressPhase::Resolved, std::memory_order_release);
		return;
	}

	// Final fault acquire before the first adapter mutation is the ingress
	// linearization point. A later worker fault closes at the next boundary.
	const auto final_fault = mailbox_.async_fault.load(std::memory_order_acquire);

	if (final_fault != NativeIngressAsyncFault::None) {
		const auto cause = final_fault == NativeIngressAsyncFault::TransportFault
				   ? NativeIngressCloseCause::TransportFault
				   : NativeIngressCloseCause::WorkerResourceExhausted;
		stopOrFault(cause, current, evaluation_hrt_us);
		disposition.kind = NativeIngressDispositionKind::DiscardedClosed;

		if (cause == NativeIngressCloseCause::WorkerResourceExhausted) {
			disposition.has_status = projectStatus(candidate, source_, evaluation_hrt_us, 8, 2,
							       disposition.status);
		}

		mailbox_.disposition = disposition;
		mailbox_.phase.store(NativeIngressPhase::Resolved, std::memory_order_release);
		return;
	}

	auto &transaction = transactions_[transaction_count_];
	transaction.candidate = candidate;
	transaction.source = source_;
	transaction.poll_input = physical(source_, evaluation_hrt_us);
	transaction.poll_result = adapter_.diagnosticTurn(transaction.poll_input, DiagPoll{});
	const auto poll_fault = unavailable(transaction.poll_result);

	if (poll_fault != NativeIngressFault::None || !negativeOnly(transaction.poll_result)) {
		source_.checked_time_us = evaluation_hrt_us;
		transaction.kind = NativeIngressTransactionKind::PollOnlyFault;
		transaction.disposition = disposition;
		transaction.disposition.kind = NativeIngressDispositionKind::Faulted;
		++transaction_count_;
		closure_.cause = poll_fault == NativeIngressFault::None
				 ? NativeIngressCloseCause::InvariantViolation : NativeIngressCloseCause::AdapterFault;
		closure_.fault = poll_fault == NativeIngressFault::None ? NativeIngressFault::InvariantViolation : poll_fault;
		closure_.retained_source = source_;
		closure_.adapter_input = transaction.poll_input;
		closure_.result = transaction.poll_result;
		first_fault_ = closure_.fault;
		has_closure_ = true;
		evidence_complete_ = false;
		state_ = NativeOwnerState::Faulted;
		mailbox_.terminal.store(true, std::memory_order_release);
		mailbox_.disposition = transaction.disposition;
		mailbox_.phase.store(NativeIngressPhase::Resolved, std::memory_order_release);
		return;
	}

	transaction.request_input = transaction.poll_input;
	transaction.has_request = true;
	DecodedRequestV1 request{};
	request.wire = {candidate.source_id, candidate.transport_binding_id, candidate.request_sequence,
			candidate.producer_monotonic_us, candidate.producer_expiry_monotonic_us,
			candidate.controller_instance_id, candidate.expected_transition_epoch, 0,
			candidate.acceleration_x_frd_m_s2, candidate.acceleration_y_frd_m_s2,
			candidate.acceleration_z_frd_m_s2, candidate.protocol_version,
			candidate.frame, candidate.quantity, candidate.reference_point
		       };
	request.target_system = candidate.target_system;
	request.target_component = candidate.target_component;
	NativeDiagnosticIngress ingress{};
	ingress.owner_lifetime_id = config_.identity.owner_lifetime_id;
	ingress.native_run_id = config_.identity.native_run_id;
	ingress.authentication = {true, true, true, true};
	ingress.origin = kDiagnosticExecutionOrigin;
	transaction.request_result = adapter_.diagnosticTurn(transaction.request_input, DiagRequest{request, ingress});
	source_.checked_time_us = evaluation_hrt_us;
	const auto request_fault = unavailable(transaction.request_result);
	disposition.kind = NativeIngressDispositionKind::Completed;
	disposition.has_actual_request = true;

	if (request_fault == NativeIngressFault::None
	    && requestSelectionRequired(transaction.request_result, candidate, transaction.source, evaluation_hrt_us)) {
		disposition.has_status = projectStatus(candidate, transaction.source, evaluation_hrt_us, 4, 1,
						       disposition.status);
	}

	if (request_fault != NativeIngressFault::None || !disposition.has_status) {
		closure_.cause = request_fault == NativeIngressFault::None
				 ? NativeIngressCloseCause::InvariantViolation : NativeIngressCloseCause::AdapterFault;
		closure_.fault = request_fault == NativeIngressFault::None ? NativeIngressFault::InvariantViolation : request_fault;
		closure_.retained_source = source_;
		closure_.adapter_input = transaction.request_input;
		closure_.result = transaction.request_result;
		first_fault_ = closure_.fault;
		has_closure_ = true;
		evidence_complete_ = false;
		state_ = NativeOwnerState::Faulted;
		mailbox_.terminal.store(true, std::memory_order_release);
	}

	transaction.disposition = disposition;
	++transaction_count_;
	mailbox_.disposition = disposition;
	mailbox_.phase.store(NativeIngressPhase::Resolved, std::memory_order_release);
}

} // namespace research_sitl
