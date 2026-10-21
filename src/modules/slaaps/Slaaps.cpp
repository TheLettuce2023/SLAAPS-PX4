#include "Px4ConfigurationSource.hpp"
#include <lib/slaaps/OutputRoutingObservation.hpp>
#include <lib/slaaps/ManualExecution.hpp>
#include <lib/research_stabilized_manual/SelectedStateObservation.hpp>

#include <px4_platform_common/module.h>
#include <px4_platform_common/px4_work_queue/ScheduledWorkItem.hpp>
#include <drivers/drv_hrt.h>
#include <uORB/Publication.hpp>
#include <uORB/Subscription.hpp>
#include <uORB/topics/arming_check_request.h>
#include <uORB/topics/arming_check_reply.h>
#include <uORB/topics/parameter_update.h>
#include <uORB/topics/register_ext_component_request.h>
#include <uORB/topics/register_ext_component_reply.h>
#include <uORB/topics/setpoint_config.h>
#include <uORB/topics/setpoint_config_reply.h>
#include <uORB/topics/unregister_ext_component.h>
#include <uORB/topics/vehicle_status.h>
#include <uORB/topics/slaaps_status.h>
#include <cstring>

namespace
{
constexpr unsigned kModeCount = 3;
constexpr unsigned kManualMode = 0;
constexpr unsigned kAccelerationMode = 1;
constexpr unsigned kBenchMode = 2;
constexpr hrt_abstime kIntervalUs = slaaps::kSupervisorIdleIntervalUs;
constexpr hrt_abstime kRetryUs = 500000;
constexpr hrt_abstime kMaximumStatusAgeUs = 1000000;
constexpr px4::wq_config_t kSupervisorQueue{"wq:slaaps", 6500, -5};
constexpr const char *kModeNames[kModeCount] = {"SLAAPS Manual", "SLAAPS Acceleration", "SLAAPS Servo Bench"};
}

// Native configuration and mode owner. Active control/driver attachment is a
// separate implementation gate: this increment publishes no actuator command.
class Slaaps final : public ModuleBase, public px4::ScheduledWorkItem
{
public:
	static Descriptor desc;
	Slaaps() : ScheduledWorkItem(MODULE_NAME, kSupervisorQueue) {}
	static int task_spawn(int argc, char *argv[]);
	static int custom_command(int argc, char *argv[]) { return print_usage("unknown command"); }
	static int print_usage(const char *reason = nullptr);
	int print_status() override;
private:
	struct Mode {
		uint64_t request{};
		hrt_abstime last_request{};
		int8_t id{-1};
		int8_t check{-1};
		bool configured{};
	};
	void Run() override;
	void updateRegistration(hrt_abstime now);
	void updateParameters(hrt_abstime now, bool changed);
	void answerChecks(hrt_abstime now);
	void updateRouting(hrt_abstime now);
	void publishStatus(hrt_abstime now);
	void updateSchedule();
	slaaps::Px4ConfigurationSource _parameters{};
	slaaps::ConfigurationStore _configuration{};
	// Reverse destruction order keeps the shared owner alive through worker stop.
	slaaps::EdfOwner _edf{};
	slaaps::ManualWorkerTransport _manual{_edf};
	// Populated only by the pending driver/flight admission transaction. Defaults
	// deliberately confer no permission; mode selection never fills these in.
	slaaps::EdfPermission _manual_permission{};
	slaaps::ManualOutputBinding _manual_binding{};
	slaaps::SelectedInputPolicy _manual_input_policy{};
	slaaps::ManualWorkerStatus _manual_status{slaaps::ManualWorkerStatus::Inactive};
	uint32_t _schedule_interval_us{kIntervalUs};
	hrt_abstime _last_maintenance{};
	research_stabilized_manual::SelectedStateObservation _inputs{};
	bool _input_metadata_valid{};
	uint32_t _input_epoch{};
	Mode _modes[kModeCount] {};
	uint32_t _snapshot_duration{};
	int32_t _last_apply{};
	int32_t _selected_use{};
	slaaps::ConfigurationFault _configuration_fault{slaaps::ConfigurationFault::NoCandidate};
	unsigned _stop_index{};
	bool _registration_fault{};
	bool _routing_valid{};
	slaaps::InventoryFault _inventory_fault{slaaps::InventoryFault::MissingBank};
	slaaps::RoutingFault _routing_fault{slaaps::RoutingFault::InvalidInventory};
	slaaps::OutputRoutingObservation _output_observation{};
	slaaps::ReportedRoutingSnapshot _routing_snapshot{};
	uORB::Subscription _parameter_update{ORB_ID(parameter_update)};
	uORB::Subscription _vehicle_status{ORB_ID(vehicle_status)};
	uORB::Subscription _registration_reply{ORB_ID(register_ext_component_reply)};
	uORB::Subscription _setpoint_reply{ORB_ID(setpoint_config_reply)};
	uORB::Subscription _arming_request{ORB_ID(arming_check_request)};
	uORB::Publication<register_ext_component_request_s> _registration{ORB_ID(register_ext_component_request)};
	uORB::Publication<setpoint_config_s> _setpoint{ORB_ID(setpoint_config)};
	uORB::Publication<arming_check_reply_s> _arming_reply{ORB_ID(arming_check_reply)};
	uORB::Publication<unregister_ext_component_s> _unregister{ORB_ID(unregister_ext_component)};
	uORB::Publication<slaaps_status_s> _status{ORB_ID(slaaps_status)};
};

ModuleBase::Descriptor Slaaps::desc{task_spawn, custom_command, print_usage};

void Slaaps::updateRegistration(hrt_abstime now)
{
	register_ext_component_reply_s reply{};

	while (_registration_reply.update(&reply)) {
		for (auto &mode : _modes) {
			if (reply.request_id != mode.request) { continue; }

			if (!reply.success || reply.arming_check_id < 0 || reply.arming_check_id >= 8
			    || reply.mode_id < vehicle_status_s::NAVIGATION_STATE_EXTERNAL1
			    || reply.mode_id > vehicle_status_s::NAVIGATION_STATE_EXTERNAL8) {
				_registration_fault = true;
				continue;
			}

			if (mode.id >= 0 && (mode.id != reply.mode_id || mode.check != reply.arming_check_id)) {
				_registration_fault = true;
				continue;
			}

			mode.id = reply.mode_id;
			mode.check = reply.arming_check_id;
			mode.last_request = 0;
		}
	}

	setpoint_config_reply_s configuration_reply{};

	while (_setpoint_reply.update(&configuration_reply)) {
		for (auto &mode : _modes) {
			if (mode.id >= 0 && configuration_reply.source_id == mode.id
			    && configuration_reply.type == setpoint_config_s::TYPE_DIRECT_ACTUATORS) {
				mode.configured = configuration_reply.result == setpoint_config_reply_s::RESULT_SUCCESS;

				if (!mode.configured) { _registration_fault = true; }
			}
		}
	}

	if (_registration_fault) { return; }

	// One outstanding registration/configuration request at a time. The
	// registration queues hold two entries, so never burst all three names.
	for (unsigned i = 0; i < kModeCount; ++i) {
		auto &mode = _modes[i];

		if (mode.configured) { continue; }

		if (mode.last_request != 0 && now - mode.last_request < kRetryUs) { return; }

		if (mode.id < 0) {
			register_ext_component_request_s request{};
			request.timestamp = now;
			request.request_id = mode.request;
			strncpy(request.name, kModeNames[i], sizeof(request.name) - 1);
			request.px4_ros2_api_version = register_ext_component_request_s::LATEST_PX4_ROS2_API_VERSION;
			request.register_arming_check = true;
			request.register_mode = true;
			_registration.publish(request);

		} else {
			setpoint_config_s request{};
			request.timestamp = now;
			request.source_id = mode.id;
			request.type = setpoint_config_s::TYPE_DIRECT_ACTUATORS;
			request.should_apply = true;
			// The SLAAPS consumer must enforce its own lease. Standard
			// actuator topics are not the prepared-value ownership transport.
			request.timeout_ms = 0;
			_setpoint.publish(request);
		}

		mode.last_request = now;
		return;
	}
}

void Slaaps::updateParameters(hrt_abstime now, bool changed)
{
	if (!changed) { return; }

	vehicle_status_s vehicle{};
	const bool disarmed = _vehicle_status.copy(&vehicle) && vehicle.timestamp != 0 && now >= vehicle.timestamp
			      && now - vehicle.timestamp <= kMaximumStatusAgeUs
			      && vehicle.arming_state == vehicle_status_s::ARMING_STATE_DISARMED;

	if (!disarmed) {
		_configuration.invalidate();
		_configuration_fault = slaaps::ConfigurationFault::Active;
		return;
	}

	slaaps::ParameterSnapshot snapshot{};

	if (!_parameters.capture(snapshot, _snapshot_duration)) {
		_configuration.invalidate();
		_configuration_fault = slaaps::ConfigurationFault::ParameterRange;
		return;
	}

	_configuration.observe(snapshot.configuration);

	if (snapshot.use != _selected_use) { _configuration.invalidate(); }

	if (!_configuration.hasCandidate()) { _configuration_fault = slaaps::ConfigurationFault::NoCandidate; }

	if (snapshot.apply_counter <= _last_apply) { return; }

	_last_apply = snapshot.apply_counter;
	_configuration.disable();

	if (snapshot.use < 0 || snapshot.use > 1) {
		_configuration_fault = slaaps::ConfigurationFault::ParameterRange;
		PX4_WARN("invalid SLA_CFG_KIND");
		return;
	}

	_selected_use = snapshot.use;
	const auto result = _configuration.commit(snapshot.configuration,
			    _selected_use == 0 ? slaaps::ConfigurationUse::Bench : slaaps::ConfigurationUse::StabilizedManual);
	_configuration_fault = result.fault;

	if (!result.valid()) {
		PX4_WARN("configuration rejected: fault=%u parameter=%s", static_cast<unsigned>(result.fault),
			 result.parameter ? result.parameter : "cross-field constraint");

	} else {
		PX4_INFO("configuration applied; output integration unavailable");
	}
}

void Slaaps::answerChecks(hrt_abstime now)
{
	arming_check_request_s request{};

	if (!_arming_request.update(&request) || request.timestamp == 0 || request.timestamp > now
	    || now - request.timestamp > kRetryUs) { return; }

	for (unsigned i = 0; i < kModeCount; ++i) {
		if (_modes[i].check < 0) { continue; }

		arming_check_reply_s reply{};
		reply.timestamp = now;
		reply.request_id = request.request_id;
		reply.registration_id = _modes[i].check;
		reply.mode_req_angular_velocity = i != kBenchMode;
		reply.mode_req_attitude = i != kBenchMode;
		reply.mode_req_manual_control = i != kAccelerationMode;
		reply.mode_req_prevent_arming = i == kBenchMode;
		// Named development ports are visible, but neither a registered name
		// nor validated parameters grant output/flight permission.
		reply.can_arm_and_run = false;
		_arming_reply.publish(reply);
	}
}

void Slaaps::updateRouting(hrt_abstime now)
{
	_routing_valid = false;
	_routing_fault = slaaps::RoutingFault::InvalidInventory;
	// Both physical banks must report, including a bank without assignments.
	// Only a successful stable capture can replace the retained snapshot.
	_inventory_fault = _output_observation.capture(now, _routing_snapshot);

	if (_inventory_fault != slaaps::InventoryFault::None || !_configuration.hasCandidate()) { return; }

	slaaps::RoutingPlan plan{};
	const auto result = slaaps::makeRoutingPlan(_configuration.configuration(), _routing_snapshot.inventory, _selected_use != 0, plan);
	_routing_fault = result.fault;
	_routing_valid = result.valid();
}

void Slaaps::publishStatus(hrt_abstime now)
{
	slaaps_status_s status{};
	status.timestamp = now;
	status.manual_command_revision = _manual.commandRevision();
	status.manual_worker_active = _manual.active();
	status.schedule_interval_us = _schedule_interval_us;
	status.manual_worker_status = static_cast<uint8_t>(_manual_status);
	status.input_metadata_valid = _input_metadata_valid;
	status.input_source_epoch = _input_epoch;
	status.configuration_valid = _configuration.hasCandidate();
	status.configuration_identity = status.configuration_valid ? _configuration.identity() : 0;
	status.configuration_fault = static_cast<uint8_t>(_configuration_fault);
	status.configuration_use = _selected_use;
	status.apply_counter = _last_apply;
	status.snapshot_duration_us = _snapshot_duration;
	status.routing_snapshot_valid = _routing_valid && status.configuration_valid;
	status.inventory_fault = static_cast<uint8_t>(_inventory_fault);
	status.routing_fault = static_cast<uint8_t>(_routing_fault);
	status.direct_actuator_modes_configured = !_registration_fault;

	for (unsigned i = 0; i < kModeCount; ++i) {
		status.mode_id[i] = _modes[i].id;
		status.direct_actuator_modes_configured &= _modes[i].configured;
	}

	status.unavailable_reason = !status.direct_actuator_modes_configured ? slaaps_status_s::UNAVAILABLE_REGISTRATION :
				    status.configuration_valid ? slaaps_status_s::UNAVAILABLE_OUTPUT_INTEGRATION :
				    slaaps_status_s::UNAVAILABLE_CONFIGURATION;
	// can_run_mask/can_arm_mask remain zero until the corresponding production
	// ownership and admission path is attached and verified.
	_status.publish(status);
}

void Slaaps::updateSchedule()
{
	uint32_t interval = slaaps::manualScheduleInterval(_configuration.configuration(), _manual.active());

	if (!interval) {
		_manual.stop();
		_manual_status = slaaps::ManualWorkerStatus::ConfigurationRejected;
		interval = kIntervalUs;
	}

	if (_schedule_interval_us != interval) {
		_schedule_interval_us = interval;
		ScheduleOnInterval(interval);
	}
}

void Slaaps::Run()
{
	const hrt_abstime now = hrt_absolute_time();

	if (should_exit()) {
		if (_manual.active()) { _manual.stop(); }

		_edf.revoke();
		_configuration.disable();
		_manual_status = slaaps::ManualWorkerStatus::Inactive;
		updateSchedule();
		publishStatus(now);

		// The unregister topic has one slot. Drain one request per scheduled
		// iteration instead of overwriting all but the last unregister.
		if (_stop_index < kModeCount) {
			auto &mode = _modes[_stop_index];

			if (mode.id >= 0) {
				unregister_ext_component_s request{};
				request.timestamp = now;
				strncpy(request.name, kModeNames[_stop_index], sizeof(request.name) - 1);
				request.arming_check_id = mode.check;
				request.mode_id = mode.id;
				request.mode_executor_id = -1;
				_unregister.publish(request);
			}

			++_stop_index;
			return;
		}

		ScheduleClear();
		exit_and_cleanup(desc);
		return;
	}

	parameter_update_s update{};
	updateParameters(now, _parameter_update.update(&update));

	if ((!_configuration.eligible() || _selected_use != 1) && _manual.active()) {
		_manual.stop();
		_manual_status = slaaps::ManualWorkerStatus::ConfigurationRejected;
	}

	const bool maintenance_due = slaaps::supervisorMaintenanceDue(now, _last_maintenance);

	if (_manual.active() || maintenance_due) {
		_input_metadata_valid = false;
		_input_epoch = 0;

		if (_configuration.hasCandidate() && _selected_use == 1) {
			const bool active = _manual.active();
			const auto observation = _inputs.capture(active ? _manual_input_policy.maximum_status_age_us : kMaximumStatusAgeUs,
						 active ? _manual_input_policy.maximum_permission_age_us : kMaximumStatusAgeUs);
			_input_metadata_valid = observation.state.valid && observation.permission_fresh;
			_input_epoch = observation.state.source_epoch;

			if (active) {
				vehicle_status_s vehicle{};
				const bool have_vehicle = _vehicle_status.copy(&vehicle);
				const auto decision_time = hrt_absolute_time(); // Evaluate after both sequential snapshots.
				const auto &mode = _modes[kManualMode];
				const bool selected = !_registration_fault && mode.configured && mode.id >= 0 && have_vehicle
						      && vehicle.timestamp && vehicle.timestamp <= decision_time
						      && decision_time - vehicle.timestamp <= kMaximumStatusAgeUs
						      && vehicle.nav_state == mode.id && vehicle.arming_state == vehicle_status_s::ARMING_STATE_ARMED;
				_manual_status = slaaps::executeManual(_manual, decision_time, _configuration.eligible(), selected,
								       _manual_permission, observation, _manual_binding);
			}
		}
	}

	updateSchedule();
	answerChecks(hrt_absolute_time());

	if (maintenance_due) {
		const auto maintenance_time = hrt_absolute_time();
		updateRouting(maintenance_time);
		updateRegistration(maintenance_time);
		publishStatus(hrt_absolute_time());
		_last_maintenance = maintenance_time;
	}
}

int Slaaps::task_spawn(int argc, char *argv[])
{
	if (argc != 1) { return print_usage("start takes no arguments"); }

	auto *instance = new Slaaps();

	if (!instance) { return PX4_ERROR; }

	slaaps::ParameterSnapshot initial{};

	if (!instance->_parameters.initialize() || !instance->_parameters.capture(initial, instance->_snapshot_duration)) {
		delete instance;
		PX4_ERR("parameter schema unavailable");
		return PX4_ERROR;
	}

	if (!instance->_manual.advertise()) {
		delete instance;
		PX4_ERR("manual command endpoint unavailable");
		return PX4_ERROR;
	}

	instance->_last_apply = initial.apply_counter;
	const uint64_t identity = hrt_absolute_time() << 3;

	for (unsigned i = 0; i < kModeCount; ++i) { instance->_modes[i].request = identity | (i + 1); }

	desc.object.store(instance);
	desc.task_id = task_id_is_work_queue;
	instance->ScheduleOnInterval(kIntervalUs);
	return PX4_OK;
}

int Slaaps::print_status()
{
	uORB::Subscription status_sub{ORB_ID(slaaps_status)};
	slaaps_status_s status{};

	if (!status_sub.copy(&status)) { return PX4_ERROR; }

	PX4_INFO("modes manual=%d acceleration=%d bench=%d configured=%d",
		 status.mode_id[kManualMode], status.mode_id[kAccelerationMode], status.mode_id[kBenchMode],
		 status.direct_actuator_modes_configured);
	PX4_INFO("configuration valid=%d use=%u apply=%ld fault=%u snapshot=%lu us",
		 status.configuration_valid, status.configuration_use, static_cast<long>(status.apply_counter),
		 status.configuration_fault, static_cast<unsigned long>(status.snapshot_duration_us));
	PX4_INFO("unavailable=%u; no admitted actuator commands", status.unavailable_reason);
	PX4_INFO("manual endpoint=%lu active=%d", static_cast<unsigned long>(status.manual_command_revision),
		 status.manual_worker_active);
	PX4_INFO("schedule=%lu us worker_status=%u", static_cast<unsigned long>(status.schedule_interval_us),
		 status.manual_worker_status);
	PX4_INFO("input metadata=%d epoch=%lu (not flight admission)",
		 status.input_metadata_valid, static_cast<unsigned long>(status.input_source_epoch));
	PX4_INFO("routing snapshot=%d inventory_fault=%u routing_fault=%u",
		 status.routing_snapshot_valid, status.inventory_fault, status.routing_fault);
	return PX4_OK;
}

int Slaaps::print_usage(const char *reason)
{
	if (reason) { PX4_WARN("%s", reason); }

	PRINT_MODULE_USAGE_NAME("slaaps", "controller");
	PRINT_MODULE_USAGE_COMMAND_DESCR("start", "Register modes; wait for explicit QGC configuration apply");
	PRINT_MODULE_USAGE_DEFAULT_COMMANDS();
	return reason ? PX4_ERROR : PX4_OK;
}

extern "C" __EXPORT int slaaps_main(int argc, char *argv[])
{
	return ModuleBase::main(Slaaps::desc, argc, argv);
}
