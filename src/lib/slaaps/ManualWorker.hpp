#pragma once

#include "ManualEdfInput.hpp"
#include "ManualOutput.hpp"
#include "ManualBankPreparation.hpp"
#include "ControlSchedule.hpp"
#include <lib/mathlib/math/Limits.hpp>
#include <cmath>
#include <cstdint>

namespace slaaps
{

enum class ManualWorkerStatus : uint8_t {
	Ready, Command, NotDue, Waiting, Inactive, ConfigurationRejected, ControlRejected, ThrottleRejected, PropulsionRejected,
	OutputRejected, ConditionRejected
};

// Serialized manual/servo-bench worker core. Retain its session history across
// mode changes. The supervisor owns the shared EDF owner across replacement
// and must stop this worker before transferring ownership.
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
				 const research_stabilized_manual::StateSample &state, const ManualOutputBinding &binding,
				 const OperatingConditionSample *condition = nullptr)
	{
		stop();
		const bool fresh_session = consumeSession(now, permission);

		// Consume session through the common owner even if subsequent entry fails.
		if (_edf.begin(now, configuration, permission) != EdfFault::None) { return ManualWorkerStatus::PropulsionRejected; }

		if (!fresh_session) { return reject(ManualWorkerStatus::PropulsionRejected); }

		research_stabilized_manual::Configuration control{};

		if (permission.configuration != fingerprint(configuration) || permission.mode != ControlMode::Manual
		    || !validManualOutputBinding(binding) || !makeManualConfiguration(configuration, control)) { return reject(ManualWorkerStatus::ConfigurationRejected); }

		// Fixed simulated thrust is legacy report metadata, never propulsion input.
		control.fixed_simulated_thrust_n = 0.F;
		_schedule_enabled = configuration.schedule.enabled == 1;
		_schedule_status = ControlScheduleStatus::Inactive;

		if (_schedule_enabled) {
			ControlSchedule::Step initial{};

			// EDF ownership starts at zero. Admission of a ground spool-up phase
			// outside the calibrated table is not inferred by this controller.
			if (!condition || !_schedule.begin(now, configuration, *condition, 0.F, initial)) {
				_schedule_status = condition ? _schedule.status() : ControlScheduleStatus::Stale;
				return reject(ManualWorkerStatus::ConditionRejected);
			}

			control.rate_p = initial.control.rate_p;
			control.rate_i = initial.control.rate_i;
			control.rate_d = initial.control.rate_d;
			control.allocation.effectiveness = initial.control.effectiveness;
			_schedule_status = ControlScheduleStatus::Ready;
		}

		if (_control.configure(control) != research_stabilized_manual::Status::Ok
		    || _control.enable(now, manual, state) != research_stabilized_manual::Status::Ok) {
			return reject(ManualWorkerStatus::ControlRejected);
		}

		if (!_throttle.configure(configuration, permission)) { return reject(ManualWorkerStatus::ConfigurationRejected); }

		EdfRequest request{};

		if (_throttle.evaluate(now, manual, request) != ManualEdfStatus::Request) { return reject(ManualWorkerStatus::ThrottleRejected); }

		if (_edf.accept(now, permission, request) != EdfFault::None) { return reject(ManualWorkerStatus::PropulsionRejected); }

		_state_age_us = control.maximum_state_age_us;
		_cycle_age_us = control.maximum_cycle_us;
		_sequence = 0;
		_handled_sequence = 0;
		_binding = binding;
		_mode = ControlMode::Manual;
		_last_calculation = now;
		_deadline = expiry(now, _cycle_age_us);
		_new_throttle = false;
		_active = true;
		return ManualWorkerStatus::Ready;
	}

	ManualWorkerStatus beginBench(uint64_t now, const VehicleConfiguration &configuration, const EdfPermission &permission,
				      const research_stabilized_manual::ManualSample &manual, const ManualOutputBinding &binding,
				      unsigned target = 0)
	{
		stop(); // Servo commissioning never acquires the propulsion owner.

		if (!consumeSession(now, permission)) { return reject(ManualWorkerStatus::PropulsionRejected); }

		research_actuator_command::BenchOutputProfile profile{};

		if (permission.configuration != fingerprint(configuration) || !validManualOutputBinding(binding)
		    || !ManualBankPreparation::activationTimingConfigured(configuration, ControlMode::ServoBench)
		    || !makeBenchProfile(configuration, profile)) { return reject(ManualWorkerStatus::ConfigurationRejected); }

		_bench_input_lease_us = math::min(profile.control.maximum_manual_age_us, profile.lease_us);
		_permission_lease_us = profile.lease_us;
		_cycle_age_us = profile.control.maximum_cycle_gap_us;
		_latest_request = {};
		_latest_request.session = permission.session;
		_latest_request.source = permission.source;
		_latest_request.configuration = permission.configuration;
		_latest_request.mode = ControlMode::ServoBench;

		if (!benchPermitted(now, permission)) { return reject(ManualWorkerStatus::PropulsionRejected); }

		if (_bench.configure(profile.control, target, profile.deadzone) != research_actuator_command::BenchStatus::Ok
		    || _bench.enable(now, manual) != research_actuator_command::BenchStatus::Ok
		    || !updateBenchInput(now, manual)) { return reject(ManualWorkerStatus::ControlRejected); }

		_sequence = 0;
		_handled_sequence = 0;
		_binding = binding;
		_mode = ControlMode::ServoBench;
		_last_calculation = now;
		_deadline = math::min(math::min(_latest_request.deadline_us, permission.deadline_us), expiry(now, _cycle_age_us));
		_active = true;
		return ManualWorkerStatus::Ready;
	}

	ManualWorkerStatus beginEdfBench(uint64_t now, const VehicleConfiguration &configuration, const EdfPermission &permission,
					 const research_stabilized_manual::ManualSample &manual, const ManualOutputBinding &binding)
	{
		stop();
		const bool fresh_session = consumeSession(now, permission);

		if (_edf.begin(now, configuration, permission) != EdfFault::None) { return ManualWorkerStatus::PropulsionRejected; }

		if (!fresh_session) { return reject(ManualWorkerStatus::PropulsionRejected); }

		if (permission.mode != ControlMode::EdfBench || permission.configuration != fingerprint(configuration)
		    || !validManualOutputBinding(binding) || !_throttle.configure(configuration, permission)) {
			return reject(ManualWorkerStatus::ConfigurationRejected);
		}

		for (unsigned axis = 0; axis < kAxes; ++axis) {
			if (!std::isfinite(manual.axes[axis]) || std::fabs(manual.axes[axis]) > configuration.centered_enter) {
				return reject(ManualWorkerStatus::ControlRejected);
			}
		}

		EdfRequest request{};

		// The raw adapter independently checks unscaled low entry and emits an
		// initial zero request, irrespective of the accepted commissioning cap.
		if (_throttle.evaluate(now, manual, request) != ManualEdfStatus::Request) { return reject(ManualWorkerStatus::ThrottleRejected); }

		if (_edf.accept(now, permission, request) != EdfFault::None) { return reject(ManualWorkerStatus::PropulsionRejected); }

		_cycle_age_us = static_cast<uint64_t>(configuration.bench_gap_ms) * 1000;
		_sequence = 0;
		_handled_sequence = 0;
		_binding = binding;
		_mode = ControlMode::EdfBench;
		_last_calculation = now;
		_deadline = math::min(math::min(request.deadline_us, permission.deadline_us), expiry(now, _cycle_age_us));
		_latest_request = request;
		_new_throttle = false;
		_active = true;
		return ManualWorkerStatus::Ready;
	}

	ManualWorkerStatus cycle(uint64_t now, const EdfPermission &permission,
				 const research_stabilized_manual::ManualSample &manual,
				 const research_stabilized_manual::StateSample &state, ManualWorkerOutput &output,
				 const ManualOutputBinding &binding, const ManualOutputAck *ack = nullptr,
				 const OperatingConditionSample *condition = nullptr)
	{
		if (!_active) { return ManualWorkerStatus::Inactive; }

		if (_mode != ControlMode::Manual || !sameManualOutputBinding(binding, _binding)
		    || now >= _deadline) { return reject(ManualWorkerStatus::OutputRejected); }

		_clock = math::max(_clock, now);

		if (_control.monitor(now, manual, state) != research_stabilized_manual::Status::Ok) {
			return reject(ManualWorkerStatus::ControlRejected);
		}

		EdfRequest request{};
		const auto throttle = _throttle.evaluate(now, manual, request);

		if (throttle != ManualEdfStatus::Request && throttle != ManualEdfStatus::Held) {
			return reject(ManualWorkerStatus::ThrottleRejected);
		}

		if (throttle == ManualEdfStatus::Request) {
			_latest_request = request;
			_new_throttle = true;
		}

		const auto observed_edf = _edf.sample(now, permission);

		if (observed_edf.disposition != EdfDisposition::Command) {
			return reject(ManualWorkerStatus::PropulsionRejected);
		}

		if (_schedule_enabled) {
			if (!condition || !_schedule.observe(now, *condition, observed_edf.normalized)) {
				_schedule_status = condition ? _schedule.status() : ControlScheduleStatus::Stale;
				return reject(ManualWorkerStatus::ConditionRejected);
			}

			_schedule_status = ControlScheduleStatus::Ready;
		}

		if (!acceptAcknowledgment(now, permission, ack)) { return reject(ManualWorkerStatus::OutputRejected); }

		if (_pending_banks) { return ManualWorkerStatus::Waiting; }

		if (now == _last_calculation) { return ManualWorkerStatus::NotDue; }

		research_stabilized_manual::Report report{};
		ControlSchedule::Step scheduled{};

		if (_schedule_enabled && !_schedule.evaluate(now, scheduled)) {
			_schedule_status = _schedule.status();
			return reject(ManualWorkerStatus::ConditionRejected);
		}

		const auto control = _schedule_enabled ? _control.cycle(now, manual, state, report, scheduled.control) :
				     _control.cycle(now, manual, state, report);

		if (control == research_stabilized_manual::Status::NotDue) { return ManualWorkerStatus::NotDue; }

		if (control != research_stabilized_manual::Status::Ok) {
			if (_schedule_enabled && (control == research_stabilized_manual::Status::InvalidSchedule
						  || control == research_stabilized_manual::Status::GainTransitionRejected)) {
				_schedule_status = control == research_stabilized_manual::Status::InvalidSchedule ?
						   ControlScheduleStatus::InvalidModel : ControlScheduleStatus::TransitionRejected;
				return reject(ManualWorkerStatus::ConditionRejected);
			}

			return reject(ManualWorkerStatus::ControlRejected);
		}

		if (_new_throttle) {
			if (_edf.accept(now, permission, _latest_request) != EdfFault::None) { return reject(ManualWorkerStatus::PropulsionRejected); }

			_new_throttle = false;
		}

		const auto edf = _edf.sample(now, permission);

		if (edf.disposition != EdfDisposition::Command) { return reject(ManualWorkerStatus::PropulsionRejected); }

		uint64_t deadline = math::min(math::min(edf.deadline_us, expiry(state.attitude_sample_us, _state_age_us)),
					      math::min(expiry(state.rate_sample_us, _state_age_us), expiry(now, _cycle_age_us)));

		if (_schedule_enabled) { deadline = math::min(deadline, _schedule.deadline()); }

		if (deadline <= now || _sequence == UINT64_MAX) { return reject(ManualWorkerStatus::ControlRejected); }

		ManualWorkerOutput candidate{};
		candidate.timestamp_us = now;
		candidate.deadline_us = deadline;
		candidate.sequence = ++_sequence;
		candidate.canards = report.allocation.command;
		candidate.edf = edf;
		candidate.binding = _binding;

		if (_schedule_enabled) { _schedule.commit(scheduled); }

		_deadline = deadline;
		_last_calculation = now;
		_pending_banks = _binding.banks;
		output = candidate;
		return ManualWorkerStatus::Command;
	}

	ManualWorkerStatus cycleBench(uint64_t now, const EdfPermission &permission,
				      const research_stabilized_manual::ManualSample &manual, ManualWorkerOutput &output,
				      const ManualOutputBinding &binding, const ManualOutputAck *ack = nullptr)
	{
		if (!_active) { return ManualWorkerStatus::Inactive; }

		_edf.revoke();

		if (_mode != ControlMode::ServoBench || !sameManualOutputBinding(binding, _binding)
		    || now < _clock || now >= _deadline) { return reject(ManualWorkerStatus::OutputRejected); }

		_clock = now;

		if (!benchPermitted(now, permission)) { return reject(ManualWorkerStatus::PropulsionRejected); }

		if (_bench.monitor(now, manual) != research_actuator_command::BenchStatus::Ok
		    || !updateBenchInput(now, manual)) { return reject(ManualWorkerStatus::ControlRejected); }

		if (!acceptAcknowledgment(now, permission, ack)) { return reject(ManualWorkerStatus::OutputRejected); }

		if (_pending_banks) { return ManualWorkerStatus::Waiting; }

		if (now == _last_calculation) { return ManualWorkerStatus::NotDue; }

		research_actuator_command::BenchReport report{};
		const auto control = _bench.cycle(now, manual, report);

		if (control == research_actuator_command::BenchStatus::NotDue) { return ManualWorkerStatus::NotDue; }

		if (control != research_actuator_command::BenchStatus::Ok) { return reject(ManualWorkerStatus::ControlRejected); }

		const uint64_t raw_deadline = math::min(_latest_request.deadline_us, permission.deadline_us);
		const uint64_t deadline = math::min(raw_deadline, expiry(now, _cycle_age_us));

		if (deadline <= now || _sequence == UINT64_MAX) { return reject(ManualWorkerStatus::ControlRejected); }

		ManualWorkerOutput candidate{};
		candidate.timestamp_us = now;
		candidate.deadline_us = deadline;
		candidate.sequence = ++_sequence;
		candidate.mode = ControlMode::ServoBench;
		candidate.edf.disposition = EdfDisposition::Inhibited;
		candidate.edf.normalized = 0.F;
		candidate.edf.session = _latest_request.session;
		candidate.edf.source = _latest_request.source;
		candidate.edf.configuration = _latest_request.configuration;
		candidate.edf.sequence = _latest_request.sequence;
		candidate.edf.request_timestamp_us = _latest_request.timestamp_us;
		candidate.edf.deadline_us = raw_deadline;
		candidate.binding = _binding;

		for (unsigned i = 0; i < kCanards; ++i) { candidate.canards[i] = report.normalized[i + 1]; }

		_deadline = deadline;
		_last_calculation = now;
		_pending_banks = _binding.banks;
		output = candidate;
		return ManualWorkerStatus::Command;
	}

	ManualWorkerStatus cycleEdfBench(uint64_t now, const EdfPermission &permission,
					 const research_stabilized_manual::ManualSample &manual, ManualWorkerOutput &output,
					 const ManualOutputBinding &binding, const ManualOutputAck *ack = nullptr)
	{
		if (!_active) { return ManualWorkerStatus::Inactive; }

		if (_mode != ControlMode::EdfBench || !sameManualOutputBinding(binding, _binding)
		    || now < _clock || now >= _deadline) { return reject(ManualWorkerStatus::OutputRejected); }

		_clock = now;
		EdfRequest request{};
		const auto throttle = _throttle.evaluate(now, manual, request);

		if (throttle != ManualEdfStatus::Request && throttle != ManualEdfStatus::Held) {
			return reject(ManualWorkerStatus::ThrottleRejected);
		}

		// Consume/validate early raw traffic without queuing a future increase.
		// After the first zero frame is handled, a genuinely new raw sample is
		// required; holding a pre-ACK sample cannot unexpectedly raise the EDF.
		if (throttle == ManualEdfStatus::Request && _handled_sequence) {
			_latest_request = request;
			_new_throttle = true;
		}

		if (_edf.sample(now, permission).disposition != EdfDisposition::Command) {
			return reject(ManualWorkerStatus::PropulsionRejected);
		}

		if (!acceptAcknowledgment(now, permission, ack)) { return reject(ManualWorkerStatus::OutputRejected); }

		if (_pending_banks) { return ManualWorkerStatus::Waiting; }

		if (now - _last_calculation < research_actuator_command::kBenchIntervalUs) { return ManualWorkerStatus::NotDue; }

		if (_new_throttle) {
			if (_edf.accept(now, permission, _latest_request) != EdfFault::None) { return reject(ManualWorkerStatus::PropulsionRejected); }

			_new_throttle = false;
		}

		const auto edf = _edf.sample(now, permission);

		if (edf.disposition != EdfDisposition::Command) { return reject(ManualWorkerStatus::PropulsionRejected); }

		const uint64_t deadline = math::min(edf.deadline_us, expiry(now, _cycle_age_us));

		if (deadline <= now || _sequence == UINT64_MAX) { return reject(ManualWorkerStatus::ControlRejected); }

		ManualWorkerOutput candidate{};
		candidate.timestamp_us = now;
		candidate.deadline_us = deadline;
		candidate.sequence = ++_sequence;
		candidate.mode = ControlMode::EdfBench;
		candidate.canards = {}; // Calibrated physical zero, independent of flight trim.
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
		_schedule_enabled = false;
		_schedule.stop();
		_pending_banks = 0;
		_mode = ControlMode::None;
		_control.disable();
		_bench.reset();
		_edf.revoke();
	}
	uint64_t handledSequence() const { return _active ? _handled_sequence : 0; }
	ControlScheduleStatus scheduleStatus() const { return _schedule_status; }

private:
	bool consumeSession(uint64_t now, const EdfPermission &permission)
	{
		if (!now || now < _clock) { return false; }

		_clock = now;

		if (!permission.session || permission.session <= _last_session || permission.session > now) { return false; }

		_last_session = permission.session;
		return true;
	}
	bool benchPermitted(uint64_t now, const EdfPermission &permission) const
	{
		return ManualBankPreparation::permitted(permission, ControlMode::ServoBench)
		       && permission.timestamp_us && permission.session <= permission.timestamp_us && permission.timestamp_us <= now
		       && now < permission.deadline_us && permission.deadline_us - permission.timestamp_us <= _permission_lease_us
		       && permission.source && permission.configuration && permission.session == _latest_request.session
		       && permission.source == _latest_request.source && permission.configuration == _latest_request.configuration;
	}
	bool updateBenchInput(uint64_t now, const research_stabilized_manual::ManualSample &manual)
	{
		// The raw validator has already accepted this exact sample. A held sample
		// keeps its sequence and original lease; new observations cannot renew it.
		if (manual.timestamp_sample_us < _latest_request.session || manual.timestamp_sample_us > UINT64_MAX - _bench_input_lease_us
		    || now >= manual.timestamp_sample_us + _bench_input_lease_us) { return false; }

		if (manual.timestamp_sample_us > _latest_request.timestamp_us) {
			if (_latest_request.sequence == UINT64_MAX) { return false; }

			++_latest_request.sequence;
			_latest_request.timestamp_us = manual.timestamp_sample_us;
			_latest_request.deadline_us = manual.timestamp_sample_us + _bench_input_lease_us;
		}

		return true;
	}
	bool acceptAcknowledgment(uint64_t now, const EdfPermission &permission, const ManualOutputAck *ack)
	{
		if (!ack) { return true; }

		if (!_pending_banks || ack->bank >= 2 || !(_pending_banks & (1U << ack->bank))
		    || ack->driver != _binding.driver[ack->bank] || ack->routing != _binding.routing
		    || ack->session != permission.session || ack->source != permission.source
		    || ack->configuration != permission.configuration || ack->sequence != _sequence
		    || ack->command_timestamp_us != _last_calculation
		    || ack->accepted_us < _last_calculation || ack->accepted_us > now) { return false; }

		_pending_banks &= ~(1U << ack->bank);

		if (!_pending_banks) { _handled_sequence = _sequence; }

		return true;
	}
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
	ControlSchedule _schedule{};
	ControlScheduleStatus _schedule_status{ControlScheduleStatus::Inactive};
	bool _schedule_enabled{};
	research_stabilized_manual::StabilizedManualControl _control{};
	research_actuator_command::BenchControlOwner _bench{};
	ManualEdfInput _throttle{};
	uint64_t _state_age_us{};
	uint64_t _cycle_age_us{};
	uint64_t _sequence{};
	uint64_t _handled_sequence{};
	bool _active{};
	ManualOutputBinding _binding{};
	// Bench uses only raw identity/sequence/expiry metadata, never EDF admission.
	EdfRequest _latest_request{};
	uint64_t _bench_input_lease_us{}, _permission_lease_us{};
	uint64_t _last_session{}, _clock{};
	uint64_t _last_calculation{};
	uint64_t _deadline{};
	uint8_t _pending_banks{};
	bool _new_throttle{};
	ControlMode _mode{ControlMode::None};
};

} // namespace slaaps
