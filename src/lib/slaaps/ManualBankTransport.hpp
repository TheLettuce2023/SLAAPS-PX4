#pragma once

#include "BoundSubscription.hpp"
#include "ManualOutputBank.hpp"
#include "ManualBankPreparation.hpp"
#include "ManualTransport.hpp"
#include <uORB/Publication.hpp>

namespace slaaps
{

// Native serialized driver-side endpoint. The driver must call dispatch even
// without new traffic, and serialize dispatch/write/confirm with permission and
// routing changes. No message grants admission. No physical write or airborne
// recovery policy is implemented here. Preserve the endpoint across sessions.
template<unsigned Bank>
class ManualBankTransport
{
	static_assert(Bank < 2, "Only MAIN and AUX are supported");
	static constexpr ORB_ID kAckTopic = Bank == 0 ? ORB_ID::slaaps_manual_ack_main : ORB_ID::slaaps_manual_ack_aux;
public:
	ManualBankTransport() = default;
	ManualBankTransport(const ManualBankTransport &) = delete;
	ManualBankTransport &operator=(const ManualBankTransport &) = delete;

	// Establish reply advertisement before supervisor session admission.
	bool advertise()
	{
		if (_owned_ack_revision) { return acknowledgmentRevision() != 0; }

		if (!_ack.advertise()) { return false; }

		_owned_ack_revision = _ack_lifetime.revision();
		return _owned_ack_revision != 0;
	}
	unsigned acknowledgmentRevision()
	{
		return _owned_ack_revision && _ack_lifetime.revision() == _owned_ack_revision ? _owned_ack_revision : 0;
	}

	bool prepare(uint64_t now, const VehicleConfiguration &configuration, const OutputInventory &inventory,
		     const ManualPreparationRequest &request)
	{
		if (_consumer.active()) { return false; } // Never disturb an active flight with a preparation request.

		if (!_preparation.prepare(now, Bank, configuration, inventory, request)
		    || !acknowledgmentRevision() || request.acknowledgment_revisions[Bank] != acknowledgmentRevision()
		    || !_command.bind(request.command_revision)) {
			revoke();
			return false;
		}

		return true;
	}

	bool checkPreparation(uint64_t now, const VehicleConfiguration &configuration, const OutputInventory &inventory)
	{
		if (_consumer.active()) { return false; }

		slaaps_manual_command_s ignored{};

		if (!_preparation.check(now, configuration, inventory) || !acknowledgmentRevision()
		    || _command.read(ignored) == BoundRead::Rejected) {
			_preparation.cancel();
			return false;
		}

		return true;
	}
	void cancelPreparation() { _preparation.cancel(); }
	bool active() const { return _consumer.active(); }

	bool beginPrepared(uint64_t now, const VehicleConfiguration &configuration, const OutputInventory &inventory,
			   const ManualOutputBinding &binding, const EdfPermission &permission, unsigned command_revision)
	{
		if (_consumer.active()) { return false; }

		if (!_preparation.consume(now, configuration, inventory, binding, permission, command_revision, acknowledgmentRevision())) {
			revoke();
			return false;
		}

		return begin(now, configuration, inventory, binding, permission, command_revision);
	}

	bool begin(uint64_t now, const VehicleConfiguration &configuration, const OutputInventory &inventory,
		   const ManualOutputBinding &binding, const EdfPermission &permission, unsigned command_revision)
	{
		_preparation.cancel();
		_received = false;
		_have_baseline = false;
		_ack_revision = acknowledgmentRevision();
		const auto result = _consumer.begin(now, Bank, configuration, inventory, binding, permission);

		if (result != ManualBankFault::None || !_ack.advertised() || !_ack_revision || !_command.bind(command_revision)) {
			revoke();
			return false;
		}

		_command_revision = command_revision;

		slaaps_manual_command_s wire{};
		const auto read = _command.read(wire);
		ManualWorkerOutput previous{};

		if (read == BoundRead::Rejected || (read == BoundRead::Data
						    && (wire.timestamp > now || wire.session >= permission.session
							|| !decodeManualCommand(wire.timestamp, wire, previous)))) {
			revoke();
			return false;
		}

		if (read == BoundRead::Data) {
			// Retained prior-session traffic is a baseline, never a command for
			// this grant. Validate at publication time, even if it is now expired.
			memcpy(&_last, &wire, sizeof(wire));
			_have_baseline = true;
		}

		return true;
	}

	ManualBankOutput dispatch(uint64_t now, const EdfPermission &permission, const OutputInventory &inventory,
				  const ManualOutputBinding &binding)
	{
		// Poll before reading, including initial silence and duplicate traffic.
		auto output = _consumer.sample(now, permission, inventory, binding);

		if (_consumer.fault() != ManualBankFault::None) { return output; }

		if (_ack_lifetime.revision() != _ack_revision) {
			revoke();
			return _consumer.sample(now, permission, inventory, binding);
		}

		slaaps_manual_command_s wire{};
		const auto result = _command.read(wire);

		if (!_received && (result == BoundRead::Waiting
				   || (result == BoundRead::Data && _have_baseline && memcmp(&wire, &_last, sizeof(wire)) == 0))) { return output; }

		ManualWorkerOutput frame{};

		if (result != BoundRead::Data || !decodeManualCommand(now, wire, frame)) {
			revoke();

		} else if (_received && wire.sequence == _last.sequence) {
			// An unchanged latest sample is not a second accept and cannot renew
			// its lease. Mutating even publication time requires a new sequence.
			if (memcmp(&wire, &_last, sizeof(wire)) != 0) { revoke(); }

		} else if (_consumer.accept(now, permission, inventory, binding, frame) == ManualBankFault::None) {
			memcpy(&_last, &wire, sizeof(wire));
			_received = true;
		}

		return _consumer.sample(now, permission, inventory, binding);
	}

	bool confirmHandled(uint64_t now, const EdfPermission &permission, const OutputInventory &inventory,
			    const ManualOutputBinding &binding, const ManualBankOutput &handled, bool success)
	{
		// Recheck transport lifetime and any intervening traffic before releasing
		// the reply. The core checks the exact ticket and current permission.
		dispatch(now, permission, inventory, binding);
		ManualOutputAck ack{};
		slaaps_manual_ack_s wire{};

		if (_consumer.confirmHandled(now, permission, inventory, binding, handled, success, ack) != ManualBankFault::None
		    || !encodeManualAck(now, Bank, ack, wire) || !_ack.publish(wire)) {
			revoke();
			return false;
		}

		return true;
	}

	bool activationAck(uint64_t now, const EdfPermission &permission, const OutputInventory &inventory,
			   const ManualOutputBinding &binding, ManualActivationAck &ack)
	{
		dispatch(now, permission, inventory, binding);
		ManualActivationAck candidate{};

		if (!_consumer.activationAck(now, permission, inventory, binding, candidate)) { return false; }

		candidate.command_revision = _command_revision;
		candidate.acknowledgment_revision = _ack_revision;
		ack = candidate;
		return true;
	}

	void revoke() { _preparation.cancel(); _command.revoke(); _consumer.revoke(); }
	bool awaitingHandling() const { return _consumer.awaitingHandling(); }
	ManualBankFault fault() const { return _consumer.fault(); }

private:
	ManualOutputBank _consumer;
	ManualBankPreparation _preparation{};
	unsigned _owned_ack_revision{};
	BoundSubscription<slaaps_manual_command_s> _command{ORB_ID::slaaps_manual_command};
	uORB::Publication<slaaps_manual_ack_s> _ack{kAckTopic};
	uORB::ResearchSubscription _ack_lifetime{kAckTopic};
	unsigned _ack_revision{};
	unsigned _command_revision{};
	slaaps_manual_command_s _last{};
	bool _received{};
	bool _have_baseline{};
};

} // namespace slaaps
