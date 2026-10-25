# Supervisor-owned transport intent, never session or actuator authority.
uint64 timestamp
uint64 deadline # Exclusive local HRT expiry; renewal does not renew a pending exchange
uint64 configuration_identity
uint64 controller_instance_id
uint64 binding_id # Changes on deliberate prepare/revoke; nonzero while enabled
uint32 maximum_round_trip_us
uint8 mavlink_instance
uint8 source_system
uint8 source_component
bool enabled
