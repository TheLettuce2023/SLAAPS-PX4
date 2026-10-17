#include "WireStatus.hpp"

#include <array>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

using namespace research_acceleration;

namespace
{
// Synthetic identities, timing and inherited unit-test bounds only.
constexpr uint64_t kController = 11;
constexpr SourceId kSource = 21;
constexpr SessionId kSession = 31;
constexpr uint64_t kMap = 41;
constexpr TimeUs kOffset = 1000000;
constexpr TimeUs kUncertainty = 4000;
constexpr TimeUs kValidity = 80000;
constexpr TimeUs kFirstCreation = 200000;
constexpr TimeUs kPairStart = 1216000;
constexpr TimeUs kLateStart = 1256000;
constexpr TimeUs kObserverPeriod = 1000;
constexpr TimeUs kDiagnosticPeriod = 1000000;
constexpr TimeUs kNoFreshThreshold = 40000;
constexpr TimeUs kPairEnd = 2007000;
constexpr TimeUs kPairLastEligible = 80000;
constexpr uint64_t kSentinel = 999999;

void require(bool condition, const char *label)
{
	if (!condition) {
		std::cerr << "FAILED: " << label << '\n';
		std::exit(1);
	}
}

ContractConfig configuration()
{
	return {kWireProtocolVersionV1, kSource, DisarmedPolicy::ValidateOnly,
		{Vec3f(-10.F, -10.F, -10.F), Vec3f(10.F, 10.F, 10.F), 12.F},
		{kValidity, kValidity, 60000, 8000, kUncertainty}, 0.01F};
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

struct Status {
	uint64_t id;
	std::string payload;
};

template<typename Report>
Status encode(const Report &report, bool broadcast = false)
{
	WireStatusV1 status{};
	const StatusRecipient recipient = broadcast ? StatusRecipient{0, 0, StatusDelivery::OperatorBroadcastSnapshot}
					  : StatusRecipient{1, 1, StatusDelivery::Directed};
	require(buildStatusV1(report, recipient, status) == StatusBuildCode::Ok, "real report encodes");
	uint8_t bytes[106] {};
	require(encodeStatusV1Payload(status, bytes) == StatusEncodeCode::Ok, "real status payload");
	std::ostringstream hex;
	hex << std::hex << std::setfill('0');

	for (uint8_t byte : bytes) { hex << std::setw(2) << unsigned(byte); }

	return {status.status_sequence, hex.str()};
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
		"all canonical fields preserved despite status delivery");
}

class Fixture
{
public:
	Fixture(const char *name, bool faults, bool late) : name_(name), faults_(faults), late_(late),
		start_(late ? kLateStart : kPairStart), adapter_(configuration(), {kController})
	{
		const SupervisorEvent grant{1, 51, 1, kController, SupervisorAction::Establish, true, {kSource, kSession}};
		const auto established = adapter_.processSession(state(kOffset, false), session(1, 0, 1), fixtureIngress(), &grant);
		require(established.code == AdminCode::Completed, "fixture establish");
		// This injected state is not an arming command or live state observation.
		const auto barrier = adapter_.poll(state(kOffset + 100000));
		require(barrier.snapshot.barrier_report_sequence == 4, "fixture barrier");
		const auto ack = adapter_.processAcknowledgement(state(kOffset + 118000), session(3, 1, 3, 110000, 4),
				 fixtureIngress());
		require(ack.code == IngressCode::Completed && !ack.admission_closed, "fixture acknowledgement");
	}

	void run()
	{
		const TimeUs end = late_ ? kNoFreshThreshold : kPairEnd;

		for (TimeUs offset = 0; offset <= end; offset += kObserverPeriod) {
			tick(offset);

			// First ineligible microsecond, while retaining all ordinary 1 ms observations.
			if ((late_ && offset == 0) || (!late_ && offset == kPairLastEligible)) { tick(offset + 1); }
		}

		require(depth_ == 0 && requests_ + snapshots_ == dropped_ + returned_, "all generated statuses accounted for");
		require(enqueued_ + backpressure_ == requests_ + snapshots_, "every generation has a queue disposition");
		prefix("summary", start_ + end);
		std::cout << ",\"requests\":" << requests_ << ",\"accesses\":" << accesses_
			  << ",\"heartbeats\":" << heartbeats_ << ",\"snapshots\":" << snapshots_
			  << ",\"enqueued\":" << enqueued_ << ",\"dropped\":" << dropped_
			  << ",\"returned\":" << returned_ << ",\"pending\":" << depth_ << "}\n";
	}

private:
	void prefix(const char *event, TimeUs now) const
	{
		std::cout << "{\"case\":\"" << name_ << "\",\"event\":\"" << event << "\",\"t_us\":" << now
			  << ",\"provenance\":\"injected-us-v1\"";
	}

	void enqueue(TimeUs now, const Status &status)
	{
		require(depth_ <= queue_.size(), "queue depth remains bounded");

		// Fixed-capacity drop-newest is an injected TEST policy, not deployed transport policy.
		if (depth_ == queue_.size()) {
			prefix("drop", now);
			std::cout << ",\"status_id\":" << status.id << ",\"depth\":" << depth_
				  << ",\"reason\":\"backpressure\",\"returned_us\":null}\n";
			++dropped_;
			++backpressure_;
			return;
		}

		queue_[depth_++] = status;
		++enqueued_;
		prefix("enqueue", now);
		std::cout << ",\"status_id\":" << status.id << ",\"depth\":" << depth_ << "}\n";
	}

	void deliver(TimeUs now, unsigned index, bool drop = false)
	{
		require(depth_ <= queue_.size() && index < depth_, "scheduled delivery has bounded queued status");
		const Status status = queue_[index];
		require(status.payload.size() == 212, "queued immutable complete status");

		for (unsigned next = 1; next < queue_.size(); ++next) {
			if (next > index && next < depth_) { queue_[next - 1] = queue_[next]; }
		}

		--depth_;
		prefix(drop ? "drop" : "return", now);
		std::cout << ",\"status_id\":" << status.id << ",\"depth\":" << depth_;

		if (drop) {
			++dropped_;
			std::cout << ",\"reason\":\"injected_loss\",\"returned_us\":null}\n";

		} else {
			++returned_;
			std::cout << ",\"returned_us\":" << now << ",\"payload\":\"" << status.payload << "\"}\n";
		}
	}

	void admit(TimeUs now, uint64_t sequence, TimeUs created, bool duplicate)
	{
		const auto result = adapter_.processRequest(state(now), request(sequence, created), fixtureIngress());
		require(result.report_count == 1 && result.has_operation_s1_decision
			&& result.operation_s1_decision.code == (duplicate ? DecisionCode::DuplicateSequence : DecisionCode::Accepted),
			"scheduled request decision");

		if (!duplicate) {
			require(result.operation_s1_decision.eligible_for_control, "admitted request is eligible");
			last_admitted_ = now;
		}

		const Status status = encode(result.reports[0]);
		++requests_;
		prefix("request", now);
		std::cout << ",\"sequence\":" << sequence << ",\"created_us\":" << created << ",\"sent_us\":" << created
			  << ",\"received_us\":" << now - 4000 << ",\"status_id\":" << status.id
			  << ",\"payload\":\"" << status.payload << "\"}\n";
		enqueue(now, status);
	}

	void tick(TimeUs offset)
	{
		const TimeUs now = start_ + offset;

		if (offset == 0) { admit(now, 1, kFirstCreation, false); }

		if (!late_ && offset == 40000) { admit(now, 2, kFirstCreation + 40000, false); }

		if (!late_ && offset == 60000) { admit(now, 2, kFirstCreation + 40000, true); }

		CanonicalRequest value{};
		value.sequence = kSentinel;
		const auto access = adapter_.getEligibleRequest(state(now), value);
		const bool eligible = access.code == AdapterCode::EligibleRequest;
		require(access.report_count == 1 && eligible == (late_ ? offset == 0 : offset <= kPairLastEligible),
			"eligibility follows request time, never status or heartbeat");

		if (eligible) {
			const bool second = !late_ && offset >= 40000;
			canonicalMatches(value, second ? 2 : 1, kFirstCreation + (second ? 40000 : 0));

		} else { require(value.sequence == kSentinel, "ineligible access leaves output unchanged"); }

		require(last_admitted_ <= now, "admission timer is ordered");
		const bool no_fresh = now - last_admitted_ >= kNoFreshThreshold;
		require(no_fresh == (late_ ? offset >= 40000 : offset >= 80000), "diagnostic timing including same-time admission");
		++accesses_;
		prefix("access", now);
		std::cout << ",\"eligible\":" << (eligible ? "true" : "false")
			  << ",\"sequence\":" << (eligible ? value.sequence : 0)
			  << ",\"last_admitted_us\":" << last_admitted_
			  << ",\"no_fresh\":" << (no_fresh ? "true" : "false")
			  << ",\"payload\":\"" << encode(access.reports[0]).payload << "\"}\n";

		if (offset % kDiagnosticPeriod == 0) {
			// No heartbeat adapter API exists; this event cannot refresh a request.
			++heartbeats_;
			prefix("heartbeat", now);
			std::cout << "}\n";
			require(snapshots_ == 0 || now - last_snapshot_ >= kDiagnosticPeriod, "snapshot at most 1 Hz");
			last_snapshot_ = now;
			++snapshots_;
			const Status status = encode(access.reports[0], true);
			prefix("snapshot", now);
			std::cout << ",\"status_id\":" << status.id << ",\"payload\":\"" << status.payload << "\"}\n";
			enqueue(now, status);
		}

		if (!faults_) {
			while (depth_ != 0) { deliver(now, 0); }

		} else {
			if (offset == 60000) { deliver(now, 1); }

			if (offset == 65000 || offset == kPairEnd) { deliver(now, 0); }

			if (offset == kDiagnosticPeriod) { deliver(now, 0, true); }
		}
	}

	const char *name_;
	const bool faults_;
	const bool late_;
	const TimeUs start_;
	WireAdapter adapter_;
	std::array<Status, 2> queue_{};
	unsigned depth_{0};
	TimeUs last_admitted_{0};
	TimeUs last_snapshot_{0};
	uint64_t requests_{0};
	uint64_t accesses_{0};
	uint64_t heartbeats_{0};
	uint64_t snapshots_{0};
	uint64_t enqueued_{0};
	uint64_t dropped_{0};
	uint64_t returned_{0};
	uint64_t backpressure_{0};
};
} // namespace

int main()
{
	Fixture("baseline", false, false).run();
	Fixture("status_faults", true, false).run();
	Fixture("late_admission", false, true).run();
	return 0;
}
