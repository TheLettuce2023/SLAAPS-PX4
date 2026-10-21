# Diagnostic candidates only. No actuator consumer or physical authority.
uint64 timestamp
uint64 timestamp_sample
uint64 session
uint32 source_epoch        # Session-local mapping of the complete publisher revision tuple
uint32[7] publisher_revision # attitude, rate, estimator, flags, manual, selection, permission
uint32 gyro_device_id
uint32 accel_device_id
uint32 cycles
uint8 state                 # research_stabilized_manual::State
uint8 fault                 # research_stabilized_manual::Status
uint8 attitude_reset_counter
bool inputs_valid          # Diagnostic checks, not production health qualification
bool candidate_valid       # Vectors valid only for a successful calculation in this record
bool outputs_enabled       # Always false in the observer
float32[4] hold_reference
float32[3] rate_setpoint
float32[3] requested_moment
float32[3] allocation_residual
float32[3] integral
float32[4] canard_candidate # Normalized; not PWM, not an actuator command
