#pragma once

#include "GrantHandshake.hpp"

namespace research_stabilized_manual
{

// Serialized request lifecycle only: no publisher, scheduler or vehicle policy.
// The owner supplies current admission and trusted local disarm completion.
// Keep this object alive across controller restarts to retain session high-water.
class SessionRequestOwner
{
public:
	enum class Phase { Revoking, Ready, GrantPending, Active };
	SessionRequestOwner() = default;
	SessionRequestOwner(const SessionRequestOwner &) = delete;
	SessionRequestOwner &operator=(const SessionRequestOwner &) = delete;
	Phase phase() const { return _phase; }
	const GrantHandshake &handshake() const { return _handshake; }

// Caller must have stopped prior writes and observed a handled disarmed write
// for this session. This is not an actuator message or an electrical receipt.
// Calling this method is explicit re-admission; check() never recovers faults.
	bool confirmDisarmed(uint64_t now, uint64_t session, bool handled)
	{
		if (_phase != Phase::Revoking || !now || now < _clock || !handled
		    || session != _handshake.request.session) { return false; }

		_clock = now;
		_phase = Phase::Ready;
		return true;
	}
	bool request(uint64_t now, uint64_t deadline, uint64_t configuration,
		     const GrantSources &sources, bool admitted)
	{
		if (_phase != Phase::Ready || !admitted || !now || now < _clock
		    || now <= _high_water || deadline <= now || !configuration
		    || !sources.request || !sources.response || !sources.command || !sources.acknowledgement) { return false; }

		_handshake = {};
		_handshake.sources = sources;
		auto &request = _handshake.request;
		request.timestamp = now;
		request.session = now;
		request.deadline = deadline;
		request.configuration = configuration;
		request.command_revision = sources.command;
		request.acknowledgement_revision = sources.acknowledgement;
		_clock = now;
		_high_water = now;
		_phase = Phase::GrantPending;
		return true;
	}
	bool check(uint64_t now, bool admitted, const GrantSources &current)
	{
		if (_phase != Phase::GrantPending && _phase != Phase::Active) { return false; }

		const auto &expected = _handshake.sources;

		if (now < _clock || !admitted || current.request != expected.request
		    || current.response != expected.response || current.command != expected.command
		    || current.acknowledgement != expected.acknowledgement
		    || (_phase == Phase::GrantPending && now >= _handshake.request.deadline)) {
			if (now > _clock) { _clock = now; }

			revoke();
			return false;
		}

		_clock = now;
		return true;
	}
	bool accept(uint64_t now, bool admitted, const GrantSources &current,
		    const stabilized_grant_response_s &response, unsigned copied_revision)
	{
		if (_phase != Phase::GrantPending || !check(now, admitted, current)
		    || !grantConfirmed(now, _handshake, response, copied_revision, current)) { return false; }

		_phase = Phase::Active;
		return true;
	}
	void revoke() { _phase = Phase::Revoking; }
private:
	GrantHandshake _handshake{};
	uint64_t _clock{}, _high_water{};
	Phase _phase{Phase::Revoking};
};

} // namespace research_stabilized_manual
