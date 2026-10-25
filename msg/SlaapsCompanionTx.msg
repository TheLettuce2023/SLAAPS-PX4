# Historical adapter status for one exact transport intent; no output permission.
uint64 timestamp
uint64 deadline # Exclusive original local HRT expiry
uint64 configuration_identity
uint64 controller_instance_id
uint64 binding_id
uint32 signing_generation # Zero only for an explicitly closed bootstrap status
uint8 mavlink_instance
uint8 target_system
uint8 target_component
uint8[106] payload # Exact v1 status payload; framing/signing remain owned by MAVLink

uint8 ORB_QUEUE_LENGTH = 8
# TOPICS slaaps_companion_tx slaaps_companion_barrier
