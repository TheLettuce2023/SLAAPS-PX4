#include "HarnessSupervisorOwner.hpp"

#include <array>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>

using namespace research_acceleration;
using namespace research_sitl;

namespace research_sitl
{
struct SupervisorBrokerTestPeer {
	// test_peer_injection: otherwise unreachable broker exhaustion.
	static void ticketFloor(SupervisorBroker &broker, uint64_t floor) { broker.ticket_floor_ = floor; }
};

struct DiagnosticSupervisorOwnerTestPeer {
	// test_peer_injection: force the serialization guard's reentrant path.
	static void active(HarnessSupervisorOwner &owner, bool value) { owner.in_call_ = value; }
	static bool queued(const HarnessSupervisorOwner &owner) { return owner.has_receipt_; }
};
} // namespace research_sitl

namespace
{
constexpr auto selected = DiagnosticSelectionState::SelectedForValidation;
constexpr auto disabled = DiagnosticSelectionState::Disabled;
constexpr uint64_t maximum = std::numeric_limits<uint64_t>::max();
size_t checks = 0;
size_t failures = 0;

void check(bool value, const std::string &name)
{
	++checks;

	if (!value) { ++failures; std::cerr << "FAILED: " << name << '\n'; }
}

OwnerHarnessConfig config()
{
	// Synthetic contract values, not measured timing or an operational profile.
	return {{
			kWireProtocolVersionV1, 7, DisarmedPolicy::ValidateOnly,
			{Vec3f(-10.F, -10.F, -10.F), Vec3f(10.F, 10.F, 10.F), 10.F}, {20, 200, 50, 10, 2}, 0.001F
		},
		{11}, 17, 50};
}

TrustedStateSample physical(TimeUs now = 9950)
{
	return {1, true, {now, ArmState::Disarmed, ResearchModeState::NotSelected, true, true, true},
		{true, 7, 100, 11, 1, 1000, 2, 8000, 10000}};
}

FixtureStateReceipt receipt(const TrustedStateSample &state, uint64_t observation)
{
	return {state, {17, observation, state.request_guard_context.validator_receive_time_us, true}, 23};
}

DiagnosticIngress ingress() { return {13, 17, {true, true, true, true}}; }

auto stateKey(const DiagnosticSnapshot &state)
{
	return std::make_tuple(state.initialized, state.terminal, state.selection, state.selector_epoch,
			       state.operator_attached, state.operator_instance, state.adapter.state_generation, state.adapter.transition_epoch,
			       state.adapter.last_report_sequence, state.session.gate, state.session.supervisor_floor,
			       state.session.wire_operation_floor, state.admission.acknowledgement_current, state.latched,
			       state.has_last_accepted_sequence, state.last_accepted_sequence, state.last_accepted_authenticated);
}

template<typename T>
std::array<unsigned char, sizeof(T)> bytes(const T &value)
{
	std::array<unsigned char, sizeof(T)> result{};
	std::memcpy(result.data(), &value, sizeof(T));
	return result;
}

void isolated(const DiagnosticResult &result, const std::string &name)
{
	check(result.record_count <= 4 && !result.snapshot.has_last_accepted_sequence
	      && !result.snapshot.last_accepted_authenticated, name + ": bounded records and no Flight history");
	check(result.has_operation_record ? result.operation_record_index < result.record_count : result.operation_record_index == 0,
	      name + ": operation index");

	for (uint8_t i = 0; i < result.record_count && i < 4; ++i) {
		const auto &record = result.records[i];
		check(record.domain == DiagnosticDomain::SitlDisarmedDiagnostic && !record.eligible_for_control
		      && (!record.common_decision || !record.common_decision->eligible_for_control), name + ": output disabled");
	}
}

void submitted(const DiagnosticConsoleResult &result, DiagnosticCode expected, const std::string &name)
{
	check(result.disposition == ConsoleDisposition::Submitted && result.broker_code == BrokerCode::Submitted
	      && result.diagnostic_code && *result.diagnostic_code == expected, name + ": actual submitted diagnostic result");
}

void local(const DiagnosticConsoleResult &result, BrokerCode expected, const std::string &name)
{
	check(result.disposition == ConsoleDisposition::NotSubmitted && result.broker_code == expected
	      && !result.diagnostic_code && !result.event_consumed && !result.action_performed,
	      name + ": no adapter result invented");
}

struct Fixture {
	HarnessSupervisorOwner owner{config(), DiagnosticOfflineTag{}, 13};
	SupervisorBroker broker{{50}, owner};
	SupervisorConsoleFacade console{broker};
	TrustedStateSample state{physical()};
	uint64_t observation{0};
	uint64_t ingress_receipt{0};

	Fixture()
	{
		check(console.attachFixtureOperator(19), "fixture independent operator attachment");
		tick();
	}
	DiagnosticOwnerView view()
	{
		DiagnosticOwnerView result{};
		check(owner.diagnosticSnapshot(result), "fixture diagnostic owner view");
		return result;
	}
	DiagnosticResult tick()
	{
		DiagnosticResult result{};
		check(owner.tickDiagnostic(receipt(state, ++observation), &result), "fixture coherent diagnostic tick");
		isolated(result, "fixture tick");
		return result;
	}
	DecodedSessionV1 packet(uint8_t operation, TimeUs raw = 0)
	{
		const auto current = view().state;
		return {11, current.adapter.transition_epoch, state.map.map_epoch, 7, state.map.session,
			current.session.wire_operation_floor + 1, raw, operation == 3 ? current.adapter.barrier_report_sequence : 0,
			kWireProtocolVersionV1, 1, 1, operation, 0, {0, 0, 0, 0}};
	}
	DecodedRequestV1 request(uint64_t sequence = 1, TimeUs raw = 9000)
	{
		return {{
				7, 100, sequence, raw, raw + 100, 11, view().state.adapter.transition_epoch, 1,
				1.F, 0.F, 0.F, kWireProtocolVersionV1, kWireFrameFrdV1, kWireQuantityInertialAccelerationV1,
				kWireReferencePointCgV1
			}, 1, 1, 0, {0, 0, 0, 0}};
	}
	void offer(uint8_t operation = 1)
	{
		check(owner.offer({17, ++ingress_receipt, state.request_guard_context.validator_receive_time_us,
				   packet(operation), ingress().authentication}), "fixture immutable admin receipt");
	}
	DiagnosticConsoleResult enable()
	{
		const auto pending = console.prepareDiagnosticSelection({7, 100});
		local(pending, BrokerCode::Ready, "prepare diagnostic selection");
		return console.confirmDiagnosticSelection(pending.ticket);
	}
	void ready()
	{
		submitted(enable(), DiagnosticCode::SelectionEnabled, "real enable");
		offer();
		const auto pending = console.prepareDiagnosticAdmin(Intent::Establish, {7, 100});
		local(pending, BrokerCode::Ready, "real establish prepare");
		submitted(console.confirmDiagnosticAdmin(pending.ticket), DiagnosticCode::SessionEstablished, "real establish");
		state.request_guard_context.validator_receive_time_us = 9965;
		tick();
		DiagnosticResult result{};
		check(owner.processDiagnosticAcknowledgement(packet(3, 8960), ingress(), result)
		      && result.code == DiagnosticCode::Acknowledged, "real owner ACK");
		isolated(result, "real owner ACK");
		state.request_guard_context.validator_receive_time_us = 10010;
		tick();
		check(owner.processDiagnosticRequest(request(), ingress(), result)
		      && result.code == DiagnosticCode::RequestValidatedDisarmed, "real owner request");
		isolated(result, "real owner request");
		check(result.has_operation_record && result.records[result.operation_record_index].common_decision
		      && result.records[result.operation_record_index].common_decision->code == DecisionCode::ValidatedDisarmed
		      && result.records[result.operation_record_index].common_decision->conservative_age_us == 12,
		      "real owner positive common decision and independent age");
	}
};

void positiveAndNonAuthority()
{
	static_assert(!std::is_copy_constructible<HarnessSupervisorOwner>::value, "single owner must not copy");
	static_assert(std::is_same < decltype(std::declval<SupervisorConsoleFacade &>().confirmDiagnosticSelection(uint64_t{})),
		      DiagnosticConsoleResult >::value, "facade exposes only redacted diagnostic result");
	Fixture positive;
	positive.ready();
	const auto selected_view = positive.view();
	check(selected_view.state.selection == selected && selected_view.state.session.gate == SessionGate::Established
	      && selected_view.state.admission.acknowledgement_current && selected_view.state.session.supervisor_floor == 2,
	      "real owner authoritative selected established ACK state");
	DiagnosticResult access{};
	check(positive.owner.accessDiagnostic(access) && access.code == DiagnosticCode::NoEligibleRequest,
	      "real diagnostic access never returns request demand");
	isolated(access, "real diagnostic access");
	DiagnosticResult audit{};
	check(positive.broker.copyLastDiagnosticResult(audit) && audit.code == DiagnosticCode::SessionEstablished
	      && audit.has_operation_record && audit.records[audit.operation_record_index].correlation.operation == DiagnosticOperationKind::Establish,
	      "broker audit is actual administration, never latest request masquerading as admin");
	positive.offer(2);
	const auto revoke = positive.console.prepareDiagnosticAdmin(Intent::Revoke, {7, 100});
	submitted(positive.console.confirmDiagnosticAdmin(revoke.ticket), DiagnosticCode::SessionRevoked, "real broker revoke");
	check(positive.broker.copyLastDiagnosticResult(audit) && audit.record_count == 3
	      && audit.operation_record_index == 1 && audit.records[0].kind == DiagnosticReportKind::SessionBarrier
	      && audit.records[1].kind == DiagnosticReportKind::Operation
	      && audit.records[2].kind == DiagnosticReportKind::SelectorBarrier && audit.snapshot.selection == disabled,
	      "broker accepts real B,O,S revoke with nonfinal operation index");
	Fixture clear;
	clear.ready();
	clear.state.request_guard_context.validator_receive_time_us = 10011;
	clear.tick();
	DiagnosticResult latched{};
	check(clear.owner.processDiagnosticRequest(clear.request(2, 8999), ingress(), latched)
	      && latched.snapshot.latched && latched.snapshot.selection == disabled, "real producer-latch setup");
	const auto clearing = clear.console.prepareDiagnosticAdmin(Intent::ClearLatch, {0, 0});
	submitted(clear.console.confirmDiagnosticAdmin(clearing.ticket), DiagnosticCode::LatchCleared, "real broker clear");
	check(clear.broker.copyLastDiagnosticResult(audit) && audit.record_count == 2
	      && audit.operation_record_index == 1 && !audit.snapshot.latched && audit.snapshot.selection == disabled,
	      "broker accepts actual B,O recovery without restoring selection");

	for (unsigned variant = 0; variant < 3; ++variant) {
		Fixture f;

		if (variant == 2) { local(f.console.prepareDiagnosticSelection({7, 100}), BrokerCode::Ready, "A04 prepare only"); }

		DiagnosticResult result{};
		const bool called = variant == 1 ? f.owner.processDiagnosticAcknowledgement(f.packet(3, 8960), ingress(), result)
				    : f.owner.processDiagnosticRequest(f.request(), ingress(), result);
		check(called && result.code == DiagnosticCode::Rejected && result.snapshot.selection == disabled
		      && !result.snapshot.session.has_supervisor_floor, "A03/A04 request, ACK and prepare cannot enable");
		isolated(result, "A03/A04 no authority");
	}
}

void ticketsAndDomains()
{
	Fixture f;
	const auto initial = f.view().state;
	const auto pending = f.console.prepareDiagnosticSelection({7, 100});
	local(f.console.confirmDiagnosticSelection(pending.ticket + 1), BrokerCode::InvalidTicket, "A07 unrelated ticket");
	local(f.console.prepareDiagnosticAdmin(Intent::ClearLatch, {0, 0}), BrokerCode::Busy, "one pending ticket across intents");
	submitted(f.console.confirmDiagnosticSelection(pending.ticket), DiagnosticCode::SelectionEnabled,
		  "unrelated ticket preserved matching action");
	local(f.console.confirmDiagnosticSelection(pending.ticket), BrokerCode::InvalidTicket, "A09 confirmation consumed once");
	check(f.view().state.session.supervisor_floor == initial.session.supervisor_floor + 1, "A09 exactly one event");

	for (unsigned variant = 0; variant < 3; ++variant) {
		Fixture wrong;

		if (variant == 1) { submitted(wrong.enable(), DiagnosticCode::SelectionEnabled, "wrong route admin setup"); wrong.offer(); }

		const auto prepared = variant == 1 ? wrong.console.prepareDiagnosticAdmin(Intent::Establish, {7, 100})
				      : wrong.console.prepareDiagnosticSelection({7, 100});
		const auto before = wrong.view().state;

		if (variant == 0) { local(wrong.console.confirmDiagnosticAdmin(prepared.ticket), BrokerCode::ContextChanged, "A07 selection via admin route"); }

		if (variant == 1) { local(wrong.console.confirmDiagnosticSelection(prepared.ticket), BrokerCode::ContextChanged, "A07 admin via selection route"); }

		if (variant == 2) {
			const auto result = wrong.console.confirm(prepared.ticket);
			check(result.code == BrokerCode::ContextChanged && !result.has_adapter_code && !result.event_consumed,
			      "A07 diagnostic ticket through Flight confirmation");
		}

		local(wrong.console.confirmDiagnosticSelection(prepared.ticket), BrokerCode::InvalidTicket, "A07 matching wrong-domain ticket canceled");
		check(stateKey(wrong.view().state) == stateKey(before), "A07 wrong route has no owner mutation");
	}

	Fixture age;
	const auto expiring = age.console.prepareDiagnosticSelection({7, 100});
	age.state.request_guard_context.validator_receive_time_us += 51;
	age.tick();
	local(age.console.confirmDiagnosticSelection(expiring.ticket), BrokerCode::Expired, "A08 explicit ticket age");
	check(age.view().state.selection == disabled && !age.view().state.session.has_supervisor_floor, "A08 expiry grants no authority");

	Fixture changed;
	const auto stale = changed.console.prepareDiagnosticSelection({7, 100});
	++changed.state.generation;
	++changed.state.map.validator_minus_producer_us;
	changed.tick();
	local(changed.console.confirmDiagnosticSelection(stale.ticket), BrokerCode::ContextChanged, "A05 full map view changed");
	check(!changed.view().state.session.has_supervisor_floor, "stale owner view consumes no event");

	Fixture diagnostic;
	const auto before = diagnostic.view().state;
	OwnerView ordinary{};
	check(!diagnostic.owner.snapshot(ordinary), "diagnostic owner refuses Flight owner snapshot");
	const auto ordinary_prepare = diagnostic.console.prepare(Intent::Establish, {7, 100});
	check(ordinary_prepare.disposition == ConsoleDisposition::NotSubmitted && !ordinary_prepare.has_adapter_code,
	      "Flight console prepare cannot select diagnostic domain");
	AdapterResult ordinary_result{};
	check(!diagnostic.owner.tick(receipt(diagnostic.state, 2), &ordinary_result), "Flight tick cannot mutate diagnostic owner");
	check(stateKey(diagnostic.view().state) == stateKey(before), "domain misuse retains all diagnostic state");

	HarnessSupervisorOwner flight(config());
	SupervisorBroker flight_broker({50}, flight);
	SupervisorConsoleFacade flight_console(flight_broker);
	check(flight_console.attachFixtureOperator(19), "Flight attachment remains available");
	check(flight.tick(receipt(physical(), 1)), "ordinary owner tick");
	DiagnosticResult sentinel{};
	sentinel.code = DiagnosticCode::Acknowledged;
	const auto untouched = bytes(sentinel);
	check(!flight.tickDiagnostic(receipt(physical(), 2), &sentinel) && bytes(sentinel) == untouched,
	      "diagnostic tick on Flight leaves output unchanged");
	const auto denied = flight_console.prepareDiagnosticSelection({7, 100});
	check(denied.disposition == ConsoleDisposition::NotSubmitted && !denied.diagnostic_code && !denied.event_consumed,
	      "diagnostic console cannot convert Flight owner");
}

void lifecycleAndReceipts()
{
	Fixture disable;
	disable.ready();
	submitted(disable.console.disableDiagnosticSelection(), DiagnosticCode::SelectionDisabled, "explicit diagnostic disable");
	const auto disabled_state = disable.view().state;
	submitted(disable.console.disableDiagnosticSelection(), DiagnosticCode::AlreadyDisabled, "idempotent diagnostic disable");
	const auto repeated_state = disable.view().state;
	check(repeated_state.selector_epoch == disabled_state.selector_epoch
	      && repeated_state.adapter.transition_epoch == disabled_state.adapter.transition_epoch
	      && repeated_state.session.supervisor_floor == disabled_state.session.supervisor_floor + 1
	      && repeated_state.session.wire_operation_floor == disabled_state.session.wire_operation_floor,
	      "idempotent closure uses shared event but no selector transition or wire reset");

	for (unsigned variant = 0; variant < 3; ++variant) {
		Fixture f;
		f.ready();
		f.offer(2);
		const auto pending = f.console.prepareDiagnosticAdmin(Intent::Revoke, {7, 100});
		local(pending, BrokerCode::Ready, "lifecycle pending revoke");
		const auto before = f.view().state;

		if (variant == 1) {
			++f.state.generation;
			f.state.request_guard_context.required_state_valid = false;
			f.tick();
		}

		if (variant == 2) {
			DiagnosticResult close{};
			check(f.owner.closeDiagnosticOwner(DiagnosticCloseReason::OperatorLost, close) == OwnerOutcome::Completed,
			      "A23 trusted operator-loss close completes");
			isolated(close, "A23 operator loss");
			check(close.snapshot.selection == disabled && !close.snapshot.operator_attached
			      && !close.snapshot.admission.acknowledgement_current && !close.grant_consumed,
			      "A23 operator loss synchronously removes authority");

		} else {
			const auto detached = f.console.detachDiagnosticOperator();
			check(detached.disposition == ConsoleDisposition::Submitted && detached.diagnostic_code
			      && !detached.event_consumed, "A23 detach synchronous submitted close");
		}

		DiagnosticOwnerView after{};
		check(f.owner.diagnosticSnapshot(after) && after.state.selection == disabled
		      && !after.state.operator_attached && !after.state.admission.acknowledgement_current
		      && after.state.session.supervisor_floor == before.session.supervisor_floor, "A23 close keeps shared event floor");
		const auto confirm = f.console.confirmDiagnosticAdmin(pending.ticket);
		check(confirm.disposition != ConsoleDisposition::Submitted && !confirm.event_consumed
		      && !confirm.action_performed, "A23 pending ticket cannot act after closure");
		check(!f.owner.offer({17, f.ingress_receipt, f.state.request_guard_context.validator_receive_time_us,
				      f.packet(2), ingress().authentication}), "A23 close does not reset immutable receipt floor");
	}

	Fixture disabled_close;
	const auto before = disabled_close.view().state;
	const auto closed = disabled_close.console.detachDiagnosticOperator();
	check(closed.diagnostic_code && *closed.diagnostic_code == DiagnosticCode::AlreadyDisabled, "A28 disabled detach");
	const auto after = disabled_close.view().state;
	check(after.selector_epoch == before.selector_epoch && after.adapter.transition_epoch == before.adapter.transition_epoch
	      && after.session.gate == SessionGate::EstablishmentRequired, "A28 close invalidates without fabricated selector epoch");

	HarnessSupervisorOwner absent(config(), DiagnosticOfflineTag{}, 13);
	SupervisorBroker absent_broker({50}, absent);
	SupervisorConsoleFacade absent_console(absent_broker);
	check(absent_console.attachFixtureOperator(19), "A24 attachment before first receipt");
	const auto failed = absent_console.detachDiagnosticOperator();
	DiagnosticResult terminal_audit{};
	check(absent.copyLastDiagnosticResult(terminal_audit) && terminal_audit.snapshot.terminal
	      && terminal_audit.snapshot.selection == DiagnosticSelectionState::Faulted
	      && !terminal_audit.snapshot.operator_attached && !failed.event_consumed,
	      "A24 no retained receipt closes terminal without usable authority");
	DiagnosticResult broker_terminal{};
	check(absent_broker.quarantined() && failed.disposition == ConsoleDisposition::Quarantined
	      && failed.diagnostic_code && *failed.diagnostic_code == DiagnosticCode::Terminal
	      && absent_broker.copyLastDiagnosticResult(broker_terminal) && broker_terminal.code == DiagnosticCode::Terminal,
	      "A24 terminal detach quarantines broker and preserves actual terminal code/audit");

	Fixture immutable;
	submitted(immutable.enable(), DiagnosticCode::SelectionEnabled, "immutable setup");
	auto offered = ImmutableIngressReceipt{17, 1, 9950, immutable.packet(1), ingress().authentication};
	check(immutable.owner.offer(offered), "immutable offered copy");
	offered.packet.operation = 2;
	offered.verdict.parser_authenticated = false;
	const auto pending = immutable.console.prepareDiagnosticAdmin(Intent::Establish, {7, 100});
	submitted(immutable.console.confirmDiagnosticAdmin(pending.ticket), DiagnosticCode::SessionEstablished,
		  "immutable offered facts cannot be mutated by caller");
	check(!immutable.owner.offer({17, 1, 9950, immutable.packet(2), ingress().authentication}), "one-shot admin receipt cannot replay");
	const auto no_receipt = immutable.console.prepareDiagnosticAdmin(Intent::Revoke, {7, 100});

	if (no_receipt.broker_code == BrokerCode::Ready) {
		const auto no_action = immutable.console.confirmDiagnosticAdmin(no_receipt.ticket);
		check(!no_action.event_consumed && !no_action.action_performed, "consumed receipt cannot authorize next administration");

	} else { check(!no_receipt.event_consumed, "no queued receipt offers no event authority"); }

	Fixture exhausted;
	SupervisorBrokerTestPeer::ticketFloor(exhausted.broker, maximum);
	const auto overflow = exhausted.console.prepareDiagnosticSelection({7, 100});
	check(exhausted.broker.quarantined() && overflow.disposition == ConsoleDisposition::Quarantined
	      && !overflow.event_consumed && !overflow.action_performed, "ticket exhaustion never wraps or enables");
}

void realOwnerRefusalAndProvenance()
{
	Fixture stale;
	stale.ready();
	stale.offer(2);
	const auto before = stale.view();
	DiagnosticSubmission submission{before,
					DiagnosticSelectionCommand{DiagnosticAction::Disable, {}, before.state.session.supervisor_floor + 1}};
	++submission.expected_view.binding.prospective_map.validator_minus_producer_us;
	DiagnosticResult untouched{};
	untouched.code = DiagnosticCode::Acknowledged;
	const auto sentinel = bytes(untouched);
	check(stale.owner.submitDiagnostic(submission, untouched) == OwnerOutcome::NotSubmitted
	      && bytes(untouched) == sentinel && stateKey(stale.view().state) == stateKey(before.state)
	      && DiagnosticSupervisorOwnerTestPeer::queued(stale.owner),
	      "real stale exact owner view leaves output, adapter, event and queued receipt unchanged");

	for (unsigned variant = 0; variant < 5; ++variant) {
		Fixture f;
		f.ready();
		f.offer(2);
		check(DiagnosticSupervisorOwnerTestPeer::queued(f.owner), "provenance test starts with queued admin receipt");
		const auto retained = f.view();
		auto invalid = receipt(f.state, ++f.observation);

		switch (variant) {
		case 0: ++invalid.clock.fixture_run_id; break;

		case 1: ++invalid.fixture_state_stream_id; break;

		case 2: invalid.clock.observation_id = 1; break;

		case 3:
			--invalid.clock.validator_time_us;
			--invalid.sample.request_guard_context.validator_receive_time_us;
			break;

		case 4: invalid.clock.continuous = false; break;
		}

		DiagnosticResult closed{};
		check(!f.owner.tickDiagnostic(invalid, &closed), "invalid provenance permanently refuses tick " + std::to_string(variant));
		isolated(closed, "invalid provenance immediate closure");
		check(closed.snapshot.selection == disabled && !closed.snapshot.operator_attached
		      && !closed.snapshot.admission.acknowledgement_current
		      && closed.snapshot.session.supervisor_floor == retained.state.session.supervisor_floor
		      && closed.snapshot.adapter.state_generation == retained.state.adapter.state_generation
		      && !DiagnosticSupervisorOwnerTestPeer::queued(f.owner),
		      "invalid provenance closes actual retained authority and cancels receipt");
		check(closed.has_operation_record
		      && closed.records[closed.operation_record_index].correlation.operation == DiagnosticOperationKind::LifecycleClose
		      && closed.records[closed.operation_record_index].physical.request_guard_context.validator_receive_time_us
		      == retained.validator_time_us, "invalid provenance never promotes rejected receipt to closure evidence");
		DiagnosticResult audit{};
		check(f.owner.copyLastDiagnosticResult(audit) && stateKey(audit.snapshot) == stateKey(closed.snapshot),
		      "invalid provenance closure remains inspectable in audit");
		DiagnosticOwnerView unavailable{};
		check(!f.owner.diagnosticSnapshot(unavailable) && !f.owner.attachDiagnosticOperator(20),
		      "invalid provenance permanently refuses snapshot/reattachment authority");
		check(!f.owner.tickDiagnostic(receipt(f.state, ++f.observation)), "valid tick cannot reset provenance failure");
		DecodedRequestV1 request{};
		DiagnosticResult refused{};
		refused.code = DiagnosticCode::Acknowledged;
		const auto refused_sentinel = bytes(refused);
		check(!f.owner.processDiagnosticRequest(request, ingress(), refused) && bytes(refused) == refused_sentinel,
		      "invalid provenance refuses later ingress without invented output");
		check(!f.owner.offer({17, ++f.ingress_receipt, retained.validator_time_us, {}, ingress().authentication}),
		      "invalid provenance cannot queue later administration");
	}

	Fixture reentrant;
	reentrant.ready();
	reentrant.offer(2);
	const auto current = reentrant.view();
	DiagnosticSubmission command{current,
				     DiagnosticSelectionCommand{DiagnosticAction::Disable, {}, current.state.session.supervisor_floor + 1}};
	DiagnosticResult result{};
	DiagnosticSupervisorOwnerTestPeer::active(reentrant.owner, true);
	const auto outcome = reentrant.owner.submitDiagnostic(command, result);
	DiagnosticSupervisorOwnerTestPeer::active(reentrant.owner, false);
	check(outcome == OwnerOutcome::Unknown, "reentrant submit reports uncertain completion, never NotSubmitted");
	DiagnosticResult audit{};
	check(reentrant.owner.copyLastDiagnosticResult(audit) && audit.snapshot.selection == disabled
	      && !audit.snapshot.operator_attached && !audit.snapshot.admission.acknowledgement_current
	      && audit.snapshot.session.supervisor_floor == current.state.session.supervisor_floor
	      && !DiagnosticSupervisorOwnerTestPeer::queued(reentrant.owner),
	      "reentrant owner fault synchronously closes authority and queued ingress");
}

// Negative-only owner seam: actual successful traces above never use a mock.
// Malformed Completed replies below start from a real owner turn and corrupt
// exactly the broker-visible result, testing independent result validation.
class FaultOwner final : public SupervisorOwner
{
public:
	enum class Fault { None, NotSubmitted, Unknown, Index, MissingBarrier, Delta, Consumption, CloseUnknown, RegressTime,
			   MissingCommon, CommonCode, CommonCause, CommonLatch, CommonFirstLatch, CommonRejections, SilentGate,
			   SelectionCause, SelectionCommon, RevokeCause, InvalidFirstLatch, UnlatchedFirstCause, LatchedAccepted,
			   LatchedNonLatch, EarlierLatch, OperationLatch, RejectedNone, RejectedCompleted, EraseLatch, LifecycleEraseLatch,
			   Authentication, SelectorCause, SessionCause, TerminalCause, FakeClearUnlatched, FakeEstablishHistory, FakeRevokeGate,
			   LifecycleCause
			 };
	HarnessSupervisorOwner real{config(), DiagnosticOfflineTag{}, 13};
	Fault fault{Fault::None};
	unsigned submissions{0};
	unsigned closures{0};
	bool corruption_applied{false};
	int changed_binding{-1}; // Negative-only owner-view injection for A05.
	SupervisorBroker *reenter_on_close{nullptr};
	DiagnosticConsoleResult nested_close{};
	DiagnosticDomain domain() const noexcept override { return DiagnosticDomain::SitlDisarmedDiagnostic; }
	bool attachDiagnosticOperator(uint64_t id) noexcept override { return real.attachDiagnosticOperator(id); }
	bool snapshot(OwnerView &out) noexcept override { return real.snapshot(out); }
	OwnerOutcome submit(const Submission &input, AdminResult &out) noexcept override { return real.submit(input, out); }
	bool diagnosticSnapshot(DiagnosticOwnerView &out) noexcept override
	{
		const bool read = real.diagnosticSnapshot(out);

		if (read && changed_binding >= 0) {
			auto &binding = out.binding;

			switch (changed_binding) {
			case 0: ++binding.owner_lifetime_id; break;

			case 1: ++binding.fixture_run_id; break;

			case 2: ++binding.controller_instance_id; binding.prospective_map.controller_instance_id = binding.controller_instance_id; break;

			case 3: ++binding.operator_instance; break;

			case 4: ++binding.state_generation; break;

			case 5: ++binding.transition_epoch; break;

			case 6: ++binding.selector_epoch; break;

			case 7: binding.prospective_map.valid = false; out.physical.request_guard_context.clock_mapping_valid = false; break;

			case 8: ++binding.prospective_map.source; break;

			case 9: ++binding.prospective_map.session; break;

			case 10: ++binding.prospective_map.controller_instance_id; break;

			case 11: ++binding.prospective_map.map_epoch; break;

			case 12: ++binding.prospective_map.validator_minus_producer_us; break;

			case 13: ++binding.prospective_map.uncertainty_us; break;

			case 14: ++binding.prospective_map.valid_from_producer_us; break;

			case 15: ++binding.prospective_map.valid_until_producer_us; break;

			case 16: out.physical.request_guard_context.clock_mapping_valid = false; break;

			default: break;
			}

			// Keep duplicated view bindings internally coherent where possible;
			// changed immutable identities must still quarantine, never dispatch.
			out.state.identity = {binding.owner_lifetime_id, binding.fixture_run_id};
			out.state.controller_instance_id = binding.controller_instance_id;
			out.state.operator_instance = binding.operator_instance;
			out.state.adapter.state_generation = binding.state_generation;
			out.state.adapter.transition_epoch = binding.transition_epoch;
			out.state.selector_epoch = binding.selector_epoch;
			out.physical.generation = binding.state_generation;
			out.physical.map = binding.prospective_map;
			corruption_applied = true;
		}

		if (read && fault == Fault::RegressTime) {
			--out.validator_time_us;
			--out.physical.request_guard_context.validator_receive_time_us;
		}

		if (read && fault == Fault::FakeClearUnlatched) {
			out.state.latched = false;
			out.state.first_latched_code = DecisionCode::Accepted;
			corruption_applied = true;
		}

		if (read && fault == Fault::FakeEstablishHistory) {
			out.state.session.has_last_successful_session = true;
			out.state.session.last_successful_session = {7, 100};
			corruption_applied = true;
		}

		if (read && fault == Fault::FakeRevokeGate) {
			out.state.session.gate = SessionGate::EstablishmentRequired;
			corruption_applied = true;
		}

		return read;
	}
	OwnerOutcome submitDiagnostic(const DiagnosticSubmission &input, DiagnosticResult &out) noexcept override
	{
		++submissions;

		if (fault == Fault::NotSubmitted) { return OwnerOutcome::NotSubmitted; }

		if (fault == Fault::Unknown) { return OwnerOutcome::Unknown; }

		auto actual_input = input;

		if (fault == Fault::FakeClearUnlatched || fault == Fault::FakeEstablishHistory || fault == Fault::FakeRevokeGate) {
			// Negative conformance only: make the real owner execute against its
			// actual state, while the broker tests the deliberately false prior
			// view. No fabricated successful operation replaces the real guard.
			if (!real.diagnosticSnapshot(actual_input.expected_view)) { return OwnerOutcome::NotSubmitted; }
		}

		const auto result = real.submitDiagnostic(actual_input, out);

		if (result != OwnerOutcome::Completed) { return result; }

		if (fault == Fault::Index) { out.operation_record_index = 0; }

		if (fault == Fault::MissingBarrier && out.record_count > 1) {
			out.records[0] = out.records[1];
			out.records[1] = {};
			--out.record_count;
			out.operation_record_index = 0;
		}

		if (fault == Fault::Delta) { ++out.snapshot.adapter.transition_epoch; }

		if (fault == Fault::Consumption) { out.grant_consumed = false; }

		if (out.has_operation_record && out.operation_record_index < out.record_count) {
			auto &operation = out.records[out.operation_record_index];

			if (fault == Fault::SelectionCause) { operation.cause = DecisionCode::Latched; corruption_applied = true; }

			if (fault == Fault::SelectionCommon) {
				operation.common_decision = Decision{DecisionCode::Accepted, false, false, false,
								     DecisionCode::Accepted, false, 0, operation.snapshot.rejection_count};
				corruption_applied = true;
			}

			if (fault == Fault::RevokeCause) { operation.cause = AdminCode::ContextRejected; corruption_applied = true; }

			if (fault == Fault::Authentication && operation.correlation.authentication) {
				operation.correlation.authentication->parser_authenticated = false;
				corruption_applied = true;
			}

			if (fault == Fault::SelectorCause || fault == Fault::SessionCause) {
				for (uint8_t i = 0; i < out.record_count; ++i) {
					if (out.records[i].kind == (fault == Fault::SelectorCause ? DiagnosticReportKind::SelectorBarrier
								    : DiagnosticReportKind::SessionBarrier)) {
						out.records[i].cause = DecisionCode::Latched;
						corruption_applied = true;
						break;
					}
				}
			}

			if (fault == Fault::InvalidFirstLatch) {
				out.records[0].snapshot.first_latched_code = static_cast<DecisionCode>(65535);
				corruption_applied = true;
			}

			if (fault == Fault::UnlatchedFirstCause || fault == Fault::LatchedAccepted || fault == Fault::LatchedNonLatch
			    || fault == Fault::EarlierLatch || fault == Fault::OperationLatch) {
				auto &snapshot = fault == Fault::OperationLatch ? operation.snapshot : out.records[0].snapshot;
				snapshot.latched = fault != Fault::UnlatchedFirstCause;
				snapshot.first_latched_code = fault == Fault::LatchedAccepted ? DecisionCode::Accepted
							      : fault == Fault::LatchedNonLatch ? DecisionCode::WrongSession : DecisionCode::ProducerTimeRegressed;
				corruption_applied = true;
			}

			if (fault == Fault::RejectedNone) { operation.cause = DiagnosticLocalReason::None; corruption_applied = true; }

			if (fault == Fault::RejectedCompleted) { operation.cause = AdminCode::Completed; corruption_applied = true; }

			if (fault == Fault::EraseLatch && out.snapshot.latched) {
				for (uint8_t i = 0; i < out.record_count; ++i) {
					out.records[i].snapshot.latched = false;
					out.records[i].snapshot.first_latched_code = DecisionCode::Accepted;
				}

				out.snapshot.latched = false;
				out.snapshot.first_latched_code = DecisionCode::Accepted;
				corruption_applied = true;
			}

			if (operation.common_decision && (out.code == DiagnosticCode::SessionEstablished || out.code == DiagnosticCode::LatchCleared)) {
				if (fault == Fault::MissingCommon) { operation.common_decision.reset(); corruption_applied = true; }

				if (fault == Fault::CommonCode) { operation.common_decision->code = DecisionCode::Latched; corruption_applied = true; }

				if (fault == Fault::CommonCause) { operation.cause = DecisionCode::Latched; corruption_applied = true; }

				if (fault == Fault::CommonLatch) { operation.common_decision->latched = true; corruption_applied = true; }

				if (fault == Fault::CommonFirstLatch) {
					operation.common_decision->first_latched_code = DecisionCode::ProducerTimeRegressed;
					corruption_applied = true;
				}

				if (fault == Fault::CommonRejections) { ++operation.common_decision->rejection_count; corruption_applied = true; }
			}

			if (fault == Fault::SilentGate && out.code == DiagnosticCode::Rejected
			    && std::holds_alternative<AdminCode>(operation.cause)
			    && std::get<AdminCode>(operation.cause) == AdminCode::ClearNotLatched
			    && out.snapshot.session.gate == SessionGate::Established && !out.snapshot.admission.acknowledgement_current) {
				operation.snapshot.session.gate = SessionGate::EstablishmentRequired;
				out.snapshot.session.gate = SessionGate::EstablishmentRequired;
				corruption_applied = true;
			}
		}

		return result;
	}
	OwnerOutcome closeDiagnosticOwner(DiagnosticCloseReason reason, DiagnosticResult &out) noexcept override
	{
		++closures;

		// Limit recursion in the fault fixture itself so a broken broker yields
		// finite failing checks rather than overflowing this test's stack.
		if (reenter_on_close != nullptr && closures == 1) { nested_close = reenter_on_close->detachDiagnosticOperator(); }

		if (fault == Fault::CloseUnknown) { return OwnerOutcome::Unknown; }

		const auto outcome = real.closeDiagnosticOwner(reason, out);

		if (fault == Fault::LifecycleCause && out.has_operation_record && out.operation_record_index < out.record_count) {
			out.records[out.operation_record_index].cause = DecisionCode::Latched;
			corruption_applied = true;
		}

		if (fault == Fault::TerminalCause && out.record_count > 0 && out.records[0].kind == DiagnosticReportKind::Terminal) {
			out.records[0].cause = DiagnosticLocalReason::None;
			corruption_applied = true;
		}

		if (fault == Fault::LifecycleEraseLatch && outcome == OwnerOutcome::Completed && out.snapshot.latched) {
			for (uint8_t i = 0; i < out.record_count; ++i) {
				out.records[i].snapshot.latched = false;
				out.records[i].snapshot.first_latched_code = DecisionCode::Accepted;
			}

			out.snapshot.latched = false;
			out.snapshot.first_latched_code = DecisionCode::Accepted;
			corruption_applied = true;
		}

		return outcome;
	}
};

void corruptedAdministration()
{
	for (unsigned admin = 0; admin < 3; ++admin) {
		for (const auto fault : {
			     FaultOwner::Fault::MissingCommon, FaultOwner::Fault::CommonCode,
			     FaultOwner::Fault::CommonCause, FaultOwner::Fault::CommonLatch, FaultOwner::Fault::CommonFirstLatch,
			     FaultOwner::Fault::CommonRejections, FaultOwner::Fault::SilentGate
		     }) {
			if ((admin == 2) != (fault == FaultOwner::Fault::SilentGate)) { continue; }

			FaultOwner owner;
			SupervisorBroker broker({50}, owner);
			SupervisorConsoleFacade console(broker);
			check(console.attachFixtureOperator(19) && owner.real.tickDiagnostic(receipt(physical(), 1)),
			      "admin corruption real owner setup");
			submitted(console.confirmDiagnosticSelection(console.prepareDiagnosticSelection({7, 100}).ticket),
				  DiagnosticCode::SelectionEnabled, "admin corruption real enable");
			DiagnosticOwnerView view{};
			check(owner.real.diagnosticSnapshot(view), "admin corruption enabled view");
			const DecodedSessionV1 establishment{11, view.state.adapter.transition_epoch, 1, 7, 100, 1, 0, 0,
							     kWireProtocolVersionV1, 1, 1, 1, 0, {0, 0, 0, 0}};
			check(owner.real.offer({17, 1, 9950, establishment, ingress().authentication}), "admin corruption real immutable receipt");

			if (admin != 0) {
				submitted(console.confirmDiagnosticAdmin(console.prepareDiagnosticAdmin(Intent::Establish, {7, 100}).ticket),
					  DiagnosticCode::SessionEstablished, "clear corruption real establish");
			}

			if (admin == 1) {
				check(owner.real.diagnosticSnapshot(view), "clear corruption established view");
				const auto epoch = view.state.adapter.transition_epoch;
				const DecodedSessionV1 ack{11, epoch, 1, 7, 100, 2, 8960, view.state.adapter.barrier_report_sequence,
							   kWireProtocolVersionV1, 1, 1, 3, 0, {0, 0, 0, 0}};
				DiagnosticResult result{};
				check(owner.real.tickDiagnostic(receipt(physical(9965), 2))
				      && owner.real.processDiagnosticAcknowledgement(ack, ingress(), result)
				      && result.code == DiagnosticCode::Acknowledged, "clear corruption real ACK");
				DecodedRequestV1 request{{
						7, 100, 1, 9000, 9100, 11, epoch, 1, 1.F, 0.F, 0.F,
						kWireProtocolVersionV1, kWireFrameFrdV1, kWireQuantityInertialAccelerationV1,
						kWireReferencePointCgV1
					}, 1, 1, 0, {0, 0, 0, 0}};
				check(owner.real.tickDiagnostic(receipt(physical(10010), 3))
				      && owner.real.processDiagnosticRequest(request, ingress(), result)
				      && result.code == DiagnosticCode::RequestValidatedDisarmed, "clear corruption real first request");
				request.wire.sequence = 2;
				request.wire.producer_monotonic_us = 8999;
				request.wire.producer_expiry_monotonic_us = 9099;
				check(owner.real.tickDiagnostic(receipt(physical(10011), 4))
				      && owner.real.processDiagnosticRequest(request, ingress(), result)
				      && result.snapshot.latched && result.snapshot.selection == disabled,
				      "clear corruption actual producer latch");
			}

			const auto pending = console.prepareDiagnosticAdmin(admin == 0 ? Intent::Establish : Intent::ClearLatch,
					     admin == 0 ? TrustedSession{7, 100} : TrustedSession{});
			local(pending, BrokerCode::Ready, "corrupt administration pending");
			owner.fault = fault;
			const auto rejected = console.confirmDiagnosticAdmin(pending.ticket);
			const auto name = "admin corruption " + std::to_string(admin) + "/" + std::to_string(static_cast<unsigned>(fault));
			check(owner.corruption_applied, name + ": corruption applied to actual expected operation");
			check(broker.quarantined() && rejected.disposition == ConsoleDisposition::Quarantined
			      && rejected.broker_code == BrokerCode::InvalidOwnerResult && owner.closures == 1,
			      name + ": malformed result quarantines and synchronously closes");
			check(owner.real.diagnosticSnapshot(view) && view.state.selection == disabled
			      && !view.state.operator_attached && !view.state.admission.acknowledgement_current,
			      name + ": real owner closed after evidence rejection");
		}
	}

	FaultOwner reentrant;
	SupervisorBroker broker({50}, reentrant);
	check(broker.attach(19) && reentrant.real.tickDiagnostic(receipt(physical(), 1)), "MAX callback setup");
	reentrant.reenter_on_close = &broker;
	SupervisorBrokerTestPeer::ticketFloor(broker, maximum);
	const auto exhausted = broker.prepareDiagnosticSelection({7, 100});
	check(broker.quarantined() && exhausted.disposition == ConsoleDisposition::Quarantined
	      && exhausted.broker_code == BrokerCode::CounterExhausted, "MAX ticket quarantine");
	check(reentrant.closures == 1, "MAX quarantine invokes close exactly once despite callback reentry");
	check(reentrant.nested_close.disposition == ConsoleDisposition::Quarantined
	      && reentrant.nested_close.broker_code == BrokerCode::Busy
	      && !reentrant.nested_close.diagnostic_code && !reentrant.nested_close.event_consumed
	      && !reentrant.nested_close.action_performed, "MAX callback preserves quarantine and rejects recursive detach as Busy");
}

void latchRealEstablishedOwner(FaultOwner &owner)
{
	DiagnosticOwnerView view{};
	check(owner.real.diagnosticSnapshot(view), "latch evidence real established view");
	const auto epoch = view.state.adapter.transition_epoch;
	const DecodedSessionV1 ack{11, epoch, 1, 7, 100, 2, 8960, view.state.adapter.barrier_report_sequence,
				   kWireProtocolVersionV1, 1, 1, 3, 0, {0, 0, 0, 0}};
	DiagnosticResult result{};
	check(owner.real.tickDiagnostic(receipt(physical(9965), 2))
	      && owner.real.processDiagnosticAcknowledgement(ack, ingress(), result)
	      && result.code == DiagnosticCode::Acknowledged, "latch evidence real ACK");
	DecodedRequestV1 request{{
			7, 100, 1, 9000, 9100, 11, epoch, 1, 1.F, 0.F, 0.F,
			kWireProtocolVersionV1, kWireFrameFrdV1, kWireQuantityInertialAccelerationV1,
			kWireReferencePointCgV1
		}, 1, 1, 0, {0, 0, 0, 0}};
	check(owner.real.tickDiagnostic(receipt(physical(10010), 3))
	      && owner.real.processDiagnosticRequest(request, ingress(), result)
	      && result.code == DiagnosticCode::RequestValidatedDisarmed, "latch evidence real first request");
	request.wire.sequence = 2;
	request.wire.producer_monotonic_us = 8999;
	request.wire.producer_expiry_monotonic_us = 9099;
	check(owner.real.tickDiagnostic(receipt(physical(10011), 4))
	      && owner.real.processDiagnosticRequest(request, ingress(), result)
	      && result.snapshot.latched && result.snapshot.first_latched_code == DecisionCode::ProducerTimeRegressed,
	      "latch evidence actual producer-time regression");
}

void remainingCauseAndLatchCorruptions()
{
	const std::array<FaultOwner::Fault, 20> faults{FaultOwner::Fault::SelectionCause, FaultOwner::Fault::SelectionCause,
			FaultOwner::Fault::SelectionCause, FaultOwner::Fault::SelectionCommon, FaultOwner::Fault::RevokeCause,
			FaultOwner::Fault::InvalidFirstLatch, FaultOwner::Fault::UnlatchedFirstCause, FaultOwner::Fault::LatchedAccepted,
			FaultOwner::Fault::LatchedNonLatch, FaultOwner::Fault::EarlierLatch, FaultOwner::Fault::OperationLatch,
			FaultOwner::Fault::RejectedNone, FaultOwner::Fault::RejectedCompleted, FaultOwner::Fault::EraseLatch,
			FaultOwner::Fault::EraseLatch, FaultOwner::Fault::LifecycleEraseLatch, FaultOwner::Fault::Authentication,
			FaultOwner::Fault::SelectorCause, FaultOwner::Fault::SessionCause, FaultOwner::Fault::LifecycleCause};

	for (unsigned variant = 0; variant < faults.size(); ++variant) {
		FaultOwner owner;
		SupervisorBroker broker({50}, owner);
		SupervisorConsoleFacade console(broker);
		check(console.attachFixtureOperator(19) && owner.real.tickDiagnostic(receipt(physical(), 1)), "remaining corruption setup");
		const bool initial_enable = variant == 0 || variant == 3 || (variant >= 5 && variant <= 9) || variant == 17;

		if (!initial_enable && variant != 2) {
			submitted(console.confirmDiagnosticSelection(console.prepareDiagnosticSelection({7, 100}).ticket),
				  DiagnosticCode::SelectionEnabled, "remaining corruption real enable");
		}

		const bool established = variant == 4 || (variant >= 10 && variant != 17);
		DiagnosticOwnerView view{};

		if (established) {
			check(owner.real.diagnosticSnapshot(view), "remaining corruption pre-establish view");
			const DecodedSessionV1 packet{11, view.state.adapter.transition_epoch, 1, 7, 100, 1, 0, 0,
						      kWireProtocolVersionV1, 1, 1, 1, 0, {0, 0, 0, 0}};
			check(owner.real.offer({17, 1, 9950, packet, ingress().authentication}), "remaining corruption establish receipt");
			submitted(console.confirmDiagnosticAdmin(console.prepareDiagnosticAdmin(Intent::Establish, {7, 100}).ticket),
				  DiagnosticCode::SessionEstablished, "remaining corruption real establishment");
		}

		if (variant >= 13 && variant <= 15) { latchRealEstablishedOwner(owner); }

		const bool revoke = variant == 4 || variant == 10 || variant == 16 || variant == 18;

		if (revoke || variant == 14) {
			check(owner.real.diagnosticSnapshot(view), "remaining corruption admin view");
			const DecodedSessionV1 packet{11, view.state.adapter.transition_epoch, 1, 7, 100,
						      view.state.session.wire_operation_floor + 1, 0, 0, kWireProtocolVersionV1, 1, 1,
						      static_cast<uint8_t>(revoke ? 2 : 1), 0, {0, 0, 0, 0}};
			check(owner.real.offer({17, 2, view.validator_time_us, packet, ingress().authentication}), "remaining corruption administration receipt");
		}

		DiagnosticConsoleResult pending{};

		if (initial_enable) { pending = console.prepareDiagnosticSelection({7, 100}); }

		if (revoke || variant == 14) { pending = console.prepareDiagnosticAdmin(revoke ? Intent::Revoke : Intent::Establish, {7, 100}); }

		if (variant == 11 || variant == 12) { pending = console.prepareDiagnosticAdmin(Intent::ClearLatch, {}); }

		owner.fault = faults[variant];
		DiagnosticConsoleResult rejected{};

		if (initial_enable) { rejected = console.confirmDiagnosticSelection(pending.ticket); }

		else if (revoke || variant == 11 || variant == 12 || variant == 14) { rejected = console.confirmDiagnosticAdmin(pending.ticket); }

		else if (variant == 15 || variant == 19) { rejected = console.detachDiagnosticOperator(); }

		else { rejected = console.disableDiagnosticSelection(); }

		const auto name = "cause/latch corruption " + std::to_string(variant);
		check(owner.corruption_applied, name + ": mutation reached real response");
		check(broker.quarantined() && rejected.disposition == ConsoleDisposition::Quarantined,
		      name + ": invalid evidence quarantines authority");
		check(owner.closures > 0, name + ": synchronous owner closure attempted");
	}

	// Genuine already-latched Disable commands are legal, including repetition.
	// The latch can only be cleared by an actual successful Clear operation.
	Fixture latched;
	latched.ready();
	latched.state.request_guard_context.validator_receive_time_us = 10011;
	latched.tick();
	DiagnosticResult result{};
	check(latched.owner.processDiagnosticRequest(latched.request(2, 8999), ingress(), result)
	      && result.snapshot.latched, "positive latched disable setup");

	for (unsigned repeat = 0; repeat < 2; ++repeat) {
		submitted(latched.console.disableDiagnosticSelection(), DiagnosticCode::AlreadyDisabled, "positive latched Disable/AlreadyDisabled");
		const auto state = latched.view().state;
		check(!latched.broker.quarantined() && state.latched && state.first_latched_code == DecisionCode::ProducerTimeRegressed
		      && state.selection == disabled, "genuine disable retains original latch evidence");
	}
}

void malformedSuccessPreconditions()
{
	for (const auto fault : {
		     FaultOwner::Fault::FakeClearUnlatched, FaultOwner::Fault::FakeEstablishHistory,
		     FaultOwner::Fault::FakeRevokeGate
	     }) {
		FaultOwner owner;
		SupervisorBroker broker({50}, owner);
		SupervisorConsoleFacade console(broker);
		check(console.attachFixtureOperator(19) && owner.real.tickDiagnostic(receipt(physical(), 1)), "success precondition actual setup");
		submitted(console.confirmDiagnosticSelection(console.prepareDiagnosticSelection({7, 100}).ticket),
			  DiagnosticCode::SelectionEnabled, "success precondition actual enable");
		DiagnosticOwnerView actual{};
		check(owner.real.diagnosticSnapshot(actual), "success precondition actual enabled view");
		const DecodedSessionV1 establish{11, actual.state.adapter.transition_epoch, 1, 7, 100, 1, 0, 0,
						 kWireProtocolVersionV1, 1, 1, 1, 0, {0, 0, 0, 0}};
		check(owner.real.offer({17, 1, 9950, establish, ingress().authentication}), "success precondition actual receipt");

		if (fault != FaultOwner::Fault::FakeEstablishHistory) {
			submitted(console.confirmDiagnosticAdmin(console.prepareDiagnosticAdmin(Intent::Establish, {7, 100}).ticket),
				  DiagnosticCode::SessionEstablished, "success precondition actual establish");
		}

		if (fault == FaultOwner::Fault::FakeClearUnlatched) { latchRealEstablishedOwner(owner); }

		if (fault == FaultOwner::Fault::FakeRevokeGate) {
			check(owner.real.diagnosticSnapshot(actual), "success precondition revoke actual view");
			const DecodedSessionV1 revoke{11, actual.state.adapter.transition_epoch, 1, 7, 100, 2, 0, 0,
						      kWireProtocolVersionV1, 1, 1, 2, 0, {0, 0, 0, 0}};
			check(owner.real.offer({17, 2, 9950, revoke, ingress().authentication}), "success precondition real revoke receipt");
		}

		owner.fault = fault;
		const Intent intent = fault == FaultOwner::Fault::FakeClearUnlatched ? Intent::ClearLatch
				      : fault == FaultOwner::Fault::FakeEstablishHistory ? Intent::Establish : Intent::Revoke;
		const auto pending = console.prepareDiagnosticAdmin(intent, intent == Intent::ClearLatch ? TrustedSession{} : TrustedSession{7, 100});
		local(pending, BrokerCode::Ready, "validly shaped but dishonest prior view reaches pending");
		const auto rejected = console.confirmDiagnosticAdmin(pending.ticket);
		check(owner.corruption_applied && owner.submissions >= 2 && broker.quarantined()
		      && rejected.disposition == ConsoleDisposition::Quarantined
		      && rejected.broker_code == BrokerCode::InvalidOwnerResult,
		      "actual admin success cannot contradict mandatory broker precondition");
	}

	FaultOwner terminal;
	SupervisorBroker broker({50}, terminal);
	check(broker.attach(19), "terminal corruption no-receipt attachment");
	terminal.fault = FaultOwner::Fault::TerminalCause;
	const auto result = broker.detachDiagnosticOperator();
	check(terminal.corruption_applied && broker.quarantined()
	      && result.disposition == ConsoleDisposition::Quarantined, "terminal cause cannot contradict retained selector reason");
}

void everyPendingBinding()
{
	const std::array<const char *, 17> fields{"owner", "run", "controller", "operator", "generation", "transition",
			"selector", "map.valid", "map.source", "map.session", "map.controller", "map.epoch", "map.offset",
			"map.uncertainty", "map.from", "map.until", "physical.clock_mapping_valid"};

	for (unsigned field = 0; field < fields.size(); ++field) {
		FaultOwner owner;
		SupervisorBroker broker({50}, owner);
		check(broker.attach(19) && owner.real.tickDiagnostic(receipt(physical(), 1)), "A05 real pending-view setup");
		const auto pending = broker.prepareDiagnosticSelection({7, 100});
		local(pending, BrokerCode::Ready, "A05 real pending selection");
		owner.changed_binding = static_cast<int>(field);
		const auto result = broker.confirmDiagnosticSelection(pending.ticket);
		check(owner.corruption_applied && owner.submissions == 0 && !result.event_consumed && !result.action_performed
		      && result.disposition != ConsoleDisposition::Submitted, std::string("A05 changed pending binding: ") + fields[field]);
		DiagnosticOwnerView actual{};
		check(owner.real.diagnosticSnapshot(actual) && actual.state.selection == disabled
		      && !actual.state.session.has_supervisor_floor, "A05 no actual selection or event from changed view");
		const auto reused = broker.confirmDiagnosticSelection(pending.ticket);
		check(owner.submissions == 0 && !reused.event_consumed && reused.disposition != ConsoleDisposition::Submitted,
		      "A05 stale confirmation cannot be replayed");
	}
}

void uncertainOwners()
{
	for (const auto fault : {
		     FaultOwner::Fault::NotSubmitted, FaultOwner::Fault::Unknown, FaultOwner::Fault::Index,
		     FaultOwner::Fault::MissingBarrier, FaultOwner::Fault::Delta, FaultOwner::Fault::Consumption,
		     FaultOwner::Fault::CloseUnknown, FaultOwner::Fault::RegressTime
	     }) {
		FaultOwner owner;
		SupervisorBroker broker({50}, owner);
		SupervisorConsoleFacade console(broker);
		check(console.attachFixtureOperator(19) && owner.real.tickDiagnostic(receipt(physical(), 1)), "negative owner setup");
		const auto pending = console.prepareDiagnosticSelection({7, 100});
		local(pending, BrokerCode::Ready, "negative owner pending");
		DiagnosticOwnerView before{};
		check(owner.real.diagnosticSnapshot(before), "negative owner actual before view");
		owner.fault = fault;
		const auto result = fault == FaultOwner::Fault::CloseUnknown ? console.detachDiagnosticOperator()
				    : console.confirmDiagnosticSelection(pending.ticket);

		if (fault == FaultOwner::Fault::NotSubmitted) {
			local(result, BrokerCode::OwnerDeclined, "A25 owner NotSubmitted");
			DiagnosticOwnerView after{};
			check(!broker.quarantined() && owner.real.diagnosticSnapshot(after)
			      && stateKey(before.state) == stateKey(after.state) && owner.closures == 0,
			      "A25 NotSubmitted guarantees no event/adapter/closure side effect");
			local(console.confirmDiagnosticSelection(pending.ticket), BrokerCode::InvalidTicket, "A09 declined ticket still one-shot");

		} else {
			check(broker.quarantined() && result.disposition == ConsoleDisposition::Quarantined,
			      "A08/A24/A25/A26 uncertainty quarantines authority");
			check(owner.closures > 0, "uncertainty invokes synchronous diagnostic closure");
			const auto blocked = console.prepareDiagnosticSelection({7, 100});
			check(blocked.disposition == ConsoleDisposition::Quarantined && !blocked.event_consumed,
			      "quarantined owner cannot authorize subsequent selection");

			if (fault != FaultOwner::Fault::CloseUnknown) {
				DiagnosticOwnerView after{};
				check(owner.real.diagnosticSnapshot(after) && after.state.selection == disabled
				      && !after.state.admission.acknowledgement_current, "quarantine closes real owner selection and ACK");
			}
		}
	}
}
} // namespace

int main()
{
	positiveAndNonAuthority();
	ticketsAndDomains();
	lifecycleAndReceipts();
	realOwnerRefusalAndProvenance();
	corruptedAdministration();
	remainingCauseAndLatchCorruptions();
	malformedSuccessPreconditions();
	everyPendingBinding();
	uncertainOwners();
	std::cout << "Diagnostic supervisor owner: " << checks << " checks, " << failures << " failures\n";
	return failures == 0 ? 0 : 1;
}
