# SLAAPS implementation: start here

Updated 2026-10-25 by Claude. Software progress is not flight acceptance.
Repository: https://github.com/TheLettuce2023/SLAAPS-PX4; branch `slaaps-sitl`.
Pushed through `e908e64f1b` (registry, stream IDs, companion integration, docs). Preserve untracked `timestamp`.
Recovery policy DECIDED 2026-10-25 by project lead: [decision record](SLAAPS_RECOVERY_POLICY.md); not yet implemented.
[Current evidence](SLAAPS_CONFIGURATION_INTEGRATION.md#october-25-ordinary-edf-bench-arming-accepted-with-synthetic-health-inputs) retains exact overlays, hashes and failures.

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
| Airborne recovery | [Decision record](SLAAPS_RECOVERY_POLICY.md); `EdfOwner`, `ManualOutputBank`, `SlaapsOutputPreparation`, Commander failsafe |
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
Do not invent ground-spool authority. Exception decided 2026-10-26 by the project lead: aerodynamic
slopes (canard force/moment derivatives, rear-fin stability) may be computed by the firmware from the
vehicle data sheet instead of being measured; computed values must be labelled as modeled, never as
measured. The EDF thrust curve is linear through the bench maximum. Other measured values stay measured.
Airborne recovery follows [the decision record](SLAAPS_RECOVERY_POLICY.md) only; its thrust/hold values
must come from data. The EDF uses DShot600, which routing does not yet support (PWM only).
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
frame until successfully handled. Armed authority loss still latches RecoveryRequired; the
decided recovery sequences (D1-D5) are not implemented yet. Native callbacks capture only.

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

## Next tasks and remaining dependencies

1. Complete acceleration authority diagnostics from port through worker/supervisor to logged status/QGC text.
   Verify a valid signed but infeasible demand and calculated-vs-handled ACK evidence; preserve admission behavior.
2. Positive EDF Bench arming is DONE natively with synthetic inputs (see evidence log, October 25).
   Isolated-supervisor barrier failure FIXED 2026-10-27: test defect, not product. The drain loop kept the
   revision from its final failed `copyQueuedBound` call, which clears revision by contract; payload and
   production MAVLink drain were correct. Full native verifier PASS on build `slaaps-native-xc32ipac`
   (binary bf9ae495, 6101 control checks); host suite PASS (4383/config). Physical health remains unqualified.
3. Recovery policy: D3/D4 library layer and transports done (see decision record status); supervisor wired and runtime-verified natively (recovery frames, frozen canards, disarm ends it); mode-left, power and estimator loss also enter recovery through the real driver path (stale-frame and early-expiry races fixed); next D1/D2/D5/D6. Original list: D4 60 s EDF-off ceiling, D1/D3 owned recovery sequences,
   D2 Manual handoff + QGC alarm, D5 armed Manual re-entry, D6 net-position parameters, closed
   Commander failsafe path. Resolve the record's open clarifications first where they change design.
4. Close stock QGC operator workflow, working-base boot integration, runtime timing/stack
   and physical acceptance, with separate authorization for any hardware operation.

Partial vehicle data is in `src/lib/slaaps/Master_Vehicle_Data_Sheet_Slaaps.xlsx` (mass 2.1 kg, CG 0.2716 m
from the tail end, inertia Ixx 0.0072 / Iyy = Izz 0.0595 kg m^2, max EDF thrust 4 kg-force, canard airfoil,
static rear fins). Not yet entered in firmware. Missing: CG Y/Z, IMU, per-canard and rear-fin geometry,
EDF test voltage, servo calibration/dynamics and power bounds.
Mapping: [data compilation doc](https://claude.ai/artifact/BQ6X9apsdjEQqcPPbP3dPJ).
Recovery hardware: net landing area near the GCS, an operator reference only (D6). Physical power-feed assignment/voltage
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
Only the decided recovery responses are authorized airborne fallbacks; transfer low-throttle reset,
stale demand reuse and lease revival remain unauthorized. D5 re-entry must start a fresh admitted session.
Capture-boundary disarm and fault precedence now pass focused native regression, including actual active stop/profile restore.
Historical failures and full hashes remain in the integration evidence record; current PASS does not explain all prior failures.
Diagnostic board/build/runtime jobs are terminal; no compiler owns the slot. Do not overlap runtime with builds.
Keep this chat while authority-diagnostic implementation is active; commit/NuttX approvals are unchanged.

## Inertia tensor (entered 2026-10-26)

Full tensor in `Tools/slaaps/parameters/slaaps_vehicle.params` (load in QGC, editable there):
Ixx 0.0072, Iyy = Izz 0.0595 kg m^2. Ixy, Ixz, Iyz = 0 by symmetry declared by the project lead; this is an
assumption, not a measurement. Replace with CAD or measured values when available. Firmware defaults stay 0
(unconfigured); the file is loaded by the operator, not compiled in.

## Rear fins (entered 2026-10-27)

Four static flat-plate fins (no airfoil), 90 degrees apart, rotated 45 degrees from the canards, area 0.0036 m^2 per fin (confirmed 2026-10-27),
COP 0.1516 m from the CG (sheet cell I4; the sheet has no IMU position, H4 is empty). Direction assumed aft of CG
(x = -0.1516 m body FRD): confirmed by the project lead 2026-10-27. EDF position from x_0 is 0 (sheet J4, tail end). Fins give no main lift; the
EDF carries the weight. Fin model not implemented; flat-plate slope will be labelled modeled.
Canards are control surfaces only, not weight-bearing (confirmed 2026-10-27); the 13 m/s lift check no longer applies.

## IMU position (entered 2026-10-27)

IMU is 323.75 mm forward of the tail end (x_0), i.e. 0.32375 - 0.2716 = +0.05215 m forward of the CG (body FRD x).
Vertical offset stated as "10 m up in the y": taken as a typo for 10 mm, pending confirmation. Up is -z in firmware
FRD (the lead's "y down" label is the same direction), so z = -0.010 m from the geometric axis; the CG's own
vertical position is still unknown, so the offset from the CG in z is unresolved. Lateral (y) offset: not given.
Not entered in firmware.

## Rear fin force model (specified 2026-10-27, per fin)

Per fin: `L_tail = q * S_tail_total * CL_tail_usable`, q = 0.5 * rho * V^2, CL_tail_usable = 0.1 (constant, labelled
modeled). Despite its name, S_tail_total is one fin's area, 0.0036 m^2 (confirmed by the project lead: the equation is
per fin; four fins). Each fin acts at its COP, 0.1516 m aft of the CG. Example at 13 m/s sea level: q = 103.5 Pa,
L = 0.0373 N per fin, moment about the CG = 0.00565 N m per fin. Not implemented. Open: force direction/sign versus
sideslip and angle of attack (CL is constant, so no slope), per-fin direction from the 90 degree layout rotated 45
degrees from the canards, and the air density source.
Engagement (project lead, 2026-10-27): two fins are engaged at any given time, each producing its force perpendicular to
the flow direction. Total at 13 m/s sea level: 2 * 0.0373 = 0.0746 N, moment about the CG 0.0113 N m. Open: which
two fins engage (selection rule from the flow direction) and the sign of each force.
Confirmed 2026-10-27: the two engaged fins are the two most crosswise to the flow (selected from sideslip and angle of
attack); force is stabilizing (pushes the tail back toward the flow direction). Air density: fixed 1.225 kg/m^3 sea level
unless changed.

## IMU and CG resolved (2026-10-27)

Confirmed by the project lead: IMU vertical offset is 10 mm up; CG is on the centerline (CG y = z = 0 on the
longitudinal axis). IMU offset from the CG in body FRD: x = +0.05215 m, y = 0 m (IMU on centerline, confirmed), z = -0.010 m.
Not yet entered in firmware.

## EDF test voltage and current (2026-10-27)

Thrust curve measured at 42 V (in-flight pack: two 5S LiPo in series, 10S), about 70 A draw (about 2.9 kW). Maps to
SLA_EDF_VOLT = 42. 42 V equals 4.2 V/cell, a full-charge open-circuit figure; whether it was the loaded voltage at the
EDF is unconfirmed. Not yet entered in firmware.
Clarified 2026-10-27: 42 V was the measured no-load, full-charge pack voltage. The thrust values were produced under
load sag from that starting point, so the curve is valid for a full-charge pack sagging naturally at about 70 A.
Loaded voltage not recorded. SLA_EDF_VOLT = 42 stands as the reference (no-load) voltage.

## Power range and spool (2026-10-27)

SLA_PWR_VMIN 30 V and SLA_PWR_VMAX 42 V (10S, 3.0 to 4.2 V/cell) added to the QGC parameter file; the 30 V floor is
my reading of "10S range". Spool time stated as near instant, ">100 ms" (likely under 100 ms): not entered.
SLA_EDF_RISE and SLA_EDF_FALL are normalized rates per second, so an under-100 ms full-range spool would be 10 per second or more.
Decided 2026-10-27: full-range EDF rise 0.4 s, fall 0.1 s, giving SLA_EDF_RISE 2.5 and SLA_EDF_FALL 10 (normalized per
second), added to the QGC parameter file. These are command slew limits, not measured physical spool.

## Canards and servos (2026-10-27)

All four servos identical. Neutral 1500 us; hardware travel 500 to 2500 us (+-90 deg, 11.1 us/deg). Vehicle limit
+-15 deg = +-167 us (normalized +-1); aerodynamic limit 12 deg = 133 us (normalized +-0.8, from stall). Slew 0.11 s per
60 deg = 545 deg/s = 36.36 normalized/s. PWM frequency 333 Hz. X layout, 90 deg apart, 40 mm from the centerline,
viewed from the rear with the nose pointing away: servo 1 top right, then clockwise (2 bottom right, 3 bottom left,
4 top left); outputs 1 to 4 on the Pixhawk 6X PWM rail (changeable in QGC). Entered in the QGC parameter file:
SLA_Cn_PWM_NEG/CTR/POS 1333/1500/1667, DEF_MAX 15, MIN/MAX -0.8/0.8, RISE/FALL 36.36. Not entered: slope sign,
mix coefficients, moment/force derivatives, trim, lag, latency, wash, output channel assignment, PWM rate.
Positive deflection sense (project lead, 2026-10-27): servo 1 counter-clockwise, servo 2 clockwise, servo 3
counter-clockwise, servo 4 clockwise. Viewing direction not yet stated. SLA_Cn_SLOPE not entered.
Canard offset (2026-10-27): the 40 mm is radial from the centerline. At 45 degrees in the X layout each canard
sits 28.28 mm over and 28.28 mm up or down: servo 1 (y,z) = (+28.28, -28.28) mm, 2 (+28.28, +28.28), 3 (-28.28, +28.28),
4 (-28.28, -28.28) in body FRD.
Viewing direction: from the outboard end looking toward the centerline. SLA_Cn_SLOPE = +0.261799 rad (15 deg) for all
four, so a positive command is positive deflection in the stated sense; the per-servo sense (1 CCW, 2 CW, 3 CCW, 4 CW)
must be carried by the moment and force derivatives when they are computed. Slope uncertainty (SLA_Cn_SL_UNC) unset.

## Modeled canard derivatives (computed 2026-10-27, labelled modeled, not measured)

Inputs: per-canard area 0.0506 x 0.0608 m = 0.003077 m^2 (chord x span, per canard assumed), aspect ratio 1.20,
finite-wing lift slope 2*pi*AR/(2+sqrt(4+AR^2)) = 1.742 /rad (all-moving surface, delta = alpha change), rho 1.225,
V = 13 m/s (q = 103.5 Pa), force along the circumferential normal to each canard (hinge radial, chord along x, flow
along -x), arm: 0.2334 m forward of the CG (direction unconfirmed), 28.28 mm over and up/down. Result: 0.555 N/rad per
canard; DFY/DFZ = +-0.3923 N/rad; DMR = +-0.0222, DMP = +-0.0916, DMY = +-0.0916 N m/rad, signs by servo as in the
parameter file. DFX = 0. Uncertainties (SLA_Cn_DUR/DUP/DUY) not set. Using the sheet's 2D-like slope (about 5.0 /rad
from Cl 1.05 at 12 deg) would scale every value by about 2.9.
Confirmed 2026-10-27: finite-wing slope (1.742 /rad) stands; canard CP is forward of the CG. CP is taken at the quarter
chord (NACA 0012), where the servo hinges sit, so aerodynamic force produces no hinge moment about the servo axis in
this model. Uncertainties still unset.

## Proposed uncertainties (2026-10-27, proposals; the lead may tighten them)

SLA_Cn_SL_UNC 0.05 rad (about 2.9 deg, 19% of the 0.2618 rad slope); SLA_Cn_DUR 0.0111 and SLA_Cn_DUP / DUY 0.0458 N m/rad
(50% of each modeled derivative). SLA_AIRSPD 13 m/s (the airflow the derivatives were modeled at). The 50% band does not reach
the sheet's 2D-like slope (2.9 times the modeled values); a measurement is needed to settle that. Firmware validation
requires a positive slope uncertainty below the slope and positive derivative uncertainties when no schedule is used.
Unset and not needed to validate: SLA_Cn_LAG, SLA_Cn_DELAY, SLA_Cn_WASH (stored only, no consumer found), SLA_Cn_TRIM
(0 is valid), SLA_Cn_BMR/BMP/BMY (proposal: sign of the matching modeled moment derivative, magnitude 1).
Needed to validate and still unset: the per-axis moment scale SLA_R/P/Y_M_SCALE, input slots and signs.

## Per-canard values entered (2026-10-27)

SLA_Cn_TRIM 0 (parameter, adjustable in QGC). SLA_Cn_WASH 13 m/s (local airflow is free stream; the 13 m/s figure is the
modeled reference). Derivatives stay modeled: no force balance is available. SLA_Cn_BMR/BMP/BMY entered per the proposal:
roll -1/+1/-1/+1, pitch +1/-1/-1/+1, yaw -1/-1/+1/+1 for canards 1 to 4.
Servo lag stated as 50 us with 20% error and delay as 8 us: not entered. Both are shorter than one 333 Hz PWM frame
(3000 us), so ms was probably meant; awaiting confirmation. There is no firmware parameter for lag uncertainty.
Confirmed 2026-10-27: servo lag 50 ms (20% error, recorded here only) and delay 8 ms. SLA_Cn_LAG 0.05 and SLA_Cn_DELAY 0.008
entered in the QGC parameter file for all four canards.

## Known values added to the QGC parameter file (2026-10-27)

SLA_MASS 2.1; SLA_CG_Y and SLA_CG_Z 0 (CG on the centerline); SLA_EDF_T0..T4 0, 9.8067, 19.6133, 29.42, 39.2266 N;
SLA_EDF_VOLT 42; moment scales SLA_R_M_SCALE 0.019, SLA_P_M_SCALE 0.077, SLA_Y_M_SCALE 0.077 N m (modeled authority
estimate, accepted as starting values). SLA_CG_X not entered: the parameter is measured from a "declared body FRD
reference" and the origin is undeclared (tail end x_0 would give 0.2716; the CG itself would give 0).
Not entered: IMU offset (no parameter found), roll/pitch/yaw input slots and signs.

## CG, IMU, sticks (2026-10-27)

Origin is the tail end (x_0): SLA_CG_X 0.2716. IMU uses stock EKF2_IMU_POS_X/Y/Z, defined relative to the vehicle CG: 0.05215, 0,
-0.010 m (confirmed in src/modules/ekf2/module.yaml). Sticks use the stock PX4 mapping: SLA_R/P/Y_INPUT 0/1/2, signs
+1/-1/+1 (stick forward is positive pitch in PX4, nose-up is negative; matches the research configuration, not verified on
a bench), SLA_THR_LOW -1 and SLA_THR_HIGH +1. All added to the QGC parameter file.

## DShot600 support: library layer (2026-10-27)

Done (native tests only, no hardware): `OutputProtocol::Dshot600`; `slaaps_output_bank.protocol` (0 PWM, 1 DShot, rate_hz is
kbit/s); `makeRoutingPlan` accepts DShot600 for the EDF only (servo on DShot, other rates, values outside the driver
range 1..1999, minimum throttle or high failsafe as the off value are rejected; stopped output is the disarm frame 0);
`assembleOutputInventory` maps a DShot bank at 600 kbit/s to Dshot600 and anything else to Unsupported.
The EDF command conversion already interpolates between the channel's reported minimum and maximum, so it needs no change.

Driver done (commit ebdb57d070, built for the 6X, not flashed or bench tested): `DShot.cpp` maps the SLAAPS EDF function (10104)
to motor slot 0, reports inventory with protocol 1 and rate in kbit/s, and sends no special commands (beep, EDT, settings) while
SLAAPS owns the channel. Still open: bench test of the DShot600 EDF output (needs separate hardware authorization) and the board
layout (Pixhawk 6X: EDF on a DShot600 timer group of PWM_AUX, servos on MAIN).

## Flash use (2026-10-27)

Trimmed `boards/px4/fmu-v6x/slaaps.px4board`: removed the uXRCE-DDS bridge (companion is signed MAVLink; ROS 2 not
required), camera trigger/capture/feedback (no camera), the landing-target estimator (needs the navigator, already off)
and the rangefinder drivers (no rangefinder, confirmed by the project lead). Kept DroneCAN (planned pitot), Septentrio,
GPS, magnetometers, RC protocols, airspeed sensors and power monitors. Image 1,948,884 -> 1,851,788 B (94.19%,
114.3 KB free). Not flashed. `Tools/slaaps/build_board_snapshot.py` still asserts its Micro-CDR donor mirror exists; it
is now unused but harmless.
Build result 2026-10-27: DShot600 commit ebdb57d070 compiles and links for the 6X in a WSL copy
(/var/tmp/slaaps-6x, CRLF cleaned there only). Not flashed, not bench tested.

## Flight-readiness increment (2026-10-27, goal: flight-ready SLAAPS airframe, all modes)

Recovery policy D1, D2, D4, D6, the Commander failsafe path, clarification 4, launch staging and production flight
admission are implemented; see the status block in [SLAAPS_RECOVERY_POLICY.md](SLAAPS_RECOVERY_POLICY.md). D5 remains.
New topic `slaaps_flight_status` (logged at 10 Hz) carries stage, launcher gate, recovery state, dropped banks and net
distance/bearing. `slaaps_status` is unchanged because the logger's format limit was already reached.
NuttX RNG/HSI48 fix (approved by the lead): local commit 9e4150bb7e on NuttX branch `slaaps/px4_firmware_nuttx-12.12.0+`,
gitlink committed in 269afdf220. Creating the TheLettuce2023/NuttX fork was blocked by the session's permission
classifier; the lead must create it, push that branch and point `.gitmodules` at it before anyone else can clone.
Other changes: bench deflection limit 13 -> 15 deg (lead, 2026-10-27; travel still clamped by SLA_Cn_MIN/MAX at 12 deg);
SLA_STATUS_AGE may now be up to 2 s because EKF2 republishes unchanged estimator_status_flags only once per second. A
1 s bound would expire the flags in steady flight and trigger D3.
`Tools/slaaps/configuration/ProfileCheck.cpp` (`slaaps_profile_check <file.params>`) runs every profile validator on a QGC
parameter file.

### Verification (2026-10-28)
Host suite PASS 4452 checks (Debug/Release/ASan-UBSan). Sealed native build /var/tmp/slaaps-native-1nt_xt1n
(binary 103b46a3) PASS for both verify_native.py (evidence slaaps-native-runtime-jzcgyt6q) and --signed-flight
(slaaps-native-runtime-svc5_tl1). The first run (h8cll6op) failed: measured_condition_valid was set while disarmed. Fixed so
all three flight flags require armed. The native scenarios do not yet exercise D1/D2/D6, launch staging or failed-bank recovery
at runtime; those are host-tested only.

### Lead decisions recorded 2026-10-27
D1 recovery thrust 0, hold 2 s, ramp 0.5/s. Pilot input is the QGC joystick on TELEM1 (SLA_IN_SOURCE 2, SLA_IN_INST 0,
QGC IDs 255/190). The EDF pack is measured by the POWER1 power module (SLA_PWR_INST 0, ID 1, source 0). Flight admission is
staged by SLA_FLT_STAGE (captive first; the launcher needs the pitot). Modeled starting values may be derived for review.

### Modeled starting values (added to the QGC parameter file, pending lead review; not measured)
Rate loop per axis from b = M_SCALE / I (normalized moment authority): roll 0.019/0.0072 = 2.64, pitch and yaw
0.077/0.0595 = 1.29 rad/s^2. Crossover wc = 5 rad/s roll, 4 rad/s pitch/yaw, below 1/(3 x 50 ms servo lag). RATE_P = wc/b
(1.89 / 3.09 / 3.09), RATE_I = RATE_P x wc/5 (1.89 / 2.47 / 2.47), RATE_D 0 (lag-dominated), I_LIM 0.3, M_LIM 1,
WEIGHT 1. ATT_P = wc/3.5 (1.43 / 1.14 / 1.14). Stick rates 2 / 1 / 1 rad/s; SP_ACC = 0.7 b (1.85 / 0.91 / 0.91 rad/s^2);
validity bounds MEAS_R 15 rad/s, MEAS_A 1000 rad/s^2. The gains assume the 13 m/s modeled authority. Authority scales with
dynamic pressure and no schedule is active, so expect retuning from captive tests.
Timing from PX4 publication rates: SLA_ARM_AGE 0.8 s (Commander republishes every 0.5 s), SLA_STATUS_AGE 1.5 s (EKF2 flags
1 s), SLA_IN_AGE 300 ms (joystick over radio), SLA_STATE_AGE 20 ms, SLA_STATE_SKEW 10 ms, SLA_CYCLE_MIN/MAX 4/12 ms,
SLA_FIRST_CMD_US 8 ms, SLA_ACT_ACK_US 3 ms, SLA_OUT_LEASE 100 ms, SLA_BENCH_GAP 50 ms, SLA_MAN_WAIT 30 s, SLA_PWR_AGE 0.5 s.
Control: SLA_STK_ENTER/EXIT 0.05/0.08, SLA_BENCH_DZ 0.05, SLA_QUAT_TOL 0.01, allocator REG 0.001, ROW 0.1, IND 0.1,
TOL 0.02. SLA_EDF_ENTRY 0.05. SLA_AOA/BETA 0 and SLA_TEST_THR 0 (modeled derivatives, no EDF dependence). SLA_FLT_STAGE 1,
SLA_LAUNCH_AS 12 m/s (the data sheet's airfoil minimum speed), SLA_LAUNCH_AGE 100 ms.
With SLA_DATA_REC/REV/STATE declared, `slaaps_profile_check` reports the SLAAPS Manual flight profile complete.

### Still open for the lead
- Declare the data record (SLA_DATA_REC, SLA_DATA_REV, SLA_DATA_STATE Calibrated) when you accept the data; flight
  admission requires it and the firmware never infers it.
- SLA_EDF_BMAX (EDF commissioning ceiling) is unset, so armed EDF Bench stays denied until it is set.
- Acceleration flight needs the 2x2 measured schedule (SLA_SCH_*), which needs the pitot airspeed source, plus the wrench
  model (including EDF reaction torque), feedback and companion source parameters.
- D5 decided (seamless bank handover, pilot centers sticks with low throttle); implementation pending.
- Rear-fin force model: specified, not yet in the effectiveness model. The fins only add static stability and the
  allocator uses canard effectiveness alone, so this mainly affects future gain derivation.

### D5 increment (2026-10-28)
Implemented. Host suite PASS 4454. Final sealed native build /var/tmp/slaaps-native-huzzhn5x PASS for verify_native.py
(evidence slaaps-native-runtime-4gmg0crv) and --signed-flight (slaaps-native-runtime-41v6gpkq), including D1-then-D5.
The first hold frame repeats the last all-bank handled canards when the controller is not yet due, so heavier load cannot
let the last pilot frame expire first. The re-entry readiness check does cheap mode checks first, captures input once and
validates the profile only when the pilot condition holds.
The D3 power-loss and mode-left checks failed once each under WSL load and passed on rerun of the same binary; treat them as
timing-sensitive.
- An armed re-entry publishes a positive grant while preparing, so it runs at control cadence (`_reentry` in control_due
  and the schedule interval); a stale grant is a bank fault.
- Trigger (`Slaaps::reentryReady`): SLAAPS Manual selected, the selected pilot input fresh and centered with throttle at or
  below SLA_EDF_ENTRY, a complete flight profile, held continuously for 200 ms. It is checked during a Manual D1 hold and
  during D3 recovery.
- `Slaaps::beginReentry` ends the lost session but keeps the accepted profile active, then runs the normal preparation in
  armed mode (`_reentry`, 1 s bound, armed input binding). Activation reuses the active profile.
- Preparation message field `reentry` (Manual only). Driver (`SlaapsOutputPreparation`): such a proposal for a newer
  session may clear the armed latch or replace a recovering bank and prepare while armed. Nothing is written while
  preparing, so the hardware keeps the last handled pulses until the fresh first frame; no lease is renewed.
- Known failure mode: if re-entry fails after the old session ended, outputs stay unwritten until disarm (canards hold the
  last pulses, EDF receives no commands), close to the D3 end state.
- D1 fixes found by the native D5 run: hold frames now use min(SLA_OUT_LEASE, SLA_IN_AGE) for Manual EDF requests, like the
  banks. `EdfOwner::acceptHold` installs the first hold request. The hold now starts two controller periods (2 x SLA_CYCLE_MIN)
  before the pilot's own EDF request expires, counting the sample offered in that cycle, and only when the session's profile
  has complete D1 data. Effective pilot-loss timeout = min(SLA_OUT_LEASE, SLA_IN_AGE) - 2 x SLA_CYCLE_MIN (92 ms with the
  QGC parameter file).
- Native scenario: block "D1 then D5" before the flight-transfer supervisor stop in VehicleConfigurationTest.cpp.
- After native PASS: rebuild the 6X image (scratchpad build_6x.sh / collect_6x.sh flow, clean build dir), replace the
  image in build/slaaps_firmware/flight-readiness-20261028, and update its README.
