#pragma once

#include "BoundSubscription.hpp"
#include "ManualPreparationTransport.hpp"
#include "ObservedRoutingBinding.hpp"
#include "ManualActivationBarrier.hpp"
#include "ManualActivationTransport.hpp"
#include <drivers/drv_hrt.h>
#include <uORB/Publication.hpp>
#include <cstring>

namespace slaaps
{

// Non-actuating supervisor transaction. Ready means all required banks echoed
// this proposal; it is not flight permission or confirmation of activation.
// Poll throughout preparation, including after Ready. Activation consumes the
// ready replies and independently monitors routing and publisher identities.
// Drivers must recheck trusted permission/routing at the final write.
class ManualPreparationCoordinator
{
public:
	bool advertise()
	{
		if (_owned_revision) { return revision() != 0; }

		if (!_proposal.advertise()) { return false; }

		_owned_revision = _proposal_lifetime.revision();
		return _owned_revision != 0;
	}
	unsigned revision() { return _owned_revision && _proposal_lifetime.revision() == _owned_revision ? _owned_revision : 0; }

	bool begin(uint64_t now, const VehicleConfiguration &configuration, const OutputInventory &inventory,
		   const ManualPreparationRequest &request, const unsigned(&ready_revisions)[2])
	{
		return beginImpl(now, configuration, inventory, request, ready_revisions, nullptr, InventoryFault::MissingBank, {});
	}
	bool beginObserved(uint64_t now, const VehicleConfiguration &configuration, const ReportedRoutingSnapshot &snapshot,
			   InventoryFault capture_result, const ReportedRoutingPolicy &policy,
			   const ManualPreparationRequest &request, const unsigned(&ready_revisions)[2])
	{
		return beginImpl(now, configuration, snapshot.inventory, request, ready_revisions, &snapshot, capture_result, policy);
	}
	PreparationStatus poll(uint64_t now, const VehicleConfiguration &configuration, const OutputInventory &inventory)
	{
		if (_pending && _observed_mode) { return reject(now); }

		return pollImpl(now, configuration, inventory);
	}
	PreparationStatus pollObserved(uint64_t now, const VehicleConfiguration &configuration, const ReportedRoutingSnapshot &snapshot,
				       InventoryFault capture_result)
	{
		if (!_pending) { return PreparationStatus::Inactive; }

		if (!_observed_mode || !_observed.check(now, configuration, snapshot, capture_result)) { return reject(now); }

		return pollImpl(now, configuration, snapshot.inventory);
	}
	// Live uORB readers return the actual post-copy decision time. Fixed-time
	// APIs above remain strict; no wire timestamp or original deadline changes.
	PreparationStatus pollObservedLive(uint64_t &now, const VehicleConfiguration &configuration,
					   const ReportedRoutingSnapshot &snapshot, InventoryFault capture_result)
	{
		now = hrt_absolute_time();

		if (!_pending) { return PreparationStatus::Inactive; }

		if (!_observed_mode || !_observed.check(now, configuration, snapshot, capture_result)) { return reject(now); }

		const auto status = pollImpl(now, configuration, snapshot.inventory, true);

		if (status == PreparationStatus::Rejected) { return status; }

		now = hrt_absolute_time();

		if (now < _clock) { return reject(now); }

		_clock = now;

		if (!revision() || !lifetimesMatch() || !ManualBankPreparation::validRequest(now, _request)
		    || !_observed.check(now, configuration, snapshot, capture_result)) { return reject(now); }

		return status;
	}
private:
	bool beginImpl(uint64_t now, const VehicleConfiguration &configuration, const OutputInventory &inventory,
		       const ManualPreparationRequest &request, const unsigned(&ready_revisions)[2], const ReportedRoutingSnapshot *snapshot,
		       InventoryFault capture_result, const ReportedRoutingPolicy &policy)
	{
		if (_activation_pending) { return false; }

		if (_pending) { cancel(now); }

		_main_ready.revoke();
		_aux_ready.revoke();
		_observed.cancel();
		_observed_mode = false;

		if (!now || now < _clock || !request.session || request.session <= _last_session || request.session > now) { return false; }

		_clock = now;
		_last_session = request.session; // Invalid transactions also consume their token.

		if (!revision() || !ManualBankPreparation::validRequest(now, request)
		    || !ManualBankPreparation::validConfiguration(configuration, request.mode)
		    || fingerprint(configuration) != request.configuration
		    || !makeRoutingPlan(configuration, inventory, ManualBankPreparation::requiresEdf(request.mode), _routing).valid()
		    || _routing.bank_mask != request.binding.banks) { return false; }

		if (snapshot && !_observed.begin(now, configuration, *snapshot, capture_result, request.binding, policy, request.mode)) { return false; }

		_request = request;

		if (!lifetimesMatch()) { return false; }

		for (unsigned bank = 0; bank < 2; ++bank) {
			_have_baseline[bank] = _seen[bank] = false;
			const bool participating = request.binding.banks & (1U << bank);

			if (participating != (ready_revisions[bank] != 0)) { return false; }

			if (!participating) { continue; }

			if (!ready(bank).bind(ready_revisions[bank])) { return false; }

			const auto read = ready(bank).read(_baseline[bank]);

			if (read == BoundRead::Rejected) { return false; }

			_have_baseline[bank] = read == BoundRead::Data;
		}

		slaaps_manual_preparation_s wire{};

		if (!encodeManualPreparation(now, request, slaaps_manual_preparation_s::REQUEST_BANK, 0, wire)
		    || !_proposal.publish(wire)) { return false; }

		_pending = true;
		_observed_mode = snapshot != nullptr;
		return true;
	}

	bool initialUnready(uint64_t now, unsigned bank, const slaaps_manual_preparation_s &wire) const
	{
		// listen() explicitly clears readiness before preparing the proposal.
		// Only its exact zero-identity shape, from the already-bound publisher
		// within this request's interval, may wait for the first positive reply.
		return wire.timestamp >= _request.timestamp_us && wire.timestamp <= now && wire.bank == bank
		       && wire.operation == slaaps_manual_preparation_s::PREPARE && !wire.timestamp_sample && !wire.deadline
		       && !wire.session && !wire.source && !wire.configuration && !wire.mode && !wire.routing
		       && !wire.driver[0] && !wire.driver[1] && !wire.prepared && !wire.command_revision && !wire.permission_revision
		       && !wire.acknowledgment_revisions[0] && !wire.acknowledgment_revisions[1] && !wire.bank_mask;
	}

	PreparationStatus pollImpl(uint64_t &now, const VehicleConfiguration &configuration, const OutputInventory &inventory,
				   bool live = false)
	{
		if (!_pending) { return PreparationStatus::Inactive; }

		RoutingPlan routing{};

		if (!now || now < _clock || !revision() || !lifetimesMatch()
		    || !ManualBankPreparation::validRequest(now, _request) || fingerprint(configuration) != _request.configuration
		    || !makeRoutingPlan(configuration, inventory, ManualBankPreparation::requiresEdf(_request.mode), routing).valid()
		    || !sameRoutingPlan(routing, _routing)) {
			return reject(now);
		}

		_clock = now;
		bool complete = true;

		for (unsigned bank = 0; bank < 2; ++bank) {
			if (!(_request.binding.banks & (1U << bank))) { continue; }

			slaaps_manual_preparation_s wire{};
			const auto read = ready(bank).read(wire);

			if (live) {
				now = hrt_absolute_time();

				if (now < _clock) { return reject(now); }

				_clock = now;
			}

			if (read == BoundRead::Rejected) { return reject(now); }

			// A baseline or canonical initialization tombstone grants no readiness.
			// After a positive reply, any revocation remains an immediate fault.
			if (!_seen[bank] && (read == BoundRead::Waiting
					     || (_have_baseline[bank] && memcmp(&wire, &_baseline[bank], sizeof(wire)) == 0)
					     || (read == BoundRead::Data && initialUnready(now, bank, wire)))) {
				complete = false;
				continue;
			}

			ManualPreparationRequest decoded{};
			uint64_t prepared{};

			if (read != BoundRead::Data || !decodeManualPreparation(now, bank, wire, decoded, prepared)
			    || !sameManualPreparation(decoded, _request)
			    || (_seen[bank] && (prepared != _prepared[bank] || wire.timestamp != _published[bank]))) { return reject(now); }

			_seen[bank] = true;
			_prepared[bank] = prepared;
			_published[bank] = wire.timestamp;
		}

		return complete ? PreparationStatus::Ready : PreparationStatus::Waiting;
	}

public:
	bool advertiseTransitions()
	{
		if (_transition_revision) { return transitionRevision() != 0; }

		if (!_transition.advertise()) { return false; }

		_transition_revision = _transition_lifetime.revision();
		return _transition_revision != 0;
	}
	unsigned transitionRevision() { return _transition_revision && _transition_lifetime.revision() == _transition_revision ? _transition_revision : 0; }

	bool beginActivation(uint64_t now, const VehicleConfiguration &configuration, const OutputInventory &inventory,
			     const unsigned(&revisions)[2], uint64_t deadline, uint64_t budget)
	{
		return startActivation(now, configuration, revisions, deadline, budget, poll(now, configuration, inventory));
	}
	bool beginActivationObserved(uint64_t now, const VehicleConfiguration &configuration, const ReportedRoutingSnapshot &snapshot,
				     InventoryFault result, const unsigned(&revisions)[2], uint64_t deadline, uint64_t budget)
	{
		return startActivation(now, configuration, revisions, deadline, budget, pollObserved(now, configuration, snapshot, result));
	}
	bool beginActivationObservedLive(uint64_t &now, const VehicleConfiguration &configuration,
					 const ReportedRoutingSnapshot &snapshot, InventoryFault result,
					 const unsigned(&revisions)[2], uint64_t deadline, uint64_t budget)
	{
		const auto prepared = pollObservedLive(now, configuration, snapshot, result);
		return startActivation(now, configuration, revisions, deadline, budget, prepared, &snapshot, result);
	}
	ActivationStatus pollActivation(uint64_t now, const VehicleConfiguration &configuration, const OutputInventory &inventory)
	{
		if (_observed_mode) { return rejectActivation(now); }

		return pollActivationImpl(now, configuration, inventory);
	}
	ActivationStatus pollActivationObserved(uint64_t now, const VehicleConfiguration &configuration,
						const ReportedRoutingSnapshot &snapshot, InventoryFault result)
	{
		if (!_observed_mode || !_observed.check(now, configuration, snapshot, result)) { return rejectActivation(now); }

		return pollActivationImpl(now, configuration, snapshot.inventory);
	}
	ActivationStatus pollActivationObservedLive(uint64_t &now, const VehicleConfiguration &configuration,
			const ReportedRoutingSnapshot &snapshot, InventoryFault result)
	{
		now = hrt_absolute_time();

		if (!_observed_mode || !_observed.check(now, configuration, snapshot, result)) { return rejectActivation(now); }

		const auto status = pollActivationImpl(now, configuration, snapshot.inventory, true);

		if (status == ActivationStatus::Rejected || status == ActivationStatus::Inactive) { return status; }

		now = hrt_absolute_time();

		if (now < _clock) { return rejectActivation(now); }

		_clock = now;

		if (!transitionRevision() || !revision() || !lifetimesMatch()
		    || !_observed.check(now, configuration, snapshot, result)) { return rejectActivation(now); }

		const auto final_status = _activation.status(now);
		return final_status == ActivationStatus::Rejected ? rejectActivation(now) : final_status;
	}
	bool consumeActivation(uint64_t now, uint64_t &deadline) { return _activation.consume(now, deadline); }
	void cancelActivation(uint64_t now)
	{
		_activation.cancel();
		_activation_pending = false;
		// Exact transaction identity is retained even after preparation expires.
		slaaps_manual_preparation_s wire{};

		if (transitionRevision() && encodeManualPreparation(_request.timestamp_us, _request,
				slaaps_manual_preparation_s::REQUEST_BANK, 0, wire)) {
			wire.timestamp = now;
			wire.operation = slaaps_manual_preparation_s::CANCEL;
			_transition.publish(wire);
		}

		cancel(now);
	}
private:
	bool startActivation(uint64_t &now, const VehicleConfiguration &configuration, const unsigned(&revisions)[2],
			     uint64_t deadline, uint64_t budget, PreparationStatus prepared,
			     const ReportedRoutingSnapshot *live_snapshot = nullptr, InventoryFault result = InventoryFault::MissingBank)
	{
		if (_activation_pending || prepared != PreparationStatus::Ready || !transitionRevision()
		    || !_activation.begin(now, configuration, _request, deadline, budget)) { cancelActivation(now); return false; }

		for (unsigned bank = 0; bank < 2; ++bank) {
			if (bool(_request.binding.banks & (1U << bank)) != bool(revisions[bank])) { cancelActivation(now); return false; }

			if (!revisions[bank]) { continue; }

			if (!activationReply(bank).bind(revisions[bank])) { cancelActivation(now); return false; }

			const auto read = activationReply(bank).read(_activation_baseline[bank]);

			if (live_snapshot) {
				now = hrt_absolute_time();

				if (now < _clock) { cancelActivation(now); return false; }

				_clock = now;
			}

			if (read == BoundRead::Rejected) { cancelActivation(now); return false; }

			_activation_have_baseline[bank] = read == BoundRead::Data;
			_activation_seen[bank] = false;
		}

		if (live_snapshot) {
			now = hrt_absolute_time();

			if (!transitionRevision() || !revision() || !lifetimesMatch()
			    || !_observed.check(now, configuration, *live_snapshot, result)
			    || _activation.status(now) != ActivationStatus::Waiting) { cancelActivation(now); return false; }

			_clock = now;
		}

		slaaps_manual_preparation_s wire{};

		if (!encodeManualPreparation(now, _request, slaaps_manual_preparation_s::REQUEST_BANK, 0, wire)) { cancelActivation(now); return false; }

		wire.operation = slaaps_manual_preparation_s::ACTIVATE;

		if (!_transition.publish(wire)) { cancelActivation(now); return false; }

		// Readiness is consumed. Activated peers invalidate their ready replies;
		// continue independent identity/routing checks instead of polling them.
		_pending = false;
		_activation_pending = true;
		return true;
	}
	ActivationStatus pollActivationImpl(uint64_t &now, const VehicleConfiguration &configuration, const OutputInventory &inventory,
					    bool live = false)
	{
		if (!_activation_pending) { return ActivationStatus::Inactive; }

		// Activation replies describe initial-command waiting, not an ongoing
		// output lease. Keep cancellation ownership after consumption; live
		// health and output expiry remain the worker/bank dispatch obligation.
		const auto status = _activation.status(now);

		if (status == ActivationStatus::Consumed && !live) { return status; }

		RoutingPlan routing{};

		if (!transitionRevision() || !revision() || !lifetimesMatch() || fingerprint(configuration) != _request.configuration
		    || !makeRoutingPlan(configuration, inventory, ManualBankPreparation::requiresEdf(_request.mode), routing).valid()
		    || !sameRoutingPlan(routing, _routing)
		    || status == ActivationStatus::Rejected) { return rejectActivation(now); }

		if (status == ActivationStatus::Consumed) { return status; }

		for (unsigned bank = 0; bank < 2; ++bank) {
			if (!(_request.binding.banks & (1U << bank))) { continue; }

			slaaps_manual_activation_s wire{};
			const auto read = activationReply(bank).read(wire);

			if (live) {
				now = hrt_absolute_time();

				if (now < _clock) { return rejectActivation(now); }

				_clock = now;
			}

			if (read == BoundRead::Rejected) { return rejectActivation(now); }

			if (!_activation_seen[bank] && (read == BoundRead::Waiting || (_activation_have_baseline[bank]
							&& memcmp(&wire, &_activation_baseline[bank], sizeof(wire)) == 0))) { continue; }

			ManualActivationAck ack{};

			if (read != BoundRead::Data || !decodeManualActivation(now, bank, wire, ack)
			    || _activation.accept(now, bank, ack) == ActivationStatus::Rejected) { return rejectActivation(now); }

			_activation_seen[bank] = true;
		}

		return _activation.status(now);
	}
	ActivationStatus rejectActivation(uint64_t now) { cancelActivation(now); return ActivationStatus::Rejected; }
	BoundSubscription<slaaps_manual_activation_s> &activationReply(unsigned bank) { return bank == 0 ? _main_activation : _aux_activation; }
	uORB::Publication<slaaps_manual_preparation_s> _transition{ORB_ID::slaaps_manual_transition};
	uORB::ResearchSubscription _transition_lifetime{ORB_ID::slaaps_manual_transition};
	BoundSubscription<slaaps_manual_activation_s> _main_activation{ORB_ID::slaaps_manual_active_main};
	BoundSubscription<slaaps_manual_activation_s> _aux_activation{ORB_ID::slaaps_manual_active_aux};
	ManualActivationBarrier _activation{};
	slaaps_manual_activation_s _activation_baseline[2] {};
	unsigned _transition_revision{};
	bool _activation_pending{}, _activation_have_baseline[2] {}, _activation_seen[2] {};
public:

	void cancel(uint64_t now)
	{
		if (_activation_pending) { cancelActivation(now); return; }

		_pending = false;
		_observed_mode = false;
		_observed.cancel();
		_main_ready.revoke();
		_aux_ready.revoke();

		if (revision()) {
			slaaps_manual_preparation_s invalid{};
			invalid.timestamp = now;
			invalid.bank = slaaps_manual_preparation_s::REQUEST_BANK;
			_proposal.publish(invalid);
		}
	}
private:
	PreparationStatus reject(uint64_t now) { cancel(now); return PreparationStatus::Rejected; }
	BoundSubscription<slaaps_manual_preparation_s> &ready(unsigned bank) { return bank == 0 ? _main_ready : _aux_ready; }
	bool lifetimesMatch()
	{
		return _command_lifetime.revision() == _request.command_revision
		       && _permission_lifetime.revision() == _request.permission_revision
		       && (!(_request.binding.banks & 1U) || _main_ack_lifetime.revision() == _request.acknowledgment_revisions[0])
		       && (!(_request.binding.banks & 2U) || _aux_ack_lifetime.revision() == _request.acknowledgment_revisions[1]);
	}
	uORB::Publication<slaaps_manual_preparation_s> _proposal{ORB_ID::slaaps_manual_prepare};
	uORB::ResearchSubscription _proposal_lifetime{ORB_ID::slaaps_manual_prepare};
	uORB::ResearchSubscription _command_lifetime{ORB_ID::slaaps_manual_command};
	uORB::ResearchSubscription _permission_lifetime{ORB_ID::slaaps_manual_permission};
	uORB::ResearchSubscription _main_ack_lifetime{ORB_ID::slaaps_manual_ack_main};
	uORB::ResearchSubscription _aux_ack_lifetime{ORB_ID::slaaps_manual_ack_aux};
	BoundSubscription<slaaps_manual_preparation_s> _main_ready{ORB_ID::slaaps_manual_ready_main};
	BoundSubscription<slaaps_manual_preparation_s> _aux_ready{ORB_ID::slaaps_manual_ready_aux};
	ObservedRoutingBinding _observed{};
	bool _observed_mode{};
	ManualPreparationRequest _request{};
	RoutingPlan _routing{};
	slaaps_manual_preparation_s _baseline[2] {};
	uint64_t _clock{}, _last_session{}, _prepared[2] {}, _published[2] {};
	unsigned _owned_revision{};
	bool _pending{}, _have_baseline[2] {}, _seen[2] {};
};

} // namespace slaaps
