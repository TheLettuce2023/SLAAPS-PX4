#include "SupervisorBroker.hpp"

#include <array>
#include <cstring>
#include <iostream>
#include <string>

using namespace research_acceleration;
using namespace research_sitl;

namespace research_sitl
{
struct SupervisorBrokerTestPeer {
	static void seed(SupervisorBroker &b, uint64_t ticket, uint64_t event)
	{
		b.ticket_floor_ = ticket;
		b.event_floor_ = event;
	}
};
}

namespace
{
int checks = 0;
int failures = 0;
void check(bool value, const std::string &name)
{
	++checks;

	if (!value) { ++failures; std::cerr << "FAILED: " << name << '\n'; }
}
template<typename T> std::array<unsigned char, sizeof(T)> bytes(const T &value)
{
	std::array<unsigned char, sizeof(T)> result{};
	std::memcpy(result.data(), &value, sizeof(T));
	return result;
}
OwnerView view()
{
	return {true, false, 11, 51, 1, 41, 1000, false, {0, 0}, false, 0};
}
ContractConfig config()
{
	return {kWireProtocolVersionV1, 21, DisarmedPolicy::ValidateOnly,
		{Vec3f(-10.F, -10.F, -10.F), Vec3f(10.F, 10.F, 10.F), 12.F}, {10, 100, 50, 10, 5}, 0.01F};
}
TrustedStateSample sample()
{
	return {51, true, {1000, ArmState::Disarmed, ResearchModeState::Selected, true, true, true},
		{true, 21, 31, 11, 41, 1000, 2, 0, 1000}};
}
// Seed fault-injection records from a real, already-observed adapter. Mutated
// copies below are SYNTHETIC result conformance cases, not real flight events.
AdminResult seedReply(bool establish)
{
	WireAdapter adapter(config(), {11});
	adapter.poll(sample());
	SupervisorEvent grant{1, 51, 1, 11, establish ? SupervisorAction::Establish : SupervisorAction::ClearLatchedFault,
			      establish, {establish ? uint64_t(21) : 0, establish ? uint64_t(31) : 0}};

	if (establish) {
		DecodedSessionV1 packet{11, 1, 41, 21, 31, 0, 0, 0, kWireProtocolVersionV1, 1, 1, 1, 0, {0, 0, 0, 0}};
		return adapter.processSession(sample(), packet, {true, true, true, true}, &grant);
	}

	return adapter.clearLatchedFault(sample(), grant);
}

class Owner final : public SupervisorOwner
{
public:
	OwnerView current{view()};
	bool available{true};
	OwnerOutcome outcome{OwnerOutcome::Completed};
	AdminResult reply{seedReply(false)};
	Submission submission{};
	unsigned captures{0};
	unsigned calls{0};
	SupervisorBroker *reenter{nullptr};
	bool reenter_on_capture{false};
	ConsoleResult nested_prepare{}, nested_confirm{};
	bool nested_attach{true}, nested_detach{true}, nested_copy{true};

	void nested() noexcept
	{
		if (reenter == nullptr) { return; }

		nested_prepare = reenter->prepare(Intent::ClearLatch, {0, 0});
		nested_confirm = reenter->confirm(1);
		nested_attach = reenter->attach(99);
		nested_detach = reenter->detach();
		AdminResult sentinel{};
		nested_copy = reenter->copyLastAdminResult(sentinel);
	}
	bool snapshot(OwnerView &output) noexcept override
	{
		++captures;

		if (reenter_on_capture) { nested(); }

		if (!available) { return false; }

		output = current;
		return true;
	}
	OwnerOutcome submit(const Submission &value, AdminResult &output) noexcept override
	{
		++calls;
		submission = value;
		nested();
		output = reply;
		return outcome;
	}
};
void expectLocal(const ConsoleResult &r, BrokerCode code, bool quarantine, const std::string &name)
{
	check(r.code == code && r.disposition == (quarantine ? ConsoleDisposition::Quarantined : ConsoleDisposition::NotSubmitted)
	      && !r.has_adapter_code && !r.event_consumed, name);
}

void attachmentAndPending()
{
	Owner owner;
	SupervisorBroker disabled({0}, owner);
	check(!disabled.attach(1), "config_attach");
	expectLocal(disabled.prepare(Intent::ClearLatch, {0, 0}), BrokerCode::NotConfigured, false, "config_prepare");
	expectLocal(disabled.confirm(1), BrokerCode::NotConfigured, false, "config_confirm");
	check(owner.captures == 0 && owner.calls == 0, "config_no_owner");

	SupervisorBroker b({10}, owner);
	expectLocal(b.confirm(1), BrokerCode::NotAttached, false, "detached_confirm");
	check(!b.attach(0) && b.attach(1) && !b.attach(2), "attachment_identity");

	for (const auto tuple : {TrustedSession{0, 31}, TrustedSession{21, 0}, TrustedSession{0, 0}}) {
		expectLocal(b.prepare(Intent::Establish, tuple), BrokerCode::InvalidIntent, false, "invalid_establish_shape");
		expectLocal(b.prepare(Intent::Revoke, tuple), BrokerCode::InvalidIntent, false, "invalid_revoke_shape");
	}
	expectLocal(b.prepare(Intent::ClearLatch, {21, 31}), BrokerCode::InvalidIntent, false, "invalid_clear_shape");
	expectLocal(b.prepare(static_cast<Intent>(99), {0, 0}), BrokerCode::InvalidIntent, false, "unknown_intent");
	check(owner.captures == 0, "malformed_no_capture");
	auto p = b.prepare(Intent::ClearLatch, {0, 0});
	check(p.code == BrokerCode::Ready && p.ticket == 1 && owner.captures == 1, "first_ticket");
	const auto before = bytes(b);
	expectLocal(b.prepare(Intent::ClearLatch, {0, 0}), BrokerCode::Busy, false, "pending_busy");
	expectLocal(b.confirm(0), BrokerCode::InvalidTicket, false, "zero_ticket");
	expectLocal(b.confirm(2), BrokerCode::InvalidTicket, false, "wrong_ticket");
	check(bytes(b) == before && owner.captures == 1, "bad_ticket_preserves_pending");
	check(b.detach() && b.detach() && !b.attach(1) && b.attach(2), "detach_idempotent_new_identity");
	expectLocal(b.confirm(p.ticket), BrokerCode::InvalidTicket, false, "old_ticket_after_attach");
	p = b.prepare(Intent::ClearLatch, {0, 0});
	check(p.ticket == 2, "reattach_ticket_not_reset");
	const auto result = b.confirm(p.ticket);
	check(result.code == BrokerCode::Submitted && result.has_adapter_code
	      && result.adapter_code == AdminCode::ClearNotLatched && result.event_consumed, "consumed_rejection_submitted");
	check(owner.submission.event.event_id == 1, "rejected_confirmation_no_event_gap");
	expectLocal(b.confirm(p.ticket), BrokerCode::InvalidTicket, false, "consumed_ticket_no_replay");
	check(owner.calls == 1, "single_dispatch");
	owner.current.has_supervisor_floor = true; owner.current.supervisor_floor = 1;
	check(b.detach() && b.attach(UINT64_MAX), "max_attachment_valid");
	p = b.prepare(Intent::ClearLatch, {0, 0});
	check(p.code == BrokerCode::Ready && p.ticket == 3, "floor_survives_detach");
	check(b.detach() && !b.attach(UINT64_MAX) && !b.attach(3), "attachment_no_wrap");
}

void snapshotMatrix()
{
	for (unsigned index = 0; index < 10; ++index) {
		Owner owner;

		switch (index) {
		case 0: owner.current.ready = false; break;

		case 1: owner.current.controller_instance_id = 0; break;

		case 2: owner.current.state_generation = 0; break;

		case 3: owner.current.transition_epoch = 0; break;

		case 4: owner.current.clock_mapping_epoch = 0; break;

		case 5: owner.current.has_session_history = true; break;

		case 6: owner.current.last_session = {21, 31}; break;
		case 7: owner.current.has_supervisor_floor = true; break;

		case 8: owner.current.supervisor_floor = 1; break;

		case 9: owner.available = false; break;
		}

		SupervisorBroker b({10}, owner); b.attach(1);
		expectLocal(b.prepare(Intent::ClearLatch, {0, 0}),
			    index == 9 ? BrokerCode::OwnerUnavailable : BrokerCode::OwnerNotReady, false, "invalid_view");
		check(owner.calls == 0 && !b.quarantined(), "invalid_view_no_dispatch");
	}

	for (unsigned index = 0; index < 7; ++index) {
		Owner owner; SupervisorBroker b({10}, owner); b.attach(1);
		const auto p = b.prepare(Intent::ClearLatch, {0, 0});

		switch (index) {
		case 0: ++owner.current.state_generation; break;

		case 1: ++owner.current.transition_epoch; break;

		case 2: ++owner.current.clock_mapping_epoch; break;

		case 3: owner.current.has_session_history = true; owner.current.last_session = {21, 31}; break;

		case 4: ++owner.current.controller_instance_id; break;

		case 5: owner.current.has_supervisor_floor = true; owner.current.supervisor_floor = 1; break;

		case 6: owner.current.validator_time_us = 999; break;
		}

		const bool fatal = index >= 4;
		expectLocal(b.confirm(p.ticket), fatal ? BrokerCode::AuthorityDiscontinuity : BrokerCode::ContextChanged, fatal, "changed_view");
		check(owner.calls == 0, "changed_view_no_dispatch");

		if (!fatal) { expectLocal(b.confirm(p.ticket), BrokerCode::InvalidTicket, false, "changed_view_consumed_ticket"); }
	}

	Owner terminal; terminal.current.terminal = true;
	SupervisorBroker b({10}, terminal); b.attach(1);
	expectLocal(b.prepare(Intent::ClearLatch, {0, 0}), BrokerCode::AuthorityDiscontinuity, true, "terminal_view");
	Owner prior; prior.current.has_supervisor_floor = true; prior.current.supervisor_floor = 1;
	SupervisorBroker new_broker({10}, prior); new_broker.attach(1);
	expectLocal(new_broker.prepare(Intent::ClearLatch, {0, 0}), BrokerCode::AuthorityDiscontinuity, true, "cannot_reconstruct_existing_stream");
}

void timingAndCounters()
{
	for (uint64_t age : {uint64_t(0), uint64_t(10), uint64_t(11)}) {
		Owner owner; owner.outcome = OwnerOutcome::NotSubmitted;
		SupervisorBroker b({10}, owner); b.attach(1); const auto p = b.prepare(Intent::ClearLatch, {0, 0});
		owner.current.validator_time_us += age;
		expectLocal(b.confirm(p.ticket), age > 10 ? BrokerCode::Expired : BrokerCode::OwnerDeclined, false, "age_boundary");
		check(owner.calls == (age > 10 ? 0U : 1U), "age_call_count");
	}
	Owner high; high.current.validator_time_us = UINT64_MAX - 5; high.outcome = OwnerOutcome::NotSubmitted;
	SupervisorBroker b({10}, high); b.attach(1); auto p = b.prepare(Intent::ClearLatch, {0, 0});
	high.current.validator_time_us = UINT64_MAX;
	expectLocal(b.confirm(p.ticket), BrokerCode::OwnerDeclined, false, "no_deadline_overflow");
	SupervisorBrokerTestPeer::seed(b, UINT64_MAX - 1, 0);
	p = b.prepare(Intent::ClearLatch, {0, 0});
	check(p.ticket == UINT64_MAX, "last_ticket_usable");
	expectLocal(b.confirm(p.ticket), BrokerCode::OwnerDeclined, false, "last_ticket_once");
	expectLocal(b.prepare(Intent::ClearLatch, {0, 0}), BrokerCode::CounterExhausted, true, "ticket_exhaustion");

	Owner events; SupervisorBroker eb({10}, events); eb.attach(1);
	SupervisorBrokerTestPeer::seed(eb, 0, UINT64_MAX);
	events.current.has_supervisor_floor = true; events.current.supervisor_floor = UINT64_MAX;
	p = eb.prepare(Intent::ClearLatch, {0, 0});
	expectLocal(eb.confirm(p.ticket), BrokerCode::CounterExhausted, true, "event_exhaustion");
	check(events.calls == 0, "exhaustion_no_owner_dispatch");

	Owner last;
	SupervisorBroker lb({10}, last); lb.attach(1);
	SupervisorBrokerTestPeer::seed(lb, 0, UINT64_MAX - 1);
	last.current.has_supervisor_floor = true; last.current.supervisor_floor = UINT64_MAX - 1;
	last.reply.correlation.supervisor_event_id = UINT64_MAX;
	last.reply.reports[0].correlation = last.reply.correlation;
	last.reply.session.supervisor_floor = UINT64_MAX;
	p = lb.prepare(Intent::ClearLatch, {0, 0});
	check(lb.confirm(p.ticket).event_consumed && last.submission.event.event_id == UINT64_MAX, "last_event_usable");
	last.current.supervisor_floor = UINT64_MAX;
	p = lb.prepare(Intent::ClearLatch, {0, 0});
	expectLocal(lb.confirm(p.ticket), BrokerCode::CounterExhausted, true, "last_event_no_wrap");
	check(last.calls == 1, "last_event_single_dispatch");
}

void outcomesAndReentry()
{
	for (auto outcome : {OwnerOutcome::Unknown, static_cast<OwnerOutcome>(99)}) {
		Owner owner; owner.outcome = outcome; SupervisorBroker b({10}, owner); b.attach(1);
		const auto p = b.prepare(Intent::ClearLatch, {0, 0});
		expectLocal(b.confirm(p.ticket), BrokerCode::OutcomeUnknown, true, "unknown_completion");
		expectLocal(b.prepare(Intent::ClearLatch, {0, 0}), BrokerCode::Quarantined, true, "quarantine_prepare");
		expectLocal(b.confirm(p.ticket), BrokerCode::Quarantined, true, "quarantine_confirm");
		check(b.detach() && !b.attach(2) && owner.calls == 1, "no_quarantine_reset");
	}

	for (bool in_capture : {false, true}) {
		Owner owner; owner.outcome = OwnerOutcome::NotSubmitted;
		SupervisorBroker b({10}, owner); b.attach(1);
		owner.reenter = &b; owner.reenter_on_capture = in_capture;
		const auto p = b.prepare(Intent::ClearLatch, {0, 0});
		b.confirm(p.ticket);
		expectLocal(owner.nested_prepare, BrokerCode::Busy, false, "reentry_prepare");
		expectLocal(owner.nested_confirm, BrokerCode::Busy, false, "reentry_confirm");
		check(!owner.nested_attach && !owner.nested_detach && !owner.nested_copy && owner.calls == 1, "reentry_no_mutation");
	}
	Owner owner; SupervisorBroker b({10}, owner); b.attach(1);
	AdminResult out{}; std::memset(&out, 0xA5, sizeof(out)); const auto sentinel = bytes(out);
	check(!b.copyLastAdminResult(out) && bytes(out) == sentinel, "no_audit_unchanged");
	const auto p = b.prepare(Intent::ClearLatch, {0, 0}); b.confirm(p.ticket);
	check(b.copyLastAdminResult(out) && bytes(out) == bytes(owner.reply), "actual_audit_exact_copy");
}

void resultMatrix()
{
	for (unsigned index = 0; index < 24; ++index) {
		Owner owner; SupervisorBroker b({10}, owner); b.attach(1);
		const auto p = b.prepare(Intent::ClearLatch, {0, 0});
		auto &r = owner.reply;

		switch (index) {
		case 0: r.report_count = 0; break;

		case 1: r.report_count = 4; break;

		case 2: r.correlation.has_supervisor_event = false; break;

		case 3: ++r.correlation.supervisor_event_id; break;

		case 4: r.snapshot.initialized = false; break;

		case 5: r.snapshot.terminal = true; break;

		case 6: ++r.snapshot.state_generation; break;

		case 7: ++r.snapshot.transition_epoch; break;

		case 8: r.session.has_supervisor_floor = false; break;

		case 9: ++r.session.supervisor_floor; break;

		case 10: r.correlation.has_packet = true; break;

		case 11: r.correlation.packet_source = 21; break;

		case 12: r.correlation.packet_session = 31; break;

		case 13: r.correlation.packet_operation_sequence = 1; break;

		case 14: r.reports[0].report_sequence = 0; break;

		case 15: r.reports[0].code = AdminCode::MalformedPacket; break;

		case 16: r.reports[0].kind = AdminReportKind::Terminal; break;

		case 17: r.reports[0].evidence.captured = false; break;

		case 18: ++r.reports[0].evidence.controller_instance_id; break;

		case 19: ++r.reports[0].validator_time_us; break;

		case 20: r.session.last_successful_session = {21, 31}; break;
		case 21: r.code = static_cast<AdminCode>(999); break;

		case 22: r.reports[0].report_sequence = UINT64_MAX; r.snapshot.last_report_sequence = UINT64_MAX; break;

		case 23: r.session.gate = static_cast<SessionGate>(99); break;
		}

		expectLocal(b.confirm(p.ticket), BrokerCode::InvalidOwnerResult, true, "malformed_result");
		AdminResult audit{};
		check(b.copyLastAdminResult(audit) && bytes(audit) == bytes(r), "invalid_result_retained_for_diagnostics");
		check(owner.calls == 1, "malformed_dispatch_once");
	}

	for (unsigned index = 0; index < 11; ++index) {
		Owner owner; owner.reply = seedReply(true); SupervisorBroker b({10}, owner); b.attach(1);
		const auto p = b.prepare(Intent::Establish, {21, 31});
		auto &r = owner.reply;

		switch (index) {
		case 0: break;

		case 1: r.correlation.has_packet = false; break;

		case 2: ++r.correlation.packet_source; break;

		case 3: ++r.correlation.packet_session; break;

		case 4: r.action_performed = false; break;

		case 5: r.admission_closed = false; break;

		case 6: r.action_kind = AdminActionKind::Revoke; break;

		case 7: r.session.gate = SessionGate::Revoked; break;

		case 8: r.reports[0].correlation.has_packet = true; break;

		case 9: r.reports[1].report_sequence = r.reports[0].report_sequence; break;

		case 10: r.session.has_last_successful_session = false; break;
		}

		const auto result = b.confirm(p.ticket);

		if (index == 0) {
			check(result.code == BrokerCode::Submitted && result.has_adapter_code && result.adapter_code == AdminCode::Completed
			      && result.event_consumed, "zero_wire_sequence_success");

		} else { expectLocal(result, BrokerCode::InvalidOwnerResult, true, "malformed_success"); }
	}

	// A structurally consistent unconsumed operation is diagnostic only.
	Owner owner; auto &r = owner.reply;
	r.correlation.supervisor_event_consumed = false;
	r.reports[0].correlation = r.correlation;
	r.session.has_supervisor_floor = false; r.session.supervisor_floor = 0;
	SupervisorBroker b({10}, owner); b.attach(1); const auto p = b.prepare(Intent::ClearLatch, {0, 0});
	const auto result = b.confirm(p.ticket);
	check(result.code == BrokerCode::AdapterUnconsumed && result.disposition == ConsoleDisposition::Quarantined
	      && result.has_adapter_code && !result.event_consumed, "unconsumed_quarantine");

	// Synthetic terminal callback shape; adapter terminal mechanics have their
	// own unchanged session/state suites. No later dispatch or reset is allowed.
	Owner terminal;
	terminal.reply = r;
	auto &t = terminal.reply;
	t.code = AdminCode::CounterExhausted;
	t.snapshot.terminal = true;
	t.snapshot.last_report_sequence = UINT64_MAX;
	t.reports[0].code = t.code;
	t.reports[0].kind = AdminReportKind::Terminal;
	t.reports[0].report_sequence = UINT64_MAX;
	SupervisorBroker tb({10}, terminal); tb.attach(1);
	const auto tp = tb.prepare(Intent::ClearLatch, {0, 0});
	const auto tr = tb.confirm(tp.ticket);
	check(tr.code == BrokerCode::AdapterUnconsumed && tr.has_adapter_code
	      && tr.adapter_code == AdminCode::CounterExhausted && !tr.event_consumed, "terminal_unconsumed");
	expectLocal(tb.prepare(Intent::ClearLatch, {0, 0}), BrokerCode::Quarantined, true, "terminal_no_next_event");
	check(terminal.calls == 1 && tb.detach() && !tb.attach(2), "terminal_no_reset");
}
}

int main()
{
	attachmentAndPending();
	snapshotMatrix();
	timingAndCounters();
	outcomesAndReentry();
	resultMatrix();
	std::cout << checks << " checks; " << failures << " failures\n";
	return failures ? 1 : 0;
}
