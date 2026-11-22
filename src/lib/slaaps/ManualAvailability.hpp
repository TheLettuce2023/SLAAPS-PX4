#pragma once

#include "VehicleConfiguration.hpp"

namespace slaaps
{

// Whether Commander may treat SLAAPS Manual as a mode that can run, and as one that can be armed.
//
// Commander uses the same "can arm and run" answer as the mode's run permission. A mode that cannot run
// triggers the fallback failsafe at the moment of arming, so the answer must stay true for the whole session
// the supervisor owns, and must be true before the operator arms. Arming is permitted only for a prepared
// captive-stage or launcher-stage session: every condition the supervisor itself requires before it will activate, and none
// that it will not. This grants no output permission: activation, the first handled frame, flight admission
// and every recovery response remain separate supervisor decisions.
enum class ManualAvailability : uint8_t {
	Denied,   // Cannot run or arm.
	Run,      // An owned session may keep running; arming is not offered.
	Prepared  // Disarmed preparation is complete; the operator may arm.
};

struct ManualAvailabilityState {
	bool manual_use_selected{};     // The accepted configuration scope is stabilized Manual.
	bool configuration_eligible{};  // Accepted and not invalidated; activation is not required before arming.
	bool modes_registered{};        // Registration succeeded for every SLAAPS mode.
	bool routing_valid{};
	bool power_valid{};             // Selected propulsion battery inside its bounds, original deadline not passed.
	bool input_valid{};             // Selected estimator state and Commander permission metadata fresh.
	bool prepared{};                // Entry PREPARED, both banks ready, original preparation deadline not passed.
	bool active{};                  // Activating, first command, running, recovering or re-entering.
	bool launch_airspeed_valid{};   // Launcher stage only: the configured raw airspeed sample is fresh and valid.
};

inline ManualAvailability manualAvailability(const VehicleConfiguration &configuration, const ManualAvailabilityState &state)
{
	if (!state.manual_use_selected || !state.configuration_eligible || !state.modes_registered) {
		return ManualAvailability::Denied;
	}

	// The supervisor applies the decided D1-D5 responses to an owned session. Withdrawing the run permission
	// here would only add a second, stock failsafe on top of them.
	if (state.active) { return ManualAvailability::Run; }

	if (!state.prepared || !state.routing_valid || !state.power_valid || !state.input_valid) {
		return ManualAvailability::Denied;
	}

	// Captive and launcher stages. The launcher also needs its own fresh airspeed sample, exactly what the supervisor
	// requires for flight admission; the attitude gate itself stays closed (canards at trim) until the worker opens it.
	const bool captive = configuration.launch.stage == static_cast<int32_t>(FlightStage::Captive);
	const bool launcher = configuration.launch.stage == static_cast<int32_t>(FlightStage::Launcher);

	if (!captive && !(launcher && state.launch_airspeed_valid)) { return ManualAvailability::Denied; }

	if (!validateFlightConfiguration(configuration).valid()) { return ManualAvailability::Denied; }

	return ManualAvailability::Prepared;
}

} // namespace slaaps
