# Atomic Commander-owned actual state for companion admission, never user intention.
# No field grants actuator or flight permission.
uint64 timestamp # Publication time since boot, us
uint64 transition # Initial 1; nonwrapping tuple-transition counter, zero if unavailable/exhausted
uint8 arming_state # Actual VehicleStatus arming state
uint8 nav_state # Actual VehicleStatus navigation state
bool armed # Commander-owned actuator_armed values, captured with the state above
bool prearmed
bool lockdown
bool kill
bool termination
bool in_esc_calibration_mode
bool attitude_invalid # Actual Commander health/failsafe result
bool angular_velocity_invalid
