# SLAAPS mode/configuration supervisor. This is not physical output feedback.
uint64 timestamp
uint64 configuration_identity
uint32 can_run_mask
uint32 can_arm_mask
bool input_metadata_valid # Single-estimator metadata only; not numerical/manual validation or flight admission
uint32 input_source_epoch # Changes when an observed publisher lifetime changes
uint32 manual_command_revision # Supervisor-owned endpoint lifetime, zero if unavailable/replaced; not output admission
uint32 manual_permission_revision # Supervisor-owned permission endpoint lifetime; not an admission flag
bool manual_worker_active
uint32 schedule_interval_us # Requested queue interval, not measured deadline compliance
uint8 manual_worker_status # slaaps::ManualWorkerStatus
uint8 manual_preparation_status # slaaps::PreparationStatus; readiness is not output authority
uint64 manual_preparation_session # Exact expiring disarmed preparation session
uint64 manual_preparation_deadline
uint8 manual_preparation_banks # Required MAIN/AUX bank mask
uint8 manual_entry_status # Software lifecycle only; never flight/output admission
uint8 MANUAL_ENTRY_INACTIVE = 0
uint8 MANUAL_ENTRY_WAIT_CONFIGURATION = 1
uint8 MANUAL_ENTRY_WAIT_ROUTING = 2
uint8 MANUAL_ENTRY_WAIT_INPUT = 3
uint8 MANUAL_ENTRY_WAIT_CENTER = 4
uint8 MANUAL_ENTRY_WAIT_THROTTLE = 5
uint8 MANUAL_ENTRY_WAIT_STATE = 6
uint8 MANUAL_ENTRY_PREPARING = 7
uint8 MANUAL_ENTRY_PREPARED = 8
uint8 MANUAL_ENTRY_REJECTED = 9
uint8 MANUAL_ENTRY_ACTIVATING = 10
uint8 MANUAL_ENTRY_FIRST_COMMAND = 11
uint8 MANUAL_ENTRY_RUNNING = 12
uint8 MANUAL_ENTRY_WAIT_SCHEDULE = 13 # Initial zero-EDF point outside configured bounds or invalid interpolated authority
uint8 MANUAL_ENTRY_WAIT_POWER = 14
uint8 bench_entry_status # Explicit servo or armed ground EDF commissioning; never flight admission
uint8 BENCH_ENTRY_INACTIVE = 0
uint8 BENCH_ENTRY_WAIT_CONFIGURATION = 1
uint8 BENCH_ENTRY_WAIT_ROUTING = 2
uint8 BENCH_ENTRY_WAIT_PREARM = 3
uint8 BENCH_ENTRY_WAIT_INPUT = 4
uint8 BENCH_ENTRY_WAIT_CENTER = 5
uint8 BENCH_ENTRY_READY = 6
uint8 BENCH_ENTRY_PREPARING = 7
uint8 BENCH_ENTRY_ACTIVATING = 8
uint8 BENCH_ENTRY_FIRST_COMMAND = 9
uint8 BENCH_ENTRY_RUNNING = 10
uint8 BENCH_ENTRY_COMPLETE = 11
uint8 BENCH_ENTRY_REJECTED = 12
uint8 BENCH_ENTRY_WAIT_SELECTION = 13
uint8 BENCH_ENTRY_WAIT_THROTTLE = 14
uint8 BENCH_ENTRY_WAIT_ARM = 15
uint8 BENCH_ENTRY_EDF_RUNNING = 16
uint8 BENCH_ENTRY_WAIT_DISARM = 17
uint8 BENCH_ENTRY_DISARM_FAILED = 18
uint8 BENCH_ENTRY_WAIT_POWER = 19
uint64 bench_session_deadline # Immutable operator-requested total session expiry
uint8 bench_target # 0 combined, 1..4 individual calibrated canard, 5 armed EDF commissioning
bool bench_worker_active
bool bench_edf_commissioning
bool bench_commissioning_ready # Fresh disarmed exact prepared session, not an armed output grant
uint64 bench_commissioning_ready_deadline # Original copied authority/input/preparation expiry, never a renewed sample
bool bench_disarm_pending # Output revoked; completion requires fresh actual disarm, not command ACK
uint32 snapshot_duration_us
int32 apply_counter
int8[3] mode_id # Manual, acceleration, servo bench; -1 until registered
uint8 configuration_fault
char[17] cfg_param # Exact last rejected apply parameter; empty for unknown/cross-field faults
uint8 configuration_use # 0 servo bench, 1 stabilized manual, 2 EDF commissioning
bool configuration_valid
bool routing_snapshot_valid # Observation only, not output permission or an atomic cross-bank transaction
uint8 inventory_fault
uint8 routing_fault
int16 io_index # Current rejected inventory index; -1 if no actual channel is identified
uint8 io_bank # 0 MAIN, 1 AUX, 255 unknown; never inferred for a missing/stale report
uint8 io_channel # One-based physical bank channel; zero when unknown
bool direct_actuator_modes_configured

uint8 UNAVAILABLE_CONFIGURATION = 0
uint8 UNAVAILABLE_OUTPUT_INTEGRATION = 1
uint8 UNAVAILABLE_ACCELERATION_PORT = 2
uint8 UNAVAILABLE_REGISTRATION = 3
uint8 AVAILABLE_SERVO_BENCH = 4 # Servo selection only; no arming, EDF or flight permission
uint8 AVAILABLE_EDF_COMMISSIONING = 5 # Deliberate bounded ground commissioning; never flight admission
uint8 unavailable_reason
bool schedule_enabled # Explicit empirical IAS/EDF table selected; not flight qualification
bool schedule_inputs_valid # Fresh selected IAS/density/voltage observations; not local flow-angle/wash qualification
uint8 schedule_status # 0 inactive, 1 ready, 2 invalid model, 3 stale, 4 source changed, 5 outside envelope, 6 transient, 7 infeasible gain transition
uint8 companion_transport_status # Receipt evidence only; no session, clock, command or flight admission
uint8 COMPANION_DISABLED = 0
uint8 COMPANION_WAIT_CONFIGURATION = 1
uint8 COMPANION_WAIT_FRAME = 2
uint8 COMPANION_AUTHENTICATION_REQUIRED = 3
uint8 COMPANION_SOURCE_MISMATCH = 4
uint8 COMPANION_TARGET_MISMATCH = 5
uint8 COMPANION_INVALID_FRAME = 6
uint8 COMPANION_AUTHENTICATED_FRAME = 7
uint8 COMPANION_STALE = 8
uint8 COMPANION_LIFETIME_CHANGED = 9 # Deliberate disarmed configuration apply required
uint8 COMPANION_SEQUENCE_GAP = 10 # Deliberate disarmed configuration apply required
uint32 companion_receiver_revision
uint64 companion_event_sequence
uint64 companion_frame_deadline # Original local receipt expiry, never producer command validity
bool power_required # Manual and EDF scopes need explicitly selected propulsion power
bool power_ready # Fresh healthy selected report inside configured voltage bounds; not flight admission
bool power_source_bound # Source identity is pinned to the current preparation/control session
uint8 power_status # slaaps::PowerObservationResult
uint8 POWER_INVALID_CONFIGURATION = 0
uint8 POWER_VALID = 1
uint8 POWER_UNAVAILABLE = 2
uint8 POWER_PUBLISHER_CHANGED = 3
uint8 POWER_IDENTITY_MISMATCH = 4
uint8 POWER_DISCONNECTED = 5
uint8 POWER_WARNING = 6
uint8 POWER_FAULT = 7
uint8 POWER_POWERING_OFF = 8
uint8 POWER_INVALID_TIMESTAMP = 9
uint8 POWER_INVALID_VOLTAGE = 10
uint8 POWER_VOLTAGE_OUT_OF_RANGE = 11
uint32 power_publisher_revision # Last successful observation; consult power_ready
uint64 power_deadline # Original battery report expiry, not renewed by polling
float32 power_voltage_v # Last successful observation; consult power_ready
