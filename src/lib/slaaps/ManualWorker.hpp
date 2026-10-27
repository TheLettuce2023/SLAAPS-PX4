#pragma once

#include "ManualEdfInput.hpp"
#include "ManualOutput.hpp"
#include "ManualBankPreparation.hpp"
#include "ControlSchedule.hpp"
#include "AccelerationControllerPort.hpp"
#include <lib/mathlib/math/Limits.hpp>
#include <cmath>
#include <cstdint>

namespace slaaps
{

enum class ManualWorkerStatus : uint8_t {
	Ready, Command, NotDue, Waiting, Inactive, ConfigurationRejected, ControlRejected, ThrottleRejected, PropulsionRejected,
	OutputRejected, ConditionRejected, HistoryUnavailable, AccelerationRejected
};
enum class AccelerationDiagnosticState : uint8_t { Inactive, Ready, Calculated, Handled, Rejected };

// Historical model/command evidence, never output permission or measured
// actuator feedback. Calculated and exact all-bank-handled identities differ.
struct AccelerationControlDiagnostic {
	uint64_t session{}, configuration{}, request_observed_us{}, fault_us{};
	uint64_t request_source{}, request_session{}, request_sequence{};
	uint64_t calculated_sequence{}, calculated_deadline_us{}, handled_sequence{}, handled_us{}, handled_deadline_us{};
	AccelerationControlFault fault{AccelerationControlFault::None};
	AccelerationAllocationStage allocation_stage{AccelerationAllocationStage::None};
	WrenchAllocationResult allocation_result{WrenchAllocationResult::Success};
	ManualWorkerStatus worker_status{ManualWorkerStatus::Inactive};
	AccelerationDiagnosticState state{AccelerationDiagnosticState::Inactive};
	uint8_t calculated_moment_mask{}, calculated_canard_mask{}, handled_moment_mask{}, handled_canard_mask{};
};

// Serialized manual/servo-bench worker core. Retain its session history across
// mode changes. The supervisor owns the shared EDF owner across replacement.
// Flight handoff uses only the explicit same-session transfer entry points.
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
		_configuration = configuration;
		_observed_state = state;
		_mode = ControlMode::Manual;
		_last_calculation = now;
		_deadline = expiry(now, _cycle_age_us);
		_new_throttle = false;
		_acceleration_diagnostic = {}; // A successfully admitted new session clears historical diagnostics.
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
		_acceleration_diagnostic = {}; // A successfully admitted new session clears historical diagnostics.
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
		_acceleration_diagnostic = {}; // A successfully admitted new session clears historical diagnostics.
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

		_observed_state = state;

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
		candidate.transition_us = permission.transition_us;
		_pending_rate_setpoint = report.rate_setpoint_rad_s;

		if (_schedule_enabled) { _schedule.commit(scheduled); }

		_deadline = deadline;
		_last_calculation = now;
		stage(candidate);
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
		stage(candidate);
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
		stage(candidate);
		output = candidate;
		return ManualWorkerStatus::Command;
	}

	void stop(ManualWorkerStatus failure = ManualWorkerStatus::Inactive, uint64_t observed_us = 0)
	{
		_recovering = false;

		if (failure != ManualWorkerStatus::Inactive && _acceleration_diagnostic.state != AccelerationDiagnosticState::Inactive
		    && _acceleration_diagnostic.state != AccelerationDiagnosticState::Rejected) {
			_acceleration_diagnostic.state = AccelerationDiagnosticState::Rejected;
			_acceleration_diagnostic.worker_status = failure;
			_acceleration_diagnostic.fault_us = observed_us ? observed_us :
							    _clock;
			const auto fault = _acceleration.fault();

			if (fault != AccelerationControlFault::Disabled) { _acceleration_diagnostic.fault = fault; }

			_acceleration_diagnostic.allocation_stage = _acceleration.allocationStage();
			_acceleration_diagnostic.allocation_result = _acceleration.allocationResult();

		} else if (_acceleration_diagnostic.state != AccelerationDiagnosticState::Rejected) {
			_acceleration_diagnostic.state = AccelerationDiagnosticState::Inactive;
		}

		_active = false;
		_schedule_enabled = false;
		_schedule.stop();
		_pending_banks = 0;
		_mode = ControlMode::None;
		_control.disable();
		_bench.reset();
		_edf.revoke();
		_acceleration.disable();
		_handled_us = 0;
		_acknowledged_us = 0;
		_have_handled = false;
	}
	uint64_t handledSequence() const { return _active ? _handled_sequence : 0; }
	bool recovering() const { return _recovering; }
	uint64_t recoveryBaseSequence() const { return _recovering ? _recovery_base_sequence : 0; }
	// Supervisor- or transport-detected loss (mode left, power, estimator) while the core itself is still
	// healthy. Same entry as a core fault: recovery only with at least one handled flight frame.
	ManualWorkerStatus abandon(ManualWorkerStatus reason, uint64_t now)
	{
		if (!_active) { return ManualWorkerStatus::Inactive; }

		return reject(reason, now);
	}
	// One recovery cycle after a fault. Produces a recovery frame (frozen canards, EDF from the owner's
	// recover()) only when every earlier recovery frame is acknowledged; ends recovery when the owner
	// reports it finished. Acknowledgments of frames older than recovery are ignored.
	ManualWorkerStatus recoverCycle(uint64_t now, const EdfPermission &permission, const ManualOutputBinding &binding,
					ManualWorkerOutput &output, const ManualOutputAck *ack = nullptr)
	{
		if (!_recovering) { return ManualWorkerStatus::Inactive; }

		if (now < _clock || !sameManualOutputBinding(binding, _binding)) { return endRecovery(ManualWorkerStatus::OutputRejected); }

		_clock = now;

		if (ack && ack->sequence <= _recovery_base_sequence) { ack = nullptr; }

		if (!acceptAcknowledgment(now, permission, ack)) { return endRecovery(ManualWorkerStatus::OutputRejected); }

		if (_pending_banks) { return ManualWorkerStatus::Waiting; }

		if (now == _last_calculation) { return ManualWorkerStatus::NotDue; }

		const auto edf = _edf.recover(now, permission);

		if (edf.disposition != EdfDisposition::Recovery || edf.deadline_us <= now || _sequence == UINT64_MAX) {
			return endRecovery(ManualWorkerStatus::Inactive);
		}

		ManualWorkerOutput candidate{};
		candidate.timestamp_us = now;
		candidate.deadline_us = edf.deadline_us;
		candidate.sequence = ++_sequence;
		candidate.canards = _recovery_canards;
		candidate.edf = edf;
		candidate.binding = _binding;
		candidate.mode = _recovery_mode;
		candidate.transition_us = _recovery_transition_us;
		_deadline = candidate.deadline_us;
		_last_calculation = now;
		stage(candidate);
		output = candidate;
		return ManualWorkerStatus::Command;
	}
	bool outstanding() const { return _active && _pending_banks; }
	uint64_t outputDeadline() const { return _active ? _deadline : 0; }
	ControlMode mode() const { return _active ? _mode : ControlMode::None; }
	const AccelerationControlDiagnostic &accelerationDiagnostic() const { return _acceleration_diagnostic; }
	bool handledOutput(ManualWorkerOutput &output, uint64_t &handled_us) const
	{
		if (!_active || !_have_handled) { return false; }

		output = _handled_output;
		handled_us = _handled_us;
		return true;
	}
	// ACK-only transition observation. No controller calculation, EDF request,
	// command publication or lease renewal is possible through this path.
	ManualWorkerStatus monitorTransfer(uint64_t now, const EdfPermission &previous,
					   const ManualOutputBinding &binding, const ManualOutputAck *ack = nullptr)
	{
		if (!_active) { return ManualWorkerStatus::Inactive; }


		if (!ManualBankPreparation::flightMode(_mode) || now < _clock || now >= _deadline
		    || !sameManualOutputBinding(binding, _binding)
		    || _edf.sample(now, previous).disposition != EdfDisposition::Command) { return reject(ManualWorkerStatus::OutputRejected, now); }

		if (_mode == ControlMode::Acceleration && _acceleration.pending()) {
			// Cleanup of one already issued ticket uses its original authenticated
			// evidence. It never retrieves/creates a newly eligible old-mode request.
			_pending_acceleration_input.now_us = now;
			observeAcceleration(_pending_acceleration_input);
			const auto current = _edf.sample(now, previous);
			_pending_acceleration_input.actual_edf_command = current.normalized;
			_pending_acceleration_input.actual_edf_deadline_us = current.deadline_us;

			if (!commandToThrust(_configuration, current.normalized, _pending_acceleration_input.actual_edf_thrust_n)) { return reject(ManualWorkerStatus::PropulsionRejected, now); }

			if (_acceleration.monitor(_pending_acceleration_input) != AccelerationPortStatus::Ready) { return reject(ManualWorkerStatus::AccelerationRejected, now); }
		}

		if (!acceptAcknowledgment(now, previous, ack)) { return reject(ManualWorkerStatus::OutputRejected, now); }

		if (!_pending_banks && _acceleration.pending() && !_acceleration.commitHandled(_handled_us)) { return reject(ManualWorkerStatus::AccelerationRejected, now); }

		_clock = now;
		return _pending_banks ? ManualWorkerStatus::Waiting : ManualWorkerStatus::Ready;
	}

	ManualWorkerStatus transferAcceleration(uint64_t now, uint64_t transition_us, const VehicleConfiguration &configuration,
						const EdfPermission &previous, const EdfPermission &next, const AccelerationControlInput &input,
						const ManualOutputBinding &binding, ManualWorkerOutput &output)
	{
		if (!transferReady(now, transition_us, configuration, previous, next, binding, ControlMode::Acceleration)
		    || input.selected_state.attitude_reset_counter != _observed_state.attitude_reset_counter
		    || input.request.producer_time_in_validator_domain_us <= input.request.clock_mapping_uncertainty_us
		    || input.request.producer_time_in_validator_domain_us - input.request.clock_mapping_uncertainty_us
		    <= transition_us) { return reject(ManualWorkerStatus::HistoryUnavailable, now); }

		_acceleration_diagnostic = {};
		_acceleration_diagnostic.session = next.session;
		_acceleration_diagnostic.configuration = next.configuration;
		_acceleration_diagnostic.state = AccelerationDiagnosticState::Ready;
		observeAcceleration(input);

		if (_acceleration.configure(configuration) != AccelerationPortStatus::Ready) { return reject(ManualWorkerStatus::ConfigurationRejected, now); }

		_configuration = configuration;
		return calculateAcceleration(now, previous, next, input, binding, output, nullptr, transition_us);
	}

	ManualWorkerStatus cycleAcceleration(uint64_t now, const EdfPermission &permission, const AccelerationControlInput &input,
					     ManualWorkerOutput &output, const ManualOutputBinding &binding, const ManualOutputAck *ack = nullptr)
	{
		if (!_active) { return ManualWorkerStatus::Inactive; }


		if (_mode != ControlMode::Acceleration) { return reject(ManualWorkerStatus::OutputRejected, now); }

		return calculateAcceleration(now, permission, permission, input, binding, output, ack, 0);
	}

	ManualWorkerStatus transferManual(uint64_t now, uint64_t transition_us, const VehicleConfiguration &configuration,
					  const EdfPermission &previous, const EdfPermission &next,
					  const research_stabilized_manual::ManualSample &manual, const research_stabilized_manual::StateSample &state,
					  const ManualOutputBinding &binding, ManualWorkerOutput &output, const OperatingConditionSample *condition = nullptr)
	{

		if (!transferReady(now, transition_us, configuration, previous, next, binding, ControlMode::Manual)
		    || state.attitude_reset_counter != _observed_state.attitude_reset_counter) { return reject(ManualWorkerStatus::HistoryUnavailable, now); }

		if (now - _handled_us < static_cast<uint64_t>(configuration.cycle_min_us)) { return ManualWorkerStatus::NotDue; }

		research_stabilized_manual::Configuration control{};

		if (!makeManualConfiguration(configuration, control)) { return reject(ManualWorkerStatus::ConfigurationRejected, now); }

		control.fixed_simulated_thrust_n = 0.F;
		const auto current = _edf.sample(now, previous);
		_schedule_enabled = configuration.schedule.enabled == 1;

		if (_schedule_enabled) {
			ControlSchedule::Step initial{};

			if (!condition || !_schedule.begin(now, configuration, *condition, current.normalized, initial)) { return reject(ManualWorkerStatus::ConditionRejected, now); }

			control.rate_p = initial.control.rate_p;
			control.rate_i = initial.control.rate_i;
			control.rate_d = initial.control.rate_d;
			control.allocation.effectiveness = initial.control.effectiveness;
		}

		if (_control.configure(control) != research_stabilized_manual::Status::Ok
		    || _control.enableFromHandled(now, manual, state, _handled_us, _handled_output.canards, _handled_rate_setpoint)
		    != research_stabilized_manual::Status::Ok
		    || !_throttle.configureForTransfer(configuration, next, transition_us, current.sequence)) { return reject(ManualWorkerStatus::ControlRejected, now); }

		EdfRequest request{};

		if (_throttle.evaluate(now, manual, request) != ManualEdfStatus::Request) { return reject(ManualWorkerStatus::ThrottleRejected, now); }

		request.deadline_us = math::min(request.deadline_us, math::min(_deadline, math::min(previous.deadline_us, next.deadline_us)));
		EdfCommand edf{};

		if (_edf.transfer(now, transition_us, previous, next, request, edf) != EdfFault::None) { return reject(ManualWorkerStatus::PropulsionRejected, now); }

		_acceleration.disable();
		_acceleration_diagnostic.state = AccelerationDiagnosticState::Inactive;
		_configuration = configuration;
		_mode = ControlMode::Manual;
		_clock = now;
		_observed_state = state;
		_latest_request = request;
		_new_throttle = false;
		_state_age_us = control.maximum_state_age_us;
		_cycle_age_us = control.maximum_cycle_us;
		ManualWorkerOutput candidate = _handled_output;
		candidate.timestamp_us = now;
		candidate.deadline_us = math::min(_deadline, edf.deadline_us);
		candidate.edf = edf;
		candidate.mode = _mode;
		candidate.transition_us = transition_us;
		candidate.previous_handled_sequence = _handled_output.sequence;
		candidate.previous_deadline_us = _deadline;
		candidate.sequence = ++_sequence;
		_pending_rate_setpoint = _handled_rate_setpoint;
		_last_calculation = now;
		_deadline = candidate.deadline_us;
		stage(candidate);
		output = candidate;
		return ManualWorkerStatus::Command;
	}
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

		_acknowledged_us = math::max(_acknowledged_us, ack->accepted_us);
		_pending_banks &= ~(1U << ack->bank);

		if (!_pending_banks) {
			_handled_sequence = _sequence;
			_handled_output = _pending_output;
			_handled_us = _acknowledged_us;
			_handled_rate_setpoint = _pending_rate_setpoint;
			_have_handled = true;

			if (_pending_output.mode == ControlMode::Acceleration && !_recovering) {
				_acceleration_diagnostic.handled_sequence = _sequence;
				_acceleration_diagnostic.handled_us = _acknowledged_us;
				_acceleration_diagnostic.handled_deadline_us = _pending_output.deadline_us;
				_acceleration_diagnostic.handled_moment_mask = _acceleration_diagnostic.calculated_moment_mask;
				_acceleration_diagnostic.handled_canard_mask = _acceleration_diagnostic.calculated_canard_mask;
				_acceleration_diagnostic.state = AccelerationDiagnosticState::Handled;
			}
		}

		return true;
	}
	void stage(const ManualWorkerOutput &output)
	{
		_pending_output = output;
		_acknowledged_us = 0;
		_pending_banks = _binding.banks;
	}
	bool sameStateSource(const research_stabilized_manual::StateSample &state) const
	{
		return state.source_epoch == _observed_state.source_epoch
		       && state.attitude_reset_counter == _observed_state.attitude_reset_counter;
	}
	bool transferReady(uint64_t now, uint64_t transition_us, const VehicleConfiguration &configuration,
			   const EdfPermission &previous, const EdfPermission &next, const ManualOutputBinding &binding, ControlMode target) const
	{
		return _active && _have_handled && !_pending_banks && _sequence != UINT64_MAX && now >= _clock && now < _deadline
		       && _handled_us <= now
		       && transition_us > previous.transition_us && transition_us > previous.session
		       && transition_us < now && previous.timestamp_us <= transition_us && next.timestamp_us >= transition_us
		       && next.transition_us == transition_us && previous.mode == _mode && next.mode == target && _mode != target
		       && ManualBankPreparation::flightMode(_mode) && ManualBankPreparation::flightMode(target)
		       && previous.session == next.session && previous.source == next.source && previous.configuration == next.configuration
		       && fingerprint(configuration) == next.configuration && sameManualOutputBinding(binding, _binding)
		       && ManualBankPreparation::permitted(previous, _mode) && ManualBankPreparation::permitted(next, target);
	}
	ManualWorkerStatus calculateAcceleration(uint64_t now, const EdfPermission &previous, const EdfPermission &permission,
			const AccelerationControlInput &provided, const ManualOutputBinding &binding, ManualWorkerOutput &output,
			const ManualOutputAck *ack, uint64_t transition_us)
	{
		if (!_active || !_have_handled || now < _clock || now >= _deadline || !sameManualOutputBinding(binding, _binding)
		    || (transition_us ? provided.selected_state.attitude_reset_counter != _observed_state.attitude_reset_counter :
			!sameStateSource(provided.selected_state))) { return reject(ManualWorkerStatus::HistoryUnavailable, now); }

		_clock = now;
		observeAcceleration(provided);
		const auto current = _edf.sample(now, previous);
		float current_thrust{};

		if (current.disposition != EdfDisposition::Command || !commandToThrust(_configuration, current.normalized, current_thrust)) { return reject(ManualWorkerStatus::PropulsionRejected, now); }

		AccelerationControlInput input = provided;
		input.now_us = now;
		input.previous_handled_us = _handled_us;
		input.previous_handled_rate_setpoint = _handled_rate_setpoint;
		input.actual_edf_command = current.normalized;
		input.actual_edf_thrust_n = current_thrust;
		input.actual_edf_deadline_us = current.deadline_us;

		for (unsigned i = 0; i < kCanards; ++i) { input.previous_handled_canard[i] = _handled_output.canards[i]; }

		if (_acceleration.monitor(input) != AccelerationPortStatus::Ready) { return reject(ManualWorkerStatus::AccelerationRejected, now); }

		if (!acceptAcknowledgment(now, permission, ack)) { return reject(ManualWorkerStatus::OutputRejected, now); }

		if (_pending_banks) { return ManualWorkerStatus::Waiting; }

		if (_acceleration.pending()) {
			if (!_acceleration.commitHandled(_handled_us)) { return reject(ManualWorkerStatus::AccelerationRejected, now); }

			input.previous_handled_us = _handled_us;
			input.previous_handled_rate_setpoint = _handled_rate_setpoint;

			for (unsigned i = 0; i < kCanards; ++i) { input.previous_handled_canard[i] = _handled_output.canards[i]; }
		}

		AccelerationControlOutput calculated{};
		const auto result = _acceleration.cycle(input, calculated);

		if (result == AccelerationPortStatus::NotDue) { return ManualWorkerStatus::NotDue; }

		if (result != AccelerationPortStatus::Calculated || _sequence == UINT64_MAX || current.sequence == UINT64_MAX) { return reject(ManualWorkerStatus::AccelerationRejected, now); }

		EdfRequest request{};
		request.session = permission.session;
		request.source = permission.source;
		request.configuration = permission.configuration;
		request.sequence = current.sequence + 1;
		request.timestamp_us = now;
		// calculated.deadline_us retains the authenticated request's conservative
		// original expiry/maximum age. Fresh feedback may update the target while
		// that body-held request lives, but cannot renew its ingress lease.
		request.deadline_us = math::min(calculated.deadline_us, math::min(permission.deadline_us,
						expiry(now, static_cast<uint64_t>(_configuration.output_lease_ms) * 1000)));
		request.mode = ControlMode::Acceleration;
		request.units = EdfUnits::Newtons;
		request.value = calculated.edf_thrust_n;
		EdfCommand edf{};

		if (transition_us) {
			request.deadline_us = math::min(request.deadline_us, math::min(_deadline, math::min(current.deadline_us, previous.deadline_us)));

			if (_edf.transfer(now, transition_us, previous, permission, request, edf) != EdfFault::None) { return reject(ManualWorkerStatus::PropulsionRejected, now); }

		} else {
			if (_edf.accept(now, permission, request) != EdfFault::None) { return reject(ManualWorkerStatus::PropulsionRejected, now); }

			edf = _edf.sample(now, permission);
		}

		// Accepting a new target at this same calculation instant must not move
		// the current EDF point used by the coupled canard solve.
		if (edf.disposition != EdfDisposition::Command || !(edf.normalized <= current.normalized && edf.normalized >= current.normalized)) { return reject(ManualWorkerStatus::PropulsionRejected, now); }

		ManualWorkerOutput candidate{};
		candidate.timestamp_us = now;
		candidate.deadline_us = math::min(calculated.deadline_us, edf.deadline_us);
		candidate.sequence = ++_sequence;
		candidate.edf = edf;
		candidate.binding = _binding;
		candidate.mode = ControlMode::Acceleration;
		candidate.transition_us = permission.transition_us;

		if (transition_us) {
			candidate.previous_handled_sequence = _handled_output.sequence;
			candidate.previous_deadline_us = _deadline;
		}

		for (unsigned i = 0; i < kCanards; ++i) { candidate.canards[i] = calculated.canard[i]; }

		if (transition_us) {
			// The outgoing numerical controller no longer owns calculations.
			// Preserve shared propulsion and acknowledged output history so a
			// later deliberate reverse entry can seed from actual handling.
			_control.disable();
			_schedule.stop();
			_schedule_enabled = false;
		}

		_mode = candidate.mode;
		_observed_state = input.selected_state;
		_pending_rate_setpoint = calculated.rate_setpoint_rad_s;
		_pending_acceleration_input = input;
		_deadline = candidate.deadline_us;
		_last_calculation = now;
		stage(candidate);
		_acceleration_diagnostic.calculated_sequence = candidate.sequence;
		_acceleration_diagnostic.calculated_deadline_us = candidate.deadline_us;
		_acceleration_diagnostic.calculated_moment_mask = 0;
		_acceleration_diagnostic.calculated_canard_mask = calculated.canard_limit_mask;
		_acceleration_diagnostic.worker_status = ManualWorkerStatus::Command;
		_acceleration_diagnostic.state = AccelerationDiagnosticState::Calculated;

		for (unsigned axis = 0; axis < kAxes; ++axis) {
			if (calculated.saturation_positive(axis)) { _acceleration_diagnostic.calculated_moment_mask |= 1U << axis; }

			if (calculated.saturation_negative(axis)) { _acceleration_diagnostic.calculated_moment_mask |= 1U << (axis + kAxes); }
		}

		output = candidate;
		return ManualWorkerStatus::Command;
	}
	static uint64_t expiry(uint64_t time, uint64_t age)
	{
		return time > UINT64_MAX - age ? UINT64_MAX : time + age;
	}
	// A fault in an active flight session with at least one fully handled frame starts D3/D4 recovery:
	// canards hold the last handled values and the shared EDF owner ramps propulsion off. Every other
	// stop, including deliberate stop() and begin(), ends recovery.
	ManualWorkerStatus reject(ManualWorkerStatus reason, uint64_t observed_us = 0)
	{
		const bool recover = _active && _have_handled && ManualBankPreparation::flightMode(_mode);
		const auto canards = _handled_output.canards;
		const auto mode = _mode;
		const uint64_t transition = _handled_output.transition_us;
		const uint64_t at = observed_us ? observed_us : _clock;
		stop(reason, observed_us);

		if (recover) {
			_edf.abandon(at);
			_recovering = true;
			_recovery_canards = canards;
			_recovery_mode = mode;
			_recovery_transition_us = transition;
			_recovery_base_sequence = _sequence;
		}

		return reason;
	}
	void observeAcceleration(const AccelerationControlInput &input)
	{
		_acceleration_diagnostic.request_observed_us = input.context.validator_receive_time_us;
		_acceleration_diagnostic.request_source = input.request.source;
		_acceleration_diagnostic.request_session = input.request.session;
		_acceleration_diagnostic.request_sequence = input.request.sequence;
	}
	ManualWorkerStatus endRecovery(ManualWorkerStatus status)
	{
		_recovering = false;
		_pending_banks = 0;
		return status;
	}
	bool _recovering{};
	research_stabilized_manual::CanardVector _recovery_canards{};
	ControlMode _recovery_mode{ControlMode::None};
	uint64_t _recovery_transition_us{}, _recovery_base_sequence{};
	EdfOwner &_edf;
	AccelerationControllerPort _acceleration{};
	AccelerationControlDiagnostic _acceleration_diagnostic{};
	AccelerationControlInput _pending_acceleration_input{};
	VehicleConfiguration _configuration{};
	research_stabilized_manual::StateSample _observed_state{};
	ManualWorkerOutput _pending_output{}, _handled_output{};
	research_stabilized_manual::Vector3f _pending_rate_setpoint{}, _handled_rate_setpoint{};
	uint64_t _handled_us{}, _acknowledged_us{};
	bool _have_handled{};
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
