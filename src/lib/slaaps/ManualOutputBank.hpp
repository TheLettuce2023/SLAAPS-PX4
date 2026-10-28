#pragma once

#include "ManualBankPreparation.hpp"
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
	// Whole-bank command eligibility is separate from the EDF disposition.
	// ServoBench canards are handled while EDF remains explicitly inhibited.
	bool command_valid{};
	ControlMode mode{ControlMode::Manual};
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
		_handled_us = 0;
		_pending_transition = 0;
		_recovery = false;
		_recovery_frame = false;
		_have_handled = false;

		if (!now || now < _clock) { return reject(ManualBankFault::InvalidTime); }

		_clock = now;

		if (!permission.session || permission.session <= _session || permission.session > now
		    || permission.mode == ControlMode::Acceleration || permission.transition_us) { return reject(ManualBankFault::InvalidGrant); }

		_session = permission.session; // Failed entry also consumes the token.

		const uint64_t cycle_max = ManualBankPreparation::cycleMaximumUs(configuration, permission.mode);

		if (!ManualBankPreparation::validConfiguration(configuration, permission.mode)
		    || permission.configuration != fingerprint(configuration) || !permission.source
		    || !validManualOutputBinding(binding) || bank >= 2 || !(binding.banks & (1U << bank))
		    || !makeRoutingPlan(configuration, inventory, ManualBankPreparation::requiresEdf(permission.mode), _routing).valid()
		    || binding.banks != _routing.bank_mask
		    || !cycle_max || now > UINT64_MAX - cycle_max) {
			return reject(ManualBankFault::InvalidConfiguration);
		}

		_configuration = configuration;
		_binding = binding;
		_grant = permission;
		_bank = bank;
		_deadline = now + cycle_max;
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
		const bool transfer = _pending_transition != 0;
		const ControlMode mode = transfer ? permission.mode : _grant.mode;
		const bool bench = mode == ControlMode::ServoBench;
		const bool edf_bench = mode == ControlMode::EdfBench;
		const uint64_t lease = static_cast<uint64_t>(mode == ControlMode::Acceleration ? _configuration.output_lease_ms :
				       math::min(_configuration.output_lease_ms, _configuration.manual_age_ms)) * 1000;

		if (_awaiting_handling || _sequence == UINT64_MAX || frame.sequence != _sequence + 1 || frame.timestamp_us < _last_timestamp
		    || (_received && frame.timestamp_us == _last_timestamp) || frame.timestamp_us > now
		    || frame.deadline_us <= now || frame.deadline_us - frame.timestamp_us > ManualBankPreparation::cycleMaximumUs(_configuration, mode)
		    || frame.deadline_us > edf.deadline_us || !sameManualOutputBinding(frame.binding, _binding)
		    || frame.mode != mode || frame.transition_us != permission.transition_us
		    || (transfer ? (frame.transition_us != _pending_transition || frame.previous_handled_sequence != _sequence
				    || frame.previous_deadline_us != _deadline || frame.deadline_us > _deadline
				    || frame.timestamp_us <= _pending_transition || edf.request_timestamp_us <= _pending_transition) :
			(frame.previous_handled_sequence || frame.previous_deadline_us))
		    || (bench ? (edf.disposition != EdfDisposition::Inhibited || !(edf.normalized >= 0.F && edf.normalized <= 0.F)) :
			(edf.disposition != EdfDisposition::Command))
		    || edf.session != _grant.session || edf.source != _grant.source
		    || edf.configuration != _grant.configuration || !edf.sequence || edf.request_timestamp_us < _grant.session
		    || edf.request_timestamp_us > frame.timestamp_us || edf.deadline_us - edf.request_timestamp_us > lease
		    || !std::isfinite(edf.normalized) || edf.normalized < 0.F || edf.normalized > 1.F
		    || (!bench && !_received && edf.normalized > _configuration.edf_entry_max)
		    || (edf_bench && (edf.normalized > _configuration.edf_commission_max || (!_received && edf.normalized > 0.F)))
		    || (_received && (edf.sequence < _last_edf.sequence || edf.request_timestamp_us < _last_edf.request_timestamp_us
				      || (edf.sequence == _last_edf.sequence && edf.request_timestamp_us != _last_edf.request_timestamp_us)
				      || (edf.sequence > _last_edf.sequence && edf.request_timestamp_us <= _last_edf.request_timestamp_us)))) {
			return reject(ManualBankFault::InvalidFrame);
		}

		ManualBankOutput candidate{};
		candidate.disposition = edf.disposition;
		candidate.command_valid = true;
		candidate.mode = frame.mode;
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

			if (!std::isfinite(value) || value < servo.minimum || value > servo.maximum
			    || (edf_bench && !(value >= 0.F && value <= 0.F))) { return reject(ManualBankFault::InvalidFrame); }

			const float endpoint = value < 0.F ? servo.pwm_negative : servo.pwm_positive;
			const float pwm = servo.pwm_neutral + std::fabs(value) * (endpoint - servo.pwm_neutral);

			if (!map(_routing.canard[i], pwm, candidate)) { return reject(ManualBankFault::InvalidFrame); }
		}

		if (bench) {
			// An assigned EDF is part of this transaction only as an explicit
			// stopped channel. Zero normalized thrust is not an active command.
			if (_routing.edf_assigned && static_cast<unsigned>(_routing.edf.bank) == _bank) {
				candidate.channel_mask |= 1U << _routing.edf.channel;
				candidate.pwm_us[_routing.edf.channel] = _routing.edf.disarmed_us;
			}

		} else {
			const float edf_pwm = _routing.edf.minimum_us + edf.normalized * (_routing.edf.maximum_us - _routing.edf.minimum_us);

			if (!map(_routing.edf, edf_pwm, candidate)) { return reject(ManualBankFault::InvalidFrame); }
		}

		_candidate = candidate;

		if (transfer) { _grant = permission; _pending_transition = 0; }

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
		if (_recovery) { return confirmRecovery(now, permission, inventory, binding, handled, success, ack); }

		if (poll(now, permission, inventory, binding) != ManualBankFault::None) { return _fault; }

		if (!success || !_awaiting_handling || now < _accepted_us || !handled.command_valid
		    || handled.mode != _grant.mode || handled.disposition != _candidate.disposition
		    || handled.channel_mask != _candidate.channel_mask || handled.sequence != _candidate.sequence
		    || handled.session != _grant.session || handled.source != _grant.source || handled.configuration != _grant.configuration
		    || handled.command_timestamp_us != _last_timestamp || !sameManualOutputBinding(handled.binding, _binding)
		    || handled.deadline_us <= now || handled.deadline_us > _candidate.deadline_us
		    || std::memcmp(handled.pwm_us, _candidate.pwm_us, sizeof(handled.pwm_us)) != 0) {
			return reject(ManualBankFault::HandlingRejected);
		}

		_awaiting_handling = false;
		_handled_us = now;
		_handled = _candidate;
		_handled_edf = _last_edf.normalized;
		_have_handled = true;
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
		// No command has been received, so this is still actual bank entry time.
		candidate.activated_us = _last_timestamp;
		candidate.first_command_deadline_us = math::min(_deadline, math::min(_grant.deadline_us, permission.deadline_us));
		candidate.bank = _bank;
		ack = candidate;
		return true;
	}
	// A recovery frame is a write ticket too, although the original grant is no longer active.
	bool awaitingHandling() const { return (_active || _recovery) && _awaiting_handling; }
	bool canCompleteAtTransition(uint64_t transition_us) const
	{
		return _active && _awaiting_handling && transition_us && _accepted_us <= transition_us
		       && _last_timestamp <= transition_us;
	}

	ManualBankOutput sample(uint64_t now, const EdfPermission &permission, const OutputInventory &inventory,
				const ManualOutputBinding &binding)
	{
		if (_recovery && recoveryPoll(now, permission, inventory, binding) == ManualBankFault::None && _recovery_frame
		    && now < _candidate.deadline_us) {
			auto result = _candidate;
			result.deadline_us = math::min(result.deadline_us, _recovery_start + kRecoveryCeilingUs);
			return result;
		}

		if (!_recovery && poll(now, permission, inventory, binding) == ManualBankFault::None && _received && !_pending_transition) {
			auto result = _candidate;
			result.deadline_us = math::min(result.deadline_us, permission.deadline_us);
			return result;
		}

		ManualBankOutput result{};
		result.mode = _grant.mode;
		result.disposition = !permission.armed || permission.kill || permission.lockdown || permission.termination
				     || ManualBankPreparation::commissioningMode(_grant.mode) ? EdfDisposition::Inhibited : EdfDisposition::RecoveryRequired;
		return result;
	}

	void revoke() { _recovery = false; _recovery_frame = false; reject(ManualBankFault::Revoked); }
	ManualBankFault fault() const { return _fault; }
	// D3/D4. After a flight session loses permission or expires with an armed, non-kill permission and at least
	// one handled frame, the canards stay at the last handled positions and only recovery frames are admitted.
	// Recovery never revives the grant, accepts a normal frame or extends the original authority. It ends at
	// kRecoveryCeilingUs, on disarm/kill/lockdown/termination, on a routing change or on an invalid frame.
	bool recovering() const { return _recovery; }
	static constexpr uint64_t kRecoveryCeilingUs = EdfOwner::kRecoveryCeilingUs;

	ManualBankFault acceptRecovery(uint64_t now, const EdfPermission &permission, const OutputInventory &inventory,
				       const ManualOutputBinding &binding, const ManualWorkerOutput &frame)
	{
		if (!_recovery) { return _fault; }

		if (recoveryPoll(now, permission, inventory, binding) != ManualBankFault::None) { return _fault; }

		const auto &edf = frame.edf;
		const uint64_t lease = static_cast<uint64_t>(_configuration.output_lease_ms) * 1000;

		if (_awaiting_handling || frame.sequence <= _sequence || frame.timestamp_us <= _last_timestamp || frame.timestamp_us > now
		    || frame.deadline_us <= now || frame.deadline_us - frame.timestamp_us > lease
		    || frame.deadline_us > _recovery_start + kRecoveryCeilingUs || frame.deadline_us > edf.deadline_us
		    || !sameManualOutputBinding(frame.binding, _binding) || frame.mode != _grant.mode
		    || frame.transition_us != _grant.transition_us || frame.previous_handled_sequence || frame.previous_deadline_us
		    || edf.disposition != EdfDisposition::Recovery || edf.session != _grant.session || edf.source != _grant.source
		    || edf.configuration != _grant.configuration || !edf.sequence
		    || !std::isfinite(edf.normalized) || edf.normalized < 0.F || edf.normalized > _recovery_edf) {
			return endRecovery(ManualBankFault::InvalidFrame);
		}

		// Canards stay at this bank's own last handled pulses; frame canards are not an input here.
		for (float value : frame.canards) {
			if (!std::isfinite(value) || value < -1.F || value > 1.F) { return endRecovery(ManualBankFault::InvalidFrame); }
		}

		ManualBankOutput candidate = _handled;
		candidate.disposition = EdfDisposition::Recovery;
		candidate.command_valid = true;
		candidate.deadline_us = frame.deadline_us;
		candidate.sequence = frame.sequence;
		candidate.command_timestamp_us = frame.timestamp_us;

		if (_routing.edf_assigned && static_cast<unsigned>(_routing.edf.bank) == _bank) {
			if (edf.normalized > 0.F) {
				const float edf_pwm = _routing.edf.minimum_us + edf.normalized * (_routing.edf.maximum_us - _routing.edf.minimum_us);

				if (!map(_routing.edf, edf_pwm, candidate)) { return endRecovery(ManualBankFault::InvalidFrame); }

			} else {
				// EDF off is the channel's explicit disarmed value, as for a stopped bench channel.
				candidate.channel_mask |= 1U << _routing.edf.channel;
				candidate.pwm_us[_routing.edf.channel] = _routing.edf.disarmed_us;
			}
		}

		_candidate = candidate;
		_recovery_frame = true;
		_recovery_edf = edf.normalized;
		_sequence = frame.sequence;
		_last_timestamp = frame.timestamp_us;
		_accepted_us = now;
		_awaiting_handling = true;
		return ManualBankFault::None;
	}

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

		const bool transfer = permission.mode != _grant.mode;
		const bool transfer_valid = transfer && ManualBankPreparation::flightMode(_grant.mode)
					    && ManualBankPreparation::flightMode(permission.mode) && _received && !_awaiting_handling && _handled_us
					    && permission.transition_us > _grant.transition_us && permission.transition_us > _grant.session
					    && _grant.timestamp_us <= permission.transition_us && _last_timestamp <= permission.transition_us
					    && _accepted_us <= permission.transition_us
					    && (!_pending_transition || _pending_transition == permission.transition_us)
					    && ManualBankPreparation::validConfiguration(_configuration, permission.mode);

		if ((transfer ? !transfer_valid : (permission.transition_us != _grant.transition_us || _pending_transition))
		    || !ManualBankPreparation::permitted(permission, permission.mode)
		    || permission.session != _grant.session || permission.source != _grant.source || permission.configuration != _grant.configuration
		    || !permission.timestamp_us || permission.session > permission.timestamp_us
		    || permission.timestamp_us > now || now >= permission.deadline_us
		    || permission.deadline_us - permission.timestamp_us > lease) { return lose(now, permission, ManualBankFault::PermissionLost); }

		if (now >= _deadline) { return lose(now, permission, ManualBankFault::Expired); }

		if (transfer) { _pending_transition = permission.transition_us; }

		RoutingPlan live{};

		if (!sameManualOutputBinding(binding, _binding)
		    || !makeRoutingPlan(_configuration, inventory, ManualBankPreparation::requiresEdf(_grant.mode), live).valid()
		    || !sameRoutingPlan(live, _routing)) { return reject(ManualBankFault::RoutingChanged); }

		return ManualBankFault::None;
	}
	ManualBankFault reject(ManualBankFault fault)
	{
		_active = false;
		_fault = fault;
		return fault;
	}
	ManualBankFault endRecovery(ManualBankFault fault)
	{
		_recovery = false;
		_recovery_frame = false;
		_awaiting_handling = false;
		return reject(fault);
	}
	// Loss of permission or expiry of an active flight session; the only way into recovery.
	ManualBankFault lose(uint64_t now, const EdfPermission &permission, ManualBankFault fault)
	{
		if (_have_handled && ManualBankPreparation::flightMode(_grant.mode) && permission.armed && !permission.kill
		    && !permission.lockdown && !permission.termination && permission.session == _grant.session
		    && permission.source == _grant.source && permission.configuration == _grant.configuration
		    && permission.timestamp_us <= now) {
			_recovery = true;
			_recovery_frame = false;
			_recovery_start = now;
			_recovery_edf = _handled_edf;
			_awaiting_handling = false;
		}

		return reject(fault);
	}
	ManualBankFault recoveryPoll(uint64_t now, const EdfPermission &permission, const OutputInventory &inventory,
				     const ManualOutputBinding &binding)
	{
		if (!_recovery) { return _fault; }

		if (now < _clock) { return endRecovery(ManualBankFault::InvalidTime); }

		_clock = now;

		if (!permission.armed || permission.kill || permission.lockdown || permission.termination
		    || permission.session != _grant.session || permission.source != _grant.source
		    || permission.configuration != _grant.configuration || permission.timestamp_us > now
		    || now - _recovery_start >= kRecoveryCeilingUs) { return endRecovery(ManualBankFault::PermissionLost); }

		RoutingPlan live{};

		if (!sameManualOutputBinding(binding, _binding)
		    || !makeRoutingPlan(_configuration, inventory, ManualBankPreparation::requiresEdf(_grant.mode), live).valid()
		    || !sameRoutingPlan(live, _routing)) { return endRecovery(ManualBankFault::RoutingChanged); }

		return ManualBankFault::None;
	}
	ManualBankFault confirmRecovery(uint64_t now, const EdfPermission &permission, const OutputInventory &inventory,
					const ManualOutputBinding &binding, const ManualBankOutput &handled, bool success,
					ManualOutputAck &ack)
	{
		if (recoveryPoll(now, permission, inventory, binding) != ManualBankFault::None) { return _fault; }

		// A normal ticket written just before recovery started is history: never acknowledged, never fatal here.
		if (handled.disposition != EdfDisposition::Recovery) { return ManualBankFault::HandlingRejected; }

		if (!success || !_awaiting_handling || !_recovery_frame || now < _accepted_us || !handled.command_valid
		    || handled.mode != _grant.mode || handled.disposition != EdfDisposition::Recovery
		    || handled.channel_mask != _candidate.channel_mask || handled.sequence != _candidate.sequence
		    || handled.session != _grant.session || handled.source != _grant.source || handled.configuration != _grant.configuration
		    || handled.command_timestamp_us != _last_timestamp || !sameManualOutputBinding(handled.binding, _binding)
		    || handled.deadline_us <= now || handled.deadline_us > _candidate.deadline_us
		    || std::memcmp(handled.pwm_us, _candidate.pwm_us, sizeof(handled.pwm_us)) != 0) {
			return endRecovery(ManualBankFault::HandlingRejected);
		}

		_awaiting_handling = false;
		ack = {_grant.session, _grant.source, _grant.configuration, _binding.routing, _binding.driver[_bank],
		       _sequence, _last_timestamp, now, static_cast<uint8_t>(_bank)
		      };
		return ManualBankFault::None;
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
	bool _recovery{}, _recovery_frame{}, _have_handled{};
	uint64_t _recovery_start{};
	float _recovery_edf{}, _handled_edf{};
	ManualBankOutput _handled{};
	uint64_t _accepted_us{};
	uint64_t _handled_us{}, _pending_transition{};
	ManualBankFault _fault{ManualBankFault::Revoked};
};

} // namespace slaaps
