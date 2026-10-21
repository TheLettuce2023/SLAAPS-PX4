#pragma once

#include "BoundSubscription.hpp"
#include "ManualPreparationTransport.hpp"
#include "ObservedRoutingBinding.hpp"
#include <uORB/Publication.hpp>
#include <cstring>

namespace slaaps
{

// Non-actuating supervisor transaction. Ready means all required banks echoed
// this proposal; it is not flight permission or confirmation of activation.
// Poll throughout preparation, including after Ready. Keep the proposal live
// until trusted activation completes, then cancel. Driver activation is a
// separate transaction and must recheck permission/routing at the final write.
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
private:
	bool beginImpl(uint64_t now, const VehicleConfiguration &configuration, const OutputInventory &inventory,
		       const ManualPreparationRequest &request, const unsigned(&ready_revisions)[2], const ReportedRoutingSnapshot *snapshot,
		       InventoryFault capture_result, const ReportedRoutingPolicy &policy)
	{
		if (_pending) { cancel(now); }

		_main_ready.revoke();
		_aux_ready.revoke();
		_observed.cancel();
		_observed_mode = false;

		if (!now || now < _clock || !request.session || request.session <= _last_session || request.session > now) { return false; }

		_clock = now;
		_last_session = request.session; // Invalid transactions also consume their token.

		if (!revision() || !ManualBankPreparation::validRequest(now, request)
		    || !validate(configuration, ConfigurationUse::StabilizedManual).valid()
		    || fingerprint(configuration) != request.configuration
		    || !makeRoutingPlan(configuration, inventory, true, _routing).valid()
		    || _routing.bank_mask != request.binding.banks) { return false; }

		if (snapshot && !_observed.begin(now, configuration, *snapshot, capture_result, request.binding, policy)) { return false; }

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

	PreparationStatus pollImpl(uint64_t now, const VehicleConfiguration &configuration, const OutputInventory &inventory)
	{
		if (!_pending) { return PreparationStatus::Inactive; }

		RoutingPlan routing{};

		if (!now || now < _clock || !revision() || !lifetimesMatch()
		    || !ManualBankPreparation::validRequest(now, _request) || fingerprint(configuration) != _request.configuration
		    || !makeRoutingPlan(configuration, inventory, true, routing).valid() || !sameRoutingPlan(routing, _routing)) {
			return reject(now);
		}

		_clock = now;
		bool complete = true;

		for (unsigned bank = 0; bank < 2; ++bank) {
			if (!(_request.binding.banks & (1U << bank))) { continue; }

			slaaps_manual_preparation_s wire{};
			const auto read = ready(bank).read(wire);

			if (read == BoundRead::Rejected) { return reject(now); }

			// A reply retained before this proposal is never readiness for it.
			// Once positive readiness was seen, returning to that payload is a fault.
			if (!_seen[bank] && (read == BoundRead::Waiting
					     || (_have_baseline[bank] && memcmp(&wire, &_baseline[bank], sizeof(wire)) == 0))) {
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
	void cancel(uint64_t now)
	{
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
		       && (!(_request.binding.banks & 1U) || _main_ack_lifetime.revision() == _request.acknowledgment_revisions[0])
		       && (!(_request.binding.banks & 2U) || _aux_ack_lifetime.revision() == _request.acknowledgment_revisions[1]);
	}
	uORB::Publication<slaaps_manual_preparation_s> _proposal{ORB_ID::slaaps_manual_prepare};
	uORB::ResearchSubscription _proposal_lifetime{ORB_ID::slaaps_manual_prepare};
	uORB::ResearchSubscription _command_lifetime{ORB_ID::slaaps_manual_command};
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
