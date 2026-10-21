# Explicit out-of-band request. Payload cannot authenticate its publisher.
uint64 timestamp # Publication time, microseconds since boot
uint64 session # Original request creation time and monotonic session token
uint64 deadline # Exclusive grant-acceptance deadline; never refreshed by retries
uint64 configuration # Immutable configuration identity shared by both endpoints
uint32 command_revision # Expected command publisher lifetime
uint32 acknowledgement_revision # Expected output acknowledgment publisher lifetime
