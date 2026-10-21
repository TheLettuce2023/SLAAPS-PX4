#pragma once

#include "SelectedStateInput.hpp"
#include <lib/research_stabilized_manual/StabilizedInputValidation.hpp>
#include <cmath>

namespace research_stabilized_manual
{

// Independent, nonintegrating input guard. Profile must remain immutable and
// outlive this serialized owner. begin is an explicit grant prerequisite;
// check never binds a new source or recovers a fault. No output authority here.
class SelectedStateGuard
{
public:
	explicit SelectedStateGuard(const ObserverProfile &profile) : _profile(profile) {}
	SelectedStateGuard(const SelectedStateGuard &) = delete;
	SelectedStateGuard &operator=(const SelectedStateGuard &) = delete;
	bool begin(const SelectedStateSnapshot &snapshot)
	{
		if (_bound || _faulted || !_profile.maximum_status_age_us || !_profile.maximum_permission_age_us) { return false; }

		StabilizedManualControl validation;

		if (validation.configure(_profile.control) != Status::Ok
		    || configureStabilizedManualInput(_profile.control, _manual) != research_actuator_command::ManualControlStatus::Ok) { return false; }

		if (!snapshot.now || !snapshot.prearmed_servo_condition || !valid(snapshot, nullptr)) { return false; }

		for (unsigned i = 0; i < 3; ++i) {
			if (std::fabs(snapshot.manual.axes[i]) > _profile.control.centered_enter) { return false; }
		}

		_gyro = snapshot.gyro;
		_accel = snapshot.accel;
		remember(snapshot);
		_bound = true;
		return true;
	}
	bool check(const SelectedStateSnapshot &snapshot)
	{
		if (!_bound || _faulted) { return false; }

		if (snapshot.now < _clock || !snapshot.prearmed_servo_condition
		    || snapshot.gyro != _gyro || snapshot.accel != _accel || snapshot.estimator_time < _estimator_time
		    || !valid(snapshot, &_last)) {
			_faulted = true;
			return false;
		}

		remember(snapshot);
		return true;
	}
	void disable() { _bound = false; _faulted = false; }
	bool bound() const { return _bound; }
	bool faulted() const { return _faulted; }
private:
	bool valid(const SelectedStateSnapshot &snapshot, const StateSample *previous)
	{
		Quaternion normalized;
		research_actuator_command::RuntimeInput discarded{};
		research_actuator_command::ManualControlReport report{};
		return validateStabilizedState(snapshot.now, snapshot.state, _profile.control, previous, normalized) == Status::Ok
		       && _manual.evaluate(snapshot.now, snapshot.manual, discarded, report) == research_actuator_command::ManualControlStatus::Ok;
	}
	void remember(const SelectedStateSnapshot &snapshot)
	{
		_clock = snapshot.now;
		_estimator_time = snapshot.estimator_time;
		_last = snapshot.state;
	}
	const ObserverProfile &_profile;
	research_actuator_command::ManualControlInput _manual{};
	StateSample _last{};
	TimeUs _clock{}, _estimator_time{};
	uint32_t _gyro{}, _accel{};
	bool _bound{}, _faulted{};
};

} // namespace research_stabilized_manual
