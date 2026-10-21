#pragma once

#include "ManualBankPreparation.hpp"

namespace slaaps
{

enum class ActivationStatus : uint8_t { Inactive, Waiting, Ready, Consumed, Rejected };

// Completion accounting after all-bank preparation. This grants no permission.
// The caller binds activation-reply publishers and continues observation/health
// checks. On rejection cancel EVERY required bank: a lost reply may conceal an
// activated peer. First-command budget is explicit and requires qualification.
class ManualActivationBarrier
{
public:
	bool begin(uint64_t now, const VehicleConfiguration &configuration, const ManualPreparationRequest &request,
		   uint64_t activation_deadline_us, uint64_t first_command_budget_us)
	{
		if (!now || now < _clock || !request.session || request.session <= _last_session || request.session > now) { return rejectBegin(); }

		_clock = now;
		_last_session = request.session;

		if (_status == ActivationStatus::Waiting || _status == ActivationStatus::Ready) { return rejectBegin(); }

		_banks = 0;

		if (!ManualBankPreparation::validRequest(now, request)
		    || !validate(configuration, ConfigurationUse::StabilizedManual).valid()
		    || request.configuration != fingerprint(configuration)
		    || first_command_budget_us <= static_cast<uint64_t>(configuration.cycle_min_us)
		    || first_command_budget_us >= static_cast<uint64_t>(configuration.cycle_max_us)
		    || activation_deadline_us <= now || activation_deadline_us > request.deadline_us
		    || activation_deadline_us - now >= static_cast<uint64_t>(configuration.cycle_max_us) - first_command_budget_us) { return rejectBegin(); }

		_request = request;
		_banks = request.binding.banks;
		_received = 0;
		_started = now;
		_deadline = activation_deadline_us;
		_budget = first_command_budget_us;
		_cycle_max = configuration.cycle_max_us;
		_status = ActivationStatus::Waiting;
		return true;
	}

	ActivationStatus accept(uint64_t now, unsigned bank, const ManualActivationAck &ack)
	{
		const auto current = status(now);

		if (current != ActivationStatus::Waiting && current != ActivationStatus::Ready) { return current; }

		if (bank >= 2 || ack.bank != bank || !(_banks & (1U << bank)) || ack.session != _request.session
		    || ack.source != _request.source || ack.configuration != _request.configuration || ack.routing != _request.binding.routing
		    || ack.driver != _request.binding.driver[bank] || ack.command_revision != _request.command_revision
		    || ack.acknowledgment_revision != _request.acknowledgment_revisions[bank]
		    || ack.activated_us < _started || ack.activated_us > now || ack.first_command_deadline_us <= now
		    || ack.first_command_deadline_us - ack.activated_us > _cycle_max
		    || ack.first_command_deadline_us - now <= _budget
		    || ((_received & (1U << bank)) && !sameManualActivation(ack, _acks[bank]))) { return reject(); }

		_acks[bank] = ack;
		_received |= 1U << bank;
		return status(now);
	}

	ActivationStatus status(uint64_t now)
	{
		if (_status != ActivationStatus::Waiting && _status != ActivationStatus::Ready) { return _status; }

		if (now < _clock || now >= _deadline) { return reject(); }

		_clock = now;

		for (unsigned bank = 0; bank < 2; ++bank) {
			if ((_received & (1U << bank)) && (_acks[bank].first_command_deadline_us <= now
							   || _acks[bank].first_command_deadline_us - now <= _budget)) { return reject(); }
		}

		_status = _received == _banks ? ActivationStatus::Ready : ActivationStatus::Waiting;
		return _status;
	}

	bool consume(uint64_t now, uint64_t &first_command_deadline_us)
	{
		if (status(now) != ActivationStatus::Ready) { return false; }

		uint64_t deadline = UINT64_MAX;

		for (unsigned bank = 0; bank < 2; ++bank) {
			if ((_banks & (1U << bank)) && _acks[bank].first_command_deadline_us < deadline) { deadline = _acks[bank].first_command_deadline_us; }
		}

		first_command_deadline_us = deadline;
		_status = ActivationStatus::Consumed;
		return true;
	}
	void cancel() { reject(); }
	uint8_t requiredBanks() const { return _banks; }
private:
	ActivationStatus reject() { _status = ActivationStatus::Rejected; return _status; }
	bool rejectBegin() { reject(); return false; }
	ManualPreparationRequest _request{};
	ManualActivationAck _acks[2] {};
	uint64_t _clock{}, _last_session{}, _started{}, _deadline{}, _budget{}, _cycle_max{};
	uint8_t _banks{}, _received{};
	ActivationStatus _status{ActivationStatus::Inactive};
};

} // namespace slaaps
