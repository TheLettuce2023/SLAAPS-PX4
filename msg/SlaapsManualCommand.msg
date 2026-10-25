# Internal combined command for a mode-bound session. Messages cannot grant permission.
# Driver lifetime tokens identify the two output devices; this is not sensor data.
uint64 timestamp # Publication time; never used to renew the original deadline
uint64 timestamp_sample # Successful control calculation time
uint64 deadline
uint64 sequence
uint64 session
uint64 source
uint64 configuration
uint64 transition # Exact same-session Manual/Acceleration transition token
uint64 previous_handled_sequence # First transfer frame only; otherwise zero
uint64 previous_deadline # Exact outgoing handled frame expiry; first transfer frame only
uint64 routing
uint64[2] driver # MAIN, AUX lifetime tokens from the admitted routing
uint64 edf_sequence # Continuous worker EDF demand sequence; not companion sequence
uint64 edf_request_timestamp
uint64 edf_deadline
float32[4] canards # Normalized logical canard commands
float32 edf # Normalized forward-only propulsion; inhibited ServoBench requires zero
uint8 bank_mask
uint8 MANUAL = 1
uint8 ACCELERATION = 2
uint8 SERVO_BENCH = 3
uint8 EDF_BENCH = 4
uint8 mode # Prepared mode or exact same-session flight transfer
uint8 EDF_INHIBITED = 0
uint8 EDF_COMMAND = 1
uint8 edf_disposition # Servo bench requires inhibited and zero EDF
