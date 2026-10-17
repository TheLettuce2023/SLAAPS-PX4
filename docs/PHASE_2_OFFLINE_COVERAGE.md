# Phase 2 offline campaign: coverage and promotion boundary

Review: 2026-09-19. Master synthesis plus independent gate/source review.

**Bounded offline contract tests and policy review: accepted. Full roadmap
Phase 2 exit: still OPEN.** The previously released, geometry-independent
offline implementation gates now have verified results. No additional named
implementation gate within that release was identified by the independent audit.
This is not an exhaustive fault cross-product or live-interface qualification.

## Evidence classification

- Facts: user-approved acceleration-only CG inertial-FRD/body-held semantics,
  offline profile, source APIs and test results pinned to PX4
  `e880f1cd79e2453ef3dfaf68ee1c067b98ea9a53`.
- Observations: 17/17 host CTest targets pass in Debug, three repeated Release
  runs and ASan/UBSan. Existing codec 8 and evidence 17 Python tests pass.
- Synthetic inputs: every profile clock/map, state, authentication verdict,
  acceleration bound and delivery fault. Queue capacity/drop-newest is a test
  policy, not deployment approval. These are not flight or link measurements.
- Inference: software boundaries behave as specified in the covered fixtures.
  No inference of physical feasibility, host/Pi deadline guarantees, operational
  authentication, actual acceleration tracking or actuator authority is allowed.

## All seven approved profile groups accounted for

| Group | Executable or review evidence | Limit of acceptance |
| --- | --- | --- |
| 1. Nominal 50 Hz and jitter | P2T1 `TimingProfileTest.cpp::streams`, independent timing oracle; 20/24 ms schedules and 1 ms accesses. | Virtual schedule, not measured producer performance. |
| 2. Timing and expiry boundaries | P2T1 `timingBoundaries`: receive/queue below/at/above, 60 ms inclusive age/+1 us, delayed access and separate exclusive-expiry isolation fixture. P2T2 late admission preserves the actual 60 ms profile. | The 100 ms age isolation fixture is expressly not an alternate approved profile. |
| 3. Heartbeat versus freshness | P2T1 `cacheFaults` ages a request away under independent 1 Hz events; P2T2 pairs heartbeat with a stopped request stream, delayed duplicate, stale access and delayed status. Its late scenario tests invalidation before the no-fresh diagnostic. | Cases are composed with inherited timing/replay tests, not every possible heartbeat/fault combination. No wire heartbeat service is deployed. |
| 4. Delay/drop/replay/restart/transitions | P2T1 timed/cache cases; P2T2 dropped/backpressured/reordered statuses and unchanged admission. Inherited RequestGuard clock restart/regression, WireAdapter state/session/admission reconnect/latch/replay/counter matrices are rerun. | Trusted state and reset events are injected; no real PX4/Pi restart or live transition observer is proved. |
| 5. Map lifetime and uncertainty | TrustedTimeMap rejected-input/arithmetic/existing-guard tests; RequestGuard uncertainty/future boundaries; adapter map/timing matrices; P2T1 map-interval/epoch faults. | Tests verify an asserted whole-lifetime map, not a deployed service's actual error bound. A TIMESYNC sample is still insufficient. |
| 6. Status delivery and diagnostics | P2T2 verifies 4,110 events, complete original/returned status bytes, explicit drops/null receipt, queue depth, order, <=1 Hz snapshots, 40 ms diagnostics and same-time admission ordering. 36 corrupted evidence cases rejected. | Synchronous virtual-time host fixture, not a network implementation, deployed retry/drop policy or wall-clock performance test. |
| 7. Acceleration-only/residual policy | P2T3 six codec-test methods reject extra/substituted objectives and malformed semantics; inherited actual frame tests cover body hold. Seven declarative policy cases reviewed. | No executable feasibility/residual-attitude resolver exists. Unavailable-direction handling, limiting, ties and rotation/rate tradeoffs remain later plant-supported control obligations, not test PASS claims. |

Inherited source anchors: `RequestGuardTest.cpp` restart/regression and exact
uncertainty/future cases; `TrustedTimeMapTest.cpp::rejectedInputs`,
`arithmeticEdges`, `existingGuardBoundary`; `WireAdapterAdmissionTest.cpp`
`latchesBarriersAndExactFit`, `mapExtremaAndStructuralPackets`,
`revokeReconnectAndIngressBarriers`, `extremeCountersAndFloors`;
`WireAdapterSessionTest.cpp::retainedLatchAndReplay`; `WireAdapterStateTest.cpp`
state-transition cases. C0c generated-MAVLink conformance remains separately
accepted historical evidence; its source was not changed by P2T2/P2T3.

## Complete path and ownership, without inventing missing links

The stock source trace in ACCELERATION_INTERFACE.md remains incompatible with
three-axis FRD acceleration-only tracking. The planned research chain is one
host/Pi producer, authenticated frame decoder, serialized trusted state/clock
and supervisor owner, existing admission/guard, existing canonical accessor,
then a later feasible-objective/controller, normalized allocation and outputs.

Implemented/tested here: offline codecs/framing fixtures, injected state and
consent ownership, admission/accessor, status encoding and software math.
Missing links remain explicit: deployed parser-to-adapter attachment and clock/
state provenance, active controller, physical-to-normalized allocation mapping,
actuator path and validated plant. No reception or status bit substitutes for
tracking. No simulator ground truth enters an operational controller. Phase 4
must retain one publisher per command stage and explicit normalized semantics
at torque/thrust/allocator boundaries; no mixer is inferred from an X layout.

## Next gate requiring human authority

The current approval expressly excludes live I/O, test/deployment keys and a
clock provider. A useful next step is the tightly bounded host-loopback release
proposed in PHASE_2_LIVE_HOST_PROPOSAL.md, not an active PX4 attachment.
Until that scope is approved, do not open sockets, provision keys, select a
live clock provider or promote synthetic state/authentication to real authority.
No new geometry is needed for that decision.

Full Phase 2 still needs justified operational control/link budgets and applicable
contract signoff. Plant/controller implementation is not a circular prerequisite
to Phase 2 design, and synthetic tests are not a substitute for its evidence.
Later live PX4, plant, active control, board/hardware and physical operation
remain separate gates. No commit, push, PR, package installation or hardware
operation was performed in this campaign.
