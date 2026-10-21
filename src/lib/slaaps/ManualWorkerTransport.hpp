#pragma once

#include "BoundSubscription.hpp"
#include "ManualTransport.hpp"
#include "ManualWorker.hpp"
#include "SelectedManualInput.hpp"
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

	ManualWorkerStatus begin(uint64_t now, const VehicleConfiguration &configuration, const EdfPermission &permission,
				 const research_stabilized_manual::ManualSample &manual,
				 const research_stabilized_manual::StateSample &state, const ManualOutputBinding &binding,
				 unsigned command_revision, const unsigned(&ack_revisions)[2])
	{
		stop();
		const auto result = _worker.begin(now, configuration, permission, manual, state, binding);

		if (result != ManualWorkerStatus::Ready) { return result; }

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

	// Production selected-input entry: core admission consumes the owner session
	// before metadata rejection, so a failed attempt cannot replay the same grant.
	ManualWorkerStatus begin(uint64_t now, const VehicleConfiguration &configuration, const EdfPermission &permission,
				 const SelectedManualInput::Snapshot &snapshot, const SelectedInputPolicy &policy,
				 const ManualOutputBinding &binding, unsigned command_revision, const unsigned(&ack_revisions)[2])
	{
		const auto result = begin(now, configuration, permission, snapshot.manual, snapshot.state, binding,
					  command_revision, ack_revisions);

		if (result != ManualWorkerStatus::Ready) { return result; }

		if (!_selected_input.begin(now, snapshot, policy)) { stop(); return ManualWorkerStatus::ControlRejected; }

		_selected_session = true;
		return result;
	}

	ManualWorkerStatus cycle(uint64_t now, const EdfPermission &permission,
				 const SelectedManualInput::Snapshot &snapshot, const ManualOutputBinding &binding)
	{
		if (!_active) { return ManualWorkerStatus::Inactive; }

		if (!_selected_session || !_selected_input.check(now, snapshot)) { stop(); return ManualWorkerStatus::ControlRejected; }

		return cycleImpl(now, permission, snapshot.manual, snapshot.state, binding);
	}

	ManualWorkerStatus cycle(uint64_t now, const EdfPermission &permission,
				 const research_stabilized_manual::ManualSample &manual,
				 const research_stabilized_manual::StateSample &state, const ManualOutputBinding &binding)
	{
		if (_selected_session) { stop(); return ManualWorkerStatus::ControlRejected; }

		return cycleImpl(now, permission, manual, state, binding);
	}

	void stop()
	{
		_active = false;
		_selected_session = false;
		_selected_input.stop();
		_worker.stop();

		for (unsigned bank = 0; bank < 2; ++bank) { acknowledgment(bank).revoke(); _have_ack[bank] = false; }
	}

private:
	ManualWorkerStatus cycleImpl(uint64_t now, const EdfPermission &permission,
				     const research_stabilized_manual::ManualSample &manual,
				     const research_stabilized_manual::StateSample &state, const ManualOutputBinding &binding)
	{
		if (!_active) { return ManualWorkerStatus::Inactive; }

		if (_lifetime.revision() != _revision) { return reject(); }

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

			if (!decodeManualAck(now, bank, wire, pending[count])) { return reject(); }

			memcpy(&_last_ack[bank], &wire, sizeof(wire));
			_have_ack[bank] = true;
			++count;
		}

		ManualWorkerOutput output{};
		ManualWorkerStatus result{ManualWorkerStatus::Inactive};

		for (unsigned i = 0; i < (count ? count : 1); ++i) {
			result = _worker.cycle(now, permission, manual, state, output, binding, count ? &pending[i] : nullptr);

			if (result == ManualWorkerStatus::Command) {
				// Any extra reply after a completed outstanding frame is invalid.
				if (i + 1 < count) { return reject(); }

				slaaps_manual_command_s wire{};

				if (!encodeManualCommand(now, output, wire) || !_command.publish(wire)) { return reject(); }

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
