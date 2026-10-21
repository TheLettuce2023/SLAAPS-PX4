#pragma once
#include "PlantEndpoint.hpp"
#include "OutputConfiguration.hpp"
#include "../stabilized_manual/FixtureConstants.hpp"
#include <gz/transport/Node.hh>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <string>

namespace research_stabilized_session_fixture
{
// Grant: [version, 3, configuration, session, deadline].
// Revoke: [version, 4, configuration, session].
// Status: [version, 5, configuration, simulation_time, session, ready, fault].
// Grant reply: [version, 6, configuration, session, deadline, accepted].
// Disarm reply: [version, 7, configuration, session, handled_time].
// The per-process namespace is supplied out of band at simulator startup and
// must never be reused. It isolates stale traffic; it is not authentication.
class GazeboPlantTransport
{
public:
	GazeboPlantTransport() : _endpoint(1, configuration(), 10.0F) {}
	~GazeboPlantTransport()
	{
		for (const auto &topic : _node.SubscribedTopics()) { _node.Unsubscribe(topic); }
	}
	static OutputConfiguration configuration() { return outputConfiguration(); }
	void configure(const std::string &prefix)
	{
		if (prefix.rfind("/slaaps_session/", 0) != 0 || prefix.size() < 32
		    || prefix.find_first_not_of("/abcdefghijklmnopqrstuvwxyz0123456789_") != std::string::npos) {
			throw std::runtime_error("Explicit unique private session namespace required");
		}

		_ack = _node.Advertise<gz::msgs::Double_V>(prefix + "/ack");
		_status = _node.Advertise<gz::msgs::Double_V>(prefix + "/status");
		_reply = _node.Advertise<gz::msgs::Double_V>(prefix + "/reply");

		if (!_ack || !_status || !_reply
		    || !_node.Subscribe(prefix + "/command", &GazeboPlantTransport::command, this)
		    || !_node.Subscribe(prefix + "/request", &GazeboPlantTransport::request, this)) {
			throw std::runtime_error("Session transport setup failed");
		}
	}
	void revoke() { _endpoint.revoke(); _inhibited = true; }
	bool prepare(TimeUs now)
	{
		std::deque<Event> events;
		bool overflow{};
		{
			std::lock_guard<std::mutex> lock(_mutex);
			events.swap(_events);
			overflow = _overflow;
			_overflow = false;
		}

		if (overflow) { revoke(); }

		gz::msgs::Double_V command;
		bool have_command = false;

		for (const auto &event : events) {
			if (event.command) {
				if (have_command) { revoke(); }

				command = event.message;
				have_command = true;
				continue;
			}

			const auto &message = event.message;
			uint64_t version{}, kind{}, identity{}, session{}, deadline{};
			bool valid = (message.data_size() == 4 || message.data_size() == 5)
				     && integer(message.data(0), version) && version == kVersion
				     && integer(message.data(1), kind)
				     && integer(message.data(2), identity) && identity == 1
				     && integer(message.data(3), session) && session;

			if (valid && kind == 4 && message.data_size() == 4 && session == _session) {
				revoke(); have_command = false;

			} else if (valid && kind == 3 && message.data_size() == 5
				   && integer(message.data(4), deadline)) {
				const bool accepted = !overflow && _endpoint.grant(now, session, deadline, 1, true);

				if (accepted) { _session = session; _inhibited = false; }

				publish(_reply, {kVersion, 6, 1, session, deadline, accepted ? uint64_t{1} : uint64_t{0}});

			} else { revoke(); have_command = false; }
		}

		const bool prepared = _endpoint.prepare(now, !_inhibited, have_command ? &command : nullptr, _write);

		if (have_command && prepared && _write.driver.candidate.disposition == OutputDisposition::Command) {
			Command decoded{};

			if (decode(command, decoded)) { _accepted = decoded; }
		}

		return prepared;
	}
	void complete(TimeUs now, bool handled)
	{
		const auto result = _endpoint.complete(_write.driver.ticket, now, handled, !_inhibited);

		if (result.has_acknowledgement && !_ack.Publish(result.acknowledgement)) { revoke(); }

		if (result.disarmed) { publish(_reply, {kVersion, 7, 1, _session, now}); }

		publish(_status, {kVersion, 5, 1, now, _session, _endpoint.ready() ? uint64_t{1} : uint64_t{0},
				  _endpoint.faulted() ? uint64_t{1} : uint64_t{0}
				 });
	}
	bool faulted() const { return _endpoint.faulted() || _inhibited; }
	bool enabled() const { return !_inhibited && _write.driver.candidate.disposition == OutputDisposition::Command; }
	float value(unsigned channel) const
	{
		return enabled() ? _write.driver.candidate.driver_control[channel] : 0.0F;
	}
	const Command &accepted() const { return _accepted; }
private:
	struct Event { bool command; gz::msgs::Double_V message; };
	void enqueue(bool command, const gz::msgs::Double_V &message)
	{
		std::lock_guard<std::mutex> lock(_mutex);

		if (_events.size() >= 32 || message.data_size() > static_cast<int>(kCommandFields)) { _overflow = true; return; }

		_events.push_back({command, message});
	}
	void command(const gz::msgs::Double_V &message) { enqueue(true, message); }
	void request(const gz::msgs::Double_V &message) { enqueue(false, message); }
	static void publish(gz::transport::Node::Publisher &publisher, std::initializer_list<uint64_t> values)
	{
		gz::msgs::Double_V message;

		for (uint64_t value : values) { message.add_data(static_cast<double>(value)); }

		(void)publisher.Publish(message); // Missing replies never grant producer authority.
	}
	gz::transport::Node _node{};
	gz::transport::Node::Publisher _ack{}, _status{}, _reply{};
	std::mutex _mutex{};
	std::deque<Event> _events{};
	bool _overflow{}, _inhibited{true};
	TimeUs _session{};
	PlantEndpoint _endpoint;
	PlantWrite _write{};
	Command _accepted{};
};
} // namespace research_stabilized_session_fixture
