
uint64 timestamp		# time since system start (microseconds)

uint64 timestamp_sample		# the timestamp of the raw data (microseconds)

float32[3] xyz			# Calibrated filtered specific force at selected IMU, body FRD, m/s^2; no gravity/lever-arm correction

# Applied selected-IMU provenance. This remains filtered specific force at the
# IMU, not gravity-compensated inertial acceleration or a CG correction.
uint32 device_id
bool calibrated # Native static calibration present, not vehicle qualification
uint64 source_generation # Nonzero monotonic applied-source epoch; zero after exhaustion
float32 filter_cutoff_hz # Actual applied LPF cutoff; zero when disabled
float32 filter_sample_rate_hz # Actual applied LPF sample rate; zero when disabled
uint64 bias_timestamp # Original publication time of the applied bias; never renewed here
uint64 bias_timestamp_sample # Original sample time of the applied bias
bool bias_valid # Matching valid applied bias and currently available source; freshness checked by consumer
uint32 bias_publisher_revision # Original applied bias lifetime; zero when not tracked/unavailable
uint8 bias_instance # Original applied bias instance; 255 when no bias is applied
uint32 raw_publisher_revision # Original raw sample publisher lifetime; zero when not tracked
uint8 raw_instance # Original raw sensor_accel instance
