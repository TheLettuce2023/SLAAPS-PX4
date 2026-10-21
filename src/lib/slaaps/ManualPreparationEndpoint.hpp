#pragma once

#include "ManualBankTransport.hpp"
#include "ManualPreparationTransport.hpp"

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
public:
	explicit ManualPreparationEndpoint(ManualBankTransport<Bank> &bank) : _bank(bank) {}
	~ManualPreparationEndpoint() { _bank.cancelPreparation(); }
	ManualPreparationEndpoint(const ManualPreparationEndpoint &) = delete;
	ManualPreparationEndpoint &operator=(const ManualPreparationEndpoint &) = delete;

	bool advertise()
	{
		if (_owned_revision) { return revision() != 0; }

		if (!_ready.advertise()) { return false; }

		_owned_revision = _ready_lifetime.revision();
		return _owned_revision != 0;
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

	PreparationStatus poll(uint64_t now, const VehicleConfiguration &configuration, const OutputInventory &inventory)
	{
		if (_bank.active() || !_listening) { return PreparationStatus::Inactive; }

		if (!now || now < _clock || !revision()) { return reject(now); }

		_clock = now;
		slaaps_manual_preparation_s wire{};
		const auto read = _proposal.read(wire);

		if (read == BoundRead::Waiting && !_have_request) { return PreparationStatus::Waiting; }

		ManualPreparationRequest request{};
		uint64_t prepared{};

		if (read != BoundRead::Data || !decodeManualPreparation(now, slaaps_manual_preparation_s::REQUEST_BANK, wire, request, prepared)) {
			return reject(now);
		}

		const bool duplicate = _have_request && request.session == _request.session;

		if (duplicate && !sameManualPreparation(request, _request)) { return reject(now); }

		if (!duplicate) {
			if (!_bank.prepare(now, configuration, inventory, request)) { return reject(now); }

			_request = request;
			_have_request = true;
		}

		if (!_bank.checkPreparation(now, configuration, inventory)) { return reject(now); }

		if (!duplicate && (!encodeManualPreparation(now, request, Bank, now, wire) || !_ready.publish(wire))) { return reject(now); }

		// Retained and republished duplicates neither repeat prepare nor renew
		// readiness timestamps/deadlines. Live configuration is still checked.
		return PreparationStatus::Ready;
	}

	bool activate(uint64_t now, const VehicleConfiguration &configuration, const OutputInventory &inventory,
		      const ManualOutputBinding &binding, const EdfPermission &permission, unsigned command_revision)
	{
		if (_bank.active()) { return false; }

		if (poll(now, configuration, inventory) != PreparationStatus::Ready) { return false; }

		const bool activated = _bank.beginPrepared(now, configuration, inventory, binding, permission, command_revision);
		cancel(now);
		return activated;
	}

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
	PreparationStatus reject(uint64_t now) { cancel(now); return PreparationStatus::Rejected; }
	ManualBankTransport<Bank> &_bank;
	BoundSubscription<slaaps_manual_preparation_s> _proposal{ORB_ID::slaaps_manual_prepare};
	uORB::Publication<slaaps_manual_preparation_s> _ready{kReadyTopic};
	uORB::ResearchSubscription _ready_lifetime{kReadyTopic};
	ManualPreparationRequest _request{};
	uint64_t _clock{};
	unsigned _owned_revision{};
	bool _listening{}, _have_request{};
};

} // namespace slaaps
