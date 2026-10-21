#pragma once
#if !defined(__PX4_POSIX) || !defined(RESEARCH_STABILIZED_SITL_ONLY)
#error Private POSIX session consumer only.
#endif
#include "RemoteCompletion.hpp"
#include "OutputConfiguration.hpp"
#include <gz/transport/Node.hh>
#include <modules/research_stabilized_manual/SelectedStateGuard.hpp>
#include <modules/research_stabilized_manual/GrantHandshake.hpp>
#include <px4_platform_common/px4_work_queue/ScheduledWorkItem.hpp>
#include <uORB/Publication.hpp>
#include <uORB/ResearchSubscription.hpp>
#include <uORB/topics/stabilized_output_command.h>
#include <uORB/topics/stabilized_output_ack.h>
#include <mutex>
#include <deque>
#include <cstring>

namespace research_stabilized_session_fixture
{
inline bool isolatedProfileValid()
{
	const char *single_estimator[] = {"EKF2_MULTI_IMU", "EKF2_MULTI_MAG", "SENS_IMU_MODE", "SENS_MAG_MODE", "COM_RC_IN_MODE", "COM_PREARM_MODE"};
	const int32_t expected[] = {0, 0, 1, 1, 1, 2};

	for (unsigned i = 0; i < 6; ++i) {
		int32_t value{};
		const param_t handle = param_find(single_estimator[i]);

		if (handle == PARAM_INVALID || param_get(handle, &value) != 0 || value != expected[i]) { return false; }
	}

	// No output functions may be assigned in this isolated profile.
	for (const char *prefix : {"SIM_GZ_SV_FUNC", "SIM_GZ_EC_FUNC", "SIM_GZ_WH_FUNC"}) {
		const unsigned count = strcmp(prefix, "SIM_GZ_EC_FUNC") == 0 ? 16 : strcmp(prefix, "SIM_GZ_SV_FUNC") == 0 ? 8 : 4;

		for (unsigned i = 1; i <= count; ++i) {
			char name[24] {};
			snprintf(name, sizeof(name), "%s%u", prefix, i);
			const param_t handle = param_find(name);
			int32_t value{};

			if (handle == PARAM_INVALID || param_get(handle, &value) != 0 || value != 0) { return false; }
		}
	}
	return true;
}

// Private lifecycle fault injection: no production publication API is changed.
template<typename T>
class SessionTestPublication : public uORB::Publication<T>
{
public:
	explicit SessionTestPublication(const orb_metadata *meta) : uORB::Publication<T>(meta) {}
	bool replace(const T &message)
	{
		if (!this->unadvertise()) { return false; }

		this->_handle = nullptr;
		return this->publish(message);
	}
};

constexpr px4::wq_config_t kSessionConsumerQueue{"wq:slaaps_cons", 10000, -5};
class Px4SessionConsumer final : public px4::ScheduledWorkItem
{
public:
	enum class Phase : unsigned { InitialDisarm, Idle, GrantPending, Active, Revoking, Fault, Stopped };
	Px4SessionConsumer(const ObserverProfile &profile, unsigned supervisor_revision) :
		ScheduledWorkItem("session_consumer", kSessionConsumerQueue), _profile(profile), _health(_profile),
		_supervisor_revision(supervisor_revision) {}
	~Px4SessionConsumer() override
	{
		for (const auto &topic : _node.SubscribedTopics()) { _node.Unsubscribe(topic); }
	}
	bool start(const std::string &prefix)
	{
		if (!isolatedProfileValid() || !_supervisor_revision || prefix.rfind("/slaaps_session/", 0) != 0 || prefix.size() < 32) { return false; }

		if (!_ack.advertise() || !_response.advertise()) { return false; }

		_response_revision = _response_source.revision();

		if (!_response_revision) { return false; }

		_command_out = _node.Advertise<gz::msgs::Double_V>(prefix + "/command");
		_request_out = _node.Advertise<gz::msgs::Double_V>(prefix + "/request");

		if (!_command_out || !_request_out
		    || !_node.Subscribe(prefix + "/ack", &Px4SessionConsumer::ack, this)
		    || !_node.Subscribe(prefix + "/reply", &Px4SessionConsumer::reply, this)
		    || !_node.Subscribe(prefix + "/status", &Px4SessionConsumer::status, this)) { return false; }

		ScheduleOnInterval(4000);
		return true;
	}
	void replaceAckSource() { _replace_ack.store(true); }
	void revoke() { _revoke_requested.store(true); }
	void stop() { _stop_requested.store(true); _revoke_requested.store(true); }
	Phase phase() const { return static_cast<Phase>(_phase.load()); }
	unsigned acknowledgements() const { return _acknowledgements.load(); }
	unsigned faults() const { return _faults.load(); }
private:
	struct Event { unsigned kind; gz::msgs::Double_V wire; };
	void enqueue(unsigned kind, const gz::msgs::Double_V &wire)
	{
		std::lock_guard<std::mutex> lock(_mutex);

		if (_events.size() >= 64 || wire.data_size() > static_cast<int>(kCommandFields)) { _overflow = true; return; }

		_events.push_back({kind, wire});
	}
	void ack(const gz::msgs::Double_V &wire) { enqueue(0, wire); }
	void reply(const gz::msgs::Double_V &wire) { enqueue(1, wire); }
	void status(const gz::msgs::Double_V &wire) { enqueue(2, wire); }
	bool sources()
	{
		return _response_source.revision() == _response_revision && _request.revision() == _supervisor_revision
		       && _command.revision() == _grant.command_revision
		       && _ack_source.revision() == _grant.acknowledgement_revision;
	}
	bool permitted()
	{
		return isolatedProfileValid() && _health.check(_inputs.capture(_profile)) && sources();
	}
	void publishRequest(std::initializer_list<uint64_t> values)
	{
		gz::msgs::Double_V wire;

		for (uint64_t value : values) { wire.add_data(static_cast<double>(value)); }

		(void)_request_out.Publish(wire);
	}
	void beginRevoke(TimeUs now, bool deliberate)
	{
		stabilized_grant_response_s invalid{};
		invalid.timestamp = now; invalid.session = _grant.session; invalid.deadline = _grant.deadline;
		invalid.configuration = 1; invalid.request_revision = _supervisor_revision;
		invalid.command_revision = _grant.command_revision;
		invalid.acknowledgement_revision = _grant.acknowledgement_revision;
		(void)_response.publish(invalid); // Revoke cached acceptance before any asynchronous wait.

		if (phase() == Phase::Revoking) { _deliberate |= deliberate; return; }

		if ((phase() == Phase::Idle || phase() == Phase::Fault) && _driver.disarmedConfirmed() && !_ticket) {
			_deliberate = deliberate; finishDisarm(now); return;
		}

		_deliberate = deliberate;

		if (!deliberate) { _faults.fetch_add(1); }

		_remote.revoke(now);
		_driver.revoke();
		_revoke_time = now;
		_phase.store(static_cast<unsigned>(Phase::Revoking));

		if (_grant.session) { publishRequest({1, 4, 1, _grant.session}); }
	}
	bool remoteDisarmed(TimeUs now, const gz::msgs::Double_V &wire)
	{
		uint32_t config{};
		uint64_t session{}, handled{};

		if (!prefix(wire, 5, 7, config) || config != 1 || !integer(wire.data(3), session)
		    || session != _grant.session || !integer(wire.data(4), handled) || handled < _revoke_time || handled > now) { return false; }

		if (_remote.pending() && _remote.confirmDisarm(now, wire).status != RemoteStatus::Disarmed) { return false; }

		return true;
	}
	void finishDisarm(TimeUs now)
	{
		// A matching plant reply proves old remote I/O stopped. Only now may
		// the abandoned local transaction be canceled and its barrier handled.
		if (_ticket) { (void)_driver.cancel(_ticket); _ticket = 0; }

		_driver.revoke();
		DriverWrite write{};

		if (!_driver.prepare(now, false, nullptr, write)
		    || !_driver.complete(write.ticket, {now, true}, false).revoke_acknowledged) { return; }
		_health.disable();
		_remote_ack = {};
		_disarm_reply = {};
		_grant = {};

		if (_stop_requested.load()) { _phase.store(static_cast<unsigned>(Phase::Stopped)); ScheduleClear(); }

		else { _phase.store(static_cast<unsigned>(_deliberate ? Phase::Idle : Phase::Fault)); }
	}
	void Run() override
	{
		TimeUs now = hrt_absolute_time();

		if (_replace_ack.load()) {
			_replace_ack.store(false);
			stabilized_output_ack_s before{}, after{}; unsigned old_revision{}, new_revision{};
			const TimeUs started = hrt_absolute_time();
			const bool ok = phase() == Phase::Active && _ack_source.copyBound(&before, old_revision)
					&& _ack.replace(before) && _ack_source.copyBound(&after, new_revision)
					&& old_revision != new_revision && std::memcmp(&before, &after, sizeof(before)) == 0;
			PX4_INFO("lifecycle ack ok=%u before=%u after=%u elapsed=%llu age=%llu", (unsigned)ok,
				 old_revision, new_revision, (unsigned long long)(hrt_absolute_time() - started),
				 (unsigned long long)(started - before.timestamp));

			if (!ok) { beginRevoke(hrt_absolute_time(), false); return; }
		}


		if (_revoke_requested.load()) { _revoke_requested.store(false); beginRevoke(now, true); }

		std::deque<Event> events;
		bool overflow{};
		{
			std::lock_guard<std::mutex> lock(_mutex);
			events.swap(_events); overflow = _overflow; _overflow = false;
		}

		if (overflow) { beginRevoke(now, false); }

		for (const auto &event : events) {
			const auto &wire = event.wire;

			if (event.kind == 2) {
				uint32_t config{};
				uint64_t stamp{}, session{}, ready{}, fault{};

				if (prefix(wire, 7, 5, config) && config == 1 && integer(wire.data(3), stamp)
				    && stamp <= now && now - stamp <= 100000 && integer(wire.data(4), session)
				    && integer(wire.data(5), ready) && ready <= 1 && integer(wire.data(6), fault) && fault <= 1) {
					if ((phase() == Phase::InitialDisarm || (phase() == Phase::Revoking && !_grant.session)) && ready && !fault && !session) { _deliberate = true; finishDisarm(now); }
				}

			} else if (event.kind == 1) {
				uint32_t disarm_config{};

				if (phase() == Phase::Revoking && prefix(wire, 5, 7, disarm_config) && disarm_config == 1) {
					_disarm_reply = wire; continue;
				}

				uint32_t config{};
				uint64_t session{}, deadline{}, accepted{};

				if (phase() == Phase::GrantPending && prefix(wire, 6, 6, config) && config == 1
				    && integer(wire.data(3), session) && session == _grant.session
				    && integer(wire.data(4), deadline) && deadline == _grant.deadline
				    && integer(wire.data(5), accepted)) {
					const bool grant_permitted = permitted();
					now = hrt_absolute_time();

					if (accepted != 1 || now >= deadline || !grant_permitted
					    || _driver.begin(now, session, _output, true) != OutputStatus::Ok) { beginRevoke(now, false); continue; }

					// A revoked session can leave an unread command in this subscription.
					// Drain it before granting the producer permission to publish a new session.
					stabilized_output_command_s discarded{}; unsigned discarded_revision{};

					if (_command.updated() && (!_command.copyBound(&discarded, discarded_revision)
								   || discarded_revision != _grant.command_revision)) { beginRevoke(now, false); continue; }

					if (!permitted() || hrt_absolute_time() >= _grant.deadline) { beginRevoke(hrt_absolute_time(), false); continue; }

					stabilized_grant_response_s response{};
					response.timestamp = now; response.session = session; response.deadline = deadline; response.configuration = 1;
					response.request_revision = _supervisor_revision; response.command_revision = _grant.command_revision;
					response.acknowledgement_revision = _grant.acknowledgement_revision; response.accepted = true;

					if (!_response.publish(response)) { beginRevoke(now, false); continue; }

					_phase.store(static_cast<unsigned>(Phase::Active));
				}

			} else if (phase() == Phase::Active && _remote.pending()) {
				if (_remote_ack.data_size()) { beginRevoke(now, false); }

				else { _remote_ack = wire; }
			}
		}

		if (phase() == Phase::Revoking) {
			if (_disarm_reply.data_size() && remoteDisarmed(now, _disarm_reply)) {
				_disarm_reply = {}; finishDisarm(now);

			} else if (_grant.session && now >= _last_revoke_send && now - _last_revoke_send >= 100000) {
				publishRequest({1, 4, 1, _grant.session}); _last_revoke_send = now;
			}

			return;
		}

		if (phase() == Phase::Idle && _request.updated()) {
			stabilized_grant_request_s request{}; unsigned revision{};

			if (!_request.copyBound(&request, revision) || revision != _supervisor_revision
			    || !validGrantRequest(hrt_absolute_time(), request, 1, _command.revision(), _ack_source.revision())) { return; }

			if (!isolatedProfileValid() || !_health.begin(_inputs.capture(_profile))) { return; }

			now = hrt_absolute_time();

			if (_request.revision() != _supervisor_revision
			    || !validGrantRequest(now, request, 1, _command.revision(), _ack_source.revision())) {
				_health.disable(); return;
			}

			_grant = request;
			_phase.store(static_cast<unsigned>(Phase::GrantPending));
			publishRequest({1, 3, 1, request.session, request.deadline});
		}

		if (phase() == Phase::GrantPending) {
			if (now >= _grant.deadline || !permitted()) { beginRevoke(now, false); }

			return;
		}

		if (phase() != Phase::Active) { return; }

		const bool allowed = permitted();
		now = hrt_absolute_time();

		if (!allowed) { beginRevoke(now, false); return; }

		if (_remote.pending()) {
			if (_remote.poll(now, allowed, _command.revision()) == RemoteStatus::Fault) { beginRevoke(now, false); return; }

			if (_remote_ack.data_size()) {
				const bool acknowledgement_permitted = permitted();
				now = hrt_absolute_time();
				const auto result = _remote.acknowledge(now, acknowledgement_permitted, _command.revision(), _remote_ack);

				if (result.status == RemoteStatus::Fault) { beginRevoke(now, false); return; }

				if (result.status == RemoteStatus::Handled) {
					const auto completed = _driver.complete(result.ticket, result.write, permitted());
					_ticket = 0; _remote_ack = {};

					if (!completed.command_acknowledged) { beginRevoke(now, false); return; }

					stabilized_output_ack_s ack{};
					ack.timestamp = completed.acknowledgement.accepted_us; ack.session = completed.acknowledgement.session;
					ack.sequence = completed.acknowledgement.sequence; ack.command_timestamp = completed.acknowledgement.command_timestamp_us;

					if (!_ack.publish(ack)) { beginRevoke(now, false); return; }

					_acknowledgements.fetch_add(1);
				}
			}

			return;
		}

		OutputFrame frame{};
		const OutputFrame *next = nullptr;

		if (_command.updated()) {
			stabilized_output_command_s message{}; unsigned revision{};

			if (!_command.copyBound(&message, revision) || revision != _grant.command_revision) { beginRevoke(now, false); return; }

			frame.session = message.session; frame.timestamp_us = message.timestamp; frame.sequence = message.sequence;

			for (unsigned i = 0; i < 4; ++i) { frame.command[i] = message.command[i]; }

			next = &frame;
		}

		DriverWrite write{};

		const bool write_permitted = permitted();
		now = hrt_absolute_time();

		if (!_driver.prepare(now, write_permitted, next, write)) { beginRevoke(now, false); return; }

		_ticket = write.ticket;

		if (_driver.state() == DriverState::Fault) { beginRevoke(now, false); return; }

		if (!next) {
			// No new physical write: existing plant lease runs independently.
			// The local held-candidate poll may complete, but emits no ack.
			(void)_driver.complete(write.ticket, {now, true}, permitted()); _ticket = 0;

			if (_driver.state() == DriverState::Fault) { beginRevoke(hrt_absolute_time(), false); }

			return;
		}

		Command command{1, frame, _output.lease_us, _profile.control.fixed_simulated_thrust_n};
		gz::msgs::Double_V wire;

		if (!_remote.start(now, write.ticket, command, _command.revision(), _output.acknowledgement_us)
		    || !encode(command, wire) || !permitted() || !_driver.pending(write.ticket)
		    || _remote.poll(hrt_absolute_time(), true, _command.revision()) != RemoteStatus::Waiting
		    || !_command_out.Publish(wire)) { beginRevoke(now, false); }
	}
	const ObserverProfile _profile;
	const OutputConfiguration _output{outputConfiguration()};
	SelectedStateInput _inputs{};
	SelectedStateGuard _health;
	const unsigned _supervisor_revision;
	unsigned _response_revision{};
	uORB::ResearchSubscription _request{ORB_ID(stabilized_grant_request)}, _command{ORB_ID(stabilized_output_command)},
	     _ack_source{ORB_ID(stabilized_output_ack)}, _response_source{ORB_ID(stabilized_grant_response)};
	SessionTestPublication<stabilized_output_ack_s> _ack{ORB_ID(stabilized_output_ack)};
	uORB::Publication<stabilized_grant_response_s> _response{ORB_ID(stabilized_grant_response)};
	stabilized_grant_request_s _grant{};
	StabilizedOutputDriver _driver{};
	RemoteCompletion _remote{};
	gz::transport::Node _node{};
	gz::transport::Node::Publisher _command_out{}, _request_out{};
	gz::msgs::Double_V _remote_ack{}, _disarm_reply{};
	std::mutex _mutex{};
	std::deque<Event> _events{};
	bool _overflow{}, _deliberate{};
	TimeUs _revoke_time{}, _last_revoke_send{};
	uint64_t _ticket{};
	px4::atomic<unsigned> _phase{static_cast<unsigned>(Phase::InitialDisarm)}, _acknowledgements{0}, _faults{0};
	px4::atomic_bool _revoke_requested{false}, _stop_requested{false}, _replace_ack{false};
};
} // namespace research_stabilized_session_fixture
