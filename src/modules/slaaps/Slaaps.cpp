#include <lib/slaaps/Px4ConfigurationSource.hpp>
#include <lib/slaaps/OutputRoutingObservation.hpp>
#include <lib/slaaps/ManualExecution.hpp>
#include <lib/slaaps/ManualPreparationCoordinator.hpp>
#include <lib/slaaps/ManualPermissionTransport.hpp>
#include <lib/research_stabilized_manual/SelectedStateObservation.hpp>
#include <lib/slaaps/OperatingConditionObservation.hpp>
#include <lib/slaaps/PowerObservation.hpp>
#include <lib/slaaps/CompanionIdentity.hpp>
#include <lib/slaaps/AccelerationControllerPort.hpp>
#include "CompanionProtocol.hpp"
#include <lib/research_stabilized_manual/StabilizedInputValidation.hpp>
#include <lib/parameters/atomic_transaction.h>
#include <systemlib/mavlink_log.h>

#include <px4_platform_common/module.h>
#include <px4_platform_common/px4_work_queue/ScheduledWorkItem.hpp>
#include <drivers/drv_hrt.h>
#include <uORB/Publication.hpp>
#include <uORB/Subscription.hpp>
#include <uORB/SubscriptionCallback.hpp>
#include <uORB/topics/arming_check_request.h>
#include <uORB/topics/arming_check_reply.h>
#include <uORB/topics/actuator_armed.h>
#include <uORB/topics/parameter_update.h>
#include <uORB/topics/register_ext_component_request.h>
#include <uORB/topics/register_ext_component_reply.h>
#include <uORB/topics/setpoint_config.h>
#include <uORB/topics/setpoint_config_reply.h>
#include <uORB/topics/unregister_ext_component.h>
#include <uORB/topics/vehicle_status.h>
#include <uORB/topics/vehicle_command.h>
#include <uORB/topics/airspeed.h>
#include <uORB/topics/slaaps_flight_status.h>
#include <uORB/topics/follow_target.h>
#include <uORB/topics/vehicle_global_position.h>
#include <lib/geo/geo.h>
#include <px4_platform_common/events.h>
#include <uORB/topics/slaaps_status.h>
#include <uORB/topics/slaaps_acceleration_status.h>
#include <uORB/topics/slaaps_companion_packet.h>
#include <mavlink.h>
#include <cmath>
#include <cstring>
#if defined(CONFIG_SLAAPS_ROUTING_TEST)
#include <atomic>
#endif

namespace
{
constexpr unsigned kModeCount = 3;
constexpr unsigned kManualMode = 0;
constexpr unsigned kAccelerationMode = 1;
constexpr unsigned kBenchMode = 2;
constexpr hrt_abstime kIntervalUs = slaaps::kSupervisorIdleIntervalUs;
constexpr hrt_abstime kRetryUs = 500000;
constexpr hrt_abstime kMaximumStatusAgeUs = 1000000;
constexpr hrt_abstime kRoutingDiagnosticIntervalUs = 1000000; // Telemetry rate bound, not output timing.
constexpr hrt_abstime kAccelerationDiagnosticIntervalUs = 1000000; // Change-edge telemetry bound only.
constexpr px4::wq_config_t kSupervisorQueue{"wq:slaaps", 6500, -5};
constexpr const char *kModeNames[kModeCount] = {"SLAAPS Manual", "SLAAPS Acceleration", "SLAAPS Bench"};
constexpr int32_t kMaximumBenchSeconds = 60; // Software command bound, not hardware qualification.
constexpr unsigned kEdfBenchTarget = slaaps::kCanards + 1;
constexpr unsigned kDisarmRequestAttempts = 3; // Bounded command delivery; output expiry is independent.
static_assert(static_cast<uint8_t>(slaaps::AccelerationControlFault::AllocationRejected) == slaaps_acceleration_status_s::FAULT_ALLOCATION
	      &&static_cast<uint8_t>(slaaps::AccelerationAllocationStage::CurrentOwnerThrust) ==
	      slaaps_acceleration_status_s::ALLOCATION_CURRENT_OWNER_MODEL
	      &&static_cast<uint8_t>(slaaps::WrenchAllocationResult::ResidualExceeded) == slaaps_acceleration_status_s::SOLVER_RESIDUAL
	      &&static_cast<uint8_t>(slaaps::AccelerationDiagnosticState::Rejected) == slaaps_acceleration_status_s::REJECTED,
	      "local acceleration diagnostic codes");

// Either first explicit disarm publication stops flight output. Reuse of the
// accepted profile still waits for both fresh publications to be disarmed.
bool ordinaryFlightDisarm(hrt_abstime now, uint64_t maximum_age,
			  const vehicle_status_s &vehicle, const actuator_armed_s &armed)
{
	return maximum_age && vehicle.timestamp && armed.timestamp && vehicle.timestamp <= now && armed.timestamp <= now
	       && now - vehicle.timestamp <= maximum_age && now - armed.timestamp <= maximum_age
	       && !(vehicle.arming_state == vehicle_status_s::ARMING_STATE_ARMED && armed.armed)
	       && (vehicle.arming_state == vehicle_status_s::ARMING_STATE_DISARMED || !armed.armed)
	       && !armed.kill && !armed.lockdown && !armed.termination && !armed.in_esc_calibration_mode;
}
}

// Native configuration, mode and shared-worker owner. Production flight
// permission remains denied until measured-condition/recovery admission exists.
class Slaaps final : public ModuleBase, public px4::ScheduledWorkItem
{
public:
	static Descriptor desc;
	Slaaps() : ScheduledWorkItem(MODULE_NAME, kSupervisorQueue) {}
	~Slaaps() override { if (_diagnostic_log_pub) { orb_unadvertise(_diagnostic_log_pub); } }
	static int task_spawn(int argc, char *argv[]);
	static int custom_command(int argc, char *argv[]) { return print_usage("unknown command"); }
	static int print_usage(const char *reason = nullptr);
	int print_status() override;
private:
#if defined(SLAAPS_SUPERVISOR_TEST_ACCESS)
	friend struct SlaapsNativeTest;
#endif
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
	void updatePreparation(hrt_abstime now);
	void updateBench(hrt_abstime now);
	bool observeBenchInput(hrt_abstime &now, bool centered, bool edf = false);
	void cancelBench(hrt_abstime now, uint8_t status, bool normal_stop = false
#if defined(SLAAPS_SUPERVISOR_TEST_ACCESS) || defined(CONFIG_SLAAPS_ROUTING_TEST)
			 , unsigned caller_line = __builtin_LINE()
#endif
			);
#if defined(CONFIG_SLAAPS_ROUTING_TEST)
	uint64_t _native_cancel_session {}; // Supervisor queue only; retain the first call per session.
	uint64_t _native_preparation_cancel_session{};
	uint64_t _native_input_failure_session{};
	std::atomic<unsigned> _native_cancel_line{0};
#endif
	void updateBenchDisarm(hrt_abstime now, const vehicle_status_s &vehicle, const actuator_armed_s &armed, bool fresh);
	bool resetBenchEnable();
	bool restoreStoppedProfile(hrt_abstime now, const vehicle_status_s &vehicle, const actuator_armed_s &armed, bool actuator_fresh);
	void updateManualActivation(hrt_abstime now, const vehicle_status_s &vehicle, const actuator_armed_s &armed, bool armed_fresh);
	void updateFlightControl(hrt_abstime now, const vehicle_status_s &vehicle, const actuator_armed_s &armed, bool armed_fresh);
	void updateRecovery(hrt_abstime now, const vehicle_status_s &vehicle, const actuator_armed_s &armed, bool armed_fresh);
	bool publishRecoveryPermission(hrt_abstime now, const actuator_armed_s &armed);
	bool capturedOrdinaryFlightDisarm(hrt_abstime now);
	void holdAfterCompanionLoss(hrt_abstime now, const vehicle_status_s &vehicle, const actuator_armed_s &armed, bool armed_fresh);
	void updateAccelerationHold(hrt_abstime now, const vehicle_status_s &vehicle, const actuator_armed_s &armed, bool armed_fresh);
	bool pilotReadyForHandoff(hrt_abstime now, const research_stabilized_manual::SelectedStateObservationSnapshot *captured = nullptr);
	void reportHold(const char *loss);
	bool reentryReady(hrt_abstime now, const vehicle_status_s &vehicle);
	void beginReentry(hrt_abstime now);
	void observeLaunchAirspeed(hrt_abstime now);
	void publishFlightStatus(hrt_abstime now);
	bool observeNet(hrt_abstime now);
	void reportNet();
	bool observePrearmInput(hrt_abstime now, bool armed = false);
	void cancelPreparation(hrt_abstime now, slaaps::PreparationStatus status, bool normal_disarm = false
#if defined(CONFIG_SLAAPS_ROUTING_TEST)
			       , unsigned caller_line = __builtin_LINE()
#endif
			      );
	bool publishManualPermission(hrt_abstime now, bool *waiting = nullptr,
				     slaaps::ControlMode flight_mode = slaaps::ControlMode::None, uint64_t transition_us = 0,
				     bool *normal_disarm = nullptr);
	unsigned permissionRevision() { return _permission_revision && _permission_lifetime.revision() == _permission_revision ? _permission_revision : 0; }
	void publishStatus(hrt_abstime now);
	void publishAccelerationStatus(hrt_abstime now);
	void updateSchedule();
	void updateCompanion(hrt_abstime now);
	uint64_t _companion_identity_seed{}; // Acquired before scheduling; never a software/time fallback.
	slaaps::CompanionProtocol _companion_protocol{};
	bool observePower(hrt_abstime &now, bool bind = false);
	slaaps::PowerObservation _power{};
	slaaps::PowerSample _power_sample{};
	slaaps::PowerSourceIdentity _power_source{};
	slaaps::PowerObservationResult _power_result{slaaps::PowerObservationResult::InvalidConfiguration};
	bool _power_valid{};
	bool _power_bound{};
	uORB::ResearchSubscription _companion_input[4] {
		{ORB_ID::slaaps_companion_packet_0}, {ORB_ID::slaaps_companion_packet_1},
		{ORB_ID::slaaps_companion_packet_2}, {ORB_ID::slaaps_companion_packet_3}
	};
	uint64_t _companion_configuration{};
	int32_t _companion_apply{};
	hrt_abstime _companion_started{};
	hrt_abstime _companion_deadline{};
	uint64_t _companion_sequence{};
	unsigned _companion_revision{};
	uint32_t _companion_signing_generation{};
	uint8_t _companion_receiver_system{};
	uint8_t _companion_receiver_component{};
	uint8_t _companion_status{slaaps_status_s::COMPANION_DISABLED};
	bool _companion_fault{};
	slaaps::Px4ConfigurationSource _parameters{};
	slaaps::ConfigurationStore _configuration{};
	// Reverse destruction order keeps the shared owner alive through worker stop.
	slaaps::EdfOwner _edf{};
	slaaps::ManualWorkerTransport _manual{_edf};
	slaaps::ManualPreparationCoordinator _preparation{};
	slaaps::PreparationStatus _preparation_status{slaaps::PreparationStatus::Inactive};
	bool _preparation_requested{};
	bool _preparation_pending{};
	bool _manual_activation_owned{};
	bool _manual_configuration_active{};
	bool _manual_profile_restore_pending{};
	slaaps::ManualPreparationRequest _prepared_request{};
	unsigned _manual_activation_revisions[2] {};
	uint64_t _manual_first_command_deadline{};
	slaaps::ControlMode _flight_transfer_target{slaaps::ControlMode::None};
	uint64_t _flight_transfer_us{}, _flight_transfer_deadline{};
	slaaps::EdfPermission _flight_previous_permission{};
	slaaps::AccelerationControlInput _acceleration_input{}; // Serialized scratch; never an eligible-demand cache.
	research_stabilized_manual::SelectedStateObservation _acceleration_states{}; // Keep Manual's all-topic binding intact.
	slaaps::ParameterSnapshot _stop_profile{};
	slaaps::ParameterSnapshot _parameter_scratch{}; // Serialized owner only; CLI owns separate scratch.
	bool _manual_entry_selected{};
	bool _manual_entry_allowed{};
	uint64_t _manual_entry_started{};
	uint64_t _manual_entry_deadline{};
	uint64_t _manual_preparation_response_deadline{};
	uint8_t _manual_entry_status{slaaps_status_s::MANUAL_ENTRY_INACTIVE};
	slaaps::SelectedManualInput _prearm_input{};
	research_actuator_command::ManualControlInput _prearm_raw{};
	research_stabilized_manual::Configuration _prearm_configuration{};
	research_stabilized_manual::StateSample _prearm_previous_state{};
	research_stabilized_manual::SelectedStateObservationSnapshot _prearm_previous_observation{};
	bool _bench_controls_initialized{};
	bool _bench_can_run{};
	bool _bench_can_arm{};
	uint64_t _bench_can_arm_deadline{};
	bool _bench_edf_authorized{};
	bool _bench_edf_granted{};
	bool _bench_arming_authorized{};
	bool _bench_disarm_pending{};
	bool _bench_restore_after_disarm{};
	uint8_t _bench_terminal_status{slaaps_status_s::BENCH_ENTRY_REJECTED};
	unsigned _bench_disarm_attempts{};
	uint64_t _bench_last_disarm_request{};
	bool _bench_entry_selected{};
	bool _bench_entry_allowed{};
	bool _bench_enable_ready{};
	bool _bench_raw_configured{};
	bool _bench_input_bound{};
	uint8_t _bench_entry_status{slaaps_status_s::BENCH_ENTRY_INACTIVE};
	uint64_t _bench_entry_started{};
	uint64_t _bench_deadline{};
	int32_t _bench_target{};
	int32_t _bench_seconds{};
	unsigned _bench_input_revision{}, _bench_vehicle_revision{}, _bench_armed_revision{};
	research_stabilized_manual::ManualSample _bench_sample{};
	uORB::ResearchSubscription _bench_manual_input{ORB_ID::manual_control_setpoint};
	uORB::ResearchSubscription _bench_vehicle_input{ORB_ID::vehicle_status};
	uORB::ResearchSubscription _bench_armed_input{ORB_ID::actuator_armed};
	param_t _bench_enable_parameter{PARAM_INVALID};
	param_t _bench_target_parameter{PARAM_INVALID};
	param_t _bench_seconds_parameter{PARAM_INVALID};
	uint64_t _preparation_session{};
	uint64_t _preparation_deadline{};
	uint8_t _preparation_banks{};
	unsigned _permission_revision{};
	uint64_t _permission_sequence{};
	uORB::ResearchSubscription _permission_lifetime{ORB_ID::slaaps_manual_permission};
	uORB::Publication<slaaps_manual_permission_s> _permission{ORB_ID::slaaps_manual_permission};
	uORB::ResearchSubscription _main_ready_lifetime{ORB_ID::slaaps_manual_ready_main};
	uORB::ResearchSubscription _aux_ready_lifetime{ORB_ID::slaaps_manual_ready_aux};
	uORB::ResearchSubscription _main_ack_lifetime{ORB_ID::slaaps_manual_ack_main};
	uORB::ResearchSubscription _aux_ack_lifetime{ORB_ID::slaaps_manual_ack_aux};
	uORB::ResearchSubscription _main_activation_lifetime{ORB_ID::slaaps_manual_active_main};
	uORB::ResearchSubscription _aux_activation_lifetime{ORB_ID::slaaps_manual_active_aux};
	uORB::SubscriptionCallbackWorkItem _main_activation_wakeup{this, ORB_ID(slaaps_manual_active_main)};
	uORB::SubscriptionCallbackWorkItem _aux_activation_wakeup{this, ORB_ID(slaaps_manual_active_aux)};
	uORB::SubscriptionCallbackWorkItem _main_handled_wakeup{this, ORB_ID(slaaps_manual_ack_main)};
	uORB::SubscriptionCallbackWorkItem _aux_handled_wakeup{this, ORB_ID(slaaps_manual_ack_aux)};
	// Recomputed from current authority every cycle. Defaults confer no permission.
	slaaps::EdfPermission _manual_permission{};
	slaaps::SelectedInputPolicy _manual_input_policy{};
	slaaps::ManualWorkerStatus _manual_status{slaaps::ManualWorkerStatus::Inactive};
	uint32_t _schedule_interval_us{kIntervalUs};
	hrt_abstime _last_maintenance{};
	research_stabilized_manual::SelectedStateObservation _inputs{};
	research_stabilized_manual::SelectedStateObservationSnapshot _input_observation{};
	bool _input_metadata_valid{};
	slaaps::OperatingConditionObservation _conditions{};
	slaaps::OperatingConditionSample _condition_sample{};
	slaaps::OperatingConditionSourceIdentity _prearm_condition_source{};
	bool _condition_valid{};
	uint32_t _input_epoch{};
	Mode _modes[kModeCount] {};
	uint32_t _snapshot_duration{};
	int32_t _last_apply{};
	int32_t _selected_use{};
	bool _restore_pending{true};
	slaaps::ConfigurationFault _configuration_fault{slaaps::ConfigurationFault::NoCandidate};
	const char *_configuration_parameter{}; // Trusted validator literal, never a parameter handle.
	uint64_t _configuration_rejection_identity{};
	int32_t _configuration_rejection_use{};
	int32_t _configuration_rejection_apply{};
	orb_advert_t _diagnostic_log_pub{};
	uORB::PublicationData<slaaps_acceleration_status_s> _acceleration_status{ORB_ID::slaaps_acceleration_status};
	hrt_abstime _last_acceleration_diagnostic{}, _reported_acceleration_fault_us{};
	uint64_t _reported_acceleration_session{}, _reported_acceleration_configuration{};
	slaaps::AccelerationControlFault _reported_acceleration_fault{slaaps::AccelerationControlFault::None};
	slaaps::AccelerationAllocationStage _reported_acceleration_stage{slaaps::AccelerationAllocationStage::None};
	slaaps::WrenchAllocationResult _reported_acceleration_solver{slaaps::WrenchAllocationResult::Success};
	slaaps::ManualWorkerStatus _reported_acceleration_worker{slaaps::ManualWorkerStatus::Inactive};
	uint8_t _reported_acceleration_moment{}, _reported_acceleration_canard{};
	bool _have_acceleration_report{}, _reported_acceleration_rejection{};
	unsigned _stop_index{};
	bool _registration_fault{};
	bool _routing_valid{};
	slaaps::InventoryFault _inventory_fault{slaaps::InventoryFault::MissingBank};
	slaaps::RoutingFault _routing_fault{slaaps::RoutingFault::InvalidInventory};
	int16_t _routing_fault_index{-1};
	uint8_t _routing_fault_bank{UINT8_MAX};
	uint8_t _routing_fault_channel{};
	slaaps::RoutingFault _routing_reported_fault{slaaps::RoutingFault::None};
	int16_t _routing_reported_index{-1};
	uint8_t _routing_reported_bank{UINT8_MAX};
	uint8_t _routing_reported_channel{};
	hrt_abstime _last_routing_diagnostic{};
	slaaps::OutputRoutingObservation _output_observation{};
	slaaps::ReportedRoutingSnapshot _routing_snapshot{};
	uORB::Subscription _parameter_update{ORB_ID(parameter_update)};
	uORB::Subscription _vehicle_status{ORB_ID(vehicle_status)};
	uORB::Subscription _actuator_armed{ORB_ID(actuator_armed)};
	uORB::Subscription _registration_reply{ORB_ID(register_ext_component_reply)};
	uORB::Subscription _setpoint_reply{ORB_ID(setpoint_config_reply)};
	uORB::Subscription _arming_request{ORB_ID(arming_check_request)};
	uORB::Publication<register_ext_component_request_s> _registration{ORB_ID(register_ext_component_request)};
	uORB::Publication<setpoint_config_s> _setpoint{ORB_ID(setpoint_config)};
	uORB::Publication<arming_check_reply_s> _arming_reply{ORB_ID(arming_check_reply)};
	uORB::Publication<unregister_ext_component_s> _unregister{ORB_ID(unregister_ext_component)};
	uORB::Publication<slaaps_status_s> _status{ORB_ID(slaaps_status)};
	uORB::Publication<vehicle_command_s> _disarm_command{ORB_ID(vehicle_command)};
	// Launcher stage: selected raw airspeed for the attitude gate; observation only.
	uORB::Publication<slaaps_flight_status_s> _flight_status{ORB_ID(slaaps_flight_status)};
	uORB::Subscription _launch_airspeed_sub{ORB_ID(airspeed)};
	float _launch_airspeed{};
	bool _launch_airspeed_valid{};
	// D6 operator reference: net position relative to QGC's own position. Never guidance.
	const param_t _net_enabled_param{param_find("SLA_NET_EN")};
	const param_t _net_north_param{param_find("SLA_NET_N")};
	const param_t _net_east_param{param_find("SLA_NET_E")};
	uORB::Subscription _gcs_position{ORB_ID(follow_target)};
	uORB::Subscription _global_position{ORB_ID(vehicle_global_position)};
	float _net_distance_m{}, _net_bearing_deg{};
	bool _net_valid{};
	uint64_t _net_reported_session{};
	// D1/D2 bookkeeping for the current session only; never authority.
	uint64_t _hold_reported_session{};
	hrt_abstime _handoff_last_request{};
	uint8_t _handoff_attempts{};
	// D5: the current deliberate entry is an armed re-entry after a D1 hold or D3 recovery. Never authority.
	bool _reentry{};
	hrt_abstime _reentry_ready_since{}, _reentry_checked{};
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
	if (!changed && !_restore_pending) { return; }

	// A stop may reuse only an unchanged accepted profile. Even a queued edit
	// observed before the final disarmed snapshot cancels that opportunity.
	if (changed) { _manual_profile_restore_pending = false; }

	vehicle_status_s vehicle{};
	const bool copied = _vehicle_status.copy(&vehicle);
	now = hrt_absolute_time();
	const bool fresh = copied && vehicle.timestamp != 0 && now >= vehicle.timestamp
			   && now - vehicle.timestamp <= kMaximumStatusAgeUs;
	const bool disarmed = fresh && vehicle.arming_state == vehicle_status_s::ARMING_STATE_DISARMED;

	if (!disarmed) {
		// The commissioning stop command is deliberately writable while armed.
		// It confers no profile-edit/apply authority: only the exact accepted
		// coherent profile and markers may survive this command notification.
		if (fresh && _bench_edf_authorized && _prepared_request.mode == slaaps::ControlMode::EdfBench) {
			slaaps::ParameterSnapshot current{};

			if (_parameters.capture(current, _snapshot_duration, _parameter_scratch) && current.use == 2 && current.use == _selected_use
			    && current.apply_counter == _last_apply && slaaps::Px4ConfigurationSource::remembered(current)
			    && slaaps::equal(current.configuration, _configuration.configuration())) { return; }
		}

		_bench_restore_after_disarm = false;

		// Wait for initial Commander state, but never defer an armed restart
		// into automatic restoration after a later disarm.
		if (fresh) { _restore_pending = false; }

		_configuration.invalidate();
		_configuration_fault = slaaps::ConfigurationFault::Active;
		_configuration_parameter = nullptr;
		_configuration_rejection_apply = 0;
		return;
	}

	slaaps::ParameterSnapshot snapshot{};

	if (!_parameters.capture(snapshot, _snapshot_duration, _parameter_scratch)) {
		_configuration.invalidate();
		_configuration_fault = slaaps::ConfigurationFault::ParameterRange;
		_configuration_parameter = nullptr;
		_configuration_rejection_apply = 0;
		return;
	}

	const bool restoring = _restore_pending && slaaps::Px4ConfigurationSource::remembered(snapshot);
	_restore_pending = false;

	// A rejection describes one actual attempted profile, not later unattempted
	// edits. Retain it across repeated observation of that same attempt only.
	if (_configuration_rejection_apply && (snapshot.apply_counter != _configuration_rejection_apply
					       || snapshot.use != _configuration_rejection_use
					       || slaaps::fingerprint(snapshot.configuration) != _configuration_rejection_identity)) {
		_configuration_rejection_apply = 0;
		_configuration_parameter = nullptr;
		_configuration_fault = slaaps::ConfigurationFault::NoCandidate;
	}

	if (_bench_restore_after_disarm && (snapshot.use != _selected_use || snapshot.apply_counter != _last_apply
					    || !slaaps::Px4ConfigurationSource::remembered(snapshot) || !slaaps::equal(snapshot.configuration, _configuration.configuration()))) {
		_bench_restore_after_disarm = false;
	}

	_configuration.observe(snapshot.configuration);

	if (snapshot.use != _selected_use) { _configuration.invalidate(); }

	if (!_configuration.hasCandidate() && !_configuration_rejection_apply) {
		_configuration_fault = slaaps::ConfigurationFault::NoCandidate;
		_configuration_parameter = nullptr;
		_configuration_rejection_apply = 0;
	}

	if (!restoring && snapshot.apply_counter <= _last_apply) { return; }

	// Accepting a profile is separate from deliberate mode entry. An apply
	// cannot renew a bound preparation or repair a rejected selected session.
	if (_preparation_pending || _prearm_input.active() || _manual_activation_owned || _manual.active() || _bench_input_bound
	    || _manual_entry_deadline) {
		if (_bench_entry_selected) { cancelBench(now, slaaps_status_s::BENCH_ENTRY_REJECTED); }

		else { cancelPreparation(now, slaaps::PreparationStatus::Rejected); }
	}

	_last_apply = snapshot.apply_counter;
	_configuration.disable();

	if (snapshot.use < 0 || snapshot.use > static_cast<int32_t>(slaaps::ConfigurationUse::EdfCommissioning)) {
		_configuration_fault = slaaps::ConfigurationFault::ParameterRange;
		_configuration_parameter = "SLA_CFG_KIND";
		_configuration_rejection_identity = slaaps::fingerprint(snapshot.configuration);
		_configuration_rejection_use = snapshot.use;
		_configuration_rejection_apply = snapshot.apply_counter;
		PX4_WARN("invalid SLA_CFG_KIND");
		mavlink_log_critical(&_diagnostic_log_pub, "SLAAPS: check SLA_CFG_KIND");
		return;
	}

	_selected_use = snapshot.use;
	const auto result = _configuration.commit(snapshot.configuration, static_cast<slaaps::ConfigurationUse>(_selected_use));
	_configuration_fault = result.fault;
	_configuration_parameter = result.parameter;
	_configuration_rejection_apply = result.valid() ? 0 : snapshot.apply_counter;

	if (!result.valid()) {
		_configuration_rejection_identity = slaaps::fingerprint(snapshot.configuration);
		_configuration_rejection_use = snapshot.use;
	}

	if (!result.valid()) {
		PX4_WARN("configuration rejected: fault=%u parameter=%s", static_cast<unsigned>(result.fault),
			 result.parameter ? result.parameter : "cross-field constraint");

		// One publication per actual apply attempt. Commander events ignore
		// argument-only changes; a second rejected parameter must still reach QGC.
		if (result.parameter) { mavlink_log_critical(&_diagnostic_log_pub, "SLAAPS: check %.16s", result.parameter); }

	} else {
		if (!restoring && !_parameters.remember(snapshot)) {
			_configuration.invalidate();
			_configuration_fault = slaaps::ConfigurationFault::NoCandidate;
			_configuration_parameter = nullptr;
			_configuration_rejection_apply = 0;
			PX4_WARN("configuration acceptance could not be saved; apply again");
			return;
		}

		PX4_INFO("configuration %s; mode readiness checked separately", restoring ? "restored" : "applied");

		if (snapshot.configuration.data_assessment > 0) {
			const char *assessment = snapshot.configuration.data_assessment == static_cast<int32_t>(slaaps::DataAssessment::Calibrated) ?
						 "calibrated" : "qualified";
			mavlink_log_info(&_diagnostic_log_pub, "SLAAPS: declared %s record %ld", assessment,
					 static_cast<long>(snapshot.configuration.data_record_id));
		}
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
		reply.mode_req_prevent_arming = i == kBenchMode && !(_bench_can_arm && now < _bench_can_arm_deadline);
		// Bench is selectable independently of its explicit output enable. Its
		// prevent-arming requirement and zero can_arm mask remain independent.
		reply.can_arm_and_run = i == kBenchMode && _bench_can_run && _configuration.hasCandidate()
					&& (_selected_use == 0 || (_selected_use == 2 && _power_valid && now < _power_sample.deadline_us));
		_arming_reply.publish(reply);
	}
}

void Slaaps::updateRouting(hrt_abstime now)
{
	_routing_valid = false;
	_routing_fault = slaaps::RoutingFault::InvalidInventory;
	_routing_fault_index = -1;
	_routing_fault_bank = UINT8_MAX;
	_routing_fault_channel = 0;
	// Both physical banks must report, including a bank without assignments.
	// Only a successful stable capture can replace the retained snapshot.
	_inventory_fault = _output_observation.captureLive(now, _routing_snapshot);

	if (_inventory_fault != slaaps::InventoryFault::None || !_configuration.hasCandidate()) {
		_routing_reported_fault = slaaps::RoutingFault::None;
		return;
	}

	slaaps::RoutingPlan plan{};
	const auto result = slaaps::makeRoutingPlan(_configuration.configuration(), _routing_snapshot.inventory, _selected_use != 0, plan);
	_routing_fault = result.fault;
	_routing_valid = result.valid();

	if (!result.valid() && result.channel >= 0 && static_cast<unsigned>(result.channel) < _routing_snapshot.inventory.count) {
		_routing_fault_index = static_cast<int16_t>(result.channel);
		const auto &channel = _routing_snapshot.inventory.channels[result.channel];

		if ((channel.bank == slaaps::OutputBank::Main || channel.bank == slaaps::OutputBank::Aux)
		    && channel.channel < slaaps::kMaximumChannelsPerBank) {
			_routing_fault_bank = static_cast<uint8_t>(channel.bank);
			_routing_fault_channel = channel.channel + 1;
		}
	}

	if (result.valid()) {
		_routing_reported_fault = slaaps::RoutingFault::None;
		return;
	}

	const bool same_report = _routing_reported_fault == _routing_fault && _routing_reported_index == _routing_fault_index
				 && _routing_reported_bank == _routing_fault_bank && _routing_reported_channel == _routing_fault_channel;

	if (_bench_disarm_pending || same_report || (_last_routing_diagnostic && (now < _last_routing_diagnostic
			|| now - _last_routing_diagnostic < kRoutingDiagnosticIntervalUs))) { return; }

	// Compare against the last emitted tuple. A changed tuple suppressed by
	// this throttle remains eligible for a later report even if input is held.
	_routing_reported_fault = _routing_fault;
	_routing_reported_index = _routing_fault_index;
	_routing_reported_bank = _routing_fault_bank;
	_routing_reported_channel = _routing_fault_channel;

	if (_routing_fault_bank <= static_cast<uint8_t>(slaaps::OutputBank::Aux) && _routing_fault_channel) {
		mavlink_log_critical(&_diagnostic_log_pub, "SLAAPS: check %s channel %u (routing %u)",
				     _routing_fault_bank == static_cast<uint8_t>(slaaps::OutputBank::Main) ? "MAIN" : "AUX",
				     _routing_fault_channel, static_cast<unsigned>(_routing_fault));

	} else if (_routing_fault == slaaps::RoutingFault::MissingCanard) {
		mavlink_log_critical(&_diagnostic_log_pub, "SLAAPS: assign all four canard output functions");

	} else if (_routing_fault == slaaps::RoutingFault::MissingEdf) {
		mavlink_log_critical(&_diagnostic_log_pub, "SLAAPS: assign EDF output function");

	} else {
		mavlink_log_critical(&_diagnostic_log_pub, "SLAAPS: check MAIN/AUX output inventory");
	}

	// Observation predates formatting/publication and may be preempted. Start
	// the next reporting interval after emission, never from that earlier time.
	_last_routing_diagnostic = hrt_absolute_time();
}

void Slaaps::cancelPreparation(hrt_abstime now, slaaps::PreparationStatus status, bool normal_disarm
#if defined(CONFIG_SLAAPS_ROUTING_TEST)
			       , unsigned caller_line
#endif
			      )
{
#if defined(CONFIG_SLAAPS_ROUTING_TEST)

	if (_preparation_session && _native_preparation_cancel_session != _preparation_session) {
		_native_preparation_cancel_session = _preparation_session;
		PX4_WARN("native preparation cancel line=%u time=%llu status=%u normal=%d",
			 caller_line, static_cast<unsigned long long>(now), static_cast<unsigned>(status), normal_disarm);
		PX4_WARN("native preparation candidate=%d eligible=%d routing=%d inventory=%u fault=%u worker=%u",
			 _configuration.hasCandidate(), _configuration.eligible(), _routing_valid,
			 static_cast<unsigned>(_inventory_fault), static_cast<unsigned>(_routing_fault), static_cast<unsigned>(_manual_status));
	}

#endif

	if (_manual_activation_owned) { _preparation.cancelActivation(now); }

	else if (_preparation_pending) { _preparation.cancel(now); }

	_manual.stop();
	_edf.revoke();
	_main_activation_wakeup.unregisterCallback();
	_aux_activation_wakeup.unregisterCallback();
	_main_handled_wakeup.unregisterCallback();
	_aux_handled_wakeup.unregisterCallback();
	_manual_activation_owned = false;
	_manual_first_command_deadline = 0;
	_flight_transfer_target = slaaps::ControlMode::None;
	_flight_transfer_us = _flight_transfer_deadline = 0;
	_flight_previous_permission = {};
	_acceleration_input = {};
	_manual_preparation_response_deadline = 0;
	_manual_permission = {};
	_power_bound = false;

	if (_manual_configuration_active) {
		_manual_profile_restore_pending = normal_disarm && _configuration.eligible();
		_configuration.disable();
		_manual_configuration_active = false;
	}

	if (_preparation_session && permissionRevision()) {
		slaaps_manual_permission_s invalid{};
		invalid.timestamp = now;
		_permission.publish(invalid);
	}

	_preparation_pending = false;
	_preparation_requested = false;
	_preparation_status = status;
	_prearm_input.stop();
	_prearm_raw.reset();
	_reentry = false;
	_manual_entry_status = status == slaaps::PreparationStatus::Inactive ? slaaps_status_s::MANUAL_ENTRY_INACTIVE :
			       slaaps_status_s::MANUAL_ENTRY_REJECTED;

	if (status == slaaps::PreparationStatus::Inactive) {
		_manual_entry_deadline = 0;
		_preparation_session = 0;
		_preparation_deadline = 0;
		_preparation_banks = 0;
		_prepared_request = {};
	}
}

bool Slaaps::restoreStoppedProfile(hrt_abstime now, const vehicle_status_s &vehicle,
				   const actuator_armed_s &armed, bool actuator_fresh)
{
	if (!_manual_profile_restore_pending) { return false; }

	const auto &retained = _configuration.configuration();
	const bool fresh_disarm = actuator_fresh && retained.actuator_permission_age_us > 0
				  && vehicle.timestamp && vehicle.timestamp <= now && armed.timestamp <= now
				  && now - armed.timestamp <= static_cast<uint64_t>(retained.actuator_permission_age_us)
				  && now - vehicle.timestamp <= static_cast<uint64_t>(retained.actuator_permission_age_us)
				  && !armed.kill && !armed.lockdown && !armed.termination && !armed.in_esc_calibration_mode;

	if (!fresh_disarm || (vehicle.arming_state == vehicle_status_s::ARMING_STATE_ARMED && armed.armed)) {
		_manual_profile_restore_pending = false;
		return false;
	}

	if (vehicle.arming_state != vehicle_status_s::ARMING_STATE_DISARMED || armed.armed) { return true; }

	_manual_profile_restore_pending = false;
	uint32_t duration{};

	// Reuse never repairs edits or acceptance changes and never resets either
	// mode's deliberate entry latch. Command parameters are outside this profile.
	if (_parameters.capture(_stop_profile, duration, _parameter_scratch) && _stop_profile.use == _selected_use
	    && _stop_profile.use == static_cast<int32_t>(_configuration.use())
	    && _stop_profile.apply_counter == _last_apply && slaaps::Px4ConfigurationSource::remembered(_stop_profile)
	    && slaaps::equal(_stop_profile.configuration, retained)) {
		const auto result = _configuration.commit(retained, _configuration.use());
		_configuration_fault = result.fault;
		_configuration_parameter = result.parameter;
		_configuration_rejection_apply = 0;
	}

	return false;
}

bool Slaaps::resetBenchEnable()
{
	int32_t value{};

	if (_bench_enable_parameter == PARAM_INVALID || param_get(_bench_enable_parameter, &value) != 0) { return false; }

	if (!value) { return true; }

	const int32_t disabled = 0;
	return param_set(_bench_enable_parameter, &disabled) == 0;
}

void Slaaps::cancelBench(hrt_abstime now, uint8_t status, bool normal_stop
#if defined(SLAAPS_SUPERVISOR_TEST_ACCESS) || defined(CONFIG_SLAAPS_ROUTING_TEST)
			 , unsigned caller_line
#endif
			)
{
#if defined(CONFIG_SLAAPS_ROUTING_TEST)

	if (_preparation_session && _native_cancel_session != _preparation_session) {
		_native_cancel_session = _preparation_session;
		_native_cancel_line.store(caller_line);
		// Retain the initiating state before cancellation invalidates the
		// profile and the next observation overwrites the routing rejection.
		PX4_WARN("native bench cancel line=%u candidate=%d eligible=%d routing=%d inventory=%u fault=%u",
			 caller_line, _configuration.hasCandidate(), _configuration.eligible(), _routing_valid,
			 static_cast<unsigned>(_inventory_fault), static_cast<unsigned>(_routing_fault));
		PX4_WARN("native bench cancel time=%llu reports=%llu/%llu active=%d worker=%u",
			 static_cast<unsigned long long>(now), static_cast<unsigned long long>(_routing_snapshot.reported_us[0]),
			 static_cast<unsigned long long>(_routing_snapshot.reported_us[1]), _manual.active(),
			 static_cast<unsigned>(_manual_status));
	}

#endif
#if defined(SLAAPS_SUPERVISOR_TEST_ACCESS)
	// Capture the initiating branch on this queue before cleanup changes the
	// configuration, routing and permission observed by the native fixture.
	SlaapsNativeTest::beforeBenchCancel(*this, now, status, normal_stop, caller_line);
#endif
	const bool edf = _bench_edf_authorized;

	if (normal_stop && _manual_configuration_active) {
		// A GUI stop cannot repair a concurrent profile/routing/owner fault.
		// Reuse requires the same current binding before cancellation consumes it.
		updateRouting(now);
		now = hrt_absolute_time();
		normal_stop = _configuration.eligible() && _routing_valid
			      && _preparation.pollActivationObservedLive(now, _configuration.configuration(), _routing_snapshot,
					      _inventory_fault) == slaaps::ActivationStatus::Consumed;

		if (!normal_stop) { status = slaaps_status_s::BENCH_ENTRY_REJECTED; }
	}

	_bench_restore_after_disarm = edf && normal_stop && _manual_configuration_active && _configuration.eligible();
	cancelPreparation(now, slaaps::PreparationStatus::Rejected, normal_stop);
	_bench_can_arm = false;
	_bench_can_arm_deadline = 0;
	_bench_arming_authorized = false;
	vehicle_status_s vehicle{};
	actuator_armed_s armed{};
	const bool have_vehicle = _vehicle_status.copy(&vehicle);
	const bool have_armed = _actuator_armed.copy(&armed);
	now = hrt_absolute_time();

	if (!edf && have_vehicle && have_armed) { (void)restoreStoppedProfile(now, vehicle, armed, armed.timestamp != 0); }

	if (edf) { _manual_profile_restore_pending = false; }

	(void)resetBenchEnable();
	_bench_enable_ready = false;
	_bench_input_bound = false;
	_bench_raw_configured = false;
	_bench_entry_status = status;
	_manual_entry_status = slaaps_status_s::MANUAL_ENTRY_INACTIVE;
	_bench_can_run &= _configuration.hasCandidate();

	if (status == slaaps_status_s::BENCH_ENTRY_INACTIVE) {
		_preparation_status = slaaps::PreparationStatus::Inactive;
		_preparation_session = 0;
		_preparation_deadline = 0;
		_preparation_banks = 0;
		_prepared_request = {};
		_bench_deadline = 0;
		_bench_entry_started = 0;
	}

	if (edf) {
		_bench_terminal_status = status;
		_bench_can_run = false;
		_bench_entry_allowed = false;

		if (!_bench_disarm_pending) { _bench_disarm_attempts = 0; _bench_last_disarm_request = 0; }

		_bench_disarm_pending = true;
		const uint64_t age = static_cast<uint64_t>(_configuration.configuration().actuator_permission_age_us);
		const bool fresh = have_vehicle && have_armed && vehicle.timestamp && armed.timestamp
				   && vehicle.timestamp <= now && armed.timestamp <= now
				   && now - vehicle.timestamp <= age && now - armed.timestamp <= age
				   && _bench_vehicle_input.revision() == _bench_vehicle_revision
				   && _bench_armed_input.revision() == _bench_armed_revision;
		updateBenchDisarm(now, vehicle, armed, fresh);
	}
}

void Slaaps::updateBenchDisarm(hrt_abstime now, const vehicle_status_s &vehicle,
			       const actuator_armed_s &armed, bool fresh)
{
	_bench_can_run = _bench_can_arm = false;

	if (fresh && vehicle.arming_state == vehicle_status_s::ARMING_STATE_DISARMED && !armed.armed) {
		_bench_disarm_pending = false;
		_bench_edf_authorized = false;
		_bench_entry_status = _bench_terminal_status;

		if (_bench_restore_after_disarm) {
			_manual_profile_restore_pending = true;
			(void)restoreStoppedProfile(now, vehicle, armed, true);
		}

		_bench_restore_after_disarm = false;
		return;
	}

	_bench_entry_status = _bench_disarm_attempts < kDisarmRequestAttempts ?
			      slaaps_status_s::BENCH_ENTRY_WAIT_DISARM : slaaps_status_s::BENCH_ENTRY_DISARM_FAILED;

	if (!_bench_edf_authorized || _bench_disarm_attempts >= kDisarmRequestAttempts
	    || (_bench_last_disarm_request && now - _bench_last_disarm_request < kRetryUs)) { return; }

	// Only a session minted by genuine disarmed entry and explicit enable can
	// reach here. Never force disarm or apply this ground-only policy to Manual.
	vehicle_command_s command{};
	command.timestamp = now;
	command.command = vehicle_command_s::VEHICLE_CMD_COMPONENT_ARM_DISARM;
	command.param1 = static_cast<float>(vehicle_command_s::ARMING_ACTION_DISARM);
	command.param2 = 0.F;
	command.target_system = command.source_system = vehicle.system_id;
	command.target_component = command.source_component = vehicle.component_id;
	command.from_external = false;
	(void)_disarm_command.publish(command);
	++_bench_disarm_attempts;
	_bench_last_disarm_request = now;

	// Publish the exhausted-attempt state in the same observation as the final
	// request. Waiting for another idle tick makes status contradict the retry
	// counter; only fresh actual disarming may complete the pending stop.
	if (_bench_disarm_attempts >= kDisarmRequestAttempts) {
		_bench_entry_status = slaaps_status_s::BENCH_ENTRY_DISARM_FAILED;
	}
}

bool Slaaps::observeBenchInput(hrt_abstime &now, bool centered, bool edf)
{
	const auto &configuration = _configuration.configuration();

	if (!_bench_raw_configured) {
		research_actuator_command::ManualControlConfiguration input{};
		input.required_axis_mask = edf ? research_actuator_command::kAllManualAxesMask : research_actuator_command::kAttitudeManualAxesMask;
		input.source_policy = configuration.manual_source == static_cast<int32_t>(slaaps::ManualInputSource::RcOnly) ?
				      research_actuator_command::ManualSourcePolicy::RcOnly : research_actuator_command::ManualSourcePolicy::MavlinkOnly;
		input.maximum_sample_age_us = static_cast<uint64_t>(configuration.manual_age_ms) * 1000;
		input.mavlink_instance = static_cast<uint8_t>(configuration.manual_instance);
		input.mavlink_system_id = static_cast<uint8_t>(configuration.manual_system);
		input.mavlink_component_id = static_cast<uint8_t>(configuration.manual_component);

		for (unsigned axis = 0; axis < slaaps::kAxes; ++axis) { input.command_map[axis + 1][axis] = 1.F; }

		if (edf) { input.command_map[0][3] = 1.F; }

		if (_prearm_raw.configure(input) != research_actuator_command::ManualControlStatus::Ok) {
			_bench_entry_status = slaaps_status_s::BENCH_ENTRY_WAIT_CONFIGURATION;
			return false;
		}

		_bench_raw_configured = true;
	}

	manual_control_setpoint_s message{};
	unsigned revision{};
	const bool copied = _bench_manual_input.copyBound(&message, revision);
	now = hrt_absolute_time();
	_bench_sample = research_actuator_command::manualControlSampleFromMessage(message);
	const bool stable = copied && revision && revision == _bench_manual_input.revision()
			    && _bench_vehicle_revision == _bench_vehicle_input.revision()
			    && _bench_armed_revision == _bench_armed_input.revision()
			    && (!_bench_input_bound || revision == _bench_input_revision);
	research_actuator_command::RuntimeInput discarded{};
	research_actuator_command::ManualControlReport report{};

	if (!stable || _bench_sample.timestamp_sample_us < _bench_entry_started
	    || _prearm_raw.evaluate(now, _bench_sample, discarded, report) != research_actuator_command::ManualControlStatus::Ok) {
		_bench_entry_status = _bench_input_bound ? slaaps_status_s::BENCH_ENTRY_REJECTED : slaaps_status_s::BENCH_ENTRY_WAIT_INPUT;
		return false;
	}

	_bench_input_revision = revision;
	_bench_input_bound = true;

	if (centered) {
		for (unsigned axis = 0; axis < slaaps::kAxes; ++axis) {
			if (std::fabs(_bench_sample.axes[axis]) > configuration.centered_enter) {
				_bench_entry_status = slaaps_status_s::BENCH_ENTRY_WAIT_CENTER;
				return false;
			}
		}

		if (edf) {
			float throttle{};

			if (!slaaps::normalizeManualThrottle(_bench_sample.axes[3], configuration.manual_throttle_low,
							     configuration.manual_throttle_high, throttle)) {
				_bench_entry_status = slaaps_status_s::BENCH_ENTRY_REJECTED;
				return false;
			}

			if (throttle > configuration.edf_entry_max) {
				_bench_entry_status = slaaps_status_s::BENCH_ENTRY_WAIT_THROTTLE;
				return false;
			}
		}
	}

	return true;
}

bool Slaaps::observePrearmInput(hrt_abstime now, bool armed)
{
	const bool bound = _prearm_input.active();
	auto fail = [&](uint8_t reason
#if defined(CONFIG_SLAAPS_ROUTING_TEST)
			, unsigned caller_line = __builtin_LINE()
#endif
	) {
#if defined(CONFIG_SLAAPS_ROUTING_TEST)

		if (_preparation_session && _native_input_failure_session != _preparation_session) {
			_native_input_failure_session = _preparation_session;
			const auto &sample = _input_observation;
			unsigned changed{};

			for (unsigned i = 0; i < research_stabilized_manual::PublisherBinding::kTopics; ++i) {
				if (sample.revisions[i] != _prearm_previous_observation.revisions[i]) { changed |= 1U << i; }
			}

			PX4_WARN("native input failure line=%u reason=%u bound=%d state=%d permission=%d revisions=%u",
				 caller_line, reason, bound, sample.state.valid, sample.permission_fresh, changed);
			PX4_WARN("native input times now=%llu capture=%llu manual=%llu attitude=%llu rate=%llu",
				 static_cast<unsigned long long>(now), static_cast<unsigned long long>(sample.now),
				 static_cast<unsigned long long>(sample.manual.timestamp_sample_us),
				 static_cast<unsigned long long>(sample.state.attitude_sample_us), static_cast<unsigned long long>(sample.state.rate_sample_us));
			PX4_WARN("native input limits manual=%d state=%d status=%llu permission=%llu",
				 _configuration.configuration().manual_age_ms, _configuration.configuration().state_age_ms,
				 static_cast<unsigned long long>(_manual_input_policy.maximum_status_age_us),
				 static_cast<unsigned long long>(_manual_input_policy.maximum_permission_age_us));
			PX4_WARN("native input metadata estimator=%llu flags=%llu permission=%llu",
				 static_cast<unsigned long long>(sample.estimator_time), static_cast<unsigned long long>(sample.flags_time),
				 static_cast<unsigned long long>(sample.permission.timestamp));
		}

#endif
		_manual_entry_status = reason;

		if (!bound) { _prearm_input.stop(); }

		return false;
	};
	const auto &observation = _input_observation;
	const auto &configuration = _configuration.configuration();

	if (configuration.schedule.enabled && (!_condition_valid || now >= _condition_sample.deadline_us
					       || (bound && !slaaps::sameOperatingConditionSource(_condition_sample.source, _prearm_condition_source)))) {
		return fail(slaaps_status_s::MANUAL_ENTRY_WAIT_INPUT);
	}

	// A retained command from before this deliberate entry cannot become its
	// initial input merely because its ordinary freshness limit has not elapsed.
	if (observation.manual.timestamp_sample_us < _manual_entry_started) {
		return fail(slaaps_status_s::MANUAL_ENTRY_WAIT_INPUT);
	}

	if (armed) {
		const auto &previous = _prearm_previous_observation;
		bool same = bound && observation.permission_fresh && observation.permission.armed
			    && observation.now && observation.now <= now && observation.state.valid
			    && observation.estimator_time && observation.estimator_time <= now
			    && now - observation.estimator_time <= _manual_input_policy.maximum_status_age_us
			    && observation.flags_time && observation.flags_time <= now
			    && now - observation.flags_time <= _manual_input_policy.maximum_status_age_us
			    && observation.permission.timestamp && observation.permission.timestamp <= now
			    && now - observation.permission.timestamp <= _manual_input_policy.maximum_permission_age_us
			    && !observation.permission.kill && !observation.permission.lockdown && !observation.permission.termination
			    && !observation.permission.in_esc_calibration_mode && observation.now >= previous.now
			    && observation.estimator_time >= previous.estimator_time && observation.flags_time >= previous.flags_time
			    && observation.permission.timestamp >= previous.permission.timestamp
			    && observation.gyro == previous.gyro && observation.accel == previous.accel
			    && observation.manual.timestamp_sample_us >= _prepared_request.session;

		for (unsigned i = 0; i < research_stabilized_manual::PublisherBinding::kTopics; ++i) {
			same &= observation.revisions[i] && observation.revisions[i] == previous.revisions[i];
		}

		if (!same) { return fail(slaaps_status_s::MANUAL_ENTRY_WAIT_INPUT); }

	} else if (!bound) {
		if (!slaaps::makeManualConfiguration(configuration, _prearm_configuration)
		    || research_stabilized_manual::configureStabilizedManualInput(_prearm_configuration, _prearm_raw, true)
		    != research_actuator_command::ManualControlStatus::Ok) {
			return fail(slaaps_status_s::MANUAL_ENTRY_WAIT_CONFIGURATION);
		}

		if (!(_reentry ? _prearm_input.begin(now, observation, _manual_input_policy) :
		      _prearm_input.beginDisarmed(now, observation, _manual_input_policy))) {
			return fail(slaaps_status_s::MANUAL_ENTRY_WAIT_INPUT);
		}

	} else if (!(_reentry ? _prearm_input.check(now, observation) : _prearm_input.checkDisarmed(now, observation))) {
		return fail(slaaps_status_s::MANUAL_ENTRY_WAIT_INPUT);
	}

	if (configuration.schedule.enabled) {
		// Check the same zero-EDF initial point as worker entry, without acquiring
		// propulsion or requiring active outputs. A ground spool-up exception
		// outside the calibrated table requires a separately defined policy.
		slaaps::ControlSchedule initial_schedule;
		slaaps::ControlSchedule::Step initial{};

		if (!initial_schedule.begin(now, configuration, _condition_sample, 0.F, initial)) {
			return fail(slaaps_status_s::MANUAL_ENTRY_WAIT_SCHEDULE);
		}

		_prearm_configuration.rate_p = initial.control.rate_p;
		_prearm_configuration.rate_i = initial.control.rate_i;
		_prearm_configuration.rate_d = initial.control.rate_d;
		_prearm_configuration.allocation.effectiveness = initial.control.effectiveness;

		if (research_stabilized_manual::StabilizedManualControl::validateConfiguration(_prearm_configuration)
		    != research_stabilized_manual::Status::Ok) {
			return fail(slaaps_status_s::MANUAL_ENTRY_WAIT_SCHEDULE);
		}
	}

	research_actuator_command::RuntimeInput discarded{};
	research_actuator_command::ManualControlReport report{};

	if (_prearm_raw.evaluate(now, observation.manual, discarded, report) != research_actuator_command::ManualControlStatus::Ok) {
		return fail(slaaps_status_s::MANUAL_ENTRY_WAIT_INPUT);
	}

	research_stabilized_manual::Quaternion attitude{};

	if (research_stabilized_manual::validateStabilizedState(now, observation.state, _prearm_configuration,
			bound ? &_prearm_previous_state : nullptr, attitude) != research_stabilized_manual::Status::Ok) {
		return fail(slaaps_status_s::MANUAL_ENTRY_WAIT_STATE);
	}

	for (unsigned axis = 0; axis < slaaps::kAxes; ++axis) {
		if (std::fabs(observation.manual.axes[axis]) > configuration.centered_enter) {
			return fail(slaaps_status_s::MANUAL_ENTRY_WAIT_CENTER);
		}
	}

	float throttle{};

	if (!slaaps::normalizeManualThrottle(
		    observation.manual.axes[static_cast<unsigned>(research_actuator_command::ManualAxis::Throttle)],
		    configuration.manual_throttle_low, configuration.manual_throttle_high, throttle)
	    || throttle > configuration.edf_entry_max) {
		return fail(slaaps_status_s::MANUAL_ENTRY_WAIT_THROTTLE);
	}

	_prearm_previous_state = observation.state;
	_prearm_previous_observation = observation;

	if (configuration.schedule.enabled) { _prearm_condition_source = _condition_sample.source; }

	return true;
}

bool Slaaps::observePower(hrt_abstime &now, bool bind)
{
	slaaps::PowerSample sample{};
	auto result = _configuration.hasCandidate() ? _power.capture(_configuration.configuration().power, sample) :
		      slaaps::PowerObservationResult::InvalidConfiguration;
	now = hrt_absolute_time();

	if (result == slaaps::PowerObservationResult::Valid) {
		if (now < sample.observed_us || now >= sample.deadline_us) {
			result = slaaps::PowerObservationResult::InvalidTimestamp;

		} else if (_power_bound && !slaaps::samePowerSource(sample.source, _power_source)) {
			result = slaaps::PowerObservationResult::PublisherChanged;

		} else if (_power_bound && (sample.timestamp_us < _power_sample.timestamp_us
					    || (sample.timestamp_us == _power_sample.timestamp_us
						&& (sample.voltage_v < _power_sample.voltage_v || sample.voltage_v > _power_sample.voltage_v)))) {
			// A held report may be reread, but its measurement cannot mutate or
			// move backwards while retaining the bound session's identity.
			result = slaaps::PowerObservationResult::InvalidTimestamp;
		}
	}

	_power_result = result;
	_power_valid = result == slaaps::PowerObservationResult::Valid;

	if (!_power_valid) { return false; }

	_power_sample = sample;

	if (bind) {
		_power_source = sample.source;
		_power_bound = true;
	}

	return true;
}

bool Slaaps::publishManualPermission(hrt_abstime now, bool *waiting, slaaps::ControlMode flight_mode,
				     uint64_t transition_us, bool *normal_disarm)
{
	if (waiting) { *waiting = false; }

	if (normal_disarm) { *normal_disarm = false; }

	const bool bench = slaaps::ManualBankPreparation::commissioningMode(_prepared_request.mode);
	const bool edf = _prepared_request.mode == slaaps::ControlMode::EdfBench;
	const auto output_mode = bench ? _prepared_request.mode : flight_mode != slaaps::ControlMode::None ? flight_mode :
				 (_manual.active() ? _manual.mode() : slaaps::ControlMode::Manual);
	const uint64_t output_transition = bench ? 0 : flight_mode != slaaps::ControlMode::None ? transition_us :
					   (_manual.active() ? _manual_permission.transition_us : 0);

	if (!permissionRevision() || !_preparation_session || !_configuration.hasCandidate()
	    || _selected_use != (edf ? 2 : bench ? 0 : 1) || _permission_sequence == UINT64_MAX
	    || (!bench && !slaaps::ManualBankPreparation::flightMode(output_mode))) { return false; }

	const auto &configuration = _configuration.configuration();
	const uint64_t lease = static_cast<uint64_t>(configuration.output_lease_ms) * 1000;

	if (!lease || !slaaps::ManualBankPreparation::activationTimingConfigured(configuration, _prepared_request.mode)) { return false; }

	vehicle_status_s vehicle{};
	actuator_armed_s armed{};
#if defined(SLAAPS_SUPERVISOR_TEST_ACCESS)

	if (!bench && _manual.active()) { SlaapsNativeTest::beforeManualPermission(); }

#endif
	const bool have_vehicle = _vehicle_status.copy(&vehicle);
	const bool have_armed = _actuator_armed.copy(&armed);
	// Timestamp this decision after its live inputs have been copied.
	now = hrt_absolute_time();

	const uint64_t maximum_age = static_cast<uint64_t>(configuration.actuator_permission_age_us);
	// Freshness permits age == maximum_age. An exclusive deadline therefore
	// ends at the following microsecond; later reads cannot renew either input.
	const uint64_t authority_span = maximum_age + 1;

	if (!have_vehicle || !have_armed || !vehicle.timestamp || !armed.timestamp
	    || vehicle.timestamp > now || armed.timestamp > now
	    || now - vehicle.timestamp > maximum_age || now - armed.timestamp > maximum_age
	    || now > UINT64_MAX - lease || vehicle.timestamp > UINT64_MAX - authority_span
	    || armed.timestamp > UINT64_MAX - authority_span) { return false; }

	const bool propulsion = !bench || edf;

	if (propulsion && (!_power_bound || !observePower(now))) { return false; }

	if (now > UINT64_MAX - lease) { return false; }

	if (!bench && _manual.active()) {
		const auto target_index = output_mode == slaaps::ControlMode::Acceleration ? kAccelerationMode : kManualMode;
		const auto other_index = target_index == kAccelerationMode ? kManualMode : kAccelerationMode;
		const bool both_armed = vehicle.arming_state == vehicle_status_s::ARMING_STATE_ARMED && armed.armed;
		const bool selected = vehicle.nav_state == _modes[target_index].id && vehicle.nav_state_user_intention == _modes[target_index].id;

		if (!both_armed || !selected) {
			if (normal_disarm && _configuration.eligible() && _routing_valid && !_registration_fault
			    && _inventory_fault == slaaps::InventoryFault::None && _routing_fault == slaaps::RoutingFault::None
			    && now < _manual.outputDeadline() && now < _manual_permission.deadline_us
			    && (!_manual_first_command_deadline || now < _manual_first_command_deadline)
			    && (_flight_transfer_target == slaaps::ControlMode::None || now < _flight_transfer_deadline)
			    && _manual.commandRevision() == _prepared_request.command_revision
			    && permissionRevision() == _prepared_request.permission_revision
			    && _preparation.revision() && _preparation.transitionRevision()
			    && (!(_prepared_request.binding.banks & 1U)
				|| (_manual_activation_revisions[0] && _main_activation_lifetime.revision() == _manual_activation_revisions[0]))
			    && (!(_prepared_request.binding.banks & 2U)
				|| (_manual_activation_revisions[1] && _aux_activation_lifetime.revision() == _manual_activation_revisions[1]))
			    && ordinaryFlightDisarm(now, maximum_age, vehicle, armed)) { *normal_disarm = true; }

			// A mode edge may occur during these live copies. Retain the exact
			// outgoing permission and issue no outgoing calculation. The next
			// tick must complete a bounded transfer or cancel the original lease.
			if (waiting && both_armed && !_registration_fault && _modes[other_index].configured
			    && vehicle.nav_state == _modes[other_index].id && vehicle.nav_state_user_intention == _modes[other_index].id
			    && !armed.kill && !armed.lockdown && !armed.termination && !armed.in_esc_calibration_mode
			    && _manual_permission.mode == _manual.mode() && now < _manual_permission.deadline_us
			    && now < _manual.outputDeadline()) { *waiting = true; return true; }

			return false;
		}
	}

	slaaps::ManualPermissionSample sample{};
	auto &permission = sample.permission;
	permission.timestamp_us = now;
	permission.deadline_us = math::min(now + lease,
					   math::min(vehicle.timestamp + authority_span, armed.timestamp + authority_span));

	if (propulsion) { permission.deadline_us = math::min(permission.deadline_us, _power_sample.deadline_us); }

	if (!bench && _flight_transfer_target != slaaps::ControlMode::None) {
		if (!_flight_transfer_deadline || now >= _flight_transfer_deadline) { return false; }

		permission.deadline_us = math::min(permission.deadline_us, _flight_transfer_deadline);
	}

	// The Manual wait bounds activation and the first handled command, not
	// ongoing flight. Fresh denied prearm observations cannot extend that bound.
	if (!bench && (!_manual.active() || !_manual.handledSequence())) {
		if (!_manual_entry_deadline || now >= _manual_entry_deadline) { return false; }

		permission.deadline_us = math::min(permission.deadline_us, _manual_entry_deadline);
	}

	permission.session = _preparation_session;
	permission.source = _modes[bench ? kBenchMode : kManualMode].request;
	permission.configuration = _configuration.identity();
	permission.mode = output_mode;
	permission.transition_us = output_transition;
	permission.armed = (have_vehicle && vehicle.arming_state == vehicle_status_s::ARMING_STATE_ARMED)
			   || (have_armed && armed.armed);
	permission.kill = have_armed && armed.kill;
	permission.lockdown = have_armed && armed.lockdown;
	permission.termination = have_armed && armed.termination;
	permission.prearmed = armed.prearmed;

	if (bench) {
		if (!_bench_deadline || now >= _bench_deadline) { return false; }

		permission.deadline_us = math::min(permission.deadline_us, _bench_deadline);
		const bool common = !permission.kill && !permission.lockdown
				    && !permission.termination && !armed.in_esc_calibration_mode && _bench_input_bound
				    && _bench_vehicle_input.revision() == _bench_vehicle_revision
				    && _bench_armed_input.revision() == _bench_armed_revision
				    && _bench_manual_input.revision() == _bench_input_revision
				    && vehicle.nav_state == _modes[kBenchMode].id && vehicle.nav_state_user_intention == _modes[kBenchMode].id;
		const bool both_armed = vehicle.arming_state == vehicle_status_s::ARMING_STATE_ARMED && armed.armed;

		if (edf && both_armed && !_bench_arming_authorized) { return false; }

		const bool partial_arming = (vehicle.arming_state == vehicle_status_s::ARMING_STATE_DISARMED && armed.armed)
					    || (vehicle.arming_state == vehicle_status_s::ARMING_STATE_ARMED && !armed.armed);

		if (edf && partial_arming && !_bench_edf_granted) {
			const auto &prior = _manual_permission;
			const bool denied_baseline = prior.mode == slaaps::ControlMode::EdfBench && prior.session == _preparation_session
						     && prior.source == _prepared_request.source && prior.configuration == _prepared_request.configuration
						     && prior.timestamp_us >= _prepared_request.timestamp_us && prior.timestamp_us <= now
						     && prior.deadline_us > prior.timestamp_us && !prior.armed && !prior.healthy
						     && !prior.flight_admitted && !prior.measured_condition_valid && !prior.commissioning_admitted
						     && !prior.kill && !prior.lockdown && !prior.termination;

			if (!waiting || !common || !_bench_edf_authorized || !_bench_arming_authorized || _bench_disarm_pending
			    || !_preparation_pending || _preparation_status != slaaps::PreparationStatus::Ready || _manual_activation_owned
			    || now >= _preparation_deadline || !denied_baseline) { return false; }

			// Arming can begin between the caller's copies and these copies.
			// Keep the real already-published disarmed denial, including its
			// original timestamp/expiry; never publish an armed denial to wait on.
			*waiting = true;
			return true;
		}

		permission.healthy = common && (edf ? both_armed && _bench_edf_authorized && _bench_arming_authorized && !_bench_disarm_pending :
						!permission.armed && permission.prearmed);
		permission.commissioning_admitted = edf && permission.healthy;
	}

	// Flight admission (SLA_FLT_STAGE, docs/SLAAPS_RECOVERY_POLICY.md). Captive and launcher stages admit
	// SLAAPS Manual/Acceleration only with complete D1 data and a declared calibrated record, a fresh healthy
	// estimator, valid routing and power inside the measured EDF-curve condition. The launcher stage also
	// needs its airspeed source; canard authority there waits for the worker's airspeed gate. Stage Off
	// keeps every flight flag denied. Servo-only Bench health is a separate disarmed/prearmed predicate and
	// never sets either flight flag.
	if (!bench) {
		const bool launcher = static_cast<slaaps::FlightStage>(configuration.launch.stage) == slaaps::FlightStage::Launcher;
		permission.healthy = permission.armed && !permission.kill && !permission.lockdown && !permission.termination
				     && !armed.in_esc_calibration_mode && _input_metadata_valid && _routing_valid && _configuration.eligible()
				     && !_registration_fault;
		// A disarmed permission is always a complete denial, whatever the profile or power state.
		permission.measured_condition_valid = permission.armed && _power_valid && now < _power_sample.deadline_us;
		permission.flight_admitted = permission.armed && slaaps::validateFlightConfiguration(configuration).valid()
					     && (!launcher || _launch_airspeed_valid)
					     && (output_mode != slaaps::ControlMode::Acceleration
						 || slaaps::validateAccelerationControlConfiguration(configuration).valid());
	}

#if defined(SLAAPS_SUPERVISOR_TEST_ACCESS)

	if (!bench) { SlaapsNativeTest::supplyPermission(now, permission); }

#endif
	_manual_permission = permission;
	sample.sequence = ++_permission_sequence;
	slaaps_manual_permission_s wire{};
	const bool published = slaaps::encodeManualPermission(now, sample, lease, wire) && _permission.publish(wire);

	if (published && edf && permission.commissioning_admitted) { _bench_edf_granted = true; }

	return published;
}

bool Slaaps::capturedOrdinaryFlightDisarm(hrt_abstime now)
{
	if (!_companion_protocol.observedOrdinaryDisarm(now) || !_manual.active() || !_configuration.eligible()
	    || _selected_use != 1 || !_routing_valid || _registration_fault
	    || _inventory_fault != slaaps::InventoryFault::None || _routing_fault != slaaps::RoutingFault::None
	    || !_power_bound || !_power_valid || now >= _power_sample.deadline_us
	    || (_configuration.configuration().schedule.enabled && (!_condition_valid || now >= _condition_sample.deadline_us))
	    || now >= _manual.outputDeadline() || now >= _manual_permission.deadline_us
	    || (_manual_first_command_deadline && now >= _manual_first_command_deadline)
	    || (_flight_transfer_target != slaaps::ControlMode::None && now >= _flight_transfer_deadline)
	    || _manual.commandRevision() != _prepared_request.command_revision
	    || permissionRevision() != _prepared_request.permission_revision || !_preparation.revision() || !_preparation.transitionRevision()) { return false; }

	if ((_prepared_request.binding.banks & 1U)
	    && (!_manual_activation_revisions[0] || _main_activation_lifetime.revision() != _manual_activation_revisions[0])) { return false; }

	if ((_prepared_request.binding.banks & 2U)
	    && (!_manual_activation_revisions[1] || _aux_activation_lifetime.revision() != _manual_activation_revisions[1])) { return false; }

	return true;
}

void Slaaps::updateFlightControl(hrt_abstime now, const vehicle_status_s &vehicle,
				 const actuator_armed_s &armed, bool armed_fresh)
{
	const auto &configuration = _configuration.configuration();
	const auto current_mode = _manual.mode();
	const bool manual_selected = _modes[kManualMode].configured && vehicle.nav_state == _modes[kManualMode].id
				     && vehicle.nav_state_user_intention == _modes[kManualMode].id;
	const bool acceleration_selected = _modes[kAccelerationMode].configured && vehicle.nav_state == _modes[kAccelerationMode].id
					   && vehicle.nav_state_user_intention == _modes[kAccelerationMode].id;
	const auto selected_mode = acceleration_selected ? slaaps::ControlMode::Acceleration :
				   manual_selected ? slaaps::ControlMode::Manual : slaaps::ControlMode::None;
	const uint64_t authority_age = configuration.actuator_permission_age_us > 0 ?
				       static_cast<uint64_t>(configuration.actuator_permission_age_us) : 0;
	bool activation_lifetimes = true;

	if (_prepared_request.binding.banks & 1U) {
		activation_lifetimes &= _manual_activation_revisions[0] && _main_activation_lifetime.revision() == _manual_activation_revisions[0];
	}

	if (_prepared_request.binding.banks & 2U) {
		activation_lifetimes &= _manual_activation_revisions[1] && _aux_activation_lifetime.revision() == _manual_activation_revisions[1];
	}

	if (!armed_fresh || !authority_age || !vehicle.timestamp || vehicle.timestamp > now || now - vehicle.timestamp > authority_age
	    || !armed.timestamp || armed.timestamp > now || now - armed.timestamp > authority_age
	    || vehicle.arming_state != vehicle_status_s::ARMING_STATE_ARMED || !armed.armed
	    || armed.kill || armed.lockdown || armed.termination || armed.in_esc_calibration_mode || _registration_fault
	    || !_configuration.eligible() || _selected_use != 1 || !_routing_valid || !activation_lifetimes
	    || !slaaps::ManualBankPreparation::flightMode(current_mode)
	    || _manual.commandRevision() != _prepared_request.command_revision || permissionRevision() != _prepared_request.permission_revision
	    || !_preparation.revision() || !_preparation.transitionRevision()
	    || (_manual_first_command_deadline && now >= _manual_first_command_deadline)
	    || _preparation.pollActivationObservedLive(now, configuration, _routing_snapshot, _inventory_fault) != slaaps::ActivationStatus::Consumed) {
		cancelPreparation(now, slaaps::PreparationStatus::Rejected);
		return;
	}

	// D3 losses while still armed and intact: the SLAAPS mode was left (pilot or failsafe), the power source or the
	// scheduled operating condition is gone. Outside a Manual/Acceleration transfer these hold the canards and ramp
	// EDF off; with no handled frame yet, or during a transfer, they cancel as before.
	const char *loss = !slaaps::ManualBankPreparation::flightMode(selected_mode) ? "mode left" :
			   !observePower(now) ? "power lost" : (configuration.schedule.enabled && !_condition_valid) ? "condition lost" : nullptr;

	if (loss) {
		if (_flight_transfer_target == slaaps::ControlMode::None) { _manual_status = _manual.abandon(slaaps::ManualWorkerStatus::ControlRejected); }

		if (!_manual.recovering()) { cancelPreparation(hrt_absolute_time(), slaaps::PreparationStatus::Rejected); return; }

		mavlink_log_critical(&_diagnostic_log_pub, "SLAAPS: %s, canards hold, EDF ramping off", loss);
		_manual_entry_status = slaaps_status_s::MANUAL_ENTRY_REJECTED;
		updateRecovery(hrt_absolute_time(), vehicle, armed, armed_fresh);
		return;
	}

	if (_flight_transfer_target == slaaps::ControlMode::None && selected_mode != current_mode) {
		slaaps::ManualWorkerOutput handled{};
		uint64_t handled_us{};

		if (_manual_first_command_deadline || _manual.attitudeGateClosed() || !_manual.handledOutput(handled, handled_us) || !handled_us
		    || handled.mode != current_mode || handled.transition_us != _manual_permission.transition_us
		    || !vehicle.nav_state_timestamp || vehicle.nav_state_timestamp > vehicle.timestamp || vehicle.nav_state_timestamp >= now
		    || vehicle.nav_state_timestamp <= _manual_permission.transition_us || vehicle.nav_state_timestamp <= _preparation_session
		    || _manual_permission.timestamp_us > vehicle.nav_state_timestamp || _manual_permission.mode != current_mode
		    || _manual_permission.session != _preparation_session || _manual_permission.source != _prepared_request.source
		    || _manual_permission.configuration != _configuration.identity()
		    || (selected_mode == slaaps::ControlMode::Acceleration && !slaaps::validateAccelerationControlConfiguration(configuration).valid())) {
			cancelPreparation(now, slaaps::PreparationStatus::Rejected);
			return;
		}

		_flight_transfer_us = vehicle.nav_state_timestamp;
		_flight_transfer_target = selected_mode;
		_flight_previous_permission = _manual_permission;
		// The latest issued ticket owns the active lease. The preceding handled
		// frame is genuine history, not the expiry of an already pending frame.
		_flight_transfer_deadline = math::min(_manual.outputDeadline(), _flight_previous_permission.deadline_us);
	}

	if (_flight_transfer_target != slaaps::ControlMode::None) {
		if (selected_mode != _flight_transfer_target || vehicle.nav_state_timestamp != _flight_transfer_us
		    || !_flight_transfer_deadline || now >= _flight_transfer_deadline) {
			cancelPreparation(now, slaaps::PreparationStatus::Rejected);
			return;
		}

		// Drain only the already issued outgoing ticket. This cannot calculate,
		// publish a new old-mode frame or renew any outgoing input/output lease.
		_manual_status = _manual.monitorTransferObserved(_flight_previous_permission, _prepared_request.binding);
		now = hrt_absolute_time();

		if (!_manual.active() || now >= _flight_transfer_deadline
		    || (_manual_status != slaaps::ManualWorkerStatus::Waiting && _manual_status != slaaps::ManualWorkerStatus::Ready)) {
			cancelPreparation(now, slaaps::PreparationStatus::Rejected);
			return;
		}

		_manual_entry_status = slaaps_status_s::MANUAL_ENTRY_ACTIVATING;

		if (_manual.outstanding()) { return; }

		if (selected_mode == slaaps::ControlMode::Acceleration) {
			_acceleration_input = {};
#if defined(SLAAPS_SUPERVISOR_TEST_ACCESS)
			SlaapsNativeTest::beforeAccelerationCapture();
#endif

			if (!_companion_protocol.captureControlInput(_configuration.identity(), _input_observation, _acceleration_input)) {
				const uint64_t stopped = hrt_absolute_time();
				const bool normal_disarm = capturedOrdinaryFlightDisarm(stopped);

				if (normal_disarm || (!_companion_protocol.awaitingControlInput()
						      && !_companion_protocol.awaitingAccelerationObservation(_flight_transfer_us))) {
					cancelPreparation(stopped, slaaps::PreparationStatus::Rejected, normal_disarm);
				}

				return;
			}

#if defined(SLAAPS_SUPERVISOR_TEST_ACCESS)
			SlaapsNativeTest::afterAccelerationCapture(_acceleration_input);
#endif
			_acceleration_input.operating_condition = _condition_sample;

		} else if (_input_observation.manual.timestamp_sample_us <= _flight_transfer_us) {
			return; // A pre-edge held pilot sample cannot resume Manual control.
		}

		bool waiting = false, normal_disarm = false;

		if (!publishManualPermission(now, &waiting, selected_mode, _flight_transfer_us, &normal_disarm)) {
			cancelPreparation(hrt_absolute_time(), slaaps::PreparationStatus::Rejected, normal_disarm);
			return;
		}

		if (waiting) { return; }

		now = hrt_absolute_time();

		if (now >= _flight_transfer_deadline || !slaaps::ManualBankPreparation::permitted(_manual_permission, selected_mode)) {
			cancelPreparation(now, slaaps::PreparationStatus::Rejected);
			return;
		}

		_manual_status = selected_mode == slaaps::ControlMode::Acceleration ?
				 _manual.transferAcceleration(now, _flight_transfer_us, configuration, _flight_previous_permission,
						 _manual_permission, _acceleration_input, _prepared_request.binding) :
				 _manual.transferManual(now, _flight_transfer_us, configuration, _flight_previous_permission,
							_manual_permission, _input_observation, _manual_input_policy, _prepared_request.binding,
							_condition_valid ? &_condition_sample : nullptr);
		_acceleration_input = {};

		if (!_manual.active() || (_manual_status != slaaps::ManualWorkerStatus::Command
					  && _manual_status != slaaps::ManualWorkerStatus::Waiting && _manual_status != slaaps::ManualWorkerStatus::NotDue)) {
			cancelPreparation(hrt_absolute_time(), slaaps::PreparationStatus::Rejected);
			return;
		}

		if (_manual_status == slaaps::ManualWorkerStatus::Command) {
			_flight_transfer_target = slaaps::ControlMode::None;
			_flight_transfer_us = _flight_transfer_deadline = 0;
			_flight_previous_permission = {};
			_manual_entry_status = slaaps_status_s::MANUAL_ENTRY_FIRST_COMMAND;
		}

		return;
	}

	if (current_mode == slaaps::ControlMode::Acceleration && _manual.holding()) {
		updateAccelerationHold(now, vehicle, armed, armed_fresh);
		return;
	}

	if (current_mode == slaaps::ControlMode::Manual && _manual.holding() && reentryReady(now, vehicle)) {
		beginReentry(hrt_absolute_time());
		return;
	}

	if (current_mode == slaaps::ControlMode::Acceleration) {
		_acceleration_input = {};
#if defined(SLAAPS_SUPERVISOR_TEST_ACCESS)
		SlaapsNativeTest::beforeAccelerationCapture();
#endif

		if (!_companion_protocol.captureControlInput(_configuration.identity(), _input_observation, _acceleration_input)) {
			const uint64_t stopped = hrt_absolute_time();
			const bool normal_disarm = capturedOrdinaryFlightDisarm(stopped);

			if (normal_disarm) {
				cancelPreparation(stopped, slaaps::PreparationStatus::Rejected, normal_disarm);
				return;
			}

			if (!_companion_protocol.awaitingControlInput()) {
				holdAfterCompanionLoss(stopped, vehicle, armed, armed_fresh);
				return;
			}

			_manual_status = _manual.monitorTransferObserved(_manual_permission, _prepared_request.binding);

			if (!_manual.active()) { cancelPreparation(hrt_absolute_time(), slaaps::PreparationStatus::Rejected); return; }

			_manual_entry_status = slaaps_status_s::MANUAL_ENTRY_ACTIVATING;
			return; // Map renewal requires a fresh ACK/request inside the old lease.
		}

#if defined(SLAAPS_SUPERVISOR_TEST_ACCESS)
		SlaapsNativeTest::afterAccelerationCapture(_acceleration_input);
#endif
		_acceleration_input.operating_condition = _condition_sample;
	}

	bool waiting = false, normal_disarm = false;

	if (!publishManualPermission(now, &waiting, slaaps::ControlMode::None, 0, &normal_disarm)) {
		cancelPreparation(hrt_absolute_time(), slaaps::PreparationStatus::Rejected, normal_disarm);
		return;
	}

	if (waiting) { _manual_entry_status = slaaps_status_s::MANUAL_ENTRY_ACTIVATING; return; }

	if (!slaaps::ManualBankPreparation::permitted(_manual_permission, current_mode)) {
		cancelPreparation(hrt_absolute_time(), slaaps::PreparationStatus::Rejected);
		return;
	}

	if (_manual.attitudeGateClosed() && _launch_airspeed_valid && _launch_airspeed >= configuration.launch.minimum_airspeed_m_s) {
		_manual.openAttitudeGate();
		mavlink_log_info(&_diagnostic_log_pub, "SLAAPS: launch airspeed reached, attitude authority on\t");
	}

	_manual_status = current_mode == slaaps::ControlMode::Acceleration ?
			 _manual.cycleAccelerationObserved(_manual_permission, _acceleration_input, _prepared_request.binding) :
			 _manual.cycleObserved(_manual_permission, _input_observation, _prepared_request.binding,
					       _manual_first_command_deadline, _condition_valid ? &_condition_sample : nullptr);
	_acceleration_input = {};

	if (_manual.holding()) { reportHold(current_mode == slaaps::ControlMode::Acceleration ? "companion demand infeasible" : "pilot input lost"); }

	if (!_manual.active()) {
		if (!_manual.recovering()) { cancelPreparation(hrt_absolute_time(), slaaps::PreparationStatus::Rejected); return; }

		// D3/D4: a core fault after at least one handled frame holds the canards and ramps EDF off.
		mavlink_log_critical(&_diagnostic_log_pub, "SLAAPS: control lost, canards hold, EDF ramping off");
		_manual_entry_status = slaaps_status_s::MANUAL_ENTRY_REJECTED;
		updateRecovery(hrt_absolute_time(), vehicle, armed, armed_fresh);
		return;
	}

	slaaps::ManualWorkerOutput handled{};
	uint64_t handled_us{};

	if (_manual.handledOutput(handled, handled_us) && handled_us && handled.mode == current_mode
	    && handled.transition_us == _manual_permission.transition_us) {
		_manual_first_command_deadline = 0;
		_manual_entry_status = slaaps_status_s::MANUAL_ENTRY_RUNNING;
	}
}

void Slaaps::publishFlightStatus(hrt_abstime now)
{
	slaaps_flight_status_s status{};
	const bool stabilized = _selected_use == 1 && _configuration.hasCandidate();
	status.flight_stage = stabilized ? static_cast<uint8_t>(_configuration.configuration().launch.stage) : 0;
	status.flight_configuration_valid = stabilized && slaaps::validateFlightConfiguration(_configuration.configuration()).valid();
	status.launch_gate_closed = _manual.attitudeGateClosed();
	status.launch_airspeed_valid = _launch_airspeed_valid;
	status.launch_airspeed_m_s = _launch_airspeed_valid ? _launch_airspeed : NAN;
	status.recovery_state = _manual.holding() ? slaaps_flight_status_s::RECOVERY_HOLD :
				_manual.recovering() ? slaaps_flight_status_s::RECOVERY_RAMP_OFF : slaaps_flight_status_s::RECOVERY_NONE;
	status.recovery_start = _manual.holdStart();
	status.failed_banks = _manual.failedBanks();
	const bool recovering = _manual.holding() || _manual.recovering();

	if (recovering) { (void)observeNet(now); }

	status.net_valid = recovering && _net_valid;
	status.net_distance_m = status.net_valid ? _net_distance_m : NAN;
	status.net_bearing_deg = status.net_valid ? _net_bearing_deg : NAN;
	status.timestamp = hrt_absolute_time();
	_flight_status.publish(status);
}

void Slaaps::observeLaunchAirspeed(hrt_abstime now)
{
	_launch_airspeed_valid = false;

	if (!_configuration.hasCandidate()) { return; }

	const auto &launch = _configuration.configuration().launch;

	if (static_cast<slaaps::FlightStage>(launch.stage) != slaaps::FlightStage::Launcher || launch.airspeed_instance < 0
	    || launch.airspeed_age_ms <= 0) { return; }

	if (_launch_airspeed_sub.get_instance() != static_cast<uint8_t>(launch.airspeed_instance)
	    && !_launch_airspeed_sub.ChangeInstance(static_cast<uint8_t>(launch.airspeed_instance))) { return; }

	airspeed_s airspeed{};

	if (!_launch_airspeed_sub.copy(&airspeed)) { return; }

	const uint64_t age = static_cast<uint64_t>(launch.airspeed_age_ms) * 1000;
	_launch_airspeed_valid = airspeed.timestamp_sample && airspeed.timestamp_sample <= now && now - airspeed.timestamp_sample <= age
				 && PX4_ISFINITE(airspeed.indicated_airspeed_m_s) && airspeed.indicated_airspeed_m_s >= 0.F
				 && PX4_ISFINITE(airspeed.confidence) && airspeed.confidence > 0.F && airspeed.confidence <= 1.F;
	_launch_airspeed = _launch_airspeed_valid ? airspeed.indicated_airspeed_m_s : 0.F;
}

bool Slaaps::observeNet(hrt_abstime now)
{
	static constexpr hrt_abstime kGcsPositionAgeUs = 5000000;
	static constexpr hrt_abstime kVehiclePositionAgeUs = 1000000;
	_net_valid = false;
	int32_t enabled{};
	float north{}, east{};

	if (_net_enabled_param == PARAM_INVALID || param_get(_net_enabled_param, &enabled) != PX4_OK || enabled != 1
	    || param_get(_net_north_param, &north) != PX4_OK || param_get(_net_east_param, &east) != PX4_OK
	    || !PX4_ISFINITE(north) || !PX4_ISFINITE(east)) { return false; }

	follow_target_s gcs{};
	vehicle_global_position_s vehicle{};

	if (!_gcs_position.copy(&gcs) || !gcs.timestamp || gcs.timestamp > now || now - gcs.timestamp > kGcsPositionAgeUs
	    || !PX4_ISFINITE(gcs.lat) || !PX4_ISFINITE(gcs.lon) || (fabs(gcs.lat) < 1.e-7 && fabs(gcs.lon) < 1.e-7)) { return false; }

	if (!_global_position.copy(&vehicle) || !vehicle.timestamp || vehicle.timestamp > now || now - vehicle.timestamp > kVehiclePositionAgeUs
	    || !PX4_ISFINITE(vehicle.lat) || !PX4_ISFINITE(vehicle.lon)) { return false; }

	double net_lat{}, net_lon{};
	add_vector_to_global_position(gcs.lat, gcs.lon, north, east, &net_lat, &net_lon);
	_net_distance_m = get_distance_to_next_waypoint(vehicle.lat, vehicle.lon, net_lat, net_lon);
	_net_bearing_deg = math::degrees(matrix::wrap_2pi(get_bearing_to_next_waypoint(vehicle.lat, vehicle.lon, net_lat, net_lon)));
	_net_valid = PX4_ISFINITE(_net_distance_m) && PX4_ISFINITE(_net_bearing_deg);
	return _net_valid;
}

// D6: once per session at the start of a recovery, tell the operator where the net is.
void Slaaps::reportNet()
{
	if (_net_reported_session == _preparation_session) { return; }

	_net_reported_session = _preparation_session;

	if (observeNet(hrt_absolute_time())) {
		mavlink_log_critical(&_diagnostic_log_pub, "SLAAPS: net %.0f m, bearing %.0f deg\t",
				     static_cast<double>(_net_distance_m), static_cast<double>(_net_bearing_deg));
	}
}

// D5 trigger: SLAAPS Manual selected, the selected pilot input fresh and centered with low throttle, and a complete
// flight profile. Every admission check of a fresh session still follows; this only decides to begin one.
bool Slaaps::reentryReady(hrt_abstime now, const vehicle_status_s &vehicle)
{
	const auto &configuration = _configuration.configuration();

	// The pilot must hold the re-entry condition this long, so a single returning sample cannot end the hold.
	static constexpr hrt_abstime kReentryStableUs = 200000;
	// Recovery ticks run this every cycle: cheap mode checks first, one input capture, and the full profile
	// validation only once the pilot condition itself holds.
	bool ready = _modes[kManualMode].configured && vehicle.nav_state == _modes[kManualMode].id
		     && vehicle.nav_state_user_intention == _modes[kManualMode].id && _configuration.eligible() && _selected_use == 1;

	if (ready) {
		float throttle{};
		const auto snapshot = _inputs.capture(static_cast<uint64_t>(configuration.selected_status_age_us),
						      static_cast<uint64_t>(configuration.actuator_permission_age_us));
		ready = pilotReadyForHandoff(now, &snapshot)
			&& slaaps::normalizeManualThrottle(snapshot.manual.axes[static_cast<unsigned>(research_actuator_command::ManualAxis::Throttle)],
					configuration.manual_throttle_low, configuration.manual_throttle_high, throttle)
			&& throttle <= configuration.edf_entry_max && slaaps::validateFlightConfiguration(configuration).valid();
	}

	// Stability must be observed continuously in this hold or recovery, never carried over from an earlier one.
	const bool continuous = _reentry_checked && now >= _reentry_checked && now - _reentry_checked <= 50000;
	_reentry_checked = now;

	if (!ready || !continuous) { _reentry_ready_since = ready ? now : 0; return false; }

	if (!_reentry_ready_since) { _reentry_ready_since = now; }

	return now - _reentry_ready_since >= kReentryStableUs;
}

// D5: end the lost session without disabling the accepted profile and begin a fresh deliberate Manual entry while
// armed. The banks keep their last handled output until the fresh session's first frame; nothing is renewed.
void Slaaps::beginReentry(hrt_abstime now)
{
	if (_manual_activation_owned) { _preparation.cancelActivation(now); }

	else if (_preparation_pending) { _preparation.cancel(now); }

	_manual.stop();
	_edf.revoke();
	_main_activation_wakeup.unregisterCallback();
	_aux_activation_wakeup.unregisterCallback();
	_main_handled_wakeup.unregisterCallback();
	_aux_handled_wakeup.unregisterCallback();
	_manual_activation_owned = false;
	_manual_first_command_deadline = 0;
	_flight_transfer_target = slaaps::ControlMode::None;
	_flight_transfer_us = _flight_transfer_deadline = 0;
	_flight_previous_permission = {};
	_acceleration_input = {};
	_manual_preparation_response_deadline = 0;
	_manual_permission = {};
	_power_bound = false;

	if (_preparation_session && permissionRevision()) {
		slaaps_manual_permission_s invalid{};
		invalid.timestamp = now;
		_permission.publish(invalid);
	}

	_preparation_pending = false;
	_preparation_status = slaaps::PreparationStatus::Waiting;
	_prearm_input.stop();
	_prearm_raw.reset();
	_reentry_ready_since = 0;
	_reentry = true;
	_manual_entry_selected = true;
	_manual_entry_allowed = false;
	_manual_entry_started = now;
	_manual_entry_deadline = 0;
	_preparation_requested = true;
	_manual_entry_status = slaaps_status_s::MANUAL_ENTRY_PREPARING;
	mavlink_log_critical(&_diagnostic_log_pub, "SLAAPS: pilot input back, re-entering Manual\t");
	events::send(events::ID("slaaps_d5_reentry"), events::Log::Critical, "SLAAPS: pilot input back, re-entering Manual");
}

// One critical QGC alarm plus event per session when a D1 hold starts.
void Slaaps::reportHold(const char *loss)
{
	if (_hold_reported_session == _preparation_session) { return; }

	_hold_reported_session = _preparation_session;
	mavlink_log_critical(&_diagnostic_log_pub, "SLAAPS: %s, holding attitude, EDF hold then off\t", loss);
	reportNet();
	events::send(events::ID("slaaps_d1_hold"), events::Log::Critical, "SLAAPS: command loss, holding attitude, EDF hold then off");
}

// D2 handoff precondition: the selected pilot input is fresh and centered, so the Manual controller's
// own entry check (centered sticks) will accept it. This is an observation, never Manual authority:
// the ordinary transfer path revalidates the exact selected input after Commander's mode edge.
bool Slaaps::pilotReadyForHandoff(hrt_abstime now, const research_stabilized_manual::SelectedStateObservationSnapshot *captured)
{
	const auto &configuration = _configuration.configuration();

	if (!_modes[kManualMode].configured || !configuration.manual_age_ms) { return false; }

	research_stabilized_manual::SelectedStateObservationSnapshot fresh{};

	if (!captured) {
		fresh = _inputs.capture(static_cast<uint64_t>(configuration.selected_status_age_us),
					static_cast<uint64_t>(configuration.actuator_permission_age_us));
		captured = &fresh;
	}

	const auto &manual = captured->manual;
	const uint64_t age = static_cast<uint64_t>(configuration.manual_age_ms) * 1000;

	if (!manual.valid || !manual.timestamp_sample_us || manual.timestamp_sample_us > now || now - manual.timestamp_sample_us > age) { return false; }

	for (unsigned axis = 0; axis < slaaps::kAxes; ++axis) {
		const int32_t slot = configuration.axis[axis].input_axis;

		if (slot < 0 || slot >= static_cast<int32_t>(manual.axes.size()) || !PX4_ISFINITE(manual.axes[slot])
		    || fabsf(manual.axes[slot]) > configuration.centered_enter) { return false; }
	}

	return true;
}

// D2: companion lost in Acceleration. Always alarm; hold (D1) in this session, and when fresh centered
// pilot input exists ask Commander for SLAAPS Manual so the ordinary exclusive transfer can take over.
void Slaaps::holdAfterCompanionLoss(hrt_abstime now, const vehicle_status_s &vehicle, const actuator_armed_s &armed, bool armed_fresh)
{
	mavlink_log_critical(&_diagnostic_log_pub, "SLAAPS: companion lost\t");
	events::send(events::ID("slaaps_companion_lost"), events::Log::Critical, "SLAAPS: companion lost");
	_handoff_attempts = 0;
	_handoff_last_request = 0;
	bool waiting = false, normal_disarm = false;

	if (!publishManualPermission(now, &waiting, slaaps::ControlMode::None, 0, &normal_disarm) || waiting
	    || !slaaps::ManualBankPreparation::permitted(_manual_permission, slaaps::ControlMode::Acceleration)) {
		_manual_status = _manual.abandon(slaaps::ManualWorkerStatus::AccelerationRejected);

		if (!_manual.recovering()) { cancelPreparation(hrt_absolute_time(), slaaps::PreparationStatus::Rejected, normal_disarm); return; }

		mavlink_log_critical(&_diagnostic_log_pub, "SLAAPS: companion lost, canards hold, EDF ramping off\t");
		updateRecovery(hrt_absolute_time(), vehicle, armed, armed_fresh);
		return;
	}

	_manual_status = _manual.holdAcceleration(_manual_permission, _input_observation, _prepared_request.binding,
			 _condition_valid ? &_condition_sample : nullptr);

	if (_manual.holding()) {
		reportHold("companion lost");
		_manual_entry_status = slaaps_status_s::MANUAL_ENTRY_RUNNING;
		return;
	}

	if (!_manual.recovering()) { cancelPreparation(hrt_absolute_time(), slaaps::PreparationStatus::Rejected); return; }

	mavlink_log_critical(&_diagnostic_log_pub, "SLAAPS: companion lost, canards hold, EDF ramping off\t");
	updateRecovery(hrt_absolute_time(), vehicle, armed, armed_fresh);
}

void Slaaps::updateAccelerationHold(hrt_abstime now, const vehicle_status_s &vehicle, const actuator_armed_s &armed, bool armed_fresh)
{
	static constexpr hrt_abstime kHandoffRetryUs = 100000;
	static constexpr uint8_t kHandoffAttempts = 10;

	if (_handoff_attempts < kHandoffAttempts && (!_handoff_last_request || now - _handoff_last_request >= kHandoffRetryUs)
	    && pilotReadyForHandoff(now)) {
		vehicle_command_s command{};
		command.timestamp = now;
		command.command = vehicle_command_s::VEHICLE_CMD_SET_NAV_STATE;
		command.param1 = static_cast<float>(_modes[kManualMode].id);
		command.target_system = command.source_system = vehicle.system_id;
		command.target_component = command.source_component = vehicle.component_id;
		command.from_external = false;
		(void)_disarm_command.publish(command);
		_handoff_last_request = now;

		if (++_handoff_attempts == 1) { mavlink_log_critical(&_diagnostic_log_pub, "SLAAPS: handing off to Manual\t"); }
	}

	bool waiting = false, normal_disarm = false;

	if (!publishManualPermission(now, &waiting, slaaps::ControlMode::None, 0, &normal_disarm)) {
		cancelPreparation(hrt_absolute_time(), slaaps::PreparationStatus::Rejected, normal_disarm);
		return;
	}

	if (waiting) { return; }

	if (!slaaps::ManualBankPreparation::permitted(_manual_permission, slaaps::ControlMode::Acceleration)) {
		cancelPreparation(hrt_absolute_time(), slaaps::PreparationStatus::Rejected);
		return;
	}

	_manual_status = _manual.holdObserved(_manual_permission, _input_observation, _prepared_request.binding,
					      _condition_valid ? &_condition_sample : nullptr);

	if (_manual.active()) { return; }

	if (!_manual.recovering()) { cancelPreparation(hrt_absolute_time(), slaaps::PreparationStatus::Rejected); return; }

	mavlink_log_critical(&_diagnostic_log_pub, "SLAAPS: hold lost, canards hold, EDF ramping off\t");
	_manual_entry_status = slaaps_status_s::MANUAL_ENTRY_REJECTED;
	updateRecovery(hrt_absolute_time(), vehicle, armed, armed_fresh);
}

// Recovery needs only that the vehicle is still armed with no kill, lockdown or termination, and that every bound
// lifetime is unchanged. The registered mode may have left SLAAPS (failsafe or pilot) and power or estimator health may
// be gone: those are exactly the losses recovery exists for. Anything else cancels as before. The permission published
// here is a denial: it grants no flight flag and renews nothing; banks use it only to see that arming continues.
void Slaaps::updateRecovery(hrt_abstime now, const vehicle_status_s &vehicle, const actuator_armed_s &armed, bool armed_fresh)
{
	const auto &configuration = _configuration.configuration();
	const uint64_t authority_age = configuration.actuator_permission_age_us > 0 ?
				       static_cast<uint64_t>(configuration.actuator_permission_age_us) : 0;
	now = hrt_absolute_time();
	bool lifetimes = true;

	if (_prepared_request.binding.banks & 1U) {
		lifetimes &= _manual_activation_revisions[0] && _main_activation_lifetime.revision() == _manual_activation_revisions[0];
	}

	if (_prepared_request.binding.banks & 2U) {
		lifetimes &= _manual_activation_revisions[1] && _aux_activation_lifetime.revision() == _manual_activation_revisions[1];
	}

	if (!armed_fresh || !authority_age || !vehicle.timestamp || vehicle.timestamp > now || now - vehicle.timestamp > authority_age
	    || !armed.timestamp || armed.timestamp > now || now - armed.timestamp > authority_age
	    || vehicle.arming_state != vehicle_status_s::ARMING_STATE_ARMED || !armed.armed
	    || armed.kill || armed.lockdown || armed.termination || armed.in_esc_calibration_mode || _registration_fault
	    || !_configuration.eligible() || !_routing_valid || !lifetimes
	    || _manual.commandRevision() != _prepared_request.command_revision || permissionRevision() != _prepared_request.permission_revision
	    || !publishRecoveryPermission(now, armed)) {
		cancelPreparation(now, slaaps::PreparationStatus::Rejected);
		return;
	}

	reportNet();
	if (reentryReady(now, vehicle)) { beginReentry(hrt_absolute_time()); return; }

	_manual_status = _manual.recoverObserved(_manual_permission, _prepared_request.binding);

	// Finished (EDF off or 60 s), or any transport fault: end the session and publish the invalid permission.
	if (!_manual.recovering()) { cancelPreparation(hrt_absolute_time(), slaaps::PreparationStatus::Rejected); }
}

bool Slaaps::publishRecoveryPermission(hrt_abstime now, const actuator_armed_s &armed)
{
	const auto &previous = _manual_permission;
	const uint64_t lease = static_cast<uint64_t>(_configuration.configuration().output_lease_ms) * 1000;

	if (!permissionRevision() || !_preparation_session || !lease || _permission_sequence == UINT64_MAX || now > UINT64_MAX - lease
	    || previous.session != _preparation_session || !slaaps::ManualBankPreparation::flightMode(previous.mode)
	    || now <= previous.timestamp_us) { return false; }

	slaaps::ManualPermissionSample sample{};
	auto &permission = sample.permission;
	permission = previous;
	permission.timestamp_us = now;
	permission.deadline_us = now + lease;
	permission.armed = true;
	permission.flight_admitted = permission.healthy = permission.measured_condition_valid = false;
	permission.commissioning_admitted = false;
	permission.kill = permission.lockdown = permission.termination = false;
	permission.prearmed = armed.prearmed;
	sample.sequence = ++_permission_sequence;
	slaaps_manual_permission_s wire{};

	if (!slaaps::encodeManualPermission(now, sample, lease, wire) || !_permission.publish(wire)) { return false; }

	_manual_permission = permission;
	return true;
}

void Slaaps::updateManualActivation(hrt_abstime now, const vehicle_status_s &vehicle,
				    const actuator_armed_s &armed, bool armed_fresh)
{
	_manual_entry_allowed = false;
	const auto &configuration = _configuration.configuration();
	const bool selected = !_registration_fault && _modes[kManualMode].configured
			      && vehicle.nav_state == _modes[kManualMode].id
			      && vehicle.nav_state_user_intention == _modes[kManualMode].id;
	const bool both_armed = vehicle.arming_state == vehicle_status_s::ARMING_STATE_ARMED && armed.armed;
	const bool both_disarmed = vehicle.arming_state == vehicle_status_s::ARMING_STATE_DISARMED && !armed.armed;
	const bool current_authority = slaaps::manualActivationTimingConfigured(configuration)
				       && vehicle.timestamp && vehicle.timestamp <= now
				       && now - vehicle.timestamp <= static_cast<uint64_t>(configuration.actuator_permission_age_us)
				       && armed.timestamp && armed.timestamp <= now
				       && now - armed.timestamp <= static_cast<uint64_t>(configuration.actuator_permission_age_us);
	const bool normal_disarm = armed_fresh && current_authority && _configuration.eligible() && _routing_valid
				   && ordinaryFlightDisarm(now, static_cast<uint64_t>(configuration.actuator_permission_age_us), vehicle, armed);

	if (normal_disarm && (_manual.active() || _manual_configuration_active)) {
		cancelPreparation(now, slaaps::PreparationStatus::Rejected, true);
		return;
	}

	if (_manual.active()) { updateFlightControl(now, vehicle, armed, armed_fresh); return; }

	if (_manual.recovering()) { updateRecovery(now, vehicle, armed, armed_fresh); return; }

	bool activation_lifetimes = true;

	if (_prepared_request.binding.banks & 1U) {
		activation_lifetimes &= _manual_activation_revisions[0] && _main_activation_lifetime.revision() == _manual_activation_revisions[0];
	}

	if (_prepared_request.binding.banks & 2U) {
		activation_lifetimes &= _manual_activation_revisions[1] && _aux_activation_lifetime.revision() == _manual_activation_revisions[1];
	}

	if (!armed_fresh || !current_authority || !selected || armed.kill || armed.lockdown || armed.termination || armed.in_esc_calibration_mode
	    || !_configuration.hasCandidate() || _selected_use != 1 || !_routing_valid
	    || !activation_lifetimes || !_manual.commandRevision() || !permissionRevision() || !_preparation.revision()
	    || !_preparation.transitionRevision()
	    || _manual.commandRevision() != _prepared_request.command_revision
	    || permissionRevision() != _prepared_request.permission_revision) {
		cancelPreparation(now, slaaps::PreparationStatus::Rejected);
		return;
	}

	if (!both_armed) {
		if (both_disarmed || _manual_activation_owned || _manual.active() || !_preparation_pending
		    || _preparation_status != slaaps::PreparationStatus::Ready || now >= _preparation_deadline
		    || _preparation.pollObservedLive(now, configuration, _routing_snapshot, _inventory_fault) != slaaps::PreparationStatus::Ready
		    || !observePrearmInput(now, _input_observation.permission.armed)) {
			cancelPreparation(now, slaaps::PreparationStatus::Rejected);

		} else {
			// Commander publishes these topics separately. An unconsumed
			// preparation can wait for fresh agreement without granting output.
			_manual_entry_status = slaaps_status_s::MANUAL_ENTRY_ACTIVATING;
		}

		return;
	}

	// Capture and the two arming topics are sequential observations. A snapshot
	// copied just before arming may finish after the fresh Commander/actuator
	// agreement. Preserve its disarmed checks and wait within the original
	// preparation bound for an actually armed selected-input snapshot.
	if (!_manual_activation_owned && !_manual.active() && !_input_observation.permission.armed) {
		if (_preparation_status != slaaps::PreparationStatus::Ready || now >= _preparation_deadline
		    || !observePrearmInput(now)
		    || _preparation.pollObservedLive(now, configuration, _routing_snapshot, _inventory_fault) != slaaps::PreparationStatus::Ready) {
			cancelPreparation(now, slaaps::PreparationStatus::Rejected);

		} else {
			_manual_entry_status = slaaps_status_s::MANUAL_ENTRY_ACTIVATING;
		}

		return;
	}

	if (!slaaps::manualActivationTimingConfigured(configuration)
	    || !publishManualPermission(now) || !_manual_permission.flight_admitted || !_manual_permission.healthy
	    || !_manual_permission.measured_condition_valid || !_manual_permission.armed) {
		cancelPreparation(now, slaaps::PreparationStatus::Rejected);
		return;
	}

	now = hrt_absolute_time();

	if (!observePrearmInput(now, true)) { cancelPreparation(now, slaaps::PreparationStatus::Rejected); return; }

	if (!_manual_activation_owned) {
		if (!_preparation_pending || _preparation_status != slaaps::PreparationStatus::Ready
		    || now >= _preparation_deadline || now > UINT64_MAX - static_cast<uint64_t>(configuration.activation_ack_timeout_us)) {
			cancelPreparation(now, slaaps::PreparationStatus::Rejected);
			return;
		}

		const uint64_t raw_age = static_cast<uint64_t>(math::min(configuration.manual_age_ms, configuration.output_lease_ms)) * 1000;
		const uint64_t raw_timestamp = _input_observation.manual.timestamp_sample_us;
		uint64_t reserve{};
		now = hrt_absolute_time();

		if (!slaaps::activationReserveUs(configuration, reserve) || raw_timestamp > UINT64_MAX - raw_age
		    || _manual_permission.deadline_us <= now || _manual_permission.deadline_us - now <= reserve
		    || _preparation_deadline <= now || _preparation_deadline - now <= reserve
		    || raw_timestamp + raw_age <= now || raw_timestamp + raw_age - now <= reserve) {
			cancelPreparation(now, slaaps::PreparationStatus::Rejected);
			return;
		}

		const uint64_t deadline = math::min(_preparation_deadline,
						    now + static_cast<uint64_t>(configuration.activation_ack_timeout_us));
		_manual_activation_owned = true; // Includes uncertain/partial activation.

		// Replies can arrive before the next controller period, including when
		// the configured ACK timeout is shorter than that period. Wake on the
		// actual reply; the periodic schedule remains the expiry fallback.
		if (((_prepared_request.binding.banks & 1U)
		     && (!_main_activation_wakeup.registerCallback() || !_main_handled_wakeup.registerCallback()))
		    || ((_prepared_request.binding.banks & 2U)
			&& (!_aux_activation_wakeup.registerCallback() || !_aux_handled_wakeup.registerCallback()))) {
			cancelPreparation(now, slaaps::PreparationStatus::Rejected);
			return;
		}

		if (!_preparation.beginActivationObservedLive(now, configuration, _routing_snapshot, _inventory_fault,
				_manual_activation_revisions, deadline, configuration.first_command_budget_us)) {
			cancelPreparation(now, slaaps::PreparationStatus::Rejected);
			return;
		}

		_preparation_pending = false;
		_manual_entry_status = slaaps_status_s::MANUAL_ENTRY_ACTIVATING;
		return;
	}

	const auto activation = _preparation.pollActivationObservedLive(now, configuration, _routing_snapshot, _inventory_fault);

	if (activation == slaaps::ActivationStatus::Waiting) { return; }

	if (activation != slaaps::ActivationStatus::Ready
	    || !_preparation.consumeActivation(now, _manual_first_command_deadline)
	    || !((_reentry && _manual_configuration_active && _configuration.eligible()) || _configuration.activate())) {
		cancelPreparation(now, slaaps::PreparationStatus::Rejected);
		return;
	}

	_reentry = false;
	_manual_first_command_deadline = math::min(_manual_first_command_deadline, _manual_entry_deadline);
	_manual_configuration_active = true;
	_manual_status = _manual.begin(now, configuration, _manual_permission, _input_observation, _manual_input_policy,
				       _prepared_request.binding, _prepared_request.command_revision, _prepared_request.acknowledgment_revisions,
				       _condition_valid ? &_condition_sample : nullptr);

	if (_manual_status != slaaps::ManualWorkerStatus::Ready) {
		cancelPreparation(now, slaaps::PreparationStatus::Rejected);
		return;
	}

	_prearm_input.stop();
	_prearm_raw.reset();
	_manual_entry_status = slaaps_status_s::MANUAL_ENTRY_FIRST_COMMAND;
}

void Slaaps::updateBench(hrt_abstime now)
{
	vehicle_status_s vehicle{};
	actuator_armed_s armed{};
	unsigned vehicle_revision{}, armed_revision{};
	const bool have_vehicle = _bench_vehicle_input.copyBound(&vehicle, vehicle_revision);
	const bool have_armed = _bench_armed_input.copyBound(&armed, armed_revision);
	now = hrt_absolute_time();
	const auto &configuration = _configuration.configuration();
	const auto &mode = _modes[kBenchMode];
	const bool mode_valid = mode.id >= vehicle_status_s::NAVIGATION_STATE_EXTERNAL1
				&& mode.id <= vehicle_status_s::NAVIGATION_STATE_EXTERNAL8;
	const bool selected = mode_valid && have_vehicle && vehicle.nav_state_user_intention == mode.id;
	const uint64_t age = configuration.actuator_permission_age_us > 0 ?
			     static_cast<uint64_t>(configuration.actuator_permission_age_us) : kMaximumStatusAgeUs;
	const bool fresh = have_vehicle && have_armed && vehicle_revision && armed_revision
			   && vehicle.timestamp && vehicle.timestamp <= now && now - vehicle.timestamp <= age
			   && armed.timestamp && armed.timestamp <= now && now - armed.timestamp <= age
			   && vehicle_revision == _bench_vehicle_input.revision() && armed_revision == _bench_armed_input.revision();
	const bool disarmed = fresh && vehicle.arming_state == vehicle_status_s::ARMING_STATE_DISARMED && !armed.armed;
	const bool healthy = fresh && !armed.kill && !armed.lockdown && !armed.termination && !armed.in_esc_calibration_mode;
	const bool uninhibited = disarmed && healthy;
	const bool both_armed = fresh && vehicle.arming_state == vehicle_status_s::ARMING_STATE_ARMED && armed.armed;
	_bench_can_arm = false;
	_bench_can_arm_deadline = 0;

	if (_bench_disarm_pending) { updateBenchDisarm(now, vehicle, armed, fresh); return; }

	const bool session = _preparation_session && slaaps::ManualBankPreparation::commissioningMode(_prepared_request.mode);
	const bool edf = session ? _prepared_request.mode == slaaps::ControlMode::EdfBench : _selected_use == 2;
	const auto control_mode = edf ? slaaps::ControlMode::EdfBench : slaaps::ControlMode::ServoBench;
	bool registered = !_registration_fault;

	for (const auto &item : _modes) { registered &= item.configured; }

	int32_t readiness_target = -1;
	const bool coherent_target = param_get(_bench_target_parameter, &readiness_target) == 0
				     && (edf ? readiness_target == static_cast<int32_t>(kEdfBenchTarget) :
					 readiness_target >= 0 && readiness_target <= static_cast<int32_t>(slaaps::kCanards));
	const bool configured = _bench_controls_initialized && _configuration.hasCandidate() && _selected_use == (edf ? 2 : 0)
				&& slaaps::ManualBankPreparation::validConfiguration(configuration, control_mode);
	const bool power_ready = !edf || observePower(now);
	_bench_can_run = configured && coherent_target && mode_valid && registered && healthy && power_ready
			 && (disarmed || (edf && session && _bench_edf_authorized && _bench_arming_authorized)) && _routing_valid
			 && _manual.commandRevision() && permissionRevision() && _preparation.revision() && _preparation.transitionRevision();
	slaaps::RoutingPlan available_routing{};

	if (_bench_can_run) {
		_bench_can_run = slaaps::makeRoutingPlan(configuration, _routing_snapshot.inventory, edf, available_routing).valid();

		if (available_routing.bank_mask & 1U) {
			_bench_can_run &= _main_ready_lifetime.revision() && _main_ack_lifetime.revision() && _main_activation_lifetime.revision();
		}

		if (available_routing.bank_mask & 2U) {
			_bench_can_run &= _aux_ready_lifetime.revision() && _aux_ack_lifetime.revision() && _aux_activation_lifetime.revision();
		}
	}

	if (!selected) {
		if (_bench_entry_selected) { cancelBench(now, slaaps_status_s::BENCH_ENTRY_INACTIVE, uninhibited); }

		if (_bench_disarm_pending) { return; }

		_bench_entry_selected = false;
		_bench_entry_allowed = uninhibited;
		return;
	}

	if (!_bench_entry_selected) {
		_bench_entry_selected = true;

		if (_preparation_session || _preparation_requested || _manual_entry_deadline || _manual.active() || _prearm_input.active()) {
			cancelPreparation(now, slaaps::PreparationStatus::Inactive);
		}

		_manual_entry_selected = false;
		_manual_entry_allowed = disarmed;

		if (!_bench_entry_allowed || !uninhibited) { cancelBench(now, slaaps_status_s::BENCH_ENTRY_REJECTED); return; }

		_bench_entry_allowed = false;
		_bench_entry_started = now;
		_bench_vehicle_revision = vehicle_revision;
		_bench_armed_revision = armed_revision;
		_bench_enable_ready = false;
		_bench_entry_status = slaaps_status_s::BENCH_ENTRY_WAIT_CONFIGURATION;
	}

	if (_bench_entry_status == slaaps_status_s::BENCH_ENTRY_REJECTED || _bench_entry_status == slaaps_status_s::BENCH_ENTRY_COMPLETE) {
		(void)resetBenchEnable();
		return;
	}

	int32_t enable{}, target{}, seconds{};
	auto read_controls = [&](int32_t &en, int32_t &ch, int32_t &sec) {
		const AtomicTransaction transaction;
		return _bench_controls_initialized && param_get(_bench_enable_parameter, &en) == 0
		       && param_get(_bench_target_parameter, &ch) == 0 && param_get(_bench_seconds_parameter, &sec) == 0
		       && en >= 0 && en <= 1 && ch >= 0 && ch <= static_cast<int32_t>(kEdfBenchTarget)
		       && sec >= 0 && sec <= kMaximumBenchSeconds;
	};
	const bool controls = read_controls(enable, target, seconds);

	if (!controls || !healthy || (!edf && !disarmed) || (edf && !disarmed && !_bench_arming_authorized)
	    || vehicle_revision != _bench_vehicle_revision || armed_revision != _bench_armed_revision) {
		cancelBench(now, slaaps_status_s::BENCH_ENTRY_REJECTED);
		return;
	}

	if (session && (target != _bench_target || seconds != _bench_seconds)) {
		cancelBench(now, slaaps_status_s::BENCH_ENTRY_REJECTED);
		return;
	}

	if (session && (!enable || now >= _bench_deadline)) {
		cancelBench(now, slaaps_status_s::BENCH_ENTRY_COMPLETE, true);
		return;
	}

	if (edf && ((session && _bench_edf_granted) || _manual.active() || _manual_activation_owned) && !both_armed) {
		cancelBench(now, disarmed ? slaaps_status_s::BENCH_ENTRY_COMPLETE : slaaps_status_s::BENCH_ENTRY_REJECTED, disarmed);
		return;
	}

	if (!configured || !coherent_target || !seconds || !_routing_valid || !registered || (!edf && !armed.prearmed)
	    || vehicle.nav_state != mode.id) {
		_bench_enable_ready = false;

		if (session || enable || (!configured && _bench_input_bound)) { cancelBench(now, slaaps_status_s::BENCH_ENTRY_REJECTED); return; }

		_bench_entry_status = !configured || !coherent_target || !seconds ? slaaps_status_s::BENCH_ENTRY_WAIT_CONFIGURATION :
				      !_routing_valid || !registered ? slaaps_status_s::BENCH_ENTRY_WAIT_ROUTING :
				      !edf && !armed.prearmed ? slaaps_status_s::BENCH_ENTRY_WAIT_PREARM : slaaps_status_s::BENCH_ENTRY_WAIT_SELECTION;
		return;
	}

	if (!power_ready) {
		_bench_enable_ready = false;

		if (session || enable) { cancelBench(now, slaaps_status_s::BENCH_ENTRY_REJECTED); }

		else { _bench_entry_status = slaaps_status_s::BENCH_ENTRY_WAIT_POWER; }

		return;
	}

	if (!observeBenchInput(now, !_manual.active(), edf)) {
		_bench_enable_ready = false;

		if (session || enable || _bench_entry_status == slaaps_status_s::BENCH_ENTRY_REJECTED) {
			cancelBench(now, slaaps_status_s::BENCH_ENTRY_REJECTED);
		}

		return;
	}

	// Linearize enable after readiness/input observation. A command concurrent
	// with that observation cannot become a previously observed ready-state edge.
	int32_t final_enable{}, final_target{}, final_seconds{};
	const bool final_controls = read_controls(final_enable, final_target, final_seconds);

	if (!final_controls || final_enable != enable
	    || final_target != target || final_seconds != seconds) {
		_bench_enable_ready = false;

		// Operator stop can arrive during the raw-input observation. Preserve
		// its normal-stop semantics without accepting any in-place target edit.
		if (session && final_controls && !final_enable && final_target == _bench_target && final_seconds == _bench_seconds) {
			cancelBench(now, slaaps_status_s::BENCH_ENTRY_COMPLETE, true);

		} else if (session || enable || final_enable) {
			cancelBench(now, slaaps_status_s::BENCH_ENTRY_REJECTED);
		}

		return;
	}

	if (!session) {
		_bench_entry_status = slaaps_status_s::BENCH_ENTRY_READY;

		if (!enable) { _bench_enable_ready = true; return; }

		if (!_bench_enable_ready) { cancelBench(now, slaaps_status_s::BENCH_ENTRY_REJECTED); return; }

		_bench_enable_ready = false;
		const uint64_t duration = static_cast<uint64_t>(seconds) * 1000000;
		const uint64_t preparation_limit = slaaps::ManualBankPreparation::maximumPreparationUs(control_mode);

		if (now > UINT64_MAX - duration || now > UINT64_MAX - preparation_limit) {
			cancelBench(now, slaaps_status_s::BENCH_ENTRY_REJECTED);
			return;
		}

		_bench_target = target;
		_bench_seconds = seconds;
		_bench_deadline = now + duration;
		slaaps::RoutingPlan routing{};

		if (!slaaps::makeRoutingPlan(configuration, _routing_snapshot.inventory, edf, routing).valid()) {
			cancelBench(now, slaaps_status_s::BENCH_ENTRY_REJECTED); return;
		}

		unsigned ready[2] {_main_ready_lifetime.revision(), _aux_ready_lifetime.revision()};
		const unsigned handled[2] {_main_ack_lifetime.revision(), _aux_ack_lifetime.revision()};
		const unsigned activation[2] {_main_activation_lifetime.revision(), _aux_activation_lifetime.revision()};
		slaaps::ManualPreparationRequest request{};
		request.timestamp_us = now;
		request.deadline_us = math::min(_bench_deadline, now + preparation_limit);
		request.session = _bench_entry_started; // Mint once on this enable edge; fresh post-entry raw input remains usable.
		request.source = mode.request;
		request.configuration = _configuration.identity();
		request.command_revision = _manual.commandRevision();
		request.permission_revision = permissionRevision();
		request.binding.routing = now;
		request.binding.banks = routing.bank_mask;
		request.mode = control_mode;

		for (unsigned bank = 0; bank < 2; ++bank) {
			const bool participating = routing.bank_mask & (1U << bank);
			ready[bank] = participating ? ready[bank] : 0;
			_manual_activation_revisions[bank] = participating ? activation[bank] : 0;

			if (!participating) { continue; }

			if (!ready[bank] || !handled[bank] || !activation[bank]) { cancelBench(now, slaaps_status_s::BENCH_ENTRY_REJECTED); return; }

			request.acknowledgment_revisions[bank] = handled[bank];
			request.binding.driver[bank] = _routing_snapshot.driver[bank];
		}

		if (edf && !observePower(now, true)) { cancelBench(now, slaaps_status_s::BENCH_ENTRY_REJECTED); return; }

		_edf.revoke();
		_prepared_request = request;
		_preparation_session = request.session;
		_preparation_deadline = request.deadline_us;
		_preparation_banks = request.binding.banks;
		_bench_edf_authorized = edf;
		_bench_edf_granted = false;
		_bench_entry_status = slaaps_status_s::BENCH_ENTRY_PREPARING;

		if (!publishManualPermission(now)) { cancelBench(now, slaaps_status_s::BENCH_ENTRY_REJECTED); return; }

		now = hrt_absolute_time();
		publishStatus(now);
		const slaaps::ReportedRoutingPolicy policy{2 * kIntervalUs, slaaps::kMaximumOutputReportAgeUs};
		_preparation_pending = _preparation.beginObserved(now, configuration, _routing_snapshot, _inventory_fault, policy, request, ready);
		_preparation_status = _preparation_pending ? slaaps::PreparationStatus::Waiting : slaaps::PreparationStatus::Rejected;

		if (!_preparation_pending) { cancelBench(now, slaaps_status_s::BENCH_ENTRY_REJECTED); }

		return;
	}

	bool lifetimes = _manual.commandRevision() == _prepared_request.command_revision
			 && permissionRevision() == _prepared_request.permission_revision && _preparation.revision() && _preparation.transitionRevision();

	if (_prepared_request.binding.banks & 1U) { lifetimes &= _main_activation_lifetime.revision() == _manual_activation_revisions[0]; }

	if (_prepared_request.binding.banks & 2U) { lifetimes &= _aux_activation_lifetime.revision() == _manual_activation_revisions[1]; }

	// A retained explicitly disarmed denial is allowed while the two live arming
	// publications converge. Never publish a fabricated disarmed snapshot or a
	// current armed denial that could later be silently revived by a grant.
	const bool publish_permission = !edf || disarmed || both_armed;
	bool permission_waiting{};

	if (!lifetimes || (publish_permission && !publishManualPermission(now, &permission_waiting))
	    || ((!edf || both_armed) && !permission_waiting && !slaaps::ManualBankPreparation::permitted(_manual_permission, control_mode))) {
		cancelBench(now, slaaps_status_s::BENCH_ENTRY_REJECTED);
		return;
	}

	now = hrt_absolute_time();

	if (_manual.active()) {
		if (!_configuration.eligible()
		    || _preparation.pollActivationObservedLive(now, configuration, _routing_snapshot, _inventory_fault) != slaaps::ActivationStatus::Consumed) {
			cancelBench(now, slaaps_status_s::BENCH_ENTRY_REJECTED); return;
		}

		_manual_status = edf ? _manual.cycleEdfBenchObserved(_manual_permission, _bench_sample, _prepared_request.binding,
				 _manual_first_command_deadline) :
				 _manual.cycleBenchObserved(_manual_permission, _bench_sample, _prepared_request.binding, _manual_first_command_deadline);

		if (!_manual.active()) { cancelBench(now, slaaps_status_s::BENCH_ENTRY_REJECTED); return; }

		if (_manual.handledSequence()) {
			_manual_first_command_deadline = 0;
			_bench_entry_status = edf ? slaaps_status_s::BENCH_ENTRY_EDF_RUNNING : slaaps_status_s::BENCH_ENTRY_RUNNING;
		}

		return;
	}

	if (_preparation_pending) {
		_preparation_status = _preparation.pollObservedLive(now, configuration, _routing_snapshot, _inventory_fault);

		if (_preparation_status == slaaps::PreparationStatus::Waiting) { return; }

		if (_preparation_status != slaaps::PreparationStatus::Ready
		    || now > UINT64_MAX - static_cast<uint64_t>(configuration.activation_ack_timeout_us)) {
			cancelBench(now, slaaps_status_s::BENCH_ENTRY_REJECTED); return;
		}

		uint64_t edf_raw_deadline{};

		if (edf) {
			_bench_arming_authorized = true;
			_bench_can_arm = disarmed && _bench_can_run && !permission_waiting;
			// Match ManualEdfInput's original, exclusive raw-command lease.
			const uint64_t raw_age = static_cast<uint64_t>(math::min(configuration.manual_age_ms, configuration.output_lease_ms)) * 1000;
			uint64_t reserve{};

			if (_bench_sample.timestamp_sample_us > UINT64_MAX - raw_age
			    || !slaaps::activationReserveUs(configuration, reserve)) { cancelBench(now, slaaps_status_s::BENCH_ENTRY_REJECTED); return; }

			edf_raw_deadline = _bench_sample.timestamp_sample_us + raw_age;
			const uint64_t original_deadline = math::min(_preparation_deadline,
							   math::min(_manual_permission.deadline_us, edf_raw_deadline));
			// Readiness must leave the declared activation window and first-command
			// reserve on the original inputs, without requiring armed outputs.
			_bench_can_arm_deadline = original_deadline > reserve ? original_deadline - reserve : 0;
			_bench_can_arm &= now < _bench_can_arm_deadline;
			_bench_entry_status = slaaps_status_s::BENCH_ENTRY_WAIT_ARM;

			if (!both_armed || permission_waiting) { return; }

			_bench_can_arm = false;
		}

		uint64_t reserve{};
		now = hrt_absolute_time();

		if (!slaaps::activationReserveUs(configuration, reserve)
		    || _manual_permission.deadline_us <= now || _manual_permission.deadline_us - now <= reserve
		    || _preparation_deadline <= now || _preparation_deadline - now <= reserve
		    || (edf && (edf_raw_deadline <= now || edf_raw_deadline - now <= reserve))) {
			cancelBench(now, slaaps_status_s::BENCH_ENTRY_REJECTED); return;
		}

		_manual_activation_owned = true;

		if (((_prepared_request.binding.banks & 1U)
		     && (!_main_activation_wakeup.registerCallback() || !_main_handled_wakeup.registerCallback()))
		    || ((_prepared_request.binding.banks & 2U)
			&& (!_aux_activation_wakeup.registerCallback() || !_aux_handled_wakeup.registerCallback()))) {
			cancelBench(now, slaaps_status_s::BENCH_ENTRY_REJECTED); return;
		}

		const uint64_t deadline = math::min(_preparation_deadline, now + static_cast<uint64_t>(configuration.activation_ack_timeout_us));

		if (!_preparation.beginActivationObservedLive(now, configuration, _routing_snapshot, _inventory_fault,
				_manual_activation_revisions, deadline, configuration.first_command_budget_us)) {
			cancelBench(now, slaaps_status_s::BENCH_ENTRY_REJECTED); return;
		}

		_preparation_pending = false;
		_bench_entry_status = slaaps_status_s::BENCH_ENTRY_ACTIVATING;
		return;
	}

	const auto activation = _preparation.pollActivationObservedLive(now, configuration, _routing_snapshot, _inventory_fault);

	if (activation == slaaps::ActivationStatus::Waiting) { return; }

	if (!_manual_activation_owned || activation != slaaps::ActivationStatus::Ready
	    || !_preparation.consumeActivation(now, _manual_first_command_deadline) || !_configuration.activate()) {
		cancelBench(now, slaaps_status_s::BENCH_ENTRY_REJECTED); return;
	}

	_manual_configuration_active = true;
	_manual_status = edf ? _manual.beginEdfBench(now, configuration, _manual_permission, _bench_sample, _prepared_request.binding,
			 _prepared_request.command_revision, _prepared_request.acknowledgment_revisions) :
			 _manual.beginBench(now, configuration, _manual_permission, _bench_sample, _prepared_request.binding,
					    _prepared_request.command_revision, _prepared_request.acknowledgment_revisions, static_cast<unsigned>(_bench_target));

	if (_manual_status != slaaps::ManualWorkerStatus::Ready) { cancelBench(now, slaaps_status_s::BENCH_ENTRY_REJECTED); return; }

	_bench_entry_status = slaaps_status_s::BENCH_ENTRY_FIRST_COMMAND;
}

void Slaaps::updatePreparation(hrt_abstime now)
{
	vehicle_status_s vehicle{};
	const bool have_vehicle = _vehicle_status.copy(&vehicle);
	now = hrt_absolute_time();
	const bool fresh = have_vehicle && vehicle.timestamp && vehicle.timestamp <= now
			   && now - vehicle.timestamp <= kMaximumStatusAgeUs;

	if (!fresh) {
		_manual_entry_allowed = false;
		_manual_profile_restore_pending = false;

		if (_manual_entry_selected || _preparation_requested || _preparation_pending || _manual_activation_owned || _manual.active()) {
			cancelPreparation(now, slaaps::PreparationStatus::Rejected);
		}

		return;
	}

	const auto &manual_mode = _modes[kManualMode];

	if (manual_mode.id < vehicle_status_s::NAVIGATION_STATE_EXTERNAL1
	    || manual_mode.id > vehicle_status_s::NAVIGATION_STATE_EXTERNAL8) {
		_manual_entry_allowed = false;

		if (_manual_activation_owned || _manual.active()) { cancelPreparation(now, slaaps::PreparationStatus::Rejected); }

		return;
	}

	const bool selected = vehicle.nav_state_user_intention == manual_mode.id;
	actuator_armed_s armed{};
	const bool have_armed = _actuator_armed.copy(&armed);
	now = hrt_absolute_time();
	const bool actuator_fresh = have_armed && armed.timestamp && armed.timestamp <= now
				    && now - armed.timestamp <= kMaximumStatusAgeUs;

	if (restoreStoppedProfile(now, vehicle, armed, actuator_fresh)) { return; }

	const bool reentry_preparing = _reentry && !_manual_activation_owned && !_manual.active() && !_manual.recovering()
				       && !(_preparation_pending && _preparation_status == slaaps::PreparationStatus::Ready);

	if (!reentry_preparing && (vehicle.arming_state != vehicle_status_s::ARMING_STATE_DISARMED || (actuator_fresh && armed.armed)
				   || _manual_activation_owned || _manual.active() || _manual_entry_status == slaaps_status_s::MANUAL_ENTRY_ACTIVATING)) {
		_manual_entry_selected |= selected;
		updateManualActivation(now, vehicle, armed, actuator_fresh);
		return;
	}

	if (!selected) {
		// An observed fresh disarmed exit is required even after supervisor
		// restart. An already-selected external slot is not a new entry grant.
		_manual_entry_selected = false;
		_manual_entry_allowed = true;
		_manual_entry_started = 0;
		cancelPreparation(now, slaaps::PreparationStatus::Inactive);
		return;
	}

	if (!_manual_entry_selected) {
		_manual_entry_selected = true;

		if (!_manual_entry_allowed) {
			cancelPreparation(now, slaaps::PreparationStatus::Rejected);
			return;
		}

		_manual_entry_allowed = false;
		_manual_entry_started = now;
		_manual_entry_deadline = 0;
		_manual_preparation_response_deadline = 0;
		_preparation_requested = true;
		_preparation_status = slaaps::PreparationStatus::Waiting;
		_manual_entry_status = slaaps_status_s::MANUAL_ENTRY_WAIT_CONFIGURATION;
	}

	if (!_preparation_requested && !_preparation_pending) { return; }

	if (_manual_entry_deadline && now >= _manual_entry_deadline) {
		cancelPreparation(now, slaaps::PreparationStatus::Rejected);
		return;
	}

	const bool bound = _prearm_input.active();

	const bool armed_state = _reentry ? (vehicle.arming_state == vehicle_status_s::ARMING_STATE_ARMED && armed.armed) :
				 (vehicle.arming_state == vehicle_status_s::ARMING_STATE_DISARMED && !armed.armed);

	if (!actuator_fresh || !armed_state || armed.kill || armed.lockdown || armed.termination || armed.in_esc_calibration_mode) {
		if (bound || _preparation_pending || actuator_fresh) {
			cancelPreparation(now, slaaps::PreparationStatus::Rejected);

		} else {
			_manual_entry_status = slaaps_status_s::MANUAL_ENTRY_WAIT_INPUT;
		}

		return;
	}

	if (!_configuration.hasCandidate() || _selected_use != 1
	    || !slaaps::manualActivationTimingConfigured(_configuration.configuration())) {
		if (bound || _preparation_pending || _manual_entry_deadline) {
			cancelPreparation(now, slaaps::PreparationStatus::Rejected);

		} else {
			_manual_entry_status = slaaps_status_s::MANUAL_ENTRY_WAIT_CONFIGURATION;
		}

		return;
	}

	if (!_manual_entry_deadline) {
		// A re-entry is bounded like an initial bank response, never by the disarmed operator window.
		const uint64_t wait = _reentry ? math::min(static_cast<uint64_t>(_configuration.configuration().manual_ready_wait_s) * 1000000,
					slaaps::ManualBankPreparation::kMaximumPreparationUs) :
				      static_cast<uint64_t>(_configuration.configuration().manual_ready_wait_s) * 1000000;

		if (!_manual_entry_started || now < _manual_entry_started || _manual_entry_started > UINT64_MAX - wait
		    || now >= _manual_entry_started + wait) {
			cancelPreparation(now, slaaps::PreparationStatus::Rejected);
			return;
		}

		// Waiting for initial configuration/input/routing never restarts this clock.
		_manual_entry_deadline = _manual_entry_started + wait;
	}

	_manual_input_policy = {static_cast<uint64_t>(_configuration.configuration().selected_status_age_us),
				static_cast<uint64_t>(_configuration.configuration().actuator_permission_age_us)
			       };

	if (_manual.active() || _manual.recovering() || _registration_fault || !_preparation.revision() || !_manual.commandRevision()
	    || !permissionRevision()) {
		cancelPreparation(now, slaaps::PreparationStatus::Rejected);
		return;
	}

	bool registered = true;

	for (const auto &mode : _modes) { registered &= mode.configured; }

	if (!_routing_valid || !registered) {
		if (bound || _preparation_pending) {
			cancelPreparation(now, slaaps::PreparationStatus::Rejected);

		} else {
			_manual_entry_status = slaaps_status_s::MANUAL_ENTRY_WAIT_ROUTING;
		}

		return;
	}

	if (!observePower(now)) {
		if (bound || _preparation_pending) { cancelPreparation(now, slaaps::PreparationStatus::Rejected); }

		else { _manual_entry_status = slaaps_status_s::MANUAL_ENTRY_WAIT_POWER; }

		return;
	}

	if (!observePrearmInput(now)) {
		if (bound || _preparation_pending) { cancelPreparation(now, slaaps::PreparationStatus::Rejected); }

		return;
	}

	if (_preparation_pending) {
		const bool already_ready = _preparation_status == slaaps::PreparationStatus::Ready;

		if ((!already_ready && (!_manual_preparation_response_deadline || now >= _manual_preparation_response_deadline))
		    || !publishManualPermission(now)) { cancelPreparation(now, slaaps::PreparationStatus::Rejected); return; }

		_preparation_status = _preparation.pollObservedLive(now, _configuration.configuration(), _routing_snapshot, _inventory_fault);

		if (_preparation_status == slaaps::PreparationStatus::Rejected
		    || (!already_ready && now >= _manual_preparation_response_deadline)) {
			cancelPreparation(now, slaaps::PreparationStatus::Rejected);

		} else {
			_manual_entry_status = _preparation_status == slaaps::PreparationStatus::Ready ?
					       slaaps_status_s::MANUAL_ENTRY_PREPARED : slaaps_status_s::MANUAL_ENTRY_PREPARING;
		}

		return;
	}

	_preparation_status = slaaps::PreparationStatus::Waiting;
	_manual_entry_status = slaaps_status_s::MANUAL_ENTRY_WAIT_ROUTING;

	slaaps::RoutingPlan routing{};

	if (!slaaps::makeRoutingPlan(_configuration.configuration(), _routing_snapshot.inventory, true, routing).valid()) { return; }

	unsigned ready_revisions[2] {_main_ready_lifetime.revision(), _aux_ready_lifetime.revision()};
	const unsigned ack_revisions[2] {_main_ack_lifetime.revision(), _aux_ack_lifetime.revision()};
	const unsigned activation_revisions[2] {_main_activation_lifetime.revision(), _aux_activation_lifetime.revision()};
	slaaps::ManualPreparationRequest request{};
	request.timestamp_us = now;

	// Keep the initial response limit separate from the immutable operator
	// window. Neither is armed permission or qualified hardware timing.
	if (now >= _manual_entry_deadline || now > UINT64_MAX - slaaps::ManualBankPreparation::kMaximumPreparationUs) {
		cancelPreparation(now, slaaps::PreparationStatus::Rejected);
		return;
	}

	request.deadline_us = _manual_entry_deadline;
	request.session = _manual_entry_started;
	request.reentry = _reentry;
	_manual_preparation_response_deadline = math::min(request.deadline_us,
						request.timestamp_us + slaaps::ManualBankPreparation::kMaximumPreparationUs);
	request.source = _modes[kManualMode].request;
	request.configuration = _configuration.identity();
	request.command_revision = _manual.commandRevision();
	request.permission_revision = permissionRevision();
	request.binding.routing = now;
	request.binding.banks = routing.bank_mask;

	for (unsigned bank = 0; bank < 2; ++bank) {
		if (!(routing.bank_mask & (1U << bank))) {
			ready_revisions[bank] = 0;
			_manual_activation_revisions[bank] = 0;
			continue;
		}

		if (!ready_revisions[bank] || !ack_revisions[bank] || !activation_revisions[bank]) { return; }

		_manual_activation_revisions[bank] = activation_revisions[bank];
		request.acknowledgment_revisions[bank] = ack_revisions[bank];
		request.binding.driver[bank] = _routing_snapshot.driver[bank];
	}

	const slaaps::ReportedRoutingPolicy policy{2 * kIntervalUs, slaaps::kMaximumOutputReportAgeUs};

	if (!observePower(now, true)) { cancelPreparation(now, slaaps::PreparationStatus::Rejected); return; }

	_preparation_requested = false;
	_preparation_session = request.session;
	_preparation_deadline = request.deadline_us;
	_preparation_banks = request.binding.banks;
	_prepared_request = request;
	_manual_entry_status = slaaps_status_s::MANUAL_ENTRY_PREPARING;

	if (!publishManualPermission(now)) { cancelPreparation(now, slaaps::PreparationStatus::Rejected); return; }

	// The bank can run immediately after proposal publication. Publish the exact
	// configuration/endpoint registration snapshot first, not at the end of Run.
	publishStatus(now);
	_preparation_pending = _preparation.beginObserved(now, _configuration.configuration(), _routing_snapshot,
			       _inventory_fault, policy, request, ready_revisions);
	_preparation_status = _preparation_pending ? slaaps::PreparationStatus::Waiting : slaaps::PreparationStatus::Rejected;

	if (!_preparation_pending) { cancelPreparation(now, slaaps::PreparationStatus::Rejected); }
}

void Slaaps::updateCompanion(hrt_abstime now)
{
	// A deliberate disarmed companion Grant may preauthorize protocol entry.
	// This does not depend on an active worker or confer flight/output admission.
	const bool entry_transitions = _configuration.hasCandidate() && _selected_use == 1 && !_registration_fault
				       && _modes[kManualMode].configured && _modes[kAccelerationMode].configured
				       && !_bench_entry_selected && !_bench_disarm_pending
				       && !slaaps::ManualBankPreparation::commissioningMode(_prepared_request.mode);
	_companion_protocol.update(_configuration.hasCandidate() ? &_configuration.configuration() : nullptr,
				   _configuration.identity(), _last_apply, _modes[kManualMode].id,
				   _modes[kAccelerationMode].id, entry_transitions, now);

	// Receipt evidence only. No payload becomes an eligible command or survives
	// this call; trusted session/clock admission and acceleration control remain
	// separate from the configured receiver's authentication observation.
	if (!_configuration.hasCandidate()) {
		_companion_deadline = 0;

		if (!_companion_fault) { _companion_status = slaaps_status_s::COMPANION_WAIT_CONFIGURATION; }

		return;
	}

	const auto &configuration = _configuration.configuration();
	const auto &companion = configuration.companion;

	if (!slaaps::validateCompanionConfiguration(configuration).valid()) {
		_companion_deadline = 0;
		_companion_status = slaaps_status_s::COMPANION_WAIT_CONFIGURATION;
		return;
	}

	if (_companion_configuration != _configuration.identity() || _companion_apply != _last_apply) {
		_companion_configuration = _configuration.identity();
		_companion_apply = _last_apply;
		_companion_started = now;
		_companion_deadline = 0;
		_companion_sequence = 0;
		_companion_revision = 0;
		_companion_signing_generation = 0;
		_companion_fault = false;
		_companion_status = slaaps_status_s::COMPANION_WAIT_FRAME;
	}

	if (companion.enabled != 1) {
		_companion_deadline = 0;
		_companion_status = slaaps_status_s::COMPANION_DISABLED;
		return;
	}

	if (_companion_fault) { return; }

	auto &input = _companion_input[companion.mavlink_instance];

	if (_companion_revision && input.revision() != _companion_revision) {
		_companion_fault = true;
		_companion_deadline = 0;
		_companion_status = slaaps_status_s::COMPANION_LIFETIME_CHANGED;
		return;
	}

	// Bound queue work so a flooded receiver cannot starve the existing output,
	// expiry or disarm path. Overwritten receiver events leave sequence gaps.
	for (unsigned count = 0; count < slaaps_companion_packet_s::ORB_QUEUE_LENGTH && input.updated(); ++count) {
		slaaps_companion_packet_s packet{};
		unsigned revision{};

		if (!input.copyQueuedBound(&packet, revision)) { break; }

		now = hrt_absolute_time();

		if (packet.timestamp <= _companion_started) { continue; }

		_companion_deadline = 0;

		if (!revision || revision != input.revision() || (_companion_revision && revision != _companion_revision)
		    || (_companion_revision && (packet.signing_generation != _companion_signing_generation
						|| packet.receiver_system != _companion_receiver_system
						|| packet.receiver_component != _companion_receiver_component))) {
			_companion_status = slaaps_status_s::COMPANION_LIFETIME_CHANGED;
			_companion_fault = true;
			return;
		}

		if (!packet.sequence || (_companion_sequence && (_companion_sequence == UINT64_MAX
					 || packet.sequence != _companion_sequence + 1))) {
			_companion_status = slaaps_status_s::COMPANION_SEQUENCE_GAP;
			_companion_fault = true;
			return;
		}

		_companion_revision = revision;
		_companion_signing_generation = packet.signing_generation;
		_companion_receiver_system = packet.receiver_system;
		_companion_receiver_component = packet.receiver_component;
		_companion_sequence = packet.sequence;

		if (packet.timestamp > now || now - packet.timestamp >= static_cast<uint64_t>(companion.maximum_age_us)
		    || packet.timestamp > UINT64_MAX - static_cast<uint64_t>(companion.maximum_age_us)) {
			_companion_status = slaaps_status_s::COMPANION_STALE;
			continue;
		}

		if (packet.mavlink_instance != companion.mavlink_instance || packet.event > slaaps_companion_packet_s::EVENT_FRAME) {
			_companion_status = slaaps_status_s::COMPANION_INVALID_FRAME;
			continue;
		}

		if (!packet.signing_active || !packet.signing_generation) {
			_companion_status = slaaps_status_s::COMPANION_AUTHENTICATION_REQUIRED;
			continue;
		}

		if (packet.event == slaaps_companion_packet_s::EVENT_LINK_STATE) {
			_companion_status = slaaps_status_s::COMPANION_WAIT_FRAME;
			continue;
		}

		if (!packet.mavlink2 || !packet.signature_verified) {
			_companion_status = slaaps_status_s::COMPANION_AUTHENTICATION_REQUIRED;
			continue;
		}

		if (packet.source_system != companion.source_system || packet.source_component != companion.source_component) {
			_companion_status = slaaps_status_s::COMPANION_SOURCE_MISMATCH;
			continue;
		}

		const bool request = packet.message_id == MAVLINK_MSG_ID_RESEARCH_ACCELERATION_REQUEST;
		const bool session = packet.message_id == MAVLINK_MSG_ID_RESEARCH_ACCELERATION_SESSION;
		// Last mandatory nonzero field, not generated untrimmed base MIN_LEN.
		constexpr uint8_t request_minimum = 85;
		constexpr uint8_t session_minimum = 71;

		if ((!request && !session)
		    || (request && (packet.payload_length < request_minimum || packet.payload_length > MAVLINK_MSG_ID_RESEARCH_ACCELERATION_REQUEST_LEN))
		    || (session && (packet.payload_length < session_minimum || packet.payload_length > MAVLINK_MSG_ID_RESEARCH_ACCELERATION_SESSION_LEN))) {
			_companion_status = slaaps_status_s::COMPANION_INVALID_FRAME;
			continue;
		}

		mavlink_message_t frame{};
		frame.msgid = packet.message_id;
		frame.len = packet.payload_length;
		memcpy(_MAV_PAYLOAD_NON_CONST(&frame), packet.payload, packet.payload_length);
		const uint64_t source = request ? mavlink_msg_research_acceleration_request_get_source_id(&frame) :
					mavlink_msg_research_acceleration_session_get_source_id(&frame);
		const uint8_t target_system = request ? mavlink_msg_research_acceleration_request_get_target_system(&frame) :
					      mavlink_msg_research_acceleration_session_get_target_system(&frame);
		const uint8_t target_component = request ? mavlink_msg_research_acceleration_request_get_target_component(&frame) :
						 mavlink_msg_research_acceleration_session_get_target_component(&frame);

		if (source != static_cast<uint64_t>(companion.source_id)) {
			_companion_status = slaaps_status_s::COMPANION_SOURCE_MISMATCH;
			continue;
		}

		if (!target_system || !target_component || target_system != packet.receiver_system || target_component != packet.receiver_component) {
			_companion_status = slaaps_status_s::COMPANION_TARGET_MISMATCH;
			continue;
		}

		_companion_deadline = packet.timestamp + static_cast<uint64_t>(companion.maximum_age_us);
		_companion_status = slaaps_status_s::COMPANION_AUTHENTICATED_FRAME;
	}

	if (_companion_deadline && now >= _companion_deadline) {
		_companion_deadline = 0;
		_companion_status = slaaps_status_s::COMPANION_STALE;
	}
}

void Slaaps::publishAccelerationStatus(hrt_abstime now)
{
	const auto &evidence = _manual.accelerationDiagnostic();
	auto &status = _acceleration_status.get();
	status = {};
	status.timestamp = now;
	status.configuration_identity = evidence.configuration;
	status.output_session = evidence.session;
	status.request_source = evidence.request_source;
	status.request_session = evidence.request_session;
	status.request_sequence = evidence.request_sequence;
	status.request_observed_us = evidence.request_observed_us;
	status.fault_observed_us = evidence.fault_us;
	status.calculated_sequence = evidence.calculated_sequence;
	status.calculated_deadline = evidence.calculated_deadline_us;
	status.handled_sequence = evidence.handled_sequence;
	status.handled_us = evidence.handled_us;
	status.handled_deadline = evidence.handled_deadline_us;
	status.active = _manual.active() && _manual.mode() == slaaps::ControlMode::Acceleration;
	status.state = static_cast<uint8_t>(evidence.state);
	status.control_fault = static_cast<uint8_t>(evidence.fault);
	status.allocation_stage = static_cast<uint8_t>(evidence.allocation_stage);
	status.allocation_result = static_cast<uint8_t>(evidence.allocation_result);
	status.worker_status = static_cast<uint8_t>(evidence.worker_status);
	status.calculated_moment_mask = evidence.calculated_moment_mask;
	status.handled_moment_mask = evidence.handled_moment_mask;
	status.calculated_canard_mask = evidence.calculated_canard_mask;
	status.handled_canard_mask = evidence.handled_canard_mask;
	_acceleration_status.update();

	const bool rejected = evidence.state == slaaps::AccelerationDiagnosticState::Rejected;

	if (evidence.state == slaaps::AccelerationDiagnosticState::Inactive) {
		_have_acceleration_report = false;
		return; // Historical masks are not active saturation after a normal exit.
	}

	if (!rejected && (!status.active || !evidence.handled_sequence || evidence.handled_us > now
			  || !evidence.handled_us || now >= evidence.handled_deadline_us)) { return; }

	const bool same_report = _have_acceleration_report && rejected == _reported_acceleration_rejection
				 && evidence.session == _reported_acceleration_session && evidence.configuration == _reported_acceleration_configuration
				 && evidence.fault == _reported_acceleration_fault && evidence.allocation_stage == _reported_acceleration_stage
				 && evidence.allocation_result == _reported_acceleration_solver && evidence.worker_status == _reported_acceleration_worker
				 && (rejected ? evidence.fault_us == _reported_acceleration_fault_us :
				     evidence.handled_moment_mask == _reported_acceleration_moment && evidence.handled_canard_mask == _reported_acceleration_canard);

	if ((!rejected && !_have_acceleration_report && !evidence.handled_moment_mask && !evidence.handled_canard_mask)
	    || _bench_disarm_pending || same_report || (_last_acceleration_diagnostic && (now < _last_acceleration_diagnostic
			    || now - _last_acceleration_diagnostic < kAccelerationDiagnosticIntervalUs))) { return; }

	if (rejected) {
		const char *message = "SLAAPS accel: check command/state evidence";

		switch (evidence.fault) {
		case slaaps::AccelerationControlFault::AllocationRejected:
			switch (evidence.allocation_result) {
			case slaaps::WrenchAllocationResult::InvalidConfiguration: message = "SLAAPS accel: check wrench profile"; break;

			case slaaps::WrenchAllocationResult::InvalidInput: message = "SLAAPS accel: check allocation inputs"; break;

			case slaaps::WrenchAllocationResult::ArithmeticFailure: message = "SLAAPS accel: allocation arithmetic fault"; break;

			default:
				message = evidence.allocation_stage == slaaps::AccelerationAllocationStage::CurrentOwnerThrust ?
					  "SLAAPS accel: current-model allocation rejected" : "SLAAPS accel: target allocation rejected";
				break;
			}

			break;

		case slaaps::AccelerationControlFault::InvalidConfiguration: message = "SLAAPS accel: check control profile"; break;

		case slaaps::AccelerationControlFault::Expired: message = "SLAAPS accel: command/state evidence expired"; break;

		case slaaps::AccelerationControlFault::StateChanged: message = "SLAAPS accel: source changed; re-enter"; break;

		case slaaps::AccelerationControlFault::InvalidSchedule: message = "SLAAPS accel: check schedule envelope"; break;

		case slaaps::AccelerationControlFault::GainTransitionRejected: message = "SLAAPS accel: scheduled gain step rejected"; break;

		case slaaps::AccelerationControlFault::ArithmeticFailure: message = "SLAAPS accel: controller arithmetic fault"; break;

		case slaaps::AccelerationControlFault::TimeRegression: message = "SLAAPS accel: observation clock rejected"; break;

		case slaaps::AccelerationControlFault::None:
			message = evidence.worker_status == slaaps::ManualWorkerStatus::OutputRejected ?
				  "SLAAPS accel: output ACK/ownership rejected" : "SLAAPS accel: shared worker rejected output";
			break;

		default: break;
		}

		mavlink_log_critical(&_diagnostic_log_pub, "%s", message);

	} else if (evidence.handled_moment_mask || evidence.handled_canard_mask) {
		mavlink_log_info(&_diagnostic_log_pub, "SLAAPS accel: model limits %u/%u; check demand",
				 evidence.handled_moment_mask, evidence.handled_canard_mask);

	} else {
		mavlink_log_info(&_diagnostic_log_pub, "SLAAPS accel: modeled clipping cleared");
	}

	_have_acceleration_report = true;
	_reported_acceleration_rejection = rejected;
	_reported_acceleration_session = evidence.session;
	_reported_acceleration_configuration = evidence.configuration;
	_reported_acceleration_fault = evidence.fault;
	_reported_acceleration_stage = evidence.allocation_stage;
	_reported_acceleration_solver = evidence.allocation_result;
	_reported_acceleration_worker = evidence.worker_status;
	_reported_acceleration_fault_us = evidence.fault_us;
	_reported_acceleration_moment = evidence.handled_moment_mask;
	_reported_acceleration_canard = evidence.handled_canard_mask;
	// A throttled change remains eligible because only emitted keys are stored.
	_last_acceleration_diagnostic = hrt_absolute_time();
}

void Slaaps::publishStatus(hrt_abstime now)
{
	slaaps_status_s status{};
	status.timestamp = now;
	status.companion_transport_status = _companion_status;
	status.companion_receiver_revision = _companion_revision;
	status.companion_event_sequence = _companion_sequence;
	status.companion_frame_deadline = _companion_deadline;
	status.manual_command_revision = _manual.commandRevision();
	status.manual_permission_revision = permissionRevision();
	status.manual_worker_active = _manual.active();
	status.bench_worker_active = _manual.active() && slaaps::ManualBankPreparation::commissioningMode(_prepared_request.mode);
	status.manual_worker_active &= !status.bench_worker_active;
	status.bench_entry_status = _bench_entry_status;
	status.bench_session_deadline = _bench_deadline;
	int32_t display_target = _bench_target;

	if (!_preparation_session) { (void)param_get(_bench_target_parameter, &display_target); }

	status.bench_target = display_target >= 0
			      && display_target <= static_cast<int32_t>(kEdfBenchTarget) ? static_cast<uint8_t>(display_target) : 0;
	status.bench_edf_commissioning = _bench_edf_authorized || (_selected_use == 2 && status.bench_target == kEdfBenchTarget);
	status.bench_commissioning_ready = _bench_can_arm && now < _bench_can_arm_deadline;
	status.bench_commissioning_ready_deadline = status.bench_commissioning_ready ? _bench_can_arm_deadline : 0;
	status.bench_disarm_pending = _bench_disarm_pending;
	status.manual_preparation_status = static_cast<uint8_t>(_preparation_status);
	status.manual_preparation_session = _preparation_session;
	status.manual_preparation_deadline = _preparation_deadline;
	status.manual_preparation_banks = _preparation_banks;
	status.manual_entry_status = _manual_entry_status;
	status.schedule_interval_us = _schedule_interval_us;
	status.manual_worker_status = static_cast<uint8_t>(_manual_status);
	status.input_metadata_valid = _input_metadata_valid;
	status.schedule_enabled = _selected_use == 1 && _configuration.configuration().schedule.enabled == 1;
	status.schedule_inputs_valid = status.schedule_enabled && _condition_valid && now < _condition_sample.deadline_us;
	status.schedule_status = static_cast<uint8_t>(_manual.scheduleStatus());
	status.power_required = _selected_use == 1 || _selected_use == 2;
	status.power_ready = status.power_required && _configuration.hasCandidate() && _power_valid && now < _power_sample.deadline_us;
	status.power_source_bound = _power_bound;
	status.power_status = static_cast<uint8_t>(_power_valid && now >= _power_sample.deadline_us ?
			      slaaps::PowerObservationResult::InvalidTimestamp : _power_result);
	status.power_publisher_revision = _power_sample.source.publisher_revision;
	status.power_deadline = _power_valid ? _power_sample.deadline_us : 0;
	status.power_voltage_v = _power_sample.voltage_v;
	status.input_source_epoch = _input_epoch;
	status.configuration_valid = _configuration.hasCandidate();
	status.configuration_identity = status.configuration_valid ? _configuration.identity() : 0;
	status.configuration_fault = static_cast<uint8_t>(_configuration_fault);

	if (_configuration_parameter) {
		strncpy(status.cfg_param, _configuration_parameter, sizeof(status.cfg_param) - 1);
	}

	status.configuration_use = _selected_use;
	status.apply_counter = _last_apply;
	status.snapshot_duration_us = _snapshot_duration;
	status.routing_snapshot_valid = _routing_valid && status.configuration_valid;
	status.inventory_fault = static_cast<uint8_t>(_inventory_fault);
	status.routing_fault = static_cast<uint8_t>(_routing_fault);
	status.io_index = _routing_fault_index;
	status.io_bank = _routing_fault_bank;
	status.io_channel = _routing_fault_channel;
	status.direct_actuator_modes_configured = !_registration_fault;

	for (unsigned i = 0; i < kModeCount; ++i) {
		status.mode_id[i] = _modes[i].id;
		status.direct_actuator_modes_configured &= _modes[i].configured;
	}

	status.unavailable_reason = !status.direct_actuator_modes_configured ? slaaps_status_s::UNAVAILABLE_REGISTRATION :
				    !status.configuration_valid ? slaaps_status_s::UNAVAILABLE_CONFIGURATION :
				    _selected_use == 1 ? slaaps_status_s::UNAVAILABLE_FLIGHT_ADMISSION :
				    slaaps_status_s::UNAVAILABLE_OUTPUT_INTEGRATION;

	if (_bench_can_run && status.configuration_valid && (_selected_use == 0 || (_selected_use == 2 && status.power_ready))
	    && _modes[kBenchMode].id >= vehicle_status_s::NAVIGATION_STATE_EXTERNAL1
	    && _modes[kBenchMode].id <= vehicle_status_s::NAVIGATION_STATE_EXTERNAL8) {
		status.can_run_mask = 1U << _modes[kBenchMode].id;
		status.unavailable_reason = _selected_use == 2 ? slaaps_status_s::AVAILABLE_EDF_COMMISSIONING : slaaps_status_s::AVAILABLE_SERVO_BENCH;

		if (status.bench_commissioning_ready) { status.can_arm_mask = status.can_run_mask; }
	}

	// Only an exact disarmed prepared EDF session permits operator arming.
	// Neither commissioning mode confers Manual/acceleration flight admission.
	_status.publish(status);
	publishAccelerationStatus(now);
	publishFlightStatus(now);
}

void Slaaps::updateSchedule()
{
	const bool active = _manual.active() || _manual_activation_owned || _reentry
			    || _manual_entry_status == slaaps_status_s::MANUAL_ENTRY_ACTIVATING;
	const bool bench = slaaps::ManualBankPreparation::commissioningMode(_prepared_request.mode) && (active || _preparation_pending);
	uint32_t interval = bench ? research_actuator_command::kBenchIntervalUs : slaaps::manualScheduleInterval(_configuration.configuration(),
			    active);

	if (_configuration.hasCandidate()) { interval = math::min(interval, slaaps::companionScheduleInterval(_configuration.configuration())); }

	if (!interval) {
		cancelPreparation(hrt_absolute_time(), slaaps::PreparationStatus::Rejected);
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
		_companion_protocol.stop();
		_companion_deadline = 0;
		_companion_status = slaaps_status_s::COMPANION_DISABLED;

		if (_bench_entry_selected || slaaps::ManualBankPreparation::commissioningMode(_prepared_request.mode)) {
			cancelBench(now, slaaps_status_s::BENCH_ENTRY_INACTIVE);

		} else {
			cancelPreparation(now, slaaps::PreparationStatus::Inactive);
		}

		(void)resetBenchEnable();
		_bench_can_run = false;

		if (_bench_disarm_pending) {
			updateBench(hrt_absolute_time());
			updateSchedule();
			publishStatus(hrt_absolute_time());
			return;
		}

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

	if (!_bench_controls_initialized) {
		_bench_enable_parameter = param_find("SLA_BENCH_EN");
		_bench_target_parameter = param_find("SLA_BENCH_CH");
		_bench_seconds_parameter = param_find("SLA_BENCH_SEC");
		_bench_controls_initialized = _bench_target_parameter != PARAM_INVALID && _bench_seconds_parameter != PARAM_INVALID && resetBenchEnable();
	}

	updateParameters(now, _parameter_update.update(&update));
	updateCompanion(hrt_absolute_time());

	const bool bench_session = slaaps::ManualBankPreparation::commissioningMode(_prepared_request.mode);
	const int32_t session_use = bench_session ? (_prepared_request.mode == slaaps::ControlMode::EdfBench ? 2 : 0) : 1;

	if ((!_configuration.hasCandidate() || _selected_use != session_use) && (_manual.active() || _manual_activation_owned)) {
		if (bench_session) { cancelBench(hrt_absolute_time(), slaaps_status_s::BENCH_ENTRY_REJECTED); }

		else { cancelPreparation(hrt_absolute_time(), slaaps::PreparationStatus::Rejected); }

		_manual_status = slaaps::ManualWorkerStatus::ConfigurationRejected;
	}

	const bool maintenance_due = slaaps::supervisorMaintenanceDue(now, _last_maintenance);
	// An armed D5 re-entry publishes a positive grant while preparing, which must stay fresh: run at control cadence.
	const bool control_due = _manual.active() || _manual_activation_owned || _reentry
				 || _manual_entry_status == slaaps_status_s::MANUAL_ENTRY_ACTIVATING || (bench_session && _preparation_pending) || _bench_disarm_pending;

	if (control_due || maintenance_due) { updateRouting(hrt_absolute_time()); }

	if (maintenance_due) {
		const auto maintenance_time = hrt_absolute_time();
		updateRegistration(maintenance_time);
		_last_maintenance = maintenance_time;
	}

	if (control_due || maintenance_due) {
		_input_metadata_valid = false;
		_input_epoch = 0;
		_input_observation = {};
		_condition_valid = false;

		if (_configuration.hasCandidate() && _selected_use == 1) {
			const auto &configuration = _configuration.configuration();
			const bool timing = slaaps::manualActivationTimingConfigured(configuration);
			vehicle_status_s mode{};
			const bool have_mode = _vehicle_status.copy(&mode);
			const bool state_only = _manual.active() && have_mode && _modes[kAccelerationMode].configured
						&& mode.nav_state == _modes[kAccelerationMode].id;
			const uint64_t status_age = timing ? static_cast<uint64_t>(configuration.selected_status_age_us) : kMaximumStatusAgeUs;
			const uint64_t permission_age = timing ? static_cast<uint64_t>(configuration.actuator_permission_age_us) : kMaximumStatusAgeUs;
			_input_observation = state_only ? _acceleration_states.captureState(status_age, permission_age) : _inputs.capture(status_age,
					     permission_age);
			_input_metadata_valid = _input_observation.state.valid && _input_observation.permission_fresh;
			_input_epoch = _input_observation.state.source_epoch;

			if (configuration.schedule.enabled) { _condition_valid = _conditions.capture(configuration.schedule, _condition_sample); }

			observeLaunchAirspeed(hrt_absolute_time());
		}

		updateBench(hrt_absolute_time());

		if (!_bench_entry_selected && !_bench_disarm_pending) { updatePreparation(hrt_absolute_time()); }

		publishStatus(hrt_absolute_time());
	}

	// An inactive profile can change on a tick where maintenance is not due.
	// Apply its observation cadence before waiting on the previous interval.
	updateSchedule();
	answerChecks(hrt_absolute_time());
}

int Slaaps::task_spawn(int argc, char *argv[])
{
	if (argc != 1) { return print_usage("start takes no arguments"); }

	auto *instance = new Slaaps();

	if (!instance) { return PX4_ERROR; }

	// Scheduling has not started; reuse the object's snapshot storage instead
	// of placing a second full profile on the 2048-byte builtin command stack.
	auto &initial = instance->_stop_profile;

	if (!instance->_parameters.initialize()
	    || !instance->_parameters.capture(initial, instance->_snapshot_duration, instance->_parameter_scratch)) {
		delete instance;
		PX4_ERR("parameter schema unavailable");
		return PX4_ERROR;
	}

	if (!instance->_manual.advertise() || !instance->_preparation.advertise()
	    || !instance->_preparation.advertiseTransitions() || !instance->_permission.advertise()) {
		delete instance;
		PX4_ERR("manual command endpoint unavailable");
		return PX4_ERROR;
	}

	instance->_permission_revision = instance->_permission_lifetime.revision();

	if (!instance->_permission_revision) { delete instance; return PX4_ERROR; }

	instance->_last_apply = initial.apply_counter;

	if (!slaaps::generateCompanionIdentity(instance->_companion_identity_seed)) {
		PX4_WARN("companion identity unavailable; manual and bench remain independent");
	}

	if (!instance->_companion_protocol.initialize(instance->_companion_identity_seed)) {
		PX4_WARN("companion action unavailable; manual and bench remain independent");
	}

	const uint64_t identity = hrt_absolute_time() << 3;

	for (unsigned i = 0; i < kModeCount; ++i) { instance->_modes[i].request = identity | (i + 1); }

	desc.object.store(instance);
	desc.task_id = task_id_is_work_queue;
	instance->ScheduleOnInterval(kIntervalUs);
	return PX4_OK;
}

int Slaaps::print_status()
{
#if defined(CONFIG_SLAAPS_ROUTING_TEST)
	PX4_INFO("native first bench cancellation line=%u", _native_cancel_line.load());
#endif
	uORB::Subscription status_sub {ORB_ID(slaaps_status)};
	slaaps_status_s status{};

	if (!status_sub.copy(&status)) { return PX4_ERROR; }

	PX4_INFO("modes manual=%d acceleration=%d bench=%d configured=%d",
		 status.mode_id[kManualMode], status.mode_id[kAccelerationMode], status.mode_id[kBenchMode],
		 status.direct_actuator_modes_configured);
	PX4_INFO("configuration valid=%d use=%u apply=%ld fault=%u snapshot=%lu us",
		 status.configuration_valid, status.configuration_use, static_cast<long>(status.apply_counter),
		 status.configuration_fault, static_cast<unsigned long>(status.snapshot_duration_us));

	// CLI runs outside the supervisor queue. Compare a coherent parameter copy
	// with the published accepted identity; never read mutable owner state here.
	// Keep both destination and scratch off the command stack, and independent
	// of the live owner's scratch. PX4 NuttX new is checked with -fcheck-new.
	auto *profile = new slaaps::ParameterSnapshot[2] {};
	uint32_t capture_duration{};
	const bool accepted = profile && _parameters.capture(profile[0], capture_duration, profile[1]) && status.configuration_valid
			      && profile->apply_counter == status.apply_counter && profile->use == status.configuration_use
			      && slaaps::fingerprint(profile->configuration) == status.configuration_identity
			      && slaaps::Px4ConfigurationSource::remembered(*profile);
	PX4_INFO("profile accepted=%u schema=%ld record=%ld revision=%ld declared_state=%ld",
		 static_cast<unsigned>(accepted), static_cast<long>(accepted ? profile->configuration.data_schema_version : 0),
		 static_cast<long>(accepted ? profile->configuration.data_record_id : 0),
		 static_cast<long>(accepted ? profile->configuration.data_revision : 0),
		 static_cast<long>(accepted ? profile->configuration.data_assessment : 0));
	delete[] profile;

	if (status.cfg_param[0]) { PX4_INFO("configuration rejected parameter=%s", status.cfg_param); }

	PX4_INFO("flight unavailable=%u", status.unavailable_reason);
	PX4_INFO("companion transport=%u receiver=%lu event=%llu deadline=%llu (no command admission)",
		 status.companion_transport_status, static_cast<unsigned long>(status.companion_receiver_revision),
		 static_cast<unsigned long long>(status.companion_event_sequence),
		 static_cast<unsigned long long>(status.companion_frame_deadline));
	_companion_protocol.printStatus();
	PX4_INFO("manual endpoint=%lu active=%d", static_cast<unsigned long>(status.manual_command_revision),
		 status.manual_worker_active);
	PX4_INFO("schedule=%lu us worker_status=%u", static_cast<unsigned long>(status.schedule_interval_us),
		 status.manual_worker_status);
	PX4_INFO("control schedule enabled=%d inputs=%d status=%u (not envelope qualification)",
		 status.schedule_enabled, status.schedule_inputs_valid, status.schedule_status);
	// CLI owns its own snapshot; do not race the queue-owned diagnostic scratch.
	// Keep the additional message off the already constrained command stack.
	uORB::Subscription acceleration_sub{ORB_ID::slaaps_acceleration_status};
	auto *acceleration = new slaaps_acceleration_status_s{};

	if (acceleration && acceleration_sub.copy(acceleration)) {
		PX4_INFO("accel active=%d state=%u fault=%u stage=%u solver=%u worker=%u",
			 acceleration->active, acceleration->state, acceleration->control_fault, acceleration->allocation_stage,
			 acceleration->allocation_result, acceleration->worker_status);
		PX4_INFO("accel output_session=%llu config=%llu fault_time=%llu",
			 static_cast<unsigned long long>(acceleration->output_session), static_cast<unsigned long long>(acceleration->configuration_identity),
			 static_cast<unsigned long long>(acceleration->fault_observed_us));
		PX4_INFO("accel request=%llu/%llu/%llu captured=%llu",
			 static_cast<unsigned long long>(acceleration->request_source), static_cast<unsigned long long>(acceleration->request_session),
			 static_cast<unsigned long long>(acceleration->request_sequence), static_cast<unsigned long long>(acceleration->request_observed_us));
		PX4_INFO("accel calculated=%llu deadline=%llu model_masks=%u/%u (not handled)",
			 static_cast<unsigned long long>(acceleration->calculated_sequence), static_cast<unsigned long long>(acceleration->calculated_deadline),
			 acceleration->calculated_moment_mask, acceleration->calculated_canard_mask);
		PX4_INFO("accel handled=%llu at=%llu deadline=%llu model_masks=%u/%u (not measured)",
			 static_cast<unsigned long long>(acceleration->handled_sequence), static_cast<unsigned long long>(acceleration->handled_us),
			 static_cast<unsigned long long>(acceleration->handled_deadline), acceleration->handled_moment_mask, acceleration->handled_canard_mask);
	}

	delete acceleration;
	PX4_INFO("power required=%d ready=%d status=%u bound=%d revision=%lu voltage=%.3f deadline=%llu",
		 status.power_required, status.power_ready, status.power_status, status.power_source_bound,
		 static_cast<unsigned long>(status.power_publisher_revision), static_cast<double>(status.power_voltage_v),
		 static_cast<unsigned long long>(status.power_deadline));
	PX4_INFO("input metadata=%d epoch=%lu (not flight admission)",
		 status.input_metadata_valid, static_cast<unsigned long>(status.input_source_epoch));
	PX4_INFO("routing snapshot=%d inventory_fault=%u routing_fault=%u index=%d bank=%u channel=%u",
		 status.routing_snapshot_valid, status.inventory_fault, status.routing_fault, status.io_index,
		 status.io_bank, status.io_channel);
	PX4_INFO("manual preparation=%u session=%llu banks=%u deadline=%llu (no flight admission)",
		 status.manual_preparation_status, static_cast<unsigned long long>(status.manual_preparation_session),
		 status.manual_preparation_banks, static_cast<unsigned long long>(status.manual_preparation_deadline));
	PX4_INFO("manual entry=%u (software lifecycle; no flight admission)", status.manual_entry_status);
	{
		uORB::Subscription permission{ORB_ID(slaaps_manual_permission)};
		slaaps_manual_permission_s snapshot{};

		if (permission.copy(&snapshot)) {
			PX4_INFO("flight output mode=%u transition=%llu (permission snapshot only)", snapshot.mode,
				 static_cast<unsigned long long>(snapshot.transition));
		}
	}

	PX4_INFO("bench entry=%u active=%d target=%u deadline=%llu (bounded ground commissioning)",
		 status.bench_entry_status, status.bench_worker_active, status.bench_target,
		 static_cast<unsigned long long>(status.bench_session_deadline));
	PX4_INFO("bench commissioning=%d ready=%d disarm_pending=%d", status.bench_edf_commissioning,
		 status.bench_commissioning_ready, status.bench_disarm_pending);
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
