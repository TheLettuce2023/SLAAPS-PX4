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
	~ManualWorkerTransport() { if (_active) { stop(); } }
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
	uint64_t handledSequence() const { return _active ? _worker.handledSequence() : 0; }
	ControlScheduleStatus scheduleStatus() const { return _worker.scheduleStatus(); }

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

		if (!_selected_session || !_selected_input.check(now, snapshot)) { stop(); return ManualWorkerStatus::ControlRejected; }

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
		_selected_session = false;
		_bench_mode = ControlMode::None;
		_selected_input.stop();
		_worker.stop();

		for (unsigned bank = 0; bank < 2; ++bank) { acknowledgment(bank).revoke(); _have_ack[bank] = false; }
	}

private:
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
				     const OperatingConditionSample *condition = nullptr)
	{
		if (!_active) { return ManualWorkerStatus::Inactive; }

		if (_lifetime.revision() != _revision) { return reject(); }

		const bool live = observed || _bench_mode != ControlMode::None;
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

			if (observed && !_selected_input.check(now, *observed)) { stop(); return ManualWorkerStatus::ControlRejected; }
		}

		ManualWorkerOutput output{};
		ManualWorkerStatus result{ManualWorkerStatus::Inactive};

		for (unsigned i = 0; i < (count ? count : 1); ++i) {
			const auto *ack = count ? &pending[i] : nullptr;

			if (_bench_mode == ControlMode::EdfBench) { result = _worker.cycleEdfBench(now, permission, manual, output, binding, ack); }

			else if (_bench_mode == ControlMode::ServoBench) { result = _worker.cycleBench(now, permission, manual, output, binding, ack); }

			else { result = _worker.cycle(now, permission, manual, *state, output, binding, ack, condition); }

			if (result == ManualWorkerStatus::Command) {
				// Any extra reply after a completed outstanding frame is invalid.
				if (i + 1 < count) { return reject(); }

				slaaps_manual_command_s wire{};
				const uint64_t published = live ? hrt_absolute_time() : now;

				if ((first_command_deadline_us && published >= first_command_deadline_us)
				    || !encodeManualCommand(published, output, wire) || !_command.publish(wire)) { return reject(); }

			} else if (result != ManualWorkerStatus::Waiting && result != ManualWorkerStatus::NotDue) {
				stop();
				return result;
			}
		}

		return result;
	}

	BoundSubscription<slaaps_manual_ack_s> &acknowledgment(unsigned bank) { return bank == 0 ? _main_ack : _aux_ack; }
	ManualWorkerStatus reject() { stop(); return ManualWorkerStatus::OutputRejected; }
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
};

} // namespace slaaps
