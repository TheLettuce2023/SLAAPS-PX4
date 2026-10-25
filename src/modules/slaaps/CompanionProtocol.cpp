#include "CompanionProtocol.hpp"
#include <lib/slaaps/AccelerationControllerPort.hpp>

#include <lib/research_stabilized_manual/StabilizedInputValidation.hpp>
#include <lib/parameters/atomic_transaction.h>
#include <drivers/drv_hrt.h>
#include <px4_platform_common/log.h>
#include <uORB/Subscription.hpp>
#include <uORB/topics/vehicle_status.h>
#include <mavlink.h>
#include <cmath>
#include <cstring>
#include <new>

namespace slaaps
{
namespace
{
using namespace research_acceleration;
bool add(uint64_t a, uint64_t b, uint64_t &result)
{
	if (a > UINT64_MAX - b) { return false; }

	result = a + b;
	return true;
}
bool offset(uint64_t stamp, int64_t shift, uint64_t &result)
{
	if (shift >= 0) { return add(stamp, static_cast<uint64_t>(shift), result); }

	const uint64_t magnitude = static_cast<uint64_t>(-(shift + 1)) + 1;

	if (stamp < magnitude) { return false; }

	result = stamp - magnitude;
	return true;
}
bool fresh(uint64_t now, uint64_t stamp, uint64_t age, uint64_t &deadline)
{
	uint64_t expiry{};

	if (!age || !stamp || stamp > now || now - stamp > age || !add(stamp, age, expiry) || !add(expiry, 1, expiry)) { return false; }

	deadline = deadline < expiry ? deadline : expiry;
	return true;
}
bool sameMap(const TrustedClockMap &a, const TrustedClockMap &b)
{
	return a.valid == b.valid && a.source == b.source && a.session == b.session
	       && a.controller_instance_id == b.controller_instance_id && a.map_epoch == b.map_epoch
	       && a.validator_minus_producer_us == b.validator_minus_producer_us && a.uncertainty_us == b.uncertainty_us
	       && a.valid_from_producer_us == b.valid_from_producer_us && a.valid_until_producer_us == b.valid_until_producer_us;
}
bool sameContext(const EvaluationContext &a, const EvaluationContext &b)
{
	return a.arm_state == b.arm_state && a.mode_state == b.mode_state
	       && a.clock_mapping_valid == b.clock_mapping_valid && a.transport_connected == b.transport_connected
	       && a.required_state_valid == b.required_state_valid;
}
bool sameWitness(const slaaps_vehicle_state_s &a, const slaaps_vehicle_state_s &b)
{
	return a.arming_state == b.arming_state && a.nav_state == b.nav_state && a.armed == b.armed && a.prearmed == b.prearmed
	       && a.lockdown == b.lockdown && a.kill == b.kill && a.termination == b.termination
	       && a.in_esc_calibration_mode == b.in_esc_calibration_mode && a.attitude_invalid == b.attitude_invalid
	       && a.angular_velocity_invalid == b.angular_velocity_invalid;
}
DecodedSessionV1 session(const mavlink_message_t &message)
{
	DecodedSessionV1 result{};
	result.controller_instance_id = mavlink_msg_research_acceleration_session_get_controller_instance_id(&message);
	result.transition_epoch = mavlink_msg_research_acceleration_session_get_transition_epoch(&message);
	result.clock_mapping_epoch = mavlink_msg_research_acceleration_session_get_clock_mapping_epoch(&message);
	result.source = mavlink_msg_research_acceleration_session_get_source_id(&message);
	result.session = mavlink_msg_research_acceleration_session_get_session_id(&message);
	result.operation_sequence = mavlink_msg_research_acceleration_session_get_operation_sequence(&message);
	result.producer_acknowledgement_monotonic_us = mavlink_msg_research_acceleration_session_get_producer_acknowledgement_monotonic_us(
				&message);
	result.acknowledged_status_sequence = mavlink_msg_research_acceleration_session_get_acknowledged_status_sequence(&message);
	result.protocol_version = mavlink_msg_research_acceleration_session_get_protocol_version(&message);
	result.target_system = mavlink_msg_research_acceleration_session_get_target_system(&message);
	result.target_component = mavlink_msg_research_acceleration_session_get_target_component(&message);
	result.operation = mavlink_msg_research_acceleration_session_get_operation(&message);
	result.flags = mavlink_msg_research_acceleration_session_get_flags(&message);
	mavlink_msg_research_acceleration_session_get_reserved(&message, result.reserved);
	return result;
}
DecodedRequest request(const mavlink_message_t &message)
{
	DecodedRequest result{};
	auto &wire = result.wire;
	wire.source_id = mavlink_msg_research_acceleration_request_get_source_id(&message);
	wire.session_id = mavlink_msg_research_acceleration_request_get_session_id(&message);
	wire.sequence = mavlink_msg_research_acceleration_request_get_sequence(&message);
	wire.producer_monotonic_us = mavlink_msg_research_acceleration_request_get_producer_monotonic_us(&message);
	wire.producer_expiry_monotonic_us = mavlink_msg_research_acceleration_request_get_producer_expiry_monotonic_us(&message);
	wire.controller_instance_id = mavlink_msg_research_acceleration_request_get_controller_instance_id(&message);
	wire.transition_epoch = mavlink_msg_research_acceleration_request_get_transition_epoch(&message);
	wire.clock_mapping_epoch = mavlink_msg_research_acceleration_request_get_clock_mapping_epoch(&message);
	wire.acceleration_x_frd_m_s2 = mavlink_msg_research_acceleration_request_get_acceleration_x_frd_m_s2(&message);
	wire.acceleration_y_frd_m_s2 = mavlink_msg_research_acceleration_request_get_acceleration_y_frd_m_s2(&message);
	wire.acceleration_z_frd_m_s2 = mavlink_msg_research_acceleration_request_get_acceleration_z_frd_m_s2(&message);
	wire.protocol_version = mavlink_msg_research_acceleration_request_get_protocol_version(&message);
	wire.frame = mavlink_msg_research_acceleration_request_get_frame(&message);
	wire.quantity = mavlink_msg_research_acceleration_request_get_quantity(&message);
	wire.reference_point = mavlink_msg_research_acceleration_request_get_reference_point(&message);
	// The generated getter zero-fills absent MAVLink2 extension bytes. The
	// adapter validates the explicit request version/kind; no attitude default
	// is supplied for legacy acceleration-only requests.
	mavlink_msg_research_acceleration_request_get_desired_attitude_ned_from_body(&message,
			wire.desired_attitude_ned_from_body);
	result.target_system = mavlink_msg_research_acceleration_request_get_target_system(&message);
	result.target_component = mavlink_msg_research_acceleration_request_get_target_component(&message);
	result.flags = mavlink_msg_research_acceleration_request_get_flags(&message);
	mavlink_msg_research_acceleration_request_get_reserved(&message, result.reserved);
	return result;
}
}

CompanionProtocol::~CompanionProtocol()
{
	// Queued publications are deliberately retained by PublicationBase unless
	// explicitly released. The disabled control still governs ordinary stop /
	// reconfigure; only final destruction ends these publisher lifetimes.
	if (_tx.advertised()) { _tx.unadvertise(); }

	if (_barrier_tx.advertised()) { _barrier_tx.unadvertise(); }

	resetAdapter();
}

void CompanionProtocol::resetAdapter()
{
	if (_adapter) {
		_adapter->~WireAdapter();
		_adapter = nullptr;
	}
}

bool CompanionProtocol::initialize(uint64_t seed)
{
	if (_initialized) { return false; }

	_next_identity = seed;
	_action = param_find("SLA_CMP_ACT");
	const int32_t none = 0;
	_initialized = _action != PARAM_INVALID && param_set(_action, &none) == 0;
	return _initialized;
}

bool CompanionProtocol::advance(uint64_t &counter)
{
	if (counter == UINT64_MAX) { _continuity_fault = true; return false; }

	++counter;
	return true;
}

void CompanionProtocol::closeMap(bool change_binding)
{
	_capture_pending = false;
	_capture_mode_pending = false;
	_capture_disarm_deadline = 0;
	_entry_authorized = _entry_arm_used = _entry_acceleration_used = false;
	_state.map = {};
	_state.map.controller_instance_id = _identity;
	_map_deadline = 0;
	_prepared = false;
	_prepared_session = 0;
	_proposal_deadline = 0;
	_have_barrier = false;
	_status.acknowledgement_current = false;

	if (change_binding && advance(_binding)) { _control_expired = false; _control_deadline = 0; }
}

void CompanionProtocol::stop()
{
	closeMap(false);

	if (_configured && _adapter) {
		auto now = hrt_absolute_time();
		(void)observe(now); // Revoke through the actual adapter's observer barrier.
	}

	_configured = false;
	_connection_deadline = 0;
	_status.phase = 0;
	publishControl(hrt_absolute_time());
}

bool CompanionProtocol::configure(const VehicleConfiguration &c, uint64_t identity, int32_t apply)
{
	stop();
	_configuration_identity = identity;
	_apply = apply;
	_configuration = c.companion;
	_feedback_required = c.feedback.enabled != 0;
	_feedback_sample = {};
	_feedback_result = _feedback_required ? AccelerationFeedbackResult::InvalidConfiguration : AccelerationFeedbackResult::Disabled;
	const bool feedback_configured = _feedback.configure(c);
	resetAdapter();
	_snapshot = {};
	_state = {};
	_observed = {};
	_status = {};
	_state_seen = _previous_valid = _continuity_fault = false;
	_receiver_revision = _signing_generation = _commander_revision = _estimator_epoch = 0;
	_packet_sequence = 0;
	_started = hrt_absolute_time();
	_clock = _started;
	_status_age = c.selected_status_age_us > 0 ? static_cast<uint64_t>(c.selected_status_age_us) : 0;
	_permission_age = c.actuator_permission_age_us > 0 ? static_cast<uint64_t>(c.actuator_permission_age_us) : 0;
	research_acceleration::ContractConfig contract{};

	if (!_next_identity || (_feedback_required && !feedback_configured)
	    || !makeCompanionContractConfiguration(c, contract)) { return false; }

	_identity = _next_identity;
	_next_identity = _next_identity == UINT64_MAX ? 0 : _next_identity + 1;
	_adapter = new (_adapter_storage) Adapter(contract, research_acceleration::AdapterIdentity{_identity});
	// Supervisor events belong to this controller lifetime. A fresh adapter
	// requires event 1; retaining the previous identity's floor faults re-entry.
	_admin_sequence = 0;
	_numerical = {};
	_numerical.maximum_state_age_us = c.state_age_ms > 0 ? static_cast<uint64_t>(c.state_age_ms) * 1000 : 0;
	_numerical.maximum_state_skew_us = c.state_skew_ms >= 0 ? static_cast<uint64_t>(c.state_skew_ms) * 1000 : 0;
	_numerical.quaternion_norm_tolerance = c.quaternion_tolerance;

	for (unsigned axis = 0; axis < kAxes; ++axis) {
		_numerical.maximum_measured_rate_rad_s(axis) = c.axis[axis].measured_rate_limit;
		_numerical.maximum_measured_accel_rad_s2(axis) = c.axis[axis].measured_accel_limit;
	}

	_state.map.controller_instance_id = _identity;
	_configured = _adapter->configured() && advance(_binding);
	_control_expired = false;
	_control_deadline = 0;
	return _configured;
}

bool CompanionProtocol::acceptEntryTransition(const slaaps_vehicle_state_s &previous, uint64_t previous_deadline,
		uint64_t now)
{
	const auto &current = _witness;
	const auto &before = _observed.request_guard_context;
	const auto &after = _state.request_guard_context;
	const auto session = _adapter->sessionSnapshot();
	const bool modes_valid = _manual_mode >= vehicle_status_s::NAVIGATION_STATE_EXTERNAL1
				 && _manual_mode <= vehicle_status_s::NAVIGATION_STATE_EXTERNAL8
				 && _acceleration_mode >= vehicle_status_s::NAVIGATION_STATE_EXTERNAL1
				 && _acceleration_mode <= vehicle_status_s::NAVIGATION_STATE_EXTERNAL8 && _manual_mode != _acceleration_mode;

	if (!_allow_entry_transitions || !_entry_authorized || !_prepared || !modes_valid
	    || !_state.map.valid || !_map_deadline || now >= _map_deadline || !previous_deadline || now >= previous_deadline
	    || session.gate != research_acceleration::SessionGate::Established || !session.has_last_successful_session
	    || session.last_successful_session.owner != _state.map.source || session.last_successful_session.session != _state.map.session
	    || previous.transition == UINT64_MAX || current.transition != previous.transition + 1
	    || !before.required_state_valid || !after.required_state_valid || !before.transport_connected || !after.transport_connected
	    || !before.clock_mapping_valid || !after.clock_mapping_valid) { return false; }

	const bool was_disarmed = previous.arming_state == vehicle_status_s::ARMING_STATE_DISARMED && !previous.armed;
	const bool is_disarmed = current.arming_state == vehicle_status_s::ARMING_STATE_DISARMED && !current.armed;
	const bool was_armed = previous.arming_state == vehicle_status_s::ARMING_STATE_ARMED && previous.armed;
	const bool is_armed = current.arming_state == vehicle_status_s::ARMING_STATE_ARMED && current.armed;
	const bool arm_edge = was_disarmed && is_armed;
	const bool mode_edge = previous.nav_state != current.nav_state;
	const bool previous_flight_mode = previous.nav_state == _manual_mode || previous.nav_state == _acceleration_mode;
	const bool current_flight_mode = current.nav_state == _manual_mode || current.nav_state == _acceleration_mode;

	// Disarm, ambiguous arming state, skipped Commander transitions and flight
	// exits never preserve a session. Preparing this protocol needs no output.
	if ((!was_disarmed && !was_armed) || (!is_disarmed && !is_armed) || (was_armed && !is_armed)
	    || (arm_edge && (_entry_arm_used || !previous_flight_mode || !current_flight_mode))
	    || (was_armed && !_entry_arm_used)) { return false; }

	if (mode_edge) {
		if (current.nav_state == _acceleration_mode) {
			if (_entry_acceleration_used || (!was_disarmed && previous.nav_state != _manual_mode)) { return false; }

		} else if (current.nav_state != _manual_mode || !was_disarmed || !is_disarmed
			   || previous_flight_mode || _entry_acceleration_used) { return false; }
	}

	// A benign prearm rise can precede the actual arm publication. Isolated
	// prearm loss is not an entry edge; it cannot silently restore readiness.
	const bool prearm_rise = was_disarmed && is_disarmed && !previous.prearmed && current.prearmed;

	if (!arm_edge && !mode_edge && !prearm_rise) { return false; }

	if (!arm_edge && previous.prearmed != current.prearmed && !prearm_rise) { return false; }

	_entry_arm_used |= arm_edge;
	_entry_acceleration_used |= mode_edge && current.nav_state == _acceleration_mode;
	return true;
}

bool CompanionProtocol::observe(uint64_t &now, actuator_armed_s *permission)
{
	if (!_adapter) { return false; }

	const auto previous_witness = _witness;
	const uint64_t previous_witness_deadline = _witness_deadline;
	slaaps_vehicle_state_s witness{};
	unsigned revision{};
	const bool copied = _vehicle.copyBound(&witness, revision);
	const bool stable = copied && revision && revision == _vehicle.revision();
	research_stabilized_manual::SelectedAccelerationObservationSnapshot acceleration{};

	if (_feedback_required) {
		acceleration = _selected.captureAccelerationState(_status_age, _permission_age);

	} else {
		acceleration.selected = _selected.captureState(_status_age, _permission_age);
	}

	const auto &selected = acceleration.selected;

	if (permission) { *permission = selected.permission; }

	now = hrt_absolute_time();

	if (now < _clock) { _continuity_fault = true; }

	uint64_t witness_deadline = UINT64_MAX;
	const bool witness_fresh = stable && witness.transition && fresh(now, witness.timestamp, _permission_age, witness_deadline);
	bool external_change = false;

	if (_commander_revision && (!witness_fresh || revision != _commander_revision
				    || witness.transition < _witness.transition || witness.timestamp < _witness.timestamp
				    || (witness.transition == _witness.transition && !sameWitness(witness, _witness)))) { _continuity_fault = true; }

	if (witness_fresh) {
		external_change = _commander_revision && witness.transition != _witness.transition;
		_commander_revision = revision;
		_witness = witness;
	}

	const bool witness_change = external_change;
	_witness_deadline = witness_fresh ? witness_deadline : 0;
	const bool source_change = selected.state.valid && _estimator_epoch
				   && (selected.state.source_epoch != _estimator_epoch || selected.gyro != _gyro || selected.accel != _accel
				       || selected.state.attitude_reset_counter != _previous_state.attitude_reset_counter);
	external_change |= source_change;
	research_stabilized_manual::Quaternion attitude{};
	bool numerical = _numerical.maximum_state_age_us && selected.permission_fresh;

	for (unsigned axis = 0; axis < kAxes; ++axis) {
		numerical &= std::isfinite(_numerical.maximum_measured_rate_rad_s(axis)) && _numerical.maximum_measured_rate_rad_s(axis) > 0.F
			     && std::isfinite(_numerical.maximum_measured_accel_rad_s2(axis)) && _numerical.maximum_measured_accel_rad_s2(axis) > 0.F;
	}

	numerical = numerical && research_stabilized_manual::validateStabilizedState(now, selected.state, _numerical,
			(_previous_valid && !source_change) ? &_previous_state : nullptr, attitude) == research_stabilized_manual::Status::Ok;
	uint64_t deadline = witness_fresh ? witness_deadline : 0;

	if (numerical) {
		numerical = fresh(now, selected.estimator_time, _status_age, deadline)
			    && fresh(now, selected.flags_time, _status_age, deadline)
			    && fresh(now, selected.permission.timestamp, _permission_age, deadline)
			    && fresh(now, selected.state.attitude_timestamp_us, _numerical.maximum_state_age_us, deadline)
			    && fresh(now, selected.state.attitude_sample_us, _numerical.maximum_state_age_us, deadline)
			    && fresh(now, selected.state.rate_timestamp_us, _numerical.maximum_state_age_us, deadline)
			    && fresh(now, selected.state.rate_sample_us, _numerical.maximum_state_age_us, deadline);
	}

	bool feedback_source_change = false;

	if (_feedback_required) {
		const auto &sample = acceleration.acceleration;
		AccelerationFeedbackInput input{};
		input.observed_us = selected.now;
		input.timestamp_us = sample.timestamp;
		input.timestamp_sample_us = sample.timestamp_sample;
		input.source_generation = sample.source_generation;
		input.publisher_revision = acceleration.acceleration_stable ? acceleration.acceleration_revision : 0;
		input.device_id = sample.device_id;
		input.selected_accel_device_id = selected.accel;
		input.selected_gyro_device_id = selected.gyro;
		input.bias_publisher_revision = sample.bias_publisher_revision;
		input.filter_cutoff_hz = sample.filter_cutoff_hz;
		input.filter_sample_rate_hz = sample.filter_sample_rate_hz;
		input.bias_timestamp = sample.bias_timestamp;
		input.bias_timestamp_sample = sample.bias_timestamp_sample;
		input.bias_instance = sample.bias_instance;
		input.calibrated = sample.calibrated;
		input.bias_valid = sample.bias_valid;
		input.bias_publisher_current = acceleration.bias_publisher_current;
		input.raw_publisher_revision = sample.raw_publisher_revision;
		input.raw_instance = sample.raw_instance;
		input.raw_publisher_current = acceleration.raw_publisher_current;
		input.specific_force_frd_m_s2 = research_stabilized_manual::Vector3f{sample.xyz};
		input.state = selected.state;
		input.state.valid &= numerical;
		_feedback_result = _feedback.evaluate(now, input, _feedback_sample);
		feedback_source_change = _feedback_result == AccelerationFeedbackResult::SourceChanged;
		external_change |= feedback_source_change;
		numerical &= _feedback_result == AccelerationFeedbackResult::Ready;

		if (numerical && _feedback_sample.deadline_us < deadline) { deadline = _feedback_sample.deadline_us; }
	}

	if (selected.state.valid) {
		_estimator_epoch = selected.state.source_epoch;
		_gyro = selected.gyro;
		_accel = selected.accel;
	}

	_previous_state = selected.state;

	for (unsigned i = 0; i < research_stabilized_manual::PublisherBinding::kTopics; ++i) { _state_revisions[i] = selected.revisions[i]; }

	_previous_valid = numerical;
	_state_deadline = numerical && witness_fresh ? deadline : 0;
	auto &context = _state.request_guard_context;
	context.validator_receive_time_us = now;
	context.arm_state = !witness_fresh ? research_acceleration::ArmState::Unknown :
			    (witness.arming_state == vehicle_status_s::ARMING_STATE_DISARMED && !witness.armed ? research_acceleration::ArmState::Disarmed :
			     witness.arming_state == vehicle_status_s::ARMING_STATE_ARMED
			     && witness.armed ? research_acceleration::ArmState::Armed : research_acceleration::ArmState::Unknown);
	context.mode_state = !witness_fresh ? research_acceleration::ResearchModeState::Unknown :
			     (_acceleration_mode >= vehicle_status_s::NAVIGATION_STATE_EXTERNAL1 && _acceleration_mode <= vehicle_status_s::NAVIGATION_STATE_EXTERNAL8
			      && witness.nav_state == _acceleration_mode ? research_acceleration::ResearchModeState::Selected :
			      research_acceleration::ResearchModeState::NotSelected);
	context.transport_connected = _connection_deadline && now < _connection_deadline;
	context.required_state_valid = witness_fresh && numerical && !witness.attitude_invalid && !witness.angular_velocity_invalid
				       && !witness.kill && !witness.lockdown && !witness.termination && !witness.in_esc_calibration_mode;

	const bool state_change = _state_seen && (external_change || context.arm_state != _observed.request_guard_context.arm_state
				  || context.mode_state != _observed.request_guard_context.mode_state
				  || context.required_state_valid != _observed.request_guard_context.required_state_valid
				  || context.transport_connected != _observed.request_guard_context.transport_connected);
	const bool expected_entry = state_change && witness_change && !source_change && !feedback_source_change && !_continuity_fault
				    && acceptEntryTransition(previous_witness, previous_witness_deadline, now);

	if ((state_change && !expected_entry) || (_entry_authorized && !_allow_entry_transitions)
	    || (_map_deadline && now >= _map_deadline) || _continuity_fault) { closeMap(false); }

	context.clock_mapping_valid = _state.map.valid;
	_state.observer_continuous = !_continuity_fault;
	const bool changed = !_state_seen || external_change || !sameContext(context, _observed.request_guard_context)
			     || !sameMap(_state.map, _observed.map);

	if (!_state_seen) { _state.generation = 1; }

	else if (changed && !advance(_state.generation)) { _state.observer_continuous = false; }

	_state_seen = true;
	_observed = _state;
	const auto result = _adapter->poll(_state);
	reports(result, now);
	_clock = now;
	return !_continuity_fault && !result.snapshot.terminal;
}

bool CompanionProtocol::captureControlInput(uint64_t configuration, AccelerationControlInput &input)
{
	return captureControlInputImpl(configuration, nullptr, input);
}

bool CompanionProtocol::captureControlInput(uint64_t configuration,
		const research_stabilized_manual::SelectedStateObservationSnapshot &selected, AccelerationControlInput &input)
{
	return captureControlInputImpl(configuration, &selected, input);
}

bool CompanionProtocol::captureControlInputImpl(uint64_t configuration,
		const research_stabilized_manual::SelectedStateObservationSnapshot *selected, AccelerationControlInput &input)
{
	_capture_pending = false;
	_capture_mode_pending = false;
	_capture_disarm_deadline = 0;
	const auto rejected = [&]() {
		const uint64_t decision = hrt_absolute_time();
		refreshStatus(decision);

		if (_continuity_fault || _snapshot.terminal || _status.first_latched_code || decision >= _capture_disarm_deadline) {
			_capture_disarm_deadline = 0;
		}

		const bool live = _status.session_established && _status.map_valid && _status.transport_connected
				  && _status.required_state_valid && _status.feedback_valid && !_status.continuity_fault;
		_capture_mode_pending &= live;
		_capture_pending &= live
				    && _state.request_guard_context.arm_state == research_acceleration::ArmState::Armed
				    && _state.request_guard_context.mode_state == research_acceleration::ResearchModeState::Selected;
		return false;
	};

	if (!_configured || !_adapter || !configuration || configuration != _configuration_identity || !_feedback_required) { return rejected(); }

	uint64_t now = hrt_absolute_time();
	const auto previous_witness = _witness;
	const uint32_t previous_epoch = _estimator_epoch, previous_gyro = _gyro, previous_accel = _accel;
	const uint8_t previous_reset = _previous_state.attitude_reset_counter;
	unsigned previous_revisions[research_stabilized_manual::PublisherBinding::kTopics] {};
	memcpy(previous_revisions, _state_revisions, sizeof(previous_revisions));
	const auto session = _adapter->sessionSnapshot();
	const bool entry_mode = _state.request_guard_context.mode_state == research_acceleration::ResearchModeState::Selected
				|| (_witness.nav_state == _manual_mode && _entry_authorized && _entry_arm_used && !_entry_acceleration_used);
	uint64_t previous_deadline = UINT64_MAX;
	const uint64_t previous_bounds[] {_state_deadline, _witness_deadline, _connection_deadline, _map_deadline, _control_deadline,
					  _feedback_sample.deadline_us
					 };

	for (uint64_t bound : previous_bounds) { previous_deadline = previous_deadline < bound ? previous_deadline : bound; }

	const bool previously_healthy = !_continuity_fault && !_snapshot.terminal && !_status.first_latched_code
					&& !_control_expired && _prepared && _state.map.valid && now < previous_deadline
					&& _receiver_revision && _input[_configuration.mavlink_instance].revision() == _receiver_revision
					&& _signing_generation && _receiver_system && _receiver_component
					&& _state.request_guard_context.arm_state == research_acceleration::ArmState::Armed && entry_mode
					&& _state.request_guard_context.required_state_valid && _state.request_guard_context.transport_connected
					&& _previous_valid && _feedback_result == AccelerationFeedbackResult::Ready
					&& session.gate == research_acceleration::SessionGate::Established && session.has_last_successful_session
					&& session.last_successful_session.owner == _state.map.source
					&& session.last_successful_session.session == _state.map.session;
	actuator_armed_s permission{};

	if (!observe(now, &permission) || !_previous_valid || _feedback_result != AccelerationFeedbackResult::Ready
	    || !_state_deadline || now >= _state_deadline) { return rejected(); }

	if (selected) {
		using Binding = research_stabilized_manual::PublisherBinding;

		// Epoch counters belong to individual observers and can differ even for
		// identical publishers. Bind the actual state publisher lifetimes instead.
		if (!selected->state.valid || !selected->now || selected->now > now
		    || selected->gyro != _gyro || selected->accel != _accel
		    || selected->state.attitude_reset_counter != _previous_state.attitude_reset_counter
		    || (selected->required_topics & Binding::kStateTopicsMask) != Binding::kStateTopicsMask) { return rejected(); }

		for (unsigned i = 0; i < Binding::kTopics; ++i) {
			if ((Binding::kStateTopicsMask & (1U << i))
			    && (!selected->revisions[i] || selected->revisions[i] != _state_revisions[i])) { return rejected(); }
		}
	}

	bool source_unchanged = _estimator_epoch == previous_epoch && _gyro == previous_gyro && _accel == previous_accel
				&& _previous_state.attitude_reset_counter == previous_reset;
	using Binding = research_stabilized_manual::PublisherBinding;

	for (unsigned i = 0; i < Binding::kTopics; ++i) {
		if (Binding::kStateTopicsMask & (1U << i)) { source_unchanged &= _state_revisions[i] == previous_revisions[i]; }
	}

	const bool witness_disarm = _witness.arming_state == vehicle_status_s::ARMING_STATE_DISARMED || !_witness.armed;
	const bool witness_ordered = (_witness.transition == previous_witness.transition && sameWitness(_witness, previous_witness))
				     || (previous_witness.transition != UINT64_MAX && _witness.transition == previous_witness.transition + 1
					 && (_witness.nav_state == previous_witness.nav_state || witness_disarm));
	const bool disarm_observed = !(_witness.arming_state == vehicle_status_s::ARMING_STATE_ARMED && _witness.armed && permission.armed)
				     && (_witness.arming_state == vehicle_status_s::ARMING_STATE_DISARMED || !_witness.armed || !permission.armed);
	const bool uninhibited = !_witness.kill && !_witness.lockdown && !_witness.termination && !_witness.in_esc_calibration_mode
				 && !permission.kill && !permission.lockdown && !permission.termination && !permission.in_esc_calibration_mode;

	if (previously_healthy && now < previous_deadline && source_unchanged && witness_ordered && disarm_observed && uninhibited
	    && (_witness.arming_state == vehicle_status_s::ARMING_STATE_ARMED || _witness.arming_state == vehicle_status_s::ARMING_STATE_DISARMED)
	    && !_status.first_latched_code && _state.request_guard_context.required_state_valid
	    && _state.request_guard_context.transport_connected && _commander_revision && _vehicle.revision() == _commander_revision
	    && _receiver_revision && _input[_configuration.mavlink_instance].revision() == _receiver_revision) {
		const uint64_t deadline = previous_deadline < _state_deadline ? previous_deadline : _state_deadline;
		// An actuator-first publication need not have reached Commander's atomic
		// witness yet. Revoke this map too, without a second authority observation.
		closeMap(false);
		_capture_disarm_deadline = deadline;
		return rejected();
	}

	if (disarm_observed || !uninhibited) {
		// A failed ordinary-stop proof remains a fault. The held atomic witness
		// must never let a newly disarmed/inhibited actuator copy reach a command.
		closeMap(false);
		return rejected();
	}

	// Commander publishes VehicleStatus before its atomic companion witness.
	// A deliberate transfer may observe that edge first. Wait only for this
	// already-authorized Manual-to-Acceleration witness; never synthesize it.
	if (_entry_authorized && _entry_arm_used && !_entry_acceleration_used && _allow_entry_transitions
	    && _state.request_guard_context.arm_state == research_acceleration::ArmState::Armed
	    && _state.request_guard_context.mode_state == research_acceleration::ResearchModeState::NotSelected
	    && _witness.nav_state == _manual_mode) {
		_capture_mode_pending = true;
		return rejected();
	}

	AccelerationControlInput candidate{};
	const auto result = _adapter->getEligibleFullRequest(_state, candidate.request);
	reports(result, now);

	if (result.code != research_acceleration::AdapterCode::EligibleRequest || result.admission_closed
	    || result.snapshot.terminal) {
		_capture_pending = result.code == research_acceleration::AdapterCode::NoEligibleRequest && !result.snapshot.terminal;
		return rejected();
	}

	candidate.context = _state.request_guard_context;
	candidate.selected_state = _previous_state;
	candidate.feedback = _feedback_sample;
	candidate.configuration = configuration;
	candidate.now_us = now;
	candidate.evidence_deadline_us = _state_deadline;
	const uint64_t deadlines[] {_witness_deadline, _connection_deadline, _map_deadline};

	for (uint64_t deadline : deadlines) {
		if (deadline <= now) { return rejected(); }

		if (deadline < candidate.evidence_deadline_us) { candidate.evidence_deadline_us = deadline; }
	}

	// Status publication above can consume time. Recheck at the actual return
	// boundary without renewing any original observation or request deadline.
	const auto &request = candidate.request;
	const uint64_t uncertainty = request.clock_mapping_uncertainty_us;
	uint64_t age_deadline{};

	if (request.producer_time_in_validator_domain_us < uncertainty || request.expiry_time_in_validator_domain_us < uncertainty
	    || !add(request.producer_time_in_validator_domain_us - uncertainty,
		    static_cast<uint64_t>(_configuration.maximum_command_age_us) + 1, age_deadline)) { return rejected(); }

	const uint64_t expiry = request.expiry_time_in_validator_domain_us - uncertainty;
	refreshStatus(hrt_absolute_time());
	candidate.now_us = hrt_absolute_time();

	if (candidate.now_us < now || candidate.now_us >= candidate.evidence_deadline_us
	    || candidate.now_us >= age_deadline || candidate.now_us >= expiry) { return rejected(); }

	input = candidate;
	return true;
}

bool CompanionProtocol::disarmed(uint64_t now)
{
	return !_continuity_fault && !_snapshot.terminal && _commander_revision && _vehicle.revision() == _commander_revision
	       && _witness_deadline && now < _witness_deadline
	       && _witness.arming_state == vehicle_status_s::ARMING_STATE_DISARMED && !_witness.armed
	       && !_witness.kill && !_witness.lockdown && !_witness.termination && !_witness.in_esc_calibration_mode;
}

bool CompanionProtocol::freezeMap(const slaaps_companion_packet_s &sample, uint64_t now)
{
	const bool renewal = _state.map.valid;
	const auto previous = _state.map;
	const uint64_t previous_deadline = _map_deadline;
	const uint64_t interval = static_cast<uint64_t>(_configuration.map_interval_us);
	// A software trigger, not a measured scheduling guarantee. Renewal opens
	// within the last half interval, or the declared RTT when it is larger.
	uint64_t renewal_lead{};

	if (!_prepared || (!renewal && !disarmed(now)) || sample.control_binding_id != _binding
	    || sample.controller_instance_id != _identity || sample.configuration_identity != _configuration_identity
	    || sample.event != slaaps_companion_packet_s::EVENT_CLOCK_SAMPLE
	    || sample.message_id != MAVLINK_MSG_ID_TIMESYNC || sample.payload_length < 16
	    || sample.payload_length > MAVLINK_MSG_ID_TIMESYNC_LEN || !sample.clock_sent_us
	    || sample.clock_sent_us > sample.timestamp || sample.timestamp > now
	    || sample.clock_sent_us > static_cast<uint64_t>(INT64_MAX) / 1000) { return false; }

	if (renewal) {
		if (!companionClockRenewalConfigured(_configuration, renewal_lead) || !previous_deadline || now >= previous_deadline
		    || previous_deadline - now > renewal_lead) { return false; }
	}

	mavlink_message_t message{};
	message.msgid = sample.message_id;
	message.len = sample.payload_length;
	memcpy(_MAV_PAYLOAD_NON_CONST(&message), sample.payload, sample.payload_length);
	const int64_t remote_ns = mavlink_msg_timesync_get_tc1(&message);
	const int64_t echoed_ns = mavlink_msg_timesync_get_ts1(&message);
	const uint64_t rtt = sample.timestamp - sample.clock_sent_us;

	if (remote_ns <= 0 || echoed_ns != static_cast<int64_t>(sample.clock_sent_us * 1000)
	    || rtt > static_cast<uint64_t>(_configuration.maximum_round_trip_us)) { return false; }

	const uint64_t producer = static_cast<uint64_t>(remote_ns) / 1000;
	const uint64_t midpoint = sample.clock_sent_us + rtt / 2;
	const uint64_t difference = midpoint >= producer ? midpoint - producer : producer - midpoint;
	const uint64_t drift_ppm = static_cast<uint64_t>(_configuration.maximum_relative_drift_ppm);
	uint64_t drift_product{}, uncertainty{}, until{}, mapped_until{};

	if (difference > static_cast<uint64_t>(INT64_MAX) || !producer || !drift_ppm || interval > UINT64_MAX / drift_ppm
	    || !add(interval * drift_ppm, 999999, drift_product)
	    || !add((rtt + 1) / 2, static_cast<uint64_t>(_configuration.timestamp_uncertainty_us), uncertainty)
	    || !add(uncertainty, drift_product / 1000000, uncertainty)
	    || !uncertainty || uncertainty > static_cast<uint64_t>(_configuration.maximum_clock_uncertainty_us)
	    || !add(producer, interval, until)) { return false; }

	const int64_t shift = midpoint >= producer ? static_cast<int64_t>(difference) : -static_cast<int64_t>(difference);

	if (!offset(until, shift, mapped_until) || mapped_until < uncertainty || mapped_until - uncertainty <= now) { return false; }

	const uint64_t candidate_deadline = mapped_until - uncertainty;

	if (renewal) {
		if (!observe(now)) { return false; }

		const auto session = _adapter->sessionSnapshot();
		uint64_t receipt_deadline = UINT64_MAX;
		uint64_t previous_projection{}, overlap{};

		// Reobserve at the final local clock. An expired/replaced namespace or
		// state fault cannot be reopened by an otherwise authentic clock reply.
		if (!_allow_entry_transitions || !_entry_authorized || !_prepared || !_state.map.valid
		    || !sameMap(previous, _state.map) || _map_deadline != previous_deadline || now >= previous_deadline
		    || previous_deadline - now > renewal_lead || !_state.request_guard_context.required_state_valid
		    || !_state.request_guard_context.transport_connected || !_state.request_guard_context.clock_mapping_valid
		    || _continuity_fault || _snapshot.terminal || session.gate != research_acceleration::SessionGate::Established
		    || !session.has_last_successful_session || session.last_successful_session.owner != previous.source
		    || session.last_successful_session.session != previous.session || previous.session != _prepared_session
		    || previous.source != static_cast<uint64_t>(_configuration.source_id) || previous.controller_instance_id != _identity
		    || !_receiver_revision || _input[_configuration.mavlink_instance].revision() != _receiver_revision
		    || !sample.mavlink2 || !sample.signing_active || !sample.signature_verified || !_signing_generation
		    || sample.signing_generation != _signing_generation || sample.mavlink_instance != _configuration.mavlink_instance
		    || sample.source_system != _configuration.source_system || sample.source_component != _configuration.source_component
		    || sample.receiver_system != _receiver_system || sample.receiver_component != _receiver_component
		    || !fresh(now, sample.timestamp, static_cast<uint64_t>(_configuration.maximum_age_us), receipt_deadline)
		    || producer <= previous.valid_from_producer_us || producer > previous.valid_until_producer_us
		    || !offset(producer, previous.validator_minus_producer_us, previous_projection)
		    || !add(previous.uncertainty_us, uncertainty, overlap)
		    || (midpoint >= previous_projection ? midpoint - previous_projection : previous_projection - midpoint) > overlap
		    || candidate_deadline <= previous_deadline || candidate_deadline - now <= renewal_lead) { return false; }
	}

	if (!advance(_map_epoch)) { return false; }

	auto &map = _state.map;
	map.valid = true;
	map.source = static_cast<uint64_t>(_configuration.source_id);
	map.session = _prepared_session;
	map.controller_instance_id = _identity;
	map.map_epoch = _map_epoch;
	map.validator_minus_producer_us = shift;
	map.uncertainty_us = uncertainty;
	map.valid_from_producer_us = producer;
	map.valid_until_producer_us = until;
	_map_deadline = candidate_deadline;
	// The caller's normal observe advances the state generation for this exact
	// map change. WireAdapter drops the old request/ACK, retaining the same
	// established session and replay floors. No request/output lease is renewed.
	return true;
}

void CompanionProtocol::packet(const slaaps_companion_packet_s &value, unsigned revision, uint64_t &now)
{
	if (!_adapter || value.timestamp <= _started) { return; }

	if (!revision || (_receiver_revision && (revision != _receiver_revision
			  || value.signing_generation != _signing_generation || value.receiver_system != _receiver_system
			  || value.receiver_component != _receiver_component))) {
		closeMap(false);
		_connection_deadline = 0;
		_packet_sequence = 0;
	}

	const bool sequence_gap = !value.sequence || (_packet_sequence
				  && (_packet_sequence == UINT64_MAX || value.sequence != _packet_sequence + 1));
	_receiver_revision = revision;
	_signing_generation = value.signing_generation;
	_receiver_system = value.receiver_system;
	_receiver_component = value.receiver_component;
	_packet_sequence = value.sequence;

	if (sequence_gap) { closeMap(false); _connection_deadline = 0; }

	uint64_t deadline = UINT64_MAX;

	if (sequence_gap || !revision || revision != _input[_configuration.mavlink_instance].revision()
	    || value.mavlink_instance != _configuration.mavlink_instance
	    || !fresh(now, value.timestamp, static_cast<uint64_t>(_configuration.maximum_age_us), deadline)
	    || value.event == slaaps_companion_packet_s::EVENT_LINK_STATE
	    || !value.mavlink2 || !value.signing_active || !value.signature_verified || !value.signing_generation
	    || value.source_system != _configuration.source_system || value.source_component != _configuration.source_component
	    || !value.receiver_system || !value.receiver_component) { return; }

	if (value.event == slaaps_companion_packet_s::EVENT_CLOCK_SAMPLE) {
		if (freezeMap(value, now)) { _connection_deadline = deadline; (void)observe(now); }

		return;
	}

	if (value.event != slaaps_companion_packet_s::EVENT_FRAME) { return; }

	const bool is_session = value.message_id == MAVLINK_MSG_ID_RESEARCH_ACCELERATION_SESSION;
	const bool is_request = value.message_id == MAVLINK_MSG_ID_RESEARCH_ACCELERATION_REQUEST;

	// MAVLink2 may trim zero trailing fields, but the last mandatory nonzero
	// v1 enum must be present. Getters decode all remaining fields from zeroed storage.
	if ((!is_session && !is_request) || value.payload_length < (is_session ? 71 : 85)
	    || value.payload_length > (is_session ? MAVLINK_MSG_ID_RESEARCH_ACCELERATION_SESSION_LEN :
				       MAVLINK_MSG_ID_RESEARCH_ACCELERATION_REQUEST_LEN)) { return; }

	mavlink_message_t message{};
	message.msgid = value.message_id;
	message.len = value.payload_length;
	memcpy(_MAV_PAYLOAD_NON_CONST(&message), value.payload, value.payload_length);
	const uint64_t source = is_session ? mavlink_msg_research_acceleration_session_get_source_id(&message) :
				mavlink_msg_research_acceleration_request_get_source_id(&message);
	const uint8_t target_system = is_session ? mavlink_msg_research_acceleration_session_get_target_system(&message) :
				      mavlink_msg_research_acceleration_request_get_target_system(&message);
	const uint8_t target_component = is_session ? mavlink_msg_research_acceleration_session_get_target_component(&message) :
					 mavlink_msg_research_acceleration_request_get_target_component(&message);

	if (source != static_cast<uint64_t>(_configuration.source_id) || target_system != value.receiver_system
	    || target_component != value.receiver_component) { return; }

	// This is authenticated configured-source transport liveness, independent
	// of the legacy last-frame diagnostic. A core-rejected packet may prove link
	// liveness, but never renews an accepted command's creation/expiry or map.
	_connection_deadline = deadline;

	if (!observe(now)) { return; }

	const Ingress ingress{true, true, true, true};

	if (is_session) {
		const auto decoded = session(message);
		bool canonical = decoded.protocol_version == research_acceleration::kWireProtocolVersionV1
				 && decoded.session && decoded.operation_sequence && !decoded.flags
				 && !decoded.producer_acknowledgement_monotonic_us && !decoded.acknowledged_status_sequence;

		for (uint8_t reserved : decoded.reserved) { canonical &= reserved == 0; }

		if (canonical && decoded.operation == 1 && decoded.controller_instance_id == _identity
		    && decoded.transition_epoch == _snapshot.transition_epoch && decoded.clock_mapping_epoch) {
			_proposal = decoded;
			_proposal_ingress = ingress;
			_proposal_deadline = deadline;
		}

		if (decoded.operation == 3) { reports(_adapter->processAcknowledgement(_state, decoded, ingress), now); }

		else { reports(_adapter->processSession(_state, decoded, ingress, nullptr), now); }

	} else { reports(_adapter->processRequest(_state, request(message), ingress), now); }
}

void CompanionProtocol::action(int32_t command, uint64_t &now)
{
	_status.action = static_cast<uint8_t>(command);
	_status.action_result = 2;

	if (!observe(now) || !disarmed(now) || command < 1 || command > 4) { return; }

	if (command == 1) {
		if (!_proposal_deadline || now >= _proposal_deadline || _proposal.controller_instance_id != _identity
		    || _proposal.transition_epoch != _snapshot.transition_epoch) { return; }

		const auto proposed_session = _proposal.session;
		closeMap();

		if (_continuity_fault) { return; }

		_prepared = true;
		_prepared_session = proposed_session;
		_state.map.source = static_cast<uint64_t>(_configuration.source_id);
		_state.map.session = proposed_session;
		(void)observe(now);
		_status.action_result = 1;
		return;
	}

	if (command == 3) {
		closeMap();
		(void)observe(now); // Actual observer barrier revokes source/session; no forged wire operation.
		_status.action_result = _continuity_fault ? 2 : 1;
		return;
	}

	if (command == 2 && (!_prepared || !_state.map.valid || !_proposal_deadline || now >= _proposal_deadline
			     || _proposal.session != _prepared_session || _proposal.controller_instance_id != _identity
			     || _proposal.transition_epoch != _snapshot.transition_epoch || _proposal.clock_mapping_epoch != _state.map.map_epoch)) { return; }

	if (command == 4) { closeMap(); if (!observe(now)) { return; } }

	if (!advance(_admin_sequence)) { return; }

	research_acceleration::SupervisorEvent grant{};
	grant.event_id = _admin_sequence;
	grant.expected_state_generation = _state.generation;
	grant.expected_transition_epoch = _snapshot.transition_epoch;
	grant.expected_controller_instance_id = _identity;
	grant.action = command == 2 ? research_acceleration::SupervisorAction::Establish :
		       research_acceleration::SupervisorAction::ClearLatchedFault;
	grant.has_session_binding = command == 2;
	grant.session = command == 2 ? research_acceleration::TrustedSession{_proposal.source, _proposal.session} :
			research_acceleration::TrustedSession{};
	const auto result = command == 2 ? _adapter->processSession(_state, _proposal, _proposal_ingress, &grant) :
			    _adapter->clearLatchedFault(_state, grant);
	reports(result, now);
	_status.action_result = result.action_performed && result.code == research_acceleration::AdminCode::Completed ? 1 : 2;

	if (command == 2 && _status.action_result == 1) {
		_entry_authorized = _allow_entry_transitions;
		_entry_arm_used = false;
		_entry_acceleration_used = _witness.nav_state == _acceleration_mode;
	}

	_proposal_deadline = 0;
}

void CompanionProtocol::publishControl(uint64_t now)
{
	slaaps_companion_control_s control{};
	control.timestamp = now;
	control.configuration_identity = _configuration_identity;
	control.controller_instance_id = _identity;
	control.binding_id = _binding;
	control.maximum_round_trip_us = _configuration.maximum_round_trip_us > 0 ? static_cast<uint32_t>(_configuration.maximum_round_trip_us) : 0;
	control.mavlink_instance = _configuration.mavlink_instance >= 0 ? static_cast<uint8_t>(_configuration.mavlink_instance) : 0;
	control.source_system = static_cast<uint8_t>(_configuration.source_system);
	control.source_component = static_cast<uint8_t>(_configuration.source_component);
	control.enabled = _configured && !_continuity_fault && !_snapshot.terminal && !_control_expired && _binding && _status_age
			  && add(now, _status_age, control.deadline);

	if (_control.publish(control) && control.enabled) { _control_deadline = control.deadline; }
}

bool CompanionProtocol::send(const WireStatus &value, uint64_t now, bool barrier)
{
	constexpr uint8_t barrier_status_class = 3; // Frozen v1 logical status class.

	if (barrier && value.status_class != barrier_status_class) { return false; }

	publishControl(now);
	slaaps_companion_tx_s tx{};
	tx.timestamp = now;
	tx.deadline = _control_deadline;

	if (_map_deadline && _map_deadline < tx.deadline) { tx.deadline = _map_deadline; }

	if (_witness_deadline && _witness_deadline < tx.deadline) { tx.deadline = _witness_deadline; }

	if (_state.request_guard_context.transport_connected && _connection_deadline < tx.deadline) { tx.deadline = _connection_deadline; }

	tx.configuration_identity = _configuration_identity;
	tx.controller_instance_id = _identity;
	tx.binding_id = _binding;
	tx.mavlink_instance = static_cast<uint8_t>(_configuration.mavlink_instance);
	tx.target_system = static_cast<uint8_t>(_configuration.source_system);
	tx.target_component = static_cast<uint8_t>(_configuration.source_component);
	tx.signing_generation = _signing_generation;
	const bool bootstrap = value.clock_mapping_state == 2 && value.transport_state == 2
			       && value.session_state != 2 && !value.source_id && !value.session_id && !(value.status_flags & 1U);

	if (!tx.signing_generation && !bootstrap) { return false; }

	return !_continuity_fault && !_control_expired && now < tx.deadline
	       && research_acceleration::encodeStatusV1Payload(value, tx.payload) == research_acceleration::StatusEncodeCode::Ok
	       && (barrier ? _barrier_tx.publish(tx) : _tx.publish(tx));
}

template<typename Report> void CompanionProtocol::report(const Report &value, uint64_t now, bool barrier)
{
	WireStatus wire{};
	const research_acceleration::StatusRecipient recipient{static_cast<uint8_t>(_configuration.source_system),
			static_cast<uint8_t>(_configuration.source_component), research_acceleration::StatusDelivery::Directed};

	if (research_acceleration::buildStatusV1(value, recipient, wire) != research_acceleration::StatusBuildCode::Ok) {
		if (_status.status_failures != UINT64_MAX) { ++_status.status_failures; }

		return;
	}

	_status.decision_code = wire.decision_code;
	_status.first_latched_code = wire.first_latched_code;
	_status.acknowledgement_current = value.evidence.acknowledgement_current;

	if (barrier) { _barrier = wire; _have_barrier = true; _last_barrier_send = now; }

	if (!send(wire, now, barrier) && _status.status_failures != UINT64_MAX) { ++_status.status_failures; }
}

void CompanionProtocol::reports(const research_acceleration::AdapterResult &result, uint64_t now)
{
	_snapshot = result.snapshot;

	for (unsigned i = 0; i < result.report_count; ++i) {
		report(result.reports[i], now, result.reports[i].kind == research_acceleration::AdapterReportKind::Barrier);
	}
}
void CompanionProtocol::reports(const research_acceleration::AdminResult &result, uint64_t now)
{
	_snapshot = result.snapshot;

	for (unsigned i = 0; i < result.report_count; ++i) {
		const auto kind = result.reports[i].kind;
		report(result.reports[i], now, kind == research_acceleration::AdminReportKind::ExogenousBarrier
		       || kind == research_acceleration::AdminReportKind::AdministrativeBarrier);
	}
}
void CompanionProtocol::reports(const research_acceleration::IngressResult &result, uint64_t now)
{
	_snapshot = result.snapshot;

	for (unsigned i = 0; i < result.report_count; ++i) {
		report(result.reports[i], now, result.reports[i].kind == research_acceleration::IngressReportKind::ExogenousBarrier);
	}
}

void CompanionProtocol::refreshStatus(uint64_t now)
{
	_status.timestamp = now;
	_status.feedback_required = _feedback_required;
	_status.feedback_result = static_cast<uint8_t>(_feedback_result);
	_status.feedback_valid = _configured && _feedback_required && _feedback_result == AccelerationFeedbackResult::Ready
				 && _feedback_sample.deadline_us && now < _feedback_sample.deadline_us;
	_status.feedback_deadline = _status.feedback_valid ? _feedback_sample.deadline_us : 0;

	for (unsigned axis = 0; axis < kAxes; ++axis) {
		_status.acceleration_cg_body[axis] = _status.feedback_valid ? _feedback_sample.acceleration_frd_m_s2(axis) : 0.F;
	}

	_status.configuration_identity = _configuration_identity;
	_status.controller_instance_id = _identity;
	_status.binding_id = _binding;
	_status.state_generation = _state.generation;
	_status.transition_epoch = _snapshot.transition_epoch;
	_status.commander_transition = _witness.transition;
	_status.commander_revision = _commander_revision;
	_status.estimator_epoch = _estimator_epoch;
	_status.receiver_revision = _receiver_revision;
	_status.signing_generation = _signing_generation;
	_status.map_epoch = _state.map.map_epoch;
	_status.map_source = _state.map.source;
	_status.map_session = _state.map.session;
	_status.map_offset_us = _state.map.validator_minus_producer_us;
	_status.map_uncertainty_us = _state.map.uncertainty_us;
	_status.map_deadline = _map_deadline;
	_status.proposal_session = _proposal.session;
	_status.proposal_deadline = _proposal_deadline;
	_status.connection_deadline = _connection_deadline;
	_status.barrier_sequence = _have_barrier ? _barrier.status_sequence : 0;
	_status.barrier_time = _have_barrier ? _barrier.validator_time_us : 0;
	_status.transport_connected = _connection_deadline && now < _connection_deadline;
	_status.required_state_valid = _state.request_guard_context.required_state_valid && _state_deadline && now < _state_deadline;
	_status.map_valid = _state.map.valid && _map_deadline && now < _map_deadline;
	_status.prepared = _prepared;
	_status.continuity_fault = _continuity_fault || _snapshot.terminal;
	const auto gate = _adapter ? _adapter->sessionSnapshot().gate : research_acceleration::SessionGate::NoTrustedSession;
	_status.session_gate = static_cast<uint8_t>(gate);
	_status.session_established = gate == research_acceleration::SessionGate::Established && !_status.continuity_fault && _status.map_valid;
	_status.phase = !_configured ? (_configuration.admission_enabled ? 1 : 0) : _status.continuity_fault ? 7 :
			_status.session_established ? 6 : _prepared ? (_status.map_valid ? 5 : 4) :
			!_witness_deadline || now >= _witness_deadline || !_status.required_state_valid ? 2 : 3;
}

void CompanionProtocol::publishStatus(uint64_t now)
{
	refreshStatus(now);
	_publication.publish(_status);
}

void CompanionProtocol::update(const VehicleConfiguration *configuration, uint64_t identity, int32_t apply,
			       int8_t manual_mode, int8_t acceleration_mode, bool allow_entry_transitions, uint64_t now)
{
	if ((_manual_mode >= 0 && manual_mode != _manual_mode) || (_acceleration_mode >= 0 && acceleration_mode != _acceleration_mode)) {
		closeMap(false);
	}

	_manual_mode = manual_mode;
	_acceleration_mode = acceleration_mode;
	_allow_entry_transitions = allow_entry_transitions;

	if (!_initialized) { publishStatus(now); return; }

	if (!configuration || !identity) {
		stop();
		_configuration_identity = 0;

	} else if (_configuration_identity != identity || _apply != apply) { (void)configure(*configuration, identity, apply); }

	if (_configured) {
		if (_control_deadline && now >= _control_deadline) {
			_control_expired = true;
			_continuity_fault = true; // No silent recovery of an expired transport namespace.
			closeMap(false);
		}

		auto &input = _input[_configuration.mavlink_instance];

		if (_receiver_revision && input.revision() != _receiver_revision) {
			closeMap(false);
			_connection_deadline = 0;
		}

		(void)observe(now);
		publishControl(now);

		for (unsigned count = 0; count < slaaps_companion_packet_s::ORB_QUEUE_LENGTH && input.updated(); ++count) {
			slaaps_companion_packet_s value{};
			unsigned revision{};

			if (!input.copyQueuedBound(&value, revision)) { break; }

			now = hrt_absolute_time();
			packet(value, revision, now);
		}
	}

	int32_t command{};
	bool consumed = false;

	{
		const AtomicTransaction transaction;
		const int32_t none = 0;
		consumed = param_get(_action, &command) == 0 && command && param_set(_action, &none) == 0;
	}

	// Consume under the existing recursive parameter lock; execution is outside
	// the critical section. A later operator action remains pending next cycle.
	if (consumed) {
		if (_configured) { action(command, now); }

		else { _status.action = command >= 1 && command <= 4 ? static_cast<uint8_t>(command) : 0; _status.action_result = 2; }

	}

	if (_configured) {
		(void)observe(now);

		if (_have_barrier && !_status.acknowledgement_current && now >= _last_barrier_send
		    && now - _last_barrier_send >= static_cast<uint64_t>(_configuration.maximum_age_us)) {
			if (!send(_barrier, now, true) && _status.status_failures != UINT64_MAX) { ++_status.status_failures; }

			_last_barrier_send = now;
		}
	}

	publishStatus(hrt_absolute_time());
}

void CompanionProtocol::printStatus() const
{
	uORB::Subscription subscription{ORB_ID(slaaps_companion_status)};
	slaaps_companion_status_s value{};

	if (!subscription.copy(&value)) { PX4_INFO("companion protocol status unavailable"); return; }

	PX4_INFO("companion protocol phase=%u action=%u result=%u controller=%llu binding=%llu generation=%llu epoch=%llu",
		 value.phase, value.action, value.action_result, static_cast<unsigned long long>(value.controller_instance_id),
		 static_cast<unsigned long long>(value.binding_id), static_cast<unsigned long long>(value.state_generation),
		 static_cast<unsigned long long>(value.transition_epoch));
	PX4_INFO("companion feedback required=%d valid=%d result=%u deadline=%llu",
		 value.feedback_required, value.feedback_valid, value.feedback_result,
		 static_cast<unsigned long long>(value.feedback_deadline));
	PX4_INFO("companion map valid=%d source=%llu session=%llu epoch=%llu offset=%lld uncertainty=%llu deadline=%llu",
		 value.map_valid, static_cast<unsigned long long>(value.map_source), static_cast<unsigned long long>(value.map_session),
		 static_cast<unsigned long long>(value.map_epoch), static_cast<long long>(value.map_offset_us),
		 static_cast<unsigned long long>(value.map_uncertainty_us), static_cast<unsigned long long>(value.map_deadline));
	PX4_INFO("companion session=%d ack=%d prepared=%d connected=%d state=%d fault=%d decision=%u barrier=%llu failures=%llu",
		 value.session_established, value.acknowledgement_current, value.prepared, value.transport_connected,
		 value.required_state_valid, value.continuity_fault, value.decision_code,
		 static_cast<unsigned long long>(value.barrier_sequence), static_cast<unsigned long long>(value.status_failures));
}

} // namespace slaaps
