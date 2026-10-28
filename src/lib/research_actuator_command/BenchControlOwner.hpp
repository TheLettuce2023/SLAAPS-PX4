#pragma once

#include "ManualControlInput.hpp"

namespace research_actuator_command
{

// User-approved bench candidates, not qualified hardware timing or flight policy.
constexpr RuntimeTimeUs kBenchIntervalUs{20000U};
constexpr RuntimeTimeUs kBenchSampleAgeUs{500000U};
// Declared SLAAPS vehicle travel limit (project lead, 2026-10-27); was 13 deg. Commanded travel stays within SLA_Cn_MIN/MAX.
constexpr float kBenchMaximumDeflectionDeg{15.0F};
constexpr std::size_t kBenchCanardCount{4};
constexpr std::size_t kBenchAxisCount{3};
using BenchVector = FixedArray<float, kBenchCanardCount>;
using BenchMix = FixedArray<FixedArray<float, kBenchAxisCount>, kBenchCanardCount>;

struct BenchServoCalibration {
	// Pulses at -maximum_deflection, zero, +maximum_deflection. A decreasing
	// sequence encodes reversal exactly once. No nominal linkage is supplied.
	float negative_pwm_us{};
	float neutral_pwm_us{};
	float positive_pwm_us{};
	float maximum_deflection_deg{};
	float rise_deg_per_s{};
	float fall_deg_per_s{};
	// Travel within the same normalized PWM calibration. Physical neutral is
	// zero, independent of a flight trim. Appended defaults preserve file profiles.
	float minimum_normalized{-1.0F};
	float maximum_normalized{1.0F};
};

struct BenchConfiguration {
	ManualSourcePolicy source_policy{ManualSourcePolicy::MavlinkOnly};
	uint8_t mavlink_instance{kNoMavlinkManualInstance};
	uint8_t source_system_id{};
	uint8_t source_component_id{};
	uint8_t stick_axis[kBenchAxisCount] {0, 1, 2};
	float stick_sign[kBenchAxisCount] {1.F, 1.F, 1.F};
	BenchMix mix{}; // Logical roll/pitch/yaw demand -> normalized C1..C4.
	FixedArray<BenchServoCalibration, kBenchCanardCount> servos{};
	float neutral_entry_tolerance{}; // Caller-measured joystick deadband.
	RuntimeTimeUs maximum_manual_age_us{kBenchSampleAgeUs};
	RuntimeTimeUs maximum_cycle_gap_us{}; // Requires measured scheduling budget.
};

enum class BenchState : uint8_t { Disabled, Ready, Active, Fault };
enum class BenchStatus : uint8_t {
	Ok, InvalidState, InvalidConfiguration, InvalidTime, NotDue,
	InputRejected, NotCentered, CycleGap, LimiterFailure
};

struct BenchReport {
	RuntimeTimeUs timestamp_us{};
	Command normalized{}; // EDF always zero; C1..C4 follow.
	BenchVector deflection_deg{};
	BenchVector pwm_us{}; // Float candidate; no driver rounding or publication.
	float mix_scale{1.0F};
	bool neutral_requested{true};
};

// Single serialized owner for a servo-only bench candidate. Does not use H2/H3
// aerodynamic effectiveness, assert physical qualification, arm or publish.
// Failure latches a fault without replacing lastAcceptedReport(). The separate
// neutral request must be serviced by a future qualified output supervisor;
// preserving a successful calculation is NOT permission to hold an actuator.
class BenchControlOwner
{
public:
	BenchControlOwner() = default;
	BenchControlOwner(const BenchControlOwner &) = delete;
	BenchControlOwner &operator=(const BenchControlOwner &) = delete;
	BenchControlOwner(BenchControlOwner &&) = delete;
	BenchControlOwner &operator=(BenchControlOwner &&) = delete;

	// Zero selects the configured combined mix; 1..4 select one canard driven
	// by logical roll. Selection and optional shaping are fixed until reset.
	BenchStatus configure(const BenchConfiguration &configuration, unsigned target = 0, float deadzone = 0.F);
	static BenchStatus validateConfiguration(const BenchConfiguration &configuration, unsigned target = 0, float deadzone = 0.F);
	BenchStatus enable(RuntimeTimeUs now_us, const ManualControlSample &sample);
	BenchStatus cycle(RuntimeTimeUs now_us, const ManualControlSample &sample, BenchReport &report);
	// Validate raw input while the output acknowledgment is pending, without
	// advancing slew history or calculating another command.
	BenchStatus monitor(RuntimeTimeUs now_us, const ManualControlSample &sample);
	void disable();
	void reset();
	bool neutralCandidate(BenchReport &report) const;
	BenchState state() const { return _state; }
	BenchStatus fault() const { return _fault; }
	ManualControlStatus inputStatus() const { return _input_status; }
	bool neutralRequested() const { return _state != BenchState::Active; }
	const BenchReport &lastAcceptedReport() const { return _last_report; }
	static constexpr bool outputsEnabled() { return false; }

private:
	BenchStatus inspect(RuntimeTimeUs now_us, const ManualControlSample &sample, RuntimeInput &input);
	BenchStatus latch(BenchStatus fault);
	BenchReport makeReport(RuntimeTimeUs now_us, const Command &command, float scale, bool neutral) const;
	BenchConfiguration _configuration{};
	LimiterConfiguration _limits{};
	ManualControlInput _input{};
	BenchReport _last_report{};
	RuntimeTimeUs _last_cycle_us{};
	RuntimeTimeUs _last_observed_us{};
	float _deadzone{};
	unsigned _target{};
	BenchState _state{BenchState::Disabled};
	BenchStatus _fault{BenchStatus::Ok};
	ManualControlStatus _input_status{ManualControlStatus::NotConfigured};
};

} // namespace research_actuator_command
