#pragma once

#include "ManualEdfInput.hpp"
#include "ManualOutput.hpp"
#include <lib/mathlib/math/Limits.hpp>
#include <cstdint>

namespace slaaps
{

enum class ManualWorkerStatus : uint8_t {
	Ready, Command, NotDue, Waiting, Inactive, ConfigurationRejected, ControlRejected, ThrottleRejected, PropulsionRejected,
	OutputRejected
};

// Serialized manual worker core. The supervisor owns the shared EDF owner across
// worker/mode replacement and must stop this worker before transferring ownership.
// No publication, driver admission or airborne fallback is supplied. All bound
// banks must acknowledge before another calculation; driver expiry is independent.
// Only Command replaces output. Every fault revokes both controllers; preserved
// caller output must never be republished after a fault or NotDue return.
class ManualWorker
{
public:
	explicit ManualWorker(EdfOwner &edf) : _edf(edf) {}
	ManualWorkerStatus begin(uint64_t now, const VehicleConfiguration &configuration, const EdfPermission &permission,
				 const research_stabilized_manual::ManualSample &manual,
				 const research_stabilized_manual::StateSample &state, const ManualOutputBinding &binding)
	{
		stop();

		// Consume session through the common owner even if subsequent entry fails.
		if (_edf.begin(now, configuration, permission) != EdfFault::None) { return ManualWorkerStatus::PropulsionRejected; }

		research_stabilized_manual::Configuration control{};

		if (permission.configuration != fingerprint(configuration) || permission.mode != ControlMode::Manual
		    || !validManualOutputBinding(binding) || !makeManualConfiguration(configuration, control)) { return reject(ManualWorkerStatus::ConfigurationRejected); }

		// Fixed simulated thrust is legacy report metadata, never propulsion input.
		control.fixed_simulated_thrust_n = 0.F;

		if (_control.configure(control) != research_stabilized_manual::Status::Ok
		    || _control.enable(now, manual, state) != research_stabilized_manual::Status::Ok) {
			return reject(ManualWorkerStatus::ControlRejected);
		}

		auto grant = permission;
		grant.timestamp_us = now;

		if (!_throttle.configure(configuration, grant)) { return reject(ManualWorkerStatus::ConfigurationRejected); }

		EdfRequest request{};

		if (_throttle.evaluate(now, manual, request) != ManualEdfStatus::Request) { return reject(ManualWorkerStatus::ThrottleRejected); }

		if (_edf.accept(now, permission, request) != EdfFault::None) { return reject(ManualWorkerStatus::PropulsionRejected); }

		_state_age_us = control.maximum_state_age_us;
		_cycle_age_us = control.maximum_cycle_us;
		_sequence = 0;
		_binding = binding;
		_last_calculation = now;
		_deadline = expiry(now, _cycle_age_us);
		_new_throttle = false;
		_active = true;
		return ManualWorkerStatus::Ready;
	}

	ManualWorkerStatus cycle(uint64_t now, const EdfPermission &permission,
				 const research_stabilized_manual::ManualSample &manual,
				 const research_stabilized_manual::StateSample &state, ManualWorkerOutput &output,
				 const ManualOutputBinding &binding, const ManualOutputAck *ack = nullptr)
	{
		if (!_active) { return ManualWorkerStatus::Inactive; }

		if (!sameManualOutputBinding(binding, _binding) || now >= _deadline) { return reject(ManualWorkerStatus::OutputRejected); }

		if (_control.monitor(now, manual, state) != research_stabilized_manual::Status::Ok) {
			return reject(ManualWorkerStatus::ControlRejected);
		}

		EdfRequest request{};
		const auto throttle = _throttle.evaluate(now, manual, request);

		if (throttle != ManualEdfStatus::Request && throttle != ManualEdfStatus::Held) {
			return reject(ManualWorkerStatus::ThrottleRejected);
		}

		if (throttle == ManualEdfStatus::Request) {
			_latest_throttle = request;
			_new_throttle = true;
		}

		if (_edf.sample(now, permission).disposition != EdfDisposition::Command) {
			return reject(ManualWorkerStatus::PropulsionRejected);
		}

		if (ack) {
			if (!_pending_banks || ack->bank >= 2 || !(_pending_banks & (1U << ack->bank))
			    || ack->driver != _binding.driver[ack->bank] || ack->routing != _binding.routing
			    || ack->session != permission.session || ack->source != permission.source
			    || ack->configuration != permission.configuration || ack->sequence != _sequence
			    || ack->command_timestamp_us != _last_calculation
			    || ack->accepted_us < _last_calculation || ack->accepted_us > now) {
				return reject(ManualWorkerStatus::OutputRejected);
			}

			_pending_banks &= ~(1U << ack->bank);
		}

		if (_pending_banks) { return ManualWorkerStatus::Waiting; }

		if (now == _last_calculation) { return ManualWorkerStatus::NotDue; }

		research_stabilized_manual::Report report{};
		const auto control = _control.cycle(now, manual, state, report);

		if (control == research_stabilized_manual::Status::NotDue) { return ManualWorkerStatus::NotDue; }

		if (control != research_stabilized_manual::Status::Ok) { return reject(ManualWorkerStatus::ControlRejected); }

		if (_new_throttle) {
			if (_edf.accept(now, permission, _latest_throttle) != EdfFault::None) { return reject(ManualWorkerStatus::PropulsionRejected); }

			_new_throttle = false;
		}

		const auto edf = _edf.sample(now, permission);

		if (edf.disposition != EdfDisposition::Command) { return reject(ManualWorkerStatus::PropulsionRejected); }

		const uint64_t deadline = math::min(math::min(edf.deadline_us, expiry(state.attitude_sample_us, _state_age_us)),
						    math::min(expiry(state.rate_sample_us, _state_age_us), expiry(now, _cycle_age_us)));

		if (deadline <= now || _sequence == UINT64_MAX) { return reject(ManualWorkerStatus::ControlRejected); }

		ManualWorkerOutput candidate{};
		candidate.timestamp_us = now;
		candidate.deadline_us = deadline;
		candidate.sequence = ++_sequence;
		candidate.canards = report.allocation.command;
		candidate.edf = edf;
		candidate.binding = _binding;
		_deadline = deadline;
		_last_calculation = now;
		_pending_banks = _binding.banks;
		output = candidate;
		return ManualWorkerStatus::Command;
	}

	void stop()
	{
		_active = false;
		_pending_banks = 0;
		_control.disable();
		_edf.revoke();
	}

private:
	static uint64_t expiry(uint64_t time, uint64_t age)
	{
		return time > UINT64_MAX - age ? UINT64_MAX : time + age;
	}
	ManualWorkerStatus reject(ManualWorkerStatus reason)
	{
		stop();
		return reason;
	}
	EdfOwner &_edf;
	research_stabilized_manual::StabilizedManualControl _control{};
	ManualEdfInput _throttle{};
	uint64_t _state_age_us{};
	uint64_t _cycle_age_us{};
	uint64_t _sequence{};
	bool _active{};
	ManualOutputBinding _binding{};
	EdfRequest _latest_throttle{};
	uint64_t _last_calculation{};
	uint64_t _deadline{};
	uint8_t _pending_banks{};
	bool _new_throttle{};
};

} // namespace slaaps
