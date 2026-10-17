# Research objectives and human-decision checkpoint

Review date: 2026-09-20. Scope: accepted C1H host-loopback and now-approved C1
output-disabled PX4 SITL attachment; no active mode, output or hardware deployment.

## Current decision and next boundary

Latest direction: continue geometry-independent development until physical
information is actually required. O2 offline owner/facade implementation is
accepted and did not need CAD, inertia, aerodynamic or servo measurements.
Do not re-request geometry for software-only interface/tests. Keep the G1
physical blockers recorded for plant/control promotion; do not treat them as
a blanket halt on independent software work.

The user approved design of a host-local SITL operator console for
independent session establishment, revocation and latch clearing. It cannot
arm, select a mode, command acceleration/actuators or approve hardware authority.
C0b admission/status implementation has passed; see PHASE_GATE_STATUS.md.

The user subsequently selected option 1: cooperative trusted-local SITL.
The O1 offline broker core and tests are implemented; see PHASE_GATE_STATUS.md.
O2 offline owner/facade and C0c generated C/Python wire conformance have passed.
The user approved PHASE_2_SITL_PROFILE_PROPOSAL.md for offline SITL testing only.
P2T1 timing/admission/accessor, P2T2 status-delivery/diagnostic and P2T3 payload
contract/policy-review gates have passed at their bounded scope. The complete
17-target suite passes Debug, repeated Release and sanitizers; see
PHASE_2_OFFLINE_COVERAGE.md. Synthetic targets remain non-operational and the
residual-attitude review is not a controller implementation.
The user approved PHASE_2_LIVE_HOST_PROPOSAL.md. C1H now passes at its bounded
scope: four sequential host campaigns with real signing/parsing, native request
access and authenticated returned status. Prior timing failures and the reviewed
bounded GC scheduling correction are retained in C1H_HOST_LOOPBACK_VERIFICATION.md.
The latest "Proceed" approves C1_PX4_ATTACHMENT_PROPOSAL.md. C1A implements
its first passive PX4 state/clock slice, now passing fresh build and two-run
verification. No research admission owner, receiver, controller or outputs
were instantiated; this is not full C1 or Phase 2 acceptance.
The user explicitly approved design of that separate SITL diagnostic state.
C1B_SITL_DIAGNOSTIC_SELECTION_DESIGN.md and its test specification now have
master/independent architecture acceptance. They preserve flight Selected and
all estimator/clock checks, require independent operator selection and prevent
eligible demand. No code or live diagnostic branch is released by this design.
The subsequent C1B-I exact offline API/barrier/evidence review now passes master
and independent review; see C1B_OFFLINE_API_CONTRACT.md and its finite matrix.
No code or runtime evidence was added in that design increment. The subsequent
C1B-G private guard implementation now passes Debug, repeated Release,
ASan/UBSan and independent review; see C1B_GUARD_IMPLEMENTATION_VERIFICATION.md.
Next is diagnostic single-adapter selector/barrier integration, then owner/
broker and full evidence tests under the existing software authorization.
No further design approval, geometry or objectives clarification is needed for
that offline work. Live state/clock/operator-loss provenance and diagnostic
wire/profile binding remain open. No active flight controller is claimed.
This profile does not protect against hostile same-user processes;
neither localhost nor source/session IDs authenticate an operator. Runtime
ingress, clock/state provenance, operational timing and live-link authority
remain separate deployment gates, not permissions inferred from option 1.

C1H parser authentication and host clock observations are real, with independent
cooperative operator actions; vehicle state remains explicitly harness-simulated.
The explicit C1 approval does not qualify state/clock/authority deployment. No operational
timing, gain, effectiveness or physical fallback value is chosen from a fixture.

The user explicitly confirmed both interface conventions during this review:
acceleration is referenced to the vehicle CG, and the vector remains fixed in
the rotating body FRD axes between updates (until superseded or expired).
They are now confirmed requirements, not provisional master assumptions.
A world-fixed vector would be a different hold rule and may not be silently
substituted. Actual CG location and IMU lever arms still require physical evidence.

## Existing objectives retained

- Pure research use; no intended interception.
- Custom vehicle with four independently actuated X-pattern canards, one rear
  fixed-axis EDF, no thrust vectoring and no main fixed wing.
- Companion-guided mode accepts only inertial acceleration expressed in FRD.
  NED is the world convention; air-relative velocity is a state quantity, not
  an additional companion command in this mode.
- PX4 owns estimation, stabilization, allocation, mode supervision, output and
  command-loss behavior. The companion does not command servos or EDF directly.
- SITL uses a host guidance script/MAVLink; eventual Pi 4B uses a translator and
  TELEM serial. Planned sensors are Pixhawk 6X onboard sensors, GNSS and a
  DroneCAN pitot. A scalar pitot is not a full air-velocity vector.
- Intended launch is nose-up (+X upward), release from rest under EDF power;
  added launch speed is considered only if validated results require it.
  Recovery intent is headfirst net entry after slowing. Neither is validated.
- Simulator truth is for independent analysis/scoring, never an operational
  controller input. Preserve stock Cessna as a separate baseline.

The latest user-directed provisional total mass is 3.041337 kg including the
canards and EDF rotor, superseding the older approximately 2.5 kg estimate.
The expected 5.5 kgf static thrust remains unmeasured. Neither is an operational
controller constant or validation of the new SDF inertia.

## Decisions needed at later gates

| Gate | Human-owned decision or evidence | Development approach until then |
| --- | --- | --- |
| Interface/deployment approval | Choose actual command rate, validity/age/jitter/uncertainty budgets and permitted producer/link identity. Approve authority, signing-key and clock-service procedures. CG/body-held semantics are already confirmed. | Verify configurable offline contracts without deployment defaults or live keys. |
| Plant-supported control design | Approve initial test objectives and feasible envelope; review actual mass/CG/inertia, geometry, local flow, EDF/servo behavior and uncertainty. Select quantitative tracking criteria and residual-attitude priorities with the controls lead. | Inventory/review supplied CAD first; request only evidence not established by those files or measurements. No guessed mixer/gains. |
| Active output integration | Approve responses to command loss, invalid estimates and loss of actuator authority, including transitions and abort/recovery behavior. | Revoke software eligibility in offline tests, without presenting that as a physically safe fallback. |
| Physical validation | Human test lead approves test area, containment, procedures, success/abort conditions and readiness. | No flashing, powered test or flight is authorized by software progress. |

The geometry folder now contains CAD plus an SDF and three meshes. The new
Gazebo inputs have been audited without modification; see
GAZEBO_GEOMETRY_INTAKE_REVIEW.md. Installed WSL SDFormat 14.9.0 rejects the
base inertia. The immediate remaining physical input is the original CAD
inertia/CG report with units, axes/reference point and component scope; the
mass assumption alone cannot correct an impossible inertia tensor. Package
layout repairs and other offline development do not require a new mass guess.

## What is not a human blocker

Ownership of an internal guard/configuration pair, checked integer arithmetic,
local counter reservation, explicit duplicate-event handling and an executable
test oracle are engineering design/review tasks. They can be resolved now.
Their software acceptance must not be reported as operational authorization,
flight-control feasibility or completion of the roadmap.
