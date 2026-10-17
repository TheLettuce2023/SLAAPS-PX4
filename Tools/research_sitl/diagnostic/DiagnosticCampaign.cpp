#include "DiagnosticJson.hpp"

#include <array>
#include <condition_variable>
#include <cstring>
#include <functional>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>

using namespace research_acceleration;
using namespace research_sitl;
namespace json = research_sitl::diagnostic_json;

namespace
{
constexpr uint64_t kOwner = 13;
constexpr uint64_t kRun = 17;
constexpr uint64_t kOperator = 19;
constexpr uint64_t kStateStream = 23;
constexpr uint32_t kExpectedCalls = 20;
constexpr uint32_t kExpectedReports = 19;
constexpr const char *kProfile = "c1b-owner-reference/v1";

void require(bool condition, const char *message)
{
	if (!condition) { throw std::runtime_error(message); }
}

OwnerHarnessConfig configuration()
{
	// Pinned synthetic software fixture. These are not operational defaults.
	return {{
			kWireProtocolVersionV1, 7, DisarmedPolicy::ValidateOnly,
			{Vec3f(-10.F, -10.F, -10.F), Vec3f(10.F, 10.F, 10.F), 10.F},
			{20, 200, 50, 10, 2}, 0.001F
		}, {11}, kRun, 50};
}

TrustedStateSample initialPhysical()
{
	return {1, true, {9950, ArmState::Disarmed, ResearchModeState::NotSelected, true, true, true},
		{true, 7, 100, 11, 1, 1000, 2, 8000, 10000}};
}

template<typename T>
std::array<unsigned char, sizeof(T)> bytes(const T &value)
{
	std::array<unsigned char, sizeof(T)> result{};
	std::memcpy(result.data(), &value, sizeof(T));
	return result;
}

// This forwarding audit observes the real trusted seam. It never decides
// admission, supplies a positive result, changes an input, or owns a guard.
class AuditOwner final : public SupervisorOwner
{
public:
	explicit AuditOwner(HarnessSupervisorOwner &owner) : owner_(owner) {}
	DiagnosticDomain domain() const noexcept override { return owner_.domain(); }
	bool attachDiagnosticOperator(uint64_t id) noexcept override { return owner_.attachDiagnosticOperator(id); }
	bool diagnosticSnapshot(DiagnosticOwnerView &output) noexcept override { return owner_.diagnosticSnapshot(output); }
	bool snapshot(OwnerView &output) noexcept override { return owner_.snapshot(output); }
	OwnerOutcome submit(const Submission &submission, AdminResult &output) noexcept override
	{ return owner_.submit(submission, output); }
	OwnerOutcome submitDiagnostic(const DiagnosticSubmission &input, DiagnosticResult &output) noexcept override
	{
		submission = input;
		outcome = owner_.submitDiagnostic(input, output);

		if (*outcome == OwnerOutcome::Completed) { result = output; }

		return *outcome;
	}
	OwnerOutcome closeDiagnosticOwner(DiagnosticCloseReason reason, DiagnosticResult &output) noexcept override
	{
		close_reason = reason;
		outcome = owner_.closeDiagnosticOwner(reason, output);

		if (*outcome == OwnerOutcome::Completed) { result = output; }

		return *outcome;
	}
	void begin()
	{
		submission.reset();
		outcome.reset();
		result.reset();
		close_reason.reset();
	}
	std::optional<DiagnosticSubmission> submission;
	std::optional<OwnerOutcome> outcome;
	std::optional<DiagnosticResult> result;
	std::optional<DiagnosticCloseReason> close_reason;
private:
	HarnessSupervisorOwner &owner_;
};

class Campaign
{
public:
	Campaign() : owner_(configuration(), DiagnosticOfflineTag{}, kOwner), audit_(owner_), broker_({50}, audit_), console_(broker_) {}
	~Campaign()
	{
		if (!closed_) {
			DiagnosticResult ignored{};
			owner_.closeDiagnosticOwner(DiagnosticCloseReason::OperatorLost, ignored);
		}
	}

	json::Json run()
	{
		recordBool("attachFixtureOperator", json::object({{"operator_instance", json::value(kOperator)}}),
		[&] { return console_.attachFixtureOperator(kOperator); });
		tick(9950);
		const auto enable = recordConsole("prepareDiagnosticSelection",
		json::object({{"prospective", json::value(TrustedSession{7, 100})}}),
		[&] { return console_.prepareDiagnosticSelection({7, 100}); }, BrokerCode::Ready);
		recordConsole("confirmDiagnosticSelection", json::object({{"ticket", json::value(enable.ticket)}}),
		[&] { return console_.confirmDiagnosticSelection(enable.ticket); }, BrokerCode::Submitted,
		DiagnosticCode::SelectionEnabled);
		offerEstablish();
		const auto establish = recordConsole("prepareDiagnosticAdmin",
		json::object({{"intent", json::value(Intent::Establish)}, {"session", json::value(TrustedSession{7, 100})}}),
		[&] { return console_.prepareDiagnosticAdmin(Intent::Establish, {7, 100}); }, BrokerCode::Ready);
		recordConsole("confirmDiagnosticAdmin", json::object({{"ticket", json::value(establish.ticket)}}),
		[&] { return console_.confirmDiagnosticAdmin(establish.ticket); }, BrokerCode::Submitted,
		DiagnosticCode::SessionEstablished);
		tick(9965);
		const auto ack = packet(3, 8960);
		DiagnosticResult acknowledgement{};
		recordBool("processDiagnosticAcknowledgement",
		json::object({{"packet", json::value(ack)}, {"ingress", json::value(ingress())}}),
		[&] { return owner_.processDiagnosticAcknowledgement(ack, ingress(), acknowledgement); }, &acknowledgement);
		require(acknowledgement.code == DiagnosticCode::Acknowledged, "Real ACK rejected");
		tick(10010);
		const auto first = request(1, 9000);
		DiagnosticResult accepted{};
		recordBool("processDiagnosticRequest",
		json::object({{"packet", json::value(first)}, {"ingress", json::value(ingress())}}),
		[&] { return owner_.processDiagnosticRequest(first, ingress(), accepted); }, &accepted);
		require(accepted.code == DiagnosticCode::RequestValidatedDisarmed, "Real request not validated");
		DiagnosticResult access{};
		recordBool("accessDiagnostic", json::object({}), [&] { return owner_.accessDiagnostic(access); }, &access);
		require(access.code == DiagnosticCode::NoEligibleRequest, "Diagnostic access produced authority");
		DiagnosticResult duplicate{};
		recordBool("processDiagnosticRequest",
		json::object({{"packet", json::value(first)}, {"ingress", json::value(ingress())}}),
		[&] { return owner_.processDiagnosticRequest(first, ingress(), duplicate); }, &duplicate);
		require(duplicate.code == DiagnosticCode::Rejected, "Duplicate accepted");
		tick(10011);
		const auto regressed = request(2, 8999);
		DiagnosticResult latched{};
		recordBool("processDiagnosticRequest",
		json::object({{"packet", json::value(regressed)}, {"ingress", json::value(ingress())}}),
		[&] { return owner_.processDiagnosticRequest(regressed, ingress(), latched); }, &latched);
		require(latched.snapshot.latched && latched.snapshot.selection == DiagnosticSelectionState::Disabled,
			"Producer regression did not close selection");
		const auto clearing = recordConsole("prepareDiagnosticAdmin",
		json::object({{"intent", json::value(Intent::ClearLatch)}, {"session", json::value(TrustedSession{})}}),
		[&] { return console_.prepareDiagnosticAdmin(Intent::ClearLatch, {}); }, BrokerCode::Ready);
		recordConsole("confirmDiagnosticAdmin", json::object({{"ticket", json::value(clearing.ticket)}}),
		[&] { return console_.confirmDiagnosticAdmin(clearing.ticket); }, BrokerCode::Submitted,
		DiagnosticCode::LatchCleared);
		recordConsole("disableDiagnosticSelection", json::object({}),
			      [&] { return console_.disableDiagnosticSelection(); }, BrokerCode::Submitted, DiagnosticCode::AlreadyDisabled);
		recordConsole("detachDiagnosticOperator", json::object({}),
			      [&] { return console_.detachDiagnosticOperator(); }, BrokerCode::Submitted, DiagnosticCode::AlreadyDisabled);
		require(isClosed(), "Owner not closed after detach");
		closed_ = true;
		ordinaryAccessSentinel();
		require(calls_.size() == kExpectedCalls && report_count_ == kExpectedReports, "Pinned campaign shape changed");

		return json::object({
			{"schema", json::value("research-c1b-offline/v1")},
			{
				"header", json::object({
					{"profile_id", json::value(kProfile)}, {"compile_domain", json::value(DiagnosticDomain::SitlDisarmedDiagnostic)},
					{"provenance", json::value("synthetic_offline")},
					{
						"identity", json::object({{"owner_lifetime_id", json::value(kOwner)}, {"fixture_run_id", json::value(kRun)},
							{"controller_instance_id", json::value(uint64_t{11})}})
					},
					{"configuration", json::object({{"owner", json::value(configuration())}, {"broker", json::value(BrokerConfig{50})}})}
				})
			},
			{
				"trace", json::object({
					{"calls", json::array(calls_)}, {
						"end", json::object({
							{"call_count", json::value(static_cast<uint32_t>(calls_.size()))}, {"report_count", json::value(report_count_)},
							{"last_report_sequence", json::value(last_snapshot_.adapter.last_report_sequence)},
							{"final_snapshot", json::value(last_snapshot_)}
						})
					}
				})
			}
		});
	}

	json::Json sinkTest(const json::Json &immutable_document)
	{
		// Controlled blocking is an evidence-sink test, never an owner callback.
		// No scheduler deadline or operational timeout is selected.
		const bool closed_before = isClosed();
		std::mutex mutex;
		std::condition_variable changed;
		bool entered = false;
		bool release = false;
		bool blocked_sink_accepted = true;
		std::thread writer([&] {
			std::unique_lock<std::mutex> lock(mutex);
			entered = true;
			changed.notify_one();
			changed.wait(lock, [&] { return release; });
			blocked_sink_accepted = false;
			(void)immutable_document;
		});

		{
			std::unique_lock<std::mutex> lock(mutex);
			changed.wait(lock, [&] { return entered; });
		}

		const bool closed_while_blocked = isClosed();

		{
			std::lock_guard<std::mutex> lock(mutex);
			release = true;
		}

		changed.notify_one();
		writer.join();
		const auto missing_sink = [](const json::Json &) { return false; };
		const bool missing_sink_accepted = missing_sink(immutable_document);
		require(closed_before && closed_while_blocked && !blocked_sink_accepted && !missing_sink_accepted,
			"Evidence sink affected owner closure");
		return json::object({
			{"schema", json::value("research-c1b-offline/sink-test/v1")}, {"profile_id", json::value(kProfile)},
			{"provenance", json::value("synthetic_offline")}, {"closed_before_sink", json::value(closed_before)},
			{"closed_while_blocked", json::value(closed_while_blocked)},
			{"blocked_sink_accepted", json::value(blocked_sink_accepted)}, {"missing_sink_accepted", json::value(missing_sink_accepted)},
			{"campaign_complete", json::value(false)}
		});
	}

private:
	bool isClosed()
	{
		DiagnosticOwnerView view{};
		return owner_.diagnosticSnapshot(view) && !view.state.terminal && !view.state.operator_attached
		       && view.state.selection == DiagnosticSelectionState::Disabled
		       && view.state.session.gate == SessionGate::EstablishmentRequired && !view.state.admission.acknowledgement_current
		       && !view.state.has_last_accepted_sequence && !view.state.last_accepted_authenticated;
	}

	DiagnosticOwnerView view()
	{
		DiagnosticOwnerView current{};
		require(owner_.diagnosticSnapshot(current), "No actual diagnostic owner view");
		return current;
	}

	DiagnosticIngress ingress() const { return {kOwner, kRun, {true, true, true, true}}; }

	DecodedSessionV1 packet(uint8_t operation, TimeUs raw = 0)
	{
		const auto current = view().state;
		return {11, current.adapter.transition_epoch, physical_.map.map_epoch, 7, physical_.map.session,
			current.session.wire_operation_floor + 1, raw, operation == 3 ? current.adapter.barrier_report_sequence : 0,
			kWireProtocolVersionV1, 1, 1, operation, 0, {0, 0, 0, 0}};
	}

	DecodedRequestV1 request(uint64_t sequence, TimeUs raw)
	{
		return {{
				7, 100, sequence, raw, raw + 100, 11, view().state.adapter.transition_epoch, 1,
				1.F, 0.F, 0.F, kWireProtocolVersionV1, kWireFrameFrdV1, kWireQuantityInertialAccelerationV1, kWireReferencePointCgV1
			},
			1, 1, 0, {0, 0, 0, 0}};
	}

	void tick(TimeUs time)
	{
		physical_.request_guard_context.validator_receive_time_us = time;
		++observation_;
		const FixtureStateReceipt receipt{physical_, {kRun, observation_, time, true}, kStateStream};
		DiagnosticResult result{};
		recordBool("tickDiagnostic", json::object({{"receipt", json::value(receipt)}}),
		[&] { return owner_.tickDiagnostic(receipt, &result); }, &result);
	}

	void offerEstablish()
	{
		offered_ = ImmutableIngressReceipt{kRun, 1, physical_.request_guard_context.validator_receive_time_us,
						   packet(1), ingress().authentication};
		recordBool("offer", json::object({{"receipt", json::value(*offered_)}}), [&] { return owner_.offer(*offered_); });
	}

	json::Json derivedTurn() const
	{
		if (audit_.close_reason) {
			return json::object({{"provenance", json::value("owner_derived")},
				{
					"operation", json::object({{"kind", json::value("LifecycleClose")},
						{"value", json::object({{"reason", json::value(*audit_.close_reason)}})}})
				}});
		}

		if (!audit_.submission) { return "null"; }

		const auto &submission = *audit_.submission;
		const auto &view = submission.expected_view;
		DiagnosticTurn operation{};

		if (const auto *selection = std::get_if<DiagnosticSelectionCommand>(&submission.command)) {
			const DiagnosticSelectionGrant grant{selection->event_id, selection->action, view.binding};

			if (selection->action == DiagnosticAction::Enable) { operation = DiagEnable{grant}; }

			else { operation = DiagDisable{grant}; }

		} else {
			const auto &admin = std::get<DiagnosticAdministrationCommand>(submission.command);
			const auto action = admin.intent == Intent::Establish ? SupervisorAction::Establish :
					    admin.intent == Intent::Revoke ? SupervisorAction::Revoke : SupervisorAction::ClearLatchedFault;
			const bool has_binding = admin.intent != Intent::ClearLatch || view.state.session.has_last_successful_session;
			const auto session = admin.intent == Intent::ClearLatch ? view.state.session.last_successful_session : admin.session;
			const DiagnosticAdminGrant grant{view.binding, {admin.event_id, view.binding.state_generation,
							 view.binding.transition_epoch, view.binding.controller_instance_id, action, has_binding, session
								       }};

			if (admin.intent == Intent::ClearLatch) { operation = DiagClear{grant}; }

			else {
				require(offered_.has_value(), "No observed receipt for owner-derived packet");
				const DiagnosticIngress authenticated{view.binding.owner_lifetime_id, view.binding.fixture_run_id, offered_->verdict};

				if (admin.intent == Intent::Establish) { operation = DiagEstablish{offered_->packet, authenticated, grant}; }

				else { operation = DiagRevoke{offered_->packet, authenticated, grant}; }
			}
		}

		return json::object({{"provenance", json::value("owner_derived")}, {"operation", json::value(operation)}});
	}

	void append(const char *name, const json::Json &input, const json::Json &outcome,
		    const std::optional<DiagnosticResult> &result)
	{
		calls_.push_back(json::object({
			{"call_id", json::value(static_cast<uint64_t>(calls_.size() + 1))}, {"observation_id", json::value(observation_)},
			{"name", json::value(name)}, {"input", input}, {"outcome", outcome},
			{"adapter_result", json::value(result)}, {"owner_submission", json::value(audit_.submission)},
			{"owner_outcome", json::value(audit_.outcome)}, {"owner_derived_turn", derivedTurn()}
		}));

		if (result) {
			report_count_ += result->record_count;
			last_snapshot_ = result->snapshot;
		}

		if (audit_.submission && std::holds_alternative<DiagnosticAdministrationCommand>(audit_.submission->command)) {
			offered_.reset();
		}
	}

	void recordBool(const char *name, const json::Json &input, const std::function<bool()> &call,
			const DiagnosticResult *result = nullptr)
	{
		audit_.begin();
		const bool returned = call();
		require(returned, "Real owner bool operation failed");
		append(name, input, json::object({{"kind", json::value("Bool")},
			{"value", json::object({{"returned", json::value(returned)}})}}),
		result != nullptr ? std::optional<DiagnosticResult> {*result} : std::nullopt);
	}

	DiagnosticConsoleResult recordConsole(const char *name, const json::Json &input,
					      const std::function<DiagnosticConsoleResult()> &call, BrokerCode expected,
					      const std::optional<DiagnosticCode> &diagnostic = std::nullopt)
	{
		audit_.begin();
		const auto result = call();
		require(result.broker_code == expected && result.diagnostic_code == diagnostic, "Real broker result differs from fixed profile");
		append(name, input, json::object({{"kind", json::value("Console")}, {"value", json::value(result)}}), audit_.result);
		return result;
	}

	void ordinaryAccessSentinel()
	{
		CanonicalRequest request{};
		request.protocol_version = kWireProtocolVersionV1;
		request.source = 7;
		request.session = 100;
		request.sequence = 777;
		request.producer_monotonic_us = 888;
		request.producer_time_in_validator_domain_us = 999;
		request.producer_expiry_monotonic_us = 1111;
		request.expiry_time_in_validator_domain_us = 1222;
		request.clock_mapping_uncertainty_us = 2;
		request.frame = Frame::Frd;
		request.quantity = Quantity::InertialAcceleration;
		request.reference_point = ReferencePoint::Cg;
		request.acceleration_frd_m_s2 = Vec3f(3.F, 4.F, 5.F);
		AdapterResult adapter{};
		adapter.code = AdapterCode::EligibleRequest;
		adapter.admission_closed = false;
		adapter.snapshot = {true, false, 701, 702, 703, 704, 705};
		const auto request_before = json::value(request);
		const auto adapter_before = json::value(adapter);
		const auto request_bytes = bytes(request);
		const auto adapter_bytes = bytes(adapter);
		audit_.begin();
		const bool returned = owner_.getEligibleRequest(request, adapter);
		const bool same_request = bytes(request) == request_bytes;
		const bool same_adapter = bytes(adapter) == adapter_bytes;
		require(!returned && same_request && same_adapter, "Ordinary access modified diagnostic output sentinel");
		append("getEligibleRequest", json::object({}), json::object({
			{"kind", json::value("FlightAccess")}, {
				"value", json::object({
					{"returned", json::value(returned)}, {"request_before", request_before}, {"request_after", json::value(request)},
					{"adapter_before", adapter_before}, {"adapter_after", json::value(adapter)},
					{"request_bytes_unchanged", json::value(same_request)}, {"adapter_bytes_unchanged", json::value(same_adapter)}
				})
			}
		}), std::nullopt);
	}

	HarnessSupervisorOwner owner_;
	AuditOwner audit_;
	SupervisorBroker broker_;
	SupervisorConsoleFacade console_;
	TrustedStateSample physical_{initialPhysical()};
	uint64_t observation_{0};
	std::optional<ImmutableIngressReceipt> offered_;
	std::vector<json::Json> calls_;
	uint32_t report_count_{0};
	DiagnosticSnapshot last_snapshot_{};
	bool closed_{false};
};
} // namespace

int main(int argc, char **argv)
{
	try {
		const bool sink_test = argc == 2 && std::string(argv[1]) == "--sink-test";

		if (argc != 1 && !sink_test) { throw std::invalid_argument("Usage: diagnostic_campaign [--sink-test]"); }

		Campaign campaign;
		const auto document = campaign.run();
		const auto output = sink_test ? campaign.sinkTest(document) : document;
		// Delivery is outside ownership and occurs only after synchronous close.
		std::cout << output << '\n';
		return std::cout ? 0 : 2;

	} catch (const std::exception &error) {
		std::cerr << "Diagnostic campaign incomplete: " << error.what() << '\n';
		return 2;
	}
}
