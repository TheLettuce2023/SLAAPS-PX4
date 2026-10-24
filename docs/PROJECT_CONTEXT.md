# SLAAPS implementation: start here

Updated 2026-10-23 by Codex. Software progress is not flight acceptance.
Current uncommitted increment: actionable configuration and routing rejection
through supervisor status, Commander events and native QGC STATUSTEXT. Host/native
checks and current FMUv6X source/package/static-resource verification pass.
Repository: https://github.com/TheLettuce2023/SLAAPS-PX4 (`origin` verified).
Branch `slaaps-sitl`; verification source base `36a17722eff48e4317bdc0888fc541b935e51b7a`.
Power-health checkpoint `58f4be89fa` is committed; diagnostic changes remain uncommitted.
Evidence retains its original source base and explicit overlays. Preserve the separate
user AGENTS.md edits and unrelated untracked `timestamp`.
Latest completed increment: retained exact apply errors and current physical routing
diagnostics through native QGC messages. Power-health and companion-receipt paths
remain integrated; companion receipt is not acceleration command admission. Current host/native/board checks pass; manual flight
admission, companion command admission/control and physical qualification remain open.

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
| Companion semantics | [Acceleration contract](ACCELERATION_WIRE_CONTRACT.md), [state contract](C0B2_STATE_CONTRACT.md); `wire`, `adapter`, `guard`, `clock` |

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
308 parameters are supported; this is not a custom QGC graphical editor or GUI test.
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
Companion acceleration remains unimplemented: AccelerationControllerPort is an
unavailable stub; `research_flight_controller` is a planned graph target, not a module.
Verified receipt transport uses repository-owned `src/modules/mavlink/slaaps.xml`
(private IDs200010/200011/200012, inherited development dialect), selected only by
the three SLAAPS targets. Receiver publishes original bounded payload and actual
signature-verification evidence on four queued per-link topics, with signing
generation and event sequence. Authentication is not session or output permission.
`research_acceleration` now has a normal PX4 library target, not yet linked/called
by the supervisor. Configured-source receipt monitoring and local status are now
implemented and pass actual-parser/native checks with queued lifetime reads. Trusted
bounded time mapping, session grant and wire status return remain missing.

## Latest evidence

Detailed results, failures, hashes and earlier scheduling evidence are in the
linked integration record. Current native build `3eg3u4yv` and runtime `gjoks2mh`
pass operator rejection diagnostics, power recovery, manual/bench lifecycles, shared
schedule/mixer checks and companion signed receipt. Isolated manual uses synthetic authority;
Commander EDF commissioning uses recorded internal forced arming in no-plant
tests. Normal vehicle health, physical outputs and QGC GUI remain unqualified.
Current board stage `diagnostics-20261023` links at image1,907,220 B
with headroom58,860 B; nine fresh probe units and 26 dependency-verified reused units. Snapshots retain baseline
Git metadata472a7d6786 with explicit committed+dirty overlays; the commit alone
does not identify tested content. Runtime stack/timing and target boot remain open.

## Next three tasks and remaining dependencies

1. Finish manual admission/recovery and vehicle-supported ground-spool/takeoff behavior
   when the required policy and measured envelope exist; keep independent software moving.
2. Connect feasible companion acceleration through the existing contract and shared
   calibrated path, with exclusive handoff; do not assume independent force control.
3. Close stock QGC operator workflow, working-base boot integration, runtime timing/stack
   and physical acceptance, with separate authorization for any hardware operation.

Still unavailable: actual mass/CG/inertia/CAD, signed effectiveness across qualified
flow/wash, loaded servo dynamics, EDF thrust/response/voltage measurements, authority
acceptance and recovery hardware/policy. These questions are pending; do not repeat
unchanged requests or invent fallback values. Physical power-feed assignment/voltage
bounds, propulsion response and landed/takeoff behavior still need vehicle acceptance.
EDF ceiling/duration is pending.
Preserve working e8ea98b404/NuttX11 servo baseline. Newer-base boot issue is unresolved;
R01-R10 operator servo passes are not numerical flight calibration or exact provenance.
Use WSL Ubuntu-24.04 and existing Tools/slaaps workflows. Subagents use Sol xhigh.
Earlier failures, recovery copies and companion/preparation evidence are retained
in the linked integration record. The deterministic initial-readiness race is fixed;
the old intermittent `6buyl3gj` failure cannot be conclusively attributed to it.
Unlinked acceleration-library host/ARM portability passes; it is not a controller.
Diagnostic host matrix `/var/tmp/slaaps-configuration-31m8u6ae` passes Debug/Release/
ASan-UBSan (3,082 each) and 308 parameters. Portable test prefix is unchanged by
native-only compact field renaming. Native build `3eg3u4yv` and full runtime
`gjoks2mh` pass emitted parameter/channel text, clearing and existing regressions.
Initial compile exceeded ULog's format-description limit; compact new field names
resolve it without enlarging logger buffers. Failed runtime `uq8klqnm` exposed a
truncated test marker; the corrected Python harness is hashed separately.
All jobs are terminal and evidence is linked; clearing history or a fresh chat is safe.
