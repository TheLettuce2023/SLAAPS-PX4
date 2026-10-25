# Local numerical/handled-command diagnostics. Never flight permission or measured actuator feedback.
uint64 timestamp # Supervisor publication only; never renews any evidence below
uint64 configuration_identity
uint64 output_session # Shared worker output session; distinct from companion request_session
uint64 request_source # Last evaluated canonical request, including a rejected calculation
uint64 request_session
uint64 request_sequence
uint64 request_observed_us # Original successful caller capture time, not a renewed producer timestamp
uint64 fault_observed_us # Original rejection observation; zero when no retained rejection
uint64 calculated_sequence # Last successfully calculated worker output, not proof it was handled
uint64 calculated_deadline # Original calculated output expiry
uint64 handled_sequence # Advances only after exact ACKs from every required bank
uint64 handled_us # Maximum original required-bank ACK time
uint64 handled_deadline # Original handled command expiry; historical after stop
bool active # Current shared worker mode is Acceleration; historical fields alone do not imply activity
uint8 state
uint8 INACTIVE = 0
uint8 READY = 1
uint8 CALCULATED = 2
uint8 HANDLED = 3
uint8 REJECTED = 4
uint8 control_fault # slaaps::AccelerationControlFault; retained across stop, cleared by deliberate new entry/session
uint8 FAULT_NONE = 0
uint8 FAULT_DISABLED = 1
uint8 FAULT_CONFIGURATION = 2
uint8 FAULT_INPUT = 3
uint8 FAULT_EXPIRED = 4
uint8 FAULT_SOURCE = 5
uint8 FAULT_SCHEDULE = 6
uint8 FAULT_GAIN_TRANSITION = 7
uint8 FAULT_ALLOCATION = 8
uint8 FAULT_ARITHMETIC = 9
uint8 FAULT_CLOCK = 10
uint8 allocation_stage
uint8 ALLOCATION_NONE = 0
uint8 ALLOCATION_FUTURE_TARGET = 1
uint8 ALLOCATION_CURRENT_OWNER_MODEL = 2 # Fixed current owner COMMAND/thrust-curve model, never measured thrust
uint8 allocation_result # slaaps::WrenchAllocationResult; a rejected solver output has no valid residual report
uint8 SOLVER_SUCCESS = 0
uint8 SOLVER_CONFIGURATION = 1
uint8 SOLVER_INPUT = 2
uint8 SOLVER_ARITHMETIC = 3
uint8 SOLVER_INFEASIBLE = 4
uint8 SOLVER_RESIDUAL = 5 # Regularized optimum exceeded limits; not proof no feasible physical solution exists
uint8 worker_status # Last calculation/rejection result; live polling status stays in SlaapsStatus. ACK failure is not a numerical fault.
# Historical masks for the corresponding calculated/handled output identities.
# Modeled rotational-demand clipping and canard hard/rate bounds, NOT measured actuator saturation.
uint8 calculated_moment_mask
uint8 handled_moment_mask
uint8 MOMENT_ROLL_POSITIVE = 1
uint8 MOMENT_PITCH_POSITIVE = 2
uint8 MOMENT_YAW_POSITIVE = 4
uint8 MOMENT_ROLL_NEGATIVE = 8
uint8 MOMENT_PITCH_NEGATIVE = 16
uint8 MOMENT_YAW_NEGATIVE = 32
uint8 calculated_canard_mask
uint8 handled_canard_mask
uint8 CANARD1_LOWER = 1
uint8 CANARD2_LOWER = 2
uint8 CANARD3_LOWER = 4
uint8 CANARD4_LOWER = 8
uint8 CANARD1_UPPER = 16
uint8 CANARD2_UPPER = 32
uint8 CANARD3_UPPER = 64
uint8 CANARD4_UPPER = 128
