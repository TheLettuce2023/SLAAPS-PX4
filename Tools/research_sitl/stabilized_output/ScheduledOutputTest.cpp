#if !defined(__PX4_POSIX)
#error Synthetic output integration is restricted to the private native fixture.
#endif
#include <px4_platform_common/module.h>
#if !defined(CONFIG_RESEARCH_STABILIZED_OUTPUT_HOOK)
#error This fixture requires the actual mixer transaction hook.
#endif
#include <lib/research_stabilized_manual/StabilizedOutputDriver.hpp>
#include <lib/research_stabilized_manual/ObserverProfile.hpp>
#include <lib/research_stabilized_manual/StabilizedManualSession.hpp>
#include <lib/mixer_module/mixer_module.hpp>
#include <lib/mixer_module/ResearchStabilizedOutput.hpp>
#include <modules/research_stabilized_manual/GrantHandshake.hpp>
#include <modules/research_stabilized_manual/SessionRequestOwner.hpp>
#include <modules/research_stabilized_manual/SelectedStateGuard.hpp>
#include <uORB/Publication.hpp>
#include <uORB/Subscription.hpp>
#include <uORB/ResearchSubscription.hpp>
#include <uORB/topics/actuator_servos.h>
#include <uORB/topics/actuator_armed.h>
#include <uORB/topics/actuator_test.h>
#include <uORB/topics/stabilized_output_command.h>
#include <uORB/topics/stabilized_output_ack.h>
#include <parameters/param.h>
#include <cmath>
#include <cstring>

using namespace research_stabilized_manual;
namespace
{
constexpr px4::wq_config_t kInputQueue{"wq:output_inputs", 7000, -5};
constexpr px4::wq_config_t kProducerQueue{"wq:output_producer", 7000, -5};
constexpr px4::wq_config_t kConsumerQueue{"wq:output_consumer", 7000, -5};
constexpr TimeUs kLease{150000}, kAck{100000}, kGrantWindow{2000000};
constexpr uint64_t kConfigurationIdentity{1}; // One immutable synthetic profile in this process.
constexpr unsigned kBankChannels{8}, kCanardChannels{4};
unsigned checks{}, failures{};
void check(bool ok, const char *text)
{
	++checks;

	if (!ok) { ++failures; PX4_ERR("FAIL %s", text); }
}
template<typename Predicate> bool wait(Predicate predicate)
{
	const TimeUs start = hrt_absolute_time();

	while (!predicate() && hrt_absolute_time() - start < 3000000) { px4_usleep(5000); }

	return predicate();
}
OutputConfiguration configuration()
{
	OutputConfiguration c{};
	c.lease_us = kLease;
	c.acknowledgement_us = kAck;
	c.on_fault = OutputFallback::Calibrated;

	for (unsigned i = 0; i < 4; ++i) {
		c.servos[i] = {1100.0F, 1450.0F, 2100.0F, 500, 1500, 2500};

		if (i % 2) { c.servos[i] = {2100.0F, 1450.0F, 1100.0F, 500, 1500, 2500}; }

		c.minimum[i] = -1.0F;
		c.maximum[i] = 1.0F;
		c.fallback[i] = 0.1F;
	}

	return c;
}
void parameter(const char *name, int32_t value)
{
	const param_t handle = param_find(name);
	check(handle != PARAM_INVALID && param_set(handle, &value) == 0, name);
}

template<typename Message>
class FixturePublication : public uORB::Publication<Message>
{
public:
	using uORB::Publication<Message>::Publication;
	bool withdraw()
	{
		const bool ok = !this->advertised() || this->unadvertise();
		this->_handle = nullptr;
		return ok;
	}
};

// Independent synthetic selected-state/manual publishers. Only this fixture
// creates sensor data; production adapters consume actual selected uORB inputs.
class SelectedInputFixture final : public px4::ScheduledWorkItem
{
public:
	explicit SelectedInputFixture(const Configuration &configuration) : ScheduledWorkItem("output_inputs", kInputQueue),
		_configuration(configuration) {}
	void start() { ScheduleOnInterval(5000); }
	px4::atomic<int> stick_milli{0};
	px4::atomic_bool stop{false}, done{false}, ready{false}, bad_health{false}, omit_manual{false}, invalid_attitude{false}, regress_estimator{false};
	px4::atomic<unsigned> restart_attitude{0}, restarts{0}, gyro{101};
private:
	void Run() override
	{
		if (stop.load()) { ScheduleClear(); done.store(true); return; }

		if (restart_attitude.fetch_and(0U)) { (void)_attitude.withdraw(); restarts.fetch_add(1); }

		const TimeUs now = hrt_absolute_time();
		manual_control_setpoint_s manual{};
		manual.timestamp = manual.timestamp_sample = now;
		manual.valid = true;
		manual.data_source = manual_control_setpoint_s::SOURCE_MAVLINK_0 + _configuration.mavlink_instance;
		manual.source_system_id = _configuration.source_system_id;
		manual.source_component_id = _configuration.source_component_id;
		manual.roll = static_cast<float>(stick_milli.load()) / 1000.0F;
		vehicle_attitude_s attitude{};
		attitude.timestamp = attitude.timestamp_sample = now;
		attitude.q[0] = invalid_attitude.load() ? NAN : 1.0F;
		vehicle_angular_velocity_s rates{};
		rates.timestamp = rates.timestamp_sample = now;
		sensor_selection_s selection{};
		selection.timestamp = now;
		selection.gyro_device_id = gyro.load(); selection.accel_device_id = 102;
		estimator_status_s estimator{};
		estimator.timestamp = regress_estimator.load() ? now - 50000 : now; // Fresh but regressed test sample.
		estimator.gyro_device_id = selection.gyro_device_id; estimator.accel_device_id = selection.accel_device_id;
		estimator_status_flags_s flags{};
		flags.timestamp = flags.timestamp_sample = now;
		flags.cs_tilt_align = flags.cs_yaw_align = true;
		flags.fs_bad_hdg = bad_health.load();
		const bool ok = (omit_manual.load() || _manual.publish(manual)) && _attitude.publish(attitude)
				&& _rates.publish(rates) && _selection.publish(selection) && _estimator.publish(estimator) && _flags.publish(flags);
		ready.store(ok);
	}
	const Configuration _configuration;
	uORB::Publication<manual_control_setpoint_s> _manual{ORB_ID(manual_control_setpoint)};
	FixturePublication<vehicle_attitude_s> _attitude{ORB_ID(vehicle_attitude)};
	uORB::Publication<vehicle_angular_velocity_s> _rates{ORB_ID(vehicle_angular_velocity)};
	uORB::Publication<sensor_selection_s> _selection{ORB_ID(sensor_selection)};
	uORB::Publication<estimator_status_s> _estimator{ORB_ID(estimator_status)};
	uORB::Publication<estimator_status_flags_s> _flags{ORB_ID(estimator_status_flags)};
};

class Supervisor
{
public:
	unsigned advertise()
	{
		if (!_request.advertise()) { return 0; }

		return _request_identity.revision();
	}
	bool send(TimeUs session, unsigned command, unsigned acknowledgement, uint64_t configuration_id = kConfigurationIdentity)
	{
		_handshake = {};
		auto &request = _handshake.request;
		request.timestamp = hrt_absolute_time();
		request.session = session;
		request.deadline = session + kGrantWindow;
		request.configuration = configuration_id;
		request.command_revision = command;
		request.acknowledgement_revision = acknowledgement;
		_handshake.sources = {_request_identity.revision(), _response_identity.revision(), command, acknowledgement};
		return _request.publish(request);
	}
	// Positive admission path uses the same lifecycle as the coupled SITL owner.
	// send() remains a raw injector for malformed/competing-request scenarios.
	bool sendAdmitted(TimeUs now, unsigned command, unsigned acknowledgement, bool disarmed)
	{
		const GrantSources sources{_request_identity.revision(), _response_identity.revision(), command, acknowledgement};

		if (!_owner.confirmDisarmed(now, _owner.handshake().request.session, disarmed)
		    || !_owner.request(now, now + kGrantWindow, kConfigurationIdentity, sources, true)) { return false; }

		_handshake = _owner.handshake();

		if (!_request.publish(_handshake.request)) { _owner.revoke(); return false; }

		return true;
	}
	bool acceptAdmitted()
	{
		stabilized_grant_response_s response{};
		unsigned revision{};

		if (!_response_identity.copyBound(&response, revision)) { return false; }

		const GrantSources sources{_request_identity.revision(), _response_identity.revision(),
					   _command_identity.revision(), _ack_identity.revision()};
		return _owner.accept(hrt_absolute_time(), true, sources, response, revision);
	}
	const GrantHandshake &handshake() const { return _handshake; }
	bool withdraw() { return _request.withdraw(); }
private:
	FixturePublication<stabilized_grant_request_s> _request{ORB_ID(stabilized_grant_request)};
	uORB::ResearchSubscription _request_identity{ORB_ID(stabilized_grant_request)};
	uORB::ResearchSubscription _response_identity{ORB_ID(stabilized_grant_response)};
	uORB::ResearchSubscription _command_identity{ORB_ID(stabilized_output_command)}, _ack_identity{ORB_ID(stabilized_output_ack)};
	SessionRequestOwner _owner{};
	GrantHandshake _handshake{};
};

class Consumer final : public OutputModuleInterface, public ResearchOutputPermission, public ResearchOutputSource
{
public:
	Consumer(unsigned supervisor_revision, const ObserverProfile &profile) : OutputModuleInterface("output_capture", kConsumerQueue),
		_mix("PWM_MAIN", kBankChannels, *this, MixingOutput::SchedulingPolicy::Disabled, false, false),
		_adapter(_mix, *this, *this), _supervisor_revision(supervisor_revision), _profile(profile), _health(_profile)
	{
		check(_response.advertise(), "advertise grant response before requests");
		OutputCandidate candidate{};
		check(!_adapter.prepare(hrt_absolute_time(), nullptr, 0, candidate), "unattached adapter cannot prepare");
		check(!std::isfinite(_adapter.control(0)), "unprepared adapter has no normalized control");
		check(_adapter.attach(), "attach transaction before first driver dispatch");
		check(!_adapter.attach(), "second attachment cannot replace owner");
		MixingOutput auxiliary("PWM_AUX", 4, *this, MixingOutput::SchedulingPolicy::Disabled, false, false);
		check(!auxiliary.attachResearchTransaction(_adapter), "transaction restricted to MAIN PWM bank");
		MixingOutput late("PWM_MAIN", kBankChannels, *this, MixingOutput::SchedulingPolicy::Disabled, false, false);
		late.update();
		check(!late.attachResearchTransaction(_adapter), "late attachment cannot take an already-dispatching bank");
		_mix.update();
		ResearchMixerResult receipt{};
		check(!_adapter.takeResult(receipt), "unprepared mixer dispatch cannot produce acknowledgment");
		check(_adapter.prepare(hrt_absolute_time(), nullptr, 0, candidate), "prepare before cancellation");
		check(_adapter.cancel(), "cancel prepared write before I/O");
		check(!_adapter.cancel(), "duplicate cancellation rejected");
		_adapter.complete(true, hrt_absolute_time());
		check(!_adapter.takeResult(receipt), "completion after cancellation cannot acknowledge");
		check(!std::isfinite(_adapter.control(0)) && !std::isfinite(_adapter.control(kCanardChannels)),
		      "cancelled and out-of-range controls remain disarmed");
		check(_adapter.state() == DriverState::Revoking, "cancellation cannot complete initial revocation");
		revokeOwner();
	}
	void start() { ScheduleOnInterval(5000); }
	px4::atomic<uint64_t> granted{0}, last_command{0}, first_fallback{0};
	px4::atomic<unsigned> commands{0}, fallbacks{0}, disarms{0}, errors{0}, acknowledgements{0};
	px4::atomic<int> state{0};
	px4::atomic_bool permitted{true}, drop_ack{false}, fail_command{false}, fail_disarm{false};
	px4::atomic_bool slow_command{false};
	px4::atomic_bool revoke_inflight{false}, permission_inflight{false};
	px4::atomic_bool permission_before_write{false}, revoke_after_completion{false}, stale_arming{false};
	px4::atomic_bool delayed_prepared{false};
	px4::atomic<unsigned> phase_faults{0};
	px4::atomic<unsigned> rejected{0}, completions{0}, refresh_params{0};
	px4::atomic<unsigned> revoke_requested{0};
	px4::atomic<unsigned> revoke_processed{0};
	px4::atomic_bool supervisor_fault{false}, health_fault{false};
	px4::atomic_bool source_fault{false};
	px4::atomic_bool source_before_write{false}, source_inflight{false}, source_after_completion{false};
	px4::atomic_bool corrupt_revision{false};
	px4::atomic<unsigned> ack_revision{0}, restart_ack{0}, ack_restarts{0};
	px4::atomic<unsigned> restart_response{0}, response_restarts{0};
	px4::atomic_bool revoked{false}, stop{false}, done{false};
	bool outputPermitted(hrt_abstime) override
	{
		return permitted.load() && !supervisor_fault.load() && _supervisor_revision
		       && _request.revision() == _supervisor_revision && _health.check(_inputs.capture(_profile));
	}
	unsigned sourceRevision() override { return _command.revision(); }
	void disturbSource()
	{
		// An overlapping advertisement/withdrawal completes within this
		// consumer dispatch, without relying on timing between work queues.
		uORB::Publication<stabilized_output_command_s> competing{ORB_ID(stabilized_output_command)};

		if (!competing.advertise()) { errors.fetch_add(1); }

		_invalidated_write = true;
		phase_faults.fetch_add(1);
	}
	void write(const OutputCandidate &candidate)
	{
		_expected = candidate;
		_invalidated_write = false;
		_result = {};
		OutputCandidate overlapping{};

		if (_adapter.prepare(hrt_absolute_time(), nullptr, 0, overlapping)) { errors.fetch_add(1); }

		if (candidate.disposition == OutputDisposition::Command && permission_before_write.load()) {
			permission_before_write.store(false);
			permitted.store(false);
			_invalidated_write = true;
			phase_faults.fetch_add(1);
		}

		if (candidate.disposition == OutputDisposition::Command && delayed_prepared.load()) {
			delayed_prepared.store(false);
			// The candidate can be a retained command, with no new ack due.
			// Cross the lease as well as the shorter acknowledgment deadline.
			px4_usleep(kLease + 30000);
			actuator_armed_s fresh{};
			fresh.timestamp = hrt_absolute_time();
			fresh.prearmed = true;

			if (!_armed.publish(fresh)) { errors.fetch_add(1); }

			phase_faults.fetch_add(1);
		}

		if (candidate.disposition == OutputDisposition::Command && source_before_write.load()) {
			source_before_write.store(false);
			disturbSource();
		}

		_mix.update();

		if (_adapter.prepare(hrt_absolute_time(), nullptr, 0, overlapping)) { errors.fetch_add(1); }

		if (candidate.disposition == OutputDisposition::Command && revoke_after_completion.load()) {
			revoke_after_completion.store(false);
			revokeOwner();
			revoked.store(false);
			_invalidated_write = true;
			phase_faults.fetch_add(1);
		}

		ResearchMixerResult receipt{};

		if (candidate.disposition == OutputDisposition::Command && source_after_completion.load()) {
			source_after_completion.store(false);
			disturbSource();
		}

		if (!_adapter.takeResult(receipt) || !receipt.ticket) { errors.fetch_add(1); }

		else { completions.fetch_add(1); }

		if (!receipt.candidate_valid) { rejected.fetch_add(1); }

		_result = receipt.dispatch;

		if (_invalidated_write && (_result.command_acknowledged || _result.revoke_acknowledged)) { errors.fetch_add(1); }

		if (_adapter.takeResult(receipt)) { errors.fetch_add(1); }

		if (std::isfinite(_adapter.control(0))) { errors.fetch_add(1); }

		_mix.updateSubscriptions(false);
	}
	bool updateOutputs(float values[MAX_ACTUATORS], unsigned count, unsigned) override
	{
		const bool disarmed = !_adapter.candidateValidated() || _expected.disposition == OutputDisposition::Disarmed;

		if (count != kBankChannels) { errors.fetch_add(1); return false; }

		for (unsigned i = 0; i < count; ++i) {
			const float expected = disarmed || i >= kCanardChannels ? 0.0F : _expected.pwm_us[i];

			if (!std::isfinite(values[i]) || std::fabs(values[i] - expected) > 0.01F) {
				errors.fetch_add(1);
				return false;
			}
		}

		if ((disarmed && fail_disarm.load()) || (!disarmed && fail_command.load())) { return false; }

		if (!disarmed && slow_command.load()) { px4_usleep(120000); }

		// Fault injection occurs inside the actual driver callback, before the
		// mixer invokes its completion hook.
		const auto &candidate = _expected;

		if (!disarmed && candidate.disposition == OutputDisposition::Command && revoke_inflight.load()) {
			revoke_inflight.store(false);
			revokeOwner();
			revoked.store(false);
			_invalidated_write = true;
			phase_faults.fetch_add(1);
		}

		if (!disarmed && candidate.disposition == OutputDisposition::Command && permission_inflight.load()) {
			permission_inflight.store(false);
			permitted.store(false);
			_invalidated_write = true;
			phase_faults.fetch_add(1);
		}

		if (!disarmed && candidate.disposition == OutputDisposition::Command && source_inflight.load()) {
			source_inflight.store(false);
			disturbSource();
		}

		return true; // The only sink is memory; no PWM/PX4IO/serial device exists.
	}
private:
	void invalidateGrant()
	{
		if (_accepted_response.accepted) {
			_accepted_response.accepted = false;
			_accepted_response.timestamp = hrt_absolute_time();

			if (!_response.publish(_accepted_response)) { errors.fetch_add(1); }
		}
	}
	void revokeOwner()
	{
		_health.disable();
		_adapter.revoke();
		invalidateGrant();
	}
	void Run() override
	{
		if (restart_response.fetch_and(0U)) {
			if (!_response.withdraw() || !_response.advertise() || !_response.publish(_accepted_response)) { errors.fetch_add(1); }

			response_restarts.fetch_add(1);
		}

		if (restart_ack.fetch_and(0U)) {
			if (!_ack.withdraw() || !_ack.advertise() || !_ack.publish(_last_ack)) { errors.fetch_add(1); }

			ack_restarts.fetch_add(1);
		}

		if (!_ack_identity.revision()) {
			if (!_ack.withdraw() || !_ack.advertise()) { errors.fetch_add(1); }
		}

		ack_revision.store(_ack_identity.revision());
		actuator_armed_s armed{};
		armed.timestamp = hrt_absolute_time();

		if (stale_arming.load()) { armed.timestamp -= kAck; }

		armed.prearmed = permitted.load();
		armed.kill = !permitted.load();

		if (!_armed.publish(armed)) { errors.fetch_add(1); }

		if (!_initialized) {
			updateParams();
			_mix.updateSubscriptions(false);
			_initialized = true;
		}

		if (refresh_params.fetch_and(0U)) { updateParams(); }

		if (revoke_requested.fetch_and(0U) || stop.load()) {
			revokeOwner();
			revoked.store(false);
			revoke_processed.fetch_add(1);
		}

		stabilized_grant_request_s request{};
		unsigned request_revision{};
		const bool have_request = _request.copyBound(&request, request_revision);
		const TimeUs now = hrt_absolute_time();

		if (!supervisor_fault.load() && (!_supervisor_revision || _request.revision() != _supervisor_revision)) {
			revokeOwner();
			supervisor_fault.store(true);
		}

		if (have_request && !supervisor_fault.load() && request_revision == _supervisor_revision
		    && request.session && request.session != granted.load()) {
			const auto requested = request.session;
			const unsigned source = request.command_revision;

			if (_adapter.state() == DriverState::Disabled && source && source == sourceRevision()) {
				if (_adapter.begin(now, {now + 1, now + 2, source}, configuration()) != OutputStatus::InvalidTime) { errors.fetch_add(1); }

				if (_adapter.begin(now, {requested, requested, source}, configuration()) != OutputStatus::InvalidTime) { errors.fetch_add(1); }

				if (now > requested && _adapter.begin(now, {requested, now, source}, configuration()) != OutputStatus::Expired) { errors.fetch_add(1); }

				if (_adapter.state() != DriverState::Disabled) { errors.fetch_add(1); }
			}

			stabilized_grant_response_s response{};
			response.timestamp = now;
			response.session = request.session;
			response.deadline = request.deadline;
			response.configuration = request.configuration;
			response.command_revision = request.command_revision;
			response.acknowledgement_revision = request.acknowledgement_revision;
			response.request_revision = request_revision;

			if (validGrantRequest(now, request, kConfigurationIdentity, sourceRevision(), _ack_identity.revision())
			    && _adapter.state() == DriverState::Disabled && _health.begin(_inputs.capture(_profile))) {
				response.accepted = _adapter.begin(hrt_absolute_time(), {requested, request.deadline, source}, configuration()) == OutputStatus::Ok;

				if (!response.accepted) { _health.disable(); }
			}

			if (!_response.publish(response)) {
				errors.fetch_add(1);
				revokeOwner();

			} else if (response.accepted) { _accepted_response = response; granted.store(requested); }
		}

		stabilized_output_command_s message{};
		OutputFrame frame{};
		unsigned revision{};
		const bool updated = _command.updated() && _command.copyBound(&message, revision);

		if (updated) {
			frame = {message.session, message.timestamp, message.sequence, {}};

			for (unsigned i = 0; i < 4; ++i) { frame.command[i] = message.command[i]; }

			if (corrupt_revision.load()) {
				corrupt_revision.store(false);
				revision = 0;
				phase_faults.fetch_add(1);
			}
		}

		// Copy transport first, then sample the evaluation clock.
		OutputCandidate candidate{};

		if (!_adapter.prepare(hrt_absolute_time(), updated ? &frame : nullptr, revision, candidate)) {
			errors.fetch_add(1);
			return;
		}

		write(candidate);
		const auto result = _result;

		if (_adapter.state() != DriverState::Active) { invalidateGrant(); }

		state.store(static_cast<int>(_adapter.state()));
		health_fault.store(_health.faulted());
		source_fault.store(_adapter.sourceFault());

		if (result.handled) {
			if (result.candidate.disposition == OutputDisposition::Command) { commands.fetch_add(1); }

			else if (result.candidate.disposition == OutputDisposition::Fallback) {
				fallbacks.fetch_add(1);

				if (!first_fallback.load()) { first_fallback.store(hrt_absolute_time()); }

			} else { disarms.fetch_add(1); }
		}

		if (result.command_acknowledged) {
			last_command.store(result.acknowledgement.command_timestamp_us);

			if (!drop_ack.load()) {
				stabilized_output_ack_s ack{};
				ack.timestamp = result.acknowledgement.accepted_us;
				ack.session = result.acknowledgement.session;
				ack.sequence = result.acknowledgement.sequence;
				ack.command_timestamp = result.acknowledgement.command_timestamp_us;

				if (!_ack.publish(ack)) { errors.fetch_add(1); }

				else { _last_ack = ack; acknowledgements.fetch_add(1); }
			}
		}

		if (result.revoke_acknowledged) { revoked.store(true); }

		if (stop.load() && revoked.load()) { ScheduleClear(); done.store(true); }
	}
	MixingOutput _mix;
	ResearchStabilizedOutput _adapter;
	stabilized_grant_response_s _accepted_response{};
	const unsigned _supervisor_revision;
	const ObserverProfile _profile;
	SelectedStateInput _inputs{};
	SelectedStateGuard _health;
	uORB::ResearchSubscription _request{ORB_ID(stabilized_grant_request)};
	FixturePublication<stabilized_grant_response_s> _response{ORB_ID(stabilized_grant_response)};
	OutputCandidate _expected{};
	DriverDispatch _result{};
	bool _initialized{}, _invalidated_write{};
	uORB::Publication<actuator_armed_s> _armed{ORB_ID(actuator_armed)};
	uORB::ResearchSubscription _command{ORB_ID(stabilized_output_command)};
	FixturePublication<stabilized_output_ack_s> _ack{ORB_ID(stabilized_output_ack)};
	uORB::ResearchSubscription _ack_identity{ORB_ID(stabilized_output_ack)};
	stabilized_output_ack_s _last_ack{};
};

class Producer final : public px4::ScheduledWorkItem
{
public:
	Producer(const ObserverProfile &profile, SelectedInputFixture &fixture) : ScheduledWorkItem("output_owner", kProducerQueue),
		_profile(profile), _config(profile.control), _fixture(fixture), _health(_profile) {}
	unsigned advertiseSource()
	{
		if (!_command.advertise()) { return 0; }

		uORB::ResearchSubscription subscription{ORB_ID(stabilized_output_command)};
		return subscription.revision();
	}
	bool start(const GrantHandshake &handshake)
	{
		stabilized_grant_response_s response{};
		unsigned response_revision{};
		uORB::ResearchSubscription replies{ORB_ID(stabilized_grant_response)};
		uORB::ResearchSubscription requests{ORB_ID(stabilized_grant_request)};
		uORB::ResearchSubscription commands{ORB_ID(stabilized_output_command)};

		if (!replies.copyBound(&response, response_revision)) { return false; }

		const GrantSources current{requests.revision(), replies.revision(), commands.revision(), _ack.revision()};
		const TimeUs confirmed = hrt_absolute_time();

		if (!grantConfirmed(confirmed, handshake, response, response_revision, current)) { return false; }

		// Negative probes operate on copies before any controller state changes.
		auto altered = response;
		altered.accepted = false;
		check(!grantConfirmed(confirmed, handshake, altered, response_revision, current), "rejected grant cannot start controller");
		altered = response; ++altered.session;
		check(!grantConfirmed(confirmed, handshake, altered, response_revision, current), "wrong session response rejected");
		altered = response; ++altered.configuration;
		check(!grantConfirmed(confirmed, handshake, altered, response_revision, current), "wrong configuration response rejected");
		altered = response; ++altered.deadline;
		check(!grantConfirmed(confirmed, handshake, altered, response_revision, current), "wrong deadline response rejected");
		altered = response; ++altered.command_revision;
		check(!grantConfirmed(confirmed, handshake, altered, response_revision, current), "wrong command lifetime response rejected");
		altered = response; ++altered.acknowledgement_revision;
		check(!grantConfirmed(confirmed, handshake, altered, response_revision, current), "wrong acknowledgment lifetime response rejected");
		altered = response; ++altered.request_revision;
		check(!grantConfirmed(confirmed, handshake, altered, response_revision, current), "wrong request lifetime response rejected");
		altered = response; altered.timestamp = handshake.request.timestamp - 1;
		check(!grantConfirmed(confirmed, handshake, altered, response_revision, current), "response predating request rejected");
		altered = response; altered.timestamp = confirmed + 1;
		check(!grantConfirmed(confirmed, handshake, altered, response_revision, current), "future grant response rejected");
		check(!grantConfirmed(handshake.request.deadline, handshake, response, response_revision, current), "expired grant response rejected");
		check(!grantConfirmed(confirmed, handshake, response, 0, current), "unbound response copy rejected");
		auto changed = current; ++changed.request;
		check(!grantConfirmed(confirmed, handshake, response, response_revision, changed), "changed supervisor lifetime rejected");
		changed = current; ++changed.response;
		check(!grantConfirmed(confirmed, handshake, response, response_revision, changed), "changed response lifetime rejected");
		changed = current; ++changed.command;
		check(!grantConfirmed(confirmed, handshake, response, response_revision, changed), "changed command lifetime rejected");
		changed = current; ++changed.acknowledgement;
		check(!grantConfirmed(confirmed, handshake, response, response_revision, changed), "changed acknowledgment lifetime rejected");

		if (_session.active() || _health.bound()) { return false; }

		const TimeUs session = handshake.request.session;
		const unsigned expected_ack_revision = handshake.sources.acknowledgement;

		// The supervisor has already confirmed old-session disarm. Drain the
		// retained ack before granting this new publisher/owner incarnation.
		stabilized_output_ack_s discarded{};
		unsigned discarded_revision{};
		(void)_ack.copyBound(&discarded, discarded_revision);

		if (!replies.copyBound(&response, response_revision)) { return false; }

		const GrantSources final_sources{requests.revision(), replies.revision(), commands.revision(), _ack.revision()};
		const TimeUs now = hrt_absolute_time();

		if (!grantConfirmed(now, handshake, response, response_revision, final_sources)) { return false; }

		_config.minimum_cycle_us = 10000;
		_config.maximum_cycle_us = 250000;
		const TimeUs enabled_now = sampleInputs(true);

		if (enabled_now >= handshake.request.deadline) { return false; }

		const bool ok = _session.begin(enabled_now, session, _config, configuration(), _manual, _sample, _input_permitted,
					       expected_ack_revision, _ack.revision()) == OutputStatus::Ok;

		if (ok) { ScheduleOnInterval(10000); }

		return ok;
	}
	px4::atomic<unsigned> accepted{0}, rates{0}, holds{0};
	px4::atomic_bool fault{false}, stop{false}, done{false};
	px4::atomic<unsigned> duplicate{0};
	px4::atomic<unsigned> restart{0}, withdraw{0};
	px4::atomic_bool source_changed{false};
	px4::atomic_bool corrupt_ack_revision{false}, bad_state{false}, ack_source_fault{false};
	px4::atomic<unsigned> waiting{0};
	px4::atomic<int> session_fault{0};
private:
	TimeUs sampleInputs(bool begin = false)
	{
		const auto snapshot = _inputs.capture(_profile);
		_manual = snapshot.manual;
		_sample = snapshot.state;
		_input_permitted = begin ? _health.begin(snapshot) : _health.check(snapshot);
		return snapshot.now;
	}

	void Run() override
	{
		if (stop.load()) {
			_session.disable();
			_health.disable();
			(void)_command.withdraw();
			ScheduleClear();
			done.store(true);
			return;
		}

		if (restart.fetch_and(0U)) {
			const bool ok = _command.withdraw() && _command.publish(_last);
			source_changed.store(ok);

			if (!ok) { fault.store(true); }

			return;
		}

		if (withdraw.fetch_and(0U)) {
			source_changed.store(_command.withdraw());
			fault.store(true);
			return;
		}

		if (fault.load()) { return; }

		stabilized_output_ack_s ack{};
		unsigned revision{};
		const bool updated = _ack.updated() && _ack.copyBound(&ack, revision);

		if (updated && corrupt_ack_revision.load()) {
			corrupt_ack_revision.store(false);
			revision = 0;
		}

		_fixture.stick_milli.store(accepted.load() < 6 ? 400 : 0);
		const TimeUs now = sampleInputs();

		if (bad_state.load()) { _sample.valid = false; }

		Report report{};
		OutputFrame frame{};
		const BoundOutputAcknowledgement received{{ack.session, ack.command_timestamp, ack.timestamp, ack.sequence}, revision};
		const auto result = _session.update(now, _input_permitted, _manual, _sample, _ack.revision(),
						    updated ? &received : nullptr, report, frame);

		if (result.acknowledged) { accepted.fetch_add(1); }

		if (result.status == OutputStatus::Waiting) { waiting.fetch_add(1); return; }

		if (result.status == OutputStatus::NoCommand) { return; }

		if (result.status != OutputStatus::Ok) {
			ack_source_fault.store(_session.acknowledgementSourceFault());
			session_fault.store(static_cast<int>(result.status));
			fault.store(true);
			return;
		}

		if (duplicate.fetch_and(0U)) { (void)_command.publish(_last); return; }

		if (report.state == State::Rate) { rates.fetch_add(1); }

		else if (report.state == State::Hold) { holds.fetch_add(1); }

		_last.timestamp = frame.timestamp_us;
		_last.session = frame.session;
		_last.sequence = frame.sequence;

		for (unsigned i = 0; i < 4; ++i) { _last.command[i] = frame.command[i]; }

		if (!_command.publish(_last)) { fault.store(true); }
	}
	const ObserverProfile _profile;
	Configuration _config;
	SelectedInputFixture &_fixture;
	SelectedStateInput _inputs{};
	SelectedStateGuard _health;
	bool _input_permitted{};
	StabilizedManualSession _session{};
	ManualSample _manual{};
	StateSample _sample{};
	stabilized_output_command_s _last{};
	FixturePublication<stabilized_output_command_s> _command{ORB_ID(stabilized_output_command)};
	uORB::ResearchSubscription _ack{ORB_ID(stabilized_output_ack)};
};

class Stall final : public px4::WorkItem
{
public:
	explicit Stall(const px4::wq_config_t &queue) : WorkItem("output_stall", queue) {}
	void Run() override { started.store(true); px4_usleep(250000); done.store(true); }
	px4::atomic_bool started{false}, done{false};
};
}

extern "C" __EXPORT int stabilized_output_test_main(int argc, char *argv[])
{
	if (argc != 3) { return 1; }

	ObserverProfile profile{};
	FILE *stream = fopen(argv[1], "r");
	const bool parsed = readObserverProfile(stream, profile);

	if (stream) { fclose(stream); }

	if (!parsed) { return 1; }

	for (unsigned i = 1; i <= kBankChannels; ++i) {
		char name[32];
		snprintf(name, sizeof(name), "PWM_MAIN_FUNC%u", i);
		parameter(name, i <= kCanardChannels ? static_cast<int>(OutputFunction::Research_Stabilized1) + i - 1 : 0);
		snprintf(name, sizeof(name), "PWM_MAIN_MIN%u", i); parameter(name, 500);
		snprintf(name, sizeof(name), "PWM_MAIN_MAX%u", i); parameter(name, 2500);
		snprintf(name, sizeof(name), "PWM_MAIN_CENT%u", i); parameter(name, 1500);
		snprintf(name, sizeof(name), "PWM_MAIN_DIS%u", i); parameter(name, 0);
	}

	parameter("PWM_MAIN_REV", 0);
	parameter("EKF2_MULTI_IMU", 0);
	parameter("EKF2_MULTI_MAG", 0);
	parameter("SENS_IMU_MODE", 1);
	parameter("SENS_MAG_MODE", 1);
	// Retained until this private process exits: no destructor race with the
	// old baseline's work-queue implementation is hidden by a timing sleep.
	Supervisor supervisor;
	const unsigned supervisor_revision = supervisor.advertise();
	check(supervisor_revision != 0, "explicit supervisor publisher binding");
	auto *inputs = new SelectedInputFixture(profile.control);
	inputs->start();
	check(wait([&]() { return inputs->ready.load(); }), "independent selected input publishers ready");
	auto *consumer = new Consumer(supervisor_revision, profile);
	auto *producer = new Producer(profile, *inputs);
	const unsigned first_source = producer->advertiseSource();
	check(first_source != 0, "explicit initial command publisher advertisement");
	consumer->start();
	check(wait([&]() { return consumer->revoked.load(); }), "initial disarmed sink confirmation");
	TimeUs session = hrt_absolute_time();
	check(session > kGrantWindow, "native clock permits expired request fixture");
	check(supervisor.send(session - kGrantWindow, first_source, consumer->ack_revision.load()), "publish coherent expired request");
	px4_usleep(30000);
	check(consumer->granted.load() == 0, "expired queued request cannot grant an owner");
	check(consumer->commands.load() == 0, "expired request leaves outputs disarmed");
	session = hrt_absolute_time();
	check(supervisor.send(session, first_source, consumer->ack_revision.load(), kConfigurationIdentity + 1),
	      "publish wrong configuration request");
	px4_usleep(30000);
	check(consumer->granted.load() == 0 && !producer->start(supervisor.handshake()), "configuration mismatch cannot grant or start");
	session = hrt_absolute_time();
	check(supervisor.send(session, first_source, consumer->ack_revision.load() + 1), "publish wrong acknowledgment binding");
	px4_usleep(30000);
	check(consumer->granted.load() == 0 && !producer->start(supervisor.handshake()), "acknowledgment mismatch cannot grant or start");
	session = hrt_absolute_time(); // A new request has a fresh monotonic identity.
	check(supervisor.sendAdmitted(session, first_source, consumer->ack_revision.load(), consumer->revoked.load()),
	      "publish admitted initial grant through shared request owner");

	check(wait([&]() { return consumer->granted.load() == session; }), "explicit consumer grant");
	check(supervisor.acceptAdmitted(), "shared request owner accepts coherent mixer-consumer grant");
	check(producer->start(supervisor.handshake()), "centered producer start with bound acknowledgment source");
	check(wait([&]() { return producer->accepted.load() >= 20 || producer->fault.load(); })
	&& producer->accepted.load() >= 20, "rate and hold commands acknowledged through actual mixer");
	check(producer->rates.load() > 0 && producer->holds.load() > 0, "both control states reach transport");
	Stall *stall = nullptr;
	const char *scenario = argv[2];

	if (strcmp(scenario, "producer_stall") == 0 || strcmp(scenario, "consumer_stall") == 0) {
		const bool owner_stall = strcmp(scenario, "producer_stall") == 0;
		stall = new Stall(owner_stall ? kProducerQueue : kConsumerQueue);
		stall->ScheduleNow();
		check(wait([&]() { return stall->started.load(); }), "queue stall starts");
		check(wait([&]() { return consumer->fallbacks.load() > 0; }), "independent expiry or resumed-consumer expiry");
		check(wait([&]() { return stall->done.load(); }), "stalled queue resumes");
		check(wait([&]() { return producer->fault.load(); }), "producer latches after queue stall");

	} else if (strcmp(scenario, "lost_ack") == 0) {
		consumer->drop_ack.store(true);
		check(wait([&]() { return producer->fault.load(); }), "lost ack faults producer");
		check(wait([&]() { return consumer->fallbacks.load() > 0; }), "lost ack cannot extend consumer lease");

	} else if (strcmp(scenario, "driver_reject") == 0) {
		consumer->fail_command.store(true);
		check(wait([&]() { return consumer->state.load() == static_cast<int>(DriverState::Fault); }), "driver rejection faults consumer");
		check(wait([&]() { return producer->fault.load(); }), "driver rejection never yields successful ack");

	} else if (strcmp(scenario, "slow_driver") == 0) {
		consumer->slow_command.store(true);
		check(wait([&]() { return consumer->state.load() == static_cast<int>(DriverState::Fault); }), "late driver completion faults consumer");
		check(wait([&]() { return producer->fault.load(); }), "late handling never yields timely ack");

	} else if (strcmp(scenario, "permission_loss") == 0) {
		consumer->permitted.store(false);
		check(wait([&]() { return consumer->state.load() == static_cast<int>(DriverState::Fault); }), "consumer permission loss");
		const auto commands = consumer->commands.load();
		consumer->permitted.store(true);
		px4_usleep(50000);
		check(consumer->commands.load() == commands, "restored permission cannot resume command");

	} else if (strcmp(scenario, "duplicate") == 0) {
		producer->duplicate.store(true);
		check(wait([&]() { return consumer->state.load() == static_cast<int>(DriverState::Fault); }), "duplicate transport frame latches");

	} else if (strcmp(scenario, "revoke_inflight") == 0) {
		consumer->revoke_inflight.store(true);
		check(wait([&]() { return consumer->phase_faults.load() == 1; }), "revoke arrives after command driver handling");
		check(wait([&]() { return producer->fault.load(); }), "invalidated command completion cannot acknowledge producer");
		check(wait([&]() { return consumer->revoked.load(); }), "subsequent disarm completes revoke");

	} else if (strcmp(scenario, "permission_inflight") == 0) {
		consumer->permission_inflight.store(true);
		check(wait([&]() { return consumer->phase_faults.load() == 1; }), "permission changes during split write");
		check(wait([&]() { return producer->fault.load(); }), "permission change suppresses command acknowledgment");
		const auto commands = consumer->commands.load();
		consumer->permitted.store(true);
		px4_usleep(50000);
		check(consumer->commands.load() == commands, "completion permission fault stays latched");

	} else if (strcmp(scenario, "mapping_change") == 0) {
		parameter("PWM_MAIN_MIN1", 600);
		consumer->refresh_params.store(1);
		check(wait([&]() { return consumer->rejected.load() > 0; }), "changed mixer calibration rejected before driver");
		check(wait([&]() { return producer->fault.load(); }), "mapping change cannot acknowledge prepared command");
		parameter("PWM_MAIN_MIN1", 500);
		consumer->refresh_params.store(1);

	} else if (strcmp(scenario, "routing_loss") == 0) {
		for (unsigned i = 1; i <= 4; ++i) {
			char name[24];
			snprintf(name, sizeof(name), "PWM_MAIN_FUNC%u", i);
			parameter(name, 0);
		}

		consumer->refresh_params.store(1);
		check(wait([&]() { return producer->fault.load(); }), "loss of all output functions revokes command");
		const auto before = consumer->completions.load();
		px4_usleep(30000);
		check(consumer->completions.load() > before, "disabled functions cannot suppress owned-bank disarm dispatch");

		for (unsigned i = 1; i <= 4; ++i) {
			char name[24];
			snprintf(name, sizeof(name), "PWM_MAIN_FUNC%u", i);
			parameter(name, static_cast<int>(OutputFunction::Research_Stabilized1) + i - 1);
		}

		consumer->refresh_params.store(1);

	} else if (strcmp(scenario, "actuator_override") == 0) {
		uORB::Publication<actuator_test_s> test{ORB_ID(actuator_test)};
		actuator_test_s message{};
		message.timestamp = hrt_absolute_time();
		message.action = actuator_test_s::ACTION_DO_CONTROL;
		message.function = static_cast<int>(OutputFunction::Research_Stabilized1);
		message.value = 1.0F;
		message.timeout_ms = 2000;
		check(test.publish(message), "publish competing actuator-test command");
		check(wait([&]() { return consumer->rejected.load() > 0; }), "actuator-test override rejected before driver");
		check(wait([&]() { return producer->fault.load(); }), "override cannot acknowledge research command");
		message.timestamp = hrt_absolute_time();
		message.action = actuator_test_s::ACTION_RELEASE_CONTROL;
		check(test.publish(message), "explicitly release test override");

	} else if (strcmp(scenario, "spare_routing") == 0) {
		parameter("PWM_MAIN_FUNC5", static_cast<int>(OutputFunction::Servo5));
		consumer->refresh_params.store(1);
		check(wait([&]() { return consumer->rejected.load() > 0; }), "assigned spare channel rejects entire bank");
		check(wait([&]() { return producer->fault.load(); }), "spare routing cannot acknowledge command");
		parameter("PWM_MAIN_FUNC5", 0);
		consumer->refresh_params.store(1);

	} else if (strcmp(scenario, "failsafe_change") == 0 || strcmp(scenario, "reversal_change") == 0) {
		const bool failsafe = strcmp(scenario, "failsafe_change") == 0;
		const char *name = failsafe ? "PWM_MAIN_FAIL1" : "PWM_MAIN_REV";
		parameter(name, failsafe ? 1500 : 1);
		consumer->refresh_params.store(1);
		check(wait([&]() { return consumer->rejected.load() > 0; }), "changed failsafe or reversal rejects bank");
		check(wait([&]() { return producer->fault.load(); }), "configuration drift cannot acknowledge command");
		parameter(name, 0);
		consumer->refresh_params.store(1);

	} else if (strcmp(scenario, "permission_before_write") == 0) {
		consumer->permission_before_write.store(true);
		check(wait([&]() { return consumer->phase_faults.load() == 1; }), "permission lost after preparation");
		check(wait([&]() { return consumer->rejected.load() > 0; }), "write-time permission rejects prepared values");
		check(wait([&]() { return producer->fault.load(); }), "permission rejection cannot acknowledge command");
		consumer->permitted.store(true);

	} else if (strcmp(scenario, "revoke_after_completion") == 0) {
		consumer->revoke_after_completion.store(true);
		check(wait([&]() { return consumer->phase_faults.load() == 1; }), "revoke before result collection");
		check(wait([&]() { return producer->fault.load(); }), "stored completion cannot acknowledge after revoke");
		check(wait([&]() { return consumer->revoked.load(); }), "fresh disarm required after stored completion revoke");

	} else if (strcmp(scenario, "stale_arming") == 0) {
		consumer->stale_arming.store(true);
		check(wait([&]() { return consumer->rejected.load() > 0; }), "stale arming state rejects prepared bank");
		check(wait([&]() { return producer->fault.load(); }), "stale arming cannot acknowledge command");
		consumer->stale_arming.store(false);

	} else if (strcmp(scenario, "delayed_prepared") == 0) {
		consumer->delayed_prepared.store(true);
		check(wait([&]() { return consumer->phase_faults.load() == 1; }), "candidate delayed after preparation");
		check(wait([&]() { return consumer->rejected.load() > 0; }), "expired candidate rejected despite fresh arming");
		check(wait([&]() { return producer->fault.load(); }), "expired candidate cannot acknowledge command");

	} else if (strcmp(scenario, "competing_servos") == 0) {
		uORB::Publication<actuator_servos_s> competing{ORB_ID(actuator_servos)};
		const auto before = producer->accepted.load();
		bool published = true;

		for (unsigned iteration = 0; iteration < 200; ++iteration) {
			actuator_servos_s message{};
			message.timestamp = message.timestamp_sample = hrt_absolute_time();

			for (float &value : message.control) { value = iteration % 2 ? -1.0F : 1.0F; }

			published = competing.publish(message) && published;
			px4_usleep(1000);
		}

		check(published, "publish competing generic servo stream");
		check(wait([&]() { return producer->accepted.load() >= before + 10 || producer->fault.load(); })
		&& producer->accepted.load() >= before + 10 && !producer->fault.load(),
		"generic servo traffic cannot replace dedicated controls");

	} else if (strcmp(scenario, "generic_remap") == 0 || strcmp(scenario, "wrong_research_route") == 0
		   || strcmp(scenario, "spare_research_route") == 0) {
		const bool spare = strcmp(scenario, "spare_research_route") == 0;
		const bool generic = strcmp(scenario, "generic_remap") == 0;
		const char *name = spare ? "PWM_MAIN_FUNC5" : "PWM_MAIN_FUNC1";
		const auto function = generic ? OutputFunction::Servo1 : spare ? OutputFunction::Research_Stabilized1 :
				      OutputFunction::Research_Stabilized2;
		parameter(name, static_cast<int>(function));
		consumer->refresh_params.store(1);
		check(wait([&]() { return consumer->rejected.load() > 0; }), "incorrect dedicated routing rejects entire bank");
		check(wait([&]() { return producer->fault.load(); }), "remapping cannot acknowledge stabilized command");
		parameter(name, spare ? 0 : static_cast<int>(OutputFunction::Research_Stabilized1));
		consumer->refresh_params.store(1);

	} else if (strcmp(scenario, "source_restart") == 0 || strcmp(scenario, "source_withdrawal") == 0) {
		if (strcmp(scenario, "source_restart") == 0) { producer->restart.store(1); }

		else { producer->withdraw.store(1); }

		check(wait([&]() { return producer->source_changed.load(); }), "publisher lifetime changes");
		check(wait([&]() { return consumer->source_fault.load(); }), "bound source lifetime loss latches");
		check(wait([&]() { return producer->fault.load(); }), "changed publisher cannot receive command acknowledgment");
		check(wait([&]() { return consumer->revoked.load(); }), "source loss reaches confirmed disarm");

	} else if (strcmp(scenario, "source_before_write") == 0 || strcmp(scenario, "source_inflight") == 0
		   || strcmp(scenario, "source_after_completion") == 0) {
		if (strcmp(scenario, "source_before_write") == 0) { consumer->source_before_write.store(true); }

		else if (strcmp(scenario, "source_inflight") == 0) { consumer->source_inflight.store(true); }

		else { consumer->source_after_completion.store(true); }

		check(wait([&]() { return consumer->phase_faults.load() == 1; }), "source changes at selected output phase");
		check(wait([&]() { return consumer->source_fault.load(); }), "phase-specific source loss latches");
		check(wait([&]() { return producer->fault.load(); }), "source-invalid completion cannot acknowledge producer");
		check(wait([&]() { return consumer->revoked.load(); }), "source-invalid completion requires fresh disarm");

	} else if (strcmp(scenario, "competing_grant") == 0) {
		const auto before = producer->accepted.load();
		check(supervisor.send(hrt_absolute_time(), first_source, consumer->ack_revision.load()), "publish competing request");
		px4_usleep(50000);
		check(consumer->granted.load() == session, "competing grant cannot replace active owner");
		check(wait([&]() { return producer->accepted.load() > before || producer->fault.load(); })
		&& producer->accepted.load() > before && !producer->fault.load(), "rejected grant preserves current owner");
		check(supervisor.send(session, first_source, consumer->ack_revision.load()), "restore current request observation");

	} else if (strcmp(scenario, "frame_revision_mismatch") == 0) {
		consumer->corrupt_revision.store(true);
		check(wait([&]() { return consumer->phase_faults.load() == 1; }), "copied frame revision mismatch injected");
		check(wait([&]() { return consumer->source_fault.load(); }), "payload without matching revision revokes owner");
		check(wait([&]() { return producer->fault.load(); }), "unbound payload cannot acknowledge command");

	} else if (strcmp(scenario, "supervisor_withdrawal") == 0 || strcmp(scenario, "supervisor_restart") == 0) {
		check(supervisor.withdraw(), "withdraw authorized supervisor");

		if (strcmp(scenario, "supervisor_restart") == 0) {
			check(supervisor.advertise() > supervisor_revision, "supervisor restart changes lifetime");
			check(supervisor.send(session, first_source, consumer->ack_revision.load()), "republish retained request after restart");
		}

		check(wait([&]() { return consumer->supervisor_fault.load(); }), "supervisor lifetime loss latches");
		check(wait([&]() { return consumer->revoked.load(); }), "supervisor loss reaches disarmed sink");
		check(wait([&]() { return producer->fault.load(); }), "supervisor loss stops acknowledgments");
		check(!producer->start(supervisor.handshake()), "revoked grant cannot restart controller");

	} else if (strcmp(scenario, "grant_response_restart") == 0) {
		const auto before = producer->accepted.load();
		consumer->restart_response.store(1);
		check(wait([&]() { return consumer->response_restarts.load() == 1; }), "grant response publisher restarts");
		check(!producer->start(supervisor.handshake()), "retained reply from replacement publisher cannot confirm old handshake");
		check(wait([&]() { return producer->accepted.load() > before || producer->fault.load(); })
		&& !producer->fault.load(), "rejected startup preserves running session");

	} else if (strcmp(scenario, "ack_restart") == 0) {
		consumer->restart_ack.store(1);
		check(wait([&]() { return consumer->ack_restarts.load() == 1; }), "acknowledgment publisher restarts");
		check(wait([&]() { return producer->fault.load(); }) && producer->ack_source_fault.load(),
		"changed ack lifetime disables producer/controller session");
		check(wait([&]() { return consumer->fallbacks.load() > 0; }), "ack source fault cannot renew output lease");

	} else if (strcmp(scenario, "ack_revision_mismatch") == 0) {
		producer->corrupt_ack_revision.store(true);
		check(wait([&]() { return producer->fault.load(); }) && producer->ack_source_fault.load(),
		"ack payload without bound lifetime disables session");
		check(wait([&]() { return consumer->fallbacks.load() > 0; }), "unbound ack cannot renew output lease");

	} else if (strcmp(scenario, "health_while_waiting") == 0) {
		const auto before = producer->waiting.load();
		consumer->drop_ack.store(true);
		check(wait([&]() { return producer->waiting.load() > before; }), "producer waits for missing acknowledgment");
		producer->bad_state.store(true);
		check(wait([&]() { return producer->fault.load(); })
		&& producer->session_fault.load() == static_cast<int>(OutputStatus::InvalidCommand),
		"invalid state while waiting faults before acknowledgment timeout");
		check(wait([&]() { return consumer->fallbacks.load() > 0; }), "health fault cannot renew output lease");

	} else if (strncmp(scenario, "selected_", 9) == 0) {
		const bool stalled = strcmp(scenario, "selected_producer_stall") == 0;

		if (stalled) {
			stall = new Stall(kProducerQueue);
			stall->ScheduleNow();
			check(wait([&]() { return stall->started.load(); }), "producer queue held during selected-health fault");
		}

		const auto disarms = consumer->disarms.load();

		if (strcmp(scenario, "selected_health") == 0 || stalled) { inputs->bad_health.store(true); }

		else if (strcmp(scenario, "selected_manual_loss") == 0) { inputs->omit_manual.store(true); }

		else if (strcmp(scenario, "selected_attitude_nan") == 0) { inputs->invalid_attitude.store(true); }

		else if (strcmp(scenario, "selected_source_restart") == 0) { inputs->restart_attitude.store(1); }

		else if (strcmp(scenario, "selected_device_change") == 0) { inputs->gyro.store(103); }

		else if (strcmp(scenario, "selected_estimator_regression") == 0) { inputs->regress_estimator.store(true); }

		else if (strcmp(scenario, "selected_profile_change") == 0) { parameter("EKF2_MULTI_IMU", 2); }

		else { check(false, "known selected-input fault"); }

		check(wait([&]() { return consumer->health_fault.load(); }), "independent consumer health guard latches");
		check(wait([&]() { return consumer->disarms.load() > disarms; }), "selected-input fault reaches disarmed sink");

		if (stalled) {
			check(!stall->done.load(), "consumer disarms while producer queue remains blocked");
			check(wait([&]() { return stall->done.load(); }), "producer queue resumes after independent disarm");
		}

		check(wait([&]() { return producer->fault.load(); }), "selected-input fault disables controller session");
		const auto commands = consumer->commands.load();
		inputs->bad_health.store(false);
		inputs->omit_manual.store(false);
		inputs->invalid_attitude.store(false);
		inputs->regress_estimator.store(false);
		inputs->gyro.store(101);
		parameter("EKF2_MULTI_IMU", 0);
		px4_usleep(30000);
		check(consumer->health_fault.load() && consumer->commands.load() == commands && producer->fault.load(),
		      "restored input facts cannot automatically recover either owner");

	} else if (strcmp(scenario, "revoke") != 0) { check(false, "known scenario"); }

	const TimeUs last_before_handoff = consumer->last_command.load();
	producer->stop.store(true);
	check(wait([&]() { return producer->done.load(); }), "producer stops before handoff");
	consumer->fail_command.store(false);
	consumer->slow_command.store(false);
	consumer->drop_ack.store(false);
	consumer->fail_disarm.store(true);
	const auto previous_revoke = consumer->revoke_processed.load();
	consumer->revoke_requested.store(true);
	check(wait([&]() { return consumer->revoke_processed.load() > previous_revoke; }), "consumer processes revoke request");
	px4_usleep(30000);
	check(!consumer->revoked.load(), "unhandled disarm cannot acknowledge revoke");

	if (!consumer->supervisor_fault.load()) {
		const TimeUs next = hrt_absolute_time();
		auto *successor = new Producer(profile, *inputs);
		const unsigned next_source = successor->advertiseSource();
		check(next_source > first_source, "successor advertises a new command source lifetime");
		check(supervisor.send(next, first_source, consumer->ack_revision.load()), "publish handoff with stale source");
		px4_usleep(30000);
		check(consumer->granted.load() == session, "new grant blocked until disarm handled");
		consumer->fail_disarm.store(false);
		check(wait([&]() { return consumer->revoked.load(); }), "revoke reaches output driver");
		px4_usleep(30000);
		check(consumer->granted.load() == session, "stale requested source cannot bind a replacement publisher");
		check(supervisor.send(next, next_source, consumer->ack_revision.load()), "publish coherent replacement request");
		check(wait([&]() { return consumer->granted.load() == next; }), "new session after confirmed revoke");
		check(successor->start(supervisor.handshake()), "fresh controller and bound producer after acknowledged handoff");
		check(wait([&]() { return successor->accepted.load() >= 10 || successor->fault.load(); })
		&& successor->accepted.load() >= 10, "successor runs after old queue stops and retained ack drains");
		check(successor->rates.load() > 0 && successor->holds.load() > 0, "successor starts with reset control state");
		successor->stop.store(true);
		check(wait([&]() { return successor->done.load(); }), "successor stops before consumer");

	} else {
		consumer->fail_disarm.store(false);
		check(supervisor.advertise() != 0, "replacement supervisor is advertised");
		check(supervisor.send(hrt_absolute_time(), first_source, consumer->ack_revision.load()), "replacement supervisor requests grant");
		px4_usleep(30000);
		check(consumer->granted.load() == session, "replacement supervisor requires explicit reauthorization");
	}

	consumer->stop.store(true);
	check(wait([&]() { return consumer->done.load(); }), "consumer stops with confirmed disarm");
	inputs->stop.store(true);
	check(wait([&]() { return inputs->done.load(); }), "selected input publisher queue stops");
	check(consumer->errors.load() == 0, "every captured channel matches expected PWM or disarm");
	PX4_INFO("OUTPUT_RESULT scenario=%s checks=%u failures=%u acks=%u",
		 scenario, checks, failures, consumer->acknowledgements.load());
	PX4_INFO("OUTPUT_CAPTURE commands=%u fallbacks=%u disarms=%u", consumer->commands.load(), consumer->fallbacks.load(),
		 consumer->disarms.load());
	PX4_INFO("OUTPUT_TIMING last=%llu fallback=%llu", (unsigned long long)last_before_handoff,
		 (unsigned long long)consumer->first_fallback.load());
	return failures ? 1 : 0;
}
