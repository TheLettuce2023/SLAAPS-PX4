#pragma once

#include "VehicleConfiguration.hpp"
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
};

struct AccelerationControlOutput {
	float canard[kCanards] {}; // Logical C1..C4, normalized calibrated deflections.
	float edf_thrust_n{}; // Passed to EdfOwner, never directly to a motor provider.
};

enum class AccelerationPortStatus : uint8_t { Unavailable, Rejected, Calculated };

// Intended extension point only; no production acceleration controller exists.
// The future implementation must retrieve eligible requests through RequestGuard
// on each cycle, retain body-FRD inertial acceleration at the CG in m/s^2,
// and retain the body-fixed vector between updates (not inertial-frame hold).
// Configuration is an immutable measured snapshot; dynamic state must come
// from the same selected-state owner as manual mode. Output is a calculation,
// not permission: the shared supervisor supplies ownership tokens and checks
// mode/arming/health, routing, command loss and driver handoff independently.
class AccelerationControllerPort
{
public:
	virtual ~AccelerationControllerPort() = default;
	virtual bool available() const { return false; }
	virtual AccelerationPortStatus configure(const VehicleConfiguration &)
	{
		return AccelerationPortStatus::Unavailable;
	}
	virtual void disable() {}
	virtual AccelerationPortStatus cycle(const AccelerationControlInput &, AccelerationControlOutput &)
	{
		// Preserve caller output on unavailable/rejected calculation.
		return AccelerationPortStatus::Unavailable;
	}
};

} // namespace slaaps
