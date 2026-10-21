# Stabilized manual: production ownership boundary

2026-10-20, Codex. Implementation decision record; not hardware acceptance.

## Current source facts

- `ResearchStabilizedManual.cpp` owns an observation-only core and publishes only
  diagnostics. Its CLI cannot enable actuator output.
- `SelectedStateInput.hpp` requires fresh disarmed status, no kill/lockdown/
  termination/ESC calibration, one estimator and coherent publisher lifetimes.
  Its additional servo prerequisite requires prearmed status. It is explicitly
  not a flight-health policy. `SelectedStateGuard` adds numerical/manual input
  validation, centered-stick admission and latched continuity checks.
- `GrantHandshake.hpp` confirms a bounded request against coherent response and
  request/response/command/ack publisher lifetimes. It does not grant vehicle
  permission or authenticate a publisher.
- `ResearchStabilizedOutput` is a MAIN PWM adapter with immutable calibration,
  exclusive-bank checks and validation immediately around driver completion.
  Its local armed-or-prearmed check is broader than the selected-input adapter;
  neither check may be silently removed to make flight mode work.
- `StabilizedOutput` maps four canards. Fixed simulated EDF thrust belongs to
  the fixture and does not define a hardware propulsion contract.
- The qualified SITL producer/consumer implements lifecycle orchestration but
  depends on fixture permission, Gazebo transport and synthetic profile values.
  Copying that predicate into production would not establish vehicle admission.

## Hardware scope decision pending

The user was asked whether the first hardware implementation is restrained
canard stabilization with EDF disabled, or flight-capable control with propulsion.
Earlier choices settled only the synthetic demonstration. Do not infer this
hardware decision from the 10 N simulated EDF value or from an elapsed wait.

Restrained work can retain the disarmed/prearmed servo prerequisite and external
restrained airflow. It still requires measured authority, calibration, physical
output timing and reviewed permission/fallback limits before hardware acceptance.
Flight work additionally needs an explicit armed flight-health/mode policy,
propulsion input and allocation ownership, transitions to/from other flight modes,
and reviewed behavior on link, estimator, servo and propulsion failures. The
current four-canard sink and disarmed input adapter do not satisfy that scope.

## Shared production flow to implement

1. Load one immutable control/output profile while disabled. Bind a configuration
   identity to the exact admitted values; a filename or synthetic profile ID
   alone is insufficient. Reject replacement while grant/active/revoke is pending.
2. Attach the output transaction owner before the driver's first mixer update.
   Confirm a handled disarmed write and exclusive channel mapping before readiness.
3. Accept a deliberate operator request only with centered sticks, valid selected
   inputs, current reviewed permission, and confirmed consumer readiness. Capture
   all publisher lifetimes and a fresh monotonic session/deadline once.
4. Consumer independently validates permission, profile identity, lifetimes and
   deadline before grant. Producer re-copies the response and selected inputs
   immediately before starting its control session. No command establishes a grant.
5. Producer runs the rate/hold core on its own queue. Consumer services leases and
   physical driver transactions independently. Acknowledgments identify completed
   driver handling; they do not prove pulse shape or servo motion.
6. Permission loss, source replacement, expired data or failed output invalidates
   acceptance immediately. Revoke remains pending until a handled disarmed write.
   No implicit reacquisition or automatic fault recovery; restart requires a fresh
   explicit request after confirmed disarm and renewed admission.
7. Preserve owner lifetime across pending driver writes and producer restarts.
   Stop must complete disarm before deleting any object used by the mixer. Neither
   queue may assume that its peer is still being scheduled.

## Independent work and verification

Transport-independent session/grant orchestration and synthetic owner tests can
proceed before the hardware scope answer. Keep the already-qualified console and
binary available while integrating a new candidate. Exercise enable/revoke/restart,
profile replacement, source changes, driver failures and separately stalled queues.
Hardware policy-dependent branches remain pending the scope decision and evidence.

The research-image diagnostic retains required portable entry points with 18,120 B
flash headroom. This excludes owner instances, queues, permission/driver glue and
full active-path runtime costs. After integration, clean-build on the working
baseline and repeat resource, native and coupled SITL checks. Restrained physical
pulse/servo tests and downstream stall failsafe verification remain human actions.
No new code path may claim that a stalled output queue guarantees physical expiry.

Stock, observer, bench, acceleration and H10 gates remain separate. No hardware
operation, flashing, real propulsion command or automatic startup is authorized
by this decision record.

## Request lifecycle implementation

`src/modules/research_stabilized_manual/SessionRequestOwner.hpp` now implements
transport-independent Revoking/Ready/GrantPending/Active ownership. It uses the
existing grant confirmation validator and actual generated uORB schemas. Initial
readiness and fault recovery require explicit trusted local disarm confirmation
for the current session. Requests bind immutable configuration and all four
publisher lifetimes. Grant deadlines never refresh; after acceptance command
leases remain the separate session/consumer responsibility. Permission/source
loss and clock regression latch revoke. Session high-water survives restart.

The caller must still implement admission, transport, actual disarm observation,
controller disable and consumer revoke; this helper does not perform those actions
or grant physical authority. It is not yet wired into production or the qualified
SITL candidate. Next integrate it into a separate candidate owner and exercise
real grant/revoke/restart traffic before promoting any orchestration change.

Host tests in `Tools/research_sitl/session_request` pass 59 checks each in debug,
optimized and address/undefined-sanitizer builds. Evidence:
`/var/tmp/research-session-request-po1gh5eq`, manifest
`5cd74c636a67ddceab71dd4e62208cc728e5b0012152e79e993077954d094818`.
Tests cover initial disarm, admission rejection, frozen request, stale/mismatched
grant, permission loss, source replacement while pending, explicit restart,
clock regression, exclusive deadline and near-integer-limit session handling.
These host checks do not establish scheduler or physical-output behavior.

## First running request-owner candidate

The private `Px4SessionSITL` producer now uses `SessionRequestOwner` for request
creation, deadline/source checks, grant acceptance and revoke. Readiness/reset is
admitted only after the actual consumer reports Idle following confirmed plant
disarm. The existing second coherent response/input copy remains before core
begin. Controller faults and stop revoke the request owner. The helper persists
across explicit restart; its session high-water is not reset. This is still the
synthetic fixture's permission policy, not production hardware admission.

Fresh build `/var/tmp/research-stabilized-sitl-0o5n1g_b` passes link, registration
and plugin checks, with 58,133 staged inputs, 676 source anchors and 253 sealed files.
ELF SHA-256 `8495c11d2452c86ef22f1b7c28f6d6d987850a9377425c99651f716bf0d5656b`.
Build receipt `de24b85bd0fb41383efa7da8c3b3d0514d3cc0375f4e0ef1d68c344ed696dc4f`.
The prior aborted preparation `research-stabilized-sitl-5bp0woul` was rejected by
the stale producer graph fingerprint; it was refreshed before the fresh build.

Four actual two-queue runtime checks pass, including fault latching, ordinary
re-enable rejection, explicit new-session restart and final zero plant targets:

| Case | Evidence directory under /var/tmp | Receipt SHA-256 |
| --- | --- | --- |
| Withheld disarm confirmation; restart rejected until restored | research-stabilized-session-runtime-aporvotp | 1ad068ef35794eab4bd914cbc124731273a3ae7cee010da4a0ab369544c28b61 |
| Lost grant reply; bounded wait without commands | research-stabilized-session-runtime-sekr1sl5 | bdcf2735d209e92d3516fd9cd060d638c9007daf32b980aecef77519a330a8d1 |
| Identical-payload command publisher replacement | research-stabilized-session-runtime-6rzgogto | d25c4679cf4d1a8dcba45c172697ec5398f28562c4e0cceb9c6366dc4a5a03c7 |
| Producer queue stalled; independent consumer faults | research-stabilized-session-runtime-99g86pjc | 31e29b19798e0f4d20eed7c3110b4ae8bd54a03cddcbf00e3407ccd5de023032 |

These are focused runtime checks, not a new 30-case qualification or console
promotion. Next run the complete qualification and actual console on this binary.
The previous qualified candidate and console remain preserved. Production owner,
hardware policy, mixer attachment and complete ARM resources are still open.

## Full request-owner qualification

The same fresh binary subsequently passes all 30 integrated cases and eight actual
console checks. Qualification `research-stabilized-session-qualification-_e84t5eb`
receipt `62722f917fdf5a048b2f5a5da014c754dc449fd127a8d2c7f58fb830ff2d8209`;
console `research-stabilized-session-console-check-uk_m1tzr` receipt
`86e11af05ca16569bed94d2dcf03eb7376b901fe8af464cd74f972d2b54d6093`.
All 786 campaign artifacts rehash and 24,311 maneuver cycles pass the independent
audit. This advances the integrated synthetic candidate only; production hardware
scope, permission admission and mixer attachment remain open. Operator instructions
now reference this candidate; previous and legacy candidates remain preserved.

## Native mixer admission with shared request owner

The native `ScheduledOutputTest` supervisor now uses `SessionRequestOwner` for
positive initial grant creation and coherent grant acceptance after the actual
consumer confirms disarm. Its raw publisher remains a distinct negative-test
injector for expired, mismatched and competing requests. The shared helper does
not replace those negative cases or claim ongoing production policy ownership.

Fresh working-baseline native evidence:
`/var/tmp/research-stabilized-output-native-request-owner-20261020`.
All 45 scenarios pass twice: 90 runs, 11,412 assertions and 2,771 handled acks.
The same binary passes the existing output-disabled observer regression; host
suites pass in debug, optimized and address/undefined-sanitizer configurations.
All 641 sealed files independently rehash. ELF SHA-256:
`024373dcf0c9ae0aa48082ad490dc93ba0360602c8efc7a176e51ff404269512`.
Manifest SHA-256:
`0e726563cdb336755efbfb737fcfc5735aa0337159be1635cbb7dc274f9fbd64`.

This exercises the real uORB/work queues, mixer adapter, transaction guard and
candidate provider with synthetic inputs and a memory sink. It does not attach a
production hardware driver, measure physical pulses, or add a flight/EDF policy.
The prior native receipts and current coupled/console qualification are preserved.
Remaining work is production scheduling/permission ownership and actual driver
attachment within the requested hardware scope, then complete ARM and physical
acceptance; the scope question and unavailable vehicle measurements remain pending.

## Goal completion audit at the current handoff

| Requirement | Current evidence | Disposition |
| --- | --- | --- |
| Body-rate sticks and quaternion hold on release | Current control cycle captures attitude on Rate-to-Hold; 16 maneuvers and actual console checks | Verified for declared synthetic fixture |
| Isolated closed-loop SITL with stated plant/control/allocation inputs | 30-case request-owner qualification, prescribed 20 m/s airflow and 10 N simulated EDF, retained profile/plant/binary identities | Verified for finite declared cases; no measured vehicle claim |
| Explicit grant, fault latch and restart/disarm lifecycle | Shared owner host tests, two-queue qualification and native mixer admission/regressions | Software evidence verified; production owner not installed |
| Production mode and physical output control | Observer still diagnostic only; research output hook disabled in new board variant | Incomplete; hardware scope/permission policy decision pending |
| Vehicle-specific control and allocation admission | No CAD/inertia, signed authority or loaded servo measurements supplied | Incomplete; human measurements required |
| Full active firmware and runtime resources | Clean observer research build; diagnostic portable-entry-point link has 18,120 B spare | Partial; full owner/driver integration and runtime stack/latency unmeasured |
| Stock baseline and bench/acceleration gates preserved | Current diff has no changes to stock fmu-v6x default, linker config or ROMFS; separate research variant; parent artifacts rehashed | Preserved; physical G4-G10 and H10 disposition unchanged |
| Human inputs identified | Hardware scope choice, vehicle measurements, timing instrumentation and reviewed permission/fallback policy documented | Identified, not yet received or accepted |

The full goal remains incomplete. The next production implementation depends on
the unanswered hardware-scope decision; do not infer it from automated goal
continuations. Measurements remain unavailable as already reported by the user.
Do not generate additional passing synthetic cases merely to avoid this decision,
or describe the observer/resource-probe image as an active hardware mode. Resume
policy-dependent ownership/driver implementation after the scope answer, while
retaining the current qualified software and the separate physical admission gates.
