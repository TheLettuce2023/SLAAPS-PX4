# Phase 2 SITL-Only Profile Proposal

Status: **user-approved for offline SITL testing only; inactive and software-only**.

The user answered "Yes" to the explicit offline-testing approval question.
Values labelled PROPOSED below are now approved synthetic test targets, not
operational requirements. Residual-attitude and inactive-loss policies have
the same limited approval. Verification remains separate from approval.
No live interface, plant, controller, output or physical-operation gate is waived.

This proposal turns the confirmed FRD, CG-referenced acceleration contract into a bounded SITL profile for review and measurement planning.
It does not enable a MAVLink dialect, receiver, Offboard/research mode, controller, allocator, actuator output, arming path, or Gazebo run.
Every numerical timing value below is a deliberately synthetic candidate, not a measured host/Pi/link result, flight limit, physical-control limit, or hardware configuration.

The applicable wire object remains the custom MAVLink 2 request/session/status design in `docs/ACCELERATION_WIRE_CONTRACT.md`.
`SET_POSITION_TARGET_LOCAL_NED` is not substituted for that contract.

## Proposed SITL Profile

| Item | Proposed value or rule | Status and rationale |
| --- | --- | --- |
| Request rate | 50 Hz; nominal period 20 ms | **PROPOSED synthetic profile value.** A software test cadence only; not a controller sample rate or a Pi performance claim. |
| Producer inter-request gap | At most 24 ms in a nominal healthy profile | **PROPOSED.** Allows 4 ms producer scheduling-release jitter above the 20 ms period. |
| Request validity interval | 80 ms, exclusive at expiry | **PROPOSED.** Four nominal periods; it leaves margin beyond the candidate admission budget without treating an old request as safe. |
| Created-to-received budget | At most 12 ms | **PROPOSED.** Includes host transport/queue delay only as a test bound; no UART baud or latency is selected. |
| Received-to-admitted budget | At most 4 ms | **PROPOSED.** Software/validator observation bound for instrumentation, not a PX4 scheduling guarantee. |
| Clock-map uncertainty | At most 4 ms over the request validity interval | **PROPOSED.** Must be proven by a whole-lifetime map evidence source before operational use. |
| Nominal conservative admission-age target | At most 20 ms | **PROPOSED measurement target**, not the guard's hard age limit: `12 + 4 + 4 ms`. Exceeding it fails the nominal-profile test, even if the guard still admits the request below its hard bound. |
| Guard maximum command age | 60 ms, inclusive | **PROPOSED synthetic configuration.** Applies at admission AND every retained-request access/poll. A 20 ms hard limit would create nominal gaps at a 20 ms producer period once delay/uncertainty is included. `20 ms` worst nominal admission age + `24 ms` next-release gap is below 60 ms. |
| Guard maximum future skew | 8 ms | **PROPOSED synthetic configuration.** Twice the 4 ms map uncertainty accommodates both interval endpoints at zero transport delay; it does not permit a future earliest creation. |
| Fixture observation cadence | Nominal 1 ms, maximum gap 2 ms | **PROPOSED test-observer budget**, not a firmware scheduler. Every eligible-request access independently rechecks time and state; delayed polling cannot authorize a stale copy. |
| No-fresh-request diagnostic threshold | 40 ms since the most recently admitted eligible request | **PROPOSED.** Diagnostic only; measured from admission, not creation. It may occur after hard-age invalidation for a late admission and never extends validity. |
| Request expiry | Conservative earliest mapped expiry is exclusive | Raw validity is 80 ms. The guard expires at `mapped creation + 80 ms - U`, or earlier when the 60 ms conservative-age limit/state gates fail. No timer is restarted at reception. |
| MAVLink heartbeat | 1 Hz diagnostic/liveness evidence only | **PROPOSED.** It never refreshes, replaces, extends, reorders, or authorizes an acceleration request. |
| Status | One correlated status decision for each decodable request/session decision where the adapter can represent a report; plus at most one diagnostic snapshot per second | **PROPOSED.** The bounded snapshot stream is diagnostic; contract exceptions for unconfigured/terminal non-reportable cases remain explicit. |
| Transport baud and endpoint | Not selected | Required before Pi/TELEM or live operation. This proposal supplies a sizing check only. |
| Acceleration/rate/slew bounds | Not selected | Plant and control evidence are required; no physical limit is inferred from the timing profile. |

The candidate interval is internally ordered as follows: a producer may release requests up to 24 ms apart; a particular request must be received within 12 ms, admitted within a further 4 ms, and carry no more than 4 ms map uncertainty, making its conservative age no more than 20 ms at admission.
The guard uses the separate 60 ms conservative-age bound for admission and
retention; the 20 ms target is only the nominal measurement criterion.
Its proposed minimum/maximum validity are both 80 ms for this profile; maximum
map uncertainty is 4 ms, with a strictly positive actual uncertainty supplied
by each map. At U = 4 ms the earliest expiry is mapped creation + 76 ms,
while the hard age bound closes retention just after mapped creation + 56 ms.
The 80 ms raw lifetime is not a promise that a request remains eligible that long.
A received heartbeat does not alter any of these timers.

## Conditional Link-Size Check

The requested source-derived signed MAVLink 2 frame-size ranges are:

| Frame | Source-derived signed size | Candidate rate | Directional byte rate, maximum size |
| --- | ---: | ---: | ---: |
| Request | 110--115 bytes | 50 Hz | 5,750 bytes/s |
| Session | 96--101 bytes | Event-driven | No periodic budget assumed |
| Status decision | 131 bytes | Up to one per 50 Hz request decision | 6,550 bytes/s |
| Diagnostic status snapshot | 131 bytes | At most 1 Hz | 131 bytes/s |

If and only if a future link is UART 8N1, use 10 transmitted bits per byte.
At the proposed maximum request/status cadence this is 57.5 kbit/s request-direction traffic and 66.81 kbit/s status-direction traffic, before standard MAVLink heartbeats, other telemetry, signing/framing changes, retries, session events, or any sharing of a channel.
For example, a hypothetical full-duplex 115200-baud link would have roughly 50 percent request-direction and 58 percent status-direction utilisation from these custom frames alone.
This is a conditional arithmetic check, not selection of 115200 baud, a TELEM port, or a throughput guarantee.
C0c independently checks the generated lengths and framing. Link throughput,
queueing and clock uncertainty still require measurements before live attachment.

## Proposed PX4 Residual-Attitude Rule

The companion supplies only the three FRD inertial-CG acceleration components.
It supplies no heading, yaw, attitude, body-rate, thrust, actuator, position, velocity, or speed objective.

**Proposed rule for a later research controller:** PX4 transforms the current body-held FRD request using its valid estimated attitude, determines the feasible acceleration/force subspace from the approved plant and state, and owns all uncommanded degrees of freedom.
Among feasible stabilizing candidates, it should prefer the minimum rotation from the current estimated attitude and the minimum body-rate demand about the residual/uncommanded axis, subject to estimator validity, rate limits, envelope limits, and actuator feasibility.
Feasibility and stabilization take priority over tracking.

This is a policy proposal, not a control law, gain choice, attitude target, yaw/heading hold instruction, or assertion that the vehicle can generate a requested acceleration.
It deliberately does not send a companion heading to resolve residual degrees of freedom.
It also cannot be implemented or tuned until the canard/EDF effectiveness, CG/inertia, local flow, actuator limits, and feasible envelope are evidenced.

## Inactive Loss and Failure Rules

For this profile, the following are software validity rules only:

- Expiry, excessive retained-request age, observed trusted map/state/session transitions, required-state loss or a guard latch revoke current eligibility according to the existing adapter/guard rules.
- A malformed, foreign, replayed or out-of-order incoming packet is rejected; it does not by itself erase a still-valid prior request, mutate trusted state, or extend any timer. Independently observed faults still take effect. This preserves the existing protection against foreign traffic disrupting current authority.
- The previous valid request may remain eligible only within its original bounds. No component holds it beyond those bounds, reuses a sequence to refresh it, or treats a zero acceleration vector as an intrinsically safe substitute.
- A 1 Hz heartbeat may remain live while the payload is stale; this is an explicit failing test case, not a refresh path.
- In the inactive harness, an unexpected nominal-profile violation fails the run with attributable evidence. Deliberate fault cases pass only when the expected rejection/invalidation and any allowed preservation of a prior request match the oracle. The harness must not arm, publish a controller objective, publish torque/thrust, publish an actuator value, or automatically disarm, RTL, land, or select a flight mode.
- Later active-vehicle loss behavior must be approved from plant/control/operations evidence. Software invalidation is not claimed to be physically safe.

## Parameterized Measurement Plan

Each test vector and any later live experiment should preserve the correlation tuple `(source_id, session_id, request sequence)` plus controller instance, transition epoch, clock-map epoch, status sequence, decision code, and these timestamps:

| Event | Timestamp/provenance to record | Purpose |
| --- | --- | --- |
| Created | Producer monotonic creation time | Immutable request age start. |
| Sent | Producer transmit observation, if available | Separates local scheduling from wire delay. |
| Received | PX4 parser/adapter receipt monotonic time | Measures created-to-received interval after map conversion. |
| Admitted/rejected | PX4 validator decision time | Measures received-to-decision and records reason. |
| Accessed | Existing `WireAdapter::getEligibleRequest` called by an offline fixture with current trusted state/time | Measures canonical-request eligibility only, not physical/controller execution. No active controller is required to exercise this seam. |
| Status emitted/returned | PX4 status-generation and producer-receipt observations | Measures decision-to-observable-result and detects status gaps. |
| Clock evidence | Map epoch, offset, uncertainty, valid interval, reset/fault observation | Proves that the uncertainty covers the entire request lifetime. |

Required parameterized cases are:

1. Nominal 50 Hz request stream with 20 ms period and separately injected release jitter through the 24 ms profile gap.
2. Delay at/below/above the 12 ms receive and 4 ms admission targets; distinguish the 20 ms nominal age target from hard-age boundaries at 60 ms and 60 ms + 1 us. Check conservative expiry at `mapped creation + 80 ms - U`, not at receipt plus 80 ms. Use a separate boundary fixture to isolate expiry if age would reject first; never weaken the proposed nominal profile to force a particular result code.
3. A live 1 Hz heartbeat with missing, stale, delayed, repeated, and expired requests.
4. Delay, jitter, drop, duplicate, reorder, replay, producer restart, PX4/controller-instance restart, session/transition change, source mismatch, and clock-map epoch/reset/fault cases.
5. Clock-map interval/uncertainty proof for the whole request lifetime, including invalid, excessive-uncertainty, future, and map-change cases; a bare `TIMESYNC` observation is insufficient.
6. Status-stream loss/reordering/backpressure observation. A missing status is diagnostic evidence, never acceptance or permission to reuse a sequence.
7. Residual-attitude policy review fixtures that show no companion heading is accepted and that infeasible/unavailable directions select invalidation/limiting evidence rather than invented actuator commands. These fixtures are not plant-performance tests.

## Predeclared Software-Profile Criteria

The following are pass/fail criteria for an inactive software profile, not physical tracking criteria:

- A nominal injected vector meeting the proposed timing/map/session/state conditions produces exactly one attributable decision/status where reporting is representable; the recorded correlation and immutable creation/expiry fields agree end to end.
- Exceeding the 20 ms nominal admission-age target fails nominal-profile timing, but is not falsely claimed to trigger the guard's separate 60 ms hard bound. Exceeding hard age, reaching conservative expiry, or violating ordering/session/source/map/finite-field rules rejects the offending request or revokes eligibility as the contract specifies. Rejected foreign/replayed traffic cannot refresh or necessarily erase a distinct still-valid request.
- A current heartbeat plus stale/expired request fails payload freshness; a heartbeat never moves creation, expiry, sequence, session, or clock-map state.
- All observed event timestamps are present with declared provenance. A deliberately dropped event has an explicit absent/drop reason, never a fabricated receipt timestamp. Unexplained missing, non-monotonic, incomparable, or unbounded clock evidence is **incomplete/fail**, not a substituted success value.
- The selected synthetic frame sizes/rates are recomputed from generated dialect output before a live-link review. A mismatch is incomplete until the sizing and budget are re-reviewed.
- Inactive-run evidence contains no arm, mode-selection, controller, torque/thrust, actuator, simulator-physics, or physical-output event.

No criterion measures acceleration tracking error, plant authority, stability margin, servo/EDF response, physical safety, or Pi/serial performance.

## What Review Would and Would Not Close

Master review accepted this document as internally consistent; the user then
approved its inactive software-testing scope. Subsequent evidence is separate.
No configuration or guard default is changed by this document. P2T1 uses the
approved values in its isolated fixture; see PHASE_2_PROFILE_VERIFICATION.md.
Approval provides a predeclared target for offline conformance work and avoids
a circular requirement to finish Phase 4 controller/allocation implementation
before completing the Phase 2 contract/source-trace design.

It would not approve a baud rate, signing key, channel, clock provider, live ingress, active research mode, controller, allocation, physical output, launch/recovery behavior, or flight fallback.
It would not demonstrate that the canard/EDF plant can stabilize or track any acceleration request.
Operational signoff still requires link/control-derived measurements and the separately approved deployment, plant, control, allocation, and operations gates.

## Review Decision Recorded

Approved by the user for offline SITL testing only. The next step is verified
offline profile evidence, not another request to approve the same values.
That bounded evidence/review is now recorded in PHASE_2_OFFLINE_COVERAGE.md;
the next proposed live host-only scope requires its own C1H approval.
Changes to these targets need explicit review; no Cessna default or guessed
physical-control value may substitute for missing operational evidence.
