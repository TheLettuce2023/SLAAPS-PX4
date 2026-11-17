# Phase 2 Exit Audit

Audit date: 2026-09-19.
Scope: read-only assessment of the current Phase 2 artefacts and pinned PX4 source at `e880f1cd79e2453ef3dfaf68ee1c067b98ea9a53`.
No simulator, firmware, transport, model, or physical test was run for this audit.

## Result

**Phase 2 roadmap exit: incomplete.**

Current update: the user approved PHASE_2_SITL_PROFILE_PROPOSAL.md for offline
SITL testing only. P2T1 timing/accessor, P2T2 status delivery/diagnostics and P2T3
payload contract/policy-review gates now pass at their bounded scope; the
17-target suite passes Debug, repeated Release and sanitizers. See
PHASE_2_OFFLINE_COVERAGE.md. The inventory below describes full roadmap
and operational gaps, not a request to approve the same synthetic profile again.
The approved residual-attitude/inactive-loss policies are design/test targets;
they are not measured control/link budgets or approved physical fallback.

The project has completed substantial offline interface work: a precise custom MAVLink-2 wire design, an accepted offline payload/admission/session/status foundation, frame/gravity semantics, and a source trace that proves the stock fixed-wing path is not a three-axis acceleration controller.
Those are useful and reviewable software foundations, not an approved live acceleration interface.

The Phase 2 contract gate remains unmet because the operating rate/deadline/age
budgets, residual-attitude policy and loss-response requirements have not been
approved from control/link evidence. Deployment identity/authentication/clock
procedures also remain open for the separately gated live interface.
Phase 2 is a contract/source-trace gate: it must not be made circular by
requiring the completed Phase 4 controller or Phase 6 integrated tracking
campaign first. The trace must explicitly identify every unimplemented seam;
it cannot turn those gaps into claims of an operational acceleration path.
No artifact claims that a request is tracked, allocated, or actuated.

## Evidence Reviewed

- `docs/CUSTOM_VEHICLE_SITL_ROADMAP.txt`, Phase 2 deliverables and exit gate.
- `docs/ACCELERATION_INTERFACE.md`, current transport/source trace and stock-path limits.
- `docs/ACCELERATION_WIRE_CONTRACT.md`, custom MAVLink-2 v1 design and deployment decisions.
- `docs/C0B2_STATE_CONTRACT.md`, offline state/admission boundary and its explicit live-attachment limits.
- `docs/PHASE_GATE_STATUS.md`, accepted offline C0b/O1/O2 evidence and retained live-interface gaps.
- `docs/RESEARCH_DECISION_CHECKPOINT.md`, confirmed requirements and human-owned decisions.
- Source spot checks: `src/modules/mavlink/mavlink_receiver.cpp:1194-1312`, `src/modules/commander/ModeUtil/control_mode.cpp:111-141`, and `src/modules/fw_mode_manager/FixedWingModeManager.cpp:387-399,1060-1091,2189-2255`.

The checked source still supports the interface audit's central finding: stock MAVLink maps fields into `trajectory_setpoint`, Commander gives position then velocity precedence over acceleration, and the fixed-wing manager requires position/velocity structure before it converts full acceleration to curvature/navigation feed-forward.
That is not an FRD, CG-referenced, three-axis acceleration consumer.

## Roadmap Deliverable Inventory

| Phase 2 deliverable | Current evidence | Offline status | Full exit status |
| --- | --- | --- | --- |
| Decide three axes or subset plus speed/thrust objective; resolve remaining degrees of freedom | `ACCELERATION_WIRE_CONTRACT.md` specifies only three finite FRD inertial-CG acceleration components; `RESEARCH_DECISION_CHECKPOINT.md` confirms no companion speed/thrust command. | Three-axis-only objective recorded. | Incomplete: PX4 residual-attitude/heading policy is explicitly deferred to plant-supported controls review. |
| Units, frame, reference, gravity, clock, validity, sequence, ownership, status | Wire contract defines FRD, CG, inertial acceleration, producer/session/sequence/expiry/epoch fields and correlated status. | Semantics and offline data model are detailed. | Incomplete: operational clock-map service, uncertainty budget, validity/age limits, session authority, link identity, and live status transport are pending. |
| NED/ENU and FRD/FLU conversions; CG versus offset IMU | `ACCELERATION_INTERFACE.md` documents stock NED/body handling and conversions; wire contract fixes current-body-held FRD-to-NED meaning and distinguishes specific force. | Complete as design/source evidence. | Actual CG/IMU values are later physical-integration inputs, not a blocker to this semantic contract. No assumption that raw IMU specific force equals CG inertial acceleration is permitted. |
| Unused/non-finite/stale/replay/bounds/mode/reconnect/rejection/fallback rules | Wire contract and C0b documents define immutable fields, finite checks, sequence/session/epoch/expiry handling, rejection status and fail-closed offline admission. | Offline guards/state/status evidence is accepted in `PHASE_GATE_STATUS.md`. | Incomplete: numeric bounds, rate/deadline, active mode entry/exit, reconnect on the real link, and a physically feasible fallback are not approved. |
| Choose initial transport and record compatible dependencies | User-selected MAVLink; custom MAVLink 2 instead of stock `SET_POSITION_TARGET_LOCAL_NED`. C0c records exact generator pins and verified local-test XML/IDs/CRCs. | Offline generated framing PASS; see C0C_GENERATED_WIRE_CONFORMANCE.md. | Live ID/channel/key deployment and Pi library/OS qualification remain separate decisions. |
| Trace reception through actual consumers and Commander flags | `ACCELERATION_INTERFACE.md` traces standard MAVLink/DDS, Commander, fixed-wing manager, attitude/rate control, allocator and gz outputs. The source spot check above agrees. | Complete for the stock paths and their limitations. | Custom-path integration is unimplemented. Phase 2 must name intended producer/consumer seams and ownership, and distinguish source-backed links from planned links; implementation and tracking stay in later gates. |
| Set command rate and deadline using control/link budgets; separate heartbeat from payload freshness | Wire contract separates heartbeat, payload validity, sequence, age, expiry, map uncertainty and status correlation. | Correct separation is specified. | Incomplete: no command rate, heartbeat rate, deadline, age, jitter, latency, or uncertainty values are approved from vehicle/control/link evidence. |

## Exit-Gate Evidence

The roadmap gate requires an approved interface document, a complete producer-to-consumer trace, and tests for frame/sign/units/time handling and stale requests.

| Gate evidence | Assessment |
| --- | --- |
| Approved interface document | **Incomplete.** The document set is a detailed design with confirmed FRD/CG/body-held semantics and MAVLink selection, but it labels dialect allocation, signing, clock map, live ingress, timing, authority and fallback as pending. It cannot be treated as deployment approval. |
| Complete producer-to-consumer trace | Stock trace identifies the actual behavior and incompatibility. The custom-path trace must retain explicit missing live receiver/state/clock and controller-consumer links; it is not yet an executable end-to-end path. Closing the Phase 2 design cannot be reported as closing those later implementation gates. |
| Frame/sign/unit tests | Offline S1/C0b evidence plus accepted C0c generated C/Python interoperability. No live-link, estimator-in-the-loop or actuator-sign test is claimed; those remain later integration/plant evidence. |
| Time and stale/replay tests | **Partially complete offline.** C0b/O1/O2 evidence covers injected state/map/session/admission cases. It does not measure a real producer clock, XRCE/MAVLink transport timing, link jitter, serial delay, real parser/signing behavior, or a live stale-command loss response. |

## Custom Path: Implemented Seams Versus Planned Links

| Stage | Source-backed state now | Required ownership/integration boundary |
| --- | --- | --- |
| Host/Pi request producer | C0a payload encoders and C0c generated-code fixtures; no deployed research sender. | One configured host script or Pi translator; immutable CG inertial-FRD objective and original producer times. |
| MAVLink frame and typed decode | C0c tests isolated generated C/Python codecs; stock receiver has no custom research handler. | One selected research MAVLink channel; authenticated parser result and exact source/target binding before admission. Unsigned test frames grant nothing. |
| State, clock, consent | C0b accepts trusted typed fixtures. O1/O2 implement offline operator consent and one by-value adapter for session administration. | Future serialized owner must supply current state, lossless transition provenance and trusted clock map independently of the producer/UI. O2 is not that live service. |
| ACK and request admission | `WireAdapter::processAcknowledgement` and `processRequest` call existing checked map/guard logic. | One adapter/guard authority; never reconstruct it to erase replay floors or a latch. No generated-parser-to-adapter live binding exists yet. |
| Controller-facing request | `WireAdapter::getEligibleRequest` copies only an ACK-gated, freshly checked canonical request. | This is the implemented software consumer seam, not a controller publication. A later controller must use current estimated attitude for body-held FRD interpretation, never simulator truth. |
| Control, allocation and output | No active research controller/mixer. H1 is only a normalized command limiter. Stock torque/thrust topics and allocator are source-traced separately. | Phase 4 must assign one publisher per stage and explicit physical-to-normalized scaling. `VehicleTorqueSetpoint.xyz` is normalized; `VehicleThrustSetpoint.xyz` is normalized [-1,1], not N or Nm. No authority matrix is inferred from X layout. |

The custom source trace reaches tested admission/accessor seams but stops at
explicit unimplemented links. No message reception, codec round trip or status
bit is evidence of acceleration tracking. Closing Phase 2 contract review does
not promote those later links or authorize output.

## Required Decisions

These decisions should not be inferred from fixtures, Cessna defaults, or source constants.

1. Approve a residual-attitude/heading policy for an FRD acceleration-only request: which attitude/yaw objectives PX4 may choose when the requested acceleration does not determine all degrees of freedom, and how the policy changes near unavailable authority.
2. Approve command, heartbeat and status rates; request validity interval; maximum conservative age; link/clock uncertainty; latency and jitter budgets; and the measurement method that demonstrates them on host SITL and Pi-to-TELEM. These need control/link evidence, not guessed values.
3. Name the deployed authenticated MAVLink instance, producer system/component/source binding, signing-key provisioning/rotation/revocation procedure, and independent supervisor authority for establish/revoke/latch-clear actions.
4. Approve the live clock-map provider, reset/epoch rules, whole-validity uncertainty bound, and status-retention/transport owner. `TIMESYNC` alone is not the approved whole-lifetime map evidence described by the wire contract.
5. Approve entry, exit, disarm/rearm, reconnect, invalid-estimate, loss-of-link and loss-of-authority behavior. The stock `COM_OBL_RC_ACT` choices are not automatically feasible for the canard/EDF/no-main-wing airframe.
6. Before later control/output promotion, provide or approve the plant evidence that determines feasible acceleration bounds, canard/EDF authority, CG/IMU geometry, slew limits and the actual physical fallback envelope. This is not a request for geometry during the current offline work.

## Unblocked Work and Boundary

### Evidence available for the timing decision

The pinned generated-C framing constants specify 12 unsigned framing bytes
and a 13-byte signing block. This is source-derived sizing, not a link
measurement or approval to provision keys. With no extensions:

| Message | Normal trimmed payload | Signed frame, trimmed / fully padded |
| --- | ---: | ---: |
| Request | 85 bytes | 110 / 115 bytes |
| Session | 71 bytes | 96 / 101 bytes |
| Status | 106 bytes | 131 / 131 bytes |

For an explicitly chosen 8N1 UART, serialization costs ten bits per byte.
Before overhead from other messages, an uplink budget must therefore cover
`10 * (115 * request_rate_hz + 101 * session_rate_hz)` bits/s using padded
worst-case sizes; downlink must cover `10 * 131 * status_rate_hz` bits/s.
Apply separate full-duplex direction budgets, then reserve headroom and bound
bursts, queueing, parsing and scheduling. Average utilization alone does not
prove a deadline. Actual baud, other traffic, rates and jitter bounds remain
unselected; a net/IP SITL path has different additional framing costs.

The command period/deadline must also fit the reviewed control/authority budget;
source-size arithmetic cannot select it. Clock uncertainty must cover the
whole request lifetime and the ACK ordering proof, not just a sample offset.
These derived sizes allow a focused measurement plan without new geometry.

### Next boundary

The bounded offline generated-dialect/framing conformance increment has passed
master/independent review and strict Debug, optimized and sanitizer host tests.
It does not add live I/O, mode activation, controller publication, allocation
or actuator output, and cannot by itself satisfy Phase 2 contract signoff.

The user approved the synthetic SITL-only profile: 50 Hz requests, 20 ms nominal
admission-age target, separate 60 ms hard age, 80 ms raw lifetime and 4 ms
uncertainty ceiling, with the stated PX4 residual-attitude/inactive-loss policies.
P2T1/P2T2/P2T3 now account for the seven groups at the released offline scope,
with inherited regression evidence and explicitly non-executable residual-policy
obligations. They do not supply operational measurements or complete the roadmap
Phase 2 gate. No further named necessary implementation gate within that offline
release was identified by independent review. Next is the proposed, unimplemented
C1H isolated host-loopback release, which needs explicit approval for local I/O,
test keys and clock qualification. Geometry is not needed for that decision.
Live PX4 deployment and control/link-derived operational budgets stay separate.

Naming an owner or approving a measurement plan does not itself satisfy the
missing numerical budget or policy evidence. Hardware TELEM/signing/clock
deployment approvals are separate; do not request geometry as a substitute
for these interface requirements or declare a provisional profile flight-ready.

## Handoff

The original audit changed only this file. Subsequent approval/P2T1 updates
are recorded in PHASE_GATE_STATUS.md and PHASE_2_PROFILE_VERIFICATION.md.

The main gap is architectural rather than transport parsing: the current stock fixed-wing path does not consume the confirmed FRD acceleration request as a three-axis control objective, and the planned custom path is intentionally not attached to a controller or outputs yet.
