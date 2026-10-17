#pragma once

#include "WireAdapter.hpp"
#include <cstdint>

namespace research_sitl
{
enum class Intent : uint8_t { Establish, Revoke, ClearLatch };
enum class ConsoleDisposition : uint8_t { NotSubmitted, Submitted, Quarantined };
enum class BrokerCode : uint8_t {
	Ready, Submitted, NotConfigured, NotAttached, Busy, InvalidIntent, InvalidTicket,
	OwnerUnavailable, OwnerNotReady, ContextChanged, Expired, CounterExhausted,
	AuthorityDiscontinuity, OwnerDeclined, OutcomeUnknown, AdapterUnconsumed,
	InvalidOwnerResult, Quarantined
};

struct BrokerConfig {
	uint64_t maximum_confirmation_age_us; // Required; no operational default.
};

// Trusted owner-only view, already observed by the actual adapter.
struct OwnerView {
	bool ready;
	bool terminal;
	uint64_t controller_instance_id;
	uint64_t state_generation;
	uint64_t transition_epoch;
	uint64_t clock_mapping_epoch;
	uint64_t validator_time_us;
	bool has_session_history;
	research_acceleration::TrustedSession last_session;
	bool has_supervisor_floor;
	uint64_t supervisor_floor;
};

struct Submission {
	Intent intent;
	research_acceleration::TrustedSession requested_session;
	OwnerView expected_view;
	research_acceleration::SupervisorEvent event;
};

enum class OwnerOutcome : uint8_t { NotSubmitted, Completed, Unknown };


#if defined(RESEARCH_SITL_DIAGNOSTIC_OFFLINE)
struct DiagnosticOwnerView {
	research_acceleration::DiagnosticBinding binding;
	uint64_t validator_time_us;
	research_acceleration::DiagnosticSnapshot state;
	research_acceleration::TrustedStateSample physical;
	bool can_enable, can_disable, can_admin;
};
struct DiagnosticSelectionCommand {
	research_acceleration::DiagnosticAction action;
	research_acceleration::TrustedSession prospective;
	uint64_t event_id;
};
struct DiagnosticAdministrationCommand {
	Intent intent;
	research_acceleration::TrustedSession session;
	uint64_t event_id;
};
struct DiagnosticSubmission {
	DiagnosticOwnerView expected_view;
	std::variant<DiagnosticSelectionCommand, DiagnosticAdministrationCommand> command;
};
struct DiagnosticConsoleResult {
	ConsoleDisposition disposition;
	BrokerCode broker_code;
	std::optional<research_acceleration::DiagnosticCode> diagnostic_code;
	uint64_t ticket;
	bool event_consumed;
	bool action_performed;
};
// Exact submission comparison; pending tickets may ignore observation-only
// report/rejection counters and validator time, but no authority context.
bool sameDiagnosticOwnerView(const DiagnosticOwnerView &a, const DiagnosticOwnerView &b,
			     bool include_time) noexcept;
#endif

// Trusted harness seam, NOT a UI or authentication API. Both callbacks and all
// adapter polling/ingress run in one serialized owner domain. submit performs
// a final exact view check (including time) before one actual adapter call.
// NotSubmitted guarantees no adapter/event side effect; Unknown means quarantine.
class SupervisorOwner
{
public:
	virtual ~SupervisorOwner() = default;
#if defined(RESEARCH_SITL_DIAGNOSTIC_OFFLINE)
	virtual research_acceleration::DiagnosticDomain domain() const noexcept { return research_acceleration::DiagnosticDomain::Flight; }
	virtual bool attachDiagnosticOperator(uint64_t) noexcept { return false; }
	virtual bool diagnosticSnapshot(DiagnosticOwnerView &) noexcept { return false; }
	virtual OwnerOutcome submitDiagnostic(const DiagnosticSubmission &, research_acceleration::DiagnosticResult &) noexcept
	{ return OwnerOutcome::NotSubmitted; }
	virtual OwnerOutcome closeDiagnosticOwner(research_acceleration::DiagnosticCloseReason,
			research_acceleration::DiagnosticResult &) noexcept { return OwnerOutcome::NotSubmitted; }
#endif
	virtual bool snapshot(OwnerView &output) noexcept = 0;
	virtual OwnerOutcome submit(const Submission &submission,
				    research_acceleration::AdminResult &output) noexcept = 0;
};

struct ConsoleResult {
	ConsoleDisposition disposition;
	BrokerCode code;
	uint64_t ticket;
	bool has_adapter_code;
	research_acceleration::AdminCode adapter_code; // Invalid unless has_adapter_code.
	bool event_consumed;
};

class SupervisorBroker
{
public:
	SupervisorBroker(const BrokerConfig &config, SupervisorOwner &owner) noexcept;
	SupervisorBroker(const SupervisorBroker &) = delete;
	SupervisorBroker &operator=(const SupervisorBroker &) = delete;
	SupervisorBroker(SupervisorBroker &&) = delete;
	SupervisorBroker &operator=(SupervisorBroker &&) = delete;

	// Trusted cooperative-harness attachment; never claims OS authentication.
	bool attach(uint64_t operator_instance) noexcept;
	bool detach() noexcept;
	ConsoleResult prepare(Intent intent, const research_acceleration::TrustedSession &session) noexcept;
	ConsoleResult confirm(uint64_t ticket) noexcept;
#if defined(RESEARCH_SITL_DIAGNOSTIC_OFFLINE)
	DiagnosticConsoleResult prepareDiagnosticSelection(const research_acceleration::TrustedSession &prospective) noexcept;
	DiagnosticConsoleResult confirmDiagnosticSelection(uint64_t ticket) noexcept;
	DiagnosticConsoleResult disableDiagnosticSelection() noexcept;
	DiagnosticConsoleResult detachDiagnosticOperator() noexcept;
	DiagnosticConsoleResult prepareDiagnosticAdmin(Intent intent, const research_acceleration::TrustedSession &session) noexcept;
	DiagnosticConsoleResult confirmDiagnosticAdmin(uint64_t ticket) noexcept;
	bool copyLastDiagnosticResult(research_acceleration::DiagnosticResult &output) const noexcept;
#endif
	bool quarantined() const noexcept { return quarantined_; }

	// Owner diagnostics only. No adapter internals are returned by UI methods.
	// False leaves output unchanged. Never calls the owner.
	bool copyLastAdminResult(research_acceleration::AdminResult &output) const noexcept;

private:
	friend struct SupervisorBrokerTestPeer;
	BrokerCode readView(OwnerView &view) noexcept;
	ConsoleResult result(BrokerCode code, uint64_t ticket = 0) const noexcept;
	ConsoleResult quarantine(BrokerCode code, uint64_t ticket) noexcept;
	static bool sameContext(const OwnerView &a, const OwnerView &b) noexcept;
	static bool validResult(const Submission &submission,
				const research_acceleration::AdminResult &result) noexcept;

#if defined(RESEARCH_SITL_DIAGNOSTIC_OFFLINE)
	DiagnosticConsoleResult diagnosticResult(BrokerCode code, uint64_t ticket = 0) const noexcept;
	DiagnosticConsoleResult diagnosticQuarantine(BrokerCode code, uint64_t ticket) noexcept;
	BrokerCode readDiagnosticView(DiagnosticOwnerView &view) noexcept;
	DiagnosticConsoleResult prepareDiagnostic(bool selection, Intent intent,
			const research_acceleration::TrustedSession &session) noexcept;
	DiagnosticConsoleResult confirmDiagnostic(uint64_t ticket, bool selection) noexcept;
	DiagnosticConsoleResult submitDiagnosticCommand(const DiagnosticSubmission &submission, uint64_t ticket) noexcept;
	static bool validDiagnosticResult(const DiagnosticSubmission &submission,
					  const research_acceleration::DiagnosticResult &result) noexcept;
	bool closeDiagnosticAuthority(research_acceleration::DiagnosticCloseReason reason,
				      research_acceleration::DiagnosticResult &response) noexcept;
	const research_acceleration::DiagnosticDomain domain_;
	bool pending_diagnostic_selection_{false};
	DiagnosticOwnerView pending_diagnostic_view_{};
	uint64_t diagnostic_owner_binding_{0};
	uint64_t diagnostic_run_binding_{0};
	bool has_diagnostic_audit_{false};
	research_acceleration::DiagnosticResult last_diagnostic_audit_{};
#endif

	const BrokerConfig config_;
	SupervisorOwner &owner_;
	bool attached_{false};
	bool in_call_{false};
	bool quarantined_{false};
	uint64_t operator_floor_{0};
	uint64_t controller_binding_{0};
	uint64_t ticket_floor_{0};
	uint64_t event_floor_{0};
	bool has_time_{false};
	uint64_t time_floor_{0};
	bool pending_{false};
	Intent pending_intent_{Intent::ClearLatch};
	research_acceleration::TrustedSession pending_session_{};
	OwnerView pending_view_{};
	uint64_t pending_ticket_{0};
	bool has_audit_{false};
	research_acceleration::AdminResult last_audit_{};
};
} // namespace research_sitl
