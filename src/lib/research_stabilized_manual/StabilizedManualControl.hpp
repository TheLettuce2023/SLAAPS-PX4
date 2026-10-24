#pragma once

#include "CanardAllocator.hpp"
#include <lib/research_actuator_command/ManualControlInput.hpp>
#include <lib/rate_control/rate_control.hpp>

namespace research_stabilized_manual
{

using TimeUs = uint64_t;
using Quaternion = matrix::Quatf; // NED from body FRD.
using ManualSample = research_actuator_command::ManualControlSample;

struct Configuration {
	research_actuator_command::ManualSourcePolicy source_policy{research_actuator_command::ManualSourcePolicy::MavlinkOnly};
	uint8_t mavlink_instance{research_actuator_command::kNoMavlinkManualInstance};
	uint8_t source_system_id{};
	uint8_t source_component_id{};
	TimeUs maximum_manual_age_us{};
	TimeUs maximum_state_age_us{};
	TimeUs maximum_state_skew_us{};
	TimeUs minimum_cycle_us{};
	TimeUs maximum_cycle_us{};
	float quaternion_norm_tolerance{};
	float centered_enter{};
	float centered_exit{};
	uint8_t stick_axis[3] {0, 1, 2}; // Body roll/pitch/yaw -> selected attitude slot.
	Vector3f stick_sign{}; // Exactly +/-1: stock forward pitch stick normally needs -1.
	Vector3f maximum_rate_rad_s{};
	Vector3f maximum_rate_setpoint_accel_rad_s2{};
	Vector3f maximum_measured_rate_rad_s{};
	Vector3f maximum_measured_accel_rad_s2{};
	Vector3f attitude_gain{};
	Vector3f rate_p{};
	Vector3f rate_i{};
	Vector3f rate_d{};
	Vector3f integrator_limit{};
	Vector3f maximum_normalized_moment{};
	float fixed_simulated_thrust_n{};
	AllocationConfiguration allocation{};
};

struct StateSample {
	TimeUs attitude_timestamp_us{};
	TimeUs attitude_sample_us{};
	TimeUs rate_timestamp_us{};
	TimeUs rate_sample_us{};
	Quaternion attitude{};
	Vector3f rate_rad_s{};
	Vector3f angular_accel_rad_s2{};
	uint32_t source_epoch{}; // Adapter-bound estimator/gyro lifecycle; changes revoke.
	uint8_t attitude_reset_counter{};
	bool valid{};
};

// Already selected/interpolated by the caller from an accepted calibrated
// operating envelope. This point grants no source, timing or flight authority.
// Moment normalization, trim, limits and servo dynamics remain unchanged.
struct ScheduledControlPoint {
	Vector3f rate_p{};
	Vector3f rate_i{};
	Vector3f rate_d{};
	MomentMatrix effectiveness{};
};

enum class State : uint8_t { Disabled, Ready, Rate, Hold, Fault };
enum class Status : uint8_t {
	Ok, InvalidConfiguration, InvalidState, InvalidTime, NotDue, InputRejected,
	StateRejected, StateChanged, NotCentered, ArithmeticFailure, AllocationFailure,
	InvalidSchedule, GainTransitionRejected
};

struct Report {
	TimeUs timestamp_us{};
	State state{State::Disabled};
	Quaternion hold_reference{};
	Vector3f rate_setpoint_rad_s{};
	Vector3f requested_moment{}; // Unclamped PID value; residual includes controller limiting.
	Vector3f bounded_moment{};
	Vector3f integral{};
	AllocationReport allocation{};
	float fixed_simulated_thrust_n{};
};

// One allocation-free output-disabled owner. No topic publication, hardware,
// arming, source selection or automatic recovery. Failed cycles preserve the
// caller report, latch Fault and revoke eligibility. disable/configure/enable
// is the explicit reset sequence; the output supervisor must honor eligibility.
class StabilizedManualControl
{
public:
	StabilizedManualControl() = default;
	StabilizedManualControl(const StabilizedManualControl &) = delete;
	StabilizedManualControl &operator=(const StabilizedManualControl &) = delete;
	// Stateless counterpart of configure's checks; no controller history/PID allocation.
	static Status validateConfiguration(const Configuration &configuration);
	Status configure(const Configuration &configuration);
	Status enable(TimeUs now, const ManualSample &manual, const StateSample &sample);
	Status cycle(TimeUs now, const ManualSample &manual, const StateSample &sample, Report &report);
	// Preserve control history and commit both gains/effectiveness only after a
	// complete successful cycle. An infeasible gain transition latches Fault;
	// neither the caller report nor the previously accepted gains/matrix change.
	Status cycle(TimeUs now, const ManualSample &manual, const StateSample &sample, Report &report,
		     const ScheduledControlPoint &scheduled);
	// Validate live input while output acknowledgment is pending, without
	// integrating PID, changing hold reference or advancing allocation history.
	Status monitor(TimeUs now, const ManualSample &manual, const StateSample &sample);
	void disable();
	State state() const { return _state; }
	Status fault() const { return _fault; }
	bool eligible() const { return _state == State::Rate || _state == State::Hold; }
	static constexpr bool outputsEnabled() { return false; }

private:
	Status cycleImpl(TimeUs now, const ManualSample &manual, const StateSample &sample, Report &report,
			 const ScheduledControlPoint *scheduled);
	Status inspect(TimeUs now, const ManualSample &manual, const StateSample &sample, Quaternion &attitude);
	Status validateState(TimeUs now, const StateSample &sample, Quaternion &attitude) const;
	Status reject(Status reason);
	Configuration _configuration{};
	research_actuator_command::ManualControlInput _manual{};
	RateControl _rate_control{};
	StateSample _last_sample{};
	Quaternion _reference{};
	Vector3f _rate_setpoint{};
	CanardVector _last_command{};
	TimeUs _last_cycle_us{};
	TimeUs _last_observed_us{};
	State _state{State::Disabled};
	Status _fault{Status::Ok};
};

} // namespace research_stabilized_manual
