#include "HarnessSupervisorOwner.hpp"

namespace research_sitl
{
using namespace research_acceleration;

namespace
{
constexpr uint8_t kEstablishOperation = 1;
constexpr uint8_t kRevokeOperation = 2;

bool sameSession(const TrustedSession &a, const TrustedSession &b)
{
	return a.owner == b.owner && a.session == b.session;
}

bool allZero(const uint8_t reserved[4])
{
	return reserved[0] == 0 && reserved[1] == 0 && reserved[2] == 0 && reserved[3] == 0;
}

SupervisorAction actionFor(Intent intent)
{
	switch (intent) {
	case Intent::Establish: return SupervisorAction::Establish;

	case Intent::Revoke: return SupervisorAction::Revoke;

	case Intent::ClearLatch: return SupervisorAction::ClearLatchedFault;
	}

	return SupervisorAction::ClearLatchedFault;
}

bool validIntent(Intent intent)
{
	return intent == Intent::Establish || intent == Intent::Revoke || intent == Intent::ClearLatch;
}
}

HarnessSupervisorOwner::HarnessSupervisorOwner(const OwnerHarnessConfig &config) noexcept :
	config_(config), adapter_(config.adapter_contract, config.adapter_identity)
{
	if (config_.fixture_run_id == 0 || config_.maximum_receipt_age_us == 0
	    || config_.adapter_identity.controller_instance_id == 0 || !adapter_.configured()) {
		administration_disabled_ = true;
	}
}

bool HarnessSupervisorOwner::enter() noexcept
{
	if (in_call_) {
		return false;
	}

	in_call_ = true;
	return true;
}

void HarnessSupervisorOwner::leave() noexcept
{
	in_call_ = false;
}

void HarnessSupervisorOwner::disableAdministration() noexcept
{
#if defined(RESEARCH_SITL_DIAGNOSTIC_OFFLINE)

	if (diagnostic_domain_) { closeDiagnosticOnFault(); return; }

#endif
	administration_disabled_ = true;
	has_receipt_ = false;
}

bool HarnessSupervisorOwner::coherentTick(const FixtureStateReceipt &receipt) const noexcept
{
	const auto validator_time = receipt.sample.request_guard_context.validator_receive_time_us;

	if (config_.fixture_run_id == 0 || config_.maximum_receipt_age_us == 0
	    || config_.adapter_identity.controller_instance_id == 0 || receipt.clock.fixture_run_id != config_.fixture_run_id
	    || receipt.fixture_state_stream_id == 0 || receipt.clock.observation_id == 0
	    || receipt.clock.validator_time_us != validator_time
	    || (has_stream_id_ && receipt.fixture_state_stream_id != stream_id_)
	    || (has_observation_id_ && receipt.clock.observation_id <= observation_id_floor_)
	    || (has_view_ && validator_time < view_.validator_time_us)) {
		return false;
	}

	return true;
}

void HarnessSupervisorOwner::retainView(const AdapterSnapshot &snapshot, const TrustedStateSample &sample) noexcept
{
	const SessionSnapshot session = adapter_.sessionSnapshot();
	view_.ready = adapter_.configured() && snapshot.initialized && !snapshot.terminal;
	view_.terminal = snapshot.terminal;
	view_.controller_instance_id = config_.adapter_identity.controller_instance_id;
	view_.state_generation = snapshot.state_generation;
	view_.transition_epoch = snapshot.transition_epoch;
	view_.clock_mapping_epoch = sample.map.map_epoch;
	view_.validator_time_us = sample.request_guard_context.validator_receive_time_us;
	view_.has_session_history = session.has_last_successful_session;
	view_.last_session = session.last_successful_session;
	view_.has_supervisor_floor = session.has_supervisor_floor;
	view_.supervisor_floor = session.supervisor_floor;
	sample_ = sample;
	has_view_ = true;
}

bool HarnessSupervisorOwner::tick(const FixtureStateReceipt &receipt) noexcept
{
	return tick(receipt, nullptr);
}

bool HarnessSupervisorOwner::tick(const FixtureStateReceipt &receipt, AdapterResult *output) noexcept
{
#if defined(RESEARCH_SITL_DIAGNOSTIC_OFFLINE)

	if (diagnostic_domain_) { return false; }

#endif

	if (!enter()) {
		disableAdministration();
		return false;
	}

	if (!coherentTick(receipt)) {
		disableAdministration();
		leave();
		return false;
	}

	if (!has_stream_id_) {
		has_stream_id_ = true;
		stream_id_ = receipt.fixture_state_stream_id;
	}

	has_observation_id_ = true;
	observation_id_floor_ = receipt.clock.observation_id;
	const AdapterResult result = adapter_.poll(receipt.sample);
	retainView(result.snapshot, receipt.sample);

	if (output != nullptr) { *output = result; }

	if (!receipt.clock.continuous || !receipt.sample.observer_continuous) {
		disableAdministration();
	}

	leave();
	return !administration_disabled_;
}

bool HarnessSupervisorOwner::processAcknowledgement(const DecodedSessionV1 &packet,
		const SessionIngressVerdict &verdict, IngressResult &output) noexcept
{
#if defined(RESEARCH_SITL_DIAGNOSTIC_OFFLINE)

	if (diagnostic_domain_) { return false; }

#endif

	if (!enter()) { disableAdministration(); return false; }

	if (administration_disabled_ || !has_view_ || !view_.ready || view_.terminal) {
		leave();
		return false;
	}

	output = adapter_.processAcknowledgement(sample_, packet, verdict);
	retainView(output.snapshot, sample_);
	leave();
	return true;
}

bool HarnessSupervisorOwner::processRequest(const DecodedRequestV1 &packet,
		const SessionIngressVerdict &verdict, IngressResult &output) noexcept
{
#if defined(RESEARCH_SITL_DIAGNOSTIC_OFFLINE)

	if (diagnostic_domain_) { return false; }

#endif

	if (!enter()) { disableAdministration(); return false; }

	if (administration_disabled_ || !has_view_ || !view_.ready || view_.terminal) {
		leave();
		return false;
	}

	output = adapter_.processRequest(sample_, packet, verdict);
	retainView(output.snapshot, sample_);
	leave();
	return true;
}

bool HarnessSupervisorOwner::getEligibleRequest(CanonicalRequest &request, AdapterResult &output) noexcept
{
#if defined(RESEARCH_SITL_DIAGNOSTIC_OFFLINE)

	if (diagnostic_domain_) { return false; }

#endif

	if (!enter()) { disableAdministration(); return false; }

	if (administration_disabled_ || !has_view_ || !view_.ready || view_.terminal) {
		leave();
		return false;
	}

	output = adapter_.getEligibleRequest(sample_, request);
	retainView(output.snapshot, sample_);
	leave();
	return true;
}

bool HarnessSupervisorOwner::receiptAgeValid(const ImmutableIngressReceipt &receipt) const noexcept
{
	if (!has_view_ || receipt.received_validator_time_us > view_.validator_time_us) {
		return false;
	}

	return view_.validator_time_us - receipt.received_validator_time_us <= config_.maximum_receipt_age_us;
}

bool HarnessSupervisorOwner::offer(const ImmutableIngressReceipt &receipt) noexcept
{
	if (!enter()) {
		disableAdministration();
		return false;
	}

	if (receipt.receipt_id == 0 || receipt.receipt_id <= receipt_id_floor_) {
		leave();
		return false;
	}

	receipt_id_floor_ = receipt.receipt_id;
	const bool acceptable = !administration_disabled_ && !has_receipt_ && receipt.fixture_run_id == config_.fixture_run_id
				&& receiptAgeValid(receipt)
#if defined(RESEARCH_SITL_DIAGNOSTIC_OFFLINE)
				&& (!diagnostic_domain_ || adapter_.diagnosticSnapshot().operator_attached)
#endif
				;

	if (acceptable) {
		receipt_ = receipt;
		has_receipt_ = true;
	}

	leave();
	return acceptable;
}

bool HarnessSupervisorOwner::snapshot(OwnerView &out) noexcept
{
#if defined(RESEARCH_SITL_DIAGNOSTIC_OFFLINE)

	if (diagnostic_domain_) { return false; }

#endif

	if (!enter()) {
		disableAdministration();
		return false;
	}

	if (!has_view_ || administration_disabled_) {
		leave();
		return false;
	}

	out = view_;
	leave();
	return true;
}

bool HarnessSupervisorOwner::sameView(const OwnerView &a, const OwnerView &b) const noexcept
{
	return a.ready == b.ready && a.terminal == b.terminal
	       && a.controller_instance_id == b.controller_instance_id && a.state_generation == b.state_generation
	       && a.transition_epoch == b.transition_epoch && a.clock_mapping_epoch == b.clock_mapping_epoch
	       && a.validator_time_us == b.validator_time_us && a.has_session_history == b.has_session_history
	       && sameSession(a.last_session, b.last_session) && a.has_supervisor_floor == b.has_supervisor_floor
	       && a.supervisor_floor == b.supervisor_floor;
}

bool HarnessSupervisorOwner::validIngress(const SessionIngressVerdict &verdict) const noexcept
{
	return verdict.mavlink2 && verdict.parser_authenticated && verdict.configured_header_channel_source_binding
	       && verdict.configured_target_binding;
}

bool HarnessSupervisorOwner::validPacketShape(const DecodedSessionV1 &packet, Intent intent) const noexcept
{
	const uint8_t expected_operation = intent == Intent::Establish ? kEstablishOperation : kRevokeOperation;
	return intent != Intent::ClearLatch && packet.protocol_version == kWireProtocolVersionV1
	       && packet.controller_instance_id != 0 && packet.transition_epoch != 0 && packet.clock_mapping_epoch != 0
	       && packet.source != 0 && packet.session != 0 && packet.target_system != 0 && packet.target_component != 0
	       && packet.producer_acknowledgement_monotonic_us == 0 && packet.acknowledged_status_sequence == 0
	       && packet.flags == 0 && allZero(packet.reserved) && packet.operation == expected_operation;
}

bool HarnessSupervisorOwner::validReceiptForSubmission(const Submission &submission) const noexcept
{
	if (!has_receipt_ || !receiptAgeValid(receipt_) || !validIngress(receipt_.verdict)
	    || !validPacketShape(receipt_.packet, submission.intent)) {
		return false;
	}

	const auto &packet = receipt_.packet;
	const auto &map = sample_.map;
	return packet.source == submission.requested_session.owner && packet.session == submission.requested_session.session
	       && packet.controller_instance_id == view_.controller_instance_id
	       && packet.transition_epoch == view_.transition_epoch && packet.clock_mapping_epoch == view_.clock_mapping_epoch
	       && map.valid && map.source == packet.source && map.session == packet.session
	       && map.controller_instance_id == packet.controller_instance_id && map.map_epoch == packet.clock_mapping_epoch;
}

OwnerOutcome HarnessSupervisorOwner::submit(const Submission &submission, AdminResult &out) noexcept
{
#if defined(RESEARCH_SITL_DIAGNOSTIC_OFFLINE)

	if (diagnostic_domain_) { return OwnerOutcome::NotSubmitted; }

#endif

	if (!enter()) {
		disableAdministration();
		return OwnerOutcome::NotSubmitted;
	}

	if (administration_disabled_ || !has_view_ || !view_.ready || view_.terminal || !validIntent(submission.intent)
	    || !sameView(view_, submission.expected_view)
	    || submission.event.event_id == 0 || submission.event.expected_controller_instance_id != view_.controller_instance_id
	    || submission.event.expected_state_generation != view_.state_generation
	    || submission.event.expected_transition_epoch != view_.transition_epoch
	    || submission.event.action != actionFor(submission.intent)) {
		leave();
		return OwnerOutcome::NotSubmitted;
	}

	if (submission.intent == Intent::ClearLatch) {
		const bool expected_binding = view_.has_session_history;

		if (submission.requested_session.owner != 0 || submission.requested_session.session != 0
		    || submission.event.has_session_binding != expected_binding
		    || (expected_binding && !sameSession(submission.event.session, view_.last_session))
		    || (!expected_binding && (submission.event.session.owner != 0 || submission.event.session.session != 0))) {
			leave();
			return OwnerOutcome::NotSubmitted;
		}

		out = adapter_.clearLatchedFault(sample_, submission.event);
		retainView(out.snapshot, sample_);
		leave();
		return OwnerOutcome::Completed;
	}

	if (!submission.event.has_session_binding || !sameSession(submission.event.session, submission.requested_session)
	    || !validReceiptForSubmission(submission)) {
		if (has_receipt_) {
			has_receipt_ = false;
		}

		leave();
		return OwnerOutcome::NotSubmitted;
	}

	const ImmutableIngressReceipt consumed = receipt_;
	has_receipt_ = false;
	out = adapter_.processSession(sample_, consumed.packet, consumed.verdict, &submission.event);
	retainView(out.snapshot, sample_);
	const bool operation_sequence_matches = out.correlation.has_packet
						&& out.correlation.packet_source == consumed.packet.source
						&& out.correlation.packet_session == consumed.packet.session
						&& out.correlation.packet_operation_sequence == consumed.packet.operation_sequence;

	if (!operation_sequence_matches) {
		disableAdministration();
		leave();
		return OwnerOutcome::Unknown;
	}

	leave();
	return OwnerOutcome::Completed;
}

} // namespace research_sitl
