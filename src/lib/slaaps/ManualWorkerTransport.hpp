#pragma once

#include "BoundSubscription.hpp"
#include "ManualTransport.hpp"
#include "ManualWorker.hpp"
#include "SelectedManualInput.hpp"
#include <drivers/drv_hrt.h>
#include <uORB/Publication.hpp>
#include <cstring>

namespace slaaps
{

// Serialized supervisor-side transport. The supervisor supplies live permission,
// selected state/input and explicitly admitted advertisement revisions. The
// shared EDF owner outlives this endpoint. No physical admission or fallback is
// inferred here. A published command keeps the core's original deadline.
class ManualWorkerTransport
{
public:
	explicit ManualWorkerTransport(EdfOwner &edf) : _worker(edf) {}
	~ManualWorkerTransport() { if (_active || _recovering) { stop(); } }
	ManualWorkerTransport(const ManualWorkerTransport &) = delete;
	ManualWorkerTransport &operator=(const ManualWorkerTransport &) = delete;

	bool advertise()
	{
		if (_advertisement_revision) { return commandRevision() != 0; }

		if (!_command.advertise()) { return false; }

		_advertisement_revision = _lifetime.revision();
		return _advertisement_revision != 0;
	}
	unsigned commandRevision()
	{
		return _advertisement_revision && _lifetime.revision() == _advertisement_revision ? _advertisement_revision : 0;
	}
	bool active() const { return _active; }
	// D3/D4 recovery keeps command publication and acknowledgment subscriptions after a controller fault.
	// The supervisor must call recoverObserved every cycle; stop() ends recovery for good.
	bool recovering() const { return _recovering; }
	// Loss detected outside the core. Recovery starts only if the core has a handled flight frame.
	ManualWorkerStatus abandon(ManualWorkerStatus reason)
	{
		if (!_active || _bench_mode != ControlMode::None) { stop(); return reason; }

		return failOrRecover(_worker.abandon(reason, hrt_absolute_time()));
	}
	ManualWorkerStatus recoverObserved(const EdfPermission &permission, const ManualOutputBinding &binding)
	{
		if (!_recovering) { return ManualWorkerStatus::Inactive; }

		if (_lifetime.revision() != _revision) { return reject(); }

		ManualOutputAck pending[2] {};
		unsigned count{};

		for (unsigned bank = 0; bank < 2; ++bank) {
			if (!(_banks & (1U << bank))) { continue; }

			slaaps_manual_ack_s wire{};
			const auto read = acknowledgment(bank).read(wire);

			if (read == BoundRead::Rejected) { return reject(); }

			if (read == BoundRead::Waiting || (_have_ack[bank] && memcmp(&wire, &_last_ack[bank], sizeof(wire)) == 0)) { continue; }

			if (!decodeManualAck(hrt_absolute_time(), bank, wire, pending[count])) { return reject(); }

			memcpy(&_last_ack[bank], &wire, sizeof(wire));
			_have_ack[bank] = true;

			// A late reply to the frame pending at the loss is history, not a recovery acknowledgment.
			if (pending[count].sequence > _worker.recoveryBaseSequence()) { ++count; }
		}

		const uint64_t now = hrt_absolute_time();
		ManualWorkerOutput output{};
		ManualWorkerStatus result{ManualWorkerStatus::Inactive};

		for (unsigned i = 0; i < (count ? count : 1); ++i) {
			result = _worker.recoverCycle(now, permission, binding, output, count ? &pending[i] : nullptr);

			if (result == ManualWorkerStatus::Command) {
				slaaps_manual_command_s wire{};

				if (i + 1 < count || !encodeManualCommand(hrt_absolute_time(), output, wire) || !_command.publish(wire)) { return reject(); }

			} else if (result != ManualWorkerStatus::Waiting && result != ManualWorkerStatus::NotDue) {
				stop();
				return result;
			}
		}

		return result;
	}
	// D1 after companion loss in Acceleration: hold the current attitude in this session. Without a
	// handled flight frame or complete recovery data the worker falls back to D3 or an ordinary stop.
	ManualWorkerStatus holdAcceleration(const EdfPermission &permission, const SelectedManualInput::Snapshot &snapshot,
					    const ManualOutputBinding &binding, const OperatingConditionSample *condition = nullptr)
	{
		if (!_active || _bench_mode != ControlMode::None || _worker.mode() != ControlMode::Acceleration) { stop(); return ManualWorkerStatus::ControlRejected; }

		if (_lifetime.revision() != _revision) { return reject(); }

		const auto result = _worker.hold(hrt_absolute_time(), permission, snapshot.state, condition);

		if (result != ManualWorkerStatus::Ready) { return failOrRecover(result); }

		return holdObserved(permission, snapshot, binding, condition);
	}
	// One live D1 cycle. A Manual session keeps its selected-input guard. An Acceleration session observes
	// state through the companion state binding, so only the worker's own state checks apply, as they do
	// for ordinary Acceleration cycles. Loss of arming metadata or state is a D3 loss.
	ManualWorkerStatus holdObserved(const EdfPermission &permission, const SelectedManualInput::Snapshot &snapshot,
					const ManualOutputBinding &binding, const OperatingConditionSample *condition = nullptr)
	{
		if (!_active || !_worker.holding()) { return ManualWorkerStatus::Inactive; }

		const auto &armed = snapshot.permission;

		if (!snapshot.state.valid || !snapshot.permission_fresh || !armed.armed || armed.kill || armed.lockdown
		    || armed.termination || armed.in_esc_calibration_mode) { return loseSelectedInput(snapshot); }

		const bool guarded = _worker.mode() == ControlMode::Manual && _selected_session;
		return cycleImpl(0, permission, snapshot.manual, &snapshot.state, binding, guarded ? &snapshot : nullptr, 0, condition,
				 nullptr, false, true, true);
	}
	bool holding() const { return _active && _worker.holding(); }
	uint8_t failedBanks() const { return _recovering ? _worker.failedBanks() : 0; }
	bool attitudeGateClosed() const { return _active && _worker.attitudeGateClosed(); }
	void openAttitudeGate() { if (_active) { _worker.openAttitudeGate(); } }
	uint64_t holdStart() const { return _active ? _worker.holdStart() : 0; }
	uint64_t handledSequence() const { return _active ? _worker.handledSequence() : 0; }
	bool outstanding() const { return _active && _worker.outstanding(); }
	uint64_t outputDeadline() const { return _active ? _worker.outputDeadline() : 0; }
	ControlMode mode() const { return _active ? _worker.mode() : ControlMode::None; }
	bool handledOutput(ManualWorkerOutput &frame, uint64_t &handled_us) const
	{
		return _active && _worker.handledOutput(frame, handled_us);
	}
	ControlScheduleStatus scheduleStatus() const { return _worker.scheduleStatus(); }
	const AccelerationControlDiagnostic &accelerationDiagnostic() const { return _worker.accelerationDiagnostic(); }

	ManualWorkerStatus monitorTransferObserved(const EdfPermission &previous, const ManualOutputBinding &binding)
	{
		research_stabilized_manual::ManualSample unused{};
		return cycleImpl(0, previous, unused, nullptr, binding, nullptr, 0, nullptr, nullptr, true);
	}
	ManualWorkerStatus transferAcceleration(uint64_t now, uint64_t transition_us, const VehicleConfiguration &configuration,
						const EdfPermission &previous, const EdfPermission &next, const AccelerationControlInput &input,
						const ManualOutputBinding &binding)
	{
		if (!_active || _bench_mode != ControlMode::None || !handledBindingsCurrent()) { return reject(); }

		ManualWorkerOutput output{};
		const auto result = _worker.transferAcceleration(now, transition_us, configuration, previous, next, input, binding, output);
		return publishResult(result, output, true);
	}
	ManualWorkerStatus transferManual(uint64_t now, uint64_t transition_us, const VehicleConfiguration &configuration,
					  const EdfPermission &previous, const EdfPermission &next, const SelectedManualInput::Snapshot &snapshot,
					  const SelectedInputPolicy &policy, const ManualOutputBinding &binding, const OperatingConditionSample *condition = nullptr)
	{
		if (!_active || _bench_mode != ControlMode::None || !handledBindingsCurrent()
		    || (_selected_session ? !_selected_input.check(now, snapshot) : !_selected_input.begin(now, snapshot, policy))) { return reject(); }

		_selected_session = true;
		ManualWorkerOutput output{};
		const auto result = _worker.transferManual(now, transition_us, configuration, previous, next, snapshot.manual,
				    snapshot.state, binding, output, condition);
		return publishResult(result, output, true);
	}
	ManualWorkerStatus cycleAccelerationObserved(const EdfPermission &permission, const AccelerationControlInput &input,
			const ManualOutputBinding &binding)
	{
		research_stabilized_manual::ManualSample unused{};
		return cycleImpl(0, permission, unused, nullptr, binding, nullptr, 0, nullptr, &input);
	}
	ManualWorkerStatus cycleAcceleration(uint64_t now, const EdfPermission &permission, const AccelerationControlInput &input,
					     const ManualOutputBinding &binding)
	{
		research_stabilized_manual::ManualSample unused{};
		return cycleImpl(now, permission, unused, nullptr, binding, nullptr, 0, nullptr, &input, false, false);
	}

	ManualWorkerStatus begin(uint64_t now, const VehicleConfiguration &configuration, const EdfPermission &permission,
				 const research_stabilized_manual::ManualSample &manual,
				 const research_stabilized_manual::StateSample &state, const ManualOutputBinding &binding,
				 unsigned command_revision, const unsigned(&ack_revisions)[2], const OperatingConditionSample *condition = nullptr)
	{
		stop();
		const auto result = _worker.begin(now, configuration, permission, manual, state, binding, condition);

		if (result != ManualWorkerStatus::Ready) { return result; }

		return bind(now, permission, binding, command_revision, ack_revisions);
	}

	// The supervisor binds the raw selected-manual publisher independently.
	// Bench admission and ongoing input checks do not require estimator state.
	ManualWorkerStatus beginBench(uint64_t now, const VehicleConfiguration &configuration, const EdfPermission &permission,
				      const research_stabilized_manual::ManualSample &manual, const ManualOutputBinding &binding,
				      unsigned command_revision, const unsigned(&ack_revisions)[2], unsigned target = 0)
	{
		stop();
		const auto result = _worker.beginBench(now, configuration, permission, manual, binding, target);

		if (result != ManualWorkerStatus::Ready) { return result; }

		const auto bound = bind(now, permission, binding, command_revision, ack_revisions);

		if (bound == ManualWorkerStatus::Ready) { _bench_mode = ControlMode::ServoBench; }

		return bound;
	}

	ManualWorkerStatus beginEdfBench(uint64_t now, const VehicleConfiguration &configuration, const EdfPermission &permission,
					 const research_stabilized_manual::ManualSample &manual, const ManualOutputBinding &binding,
					 unsigned command_revision, const unsigned(&ack_revisions)[2])
	{
		stop();
		const auto result = _worker.beginEdfBench(now, configuration, permission, manual, binding);

		if (result != ManualWorkerStatus::Ready) { return result; }

		const auto bound = bind(now, permission, binding, command_revision, ack_revisions);

		if (bound == ManualWorkerStatus::Ready) { _bench_mode = ControlMode::EdfBench; }

		return bound;
	}

	// Production selected-input entry: core admission consumes the owner session
	// before metadata rejection, so a failed attempt cannot replay the same grant.
	ManualWorkerStatus begin(uint64_t now, const VehicleConfiguration &configuration, const EdfPermission &permission,
				 const SelectedManualInput::Snapshot &snapshot, const SelectedInputPolicy &policy,
				 const ManualOutputBinding &binding, unsigned command_revision, const unsigned(&ack_revisions)[2],
				 const OperatingConditionSample *condition = nullptr)
	{
		const auto result = begin(now, configuration, permission, snapshot.manual, snapshot.state, binding,
					  command_revision, ack_revisions, condition);

		if (result != ManualWorkerStatus::Ready) { return result; }

		if (!_selected_input.begin(now, snapshot, policy)) { stop(); return ManualWorkerStatus::ControlRejected; }

		_selected_session = true;
		return result;
	}

	ManualWorkerStatus cycle(uint64_t now, const EdfPermission &permission,
				 const SelectedManualInput::Snapshot &snapshot, const ManualOutputBinding &binding,
				 const OperatingConditionSample *condition = nullptr)
	{
		if (!_active) { return ManualWorkerStatus::Inactive; }

		if (!_selected_session) { stop(); return ManualWorkerStatus::ControlRejected; }

		if (!_selected_input.check(now, snapshot)) { return loseSelectedInput(snapshot); }

		return cycleImpl(now, permission, snapshot.manual, &snapshot.state, binding, nullptr, 0, condition);
	}

	// Live uORB observation can include replies published after the caller's
	// earlier state capture. Validate each copied reply against its actual read
	// time, then evaluate selected input and the worker after all ACK copies.
	// Explicit-time overloads above/below retain their strict caller-time checks.
	ManualWorkerStatus cycleObserved(const EdfPermission &permission, const SelectedManualInput::Snapshot &snapshot,
					 const ManualOutputBinding &binding, uint64_t first_command_deadline_us = 0,
					 const OperatingConditionSample *condition = nullptr)
	{
		if (!_active) { return ManualWorkerStatus::Inactive; }

		if (!_selected_session) { stop(); return ManualWorkerStatus::ControlRejected; }

		return cycleImpl(0, permission, snapshot.manual, &snapshot.state, binding, &snapshot, first_command_deadline_us, condition);
	}

	ManualWorkerStatus cycleBenchObserved(const EdfPermission &permission, const research_stabilized_manual::ManualSample &manual,
					      const ManualOutputBinding &binding, uint64_t first_command_deadline_us = 0)
	{
		if (!_active) { return ManualWorkerStatus::Inactive; }

		if (_bench_mode != ControlMode::ServoBench) { stop(); return ManualWorkerStatus::ControlRejected; }

		return cycleImpl(0, permission, manual, nullptr, binding, nullptr, first_command_deadline_us);
	}

	ManualWorkerStatus cycleEdfBenchObserved(const EdfPermission &permission, const research_stabilized_manual::ManualSample &manual,
			const ManualOutputBinding &binding, uint64_t first_command_deadline_us = 0)
	{
		if (!_active) { return ManualWorkerStatus::Inactive; }

		if (_bench_mode != ControlMode::EdfBench) { stop(); return ManualWorkerStatus::ControlRejected; }

		return cycleImpl(0, permission, manual, nullptr, binding, nullptr, first_command_deadline_us);
	}

	ManualWorkerStatus cycle(uint64_t now, const EdfPermission &permission,
				 const research_stabilized_manual::ManualSample &manual,
				 const research_stabilized_manual::StateSample &state, const ManualOutputBinding &binding,
				 const OperatingConditionSample *condition = nullptr)
	{
		if (_selected_session || _bench_mode != ControlMode::None) { stop(); return ManualWorkerStatus::ControlRejected; }

		return cycleImpl(now, permission, manual, &state, binding, nullptr, 0, condition);
	}

	void stop()
	{
		_active = false;
		_recovering = false;
		_selected_session = false;
		_bench_mode = ControlMode::None;
		_selected_input.stop();
		_worker.stop();

		for (unsigned bank = 0; bank < 2; ++bank) { acknowledgment(bank).revoke(); _have_ack[bank] = false; }
	}

private:
	bool handledBindingsCurrent()
	{
		if (_lifetime.revision() != _revision || _worker.outstanding()) { return false; }

		for (unsigned bank = 0; bank < 2; ++bank) {
			if (!(_banks & (1U << bank))) { continue; }

			slaaps_manual_ack_s wire{};

			if (!_have_ack[bank] || acknowledgment(bank).read(wire) != BoundRead::Data
			    || memcmp(&wire, &_last_ack[bank], sizeof(wire)) != 0) { return false; }
		}

		return true;
	}
	ManualWorkerStatus bind(uint64_t now, const EdfPermission &permission, const ManualOutputBinding &binding,
				unsigned command_revision, const unsigned(&ack_revisions)[2])
	{
		if (!command_revision || commandRevision() != command_revision) { return reject(); }

		for (unsigned bank = 0; bank < 2; ++bank) {
			if (!(binding.banks & (1U << bank))) {
				if (ack_revisions[bank]) { return reject(); }

				continue;
			}

			if (!acknowledgment(bank).bind(ack_revisions[bank])) { return reject(); }

			slaaps_manual_ack_s wire{};
			const auto read = acknowledgment(bank).read(wire);

			if (read == BoundRead::Rejected) { return reject(); }

			if (read == BoundRead::Data) {
				// A retained reply from an older session is a baseline only. It
				// cannot acknowledge this session's first command. Future/current
				// session replies at entry are not legitimate prior traffic.
				ManualOutputAck ack{};

				if (!decodeManualAck(now, bank, wire, ack) || ack.session >= permission.session) { return reject(); }

				memcpy(&_last_ack[bank], &wire, sizeof(wire));
				_have_ack[bank] = true;
			}
		}

		_revision = command_revision;
		_banks = binding.banks;
		_active = true;
		return ManualWorkerStatus::Ready;
	}
	ManualWorkerStatus cycleImpl(uint64_t now, const EdfPermission &permission,
				     const research_stabilized_manual::ManualSample &manual,
				     const research_stabilized_manual::StateSample *state, const ManualOutputBinding &binding,
				     const SelectedManualInput::Snapshot *observed = nullptr, uint64_t first_command_deadline_us = 0,
				     const OperatingConditionSample *condition = nullptr,
				     const AccelerationControlInput *acceleration = nullptr, bool monitor_only = false, bool acceleration_live = true,
				     bool hold_live = false)
	{
		if (!_active) { return ManualWorkerStatus::Inactive; }

		if (_lifetime.revision() != _revision) { return reject(); }

		const bool live = observed || _bench_mode != ControlMode::None || monitor_only || (acceleration && acceleration_live) || hold_live;
		ManualOutputAck pending[2] {};
		unsigned count{};

		// Validate every bound transport before advancing the controller or
		// publishing; a fault on the second bank cannot follow a new command.
		for (unsigned bank = 0; bank < 2; ++bank) {
			if (!(_banks & (1U << bank))) { continue; }

			slaaps_manual_ack_s wire{};
			const auto read = acknowledgment(bank).read(wire);

			if (read == BoundRead::Rejected) { return reject(); }

			if (read == BoundRead::Waiting || (_have_ack[bank] && memcmp(&wire, &_last_ack[bank], sizeof(wire)) == 0)) { continue; }

			const uint64_t read_time = live ? hrt_absolute_time() : now;

			if (!decodeManualAck(read_time, bank, wire, pending[count])) { return reject(); }

			memcpy(&_last_ack[bank], &wire, sizeof(wire));
			_have_ack[bank] = true;
			++count;
		}

		if (live) {
			now = hrt_absolute_time(); // Evaluate after the complete sequential ACK snapshot.

			if (first_command_deadline_us && now >= first_command_deadline_us) { return reject(); }

			if (observed && !_selected_input.check(now, *observed)) { return loseSelectedInput(*observed); }
		}

		ManualWorkerOutput output{};
		ManualWorkerStatus result{ManualWorkerStatus::Inactive};

		for (unsigned i = 0; i < (count ? count : 1); ++i) {
			const auto *ack = count ? &pending[i] : nullptr;

			if (monitor_only) { result = _worker.monitorTransfer(now, permission, binding, ack); }

			else if (acceleration) { result = _worker.cycleAcceleration(now, permission, *acceleration, output, binding, ack); }

			else if (_bench_mode == ControlMode::EdfBench) { result = _worker.cycleEdfBench(now, permission, manual, output, binding, ack); }

			else if (_bench_mode == ControlMode::ServoBench) { result = _worker.cycleBench(now, permission, manual, output, binding, ack); }

			else { result = _worker.cycle(now, permission, manual, *state, output, binding, ack, condition); }

			if (result == ManualWorkerStatus::Command) {
				// Any extra reply after a completed outstanding frame is invalid.
				if (i + 1 < count) { return reject(); }

				slaaps_manual_command_s wire{};
				const uint64_t published = live ? hrt_absolute_time() : now;

				if ((first_command_deadline_us && published >= first_command_deadline_us)
				    || !encodeManualCommand(published, output, wire) || !_command.publish(wire)) { return reject(); }

			} else if (result != ManualWorkerStatus::Waiting && result != ManualWorkerStatus::NotDue
				   && !(monitor_only && result == ManualWorkerStatus::Ready)) {
				return failOrRecover(result);
			}
		}

		return result;
	}
	// Estimator/selected-state loss while still armed without kill, lockdown or termination is a D3 loss.
	ManualWorkerStatus loseSelectedInput(const SelectedManualInput::Snapshot &snapshot)
	{
		const auto &armed = snapshot.permission;

		if (snapshot.permission_fresh && armed.armed && !armed.kill && !armed.lockdown && !armed.termination
		    && !armed.in_esc_calibration_mode) { return abandon(ManualWorkerStatus::ControlRejected); }

		stop();
		return ManualWorkerStatus::ControlRejected;
	}
	// A core fault that started recovery keeps the bound publication and acknowledgment state.
	ManualWorkerStatus failOrRecover(ManualWorkerStatus result)
	{
		if (!_worker.recovering()) { stop(); return result; }

		_active = false;
		_recovering = true;
		_selected_session = false;
		_bench_mode = ControlMode::None;
		_selected_input.stop();
		return result;
	}
	ManualWorkerStatus publishResult(ManualWorkerStatus result, const ManualWorkerOutput &output, bool live)
	{
		if (result == ManualWorkerStatus::Command) {
			slaaps_manual_command_s wire{};
			const uint64_t now = live ? hrt_absolute_time() : output.timestamp_us;

			if (_lifetime.revision() != _revision || !encodeManualCommand(now, output, wire) || !_command.publish(wire)) { return reject(); }

		} else if (result != ManualWorkerStatus::NotDue && result != ManualWorkerStatus::Waiting) { stop(); }

		return result;
	}

	BoundSubscription<slaaps_manual_ack_s> &acknowledgment(unsigned bank) { return bank == 0 ? _main_ack : _aux_ack; }
	ManualWorkerStatus reject()
	{
		_worker.stop(ManualWorkerStatus::OutputRejected, hrt_absolute_time());
		stop();
		return ManualWorkerStatus::OutputRejected;
	}
	ManualWorker _worker;
	SelectedManualInput _selected_input{};
	bool _selected_session{};
	ControlMode _bench_mode{ControlMode::None};
	uORB::Publication<slaaps_manual_command_s> _command{ORB_ID::slaaps_manual_command};
	uORB::ResearchSubscription _lifetime{ORB_ID::slaaps_manual_command};
	BoundSubscription<slaaps_manual_ack_s> _main_ack{ORB_ID::slaaps_manual_ack_main};
	BoundSubscription<slaaps_manual_ack_s> _aux_ack{ORB_ID::slaaps_manual_ack_aux};
	slaaps_manual_ack_s _last_ack[2] {};
	bool _have_ack[2] {};
	unsigned _revision{};
	unsigned _advertisement_revision{};
	uint8_t _banks{};
	bool _active{};
	bool _recovering{};
};

} // namespace slaaps
