#pragma once

#include "ManualOutput.hpp"
#include "ActuatorRouting.hpp"

namespace slaaps
{

struct ManualPreparationRequest {
	uint64_t timestamp_us{};
	uint64_t deadline_us{};
	uint64_t session{};
	uint64_t source{};
	uint64_t configuration{};
	unsigned command_revision{};
	unsigned acknowledgment_revisions[2] {};
	ManualOutputBinding binding{};
};

// Non-actuating driver preparation. No EdfPermission, PWM candidate or command
// acknowledgment is produced. Activation consumes this exact proposal once and
// must then pass the bank's independent armed/flight-permission checks.
class ManualBankPreparation
{
public:
	static constexpr uint64_t kMaximumPreparationUs = 1000000;
	static bool validRequest(uint64_t now, const ManualPreparationRequest &request)
	{
		if (!request.timestamp_us || request.timestamp_us > now || !request.session || request.session > request.timestamp_us
		    || request.deadline_us <= now || request.deadline_us - request.timestamp_us > kMaximumPreparationUs
		    || !request.source || !request.configuration || !request.command_revision || !validManualOutputBinding(request.binding)) { return false; }

		for (unsigned i = 0; i < 2; ++i) {
			if (static_cast<bool>(request.binding.banks & (1U << i)) != (request.acknowledgment_revisions[i] != 0)) { return false; }
		}

		return true;
	}
	bool prepare(uint64_t now, unsigned bank, const VehicleConfiguration &configuration,
		     const OutputInventory &inventory, const ManualPreparationRequest &request)
	{
		_pending = false;

		if (!now || now < _clock) { return false; }

		_clock = now;

		if (!request.session || request.session <= _last_session || request.session > now) { return false; }

		_last_session = request.session; // Failed proposals cannot be replayed after repair.

		if (!validRequest(now, request) || bank >= 2 || !(request.binding.banks & (1U << bank))
		    || !validate(configuration, ConfigurationUse::StabilizedManual).valid()
		    || request.configuration != fingerprint(configuration)) { return false; }

		RoutingPlan routing{};

		if (!makeRoutingPlan(configuration, inventory, true, routing).valid()
		    || request.binding.banks != routing.bank_mask) { return false; }

		_bank = bank;
		_request = request;
		_routing = routing;
		_pending = true;
		return true;
	}
	bool consume(uint64_t now, const VehicleConfiguration &configuration, const OutputInventory &inventory,
		     const ManualOutputBinding &binding, const EdfPermission &permission, unsigned command_revision, unsigned acknowledgment_revision)
	{
		const bool ready = prepared(now);
		_pending = false;

		if (!ready) { return false; }

		_clock = now;

		if (permission.session != _request.session || permission.source != _request.source
		    || permission.configuration != _request.configuration || permission.mode != ControlMode::Manual
		    || fingerprint(configuration) != _request.configuration || command_revision != _request.command_revision
		    || acknowledgment_revision != _request.acknowledgment_revisions[_bank]
		    || !sameManualOutputBinding(binding, _request.binding)) { return false; }

		RoutingPlan routing{};
		return makeRoutingPlan(configuration, inventory, true, routing).valid() && sameRoutingPlan(routing, _routing);
	}
	bool check(uint64_t now, const VehicleConfiguration &configuration, const OutputInventory &inventory)
	{
		RoutingPlan routing{};

		if (!prepared(now) || fingerprint(configuration) != _request.configuration
		    || !makeRoutingPlan(configuration, inventory, true, routing).valid() || !sameRoutingPlan(routing, _routing)) {
			cancel();
			return false;
		}

		_clock = now;
		return true;
	}
	bool prepared(uint64_t now) const { return _pending && now >= _clock && now < _request.deadline_us; }
	void cancel() { _pending = false; }
private:
	ManualPreparationRequest _request{};
	RoutingPlan _routing{};
	uint64_t _clock{}, _last_session{};
	unsigned _bank{};
	bool _pending{};
};

} // namespace slaaps
