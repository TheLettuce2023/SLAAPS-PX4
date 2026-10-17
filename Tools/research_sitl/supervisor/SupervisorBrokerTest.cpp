#include "SupervisorBroker.hpp"

#include <iostream>
#include <limits>

using namespace research_acceleration;
using namespace research_sitl;

namespace research_sitl
{
struct SupervisorBrokerTestPeer {
	static void seedCounters(SupervisorBroker &b, uint64_t t, uint64_t e) { b.ticket_floor_ = t; b.event_floor_ = e; }
};
}

namespace
{
int checks = 0;
int failures = 0;
void check(bool v, const char *n) { ++checks; if (!v) { ++failures; std::cerr << "FAILED: " << n << '\n'; } }
OwnerView view(uint64_t time = 1000) { return {true, false, 11, 51, 1, 41, time, false, {0, 0}, false, 0}; }
bool code(const ConsoleResult &r, ConsoleDisposition d, BrokerCode c)
{ return r.disposition == d && r.code == c && !r.has_adapter_code && !r.event_consumed; }
class Owner final : public SupervisorOwner
{
public:
	OwnerView current{view()}; bool available{true}; OwnerOutcome outcome{OwnerOutcome::NotSubmitted};
	int snapshots{0}; int submits{0}; SupervisorBroker *nested_broker{nullptr}; ConsoleResult nested{};
	bool snapshot(OwnerView &o) noexcept override { ++snapshots; if (!available) { return false; } o = current; return true; }
	OwnerOutcome submit(const Submission &, AdminResult &o) noexcept override
	{
		++submits;

		if (nested_broker != nullptr) { nested = nested_broker->prepare(Intent::ClearLatch, {0, 0}); } o = {}; return outcome;
	}
};
void lifecycle()
{
	Owner o{}; SupervisorBroker b({10}, o);
	check(code(b.prepare(Intent::Establish, {21, 31}), ConsoleDisposition::NotSubmitted, BrokerCode::NotAttached)
	      && o.snapshots == 0, "detached");
	check(!b.attach(0) && b.attach(1) && !b.attach(1), "attach");
	check(code(b.prepare(static_cast<Intent>(99), {21, 31}), ConsoleDisposition::NotSubmitted, BrokerCode::InvalidIntent), "invalid_intent");
	auto p = b.prepare(Intent::Establish, {21, 31});
	check(code(p, ConsoleDisposition::NotSubmitted, BrokerCode::Ready) && p.ticket == 1 && o.snapshots == 1
	      && o.submits == 0, "prepare_snapshot");
	check(code(b.prepare(Intent::ClearLatch, {0, 0}), ConsoleDisposition::NotSubmitted, BrokerCode::Busy), "one_pending");
	check(code(b.confirm(0), ConsoleDisposition::NotSubmitted, BrokerCode::InvalidTicket) && o.snapshots == 1, "ticket_retained");
	check(b.detach() && b.detach() && o.submits == 0, "detach");
}
void faults()
{
	Owner o{}; SupervisorBroker b({10}, o); b.attach(1);
	auto p = b.prepare(Intent::ClearLatch, {0, 0}); o.current.validator_time_us = 1011;
	check(code(b.confirm(p.ticket), ConsoleDisposition::NotSubmitted, BrokerCode::Expired) && o.submits == 0, "expiry");
	p = b.prepare(Intent::ClearLatch, {0, 0}); ++o.current.state_generation;
	check(code(b.confirm(p.ticket), ConsoleDisposition::NotSubmitted, BrokerCode::ContextChanged), "context_changed");
	Owner u{}; SupervisorBroker ub({10}, u); ub.attach(1); p = ub.prepare(Intent::ClearLatch, {0, 0}); u.available = false;
	check(code(ub.confirm(p.ticket), ConsoleDisposition::NotSubmitted, BrokerCode::OwnerUnavailable), "unavailable");
	Owner q{}; SupervisorBroker qb({10}, q); qb.attach(1); p = qb.prepare(Intent::ClearLatch, {0, 0}); q.outcome = OwnerOutcome::Unknown;
	check(code(qb.confirm(p.ticket), ConsoleDisposition::Quarantined, BrokerCode::OutcomeUnknown) && qb.quarantined(), "unknown_quarantine");
}
void reentryAndCounters()
{
	Owner o{}; SupervisorBroker b({10}, o); b.attach(1); auto p = b.prepare(Intent::ClearLatch, {0, 0}); o.nested_broker = &b;
	check(code(b.confirm(p.ticket), ConsoleDisposition::NotSubmitted, BrokerCode::OwnerDeclined)
	      && code(o.nested, ConsoleDisposition::NotSubmitted, BrokerCode::Busy), "reentry");
	Owner e{}; SupervisorBroker eb({10}, e); eb.attach(1); SupervisorBrokerTestPeer::seedCounters(eb, std::numeric_limits<uint64_t>::max(), 0);
	check(code(eb.prepare(Intent::ClearLatch, {0, 0}), ConsoleDisposition::Quarantined, BrokerCode::CounterExhausted)
	      && eb.quarantined(), "no_wrap");
	AdminResult sentinel{}; sentinel.code = AdminCode::Completed; check(!b.copyLastAdminResult(sentinel)
			&& sentinel.code == AdminCode::Completed, "audit_unchanged");
}
ContractConfig config()
{
	return {kWireProtocolVersionV1, 21, DisarmedPolicy::ValidateOnly,
		{Vec3f(-10.F, -10.F, -10.F), Vec3f(10.F, 10.F, 10.F), 12.F}, {10, 100, 50, 10, 5}, 0.01F};
}
TrustedStateSample sample(uint64_t time)
{
	TrustedStateSample s{}; s.generation = 51; s.observer_continuous = true;
	s.request_guard_context = {time, ArmState::Disarmed, ResearchModeState::Selected, true, true, true};
	s.map = {true, 21, 31, 11, 41, 1000, 2, 0, 1000}; return s;
}
class AdapterOwner final : public SupervisorOwner
{
public:
	WireAdapter adapter{config(), {11}};
	TrustedStateSample state{sample(1000)};
	AdapterSnapshot snap{};
	SessionSnapshot session{};
	// Independent test ingress evidence, never generated from a UI intent.
	DecodedSessionV1 packet{11, 1, 41, 21, 31, 0, 0, 0, kWireProtocolVersionV1, 1, 1, 1, 0, {0, 0, 0, 0}};
	SessionIngressVerdict ingress{true, true, true, true};
	bool packet_available{false};
	bool unknown_completion{false};
	unsigned dispatches{0};
	AdapterOwner() { observe(); }
	void observe()
	{
		const auto result = adapter.poll(state);
		snap = result.snapshot;
		session = adapter.sessionSnapshot();
	}
	OwnerView current() const
	{
		return {true, snap.terminal, 11, state.generation, snap.transition_epoch, state.map.map_epoch,
			state.request_guard_context.validator_receive_time_us, session.has_last_successful_session,
			session.last_successful_session, session.has_supervisor_floor, session.supervisor_floor};
	}
	bool snapshot(OwnerView &v) noexcept override
	{
		observe();
		v = current();
		return true;
	}
	OwnerOutcome submit(const Submission &s, AdminResult &out) noexcept override
	{
		const auto v = current();
		const auto &e = s.expected_view;

		if (v.ready != e.ready || v.terminal != e.terminal || v.controller_instance_id != e.controller_instance_id
		    || v.state_generation != e.state_generation || v.transition_epoch != e.transition_epoch
		    || v.clock_mapping_epoch != e.clock_mapping_epoch || v.validator_time_us != e.validator_time_us
		    || v.has_session_history != e.has_session_history || v.last_session.owner != e.last_session.owner
		    || v.last_session.session != e.last_session.session || v.has_supervisor_floor != e.has_supervisor_floor
		    || v.supervisor_floor != e.supervisor_floor) { return OwnerOutcome::NotSubmitted; }

		if (unknown_completion) { return OwnerOutcome::Unknown; }

		if (s.intent == Intent::ClearLatch) {
			++dispatches;
			out = adapter.clearLatchedFault(state, s.event);

		} else {
			const uint8_t op = s.intent == Intent::Establish ? 1 : 2;

			if (!packet_available || packet.source != s.requested_session.owner
			    || packet.session != s.requested_session.session || packet.operation != op) { return OwnerOutcome::NotSubmitted; }

			packet_available = false;
			++dispatches;
			out = adapter.processSession(state, packet, ingress, &s.event);
		}

		snap = out.snapshot; session = out.session; return OwnerOutcome::Completed;
	}
};
void actualAdapterCases()
{
	AdapterOwner owner{}; SupervisorBroker broker({10}, owner); broker.attach(1);
	auto p = broker.prepare(Intent::Establish, {21, 31}); auto r = broker.confirm(p.ticket);
	check(r.code == BrokerCode::OwnerDeclined && owner.dispatches == 0, "ui_tuple_not_guidance_evidence");
	owner.packet_available = true;
	p = broker.prepare(Intent::Establish, {21, 31}); r = broker.confirm(p.ticket);
	check(r.disposition == ConsoleDisposition::Submitted && r.code == BrokerCode::Submitted && r.event_consumed
	      && r.adapter_code == AdminCode::Completed, "actual_establish_consumed");
	owner.packet.operation = 2;
	owner.packet.operation_sequence = 1;
	owner.packet.transition_epoch = 2;
	owner.packet_available = true;
	owner.ingress = {false, true, true, true};
	owner.state.request_guard_context.validator_receive_time_us = 1001;
	p = broker.prepare(Intent::Revoke, {21, 31}); r = broker.confirm(p.ticket);
	check(r.disposition == ConsoleDisposition::Submitted && r.event_consumed && r.adapter_code == AdminCode::IngressRejected
	      && owner.snap.transition_epoch == 2, "actual_consumed_rejection");
	owner.ingress = {true, true, true, true};
	owner.packet_available = true;
	owner.state.request_guard_context.validator_receive_time_us = 1002;
	p = broker.prepare(Intent::Revoke, {21, 31}); r = broker.confirm(p.ticket);
	check(r.disposition == ConsoleDisposition::Submitted && r.event_consumed
	      && r.adapter_code == AdminCode::Completed, "actual_revoke_consumed");
	check(owner.snap.transition_epoch == 3 && owner.session.gate == SessionGate::Revoked, "actual_epoch_and_gate");
}
void clearAndPollingCases()
{
	AdapterOwner owner{};
	owner.state.request_guard_context.validator_receive_time_us = 999;
	const auto latched = owner.adapter.poll(owner.state); owner.snap = latched.snapshot; owner.session = owner.adapter.sessionSnapshot();
	SupervisorBroker broker({10}, owner); broker.attach(1);
	auto p = broker.prepare(Intent::ClearLatch, {0, 0}); auto r = broker.confirm(p.ticket);
	check(r.disposition == ConsoleDisposition::Submitted && r.adapter_code == AdminCode::Completed && r.event_consumed
	      && owner.session.gate == SessionGate::EstablishmentRequired, "actual_clear_latch");
	broker.detach(); const auto detached_poll = owner.adapter.poll(owner.state);
	check(detached_poll.snapshot.initialized && detached_poll.report_count == 1, "poll_while_detached");
	broker.attach(2);
	p = broker.prepare(Intent::ClearLatch, {0, 0});
	owner.unknown_completion = true;
	broker.confirm(p.ticket);
	const auto before = owner.snap.last_report_sequence;
	owner.observe();
	check(broker.quarantined() && owner.snap.initialized && owner.snap.last_report_sequence > before,
	      "same_owner_poll_while_quarantined");
}
}
int main() { lifecycle(); faults(); reentryAndCounters(); actualAdapterCases(); clearAndPollingCases(); std::cout << checks << " checks; " << failures << " failures\n"; return failures == 0 ? 0 : 1; }
