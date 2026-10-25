# Local companion protocol evidence; never acceleration/controller/output permission.
uint64 timestamp
uint64 configuration_identity
uint64 controller_instance_id
uint64 binding_id
uint64 state_generation
uint64 transition_epoch
uint64 commander_transition
uint64 map_epoch
uint64 map_source
uint64 map_session
int64 map_offset_us
uint64 map_uncertainty_us
uint64 map_deadline
uint64 proposal_session
uint64 proposal_deadline
uint64 connection_deadline
uint64 barrier_sequence
uint64 barrier_time
uint64 status_failures
uint32 receiver_revision
uint32 signing_generation
uint32 commander_revision
uint32 estimator_epoch
uint16 decision_code
uint16 first_latched_code
uint8 phase # 0 disabled, 1 unconfigured, 2 waiting state, 3 waiting proposal, 4 mapping, 5 waiting grant, 6 session, 7 fault
uint8 action # Last consumed SLA_CMP_ACT
uint8 action_result # 0 none, 1 completed, 2 rejected
uint8 session_gate # research_acceleration::SessionGate
bool transport_connected
bool required_state_valid
bool map_valid
bool prepared
bool session_established
bool acknowledgement_current
bool continuity_fault
# Local measured-feedback diagnostics; not flight or actuator permission.
bool feedback_required
bool feedback_valid
uint8 feedback_result # slaaps::AccelerationFeedbackResult; zero disabled, one ready
uint64 feedback_deadline
float32[3] acceleration_cg_body
