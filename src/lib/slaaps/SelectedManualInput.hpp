#pragma once

#include <lib/research_stabilized_manual/SelectedStateObservation.hpp>

namespace slaaps
{

struct SelectedInputPolicy {
	uint64_t maximum_status_age_us{};
	uint64_t maximum_permission_age_us{};
};

// Session-bound metadata prerequisite, explicitly bound to armed or disarmed
// observation. The caller separately validates numerical state, manual sender/
// axes, reset counters and sample continuity. No authority is granted here. A
// fault latches until a deliberate new begin; callers serialize observation,
// permission, this guard and worker execution.
class SelectedManualInput
{
public:
	using Snapshot = research_stabilized_manual::SelectedStateObservationSnapshot;
	bool begin(uint64_t now, const Snapshot &snapshot, const SelectedInputPolicy &policy)
	{
		return beginImpl(now, snapshot, policy, false);
	}
	bool beginDisarmed(uint64_t now, const Snapshot &snapshot, const SelectedInputPolicy &policy)
	{
		return beginImpl(now, snapshot, policy, true);
	}
	bool check(uint64_t now, const Snapshot &snapshot) { return checkImpl(now, snapshot, false); }
	bool checkDisarmed(uint64_t now, const Snapshot &snapshot) { return checkImpl(now, snapshot, true); }
	void stop() { _active = false; }
	bool active() const { return _active; }
private:
	bool beginImpl(uint64_t now, const Snapshot &snapshot, const SelectedInputPolicy &policy, bool disarmed)
	{
		stop();
		_policy = policy;
		_disarmed_session = disarmed;

		if (!valid(now, snapshot)) { return false; }

		for (unsigned i = 0; i < research_stabilized_manual::PublisherBinding::kTopics; ++i) {
			if (!snapshot.revisions[i]) { return false; }

			_revisions[i] = snapshot.revisions[i];
		}

		_gyro = snapshot.gyro;
		_accel = snapshot.accel;
		_epoch = snapshot.state.source_epoch;
		remember(now, snapshot);
		_active = true;
		return true;
	}
	bool checkImpl(uint64_t now, const Snapshot &snapshot, bool disarmed)
	{
		if (!_active) { return false; }

		if (_disarmed_session != disarmed) {
			stop();
			return false;
		}

		bool same = true;

		for (unsigned i = 0; i < research_stabilized_manual::PublisherBinding::kTopics; ++i) {
			same &= snapshot.revisions[i] == _revisions[i];
		}

		if (!same || !valid(now, snapshot) || now < _clock || snapshot.now < _capture_time
		    || snapshot.estimator_time < _estimator_time || snapshot.flags_time < _flags_time
		    || snapshot.permission.timestamp < _permission_time || snapshot.gyro != _gyro || snapshot.accel != _accel
		    || snapshot.state.source_epoch != _epoch) {
			stop();
			return false;
		}

		remember(now, snapshot);
		return true;
	}
	static bool fresh(uint64_t now, uint64_t stamp, uint64_t maximum)
	{
		return maximum && stamp && stamp <= now && now - stamp <= maximum;
	}
	bool valid(uint64_t now, const Snapshot &snapshot) const
	{
		const auto &armed = snapshot.permission;
		return snapshot.now && snapshot.now <= now && snapshot.state.valid && snapshot.state.source_epoch
		       && snapshot.gyro && snapshot.accel && snapshot.permission_fresh
		       && fresh(now, snapshot.estimator_time, _policy.maximum_status_age_us)
		       && fresh(now, snapshot.flags_time, _policy.maximum_status_age_us)
		       && fresh(now, armed.timestamp, _policy.maximum_permission_age_us)
		       && (_disarmed_session ? !armed.armed : armed.armed)
		       && !armed.kill && !armed.lockdown && !armed.termination && !armed.in_esc_calibration_mode;
	}
	void remember(uint64_t now, const Snapshot &snapshot)
	{
		_clock = now;
		_capture_time = snapshot.now;
		_estimator_time = snapshot.estimator_time;
		_flags_time = snapshot.flags_time;
		_permission_time = snapshot.permission.timestamp;
	}
	SelectedInputPolicy _policy{};
	unsigned _revisions[research_stabilized_manual::PublisherBinding::kTopics] {};
	uint64_t _clock{}, _capture_time{}, _estimator_time{}, _flags_time{}, _permission_time{};
	uint32_t _gyro{}, _accel{}, _epoch{};
	bool _active{};
	bool _disarmed_session{};
};

} // namespace slaaps
