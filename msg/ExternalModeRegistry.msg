# Current external-mode registry, owned by Commander. Queue-one state lets a
# newly started MAVLink link recover every registered name without replaying
# component registration. Slot zero is VehicleStatus.NAVIGATION_STATE_EXTERNAL1.
# This describes registration and user selectability, not can_run or can_arm.

uint64 timestamp # Time since system start (microseconds)

uint8 NUM_MODES = 8
uint8 NAME_LENGTH = 25

uint8 valid_mask # One bit per occupied external-mode slot
uint8 not_user_selectable_mask # Original registration property for each valid slot
char[200] names # Eight contiguous NAME_LENGTH-byte, null-terminated mode names
