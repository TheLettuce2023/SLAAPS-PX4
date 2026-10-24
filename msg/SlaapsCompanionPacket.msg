# Receiver-owned immutable frame evidence for one explicitly selected MAVLink link.
# Authentication is not source/session authorization, clock trust or output permission.
uint64 timestamp # Local HRT after the complete frame or signing-state observation
uint64 sequence # Nonwrapping receiver-local event sequence; gaps are observable
uint32 signing_generation # Nonwrapping local signing configuration lifetime, not key material
uint32 message_id
uint8 mavlink_instance # Supported instances 0..3, matching the topic suffix
uint8 source_system
uint8 source_component
uint8 receiver_system # Local MAVLink identity at receipt; bind against runtime changes
uint8 receiver_component
uint8 payload_length
uint8 event
uint8 EVENT_LINK_STATE = 0
uint8 EVENT_FRAME = 1
bool mavlink2
bool signing_active
bool signature_verified # Configured verifier accepted this exact signed frame; never the signed bit alone
uint8[106] payload # Original MAVLink payload bytes; retain its actual trimmed length

uint8 ORB_QUEUE_LENGTH = 8
# TOPICS slaaps_companion_packet_0 slaaps_companion_packet_1 slaaps_companion_packet_2 slaaps_companion_packet_3
