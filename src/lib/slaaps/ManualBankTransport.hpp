#pragma once

#include "BoundSubscription.hpp"
#include "ManualOutputBank.hpp"
#include "ManualBankPreparation.hpp"
#include "ManualTransport.hpp"
#include <drivers/drv_hrt.h>
#include <uORB/Publication.hpp>

namespace slaaps
{

// Native serialized driver-side endpoint. The driver must call dispatch even
// without new traffic, and serialize dispatch/write/confirm with permission and
// routing changes. Before prepare/beginPrepared, compare binding.driver[Bank]
// with the physical driver's own inventory token; never derive that token from
// the request. This transport cannot independently observe physical identity.
// No message grants admission. No physical write or airborne
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
		return checkPreparationImpl(now, configuration, inventory, false);
	}
	bool checkPreparationLive(uint64_t &now, const VehicleConfiguration &configuration, const OutputInventory &inventory)
	{
		now = hrt_absolute_time();
		const bool result = checkPreparationImpl(now, configuration, inventory, true);
		now = hrt_absolute_time();
		return result;
	}
private:
	bool checkPreparationImpl(uint64_t &now, const VehicleConfiguration &configuration, const OutputInventory &inventory,
				  bool live)
	{
		if (_consumer.active()) { return false; }

		_read_wire = {};

		if (!_preparation.check(now, configuration, inventory) || !acknowledgmentRevision()
		    || _command.read(_read_wire) == BoundRead::Rejected) {
			_preparation.cancel();
			return false;
		}

		if (live) {
			now = hrt_absolute_time();

			if (!_preparation.check(now, configuration, inventory) || !acknowledgmentRevision()) {
				_preparation.cancel();
				return false;
			}
		}

		return true;
	}
public:
	void cancelPreparation() { _preparation.cancel(); }
	bool active() const { return _consumer.active(); }

	bool beginPrepared(uint64_t now, const VehicleConfiguration &configuration, const OutputInventory &inventory,
			   const ManualOutputBinding &binding, const EdfPermission &permission, unsigned command_revision)
	{
		return beginPreparedImpl(now, configuration, inventory, binding, permission, command_revision, false);
	}
	// Initialization consumes the original preparation/session before copying
	// the command baseline. Success is not current output permission: the caller
	// must enforce the original preparation deadline and use activationAckLive
	// at the propagated time before publishing activation or handling output.
	bool beginPreparedLive(uint64_t &now, const VehicleConfiguration &configuration, const OutputInventory &inventory,
			       const ManualOutputBinding &binding, const EdfPermission &permission, unsigned command_revision)
	{
		now = hrt_absolute_time();
		const bool result = beginPreparedImpl(now, configuration, inventory, binding, permission, command_revision, true);
		now = hrt_absolute_time();
		return result;
	}
private:
	bool beginPreparedImpl(uint64_t &now, const VehicleConfiguration &configuration, const OutputInventory &inventory,
			       const ManualOutputBinding &binding, const EdfPermission &permission, unsigned command_revision, bool live)
	{
		if (_consumer.active()) { return false; }

		if (!_preparation.consume(now, configuration, inventory, binding, permission, command_revision, acknowledgmentRevision())) {
			revoke();
			return false;
		}

		return beginImpl(now, configuration, inventory, binding, permission, command_revision, live);
	}

public:
	bool begin(uint64_t now, const VehicleConfiguration &configuration, const OutputInventory &inventory,
		   const ManualOutputBinding &binding, const EdfPermission &permission, unsigned command_revision)
	{
		return beginImpl(now, configuration, inventory, binding, permission, command_revision, false);
	}
	// Initialization only, as for beginPreparedLive. activationAckLive performs
	// the final core permission/expiry check after this baseline observation.
	bool beginLive(uint64_t &now, const VehicleConfiguration &configuration, const OutputInventory &inventory,
		       const ManualOutputBinding &binding, const EdfPermission &permission, unsigned command_revision)
	{
		now = hrt_absolute_time();
		const bool result = beginImpl(now, configuration, inventory, binding, permission, command_revision, true);
		now = hrt_absolute_time();
		return result;
	}
private:
	bool beginImpl(uint64_t &now, const VehicleConfiguration &configuration, const OutputInventory &inventory,
		       const ManualOutputBinding &binding, const EdfPermission &permission, unsigned command_revision, bool live)
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

		auto &wire = _read_wire;
		wire = {};
		const auto read = _command.read(wire);

		if (live) { now = hrt_absolute_time(); }

		auto &previous = _decoded_frame;
		previous = {};

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

public:
	ManualBankOutput dispatch(uint64_t now, const EdfPermission &permission, const OutputInventory &inventory,
				  const ManualOutputBinding &binding)
	{
		return dispatchImpl(now, permission, inventory, binding, false);
	}
	ManualBankOutput dispatchLive(uint64_t &now, const EdfPermission &permission, const OutputInventory &inventory,
				      const ManualOutputBinding &binding)
	{
		now = hrt_absolute_time();
		return dispatchImpl(now, permission, inventory, binding, true);
	}
	// Actual nav mode may cross before its new permission publication. Observe
	// lifetime/expiry but neither consume a command nor create a write ticket.
	ManualBankOutput waitTransferLive(uint64_t &now, const EdfPermission &permission, const OutputInventory &inventory,
					  const ManualOutputBinding &binding)
	{
		now = hrt_absolute_time();
		return dispatchImpl(now, permission, inventory, binding, true, false);
	}
private:
	ManualBankOutput dispatchImpl(uint64_t &now, const EdfPermission &permission, const OutputInventory &inventory,
				      const ManualOutputBinding &binding, bool live, bool allow_command = true)
	{
		// Poll before reading, including initial silence and duplicate traffic.
		auto output = _consumer.sample(now, permission, inventory, binding);

		if (_consumer.recovering()) { return recoverImpl(now, permission, inventory, binding, live, allow_command, output); }

		if (_consumer.fault() != ManualBankFault::None) { return output; }

		if (_ack_lifetime.revision() != _ack_revision) {
			revoke();
			return _consumer.sample(now, permission, inventory, binding);
		}

		auto &wire = _read_wire;
		wire = {};
		const auto result = _command.read(wire);

		if (live) {
			now = hrt_absolute_time();
			output = _consumer.sample(now, permission, inventory, binding);

			if (_consumer.fault() != ManualBankFault::None) { return output; }
		}

		if (!allow_command) {
			if (result == BoundRead::Rejected || (_consumer.awaitingHandling()
							      && (result != BoundRead::Data || memcmp(&wire, &_last, sizeof(wire)) != 0))) { revoke(); }

			output.command_valid = _consumer.awaitingHandling() && _consumer.fault() == ManualBankFault::None;
			return output;
		}

		if (!_received && (result == BoundRead::Waiting
				   || (result == BoundRead::Data && _have_baseline && memcmp(&wire, &_last, sizeof(wire)) == 0))) { return output; }

		auto &frame = _decoded_frame;
		frame = {};

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

		if (live) { now = hrt_absolute_time(); }

		return _consumer.sample(now, permission, inventory, binding);
	}

	// D3/D4: after the original session is lost the canards hold and only recovery frames are read.
	// Retained traffic identical to the last accepted frame is ignored and cannot renew anything.
	ManualBankOutput recoverImpl(uint64_t &now, const EdfPermission &permission, const OutputInventory &inventory,
				     const ManualOutputBinding &binding, bool live, bool allow_command, const ManualBankOutput &current)
	{
		if (_ack_lifetime.revision() != _ack_revision) {
			revoke();
			return _consumer.sample(now, permission, inventory, binding);
		}

		if (!allow_command) { return current; }

		auto &wire = _read_wire;
		wire = {};
		const auto result = _command.read(wire);

		if (live) { now = hrt_absolute_time(); }

		if (result == BoundRead::Waiting) { return current; }

		auto &frame = _decoded_frame;
		frame = {};

		if (result != BoundRead::Data) {
			revoke();

		} else if (wire.sequence == _last.sequence) {
			if (memcmp(&wire, &_last, sizeof(wire)) != 0) { revoke(); }

		} else if (!decodeManualCommand(now, wire, frame)) {
			revoke();

		} else if (_consumer.acceptRecovery(now, permission, inventory, binding, frame) == ManualBankFault::None) {
			memcpy(&_last, &wire, sizeof(wire));
		}

		if (live) { now = hrt_absolute_time(); }

		return _consumer.sample(now, permission, inventory, binding);
	}

public:
	bool confirmHandled(uint64_t now, const EdfPermission &permission, const OutputInventory &inventory,
			    const ManualOutputBinding &binding, const ManualBankOutput &handled, bool success)
	{
		return confirmHandledImpl(now, permission, inventory, binding, handled, success, false);
	}
	bool confirmHandledLive(uint64_t &now, const EdfPermission &permission, const OutputInventory &inventory,
				const ManualOutputBinding &binding, const ManualBankOutput &handled, bool success)
	{
		now = hrt_absolute_time();
		const bool result = confirmHandledImpl(now, permission, inventory, binding, handled, success, true);
		now = hrt_absolute_time();
		return result;
	}
private:
	bool confirmHandledImpl(uint64_t &now, const EdfPermission &permission, const OutputInventory &inventory,
				const ManualOutputBinding &binding, const ManualBankOutput &handled, bool success, bool live)
	{
		// Recheck transport lifetime and any intervening traffic before releasing
		// the reply. The core checks the exact ticket and current permission.
		dispatchImpl(now, permission, inventory, binding, live);

		if (live) { now = hrt_absolute_time(); }

		ManualOutputAck ack{};
		slaaps_manual_ack_s wire{};

		if (_consumer.confirmHandled(now, permission, inventory, binding, handled, success, ack) != ManualBankFault::None
		    || !encodeManualAck(now, Bank, ack, wire) || !_ack.publish(wire)) {
			revoke();
			return false;
		}

		return true;
	}

public:
	bool activationAck(uint64_t now, const EdfPermission &permission, const OutputInventory &inventory,
			   const ManualOutputBinding &binding, ManualActivationAck &ack)
	{
		return activationAckImpl(now, permission, inventory, binding, ack, false);
	}
	bool activationAckLive(uint64_t &now, const EdfPermission &permission, const OutputInventory &inventory,
			       const ManualOutputBinding &binding, ManualActivationAck &ack)
	{
		now = hrt_absolute_time();
		const bool result = activationAckImpl(now, permission, inventory, binding, ack, true);
		now = hrt_absolute_time();
		return result;
	}
private:
	bool activationAckImpl(uint64_t &now, const EdfPermission &permission, const OutputInventory &inventory,
			       const ManualOutputBinding &binding, ManualActivationAck &ack, bool live)
	{
		dispatchImpl(now, permission, inventory, binding, live);

		if (live) { now = hrt_absolute_time(); }

		ManualActivationAck candidate{};

		if (!_consumer.activationAck(now, permission, inventory, binding, candidate)) { return false; }

		candidate.command_revision = _command_revision;
		candidate.acknowledgment_revision = _ack_revision;
		ack = candidate;
		return true;
	}

public:
	void revoke() { _preparation.cancel(); _command.revoke(); _consumer.revoke(); }
	bool awaitingHandling() const { return _consumer.awaitingHandling(); }
	bool recovering() const { return _consumer.recovering(); }
	bool canCompleteAtTransition(uint64_t transition_us) const { return _consumer.canCompleteAtTransition(transition_us); }
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
	// Serialized read/decode scratch is dead between calls. Keep it separate
	// from the retained baseline and the consumer's pending handling ticket.
	slaaps_manual_command_s _read_wire{};
	ManualWorkerOutput _decoded_frame{};
	slaaps_manual_command_s _last{};
	bool _received{};
	bool _have_baseline{};
};

} // namespace slaaps
