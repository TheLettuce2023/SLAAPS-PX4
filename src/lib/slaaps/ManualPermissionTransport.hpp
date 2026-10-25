#pragma once

#include "ManualBankPreparation.hpp"
#include <uORB/topics/slaaps_manual_permission.h>

#if defined(__PX4_NUTTX) || defined(__PX4_POSIX)
#include "BoundSubscription.hpp"
#include <drivers/drv_hrt.h>
#include <cstring>
#endif

namespace slaaps
{

struct ManualPermissionSample {
	EdfPermission permission{};
	uint64_t sequence{};
};

inline bool validManualPermission(uint64_t now, const ManualPermissionSample &sample, uint64_t maximum_lease_us)
{
	const auto &p = sample.permission;
	return now && maximum_lease_us && sample.sequence && p.timestamp_us && p.timestamp_us <= now
	       && p.deadline_us > now && p.deadline_us - p.timestamp_us <= maximum_lease_us
	       && p.session && p.session <= p.timestamp_us && p.source && p.configuration && ManualBankPreparation::validMode(p.mode)
	       && (!p.transition_us || (ManualBankPreparation::flightMode(p.mode)
					&& p.transition_us > p.session && p.transition_us <= p.timestamp_us));
}

// Preserve decision time and expiry. Syntactically valid denied decisions are
// useful; decoding never grants authority or substitutes for local arming checks.
inline bool encodeManualPermission(uint64_t now, const ManualPermissionSample &sample, uint64_t maximum_lease_us,
				   slaaps_manual_permission_s &message)
{
	if (!validManualPermission(now, sample, maximum_lease_us)) { return false; }

	static_assert(static_cast<unsigned>(ControlMode::Manual) == slaaps_manual_permission_s::MANUAL, "manual wire mode");
	static_assert(static_cast<unsigned>(ControlMode::Acceleration) == slaaps_manual_permission_s::ACCELERATION, "acceleration wire mode");
	static_assert(static_cast<unsigned>(ControlMode::ServoBench) == slaaps_manual_permission_s::SERVO_BENCH, "bench wire mode");
	static_assert(static_cast<unsigned>(ControlMode::EdfBench) == slaaps_manual_permission_s::EDF_BENCH, "EDF bench wire mode");
	const auto &p = sample.permission;
	slaaps_manual_permission_s candidate{};
	candidate.timestamp = now;
	candidate.timestamp_sample = p.timestamp_us;
	candidate.deadline = p.deadline_us;
	candidate.sequence = sample.sequence;
	candidate.session = p.session;
	candidate.source = p.source;
	candidate.configuration = p.configuration;
	candidate.transition = p.transition_us;
	candidate.mode = static_cast<uint8_t>(p.mode);
	candidate.armed = p.armed;
	candidate.flight_admitted = p.flight_admitted;
	candidate.healthy = p.healthy;
	candidate.measured_condition_valid = p.measured_condition_valid;
	candidate.kill = p.kill;
	candidate.lockdown = p.lockdown;
	candidate.termination = p.termination;
	candidate.prearmed = p.prearmed;
	candidate.commissioning_admitted = p.commissioning_admitted;
	message = candidate;
	return true;
}

inline bool decodeManualPermission(uint64_t now, const slaaps_manual_permission_s &message, uint64_t maximum_lease_us,
				   ManualPermissionSample &sample)
{
	if (message.timestamp < message.timestamp_sample || message.timestamp > now
	    || !ManualBankPreparation::validMode(static_cast<ControlMode>(message.mode))) { return false; }

	ManualPermissionSample candidate{};
	auto &p = candidate.permission;
	p.timestamp_us = message.timestamp_sample;
	p.deadline_us = message.deadline;
	p.session = message.session;
	p.source = message.source;
	p.configuration = message.configuration;
	p.transition_us = message.transition;
	p.mode = static_cast<ControlMode>(message.mode);
	p.armed = message.armed;
	p.flight_admitted = message.flight_admitted;
	p.healthy = message.healthy;
	p.measured_condition_valid = message.measured_condition_valid;
	p.kill = message.kill;
	p.lockdown = message.lockdown;
	p.termination = message.termination;
	p.prearmed = message.prearmed;
	p.commissioning_admitted = message.commissioning_admitted;
	candidate.sequence = message.sequence;

	if (!validManualPermission(now, candidate, maximum_lease_us)) { return false; }

	sample = candidate;
	return true;
}

#if defined(__PX4_NUTTX) || defined(__PX4_POSIX)
// Bind once per prepared session. Actual local Commander/actuator_armed state,
// selected registered mode and routing must still be checked by the driver.
class ManualPermissionSubscription
{
public:
	bool begin(uint64_t now, const ManualPreparationRequest &request, uint64_t maximum_lease_us)
	{
		return beginImpl(now, request, maximum_lease_us, false);
	}
	bool beginLive(uint64_t &now, const ManualPreparationRequest &request, uint64_t maximum_lease_us)
	{
		now = hrt_absolute_time();
		const bool result = beginImpl(now, request, maximum_lease_us, true);
		now = hrt_absolute_time();
		return result;
	}
private:
	bool beginImpl(uint64_t &now, const ManualPreparationRequest &request, uint64_t maximum_lease_us, bool live)
	{
		stop();

		if (!ManualBankPreparation::validRequest(now, request) || !maximum_lease_us
		    || !_subscription.bind(request.permission_revision)) { return false; }

		const auto read = _subscription.read(_baseline);

		if (live) {
			now = hrt_absolute_time();

			if (!ManualBankPreparation::validRequest(now, request)) { return false; }
		}

		if (read == BoundRead::Rejected) { return false; }

		if (read == BoundRead::Data) {
			ManualPermissionSample previous{};
			slaaps_manual_permission_s invalid{};
			invalid.timestamp = _baseline.timestamp;
			const bool old_sample = _baseline.session < request.session
						&& decodeManualPermission(_baseline.timestamp, _baseline, maximum_lease_us, previous);
			const bool tombstone = memcmp(&_baseline, &invalid, sizeof(invalid)) == 0;
			_have_baseline = _baseline.timestamp <= now && (old_sample || tombstone);
		}

		_session = request.session;
		_source = request.source;
		_configuration = request.configuration;
		_earliest = request.timestamp_us;
		_lease = maximum_lease_us;
		_clock = now;
		_first_deadline = request.deadline_us;
		_mode = request.mode;
		_transition_us = 0;
		_active = true;
		return true;
	}

public:
	BoundRead read(uint64_t now, EdfPermission &permission) { return readImpl(now, permission, false, true, false); }
	// Live copies validate at post-copy HRT and return the final observation
	// time. Callers still recheck independent local authority and original expiry.
	BoundRead readLive(uint64_t &now, EdfPermission &permission)
	{
		now = hrt_absolute_time();
		const auto result = readImpl(now, permission, false, true, true);
		now = hrt_absolute_time();
		return result;
	}
	// Only an already-prepared consumer may use this before its first armed
	// grant. A retained explicit disarmed denial is not authority and may wait
	// only to the original preparation deadline, including after its own lease.
	// An armed denial/inhibit, stale positive grant or lost binding is a fault.
	// A fresh positive may also wait for independently fresh local arming
	// agreement; that never renews its lease or accepts it as an active grant.
	BoundRead awaitGrant(uint64_t now, EdfPermission &permission, bool can_accept_grant = true)
	{
		return readImpl(now, permission, true, can_accept_grant, false);
	}
	BoundRead awaitGrantLive(uint64_t &now, EdfPermission &permission, bool can_accept_grant = true)
	{
		now = hrt_absolute_time();
		const auto result = readImpl(now, permission, true, can_accept_grant, true);
		now = hrt_absolute_time();
		return result;
	}
	void stop() { _subscription.revoke(); _active = false; _seen = false; _have_baseline = false; _grant_seen = false; }
private:
	BoundRead readImpl(uint64_t &now, EdfPermission &permission, bool awaiting_grant, bool can_accept_grant, bool live)
	{
		if (!_active || now < _clock
		    || (awaiting_grant && ((!_grant_seen && now >= _first_deadline) || (_grant_seen && !can_accept_grant)))) { return reject(); }

		_clock = now;
		slaaps_manual_permission_s wire{};
		const auto result = _subscription.read(wire);

		if (live) {
			now = hrt_absolute_time();

			if (now < _clock
			    || (awaiting_grant && ((!_grant_seen && now >= _first_deadline) || (_grant_seen && !can_accept_grant)))) { return reject(); }

			_clock = now;
		}

		if (!_seen && (result == BoundRead::Waiting
			       || (result == BoundRead::Data && _have_baseline && memcmp(&wire, &_baseline, sizeof(wire)) == 0))) {
			return now < _first_deadline ? BoundRead::Waiting : reject();
		}

		ManualPermissionSample sample{};
		const bool fresh = result == BoundRead::Data && decodeManualPermission(now, wire, _lease, sample);

		if (!fresh && (!awaiting_grant || _grant_seen || result != BoundRead::Data
			       || !decodeManualPermission(wire.timestamp, wire, _lease, sample))) { return reject(); }

		const auto &p = sample.permission;
		const bool disarmed_denial = !p.armed && !p.flight_admitted && !p.healthy && !p.measured_condition_valid
					     && !p.commissioning_admitted && !p.kill && !p.lockdown && !p.termination;
		const bool transfer = _grant_seen && ManualBankPreparation::flightMode(_mode)
				      && ManualBankPreparation::flightMode(p.mode) && p.mode != _mode
				      && p.transition_us > _transition_us && p.transition_us > _session
				      && _last.timestamp_sample <= p.transition_us;
		const bool granted = ManualBankPreparation::permitted(p, transfer ? p.mode : _mode);

		if (wire.timestamp > now || (!fresh && !disarmed_denial)
		    || (awaiting_grant && !(granted || (!_grant_seen && disarmed_denial)))
		    || (!transfer && (p.mode != _mode || p.transition_us != _transition_us))
		    || (transfer && (!fresh || !granted)) || sample.permission.session != _session || sample.permission.source != _source
		    || sample.permission.configuration != _configuration || sample.permission.timestamp_us < _earliest) { return reject(); }

		if (_seen) {
			if (wire.sequence < _last.sequence
			    || (wire.sequence == _last.sequence && memcmp(&wire, &_last, sizeof(wire)) != 0)
			    || (wire.sequence > _last.sequence && (wire.timestamp_sample <= _last.timestamp_sample
					    || wire.timestamp < _last.timestamp))) { return reject(); }
		}

		_last = wire;

		// Permission-first transfer consumes its marker without changing the
		// bank's handled mode/history or output expiry. Rollback is never allowed.
		if (transfer) { _mode = p.mode; _transition_us = p.transition_us; }

		_seen = true;
		_grant_seen |= granted && (!awaiting_grant || can_accept_grant);

		if (awaiting_grant && (disarmed_denial || !can_accept_grant)) { return BoundRead::Waiting; }

		permission = sample.permission;
		return BoundRead::Data;
	}
	BoundRead reject() { stop(); return BoundRead::Rejected; }
	BoundSubscription<slaaps_manual_permission_s> _subscription{ORB_ID::slaaps_manual_permission};
	slaaps_manual_permission_s _last{};
	slaaps_manual_permission_s _baseline{};
	uint64_t _session{}, _source{}, _configuration{}, _earliest{}, _lease{}, _clock{}, _first_deadline{};
	uint64_t _transition_us{};
	bool _active{}, _seen{}, _have_baseline{}, _grant_seen{};
	ControlMode _mode{ControlMode::Manual};
};
#endif

} // namespace slaaps
