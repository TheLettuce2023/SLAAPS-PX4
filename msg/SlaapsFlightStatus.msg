# SLAAPS flight admission staging and decided airborne responses (docs/SLAAPS_RECOVERY_POLICY.md).
# Observation only; never flight permission or physical feedback.
uint64 timestamp
uint8 flight_stage # SLA_FLT_STAGE of the selected stabilized profile: 0 off, 1 captive, 2 launcher
bool flight_configuration_valid # Profile half of flight admission; runtime health is separate
bool launch_gate_closed # Launcher stage: no canard authority until the airspeed gate opens
bool launch_airspeed_valid # Fresh selected launcher airspeed
float32 launch_airspeed_m_s # Selected raw indicated airspeed; NaN when not valid
uint8 recovery_state
uint8 RECOVERY_NONE = 0
uint8 RECOVERY_HOLD = 1 # D1: attitude hold, EDF hold then ramp to off
uint8 RECOVERY_RAMP_OFF = 2 # D3: canards frozen, EDF ramping off
uint64 recovery_start # D1 hold start; zero when not holding
uint8 failed_banks # D3: banks dropped after they stopped confirming (bit 0 MAIN, bit 1 AUX)
bool net_valid # D6 during recovery: fresh GCS and vehicle positions with SLA_NET_EN; operator reference only
float32 net_distance_m # Vehicle to recovery net
float32 net_bearing_deg # Vehicle to recovery net, true north, 0..360
