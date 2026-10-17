#include "WireStatus.hpp"

#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

using namespace research_acceleration;

namespace
{
// Injected test identities/times only. No transport, firmware, or output owner.
constexpr uint64_t kController = 11;
constexpr SourceId kSource = 21;
constexpr SessionId kSession = 31;
constexpr uint64_t kMap = 41;
constexpr TimeUs kOffset = 1000000;
constexpr TimeUs kUncertainty = 4000;
constexpr TimeUs kValidity = 80000;
constexpr TimeUs kHardAge = 60000;
constexpr TimeUs kFirstCreation = 200000;
constexpr TimeUs kObserverPeriod = 1000;
constexpr unsigned kStreamCount = 50;
constexpr uint64_t kSentinel = 999999;
unsigned stream_requests = 0;
unsigned boundary_cases = 0;

void require(bool condition, const char *label)
{
	if (!condition) {
		std::cerr << "FAILED: " << label << '\n';
		std::exit(1);
	}
}

ContractConfig configuration(TimeUs hard_age = kHardAge)
{
	// Bounds are synthetic scaffolding inherited from unit tests, not flight limits.
	return {kWireProtocolVersionV1, kSource, DisarmedPolicy::ValidateOnly,
		{Vec3f(-10.F, -10.F, -10.F), Vec3f(10.F, 10.F, 10.F), 12.F},
		{kValidity, kValidity, hard_age, 8000, kUncertainty}, 0.01F};
}

TrustedStateSample state(TimeUs now, bool armed = true)
{
	return {armed ? 52U : 51U, true,
		{now, armed ? ArmState::Armed : ArmState::Disarmed, ResearchModeState::Selected, true, true, true},
		{true, kSource, kSession, kController, kMap, kOffset, kUncertainty, 0, 10000000}};
}

SessionIngressVerdict fixtureIngress() { return {true, true, true, true}; }

DecodedSessionV1 session(uint64_t epoch, uint64_t sequence, uint8_t operation,
			 TimeUs raw = 0, uint64_t status = 0)
{
	return {kController, epoch, kMap, kSource, kSession, sequence, raw, status,
		kWireProtocolVersionV1, 1, 1, operation, 0, {0, 0, 0, 0}};
}

DecodedRequestV1 request(uint64_t sequence, TimeUs created)
{
	return {{
			kSource, kSession, sequence, created, created + kValidity, kController, 3, kMap,
			1.F, -2.F, 3.F, kWireProtocolVersionV1, kWireFrameFrdV1,
			kWireQuantityInertialAccelerationV1, kWireReferencePointCgV1
		}, 1, 1, 0, {0, 0, 0, 0}};
}

struct Fixture {
	WireAdapter adapter;

	explicit Fixture(TimeUs hard_age = kHardAge) : adapter(configuration(hard_age), {kController})
	{
		const SupervisorEvent grant{1, 51, 1, kController, SupervisorAction::Establish, true, {kSource, kSession}};
		const auto established = adapter.processSession(state(kOffset, false), session(1, 0, 1), fixtureIngress(), &grant);
		require(established.code == AdminCode::Completed, "fixture establish");
		// Injecting ArmState::Armed is NOT an arming command or live state observation.
		const auto barrier = adapter.poll(state(kOffset + 100000));
		require(barrier.snapshot.barrier_report_sequence == 4, "fixture barrier");
		const auto ack = adapter.processAcknowledgement(state(kOffset + 118000), session(3, 1, 3, 110000, 4), fixtureIngress());
		require(ack.code == IngressCode::Completed && !ack.admission_closed, "fixture acknowledgement");
	}
};

template<typename Report>
std::string payload(const Report &report)
{
	WireStatusV1 status{};
	require(buildStatusV1(report, {1, 1, StatusDelivery::Directed}, status) == StatusBuildCode::Ok, "real report encodes");
	uint8_t bytes[106] {};
	require(encodeStatusV1Payload(status, bytes) == StatusEncodeCode::Ok, "real status payload");
	std::ostringstream hex;
	hex << std::hex << std::setfill('0');

	for (uint8_t byte : bytes) { hex << std::setw(2) << unsigned(byte); }

	return hex.str();
}

void canonicalMatches(const CanonicalRequest &value, uint64_t sequence, TimeUs created)
{
	require(value.source == kSource && value.session == kSession && value.sequence == sequence
		&& value.producer_monotonic_us == created && value.producer_expiry_monotonic_us == created + kValidity
		&& value.producer_time_in_validator_domain_us == created + kOffset
		&& value.expiry_time_in_validator_domain_us == created + kOffset + kValidity
		&& value.clock_mapping_uncertainty_us == kUncertainty && value.protocol_version == kWireProtocolVersionV1
		&& value.frame == Frame::Frd && value.quantity == Quantity::InertialAcceleration
		&& value.reference_point == ReferencePoint::Cg && value.acceleration_frd_m_s2(0) == 1.F
		&& value.acceleration_frd_m_s2(1) == -2.F && value.acceleration_frd_m_s2(2) == 3.F,
		"all canonical fields preserved");
}

void timed(Fixture &fixture, const char *name, uint64_t sequence, TimeUs created,
	   TimeUs receive_delay, TimeUs queue_delay, TimeUs access_delay,
	   DecisionCode admission_code, bool eligible, TimeUs hard_age = kHardAge)
{
	const TimeUs received = created + kOffset + receive_delay;
	const TimeUs admitted = received + queue_delay;
	const TimeUs accessed = admitted + access_delay;
	const auto admitted_result = fixture.adapter.processRequest(state(admitted), request(sequence, created), fixtureIngress());
	require(admitted_result.report_count == 1 && admitted_result.has_operation_s1_decision
		&& admitted_result.operation_s1_decision.code == admission_code, "admission oracle");
	CanonicalRequest value{};
	value.sequence = kSentinel;
	const auto access = fixture.adapter.getEligibleRequest(state(accessed), value);
	require(access.report_count == 1 && (access.code == AdapterCode::EligibleRequest) == eligible, "access oracle");

	if (eligible) { canonicalMatches(value, sequence, created); }

	else { require(value.sequence == kSentinel, "rejected access leaves output unchanged"); }

	// Receipt/return are deterministic injected events, not measured network timestamps.
	std::cout << "{\"case\":\"" << name << "\",\"sequence\":" << sequence
		  << ",\"provenance\":\"injected-us-v1\",\"created_us\":" << created
		  << ",\"sent_us\":" << created << ",\"received_us\":" << received
		  << ",\"admitted_us\":" << admitted << ",\"accessed_us\":" << accessed
		  << ",\"status_emitted_us\":" << admitted << ",\"status_returned_us\":" << admitted
		  << ",\"offset_us\":" << kOffset << ",\"uncertainty_us\":" << kUncertainty
		  << ",\"map_from_us\":0,\"map_until_us\":10000000,\"validity_us\":" << kValidity
		  << ",\"hard_age_us\":" << hard_age << ",\"eligible\":" << (eligible ? "true" : "false")
		  << ",\"request_status\":\"" << payload(admitted_result.reports[0])
		  << "\",\"access_status\":\"" << payload(access.reports[0]) << "\"}\n";
}

void streams()
{
	for (unsigned jitter = 0; jitter < 2; ++jitter) {
		Fixture fixture;
		TimeUs created = kFirstCreation;

		for (unsigned index = 0; index < kStreamCount; ++index) {
			const TimeUs period = jitter && index % 2 ? 24000 : 20000;
			timed(fixture, jitter ? "jitter" : "periodic", index + 1, created, 12000, 4000, 0, DecisionCode::Accepted, true);
			++stream_requests;

			for (TimeUs tick = kObserverPeriod; tick < period; tick += kObserverPeriod) {
				CanonicalRequest current{};
				const auto access = fixture.adapter.getEligibleRequest(state(created + kOffset + 16000 + tick), current);
				require(access.code == AdapterCode::EligibleRequest, "continuous nominal eligibility at 1 ms polls");
				canonicalMatches(current, index + 1, created);
			}

			created += period;
		}
	}
}

void timingBoundaries()
{
	struct Case { const char *name; TimeUs receive; TimeUs queue; TimeUs access; DecisionCode decision; bool eligible; };
	const Case cases[] {
		{"receive_below", 11999, 4000, 0, DecisionCode::Accepted, true},
		{"receive_at", 12000, 4000, 0, DecisionCode::Accepted, true},
		{"receive_above", 12001, 4000, 0, DecisionCode::Accepted, true},
		{"queue_below", 12000, 3999, 0, DecisionCode::Accepted, true},
		{"queue_at", 12000, 4000, 0, DecisionCode::Accepted, true},
		{"queue_above", 12000, 4001, 0, DecisionCode::Accepted, true},
		{"age_at", 52000, 4000, 0, DecisionCode::Accepted, true},
		{"age_above", 52001, 4000, 0, DecisionCode::RequestTooOld, false},
		{"retained_at", 12000, 4000, 40000, DecisionCode::Accepted, true},
		{"retained_above", 12000, 4000, 40001, DecisionCode::Accepted, false},
		// Delayed observer: the accessor itself enforces time, without a previous poll.
		{"delayed_access", 12000, 4000, 60000, DecisionCode::Accepted, false},
	};

	for (const auto &test : cases) {
		Fixture fixture;
		timed(fixture, test.name, 1, kFirstCreation, test.receive, test.queue, test.access, test.decision, test.eligible);
		++boundary_cases;
	}

	// Not the approved profile: isolate exclusive expiry from the earlier age check.
	for (unsigned boundary = 0; boundary < 2; ++boundary) {
		constexpr TimeUs isolation_age = 100000;
		Fixture fixture(isolation_age);
		timed(fixture, boundary ? "expiry_at_isolation" : "expiry_before_isolation", 1, kFirstCreation,
		      12000, 4000, boundary ? 60000 : 59999, DecisionCode::Accepted, boundary == 0, isolation_age);
		++boundary_cases;
	}
}

void cacheFaults()
{
	unsigned cache_fault_cases = 0;

	for (unsigned fault = 0; fault < 9; ++fault) {
		Fixture fixture;
		const auto original = request(2, kFirstCreation);
		const auto accepted = fixture.adapter.processRequest(state(1216000), original, fixtureIngress());
		require(accepted.code == IngressCode::Completed, "fault fixture admission");
		auto bad = request(3, kFirstCreation + 1000);
		auto current_state = state(1217000);

		if (fault == 0) { bad = original; } // duplicate

		if (fault == 1) { bad.wire.sequence = 1; } // reordered sequence

		if (fault == 2) { ++bad.wire.source_id; } // foreign payload

		if (fault == 3) { bad.flags = 1; } // malformed

		if (fault == 4) { bad.wire.producer_expiry_monotonic_us = 10000001; } // map lacks lifetime

		if (fault == 5) { ++current_state.generation; current_state.request_guard_context.required_state_valid = false; }

		if (fault == 6) { ++current_state.generation; current_state.request_guard_context.transport_connected = false; }

		if (fault == 7) { ++current_state.generation; ++current_state.map.map_epoch; }

		if (fault == 8) { ++current_state.generation; current_state.map.valid = false; current_state.request_guard_context.clock_mapping_valid = false; }

		const auto rejected = fixture.adapter.processRequest(current_state, bad, fixtureIngress());
		require(rejected.code != IngressCode::Completed, "fault is rejected");
		CanonicalRequest value{};
		const auto accessed = fixture.adapter.getEligibleRequest(current_state, value);
		require((accessed.code == AdapterCode::EligibleRequest) == (fault < 5), "prior cache preservation depends on trusted fault");

		if (fault < 5) { canonicalMatches(value, 2, kFirstCreation); }

		++cache_fault_cases;
	}

	// Heartbeat has no adapter API. Model it as an independent event, never a request.
	Fixture fixture;
	fixture.adapter.processRequest(state(1216000), request(1, kFirstCreation), fixtureIngress());
	unsigned heartbeats = 0;
	unsigned heartbeat_accesses = 0;

	for (TimeUs now = 1216000; now <= 3216000; now += kObserverPeriod) {
		if ((now - 1216000) % 1000000 == 0) { ++heartbeats; }

		CanonicalRequest value{};
		const auto accessed = fixture.adapter.getEligibleRequest(state(now), value);
		require((accessed.code == AdapterCode::EligibleRequest) == (now <= 1256000), "live heartbeat cannot refresh payload");
		++heartbeat_accesses;
	}

	require(heartbeats == 3, "injected 1 Hz heartbeat remains live");
	std::cout << "{\"summary\":\"injected-only\",\"stream_requests\":" << stream_requests
		  << ",\"boundary_cases\":" << boundary_cases << ",\"cache_fault_cases\":" << cache_fault_cases
		  << ",\"heartbeat_events\":" << heartbeats << ",\"heartbeat_accesses\":" << heartbeat_accesses << "}\n";
}
} // namespace

int main()
{
	streams();
	timingBoundaries();
	cacheFaults();
	return 0;
}
