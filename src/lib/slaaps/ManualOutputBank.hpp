#pragma once

#include "ManualOutput.hpp"
#include "ActuatorRouting.hpp"
#include <lib/mathlib/math/Limits.hpp>
#include <cmath>
#include <cstring>
#include <cstdint>

namespace slaaps
{

enum class ManualBankFault : uint8_t {
	None, InvalidGrant, InvalidConfiguration, InvalidTime, PermissionLost, RoutingChanged, InvalidFrame, Expired, Revoked, HandlingRejected
};

struct ManualBankOutput {
	EdfDisposition disposition{EdfDisposition::Inhibited};
	uint16_t channel_mask{};
	float pwm_us[kMaximumChannelsPerBank] {};
	uint64_t deadline_us{};
	uint64_t sequence{};
	uint64_t session{};
	uint64_t source{};
	uint64_t configuration{};
	uint64_t command_timestamp_us{};
	ManualOutputBinding binding{};
};

// Independent consumer clock. Preserve this endpoint across session changes.
// All calls must be serialized with the driver's routing/configuration updates.
// Live inventory must be complete; message fields alone cannot admit a session.
// accept() prepares only. confirmHandled() releases an acknowledgment only after
// the trusted driver adapter reports handling the exact sampled candidate. The
// adapter must serialize its write and confirmation with permission/routing edits.
// sample() MUST run on every dispatch, including without new controller traffic.
// RecoveryRequired is not a PWM command or a chosen airborne fallback.
class ManualOutputBank
{
public:
	ManualBankFault begin(uint64_t now, unsigned bank, const VehicleConfiguration &configuration,
			      const OutputInventory &inventory, const ManualOutputBinding &binding, const EdfPermission &permission)
	{
		_active = false;
		_received = false;
		_awaiting_handling = false;

		if (!now || now < _clock) { return reject(ManualBankFault::InvalidTime); }

		_clock = now;

		if (!permission.session || permission.session <= _session || permission.session > now) { return reject(ManualBankFault::InvalidGrant); }

		_session = permission.session; // Failed entry also consumes the token.

		if (!validate(configuration, ConfigurationUse::StabilizedManual).valid()
		    || permission.configuration != fingerprint(configuration) || !permission.source
		    || !validManualOutputBinding(binding) || bank >= 2 || !(binding.banks & (1U << bank))
		    || !makeRoutingPlan(configuration, inventory, true, _routing).valid() || binding.banks != _routing.bank_mask
		    || now > UINT64_MAX - static_cast<uint64_t>(configuration.cycle_max_us)) {
			return reject(ManualBankFault::InvalidConfiguration);
		}

		_configuration = configuration;
		_binding = binding;
		_grant = permission;
		_grant.timestamp_us = now;
		_bank = bank;
		_deadline = now + configuration.cycle_max_us;
		_sequence = 0;
		_last_timestamp = now;
		_active = true;
		_fault = ManualBankFault::None;
		return poll(now, permission, inventory, binding);
	}

	ManualBankFault accept(uint64_t now, const EdfPermission &permission, const OutputInventory &inventory,
			       const ManualOutputBinding &binding, const ManualWorkerOutput &frame)
	{
		if (poll(now, permission, inventory, binding) != ManualBankFault::None) { return _fault; }

		const auto &edf = frame.edf;
		const uint64_t lease = static_cast<uint64_t>(math::min(_configuration.output_lease_ms, _configuration.manual_age_ms)) * 1000;

		if (_awaiting_handling || _sequence == UINT64_MAX || frame.sequence != _sequence + 1 || frame.timestamp_us < _last_timestamp
		    || (_received && frame.timestamp_us == _last_timestamp) || frame.timestamp_us > now
		    || frame.deadline_us <= now || frame.deadline_us - frame.timestamp_us > static_cast<uint64_t>(_configuration.cycle_max_us)
		    || frame.deadline_us > edf.deadline_us || !sameManualOutputBinding(frame.binding, _binding)
		    || edf.disposition != EdfDisposition::Command || edf.session != _grant.session || edf.source != _grant.source
		    || edf.configuration != _grant.configuration || !edf.sequence || edf.request_timestamp_us < _grant.timestamp_us
		    || edf.request_timestamp_us > frame.timestamp_us || edf.deadline_us - edf.request_timestamp_us > lease
		    || !std::isfinite(edf.normalized) || edf.normalized < 0.F || edf.normalized > 1.F
		    || (!_received && edf.normalized > _configuration.edf_entry_max)
		    || (_received && (edf.sequence < _last_edf.sequence || edf.request_timestamp_us < _last_edf.request_timestamp_us
				      || (edf.sequence == _last_edf.sequence && edf.request_timestamp_us != _last_edf.request_timestamp_us)
				      || (edf.sequence > _last_edf.sequence && edf.request_timestamp_us <= _last_edf.request_timestamp_us)))) {
			return reject(ManualBankFault::InvalidFrame);
		}

		ManualBankOutput candidate{};
		candidate.disposition = EdfDisposition::Command;
		candidate.deadline_us = frame.deadline_us;
		candidate.sequence = frame.sequence;
		candidate.session = edf.session;
		candidate.source = edf.source;
		candidate.configuration = edf.configuration;
		candidate.command_timestamp_us = frame.timestamp_us;
		candidate.binding = frame.binding;

		for (unsigned i = 0; i < kCanards; ++i) {
			const float value = frame.canards[i];
			const auto &servo = _configuration.canard[i];

			if (!std::isfinite(value) || value < servo.minimum || value > servo.maximum) { return reject(ManualBankFault::InvalidFrame); }

			const float endpoint = value < 0.F ? servo.pwm_negative : servo.pwm_positive;
			const float pwm = servo.pwm_neutral + std::fabs(value) * (endpoint - servo.pwm_neutral);

			if (!map(_routing.canard[i], pwm, candidate)) { return reject(ManualBankFault::InvalidFrame); }
		}

		const float edf_pwm = _routing.edf.minimum_us + edf.normalized * (_routing.edf.maximum_us - _routing.edf.minimum_us);

		if (!map(_routing.edf, edf_pwm, candidate)) { return reject(ManualBankFault::InvalidFrame); }

		_candidate = candidate;
		_received = true;
		_sequence = frame.sequence;
		_last_timestamp = frame.timestamp_us;
		_deadline = frame.deadline_us;
		_last_edf = edf;
		_accepted_us = now;
		_awaiting_handling = true;
		return ManualBankFault::None;
	}

	ManualBankFault confirmHandled(uint64_t now, const EdfPermission &permission, const OutputInventory &inventory,
				       const ManualOutputBinding &binding, const ManualBankOutput &handled, bool success,
				       ManualOutputAck &ack)
	{
		if (poll(now, permission, inventory, binding) != ManualBankFault::None) { return _fault; }

		if (!success || !_awaiting_handling || now < _accepted_us || handled.disposition != EdfDisposition::Command
		    || handled.channel_mask != _candidate.channel_mask || handled.sequence != _candidate.sequence
		    || handled.session != _grant.session || handled.source != _grant.source || handled.configuration != _grant.configuration
		    || handled.command_timestamp_us != _last_timestamp || !sameManualOutputBinding(handled.binding, _binding)
		    || handled.deadline_us <= now || handled.deadline_us > _candidate.deadline_us
		    || std::memcmp(handled.pwm_us, _candidate.pwm_us, sizeof(handled.pwm_us)) != 0) {
			return reject(ManualBankFault::HandlingRejected);
		}

		_awaiting_handling = false;
		ack = {_grant.session, _grant.source, _grant.configuration, _binding.routing, _binding.driver[_bank],
		       _sequence, _last_timestamp, now, static_cast<uint8_t>(_bank)
		      };
		return ManualBankFault::None;
	}

	bool active() const { return _active; }
	bool activationAck(uint64_t now, const EdfPermission &permission, const OutputInventory &inventory,
			   const ManualOutputBinding &binding, ManualActivationAck &ack)
	{
		if (poll(now, permission, inventory, binding) != ManualBankFault::None || _received) { return false; }

		ManualActivationAck candidate{};
		candidate.session = _grant.session;
		candidate.source = _grant.source;
		candidate.configuration = _grant.configuration;
		candidate.routing = _binding.routing;
		candidate.driver = _binding.driver[_bank];
		candidate.activated_us = _grant.timestamp_us;
		candidate.first_command_deadline_us = math::min(_deadline, math::min(_grant.deadline_us, permission.deadline_us));
		candidate.bank = _bank;
		ack = candidate;
		return true;
	}
	bool awaitingHandling() const { return _active && _awaiting_handling; }

	ManualBankOutput sample(uint64_t now, const EdfPermission &permission, const OutputInventory &inventory,
				const ManualOutputBinding &binding)
	{
		if (poll(now, permission, inventory, binding) == ManualBankFault::None && _received) {
			auto result = _candidate;
			result.deadline_us = math::min(result.deadline_us, permission.deadline_us);
			return result;
		}

		ManualBankOutput result{};
		result.disposition = !permission.armed || permission.kill || permission.lockdown || permission.termination
				     || permission.mode == ControlMode::ServoBench ? EdfDisposition::Inhibited : EdfDisposition::RecoveryRequired;
		return result;
	}

	void revoke() { reject(ManualBankFault::Revoked); }
	ManualBankFault fault() const { return _fault; }

private:
	bool map(const OutputChannel &channel, float pwm, ManualBankOutput &candidate) const
	{
		if (!std::isfinite(pwm) || pwm < channel.minimum_us || pwm > channel.maximum_us) { return false; }

		if (static_cast<unsigned>(channel.bank) == _bank) {
			candidate.channel_mask |= 1U << channel.channel;
			candidate.pwm_us[channel.channel] = pwm;
		}

		return true;
	}
	ManualBankFault poll(uint64_t now, const EdfPermission &permission, const OutputInventory &inventory,
			     const ManualOutputBinding &binding)
	{
		if (!_active) { return _fault; }

		if (now < _clock) { return reject(ManualBankFault::InvalidTime); }

		_clock = now;
		const uint64_t lease = static_cast<uint64_t>(_configuration.output_lease_ms) * 1000;

		if (!permission.armed || !permission.flight_admitted || !permission.healthy || !permission.measured_condition_valid
		    || permission.kill || permission.lockdown || permission.termination || permission.mode != ControlMode::Manual
		    || permission.session != _grant.session || permission.source != _grant.source || permission.configuration != _grant.configuration
		    || !permission.timestamp_us || permission.timestamp_us > now || now >= permission.deadline_us
		    || permission.deadline_us - permission.timestamp_us > lease) { return reject(ManualBankFault::PermissionLost); }

		if (now >= _deadline) { return reject(ManualBankFault::Expired); }

		RoutingPlan live{};

		if (!sameManualOutputBinding(binding, _binding) || !makeRoutingPlan(_configuration, inventory, true, live).valid()
		    || !sameRoutingPlan(live, _routing)) { return reject(ManualBankFault::RoutingChanged); }

		return ManualBankFault::None;
	}
	ManualBankFault reject(ManualBankFault fault)
	{
		_active = false;
		_fault = fault;
		return fault;
	}
	VehicleConfiguration _configuration{};
	RoutingPlan _routing{};
	ManualOutputBinding _binding{};
	EdfPermission _grant{};
	EdfCommand _last_edf{};
	ManualBankOutput _candidate{};
	uint64_t _session{};
	uint64_t _clock{};
	uint64_t _deadline{};
	uint64_t _last_timestamp{};
	uint64_t _sequence{};
	unsigned _bank{};
	bool _active{};
	bool _received{};
	bool _awaiting_handling{};
	uint64_t _accepted_us{};
	ManualBankFault _fault{ManualBankFault::Revoked};
};

} // namespace slaaps
