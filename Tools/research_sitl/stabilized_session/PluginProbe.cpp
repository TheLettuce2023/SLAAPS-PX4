#include "RemoteCompletion.hpp"
#include <gz/transport/Node.hh>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <fstream>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>
using namespace research_stabilized_session_fixture;

class Probe
{
public:
	~Probe()
	{
		for (const auto &topic : _node.SubscribedTopics()) { _node.Unsubscribe(topic); }
	}
	bool run(const std::string &prefix, const std::string &csv)
	{
		_log.open(csv);
		_log << "session,sequence,command_time,accepted_time\n";

		if (!_node.Subscribe(prefix + "/status", &Probe::status, this)
		    || !_node.Subscribe(prefix + "/reply", &Probe::reply, this)
		    || !_node.Subscribe(prefix + "/ack", &Probe::ack, this)) { return false; }

		_command = _node.Advertise<gz::msgs::Double_V>(prefix + "/command");
		_request = _node.Advertise<gz::msgs::Double_V>(prefix + "/request");

		if (!wait([&] { return _time > 0 && _ready && _command.HasConnections() && _request.HasConnections(); })) { return false; }

		for (unsigned cycle = 0; cycle < 2; ++cycle) {
			uint64_t session{};
			{
				std::lock_guard<std::mutex> lock(_mutex);
				session = _time;
			}
			publish({1, 3, 1, session, session + 1000000});

			if (!wait([&] { return grantReply(session, true); })) { return false; }
			// An expired competing request cannot steal the accepted session.
			publish({1, 3, 1, session + 1, session + 1});

			if (!wait([&] { return grantReply(session + 1, false); })) { return false; }
			uint64_t last = session;

			for (uint64_t sequence = 1; sequence <= 20; ++sequence) {
				if (!wait([&] { return _time >= last + 12000; })) { return false; }
				TimeUs now{};
				{
					std::lock_guard<std::mutex> lock(_mutex);
					now = _time;
				}
				Command command{1, {session, now, sequence, {0.25F, -0.25F, 0.25F, -0.25F}}, 100000, 10.0F};
				gz::msgs::Double_V wire;

				if (!_completion.start(now, sequence, command, 1, 50000) || !encode(command, wire) || !_command.Publish(wire)) { return false; }

				if (!wait([&] {
				for (const auto &a : _acks)
					{
						if (a.value.session == session && a.value.sequence == sequence) {
							gz::msgs::Double_V receipt;

							if (!encode(a, receipt)) { return false; }

							const auto completed = _completion.acknowledge(_time, true, 1, receipt);
							return completed.status == RemoteStatus::Handled && completed.ticket == sequence
							       && completed.write.handled && completed.write.completed_us == a.value.accepted_us;
						}
					}
					return false;
				})) { return false; }
				last = now;
			}

			if (!wait([&] { return _fault && _time >= last + 100000; })) { return false; }
			// Healthy data cannot revive the expired session.
			Command delayed{1, {session, last + 12000, 21, {}}, 100000, 10.0F};
			gz::msgs::Double_V wire;

			if (!encode(delayed, wire) || !_command.Publish(wire)) { return false; }

			if (!wait([&] { return _time >= last + 220000; })) { return false; }
			publish({1, 4, 1, session});

			if (!wait([&] { return _ready && disarmReply(session); })) { return false; }
		}

		std::lock_guard<std::mutex> lock(_mutex);

		if (_acks.size() != 40) { return false; }

		std::printf("PLUGIN_RESULT acknowledgements=%zu sessions=2 expiry=2 disarm=2 PASS\n", _acks.size());
		return true;
	}
private:
	bool wait(const std::function<bool()> &predicate)
	{
		std::unique_lock<std::mutex> lock(_mutex);
		const bool ok = _condition.wait_for(lock, std::chrono::seconds(10), predicate);

		if (!ok) { std::fprintf(stderr, "Timeout time=%llu ready=%d fault=%d acks=%zu\n", (unsigned long long)_time, _ready, _fault, _acks.size()); }

		return ok;
	}
	void publish(std::initializer_list<uint64_t> values)
	{
		gz::msgs::Double_V message;

		for (uint64_t value : values) { message.add_data(static_cast<double>(value)); }

		_request.Publish(message);
	}
	bool grantReply(uint64_t session, bool accepted)
	{
		for (const auto &r : _replies) {
			if (r.data_size() == 6 && r.data(1) == 6 && r.data(3) == static_cast<double>(session)
			    && r.data(5) == (accepted ? 1.0 : 0.0)) { return true; }
		}

		return false;
	}
	bool disarmReply(uint64_t session)
	{
		for (const auto &r : _replies) {
			if (r.data_size() == 5 && r.data(1) == 7 && r.data(3) == static_cast<double>(session)) { return true; }
		}

		return false;
	}
	void status(const gz::msgs::Double_V &message)
	{
		if (message.data_size() != 7) { return; }

		std::lock_guard<std::mutex> lock(_mutex);
		_time = static_cast<uint64_t>(message.data(3));
		_ready = message.data(5) == 1.0;
		_fault = message.data(6) == 1.0;
		_condition.notify_all();
	}
	void reply(const gz::msgs::Double_V &message)
	{
		std::lock_guard<std::mutex> lock(_mutex);
		_replies.push_back(message);
		_condition.notify_all();
	}
	void ack(const gz::msgs::Double_V &message)
	{
		Acknowledgement result{};

		if (!decode(message, result)) { return; }

		std::lock_guard<std::mutex> lock(_mutex);
		_acks.push_back(result);
		const auto &a = result.value;
		_log << a.session << ',' << a.sequence << ',' << a.command_timestamp_us << ',' << a.accepted_us << std::endl;
		_condition.notify_all();
	}
	RemoteCompletion _completion{};
	gz::transport::Node _node{};
	gz::transport::Node::Publisher _command{}, _request{};
	std::mutex _mutex{};
	std::condition_variable _condition{};
	uint64_t _time{};
	bool _ready{}, _fault{};
	std::vector<gz::msgs::Double_V> _replies{};
	std::vector<Acknowledgement> _acks{};
	std::ofstream _log{};
};
int main(int argc, char **argv)
{
	if (argc != 3) { return 2; }

	Probe probe;
	return probe.run(argv[1], argv[2]) ? 0 : 1;
}
