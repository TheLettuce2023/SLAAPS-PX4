#include "TrustedTimeMap.hpp"

#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>

using namespace research_acceleration;

namespace
{
int checks = 0;
int failures = 0;
constexpr uint32_t version = 0x00010000;
constexpr TimeUs maximum = std::numeric_limits<TimeUs>::max();
constexpr int64_t minimum_offset = std::numeric_limits<int64_t>::min();
constexpr int64_t maximum_offset = std::numeric_limits<int64_t>::max();

void check(bool ok, const std::string &name)
{
	++checks;

	if (!ok) {
		++failures;
		std::cerr << "FAILED: " << name << '\n';
	}
}

// All identity, time and acceleration values are non-operational fixtures.
WireRequestV1 wireFixture()
{
	return {9, 77, 3, 100, 200, 5, 6, 7, 1.5F, -2.25F, -0.F, version, 1, 1, 1};
}
TrustedClockMap mapFixture()
{
	return {true, 9, 77, 5, 7, 1000, 2, 100, 200};
}
CanonicalRequest sentinel()
{
	return {83, 89, 97, 101, 103, 107, 109, 113, 127, Frame::Frd,
		Quantity::InertialAcceleration, ReferencePoint::Cg, Vec3f(131.F, -137.F, -0.F)};
}
template<typename T>
std::array<unsigned char, sizeof(T)> bytes(const T &value)
{
	std::array<unsigned char, sizeof(T)> result{};
	std::memcpy(result.data(), &value, sizeof(T));
	return result;
}
void expectFailure(const WireRequestV1 &wire, const TrustedClockMap &map,
		   CanonicalizeCode expected, const std::string &name)
{
	CanonicalRequest output = sentinel();
	const auto before = bytes(output);
	const auto wire_before = bytes(wire);
	const auto map_before = bytes(map);
	check(canonicalizeRequestV1(wire, map, output) == expected, name + "_code");
	check(bytes(output) == before, name + "_output_unchanged");
	check(bytes(wire) == wire_before && bytes(map) == map_before, name + "_inputs_unchanged");
}

void copiedFields()
{
	const auto wire = wireFixture();
	const auto map = mapFixture();
	CanonicalRequest output = sentinel();
	check(canonicalizeRequestV1(wire, map, output) == CanonicalizeCode::Ok, "valid_map");
	check(output.protocol_version == version, "copy_protocol");
	check(output.source == 9 && output.session == 77 && output.sequence == 3, "copy_identity_sequence");
	check(output.producer_monotonic_us == 100 && output.producer_expiry_monotonic_us == 200, "copy_raw_times");
	check(output.producer_time_in_validator_domain_us == 1100 && output.expiry_time_in_validator_domain_us == 1200,
	      "positive_offset_times");
	check(output.clock_mapping_uncertainty_us == 2, "copy_trusted_uncertainty");
	check(output.frame == Frame::Frd && output.quantity == Quantity::InertialAcceleration
	      && output.reference_point == ReferencePoint::Cg, "explicit_enum_mapping");
	check(output.acceleration_frd_m_s2(0) == 1.5F && output.acceleration_frd_m_s2(1) == -2.25F
	      && output.acceleration_frd_m_s2(2) == 0.F && std::signbit(output.acceleration_frd_m_s2(2)),
	      "copy_frd_no_rotation_preserve_signed_zero");

	for (uint64_t value : {uint64_t{0}, maximum}) {
		auto changed = wire;
		changed.sequence = value;
		check(canonicalizeRequestV1(changed, map, output) == CanonicalizeCode::Ok && output.sequence == value,
		      "sequence_width_no_replay_policy");
	}
	auto high_wire = wire;
	auto high_map = map;
	high_wire.source_id = high_wire.session_id = high_wire.controller_instance_id = high_wire.transition_epoch =
				      high_wire.clock_mapping_epoch = maximum;
	high_map.source = high_map.session = high_map.controller_instance_id = high_map.map_epoch = maximum;
	high_map.uncertainty_us = maximum;
	check(canonicalizeRequestV1(high_wire, high_map, output) == CanonicalizeCode::Ok
	      && output.source == maximum && output.session == maximum && output.clock_mapping_uncertainty_us == maximum,
	      "maximum_bindings_and_uncertainty_not_admission");

	for (float value : {
		     std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(),
		     std::numeric_limits<float>::denorm_min(), -0.F
	     }) {
		auto changed = wire;
		changed.acceleration_x_frd_m_s2 = value;
		check(canonicalizeRequestV1(changed, map, output) == CanonicalizeCode::Ok
		      && output.acceleration_frd_m_s2(0) == value
		      && std::signbit(output.acceleration_frd_m_s2(0)) == std::signbit(value), "finite_vector_no_control_bounds");
	}
}

void rejectedInputs()
{
	const auto wire = wireFixture();
	const auto map = mapFixture();
	auto bad_map = map;
	bad_map.valid = false;
	expectFailure(wire, bad_map, CanonicalizeCode::MapInvalid, "invalid_map_flag");

	for (auto member : {
		     &TrustedClockMap::source, &TrustedClockMap::session,
		     &TrustedClockMap::controller_instance_id, &TrustedClockMap::map_epoch, &TrustedClockMap::uncertainty_us
	     }) {
		bad_map = map;
		bad_map.*member = 0;
		expectFailure(wire, bad_map, CanonicalizeCode::MapInvalid, "zero_map_field");
	}
	bad_map = map;
	bad_map.valid_from_producer_us = 201;
	expectFailure(wire, bad_map, CanonicalizeCode::MapIntervalInvalid, "inverted_map_interval");
	bad_map = map;
	bad_map.valid_from_producer_us = 101;
	expectFailure(wire, bad_map, CanonicalizeCode::MapIntervalInvalid, "creation_before_interval");
	bad_map = map;
	bad_map.valid_until_producer_us = 199;
	expectFailure(wire, bad_map, CanonicalizeCode::MapIntervalInvalid, "expiry_endpoint_outside_map");
	bad_map = map;
	bad_map.valid_from_producer_us = bad_map.valid_until_producer_us = 100;
	expectFailure(wire, bad_map, CanonicalizeCode::MapIntervalInvalid, "point_map_cannot_cover_duration");

	for (auto member : {
		     &WireRequestV1::source_id, &WireRequestV1::session_id,
		     &WireRequestV1::controller_instance_id, &WireRequestV1::clock_mapping_epoch
	     }) {
		for (uint64_t value : {uint64_t{0}, maximum}) {
			auto changed = wire;
			changed.*member = value;
			expectFailure(changed, map, CanonicalizeCode::MapBindingMismatch, "wire_binding_mismatch");
		}
	}
	auto changed = wire;
	changed.transition_epoch = 0;
	expectFailure(changed, map, CanonicalizeCode::WireIdentityInvalid, "zero_transition_epoch");

	for (uint32_t value : {uint32_t{0}, uint32_t{1}, version + 1, std::numeric_limits<uint32_t>::max()}) {
		changed = wire;
		changed.protocol_version = value;
		expectFailure(changed, map, CanonicalizeCode::WireVersionInvalid, "wrong_protocol");
	}

	for (auto member : {&WireRequestV1::frame, &WireRequestV1::quantity, &WireRequestV1::reference_point}) {
		for (uint8_t value : {uint8_t{0}, uint8_t{2}, uint8_t{255}}) {
			changed = wire;
			changed.*member = value;
			expectFailure(changed, map, CanonicalizeCode::WireEnumInvalid, "wrong_wire_enum");
		}
	}

	for (TimeUs expiry : {TimeUs{100}, TimeUs{99}, TimeUs{0}}) {
		changed = wire;
		changed.producer_expiry_monotonic_us = expiry;
		expectFailure(changed, map, CanonicalizeCode::WireTimeInvalid, "nonpositive_raw_duration");
	}

	for (auto member : {
		     &WireRequestV1::acceleration_x_frd_m_s2, &WireRequestV1::acceleration_y_frd_m_s2,
		     &WireRequestV1::acceleration_z_frd_m_s2
	     }) {
		for (float value : {
			     std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
			     -std::numeric_limits<float>::infinity()
		     }) {
			changed = wire;
			changed.*member = value;
			expectFailure(changed, map, CanonicalizeCode::WireNonFinite, "nonfinite_axis");
		}
	}
}
void arithmeticEdges()
{
	struct Vector {
		TimeUs creation;
		TimeUs expiry;
		int64_t offset;
		TimeUs expected_creation;
		TimeUs expected_expiry;
	};
	// Independent integer identities, not expectations produced by the converter.
	const Vector vectors[] = {
		{0, maximum, 0, 0, maximum},
		{0, 1, maximum_offset, static_cast<TimeUs>(maximum_offset), TimeUs{1} << 63},
		{TimeUs{1} << 63, (TimeUs{1} << 63) + 1, minimum_offset, 0, 1},
		{maximum - 1, maximum, minimum_offset, static_cast<TimeUs>(maximum_offset) - 1, static_cast<TimeUs>(maximum_offset)},
		{1, 2, -1, 0, 1},
		{maximum - 2, maximum - 1, 1, maximum - 1, maximum},
		{100, 200, -99, 1, 101},
		{100, 200, 1000, 1100, 1200},
	};

	for (const auto &vector : vectors) {
		auto wire = wireFixture();
		auto map = mapFixture();
		wire.producer_monotonic_us = vector.creation;
		wire.producer_expiry_monotonic_us = vector.expiry;
		map.valid_from_producer_us = vector.creation;
		map.valid_until_producer_us = vector.expiry;
		map.validator_minus_producer_us = vector.offset;
		CanonicalRequest output = sentinel();
		check(canonicalizeRequestV1(wire, map, output) == CanonicalizeCode::Ok, "boundary_mapping_success");
		check(output.producer_time_in_validator_domain_us == vector.expected_creation
		      && output.expiry_time_in_validator_domain_us == vector.expected_expiry, "boundary_mapping_exact_times");
		check(output.expiry_time_in_validator_domain_us - output.producer_time_in_validator_domain_us
		      == vector.expiry - vector.creation, "duration_preserved");
	}

	for (const auto &vector : {
		     Vector{maximum - 2, maximum - 1, 3, 0, 0},
		     Vector{maximum - 2, maximum, 1, 0, 0}, Vector{0, 2, -1, 0, 0},
		     Vector{(TimeUs{1} << 63) - 1, TimeUs{1} << 63, minimum_offset, 0, 0}
	     }) {
		auto wire = wireFixture();
		auto map = mapFixture();
		wire.producer_monotonic_us = vector.creation;
		wire.producer_expiry_monotonic_us = vector.expiry;
		map.valid_from_producer_us = 0;
		map.valid_until_producer_us = maximum;
		map.validator_minus_producer_us = vector.offset;
		expectFailure(wire, map, CanonicalizeCode::MapArithmeticInvalid, "offset_overflow_underflow");
	}
}

void existingGuardBoundary()
{
	// Existing S1 called unchanged, not a session/epoch adapter emulator.
	const ContractConfig config{version, 9, DisarmedPolicy::ValidateOnly,
		{Vec3f(-10.F, -10.F, -10.F), Vec3f(10.F, 10.F, 10.F), 12.F}, {10, 100, 50, 10, 5}, 0.01F};
	RequestGuard guard(config);
	EvaluationContext context{1100, ArmState::Disarmed, ResearchModeState::Selected, true, true, true};
	check(guard.establishTrustedSession({9, 77}, true, context).code == DecisionCode::Accepted, "fixture_trusted_session");
	CanonicalRequest output = sentinel();
	check(canonicalizeRequestV1(wireFixture(), mapFixture(), output) == CanonicalizeCode::Ok, "guard_fixture_map");
	context.arm_state = ArmState::Armed;
	context.validator_receive_time_us = 1110;
	const auto accepted = guard.evaluate(output, context);
	check(accepted.code == DecisionCode::Accepted && accepted.eligible_for_control
	      && accepted.conservative_age_valid && accepted.conservative_age_us == 12, "s1_checks_conservative_age");
	check(guard.evaluate(output, context).code == DecisionCode::DuplicateSequence, "mapping_does_not_authorize_replay");
	context.validator_receive_time_us = 1198;
	check(!guard.poll(context).eligible_for_control, "s1_not_mapping_owns_expiration");
	RequestGuard uncertain(config);
	context = {1100, ArmState::Disarmed, ResearchModeState::Selected, true, true, true};
	check(uncertain.establishTrustedSession({9, 77}, true, context).code == DecisionCode::Accepted, "fixture_second_session");
	auto map = mapFixture();
	map.uncertainty_us = 6;
	check(canonicalizeRequestV1(wireFixture(), map, output) == CanonicalizeCode::Ok, "mapping_not_uncertainty_admission");
	context.arm_state = ArmState::Armed;
	context.validator_receive_time_us = 1110;
	check(uncertain.evaluate(output, context).code == DecisionCode::ClockUncertaintyExceeded,
	      "s1_enforces_configured_uncertainty_limit");
}
} // namespace

int main()
{
	copiedFields();
	rejectedInputs();
	arithmeticEdges();
	existingGuardBoundary();
	std::cout << checks << " non-operational mapping checks, " << failures << " failures\n";
	return failures == 0 ? 0 : 1;
}
