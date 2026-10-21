#pragma once

#include "Transport.hpp"
#include <lib/research_stabilized_manual/StabilizedOutputDriver.hpp>

namespace research_stabilized_session_fixture
{

struct PlantWrite {
	DriverWrite driver{};
	float thrust_n{};
};
struct PlantCompletion {
	bool handled{};
	bool disarmed{};
	bool has_acknowledgement{};
	gz::msgs::Double_V acknowledgement{};
};

// Serialized simulation-side output owner; transport callbacks must enqueue,
// never invoke this concurrently. Retain across controller replacement. Grant
// is an explicit supervisor operation, never inferred from command traffic.
// Call prepare on EVERY simulation step, then complete only after handling
// the exact candidate. No callback or Publish return may stand in for handling.
class PlantEndpoint
{
public:
	PlantEndpoint(uint32_t identity, const OutputConfiguration &configuration, float fixed_thrust_n) :
		_identity(identity), _configuration(configuration), _thrust(fixed_thrust_n),
		_valid(identity && validOutputConfiguration(configuration)
		       && configuration.on_fault == OutputFallback::Disarmed
		       && std::isfinite(fixed_thrust_n) && fixed_thrust_n >= 0.0F) {}
	PlantEndpoint(const PlantEndpoint &) = delete;
	PlantEndpoint &operator=(const PlantEndpoint &) = delete;

	bool grant(TimeUs now, TimeUs session, TimeUs deadline, uint32_t identity, bool permitted)
	{
		if (!_valid || _granted || _faulted || identity != _identity || !session || session > now
		    || deadline <= now || deadline > kMaximumWireInteger || now > kMaximumWireInteger
		    || !_driver.disarmedConfirmed() || _driver.state() != DriverState::Disabled) { return false; }

		if (_driver.begin(now, session, _configuration, permitted) != OutputStatus::Ok) {
			_faulted = true;
			return false;
		}

		_granted = true;
		return true;
	}
	void revoke()
	{
		_driver.revoke();
		_granted = false;
		_faulted = false;
	}
	bool prepare(TimeUs now, bool permitted, const gz::msgs::Double_V *wire, PlantWrite &write)
	{
		write = {};
		Command command{};
		const OutputFrame *frame = nullptr;

		if (wire) {
			if (!_granted || !decode(*wire, command) || command.configuration != _identity
			    || command.lease_us != _configuration.lease_us || command.thrust_n != _thrust) {
				_faulted = true;

			} else { frame = &command.frame; }
		}

		if (now > kMaximumWireInteger) { _faulted = true; }

		if (!_driver.prepare(now, _valid && _granted && !_faulted && permitted, frame, write.driver)) { return false; }

		if (_driver.state() == DriverState::Fault) { _faulted = true; }

		if (!_faulted && write.driver.candidate.disposition == OutputDisposition::Command) { write.thrust_n = _thrust; }

		return true;
	}
	bool pending(uint64_t ticket) const { return _driver.pending(ticket); }
	bool cancel(uint64_t ticket)
	{
		if (!_driver.cancel(ticket)) { return false; }

		_faulted = true;
		return true;
	}
	PlantCompletion complete(uint64_t ticket, TimeUs completed_us, bool handled, bool permitted)
	{
		const auto result = _driver.complete(ticket, {completed_us, handled},
						     _valid && _granted && !_faulted && permitted && completed_us <= kMaximumWireInteger);
		PlantCompletion completion{};
		completion.handled = result.handled;
		completion.disarmed = result.revoke_acknowledged;

		if (_driver.state() == DriverState::Fault) { _faulted = true; }

		if (result.command_acknowledged && !_faulted) {
			const Acknowledgement ack{_identity, result.acknowledgement};
			completion.has_acknowledgement = encode(ack, completion.acknowledgement);

			if (!completion.has_acknowledgement) { _faulted = true; }
		}

		return completion;
	}
	bool ready() const { return _valid && !_faulted && !_granted && _driver.disarmedConfirmed(); }
	bool faulted() const { return _faulted; }
private:
	const uint32_t _identity;
	const OutputConfiguration _configuration;
	const float _thrust;
	const bool _valid;
	StabilizedOutputDriver _driver{};
	bool _granted{}, _faulted{};
};
} // namespace research_stabilized_session_fixture
