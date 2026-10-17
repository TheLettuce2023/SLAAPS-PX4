# P2T3: offline acceleration-only contract and policy review

Status: **P2T3 bounded contract-test/policy-review gate PASS** (2026-09-19),
including master and independent source review. This does not mean a residual
attitude controller has been implemented or tested.
The user-approved [offline profile](PHASE_2_SITL_PROFILE_PROPOSAL.md) supplies
the policy. This increment adds payload-boundary tests and declarative review
cases only. It implements no residual-attitude resolver, feasibility model,
controller, limiter, physical fallback, or output publisher.

## Executable acceptance criteria

`Tools/research_sitl/profile/test_policy_contract.py` calls the existing
`Tools/research_sitl/wire/codec.py`, without changing it or using its private
field lists as the expected schema. The independently transcribed v1 contract
provides the request fields and byte offsets.

- Preserve all three finite signed/zero FRD acceleration components and every
  metadata field. The wire tags remain FRD, inertial acceleration and CG.
- Reject added heading, yaw, attitude, rate, position, velocity, speed, thrust,
  torque, actuator, specific-force and force objectives. Also reject each
  objective substituted for any required acceleration component. Session
  administration cannot carry these extra objectives either.
- Reject every other uint8 frame, quantity and reference-point tag through
  both encoding and independently mutated payload decoding.
- Reject absent/non-finite acceleration components; there is no unused-axis
  NaN convention. Exercise non-finite bytes without passing the encoder first.
- Reject nonzero flag/reserved tail bytes and appended payload bytes in request
  and session messages. Explicit zero does not make an extra objective legal.

This establishes structural payload constraints only. A sender's scalar values
cannot prove its physical intent, units or CG calibration. A valid codec payload
is not an admitted command, an authenticated sender, a feasible acceleration,
or evidence of a live PX4 consumer. Session metadata remains administrative.

## Existing executable body-held evidence

The body-held rule is `a_N(t) = R_NB(q_current(t)) * a_B`: current estimated
attitude transforms the same held FRD components on each evaluation. There is
no arrival-time world-vector cache in this policy.

`src/lib/research_acceleration/FrameMath.cpp` implements
`heldInertialAccelerationFrdToNed` by calling the real checked FRD-to-NED
quaternion helper with its current-attitude argument.
`src/lib/research_acceleration/FrameMathTest.cpp` already exercises
`fixture_held_identity_status`, `fixture_held_rotating_status` and
`fixture_held_frd_changes_ned_with_attitude`: one synthetic `(4,0,0)` FRD vector
becomes `(4,0,0)` NED at identity and `(0,0,-4)` NED nose-up. Its separate
`fixture_nose_up_frd_positive_x_is_ned_up`, invalid-quaternion and gravity tests
retain sign, validity and inertial-versus-specific-force coverage.
These are math-helper regressions; no Python copy of that math is introduced.
The [P2T1 record](PHASE_2_PROFILE_VERIFICATION.md) records those inherited tests
in its complete host suite. Current-suite reproduction is recorded separately
from historical evidence when the integration lead runs it.

## Declarative residual-attitude review fixtures

These finite cases express policy obligations for a later plant-supported
resolver. They are not executable tests, measured feasible candidates, or
claims that any physical response has passed. A/B are abstract candidate labels;
comparisons are qualitative predicates, with no invented force, moment, angle,
rate, effectiveness, gain or tracking-error values.

| Case | Hypothetical evidence supplied by a future validated plant/resolver | Required policy interpretation |
| --- | --- | --- |
| P1: feasibility before tracking | A would track better but violates a joint actuator/envelope constraint; B is feasible and stabilizing. | Exclude A. Tracking cannot override feasibility. Any limitation/residual needs attributable evidence. |
| P2: stabilization before tracking | A would track better but cannot provide required stabilization; B is feasible and stabilizing. | Exclude A. Stabilization takes priority over tracking. |
| P3: minimum rotation preference | A and B otherwise satisfy the same feasible stabilizing objective; A requires less rotation with equal residual-axis rate demand. | Prefer A under the approved preference. No companion heading is introduced. |
| P4: minimum residual-rate preference | A and B otherwise satisfy the same feasible stabilizing objective; their rotation demand is equal and A requires less residual-axis body rate. | Prefer A under the approved preference. |
| P5: conflicting preferences or exact tie | Both are feasible/stabilizing; A rotates less but demands more residual rate, or both preferences tie. | The profile supplies neither weights nor a lexicographic tie-break. Defer the choice to plant-supported continuous control design; do not invent an ordering. |
| P6: no feasible stabilizing candidate | All supplied candidates violate required state, stabilization or authority conditions, or none can be established. | A future resolver must invalidate its executable objective and produce attributable unavailable-direction/authority evidence. No acceleration request is silently relabelled as feasible, no zero vector is asserted safe, and no actuator or flight-mode substitute is invented. |
| P7: feasible limited alternative | Exact acceleration tracking is unavailable but a stabilizing limited objective is supported by a future approved plant and limiting policy. | Limiting requires explicit residual/status evidence and that approved limiting policy. This document selects no limited vector or actuator command. |

P6 concerns the future executable objective, not erasure of a still-valid input
merely because a foreign or malformed packet arrives. Existing adapter/guard
eligibility and preservation rules remain intact. Its injected required-state
Boolean cannot establish aerodynamic feasibility or verify P6 physical behavior.

The accepted preference is minimum rotation/minimum residual-axis rate among
feasible stabilizing options. Rotation-versus-rate tradeoffs, ties, continuity,
unavailable-direction detection and actual limiting remain unresolved until
plant-supported control design. Near-vertical, zero, parallel and antiparallel
force cases still require that later design and tests. Declarative review
closes documentation of the policy boundary only; controller-policy execution
coverage remains deferred.

## Reproduction and evidence boundary

From the checkout root, using an existing Python installation:

```sh
python3 -B Tools/research_sitl/profile/test_policy_contract.py -v
python3 -B Tools/research_sitl/wire/test_codec.py -v
```

The new test has six unittest methods. Subtests isolate forbidden fields,
all uint8 semantic-tag alternatives, non-finite axes and extension bytes;
unittest method counts must not be advertised as physical scenarios.
No package installation, serial/socket I/O, simulator run, firmware build,
geometry change, uORB publication or hardware operation is required.
Full Phase 2 remains open for the remaining operational budget/source evidence
and design signoff; executable plant-dependent policy behavior belongs to the
later control gate.

Verification on 2026-09-19 using existing Ubuntu-24.04 WSL Python: all six new
methods and all eight unchanged codec methods pass. The new methods include
765 invalid semantic-tag values through each of encode/decode, 24 added-objective
cases across request/session, 36 axis-substitution cases, three absent axes,
nine non-finite axis cases through each path, 30 flag/reserved byte mutations,
six appended-payload cases and six signed/zero positive round trips.
`git diff --check` reports no whitespace errors. Windows `python` was unavailable;
the existing WSL interpreter was used without installation. No standalone
build or frame regression was run by this increment's author; integration
results must be supplied by the integration lead before crediting a fresh
complete-suite result.

Integration lead verification: all 17 standalone CTest targets pass in strict
Debug, Release repeated three times and ASan/UBSan. This freshly reruns the
actual frame/body-held tests and the six new policy-contract methods. Build/log
locations and suite hashes are in PHASE_2_STATUS_DELIVERY_VERIFICATION.md.
Policy-test source SHA-256:
`6eb50dab6c0333191a989cfe3efc1a4cefde44cfd27db062fd6fbf045ab1618e`.
The seven declarative policy cases were reviewed as obligations only; none is
counted as executed physical feasibility or controller-selection evidence.
