# Internal combined manual command. Messages cannot grant output permission.
# Driver lifetime tokens identify the two output devices; this is not sensor data.
uint64 timestamp # Publication time; never used to renew the original deadline
uint64 timestamp_sample # Successful control calculation time
uint64 deadline
uint64 sequence
uint64 session
uint64 source
uint64 configuration
uint64 routing
uint64[2] driver # MAIN, AUX lifetime tokens from the admitted routing
uint64 edf_sequence # Original selected-input request sequence
uint64 edf_request_timestamp
uint64 edf_deadline
float32[4] canards # Normalized logical canard commands
float32 edf # Normalized forward-only propulsion command
uint8 bank_mask
