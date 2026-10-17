// Synthetic host contract checks. These source events are not native PX4 facts.
#include "NativeIngressDiagnosticOwner.hpp"

#include <cstdio>
#include <limits>
#include <memory>
#include <type_traits>
#include <variant>

namespace research_sitl
{
struct NativeIngressOwnerTestPeer {
	static std::unique_ptr<NativeIngressDiagnosticOwner> make(const NativeIngressOwnerConfig &config,
			NativeIngressMailbox &mailbox)
	{
		return std::unique_ptr<NativeIngressDiagnosticOwner>(new NativeIngressDiagnosticOwner(config, mailbox));
	}
	static void initial(NativeIngressDiagnosticOwner &owner, RawState state, uint64_t time)
	{
		owner.initial(state, time);
	}
	static void transition(NativeIngressDiagnosticOwner &owner, RawState before, RawState after, uint64_t time)
	{
		owner.transition(before, after, time);
	}
	static void sample(NativeIngressDiagnosticOwner &owner, RawState state, uint64_t time)
	{
		owner.sample(state, time);
	}
	static void resolve(NativeIngressDiagnosticOwner &owner, RawState state, uint64_t time)
	{
		owner.resolveReady(state, time);
	}
	static void close(NativeIngressDiagnosticOwner &owner, NativeIngressCloseCause cause, RawState state,
			  uint64_t time)
	{
		owner.stopOrFault(cause, state, time);
	}
};
} // namespace research_sitl

namespace
{
using namespace research_acceleration;
using namespace research_sitl;
using Peer = NativeIngressOwnerTestPeer;
constexpr RawState kDisarmed{1, 0};
constexpr RawState kOffboard{1, 14};
unsigned checks = 0;
unsigned failures = 0;

void check(bool condition, const char *message)
{
	++checks;

	if (!condition) { ++failures; std::fprintf(stderr, "FAIL: %s\n", message); }
}

NativeIngressOwnerConfig configuration()
{
	NativeIngressOwnerConfig config{};
	config.contract = {kWireProtocolVersionV1, 7, DisarmedPolicy::ValidateOnly,
		{{-10.F, -10.F, -10.F}, {10.F, 10.F, 10.F}, 12.F},
		{80000, 80000, 60000, 8000, 4000}, 0.01F
	};
	config.adapter_identity = {11};
	config.identity = {13, 17, kDiagnosticExecutionOrigin};
	config.transport_binding_id = 19;
	config.producer_lifetime_id = 23;
	config.source_clock_id = 29;
	config.channel = 3;
	config.peer_port = 14600;
	return config;
}

NativeIngressCandidate candidate(uint64_t token, uint64_t arrival = 101, uint64_t transition = 1)
{
	NativeIngressCandidate value{};
	value.token = token;
	value.native_run_id = 17;
	value.owner_lifetime_id = 13;
	value.controller_instance_id = 11;
	value.source_id = 7;
	value.transport_binding_id = 19;
	value.request_sequence = token;
	value.expected_transition_epoch = transition;
	value.producer_monotonic_us = 1000 + token;
	value.producer_expiry_monotonic_us = 1800 + token;
	value.arrival_native_us = arrival;
	value.acceleration_x_frd_m_s2 = 1.5F;
	value.acceleration_y_frd_m_s2 = -2.25F;
	value.acceleration_z_frd_m_s2 = 0.5F;
	value.protocol_version = kWireProtocolVersionV1;
	value.domain = 1;
	value.profile = 1;
	value.target_system = 1;
	value.target_component = 1;
	value.frame = 1;
	value.quantity = 1;
	value.reference_point = 1;
	value.channel = 3;
	value.header_system = 42;
	value.header_component = 191;
	value.incoming_link_id = 7;
	value.signing_timestamp_48 = 10000000 + token;
	value.mavlink_sequence = 17;
	value.payload_length = 108;
	value.parser_stage = 1;
	value.frame_crc = 123;
	value.signature[0] = 7;

	for (size_t index = 0; index < 6; ++index) {
		value.signature[index + 1] = static_cast<uint8_t>(value.signing_timestamp_48 >>(index * 8));
	}

	value.peer_port = 14600;
	return value;
}

void publish(NativeIngressMailbox &mailbox, const NativeIngressCandidate &value)
{
	check(mailbox.phase.load(std::memory_order_acquire) == NativeIngressPhase::Empty,
	      "synthetic worker slot starts empty");
	mailbox.phase.store(NativeIngressPhase::Preparing, std::memory_order_relaxed);
	mailbox.candidate = value;
	mailbox.phase.store(NativeIngressPhase::Ready, std::memory_order_release);
}

NativeIngressDisposition take(NativeIngressMailbox &mailbox)
{
	check(mailbox.phase.load(std::memory_order_acquire) == NativeIngressPhase::Resolved,
	      "owner published a resolved disposition");
	const auto disposition = mailbox.disposition;
	mailbox.phase.store(NativeIngressPhase::Empty, std::memory_order_release);
	return disposition;
}

void noAuthority(const DiagnosticResult &result)
{
	check(result.snapshot.domain == DiagnosticDomain::SitlDisarmedDiagnostic,
	      "owner result remains diagnostic");
	check(result.snapshot.selection == DiagnosticSelectionState::Disabled
	      && !result.snapshot.operator_attached && result.snapshot.operator_instance == 0,
	      "no operator or selector authority");
	check(result.snapshot.session.gate != SessionGate::Established
	      && !result.snapshot.admission.acknowledgement_current,
	      "no established session or ACK");
	check(!result.snapshot.has_last_accepted_sequence && !result.snapshot.last_accepted_authenticated,
	      "no accepted request history");

	for (uint8_t index = 0; index < result.record_count; ++index) {
		check(!result.records[index].eligible_for_control, "no eligible report");
	}
}

void completedTransaction()
{
	NativeIngressMailbox mailbox{};
	auto owner = Peer::make(configuration(), mailbox);
	check(owner->summary().state == NativeOwnerState::AwaitingInitial, "await real Initial");
	Peer::initial(*owner, kDisarmed, 100);
	Peer::sample(*owner, kDisarmed, 101);
	Peer::transition(*owner, kDisarmed, kOffboard, 102);
	Peer::transition(*owner, kOffboard, kDisarmed, 102);
	Peer::transition(*owner, kDisarmed, kDisarmed, 103);
	const auto before = owner->summary();
	check(before.source_count == 4 && before.source.sequence == 4
	      && before.source.physical_generation == 3 && before.source.transition_epoch == 3
	      && before.source.committed_time_us == 102 && before.source.checked_time_us == 103,
	      "initial, sample, ABA and unchanged assignment chronology");
	check(before.transaction_count == 0 && before.last_status_sequence == 0,
	      "source turns are not ingress transactions");
	publish(mailbox, candidate(41, 103, 3));
	Peer::resolve(*owner, kDisarmed, 104);
	const auto disposition = take(mailbox);
	check(disposition.token == 41 && disposition.kind == NativeIngressDispositionKind::Completed
	      && disposition.has_actual_request && disposition.has_status,
	      "completed request returns token and actual result status");
	const auto &status = disposition.status;
	check(status.reason == 4 && status.outcome == 1 && status.request_sequence == 41
	      && status.arrival_native_us == 103 && status.evaluation_native_us == 104
	      && status.source_checked_native_us == 103,
	      "status projects actual SelectionRequired with separate native times");
	check(status.source_sequence == 4 && status.state_generation == 3 && status.transition_epoch == 3
	      && status.arm_state == 1 && status.raw_nav_state == 0 && status.clock_mapping_epoch == 0,
	      "status projects retained source and absent map");
	check(status.selector_state == 0 && status.estimator_valid == 0 && status.clock_map_valid == 0
	      && status.eligible_for_control == 0 && status.target_system == 42 && status.target_component == 191,
	      "returned status grants no authority and targets the sender");
	check(owner->summary().transaction_count == 1 && owner->summary().last_status_sequence == 1
	      && owner->summary().source.checked_time_us == 104,
	      "one retained two-result transaction advances checked time and status floor");
	Peer::close(*owner, NativeIngressCloseCause::OwnerStop, kDisarmed, 105);
	NativeIngressTransactionRecord record{};
	check(owner->copyTransaction(0, record), "closed owner copies transaction");
	check(record.kind == NativeIngressTransactionKind::Completed && record.has_request
	      && record.candidate.token == 41 && record.disposition.kind == disposition.kind,
	      "retained transaction agrees with disposition");
	check(record.poll_result.code == DiagnosticCode::Rejected
	      && record.poll_result.has_operation_record
	      && record.request_result.code == DiagnosticCode::Rejected
	      && record.request_result.has_operation_record,
	      "two distinct real adapter turns were retained");
	const auto &poll = record.poll_result.records[record.poll_result.operation_record_index];
	const auto &request = record.request_result.records[record.request_result.operation_record_index];
	const auto *poll_cause = std::get_if<DecisionCode>(&poll.cause);
	const auto *request_cause = std::get_if<DiagnosticLocalReason>(&request.cause);
	check(poll.correlation.operation == DiagnosticOperationKind::Poll && poll_cause
	      && *poll_cause == DecisionCode::ClockMappingInvalid,
	      "actual poll reports absent clock map");
	check(request.correlation.operation == DiagnosticOperationKind::Request && request_cause
	      && *request_cause == DiagnosticLocalReason::SelectionRequired,
	      "actual request reports selection required");
	noAuthority(record.poll_result);
	noAuthority(record.request_result);
	NativeIngressClosureRecord closure{};
	check(owner->copyClosure(closure) && closure.cause == NativeIngressCloseCause::OwnerStop
	      && closure.fault == NativeIngressFault::None,
	      "normal Stop retains its distinct first cause");
	check(owner->summary().last_status_sequence == 1,
	      "later unsolicited Stop does not overwrite completed status");
}

void discardedCandidates()
{
	NativeIngressMailbox mailbox{};
	auto owner = Peer::make(configuration(), mailbox);
	Peer::initial(*owner, kDisarmed, 100);
	auto foreign = candidate(1);
	foreign.transport_binding_id = 99;
	publish(mailbox, foreign);
	Peer::resolve(*owner, kDisarmed, 101);
	auto disposition = take(mailbox);
	check(disposition.token == 1 && disposition.kind == NativeIngressDispositionKind::DiscardedForeign
	      && !disposition.has_actual_request && !disposition.has_status,
	      "foreign transport binding discarded without owner turn");
	auto unauthenticated = candidate(5);
	unauthenticated.parser_stage = 0;
	publish(mailbox, unauthenticated);
	Peer::resolve(*owner, kDisarmed, 101);
	disposition = take(mailbox);
	check(disposition.kind == NativeIngressDispositionKind::DiscardedForeign && !disposition.has_status,
	      "missing authenticated parser stage cannot reach owner turn");
	auto bad_length = candidate(6);
	bad_length.payload_length = 102;
	publish(mailbox, bad_length);
	Peer::resolve(*owner, kDisarmed, 101);
	disposition = take(mailbox);
	check(disposition.kind == NativeIngressDispositionKind::DiscardedForeign,
	      "invalid logical payload length cannot reach owner turn");
	publish(mailbox, candidate(2, 101, 99));
	Peer::resolve(*owner, kDisarmed, 101);
	disposition = take(mailbox);
	check(disposition.token == 2 && disposition.kind == NativeIngressDispositionKind::DiscardedStale
	      && !disposition.has_status, "stale transition discarded");
	publish(mailbox, candidate(3, 103));
	Peer::resolve(*owner, kDisarmed, 102);
	disposition = take(mailbox);
	check(disposition.token == 3 && disposition.kind == NativeIngressDispositionKind::DiscardedStale,
	      "future arrival cannot be evaluated");
	publish(mailbox, candidate(4, 102));
	Peer::transition(*owner, kDisarmed, kOffboard, 102);
	Peer::resolve(*owner, kOffboard, 103);
	disposition = take(mailbox);
	check(disposition.token == 4 && disposition.kind == NativeIngressDispositionKind::DiscardedStale,
	      "source transition before owner turn invalidates candidate");
	check(owner->summary().transaction_count == 0 && owner->summary().last_status_sequence == 0,
	      "discards leave transaction and status floors unchanged");
	Peer::close(*owner, NativeIngressCloseCause::OwnerStop, kOffboard, 104);
	check(owner->summary().source_count == 2 && owner->summary().transaction_count == 0,
	      "only actual source events reached the archive");
}

void readyClosure(NativeIngressCloseCause cause, uint16_t wire_reason, NativeIngressFault expected_fault)
{
	NativeIngressMailbox mailbox{};
	auto owner = Peer::make(configuration(), mailbox);
	Peer::initial(*owner, kDisarmed, 100);
	publish(mailbox, candidate(77, 101));
	Peer::close(*owner, cause, kDisarmed, 102);
	const auto disposition = take(mailbox);
	check(disposition.token == 77 && disposition.kind == NativeIngressDispositionKind::DiscardedClosed
	      && !disposition.has_actual_request,
	      "closure retires waiting candidate without request turn");
	check(disposition.has_status == (wire_reason != 0),
	      "only representable first closures return status");

	if (wire_reason != 0) {
		check(disposition.status.outcome == 2 && disposition.status.reason == wire_reason,
		      "closed status matches first closure cause");
	}

	NativeIngressClosureRecord closure{};
	check(owner->copyClosure(closure) && closure.cause == cause && closure.fault == expected_fault,
	      "first closure is retained separately");
	check(mailbox.terminal.load(std::memory_order_acquire)
	      && owner->summary().transaction_count == 0,
	      "closure is sticky and no packet-specific turn occurred");
	Peer::resolve(*owner, kDisarmed, 103);
	check(owner->summary().transaction_count == 0, "postclose resolve cannot reopen owner");
}

void faultAndCapacity()
{
	readyClosure(NativeIngressCloseCause::OwnerStop, 7, NativeIngressFault::None);
	readyClosure(NativeIngressCloseCause::SourceContinuityLost, 6, NativeIngressFault::SourceLifetimeLost);
	readyClosure(NativeIngressCloseCause::TransportFault, 0, NativeIngressFault::TransportFault);
	readyClosure(NativeIngressCloseCause::WorkerResourceExhausted, 8,
		     NativeIngressFault::WorkerResourceExhausted);
	{
		NativeIngressMailbox mailbox{};
		auto owner = Peer::make(configuration(), mailbox);
		Peer::initial(*owner, kDisarmed, 100);
		mailbox.async_fault.store(NativeIngressAsyncFault::TransportFault, std::memory_order_release);
		publish(mailbox, candidate(88, 101));
		Peer::resolve(*owner, kDisarmed, 102);
		const auto disposition = take(mailbox);
		check(disposition.kind == NativeIngressDispositionKind::DiscardedClosed && !disposition.has_status,
		      "async transport fault wins before ingress and has no invented wire reason");
		check(owner->summary().fault == NativeIngressFault::TransportFault,
		      "async fault names transport loss");
	}
	{
		NativeIngressMailbox mailbox{};
		auto owner = Peer::make(configuration(), mailbox);
		Peer::initial(*owner, kDisarmed, 100);

		for (size_t index = 1; index < kNativeIngressSourceCapacity; ++index) {
			Peer::sample(*owner, kDisarmed, 100 + index);
		}

		check(owner->summary().source_count == kNativeIngressSourceCapacity,
		      "exact 256 source records are retained");
		Peer::sample(*owner, kDisarmed, 100 + kNativeIngressSourceCapacity);
		check(owner->summary().state == NativeOwnerState::Faulted
		      && owner->summary().fault == NativeIngressFault::OwnerEvidenceExhausted
		      && owner->summary().source_count == kNativeIngressSourceCapacity,
		      "source record 257 closes before installing input");
		NativeSourceRecord last{};
		check(owner->copySource(kNativeIngressSourceCapacity - 1, last)
		      && last.after.sequence == kNativeIngressSourceCapacity,
		      "capacity closure preserves final valid source record");
	}
	{
		NativeIngressMailbox mailbox{};
		auto owner = Peer::make(configuration(), mailbox);
		Peer::initial(*owner, kDisarmed, 100);

		for (uint64_t index = 1; index <= kNativeIngressTransactionCapacity; ++index) {
			publish(mailbox, candidate(index, 100 + index));
			Peer::resolve(*owner, kDisarmed, 100 + index);
			const auto disposition = take(mailbox);
			check(disposition.kind == NativeIngressDispositionKind::Completed
			      && disposition.has_actual_request && disposition.has_status
			      && disposition.status.status_sequence == index,
			      "one completed negative transaction consumes each archive slot");
		}

		check(owner->summary().transaction_count == kNativeIngressTransactionCapacity,
		      "exact 32 transactions fit");
		publish(mailbox, candidate(33, 133));
		Peer::resolve(*owner, kDisarmed, 133);
		const auto disposition = take(mailbox);
		check(disposition.kind == NativeIngressDispositionKind::DiscardedClosed
		      && !disposition.has_actual_request && disposition.has_status
		      && disposition.status.reason == 8 && disposition.status.outcome == 2,
		      "transaction 33 closes through reserved terminal capacity");
		check(owner->summary().fault == NativeIngressFault::OwnerEvidenceExhausted
		      && owner->summary().transaction_count == kNativeIngressTransactionCapacity,
		      "overflow preserves 32 completed transactions");
		NativeIngressTransactionRecord last{};
		check(owner->copyTransaction(kNativeIngressTransactionCapacity - 1, last)
		      && last.candidate.token == kNativeIngressTransactionCapacity
		      && last.disposition.kind == NativeIngressDispositionKind::Completed,
		      "last completed transaction survives terminal overflow");
	}
}

} // namespace

int main()
{
	completedTransaction();
	discardedCandidates();
	faultAndCapacity();
	std::printf("%u C1E synthetic owner checks, %u failures\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
