#include "WireAdapter.hpp"

#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>

using namespace research_acceleration;

namespace research_acceleration
{
struct WireAdapterAdmissionTestPeer {
	static void seed(WireAdapter &adapter, uint64_t epoch, uint64_t report,
			 bool has_floor, uint64_t floor)
	{
		adapter.snapshot_.transition_epoch = epoch;
		adapter.snapshot_.last_report_sequence = report;
		adapter.session_snapshot_.has_wire_operation_floor = has_floor;
		adapter.session_snapshot_.wire_operation_floor = floor;
	}

	static const RequestGuard *guardAddress(const WireAdapter &adapter) { return &adapter.guard_; }
};
} // namespace research_acceleration

namespace
{
constexpr uint64_t kController = 11;
constexpr SourceId kSource = 21;
constexpr SessionId kSession = 31;
constexpr uint64_t kMapEpoch = 41;
constexpr uint64_t kGeneration = 51;
constexpr uint64_t kArmedGeneration = 52;
constexpr uint64_t kMaximum = std::numeric_limits<uint64_t>::max();
int checks = 0;
int failures = 0;

void check(bool value, const std::string &name)
{
	++checks;

	if (!value) { ++failures; std::cerr << "FAILED: " << name << '\n'; }
}

ContractConfig config()
{
	return {kWireProtocolVersionV1, kSource, DisarmedPolicy::ValidateOnly,
		{Vec3f(-10.F, -10.F, -10.F), Vec3f(10.F, 10.F, 10.F), 12.F},
		{10, 100, 50, 10, 5}, 0.01F};
}

TrustedStateSample sample(uint64_t generation = kGeneration, TimeUs time = 1000,
			  ArmState arm = ArmState::Disarmed)
{
	TrustedStateSample result{};
	result.generation = generation;
	result.observer_continuous = true;
	result.request_guard_context = {time, arm, ResearchModeState::Selected, true, true, true};
	result.map = {true, kSource, kSession, kController, kMapEpoch, 1000, 2, 0, 1000};
	return result;
}

SessionIngressVerdict ingress() { return {true, true, true, true}; }

DecodedSessionV1 session(uint64_t epoch, uint64_t sequence, uint8_t operation,
			 TimeUs raw = 0, uint64_t status = 0)
{
	return {kController, epoch, kMapEpoch, kSource, kSession, sequence, raw, status,
		kWireProtocolVersionV1, 1, 1, operation, 0, {0, 0, 0, 0}};
}

DecodedRequestV1 request(uint64_t epoch, uint64_t sequence, TimeUs creation = 110,
			 TimeUs expiry = 130)
{
	WireRequestV1 wire{kSource, kSession, sequence, creation, expiry, kController,
			   epoch, kMapEpoch, 1.F, 0.F, 0.F, kWireProtocolVersionV1,
			   kWireFrameFrdV1, kWireQuantityInertialAccelerationV1,
			   kWireReferencePointCgV1};
	return {wire, 1, 1, 0, {0, 0, 0, 0}};
}

SupervisorEvent establishGrant(uint64_t id, uint64_t generation, uint64_t epoch)
{
	return {id, generation, epoch, kController, SupervisorAction::Establish,
		true, {kSource, kSession}};
}

template<typename T>
std::array<unsigned char, sizeof(T)> bytes(const T &value)
{
	std::array<unsigned char, sizeof(T)> result{};
	std::memcpy(result.data(), &value, sizeof(T));
	return result;
}

bool zeroCorrelation(const IngressCorrelation &value)
{
	return value.packet_kind == IngressPacketKind::None && value.source == 0 && value.session == 0
	       && value.controller_instance_id == 0 && value.transition_epoch == 0
	       && value.map_epoch == 0 && value.packet_sequence == 0
	       && value.acknowledged_status_sequence == 0;
}

bool zeroDecision(const Decision &value)
{
	return value.code == DecisionCode::Accepted && !value.syntactically_valid
	       && !value.eligible_for_control && !value.latched
	       && value.first_latched_code == DecisionCode::Accepted
	       && !value.conservative_age_valid && value.conservative_age_us == 0
	       && value.rejection_count == 0;
}

void checkUnused(const IngressResult &result, const std::string &name)
{
	check(result.report_count <= 2, name + "_bounded");

	for (uint8_t index = result.report_count; index < 2; ++index) {
		check(result.reports[index].report_sequence == 0
		      && result.reports[index].kind == IngressReportKind::ExogenousBarrier
		      && result.reports[index].code == IngressCode::Observed
		      && !result.reports[index].has_s1_decision
		      && zeroCorrelation(result.reports[index].correlation), name + "_unused_zero");
	}
}

void establishAndArm(WireAdapter &adapter)
{
	auto supervisor_grant = establishGrant(1, kGeneration, 1);
	const auto established = adapter.processSession(sample(), session(1, 0, 1), ingress(), &supervisor_grant);
	check(established.code == AdminCode::Completed && established.report_count == 3
	      && established.snapshot.transition_epoch == 2 && established.snapshot.last_report_sequence == 3
	      && established.session.gate == SessionGate::Established
	      && established.session.wire_operation_floor == 0, "setup_established");
	const auto armed = adapter.poll(sample(kArmedGeneration, 1100, ArmState::Armed));
	check(armed.report_count == 2 && armed.reports[0].report_sequence == 4
	      && armed.reports[0].transition_epoch == 3 && armed.reports[1].report_sequence == 5
	      && armed.snapshot.barrier_report_sequence == 4
	      && armed.snapshot.barrier_validator_time_us == 1100, "setup_armed_barrier");
}

IngressResult acceptAck(WireAdapter &adapter, uint64_t expected_report_sequence = 6)
{
	const auto result = adapter.processAcknowledgement(sample(kArmedGeneration, 1110, ArmState::Armed),
			    session(3, 1, 3, 105, 4), ingress());
	checkUnused(result, "ack");
	check(result.code == IngressCode::Completed && !result.admission_closed && result.report_count == 1
	      && (expected_report_sequence == 0 || result.reports[0].report_sequence == expected_report_sequence)
	      && result.reports[0].transition_epoch == 3
	      && result.reports[0].kind == IngressReportKind::Operation
	      && result.has_prestate_s1_decision && !result.has_operation_s1_decision
	      && result.prestate_s1_decision.code == DecisionCode::NoEligibleRequest
	      && result.reports[0].s1_decision.code == DecisionCode::NoEligibleRequest
	      && zeroDecision(result.operation_s1_decision),
	      "ack_golden_report");
	check(result.admission.acknowledgement_current && result.admission.acknowledgement_operation_sequence == 1
	      && result.admission.acknowledged_status_sequence == 4
	      && result.admission.acknowledgement_raw_producer_time_us == 105
	      && result.admission.mapped_acknowledgement_time_us == 1105
	      && result.admission.earliest_acknowledgement_time_us == 1103
	      && result.admission.latest_acknowledgement_time_us == 1107
	      && result.admission.source == kSource && result.admission.session == kSession
	      && result.admission.controller_instance_id == kController
	      && result.admission.transition_epoch == 3 && result.admission.map_epoch == kMapEpoch
	      && result.admission.map.validator_minus_producer_us == 1000
	      && result.admission.map.uncertainty_us == 2
	      && result.session.wire_operation_floor == 1
	      && result.reports[0].correlation.packet_kind == IngressPacketKind::Acknowledgement,
	      "ack_golden_snapshot");
	return result;
}

IngressResult submit(WireAdapter &adapter, uint64_t sequence = 1, TimeUs creation = 110,
		     TimeUs expiry = 130, TimeUs now = 1115)
{
	return adapter.processRequest(sample(kArmedGeneration, now, ArmState::Armed),
				      request(3, sequence, creation, expiry), ingress());
}

void goldenAndAccess()
{
	WireAdapter adapter(config(), {kController});
	establishAndArm(adapter);
	acceptAck(adapter);
	const auto accepted = submit(adapter);
	check(accepted.code == IngressCode::Completed && accepted.report_count == 1
	      && accepted.reports[0].report_sequence == 7 && accepted.has_operation_s1_decision
	      && accepted.operation_s1_decision.code == DecisionCode::Accepted
	      && accepted.session.wire_operation_floor == 1, "request_golden");
	CanonicalRequest output{};
	const auto access = adapter.getEligibleRequest(sample(kArmedGeneration, 1116, ArmState::Armed), output);
	check(access.code == AdapterCode::EligibleRequest && !access.admission_closed
	      && access.reports[0].report_sequence == 8 && output.sequence == 1
	      && output.producer_time_in_validator_domain_us == 1110, "getter_uses_s1_cache");
	const CanonicalRequest sentinel{7, 8, 9, 10, 11, 12, 13, 14, 15, Frame::Frd,
					Quantity::InertialAcceleration, ReferencePoint::Cg, Vec3f(3.F, 4.F, 5.F)};
	output = sentinel;
	const auto expired = adapter.getEligibleRequest(sample(kArmedGeneration, 1128, ArmState::Armed), output);
	check(expired.code == AdapterCode::GuardRejected && !expired.admission_closed
	      && expired.reports[0].report_sequence == 9
	      && expired.reports[0].s1_decision.code == DecisionCode::RequestExpired
	      && bytes(output) == bytes(sentinel),
	      "expiry_keeps_ack_and_output");
}

void packetNegatives()
{
	WireAdapter no_session(config(), {kController});
	const auto no_floor = no_session.processAcknowledgement(sample(), session(1, 0, 3, 1, 1), ingress());
	check(no_floor.code == IngressCode::SessionGateRejected, "ack_zero_without_floor_gate");
	WireAdapter adapter(config(), {kController});
	establishAndArm(adapter);
	auto malformed = session(3, 1, 3, 105, 4);
	malformed.flags = 1;
	check(adapter.processAcknowledgement(sample(kArmedGeneration, 1110, ArmState::Armed), malformed, ingress()).code
	      == IngressCode::MalformedPacket, "ack_malformed");

	for (unsigned index = 0; index < 4; ++index) {
		auto verdict = ingress();

		if (index == 0) { verdict.mavlink2 = false; }

		if (index == 1) { verdict.parser_authenticated = false; }

		if (index == 2) { verdict.configured_header_channel_source_binding = false; }

		if (index == 3) { verdict.configured_target_binding = false; }

		check(adapter.processAcknowledgement(sample(kArmedGeneration, 1110, ArmState::Armed),
						     session(3, 1, 3, 105, 4), verdict).code == IngressCode::IngressRejected,
		      "ack_ingress_negative");
	}

	check(adapter.processAcknowledgement(sample(kArmedGeneration, 1110, ArmState::Armed),
					     session(2, 1, 3, 105, 4), ingress()).code == IngressCode::BindingMismatch,
	      "ack_binding");
	check(adapter.processAcknowledgement(sample(kArmedGeneration, 1110, ArmState::Armed),
					     session(3, 1, 3, 105, 3), ingress()).code == IngressCode::AcknowledgementStatusMismatch,
	      "ack_status");
	check(adapter.processAcknowledgement(sample(kArmedGeneration, 1110, ArmState::Armed),
					     session(3, 1, 3, 102, 4), ingress()).code == IngressCode::AcknowledgementOrdering,
	      "ack_strict_barrier");
	acceptAck(adapter, 0);
	check(adapter.processAcknowledgement(sample(kArmedGeneration, 1111, ArmState::Armed),
					     session(3, 1, 3, 105, 4), ingress()).code == IngressCode::WireOperationReplay,
	      "ack_duplicate_floor");
	const auto later = adapter.processAcknowledgement(sample(kArmedGeneration, 1111, ArmState::Armed),
			   session(3, 2, 3, 106, 4), ingress());
	check(later.code == IngressCode::AcknowledgementFrozen
	      && later.admission.acknowledgement_raw_producer_time_us == 105, "ack_immutable");
}

void requestCacheAndFailures()
{
	WireAdapter missing(config(), {kController});
	establishAndArm(missing);
	check(submit(missing).code == IngressCode::AcknowledgementRequired, "request_no_ack");
	WireAdapter adapter(config(), {kController});
	establishAndArm(adapter);
	acceptAck(adapter);
	auto malformed = request(3, 1);
	malformed.flags = 1;
	check(adapter.processRequest(sample(kArmedGeneration, 1115, ArmState::Armed), malformed, ingress()).code
	      == IngressCode::MalformedPacket, "request_malformed");
	check(submit(adapter, 1, 130, 130).code == IngressCode::WireTimeInvalid, "request_wire_time");
	auto nonfinite = request(3, 1);
	nonfinite.wire.acceleration_x_frd_m_s2 = std::numeric_limits<float>::infinity();
	check(adapter.processRequest(sample(kArmedGeneration, 1115, ArmState::Armed), nonfinite, ingress()).code
	      == IngressCode::WireNonFinite, "request_nonfinite");
	check(submit(adapter, 1, 109, 130).code == IngressCode::AcknowledgementOrdering, "request_ack_order");
	check(submit(adapter, 1, 120, 220, 1125).code == IngressCode::Completed, "cache_old_request");
	const auto too_old = submit(adapter, 2, 110, 210, 1159);
	check(too_old.code == IngressCode::S1Rejected
	      && too_old.operation_s1_decision.code == DecisionCode::RequestTooOld
	      && !too_old.operation_s1_decision.latched, "candidate_old_preserves_cache");
	CanonicalRequest old{};
	check(adapter.getEligibleRequest(sample(kArmedGeneration, 1159, ArmState::Armed), old).code
	      == AdapterCode::EligibleRequest && old.sequence == 1 && old.producer_monotonic_us == 120,
	      "old_cache_accessible");
	WireAdapter replacement(config(), {kController});
	establishAndArm(replacement);
	acceptAck(replacement);
	check(submit(replacement, 1, 110, 210).code == IngressCode::Completed, "replacement_old");
	check(submit(replacement, 2, 160, 190, 1161).code == IngressCode::Completed,
	      "fresh_replaces_after_old_pre_poll");
}

void closureAndCapacity()
{
	ContractConfig invalid = config();
	invalid.permitted_source = 0;
	WireAdapter not_configured(invalid, {kController});
	const auto inert = not_configured.processAcknowledgement(sample(), session(1, 0, 3, 1, 1), ingress());
	check(inert.code == IngressCode::NotConfigured && inert.report_count == 0
	      && inert.snapshot.last_report_sequence == 0, "ingress_not_configured_inert");

	WireAdapter adapter(config(), {kController});
	establishAndArm(adapter);
	acceptAck(adapter);
	auto loss = sample(kArmedGeneration + 1, 1200, ArmState::Armed);
	loss.request_guard_context.transport_connected = false;
	auto bad = request(4, 2);
	bad.flags = 1;
	const auto veto = adapter.processRequest(loss, bad, ingress());
	check(veto.code == IngressCode::PrestateRejected && veto.admission_closed
	      && veto.report_count == 2 && veto.reports[0].kind == IngressReportKind::ExogenousBarrier
	      && veto.reports[1].s1_decision.code == DecisionCode::Disconnected,
	      "trusted_loss_primary_and_closes_ack");
	WireAdapter capacity(config(), {kController});
	establishAndArm(capacity);
	acceptAck(capacity);
	WireAdapterAdmissionTestPeer::seed(capacity, 3, kMaximum - 2, true, 1);
	const auto exhausted = capacity.processRequest(sample(kArmedGeneration + 1, 1200, ArmState::Armed),
			       request(4, 2), ingress());
	check(exhausted.code == IngressCode::CounterExhausted && exhausted.report_count == 1
	      && exhausted.reports[0].report_sequence == kMaximum
	      && exhausted.reports[0].kind == IngressReportKind::Terminal
	      && exhausted.admission_closed && !exhausted.has_prestate_s1_decision,
	      "capacity_no_partial_poll");
	const auto *guard = WireAdapterAdmissionTestPeer::guardAddress(capacity);
	const auto repeated = capacity.processRequest(sample(kArmedGeneration + 1, 1200, ArmState::Armed),
			      request(4, 2), ingress());
	check(repeated.code == IngressCode::CounterExhausted && repeated.report_count == 0
	      && WireAdapterAdmissionTestPeer::guardAddress(capacity) == guard, "terminal_stable");
}

void acknowledgementTimingAndArithmetic()
{
	WireAdapter age_equal(config(), {kController});
	establishAndArm(age_equal);
	const auto equal = age_equal.processAcknowledgement(sample(kArmedGeneration, 1153, ArmState::Armed),
			   session(3, 1, 3, 105, 4), ingress());
	check(equal.code == IngressCode::Completed, "ack_age_equal_allowed");
	WireAdapter age_excess(config(), {kController});
	establishAndArm(age_excess);
	const auto excess = age_excess.processAcknowledgement(sample(kArmedGeneration, 1154, ArmState::Armed),
			    session(3, 1, 3, 105, 4), ingress());
	check(excess.code == IngressCode::AcknowledgementTimingInvalid, "ack_age_plus_one");

	ContractConfig future_config = config();
	future_config.timing.maximum_future_skew_us = 2;
	WireAdapter future_equal(future_config, {kController});
	establishAndArm(future_equal);
	auto transition = future_equal.poll(sample(kArmedGeneration + 1, 1200, ArmState::Armed));
	check(transition.report_count == 2 && transition.snapshot.transition_epoch == 4, "future_fixture_barrier");
	const auto future_pass = future_equal.processAcknowledgement(sample(kArmedGeneration + 1, 1204, ArmState::Armed),
				 session(4, 1, 3, 204, 6), ingress());
	check(future_pass.code == IngressCode::Completed, "ack_future_equal_allowed");
	WireAdapter future_excess(future_config, {kController});
	establishAndArm(future_excess);
	future_excess.poll(sample(kArmedGeneration + 1, 1200, ArmState::Armed));
	const auto future_fail = future_excess.processAcknowledgement(sample(kArmedGeneration + 1, 1203, ArmState::Armed),
				 session(4, 1, 3, 204, 6), ingress());
	check(future_fail.code == IngressCode::AcknowledgementTimingInvalid, "ack_future_plus_one");

	WireAdapter interval(config(), {kController});
	establishAndArm(interval);
	const auto outside = interval.processAcknowledgement(sample(kArmedGeneration, 1110, ArmState::Armed),
			     session(3, 1, 3, 1001, 4), ingress());
	check(outside.code == IngressCode::MapIntervalInvalid, "ack_raw_interval");
	WireAdapter uncertainty(config(), {kController});
	establishAndArm(uncertainty);
	auto large_u = sample(kArmedGeneration + 1, 1200, ArmState::Armed);
	large_u.map.uncertainty_us = 6;
	uncertainty.poll(large_u);
	auto u_receive = large_u;
	u_receive.request_guard_context.validator_receive_time_us = 1210;
	const auto u_fail = uncertainty.processAcknowledgement(u_receive, session(4, 1, 3, 205, 6), ingress());
	check(u_fail.code == IngressCode::AcknowledgementTimingInvalid, "ack_uncertainty_limit");

	WireAdapter overflow(config(), {kController});
	establishAndArm(overflow);
	auto positive = sample(kArmedGeneration + 1, 1200, ArmState::Armed);
	positive.map.validator_minus_producer_us = std::numeric_limits<int64_t>::max();
	positive.map.valid_from_producer_us = UINT64_C(9223372036854775809);
	positive.map.valid_until_producer_us = UINT64_C(9223372036854775809);
	overflow.poll(positive);
	const auto over = overflow.processAcknowledgement(positive,
			  session(4, 1, 3, UINT64_C(9223372036854775809), 6), ingress());
	check(over.code == IngressCode::MapArithmeticInvalid, "ack_positive_offset_overflow");
}

void requestIngressPoliciesAndAdministration()
{
	WireAdapter ingress_adapter(config(), {kController});
	establishAndArm(ingress_adapter);
	acceptAck(ingress_adapter, 0);

	for (unsigned index = 0; index < 4; ++index) {
		auto verdict = ingress();

		if (index == 0) { verdict.mavlink2 = false; }

		if (index == 1) { verdict.parser_authenticated = false; }

		if (index == 2) { verdict.configured_header_channel_source_binding = false; }

		if (index == 3) { verdict.configured_target_binding = false; }

		check(ingress_adapter.processRequest(sample(kArmedGeneration, 1115, ArmState::Armed),
						     request(3, 1), verdict).code == IngressCode::IngressRejected, "request_ingress_negative");
	}

	auto binding = request(2, 1);
	check(ingress_adapter.processRequest(sample(kArmedGeneration, 1115, ArmState::Armed), binding, ingress()).code
	      == IngressCode::BindingMismatch, "request_binding");
	auto enum_bad = request(3, 1);
	enum_bad.wire.frame = 2;
	check(ingress_adapter.processRequest(sample(kArmedGeneration, 1115, ArmState::Armed), enum_bad, ingress()).code
	      == IngressCode::WireEnumInvalid, "request_enum");

	check(submit(ingress_adapter, 1, 120, 220, 1125).code == IngressCode::Completed,
	      "admin_preservation_cache");
	auto admin_bad = session(3, 2, 1);
	admin_bad.flags = 1;
	const auto rejected_admin = ingress_adapter.processSession(sample(kArmedGeneration, 1126, ArmState::Armed),
				    admin_bad, ingress(), nullptr);
	check(rejected_admin.code == AdminCode::MalformedPacket && !rejected_admin.admission_closed,
	      "rejected_admin_preserves_open_ack");
	CanonicalRequest retained{};
	check(ingress_adapter.getEligibleRequest(sample(kArmedGeneration, 1126, ArmState::Armed), retained).code
	      == AdapterCode::EligibleRequest && retained.sequence == 1, "rejected_admin_preserves_cache");

	WireAdapter validate(config(), {kController});
	establishAndArm(validate);
	validate.poll(sample(kArmedGeneration + 1, 1200, ArmState::Disarmed));
	const auto validate_ack = validate.processAcknowledgement(sample(kArmedGeneration + 1, 1210, ArmState::Disarmed),
				  session(4, 1, 3, 205, 6), ingress());
	const auto validated = validate.processRequest(sample(kArmedGeneration + 1, 1215, ArmState::Disarmed),
			       request(4, 1, 210, 230), ingress());
	check(validate_ack.code == IngressCode::Completed && validated.code == IngressCode::S1Rejected
	      && validated.operation_s1_decision.code == DecisionCode::ValidatedDisarmed,
	      "validate_only_no_cache");

	ContractConfig reject_config = config();
	reject_config.disarmed_policy = DisarmedPolicy::Reject;
	WireAdapter reject(reject_config, {kController});
	establishAndArm(reject);
	reject.poll(sample(kArmedGeneration + 1, 1200, ArmState::Disarmed));
	reject.processAcknowledgement(sample(kArmedGeneration + 1, 1210, ArmState::Disarmed),
				      session(4, 1, 3, 205, 6), ingress());
	const auto disarmed = reject.processRequest(sample(kArmedGeneration + 1, 1215, ArmState::Disarmed),
			      request(4, 1, 210, 230), ingress());
	check(disarmed.code == IngressCode::S1Rejected
	      && disarmed.operation_s1_decision.code == DecisionCode::DisarmedRejected, "reject_policy");

	WireAdapter mode(config(), {kController});
	establishAndArm(mode);
	auto not_selected = sample(kArmedGeneration + 1, 1200, ArmState::Armed);
	not_selected.request_guard_context.mode_state = ResearchModeState::NotSelected;
	mode.poll(not_selected);
	auto mode_ack = not_selected;
	mode_ack.request_guard_context.validator_receive_time_us = 1210;
	mode.processAcknowledgement(mode_ack, session(4, 1, 3, 205, 6), ingress());
	auto mode_request = not_selected;
	mode_request.request_guard_context.validator_receive_time_us = 1215;
	const auto rejected_mode = mode.processRequest(mode_request, request(4, 1, 210, 230), ingress());
	check(rejected_mode.code == IngressCode::S1Rejected
	      && rejected_mode.operation_s1_decision.code == DecisionCode::ModeNotSelected,
	      "mode_not_selected_s1_owned");
}

void latchesBarriersAndExactFit()
{
	WireAdapter producer(config(), {kController});
	establishAndArm(producer);
	acceptAck(producer);
	check(submit(producer, 1, 120, 220, 1125).code == IngressCode::Completed, "producer_floor_setup");
	const auto regressed = submit(producer, 2, 110, 210, 1150);
	check(regressed.code == IngressCode::S1Rejected
	      && regressed.operation_s1_decision.code == DecisionCode::ProducerTimeRegressed
	      && regressed.operation_s1_decision.latched && regressed.admission_closed,
	      "producer_regression_latches_and_clears_ack");

	WireAdapter clock(config(), {kController});
	establishAndArm(clock);
	acceptAck(clock);
	const auto time_fault = clock.processRequest(sample(kArmedGeneration, 1109, ArmState::Armed),
				request(3, 1), ingress());
	check(time_fault.code == IngressCode::PrestateRejected && time_fault.admission_closed
	      && time_fault.prestate_s1_decision.code == DecisionCode::ValidatorTimeRegressed,
	      "validator_regression_primary");
	auto disarmed = sample(kArmedGeneration + 1, 1110, ArmState::Disarmed);
	clock.poll(disarmed);
	SupervisorEvent clear{2, kArmedGeneration + 1, 4, kController,
			      SupervisorAction::ClearLatchedFault, true, {kSource, kSession}};
	const auto cleared = clock.clearLatchedFault(disarmed, clear);
	check(cleared.code == AdminCode::Completed && cleared.action_performed
	      && cleared.action_s1_decision.code == DecisionCode::Accepted
	      && cleared.admission_closed, "validator_latch_clear");

	for (unsigned operation = 0; operation < 3; ++operation) {
		WireAdapter barrier(config(), {kController});
		establishAndArm(barrier);
		acceptAck(barrier);
		CanonicalRequest output{};
		const auto changed = sample(kArmedGeneration + 1, 1200, ArmState::Armed);
		AdapterResult result{};

		if (operation == 0) { result = barrier.observeTrustedState(changed); }

		if (operation == 1) { result = barrier.poll(changed); }

		if (operation == 2) { result = barrier.getEligibleRequest(changed, output); }

		check(result.report_count == 2 && result.admission_closed && result.snapshot.transition_epoch == 4,
		      "ordinary_barrier_clears_ack");
	}

	WireAdapter fit(config(), {kController});
	establishAndArm(fit);
	WireAdapterAdmissionTestPeer::seed(fit, 3, kMaximum - 3, true, 0);
	const auto exact = fit.processAcknowledgement(sample(kArmedGeneration + 1, 1200, ArmState::Armed),
			   session(4, 1, 3, 202, kMaximum - 2), ingress());
	check(exact.report_count == 2 && exact.reports[0].report_sequence == kMaximum - 2
	      && exact.reports[1].report_sequence == kMaximum - 1
	      && exact.code == IngressCode::AcknowledgementOrdering, "new_generation_exact_fit");
	WireAdapter one_short(config(), {kController});
	establishAndArm(one_short);
	WireAdapterAdmissionTestPeer::seed(one_short, 3, kMaximum - 2, true, 0);
	const auto terminal = one_short.processAcknowledgement(sample(kArmedGeneration + 1, 1200, ArmState::Armed),
			      session(4, 1, 3, 205, kMaximum - 1), ingress());
	check(terminal.code == IngressCode::CounterExhausted && terminal.report_count == 1
	      && terminal.reports[0].report_sequence == kMaximum, "new_generation_one_short");
	check(one_short.poll(sample(kArmedGeneration + 1, 1200, ArmState::Armed)).code
	      == AdapterCode::CounterExhausted, "terminal_cross_api");
}

void mapExtremaAndStructuralPackets()
{
	WireAdapter negative(config(), {kController});
	establishAndArm(negative);
	auto negative_map = sample(kArmedGeneration + 1, 1100, ArmState::Armed);
	negative_map.map.validator_minus_producer_us = std::numeric_limits<int64_t>::min();
	negative_map.map.valid_from_producer_us = UINT64_C(9223372036854776913);
	negative_map.map.valid_until_producer_us = UINT64_C(9223372036854776913);
	negative.poll(negative_map);
	auto negative_receive = negative_map;
	negative_receive.request_guard_context.validator_receive_time_us = 1110;
	const auto negative_ack = negative.processAcknowledgement(negative_receive,
				  session(4, 1, 3, UINT64_C(9223372036854776913), 6), ingress());
	check(negative_ack.code == IngressCode::Completed
	      && negative_ack.admission.mapped_acknowledgement_time_us == 1105,
	      "ack_int64_min_exact_mapping");

	WireAdapter underflow(config(), {kController});
	establishAndArm(underflow);
	auto underflow_map = sample(kArmedGeneration + 1, 1100, ArmState::Armed);
	underflow_map.map.validator_minus_producer_us = std::numeric_limits<int64_t>::min();
	underflow_map.map.valid_from_producer_us = 0;
	underflow_map.map.valid_until_producer_us = 0;
	underflow.poll(underflow_map);
	const auto underflow_ack = underflow.processAcknowledgement(underflow_map, session(4, 1, 3, 0, 6), ingress());
	check(underflow_ack.code == IngressCode::MapArithmeticInvalid, "ack_int64_min_underflow");

	WireAdapter structural(config(), {kController});
	establishAndArm(structural);
	acceptAck(structural);
	auto changed = sample(kArmedGeneration, 1115, ArmState::Armed);
	changed.map.validator_minus_producer_us++;
	const auto state_fault = structural.processRequest(changed, request(3, 1), ingress());
	check(state_fault.code == IngressCode::TrustedStateFault && state_fault.snapshot.terminal
	      && state_fault.report_count == 1 && state_fault.admission_closed,
	      "same_generation_full_map_mutation_terminal");

	WireAdapter packets(config(), {kController});
	establishAndArm(packets);
	acceptAck(packets);

	for (unsigned mutation = 0; mutation < 5; ++mutation) {
		auto bad = request(3, 1);

		if (mutation == 0) { bad.target_system = 0; }

		if (mutation == 1) { bad.target_component = 0; }

		if (mutation == 2) { bad.flags = 1; }

		if (mutation == 3) { bad.reserved[0] = 1; }

		if (mutation == 4) { bad.wire.source_id = 0; }

		check(packets.processRequest(sample(kArmedGeneration, 1115, ArmState::Armed), bad, ingress()).code
		      == IngressCode::MalformedPacket, "request_each_outer_structural_field");
	}
}

void remainingAckArithmetic()
{
	struct Case {
		int64_t offset;
		TimeUs uncertainty;
		TimeUs raw;
		TimeUs receive;
		IngressCode expected;
	};
	const Case cases[] = {
		{-1000, 2, 2105, 1110, IngressCode::Completed},
		{1000, 5, 110, 1110, IngressCode::Completed},
		{0, 2, 1, 1110, IngressCode::AcknowledgementTimingInvalid},
		{0, 2, kMaximum, 1110, IngressCode::AcknowledgementTimingInvalid},
		{1000, 2, 105, kMaximum, IngressCode::AcknowledgementTimingInvalid},
		{
			std::numeric_limits<int64_t>::max(), 2, UINT64_C(9223372036854775808), 1110,
			IngressCode::AcknowledgementTimingInvalid
		},
		{1000, 2, 103, 1110, IngressCode::Completed}
	};

	for (const auto &test : cases) {
		WireAdapter adapter(config(), {kController});
		establishAndArm(adapter);
		auto changed = sample(kArmedGeneration + 1, 1100, ArmState::Armed);
		changed.map.validator_minus_producer_us = test.offset;
		changed.map.uncertainty_us = test.uncertainty;
		changed.map.valid_from_producer_us = test.raw;
		changed.map.valid_until_producer_us = test.raw;
		adapter.poll(changed);
		changed.request_guard_context.validator_receive_time_us = test.receive;
		const auto result = adapter.processAcknowledgement(changed, session(4, 1, 3, test.raw, 6), ingress());
		check(result.code == test.expected, "ack_checked_arithmetic_and_inclusive_singleton");
		check(result.session.wire_operation_floor == (test.expected == IngressCode::Completed ? 1U : 0U),
		      "ack_arithmetic_failure_never_moves_floor");
	}

	for (unsigned mutation = 0; mutation < 4; ++mutation) {
		WireAdapter adapter(config(), {kController});
		establishAndArm(adapter);
		auto changed = sample(kArmedGeneration + 1, 1200, ArmState::Armed);

		if (mutation == 0) { changed.map.uncertainty_us = 0; }

		if (mutation == 1) { changed.map.valid_from_producer_us = 1001; }

		if (mutation == 2) { changed.map.valid = false; changed.request_guard_context.clock_mapping_valid = false; }

		if (mutation == 3) { changed.request_guard_context.required_state_valid = false; }

		auto bad = session(4, 1, 3, 205, 6);
		bad.flags = 1;
		const auto result = adapter.processAcknowledgement(changed, bad, ingress());
		check(result.code == (mutation < 2 ? IngressCode::TrustedStateFault : IngressCode::PrestateRejected)
		      && result.admission_closed && !result.has_operation_s1_decision, "trusted_fault_preempts_bad_ack");
	}
}

void completePacketMatrices()
{
	using RequestMutation = void (*)(DecodedRequestV1 &);
	struct RequestCase { RequestMutation mutate; IngressCode code; };
	const RequestCase requests[] = {
		{[](DecodedRequestV1 & p) { p.wire.session_id = 0; }, IngressCode::MalformedPacket},
		{[](DecodedRequestV1 & p) { p.wire.controller_instance_id = 0; }, IngressCode::MalformedPacket},
		{[](DecodedRequestV1 & p) { p.wire.transition_epoch = 0; }, IngressCode::MalformedPacket},
		{[](DecodedRequestV1 & p) { p.wire.clock_mapping_epoch = 0; }, IngressCode::MalformedPacket},
		{[](DecodedRequestV1 & p) { p.reserved[1] = 1; }, IngressCode::MalformedPacket},
		{[](DecodedRequestV1 & p) { p.reserved[2] = 1; }, IngressCode::MalformedPacket},
		{[](DecodedRequestV1 & p) { p.reserved[3] = 1; }, IngressCode::MalformedPacket},
		{[](DecodedRequestV1 & p) { ++p.wire.source_id; }, IngressCode::BindingMismatch},
		{[](DecodedRequestV1 & p) { ++p.wire.session_id; }, IngressCode::BindingMismatch},
		{[](DecodedRequestV1 & p) { ++p.wire.controller_instance_id; }, IngressCode::BindingMismatch},
		{[](DecodedRequestV1 & p) { ++p.wire.clock_mapping_epoch; }, IngressCode::BindingMismatch},
		{[](DecodedRequestV1 & p) { p.wire.protocol_version = 0; }, IngressCode::WireVersionInvalid},
		{[](DecodedRequestV1 & p) { p.wire.quantity = 0; }, IngressCode::WireEnumInvalid},
		{[](DecodedRequestV1 & p) { p.wire.reference_point = 2; }, IngressCode::WireEnumInvalid},
		{[](DecodedRequestV1 & p) { p.wire.acceleration_y_frd_m_s2 = std::numeric_limits<float>::quiet_NaN(); }, IngressCode::WireNonFinite},
		{[](DecodedRequestV1 & p) { p.wire.acceleration_z_frd_m_s2 = -std::numeric_limits<float>::infinity(); }, IngressCode::WireNonFinite},
		{[](DecodedRequestV1 & p) { p.wire.producer_expiry_monotonic_us = 1001; }, IngressCode::MapIntervalInvalid}
	};
	WireAdapter adapter(config(), {kController});
	establishAndArm(adapter);
	acceptAck(adapter);
	check(submit(adapter).code == IngressCode::Completed, "matrix_cache_setup");

	for (const auto &test : requests) {
		auto packet = request(3, 2, 111, 131);
		test.mutate(packet);
		const auto result = adapter.processRequest(sample(kArmedGeneration, 1116, ArmState::Armed), packet, ingress());
		check(result.code == test.code && !result.admission_closed && !result.has_operation_s1_decision,
		      "request_matrix_primary_and_ack_preserved");
		CanonicalRequest retained{};
		check(adapter.getEligibleRequest(sample(kArmedGeneration, 1116, ArmState::Armed), retained).code
		      == AdapterCode::EligibleRequest && retained.sequence == 1, "request_matrix_cache_preserved");
	}

	using AckMutation = void (*)(DecodedSessionV1 &);
	struct AckCase { AckMutation mutate; IngressCode code; };
	const AckCase acknowledgements[] = {
		{[](DecodedSessionV1 & p) { p.controller_instance_id = 0; }, IngressCode::MalformedPacket},
		{[](DecodedSessionV1 & p) { p.transition_epoch = 0; }, IngressCode::MalformedPacket},
		{[](DecodedSessionV1 & p) { p.clock_mapping_epoch = 0; }, IngressCode::MalformedPacket},
		{[](DecodedSessionV1 & p) { p.source = 0; }, IngressCode::MalformedPacket},
		{[](DecodedSessionV1 & p) { p.session = 0; }, IngressCode::MalformedPacket},
		{[](DecodedSessionV1 & p) { p.protocol_version = 0; }, IngressCode::MalformedPacket},
		{[](DecodedSessionV1 & p) { p.target_system = 0; }, IngressCode::MalformedPacket},
		{[](DecodedSessionV1 & p) { p.target_component = 0; }, IngressCode::MalformedPacket},
		{[](DecodedSessionV1 & p) { p.flags = 1; }, IngressCode::MalformedPacket},
		{[](DecodedSessionV1 & p) { p.operation = 4; }, IngressCode::MalformedPacket},
		{[](DecodedSessionV1 & p) { p.operation = 1; }, IngressCode::UnsupportedSessionOperation},
		{[](DecodedSessionV1 & p) { ++p.controller_instance_id; }, IngressCode::BindingMismatch},
		{[](DecodedSessionV1 & p) { ++p.source; }, IngressCode::BindingMismatch},
		{[](DecodedSessionV1 & p) { ++p.session; }, IngressCode::BindingMismatch},
		{[](DecodedSessionV1 & p) { ++p.clock_mapping_epoch; }, IngressCode::BindingMismatch}
	};

	for (const auto &test : acknowledgements) {
		auto packet = session(3, 2, 3, 106, 4);
		test.mutate(packet);
		const auto result = adapter.processAcknowledgement(sample(kArmedGeneration, 1116, ArmState::Armed), packet, ingress());
		check(result.code == test.code && !result.admission_closed && result.session.wire_operation_floor == 1,
		      "ack_matrix_primary_and_floor_preserved");
		CanonicalRequest retained{};
		check(adapter.getEligibleRequest(sample(kArmedGeneration, 1116, ArmState::Armed), retained).code
		      == AdapterCode::EligibleRequest && retained.sequence == 1, "ack_matrix_cache_preserved");
	}

	for (unsigned index = 0; index < 4; ++index) {
		auto packet = session(3, 2, 3, 106, 4);
		packet.reserved[index] = 1;
		check(adapter.processAcknowledgement(sample(kArmedGeneration, 1116, ArmState::Armed), packet, ingress()).code
		      == IngressCode::MalformedPacket, "ack_each_reserved_byte");
	}

	const auto grant = establishGrant(2, kArmedGeneration, 3);
	const auto legacy = adapter.processSession(sample(kArmedGeneration, 1116, ArmState::Armed),
			    session(3, 2, 3, 106, 4), ingress(), &grant);
	check(legacy.code == AdminCode::UnsupportedOperation && legacy.correlation.supervisor_event_consumed
	      && legacy.session.supervisor_floor == 2 && legacy.session.wire_operation_floor == 1
	      && !legacy.admission_closed, "legacy_op3_consumes_only_grant_and_preserves_ack");
}

void realS1BoundariesAndReplacement()
{
	using Mutation = void (*)(DecodedRequestV1 &);
	struct Case { Mutation mutate; DecisionCode code; };
	const Case cases[] = {
		{[](DecodedRequestV1 & p) { p.wire.sequence = 1; }, DecisionCode::DuplicateSequence},
		{[](DecodedRequestV1 & p) { p.wire.sequence = 0; }, DecisionCode::OutOfOrderSequence},
		{[](DecodedRequestV1 & p) { p.wire.producer_monotonic_us = 110; }, DecisionCode::ProducerTimeNotIncreasing},
		{[](DecodedRequestV1 & p) { p.wire.producer_expiry_monotonic_us = 120; }, DecisionCode::ValidityOutOfRange},
		{[](DecodedRequestV1 & p) { p.wire.producer_expiry_monotonic_us = 212; }, DecisionCode::ValidityOutOfRange},
		{[](DecodedRequestV1 & p) { p.wire.acceleration_x_frd_m_s2 = 11.F; }, DecisionCode::AxisOutOfBounds},
		{[](DecodedRequestV1 & p) { p.wire.acceleration_x_frd_m_s2 = 8.F; p.wire.acceleration_y_frd_m_s2 = 8.F; p.wire.acceleration_z_frd_m_s2 = 8.F; }, DecisionCode::NormOutOfBounds},
		{[](DecodedRequestV1 & p) { p.wire.producer_monotonic_us = 120; p.wire.producer_expiry_monotonic_us = 140; }, DecisionCode::ProducerTimeInFuture}
	};

	for (const auto &test : cases) {
		WireAdapter adapter(config(), {kController});
		establishAndArm(adapter);
		acceptAck(adapter);
		submit(adapter);
		auto packet = request(3, 2, 111, 131);
		test.mutate(packet);
		const auto result = adapter.processRequest(sample(kArmedGeneration, 1116, ArmState::Armed), packet, ingress());
		check(result.code == IngressCode::S1Rejected && result.has_operation_s1_decision
		      && result.operation_s1_decision.code == test.code && !result.admission_closed,
		      "real_s1_rejection_not_wrapper_substitute");
		CanonicalRequest retained{};
		check(adapter.getEligibleRequest(sample(kArmedGeneration, 1116, ArmState::Armed), retained).code
		      == AdapterCode::EligibleRequest && retained.sequence == 1, "real_s1_rejection_preserves_old_cache");
	}

	WireAdapter expiry(config(), {kController});
	establishAndArm(expiry);
	acceptAck(expiry);
	submit(expiry);
	const auto replacement = submit(expiry, 2, 130, 150, 1135);
	check(replacement.prestate_s1_decision.code == DecisionCode::RequestExpired
	      && replacement.code == IngressCode::Completed && !replacement.admission_closed,
	      "fresh_replacement_after_expiry_same_call");
	CanonicalRequest value{};
	check(expiry.getEligibleRequest(sample(kArmedGeneration, 1135, ArmState::Armed), value).code
	      == AdapterCode::EligibleRequest && value.sequence == 2, "expiry_replacement_retrievable");
	check(value.protocol_version == kWireProtocolVersionV1 && value.source == kSource && value.session == kSession
	      && value.producer_monotonic_us == 130 && value.producer_time_in_validator_domain_us == 1130
	      && value.producer_expiry_monotonic_us == 150 && value.expiry_time_in_validator_domain_us == 1150
	      && value.clock_mapping_uncertainty_us == 2 && value.frame == Frame::Frd
	      && value.quantity == Quantity::InertialAcceleration && value.reference_point == ReferencePoint::Cg
	      && value.acceleration_frd_m_s2(0) == 1.F && value.acceleration_frd_m_s2(1) == 0.F
	      && value.acceleration_frd_m_s2(2) == 0.F, "all_canonical_fields_preserved");
}

void revokeReconnectAndIngressBarriers()
{
	WireAdapter administration(config(), {kController});
	auto grant = establishGrant(1, kGeneration, 1);
	administration.processSession(sample(), session(1, 0, 1), ingress(), &grant);
	check(administration.processAcknowledgement(sample(kGeneration, 1010), session(2, 1, 3, 5, 2), ingress()).code
	      == IngressCode::Completed, "disarmed_ack_before_revoke");
	grant = {2, kGeneration, 2, kController, SupervisorAction::Revoke, true, {kSource, kSession}};
	const auto revoked = administration.processSession(sample(kGeneration, 1011), session(2, 2, 2), ingress(), &grant);
	check(revoked.code == AdminCode::Completed && revoked.admission_closed
	      && revoked.session.gate == SessionGate::Revoked && revoked.snapshot.transition_epoch == 3,
	      "successful_revoke_clears_current_ack");
	const auto cannot_ack = administration.processAcknowledgement(sample(kGeneration, 1015),
				session(3, 3, 3, 11, revoked.snapshot.barrier_report_sequence), ingress());
	check(cannot_ack.code == IngressCode::SessionGateRejected && !cannot_ack.admission.acknowledgement_current,
	      "ack_cannot_reopen_revoked_session");
	auto next_state = sample(kArmedGeneration, 1020);
	next_state.map.session = kSession + 1;
	auto next_packet = session(4, 3, 1);
	next_packet.session = kSession + 1;
	grant = {3, kArmedGeneration, 4, kController, SupervisorAction::Establish, true, {kSource, kSession + 1}};
	const auto established = administration.processSession(next_state, next_packet, ingress(), &grant);
	check(established.code == AdminCode::Completed && established.admission_closed
	      && established.snapshot.transition_epoch == 5 && established.session.gate == SessionGate::Established,
	      "distinct_reestablishment_after_revoke");
	next_state.request_guard_context.validator_receive_time_us = 1030;
	next_packet = session(5, 4, 3, 25, established.snapshot.barrier_report_sequence);
	next_packet.session = kSession + 1;
	check(administration.processAcknowledgement(next_state, next_packet, ingress()).code == IngressCode::Completed,
	      "new_session_requires_and_accepts_fresh_ack");

	WireAdapter reconnect(config(), {kController});
	establishAndArm(reconnect);
	acceptAck(reconnect);
	submit(reconnect);
	auto loss = sample(kArmedGeneration + 1, 1120, ArmState::Armed);
	loss.request_guard_context.transport_connected = false;
	const auto lost = reconnect.processAcknowledgement(loss, session(4, 2, 3, 116, 8), ingress());
	check(lost.code == IngressCode::PrestateRejected && lost.admission_closed, "ack_entry_loss_closes_cache");
	auto recovered = sample(kArmedGeneration + 2, 1130, ArmState::Armed);
	const auto before_ack = reconnect.processRequest(recovered, request(5, 2, 125, 145), ingress());
	check(before_ack.code == IngressCode::AcknowledgementRequired && before_ack.report_count == 2
	      && before_ack.reports[0].kind == IngressReportKind::ExogenousBarrier
	      && zeroCorrelation(before_ack.reports[0].correlation), "request_entry_reconnect_does_not_restore_ack");
	recovered.request_guard_context.validator_receive_time_us = 1140;
	const auto fresh_ack = reconnect.processAcknowledgement(recovered,
			       session(5, 2, 3, 135, before_ack.snapshot.barrier_report_sequence), ingress());
	check(fresh_ack.code == IngressCode::Completed && !fresh_ack.admission_closed, "reconnect_fresh_ack");
	recovered.request_guard_context.validator_receive_time_us = 1145;
	check(reconnect.processRequest(recovered, request(5, 2, 140, 160), ingress()).code == IngressCode::Completed,
	      "reconnect_fresh_request");

	for (unsigned operation = 0; operation < 3; ++operation) {
		WireAdapter adapter(config(), {kController});
		establishAndArm(adapter);
		acceptAck(adapter);
		submit(adapter);
		auto changed = sample(kArmedGeneration + 1, 1120, ArmState::Armed);

		if (operation == 0) {
			const auto result = adapter.processAcknowledgement(changed, session(4, 2, 3, 122, 8), ingress());
			check(result.code == IngressCode::AcknowledgementOrdering && result.admission_closed
			      && result.report_count == 2, "same_call_barrier_cannot_ack");

		} else if (operation == 1) {
			const auto result = adapter.processSession(changed, session(4, 2, 3), ingress(), nullptr);
			check(result.admission_closed && result.report_count == 2
			      && result.code == AdminCode::UnsupportedOperation, "admin_entry_barrier_clears_ack");

		} else {
			changed.request_guard_context.arm_state = ArmState::Disarmed;
			const SupervisorEvent clear{2, kArmedGeneration + 1, 4, kController,
						    SupervisorAction::ClearLatchedFault, true, {kSource, kSession}};
			const auto result = adapter.clearLatchedFault(changed, clear);
			check(result.admission_closed && result.report_count == 2
			      && result.code == AdminCode::ClearNotLatched, "clear_entry_barrier_closes_without_fake_latch");
		}
	}

	WireAdapter rebind(config(), {kController});
	establishAndArm(rebind);
	acceptAck(rebind);
	auto state = sample(kArmedGeneration + 1, 1120, ArmState::Armed);
	state.map.session = kSession + 1;
	auto packet = session(4, 2, 3, 122, 7);
	packet.session = kSession + 1;
	const auto rebound = rebind.processAcknowledgement(state, packet, ingress());
	check(rebound.code == IngressCode::SessionGateRejected && rebound.session.gate == SessionGate::Revoked
	      && rebound.admission_closed, "new_map_session_rebind_revokes_session");
}

void extremeCountersAndFloors()
{
	WireAdapter epoch(config(), {kController});
	establishAndArm(epoch);
	WireAdapterAdmissionTestPeer::seed(epoch, kMaximum - 1, 5, true, 0);
	const auto final_epoch = epoch.processAcknowledgement(sample(kArmedGeneration + 1, 1200, ArmState::Armed),
				 session(kMaximum, 1, 3, 202, 6), ingress());
	check(final_epoch.snapshot.transition_epoch == kMaximum && final_epoch.code == IngressCode::AcknowledgementOrdering,
	      "last_epoch_no_wrap");
	const auto last_ack = epoch.processAcknowledgement(sample(kArmedGeneration + 1, 1210, ArmState::Armed),
			      session(kMaximum, 1, 3, 205, 6), ingress());
	check(last_ack.code == IngressCode::Completed && last_ack.snapshot.transition_epoch == kMaximum,
	      "ack_at_last_epoch_needs_no_advance");
	const auto terminal = epoch.processRequest(sample(kArmedGeneration + 2, 1220, ArmState::Armed),
			      request(kMaximum, 1, 210, 230), ingress());
	check(terminal.code == IngressCode::CounterExhausted && terminal.admission_closed
	      && terminal.snapshot.transition_epoch == kMaximum && !terminal.has_prestate_s1_decision,
	      "epoch_exhaustion_closes_before_poll");

	WireAdapter report(config(), {kController});
	establishAndArm(report);
	WireAdapterAdmissionTestPeer::seed(report, 3, kMaximum - 2, true, 0);
	const auto last_report = report.processAcknowledgement(sample(kArmedGeneration, 1110, ArmState::Armed),
				 session(3, 1, 3, 105, 4), ingress());
	check(last_report.code == IngressCode::Completed && last_report.reports[0].report_sequence == kMaximum - 1,
	      "unchanged_ingress_exact_last_normal_report");
	const auto end = report.processRequest(sample(kArmedGeneration, 1115, ArmState::Armed), request(3, 1), ingress());
	check(end.code == IngressCode::CounterExhausted && end.reports[0].report_sequence == kMaximum
	      && end.admission_closed && !end.has_operation_s1_decision, "last_report_terminal_once");

	WireAdapter floor(config(), {kController});
	establishAndArm(floor);
	const auto ack = floor.processAcknowledgement(sample(kArmedGeneration, 1110, ArmState::Armed),
			 session(3, kMaximum, 3, 105, 4), ingress());
	check(ack.code == IngressCode::Completed && ack.session.wire_operation_floor == kMaximum, "wire_floor_max_accepted");
	check(submit(floor, 0).code == IngressCode::Completed, "request_zero_sequence_independent_of_wire_max");
	const auto replay = floor.processAcknowledgement(sample(kArmedGeneration, 1116, ArmState::Armed),
			    session(3, kMaximum, 3, 106, 4), ingress());
	check(replay.code == IngressCode::WireOperationReplay && !replay.admission_closed
	      && replay.session.wire_operation_floor == kMaximum, "wire_floor_max_never_wraps_or_replaces_ack");
}

void finalIngressOracleEdges()
{
	for (unsigned api = 0; api < 2; ++api) {
		for (unsigned fault = 0; fault < 8; ++fault) {
			WireAdapter adapter(config(), {kController});
			establishAndArm(adapter);
			acceptAck(adapter);
			submit(adapter);
			auto bad = sample(kArmedGeneration, 1116, ArmState::Armed);

			if (fault == 0) { bad.generation = 0; }

			if (fault == 1) { bad.generation = kGeneration; }

			if (fault == 2) { bad.generation = kArmedGeneration + 2; }

			if (fault == 3) { bad.request_guard_context.arm_state = static_cast<ArmState>(99); }

			if (fault == 4) { bad.request_guard_context.mode_state = static_cast<ResearchModeState>(99); }

			if (fault == 5) { bad.observer_continuous = false; }

			if (fault == 6) { bad.request_guard_context.clock_mapping_valid = false; }

			if (fault == 7) { ++bad.map.controller_instance_id; }

			const auto result = api == 0 ? adapter.processAcknowledgement(bad, session(3, 2, 3, 106, 4), ingress())
					    : adapter.processRequest(bad, request(3, 2, 111, 131), ingress());
			check(result.code == IngressCode::TrustedStateFault && result.report_count == 1
			      && result.snapshot.terminal && result.snapshot.state_generation == kArmedGeneration
			      && result.snapshot.transition_epoch == 3 && result.admission_closed
			      && !result.has_prestate_s1_decision && !result.has_operation_s1_decision,
			      "ingress_structural_matrix_preserves_last_valid_state");
			const auto repeat = adapter.processRequest(sample(kArmedGeneration, 1116, ArmState::Armed), request(3, 2), ingress());
			check(repeat.code == IngressCode::TrustedStateFault && repeat.report_count == 0, "structural_repeat_has_no_report");
			const auto admin = adapter.processSession(sample(kArmedGeneration, 1116, ArmState::Armed), session(3, 2, 3), ingress(), nullptr);
			check(admin.code == AdminCode::TrustedStateFault && admin.report_count == 0, "structural_terminal_admin_zero_reports");
			CanonicalRequest sentinel{};
			sentinel.sequence = 123;
			const auto before = bytes(sentinel);
			const auto getter = adapter.getEligibleRequest(sample(kArmedGeneration, 1116, ArmState::Armed), sentinel);
			check(getter.code == AdapterCode::TrustedStateFault && getter.report_count == 0 && bytes(sentinel) == before,
			      "structural_terminal_getter_unchanged");
		}

		WireAdapter first(config(), {kController});
		auto invalid_first = sample();
		invalid_first.generation = 0;
		const auto first_result = api == 0 ? first.processAcknowledgement(invalid_first, session(1, 0, 3), ingress())
					  : first.processRequest(invalid_first, request(1, 0), ingress());
		check(first_result.code == IngressCode::TrustedStateFault && !first_result.snapshot.initialized
		      && first_result.snapshot.transition_epoch == 0 && first_result.report_count == 1
		      && first_result.reports[0].report_sequence == 1 && !first_result.has_prestate_s1_decision,
		      "invalid_first_ingress_never_fabricates_epoch");
	}

	WireAdapter ordering(config(), {kController});
	establishAndArm(ordering);
	acceptAck(ordering);
	check(submit(ordering, 1, 105, 130).code == IngressCode::AcknowledgementOrdering, "request_raw_equal_ack");
	auto bad_frame = request(3, 1);
	bad_frame.wire.frame = 0;
	check(ordering.processRequest(sample(kArmedGeneration, 1115, ArmState::Armed), bad_frame, ingress()).code
	      == IngressCode::WireEnumInvalid, "request_frame_enum_zero");
	const auto expired = submit(ordering, 1, 110, 130, 1128);
	check(expired.code == IngressCode::S1Rejected && expired.operation_s1_decision.code == DecisionCode::RequestExpired,
	      "request_expiry_exact_receive");

	for (unsigned short_capacity = 0; short_capacity < 2; ++short_capacity) {
		WireAdapter capacity(config(), {kController});
		establishAndArm(capacity);
		WireAdapterAdmissionTestPeer::seed(capacity, 3, kMaximum - 3 + short_capacity, true, 0);
		auto malformed = request(4, 1, 210, 230);
		malformed.flags = 1;
		const auto result = capacity.processRequest(sample(kArmedGeneration + 1, 1200, ArmState::Armed), malformed, ingress());
		check(result.code == (short_capacity == 0 ? IngressCode::MalformedPacket : IngressCode::CounterExhausted)
		      && result.report_count == (short_capacity == 0 ? 2 : 1)
		      && result.snapshot.last_report_sequence == (short_capacity == 0 ? kMaximum - 1 : kMaximum),
		      "later_rejected_packet_obeys_worst_case_capacity");
	}
}

} // namespace

int main()
{
	goldenAndAccess();
	packetNegatives();
	requestCacheAndFailures();
	closureAndCapacity();
	acknowledgementTimingAndArithmetic();
	requestIngressPoliciesAndAdministration();
	latchesBarriersAndExactFit();
	mapExtremaAndStructuralPackets();
	remainingAckArithmetic();
	completePacketMatrices();
	realS1BoundariesAndReplacement();
	revokeReconnectAndIngressBarriers();
	extremeCountersAndFloors();
	finalIngressOracleEdges();
	std::cout << checks << " checks; " << failures << " failures\n";
	return failures == 0 ? 0 : 1;
}
