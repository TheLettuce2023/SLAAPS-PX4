#pragma once

#include "SupervisorBroker.hpp"

#include <cstdint>

namespace research_sitl
{

struct OwnerHarnessConfig {
	research_acceleration::ContractConfig adapter_contract;
	research_acceleration::AdapterIdentity adapter_identity;
	uint64_t fixture_run_id;
	uint64_t maximum_receipt_age_us;
};

struct FixtureClockProvenance {
	uint64_t fixture_run_id;
	uint64_t observation_id;
	uint64_t validator_time_us;
	bool continuous;
};

struct FixtureStateReceipt {
	research_acceleration::TrustedStateSample sample;
	FixtureClockProvenance clock;
	uint64_t fixture_state_stream_id;
};

struct ImmutableIngressReceipt {
	uint64_t fixture_run_id;
	uint64_t receipt_id;
	uint64_t received_validator_time_us;
	research_acceleration::DecodedSessionV1 packet;
	research_acceleration::SessionIngressVerdict verdict;
};

// Offline fixture owner. The caller must serialize tick, offer and broker calls.
class HarnessSupervisorOwner final : public SupervisorOwner
{
public:
	explicit HarnessSupervisorOwner(const OwnerHarnessConfig &config) noexcept;
#if defined(RESEARCH_SITL_DIAGNOSTIC_OFFLINE)
	HarnessSupervisorOwner(const OwnerHarnessConfig &config, research_acceleration::DiagnosticOfflineTag,
			       uint64_t owner_lifetime_id) noexcept;
	research_acceleration::DiagnosticDomain domain() const noexcept override;
	bool attachDiagnosticOperator(uint64_t operator_instance) noexcept override;
	bool tickDiagnostic(const FixtureStateReceipt &receipt, research_acceleration::DiagnosticResult *output = nullptr) noexcept;
	bool processDiagnosticAcknowledgement(const research_acceleration::DecodedSessionV1 &packet,
					      const research_acceleration::DiagnosticIngress &ingress, research_acceleration::DiagnosticResult &output) noexcept;
	bool processDiagnosticRequest(const research_acceleration::DecodedRequestV1 &packet,
				      const research_acceleration::DiagnosticIngress &ingress, research_acceleration::DiagnosticResult &output) noexcept;
	bool accessDiagnostic(research_acceleration::DiagnosticResult &output) noexcept;
	bool diagnosticSnapshot(DiagnosticOwnerView &output) noexcept override;
	OwnerOutcome submitDiagnostic(const DiagnosticSubmission &submission,
				      research_acceleration::DiagnosticResult &output) noexcept override;
	OwnerOutcome closeDiagnosticOwner(research_acceleration::DiagnosticCloseReason reason,
					  research_acceleration::DiagnosticResult &output) noexcept override;
	bool copyLastDiagnosticResult(research_acceleration::DiagnosticResult &output) const noexcept;
#endif
	HarnessSupervisorOwner(const HarnessSupervisorOwner &) = delete;
	HarnessSupervisorOwner &operator=(const HarnessSupervisorOwner &) = delete;
	HarnessSupervisorOwner(HarnessSupervisorOwner &&) = delete;
	HarnessSupervisorOwner &operator=(HarnessSupervisorOwner &&) = delete;

	// True only when a poll completed without a permanent provenance fault.
	bool tick(const FixtureStateReceipt &receipt) noexcept;
	bool tick(const FixtureStateReceipt &receipt, research_acceleration::AdapterResult *output) noexcept;
	bool offer(const ImmutableIngressReceipt &receipt) noexcept;
	// Trusted serialized host driver only; never exposed through the console.
	// A fresh tick is required immediately before each live operation.
	bool processAcknowledgement(const research_acceleration::DecodedSessionV1 &packet,
				    const research_acceleration::SessionIngressVerdict &verdict,
				    research_acceleration::IngressResult &output) noexcept;
	bool processRequest(const research_acceleration::DecodedRequestV1 &packet,
			    const research_acceleration::SessionIngressVerdict &verdict,
			    research_acceleration::IngressResult &output) noexcept;
	bool getEligibleRequest(research_acceleration::CanonicalRequest &request,
				research_acceleration::AdapterResult &output) noexcept;
	// False leaves out unchanged.
	bool snapshot(OwnerView &out) noexcept override;
	// NotSubmitted leaves out unchanged.
	OwnerOutcome submit(const Submission &submission,
			    research_acceleration::AdminResult &out) noexcept override;

private:
	friend struct HarnessSupervisorOwnerTestPeer;
#if defined(RESEARCH_SITL_DIAGNOSTIC_OFFLINE)
	friend struct DiagnosticSupervisorOwnerTestPeer;
	DiagnosticOwnerView currentDiagnosticView() const noexcept;
	void retainDiagnosticResult(const research_acceleration::DiagnosticResult &result) noexcept;
	void closeDiagnosticOnFault() noexcept;
	bool runDiagnosticIngress(const research_acceleration::DiagnosticTurn &turn,
				  research_acceleration::DiagnosticResult &output) noexcept;
#endif

	bool enter() noexcept;
	void leave() noexcept;
	void disableAdministration() noexcept;
	bool coherentTick(const FixtureStateReceipt &receipt) const noexcept;
	bool receiptAgeValid(const ImmutableIngressReceipt &receipt) const noexcept;
	bool sameView(const OwnerView &a, const OwnerView &b) const noexcept;
	bool validReceiptForSubmission(const Submission &submission) const noexcept;
	bool validPacketShape(const research_acceleration::DecodedSessionV1 &packet,
			      Intent intent) const noexcept;
	bool validIngress(const research_acceleration::SessionIngressVerdict &verdict) const noexcept;
	void retainView(const research_acceleration::AdapterSnapshot &snapshot,
			const research_acceleration::TrustedStateSample &sample) noexcept;

	const OwnerHarnessConfig config_;
	research_acceleration::WireAdapter adapter_;
#if defined(RESEARCH_SITL_DIAGNOSTIC_OFFLINE)
	const bool diagnostic_domain_ {false};
	bool has_diagnostic_audit_{false};
	research_acceleration::DiagnosticResult diagnostic_audit_{};
#endif
	bool in_call_ {false};
	bool administration_disabled_{false};
	bool has_stream_id_{false};
	uint64_t stream_id_{0};
	bool has_observation_id_{false};
	uint64_t observation_id_floor_{0};
	bool has_view_{false};
	OwnerView view_{};
	research_acceleration::TrustedStateSample sample_{};
	bool has_receipt_{false};
	ImmutableIngressReceipt receipt_{};
	uint64_t receipt_id_floor_{0};
};

// Redacted offline facade only. It has no owner, adapter, fixture or ingress API.
class SupervisorConsoleFacade final
{
public:
	explicit SupervisorConsoleFacade(SupervisorBroker &broker) noexcept : broker_(broker) {}
	bool attachFixtureOperator(uint64_t injected_operator_instance) noexcept
	{
		return broker_.attach(injected_operator_instance);
	}
	bool detachFixtureOperator() noexcept { return broker_.detach(); }
	ConsoleResult prepare(Intent intent, const research_acceleration::TrustedSession &session) noexcept
	{
		return broker_.prepare(intent, session);
	}
	ConsoleResult confirm(uint64_t ticket) noexcept { return broker_.confirm(ticket); }
#if defined(RESEARCH_SITL_DIAGNOSTIC_OFFLINE)
	DiagnosticConsoleResult prepareDiagnosticSelection(const research_acceleration::TrustedSession &prospective) noexcept
	{ return broker_.prepareDiagnosticSelection(prospective); }
	DiagnosticConsoleResult confirmDiagnosticSelection(uint64_t ticket) noexcept
	{ return broker_.confirmDiagnosticSelection(ticket); }
	DiagnosticConsoleResult disableDiagnosticSelection() noexcept
	{ return broker_.disableDiagnosticSelection(); }
	DiagnosticConsoleResult detachDiagnosticOperator() noexcept
	{ return broker_.detachDiagnosticOperator(); }
	DiagnosticConsoleResult prepareDiagnosticAdmin(Intent intent, const research_acceleration::TrustedSession &session) noexcept
	{ return broker_.prepareDiagnosticAdmin(intent, session); }
	DiagnosticConsoleResult confirmDiagnosticAdmin(uint64_t ticket) noexcept
	{ return broker_.confirmDiagnosticAdmin(ticket); }
#endif

private:
	SupervisorBroker &broker_;
};

} // namespace research_sitl
