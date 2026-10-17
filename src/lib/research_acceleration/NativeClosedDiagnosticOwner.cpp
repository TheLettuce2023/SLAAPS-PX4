#include "NativeClosedDiagnosticOwner.hpp"

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
	// Pinned native vehicle_status values, not a claim of flight authority.
	return (state.arming_state == 1 || state.arming_state == 2) && state.nav_state < 31
	       && state.nav_state != 9 && state.nav_state != 11 && state.nav_state != 16;
}

bool matchesExactFiniteValue(float value, float expected) noexcept
{
	// Exact profile identity, not an approximate physical-value comparison.
	return std::isfinite(value) && value >= expected && value <= expected;
}

bool validConfiguration(const NativeClosedOwnerConfig &config) noexcept
{
	const auto &c = config.contract;

	if (c.protocol_version != kWireProtocolVersionV1 || c.permitted_source == 0
	    || c.disarmed_policy != DisarmedPolicy::ValidateOnly || !matchesExactFiniteValue(c.acceleration_bounds.maximum_norm_m_s2, 12.F)
	    || c.timing.minimum_validity_us != 80000 || c.timing.maximum_validity_us != 80000
	    || c.timing.maximum_command_age_us != 60000 || c.timing.maximum_future_skew_us != 8000
	    || c.timing.maximum_clock_uncertainty_us != 4000 || !matchesExactFiniteValue(c.quaternion_norm_tolerance, 0.01F)
	    || config.adapter_identity.controller_instance_id == 0 || config.identity.owner_lifetime_id == 0
	    || config.identity.native_run_id == 0 || config.identity.origin != kDiagnosticExecutionOrigin
	    || config.producer_lifetime_id == 0 || config.clock_epoch == 0) { return false; }

	for (size_t axis = 0; axis < 3; ++axis) {
		if (!matchesExactFiniteValue(c.acceleration_bounds.minimum_m_s2(axis), -10.F)
		    || !matchesExactFiniteValue(c.acceleration_bounds.maximum_m_s2(axis), 10.F)) { return false; }
	}

	return true;
}

NativeOwnerFault unavailable(const DiagnosticResult &result) noexcept
{
	if (result.code == DiagnosticCode::Terminal || result.snapshot.terminal) { return NativeOwnerFault::AdapterTerminal; }

	if (result.code == DiagnosticCode::NotConfigured || result.code == DiagnosticCode::WrongDomain) {
		return NativeOwnerFault::AdapterUnavailable;
	}

	return NativeOwnerFault::None;
}
} // namespace

NativeClosedDiagnosticOwner::NativeClosedDiagnosticOwner(const NativeClosedOwnerConfig &config) noexcept :
	config_(config), adapter_(config.contract, config.adapter_identity, DiagnosticNativeSitlTag{}, config.identity)
{
	if (!validConfiguration(config)) {
		fail(NativeOwnerFault::InvalidConfiguration, std::nullopt, DiagnosticCloseReason::OwnerConfigurationInvalid);
	}
}

bool NativeClosedDiagnosticOwner::closed() const noexcept
{
	return state_ == NativeOwnerState::Stopped || state_ == NativeOwnerState::Faulted;
}

NativeOwnerSummary NativeClosedDiagnosticOwner::summary() const noexcept
{
	return {state_, first_fault_, source_, record_count_, has_closure_};
}

bool NativeClosedDiagnosticOwner::copyRecord(size_t index, NativeSourceRecord &out) const noexcept
{
	if (!closed() || index >= record_count_) { return false; }

	out = records_[index];
	return true;
}

bool NativeClosedDiagnosticOwner::copyClosure(NativeClosureRecord &out) const noexcept
{
	if (!closed() || !has_closure_) { return false; }

	out = closure_;
	return true;
}

TrustedStateSample NativeClosedDiagnosticOwner::physical(const NativeSourceSnapshot &source, uint64_t time_us) const noexcept
{
	TrustedStateSample result{};
	result.generation = source.physical_generation;
	result.observer_continuous = true;
	result.request_guard_context.validator_receive_time_us = time_us;
	result.request_guard_context.arm_state = source.state.arming_state == 1 ? ArmState::Disarmed : ArmState::Armed;
	result.request_guard_context.mode_state = ResearchModeState::NotSelected;
	// Required state, transport and map validity are deliberately unavailable.
	result.map.controller_instance_id = config_.adapter_identity.controller_instance_id;
	return result;
}

void NativeClosedDiagnosticOwner::finishAdapterClosure(NativeOwnerFault fault) noexcept
{
	first_fault_ = fault;
	closure_.fault = fault;
	closure_.retained_source = source_;
	has_closure_ = true;
	state_ = fault == NativeOwnerFault::None ? NativeOwnerState::Stopped : NativeOwnerState::Faulted;
}

void NativeClosedDiagnosticOwner::fail(NativeOwnerFault fault,
				       const std::optional<NativeSourceObservation> &attempted, DiagnosticCloseReason reason) noexcept
{
	if (closed()) { return; }

	closure_.attempted = attempted;
	closure_.adapter_input = std::nullopt;
	closure_.result = adapter_.closeDiagnosticLifecycle(std::nullopt, reason);
	finishAdapterClosure(fault);
}

void NativeClosedDiagnosticOwner::initial(RawState state, uint64_t time_us) noexcept
{
	observe({NativeSourceKind::Initial, state, state, time_us});
}

void NativeClosedDiagnosticOwner::transition(RawState before, RawState after, uint64_t time_us) noexcept
{
	observe({NativeSourceKind::Transition, before, after, time_us});
}

void NativeClosedDiagnosticOwner::sample(RawState state, uint64_t time_us) noexcept
{
	observe({NativeSourceKind::Sample, state, state, time_us});
}

void NativeClosedDiagnosticOwner::stop(RawState state, uint64_t time_us) noexcept
{
	observe({NativeSourceKind::Stop, state, state, time_us});
}

void NativeClosedDiagnosticOwner::sourceContinuityLost() noexcept
{
	fail(NativeOwnerFault::SourceLifetimeLost, std::nullopt, DiagnosticCloseReason::SourceContinuityLost);
}

void NativeClosedDiagnosticOwner::observe(const NativeSourceObservation &observation) noexcept
{
	if (closed()) { return; }

	const auto reject = [this, &observation](NativeOwnerFault fault) {
		fail(fault, observation, DiagnosticCloseReason::SourceContinuityLost);
	};
	const bool initial_event = observation.kind == NativeSourceKind::Initial;

	if (!source_.initialized && !initial_event) { reject(NativeOwnerFault::MissingInitial); return; }

	if (source_.initialized && initial_event) { reject(NativeOwnerFault::RepeatedInitial); return; }

	if (!validState(observation.before) || !validState(observation.after)) {
		reject(NativeOwnerFault::InvalidState); return;
	}

	if ((!initial_event && !sameState(observation.before, source_.state))
	    || (observation.kind != NativeSourceKind::Transition && !sameState(observation.before, observation.after))) {
		reject(NativeOwnerFault::StateContradiction); return;
	}

	if (observation.source_time_us == 0) { reject(NativeOwnerFault::TimeInvalid); return; }

	if (source_.initialized && observation.source_time_us < source_.checked_time_us) {
		reject(NativeOwnerFault::TimeRegressed); return;
	}

	if (observation.kind == NativeSourceKind::Stop) {
		closure_.attempted = observation;
		closure_.adapter_input = physical(source_, observation.source_time_us);
		closure_.result = adapter_.closeDiagnosticLifecycle(closure_.adapter_input, DiagnosticCloseReason::OwnerStop);
		const auto fault = unavailable(closure_.result);

		if (fault == NativeOwnerFault::None) { source_.checked_time_us = observation.source_time_us; }

		finishAdapterClosure(fault);
		return;
	}

	const bool changed = observation.kind == NativeSourceKind::Transition && !sameState(observation.before, observation.after);

	if (observation.kind == NativeSourceKind::Transition && !changed) {
		// S2-compatible assignment chronology: no receipt, poll, or generation.
		source_.checked_time_us = observation.source_time_us;
		return;
	}

	if (record_count_ >= records_.size()) {
		fail(NativeOwnerFault::EvidenceExhausted, observation, DiagnosticCloseReason::OwnerEvidenceExhausted);
		return;
	}

	constexpr uint64_t maximum = std::numeric_limits<uint64_t>::max();

	if (!initial_event && (source_.sequence == maximum
			       || (changed && (source_.transition_epoch == maximum || source_.physical_generation == maximum)))) {
		reject(NativeOwnerFault::CounterExhausted); return;
	}

	// This reserved slot is invisible to readers until committed. No whole trace
	// record is placed on the caller's stack, and a terminal result is not replayed.
	auto &record = records_[record_count_];
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

	if (fault != NativeOwnerFault::None) {
		closure_.attempted = observation;
		closure_.adapter_input = record.adapter_input;
		closure_.result = record.result;
		finishAdapterClosure(fault);
		return;
	}

	source_ = record.after;
	++record_count_;
	state_ = NativeOwnerState::Observing;
}

} // namespace research_sitl
