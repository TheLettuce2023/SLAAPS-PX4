#pragma once

#include "DiagnosticBuildProfile.hpp"
#include "FrameMath.hpp"

#include <cstdint>

#if RESEARCH_SITL_DIAGNOSTIC_CAPABILITY
#include "DiagnosticTypes.hpp"
#include <optional>
#include <variant>
#endif

namespace research_acceleration
{

using TimeUs = uint64_t;
using SourceId = uint64_t;
using SessionId = uint64_t;

enum class ArmState : uint8_t { Disarmed, Armed, Unknown };
enum class ResearchModeState : uint8_t { NotSelected, Selected, Unknown };
enum class DisarmedPolicy : uint8_t { Reject, ValidateOnly };
enum class Frame : uint8_t { Frd };
enum class Quantity : uint8_t { InertialAcceleration };
enum class ReferencePoint : uint8_t { Cg };

enum class DecisionCode : uint16_t {
	Accepted, ValidatedDisarmed, NotConfigured, WrongProtocolVersion, WrongSource, NoSession,
	WrongSession, UnauthorizedSessionEstablishment, SessionChangeWhileArmed,
	InvalidTrustedSession, SessionEstablishmentRequired, DuplicateSequence, OutOfOrderSequence,
	ProducerTimeNotIncreasing, ClockMappingInvalid, ClockUncertaintyExceeded,
	ProducerTimeInFuture, ProducerTimeRegressed, ValidatorTimeRegressed, RequestTooOld,
	ValidityOutOfRange, RequestExpired, TimeArithmeticInvalid, RequiredStateInvalid, WrongFrame,
	WrongQuantity, WrongReferencePoint, NonFinite, AxisOutOfBounds, NormOutOfBounds,
	DisarmedRejected, ModeNotSelected, ArmStateUnknown, ModeStateUnknown, Disconnected, NoEligibleRequest, Latched,
	ApiDomainMismatch // Local-only; append without changing existing wire ordinals.
};

struct AccelerationBounds {
	Vec3f minimum_m_s2;
	Vec3f maximum_m_s2;
	float maximum_norm_m_s2;
};

struct TimingRequirements {
	TimeUs minimum_validity_us;
	TimeUs maximum_validity_us;
	TimeUs maximum_command_age_us;
	TimeUs maximum_future_skew_us;
	TimeUs maximum_clock_uncertainty_us;
};

struct ContractConfig {
	uint32_t protocol_version;
	SourceId permitted_source;
	DisarmedPolicy disarmed_policy;
	AccelerationBounds acceleration_bounds;
	TimingRequirements timing;
	float quaternion_norm_tolerance;
};

struct TrustedSession { SourceId owner; SessionId session; };

struct CanonicalRequest {
	uint32_t protocol_version;
	SourceId source;
	SessionId session;
	uint64_t sequence;
	TimeUs producer_monotonic_us;
	TimeUs producer_time_in_validator_domain_us;
	TimeUs producer_expiry_monotonic_us;
	TimeUs expiry_time_in_validator_domain_us;
	// Symmetric mapping uncertainty applies to both mapped creation and expiry.
	TimeUs clock_mapping_uncertainty_us;
	Frame frame;
	Quantity quantity;
	ReferencePoint reference_point;
	Vec3f acceleration_frd_m_s2;
};

struct EvaluationContext {
	TimeUs validator_receive_time_us;
	ArmState arm_state;
	ResearchModeState mode_state;
	bool clock_mapping_valid;
	bool transport_connected;
	bool required_state_valid;
};

struct Decision {
	DecisionCode code;
	bool syntactically_valid;
	// True only when this call's result is an eligible accepted request/poll.
	// It does not describe whether a prior cached request survived a rejection.
	bool eligible_for_control;
	bool latched;
	DecisionCode first_latched_code;
	bool conservative_age_valid;
	TimeUs conservative_age_us;
	uint64_t rejection_count;
};

#if RESEARCH_SITL_DIAGNOSTIC_CAPABILITY
struct DiagnosticGuardResult {
	DiagnosticGuardOperation operation;
	DiagnosticCode code;
	std::variant<DiagnosticLocalReason, DecisionCode> cause;
	std::optional<Decision> common_decision;
};
#endif

class RequestGuard
{
public:
	// This stateful guard is not thread-safe; callers serialize administration,
	// validation, polling, and access for a single validator time oracle.
	explicit RequestGuard(const ContractConfig &config);
	bool configured() const;
	RequestGuard(const RequestGuard &) = delete;
	RequestGuard &operator=(const RequestGuard &) = delete;

	Decision establishTrustedSession(const TrustedSession &session, bool supervisor_authorised,
					 const EvaluationContext &context);
	Decision evaluate(const CanonicalRequest &request, const EvaluationContext &context);
	Decision poll(const EvaluationContext &context);
	Decision clearLatch(bool supervisor_authorised, ArmState arm_state);
	void revoke();
	bool getEligibleRequest(const EvaluationContext &context, CanonicalRequest &request, Decision &decision);

private:
#if RESEARCH_SITL_DIAGNOSTIC_CAPABILITY
	friend class WireAdapter;
	struct DiagnosticConstructionTag {};
	RequestGuard(const ContractConfig &config, DiagnosticConstructionTag);
#if defined(RESEARCH_SITL_DIAGNOSTIC_OFFLINE)
	friend struct DiagnosticGuardTestPeer;
	RequestGuard(const ContractConfig &config, DiagnosticOfflineTag);
#endif
	DiagnosticGuardResult evaluateDiagnostic(const CanonicalRequest &request, const EvaluationContext &context,
			DiagnosticSelectionState selection);
	DiagnosticGuardResult pollDiagnostic(const EvaluationContext &context, DiagnosticSelectionState selection);
	DiagnosticGuardResult establishDiagnosticSession(const TrustedSession &session, bool authorized,
			const EvaluationContext &context, DiagnosticSelectionState selection);
	DiagnosticGuardResult clearDiagnosticLatch(bool authorized, ArmState arm_state);
	void invalidateDiagnosticSession();
	DiagnosticGuardResult diagnosticContext(const EvaluationContext &context, DiagnosticSelectionState selection,
						DiagnosticGuardOperation operation);
	DiagnosticGuardResult diagnosticCommon(DiagnosticGuardOperation operation, DiagnosticCode success,
					       const Decision &result) const;
	DiagnosticGuardResult diagnosticLocal(DiagnosticGuardOperation operation, DiagnosticLocalReason reason);
	const bool diagnostic_domain_{false};
#endif
	Decision contextPrerequisiteDecision(const EvaluationContext &context);
	Decision establishSessionChecks(const TrustedSession &session, bool authorized, ArmState arm_state);
	Decision commitSession(const TrustedSession &session);
	Decision evaluatePayload(const CanonicalRequest &request, const EvaluationContext &context);
	Decision clearLatchImpl(bool authorized, ArmState arm_state);
	Decision decision(DecisionCode code, bool syntactically_valid = false,
			  bool conservative_age_valid = false, TimeUs conservative_age_us = 0) const;
	Decision reject(DecisionCode code, bool syntactically_valid = false,
			bool conservative_age_valid = false, TimeUs conservative_age_us = 0);
	Decision latch(DecisionCode cause);
	bool observeValidatorTime(TimeUs time_us);
	Decision contextDecision(const EvaluationContext &context, bool for_request);

	const ContractConfig config_;
	const bool configured_;
	bool trusted_session_established_{false};
	bool session_establishment_required_{false};
	TrustedSession trusted_session_{};
	bool has_sequence_floor_{false};
	uint64_t sequence_floor_{0};
	bool has_producer_time_floor_{false};
	TimeUs producer_time_floor_{0};
	bool has_validator_time_{false};
	TimeUs validator_time_floor_{0};
	bool has_active_request_{false};
	CanonicalRequest active_request_{};
	bool latched_{false};
	DecisionCode first_latched_code_{DecisionCode::Accepted};
	uint64_t rejection_count_{0};
};

} // namespace research_acceleration
