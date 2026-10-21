# Consumer decision, not electrical feedback. Match the entire original request.
uint64 timestamp # Decision time, microseconds since boot
uint64 session
uint64 deadline
uint64 configuration
uint32 command_revision
uint32 acknowledgement_revision
uint32 request_revision # Metadata from the atomic request copy
bool accepted
