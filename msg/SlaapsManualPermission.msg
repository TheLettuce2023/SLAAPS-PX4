# Supervisor permission snapshot. Does not select a mode or replace local arming checks.
# Consumers bind the publisher lifetime from the exact preparation request.
uint64 timestamp # Publication time; cannot renew sample validity
uint64 timestamp_sample # Original supervisor decision time
uint64 deadline # Original decision expiry
uint64 sequence # Advances only for a new decision, never a retransmission
uint64 session
uint64 source
uint64 configuration
uint8 MANUAL = 1
uint8 SERVO_BENCH = 3
uint8 EDF_BENCH = 4
uint8 mode
bool armed
bool flight_admitted # Requires implemented measured-condition/recovery admission, not a parameter switch
bool healthy
bool measured_condition_valid
bool kill
bool lockdown
bool termination
bool prearmed # Actual actuator_armed prearm state; does not permit EDF operation
bool commissioning_admitted # Explicit armed ground commissioning only; never flight admission
