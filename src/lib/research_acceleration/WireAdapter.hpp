#pragma once

#include "TrustedTimeMap.hpp"

#include <cstdint>

#if defined(RESEARCH_SITL_DIAGNOSTIC_OFFLINE)
namespace research_sitl { class HarnessSupervisorOwner; }
#elif RESEARCH_SITL_DIAGNOSTIC_CAPABILITY
namespace research_sitl { class NativeClosedDiagnosticOwner; class NativeIngressDiagnosticOwner; }
#endif

namespace research_acceleration
{

struct AdapterIdentity {
	uint64_t controller_instance_id;
};

struct TrustedStateSample {
	uint64_t generation;
	bool observer_continuous;
	EvaluationContext request_guard_context;
	TrustedClockMap map;
};

enum class SessionGate : uint8_t { NoTrustedSession, Established, Revoked, EstablishmentRequired };
enum class StatusDecisionOrigin : uint8_t { None, S1, Local };

struct ReportEvidence {
	bool captured;
	uint64_t controller_instance_id;
	bool state_reference_valid;
	uint64_t clock_mapping_epoch;
	ArmState arm_state;
	ResearchModeState mode_state;
	SessionGate session_gate;
	bool transport_connected;
	bool clock_mapping_valid;
	SourceId trusted_source;
	SessionId trusted_session;
	bool packet_mavlink2;
	bool packet_parser_authenticated;
	bool packet_header_source_bound;
	bool packet_target_bound;
	bool acknowledgement_current;
	bool has_last_accepted_sequence;
	uint64_t last_accepted_sequence;
	bool last_accepted_authenticated;
	bool latched;
	DecisionCode first_latched_code;
	uint64_t rejection_count;
	bool syntactically_valid;
	bool conservative_age_valid;
	TimeUs conservative_age_us;
	StatusDecisionOrigin decision_origin;
	uint16_t local_wire_decision_code;
};

enum class AdapterCode : uint16_t {
	Observed,
	NoEligibleRequest,
	GuardRejected,
	TrustedStateFault,
	CounterExhausted,
	NotConfigured,
	EligibleRequest,
	ApiDomainMismatch
};

enum class AdapterReportKind : uint8_t { Observation, Poll, Access, Barrier, Terminal };

struct AdapterReport {
	uint64_t report_sequence;
	uint64_t transition_epoch;
	TimeUs validator_time_us;
	AdapterReportKind kind;
	AdapterCode code;
	bool has_s1_decision;
	Decision s1_decision;
	ReportEvidence evidence;
};

struct AdapterSnapshot {
	bool initialized;
	bool terminal;
	uint64_t state_generation;
	uint64_t transition_epoch;
	uint64_t barrier_report_sequence;
	TimeUs barrier_validator_time_us;
	uint64_t last_report_sequence;
};

struct AdapterResult {
	AdapterCode code;
	bool admission_closed;
	AdapterSnapshot snapshot;
	uint8_t report_count;
	AdapterReport reports[2]; // Fixed barrier-plus-operation state-core capacity.
};

struct DecodedSessionV1 {
	uint64_t controller_instance_id;
	uint64_t transition_epoch;
	uint64_t clock_mapping_epoch;
	SourceId source;
	SessionId session;
	uint64_t operation_sequence;
	TimeUs producer_acknowledgement_monotonic_us;
	uint64_t acknowledged_status_sequence;
	uint32_t protocol_version;
	uint8_t target_system;
	uint8_t target_component;
	uint8_t operation;
	uint8_t flags;
	uint8_t reserved[4];
};

struct SessionIngressVerdict {
	bool mavlink2;
	bool parser_authenticated;
	bool configured_header_channel_source_binding;
	bool configured_target_binding;
};

enum class SupervisorAction : uint8_t { Establish, Revoke, ClearLatchedFault };

struct SupervisorEvent {
	uint64_t event_id;
	uint64_t expected_state_generation;
	uint64_t expected_transition_epoch;
	uint64_t expected_controller_instance_id;
	SupervisorAction action;
	bool has_session_binding;
	TrustedSession session;
};

enum class AdminCode : uint16_t {
	Observed,
	Completed,
	NotConfigured,
	TrustedStateFault,
	CounterExhausted,
	SupervisorMissing,
	SupervisorDuplicate,
	SupervisorStreamFault,
	MalformedPacket,
	IngressRejected,
	UnsupportedOperation,
	PacketOperationReplay,
	BindingMismatch,
	EstablishmentContextRejected,
	ContextRejected,
	NoEstablishedSession,
	ClearNotLatched,
	S1ActionRejected,
	ApiDomainMismatch
};

enum class AdminActionKind : uint8_t { None, EstablishTrustedSession, Revoke, ClearLatch };
enum class AdminReportKind : uint8_t { ExogenousBarrier, AdministrativeBarrier, Operation, Terminal };

struct SessionSnapshot {
	SessionGate gate;
	bool has_last_successful_session;
	TrustedSession last_successful_session;
	bool has_wire_operation_floor;
	uint64_t wire_operation_floor;
	bool has_supervisor_floor;
	uint64_t supervisor_floor;
};

struct AdminCorrelation {
	bool has_packet;
	SourceId packet_source;
	SessionId packet_session;
	uint64_t packet_operation_sequence;
	bool has_supervisor_event;
	uint64_t supervisor_event_id;
	bool supervisor_event_consumed;
};

struct AdminReport {
	uint64_t report_sequence;
	uint64_t transition_epoch;
	TimeUs validator_time_us;
	AdminReportKind kind;
	AdminCode code;
	bool has_s1_decision;
	Decision s1_decision;
	AdminCorrelation correlation;
	ReportEvidence evidence;
};

struct AdminResult {
	AdminCode code;
	bool admission_closed;
	AdapterSnapshot snapshot;
	SessionSnapshot session;
	bool has_prestate_s1_decision;
	Decision prestate_s1_decision;
	bool has_action_s1_decision;
	Decision action_s1_decision;
	bool action_performed;
	AdminActionKind action_kind;
	AdminCorrelation correlation;
	uint8_t report_count;
	AdminReport reports[3];
};

struct DecodedRequest {
	WireRequest wire;
	uint8_t target_system;
	uint8_t target_component;
	uint8_t flags;
	uint8_t reserved[4];
};

using DecodedRequestV1 = DecodedRequest;

enum class IngressPacketKind : uint8_t { None, Acknowledgement, Request };
enum class IngressReportKind : uint8_t { ExogenousBarrier, Operation, Terminal };

enum class IngressCode : uint16_t {
	Observed,
	Completed,
	NotConfigured,
	TrustedStateFault,
	CounterExhausted,
	PrestateRejected,
	MalformedPacket,
	IngressRejected,
	UnsupportedSessionOperation,
	WireOperationReplay,
	SessionGateRejected,
	BindingMismatch,
	AcknowledgementRequired,
	AcknowledgementFrozen,
	AcknowledgementStatusMismatch,
	AcknowledgementOrdering,
	AcknowledgementTimingInvalid,
	MapInvalid,
	MapIntervalInvalid,
	MapArithmeticInvalid,
	WireVersionInvalid,
	WireEnumInvalid,
	WireTimeInvalid,
	WireNonFinite,
	S1Rejected,
	ApiDomainMismatch,
	WireAttitudeInvalid
};

struct IngressCorrelation {
	IngressPacketKind packet_kind;
	SourceId source;
	SessionId session;
	uint64_t controller_instance_id;
	uint64_t transition_epoch;
	uint64_t map_epoch;
	uint64_t packet_sequence;
	uint64_t acknowledged_status_sequence;
};

struct IngressReport {
	uint64_t report_sequence;
	uint64_t transition_epoch;
	TimeUs validator_time_us;
	IngressReportKind kind;
	IngressCode code;
	bool has_s1_decision;
	Decision s1_decision;
	IngressCorrelation correlation;
	ReportEvidence evidence;
};

struct AdmissionSnapshot {
	bool acknowledgement_current;
	uint64_t acknowledgement_operation_sequence;
	uint64_t acknowledged_status_sequence;
	TimeUs acknowledgement_raw_producer_time_us;
	TimeUs mapped_acknowledgement_time_us;
	TimeUs earliest_acknowledgement_time_us;
	TimeUs latest_acknowledgement_time_us;
	SourceId source;
	SessionId session;
	uint64_t controller_instance_id;
	uint64_t transition_epoch;
	uint64_t map_epoch;
	TrustedClockMap map;
};

struct IngressResult {
	IngressCode code;
	bool admission_closed;
	AdapterSnapshot snapshot;
	SessionSnapshot session;
	AdmissionSnapshot admission;
	bool has_prestate_s1_decision;
	Decision prestate_s1_decision;
	bool has_operation_s1_decision;
	Decision operation_s1_decision;
	uint8_t report_count;
	IngressReport reports[2];
};

#if RESEARCH_SITL_DIAGNOSTIC_CAPABILITY
enum class DiagnosticDomain : uint8_t { Flight, SitlDisarmedDiagnostic };
enum class DiagnosticAction : uint8_t { Enable, Disable };
enum class DiagnosticCloseReason : uint8_t {
	OperatorDetach, OperatorLost
#if !defined(RESEARCH_SITL_DIAGNOSTIC_OFFLINE)
	, OwnerStop, SourceContinuityLost, OwnerEvidenceExhausted, OwnerConfigurationInvalid
#if defined(RESEARCH_C1E_NEGATIVE_OWNER_TARGET) || defined(RESEARCH_C1E_NEGATIVE_CONTRACT_TEST)
	, TransportFault, WorkerResourceExhausted, AdapterCapacityExhausted
#endif
#endif
};
enum class DiagnosticReportKind : uint8_t { PhysicalBarrier, SelectorBarrier, SessionBarrier, Operation, Terminal };
enum class DiagnosticOperationKind : uint8_t { None, Observe, Poll, Access, Enable, Disable, Establish, Revoke, Clear, Acknowledge, Request, LifecycleClose };
using DiagnosticCause = std::variant<DiagnosticLocalReason, DecisionCode, AdminCode, IngressCode>;

#if defined(RESEARCH_SITL_DIAGNOSTIC_OFFLINE)
struct DiagnosticIdentity { uint64_t owner_lifetime_id; uint64_t fixture_run_id; };
struct DiagnosticBinding {
	uint64_t owner_lifetime_id, fixture_run_id, controller_instance_id;
	uint64_t operator_instance, state_generation, transition_epoch, selector_epoch;
	TrustedClockMap prospective_map;
};
struct DiagnosticIngress { uint64_t owner_lifetime_id, fixture_run_id; SessionIngressVerdict authentication; };
using DiagnosticProfileIdentity = DiagnosticIdentity;
using DiagnosticProfileBinding = DiagnosticBinding;
using DiagnosticProfileIngress = DiagnosticIngress;
#else
struct NativeDiagnosticIdentity {
	uint64_t owner_lifetime_id;
	uint64_t native_run_id;
	DiagnosticExecutionOrigin origin{kDiagnosticExecutionOrigin};
};
struct NativeDiagnosticBinding {
	uint64_t owner_lifetime_id, native_run_id, controller_instance_id;
	uint64_t operator_instance, state_generation, transition_epoch, selector_epoch;
	TrustedClockMap prospective_map;
	DiagnosticExecutionOrigin origin{kDiagnosticExecutionOrigin};
};
struct NativeDiagnosticIngress {
	uint64_t owner_lifetime_id, native_run_id;
	SessionIngressVerdict authentication;
	DiagnosticExecutionOrigin origin{kDiagnosticExecutionOrigin};
};
using DiagnosticProfileIdentity = NativeDiagnosticIdentity;
using DiagnosticProfileBinding = NativeDiagnosticBinding;
using DiagnosticProfileIngress = NativeDiagnosticIngress;
#endif
struct DiagnosticSelectionGrant { uint64_t event_id; DiagnosticAction action; DiagnosticProfileBinding expected; };
struct DiagnosticAdminGrant { DiagnosticProfileBinding expected; SupervisorEvent event; };
struct DiagObserve {};
struct DiagPoll {};
struct DiagAccess {};
struct DiagEnable { DiagnosticSelectionGrant grant; };
struct DiagDisable { DiagnosticSelectionGrant grant; };
struct DiagEstablish { DecodedSessionV1 packet; DiagnosticProfileIngress ingress; DiagnosticAdminGrant grant; };
struct DiagRevoke { DecodedSessionV1 packet; DiagnosticProfileIngress ingress; DiagnosticAdminGrant grant; };
struct DiagClear { DiagnosticAdminGrant grant; };
struct DiagAcknowledge { DecodedSessionV1 packet; DiagnosticProfileIngress ingress; };
struct DiagRequest { DecodedRequestV1 packet; DiagnosticProfileIngress ingress; };
using DiagnosticTurn = std::variant<DiagObserve, DiagPoll, DiagAccess, DiagEnable, DiagDisable,
      DiagEstablish, DiagRevoke, DiagClear, DiagAcknowledge, DiagRequest>;

struct DiagnosticSnapshot {
	DiagnosticDomain domain;
	DiagnosticProfileIdentity identity;
	uint64_t controller_instance_id;
	bool configured, initialized, terminal;
	DiagnosticSelectionState selection;
	uint64_t selector_epoch;
	DiagnosticCause selection_reason;
	bool operator_attached;
	uint64_t operator_instance; // Retained lifetime floor, even after detach.
	AdapterSnapshot adapter;
	SessionSnapshot session;
	AdmissionSnapshot admission;
	bool latched;
	DecisionCode first_latched_code;
	uint64_t rejection_count;
	bool has_last_accepted_sequence;
	uint64_t last_accepted_sequence;
	bool last_accepted_authenticated;
};

struct DiagnosticCorrelation {
	DiagnosticOperationKind operation;
	IngressCorrelation ingress;
	AdminCorrelation admin;
	uint64_t operator_instance;
	std::optional<DiagnosticCloseReason> close_reason;
	std::optional<SessionIngressVerdict> authentication;
};

struct DiagnosticReport {
	DiagnosticDomain domain;
	DiagnosticProfileIdentity identity;
	uint64_t report_sequence, transition_epoch, selector_epoch;
	DiagnosticReportKind kind;
	DiagnosticCode code;
	DiagnosticCause cause;
	DiagnosticCorrelation correlation;
	bool state_reference_valid;
	TrustedStateSample physical;
	DiagnosticSnapshot snapshot;
	std::optional<Decision> common_decision;
	bool eligible_for_control;
};

struct DiagnosticResult {
	DiagnosticCode code;
	DiagnosticSnapshot snapshot;
	uint8_t record_count;
	DiagnosticReport records[4];
	bool has_operation_record;
	uint8_t operation_record_index;
	bool grant_consumed, action_performed;
};
#endif

class WireAdapter
{
public:
	WireAdapter(const ContractConfig &configuration, const AdapterIdentity &identity);
#if RESEARCH_SITL_DIAGNOSTIC_CAPABILITY
#if defined(RESEARCH_SITL_DIAGNOSTIC_OFFLINE)
	WireAdapter(const ContractConfig &configuration, const AdapterIdentity &identity,
		    DiagnosticOfflineTag, const DiagnosticIdentity &diagnostic_identity);
#endif
	DiagnosticResult diagnosticTurn(const TrustedStateSample &physical, const DiagnosticTurn &operation) noexcept;
	DiagnosticSnapshot diagnosticSnapshot() const noexcept;
#endif
	WireAdapter(const WireAdapter &) = delete;
	WireAdapter &operator=(const WireAdapter &) = delete;
	WireAdapter(WireAdapter &&) = delete;
	WireAdapter &operator=(WireAdapter &&) = delete;

	// Configuration is immutable after construction; this is not runtime health.
	bool configured() const;

	// The caller serializes calls. Only the ACK-gated accessor may copy a request.
	// Reports are chronological in [0, report_count); snapshot is post-call state.
	AdapterResult observeTrustedState(const TrustedStateSample &sample);
	AdapterResult poll(const TrustedStateSample &sample);
	AdapterResult getEligibleRequest(const TrustedStateSample &sample, CanonicalRequest &request);
	// Full controller boundary: v1 acceleration-only commissioning is insufficient.
	// Same freshness, session, barrier ACK and output-preservation rules as above.
	AdapterResult getEligibleFullRequest(const TrustedStateSample &sample, CanonicalRequest &request);

	// Successful administration closes ACK; rejected administration may preserve it. No transport I/O.
	AdminResult processSession(const TrustedStateSample &sample, const DecodedSessionV1 &packet,
				   const SessionIngressVerdict &ingress, const SupervisorEvent *grant);
	AdminResult clearLatchedFault(const TrustedStateSample &sample, const SupervisorEvent &grant);
	SessionSnapshot sessionSnapshot() const;

	// Offline typed payload admission only: no framing, I/O, or supervisor grant.
	IngressResult processAcknowledgement(const TrustedStateSample &sample, const DecodedSessionV1 &packet,
					     const SessionIngressVerdict &ingress);
	IngressResult processRequest(const TrustedStateSample &sample, const DecodedRequestV1 &packet,
				     const SessionIngressVerdict &ingress);

private:
#if RESEARCH_SITL_DIAGNOSTIC_CAPABILITY
#if defined(RESEARCH_SITL_DIAGNOSTIC_OFFLINE)
	friend class research_sitl::HarnessSupervisorOwner;
	friend struct DiagnosticAdapterTestPeer;
#else
	friend class research_sitl::NativeClosedDiagnosticOwner;
#if defined(RESEARCH_C1E_NEGATIVE_OWNER_TARGET) || defined(RESEARCH_C1E_NEGATIVE_CONTRACT_TEST)
	friend class research_sitl::NativeIngressDiagnosticOwner;
#endif
	WireAdapter(const ContractConfig &configuration, const AdapterIdentity &identity,
		    DiagnosticNativeSitlTag, const NativeDiagnosticIdentity &diagnostic_identity);
#endif
	struct DiagnosticConstructionTag {};
	WireAdapter(const ContractConfig &configuration, const AdapterIdentity &identity,
		    DiagnosticConstructionTag, const DiagnosticProfileIdentity &diagnostic_identity);
	bool attachDiagnosticOperator(uint64_t operator_instance) noexcept;
	DiagnosticResult closeDiagnosticLifecycle(const std::optional<TrustedStateSample> &receipt, DiagnosticCloseReason reason) noexcept;
	DiagnosticResult processDiagnostic(const TrustedStateSample *physical, const DiagnosticTurn *operation,
					   const std::optional<DiagnosticCloseReason> &close_reason) noexcept;
	DiagnosticResult diagnosticInitialResult(DiagnosticCode code) const;
	DiagnosticResult diagnosticTerminal(DiagnosticResult result, const DiagnosticCorrelation &correlation,
					    DiagnosticCause cause, bool force_sentinel);
	void appendDiagnosticReport(DiagnosticResult &result, DiagnosticReportKind kind, DiagnosticCode code,
				    DiagnosticCause cause, const DiagnosticCorrelation &correlation, const std::optional<Decision> &decision = std::nullopt);
	void changeDiagnosticSelection(DiagnosticResult &result, DiagnosticSelectionState selection, DiagnosticCause cause,
				       const DiagnosticCorrelation &correlation);
	bool matchesDiagnosticBinding(const DiagnosticProfileBinding &binding) const;
	bool diagnosticIngressIdentity(const DiagnosticProfileIngress &ingress) const;
	void observeDiagnosticGuard(const DiagnosticGuardResult &result);
#if defined(RESEARCH_C1E_NEGATIVE_OWNER_TARGET) || defined(RESEARCH_C1E_NEGATIVE_CONTRACT_TEST)
	bool nativeNegativeTwoTurnCapacity() const noexcept;
#endif
#endif
#if !defined(RESEARCH_SITL_DIAGNOSTIC_NATIVE)
	friend struct WireAdapterStateTestPeer;
	friend struct WireAdapterSessionTestPeer;
	friend struct WireAdapterAdmissionTestPeer;
#endif

	enum class OperationKind : uint8_t { Observation, Poll, Access };

	AdapterResult process(const TrustedStateSample &sample, OperationKind operation, CanonicalRequest *request = nullptr,
			      bool require_full_command = false);
	bool structurallyValid(const TrustedStateSample &sample) const;
	bool matchesRetainedState(const TrustedStateSample &sample) const;
	bool hasNormalReportCapacity(uint8_t count) const;
	AdapterResult terminalResult(AdapterCode code, TimeUs validator_time_us, bool allow_normal_report);
	AdapterResult initialResult(AdapterCode code) const;
	AdminResult processAdmin(const TrustedStateSample &sample, const DecodedSessionV1 *packet,
				 const SessionIngressVerdict *ingress, const SupervisorEvent *grant);
	AdminResult adminInitialResult(AdminCode code, const AdminCorrelation &correlation) const;
	AdminResult adminTerminalResult(AdminCode code, TimeUs validator_time_us,
					const AdminCorrelation &correlation, bool allow_normal_report);
	IngressResult processIngress(const TrustedStateSample &sample, IngressPacketKind kind,
				     const DecodedSessionV1 *acknowledgement, const DecodedRequestV1 *request,
				     const SessionIngressVerdict &ingress);
	IngressResult ingressInitialResult(IngressCode code) const;
	IngressResult ingressTerminalResult(IngressCode code, TimeUs validator_time_us,
					    const IngressCorrelation &correlation, bool allow_normal_report);
	bool acknowledgementCurrent() const;
	IngressCode admitAcknowledgement(const TrustedStateSample &sample, const DecodedSessionV1 &packet,
					 const SessionIngressVerdict &ingress);
	IngressCode prepareRequest(const TrustedStateSample &sample, const DecodedRequestV1 &packet,
				   const SessionIngressVerdict &ingress, CanonicalRequest &canonical) const;
	void clearAcknowledgement();
	void observeDecisionEvidence(const Decision &decision);
	ReportEvidence captureEvidence(const TrustedStateSample *sample, bool state_reference_valid,
				       StatusDecisionOrigin origin, uint16_t local_wire_decision_code,
				       const Decision *operation_decision, const SessionIngressVerdict *ingress) const;
	uint16_t adapterLocalWireCode(AdapterCode code) const;
	uint16_t adminLocalWireCode(AdminCode code, const TrustedStateSample &sample,
				    const DecodedSessionV1 *packet, const SupervisorEvent *grant,
				    const SessionIngressVerdict *ingress) const;
	uint16_t ingressLocalWireCode(IngressCode code, const TrustedStateSample &sample,
				      const DecodedSessionV1 *acknowledgement, const DecodedRequestV1 *request,
				      const SessionIngressVerdict &ingress) const;

	const ContractConfig configuration_;
	RequestGuard guard_;
	const AdapterIdentity identity_;
	const bool configured_;
#if RESEARCH_SITL_DIAGNOSTIC_CAPABILITY
	const bool diagnostic_domain_ {false};
	const DiagnosticProfileIdentity diagnostic_identity_{};
	DiagnosticSelectionState diagnostic_selection_{DiagnosticSelectionState::Disabled};
	uint64_t diagnostic_selector_epoch_{0};
	DiagnosticCause diagnostic_selection_reason_{DiagnosticLocalReason::None};
	bool diagnostic_operator_attached_{false};
	uint64_t diagnostic_operator_instance_{0};
#endif
	AdapterSnapshot snapshot_ {};
	TrustedStateSample last_sample_{};
	AdapterCode terminal_code_{AdapterCode::TrustedStateFault};
	SessionSnapshot session_snapshot_{};
	AdminCode admin_terminal_code_{AdminCode::TrustedStateFault};
	AdmissionSnapshot admission_snapshot_{};
	bool has_diagnostic_decision_{false};
	bool diagnostic_latched_{false};
	DecisionCode diagnostic_first_latched_code_{DecisionCode::Accepted};
	uint64_t diagnostic_rejection_count_{0};
	bool has_last_accepted_sequence_{false};
	uint64_t last_accepted_sequence_{0};
	bool last_accepted_authenticated_{false};
};

} // namespace research_acceleration
