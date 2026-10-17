# C1C capture-only source evidence; no actuator, controller or admission command.
uint64 timestamp
uint64 producer_lifetime_id
uint64 clock_epoch
uint64 sequence
uint64 transition_epoch
uint64 producer_time_us
uint8 kind
uint8 before_arming_state
uint8 before_nav_state
uint8 after_arming_state
uint8 after_nav_state
uint8 KIND_INITIAL = 0
uint8 KIND_TRANSITION = 1
uint8 KIND_SAMPLE = 2
uint8 ORB_QUEUE_LENGTH = 32
