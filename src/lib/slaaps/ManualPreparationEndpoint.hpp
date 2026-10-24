#pragma once

#include "ManualBankTransport.hpp"
#include "ManualPreparationTransport.hpp"
#include "ManualActivationTransport.hpp"
#include <drivers/drv_hrt.h>

namespace slaaps
{

// Driver-side preparation endpoint. Serialize every call with the bank and
// configuration/inventory changes. The bank must outlive this endpoint. Its
// readiness publication is distinct from the bank's handled acknowledgment.
// No message grants authority: activate still requires a trusted permission.
template<unsigned Bank>
class ManualPreparationEndpoint
{
	static_assert(Bank < 2, "Only MAIN and AUX are supported");
	static constexpr ORB_ID kReadyTopic = Bank == 0 ? ORB_ID::slaaps_manual_ready_main : ORB_ID::slaaps_manual_ready_aux;
	static constexpr ORB_ID kActivationTopic = Bank == 0 ? ORB_ID::slaaps_manual_active_main : ORB_ID::slaaps_manual_active_aux;
public:
	explicit ManualPreparationEndpoint(ManualBankTransport<Bank> &bank) : _bank(bank) {}
	~ManualPreparationEndpoint() { _bank.cancelPreparation(); }
	ManualPreparationEndpoint(const ManualPreparationEndpoint &) = delete;
	ManualPreparationEndpoint &operator=(const ManualPreparationEndpoint &) = delete;

	bool advertise()
	{
		if (_owned_revision) { return revision() != 0 && activationRevision() != 0; }

		if (!_ready.advertise()) { return false; }

		_owned_revision = _ready_lifetime.revision();

		// Both reply lifetimes must exist before the supervisor can issue its
		// first proposal. Retain the ready revision even on a partial failure;
		// retrying advertise cannot adopt a replacement or repair that endpoint.
		if (!_owned_revision || !_activation.advertise()) { return false; }

		_activation_revision = _activation_lifetime.revision();
		return _activation_revision != 0;
	}
	unsigned revision() { return _owned_revision && _ready_lifetime.revision() == _owned_revision ? _owned_revision : 0; }

	bool listen(uint64_t now, unsigned proposal_revision)
	{
		if (_bank.active()) { return false; }

		cancel(now);

		if (!now || now < _clock || !revision() || !_proposal.bind(proposal_revision)) { return false; }

		_clock = now;
		_listening = true;
		return true;
	}

	PreparationStatus poll(uint64_t now, const VehicleConfiguration &configuration, const OutputInventory &inventory,
			       bool allow_new_request = true)
	{
		return pollImpl(now, configuration, inventory, allow_new_request, false);
	}
	// Production readers advance only their local decision time after copying;
	// proposal identity, preparation expiry and retained replies remain exact.
	PreparationStatus pollLive(uint64_t &now, const VehicleConfiguration &configuration, const OutputInventory &inventory,
				   bool allow_new_request = true)
	{
		now = hrt_absolute_time();
		return pollImpl(now, configuration, inventory, allow_new_request, true);
	}
private:
	PreparationStatus pollImpl(uint64_t &now, const VehicleConfiguration &configuration, const OutputInventory &inventory,
				   bool allow_new_request, bool live)
	{
		if (_bank.active() || !_listening) { return PreparationStatus::Inactive; }

		if (!now || now < _clock || !revision()) { return reject(now); }

		_clock = now;
		auto &wire = _poll_wire;
		wire = {};
		const auto read = _proposal.read(wire);

		if (live) {
			now = hrt_absolute_time();

			if (now < _clock || !revision() || !activationRevision()) { return reject(now); }

			_clock = now;
		}

		if (read == BoundRead::Waiting && !_have_request) { return PreparationStatus::Waiting; }

		auto &request = _poll_request;
		request = {};
		uint64_t prepared{};

		if (read != BoundRead::Data || !decodeManualPreparation(now, slaaps_manual_preparation_s::REQUEST_BANK, wire, request, prepared)) {
			return reject(now);
		}

		const bool duplicate = _have_request && request.session == _request.session;

		if (duplicate && !sameManualPreparation(request, _request)) { return reject(now); }

		if (!duplicate && !allow_new_request) { return reject(now); }

		if (!duplicate) {
			if (!_bank.prepare(now, configuration, inventory, request)) { return reject(now); }

			_request = request;
			_have_request = true;
		}

		const bool current = live ? _bank.checkPreparationLive(now, configuration, inventory) :
				     _bank.checkPreparation(now, configuration, inventory);

		if (live) {
			if (now < _clock) { return reject(now); }

			_clock = now;
		}

		if (!current) { return reject(now); }

		if (live) {
			now = hrt_absolute_time();

			if (now < _clock || !revision() || !activationRevision()
			    || !ManualBankPreparation::validRequest(now, _request)) { return reject(now); }

			_clock = now;
		}

		if (!duplicate && (!encodeManualPreparation(now, request, Bank, now, wire) || !_ready.publish(wire))) { return reject(now); }

		// Retained and republished duplicates neither repeat prepare nor renew
		// readiness timestamps/deadlines. Live configuration is still checked.
		return PreparationStatus::Ready;
	}
public:
	bool activate(uint64_t now, const VehicleConfiguration &configuration, const OutputInventory &inventory,
		      const ManualOutputBinding &binding, const EdfPermission &permission, unsigned command_revision)
	{
		return activateImpl(now, configuration, inventory, binding, permission, command_revision, false);
	}
	bool activateLive(uint64_t &now, const VehicleConfiguration &configuration, const OutputInventory &inventory,
			  const ManualOutputBinding &binding, const EdfPermission &permission, unsigned command_revision)
	{
		return activateImpl(now, configuration, inventory, binding, permission, command_revision, true);
	}
private:
	bool activateImpl(uint64_t &now, const VehicleConfiguration &configuration, const OutputInventory &inventory,
			  const ManualOutputBinding &binding, const EdfPermission &permission, unsigned command_revision, bool live)
	{
		if (_bank.active()) { return false; }

		const auto prepared = live ? pollLive(now, configuration, inventory, false) : poll(now, configuration, inventory, false);

		if (prepared != PreparationStatus::Ready) { return false; }

		const bool activated = live ? _bank.beginPreparedLive(now, configuration, inventory, binding, permission, command_revision) :
				       _bank.beginPrepared(now, configuration, inventory, binding, permission, command_revision);

		if (live) {
			_clock = now;

			if (activated && !ManualBankPreparation::validRequest(now, _request)) {
				_bank.revoke();
				cancel(now);
				return false;
			}
		}

		cancel(now);
		return activated;
	}
public:
	// Bind independently from preparation. The caller supplies trusted permission;
	// transition traffic cannot arm or grant authority. Poll every driver cycle.
	bool bindTransitions(unsigned revision)
	{
		if (_bank.active() || !this->revision() || !activationRevision()) { return false; }

		_transition_seen = false;
		_activation_requested = 0;
		return _transition.bind(revision);
	}
	unsigned activationRevision() { return _activation_revision && _activation_lifetime.revision() == _activation_revision ? _activation_revision : 0; }

	PreparationStatus processTransition(uint64_t now, const VehicleConfiguration &configuration,
					    const OutputInventory &inventory, const EdfPermission &permission, bool allow_activation = true)
	{
		return processTransitionImpl(now, configuration, inventory, permission, allow_activation, false);
	}
	PreparationStatus processTransitionLive(uint64_t &now, const VehicleConfiguration &configuration,
						const OutputInventory &inventory, const EdfPermission &permission, bool allow_activation = true)
	{
		return processTransitionImpl(now, configuration, inventory, permission, allow_activation, true);
	}
private:
	PreparationStatus processTransitionImpl(uint64_t &now, const VehicleConfiguration &configuration,
						const OutputInventory &inventory, const EdfPermission &permission, bool allow_activation, bool live)
	{
		auto &wire = _transition_wire;
		wire = {};
		const auto read = _transition.read(wire);

		if (live) {
			now = hrt_absolute_time();

			if (!now || now < _transition_clock || !activationRevision()) { return rejectTransition(now); }

			_transition_clock = now;
		}

		if (read == BoundRead::Waiting) { return PreparationStatus::Waiting; }

		if (read != BoundRead::Data || !now || now < _transition_clock || !activationRevision()) { return rejectTransition(now); }

		_transition_clock = now;

		// A previous session retained on this lifetime is never an activation.
		if (!_transition_seen && !_activation_requested && _have_request && wire.session < _request.session) {
			return PreparationStatus::Waiting;
		}

		const uint8_t operation = wire.operation;
		wire.operation = slaaps_manual_preparation_s::PREPARE;
		auto &request = _transition_request;
		request = {};
		uint64_t prepared{};
		// Cancellation remains usable after the proposal deadline, but must still
		// carry the exact proposal and an original, nonfuture publication time.
		const uint64_t decode_time = operation == slaaps_manual_preparation_s::CANCEL ? wire.timestamp_sample :
					     (_transition_seen ? wire.timestamp : now);
		const uint64_t published = wire.timestamp;

		if (operation == slaaps_manual_preparation_s::CANCEL) { wire.timestamp = wire.timestamp_sample; }

		if (published > now || published < wire.timestamp_sample
		    || !decodeManualPreparation(decode_time, slaaps_manual_preparation_s::REQUEST_BANK, wire, request, prepared)
		    || !sameManualPreparation(request, _request)) { return rejectTransition(now); }

		if (operation == slaaps_manual_preparation_s::CANCEL) { return rejectTransition(now); }

		if (operation != slaaps_manual_preparation_s::ACTIVATE) { return rejectTransition(now); }

		if (_transition_seen) {
			return _bank.active() && published == _activation_requested ? PreparationStatus::Ready : rejectTransition(now);
		}

		// Permission and transition publications can interleave across queues.
		// Retain the exact deferred ACTIVATE without consuming preparation;
		// cancellation/replacement and the original expiry remain live.
		if (_activation_requested && published != _activation_requested) { return rejectTransition(now); }

		_activation_requested = published;

		if (!allow_activation) { return PreparationStatus::Waiting; }

		_transition_seen = true;

		const bool activated = live ? activateLive(now, configuration, inventory, request.binding, permission, request.command_revision) :
				       activate(now, configuration, inventory, request.binding, permission, request.command_revision);

		if (!activated) { return rejectTransition(now); }

		auto &ack = _activation_ack;
		auto &reply = _activation_reply;
		ack = {};
		reply = {};

		const bool acknowledged = live ? _bank.activationAckLive(now, permission, inventory, request.binding, ack) :
					  _bank.activationAck(now, permission, inventory, request.binding, ack);

		if (live) {
			now = hrt_absolute_time();
			_transition_clock = now;

			if (!ManualBankPreparation::validRequest(now, _request)) { return rejectTransition(now); }
		}

		if (!acknowledged || !encodeManualActivation(now, Bank, ack, reply) || !_activation.publish(reply)) { return rejectTransition(now); }

		_transition_clock = now;

		return PreparationStatus::Ready;
	}
public:
	void cancel(uint64_t now)
	{
		_bank.cancelPreparation();
		_proposal.revoke();
		_listening = false;
		_have_request = false;

		if (revision()) {
			// Explicit invalidation: a zero-identity payload never decodes as
			// readiness, so retained positive readiness cannot survive cancel.
			slaaps_manual_preparation_s invalid{};
			invalid.timestamp = now;
			invalid.bank = Bank;
			_ready.publish(invalid);
		}
	}
private:
	PreparationStatus rejectTransition(uint64_t now)
	{
		_bank.revoke();
		_transition.revoke();
		cancel(now);
		slaaps_manual_activation_s invalid{};
		invalid.timestamp = now;
		invalid.bank = Bank;
		_activation.publish(invalid);
		return PreparationStatus::Rejected;
	}
	BoundSubscription<slaaps_manual_preparation_s> _transition{ORB_ID::slaaps_manual_transition};
	uORB::Publication<slaaps_manual_activation_s> _activation{kActivationTopic};
	uORB::ResearchSubscription _activation_lifetime{kActivationTopic};
	unsigned _activation_revision{};
	uint64_t _transition_clock{}, _activation_requested{};
	bool _transition_seen{};
	PreparationStatus reject(uint64_t now) { cancel(now); return PreparationStatus::Rejected; }
	ManualBankTransport<Bank> &_bank;
	BoundSubscription<slaaps_manual_preparation_s> _proposal{ORB_ID::slaaps_manual_prepare};
	uORB::Publication<slaaps_manual_preparation_s> _ready{kReadyTopic};
	uORB::ResearchSubscription _ready_lifetime{kReadyTopic};
	ManualPreparationRequest _request{};
	// Serialized per-endpoint scratch. Activation nests poll(), so its wire
	// and decoded proposal must remain distinct from transition scratch.
	slaaps_manual_preparation_s _poll_wire{}, _transition_wire{};
	ManualPreparationRequest _poll_request{}, _transition_request{};
	ManualActivationAck _activation_ack{};
	slaaps_manual_activation_s _activation_reply{};
	uint64_t _clock{};
	unsigned _owned_revision{};
	bool _listening{}, _have_request{};
};

} // namespace slaaps
