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

bool EdfOwner::permitted(uint64_t now, const EdfPermission &p) const
{
	return !inhibited(p) && p.flight_admitted && p.healthy && p.measured_condition_valid
	       && p.timestamp_us != 0 && p.timestamp_us <= now && now < p.deadline_us
	       && p.deadline_us - p.timestamp_us <= _lease_us
	       && p.session == _grant.session && p.source == _grant.source
	       && p.configuration == _grant.configuration && p.mode == _grant.mode;
}

EdfFault EdfOwner::begin(uint64_t now, const VehicleConfiguration &configuration, const EdfPermission &permission)
{
	// Failed replacement also revokes the old grant. No overlapping owners.
	_active = false;
	_received = false;
	_output = 0.F;
	_target = 0.F;

	if (now == 0 || now < _clock) { return reject(EdfFault::InvalidTime); }

	_clock = now;

	if (permission.session == 0 || permission.session <= _last_session || permission.session > now
	    || permission.source == 0 || permission.configuration == 0
	    || (permission.mode != ControlMode::Manual && permission.mode != ControlMode::Acceleration)) {
		return reject(EdfFault::InvalidGrant);
	}

	// Consume even a rejected token; retry needs a deliberate new session.
	_last_session = permission.session;

	if (!validate(configuration, ConfigurationUse::StabilizedManual).valid()) {
		return reject(EdfFault::InvalidConfiguration);
	}

	_configuration = configuration;
	_lease_us = static_cast<uint64_t>(configuration.output_lease_ms) * kUsPerMs;
	_grant = permission;

	if (!permitted(now, permission)) { return reject(EdfFault::PermissionLost); }

	_grant.timestamp_us = now;
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

	if (!permitted(now, permission)) { return reject(EdfFault::PermissionLost); }

	if ((_received && now >= _request.deadline_us)
	    || (!_received && now - _grant.timestamp_us >= _lease_us)) { return reject(EdfFault::Expired); }

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
	    || request.timestamp_us < _grant.timestamp_us || request.timestamp_us > now
	    || (_received && request.timestamp_us <= _request.timestamp_us)
	    || request.deadline_us <= now || request.deadline_us - request.timestamp_us > _lease_us
	    || !std::isfinite(request.value)) { return reject(EdfFault::InvalidRequest); }

	float target{};

	if (request.mode == ControlMode::Manual) {
		if (request.units != EdfUnits::Normalized || request.value < 0.F || request.value > 1.F) {
			return reject(EdfFault::InvalidRequest);
		}

		target = request.value;

	} else {
		if (request.units != EdfUnits::Newtons || !thrustToCommand(_configuration, request.value, target)) {
			return reject(EdfFault::InvalidRequest);
		}
	}

	if (!_received && target > _configuration.edf_entry_max) { return reject(EdfFault::NotLowThrottle); }

	// Account for time under the previous target before installing this one.
	// A new request cannot spend slew budget accumulated before its arrival.
	if (_received) { sample(now, permission); }

	_last_sample = now;
	_request = request;
	_target = target;
	_received = true;
	return EdfFault::None;
}

EdfCommand EdfOwner::sample(uint64_t now, const EdfPermission &permission)
{
	EdfCommand command{};

	if (check(now, permission) != EdfFault::None || !_received) {
		command.disposition = inhibited(permission) ? EdfDisposition::Inhibited : EdfDisposition::RecoveryRequired;
		return command;
	}

	// The consumer must check this expiry independently of this producer.
	const float dt = static_cast<float>(now - _last_sample) * kSecondsPerUs;
	_output += math::constrain(_target - _output, -_configuration.edf_fall * dt, _configuration.edf_rise * dt);
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

void EdfOwner::revoke()
{
	reject(EdfFault::Revoked);
}
} // namespace slaaps
