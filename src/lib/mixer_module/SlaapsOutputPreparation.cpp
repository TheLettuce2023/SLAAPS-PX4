#include "mixer_module.hpp"

#include <lib/slaaps/ManualPreparationEndpoint.hpp>
#include <lib/slaaps/ManualPermissionTransport.hpp>
#include <lib/slaaps/OutputRoutingObservation.hpp>
#include <lib/slaaps/Px4ConfigurationSource.hpp>
#include <uORB/topics/vehicle_status.h>
#include <uORB/topics/slaaps_status.h>

class SlaapsOutputPreparation
{
public:
	virtual ~SlaapsOutputPreparation() = default;
	virtual SlaapsOutputStatus poll(uint64_t now, uint64_t driver, bool ready, const actuator_armed_s &armed) = 0;
	virtual bool confirm(uint64_t now, uint64_t driver, bool ready, const actuator_armed_s &armed, bool success) = 0;
	virtual void confirmInhibited(bool success) = 0;
	virtual bool inhibitPending() const = 0;
	virtual uint16_t stopMask() const = 0;
	virtual float stopPwm(unsigned channel, bool termination) const = 0;
	virtual const slaaps::ManualBankOutput &candidate() const = 0;
	virtual uint32_t interval() const = 0;
	virtual bool servoBench() const = 0;
	virtual bool edfBench() const = 0;
	virtual bool retainedCommissioningStop(uint64_t driver) const = 0;
};

namespace
{
constexpr int32_t kSlaapsAirframe = 22001;
constexpr uint64_t kCommanderAgeUs = 1000000;
constexpr hrt_abstime kPreparationIntervalUs = 20000;

template<unsigned Bank>
class BankPreparation final : public SlaapsOutputPreparation
{
public:
	BankPreparation() : _endpoint(_bank)
	{
		_initialized = _parameters.initialize() && _bank.advertise() && _endpoint.advertise();
	}
	~BankPreparation() override { cancel(hrt_absolute_time()); }

	SlaapsOutputStatus poll(uint64_t now, uint64_t driver, bool ready, const actuator_armed_s &armed) override
	{
		const bool vehicle_fresh = readVehicle(now);
		const bool actually_armed = armed.armed || _vehicle_sample.arming_state == vehicle_status_s::ARMING_STATE_ARMED;
		const bool actuator_fresh = armed.timestamp && armed.timestamp <= now && now - armed.timestamp < kCommanderAgeUs;
		const bool stopped = actuator_fresh && ((vehicle_fresh && !actually_armed && !servoBench())
							|| armed.kill || armed.lockdown || armed.termination || armed.in_esc_calibration_mode
							|| (servoBench() && (actually_armed || !armed.prearmed)));

		if (stopped && (_claimed || _stop_pending)) {
			cancel(now);
			_stop_pending = true;
			return SlaapsOutputStatus::Inhibited;
		}

		if (stopped && _stop_complete && actually_armed) { return SlaapsOutputStatus::Inhibited; }

		// A completed ground stop cannot be revived by retained armed state,
		// stale authority or the old enable command. It owns no new write ticket.
		if (edfBench() && _stop_complete && (!vehicle_fresh || !actuator_fresh || actually_armed)) {
			return SlaapsOutputStatus::Inhibited;
		}

		// Releasing kill while still armed cannot resume the cancelled session.
		if (_stop_pending || (_stop_complete && actually_armed)) { return fail(now, true); }

		if (_stop_complete && vehicle_fresh && actuator_fresh && !actually_armed) { _stop_complete = false; }

		if (_fault_latched) { return SlaapsOutputStatus::RecoveryRequired; }

		if (!_initialized || !ready || !driver || !vehicle_fresh) { return fail(now, actually_armed); }

		if (!_claimed && !actually_armed && (!_listening || !servoBench())) {
			if (armed.kill || armed.lockdown || armed.termination || armed.in_esc_calibration_mode) { return fail(now, false); }

			return prepare(now, driver);
		}

		// No session may start from an already armed, unprepared bank.
		if (!_listening && !_bank.active()) { return fail(now, actually_armed); }

		// Claim the retained stop frame as soon as an already-prepared bank
		// observes arming, even while no command authority has been granted.
		_claimed = true;
		const bool awaiting_grant = !_bank.active();
		const bool granted = context(now, driver, ready, armed, awaiting_grant);

		if (!granted && !_waiting_for_grant) { return fail(now, true); }

		// Until activation consumes readiness, preserve every original prepare
		// check/deadline while waiting across the independent permission queue.
		if (!_bank.active() && _endpoint.pollLive(now, _configuration.configuration, _routing.inventory, false)
		    != slaaps::PreparationStatus::Ready) { return fail(now, true); }

		if (!granted) {
			if (_endpoint.processTransitionLive(now, _configuration.configuration, _routing.inventory, _permission, false)
			    == slaaps::PreparationStatus::Rejected) { return fail(now, true); }

			return SlaapsOutputStatus::Waiting;
		}

		if (_endpoint.processTransitionLive(now, _configuration.configuration, _routing.inventory, _permission)
		    == slaaps::PreparationStatus::Rejected) { return fail(now, true); }

		if (!_bank.active()) { return SlaapsOutputStatus::Waiting; }

		_candidate = _bank.dispatchLive(now, _permission, _routing.inventory, _request.binding);

		if (_bank.fault() != slaaps::ManualBankFault::None) { return fail(now, true); }

		if (!fresh(_vehicle_sample.timestamp, now) || !fresh(armed.timestamp, now)
		    || !fresh(_supervisor_sample.timestamp, now) || !routingFresh(now)) { return fail(now, true); }

		// Before the first command and after a confirmed retained command there
		// is no new ticket to write or acknowledge. dispatch still enforces expiry.
		return _candidate.command_valid && _bank.awaitingHandling()
		       ? SlaapsOutputStatus::Command : SlaapsOutputStatus::Waiting;
	}

	bool confirm(uint64_t now, uint64_t driver, bool ready, const actuator_armed_s &armed, bool success) override
	{
		// The physical callback may have taken time. Re-read independent authority,
		// configuration and inventory before acknowledging this exact saved ticket.
		if (!context(now, driver, ready, armed)
		    || !_bank.confirmHandledLive(now, _permission, _routing.inventory, _request.binding, _candidate, success)) {
			fail(now, true);
			return false;
		}

		return true;
	}

	void confirmInhibited(bool success) override
	{
		// Failed physical stop handling remains pending; only a later fresh
		// explicit inhibition may retry. There is no command acknowledgment.
		_stop_pending = !success;

		if (success) {
			_claimed = false;
			_fault_latched = false;
			_stop_complete = true;
		}
	}

	bool inhibitPending() const override { return _stop_pending; }
	uint16_t stopMask() const override { return _stop_mask; }
	float stopPwm(unsigned channel, bool termination) const override
	{
		return termination ? _stop_failsafe[channel] : _stop_disarmed[channel];
	}
	const slaaps::ManualBankOutput &candidate() const override { return _candidate; }
	bool servoBench() const override { return _request.mode == slaaps::ControlMode::ServoBench; }
	bool edfBench() const override { return _request.mode == slaaps::ControlMode::EdfBench; }
	bool retainedCommissioningStop(uint64_t driver) const override
	{
		return edfBench() && _claimed && _stop_pending && _stop_mask && driver && driver == _stop_driver
		       && _stop_session == _request.session && _stop_mode == _request.mode;
	}
	uint32_t interval() const override
	{
		return (_listening || _claimed) && _configuration_valid
		       ? static_cast<uint32_t>(slaaps::ManualBankPreparation::cycleMinimumUs(_configuration.configuration,
					       _request.mode)) : kPreparationIntervalUs;
	}
private:
	bool readVehicle(uint64_t &now)
	{
		const bool copied = _vehicle.copy(&_vehicle_sample);
		now = hrt_absolute_time();
		return copied && fresh(_vehicle_sample.timestamp, now);
	}
	static bool fresh(uint64_t timestamp, uint64_t now)
	{
		return timestamp && timestamp <= now && now - timestamp < kCommanderAgeUs;
	}

	bool configuration(bool force, slaaps::ControlMode mode)
	{
		parameter_update_s update{};
		const bool changed = _parameter_update.update(&update);

		// An active controller never adopts a parameter epoch in place.
		if (changed && _claimed) { _configuration_valid = false; return false; }

		if (force || changed || !_have_configuration) {
			uint32_t duration{};
			_have_configuration = _parameters.capture(_configuration, duration);
			_configuration_valid = _have_configuration
					       && _configuration.use == static_cast<int32_t>(slaaps::ManualBankPreparation::configurationUse(mode))
					       && slaaps::Px4ConfigurationSource::remembered(_configuration)
					       && slaaps::validate(_configuration.configuration, slaaps::ManualBankPreparation::configurationUse(mode)).valid()
					       && slaaps::ManualBankPreparation::activationTimingConfigured(_configuration.configuration, mode);
		}

		return _configuration_valid;
	}

	bool supervisor(uint64_t &now)
	{
		const auto read = _status_reader.read(_supervisor_sample);
		now = hrt_absolute_time();
		return read == slaaps::BoundRead::Data && fresh(_supervisor_sample.timestamp, now)
		       && _supervisor_sample.configuration_valid
		       && _supervisor_sample.configuration_use == static_cast<unsigned>(slaaps::ManualBankPreparation::configurationUse(_request.mode))
		       && _supervisor_sample.configuration_identity == _request.configuration
		       && _supervisor_sample.direct_actuator_modes_configured
		       && _supervisor_sample.manual_command_revision == _request.command_revision
		       && _supervisor_sample.manual_permission_revision == _request.permission_revision
		       && _supervisor_sample.mode_id[commissioning() ? 2 : 0] >= vehicle_status_s::NAVIGATION_STATE_EXTERNAL1
		       && _supervisor_sample.mode_id[commissioning() ? 2 : 0] <= vehicle_status_s::NAVIGATION_STATE_EXTERNAL8;
	}

	SlaapsOutputStatus prepare(uint64_t now, uint64_t driver)
	{
		unsigned revision{};
		uint64_t prepared{};

		const bool copied = _proposal.copyBound(&_proposal_wire, revision);
		now = hrt_absolute_time();

		if (!copied || !slaaps::decodeManualPreparation(now, slaaps_manual_preparation_s::REQUEST_BANK, _proposal_wire, _incoming, prepared)
		    || _incoming.binding.driver[Bank] != driver || !(_incoming.binding.banks & (1U << Bank))) { return fail(now, false); }

		const bool new_session = _incoming.session != _session;

		if (!configuration(new_session, _incoming.mode)
		    || _observation.captureLive(now, _routing, _capture_workspace) != slaaps::InventoryFault::None
		    || _routing.driver[Bank] != driver) { return fail(now, false); }

		if (!_listening || new_session) {
			cancel(now);

			if (_incoming.session <= _session) { return SlaapsOutputStatus::Inactive; }

			_session = _incoming.session;
			_request = _incoming;
			const unsigned status_revision = _status_lifetime.revision();
			const unsigned transition_revision = _transition_lifetime.revision();

			if (!_status_reader.bind(status_revision) || !supervisor(now)
			    || !_permissions.beginLive(now, _request, static_cast<uint64_t>(_configuration.configuration.output_lease_ms) * 1000)
			    || !_endpoint.bindTransitions(transition_revision) || !_endpoint.listen(now, revision)) { return fail(now, false); }

			_listening = true;
		}

		if (!supervisor(now) || _endpoint.pollLive(now, _configuration.configuration, _routing.inventory) != slaaps::PreparationStatus::Ready
		    || !fresh(_vehicle_sample.timestamp, now) || !fresh(_supervisor_sample.timestamp, now) || !routingFresh(now)) {
			return fail(now, false);
		}

		// Human arming may take seconds. Cancellation and transition-lifetime
		// loss remain live throughout that disarmed wait; ACTIVATE is deferred
		// without granting permission or writing an output.
		if (edfBench() && _endpoint.processTransitionLive(now, _configuration.configuration, _routing.inventory, _permission, false)
		    == slaaps::PreparationStatus::Rejected) { return fail(now, false); }

		// Retain the accepted physical stop frame across armed configuration
		// corruption. A future deliberate prepared session may replace it.
		_stop_mask = 0;
		_stop_driver = driver;
		_stop_session = _request.session;
		_stop_mode = _request.mode;

		for (unsigned i = 0; i < _routing.inventory.count; ++i) {
			const auto &channel = _routing.inventory.channels[i];

			if (static_cast<unsigned>(channel.bank) == Bank && channel.channel < slaaps::kMaximumChannelsPerBank
			    && channel.function >= slaaps::kCanardFunctionFirst && channel.function <= slaaps::kEdfFunction) {
				_stop_mask |= 1U << channel.channel;
				_stop_disarmed[channel.channel] = channel.disarmed_us;
				_stop_failsafe[channel.channel] = channel.failsafe_us;
			}
		}

		return SlaapsOutputStatus::Prepared;
	}

	bool context(uint64_t &now, uint64_t driver, bool ready, const actuator_armed_s &armed, bool awaiting_grant = false)
	{
		_permission = {};
		_waiting_for_grant = false;
		const bool vehicle_read = readVehicle(now);
		const bool configured = configuration(false, _request.mode) && slaaps::fingerprint(_configuration.configuration) == _request.configuration;
		const bool supervisor_read = supervisor(now);
		const bool routing_read = _observation.captureLive(now, _routing, _capture_workspace) == slaaps::InventoryFault::None;
		const bool vehicle_armed = vehicle_read && _vehicle_sample.arming_state == vehicle_status_s::ARMING_STATE_ARMED;
		const bool both_armed = vehicle_read && fresh(_vehicle_sample.timestamp, now) && fresh(armed.timestamp, now)
					&& armed.armed && vehicle_armed;
		const bool both_disarmed = vehicle_read && fresh(_vehicle_sample.timestamp, now) && fresh(armed.timestamp, now)
					   && !armed.armed && _vehicle_sample.arming_state == vehicle_status_s::ARMING_STATE_DISARMED;
		const bool locally_enabled = servoBench() ? both_disarmed && armed.prearmed : both_armed;
		const auto permission_read = awaiting_grant ? _permissions.awaitGrantLive(now, _permission, locally_enabled)
					     : _permissions.readLive(now, _permission);
		// All independent copies are complete. Recheck the retained samples at
		// one final decision time; copying a later topic never renews an earlier one.
		now = hrt_absolute_time();
		const bool permission_fresh = permission_read == slaaps::BoundRead::Data
					      && _permission.timestamp_us <= now && now < _permission.deadline_us;
		const bool granted_armed = _permission.armed;
		const bool granted_prearmed = _permission.prearmed;

		// Missing/denied permission never synthesizes disarm. Initial bounded
		// waiting sends no command; permission loss after a grant needs recovery.
		_permission.armed = armed.armed || vehicle_armed || (servoBench() && granted_armed);
		_permission.prearmed = granted_prearmed && both_disarmed && armed.prearmed;
		_permission.kill |= armed.kill;
		_permission.lockdown |= armed.lockdown;
		_permission.termination |= armed.termination;

		if (!servoBench()) { _permission.flight_admitted &= granted_armed && both_armed; }

		if (edfBench()) { _permission.commissioning_admitted &= granted_armed && both_armed; }

		const bool local = ready && driver == _request.binding.driver[Bank]
				   && vehicle_read && fresh(_vehicle_sample.timestamp, now) && fresh(armed.timestamp, now)
				   && !armed.kill && !armed.lockdown && !armed.termination && !armed.in_esc_calibration_mode
				   && configured && supervisor_read && fresh(_supervisor_sample.timestamp, now)
				   && _vehicle_sample.nav_state == _supervisor_sample.mode_id[commissioning() ? 2 : 0]
				   && routing_read && routingFresh(now)
				   && _routing.driver[Bank] == driver;

		if (permission_fresh && local) {
			// The local grant cannot outlive any copied authority sample. The
			// bank rechecks this shortened lease after its later command copy,
			// including immediately before a handled acknowledgment.
			const uint64_t commander_stamp = math::min(_vehicle_sample.timestamp,
							 math::min(armed.timestamp, _supervisor_sample.timestamp));
			const uint64_t report_stamp = math::min(_routing.reported_us[0], _routing.reported_us[1]);

			if (commander_stamp <= UINT64_MAX - kCommanderAgeUs) {
				_permission.deadline_us = math::min(_permission.deadline_us, commander_stamp + kCommanderAgeUs);
			}

			if (report_stamp <= UINT64_MAX - slaaps::kMaximumOutputReportAgeUs) {
				_permission.deadline_us = math::min(_permission.deadline_us, report_stamp + slaaps::kMaximumOutputReportAgeUs);
			}
		}

		_permission.healthy &= permission_fresh && local;
		_waiting_for_grant = awaiting_grant && permission_read == slaaps::BoundRead::Waiting && local && now < _request.deadline_us;
		return permission_fresh && slaaps::ManualBankPreparation::permitted(_permission, _request.mode);
	}

	bool routingFresh(uint64_t now) const
	{
		if (!_routing.captured_us || _routing.captured_us > now) { return false; }

		for (unsigned bank = 0; bank < 2; ++bank) {
			const auto timestamp = _routing.reported_us[bank];

			if (!timestamp || timestamp > now || now - timestamp >= slaaps::kMaximumOutputReportAgeUs) { return false; }
		}

		return true;
	}

	SlaapsOutputStatus fail(uint64_t now, bool armed)
	{
		cancel(now);

		// Ground commissioning never owns an airborne recovery policy. Any loss
		// ends its ticket and requests the retained calibrated off frame.
		if (commissioning() && _claimed) { _stop_pending = true; return SlaapsOutputStatus::Inhibited; }

		if (armed) { _claimed = true; _fault_latched = true; return SlaapsOutputStatus::RecoveryRequired; }

		return SlaapsOutputStatus::Inactive;
	}

	void cancel(uint64_t now)
	{
		if (_listening) { _endpoint.cancel(now); }

		_bank.revoke();
		_permissions.stop();
		_status_reader.revoke();
		_listening = false;
	}
	bool commissioning() const { return slaaps::ManualBankPreparation::commissioningMode(_request.mode); }
	slaaps::ManualBankTransport<Bank> _bank{};
	slaaps::ManualPreparationEndpoint<Bank> _endpoint;
	slaaps::ManualPermissionSubscription _permissions{};
	slaaps::EdfPermission _permission{};
	slaaps::ManualBankOutput _candidate{};
	slaaps::Px4ConfigurationSource _parameters{};
	slaaps::ParameterSnapshot _configuration{};
	slaaps::OutputRoutingObservation _observation{};
	slaaps::OutputRoutingObservation::Workspace _capture_workspace{};
	slaaps::ReportedRoutingSnapshot _routing{};
	// Driver-queue scratch: read/decode failures return before retained data use.
	vehicle_status_s _vehicle_sample{};
	slaaps_status_s _supervisor_sample{};
	slaaps_manual_preparation_s _proposal_wire{};
	slaaps::ManualPreparationRequest _incoming{}, _request{};
	uORB::ResearchSubscription _proposal{ORB_ID::slaaps_manual_prepare};
	uORB::ResearchSubscription _transition_lifetime{ORB_ID::slaaps_manual_transition};
	uORB::ResearchSubscription _status_lifetime{ORB_ID::slaaps_status};
	slaaps::BoundSubscription<slaaps_status_s> _status_reader{ORB_ID::slaaps_status};
	uORB::Subscription _vehicle{ORB_ID(vehicle_status)};
	uORB::Subscription _parameter_update{ORB_ID(parameter_update)};
	uint64_t _session{};
	uint64_t _stop_driver{}, _stop_session{};
	slaaps::ControlMode _stop_mode{slaaps::ControlMode::None};
	bool _initialized{}, _listening{}, _have_configuration{}, _configuration_valid{}, _claimed{}, _fault_latched{}, _waiting_for_grant{};
	bool _stop_pending{}, _stop_complete{};
	uint16_t _stop_mask{};
	float _stop_disarmed[slaaps::kMaximumChannelsPerBank] {}, _stop_failsafe[slaaps::kMaximumChannelsPerBank] {};
};
}

void MixingOutput::initializeSlaapsPreparation()
{
	if (_slaaps_preparation) { return; }

	int32_t airframe{};

	if (param_get(param_find("SYS_AUTOSTART"), &airframe) != 0 || airframe != kSlaapsAirframe) { return; }

	if (strcmp(_param_prefix, "PWM_MAIN") == 0) { _slaaps_preparation = new BankPreparation<0>(); }

	else if (strcmp(_param_prefix, "PWM_AUX") == 0) { _slaaps_preparation = new BankPreparation<1>(); }

	if (_slaaps_preparation) {
		_lowrate_schedule_interval = kPreparationIntervalUs;
		_need_function_update = true;
	}
}

bool MixingOutput::slaapsLocallyReady() const
{
	const auto &report = _slaaps_last_inventory;
	bool ready = !_slaaps_inventory_invalidated && report.ready && !_need_function_update && report.reversed_mask == _reverse_output_mask;

	for (unsigned i = 0; i < report.channel_count && ready; ++i) {
		ready = report.function[i] == static_cast<uint16_t>(_function_assignment[i])
			&& report.minimum_us[i] == _min_value[i] && report.center_us[i] == _center_value[i]
			&& report.maximum_us[i] == _max_value[i] && report.disarmed_us[i] == _disarmed_value[i]
			&& report.failsafe_us[i] == _failsafe_value[i];
	}

	return ready;
}

bool MixingOutput::updateSlaapsPreparation()
{
	if (!_slaaps_preparation) {
		// Claim the dedicated airframe before its first inventory report. A zero
		// driver token remains unready; it never permits stock output meanwhile.
		initializeSlaapsPreparation();

		if (!_slaaps_preparation) {
			// Unsupported banks or allocation failure cannot open another output
			// owner. Other airframes continue through their normal mixer path.
			int32_t airframe{};
			const bool dedicated = param_get(param_find("SYS_AUTOSTART"), &airframe) == 0 && airframe == kSlaapsAirframe;

			if (dedicated && _scheduling_policy == SchedulingPolicy::Auto) {
				_interface.ScheduleOnInterval(kPreparationIntervalUs);
			}

			return dedicated;
		}
	}

	actuator_armed_s current = _armed;
	const bool current_known = _armed_sub.copy(&current);
	_slaaps_output_status = _slaaps_preparation->poll(hrt_absolute_time(), _slaaps_driver_instance,
				current_known && slaapsLocallyReady(), current);
	// Consume a driver mutation only after the bank has observed its denial.
	_slaaps_inventory_invalidated = false;
	const uint32_t interval = _slaaps_preparation->interval();

	if (_lowrate_schedule_interval != interval) {
		_lowrate_schedule_interval = interval;

		if (_scheduling_policy == SchedulingPolicy::Auto) { _interface.ScheduleOnInterval(interval); }
	}

	// Inactive/prepared is still exclusively owned. Falling through would let
	// stock function providers or actuator_test drive a mixed invalid mapping.
	if (_slaaps_output_status != SlaapsOutputStatus::Command && _slaaps_output_status != SlaapsOutputStatus::Inhibited) { return true; }

	if (_slaaps_output_status == SlaapsOutputStatus::Inhibited && !_slaaps_preparation->inhibitPending()) { return true; }

	// Recheck complete independent authority immediately before writing.
	// Retained dispatch preserves the pending ticket and never renews its lease.
	const bool final_authority_known = _armed_sub.copy(&current);

	if (_slaaps_output_status == SlaapsOutputStatus::Command) {
		_slaaps_output_status = _slaaps_preparation->poll(hrt_absolute_time(), _slaaps_driver_instance,
					final_authority_known && slaapsLocallyReady(), current);
	}

	const bool command = _slaaps_output_status == SlaapsOutputStatus::Command;
	const bool bench = _slaaps_preparation->servoBench();
	// Only the accepted EDF commissioning owner may service its immutable off
	// ticket without live authority. It cannot acknowledge or authorize a command,
	// adopt another driver/routing or relax any airborne Manual loss behavior.
	const bool retained_stop = !command && _slaaps_preparation->retainedCommissioningStop(_slaaps_driver_instance);
	const bool inhibit = current.kill || current.lockdown || current.termination || current.in_esc_calibration_mode
			     || (bench ? current.armed || !current.prearmed : !current.armed);
	const uint64_t before_write = hrt_absolute_time();
	const bool authority_fresh = final_authority_known && current.timestamp && current.timestamp <= before_write
				     && before_write - current.timestamp < kCommanderAgeUs;

	if ((!command && _slaaps_output_status != SlaapsOutputStatus::Inhibited)
	    || (command && (inhibit || _slaaps_preparation->candidate().deadline_us <= before_write))
	    || (!command && !inhibit && !bench && !retained_stop)
	    || (!retained_stop && !authority_fresh)) {
		_slaaps_output_status = SlaapsOutputStatus::RecoveryRequired;
		return true;
	}

	_slaaps_writing_mask = command ? _slaaps_preparation->candidate().channel_mask : _slaaps_preparation->stopMask();

	for (unsigned i = 0; i < _max_num_outputs; ++i) {
		_current_output_value[i] = command ? _slaaps_preparation->candidate().pwm_us[i]
					   : ((_slaaps_writing_mask & (1U << i)) ? _slaaps_preparation->stopPwm(i, current.termination) : 0.f);
	}

	const bool handled = _interface.updateOutputs(_current_output_value, _max_num_outputs, 1);
	_slaaps_writing_mask = 0;

	if (command) {
		const bool post_write_known = _armed_sub.copy(&current);

		if (!_slaaps_preparation->confirm(hrt_absolute_time(), _slaaps_driver_instance,
						  post_write_known && slaapsLocallyReady(), current, handled)) {
			_slaaps_output_status = handled ? SlaapsOutputStatus::RecoveryRequired : SlaapsOutputStatus::HandlingRejected;
		}
	}

	if (!command) {
		_slaaps_preparation->confirmInhibited(handled);

		if (!handled) { _slaaps_output_status = SlaapsOutputStatus::HandlingRejected; }
	}

	if (handled) {
		actuator_outputs_s outputs{};
		setAndPublishActuatorOutputs(_max_num_outputs, outputs);
	}

	return true;
}

void MixingOutput::destroySlaapsPreparation()
{
	delete _slaaps_preparation;
	_slaaps_preparation = nullptr;
}
