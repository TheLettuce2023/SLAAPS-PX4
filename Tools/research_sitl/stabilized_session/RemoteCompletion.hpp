#pragma once
#include "Transport.hpp"
#include <lib/research_stabilized_manual/StabilizedOutputDriver.hpp>

namespace research_stabilized_session_fixture
{
enum class RemoteStatus { Idle, Waiting, Handled, Fault, Disarmed };
struct RemoteResult {
	RemoteStatus status{RemoteStatus::Idle};
	uint64_t ticket{};
	OutputWriteResult write{};
};

// One nonblocking pending remote write. Call only on the serialized PX4
// consumer queue; transport callbacks enqueue messages. Source revision is
// trusted local command-publication metadata, not a value from the network.
// This helper grants no ownership and never completes a mixer transaction.
class RemoteCompletion
{
public:
	bool start(TimeUs now, uint64_t ticket, const Command &command, uint32_t source_revision, TimeUs acknowledgement_us)
	{
		gz::msgs::Double_V checked;

		if (_active || _fault || now < _clock || !ticket || !source_revision || !acknowledgement_us
		    || acknowledgement_us >= command.lease_us || !encode(command, checked)
		    || now < command.frame.timestamp_us || now - command.frame.timestamp_us >= acknowledgement_us) { return false; }

		_command = command;
		_ticket = ticket;
		_source = source_revision;
		_budget = acknowledgement_us;
		_started = now;
		_clock = now;
		_revoke_time = 0;
		_active = true;
		return true;
	}
	RemoteStatus poll(TimeUs now, bool permitted, uint32_t source_revision)
	{
		if (!_active) { return _fault ? RemoteStatus::Fault : RemoteStatus::Idle; }

		if (now < _clock || !permitted || source_revision != _source
		    || now < _command.frame.timestamp_us || now - _command.frame.timestamp_us >= _budget) { _fault = true; }

		if (now >= _clock) { _clock = now; }

		return _fault ? RemoteStatus::Fault : RemoteStatus::Waiting;
	}
	RemoteResult acknowledge(TimeUs now, bool permitted, uint32_t source_revision, const gz::msgs::Double_V &wire)
	{
		const auto state = poll(now, permitted, source_revision);

		if (state != RemoteStatus::Waiting) { return {state, 0, {}}; }

		Acknowledgement ack{};

		if (!decode(wire, ack) || ack.configuration != _command.configuration
		    || ack.value.session != _command.frame.session || ack.value.sequence != _command.frame.sequence
		    || ack.value.command_timestamp_us != _command.frame.timestamp_us
		    || ack.value.accepted_us < _started
		    || ack.value.accepted_us - _command.frame.timestamp_us >= _budget) {
			_fault = true;
			return {RemoteStatus::Fault, 0, {}};
		}

		// Gazebo can publish during the next lockstep phase before PX4's
		// clock advances. Retain the same queued receipt and retry; never
		// consume a future completion or extend the original deadline.
		if (ack.value.accepted_us > now) { return {RemoteStatus::Waiting, 0, {}}; }

		_active = false;
		return {RemoteStatus::Handled, _ticket, {ack.value.accepted_us, true}};
	}
	void revoke(TimeUs now)
	{
		if (!_active) { if (now >= _clock) { _clock = now; } return; }

		_fault = true;

		if (!_revoke_time && now >= _clock) { _revoke_time = now; }

		if (now >= _clock) { _clock = now; }
	}
	RemoteResult confirmDisarm(TimeUs now, const gz::msgs::Double_V &wire)
	{
		// Only a reply for this pending session, handled at/after the explicit
		// revoke request, proves that its remote I/O has stopped. The caller
		// may then cancel the local ticket and run its normal disarm barrier.
		uint32_t configuration{};
		uint64_t session{}, handled{};
		const bool clock_valid = now >= _clock;

		if (clock_valid) { _clock = now; }

		if (!_active || !_fault || !_revoke_time || !clock_valid
		    || !prefix(wire, 5, 7, configuration) || configuration != _command.configuration
		    || !integer(wire.data(3), session) || session != _command.frame.session
		    || !integer(wire.data(4), handled) || handled < _revoke_time || handled > now) {
			return {RemoteStatus::Fault, 0, {}};
		}

		_clock = now;
		_active = false;
		_fault = false;
		return {RemoteStatus::Disarmed, _ticket, {handled, true}};
	}
	bool pending() const { return _active; }
private:
	Command _command{};
	TimeUs _budget{}, _clock{}, _started{}, _revoke_time{};
	uint64_t _ticket{};
	uint32_t _source{};
	bool _active{}, _fault{};
};
} // namespace research_stabilized_session_fixture
