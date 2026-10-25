#include "RequestGuard.hpp"

#include <cmath>
#include <iostream>
#include <limits>

using namespace research_acceleration;

namespace
{

int failures = 0;
int checks = 0;

void check(bool condition, const char *name)
{
	++checks;

	if (!condition) {
		std::cerr << "FAILED: " << name << '\n';
		++failures;
	}
}

ContractConfig fixtureConfig(DisarmedPolicy policy = DisarmedPolicy::ValidateOnly)
{
	// These are explicitly non-operational fixture bounds and times.
	return {1, 9, policy, {Vec3f(-10.0F, -10.0F, -10.0F), Vec3f(10.0F, 10.0F, 10.0F), 12.0F},
		{10, 100, 50, 10, 5}, 0.01F};
}

EvaluationContext fixtureContext(TimeUs now, ArmState arm = ArmState::Armed,
				 ResearchModeState mode = ResearchModeState::Selected)
{
	return {now, arm, mode, true, true, true};
}

CanonicalRequest fixtureRequest(uint64_t sequence = 1, TimeUs producer_time = 100,
				TimeUs mapped_time = 100, TimeUs duration = 100, TimeUs uncertainty = 1)
{
	return {1, 9, 77, sequence, producer_time, mapped_time, producer_time + duration, mapped_time + duration,
		uncertainty, Frame::Frd, Quantity::InertialAcceleration, ReferencePoint::Cg, Vec3f(1.0F, -2.0F, 3.0F)};
}

void establish(RequestGuard &guard, TimeUs now = 100)
{
	const Decision established = guard.establishTrustedSession({9, 77}, true, fixtureContext(now, ArmState::Disarmed));
	check(established.code == DecisionCode::Accepted, "fixture_establish_trusted_session");
	check(established.rejection_count == 0, "fixture_establish_trusted_session_does_not_count_admin_poll");
}

Decision evaluateOnce(const ContractConfig &config, const CanonicalRequest &request, const EvaluationContext &context)
{
	RequestGuard guard(config);
	const Decision established = guard.establishTrustedSession({9, 77}, true,
				     fixtureContext(context.validator_receive_time_us, ArmState::Disarmed));

	if (established.code != DecisionCode::Accepted) {
		return established;
	}

	return guard.evaluate(request, context);
}

void fullCommandFixtures()
{
	auto config = fixtureConfig();
	config.protocol_version = kRequestProtocolVersionV1;
	RequestGuard guard(config);
	establish(guard);
	auto request = fixtureRequest();
	request.protocol_version = kRequestProtocolVersionV2;
	request.command_kind = CommandKind::AccelerationAndAttitude;
	request.desired_attitude_ned_from_body = QuaternionNedFromFrd(-1.F, 0.F, 0.F, 0.F);
	check(guard.evaluate(request, fixtureContext(110)).code == DecisionCode::Accepted,
	      "full_command_uses_explicit_v2_under_v1_admin_envelope");
	CanonicalRequest copied{};
	Decision decision{};
	check(guard.getEligibleRequest(fixtureContext(110), copied, decision)
	      && copied.protocol_version == kRequestProtocolVersionV2
	      && copied.command_kind == CommandKind::AccelerationAndAttitude
	      && copied.desired_attitude_ned_from_body(0) == 1.F,
	      "full_command_guard_caches_canonical_absolute_attitude");

	for (unsigned fault = 0; fault < 6; ++fault) {
		auto bad = request;
		bad.sequence = 2;
		bad.producer_monotonic_us = bad.producer_time_in_validator_domain_us = 101;
		bad.producer_expiry_monotonic_us = bad.expiry_time_in_validator_domain_us = 201;

		if (fault == 0) { bad.protocol_version = kRequestProtocolVersionV1; }

		if (fault == 1) { bad.command_kind = CommandKind::AccelerationOnly; }

		if (fault == 2) { bad.command_kind = static_cast<CommandKind>(99); }

		if (fault == 3) { bad.desired_attitude_ned_from_body = QuaternionNedFromFrd(0.F, 0.F, 0.F, 0.F); }

		if (fault == 4) { bad.desired_attitude_ned_from_body(0) = 1.02F; }

		if (fault == 5) { bad.desired_attitude_ned_from_body(0) = std::numeric_limits<float>::infinity(); }

		check(guard.evaluate(bad, fixtureContext(111)).code != DecisionCode::Accepted,
		      "full_command_bad_version_kind_or_attitude_rejected");
		check(guard.getEligibleRequest(fixtureContext(111), copied, decision) && copied.sequence == 1,
		      "full_command_malformed_packet_does_not_replace_fresh_accepted_objective");
	}

	request.sequence = 2;
	request.producer_monotonic_us = request.producer_time_in_validator_domain_us = 101;
	request.producer_expiry_monotonic_us = request.expiry_time_in_validator_domain_us = 201;
	request.desired_attitude_ned_from_body = QuaternionNedFromFrd(1.F, 0.F, 0.F, 0.F);
	check(guard.evaluate(request, fixtureContext(111)).code == DecisionCode::Accepted,
	      "full_command_rejections_do_not_consume_valid_sequence");
	check(guard.evaluate(request, fixtureContext(111)).code == DecisionCode::DuplicateSequence,
	      "full_command_keeps_original_replay_protection");
}

void additionalContractFixtures()
{
	// Non-operational branch/transition fixtures from independent S1 review.
	ContractConfig invalid = fixtureConfig();
	invalid.permitted_source = 0;
	check(!RequestGuard(invalid).configured(), "fixture_zero_permitted_source");
	invalid = fixtureConfig();
	invalid.timing.minimum_validity_us = invalid.timing.maximum_validity_us + 1;
	check(!RequestGuard(invalid).configured(), "fixture_inverted_validity_configuration");
	invalid = fixtureConfig();
	invalid.quaternion_norm_tolerance = 0.F;
	check(!RequestGuard(invalid).configured(), "fixture_zero_quaternion_tolerance_configuration");

	for (float bad : {
		     std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
		     -std::numeric_limits<float>::infinity(), -1.F
	     }) {
		invalid = fixtureConfig();
		invalid.acceleration_bounds.maximum_norm_m_s2 = bad;
		check(!RequestGuard(invalid).configured(), "fixture_invalid_maximum_norm_configuration");
		invalid = fixtureConfig();
		invalid.quaternion_norm_tolerance = bad;
		check(!RequestGuard(invalid).configured(), "fixture_invalid_norm_tolerance_configuration");
	}

	RequestGuard admin(fixtureConfig());
	check(admin.establishTrustedSession({8, 77}, true, fixtureContext(100, ArmState::Disarmed)).code
	      == DecisionCode::InvalidTrustedSession, "fixture_wrong_trusted_owner");
	RequestGuard latch_guard(fixtureConfig());
	establish(latch_guard);
	const Decision first = latch_guard.poll(fixtureContext(99));
	check(first.latched && first.first_latched_code == DecisionCode::ValidatorTimeRegressed,
	      "fixture_first_clock_latch_status");
	const Decision attempted = latch_guard.establishTrustedSession({9, 78}, true, fixtureContext(100, ArmState::Disarmed));
	check(attempted.code == DecisionCode::Latched && attempted.rejection_count == first.rejection_count + 1
	      && attempted.first_latched_code == first.first_latched_code, "fixture_latched_admin_counted_once");

	RequestGuard reconnect(fixtureConfig());
	establish(reconnect);
	const CanonicalRequest original = fixtureRequest();
	check(reconnect.evaluate(original, fixtureContext(110)).eligible_for_control, "fixture_reconnect_initial_request");
	EvaluationContext lost = fixtureContext(111);
	lost.transport_connected = false;
	check(reconnect.poll(lost).code == DecisionCode::Disconnected, "fixture_reconnect_observed_loss");
	check(reconnect.evaluate(original, fixtureContext(112)).code == DecisionCode::DuplicateSequence,
	      "fixture_reconnect_replay_rejected");
	CanonicalRequest out = fixtureRequest(999);
	Decision status{};
	check(!reconnect.getEligibleRequest(fixtureContext(112), out, status) && out.sequence == 999,
	      "fixture_reconnect_replay_cannot_resurrect");
	check(reconnect.evaluate(fixtureRequest(2, 101), fixtureContext(113)).eligible_for_control,
	      "fixture_reconnect_newer_request_accepted");

	const Decision too_old = evaluateOnce(fixtureConfig(), fixtureRequest(1, 100, 60, 100, 0), fixtureContext(111));
	check(too_old.code == DecisionCode::RequestTooOld && too_old.conservative_age_valid
	      && too_old.conservative_age_us == 51, "fixture_too_old_age_diagnostic");
	const Decision expired = evaluateOnce(fixtureConfig(), fixtureRequest(1, 100, 100, 20, 1), fixtureContext(119));
	check(expired.code == DecisionCode::RequestExpired && expired.conservative_age_valid
	      && expired.conservative_age_us == 20, "fixture_expired_arrival_age_diagnostic");

	RequestGuard evaluate_clock(fixtureConfig());
	establish(evaluate_clock);
	check(evaluate_clock.evaluate(original, fixtureContext(99)).code == DecisionCode::ValidatorTimeRegressed,
	      "fixture_validator_regression_evaluate_path");
	RequestGuard get_clock(fixtureConfig());
	establish(get_clock);
	check(!get_clock.getEligibleRequest(fixtureContext(99), out, status)
	      && status.code == DecisionCode::ValidatorTimeRegressed && status.latched,
	      "fixture_validator_regression_get_path");
	check(get_clock.clearLatch(true, ArmState::Disarmed).code == DecisionCode::Accepted,
	      "fixture_explicit_disarmed_clock_epoch_reset");
	check(get_clock.establishTrustedSession({9, 78}, true, fixtureContext(10, ArmState::Disarmed)).code
	      == DecisionCode::Accepted, "fixture_new_session_after_clock_epoch_reset");
	CanonicalRequest restarted = fixtureRequest(0, 1, 10, 10, 0);
	restarted.session = 78;
	check(get_clock.evaluate(restarted, fixtureContext(11)).eligible_for_control,
	      "fixture_new_session_accepts_fresh_low_sequence");

	RequestGuard polling(fixtureConfig());
	establish(polling);
	check(polling.evaluate(original, fixtureContext(110)).eligible_for_control, "fixture_polling_initial");
	const Decision early = polling.poll(fixtureContext(111));
	const Decision later = polling.poll(fixtureContext(112));
	check(early.conservative_age_valid && later.conservative_age_valid && early.conservative_age_us == 12
	      && later.conservative_age_us == 13, "fixture_polling_age_increases_not_refreshed");
	check(polling.getEligibleRequest(fixtureContext(113), out, status)
	      && out.expiry_time_in_validator_domain_us == original.expiry_time_in_validator_domain_us
	      && out.producer_expiry_monotonic_us == original.producer_expiry_monotonic_us
	      && out.session == original.session && out.sequence == original.sequence,
	      "fixture_polling_original_expiry_session_preserved");
}

} // namespace

int main()
{
	// Configuration rejects absent, non-finite, inverted, and invalid timing values without defaults.
	ContractConfig invalid = fixtureConfig();
	invalid.protocol_version = 0;
	check(!RequestGuard(invalid).configured(), "fixture_zero_protocol_config_rejected");
	invalid = fixtureConfig();
	invalid.acceleration_bounds.minimum_m_s2(0) = 2.0F;
	invalid.acceleration_bounds.maximum_m_s2(0) = 1.0F;
	check(!RequestGuard(invalid).configured(), "fixture_inverted_axis_config_rejected");
	invalid = fixtureConfig();
	invalid.acceleration_bounds.maximum_norm_m_s2 = std::numeric_limits<float>::infinity();
	check(!RequestGuard(invalid).configured(), "fixture_nonfinite_bound_config_rejected");
	invalid = fixtureConfig();
	invalid.timing.maximum_command_age_us = 0;
	check(!RequestGuard(invalid).configured(), "fixture_zero_time_config_rejected");

	for (int axis = 0; axis < 3; ++axis) {
		invalid = fixtureConfig();
		invalid.acceleration_bounds.minimum_m_s2(axis) = std::numeric_limits<float>::quiet_NaN();
		check(!RequestGuard(invalid).configured(), "fixture_nonfinite_minimum_axis_config_rejected");
		invalid = fixtureConfig();
		invalid.acceleration_bounds.maximum_m_s2(axis) = std::numeric_limits<float>::infinity();
		check(!RequestGuard(invalid).configured(), "fixture_nonfinite_maximum_axis_config_rejected");
	}

	for (TimeUs TimingRequirements::*member : {
		     &TimingRequirements::minimum_validity_us,
		     &TimingRequirements::maximum_validity_us, &TimingRequirements::maximum_command_age_us,
		     &TimingRequirements::maximum_future_skew_us, &TimingRequirements::maximum_clock_uncertainty_us
	     }) {
		invalid = fixtureConfig();
		invalid.timing.*member = 0;
		check(!RequestGuard(invalid).configured(), "fixture_each_zero_timing_config_rejected");
	}
	invalid = fixtureConfig();
	invalid.acceleration_bounds.maximum_norm_m_s2 = 0.0F;
	check(!RequestGuard(invalid).configured(), "fixture_zero_norm_config_rejected");
	invalid = fixtureConfig();
	invalid.quaternion_norm_tolerance = std::numeric_limits<float>::quiet_NaN();
	check(!RequestGuard(invalid).configured(), "fixture_nonfinite_quaternion_tolerance_config_rejected");
	invalid = fixtureConfig();
	invalid.quaternion_norm_tolerance = 1.0F;
	check(!RequestGuard(invalid).configured(), "fixture_unit_quaternion_tolerance_config_rejected");
	invalid = fixtureConfig();
	invalid.disarmed_policy = static_cast<DisarmedPolicy>(99);
	check(!RequestGuard(invalid).configured(), "fixture_invalid_policy_config_rejected");

	RequestGuard guard(fixtureConfig());
	establish(guard);
	Decision accepted = guard.evaluate(fixtureRequest(), fixtureContext(110));
	check(accepted.code == DecisionCode::Accepted && accepted.eligible_for_control && accepted.conservative_age_us == 11,
	      "fixture_accepts_valid_request_at_inclusive_bounds");

	CanonicalRequest copied{};
	Decision access{};
	check(guard.getEligibleRequest(fixtureContext(111), copied, access) && copied.sequence == 1,
	      "fixture_get_rechecks_and_copies_live_request");

	CanonicalRequest malformed = fixtureRequest(2, 101);
	malformed.acceleration_frd_m_s2(0) = std::numeric_limits<float>::quiet_NaN();
	check(guard.evaluate(malformed, fixtureContext(112)).code == DecisionCode::NonFinite,
	      "fixture_malformed_owner_packet_rejected");
	check(guard.getEligibleRequest(fixtureContext(113), copied, access) && copied.sequence == 1,
	      "fixture_malformed_owner_packet_does_not_refresh_or_replace");

	CanonicalRequest foreign = fixtureRequest(2, 101);
	foreign.source = 8;
	check(guard.evaluate(foreign, fixtureContext(114)).code == DecisionCode::WrongSource,
	      "fixture_foreign_packet_rejected_without_latch");
	check(guard.getEligibleRequest(fixtureContext(115), copied, access) && copied.sequence == 1,
	      "fixture_foreign_packet_preserves_owner_request");

	check(guard.evaluate(fixtureRequest(1, 101), fixtureContext(116)).code == DecisionCode::DuplicateSequence,
	      "fixture_duplicate_sequence_rejected");
	check(guard.evaluate(fixtureRequest(0, 101), fixtureContext(117)).code == DecisionCode::OutOfOrderSequence,
	      "fixture_out_of_order_sequence_rejected");
	check(guard.evaluate(fixtureRequest(2, 100), fixtureContext(118)).code == DecisionCode::ProducerTimeNotIncreasing,
	      "fixture_equal_producer_time_rejected_without_extension");
	check(guard.getEligibleRequest(fixtureContext(119), copied, access) && copied.sequence == 1,
	      "fixture_duplicate_or_equal_time_preserves_request");

	RequestGuard regression_guard(fixtureConfig());
	establish(regression_guard);
	check(regression_guard.evaluate(fixtureRequest(2, 101), fixtureContext(110)).code == DecisionCode::Accepted,
	      "fixture_newer_owner_before_regression");
	check(regression_guard.evaluate(fixtureRequest(3, 100), fixtureContext(111)).code == DecisionCode::ProducerTimeRegressed,
	      "fixture_newer_owner_producer_regression_latches");
	check(regression_guard.poll(fixtureContext(112)).code == DecisionCode::Latched,
	      "fixture_latch_persists_after_valid_context");
	check(regression_guard.clearLatch(false, ArmState::Disarmed).code == DecisionCode::UnauthorizedSessionEstablishment,
	      "fixture_unauthorised_latch_clear_rejected");
	check(regression_guard.clearLatch(true, ArmState::Armed).code == DecisionCode::SessionChangeWhileArmed,
	      "fixture_armed_latch_clear_rejected");
	check(regression_guard.clearLatch(true, ArmState::Disarmed).code == DecisionCode::Accepted,
	      "fixture_disarmed_authorised_latch_clear");
	check(regression_guard.evaluate(fixtureRequest(4, 102), fixtureContext(113)).code == DecisionCode::SessionEstablishmentRequired,
	      "fixture_clear_requires_explicit_new_session");
	check(regression_guard.establishTrustedSession({9, 77}, true, fixtureContext(114, ArmState::Disarmed)).code == DecisionCode::WrongSession,
	      "fixture_same_session_cannot_reset_floors");
	check(regression_guard.establishTrustedSession({9, 78}, true, fixtureContext(115, ArmState::Disarmed)).code == DecisionCode::Accepted,
	      "fixture_different_disarmed_session_restarts_floors");

	RequestGuard validator_clock_guard(fixtureConfig());
	establish(validator_clock_guard, 100);
	const Decision validator_regressed = validator_clock_guard.poll(fixtureContext(99));
	check(validator_regressed.code == DecisionCode::ValidatorTimeRegressed,
	      "fixture_validator_clock_regression_latches");
	check(validator_regressed.rejection_count == 1 && validator_regressed.first_latched_code == DecisionCode::ValidatorTimeRegressed,
	      "fixture_validator_clock_regression_records_one_first_cause");
	check(validator_clock_guard.poll(fixtureContext(100)).code == DecisionCode::Latched,
	      "fixture_validator_clock_catchup_does_not_resume");

	RequestGuard timing_guard(fixtureConfig());
	establish(timing_guard);
	CanonicalRequest future = fixtureRequest();
	future.producer_time_in_validator_domain_us = 112;
	future.expiry_time_in_validator_domain_us = 212;
	check(timing_guard.evaluate(future, fixtureContext(110)).code == DecisionCode::ProducerTimeInFuture,
	      "fixture_entirely_future_interval_rejected_without_unsigned_age_clip");
	CanonicalRequest old = fixtureRequest();
	old.producer_time_in_validator_domain_us = 40;
	old.expiry_time_in_validator_domain_us = 140;
	check(timing_guard.evaluate(old, fixtureContext(110)).code == DecisionCode::RequestTooOld,
	      "fixture_conservative_old_age_rejected");
	CanonicalRequest overflow = fixtureRequest();
	overflow.producer_time_in_validator_domain_us = std::numeric_limits<TimeUs>::max();
	overflow.expiry_time_in_validator_domain_us = std::numeric_limits<TimeUs>::max();
	check(timing_guard.evaluate(overflow, fixtureContext(110)).code == DecisionCode::TimeArithmeticInvalid,
	      "fixture_uint64_time_overflow_rejected");

	RequestGuard context_guard(fixtureConfig());
	establish(context_guard);
	check(context_guard.evaluate(fixtureRequest(), fixtureContext(110)).code == DecisionCode::Accepted,
	      "fixture_context_guard_accepts_initial_request");
	EvaluationContext disconnected = fixtureContext(111);
	disconnected.transport_connected = false;
	check(context_guard.poll(disconnected).code == DecisionCode::Disconnected, "fixture_disconnect_revokes_request");
	check(!context_guard.getEligibleRequest(fixtureContext(112), copied, access), "fixture_reconnect_does_not_resurrect_request");
	check(context_guard.evaluate(fixtureRequest(2, 101), fixtureContext(113)).code == DecisionCode::Accepted,
	      "fixture_fresh_request_required_after_disconnect");
	EvaluationContext no_mode = fixtureContext(114);
	no_mode.mode_state = ResearchModeState::NotSelected;
	check(context_guard.poll(no_mode).code == DecisionCode::ModeNotSelected, "fixture_mode_loss_revokes_request");

	RequestGuard expiry_guard(fixtureConfig());
	establish(expiry_guard);
	check(expiry_guard.evaluate(fixtureRequest(), fixtureContext(110)).code == DecisionCode::Accepted,
	      "fixture_expiry_guard_accepts");
	check(expiry_guard.poll(fixtureContext(199)).code == DecisionCode::RequestExpired,
	      "fixture_earliest_expiry_boundary_rejected");
	check(!expiry_guard.getEligibleRequest(fixtureContext(200), copied, access), "fixture_expiry_no_cached_resurrection");

	RequestGuard validate_only_guard(fixtureConfig(DisarmedPolicy::ValidateOnly));
	establish(validate_only_guard);
	check(validate_only_guard.evaluate(fixtureRequest(), fixtureContext(110, ArmState::Disarmed)).code == DecisionCode::ValidatedDisarmed,
	      "fixture_disarmed_validate_only_reports_without_control");
	check(!validate_only_guard.getEligibleRequest(fixtureContext(111), copied, access),
	      "fixture_validate_only_never_carries_across_arm");
	check(validate_only_guard.evaluate(fixtureRequest(2, 101), fixtureContext(112)).code == DecisionCode::Accepted,
	      "fixture_post_arm_fresh_request_required_and_accepted");

	RequestGuard reject_disarmed_guard(fixtureConfig(DisarmedPolicy::Reject));
	establish(reject_disarmed_guard);
	check(reject_disarmed_guard.evaluate(fixtureRequest(), fixtureContext(110, ArmState::Disarmed)).code == DecisionCode::DisarmedRejected,
	      "fixture_disarmed_reject_policy");

	invalid = fixtureConfig();
	invalid.acceleration_bounds.minimum_m_s2 = Vec3f(5.0F, 5.0F, 5.0F);
	invalid.acceleration_bounds.maximum_m_s2 = Vec3f(6.0F, 6.0F, 6.0F);
	invalid.acceleration_bounds.maximum_norm_m_s2 = 8.0F;
	check(!RequestGuard(invalid).configured(), "fixture_empty_axis_box_norm_intersection_rejected");

	for (DisarmedPolicy policy : {DisarmedPolicy::Reject, DisarmedPolicy::ValidateOnly}) {
		RequestGuard transition_guard(fixtureConfig(policy));
		establish(transition_guard);
		check(transition_guard.evaluate(fixtureRequest(), fixtureContext(110)).code == DecisionCode::Accepted,
		      "fixture_transition_guard_initial_request");
		CanonicalRequest malformed_after_disarm = fixtureRequest(2, 101);
		malformed_after_disarm.acceleration_frd_m_s2(0) = std::numeric_limits<float>::quiet_NaN();
		const DecisionCode expected = policy == DisarmedPolicy::Reject ? DecisionCode::DisarmedRejected : DecisionCode::NonFinite;
		check(transition_guard.evaluate(malformed_after_disarm, fixtureContext(111, ArmState::Disarmed)).code == expected,
		      "fixture_disarm_clears_before_malformed_packet");
		check(!transition_guard.getEligibleRequest(fixtureContext(112), copied, access),
		      "fixture_disarm_malformed_rearm_never_resurrects_request");
	}

	RequestGuard enum_guard(fixtureConfig());
	establish(enum_guard);
	EvaluationContext invalid_arm_enum = fixtureContext(110);
	invalid_arm_enum.arm_state = static_cast<ArmState>(99);
	check(enum_guard.evaluate(fixtureRequest(), invalid_arm_enum).code == DecisionCode::ArmStateUnknown,
	      "fixture_invalid_arm_enum_rejected");
	EvaluationContext invalid_mode_enum = fixtureContext(111);
	invalid_mode_enum.mode_state = static_cast<ResearchModeState>(99);
	check(enum_guard.evaluate(fixtureRequest(), invalid_mode_enum).code == DecisionCode::ModeStateUnknown,
	      "fixture_invalid_mode_enum_rejected");

	RequestGuard maximum_sequence_guard(fixtureConfig());
	establish(maximum_sequence_guard);
	check(maximum_sequence_guard.evaluate(fixtureRequest(std::numeric_limits<uint64_t>::max(), 100), fixtureContext(110)).code
	      == DecisionCode::Accepted, "fixture_maximum_sequence_accepts_once");
	check(maximum_sequence_guard.evaluate(fixtureRequest(std::numeric_limits<uint64_t>::max(), 101), fixtureContext(111)).code
	      == DecisionCode::DuplicateSequence, "fixture_maximum_sequence_exhaustion_rejects_reuse");
	check(maximum_sequence_guard.establishTrustedSession({9, 78}, true, fixtureContext(112, ArmState::Disarmed)).code
	      == DecisionCode::Accepted, "fixture_new_session_after_maximum_sequence_has_new_floor");

	// Compact boundary tables use explicitly non-operational fixture values.
	CanonicalRequest boundary_request = fixtureRequest(1, 100, 100, 100, 0);

	for (int axis = 0; axis < 3; ++axis) {
		for (bool upper : {false, true}) {
			CanonicalRequest request = boundary_request;
			const float bound = upper ? fixtureConfig().acceleration_bounds.maximum_m_s2(axis)
					    : fixtureConfig().acceleration_bounds.minimum_m_s2(axis);
			request.acceleration_frd_m_s2(axis) = bound;
			check(evaluateOnce(fixtureConfig(), request, fixtureContext(100)).code == DecisionCode::Accepted,
			      "fixture_axis_exact_boundary_accepted");
			request.acceleration_frd_m_s2(axis) = std::nextafter(bound,
							      upper ? std::numeric_limits<float>::infinity() : -std::numeric_limits<float>::infinity());
			check(evaluateOnce(fixtureConfig(), request, fixtureContext(100)).code == DecisionCode::AxisOutOfBounds,
			      "fixture_axis_one_ulp_beyond_rejected");
		}
	}

	ContractConfig norm_config = fixtureConfig();
	norm_config.acceleration_bounds.maximum_m_s2 = Vec3f(20.0F, 20.0F, 20.0F);
	boundary_request.acceleration_frd_m_s2 = Vec3f(12.0F, 0.0F, 0.0F);
	check(evaluateOnce(norm_config, boundary_request, fixtureContext(100)).code == DecisionCode::Accepted,
	      "fixture_norm_exact_boundary_accepted");
	boundary_request.acceleration_frd_m_s2(0) = std::nextafter(12.0F, std::numeric_limits<float>::infinity());
	check(evaluateOnce(norm_config, boundary_request, fixtureContext(100)).code == DecisionCode::NormOutOfBounds,
	      "fixture_norm_one_ulp_beyond_rejected");

	for (TimeUs duration : {TimeUs{10}, TimeUs{100}}) {
		CanonicalRequest request = fixtureRequest(1, 100, 100, duration, 0);
		check(evaluateOnce(fixtureConfig(), request, fixtureContext(100)).code == DecisionCode::Accepted,
		      "fixture_validity_exact_boundary_accepted");
	}

	for (TimeUs duration : {TimeUs{9}, TimeUs{101}}) {
		CanonicalRequest request = fixtureRequest(1, 100, 100, duration, 0);
		check(evaluateOnce(fixtureConfig(), request, fixtureContext(100)).code == DecisionCode::ValidityOutOfRange,
		      "fixture_validity_one_unit_beyond_rejected");
	}

	CanonicalRequest time_request = fixtureRequest(1, 100, 100, 100, 0);
	time_request.producer_expiry_monotonic_us = 99;
	check(evaluateOnce(fixtureConfig(), time_request, fixtureContext(100)).code == DecisionCode::TimeArithmeticInvalid,
	      "fixture_raw_expiry_underflow_rejected");
	time_request = fixtureRequest(1, 100, 100, 100, 0);
	time_request.expiry_time_in_validator_domain_us = 99;
	check(evaluateOnce(fixtureConfig(), time_request, fixtureContext(100)).code == DecisionCode::TimeArithmeticInvalid,
	      "fixture_mapped_expiry_underflow_rejected");
	time_request = fixtureRequest(1, 100, 100, 100, 0);
	time_request.expiry_time_in_validator_domain_us = 199;
	check(evaluateOnce(fixtureConfig(), time_request, fixtureContext(100)).code == DecisionCode::TimeArithmeticInvalid,
	      "fixture_raw_mapped_duration_mismatch_rejected");

	for (TimeUs creation : {TimeUs{60}, TimeUs{59}}) {
		CanonicalRequest request = fixtureRequest(1, 100, creation, 100, 0);
		const DecisionCode expected = creation == 60 ? DecisionCode::Accepted : DecisionCode::RequestTooOld;
		check(evaluateOnce(fixtureConfig(), request, fixtureContext(110)).code == expected,
		      "fixture_age_exact_and_one_unit_beyond");
	}

	for (TimeUs uncertainty : {TimeUs{5}, TimeUs{6}}) {
		CanonicalRequest request = fixtureRequest(1, 100, 105, 100, uncertainty);
		const DecisionCode expected = uncertainty == 5 ? DecisionCode::Accepted : DecisionCode::ClockUncertaintyExceeded;
		check(evaluateOnce(fixtureConfig(), request, fixtureContext(110)).code == expected,
		      "fixture_uncertainty_exact_and_one_unit_beyond");
	}
	ContractConfig future_config = fixtureConfig();
	future_config.timing.maximum_clock_uncertainty_us = 10;

	for (TimeUs mapped_creation : {TimeUs{110}, TimeUs{111}}) {
		CanonicalRequest request = fixtureRequest(1, 100, mapped_creation, 100, 10);
		const DecisionCode expected = mapped_creation == 110 ? DecisionCode::Accepted : DecisionCode::ProducerTimeInFuture;
		check(evaluateOnce(future_config, request, fixtureContext(110)).code == expected,
		      "fixture_latest_future_boundary_with_earliest_nonfuture");
	}
	time_request = fixtureRequest(1, 100, 1, 100, 5);
	check(evaluateOnce(fixtureConfig(), time_request, fixtureContext(110)).code == DecisionCode::TimeArithmeticInvalid,
	      "fixture_creation_minus_uncertainty_underflow_rejected");
	ContractConfig overflow_config = fixtureConfig();
	overflow_config.timing.minimum_validity_us = 1;
	time_request = fixtureRequest(1, 100, std::numeric_limits<TimeUs>::max() - 1, 1, 5);
	check(evaluateOnce(overflow_config, time_request, fixtureContext(110)).code == DecisionCode::TimeArithmeticInvalid,
	      "fixture_creation_plus_uncertainty_overflow_rejected");
	time_request = fixtureRequest(1, 100, std::numeric_limits<TimeUs>::max() - 20, 10, 0);
	check(evaluateOnce(overflow_config, time_request,
			   fixtureContext(std::numeric_limits<TimeUs>::max() - 5)).code == DecisionCode::TimeArithmeticInvalid,
	      "fixture_now_plus_future_skew_overflow_rejected");

	ContractConfig expiry_config = fixtureConfig();
	expiry_config.timing.maximum_command_age_us = 200;
	RequestGuard expiry_boundary_guard(expiry_config);
	establish(expiry_boundary_guard);
	check(expiry_boundary_guard.evaluate(fixtureRequest(), fixtureContext(110)).code == DecisionCode::Accepted,
	      "fixture_expiry_boundary_request_accepted");
	check(expiry_boundary_guard.poll(fixtureContext(198)).code == DecisionCode::Accepted,
	      "fixture_just_before_earliest_expiry_accepted");
	check(expiry_boundary_guard.poll(fixtureContext(199)).code == DecisionCode::RequestExpired,
	      "fixture_at_earliest_expiry_rejected");

	for (DecisionCode expected : {
		     DecisionCode::WrongProtocolVersion, DecisionCode::WrongSource, DecisionCode::WrongSession,
		     DecisionCode::WrongFrame, DecisionCode::WrongQuantity, DecisionCode::WrongReferencePoint
	     }) {
		CanonicalRequest request = fixtureRequest(1, 100);

		switch (expected) {
		case DecisionCode::WrongProtocolVersion: request.protocol_version = 2; break;

		case DecisionCode::WrongSource: request.source = 8; break;

		case DecisionCode::WrongSession: request.session = 78; break;

		case DecisionCode::WrongFrame: request.frame = static_cast<Frame>(99); break;

		case DecisionCode::WrongQuantity: request.quantity = static_cast<Quantity>(99); break;

		case DecisionCode::WrongReferencePoint: request.reference_point = static_cast<ReferencePoint>(99); break;

		default: break;
		}

		check(evaluateOnce(fixtureConfig(), request, fixtureContext(110)).code == expected,
		      "fixture_protocol_identity_enum_rejected");
	}
	CanonicalRequest no_session_request = fixtureRequest();
	no_session_request.session = 0;
	check(evaluateOnce(fixtureConfig(), no_session_request, fixtureContext(110)).code == DecisionCode::NoSession,
	      "fixture_canonical_zero_session_rejected");
	RequestGuard administration_guard(fixtureConfig());
	check(administration_guard.establishTrustedSession({9, 77}, false, fixtureContext(100, ArmState::Disarmed)).code
	      == DecisionCode::UnauthorizedSessionEstablishment, "fixture_unauthorised_session_establishment_rejected");
	check(administration_guard.establishTrustedSession({9, 0}, true, fixtureContext(101, ArmState::Disarmed)).code
	      == DecisionCode::InvalidTrustedSession, "fixture_zero_session_establishment_rejected");
	check(administration_guard.establishTrustedSession({9, 77}, true, fixtureContext(102, ArmState::Armed)).code
	      == DecisionCode::SessionChangeWhileArmed, "fixture_armed_session_establishment_rejected");
	check(administration_guard.clearLatch(true, static_cast<ArmState>(99)).code == DecisionCode::ArmStateUnknown,
	      "fixture_invalid_arm_enum_latch_clear_rejected");

	RequestGuard preservation_guard(fixtureConfig());
	establish(preservation_guard);
	check(preservation_guard.evaluate(fixtureRequest(5, 105), fixtureContext(110)).code == DecisionCode::Accepted,
	      "fixture_preservation_request_accepted");
	preservation_guard.revoke();
	check(preservation_guard.evaluate(fixtureRequest(5, 106), fixtureContext(111)).code == DecisionCode::DuplicateSequence,
	      "fixture_explicit_revoke_preserves_sequence_floor");
	check(preservation_guard.evaluate(fixtureRequest(6, 104), fixtureContext(112)).code == DecisionCode::ProducerTimeRegressed,
	      "fixture_explicit_revoke_preserves_producer_time_floor");
	check(preservation_guard.poll(fixtureContext(113)).first_latched_code == DecisionCode::ProducerTimeRegressed,
	      "fixture_first_latch_cause_retained");

	RequestGuard no_request_guard(fixtureConfig());
	establish(no_request_guard);
	CanonicalRequest untouched = fixtureRequest(999, 999);
	Decision no_request_decision{};
	check(!no_request_guard.getEligibleRequest(fixtureContext(110), untouched, no_request_decision)
	      && no_request_decision.code == DecisionCode::NoEligibleRequest && untouched.sequence == 999,
	      "fixture_idle_get_is_nonrejection_and_preserves_output");
	check(accepted.conservative_age_valid && !guard.evaluate(foreign, fixtureContext(120)).conservative_age_valid,
	      "fixture_age_validity_diagnostics");

	struct ContextCase {
		DecisionCode expected;
		EvaluationContext context;
	};
	EvaluationContext bad_mapping = fixtureContext(111);
	bad_mapping.clock_mapping_valid = false;
	EvaluationContext bad_transport = fixtureContext(111);
	bad_transport.transport_connected = false;
	EvaluationContext bad_state = fixtureContext(111);
	bad_state.required_state_valid = false;
	EvaluationContext unknown_arm = fixtureContext(111, ArmState::Unknown);
	EvaluationContext invalid_arm = fixtureContext(111);
	invalid_arm.arm_state = static_cast<ArmState>(99);
	EvaluationContext not_selected = fixtureContext(111, ArmState::Armed, ResearchModeState::NotSelected);
	EvaluationContext unknown_mode = fixtureContext(111, ArmState::Armed, ResearchModeState::Unknown);
	EvaluationContext invalid_mode = fixtureContext(111);
	invalid_mode.mode_state = static_cast<ResearchModeState>(99);
	const ContextCase context_cases[] = {
		{DecisionCode::ClockMappingInvalid, bad_mapping},
		{DecisionCode::Disconnected, bad_transport},
		{DecisionCode::RequiredStateInvalid, bad_state},
		{DecisionCode::ArmStateUnknown, unknown_arm},
		{DecisionCode::ArmStateUnknown, invalid_arm},
		{DecisionCode::ModeNotSelected, not_selected},
		{DecisionCode::ModeStateUnknown, unknown_mode},
		{DecisionCode::ModeStateUnknown, invalid_mode}
	};

	for (const ContextCase &test_case : context_cases) {
		RequestGuard state_guard(fixtureConfig());
		establish(state_guard);
		check(state_guard.evaluate(fixtureRequest(), fixtureContext(110)).code == DecisionCode::Accepted,
		      "fixture_context_case_initial_request_accepted");
		check(state_guard.poll(test_case.context).code == test_case.expected,
		      "fixture_each_context_failure_revokes_request");
		check(!state_guard.getEligibleRequest(fixtureContext(112), copied, access)
		      && access.code == DecisionCode::NoEligibleRequest,
		      "fixture_context_recovery_needs_fresh_request");
	}

	RequestGuard precedence_guard(fixtureConfig());
	establish(precedence_guard);
	check(precedence_guard.evaluate(fixtureRequest(), fixtureContext(110)).code == DecisionCode::Accepted,
	      "fixture_precedence_initial_request_accepted");
	CanonicalRequest malformed_foreign = fixtureRequest(2, 101);
	malformed_foreign.source = 8;
	malformed_foreign.acceleration_frd_m_s2(0) = std::numeric_limits<float>::quiet_NaN();
	check(precedence_guard.evaluate(malformed_foreign, bad_mapping).code == DecisionCode::ClockMappingInvalid,
	      "fixture_global_context_loss_precedes_malformed_foreign_packet");
	additionalContractFixtures();
	fullCommandFixtures();
	std::cout << checks << " checks; " << failures << " failures\n";
	return failures == 0 ? 0 : 1;
}
