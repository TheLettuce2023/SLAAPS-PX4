# Internal driver-handled acknowledgment, not electrical feedback or permission.
# Separate bank topics prevent one bank from overwriting the other's reply.
uint64 timestamp # Publication time
uint64 timestamp_sample # Original control calculation time
uint64 accepted # Time trusted driver adapter confirmed handling
uint64 session
uint64 source
uint64 configuration
uint64 routing
uint64 driver # Output device lifetime; transport must bind actual publisher too
uint64 sequence
uint8 bank # 0 MAIN, 1 AUX; must match the selected topic

# TOPICS slaaps_manual_ack_main slaaps_manual_ack_aux
