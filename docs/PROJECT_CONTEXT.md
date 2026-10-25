# SLAAPS implementation: start here

Updated 2026-10-24 by Codex. Software progress is not flight acceptance.
Repository: https://github.com/TheLettuce2023/SLAAPS-PX4; branch `slaaps-sitl`.
Committed, not pushed: power `58f4be89fa`, diagnostics `54b2da3c6b`.
Companion/reserve/provenance integration is uncommitted. Preserve user AGENTS.md and `timestamp`.
[Current evidence](SLAAPS_CONFIGURATION_INTEGRATION.md#october-24-acceleration-diagnostics-compiled-lifecycle-regression-under-investigation) retains exact overlays, hashes and failures.

Signed UDP/parser -> active acceleration -> both captured banks and bidirectional Manual handoff pass.
Authority/sensors/configuration are synthetic; ordinary flight admission and physical qualification remain absent.
Last full native matrix: `50clq98f`/runtime `iz6yyzeg` PASS75a4019c (predates authority diagnostics).
Diagnostic production: `impnyvop` LINK PASS; observation `987m0uek`/runtime `ryjlhws3` signed PASS.
Prior runtime `4r3fx80u` pilot-expiry failure remains unexplained; source permits bounded map-renewal ACK wait.
New fixture checks fresh handled output after pilot expiry within the original lease, not instantaneous RUNNING.
Diagnostic fixture `mq082hsd` LINK PASS26387 (manifestc71ebdc5/binaryde4c8509); `ns1d23at` FAIL88605.
Failure:7303 epoch12/map10 barrier211 absent from received wire; client correctly withheld ACK.
Retry100ms cannot rescue first-barrier loss within the outgoing100ms lease; initial drop location unproven.
Input-config agent implements isolated barrier topic alias; manual-entry agent reviews sender/lifecycle guards.
Review fixes landed: pin before no-space wait; preserve cached revision across valid new-binding retry. Independent review clear.
Priority native `u5sxsmx9` LINK PASS38799; runtime `ws3sj92x` C++ scenarios pass but final Python acceptance-status assertion FAIL76388.
Actual signed request16 reaches allocation rejection/QGC log; investigate final queued status versus immediate revocation.
Test-only observer after real guarded capture will prove acceptance directly; do not permit post-revocation TX for test receipts.
Bounded latest queue drain, one priority attempt within8 total; preserve exact payload/original expiry on retry.
No wire, lease or producer-retry changes. This increment is excluded from the sealed diagnostic board.
Board `acceleration-diagnostics-20261024` PASS5fb1bc0d;45 fresh/10 exact object-source-command reuses.
Image1,937,396/free28,684; AXI109116/SRAM42048. Static identified supervisor5516/6500,
companion4848/6500, MAIN2976/AUX2920 of4096, logger2232/3700; runtime/total stack and boot unqualified.
Diagnostic topic separates calculated vs exact all-bank-handled evidence; generated formats596/1440 (<1450).
Host `mq082hsd` suite `y0x_1zx3` PASS36101: Debug/Release/ASan-UBSan,473 parameters; receipte60ee8f6.
WSL recovered;11019 left no verifier/compiler. Old0u4fbgjq px4-param401 remains untouched; vsock/VMBus faults observed.
Priority board37324 PASS (image1,937,860/free28,220; receiptaf1e38c4 verified); resource79699 active, board owns slot.
Current schema: 473 parameters / 463 profile fields. Physical qualification remains absent.

Earlier unexplained failures remain in the evidence log; current passes do not establish their causes.
NuttX RNG fixes remain dirty; user approval for a TheLettuce2023/NuttX fork/gitlink is pending:
`build/slaaps_recovery/nuttx-rng-20261024/commit-review.patch`.
Manual flight admission, cold acceleration startup and measured vehicle qualification remain open.
Profile declarations now reuse apply/fingerprint/persistence; Qualified grants no authority.
SLA_MAN_WAIT fixes a 1–60s non-actuating window at entry; READY response remains <=1s.
Initial activation/first command stay inside that window; running leases remain independent.
Delayed proposals cannot extend it; expiry requires deliberate re-entry. Flight flags stay denied.
Mode names/custom IDs, wire selection, CURRENT_MODE, removal/restore pass; GUI and flight permission remain unqualified.
Only five production registry files are staged (74 insertions/41 deletions); not committed, pending DCO review.
The earlier six-file review is not standalone; the harness and other work remain unstaged.

## Navigation

Read [AGENTS.md](../AGENTS.md) and matching scoped/nested instructions before edits.
Use relevant entries in [RESEARCH_CODE_GRAPH.json](RESEARCH_CODE_GRAPH.json);
revalidate affected anchors including untracked files, not whole historical graphs.
Historical startup context: [October 22 archive](PROJECT_CONTEXT_ARCHIVE_20261022.md).

| Task | Entry / graph nodes |
| --- | --- |
| Current integration/evidence | [Diagnostic checkpoint](SLAAPS_CONFIGURATION_INTEGRATION.md#october-23-actionable-configuration-and-routing-rejection), [scheduling milestone](SLAAPS_CONFIGURATION_INTEGRATION.md#october-23-scheduled-manual-control), [goal](SLAAPS_AIRFRAME_GOAL.md) |
| Manual activation/output | `slaaps_preparation_coordinator`, `slaaps_preparation_endpoint`, `slaaps_manual_worker_transport`, `slaaps_manual_bank_transport`, `slaaps_production_mixer_preparation` |
| Shared configuration/QGC | `slaaps_configuration_api`, `slaaps_parameter_capture`, `slaaps_parameter_schema`, `slaaps_parameter_metadata`, `slaaps_routing_core` |
| Scheduling | `slaaps_control_schedule`, `slaaps_operating_condition`, `slaaps_operating_condition_observation`, `slaaps_schedule_portable_evidence` |
| Verification | `slaaps_native_build`, `slaaps_native_test`, `slaaps_board_builder`, `slaaps_board_verifier` |
| Commissioning | [EDF milestone](SLAAPS_CONFIGURATION_INTEGRATION.md#october-23-armed-edf-commissioning), [R09/R10 report](MANUAL_BENCH_R09_R10_OPERATOR_REPORT_20261020.md) |
| Companion integration | `slaaps_companion_protocol_owner`; [acceleration contract](ACCELERATION_WIRE_CONTRACT.md), [state contract](C0B2_STATE_CONTRACT.md); `wire`, `adapter`, `guard`, `clock` |

## Objective and boundaries

Deliver selectable stabilized manual, companion acceleration and shared bench
commissioning, with separate disarmed/armed state. Finish usable manual first.
Four X-canards and a fixed rear EDF; no thrust vectoring or main wing.
Manual means body-rate sticks, quaternion hold on centering and pilot EDF throttle;
it does not provide altitude/position hold. Preserve nose-up/body conventions.
Acceleration means inertial acceleration at CG in rotating body FRD, m/s²,
body-fixed between updates; never reinterpret as specific force or direct actuators.
Software implementation is authorized, not flashing, actuator/EDF operation or flight.
Do not invent measured coefficients, ground-spool authority or airborne recovery.
Bench is never an airborne fallback or an entry from active flight. EDF commissioning
requires deliberate visibly armed operation, bounded command/duration and stop/kill.

## Actual production state

Dedicated airframe22001, board target/startup, external mode registration and Commander
guard exist. Configured Bench can advertise availability; manual/acceleration flight
availability remains zero. Do not call registered names operational flight modes.
Supervisor calls disarmed preparation, armed activation, worker begin/cycle and exit.
Both fresh arming publications must agree within original preparation deadlines.
RUNNING requires every bank's exact first handled ACK. Routing, configuration, pilot,
estimator, ownership and publisher identities remain bound through each session.
Normal disarm stops; faults require disarmed reapply and deliberate mode exit/reentry.
Restored inputs, kill release or reapply alone cannot revive a rejected session.

MAIN/AUX mixer endpoints consume calibrated canard/EDF commands through actual
PWMOut/PX4IO callback interfaces. Exact saved tickets gate handled ACKs; commands
cannot be written/acknowledged twice. Post-copy HRT caps original authority/report
expiry; no lease is renewed. Disarm/kill/lockdown/termination retain an explicit stop
frame until successfully handled. Armed authority loss latches RecoveryRequired;
there is no invented airborne cut/hold-thrust policy. Native callbacks capture only.

One typed configuration and native QGC parameter metadata provide coherent inactive
apply, persistence and export/restore: mass/CG/inertia, geometry, servo conversion,
dynamics, controller, EDF curve, input mapping, timing and optional schedule.
473 parameters (463 profile fields) are supported in current source; this is not a custom QGC editor or GUI test.
Schema/record/assessment parameters verify neither external authenticity nor physical qualification.
Never retune/remap an armed controller. Supported production routing is MAIN/AUX PWM;
do not imply other boards/banks/protocols are implemented.

Scheduling uses an empirical 2x2 raw indicated-airspeed/normalized-EDF-command table.
Shared filtered interpolation updates PID gains and allocation effectiveness together;
feasible integral-output compensation preserves P/D continuity. No extrapolation.
Original sensor expiry caps commands even while ACKs are pending. Invalid confidence,
source replacement, density/voltage/coordinate bounds, transients or matrix/gain
transitions reject operation. Prearm checks the initial zero-EDF point/rank without
activating propulsion. Raw IAS is not local canard flow; fallback temperature and
computed density are not measurement qualification. Missing flow-angle/wash and
zero-airflow ground-spool/takeoff acceptance remain real vehicle dependencies.

Shared Bench supports individual/combined canards and explicit armed EDF commissioning
using the same calibration, owner, routing and conversion as flight. EDF scope2/target5,
zero-default SLA_EDF_BMAX, explicit enable edge, low raw throttle, first handled zero,
capped slew, parked canards, immutable duration and ordinary disarm are integrated.
SLA_BENCH_EN/CH/SEC control sessions; saved enable never restores permission.
Servo-only Bench inhibits propulsion. Legacy separate research bench stays disabled.
Stock FW/MC controllers and hover estimator are disabled/not started. Stock allocator
is compiled for metadata only, not started as another actuator owner.
Companion numerical control, EDF transfer and worker/caller handoff pass native synthetic tests;
cold startup, ordinary flight admission and current target refresh remain incomplete.
Verified receipt transport uses repository-owned `src/modules/mavlink/slaaps.xml`
(private IDs200010/200011/200012, inherited development dialect), selected only by
the three SLAAPS targets. Receiver publishes original bounded payload and actual
signature-verification evidence on four queued per-link topics, with signing
generation and event sequence. Authentication is not session or output permission.
`research_acceleration` is linked through the production CompanionProtocol owner.
Explicit preparation, matched signed TIMESYNC, frozen bounded maps, session grant,
exact barrier acknowledgement, revoke and deliberate re-entry are integrated.
Source/key/publisher changes and original map expiry cannot revive a session.
The controller and exclusive active handoff pass native capture tests; cold acceleration entry remains incomplete.

## Next three tasks and remaining dependencies

1. Complete acceleration authority diagnostics from port through worker/supervisor to logged status/QGC text.
   Verify a valid signed but infeasible demand and calculated-vs-handled ACK evidence; preserve admission behavior.
2. Normal EDF Bench arming already exposes its readiness gate; the native test lacks raw sensors
   and avionics system_power. Preserve common checks; positive vehicle-health acceptance remains open.
3. Close stock QGC operator workflow, working-base boot integration, runtime timing/stack
   and physical acceptance, with separate authorization for any hardware operation.

Still unavailable: actual mass/CG/inertia/CAD, signed effectiveness across qualified
flow/wash, loaded servo dynamics, EDF thrust/response/voltage measurements, authority
acceptance and recovery hardware/policy. These questions are pending; do not repeat
unchanged requests or invent fallback values. Physical power-feed assignment/voltage
bounds, propulsion response, landed/takeoff behavior and EDF ceiling/duration need acceptance.
Preserve working e8ea98b404/NuttX11 servo baseline. Newer-base boot issue is unresolved;
R01-R10 operator servo passes are not numerical flight calibration or exact provenance.
Use WSL Ubuntu-24.04 and existing Tools/slaaps workflows. Subagents use Sol xhigh.
Request v2/flags1 supplies absolute NED-from-body WXYZ; v1 remains acceleration-only; session/status stay v1.
Feedback uses f_IMU + R(q)^T*g - alpha cross r - omega cross(omega cross r); EKF2_IMU_POS is CG-to-IMU.
Numerical control uses scheduled PID and coupled physical wrench allocation with current EDF fixed in the applied solve.
Future EDF target never counts as achieved thrust; original command/map/link/state bounds cap output leases.
Pending numerical state commits only after exact all-bank handled ACK time, without invented initial trim history.
Cold acceleration requires genuine first handled output and an accepted ground-support/spool/takeoff model
or measured feasible zero-EDF condition; a zero-output ACK alone cannot supply missing ground-support force.
No automatic airborne fallback, transfer low-throttle reset, stale demand reuse or lease revival is authorized.
Capture-boundary disarm and fault precedence now pass focused native regression, including actual active stop/profile restore.
Historical failures and full hashes remain in the integration evidence record; current PASS does not explain all prior failures.
Diagnostic board/build/runtime jobs are terminal; no compiler owns the slot. Do not overlap runtime with builds.
Keep this chat while authority-diagnostic implementation is active; commit/NuttX approvals are unchanged.
