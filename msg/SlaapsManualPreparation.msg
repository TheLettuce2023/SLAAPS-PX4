# Internal non-actuating preparation proposal / readiness echo.
# Neither topic grants flight permission or acknowledges actuator handling.
# Separate reply topics prevent MAIN/AUX overwriting one another.
uint64 timestamp # Publication time; does not renew deadline
uint64 timestamp_sample # Original proposal creation time
uint64 deadline # Original preparation expiry
uint64 session
uint64 source
uint64 configuration
uint8 MANUAL = 1
uint8 SERVO_BENCH = 3
uint8 EDF_BENCH = 4
uint8 mode # Immutable owner mode, echoed by every bank
bool reentry # D5: deliberate armed re-entry into a fresh Manual session; set only by the supervisor's D5 path
uint64 routing
uint64[2] driver # MAIN/AUX output device lifetime tokens, not sensor IDs
uint64 prepared # Zero on request; original successful bank preparation time on reply
uint32 command_revision # Expected command advertisement lifetime
uint32 permission_revision # Expected independent permission advertisement lifetime
uint32[2] acknowledgment_revisions # Expected handled-reply advertisement lifetimes
uint8 PREPARE = 0
uint8 ACTIVATE = 1
uint8 CANCEL = 2
uint8 operation # Only transition topic accepts ACTIVATE/CANCEL
uint8 bank_mask
uint8 REQUEST_BANK = 255
uint8 bank # REQUEST_BANK on proposal; 0 MAIN or 1 AUX on readiness topic

# TOPICS slaaps_manual_prepare slaaps_manual_ready_main slaaps_manual_ready_aux slaaps_manual_transition
