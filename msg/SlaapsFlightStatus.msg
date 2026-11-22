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
# Latched record of the last "control lost" D3 event: which check fired and the timing around it. Observation only.
uint32 fault_count # Events since boot; zero means none yet
uint64 fault_time # HRT time of the last event
uint8 fault_site # 0 none, 1 core worker fault after a handled frame, 2 permission or health loss, 3 mode, power or condition loss
uint8 fault_reason # Site 3 only: 1 mode left, 2 power lost, 3 condition lost
uint8 fault_worker_status # ManualWorkerStatus of the failing cycle (6 control, 7 throttle, 8 propulsion, 9 output, 10 condition rejected)
uint16 fault_core_line # Source line of the last core (ManualWorker.hpp) reject
uint16 fault_transport_line # Source line of the last transport (ManualWorkerTransport.hpp) reject
uint8 fault_schedule_status # ControlScheduleStatus at the event (0 inactive, 1 ready, 2 invalid, 3 stale, 4 source changed, 5 outside envelope, 6 transient, 7 transition)
int32 fault_output_margin_us # Previous output deadline minus the failing cycle start; negative: the deadline had already passed
int32 fault_permission_margin_us # Permission deadline minus the event time; negative: the lease had lapsed
uint32 fault_state_age_us # Older of the selected attitude and rate samples at the event
uint32 fault_pilot_age_us # Age of the newest pilot sample at the event
uint32 fault_since_ok_us # Time since the previous cycle that completed without a fault
bool net_valid # D6 during recovery: fresh GCS and vehicle positions with SLA_NET_EN; operator reference only
float32 net_distance_m # Vehicle to recovery net
float32 net_bearing_deg # Vehicle to recovery net, true north, 0..360
