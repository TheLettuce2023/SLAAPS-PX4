#pragma once

#include "ControlRuntimeOwner.hpp"

#include <cstddef>
#include <cstdint>

namespace research_actuator_command
{

constexpr std::size_t kManualAxisCount{4};
constexpr uint8_t kMaximumMavlinkManualInstance{5U};
constexpr uint8_t kNoMavlinkManualInstance{0xFFU};
constexpr uint8_t kAllManualAxesMask{0x0FU};
constexpr uint8_t kAttitudeManualAxesMask{0x07U};

enum class ManualAxis : std::size_t {
	Roll = 0,
	Pitch,
	Yaw,
	Throttle
};

enum class ManualControlSource : uint8_t {
	Unknown,
	Rc,
	Mavlink
};

enum class ManualSourcePolicy : uint8_t {
	RcOnly,
	MavlinkOnly,
	RcOrMavlink
};

enum class ManualControlStatus : uint8_t {
	Ok,
	NotConfigured,
	InvalidConfiguration,
	NoSample,
	InvalidTime,
	InvalidSample,
	InvalidSource,
	SourceRejected,
	IdentityRejected,
	NonFinite,
	OutOfRange,
	StaleSample,
	RegressedSample,
	ConflictingSample,
	ArithmeticFailure
};

using ManualAxisVector = FixedArray<float, kManualAxisCount>;
using ManualDemandMap = FixedArray<ManualAxisVector, kControlAxisCount>;
using ManualCommandMap = FixedArray<ManualAxisVector, kChannelCount>;

struct ManualControlConfiguration {
	// Disabled axes must have zero map columns and are ignored, including NaN.
	// The default preserves H6's original four-axis contract.
	uint8_t required_axis_mask{kAllManualAxesMask};
	ManualSourcePolicy source_policy{};
	RuntimeTimeUs maximum_sample_age_us{};
	uint8_t mavlink_instance{kNoMavlinkManualInstance};
	uint8_t mavlink_system_id{};
	uint8_t mavlink_component_id{};
	ManualDemandMap demand_map{};
	ControlVector demand_offset{};
	ManualCommandMap command_map{};
	Command command_offset{};
};

struct ManualControlSample {
	RuntimeTimeUs timestamp_us{};
	RuntimeTimeUs timestamp_sample_us{};
	ManualControlSource source{ManualControlSource::Unknown};
	uint8_t mavlink_instance{kNoMavlinkManualInstance};
	uint8_t source_system_id{};
	uint8_t source_component_id{};
	ManualAxisVector axes{};
	uint16_t buttons{};
	bool valid{};
	bool sticks_moving{};
};

struct ManualControlReport {
	RuntimeTimeUs timestamp_us{};
	RuntimeTimeUs timestamp_sample_us{};
	RuntimeTimeUs sample_age_us{};
	ManualControlSource source{ManualControlSource::Unknown};
	uint8_t mavlink_instance{kNoMavlinkManualInstance};
	uint8_t source_system_id{};
	uint8_t source_component_id{};
	uint16_t buttons{};
	bool new_sample{};
	bool sticks_moving{};
};

// Output-disabled boundary from PX4-selected manual input to the existing H5
// runtime-input contract. The caller supplies every source, identity, freshness
// and mapping value. There is no source priority, timeout, gain, mix, trim or
// actuator default.
//
// Selected input axes preserve PX4 manual_control_setpoint semantics. The two
// caller-provided affine maps independently form normalized control demand and
// the proposed EDF-plus-four-canard command. This class does not assert that the
// maps are physically correct, solve allocation, arm, publish or produce output.
//
// A successful evaluation accepts either a new raw sample or an unchanged held
// sample that remains fresh. Reusing a timestamp with changed source, identity,
// axes or buttons is rejected. Every failure preserves the last accepted sample
// and both caller outputs.
class ManualControlInput
{
public:
	ManualControlInput() = default;
	ManualControlInput(const ManualControlInput &) = delete;
	ManualControlInput &operator=(const ManualControlInput &) = delete;
	ManualControlInput(ManualControlInput &&) = delete;
	ManualControlInput &operator=(ManualControlInput &&) = delete;

	ManualControlStatus configure(const ManualControlConfiguration &configuration);
	ManualControlStatus evaluate(RuntimeTimeUs now_us,
				     const ManualControlSample &sample,
				     RuntimeInput &input,
				     ManualControlReport &report);
	void reset();

	bool configured() const { return _configured; }
	RuntimeTimeUs lastAcceptedSampleTimeUs() const { return _last_sample.timestamp_sample_us; }
	const ManualControlConfiguration &configuration() const { return _configuration; }

private:
	ManualControlConfiguration _configuration{};
	ManualControlSample _last_sample{};
	bool _configured{false};
	bool _have_last_sample{false};
};

} // namespace research_actuator_command
