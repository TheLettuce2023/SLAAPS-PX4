# SLAAPS mode/configuration supervisor. This is not physical output feedback.
uint64 timestamp
uint64 configuration_identity
uint32 can_run_mask
uint32 can_arm_mask
bool input_metadata_valid # Single-estimator metadata only; not numerical/manual validation or flight admission
uint32 input_source_epoch # Changes when an observed publisher lifetime changes
uint32 manual_command_revision # Supervisor-owned endpoint lifetime, zero if unavailable/replaced; not output admission
bool manual_worker_active
uint32 schedule_interval_us # Requested queue interval, not measured deadline compliance
uint8 manual_worker_status # slaaps::ManualWorkerStatus
uint32 snapshot_duration_us
int32 apply_counter
int8[3] mode_id # Manual, acceleration, servo bench; -1 until registered
uint8 configuration_fault
uint8 configuration_use # 0 bench, 1 stabilized manual
bool configuration_valid
bool routing_snapshot_valid # Observation only, not output permission or an atomic cross-bank transaction
uint8 inventory_fault
uint8 routing_fault
bool direct_actuator_modes_configured

uint8 UNAVAILABLE_CONFIGURATION = 0
uint8 UNAVAILABLE_OUTPUT_INTEGRATION = 1
uint8 UNAVAILABLE_ACCELERATION_PORT = 2
uint8 UNAVAILABLE_REGISTRATION = 3
uint8 unavailable_reason
