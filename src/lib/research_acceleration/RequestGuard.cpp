#include "RequestGuard.hpp"

#include <cmath>
#include <limits>

namespace research_acceleration
{
namespace
{

bool finite(const Vec3f &value)
{
	return std::isfinite(value(0)) && std::isfinite(value(1)) && std::isfinite(value(2));
}

bool add(TimeUs left, TimeUs right, TimeUs &result)
{
	if (left > std::numeric_limits<TimeUs>::max() - right) {
		return false;
	}

	result = left + right;
	return true;
}

bool subtract(TimeUs left, TimeUs right, TimeUs &result)
{
	if (left < right) {
		return false;
	}

	result = left - right;
	return true;
}

bool validConfig(const ContractConfig &config)
{
	if (config.protocol_version == 0 || config.permitted_source == 0
	    || (config.disarmed_policy != DisarmedPolicy::Reject && config.disarmed_policy != DisarmedPolicy::ValidateOnly)
	    || !finite(config.acceleration_bounds.minimum_m_s2) || !finite(config.acceleration_bounds.maximum_m_s2)
	    || !std::isfinite(config.acceleration_bounds.maximum_norm_m_s2)
	    || config.acceleration_bounds.maximum_norm_m_s2 <= 0.0F
	    || !std::isfinite(config.quaternion_norm_tolerance) || config.quaternion_norm_tolerance <= 0.0F
	    || config.quaternion_norm_tolerance >= 1.0F) {
		return false;
	}

	for (int index = 0; index < 3; ++index) {
		if (config.acceleration_bounds.minimum_m_s2(index) > config.acceleration_bounds.maximum_m_s2(index)) {
			return false;
		}
	}

	double closest_norm_squared = 0.0;

	for (int index = 0; index < 3; ++index) {
		const float minimum = config.acceleration_bounds.minimum_m_s2(index);
		const float maximum = config.acceleration_bounds.maximum_m_s2(index);
		const double closest = minimum > 0.0F ? minimum : (maximum < 0.0F ? maximum : 0.0F);
		closest_norm_squared += closest * closest;
	}

	if (!std::isfinite(closest_norm_squared)
	    || std::sqrt(closest_norm_squared) > static_cast<double>(config.acceleration_bounds.maximum_norm_m_s2)) {
		return false;
	}

	const TimingRequirements &timing = config.timing;
	return timing.minimum_validity_us != 0 && timing.maximum_validity_us != 0
	       && timing.maximum_command_age_us != 0 && timing.maximum_future_skew_us != 0
	       && timing.maximum_clock_uncertainty_us != 0
	       && timing.minimum_validity_us <= timing.maximum_validity_us;
}

} // namespace

RequestGuard::RequestGuard(const ContractConfig &config) : config_(config), configured_(validConfig(config)) {}

#if RESEARCH_SITL_DIAGNOSTIC_CAPABILITY
RequestGuard::RequestGuard(const ContractConfig &config, DiagnosticConstructionTag) :
	diagnostic_domain_(true), config_(config),
	configured_(validConfig(config) && config.disarmed_policy == DisarmedPolicy::ValidateOnly) {}
#if defined(RESEARCH_SITL_DIAGNOSTIC_OFFLINE)
RequestGuard::RequestGuard(const ContractConfig &config, DiagnosticOfflineTag) :
	RequestGuard(config, DiagnosticConstructionTag{}) {}
#endif
#endif

bool RequestGuard::configured() const { return configured_; }

Decision RequestGuard::decision(DecisionCode code, bool syntactically_valid, bool conservative_age_valid,
				TimeUs conservative_age_us) const
{
	bool eligible = code == DecisionCode::Accepted && has_active_request_;
#if RESEARCH_SITL_DIAGNOSTIC_CAPABILITY
	eligible = eligible && !diagnostic_domain_;
#endif
	return {code, syntactically_valid, eligible, latched_,
		first_latched_code_, conservative_age_valid, conservative_age_us, rejection_count_
	       };
}

Decision RequestGuard::reject(DecisionCode code, bool syntactically_valid, bool conservative_age_valid,
			      TimeUs conservative_age_us)
{
	if (rejection_count_ != std::numeric_limits<uint64_t>::max()) {
		++rejection_count_;
	}

	return decision(code, syntactically_valid, conservative_age_valid, conservative_age_us);
}

Decision RequestGuard::latch(DecisionCode cause)
{
	has_active_request_ = false;

	if (!latched_) {
		latched_ = true;
		first_latched_code_ = cause;
	}

	return reject(cause);
}

bool RequestGuard::observeValidatorTime(TimeUs time_us)
{
	if (has_validator_time_ && time_us < validator_time_floor_) {
		latch(DecisionCode::ValidatorTimeRegressed);
		return false;
	}

	has_validator_time_ = true;
	validator_time_floor_ = time_us;
	return true;
}

Decision RequestGuard::contextPrerequisiteDecision(const EvaluationContext &context)
{
	// Observing any non-armed state is a revocation boundary, even if a later
	// packet is malformed. A request cannot survive a disarm/rearm transition.
	if (context.arm_state != ArmState::Armed) {
		has_active_request_ = false;
	}

	if (!context.clock_mapping_valid) {
		has_active_request_ = false;
		return reject(DecisionCode::ClockMappingInvalid);
	}

	if (!context.transport_connected) {
		has_active_request_ = false;
		return reject(DecisionCode::Disconnected);
	}

	if (!context.required_state_valid) {
		has_active_request_ = false;
		return reject(DecisionCode::RequiredStateInvalid);
	}

	if (context.arm_state != ArmState::Disarmed && context.arm_state != ArmState::Armed
	    && context.arm_state != ArmState::Unknown) {
		has_active_request_ = false;
		return reject(DecisionCode::ArmStateUnknown);
	}

	if (context.arm_state == ArmState::Unknown) {
		return reject(DecisionCode::ArmStateUnknown);
	}

	if (context.mode_state != ResearchModeState::NotSelected && context.mode_state != ResearchModeState::Selected
	    && context.mode_state != ResearchModeState::Unknown) {
		has_active_request_ = false;
		return reject(DecisionCode::ModeStateUnknown);
	}

	if (context.mode_state == ResearchModeState::Unknown) {
		has_active_request_ = false;
		return reject(DecisionCode::ModeStateUnknown);
	}

	return decision(DecisionCode::Accepted);
}

Decision RequestGuard::contextDecision(const EvaluationContext &context, bool for_request)
{
	const Decision prerequisites = contextPrerequisiteDecision(context);

	if (prerequisites.code != DecisionCode::Accepted) {
		return prerequisites;
	}

	if (context.mode_state != ResearchModeState::Selected) {
		has_active_request_ = false;
		return reject(DecisionCode::ModeNotSelected);
	}

	if (context.arm_state == ArmState::Disarmed && config_.disarmed_policy == DisarmedPolicy::Reject) {
		has_active_request_ = false;
		return reject(DecisionCode::DisarmedRejected);
	}

	if (!for_request && context.arm_state == ArmState::Disarmed) {
		has_active_request_ = false;
		return reject(config_.disarmed_policy == DisarmedPolicy::ValidateOnly ? DecisionCode::ValidatedDisarmed : DecisionCode::DisarmedRejected);
	}

	return decision(DecisionCode::Accepted);
}

Decision RequestGuard::establishTrustedSession(const TrustedSession &session, bool supervisor_authorised,
		const EvaluationContext &context)
{
#if RESEARCH_SITL_DIAGNOSTIC_CAPABILITY

	if (diagnostic_domain_) {
		return decision(DecisionCode::ApiDomainMismatch);
	}

#endif

	if (!configured_) {
		return decision(DecisionCode::NotConfigured);
	}

	// Administration observes the same expiry/revocation boundary as access.
	// Its result is intentionally not an authorization decision by itself.
	const uint64_t rejection_count_before_poll = rejection_count_;
	const Decision freshness = poll(context);

	if (latched_) {
		return freshness;
	}

	// A normal supervisor establishment is not a rejected request. Context and
	// expiry are still observed, but their poll bookkeeping is administrative.
	rejection_count_ = rejection_count_before_poll;
	const Decision checks = establishSessionChecks(session, supervisor_authorised, context.arm_state);
	return checks.code == DecisionCode::Accepted ? commitSession(session) : checks;
}

Decision RequestGuard::establishSessionChecks(const TrustedSession &session, bool supervisor_authorised,
		ArmState arm_state)
{
	if (!supervisor_authorised) {
		return reject(DecisionCode::UnauthorizedSessionEstablishment);
	}

	if (session.owner != config_.permitted_source || session.session == 0) {
		return reject(DecisionCode::InvalidTrustedSession);
	}

	if (arm_state != ArmState::Disarmed && arm_state != ArmState::Armed
	    && arm_state != ArmState::Unknown) {
		return reject(DecisionCode::ArmStateUnknown);
	}

	if (arm_state == ArmState::Unknown) {
		return reject(DecisionCode::ArmStateUnknown);
	}

	if (arm_state != ArmState::Disarmed) {
		return reject(DecisionCode::SessionChangeWhileArmed);
	}

	if (trusted_session_established_ && session.session == trusted_session_.session) {
		return reject(DecisionCode::WrongSession);
	}

	return decision(DecisionCode::Accepted);
}

Decision RequestGuard::commitSession(const TrustedSession &session)
{
	trusted_session_ = session;
	trusted_session_established_ = true;
	session_establishment_required_ = false;
	has_sequence_floor_ = false;
	has_producer_time_floor_ = false;
	has_active_request_ = false;
	return decision(DecisionCode::Accepted);
}

Decision RequestGuard::evaluate(const CanonicalRequest &request, const EvaluationContext &context)
{
#if RESEARCH_SITL_DIAGNOSTIC_CAPABILITY

	if (diagnostic_domain_) {
		return decision(DecisionCode::ApiDomainMismatch);
	}

#endif

	if (!configured_) {
		return decision(DecisionCode::NotConfigured);
	}

	if (!observeValidatorTime(context.validator_receive_time_us)) {
		return decision(DecisionCode::ValidatorTimeRegressed);
	}

	if (latched_) {
		return reject(DecisionCode::Latched);
	}

	if (!trusted_session_established_ || session_establishment_required_) {
		return reject(session_establishment_required_ ? DecisionCode::SessionEstablishmentRequired : DecisionCode::NoSession);
	}

	const Decision context_decision = contextDecision(context, true);

	if (context_decision.code != DecisionCode::Accepted) {
		return context_decision;
	}

	const Decision payload = evaluatePayload(request, context);

	if (payload.code == DecisionCode::Accepted) {
		active_request_ = request;
		has_active_request_ = true;
		return decision(DecisionCode::Accepted, true, true, payload.conservative_age_us);
	}

	return payload;
}

Decision RequestGuard::evaluatePayload(const CanonicalRequest &request, const EvaluationContext &context)
{
	if (request.protocol_version != config_.protocol_version) {
		return reject(DecisionCode::WrongProtocolVersion);
	}

	if (request.source != config_.permitted_source) {
		return reject(DecisionCode::WrongSource);
	}

	if (request.session == 0) {
		return reject(DecisionCode::NoSession);
	}

	if (request.session != trusted_session_.session) {
		return reject(DecisionCode::WrongSession);
	}

	if (request.frame != Frame::Frd) {
		return reject(DecisionCode::WrongFrame);
	}

	if (request.quantity != Quantity::InertialAcceleration) {
		return reject(DecisionCode::WrongQuantity);
	}

	if (request.reference_point != ReferencePoint::Cg) {
		return reject(DecisionCode::WrongReferencePoint);
	}

	if (!finite(request.acceleration_frd_m_s2)) {
		return reject(DecisionCode::NonFinite);
	}

	for (int index = 0; index < 3; ++index) {
		if (request.acceleration_frd_m_s2(index) < config_.acceleration_bounds.minimum_m_s2(index)
		    || request.acceleration_frd_m_s2(index) > config_.acceleration_bounds.maximum_m_s2(index)) {
			return reject(DecisionCode::AxisOutOfBounds);
		}
	}

	const double norm_squared = static_cast<double>(request.acceleration_frd_m_s2(0)) * static_cast<double>(request.acceleration_frd_m_s2(0))
				    + static_cast<double>(request.acceleration_frd_m_s2(1)) * static_cast<double>(request.acceleration_frd_m_s2(1))
				    + static_cast<double>(request.acceleration_frd_m_s2(2)) * static_cast<double>(request.acceleration_frd_m_s2(2));

	if (!std::isfinite(norm_squared) || std::sqrt(norm_squared) > static_cast<double>(config_.acceleration_bounds.maximum_norm_m_s2)) {
		return reject(DecisionCode::NormOutOfBounds);
	}

	TimeUs raw_duration{};
	TimeUs mapped_duration{};

	if (!subtract(request.producer_expiry_monotonic_us, request.producer_monotonic_us, raw_duration)
	    || !subtract(request.expiry_time_in_validator_domain_us, request.producer_time_in_validator_domain_us, mapped_duration)
	    || raw_duration != mapped_duration) {
		return reject(DecisionCode::TimeArithmeticInvalid);
	}

	if (raw_duration < config_.timing.minimum_validity_us || raw_duration > config_.timing.maximum_validity_us) {
		return reject(DecisionCode::ValidityOutOfRange);
	}

	if (request.clock_mapping_uncertainty_us > config_.timing.maximum_clock_uncertainty_us) {
		return reject(DecisionCode::ClockUncertaintyExceeded);
	}

	if (has_sequence_floor_ && request.sequence <= sequence_floor_) {
		return reject(request.sequence == sequence_floor_ ? DecisionCode::DuplicateSequence : DecisionCode::OutOfOrderSequence);
	}

	TimeUs earliest_creation{};
	TimeUs earliest_expiry{};
	TimeUs latest_creation{};
	TimeUs future_limit{};

	if (!subtract(request.producer_time_in_validator_domain_us, request.clock_mapping_uncertainty_us, earliest_creation)
	    || !subtract(request.expiry_time_in_validator_domain_us, request.clock_mapping_uncertainty_us, earliest_expiry)
	    || !add(request.producer_time_in_validator_domain_us, request.clock_mapping_uncertainty_us, latest_creation)
	    || !add(context.validator_receive_time_us, config_.timing.maximum_future_skew_us, future_limit)) {
		return reject(DecisionCode::TimeArithmeticInvalid);
	}

	if (earliest_creation > context.validator_receive_time_us || latest_creation > future_limit) {
		return reject(DecisionCode::ProducerTimeInFuture);
	}

	TimeUs conservative_age{};

	if (!subtract(context.validator_receive_time_us, earliest_creation, conservative_age)) {
		return reject(DecisionCode::TimeArithmeticInvalid);
	}

	if (conservative_age > config_.timing.maximum_command_age_us) {
		return reject(DecisionCode::RequestTooOld, false, true, conservative_age);
	}

	if (earliest_expiry <= context.validator_receive_time_us) {
		return reject(DecisionCode::RequestExpired, false, true, conservative_age);
	}

	if (has_producer_time_floor_) {
		if (request.producer_monotonic_us < producer_time_floor_) {
			return latch(DecisionCode::ProducerTimeRegressed);
		}

		if (request.producer_monotonic_us == producer_time_floor_) {
			return reject(DecisionCode::ProducerTimeNotIncreasing);
		}
	}

	has_sequence_floor_ = true;
	sequence_floor_ = request.sequence;
	has_producer_time_floor_ = true;
	producer_time_floor_ = request.producer_monotonic_us;

	if (context.arm_state == ArmState::Disarmed) {
		has_active_request_ = false;
		return decision(DecisionCode::ValidatedDisarmed, true, true, conservative_age);
	}

	return decision(DecisionCode::Accepted, true, true, conservative_age);
}

Decision RequestGuard::poll(const EvaluationContext &context)
{
#if RESEARCH_SITL_DIAGNOSTIC_CAPABILITY

	if (diagnostic_domain_) {
		return decision(DecisionCode::ApiDomainMismatch);
	}

#endif

	if (!configured_) {
		return decision(DecisionCode::NotConfigured);
	}

	if (!observeValidatorTime(context.validator_receive_time_us)) {
		return decision(DecisionCode::ValidatorTimeRegressed);
	}

	if (latched_) {
		return reject(DecisionCode::Latched);
	}

	const Decision context_decision = contextDecision(context, false);

	if (context_decision.code != DecisionCode::Accepted) {
		return context_decision;
	}

	if (!has_active_request_) {
		return decision(DecisionCode::NoEligibleRequest);
	}

	TimeUs earliest_expiry{};

	if (!subtract(active_request_.expiry_time_in_validator_domain_us,
		      active_request_.clock_mapping_uncertainty_us, earliest_expiry)) {
		has_active_request_ = false;
		return reject(DecisionCode::TimeArithmeticInvalid);
	}

	if (earliest_expiry <= context.validator_receive_time_us) {
		has_active_request_ = false;
		return reject(DecisionCode::RequestExpired);
	}

	TimeUs earliest_creation{};
	TimeUs age{};

	if (!subtract(active_request_.producer_time_in_validator_domain_us,
		      active_request_.clock_mapping_uncertainty_us, earliest_creation)
	    || earliest_creation > context.validator_receive_time_us
	    || !subtract(context.validator_receive_time_us, earliest_creation, age)) {
		has_active_request_ = false;
		return reject(DecisionCode::TimeArithmeticInvalid);
	}

	if (age > config_.timing.maximum_command_age_us) {
		has_active_request_ = false;
		return reject(DecisionCode::RequestTooOld, false, true, age);
	}

	return decision(DecisionCode::Accepted, true, true, age);
}

Decision RequestGuard::clearLatch(bool supervisor_authorised, ArmState arm_state)
{
#if RESEARCH_SITL_DIAGNOSTIC_CAPABILITY

	if (diagnostic_domain_) {
		return decision(DecisionCode::ApiDomainMismatch);
	}

#endif
	return clearLatchImpl(supervisor_authorised, arm_state);
}

Decision RequestGuard::clearLatchImpl(bool supervisor_authorised, ArmState arm_state)
{
	if (!configured_) {
		return decision(DecisionCode::NotConfigured);
	}

	if (!supervisor_authorised) {
		return reject(DecisionCode::UnauthorizedSessionEstablishment);
	}

	if (arm_state != ArmState::Disarmed && arm_state != ArmState::Armed && arm_state != ArmState::Unknown) {
		return reject(DecisionCode::ArmStateUnknown);
	}

	if (arm_state == ArmState::Unknown) {
		return reject(DecisionCode::ArmStateUnknown);
	}

	if (arm_state != ArmState::Disarmed) {
		return reject(DecisionCode::SessionChangeWhileArmed);
	}

	latched_ = false;
	first_latched_code_ = DecisionCode::Accepted;
	has_active_request_ = false;
	// This is the sole authorized clock-epoch reset. A different trusted
	// session must still be established before requests can resume.
	has_validator_time_ = false;
	validator_time_floor_ = 0;
	session_establishment_required_ = true;
	return decision(DecisionCode::Accepted);
}

void RequestGuard::revoke()
{
#if RESEARCH_SITL_DIAGNOSTIC_CAPABILITY

	if (diagnostic_domain_) {
		return;
	}

#endif
	has_active_request_ = false;
}

bool RequestGuard::getEligibleRequest(const EvaluationContext &context, CanonicalRequest &request, Decision &decision_out)
{
	decision_out = poll(context);

	if (decision_out.code != DecisionCode::Accepted || !decision_out.eligible_for_control) {
		return false;
	}

	request = active_request_;
	return true;
}

} // namespace research_acceleration
