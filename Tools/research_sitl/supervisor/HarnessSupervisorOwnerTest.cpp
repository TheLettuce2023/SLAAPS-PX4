#include "HarnessSupervisorOwner.hpp"

#include <iostream>
#include <limits>
#include <string>
#include <type_traits>
#include <utility>

using namespace research_acceleration;
using namespace research_sitl;

namespace research_sitl
{
struct HarnessSupervisorOwnerTestPeer {
	static uint64_t retainedTime(const HarnessSupervisorOwner &owner) { return owner.view_.validator_time_us; }
	static uint64_t receiptFloor(const HarnessSupervisorOwner &owner) { return owner.receipt_id_floor_; }
	static const WireAdapter *adapterAddress(const HarnessSupervisorOwner &owner) { return &owner.adapter_; }
	static void latchBeforeFirstTick(HarnessSupervisorOwner &owner, const TrustedStateSample &first,
					 const TrustedStateSample &regressed)
	{
		owner.adapter_.poll(first);
		owner.adapter_.poll(regressed);
	}
};
} // namespace research_sitl

namespace
{
constexpr uint64_t kController = 11;
constexpr SourceId kSource = 21;
constexpr SessionId kSession = 31;
constexpr uint64_t kMapEpoch = 41;
constexpr uint64_t kFirstGeneration = 51;
constexpr uint64_t kSecondGeneration = 52;
constexpr uint64_t kRun = 71;
constexpr uint64_t kStream = 81;
constexpr TimeUs kInitialTime = 1000;
constexpr TimeUs kArmTime = 1100;
constexpr uint64_t kMaximumReceiptAge = 10;
constexpr uint64_t kFirstOperator = 1;
constexpr uint64_t kSecondOperator = 2;
constexpr uint64_t kFirstObservation = 1;
constexpr uint64_t kSecondObservation = 2;
constexpr uint64_t kThirdObservation = 3;
constexpr uint64_t kFirstReceipt = 1;
constexpr uint64_t kSecondReceipt = 2;
constexpr uint64_t kThirdReceipt = 3;
constexpr uint64_t kFourthReceipt = 4;
constexpr uint8_t kEstablishOperation = 1;
constexpr uint8_t kRevokeOperation = 2;
constexpr uint64_t kFirstOperationSequence = 0;
constexpr uint64_t kSecondOperationSequence = 1;
constexpr uint64_t kZero = 0;

int checks = 0;
int failures = 0;

void check(bool condition, const std::string &name)
{
	++checks;

	if (!condition) {
		++failures;
		std::cerr << "FAILED: " << name << '\n';
	}
}

template<typename T, typename = void>
struct HasReset : std::false_type {};

template<typename T>
struct HasReset<T, std::void_t<decltype(std::declval<T &>().reset())>> : std::true_type {};

ContractConfig contract()
{
	return {kWireProtocolVersionV1, kSource, DisarmedPolicy::ValidateOnly,
		{Vec3f(-10.F, -10.F, -10.F), Vec3f(10.F, 10.F, 10.F), 12.F},
		{10, 100, 50, 10, 5}, 0.01F};
}

OwnerHarnessConfig ownerConfig()
{
	return {contract(), {kController}, kRun, kMaximumReceiptAge};
}

TrustedStateSample state(uint64_t generation, TimeUs time, ArmState arm = ArmState::Disarmed)
{
	TrustedStateSample value{};
	value.generation = generation;
	value.observer_continuous = true;
	value.request_guard_context = {time, arm, ResearchModeState::Selected, true, true, true};
	value.map = {true, kSource, kSession, kController, kMapEpoch, 0, 2, 0, 2000};
	return value;
}

FixtureStateReceipt tickReceipt(uint64_t observation, uint64_t generation, TimeUs time,
				bool continuous = true, uint64_t stream = kStream, uint64_t run = kRun)
{
	return {state(generation, time), {run, observation, time, continuous}, stream};
}

DecodedSessionV1 packet(uint64_t epoch, uint8_t operation, uint64_t sequence)
{
	return {kController, epoch, kMapEpoch, kSource, kSession, sequence, 0, 0,
		kWireProtocolVersionV1, 1, 1, operation, 0, {0, 0, 0, 0}};
}

ImmutableIngressReceipt receipt(uint64_t identifier, TimeUs received, uint64_t epoch,
				uint8_t operation, uint64_t sequence, uint64_t run = kRun)
{
	return {run, identifier, received, packet(epoch, operation, sequence), {true, true, true, true}};
}

bool initialTick(HarnessSupervisorOwner &owner)
{
	return owner.tick(tickReceipt(kFirstObservation, kFirstGeneration, kInitialTime));
}

bool sameView(const OwnerView &left, const OwnerView &right)
{
	return left.ready == right.ready && left.terminal == right.terminal
	       && left.controller_instance_id == right.controller_instance_id
	       && left.state_generation == right.state_generation
	       && left.transition_epoch == right.transition_epoch
	       && left.clock_mapping_epoch == right.clock_mapping_epoch
	       && left.validator_time_us == right.validator_time_us
	       && left.has_session_history == right.has_session_history
	       && left.last_session.owner == right.last_session.owner
	       && left.last_session.session == right.last_session.session
	       && left.has_supervisor_floor == right.has_supervisor_floor
	       && left.supervisor_floor == right.supervisor_floor;
}

bool resultIs(const ConsoleResult &result, ConsoleDisposition disposition, BrokerCode code)
{
	return result.disposition == disposition && result.code == code;
}

void establishRevokeAndFacade()
{
	static_assert(!std::is_copy_constructible<HarnessSupervisorOwner>::value, "owner must not copy");
	static_assert(!std::is_move_constructible<HarnessSupervisorOwner>::value, "owner must not move");
	static_assert(!HasReset<HarnessSupervisorOwner>::value && !HasReset<SupervisorConsoleFacade>::value,
		      "O2 has no reset API");
	static_assert(std::is_same < decltype(std::declval<SupervisorConsoleFacade &>().confirm(uint64_t{})),
		      ConsoleResult >::value, "facade returns redacted ConsoleResult only");

	HarnessSupervisorOwner owner(ownerConfig());
	SupervisorBroker broker({kMaximumReceiptAge}, owner);
	SupervisorConsoleFacade facade(broker);
	check(!facade.attachFixtureOperator(kZero) && facade.attachFixtureOperator(kFirstOperator), "facade_attach");
	check(initialTick(owner), "establish_tick");
	check(owner.offer(receipt(kFirstReceipt, kInitialTime, 1, kEstablishOperation, kFirstOperationSequence)),
	      "establish_evidence");
	const auto establish_pending = facade.prepare(Intent::Establish, {kSource, kSession});
	const auto established = facade.confirm(establish_pending.ticket);
	AdminResult audit{};
	check(resultIs(established, ConsoleDisposition::Submitted, BrokerCode::Submitted)
	      && established.has_adapter_code && established.adapter_code == AdminCode::Completed
	      && established.event_consumed && broker.copyLastAdminResult(audit)
	      && audit.code == AdminCode::Completed && audit.correlation.supervisor_event_consumed
	      && audit.action_performed && audit.action_kind == AdminActionKind::EstablishTrustedSession,
	      "real_establish_preserves_adapter_result");

	auto armed = tickReceipt(kSecondObservation, kSecondGeneration, kArmTime);
	armed.sample.request_guard_context.arm_state = ArmState::Armed;
	check(owner.tick(armed), "armed_transition_tick");
	check(owner.offer(receipt(kSecondReceipt, kArmTime, 3, kRevokeOperation, kSecondOperationSequence)),
	      "revoke_evidence");
	const auto revoke_pending = facade.prepare(Intent::Revoke, {kSource, kSession});
	const auto armed_rejection = facade.confirm(revoke_pending.ticket);
	check(resultIs(armed_rejection, ConsoleDisposition::Submitted, BrokerCode::Submitted)
	      && armed_rejection.has_adapter_code && armed_rejection.adapter_code == AdminCode::ContextRejected
	      && armed_rejection.event_consumed && broker.copyLastAdminResult(audit)
	      && !audit.action_performed && audit.session.gate == SessionGate::Established
	      && audit.session.supervisor_floor == 2 && audit.snapshot.transition_epoch == 3,
	      "armed_revoke_preserves_consumed_context_rejection");
	check(!owner.offer(receipt(kSecondReceipt, kArmTime, 3, kRevokeOperation, kSecondOperationSequence)),
	      "consumed_rejection_receipt_cannot_retry");
	check(owner.tick(tickReceipt(kThirdObservation, kSecondGeneration + 1, kArmTime + 1)),
	      "disarm_transition_tick");
	check(owner.offer(receipt(kThirdReceipt, kArmTime + 1, 4, kRevokeOperation, kSecondOperationSequence)),
	      "fresh_disarmed_revoke_evidence");
	const auto revoked = facade.confirm(facade.prepare(Intent::Revoke, {kSource, kSession}).ticket);
	check(resultIs(revoked, ConsoleDisposition::Submitted, BrokerCode::Submitted)
	      && revoked.has_adapter_code && revoked.adapter_code == AdminCode::Completed
	      && revoked.event_consumed && broker.copyLastAdminResult(audit)
	      && audit.session.gate == SessionGate::Revoked && audit.action_performed
	      && audit.action_kind == AdminActionKind::Revoke && audit.snapshot.transition_epoch == 5
	      && audit.session.supervisor_floor == 3 && audit.report_count == 2
	      && audit.reports[0].kind == AdminReportKind::AdministrativeBarrier
	      && audit.reports[1].kind == AdminReportKind::Operation
	      && audit.reports[1].report_sequence == audit.reports[0].report_sequence + 1,
	      "real_revoke_preserves_adapter_result");

	OwnerView before_detach{};
	OwnerView after_detach{};
	check(owner.snapshot(before_detach) && facade.detachFixtureOperator()
	      && facade.attachFixtureOperator(kSecondOperator) && owner.snapshot(after_detach)
	      && sameView(before_detach, after_detach)
	      && !owner.offer(receipt(kSecondReceipt, kArmTime, 3, kRevokeOperation, kSecondOperationSequence)),
	      "detach_does_not_reset_owner_or_replay_evidence");
}

void clearAndImmutableEvidence()
{
	HarnessSupervisorOwner unlatched(ownerConfig());
	SupervisorBroker reject_broker({kMaximumReceiptAge}, unlatched);
	SupervisorConsoleFacade reject_facade(reject_broker);
	check(reject_facade.attachFixtureOperator(kFirstOperator) && initialTick(unlatched), "unlatched_setup");
	const auto rejected = reject_facade.confirm(reject_facade.prepare(Intent::ClearLatch, {0, 0}).ticket);
	check(resultIs(rejected, ConsoleDisposition::Submitted, BrokerCode::Submitted)
	      && rejected.has_adapter_code && rejected.adapter_code == AdminCode::ClearNotLatched
	      && rejected.event_consumed, "consumed_clear_rejection");

	HarnessSupervisorOwner latched(ownerConfig());
	auto first = state(kFirstGeneration, kInitialTime);
	auto regressed = first;
	regressed.request_guard_context.validator_receive_time_us = kInitialTime - 1;
	HarnessSupervisorOwnerTestPeer::latchBeforeFirstTick(latched, first, regressed);
	SupervisorBroker clear_broker({kMaximumReceiptAge}, latched);
	SupervisorConsoleFacade clear_facade(clear_broker);
	check(clear_facade.attachFixtureOperator(kFirstOperator) && initialTick(latched), "latched_fixture_setup");
	const auto cleared = clear_facade.confirm(clear_facade.prepare(Intent::ClearLatch, {0, 0}).ticket);
	AdminResult audit{};
	check(resultIs(cleared, ConsoleDisposition::Submitted, BrokerCode::Submitted)
	      && cleared.has_adapter_code && cleared.adapter_code == AdminCode::Completed && cleared.event_consumed
	      && clear_broker.copyLastAdminResult(audit) && audit.action_performed
	      && audit.action_kind == AdminActionKind::ClearLatch, "real_clear_pre_latched_only");

	HarnessSupervisorOwner immutable(ownerConfig());
	SupervisorBroker immutable_broker({kMaximumReceiptAge}, immutable);
	SupervisorConsoleFacade immutable_facade(immutable_broker);
	check(immutable_facade.attachFixtureOperator(kFirstOperator) && initialTick(immutable), "immutable_setup");
	auto offered = receipt(kFirstReceipt, kInitialTime, 1, kEstablishOperation, kFirstOperationSequence);
	check(immutable.offer(offered), "immutable_offer");
	offered.packet.operation = kRevokeOperation;
	offered.verdict.parser_authenticated = false;
	const auto result = immutable_facade.confirm(immutable_facade.prepare(Intent::Establish, {kSource, kSession}).ticket);
	check(result.adapter_code == AdminCode::Completed && result.event_consumed, "immutable_receipt_copy");
}

void slotAndReceiptTime()
{
	HarnessSupervisorOwner owner(ownerConfig());
	SupervisorBroker broker({kMaximumReceiptAge}, owner);
	SupervisorConsoleFacade facade(broker);
	check(facade.attachFixtureOperator(kFirstOperator) && initialTick(owner), "slot_setup");
	auto wrong_run = receipt(kFirstReceipt, kInitialTime, 1, kEstablishOperation, kFirstOperationSequence, kRun + 1);
	check(!owner.offer(wrong_run), "wrong_run_rejected");
	wrong_run.fixture_run_id = kRun;
	check(!owner.offer(wrong_run) && HarnessSupervisorOwnerTestPeer::receiptFloor(owner) == kFirstReceipt,
	      "rejected_identifier_is_not_reusable");
	check(owner.offer(receipt(kSecondReceipt, kInitialTime, 1, kEstablishOperation, kFirstOperationSequence)),
	      "first_slot_offer");
	check(!owner.offer(receipt(kThirdReceipt, kInitialTime, 1, kEstablishOperation, kFirstOperationSequence)),
	      "one_slot_offer_rejected");
	check(facade.confirm(facade.prepare(Intent::Establish, {kSource, kSession}).ticket).event_consumed,
	      "first_slot_dispatched_once");
	check(!owner.offer(receipt(kThirdReceipt, kInitialTime, 2, kRevokeOperation, kSecondOperationSequence))
	      && owner.offer(receipt(kFourthReceipt, kInitialTime, 2, kRevokeOperation, kSecondOperationSequence)),
	      "full_slot_identifier_cannot_replay_after_dispatch");

	HarnessSupervisorOwner inclusive(ownerConfig());
	HarnessSupervisorOwner expired(ownerConfig());
	HarnessSupervisorOwner future(ownerConfig());
	check(initialTick(inclusive)
	      && inclusive.offer(receipt(kFirstReceipt, kInitialTime - kMaximumReceiptAge, 1,
					 kEstablishOperation, kFirstOperationSequence)), "inclusive_age");
	check(initialTick(expired)
	      && !expired.offer(receipt(kFirstReceipt, kInitialTime - kMaximumReceiptAge - 1, 1,
					kEstablishOperation, kFirstOperationSequence)), "expired_age");
	check(initialTick(future)
	      && !future.offer(receipt(kFirstReceipt, kInitialTime + 1, 1,
				       kEstablishOperation, kFirstOperationSequence)), "future_age");

	HarnessSupervisorOwner maximum(ownerConfig());
	const TimeUs maximum_time = std::numeric_limits<TimeUs>::max();
	check(maximum.tick(tickReceipt(kFirstObservation, kFirstGeneration, maximum_time))
	      && maximum.offer(receipt(kFirstReceipt, maximum_time - kMaximumReceiptAge, 1,
				       kEstablishOperation, kFirstOperationSequence)), "maximum_time_checked_subtraction");
}

void provenanceAndNoDispatch()
{
	HarnessSupervisorOwner owner(ownerConfig());
	check(initialTick(owner), "provenance_setup");
	check(!owner.tick(tickReceipt(kSecondObservation, kFirstGeneration, kInitialTime + 1, false)),
	      "continuity_disables_admin");
	OwnerView unchanged{};
	unchanged.validator_time_us = 77;
	check(!owner.snapshot(unchanged) && unchanged.validator_time_us == 77
	      && !owner.tick(tickReceipt(kThirdObservation, kFirstGeneration, kInitialTime + 2))
	      && HarnessSupervisorOwnerTestPeer::retainedTime(owner) == kInitialTime + 2,
	      "polling_continues_after_permanent_disable");

	for (unsigned fault = 0; fault < 6; ++fault) {
		HarnessSupervisorOwner invalid(ownerConfig());
		check(initialTick(invalid), "provenance_baseline");
		auto next = tickReceipt(kSecondObservation, kFirstGeneration, kInitialTime + 1);

		if (fault == 0) { next.clock.fixture_run_id = kRun + 1; }

		if (fault == 1) { next.fixture_state_stream_id = kStream + 1; }

		if (fault == 2) { next.clock.observation_id = kFirstObservation; }

		if (fault == 3) { next.clock.validator_time_us = kInitialTime; }

		if (fault == 4) {
			next.sample.request_guard_context.validator_receive_time_us = kInitialTime - 1;
			next.clock.validator_time_us = kInitialTime - 1;
		}

		if (fault == 5) { next.sample.observer_continuous = false; }

		check(!invalid.tick(next), "provenance_rejected");
	}

	HarnessSupervisorOwner direct(ownerConfig());
	check(initialTick(direct), "direct_submit_setup");
	OwnerView view{};
	check(direct.snapshot(view), "direct_view");
	AdminResult sentinel{};
	sentinel.code = AdminCode::Completed;
	sentinel.report_count = 2;
	Submission mismatched{Intent::ClearLatch, {0, 0}, view,
		{
			1, view.state_generation, view.transition_epoch, kController,
			SupervisorAction::ClearLatchedFault, false, {0, 0}
		}};
	mismatched.expected_view.clock_mapping_epoch += 1;
	Submission clear_tuple{Intent::ClearLatch, {kSource, kSession}, view,
		{
			1, view.state_generation, view.transition_epoch, kController,
			SupervisorAction::ClearLatchedFault, false, {0, 0}
		}};
	Submission unknown{static_cast<Intent>(99), {kSource, kSession}, view,
		{
			1, view.state_generation, view.transition_epoch, kController,
			SupervisorAction::ClearLatchedFault, false, {0, 0}
		}};
	Submission no_evidence{Intent::Establish, {kSource, kSession}, view,
		{
			1, view.state_generation, view.transition_epoch, kController,
			SupervisorAction::Establish, true, {kSource, kSession}
		}};
	check(direct.submit(mismatched, sentinel) == OwnerOutcome::NotSubmitted
	      && direct.submit(clear_tuple, sentinel) == OwnerOutcome::NotSubmitted
	      && direct.submit(unknown, sentinel) == OwnerOutcome::NotSubmitted
	      && direct.submit(no_evidence, sentinel) == OwnerOutcome::NotSubmitted
	      && sentinel.code == AdminCode::Completed && sentinel.report_count == 2,
	      "view_packet_and_intent_rejections_do_not_dispatch");
	SupervisorBroker broker({kMaximumReceiptAge}, direct);
	SupervisorConsoleFacade facade(broker);
	check(facade.attachFixtureOperator(kFirstOperator)
	      && facade.confirm(facade.prepare(Intent::ClearLatch, {0, 0}).ticket).event_consumed,
	      "direct_rejections_preserve_first_broker_event");

	auto disabled_config = ownerConfig();
	disabled_config.fixture_run_id = kZero;
	HarnessSupervisorOwner disabled(disabled_config);
	OwnerView unavailable{};
	unavailable.validator_time_us = kInitialTime;
	check(!disabled.tick(tickReceipt(kFirstObservation, kFirstGeneration, kInitialTime))
	      && !disabled.offer(receipt(kFirstReceipt, kInitialTime, 1, kEstablishOperation, kFirstOperationSequence))
	      && !disabled.snapshot(unavailable) && unavailable.validator_time_us == kInitialTime,
	      "mandatory_configuration_disabled");
}

void bindingAndViewMutationMatrix()
{
	for (unsigned mutation = 0; mutation < 22; ++mutation) {
		HarnessSupervisorOwner owner(ownerConfig());
		SupervisorBroker broker({kMaximumReceiptAge}, owner);
		SupervisorConsoleFacade facade(broker);
		check(facade.attachFixtureOperator(kFirstOperator) && initialTick(owner), "packet_matrix_setup");
		OwnerView before{};
		owner.snapshot(before);
		auto supplied = receipt(kFirstReceipt, kInitialTime, 1, kEstablishOperation, kFirstOperationSequence);

		switch (mutation) {
		case 0: supplied.packet.protocol_version = 0; break;

		case 1: supplied.packet.controller_instance_id = 0; break;

		case 2: supplied.packet.transition_epoch = 0; break;

		case 3: supplied.packet.clock_mapping_epoch = 0; break;

		case 4: supplied.packet.source = 0; break;

		case 5: supplied.packet.session = 0; break;

		case 6: supplied.packet.target_system = 0; break;

		case 7: supplied.packet.target_component = 0; break;

		case 8: supplied.packet.producer_acknowledgement_monotonic_us = 1; break;

		case 9: supplied.packet.acknowledged_status_sequence = 1; break;

		case 10: supplied.packet.flags = 1; break;

		case 11: supplied.packet.reserved[0] = 1; break;

		case 12: supplied.packet.reserved[1] = 1; break;

		case 13: supplied.packet.reserved[2] = 1; break;

		case 14: supplied.packet.reserved[3] = 1; break;

		case 15: supplied.packet.operation = kRevokeOperation; break;

		case 16: supplied.packet.controller_instance_id = kController + 1; break;

		case 17: supplied.packet.transition_epoch = 2; break;

		case 18: supplied.packet.clock_mapping_epoch = kMapEpoch + 1; break;

		case 19: supplied.packet.source = kSource + 1; break;

		case 20: supplied.packet.session = kSession + 1; break;

		case 21: supplied.received_validator_time_us = kInitialTime - kMaximumReceiptAge - 1; break;
		}

		const bool accepted_offer = owner.offer(supplied);
		check(mutation == 21 ? !accepted_offer : accepted_offer, "packet_matrix_offer_or_stale_rejection");
		const auto answer = facade.confirm(facade.prepare(Intent::Establish, {kSource, kSession}).ticket);
		OwnerView after{};
		check(resultIs(answer, ConsoleDisposition::NotSubmitted, BrokerCode::OwnerDeclined)
		      && !answer.has_adapter_code && !answer.event_consumed && owner.snapshot(after)
		      && sameView(before, after), "packet_or_binding_rejected_before_adapter");
	}

	for (unsigned verdict_bit = 0; verdict_bit < 4; ++verdict_bit) {
		HarnessSupervisorOwner owner(ownerConfig());
		SupervisorBroker broker({kMaximumReceiptAge}, owner);
		SupervisorConsoleFacade facade(broker);
		check(facade.attachFixtureOperator(kFirstOperator) && initialTick(owner), "ingress_matrix_setup");
		OwnerView before{};
		owner.snapshot(before);
		auto supplied = receipt(kFirstReceipt, kInitialTime, 1, kEstablishOperation, kFirstOperationSequence);

		if (verdict_bit == 0) { supplied.verdict.mavlink2 = false; }

		if (verdict_bit == 1) { supplied.verdict.parser_authenticated = false; }

		if (verdict_bit == 2) { supplied.verdict.configured_header_channel_source_binding = false; }

		if (verdict_bit == 3) { supplied.verdict.configured_target_binding = false; }

		check(owner.offer(supplied), "ingress_matrix_offer");
		const auto answer = facade.confirm(facade.prepare(Intent::Establish, {kSource, kSession}).ticket);
		OwnerView after{};
		check(resultIs(answer, ConsoleDisposition::NotSubmitted, BrokerCode::OwnerDeclined)
		      && !answer.has_adapter_code && !answer.event_consumed && owner.snapshot(after)
		      && sameView(before, after), "all_ingress_evidence_bits_required");
	}

	for (unsigned field = 0; field < 12; ++field) {
		HarnessSupervisorOwner owner(ownerConfig());
		check(initialTick(owner), "view_matrix_setup");
		OwnerView actual{};
		owner.snapshot(actual);
		OwnerView expected = actual;

		switch (field) {
		case 0: expected.ready = false; break;

		case 1: expected.terminal = true; break;

		case 2: expected.controller_instance_id += 1; break;

		case 3: expected.state_generation += 1; break;

		case 4: expected.transition_epoch += 1; break;

		case 5: expected.clock_mapping_epoch += 1; break;

		case 6: expected.validator_time_us += 1; break;

		case 7: expected.has_session_history = true; break;

		case 8: expected.last_session.owner = kSource + 1; break;

		case 9: expected.last_session.session = kSession + 1; break;

		case 10: expected.has_supervisor_floor = true; break;

		case 11: expected.supervisor_floor = 1; break;
		}

		Submission submission{Intent::ClearLatch, {0, 0}, expected,
			{
				1, actual.state_generation, actual.transition_epoch, kController,
				SupervisorAction::ClearLatchedFault, false, {0, 0}
			}};
		AdminResult sentinel{};
		sentinel.code = AdminCode::Completed;
		sentinel.report_count = 2;
		check(owner.submit(submission, sentinel) == OwnerOutcome::NotSubmitted
		      && sentinel.code == AdminCode::Completed && sentinel.report_count == 2,
		      "each_expected_view_field_is_a_direct_submit_guard");
	}
}
void ownerBoundaryCases()
{
	HarnessSupervisorOwner unticked(ownerConfig());
	OwnerView untouched{};
	untouched.validator_time_us = 77;
	AdminResult sentinel{};
	sentinel.report_count = 2;
	const OwnerView forged{true, false, kController, kFirstGeneration, 1, kMapEpoch, kInitialTime,
			       false, {0, 0}, false, 0};
	Submission clear{Intent::ClearLatch, {0, 0}, forged,
		{1, kFirstGeneration, 1, kController, SupervisorAction::ClearLatchedFault, false, {0, 0}}};
	check(!unticked.snapshot(untouched) && untouched.validator_time_us == 77
	      && unticked.submit(clear, sentinel) == OwnerOutcome::NotSubmitted && sentinel.report_count == 2,
	      "no_first_tick_no_view_or_dispatch");

	for (unsigned invalid = 0; invalid < 4; ++invalid) {
		auto config = ownerConfig();

		if (invalid == 0) { config.fixture_run_id = 0; }

		if (invalid == 1) { config.maximum_receipt_age_us = 0; }

		if (invalid == 2) { config.adapter_identity.controller_instance_id = 0; }

		if (invalid == 3) { config.adapter_contract.protocol_version = 0; }

		HarnessSupervisorOwner disabled(config);
		check(!initialTick(disabled) && !disabled.snapshot(untouched)
		      && disabled.submit(clear, sentinel) == OwnerOutcome::NotSubmitted,
		      "every_invalid_owner_configuration_stays_closed");
	}

	for (unsigned expired = 0; expired < 2; ++expired) {
		HarnessSupervisorOwner owner(ownerConfig());
		SupervisorBroker broker({kMaximumReceiptAge}, owner);
		SupervisorConsoleFacade facade(broker);
		check(initialTick(owner) && facade.attachFixtureOperator(kFirstOperator)
		      && owner.offer(receipt(kFirstReceipt, kInitialTime, 1, kEstablishOperation, kFirstOperationSequence))
		      && owner.tick(tickReceipt(kSecondObservation, kFirstGeneration, kInitialTime + kMaximumReceiptAge + expired)),
		      "receipt_ages_between_offer_and_submit");
		const auto result = facade.confirm(facade.prepare(Intent::Establish, {kSource, kSession}).ticket);
		check(expired ? resultIs(result, ConsoleDisposition::NotSubmitted, BrokerCode::OwnerDeclined)
		      : (resultIs(result, ConsoleDisposition::Submitted, BrokerCode::Submitted)
			 && result.has_adapter_code && result.adapter_code == AdminCode::Completed),
		      "submit_rechecks_inclusive_receipt_age");
		check(!owner.offer(receipt(kFirstReceipt, kInitialTime + kMaximumReceiptAge + expired, 1,
					   kEstablishOperation, kFirstOperationSequence)), "expired_or_dispatched_id_cannot_refresh");
	}

	HarnessSupervisorOwner owner(ownerConfig());
	SupervisorBroker broker({kMaximumReceiptAge}, owner);
	SupervisorConsoleFacade facade(broker);
	check(initialTick(owner) && facade.attachFixtureOperator(kFirstOperator)
	      && owner.offer(receipt(kFirstReceipt, kInitialTime, 1, kEstablishOperation, kFirstOperationSequence)),
	      "clear_with_independent_receipt_setup");
	const auto rejected = facade.confirm(facade.prepare(Intent::ClearLatch, {0, 0}).ticket);
	const auto established = facade.confirm(facade.prepare(Intent::Establish, {kSource, kSession}).ticket);
	check(rejected.has_adapter_code && rejected.adapter_code == AdminCode::ClearNotLatched && rejected.event_consumed
	      && established.has_adapter_code && established.adapter_code == AdminCode::Completed && established.event_consumed,
	      "clear_does_not_consume_establish_receipt");
	const auto pending = facade.prepare(Intent::Revoke, {kSource, kSession});
	auto inconsistent = tickReceipt(kSecondObservation, kFirstGeneration, kInitialTime + 1);
	inconsistent.sample.request_guard_context.arm_state = ArmState::Armed; // No generation increment: actual adapter fault.
	check(owner.tick(inconsistent) && owner.snapshot(untouched) && untouched.terminal && !untouched.ready,
	      "adapter_terminal_visible_without_owner_provenance_fault");
	check(facade.confirm(pending.ticket).disposition == ConsoleDisposition::Quarantined && broker.quarantined()
	      && owner.tick(tickReceipt(kThirdObservation, kFirstGeneration, kInitialTime + 2))
	      && owner.snapshot(untouched) && untouched.terminal,
	      "quarantine_does_not_stop_ticks_or_reset_terminal_adapter");

	HarnessSupervisorOwner exhausted(ownerConfig());
	const auto maximum = std::numeric_limits<uint64_t>::max();
	check(initialTick(exhausted)
	      && exhausted.offer(receipt(maximum, kInitialTime, 1, kEstablishOperation, kFirstOperationSequence))
	      && !exhausted.offer(receipt(0, kInitialTime, 1, kEstablishOperation, kFirstOperationSequence))
	      && !exhausted.offer(receipt(1, kInitialTime, 1, kEstablishOperation, kFirstOperationSequence))
	      && HarnessSupervisorOwnerTestPeer::receiptFloor(exhausted) == maximum,
	      "receipt_floor_never_wraps");
	auto restarted_config = ownerConfig();
	restarted_config.fixture_run_id = kRun + 1;
	HarnessSupervisorOwner restarted(restarted_config);
	check(restarted.tick(tickReceipt(kFirstObservation, kFirstGeneration, kInitialTime, true, kStream, kRun + 1))
	      && !restarted.offer(receipt(kFirstReceipt, kInitialTime, 1, kEstablishOperation, kFirstOperationSequence))
	      && restarted.snapshot(untouched) && !untouched.has_session_history && !untouched.has_supervisor_floor,
	      "new_run_does_not_import_old_evidence_or_history");
}
} // namespace

int main()
{
	establishRevokeAndFacade();
	clearAndImmutableEvidence();
	slotAndReceiptTime();
	provenanceAndNoDispatch();
	bindingAndViewMutationMatrix();
	ownerBoundaryCases();
	std::cout << checks << " checks; " << failures << " failures\n";
	return failures == 0 ? 0 : 1;
}
