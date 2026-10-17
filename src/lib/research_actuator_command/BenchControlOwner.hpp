#pragma once

#include "ManualControlInput.hpp"

namespace research_actuator_command
{

// User-approved bench candidates, not qualified hardware timing or flight policy.
constexpr RuntimeTimeUs kBenchIntervalUs{20000U};
constexpr RuntimeTimeUs kBenchSampleAgeUs{500000U};
constexpr float kBenchMaximumDeflectionDeg{13.0F};
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
};

struct BenchConfiguration {
	uint8_t mavlink_instance{kNoMavlinkManualInstance};
	uint8_t source_system_id{};
	uint8_t source_component_id{};
	BenchMix mix{}; // PX4 roll, forward-stick pitch, yaw -> normalized C1..C4.
	FixedArray<BenchServoCalibration, kBenchCanardCount> servos{};
	float neutral_entry_tolerance{}; // Caller-measured joystick deadband.
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

	BenchStatus configure(const BenchConfiguration &configuration);
	BenchStatus enable(RuntimeTimeUs now_us, const ManualControlSample &sample);
	BenchStatus cycle(RuntimeTimeUs now_us, const ManualControlSample &sample, BenchReport &report);
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
	BenchStatus latch(BenchStatus fault);
	BenchReport makeReport(RuntimeTimeUs now_us, const Command &command, float scale, bool neutral) const;
	BenchConfiguration _configuration{};
	LimiterConfiguration _limits{};
	ManualControlInput _input{};
	BenchReport _last_report{};
	RuntimeTimeUs _last_cycle_us{};
	BenchState _state{BenchState::Disabled};
	BenchStatus _fault{BenchStatus::Ok};
	ManualControlStatus _input_status{ManualControlStatus::NotConfigured};
};

} // namespace research_actuator_command
