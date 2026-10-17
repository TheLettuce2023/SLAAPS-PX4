# Acceleration Wire Contract

## Status And Scope

C0c update (2026-09-19): isolated generated C/Python framing conformance now
passes under a parent-owned offline dialect with local test IDs
200000/200001/200002 and CRC extras 130/188/131 respectively. This does not
allocate global/deployment IDs or attach a live receiver. See
C0C_GENERATED_WIRE_CONFORMANCE.md and the current phase gate. Statements below
about pending generated framing are historical; live allocation, signing,
clock/authority deployment and the operational timing contract remain open.

Implementation update (2026-09-19): the frozen payload codec and isolated C0b
map/state/session/ACK-request/status libraries are accepted as recorded in
PHASE_GATE_STATUS.md. This document remains a wire design: generated MAVLink
framing/interoperability and live ingress are not implemented or approved by
that acceptance. Historical future-tense adapter references below do not
supersede the concrete C0B2/C0B3 designs or the current gate.

This is the proposed version-1 MAVLink 2 wire design for the inactive research-acceleration adapter.
It is a design artefact only.
It does not allocate a MAVLink message ID, change a dialect, add a MAVLink receiver or publisher, select a serial rate or a command rate, or authorise any control or actuator output.

The vehicle remains four independently actuated canard servos and one fixed-axis rear EDF, with no main wing and no thrust vectoring.
The intended initial paths are a host guidance script to PX4 SITL and a Raspberry Pi 4B translator to the Pixhawk 6X MAVLink TELEM link.
The planned GNSS and DroneCAN pitot are estimator/sensor inputs; neither Gazebo truth nor scorer output is an operational input.
The controller, feasibility envelope, allocator, operational limits, loss response, and physical-output path remain unreleased.

This contract is compatible with the completed S1 `research_acceleration::RequestGuard` and `FrameMath` API.
It supplies all fields of S1's `CanonicalRequest`; the future adapter performs the final admission and must not weaken S1's checks.
S1's fixed-width `DecisionCode` values are the source of validator decisions, not a claim that any MAVLink status message already exists.

## Chosen Transport And Why A Custom Message Is Necessary

Use MAVLink 2 framing on one explicitly selected MAVLink instance for the research link.
The host SITL producer and Pi translator emit the same custom messages and canonical test vectors.
MAVLink 1 frames are not valid for this contract.

The local pinned PX4 source accepts `SET_POSITION_TARGET_LOCAL_NED`, but that standard message is not this interface:

- Its acceleration fields are local NED; its handler accepts `MAV_FRAME_LOCAL_NED` and the deprecated `MAV_FRAME_BODY_NED`, not `MAV_FRAME_BODY_FRD`.
  The body case is rotated at receipt, whereas this contract holds the original FRD components against the *current* body attitude throughout validity.
- It has no source identifier, trusted-session identifier, sequence, producer monotonic creation/expiry times, trusted clock-map epoch, transition epoch, controller-instance binding, or per-request acceptance result.
  PX4 replaces producer timing with receipt timestamps.
- A fresh Offboard control-mode indication is liveness only.
  It does not prove payload age, ordering, validity, or research-controller support.
  The configured fixed-wing consumer does not implement acceleration-only three-axis tracking.

Do not overload `COMMAND_LONG`, `COMMAND_ACK`, `STATUSTEXT`, or a mission item.
Those standard messages do not carry this request's complete immutable object or a request-correlated validator result.
Use standard `HEARTBEAT` only as transport/liveness evidence; it never refreshes an acceleration request or establishes a session.
MAVLink 2 signing is a framing/link authentication mechanism, not a session message and not a substitute for source/session authorisation.

The proposed custom dialect has three messages:

| Symbolic message name | Direction | Purpose |
| --- | --- | --- |
| `RESEARCH_ACCELERATION_REQUEST` | producer to PX4 | One immutable FRD inertial-CG acceleration request. |
| `RESEARCH_ACCELERATION_SESSION` | producer to PX4 | Establish/revoke a trusted session while disarmed, or acknowledge an already-authorised current epoch without changing the session. |
| `RESEARCH_ACCELERATION_STATUS` | PX4 to producer/operator | Correlated decision, session/transition state, and diagnostic sequence. |

Their numerical MAVLink message IDs, CRC extras, dialect XML location, include order, and generated bindings are deliberately **pending dialect ID allocation**.
No number in this document reserves an existing MAVLink ID.
The message names and payload layouts are the design input to that allocation; a generated dialect is authoritative once allocated.

## Canonical Meaning

The only control objective is a three-component inertial acceleration at the centre of gravity:

`a_CG_NED = dv_CG_NED / dt` in `m/s^2`.

NED is the world frame: North, East, Down.
FRD is the current body frame: Forward, Right, Down.
For a valid current attitude `R_NED_from_FRD(t)`, each eligible request is evaluated as:

`a_req_NED(t) = R_NED_from_FRD(t) * a_req_FRD_active`.

Thus the request is body-held, not world-held: its FRD values are never rotated once at packet arrival.
The request's reference point is the CG.
CG reference and current-body-held interpretation were explicitly confirmed by the user during C0b.2 review (2026-09-17). Neither confirmation is a measured geometry datum or deployment approval.
It is neither a force nor accelerometer specific force; no gravity term is inserted into the request.
Where needed by a later controller, specific force is `f_CG_NED = a_CG_NED - g_NED` using a separately supplied, finite gravity vector.
An offset IMU's specific force is not silently treated as CG acceleration.

Version 1 permits exactly one wire frame, quantity, and reference point:

| Field | Valid v1 value | Meaning |
| --- | --- | --- |
| `frame` | `1` (`FRD`) | Body Forward, Right, Down. |
| `quantity` | `1` (`INERTIAL_ACCELERATION`) | `dv_NED/dt`, not force or specific force. |
| `reference_point` | `1` (`CG`) | Vehicle centre of gravity. |

Zero is reserved/invalid for each *wire* enum; unknown values are rejected.
The adapter performs the explicit checked mapping `wire 1 -> S1 Frame::Frd`, `wire 1 -> S1 Quantity::InertialAcceleration`, and `wire 1 -> S1 ReferencePoint::Cg` before construction of a `CanonicalRequest`.
It must not `static_cast` a wire value to an S1 enum: S1's sole enum alternatives have underlying value zero.
Future meanings require a new compatible contract version and conformance vectors, not reinterpretation of a v1 packet.
All three acceleration components must be finite IEEE-754 `float` values.
Numeric acceleration, validity, age, uncertainty, skew, and slew limits remain approved configuration requirements, not wire defaults or fixture values.

## Version And Payload Rules

`protocol_version` is a `uint32_t` carrying `(major << 16) | minor`.
For version 1, the future adapter requires its configured exact value, matching S1 `ContractConfig::protocol_version`.
It must reject an unknown major or unsupported minor before an object reaches the request guard.
There is no implicit downgrade, default value, extension-field reinterpretation, or NaN sentinel.

All fields below are MAVLink payload bytes, little-endian for multibyte scalars, with no C/C++ structure padding.
The stated offsets follow MAVLink's generated wire ordering: fields are ordered by decreasing primitive width, not by XML declaration order.
The XML declaration and generated C/Python bindings must be checked against these layouts before any integration test is accepted.
Every `reserved` byte/flag bit is transmitted as zero by v1 and rejected when nonzero by a v1 receiver.

MAVLink 2 may trim trailing zero payload bytes.
The decoder therefore zero-fills absent trailing bytes according to generated MAVLink 2 decode rules before field validation; it must not reject a valid generated frame merely because its transmitted length is shorter than the nominal layout.
It accepts only these v1 logical ranges: request 85--90 bytes (the absent `flags`/`reserved` tail is zero), session 71--76 bytes (the absent `flags`/`reserved` tail is zero), and status 106 bytes.
A frame shorter than its message's minimum logical length, longer than its nominal length, or with a nonzero supplied reserved byte is malformed.

A valid status cannot be shortened: its final byte at offset 105 is `clock_mapping_state`, whose valid values 1--3 are all nonzero. A 105-byte status would zero-fill an invalid state, so both trim settings necessarily produce 106 bytes.

### `RESEARCH_ACCELERATION_REQUEST`

Nominal v1 payload length: 90 bytes.

| Offset | Type | Field | Required v1 meaning |
| ---: | --- | --- | --- |
| 0 | `uint64_t` | `source_id` | Stable configured logical producer identity; nonzero. |
| 8 | `uint64_t` | `session_id` | Nonzero trusted-session identifier. |
| 16 | `uint64_t` | `sequence` | Strictly increasing producer sequence within the trusted session. |
| 24 | `uint64_t` | `producer_monotonic_us` | Producer-clock creation instant. |
| 32 | `uint64_t` | `producer_expiry_monotonic_us` | Producer-clock exclusive validity end. |
| 40 | `uint64_t` | `controller_instance_id` | PX4 boot-instance nonce observed in status; binds requests to one validator instance. |
| 48 | `uint64_t` | `transition_epoch` | PX4 transition epoch observed in status. |
| 56 | `uint64_t` | `clock_mapping_epoch` | Active producer-to-validator map identity observed in status. |
| 64 | `float` | `acceleration_x_frd_m_s2` | Forward inertial acceleration at CG. |
| 68 | `float` | `acceleration_y_frd_m_s2` | Right inertial acceleration at CG. |
| 72 | `float` | `acceleration_z_frd_m_s2` | Down inertial acceleration at CG. |
| 76 | `uint32_t` | `protocol_version` | Exact contract version. |
| 80 | `uint8_t` | `target_system` | Exact selected PX4 system ID; broadcast is invalid. |
| 81 | `uint8_t` | `target_component` | Exact selected PX4 component ID; broadcast is invalid. |
| 82 | `uint8_t` | `frame` | `1` only (`FRD`). |
| 83 | `uint8_t` | `quantity` | `1` only (`INERTIAL_ACCELERATION`). |
| 84 | `uint8_t` | `reference_point` | `1` only (`CG`). |
| 85 | `uint8_t` | `flags` | Must be zero in v1. |
| 86 | `uint8_t[4]` | `reserved` | All zero in v1. |

The request carries raw producer-clock endpoints only.
PX4 computes both validator-domain endpoints and the clock uncertainty from its trusted map record before it constructs S1's `CanonicalRequest`; the common frozen offset makes S1's raw and mapped durations exactly equal under checked arithmetic.
The expiry instant is exclusive: an uncertainty-conservative earliest expiry equal to validator receive time is expired.
The receiver rejects underflow/overflow, non-finite values, invalid enum/target/version/reserved fields, and any message that cannot become an S1 `CanonicalRequest` without information loss.

### `RESEARCH_ACCELERATION_SESSION`

Nominal v1 payload length: 76 bytes.
This is an administrative request, never a control request.

| Offset | Type | Field | Required v1 meaning |
| ---: | --- | --- | --- |
| 0 | `uint64_t` | `controller_instance_id` | Expected PX4 boot-instance nonce. |
| 8 | `uint64_t` | `transition_epoch` | Expected PX4 transition epoch. |
| 16 | `uint64_t` | `clock_mapping_epoch` | Expected active clock-map epoch. |
| 24 | `uint64_t` | `source_id` | Candidate configured logical producer identity. |
| 32 | `uint64_t` | `session_id` | New nonzero session, or current session for revoke. |
| 40 | `uint64_t` | `operation_sequence` | Strictly increasing administrative correlation number for this controller instance. |
| 48 | `uint64_t` | `producer_acknowledgement_monotonic_us` | Producer-clock instant after receiving the status being acknowledged. |
| 56 | `uint64_t` | `acknowledged_status_sequence` | `status_sequence` actually observed by the producer. |
| 64 | `uint32_t` | `protocol_version` | Exact contract version. |
| 68 | `uint8_t` | `target_system` | Exact selected PX4 system ID. |
| 69 | `uint8_t` | `target_component` | Exact selected PX4 component ID. |
| 70 | `uint8_t` | `operation` | `1` establish; `2` revoke; `3` acknowledge current epoch. |
| 71 | `uint8_t` | `flags` | Must be zero in v1. |
| 72 | `uint8_t[4]` | `reserved` | All zero in v1. |

An establish request is accepted only when all of the following are true: valid MAVLink 2 authentication, configured source/header/channel binding, an independently supervisor-authorised establishment action, current `controller_instance_id`, current transition and clock-map epochs, and PX4 disarmed state.
It calls the S1 trusted-session establishment path; receipt of a data request can never establish, reset, or replace a session.
A session change while armed is rejected.
Revoke is accepted only while disarmed with current source/session/epoch binding and independent supervisor authorisation. It clears eligibility, sets an adapter-owned revoked-session gate, and preserves S1 replay floors. An epoch acknowledgement cannot reopen that gate: a different supervisor-authorised trusted session must first be established. `RequestGuard::revoke()` alone does not implement this administrative gate.
`acknowledge current epoch` is not a session establishment, cannot change a session, cannot clear a latch, and never creates an eligible request.
It is admitted only for the current authenticated trusted source/session and current epochs.
It records the producer acknowledgement barrier described below; it may be processed while armed because it is neither an authorisation change nor a control request.
The exact supervisor-authorisation mechanism is an unresolved human operational decision; it must be independent of unauthenticated header identity and cannot be replaced by a companion packet alone.

### `RESEARCH_ACCELERATION_STATUS`

Nominal v1 payload length: 106 bytes.
While the configured adapter can allocate a representable report, every received
session operation and every request that reaches custom-message decoding
produces one attributable status decision, including rejections.
Periodic snapshots and transition/fault notices may also be sent, but no status rate is selected here.

Offline status review clarification (2026-09-19): an unconfigured adapter and
calls after a retained terminal produce no new report. Counter exhaustion emits
its one reserved final report, never a repeated or wrapped status sequence.
A first structural fault can have no established transition epoch; v1 cannot
serialize that zero epoch. Such evidence remains local and is not repaired with
an invented identity or a later snapshot. These explicit fail-closed exceptions
supersede the earlier unconditional per-packet wording. A future live wrapper
must expose adapter unavailability through independent health/operator evidence;
silence is never acceptance and must not refresh a companion demand.

| Offset | Type | Field | Required v1 meaning |
| ---: | --- | --- | --- |
| 0 | `uint64_t` | `controller_instance_id` | Current PX4 boot-instance nonce. |
| 8 | `uint64_t` | `transition_epoch` | Current PX4 transition epoch. |
| 16 | `uint64_t` | `clock_mapping_epoch` | Current map epoch. |
| 24 | `uint64_t` | `source_id` | Source from correlated packet, or zero when none exists. |
| 32 | `uint64_t` | `session_id` | Session from correlated packet/current session, or zero when none exists. |
| 40 | `uint64_t` | `correlation_sequence` | Request `sequence`, session `operation_sequence`, or zero for unsolicited status. |
| 48 | `uint64_t` | `last_accepted_sequence` | Last accepted data sequence when `HAS_LAST_ACCEPTED_SEQUENCE` is set. |
| 56 | `uint64_t` | `validator_time_us` | PX4 validator monotonic time when decision/status was made. |
| 64 | `uint64_t` | `status_sequence` | Monotonic PX4 status stream sequence for gap diagnostics. |
| 72 | `uint64_t` | `rejection_count` | Saturating S1 diagnostic count at status generation. |
| 80 | `uint64_t` | `conservative_age_us` | Conservative request age only when its flag is set; otherwise zero. |
| 88 | `uint32_t` | `protocol_version` | Status contract version. |
| 92 | `uint16_t` | `decision_code` | Stable mapped S1/adapter decision code; `0` is only an unsolicited healthy snapshot. |
| 94 | `uint16_t` | `first_latched_code` | Stable mapped first latch cause only when latched; otherwise zero. |
| 96 | `uint16_t` | `status_flags` | Bitset defined below. |
| 98 | `uint8_t` | `target_system` | Receiving producer system ID, or zero only for an operator broadcast snapshot. |
| 99 | `uint8_t` | `target_component` | Receiving producer component ID, or zero only for an operator broadcast snapshot. |
| 100 | `uint8_t` | `status_class` | `1` request decision, `2` session decision, `3` transition, `4` fault, `5` snapshot. |
| 101 | `uint8_t` | `arm_state` | `1` disarmed, `2` armed, `3` unknown. |
| 102 | `uint8_t` | `research_mode_state` | `1` not selected, `2` selected, `3` unknown. |
| 103 | `uint8_t` | `session_state` | `1` none, `2` established, `3` establishment required, `4` revoked, `5` latched. |
| 104 | `uint8_t` | `transport_state` | `1` connected, `2` disconnected, `3` unknown. |
| 105 | `uint8_t` | `clock_mapping_state` | `1` valid, `2` invalid, `3` faulted/epoch changed. |

`status_flags` uses bit 0 `ELIGIBLE_FOR_CONTROL`, bit 1 `SYNTACTICALLY_VALID`, bit 2 `LATCHED`, bit 3 `CONSERVATIVE_AGE_VALID`, bit 4 `HAS_LAST_ACCEPTED_SEQUENCE`, and bit 5 `AUTHENTICATED_LINK`.
Bits 6--15 are zero in v1.
When `CONSERVATIVE_AGE_VALID` is clear, `conservative_age_us` is zero; when `LATCHED` is clear, `first_latched_code` is zero.

The v1 `decision_code` mapping is fixed, not an enum cast: `0 NONE`; `1 ACCEPTED`; `2 VALIDATED_DISARMED`; `3 NOT_CONFIGURED`; `4 WRONG_PROTOCOL_VERSION`; `5 WRONG_SOURCE`; `6 NO_SESSION`; `7 WRONG_SESSION`; `8 UNAUTHORISED_SESSION_ESTABLISHMENT`; `9 SESSION_CHANGE_WHILE_ARMED`; `10 INVALID_TRUSTED_SESSION`; `11 SESSION_ESTABLISHMENT_REQUIRED`; `12 DUPLICATE_SEQUENCE`; `13 OUT_OF_ORDER_SEQUENCE`; `14 PRODUCER_TIME_NOT_INCREASING`; `15 CLOCK_MAPPING_INVALID`; `16 CLOCK_UNCERTAINTY_EXCEEDED`; `17 PRODUCER_TIME_IN_FUTURE`; `18 PRODUCER_TIME_REGRESSED`; `19 VALIDATOR_TIME_REGRESSED`; `20 REQUEST_TOO_OLD`; `21 VALIDITY_OUT_OF_RANGE`; `22 REQUEST_EXPIRED`; `23 TIME_ARITHMETIC_INVALID`; `24 REQUIRED_STATE_INVALID`; `25 WRONG_FRAME`; `26 WRONG_QUANTITY`; `27 WRONG_REFERENCE_POINT`; `28 NON_FINITE`; `29 AXIS_OUT_OF_BOUNDS`; `30 NORM_OUT_OF_BOUNDS`; `31 DISARMED_REJECTED`; `32 MODE_NOT_SELECTED`; `33 ARM_STATE_UNKNOWN`; `34 MODE_STATE_UNKNOWN`; `35 DISCONNECTED`; `36 NO_ELIGIBLE_REQUEST`; `37 LATCHED`.
Codes `64` through `83` are fixed by the C0a table below; all other values above 37 are reserved. These adapter diagnostic values are not MAVLink message IDs.
No code outside these sets is a v1 success.
No status says that a requested acceleration was physically feasible, tracked, allocated, or actuated.

## Authentication, Source, And Session Boundaries

These mechanisms are intentionally separate:

1. **Authentication:** when the research link is enabled, a dedicated MAVLink 2 link accepts only packets whose MAVLink signing policy has validated them with the provisioned key and permitted signing-link policy.
   Unsigned, bad-signature, and parser-replayed frames do not reach the custom decoder.
   Key provisioning, key rotation/revocation, and the SITL test-key handling procedure are deployment decisions that must be settled before live ingress is enabled.
2. **Source authorisation:** the decoder binds the authenticated MAVLink sender `(system ID, component ID)`, selected MAVLink channel, and nonzero payload `source_id` to one configured owner.
   Header identity and `source_id` alone are not authentication.
   A validly signed but unconfigured source is rejected and cannot alter cache, replay floor, session, or latch state.
3. **Session authorisation:** only an independently supervisor-authorised, disarmed `RESEARCH_ACCELERATION_SESSION` establishment can select a new S1 trusted session.
   `session_id` is a correlation/replay namespace, not a cryptographic credential.
4. **Request admission:** only the established source/session can advance its request sequence and producer time floors.
   Malformed, foreign, duplicate, out-of-order, or stale traffic cannot refresh the last valid demand.

The software design selects typed identity binding, custom session/status messages, and offline codec fixtures.
Deployment values for sender/target IDs, TELEM instance/channel, signing-key lifecycle, supervisor-authorisation mechanism, and any operator broadcast remain approval records for the human team; they are not safely inferred from this workstation, the host script, or a Pi connection.

## Clock Mapping And Whole-Lifetime Uncertainty

The producer clock and PX4 validator clock are distinct monotonic domains.
The PX4 adapter, not the producer, owns the currently trusted map record `(e, O_e, U_e)` together with an explicit producer-domain validity interval and clock/source/session binding. The offset is signed; applying it to unsigned endpoints requires checked addition/subtraction, never unsigned wraparound. Both request endpoints and each acknowledgement time used in an ordering proof must lie in the map's certified interval. Outside it, the adapter rejects the packet; independently expired/invalid map service state revokes eligibility.
The producer receives only the current map epoch `e` in status and sends raw producer times.
For each request the adapter uses its already-frozen single map `M_e(t) = t + O_e` for both producer endpoints:

`mapped_create = M_e(producer_monotonic_us)`

`mapped_expiry = M_e(producer_expiry_monotonic_us)`.

`U_e` is a symmetric, nonzero validator-owned bound such that, for *every* producer-clock instant from creation through the exclusive expiry, the true validator-domain instant lies within `M_e(t) +/- U_e`.
It is not merely a one-time offset estimate and it must include bounded drift, synchronisation error, timestamp quantisation, and declared translator mapping error across the entire request lifetime.
The common frozen offset makes mapped duration exactly equal to raw duration, as S1 requires; creation and expiry must never be independently skewed to hide clock error.
The producer supplies no offset, mapped time, or uncertainty that can influence timing admission.
The adapter computes both mapped endpoints and constructs S1's `CanonicalRequest` from its immutable `(e, O_e, U_e)` record, so a packet cannot relax age, future, expiry, or epoch checks.

The future adapter rejects a request unless the map epoch is current and valid, the two raw/mapped durations are exactly equal under checked arithmetic, and S1's age/future/expiry tests pass using the conservative endpoints:

- earliest creation `mapped_create - U`;
- latest creation `mapped_create + U`;
- earliest expiry `mapped_expiry - U`.

Consequently, a request is too old using the worst-case older creation time and is expired when earliest expiry is at or before validator receive time.
Only a trusted map-service loss, discontinuity, epoch change, or configured-bound violation invalidates the current map and revokes eligibility.
A malformed/foreign request's raw times, epoch, or metadata merely reject that packet; they cannot mutate the trusted map, epoch, cache, replay floors, or latch state.
Mapping arithmetic failure, producer-time regression, or validator-clock regression follows S1's rejection/latch behaviour.
Later synchronisation evidence may invalidate a map/epoch, but it cannot remap an accepted cached request, extend its original expiry, or revive its eligibility.
Validator-clock and producer-time regressions retain S1's first-cause latch; transport recovery, a fresh heartbeat, or a later well-formed request cannot clear it.
Only supervisor-authorised, disarmed latch clearing followed by a new trusted-session establishment may create a later request namespace.

The map acquisition protocol and evidence source are still pending: it must work both for host SITL and Pi-to-TELEM, expose a monotonic epoch, and prove the advertised bound for the whole validity interval.
Until then the adapter's clock mapping state is invalid and no request is eligible.

## Transition, Restart, And Re-entry Rules

PX4 generates a nonzero `controller_instance_id` once per boot and exposes it in every status.
It increments `transition_epoch` whenever an event invalidates a previously eligible request: arm/disarm boundary, research-mode select/deselect boundary, required-state-valid loss or recovery, trusted-session change/revoke, clock-map epoch change/fault, transport-loss boundary, or latch clear.
The exact durable/random boot-nonce source is an implementation design item; it must not be a predictable static constant.

The adapter serializes state observation, epoch/barrier mutation, custom-message decode, S1 `evaluate()`, S1 `poll()`, and eligible-request access through one validator-time authority and one critical section/work queue.
It samples current arm, research-mode, required-state-valid, transport, and clock-map state before each admission/poll decision; a detected change increments/installs the barrier before that packet can be decoded into an active request.
The adapter installs an epoch barrier immediately on each observed transition and clears its cached eligibility before processing a later packet.
On adapter start, state-subscription loss/unknown state, required-state-valid loss or recovery, or recovery from a link/state observer restart, it also installs a barrier rather than trusting an old cached request or a still-established S1 session.
It must not reconstruct `RequestGuard` to erase a latch or replay floors.

Sampling only the latest state is insufficient: disarm/rearm or deselect/reselect can occur between samples. The trusted supervisor must expose lossless transition events or monotonic transition-generation counters covering every relevant boundary. A missed event, observer restart, unknown state, or unprovable generation continuity closes the epoch barrier before any eligibility decision. Repeated stable invalid-state samples do not repeatedly increment the epoch. Epoch, status and administrative sequence counters must never wrap or be reused; exhaustion closes admission until an authorised new instance/session context is established.

The producer obtains fresh status before it establishes a session or sends usable requests, then echoes the current controller instance, transition epoch, and clock-map epoch in both custom inbound messages.
For each new barrier it must send `acknowledge current epoch` after observing the associated PX4 status.
The adapter accepts that acknowledgement only when its `acknowledged_status_sequence` identifies a status issued in the current epoch and its conservative earliest acknowledgement time
`M_e(producer_acknowledgement_monotonic_us) - U_e`
is strictly after the adapter's recorded validator-domain barrier time.

The adapter records the accepted acknowledgement's raw producer time and its complete adapter-computed mapped uncertainty interval.
Before passing a data request to S1 it requires both a strictly increasing raw producer creation time and a conservative temporal ordering proof:

`M_e(producer_monotonic_us) - U_e > M_e(producer_acknowledgement_monotonic_us) + U_e`.

This proves that even the earliest possible creation of the request occurred after the latest possible producer acknowledgement of the epoch.
Queued packets from before a same-session disarm/rearm, mode/link/state recovery, or adapter restart therefore cannot activate merely because S1 did not itself observe the earlier transition.
The acknowledgement neither establishes a session nor makes any request eligible; it only opens the current epoch barrier for a later fresh request.

Acknowledgement admission also validates checked mapping arithmetic, configured age/future bounds, current source/session and monotonic administrative sequence. A failed acknowledgement preserves the existing valid barrier state and does not advance its sequence floor. An accepted acknowledgement is frozen for that epoch; duplicate acknowledgements cannot move its time barrier. All independent current-state observations still apply even when the packet itself is rejected.

An old packet from before a reboot, mode re-entry, disarm/rearm, session transition, or mapping transition consequently fails before activation.
PX4 never resumes a cached objective across any such boundary.

Armed data requests can become eligible only after a trusted session was established while disarmed and a new request passes all checks in the current epoch.
Disarmed behaviour follows the configured S1 policy: `Reject` rejects data requests; `ValidateOnly` records validation without retaining an eligible request.
Either way, disarm clears an active request, and rearm requires a fresh in-epoch request.
The future integration must not claim that the standard Offboard heartbeat or a mode transition itself authorises research control.

## Processing And Status Correlation

The proposed inactive adapter has this ordered boundary, with no control publication:

`authenticated MAVLink 2 frame -> exact target/source binding -> custom decode/version/reserved validation -> epoch/map admission -> S1 RequestGuard -> correlated status/log record`.

Only a request that S1 marks eligible remains available to a later controller-facing interface.
In the next inactive slice there is no controller-facing uORB request publisher, no torque/thrust publisher, no actuator publication, and no autostart selection.
No receipt, `Accepted` decision, or status flag is a tracking, allocator, or physical-output result.

For a request-decision status, `ELIGIBLE_FOR_CONTROL` describes that request's S1 decision, not whether a previous accepted request survived its rejection. A snapshot reports eligibility only after a fresh `getEligibleRequest()`/poll with complete trusted context. Status loss or transmit backpressure must never block polling, state revocation or expiry; local decision evidence and status-stream gaps must make dropped reports observable.

For every request decision, `RESEARCH_ACCELERATION_STATUS` echoes the request `(source_id, session_id, sequence)` through `source_id`, `session_id`, and `correlation_sequence`.
For every session decision it instead echoes `(source_id, session_id, operation_sequence)` and sets `status_class = 2`.
The producer records the complete tuple plus `controller_instance_id`, transition/map epochs, validator time, `status_sequence`, decision, and flags.
It must detect missing/out-of-order status stream entries diagnostically; status loss never authorises retransmitting an already-used request sequence as a new request.
The request side retransmission policy is deliberately not selected here: an identical sequence is a replay by design, while a later sequence must contain a newly created, in-window immutable request.

## Offline Conformance Cases

These are deterministic interface cases, not a simulator or flight campaign.
They use non-operational fixtures only and assert fields/decisions, not numeric flight capability.

| Case | Required result |
| --- | --- |
| Generated-layout check | Generated XML/C/Python wire offsets, lengths, field order, CRC extra, and MAVLink-2-only framing match the approved dialect allocation. |
| Canonical round trip | Host and Pi serialise/decode identical v1 raw request vectors; the harness computes every S1 `CanonicalRequest` mapped-time/uncertainty member from an injected PX4-owned map without rotation or loss of source/session/epoch metadata. |
| Frame/gravity meaning | FRD basis, current-attitude body-held transformation, NED result, CG reference, and inertial-versus-specific-force fixtures agree with S1 `FrameMath`; no once-at-receipt rotation. |
| Standard-message exclusion | A `SET_POSITION_TARGET_LOCAL_NED`, heartbeat, command acknowledgement, or status text cannot be decoded as this request or establish/refresh its session. |
| Decode rejection | Wrong protocol/version/frame/quantity/reference/target, nonzero reserved/flags, non-finite acceleration, injected unauthenticated parser result, bad source/header binding, or malformed logical length yields no eligible request and an attributable status where parsing reached the adapter. |
| Mapping interval | Raw-validity or derived-map arithmetic failure, map epoch mismatch, invalid injected map, configured uncertainty excess, future/old/expired conservative endpoint, and a trusted map change during validity all fail closed. |
| Replay and restart | Duplicate/out-of-order sequence, producer-time nonincrease/regression, stale packet after reconnect, stale controller instance, and stale transition epoch cannot replace a valid request; fixed clock faults remain latched. |
| Session/re-entry | A data request cannot establish a session; establishment/change while armed fails; disarm/rearm, mode deselect/reselect, required-state-valid loss/recovery, revoke, latch clear, and adapter observer restart require the documented new status/epoch-acknowledgement/request order.  A pre-acknowledgement or conservatively pre-acknowledgement-created queued request fails. |
| Heartbeat separation | A current heartbeat with an absent, stale, delayed, repeated, or expired request leaves payload eligibility false. |
| Status correlation | Accepted and rejected request/session vectors produce a status carrying the exact correlation tuple, decision, current epochs, state, and status sequence; no status asserts physical execution. |

The test evidence must preserve raw frames, decoded records, map inputs/bounds, validator decisions, associated current-estimator-attitude sample identifiers for later evaluation, and source/dialect/configuration hashes.
It must report operational timing/limit tests as incomplete until human-approved requirements are supplied.

## Deployment Decisions Still Required

- Approve the dedicated MAVLink TELEM instance/channel, the producer and PX4 system/component IDs, and whether any operator-only status broadcast is allowed.
- Approve MAVLink 2 signing-key provisioning, rotation, revocation, parser replay policy, and a safe SITL test-key procedure before enabling a live ingress link.
- Name the supervisor authority and out-of-band authorisation procedure for session establish/revoke and latch clear before live use.
- Approve the deployed host/Pi clock-map source, reset conditions, and whole-lifetime uncertainty budget before live use.
- Approve command rate, validity/deadline/age/jitter limits, acceleration bounds/slew, estimator-validity criteria, loss response, feasible envelope, and all control/physical-output decisions from plant evidence.

The implementation team, under normal review, selects an available private dialect ID, generated CRC extras, and the concrete injected-map interface after acceptance of this document.
MAVLink `TIMESYNC` is not evidence by itself for the required whole-lifetime uncertainty bound; an offline map fixture may instead inject a frozen offset and explicitly supplied drift/uncertainty envelope.

## C0a Frozen Offline Payload API (2026-09-17)

Astra releases only the stateless payload codec sub-slice, C0a. This narrows the larger C0 harness gate below. It implements the documented payload bytes, not complete MAVLink frames: no message IDs, CRC extras, signing, routing, serial/network I/O, clock mapping, S1 state machine or eligibility decisions. Payload validation is not command admission.

The exact protocol value is `0x00010000` (major 1, minor 0). Primitive ordering at equal width is exactly the table order above; future XML must preserve that order. The length rules above apply to decoded payload bytes, not entire frames.

Frozen Python API in `Tools/research_sitl/wire/codec.py`:

- `encode_request(fields, *, trim=True) -> bytes`, `decode_request(payload) -> dict`.
- `encode_session(fields, *, trim=True) -> bytes`, `decode_session(payload) -> dict`.
- `encode_status(fields, *, trim=True) -> bytes`, `decode_status(payload) -> dict`.
- `CodecError(ValueError)` for invalid caller data/payload. No error path returns a partially decoded object.
- Inputs are plain dictionaries with exactly all fields in their table; no defaults, missing or unknown keys. Integer fields require actual Python integers (not booleans), within their unsigned wire width. Reserved arrays require four zero bytes. The `trim` option requires an actual boolean; payload inputs require `bytes`.
- Acceleration inputs permit Python int/float except bool, must be finite before and after binary32 conversion, and reject overflow. IEEE-754 nearest binary32 rounding and subnormal underflow-to-zero are serialization effects, not operational limiting. Preserve signed zero. Decoders return the represented binary32 value.
- Encoders validate before packing; decoders reject wrong lengths then zero-pad only the permitted request/session tails before validating. `trim=False` emits nominal lengths; `trim=True` removes zero tails down to the valid minimum (85/71/106). All allowed partial zero-tail lengths must decode identically.
- Request/session nonzero fields: source, session, controller-instance, transition-epoch and clock-map-epoch identifiers and both target IDs. Epochs start at 1. Sequence and raw timestamps may be zero; a request requires expiry strictly greater than creation. Session operation is exactly 1, 2 or 3. Frame/quantity/reference are exactly 1; flags and reserved bytes are zero.
- The codec rejects forbidden extra position, velocity, attitude, thrust or actuator fields. It does not choose acceleration bounds or timing deadlines, decide source ownership, compare epochs with current state or reject duplicate requests. Those are adapter/S1 responsibilities; repeatedly decoding bytes is not replay acceptance.

Status structural consistency, applied to encoding and decoding:

- Controller-instance and transition epoch are nonzero; a valid clock-map state requires a nonzero clock epoch. Other zero-valued identity/correlation fields remain permissible to report rejected or unsolicited traffic.
- State fields use only their tabulated enums. Target IDs are both nonzero, or both zero only for snapshot class 5; mixed zero/nonzero targets fail. Classes 3/4/5 are unsolicited and require correlation sequence zero.
- Decision codes are 0--37 or the assigned 64--83 values below; unassigned values fail. Code 0 is permitted only for class 5 and cannot advertise eligibility.
- Only flag bits 0--5 are defined. An absent age/last-sequence flag requires its associated value to be zero. `LATCHED` is equivalent to session state 5; when set, first cause is code 18 or 19, otherwise first cause is zero. Decision 18, 19 or 37 requires latched state.
- `ELIGIBLE_FOR_CONTROL` requires decision 1, class 1 or 5, syntactic and authenticated-link flags, armed state 2, selected mode 2, established session 2, connected transport 1, valid map 1, and nonzero source/session. It cannot coexist with a latch. This only checks internal consistency of a reported claim, not its truth or authorization.
- Decision 2 requires disarmed state 1 and no eligibility. Other relationships are left to the future stateful adapter; the codec must not invent a flight-response policy.

Frozen adapter-only decision names (protocol values, not MAVLink message IDs):

| Value | Name |
| --- | --- |
| 64 | MAVLINK2_REQUIRED |
| 65 | AUTHENTICATION_FAILED |
| 66 | TARGET_MISMATCH |
| 67 | RESERVED_NONZERO |
| 68 | CONTROLLER_INSTANCE_MISMATCH |
| 69 | TRANSITION_EPOCH_MISMATCH |
| 70 | EPOCH_ACK_REQUIRED |
| 71 | EPOCH_ACK_ORDERING |
| 72 | CLOCK_EPOCH_MISMATCH |
| 73 | UNKNOWN_SESSION_OPERATION |
| 74 | MALFORMED_PAYLOAD |
| 75 | ADMIN_SEQUENCE_REPLAY |
| 76 | SESSION_REVOKED |
| 77 | MAP_INTERVAL_INVALID |
| 78 | COUNTER_EXHAUSTED |
| 79 | STATE_GENERATION_UNKNOWN |
| 80 | STATUS_REFERENCE_INVALID |
| 81 | MAP_BINDING_MISMATCH |
| 82 | MAP_ARITHMETIC_INVALID |
| 83 | UNSUPPORTED_FLAGS |

Values 38--63 and 84--65535 are reserved and rejected in v1. Codec errors are local exceptions, not automatic wire status publication.

Frozen injected boundary for later C0b (design only in C0a):

- `TrustedClockMap`: valid boolean; source/session/controller-instance/map-epoch uint64 binding; signed-int64 offset; uint64 uncertainty; inclusive producer-domain `valid_from_us`/`valid_until_us`. A valid map requires nonzero bindings, positive uncertainty and ordered bounds. Checked mapping must cover creation, exclusive-expiry endpoint and any retained acknowledgement instant. These immutable inputs come from the trusted harness/PX4 side, never decoded packet fields.
- `TrustedIngressContext`: validator monotonic uint64 time; authenticated-parser verdict; independently configured header/channel/source-binding verdict; controller instance and transition generation; observer-continuity verdict; S1 arm/mode states, transport/required-state/map validity. Supervisor session/revoke/latch-reset actions are separate trusted administrative events, not booleans copied from the packet.
- A future `canonicalize` boundary maps raw endpoints using the frozen offset, fills S1 canonical fields and explicitly maps wire enums. It returns failure on bad bindings, interval or arithmetic. It does not set eligibility. Stateful administration, polling and admission must call the existing C++ S1 guard; no Python replacement guard is authorized.
- The codec consumes none of these trusted objects and cannot create one from received data. Concrete C++ wrapper signatures, transition event storage and the full C0b test harness still require a separate source-path/API review.

C0a acceptance: independent table-derived byte vectors for all three payloads; every permitted trimmed length; uint-width boundaries and bool rejection; finite/overflow/subnormal/signed-zero binary32 behavior; malformed length/version/enum/reserved/extra-field rejection; status consistency checks; no input mutation; and complete regression of existing E1 tooling. Fixtures are non-operational. Generated C/Python MAVLink interoperability remains pending until a reviewed dialect exists.

## Exact Next Release Gate

Master review: the C0a payload/API subset above is frozen for offline implementation. The broader generated dialect and stateful C0b adapter remain unreleased. The generated dialect/layout and transition conformance tests remain required evidence, not completed checks.

The next possible implementation release is **Gate C0: Offline Wire Codec And Canonical-Adapter Harness**, after Astra accepts this wire contract and assigns paths.
It may allocate the dialect and implement only pure custom encode/decode, zero-trim handling, checked wire-to-S1 conversion, epoch/session state orchestration, `RequestGuard` invocation, structured status generation, and the offline conformance cases.
Authentication/parser result, source binding, supervisor decision, current vehicle state, and PX4-owned frozen map `(e, O_e, U_e)` are injected trusted inputs in non-operational fixtures; no deployed signing key, endpoint ID, TELEM configuration, live clock synchronisation, or approved operational drift budget is needed to test those branches.
The harness must stay outside live ingress and remain opt-in/inactive: no root build/startup integration, mode activation, trajectory/attitude/rate/torque/thrust/actuator publisher, airframe/model change, SITL run, board build, flash, or physical operation.

**Gate C1: Live Ingress Attachment** is separately blocked on the deployment approvals above and integration evidence that the parser provides the required authenticated-frame result and the adapter receives the serialized current-state/map inputs.
It still permits no active controller or outputs.

Any acceleration-to-wrench/attitude control, allocation, or output-producing path remains behind the later plant-supported control-design and output-integration gates.
