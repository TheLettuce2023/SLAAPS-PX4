#pragma once

#include "VehicleConfiguration.hpp"
#include "AccelerationFeedback.hpp"
#include "ControlSchedule.hpp"
#include "WrenchAllocator.hpp"
#include <lib/research_acceleration/RequestGuard.hpp>

namespace slaaps
{

// The producer-side wire/session/time-map validation remains owned by the
// existing research_acceleration guard. Never turn this port into a second
// wire parser or accept a raw acceleration vector as an actuator command.
struct AccelerationControlInput {
	research_acceleration::CanonicalRequest request{};
	research_acceleration::EvaluationContext context{};
	research_stabilized_manual::StateSample selected_state{};
	uint64_t configuration{};
	uint64_t now_us{}, previous_handled_us{};
	uint64_t evidence_deadline_us{}; // Original trusted map/link/Commander/state observation minimum.
	AccelerationFeedbackSample feedback{};
	OperatingConditionSample operating_condition{};
	float previous_handled_canard[kCanards] {};
	research_stabilized_manual::Vector3f previous_handled_rate_setpoint{};
	float actual_edf_command{}, actual_edf_thrust_n{};
	uint64_t actual_edf_deadline_us{};
};

struct AccelerationControlOutput {
	float canard[kCanards] {}; // Logical C1..C4, normalized calibrated deflections.
	float edf_thrust_n{}; // Passed to EdfOwner, never directly to a motor provider.
	uint64_t deadline_us{}; // Original new-command evidence; never the old EDF request's lease.
	uint64_t current_deadline_us{};
	float current_edf_thrust_n{}; // Current owner COMMAND/curve model, not measured thrust.
	float requested_wrench[kWrenchAxes] {}, achieved_wrench[kWrenchAxes] {}, residual[kWrenchAxes] {};
	research_stabilized_manual::Vector3f rate_setpoint_rad_s{}, integral{}, acceleration_correction{};
	research_stabilized_manual::Vector3f requested_normalized_moment{};
	matrix::Vector3<bool> saturation_positive{}, saturation_negative{};
	uint8_t canard_limit_mask{}; // Model hard/rate bounds: lower C1..4 bits0..3, upper bits4..7; not measured saturation.
};

enum class AccelerationPortStatus : uint8_t { Unavailable, Rejected, Calculated, Ready, NotDue, Pending };
enum class AccelerationControlFault : uint8_t {
	None, Disabled, InvalidConfiguration, InvalidInput, Expired, StateChanged, InvalidSchedule,
	GainTransitionRejected, AllocationRejected, ArithmeticFailure, TimeRegression
};
enum class AccelerationAllocationStage : uint8_t { None, FutureTarget, CurrentOwnerThrust };

// Numerical calculation only. The serialized worker must retrieve the request
// with WireAdapter::getEligibleFullRequest on every cycle/monitor turn. This
// object cannot establish authenticated ingress, session or output permission.
// Acceleration stays body-held FRD; desired quaternion is absolute NED-from-body.
// Configuration is an immutable measured snapshot; dynamic state must come
// from the same selected-state owner as manual mode. Output is a calculation,
// not permission: the shared supervisor supplies ownership tokens and checks
// mode/arming/health, routing, command loss and driver handoff independently.
class AccelerationControllerPort
{
public:
	virtual ~AccelerationControllerPort() = default;
	AccelerationControllerPort() = default;
	AccelerationControllerPort(const AccelerationControllerPort &) = delete;
	AccelerationControllerPort &operator=(const AccelerationControllerPort &) = delete;
	virtual bool available() const { return _configured && !_failed; }
	virtual AccelerationPortStatus configure(const VehicleConfiguration &configuration);
	virtual void disable();
	virtual AccelerationPortStatus cycle(const AccelerationControlInput &input, AccelerationControlOutput &output);
	AccelerationPortStatus monitor(const AccelerationControlInput &input);
	// Privileged callback from the existing serialized worker ONLY after its
	// exact all-bank command-ticket ACK check and monitor at final observation
	// time. handled_us is the maximum real bank ACK time, never calculation time.
	// No external ACK/counter API.
	bool commitHandled(uint64_t handled_us);
	void discard() { disable(); }
	bool pending() const { return _pending; }
	AccelerationControlFault fault() const { return _fault; }
	AccelerationAllocationStage allocationStage() const { return _allocation_stage; }
	WrenchAllocationResult allocationResult() const { return _allocation_result; }
private:
	AccelerationPortStatus reject(AccelerationControlFault reason);
	AccelerationPortStatus inspect(const AccelerationControlInput &input,
				       research_stabilized_manual::Quaternion &attitude, uint64_t &deadline);
	VehicleConfiguration _configuration{};
	research_stabilized_manual::Configuration _numerical{};
	research_stabilized_manual::AllocationConfiguration _allocation_validation{};
	ControlSchedule _schedule{};
	ControlSchedule::Step _pending_step{};
	RateControl _rate_control{}, _pending_rate_control{};
	research_stabilized_manual::Vector3f _rate_setpoint{}, _pending_rate_setpoint{};
	research_stabilized_manual::StateSample _observed_state{};
	AccelerationFeedbackSample _observed_feedback{};
	float _handled_canard[kCanards] {}, _pending_canard[kCanards] {};
	uint64_t _identity{}, _clock{}, _last_cycle_us{}, _last_handled_us{}, _pending_cycle_us{}, _pending_deadline_us{};
	uint64_t _source{}, _session{};
	bool _configured{}, _failed{}, _pending{}, _schedule_started{}, _observed{}, _committed{};
	AccelerationControlFault _fault{AccelerationControlFault::Disabled};
	AccelerationAllocationStage _allocation_stage{AccelerationAllocationStage::None};
	WrenchAllocationResult _allocation_result{WrenchAllocationResult::Success};
};

} // namespace slaaps
