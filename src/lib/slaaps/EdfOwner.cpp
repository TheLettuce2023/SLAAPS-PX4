#include "EdfOwner.hpp"

#include <lib/mathlib/math/Functions.hpp>
#include <cmath>

namespace slaaps
{
namespace
{
constexpr uint64_t kUsPerMs = 1000;
constexpr float kSecondsPerUs = 1.e-6F;

bool inhibited(const EdfPermission &p)
{
	return !p.armed || p.mode == ControlMode::ServoBench || p.kill || p.lockdown || p.termination;
}
}

EdfFault EdfOwner::reject(EdfFault fault)
{
	_active = false;
	_fault = fault;
	return fault;
}

EdfFault EdfOwner::lose(uint64_t now, EdfFault fault)
{
	// Only a flight grant that was actually active can start recovery; bench and explicit rejects cannot.
	if (_grant.mode == ControlMode::Manual || _grant.mode == ControlMode::Acceleration) {
		_recovery_active = true;
		_recovery_start_us = now;
		_recovery_output = _output;
		_recovery_last_us = _last_sample;
	}

	return reject(fault);
}

bool EdfOwner::permitted(uint64_t now, const EdfPermission &p) const
{
	return permitted(now, p, _grant);
}

bool EdfOwner::permitted(uint64_t now, const EdfPermission &p, const EdfPermission &grant) const
{
	const bool admitted = grant.mode == ControlMode::EdfBench ?
			      (p.commissioning_admitted && !p.flight_admitted && !p.measured_condition_valid) :
			      (!p.commissioning_admitted && p.flight_admitted && p.measured_condition_valid);
	return !inhibited(p) && admitted && p.healthy
	       && p.timestamp_us != 0 && p.session <= p.timestamp_us && p.timestamp_us <= now && now < p.deadline_us
	       && p.deadline_us - p.timestamp_us <= _lease_us
	       && p.session == grant.session && p.source == grant.source
	       && p.configuration == grant.configuration && p.mode == grant.mode && p.transition_us == grant.transition_us;
}

EdfFault EdfOwner::begin(uint64_t now, const VehicleConfiguration &configuration, const EdfPermission &permission)
{
	// Failed replacement also revokes the old grant. No overlapping owners.
	_active = false;
	_recovery_active = false;
	_received = false;
	_output = 0.F;
	_target = 0.F;
	_grant = permission;

	if (now == 0 || now < _clock) { return reject(EdfFault::InvalidTime); }

	_clock = now;

	if (permission.session == 0 || permission.session <= _last_session || permission.session > now) {
		return reject(EdfFault::InvalidGrant);
	}

	// Consume even a rejected token; retry needs a deliberate new session.
	_last_session = permission.session;

	if (permission.source == 0 || permission.configuration == 0 || permission.transition_us != 0
	    || (permission.mode != ControlMode::Manual && permission.mode != ControlMode::Acceleration
		&& permission.mode != ControlMode::EdfBench)) { return reject(EdfFault::InvalidGrant); }

	const bool commissioning = permission.mode == ControlMode::EdfBench;

	if (!validate(configuration, commissioning ? ConfigurationUse::EdfCommissioning : ConfigurationUse::StabilizedManual).valid()
	    || (commissioning && permission.configuration != fingerprint(configuration))) {
		return reject(EdfFault::InvalidConfiguration);
	}

	_configuration = configuration;
	_lease_us = static_cast<uint64_t>(configuration.output_lease_ms) * kUsPerMs;
	_grant = permission;

	if (!permitted(now, permission)) { return reject(EdfFault::PermissionLost); }

	_started_us = now;
	_request = {};
	_last_sample = now;
	_active = true;
	_fault = EdfFault::None;
	return _fault;
}

EdfFault EdfOwner::check(uint64_t now, const EdfPermission &permission)
{
	if (!_active) { return _fault; }

	if (now == 0 || now < _clock) { return reject(EdfFault::InvalidTime); }

	_clock = now;

	if (!permitted(now, permission)) { return lose(now, EdfFault::PermissionLost); }

	if ((_received && now >= _request.deadline_us)
	    || (!_received && now - _started_us >= _lease_us)) { return lose(now, EdfFault::Expired); }

	return EdfFault::None;
}

EdfFault EdfOwner::accept(uint64_t now, const EdfPermission &permission, const EdfRequest &request)
{
	if (check(now, permission) != EdfFault::None) { return _fault; }

	if (request.session != _grant.session || request.source != _grant.source
	    || request.configuration != _grant.configuration || request.mode != _grant.mode) {
		return reject(EdfFault::WrongOwner);
	}

	if (request.sequence == 0 || request.sequence <= _request.sequence
	    || request.timestamp_us < _grant.session || request.timestamp_us > now
	    || (_received && request.timestamp_us <= _request.timestamp_us)
	    || request.deadline_us <= now || request.deadline_us - request.timestamp_us > _lease_us
	    || !std::isfinite(request.value)) { return reject(EdfFault::InvalidRequest); }

	float target{};

	if (request.mode == ControlMode::Manual || request.mode == ControlMode::EdfBench) {
		const float maximum = request.mode == ControlMode::EdfBench ? _configuration.edf_commission_max : 1.F;

		if (request.units != EdfUnits::Normalized || request.value < 0.F || request.value > maximum) {
			return reject(EdfFault::InvalidRequest);
		}

		target = request.value;

	} else {
		if (request.units != EdfUnits::Newtons || !thrustToCommand(_configuration, request.value, target)) {
			return reject(EdfFault::InvalidRequest);
		}
	}

	if (!_received && target > (_grant.mode == ControlMode::EdfBench ? 0.F : _configuration.edf_entry_max)) {
		return reject(EdfFault::NotLowThrottle);
	}

	// Account for time under the previous target before installing this one.
	// A new request cannot spend slew budget accumulated before its arrival.
	if (_received) { sample(now, permission); }

	_last_sample = now;
	_request = request;
	_target = target;
	_received = true;
	return EdfFault::None;
}

EdfFault EdfOwner::transfer(uint64_t now, uint64_t transition_us, const EdfPermission &previous,
			    const EdfPermission &next, const EdfRequest &request, EdfCommand &current)
{
	if (check(now, previous) != EdfFault::None) { return _fault; }

	if (!transition_us || transition_us <= _last_transition_us || transition_us <= _grant.session
	    || transition_us > now || previous.timestamp_us > transition_us) { return reject(EdfFault::InvalidGrant); }

	// A rejected transfer cannot replay this supervisor event after repair.
	_last_transition_us = transition_us;
	const bool opposite_modes = (_grant.mode == ControlMode::Manual && next.mode == ControlMode::Acceleration)
				    || (_grant.mode == ControlMode::Acceleration && next.mode == ControlMode::Manual);

	if (!opposite_modes || next.session != _grant.session || next.source != _grant.source
	    || next.configuration != _grant.configuration || _grant.configuration != fingerprint(_configuration)
	    || next.transition_us != transition_us || next.timestamp_us < transition_us
	    || !permitted(now, next, next)) { return reject(EdfFault::InvalidGrant); }

	if (!_received || request.session != next.session || request.source != next.source
	    || request.configuration != next.configuration || request.mode != next.mode) { return reject(EdfFault::WrongOwner); }

	if (!request.sequence || request.sequence <= _request.sequence || request.timestamp_us <= transition_us
	    || request.timestamp_us <= _request.timestamp_us || request.timestamp_us > now || request.deadline_us <= now
	    || request.deadline_us - request.timestamp_us > _lease_us || request.deadline_us > _request.deadline_us
	    || request.deadline_us > previous.deadline_us || request.deadline_us > next.deadline_us
	    || !std::isfinite(request.value)) { return reject(EdfFault::InvalidRequest); }

	float target{};

	if (next.mode == ControlMode::Manual) {
		if (request.units != EdfUnits::Normalized || request.value < 0.F || request.value > 1.F) { return reject(EdfFault::InvalidRequest); }

		target = request.value;

	} else if (request.units != EdfUnits::Newtons || !thrustToCommand(_configuration, request.value, target)) {
		return reject(EdfFault::InvalidRequest);
	}

	// Spend elapsed slew time only under the outgoing target, exactly as an
	// ordinary accept would. The incoming target starts now, with no saved time
	// budget and no low-throttle reset. Nothing has altered the sampled output.
	const float dt = static_cast<float>(now - _last_sample) * kSecondsPerUs;
	const float sampled = _output + math::constrain(_target - _output, -_configuration.edf_fall * dt,
			      _configuration.edf_rise * dt);

	if (!std::isfinite(sampled) || sampled < 0.F || sampled > 1.F) { return reject(EdfFault::InvalidRequest); }

	EdfCommand candidate{};
	candidate.disposition = EdfDisposition::Command;
	candidate.normalized = sampled;
	candidate.session = request.session;
	candidate.source = request.source;
	candidate.configuration = request.configuration;
	candidate.sequence = request.sequence;
	candidate.request_timestamp_us = request.timestamp_us;
	candidate.deadline_us = request.deadline_us;
	_grant = next;
	_request = request;
	_target = target;
	_output = sampled;
	_last_sample = now;
	current = candidate;
	return EdfFault::None;
}

EdfCommand EdfOwner::sample(uint64_t now, const EdfPermission &permission)
{
	EdfCommand command{};

	if (check(now, permission) != EdfFault::None || !_received) {
		command.disposition = inhibited(permission) || _grant.mode == ControlMode::EdfBench ?
				      EdfDisposition::Inhibited : EdfDisposition::RecoveryRequired;
		return command;
	}

	// The consumer must check this expiry independently of this producer.
	const float dt = static_cast<float>(now - _last_sample) * kSecondsPerUs;
	_output += math::constrain(_target - _output, -_configuration.edf_fall * dt, _configuration.edf_rise * dt);

	if (_grant.mode == ControlMode::EdfBench) { _output = math::constrain(_output, 0.F, _configuration.edf_commission_max); }

	_last_sample = now;
	command.disposition = EdfDisposition::Command;
	command.normalized = _output;
	command.session = _request.session;
	command.source = _request.source;
	command.configuration = _request.configuration;
	command.sequence = _request.sequence;
	command.request_timestamp_us = _request.timestamp_us;
	command.deadline_us = _request.deadline_us < permission.deadline_us ? _request.deadline_us : permission.deadline_us;
	return command;
}

void EdfOwner::abandon(uint64_t now)
{
	if (_recovery_active || (_grant.mode != ControlMode::Manual && _grant.mode != ControlMode::Acceleration)
	    || !_grant.session) { return; }

	_recovery_active = true;
	_recovery_start_us = now < _clock ? _clock : now;
	_recovery_output = _output;
	_recovery_last_us = _last_sample;
	reject(EdfFault::Revoked);
}

EdfCommand EdfOwner::recover(uint64_t now, const EdfPermission &permission)
{
	EdfCommand command{};

	if (!_recovery_active) { return command; }

	if (now == 0 || now < _clock) { return command; }

	_clock = now;

	if (inhibited(permission) || permission.session != _grant.session || permission.source != _grant.source
	    || permission.configuration != _grant.configuration || permission.mode != _grant.mode
	    || now - _recovery_start_us >= kRecoveryCeilingUs) {
		// Disarm, kill, lockdown, termination, another session or the ceiling end recovery for good.
		_recovery_active = false;
		return command;
	}

	const float dt = static_cast<float>(now - _recovery_last_us) * kSecondsPerUs;
	_recovery_output = math::max(0.F, _recovery_output - _configuration.edf_fall * dt);
	_recovery_last_us = now;
	command.disposition = EdfDisposition::Recovery;
	command.normalized = _recovery_output;
	command.session = _grant.session;
	command.source = _grant.source;
	command.configuration = _grant.configuration;
	command.sequence = _request.sequence;
	command.request_timestamp_us = _request.timestamp_us;
	const uint64_t lease_end = now + _lease_us;
	const uint64_t ceiling = _recovery_start_us + kRecoveryCeilingUs;
	command.deadline_us = lease_end < ceiling ? lease_end : ceiling;
	return command;
}

void EdfOwner::revoke()
{
	_recovery_active = false;
	reject(EdfFault::Revoked);
}
} // namespace slaaps
