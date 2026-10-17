#include "WireStatus.hpp"

#include <array>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <string>

using namespace research_acceleration;

namespace
{
int checks = 0;
int failures = 0;

void check(bool value, const std::string &name)
{
	++checks;

	if (!value) { ++failures; std::cerr << "FAILED: " << name << '\n'; }
}

ReportEvidence evidence()
{
	return {true, 11, true, 41, ArmState::Armed, ResearchModeState::Selected,
		SessionGate::Established, true, true, 21, 31, true, true, true, true,
		true, true, 1, true, false, DecisionCode::Accepted, 1, true, true, 7,
		StatusDecisionOrigin::S1, 0};
}

IngressReport acceptedRequest()
{
	IngressReport report{};
	report.report_sequence = 7;
	report.transition_epoch = 3;
	report.validator_time_us = 1115;
	report.kind = IngressReportKind::Operation;
	report.code = IngressCode::Completed;
	report.has_s1_decision = true;
	report.s1_decision = {DecisionCode::Accepted, true, true, false,
			      DecisionCode::Accepted, true, 7, 1
			     };
	report.correlation = {IngressPacketKind::Request, 21, 31, 11, 3, 41, 1, 0};
	report.evidence = evidence();
	return report;
}

void mapAllS1()
{
	for (unsigned raw = 0; raw < 37; ++raw) {
		uint16_t mapped = 0;
		const auto code = static_cast<DecisionCode>(raw);
		check(mapDecisionCodeV1(code, mapped) && mapped == raw + 1, "all_s1_decisions");
	}

	uint16_t sentinel = 99;
	check(!mapDecisionCodeV1(static_cast<DecisionCode>(99), sentinel) && sentinel == 99,
	      "unknown_s1_does_not_write");
}

constexpr uint64_t kController = 11;
constexpr SourceId kSource = 21;
constexpr SessionId kSession = 31;
constexpr uint64_t kMapEpoch = 41;

ContractConfig historyConfig()
{
	return {kWireProtocolVersionV1, kSource, DisarmedPolicy::ValidateOnly,
		{Vec3f(-10.F, -10.F, -10.F), Vec3f(10.F, 10.F, 10.F), 12.F}, {10, 100, 50, 10, 5}, 0.01F};
}

TrustedStateSample historySample(uint64_t generation, TimeUs time, ArmState arm)
{
	TrustedStateSample result{};
	result.generation = generation;
	result.observer_continuous = true;
	result.request_guard_context = {time, arm, ResearchModeState::Selected, true, true, true};
	result.map = {true, kSource, kSession, kController, kMapEpoch, 1000, 2, 0, 1000};
	return result;
}

SessionIngressVerdict historyIngress() { return {true, true, true, true}; }

DecodedSessionV1 historySession(uint64_t epoch, uint64_t sequence, uint8_t operation, TimeUs raw = 0,
				uint64_t status = 0)
{
	return {kController, epoch, kMapEpoch, kSource, kSession, sequence, raw, status, kWireProtocolVersionV1,
		1, 1, operation, 0, {0, 0, 0, 0}};
}

DecodedRequestV1 historyRequest()
{
	WireRequestV1 wire{kSource, kSession, 1, 110, 130, kController, 3, kMapEpoch, 1.F, 0.F, 0.F,
			   kWireProtocolVersionV1, kWireFrameFrdV1, kWireQuantityInertialAccelerationV1,
			   kWireReferencePointCgV1};
	return {wire, 1, 1, 0, {0, 0, 0, 0}};
}

struct History {
	AdminResult establish;
	AdapterResult arm;
	IngressResult acknowledgement;
	IngressResult request;
	AdapterResult access;
	AdapterResult expiry;
};

History realHistory()
{
	WireAdapter adapter(historyConfig(), {kController});
	const SupervisorEvent grant{1, 51, 1, kController, SupervisorAction::Establish, true, {kSource, kSession}};
	History result{};
	result.establish = adapter.processSession(historySample(51, 1000, ArmState::Disarmed), historySession(1, 0, 1),
			   historyIngress(), &grant);
	result.arm = adapter.poll(historySample(52, 1100, ArmState::Armed));
	result.acknowledgement = adapter.processAcknowledgement(historySample(52, 1110, ArmState::Armed),
				 historySession(3, 1, 3, 105, 4), historyIngress());
	result.request = adapter.processRequest(historySample(52, 1115, ArmState::Armed), historyRequest(), historyIngress());
	CanonicalRequest request{};
	result.access = adapter.getEligibleRequest(historySample(52, 1116, ArmState::Armed), request);
	result.expiry = adapter.getEligibleRequest(historySample(52, 1128, ArmState::Armed), request);
	return result;
}

template<typename Report>
void emit(const char *name, const Report &report)
{
	WireStatusV1 status{};
	const StatusRecipient directed{1, 1, StatusDelivery::Directed};

	if (buildStatusV1(report, directed, status) != StatusBuildCode::Ok) { std::exit(1); }

	uint8_t payload[106] {};

	if (encodeStatusV1Payload(status, payload) != StatusEncodeCode::Ok) { std::exit(1); }

	std::cout << name << ' ';

	for (uint8_t byte : payload) { std::cout << std::hex << std::setw(2) << std::setfill('0') << unsigned(byte); }

	std::cout << '\n';
}

void realHistoricalReports()
{
	const History history = realHistory();
	check(history.establish.report_count == 3 && history.arm.report_count == 2
	      && history.acknowledgement.report_count == 1 && history.request.report_count == 1
	      && history.access.report_count == 1 && history.expiry.report_count == 1, "history_report_counts");
	check(history.establish.reports[0].evidence.session_gate == SessionGate::NoTrustedSession
	      && !history.establish.reports[0].evidence.acknowledgement_current
	      && history.establish.reports[1].evidence.session_gate == SessionGate::Established,
	      "history_barriers_are_not_rewritten");
	check(history.request.reports[0].evidence.has_last_accepted_sequence
	      && history.request.reports[0].evidence.last_accepted_sequence == 1
	      && history.expiry.reports[0].evidence.has_last_accepted_sequence
	      && history.expiry.reports[0].evidence.last_accepted_sequence == 1,
	      "history_request_sequence_not_rewritten");
	const StatusRecipient directed{1, 1, StatusDelivery::Directed};
	WireStatusV1 status{};
	check(buildStatusV1(history.establish.reports[0], directed, status) == StatusBuildCode::Ok
	      && buildStatusV1(history.establish.reports[1], directed, status) == StatusBuildCode::Ok
	      && buildStatusV1(history.establish.reports[2], directed, status) == StatusBuildCode::Ok
	      && buildStatusV1(history.arm.reports[0], directed, status) == StatusBuildCode::Ok
	      && buildStatusV1(history.arm.reports[1], directed, status) == StatusBuildCode::Ok
	      && buildStatusV1(history.acknowledgement.reports[0], directed, status) == StatusBuildCode::Ok
	      && buildStatusV1(history.request.reports[0], directed, status) == StatusBuildCode::Ok
	      && buildStatusV1(history.access.reports[0], directed, status) == StatusBuildCode::Ok
	      && buildStatusV1(history.expiry.reports[0], directed, status) == StatusBuildCode::Ok,
	      "all_real_historical_reports_build");
}

void goldenAndFailures()
{
	const StatusRecipient directed{1, 1, StatusDelivery::Directed};
	WireStatusV1 status{};
	const auto report = acceptedRequest();
	check(buildStatusV1(report, directed, status) == StatusBuildCode::Ok,
	      "accepted_request_build");
	check(status.controller_instance_id == 11 && status.transition_epoch == 3
	      && status.clock_mapping_epoch == 41 && status.source_id == 21
	      && status.session_id == 31 && status.correlation_sequence == 1
	      && status.last_accepted_sequence == 1 && status.validator_time_us == 1115
	      && status.status_sequence == 7 && status.rejection_count == 1
	      && status.conservative_age_us == 7 && status.decision_code == 1
	      && status.status_flags == 59 && status.status_class == 1,
	      "accepted_request_fields");
	uint8_t payload[106] {};
	check(encodeStatusV1Payload(status, payload) == StatusEncodeCode::Ok,
	      "accepted_request_encode");
	const WireStatusV1 sentinel = status;
	const StatusRecipient invalid{0, 1, StatusDelivery::Directed};
	check(buildStatusV1(report, invalid, status) == StatusBuildCode::InvalidRecipient
	      && status.controller_instance_id == sentinel.controller_instance_id
	      && status.status_sequence == sentinel.status_sequence && status.status_flags == sentinel.status_flags,
	      "recipient_failure_unchanged");
	auto missing = report;
	missing.evidence.captured = false;
	check(buildStatusV1(missing, directed, status) == StatusBuildCode::MissingEvidence
	      && status.controller_instance_id == sentinel.controller_instance_id
	      && status.status_sequence == sentinel.status_sequence && status.status_flags == sentinel.status_flags,
	      "evidence_failure_unchanged");
}

void emitVectors()
{
	const History history = realHistory();
	emit("establish_exogenous_barrier", history.establish.reports[0]);
	emit("establish_administrative_barrier", history.establish.reports[1]);
	emit("establish_operation", history.establish.reports[2]);
	emit("arm_barrier", history.arm.reports[0]);
	emit("arm_poll", history.arm.reports[1]);
	emit("acknowledgement", history.acknowledgement.reports[0]);
	emit("accepted_request", history.request.reports[0]);
	emit("eligible_access", history.access.reports[0]);
	emit("expiry_access", history.expiry.reports[0]);
}
} // namespace

int main(int argc, char **argv)
{
	if (argc == 2 && std::string(argv[1]) == "--emit-vectors") { emitVectors(); return 0; }

	mapAllS1();
	goldenAndFailures();
	realHistoricalReports();
	std::cout << checks << " checks; " << failures << " failures\n";
	return failures == 0 ? 0 : 1;
}
