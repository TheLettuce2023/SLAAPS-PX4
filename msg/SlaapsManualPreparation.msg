# Internal non-actuating preparation proposal / readiness echo.
# Neither topic grants flight permission or acknowledges actuator handling.
# Separate reply topics prevent MAIN/AUX overwriting one another.
uint64 timestamp # Publication time; does not renew deadline
uint64 timestamp_sample # Original proposal creation time
uint64 deadline # Original preparation expiry
uint64 session
uint64 source
uint64 configuration
uint64 routing
uint64[2] driver # MAIN/AUX output device lifetime tokens, not sensor IDs
uint64 prepared # Zero on request; original successful bank preparation time on reply
uint32 command_revision # Expected command advertisement lifetime
uint32[2] acknowledgment_revisions # Expected handled-reply advertisement lifetimes
uint8 bank_mask
uint8 REQUEST_BANK = 255
uint8 bank # REQUEST_BANK on proposal; 0 MAIN or 1 AUX on readiness topic

# TOPICS slaaps_manual_prepare slaaps_manual_ready_main slaaps_manual_ready_aux
