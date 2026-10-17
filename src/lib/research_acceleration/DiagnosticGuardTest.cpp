#include "RequestGuard.hpp"
#include "WireStatus.hpp"

#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <tuple>
#include <type_traits>

namespace research_acceleration
{

// Offline tests alone can reach these private seams. This peer never supplies
// a runtime source of selection, physical state, authentication or authority.
struct DiagnosticGuardTestPeer {
	static RequestGuard create(const ContractConfig &config) { return RequestGuard(config, DiagnosticOfflineTag{}); }
	static DiagnosticGuardResult evaluate(RequestGuard &guard, const CanonicalRequest &request,
					      const EvaluationContext &context, DiagnosticSelectionState selector)
	{
		return guard.evaluateDiagnostic(request, context, selector);
	}
	static DiagnosticGuardResult poll(RequestGuard &guard, const EvaluationContext &context,
					  DiagnosticSelectionState selector)
	{
		return guard.pollDiagnostic(context, selector);
	}
	static DiagnosticGuardResult establish(RequestGuard &guard, const TrustedSession &session, bool authorized,
					       const EvaluationContext &context, DiagnosticSelectionState selector)
	{
		return guard.establishDiagnosticSession(session, authorized, context, selector);
	}
	static DiagnosticGuardResult clear(RequestGuard &guard, bool authorized, ArmState arm)
	{
		return guard.clearDiagnosticLatch(authorized, arm);
	}
	static void invalidate(RequestGuard &guard) { guard.invalidateDiagnosticSession(); }
	static bool active(const RequestGuard &guard) { return guard.has_active_request_; }
	static bool latched(const RequestGuard &guard) { return guard.latched_; }
	static bool sessionRequired(const RequestGuard &guard) { return guard.session_establishment_required_; }
	static uint64_t rejections(const RequestGuard &guard) { return guard.rejection_count_; }
	static auto floors(const RequestGuard &guard)
	{
		return std::make_tuple(guard.has_sequence_floor_, guard.sequence_floor_,
				       guard.has_producer_time_floor_, guard.producer_time_floor_);
	}
	static auto clock(const RequestGuard &guard)
	{
		return std::make_tuple(guard.has_validator_time_, guard.validator_time_floor_);
	}
	static auto cache(const RequestGuard &guard)
	{
		std::array<unsigned char, sizeof(CanonicalRequest)> bytes{};
		std::memcpy(bytes.data(), &guard.active_request_, bytes.size());
		return bytes;
	}
	static auto snapshot(const RequestGuard &guard)
	{
		return std::make_tuple(guard.trusted_session_established_, guard.session_establishment_required_,
				       guard.trusted_session_.owner, guard.trusted_session_.session, floors(guard), clock(guard),
				       guard.has_active_request_, cache(guard), guard.latched_, guard.first_latched_code_, guard.rejection_count_);
	}
};

} // namespace research_acceleration

using namespace research_acceleration;

namespace
{

using Peer = DiagnosticGuardTestPeer;
constexpr auto selected = DiagnosticSelectionState::SelectedForValidation;
constexpr auto disabled = DiagnosticSelectionState::Disabled;
constexpr TimeUs maximum_time = std::numeric_limits<TimeUs>::max();
size_t checks = 0;
size_t failures = 0;
size_t request_variants = 0;

void check(bool condition, const std::string &name)
{
	++checks;

	if (!condition) {
		++failures;
		std::cerr << "FAILED: " << name << '\n';
	}
}

// All values are synthetic software fixtures from the contract/matrix. They
// are not a vehicle envelope, measured timing, clock qualification or signing.
ContractConfig config()
{
	return {1, 7, DisarmedPolicy::ValidateOnly,
		{Vec3f(-10.F, -10.F, -10.F), Vec3f(10.F, 10.F, 10.F), 10.F}, {20, 200, 50, 10, 2}, 0.001F};
}

EvaluationContext context(TimeUs now = 10010)
{
	return {now, ArmState::Disarmed, ResearchModeState::NotSelected, true, true, true};
}

CanonicalRequest request(uint64_t sequence = 1, TimeUs raw = 9000, TimeUs mapped = 10000,
			 TimeUs duration = 100, TimeUs uncertainty = 2)
{
	return {1, 7, 100, sequence, raw, mapped, raw + duration, mapped + duration, uncertainty,
		Frame::Frd, Quantity::InertialAcceleration, ReferencePoint::Cg, Vec3f(1.F, 0.F, 0.F)};
}

void isolation(const RequestGuard &guard, const DiagnosticGuardResult &result, const std::string &name)
{
	check(!Peer::active(guard), name + ": no active demand");
	check(!result.common_decision || !result.common_decision->eligible_for_control, name + ": no eligible result");
}

void common(const DiagnosticGuardResult &result, DecisionCode expected, const std::string &name)
{
	check(std::holds_alternative<DecisionCode>(result.cause)
	      && std::get<DecisionCode>(result.cause) == expected, name + ": typed common cause");
	check(result.common_decision && result.common_decision->code == expected, name + ": actual common decision");
}

void local(const DiagnosticGuardResult &result, DiagnosticLocalReason expected, const std::string &name)
{
	check(std::holds_alternative<DiagnosticLocalReason>(result.cause)
	      && std::get<DiagnosticLocalReason>(result.cause) == expected, name + ": typed local cause");
	check(!result.common_decision, name + ": no invented common decision");
}

void establish(RequestGuard &guard, TimeUs now = 9950, SessionId session = 100)
{
	const uint64_t previous_rejections = Peer::rejections(guard);
	const auto result = Peer::establish(guard, {7, session}, true, context(now), selected);
	check(result.operation == DiagnosticGuardOperation::Establish, "setup establishment operation");
	check(result.code == DiagnosticCode::SessionEstablished, "setup independent establishment");
	common(result, DecisionCode::Accepted, "setup establishment");
	check(result.common_decision && result.common_decision->rejection_count == previous_rejections,
	      "setup does not duplicate poll rejection bookkeeping");
	isolation(guard, result, "setup establishment");
}

void expectRequest(RequestGuard &guard, const CanonicalRequest &input, const EvaluationContext &physical,
		   DecisionCode expected, const std::string &name)
{
	++request_variants;
	const auto before_floors = Peer::floors(guard);
	const auto before_cache = Peer::cache(guard);
	const auto result = Peer::evaluate(guard, input, physical, selected);
	check(result.operation == DiagnosticGuardOperation::Request, name + ": request operation");
	check(result.code == (expected == DecisionCode::ValidatedDisarmed ? DiagnosticCode::RequestValidatedDisarmed
			      : DiagnosticCode::Rejected), name + ": diagnostic result");
	common(result, expected, name);
	isolation(guard, result, name);
	check(Peer::cache(guard) == before_cache, name + ": canonical request never cached");

	if (expected == DecisionCode::ValidatedDisarmed) {
		check(Peer::floors(guard) == std::make_tuple(true, input.sequence, true, input.producer_monotonic_us),
		      name + ": successful validation advances both floors");
		check(result.common_decision && result.common_decision->syntactically_valid
		      && result.common_decision->conservative_age_valid, name + ": validation evidence");
		const TimeUs expected_age = physical.validator_receive_time_us
					    - (input.producer_time_in_validator_domain_us - input.clock_mapping_uncertainty_us);
		check(result.common_decision && result.common_decision->conservative_age_us == expected_age,
		      name + ": independent conservative age arithmetic");

	} else {
		check(Peer::floors(guard) == before_floors, name + ": rejection retains both floors");
	}
}

void once(const std::string &name, const CanonicalRequest &input, DecisionCode expected,
	  const EvaluationContext &physical = context(), const ContractConfig &settings = config())
{
	auto guard = Peer::create(settings);
	establish(guard, physical.validator_receive_time_us);
	expectRequest(guard, input, physical, expected, name);
}

void predicateProduct()
{
	const ArmState arms[] = {ArmState::Disarmed, ArmState::Armed, ArmState::Unknown, static_cast<ArmState>(255)};
	const ResearchModeState modes[] = {ResearchModeState::NotSelected, ResearchModeState::Selected,
					   ResearchModeState::Unknown, static_cast<ResearchModeState>(255)
					  };
	const DiagnosticSelectionState selectors[] = {disabled, selected, DiagnosticSelectionState::Faulted,
						      static_cast<DiagnosticSelectionState>(255)
						     };
	size_t tuples = 0;
	size_t passes = 0;

	for (size_t a = 0; a < 4; ++a) {
		for (size_t m = 0; m < 4; ++m) {
			for (size_t s = 0; s < 4; ++s) {
				for (unsigned bits = 0; bits < 8; ++bits) {
					for (unsigned policy = 0; policy < 2; ++policy) {
						++tuples;
						const std::string name = "T-" + std::to_string(a) + "-" + std::to_string(m) + "-" + std::to_string(s)
									 + "-" + std::to_string(bits) + "-" + std::to_string(policy);
						auto settings = config();
						settings.disarmed_policy = policy == 0 ? DisarmedPolicy::ValidateOnly : DisarmedPolicy::Reject;
						auto guard = Peer::create(settings);

						if (policy == 0) { establish(guard); }

						auto physical = context();
						physical.arm_state = arms[a];
						physical.mode_state = modes[m];
						physical.clock_mapping_valid = (bits & 1) != 0;
						physical.transport_connected = (bits & 2) != 0;
						physical.required_state_valid = (bits & 4) != 0;
						const auto before = Peer::snapshot(guard);
						const auto before_cache = Peer::cache(guard);
						const auto before_floors = Peer::floors(guard);
						const auto result = Peer::evaluate(guard, request(), physical, selectors[s]);
						check(result.operation == DiagnosticGuardOperation::Request, name + ": operation");
						isolation(guard, result, name);
						check(Peer::cache(guard) == before_cache, name + ": no request cache write");

						// Independent table oracle: use dimension indexes and literal priority
						// from the approved matrix, never production context helpers.
						if (policy == 1) {
							check(!guard.configured() && result.code == DiagnosticCode::NotConfigured, name + ": inert Reject policy");
							check(Peer::snapshot(guard) == before, name + ": invalid construction cannot mutate");

						} else {
							if ((bits & 1) == 0) { common(result, DecisionCode::ClockMappingInvalid, name); }

							else if ((bits & 2) == 0) { common(result, DecisionCode::Disconnected, name); }

							else if ((bits & 4) == 0) { common(result, DecisionCode::RequiredStateInvalid, name); }

							else if (a >= 2) { common(result, DecisionCode::ArmStateUnknown, name); }

							else if (m >= 2) { common(result, DecisionCode::ModeStateUnknown, name); }

							else if (a == 1) { local(result, DiagnosticLocalReason::NotDisarmed, name); }

							else if (m == 1) { local(result, DiagnosticLocalReason::FlightModeConflict, name); }

							else if (s == 3) { local(result, DiagnosticLocalReason::SelectorInvalid, name); }

							else if (s == 2) { local(result, DiagnosticLocalReason::SelectorFaulted, name); }

							else if (s == 0) { local(result, DiagnosticLocalReason::SelectionRequired, name); }

							else {
								common(result, DecisionCode::ValidatedDisarmed, name);
								check(result.code == DiagnosticCode::RequestValidatedDisarmed, name + ": sole passing tuple");
								++passes;
							}

							const bool should_pass = a == 0 && m == 0 && s == 1 && bits == 7;
							check(should_pass || result.code == DiagnosticCode::Rejected, name + ": rejection result");
							check(Peer::floors(guard) == (should_pass ? std::make_tuple(true, uint64_t{1}, true, TimeUs{9000})
										      : before_floors), name + ": exact floor mutation");
						}
					}
				}
			}
		}
	}

	check(tuples == 1024 && passes == 1, "predicate product: 1024 tuples, exactly one valid tuple");
	std::cout << "predicate tuples=" << tuples << "; oracle positive tuples=" << passes << '\n';
}

void payloadBoundaries()
{
	const DecisionCode identity_codes[] = {DecisionCode::WrongProtocolVersion, DecisionCode::WrongSource,
					       DecisionCode::WrongSession, DecisionCode::WrongFrame, DecisionCode::WrongQuantity, DecisionCode::WrongReferencePoint
					      };

	for (size_t i = 0; i < 6; ++i) {
		auto input = request();

		switch (i) {
		case 0: input.protocol_version = 2; break;

		case 1: input.source = 8; break;

		case 2: input.session = 101; break;

		case 3: input.frame = static_cast<Frame>(255); break;

		case 4: input.quantity = static_cast<Quantity>(255); break;

		case 5: input.reference_point = static_cast<ReferencePoint>(255); break;
		}

		once("R01-" + std::to_string(i), input, identity_codes[i]);
	}

	const float nonfinite[] = {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
				   -std::numeric_limits<float>::infinity()
				  };
	auto zero_session = request();
	zero_session.session = 0;
	once("canonical-zero-session", zero_session, DecisionCode::NoSession);

	for (int axis = 0; axis < 3; ++axis) {
		for (size_t kind = 0; kind < 3; ++kind) {
			auto input = request();
			input.acceleration_frd_m_s2(axis) = nonfinite[kind];
			once("R02-" + std::to_string(axis) + "-" + std::to_string(kind), input, DecisionCode::NonFinite);
		}

		const float values[] = {std::nextafter(-10.F, -std::numeric_limits<float>::infinity()), -10.F, 10.F,
					std::nextafter(10.F, std::numeric_limits<float>::infinity())
				       };

		for (size_t boundary = 0; boundary < 4; ++boundary) {
			auto input = request();
			input.acceleration_frd_m_s2 = Vec3f(0.F, 0.F, 0.F);
			input.acceleration_frd_m_s2(axis) = values[boundary];
			once("R03-" + std::to_string(axis) + "-" + std::to_string(boundary), input,
			     boundary == 0 || boundary == 3 ? DecisionCode::AxisOutOfBounds : DecisionCode::ValidatedDisarmed);
		}
	}

	// Widen only the axis box so the norm predicate is independently reachable.
	auto norm_config = config();
	norm_config.acceleration_bounds.maximum_m_s2 = Vec3f(20.F, 20.F, 20.F);
	const float norms[] = {std::nextafter(10.F, 0.F), 10.F, std::nextafter(10.F, 20.F)};

	for (size_t i = 0; i < 3; ++i) {
		auto input = request();
		input.acceleration_frd_m_s2 = Vec3f(norms[i], 0.F, 0.F);
		once("R04-" + std::to_string(i), input, i == 2 ? DecisionCode::NormOutOfBounds : DecisionCode::ValidatedDisarmed,
		     context(), norm_config);
	}

	for (TimeUs duration : {TimeUs{19}, TimeUs{20}, TimeUs{200}, TimeUs{201}}) {
		once("R05-" + std::to_string(duration), request(1, 9000, 10000, duration),
		     duration < 20 || duration > 200 ? DecisionCode::ValidityOutOfRange : DecisionCode::ValidatedDisarmed);
	}

	for (TimeUs uncertainty : {TimeUs{2}, TimeUs{3}}) {
		once("R06-" + std::to_string(uncertainty), request(1, 9000, 10000, 100, uncertainty),
		     uncertainty == 2 ? DecisionCode::ValidatedDisarmed : DecisionCode::ClockUncertaintyExceeded);
	}

	for (uint64_t sequence : {uint64_t{4}, uint64_t{5}, uint64_t{6}}) {
		auto guard = Peer::create(config());
		establish(guard);
		expectRequest(guard, request(5), context(), DecisionCode::ValidatedDisarmed, "R07 setup");
		expectRequest(guard, request(sequence, 9001, 10001), context(), sequence < 5 ? DecisionCode::OutOfOrderSequence
			      : (sequence == 5 ? DecisionCode::DuplicateSequence : DecisionCode::ValidatedDisarmed), "R07-" + std::to_string(sequence));
	}

	for (unsigned variant = 0; variant < 3; ++variant) {
		auto input = request();

		if (variant == 0) { input.producer_expiry_monotonic_us = 8999; }

		else if (variant == 1) { input.expiry_time_in_validator_domain_us = 9999; }

		else { --input.expiry_time_in_validator_domain_us; }

		once("R08-" + std::to_string(variant), input, DecisionCode::TimeArithmeticInvalid);
	}

	once("R09-earliest-creation-underflow", request(1, 9000, 1), DecisionCode::TimeArithmeticInvalid);
	// Positive duration makes earliest-expiry-only underflow unreachable: if
	// expiry < uncertainty then creation < uncertainty too. This explicit input
	// covers both underflows and is masked by the earlier creation check.
	auto wide_uncertainty = config();
	wide_uncertainty.timing.maximum_clock_uncertainty_us = 200;
	once("R09-earliest-expiry-underflow-masked-by-creation", request(1, 9000, 1, 20, 22),
	     DecisionCode::TimeArithmeticInvalid, context(), wide_uncertainty);
	auto short_duration = config();
	short_duration.timing.minimum_validity_us = 1;
	once("R09-latest-creation-overflow", request(1, 9000, maximum_time - 1, 1, 2),
	     DecisionCode::TimeArithmeticInvalid, context(maximum_time - 2), short_duration);
	once("R09-now-plus-skew-overflow", request(1, 9000, maximum_time - 100),
	     DecisionCode::TimeArithmeticInvalid, context(maximum_time - 5));

	for (TimeUs mapped : {TimeUs{10012}, TimeUs{10013}}) {
		once("R10-earliest-" + std::to_string(mapped), request(1, 9000, mapped),
		     mapped == 10012 ? DecisionCode::ValidatedDisarmed : DecisionCode::ProducerTimeInFuture);
	}

	// At U=2 the earliest-creation predicate masks the upper future boundary.
	// This separately named synthetic config makes both future cases reachable.
	auto future_config = config();
	future_config.timing.maximum_clock_uncertainty_us = 10;

	for (TimeUs mapped : {TimeUs{10010}, TimeUs{10011}}) {
		once("R10-latest-" + std::to_string(mapped), request(1, 9000, mapped, 100, 10),
		     mapped == 10010 ? DecisionCode::ValidatedDisarmed : DecisionCode::ProducerTimeInFuture, context(), future_config);
	}

	for (TimeUs mapped : {TimeUs{9962}, TimeUs{9961}}) {
		once("R11-" + std::to_string(mapped), request(1, 9000, mapped),
		     mapped == 9962 ? DecisionCode::ValidatedDisarmed : DecisionCode::RequestTooOld);
	}

	for (TimeUs duration : {TimeUs{20}, TimeUs{21}, TimeUs{22}}) {
		once("R12-" + std::to_string(duration), request(1, 9000, 9991, duration),
		     duration == 22 ? DecisionCode::ValidatedDisarmed : DecisionCode::RequestExpired);
	}

	for (TimeUs raw : {TimeUs{8999}, TimeUs{9000}, TimeUs{9001}}) {
		auto guard = Peer::create(config());
		establish(guard);
		expectRequest(guard, request(), context(), DecisionCode::ValidatedDisarmed, "R13 setup");
		expectRequest(guard, request(2, raw), context(), raw < 9000 ? DecisionCode::ProducerTimeRegressed
			      : (raw == 9000 ? DecisionCode::ProducerTimeNotIncreasing : DecisionCode::ValidatedDisarmed), "R13-" + std::to_string(raw));
		check(Peer::latched(guard) == (raw < 9000), "R13 only strict producer regression latches");
	}

	for (unsigned variant = 0; variant < 4; ++variant) {
		auto guard = Peer::create(config());
		establish(guard);
		expectRequest(guard, request(5), context(), DecisionCode::ValidatedDisarmed, "R16 setup");
		auto input = request(6, 9001);
		DecisionCode expected = DecisionCode::RequestTooOld;

		switch (variant) {
		case 0:
			input = request(4, 9001, 9990, 20);
			expected = DecisionCode::OutOfOrderSequence;
			break;

		case 1:
			input.frame = static_cast<Frame>(255);
			input.acceleration_frd_m_s2(0) = std::numeric_limits<float>::quiet_NaN();
			expected = DecisionCode::WrongFrame;
			break;

		case 2:
			input.sequence = 5;
			input.clock_mapping_uncertainty_us = 3;
			expected = DecisionCode::ClockUncertaintyExceeded;
			break;

		case 3:
			input = request(6, 8999, 9950);
			break;
		}

		expectRequest(guard, input, context(), expected, "R16-" + std::to_string(variant));
		check(!Peer::latched(guard), "R16 unreached producer regression cannot latch");
	}
}

void recoveryAndAdministration()
{
	auto guard = Peer::create(config());
	expectRequest(guard, request(), context(9950), DecisionCode::NoSession, "R14-guard-no-session");
	establish(guard);
	expectRequest(guard, request(), context(), DecisionCode::ValidatedDisarmed, "reference-trace step 5");
	const auto floors = Peer::floors(guard);
	const auto idle = Peer::poll(guard, context(), selected);
	check(idle.operation == DiagnosticGuardOperation::Poll && idle.code == DiagnosticCode::NoEligibleRequest,
	      "poll cannot report request validation");
	common(idle, DecisionCode::NoEligibleRequest, "poll");
	check(idle.common_decision && !idle.common_decision->syntactically_valid && !idle.common_decision->conservative_age_valid,
	      "poll cannot invent request syntax or age evidence");
	isolation(guard, idle, "poll");
	check(Peer::floors(guard) == floors, "poll retains request floors");
	auto armed = context();
	armed.arm_state = ArmState::Armed;
	const auto armed_poll = Peer::poll(guard, armed, selected);
	local(armed_poll, DiagnosticLocalReason::NotDisarmed, "post-validation arm transition");
	isolation(guard, armed_poll, "post-validation arm transition");
	auto flight_selected = context();
	flight_selected.mode_state = ResearchModeState::Selected;
	const auto mode_poll = Peer::poll(guard, flight_selected, selected);
	local(mode_poll, DiagnosticLocalReason::FlightModeConflict, "post-validation Flight mode transition");
	isolation(guard, mode_poll, "post-validation Flight mode transition");
	check(Peer::floors(guard) == floors, "arm/mode transitions cannot reset validation floors");
	expectRequest(guard, request(2, 8999, 9999), context(10011), DecisionCode::ProducerTimeRegressed, "reference-trace step 6");
	const auto latched = Peer::poll(guard, context(10012), selected);
	common(latched, DecisionCode::Latched, "producer latch persists");
	check(latched.common_decision && latched.common_decision->first_latched_code == DecisionCode::ProducerTimeRegressed,
	      "producer first latch reason retained");
	Peer::invalidate(guard);
	check(Peer::sessionRequired(guard) && Peer::floors(guard) == floors, "invalidation retains replay floors");
	const auto retained_clock = Peer::clock(guard);
	const auto denied = Peer::clear(guard, false, ArmState::Disarmed);
	common(denied, DecisionCode::UnauthorizedSessionEstablishment, "unauthorized clear");
	check(Peer::latched(guard) && Peer::clock(guard) == retained_clock, "unauthorized clear retains latch and clock");

	for (ArmState arm : {ArmState::Armed, ArmState::Unknown, static_cast<ArmState>(255)}) {
		const auto result = Peer::clear(guard, true, arm);
		common(result, arm == ArmState::Armed ? DecisionCode::SessionChangeWhileArmed : DecisionCode::ArmStateUnknown,
		       "clear requires actual known Disarmed");
		check(Peer::latched(guard), "non-disarmed clear cannot recover latch");
		isolation(guard, result, "rejected clear");
	}

	const auto cleared = Peer::clear(guard, true, ArmState::Disarmed);
	check(cleared.operation == DiagnosticGuardOperation::Clear && cleared.code == DiagnosticCode::LatchCleared,
	      "clear reports clear, not request validation");
	common(cleared, DecisionCode::Accepted, "authorized clear");
	isolation(guard, cleared, "authorized clear");
	check(!Peer::latched(guard) && Peer::clock(guard) == std::make_tuple(false, TimeUs{0})
	      && Peer::sessionRequired(guard) && Peer::floors(guard) == floors, "clear resets only authorized clock/latch state");
	expectRequest(guard, request(2, 9001), context(9950), DecisionCode::SessionEstablishmentRequired, "clear cannot restore session");
	common(Peer::establish(guard, {7, 100}, true, context(9950), selected), DecisionCode::WrongSession,
	       "old established session cannot be reused after clear");
	check(Peer::floors(guard) == floors, "same-session rejection preserves floors");
	const auto next_session = Peer::establish(guard, {7, 101}, true, context(9950), selected);
	check(next_session.code == DiagnosticCode::SessionEstablished && !Peer::sessionRequired(guard), "distinct session recovery");
	check(!std::get<0>(Peer::floors(guard)) && !std::get<2>(Peer::floors(guard)), "distinct session resets floor validity");
	check(Peer::clock(guard) == std::make_tuple(true, TimeUs{9950}), "session establishment preserves observed validator clock");
	auto next = request();
	next.session = 101;
	expectRequest(guard, next, context(), DecisionCode::ValidatedDisarmed, "distinct session can validate new request");
	const auto next_floors = Peer::floors(guard);
	Peer::invalidate(guard);
	check(Peer::floors(guard) == next_floors && Peer::sessionRequired(guard), "healthy invalidation preserves floors");
	expectRequest(guard, next, context(), DecisionCode::SessionEstablishmentRequired, "R14-guard-invalidated-session");
	common(Peer::establish(guard, {7, 101}, true, context(), selected), DecisionCode::WrongSession,
	       "invalidation cannot authorize reuse of prior session");

	for (unsigned operation = 0; operation < 3; ++operation) {
		auto clock_guard = Peer::create(config());
		establish(clock_guard, 10010);
		const auto result = operation == 0 ? Peer::evaluate(clock_guard, request(), context(10009), selected)
				    : operation == 1 ? Peer::poll(clock_guard, context(10009), selected)
				    : Peer::establish(clock_guard, {7, 101}, true, context(10009), selected);
		common(result, DecisionCode::ValidatorTimeRegressed, "validator regression operation " + std::to_string(operation));
		check(Peer::latched(clock_guard) && result.common_decision && result.common_decision->rejection_count == 1,
		      "one clock observation records one latch rejection");
		isolation(clock_guard, result, "clock regression");
		common(Peer::poll(clock_guard, context(10011), selected), DecisionCode::Latched, "clock catchup cannot recover");
	}

	// Fresh instances prevent prior rejection/latch precedence from masking any
	// session admission condition; selection and physical state remain separate.
	for (unsigned variant = 0; variant < 8; ++variant) {
		auto session_guard = Peer::create(config());
		auto physical = context();
		TrustedSession session{7, 100};
		bool authorized = true;
		auto selector = selected;
		DecisionCode expected = DecisionCode::InvalidTrustedSession;

		switch (variant) {
		case 0: authorized = false; expected = DecisionCode::UnauthorizedSessionEstablishment; break;

		case 1: session.owner = 8; break;

		case 2: session.session = 0; break;

		case 3: physical.arm_state = ArmState::Armed; expected = DecisionCode::SessionChangeWhileArmed; break;

		case 4: physical.clock_mapping_valid = false; expected = DecisionCode::ClockMappingInvalid; break;

		case 5: physical.required_state_valid = false; expected = DecisionCode::RequiredStateInvalid; break;

		case 6: physical.mode_state = ResearchModeState::Selected; break;

		case 7: selector = disabled; break;
		}

		const auto result = Peer::establish(session_guard, session, authorized, physical, selector);
		check(result.code == DiagnosticCode::Rejected, "session prerequisites rejection");

		if (variant < 6) { common(result, expected, "session prerequisites " + std::to_string(variant)); }

		else {
			local(result, variant == 6 ? DiagnosticLocalReason::FlightModeConflict : DiagnosticLocalReason::SelectionRequired,
			      "session diagnostic prerequisite");
		}

		isolation(session_guard, result, "rejected establishment");
		expectRequest(session_guard, request(), context(), DecisionCode::NoSession, "rejected establishment never installs session");
	}
}

void domainIsolation()
{
	uint16_t wire_sentinel = 99;
	check(!mapDecisionCodeV1(DecisionCode::ApiDomainMismatch, wire_sentinel) && wire_sentinel == 99,
	      "B03 local mismatch cannot map to frozen v1 or alter destination");
	static_assert(!std::is_convertible<DiagnosticGuardResult, Decision>::value, "diagnostic result cannot become Flight decision");
	static_assert(!std::is_constructible<RequestGuard, const ContractConfig &, DiagnosticOfflineTag>::value,
		      "diagnostic construction must stay private");
	auto diagnostic = Peer::create(config());
	establish(diagnostic);
	expectRequest(diagnostic, request(), context(), DecisionCode::ValidatedDisarmed, "domain setup");
	const auto before = Peer::snapshot(diagnostic);
	auto flight_context = context(1); // Would latch if any ordinary call observed time.
	flight_context.arm_state = ArmState::Armed;
	flight_context.mode_state = ResearchModeState::Selected;
	check(diagnostic.configured(), "diagnostic configured query remains truthful");
	check(Peer::snapshot(diagnostic) == before, "configured query no mutation");
	check(diagnostic.evaluate(request(9), flight_context).code == DecisionCode::ApiDomainMismatch, "Flight evaluate on diagnostic");
	check(Peer::snapshot(diagnostic) == before, "Flight evaluate no mutation");
	check(diagnostic.poll(flight_context).code == DecisionCode::ApiDomainMismatch, "Flight poll on diagnostic");
	check(Peer::snapshot(diagnostic) == before, "Flight poll no mutation");
	check(diagnostic.establishTrustedSession({7, 101}, true, flight_context).code == DecisionCode::ApiDomainMismatch,
	      "Flight establishment on diagnostic");
	check(Peer::snapshot(diagnostic) == before, "Flight establishment no mutation");
	check(diagnostic.clearLatch(true, ArmState::Disarmed).code == DecisionCode::ApiDomainMismatch, "Flight clear on diagnostic");
	check(Peer::snapshot(diagnostic) == before, "Flight clear no mutation");
	diagnostic.revoke();
	check(Peer::snapshot(diagnostic) == before, "Flight revoke no mutation");
	auto sentinel = request(999, 999, 999);
	std::array<unsigned char, sizeof(CanonicalRequest)> sentinel_bytes{};
	std::memcpy(sentinel_bytes.data(), &sentinel, sentinel_bytes.size());
	Decision access{};
	check(!diagnostic.getEligibleRequest(flight_context, sentinel, access) && access.code == DecisionCode::ApiDomainMismatch,
	      "Flight access on diagnostic returns mismatch");
	check(std::memcmp(sentinel_bytes.data(), &sentinel, sentinel_bytes.size()) == 0, "request sentinel byte-identical after misuse");
	check(Peer::snapshot(diagnostic) == before, "Flight access no mutation");

	RequestGuard flight(config());
	auto disarmed_flight = context(9950);
	disarmed_flight.mode_state = ResearchModeState::Selected;
	check(flight.establishTrustedSession({7, 100}, true, disarmed_flight).code == DecisionCode::Accepted, "Flight setup session");
	flight_context.validator_receive_time_us = 10010;
	check(flight.evaluate(request(), flight_context).eligible_for_control, "Flight setup has actual cached demand");
	const auto flight_before = Peer::snapshot(flight);

	for (unsigned operation = 0; operation < 4; ++operation) {
		const auto result = operation == 0 ? Peer::evaluate(flight, request(), context(1), selected)
				    : operation == 1 ? Peer::poll(flight, context(1), selected)
				    : operation == 2 ? Peer::establish(flight, {7, 101}, true, context(1), selected)
				    : Peer::clear(flight, true, ArmState::Disarmed);
		check(result.code == DiagnosticCode::WrongDomain, "diagnostic method on Flight " + std::to_string(operation));
		check(!result.common_decision, "wrong domain cannot return eligible Flight common decision");
		check(Peer::snapshot(flight) == flight_before, "diagnostic misuse cannot change Flight history/cache");
	}

	Peer::invalidate(flight);
	check(Peer::snapshot(flight) == flight_before, "diagnostic invalidation on Flight no mutation");

	for (unsigned variant = 0; variant < 3; ++variant) {
		auto invalid_config = config();

		if (variant == 0) { invalid_config.permitted_source = 0; }

		else if (variant == 1) { invalid_config.timing.minimum_validity_us = 0; }

		else { invalid_config.disarmed_policy = DisarmedPolicy::Reject; }

		auto invalid = Peer::create(invalid_config);
		const auto invalid_before = Peer::snapshot(invalid);
		check(!invalid.configured(), "invalid diagnostic configuration inert");
		check(Peer::evaluate(invalid, request(), context(), selected).code == DiagnosticCode::NotConfigured, "invalid evaluate");
		check(Peer::poll(invalid, context(), selected).code == DiagnosticCode::NotConfigured, "invalid poll");
		check(Peer::establish(invalid, {7, 100}, true, context(), selected).code == DiagnosticCode::NotConfigured, "invalid establish");
		check(Peer::clear(invalid, true, ArmState::Disarmed).code == DiagnosticCode::NotConfigured, "invalid clear");
		Peer::invalidate(invalid);
		check(Peer::snapshot(invalid) == invalid_before, "invalid configuration all diagnostic operations no mutation");
	}
}

} // namespace

int main()
{
	predicateProduct();
	payloadBoundaries();
	recoveryAndAdministration();
	domainIsolation();
	std::cout << "synthetic_offline guard request variants=" << request_variants << "; " << checks << " checks; "
		  << failures << " failures\n";
	std::cout << "Scope: private guard only; no adapter, owner, ACK/authentication, capacity, wire, live or physical qualification.\n";
	return failures == 0 ? 0 : 1;
}
