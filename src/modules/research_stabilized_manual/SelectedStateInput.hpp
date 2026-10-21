#pragma once

#include <lib/research_stabilized_manual/SelectedStateObservation.hpp>
#include <lib/research_stabilized_manual/ObserverProfile.hpp>

namespace research_stabilized_manual
{

struct SelectedStateSnapshot : SelectedStateObservationSnapshot {
	bool prearmed_servo_condition{};
};

// Bench/observer compatibility adapter. Armed observations never qualify here.
class SelectedStateInput
{
public:
	SelectedStateSnapshot capture(const ObserverProfile &profile)
	{
		SelectedStateSnapshot result{};
		static_cast<SelectedStateObservationSnapshot &>(result) =
			_observation.capture(profile.maximum_status_age_us, profile.maximum_permission_age_us);
		const auto &armed = result.permission;
		result.state.valid = result.state.valid && result.permission_fresh && !armed.armed
				     && !armed.kill && !armed.lockdown && !armed.termination && !armed.in_esc_calibration_mode;
		result.prearmed_servo_condition = result.state.valid && armed.prearmed;
		return result;
	}
private:
	SelectedStateObservation _observation{};
};

} // namespace research_stabilized_manual
