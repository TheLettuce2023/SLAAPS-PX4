#include "Px4SessionConsumer.hpp"
#include <modules/research_stabilized_manual/SessionRequestOwner.hpp>
#include <lib/research_stabilized_manual/StabilizedManualSession.hpp>
#include <px4_platform_common/module.h>
#include <cstring>

using namespace research_stabilized_session_fixture;
namespace
{
constexpr px4::wq_config_t kSessionProducerQueue{"wq:slaaps_prod", 10000, -5};
class Px4SessionSITL final : public px4::ScheduledWorkItem
{
public:
	enum class Phase : unsigned { Ready, GrantPending, Active, Fault, Stopping, Stopped };
	explicit Px4SessionSITL(const ObserverProfile &profile) :
		ScheduledWorkItem("session_producer", kSessionProducerQueue), _profile(profile), _health(_profile) {}
	bool start(const char *prefix, const char *csv)
	{
		if (!isolatedProfileValid() || _profile.control.fixed_simulated_thrust_n < 10.0F || _profile.control.fixed_simulated_thrust_n > 10.0F
		    || !_request_pub.advertise() || !_command_pub.advertise()) { return false; }

		_log = std::fopen(csv, "w");

		if (!_log) { return false; }

		setvbuf(_log, nullptr, _IOLBF, 0);
		fprintf(_log,
			"time,state,att_time,att_sample,rate_time,rate_sample,gyro_id,accel_id,reset,sysid,compid,manual_time,manual_sample,instance,manual_valid,roll,pitch,yaw,qw,qx,qy,qz,rw,rx,ry,rz,wx,sx,mx,ax,ix,bx,ex,px,nx,wy,sy,my,ay,iy,by,ey,py,ny,wz,sz,mz,az,iz,bz,ez,pz,nz,u0,u1,u2,u3,thrust\n");
		_consumer = new Px4SessionConsumer(_profile, _request.revision());

		if (!_consumer || !_consumer->start(prefix)) { return false; }

		ScheduleOnInterval(4000);
		return true;
	}
	bool command(const char *name)
	{
		if (!_consumer) { return false; }

		if (std::strcmp(name, "replace_ack_source") == 0 && phase() == Phase::Active) {
			_consumer->replaceAckSource(); return true;
		}

		unsigned request{};

		if (std::strcmp(name, "enable") == 0 && phase() == Phase::Ready && _ready.load()) { request = 1; }

		if (std::strcmp(name, "disable") == 0) { request = 2; }

		if (std::strcmp(name, "stop") == 0) { request = 3; }

		if (std::strcmp(name, "restart") == 0 && phase() == Phase::Fault
		    && _consumer->phase() == Px4SessionConsumer::Phase::Idle) { request = 4; }

		if (std::strcmp(name, "replace_command_source") == 0 && phase() == Phase::Active) { request = 5; }

		if (!request) { return false; }

		unsigned expected{};
		return _operation.compare_exchange(&expected, request);
	}
	Phase phase() const { return static_cast<Phase>(_phase.load()); }
	void status()
	{
		PX4_INFO("phase=%u state=%u ready=%u consumer=%u commands=%u acknowledgements=%u fault=%u private SITL only",
			 static_cast<unsigned>(phase()), _state.load(), _ready.load(),
			 static_cast<unsigned>(_consumer ? _consumer->phase() : Px4SessionConsumer::Phase::Stopped),
			 _commands.load(), _consumer ? _consumer->acknowledgements() : 0u, _fault.load());
	}
private:
	GrantSources sources()
	{
		return {_request.revision(), _response.revision(), _command.revision(), _ack.revision()};
	}
	bool sourcesMatch(const GrantSources &current)
	{
		const auto &expected = _handshake.sources;
		return current.request == expected.request && current.response == expected.response
		       && current.command == expected.command && current.acknowledgement == expected.acknowledgement;
	}
	void fail(OutputStatus reason)
	{
		_request_owner.revoke(); _controller.disable(); _health.disable(); _consumer->revoke();
		_phase.store(static_cast<unsigned>(Phase::Fault)); _state.store(static_cast<unsigned>(State::Fault));
		_fault.store(static_cast<unsigned>(reason)); _ready.store(0);
	}
	void log(uint64_t now, const StateSample &sample, const ManualSample &manual, const Report &report);
	void Run() override
	{
		const unsigned operation = _operation.fetch_and(0);

		if (operation == 5) {
			stabilized_output_command_s before{}, after{}; unsigned old_revision{}, new_revision{};
			const TimeUs started = hrt_absolute_time();
			const bool ok = phase() == Phase::Active && _command.copyBound(&before, old_revision)
					&& _command_pub.replace(before) && _command.copyBound(&after, new_revision)
					&& old_revision != new_revision && std::memcmp(&before, &after, sizeof(before)) == 0;
			PX4_INFO("lifecycle command ok=%u before=%u after=%u elapsed=%llu age=%llu", (unsigned)ok,
				 old_revision, new_revision, (unsigned long long)(hrt_absolute_time() - started),
				 (unsigned long long)(started - before.timestamp));

			if (!ok) { fail(OutputStatus::InvalidState); return; }
		}


		if (operation == 3) {
			_request_owner.revoke(); _controller.disable(); _health.disable(); _consumer->stop();
			_phase.store(static_cast<unsigned>(Phase::Stopping)); _ready.store(0);
		}

		if (phase() == Phase::Stopping) {
			if (_consumer->phase() == Px4SessionConsumer::Phase::Stopped) {
				_phase.store(static_cast<unsigned>(Phase::Stopped)); ScheduleClear();

				if (_log) { std::fclose(_log); _log = nullptr; }
			}

			return;
		}

		if (operation == 2) { fail(OutputStatus::PermissionLost); return; }

		if (operation == 4) {
			_controller.disable(); _health.disable(); _fault.store(0);
			_phase.store(static_cast<unsigned>(Phase::Ready)); _state.store(static_cast<unsigned>(State::Ready));
		}

		if (phase() == Phase::Fault || phase() == Phase::Stopped) { return; }

		auto snapshot = _inputs.capture(_profile);
		_gyro_id = snapshot.gyro; _accel_id = snapshot.accel;
		_ready.store(isolatedProfileValid() && snapshot.prearmed_servo_condition && snapshot.manual.valid
			     && _consumer->phase() == Px4SessionConsumer::Phase::Idle);

		if (phase() == Phase::Ready) {
			if (operation != 1 && operation != 4) { return; }

			if (!isolatedProfileValid() || !_health.begin(snapshot) || _consumer->phase() != Px4SessionConsumer::Phase::Idle) {
				fail(OutputStatus::PermissionLost); return;
			}

			const auto now = hrt_absolute_time();

			// Idle is reached only after the consumer confirms plant disarm.
			// This fixture admission is not a production hardware policy.
			if (!_request_owner.confirmDisarmed(now, _handshake.request.session, true)
			    || !_request_owner.request(now, now + 2000000, 1, sources(), true)) {
				fail(OutputStatus::InvalidState); return;
			}

			_session = _handshake.request.session;
			const auto &request = _handshake.request;

			if (!_request_pub.publish(request)) { fail(OutputStatus::InvalidState); return; }

			_phase.store(static_cast<unsigned>(Phase::GrantPending)); _ready.store(0);
			return;
		}

		if (!isolatedProfileValid() || !_health.check(snapshot) || !sourcesMatch(sources())) { fail(OutputStatus::PermissionLost); return; }

		if (!_request_owner.check(snapshot.now, true, sources())) { fail(OutputStatus::Expired); return; }

		if (phase() == Phase::GrantPending) {
			if (snapshot.now >= _handshake.request.deadline) { fail(OutputStatus::Expired); return; }

			stabilized_grant_response_s response{}; unsigned revision{};

			if (!_response.copyBound(&response, revision)
			    || !grantConfirmed(hrt_absolute_time(), _handshake, response, revision, sources())) { return; }

			// Drain acknowledgments from a previous session, then re-copy the
			// coherent response and selected inputs immediately before begin.
			stabilized_output_ack_s discarded{}; unsigned ignored{};
			(void)_ack.copyBound(&discarded, ignored);
			snapshot = _inputs.capture(_profile);

			if (!isolatedProfileValid() || !_health.check(snapshot) || !_response.copyBound(&response, revision)
			    || _consumer->phase() != Px4SessionConsumer::Phase::Active
			    || !_request_owner.accept(hrt_absolute_time(), true, sources(), response, revision)) { fail(OutputStatus::PermissionLost); return; }

			const auto result = _controller.begin(hrt_absolute_time(), _session, _profile.control, outputConfiguration(),
							      snapshot.manual, snapshot.state, true, _handshake.sources.acknowledgement, _ack.revision());

			if (result != OutputStatus::Ok) { fail(result); return; }

			_phase.store(static_cast<unsigned>(Phase::Active)); _state.store(static_cast<unsigned>(_controller.controlState()));
			return;
		}

		if (_consumer->phase() != Px4SessionConsumer::Phase::Active) { fail(OutputStatus::PermissionLost); return; }

		BoundOutputAcknowledgement acknowledgement{};
		const BoundOutputAcknowledgement *received = nullptr;

		if (_ack.updated()) {
			stabilized_output_ack_s message{}; unsigned revision{};

			if (!_ack.copyBound(&message, revision)) { fail(OutputStatus::InvalidAcknowledgement); return; }

			acknowledgement = {{message.session, message.command_timestamp, message.timestamp, message.sequence}, revision};
			received = &acknowledgement;
		}

		Report report{}; OutputFrame frame{};
		const auto update = _controller.update(hrt_absolute_time(), sourcesMatch(sources()), snapshot.manual,
						       snapshot.state, _ack.revision(), received, report, frame);

		if (update.status == OutputStatus::Waiting || update.status == OutputStatus::NoCommand) { return; }

		if (update.status != OutputStatus::Ok) { fail(update.status); return; }

		stabilized_output_command_s message{};
		message.timestamp = frame.timestamp_us; message.session = frame.session; message.sequence = frame.sequence;

		for (unsigned i = 0; i < 4; ++i) { message.command[i] = frame.command[i]; }

		if (!sourcesMatch(sources()) || !_command_pub.publish(message)) { fail(OutputStatus::InvalidState); return; }

		_state.store(static_cast<unsigned>(_controller.controlState())); _commands.fetch_add(1);
		log(frame.timestamp_us, snapshot.state, snapshot.manual, report);
	}
	const ObserverProfile _profile;
	SelectedStateInput _inputs{};
	SelectedStateGuard _health;
	StabilizedManualSession _controller{};
	Px4SessionConsumer *_consumer{};
	uORB::Publication<stabilized_grant_request_s> _request_pub{ORB_ID(stabilized_grant_request)};
	SessionTestPublication<stabilized_output_command_s> _command_pub{ORB_ID(stabilized_output_command)};
	uORB::ResearchSubscription _request{ORB_ID(stabilized_grant_request)}, _command{ORB_ID(stabilized_output_command)},
	     _response{ORB_ID(stabilized_grant_response)}, _ack{ORB_ID(stabilized_output_ack)};
	SessionRequestOwner _request_owner{};
	const GrantHandshake &_handshake{_request_owner.handshake()};
	FILE *_log{};
	TimeUs _session{};
	uint32_t _gyro_id{}, _accel_id{};
	px4::atomic<unsigned> _operation{0}, _phase{static_cast<unsigned>(Phase::Ready)}, _state{static_cast<unsigned>(State::Ready)},
	    _ready{0}, _fault{0}, _commands{0};
};
void Px4SessionSITL::log(uint64_t now, const StateSample &sample, const ManualSample &manual, const Report &report)
{
	fprintf(_log, "%llu,%d,%llu,%llu,%llu,%llu,%u,%u,%u,%u,%u",
		(unsigned long long)now, static_cast<int>(_controller.controlState()),
		(unsigned long long)sample.attitude_timestamp_us, (unsigned long long)sample.attitude_sample_us,
		(unsigned long long)sample.rate_timestamp_us, (unsigned long long)sample.rate_sample_us,
		_gyro_id, _accel_id, sample.attitude_reset_counter, manual.source_system_id, manual.source_component_id);
	fprintf(_log, ",%llu,%llu,%u,%u", (unsigned long long)manual.timestamp_us,
		(unsigned long long)manual.timestamp_sample_us, manual.mavlink_instance, (unsigned)manual.valid);

	for (unsigned i = 0; i < 3; ++i) { fprintf(_log, ",%.9g", (double)manual.axes[i]); }

	for (unsigned i = 0; i < 4; ++i) { fprintf(_log, ",%.9g", (double)sample.attitude(i)); }

	for (unsigned i = 0; i < 4; ++i) { fprintf(_log, ",%.9g", (double)report.hold_reference(i)); }

	for (unsigned i = 0; i < 3; ++i) {
		fprintf(_log, ",%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%u,%u", (double)sample.rate_rad_s(i),
			(double)report.rate_setpoint_rad_s(i), (double)report.requested_moment(i),
			(double)report.allocation.achieved(i), (double)report.integral(i),
			(double)report.bounded_moment(i), (double)report.allocation.residual(i),
			(unsigned)report.allocation.saturation_positive(i), (unsigned)report.allocation.saturation_negative(i));
	}

	for (float control : report.allocation.command) { fprintf(_log, ",%.9g", (double)control); }

	fprintf(_log, ",%.9g\n", (double)report.fixed_simulated_thrust_n);
}

Px4SessionSITL *owner{};
}
extern "C" __EXPORT int research_session_sitl_main(int argc, char **argv)
{
	if (argc == 2 && owner && std::strcmp(argv[1], "status") == 0) { owner->status(); return PX4_OK; }

	if (argc == 2 && owner) { return owner->command(argv[1]) ? PX4_OK : PX4_ERROR; }

	if (argc != 5 || owner || std::strcmp(argv[1], "start") != 0) {
		PX4_WARN("start <observer-profile> <unique-plant-namespace> <csv>; enable|disable|restart|status|stop");
		return PX4_ERROR;
	}

	FILE *file = std::fopen(argv[2], "r");

	if (!file) { return PX4_ERROR; }

	ObserverProfile profile{};
	const bool valid = readObserverProfile(file, profile);
	std::fclose(file);

	if (!valid) { return PX4_ERROR; }

	// Observer files remain output-disabled; only this private synthetic mode supplies thrust.
	profile.control.fixed_simulated_thrust_n = 10.0F; // Declared fixed simulated EDF thrust, in newtons.

	// Retain owners until process exit, including failed asynchronous starts.
	owner = new Px4SessionSITL(profile);
	return owner && owner->start(argv[3], argv[4]) ? PX4_OK : PX4_ERROR;
}
