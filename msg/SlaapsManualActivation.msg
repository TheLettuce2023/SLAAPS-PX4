# Internal initial-command-wait confirmation, not a flight permission or handled output.
uint64 timestamp # Publication time; cannot renew activation or deadline
uint64 timestamp_sample # Original bank activation time
uint64 first_command_deadline # Original bank first-command expiry
uint64 session
uint64 source
uint64 configuration
uint64 routing
uint64 driver # Physical output-device lifetime token, not sensor device_id
uint32 command_revision # Command advertisement lifetime
uint32 acknowledgment_revision # Handled-output reply lifetime, not this topic's lifetime
uint8 bank # 0 MAIN or 1 AUX, must match the receiving topic

# TOPICS slaaps_manual_active_main slaaps_manual_active_aux
