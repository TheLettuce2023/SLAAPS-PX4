# SLAAPS configuration and routing implementation

2026-10-20, Codex. First software increment toward the
[airframe goal](SLAAPS_AIRFRAME_GOAL.md); no native mode or physical output
integration is claimed by the host evidence below.

## Manual propulsion request adapter

The user authorized flight-capable control with propulsion. `ManualEdfInput.hpp`
now connects selected manual throttle to the common `EdfOwner` request contract
in host tests. It reuses `ManualControlInput` for MAVLink instance/sender,
freshness, finite range and sample-continuity validation. PX4 throttle -1 means
zero command; +1 means full command; center means 50% and is rejected by the
owner's low-throttle entry check.

Requests retain their original sample timestamp and a deadline bounded by both
manual freshness and the output lease. Held or republished samples generate no
new request. Invalid input latches the adapter inactive and preserves the caller's
request; the future worker must handle rejection as input loss, rather than resend
the preserved request. Reconfiguration is not flight admission: the common owner
independently enforces session replay, permission, low entry, slew and expiry.
Only throttle is validated here; the attitude controller must validate its axes
before the worker admits a combined control cycle.

Evidence `/var/tmp/slaaps-configuration-72_4fyda` passes 1,554 checks each in
Debug, Release and ASan/UBSan, plus actual PX4 metadata generation (175 parameters).
The 53 new checks cover the adapter with the real EDF owner, source rejection,
malformed/stale/pre-grant/future/overflow samples, immutable identity, held-input
expiry, centered-throttle entry rejection and fault latching.
This is synthetic host evidence. The native supervisor does not yet instantiate
the adapter or publish propulsion. Prior native and board artifacts remain bound
to their original sources; this increment does not requalify those artifacts.

## Combined manual worker core

`src/lib/slaaps/ManualWorker.hpp` composes the actual stabilized attitude/rate
controller, allocator, selected throttle adapter and shared EDF owner. Entry
requires the applied configuration fingerprint, centered attitude sticks, low
throttle and a new common-owner session. The supervisor retains the EDF owner
across worker replacement and must serialize stop/ownership transfer.

A successful cycle returns four canard commands and the EDF command together.
Its deadline is the earliest of propulsion expiry, attitude/rate sample expiry
and maximum control-cycle gap. Early polls produce no replacement output.
Failures latch the worker inactive and revoke the shared owner; the caller must
not republish preserved output. RecoveryRequired remains an explicit unresolved
supervisor flight-loss decision, not an automatic airborne thrust-cut policy.
Fixed simulated thrust metadata is cleared and never used as propulsion demand.

The worker now requires an explicit routing/lifetime binding and one software
acknowledgment from each participating MAIN/AUX bank for every combined frame.
Each reply must match session, source, configuration, routing, driver lifetime,
combined sequence and calculation timestamp, with a nonfuture acceptance time.
Partial replies keep the worker waiting; duplicate, malformed, late or unexpected
replies latch a fault. Every cycle checks the live binding, so routing changes or
driver replacement cannot inherit the old command stream. The future transport
must verify the actual publisher; fields supplied in a message are not proof of
publisher identity or electrical application.

While waiting, the existing controller's monitor validates input and estimator
health without integrating PID or advancing allocation history. The throttle
adapter continues continuity checks and retains the latest fresh request with
its original timestamp. The EDF target is not changed until all bank replies
arrive. The combined deadline is never extended by polling or acknowledgment;
expiry revokes the worker even if a late reply would otherwise match. Same-time
replies may complete acknowledgment but cannot cause a second integration.
This is producer-side protocol behavior; independent driver expiry and native
transport remain required.

The native supervisor does not instantiate this worker yet. Output acknowledgment,
per-bank admission and independent expiry still need integration before any
publication is eligible. The existing one-condition allocation model still needs
measured airflow/control-authority admission; variable throttle alone cannot prove
that the measured canard operating condition remains valid. Tests supply synthetic
permission and configuration and do not establish flight acceptance.

Host evidence `/var/tmp/slaaps-configuration-fsfptqs2` passes 1,611 checks each
in Debug, Release and ASan/UBSan plus the 175-parameter metadata checks. The 57
new checks exercise combined allocation/propulsion, entry, cycle timing, session
replay and faults in either subsystem. No native/board rebuild is claimed.

Latest host evidence `/var/tmp/slaaps-configuration-b92d75tq` passes 1,688 checks
each in Debug, Release and ASan/UBSan and 175-parameter metadata validation.
The 77 added checks cover per-bank acknowledgment, pending live-health faults,
routing/driver replacement, deadline enforcement and retained throttle requests.
Previous evidence remains scoped to its original sources.

## Independent manual bank consumer

`ManualOutput.hpp` now holds the shared combined-frame, bank-binding and
acknowledgment types. `ManualOutputBank.hpp` implements one independently clocked
consumer per participating MAIN/AUX bank. Entry requires a new session, a complete
validated vehicle configuration, a matching fingerprint, explicit driver/routing
lifetimes and a valid complete routing inventory. Failed entry consumes the
session; neither frames nor acknowledgments can grant a session.

Each dispatch checks permission, immutable routing/calibration and the stored
command deadline. It validates the combined frame and maps calibrated canards
and forward-only EDF commands to that bank's assigned PWM channels. Logical
servo reversal is applied once through the measured endpoint calibration.
Duplicate/out-of-order frames, invalid commands, permission loss, driver/routing
changes and expiry latch faults. Polling without new traffic does not renew the
lease; initial silence is bounded too. Armed faults return RecoveryRequired,
while disarm/kill/lockdown/termination/bench return Inhibited. Neither disposition
is a command to write zero-valued PWM data or an implemented airborne fallback.

The current API separates preparation from handling confirmation: `accept()`
returns no acknowledgment. `confirmHandled()` releases one only after the trusted
driver adapter reports successful handling of the exact sampled ticket. Native
transport must bind the actual publisher, provide fresh complete inventory, and
serialize validation, writes and confirmation with routing/permission changes. This code does not supply that native adapter,
cross-bank atomic admission, electrical feedback or a flight-loss policy.
Producer-side control and EDF owners still enforce slew; independent driver-side
health and final actuation acceptance remain integration work.

Latest consumer evidence `/var/tmp/slaaps-configuration-kq6bnc5k` passes 1,761
checks each in Debug, Release and ASan/UBSan plus 175-parameter metadata validation.
The 73 added checks connect the real worker to both consumers, including nonzero
slewed propulsion PWM, mixed-bank routing, reversed calibration, independent
expiry and malformed-frame/live-permission faults. Initial consumer evidence
`/var/tmp/slaaps-configuration-frwzp7ib` remains retained (1,757 checks); the later
run adds four checks for nonzero propulsion through successive acknowledged frames.
No native/board rebuild or physical output test is claimed.

## Driver-handling confirmation

`ManualOutputBank::accept()` now prepares a candidate without producing an
acknowledgment. The sampled output carries session, source, configuration,
routing/driver lifetimes, command timestamp, sequence, channel mask, deadline and
PWM payload. The trusted driver adapter reports successful handling of that exact
ticket through `confirmHandled()`. Live permission, routing and expiry are
rechecked before an acknowledgment is released. Its acceptance time is the
confirmation time, not the earlier preparation time.

Failed handling, altered PWM or metadata, duplicate confirmation, stale-session
tickets and changes during preparation/handling latch faults without modifying
the caller's acknowledgment. A new frame cannot replace an unhandled candidate.
The host fixture explicitly uses a memory-only successful-handler stand-in;
there is still no physical-driver adapter or electrical feedback. Caller reports
of success must come from the real serialized driver path before flight admission.

Host evidence `/var/tmp/slaaps-configuration-5b7km9ro` passes 1,821 checks each in
Debug, Release and ASan/UBSan plus actual parameter metadata generation. The 60
new checks cover delayed and failed handling, ticket integrity, stale sessions,
premature/duplicate confirmation, pending replacement and live permission/routing
changes. Earlier evidence remains bound to the pre-confirmation API.

The pending native rebuild completed before the user-requested pause. After
resume, its binary and manifest hashes were verified, and the current consumer
header and test source were confirmed identical to the sealed snapshot. Native
build `/var/tmp/slaaps-native-cldwgvji` and runtime
`/var/tmp/slaaps-native-runtime-5gglvtox` pass all 1,821 control checks and 119
successful runtime commands, including the supervisor lifecycle/routing checks.
Binary SHA-256:
`f26dbe7e9694de9a812815f8a3c776e2747581faa6d065b5e39aa09e12e34182`.
All 77 source hashes bound by the host receipt still match the current worktree.
The rebuilt native fixture still has no driver transport or physical output.

The native builder now supports explicit `--refresh-file` entries with
`--test-from`: each refreshed path is recorded with before/after hashes in the
fork's manifest, while the sealed parent remains unchanged. Paths are normalized
inside the workspace. This rebuild explicitly refreshed the bank consumer header
and shared test source; prior evidence is retained under its original identity.

## Manual command and acknowledgment wire boundary

`SlaapsManualCommand` transports one combined manual frame, retaining calculation
time, original deadline, combined and EDF request sequences, session/source/
configuration identities, routing/driver lifetimes, logical canard values and
normalized EDF output. Publication time is separate and cannot renew the command.
`SlaapsManualAck` uses separate `slaaps_manual_ack_main` and
`slaaps_manual_ack_aux` topics so one bank cannot overwrite the other bank's reply.
Acknowledgment publication, calculation and driver-handling times remain distinct.
These are internal control messages, not new MAVLink streams or QGC parameters.

`ManualTransport.hpp` encodes/decodes these generated schemas. It rejects missing
identities, inconsistent/future times, expired commands, unsupported bank masks,
wrong-bank replies and nonfinite/out-of-range commands while preserving caller
output. Stateful replay, permission and lease-bound checks remain in the worker
and bank consumers. The codec neither authenticates a publisher nor grants a
session. Payload source fields are not proof of actual publisher identity.

The older atomic uORB publisher-revision support is currently gated on
`CONFIG_MODULES_RESEARCH_STABILIZED_MANUAL`; SLAAPS does not silently inherit it.
Production transport must bind the publisher and copied payload atomically,
reject publisher replacement and connect a real serialized driver adapter before
mode availability can change. Current native round-trip fixtures exercise uORB
publication/copy only, with synthetic timestamps and no actuator application.

Host evidence `/var/tmp/slaaps-configuration-qr1u1rlb` passes 1,879 checks each in
Debug, Release and ASan/UBSan, including 58 new codec checks and actual generated
message headers plus the unchanged 175-parameter metadata checks. Valid combined
worker frames traverse the codec in the bank fixture. Invalid-input consumer
fault tests retain their direct path so the consumer's fault latch is also tested.

Native wire evidence: `/var/tmp/slaaps-native-mah0jeye`, runtime
`/var/tmp/slaaps-native-runtime-72xsqqdr`. The suite passes 1,886 checks: the
1,879 shared checks plus seven native uORB publication/copy/decoding checks.
All 116 runtime commands succeed, preserving supervisor configuration/routing
and availability gates. Binary SHA-256:
`db2926843032b2dbc0f6af4b186e65ecaced358b78ee185532efbfe4307f0cef`.
Binary/manifest hashes, the changed schema/codec/test sources and all 80 host
source hashes were verified against the retained receipts. The native receipt's
no-driver-transport scope remains accurate: these are message round trips, not
publisher-authenticated driver application or flight qualification.

## Native control-core verification

The explicit `px4_sitl_slaaps-test` target now compiles the shared configuration,
worker and bank-consumer suite as `slaaps_control_test`. It links the real PX4
SLAAPS libraries and generated headers under native warning/optimization flags.
The command is guarded by the existing POSIX wall-clock-only test Kconfig option
and is not present in vehicle targets. It uses synthetic inputs and publishes no
actuator command. This is native execution of the portable pipeline, not native
uORB transport, driver dispatch, electrical output or plant qualification.

`build_native.py --wall-clock-test` stages the current checkout directly for this
test target. The source overlay now includes the shared test source. The runtime
verifier requires the command's success marker and at least 1,761 checks before
running the existing supervisor checks. The native compiler exposed three exact
float assertions in the test harness; they now use explicit bitwise equality,
retaining strict zero/preservation tests without relaxing compiler warnings.

Evidence: build `/var/tmp/slaaps-native-wg2bcgil`, runtime
`/var/tmp/slaaps-native-runtime-965uvaxu`. Binary SHA-256:
`5de9612fa3ed5559c20598f674d265ac0c3bc82badbd6a369cd8b374f6ebb3a9`.
The native command passes 1,761 checks; all 118 runtime commands succeed.
Registration, zero availability masks, explicit configuration application,
routing permutations/faults/expiry and three supervisor restarts pass on that
binary. The retained failed-compile log and source-manifest revision record bind
the assertion correction. Binary and manifest hashes were independently rechecked.
The same final test source passes 1,761 checks each in Debug, Release and
ASan/UBSan at `/var/tmp/slaaps-configuration-nxd0ulhp`, with the 175-parameter
metadata checks. Prior host evidence `qwmc48dy` predates the assertion correction.
No board firmware rebuild, physical operation or QGC GUI acceptance is claimed.

## Native integration in progress

Airframe 22001 and dedicated startup, native mode supervisor, parameter snapshot
adapter, commander availability guard and separate SLAAPS board targets are now
present. `SLA_CFG_KIND` selects bench/manual validation and `SLA_CFG_APPLY`
requires an explicit counter increase after completed disarmed edits. Saved
counter values do not apply a configuration on supervisor restart. These two
controls bring the generated parameter count to 175; the vehicle schema remains
173 fields. Native module CMake now registers the metadata.

The supervisor registers three named external modes and requests direct-actuator
control flags. All availability masks remain zero until control/driver ownership
is implemented; registration is not operational bench or flight integration.
The commander guard rejects stock modes for this airframe and stale supervisor
status. Active bench/manual workers, EDF output, the acceleration algorithm,
land detection, driver write admission and QGC GUI acceptance remain pending.

### Driver-reported routing

The PWMOut and PX4IO drivers now publish effective PWM-bank observations through
`SlaapsOutputBank`. Reports include the driver lifetime token, current function
assignments, actual channel mask/rates and mixer calibration/reversal/off values.
PX4IO reads back rate groups and rates and reports serial servo output unsupported.
Publication occurs on the driver work queue before any work-queue switch.
The dedicated fmu-v6x path requires fresh MAIN and AUX reports, even if all user
assignments occupy one bank. Missing, duplicate, stale or unready banks invalidate
the observed routing. Unavailable hardware, OneShot/unsupported rates, duplicate
functions, unresolved failsafe defaults and calibration mismatches are rejected.

The supervisor checks the reported inventory against the applied configuration
and exposes `routing_snapshot_valid`, `inventory_fault` and `routing_fault`.
This is a diagnostic observation, not an atomic cross-bank output transaction,
proof that every possible non-PWM transport is absent, or a permission to write.
The final driver boundary still needs live routing revalidation, immutable
session handoff, expiry and acknowledgments. No SLAAPS function provider exists
yet, and all mode availability masks remain zero.

Host evidence `/var/tmp/slaaps-configuration-waxqub5y` passes 1,501 checks each
Debug/Release/ASan-UBSan, including 30 new report-to-routing tests and the NuttX
portability correction to the EDF limiter. Native production snapshot
`/var/tmp/slaaps-native-y8no43f3` links. Its explicit test overlay
`/var/tmp/slaaps-native-8v54g2g_` adds a synthetic bank publisher available only in
the wall-clock test target. Runtime evidence
`/var/tmp/slaaps-native-runtime-q28ecapp` passes registration/apply/restart plus
valid cross-bank routing, duplicate/unavailable/miscalibrated/stale reports and
heartbeat expiry. It does not exercise a physical driver or actual QGC GUI.

### Board compile and resource evidence

The current dirty `slaaps-sitl` checkout at `472a7d6786` now builds target
`px4_fmu-v6x_slaaps`. Evidence is retained in
`build/slaaps_firmware/routing-20261020` and Docker volume
`px4-slaaps-routing-20261020`. Source comes from the sealed native snapshot plus
explicit `board_revisions` in `board-source-manifest.json`. Those revisions use
PX4 math instead of unavailable NuttX `<algorithm>`, avoid an unnecessary C++
limits dependency, and omit optional MSP video overlay and camera-gimbal control
from this dedicated target. Existing stock/bench/observer target configurations
are unchanged; no binary-equivalence claim is made for the shared new messages.

The initial link exceeded flash by 10,812 bytes. Final image: 1,952,100 bytes,
13,980 bytes flash headroom; linked AXI SRAM: 111,356 bytes. The `.px4` package is
1,850,960 bytes. `independent-verification.json` rehashes artifacts, decodes the
package, checks board ID 53 and binary equality/capacity, and confirms thirteen
changed production paths match the recorded sources. Board receipt SHA-256:
`a5f29150dc750781dcc704f18eab8a4320961eb16335e17239187576c244e592`.

Package:
`build/slaaps_firmware/routing-20261020/artifacts/px4_fmu-v6x_slaaps_472a7d6786_dirty_output.px4`.
Temporary WSL stage:
`/tmp/px4-firmware/px4_fmu-v6x_slaaps_472a7d6786_dirty_routing_20261020.px4`.
Package SHA-256:
`7ea5849775ddd7457246f5f46ce4f78c0ea662826e0a0ee9b3b9632c5c35499e`.
This is the current checkout's build, not a completed port to the known-working
e8ea98b404/NuttX 11 baseline. It has not been flashed or boot-tested and does not
establish physical output behavior. Further active integration must stay within
the actual remaining flash/stack budget.

Fresh host evidence after native compiler compatibility fixes:
`/var/tmp/slaaps-configuration-u9j4z0ws`, all 1,347 checks in Debug, Release and
ASan/UBSan, plus actual PX4 metadata generation for all 175 parameters.
Native build `/var/tmp/slaaps-native-dkdvdxzc` links successfully. Source revisions
record corrections to event parsing and exact PWM comparisons. The separate
wall-clock test variant (`boards/px4/sitl/slaaps-test.px4board`) builds from its
sealed source snapshot at `/var/tmp/slaaps-native-bmrdeobx`. This variant advances
time without a simulator; the normal SLAAPS SITL target retains lockstep.

`Tools/slaaps/verify_native.py` passes against that binary, evidence
`/var/tmp/slaaps-native-runtime-bc9732od`: three unique registered modes
(23/24/25), zero availability masks, explicit apply, edit invalidation retained
after reverting values, rejection of bench-only data for manual scope, recovery
after a new valid bench apply, and three stop/start cycles with slot reuse and
no saved-counter auto-apply. Native snapshots precede one whitespace-only final
formatter change to VehicleConfiguration.cpp. These are supervisor lifecycle
checks, not flight behavior, physical outputs or actual QGC GUI acceptance.

## Implemented

### Shared EDF request owner and acceleration development port

`EdfOwner` now defines the portable common propulsion-request path. One grant
binds a monotonically increasing boot session, source-instance token,
configuration-generation token and selected manual/acceleration mode. Requests
carry the same binding, increasing sequence/time and a bounded absolute deadline.
Manual requests use normalized [0,1] throttle; acceleration requests use newtons
converted through the measured five-point curve. Both require valid measured
configuration, fresh admitted/healthy armed permission and a valid curve
condition. These permission fields are supervisor evidence, not new QGC switches.

The first demand must satisfy the configured low-throttle entry threshold.
Rise/fall limits apply to elapsed owner time; a newly arrived demand cannot spend
slew budget from before its arrival. Polling never refreshes the request deadline.
Foreign, malformed, stale or expired requests latch revocation; restoring values
does not restore ownership. Replacement and failed replacement discard the prior
grant. The future driver handoff must separately confirm old-output revocation.

Bench, disarmed, kill, lockdown and termination produce `Inhibited`. Other
armed ownership/health/request loss produces `RecoveryRequired`, with no command
authority. This deliberately does not decide whether an airborne vehicle should
hold, stop or transition thrust. A qualified flight recovery policy is required
before the supervisor may set `flight_admitted`. Physical output attachment,
independent driver expiry, actual throttle adapter and acknowledgment handling
remain pending; this class is not an operational motor controller.

`AccelerationControllerPort.hpp` preserves the existing guarded canonical
body-FRD inertial-acceleration-at-CG request and selected-state types. Its output
is logical canard deflections plus EDF thrust in newtons; the latter must enter
`EdfOwner`. The default implementation is unavailable and preserves caller output.
It defines no alternate wire parser and never enables an acceleration flight mode.
Future controller development must keep request guard/session/time-map checks,
body-fixed behavior between updates and shared supervisor/driver ownership.

Host evidence `/var/tmp/slaaps-configuration-dmiir12d` passes 1,471 checks in
Debug, Release and ASan/UBSan, including 124 new EDF/port checks. The EDF tests
exercise both request units, low entry, causal rise/fall slew, original expiry,
session replacement, wrong owner/configuration, stale/replayed/malformed data,
health/condition/permission loss and unconditional bench/kill inhibition.
Fresh native target `/var/tmp/slaaps-native-xj9r0n6l` links and compiles the EDF
owner with PX4 flags. Receipt SHA-256:
`6403a722a0167d535faa685db4943754cc82240c3232225ba20856df49233382`.
EDF object SHA-256:
`4520995a53d6919f34e8f3f03a0198e4c963b262964dfb2912a986ea12a2b886`.
The supervisor does not instantiate it yet; object compilation is not evidence
of linked active propulsion or mode behavior.

`src/lib/slaaps` now supplies a typed vehicle configuration, adapters to the
existing `BenchControlOwner` and `StabilizedManualControl`, an immutable
configuration store, a measured EDF curve interpolation function and a
logical-to-physical routing validator. The library is registered in the PX4
library tree with `EXCLUDE_FROM_ALL`; the new SLAAPS module links it explicitly.

`VehicleParameters.def` is the single scalar schema. It generates 173 PX4
parameters in `src/modules/slaaps/module.yaml`, grouped by vehicle, individual
canard, axis, input/timing, control and EDF. They include units, descriptions,
ranges and deliberately unconfigured physical defaults. Generate or check
the metadata with:

```text
python Tools/slaaps/generate_parameters.py
python Tools/slaaps/generate_parameters.py --check
```

The actual PX4 YAML-to-C-to-XML/JSON toolchain accepts every parameter.
This prepares QGC parameter editing; it is not evidence that those parameters
are present in an installed firmware or visible in an actual QGC session.
The `slaaps` module registers the metadata in its CMake target.

Validation checks scalar bounds, calibration ordering, full inertia positive
definiteness and rigid-body principal-moment constraints, source identity,
timing, local physical effectiveness conversion, actual allocator rank/
configuration checks, controller configuration and a monotonic EDF curve.
The bench adapter does not require aerodynamic or propulsion data. It retains
the existing bench module's 13-degree maximum deflection contract.

Configuration candidates are immutable after activation. An observed change
latches invalidation even if the values later revert; explicit disable and a
new valid commit are required. Failed candidate replacement cannot silently
fall back to an older candidate. The store retains the mode class used for
validation. Its fieldwise comparison, rather than its diagnostic FNV hash,
determines equality. No hash claims authentication or measured acceptance.

The EDF helper maps thrust in newtons through a five-point, forward-only
measured curve at normalized commands 0, .25, .5, .75 and 1. It rejects
out-of-range demand instead of silently clamping it. Voltage/airflow
applicability and the propulsion owner must still be implemented. The
controller's legacy `fixed_simulated_thrust_n` report is set to zero and is
never interpreted as a propulsion request.

## Configurable channel mapping

Reserved logical function IDs are `SLAAPS_Canard1..4` = 10100..10103 and
`SLAAPS_EDF` = 10104. They are excluded from ordinary actuator testing.
Existing bench/stabilized function IDs and providers remain unchanged.
These new names have no output provider yet and must not be presented as a
working actuator path.

The routing validator accepts arbitrary unique physical pins across MAIN and
AUX in a supplied complete driver inventory. Calibration follows the logical
canard, not the inventory position. It checks physical output limits, center
and reversal against that canard's calibration; requires pulses to fit the
PWM period; rejects duplicate functions/pins, unsupported protocols and raw
motor/servo/passthrough functions that would bypass the intended owner.

Initial supported routing is PWM at at most 400 Hz, subject to subsequent
measured hardware limits. Other protocols are rejected explicitly. Canard
disarmed/failsafe settings currently require no pulse. EDF disarmed/failsafe
settings must agree and be either no pulse or the calibrated minimum pulse;
reversal is rejected. These are proposed software output boundaries, not
qualified airborne recovery behavior. Flight requires an EDF assignment;
bench may leave it unassigned.

The native inventory adapter must enumerate all drivers, including unsupported
active outputs. It must check shared timer configuration and actual supported
pins rather than assuming that every index representable in the portable
inventory exists on the board. Driver-side acknowledgments must cover every
bank in the validated plan. No native enumeration or physical routing test
has been performed.

## Verification

Run in the configured Linux/WSL environment:

```text
python3 Tools/slaaps/verify_configuration.py
```

Final evidence: `/var/tmp/slaaps-configuration-9d9e_9ht`.
`result.json` SHA-256:
`cf55d60132b0c67264ea4961c4137ba15175e748b7bb641f8d5863e8ea524b6e`.
Each Debug, Release and ASan/UBSan build passes 1,347 checks. Coverage includes:

- Missing/malformed data, every scalar field's invalid values and identity
  participation, invalid inertia, calibration, rank and thrust curves.
- Actual bench and stabilized-controller entry/cycles using the new adapters.
- Configuration edit/reversion, active replacement rejection and failed
  replacement invalidation.
- All 120 orderings of a mixed MAIN/AUX five-output inventory, plus duplicate,
  missing, incompatible and bypass assignments, reversal and EDF off values.
- Real PX4-generated output-function IDs and all 173 generated parameters.

The verifier records source hashes and snapshots for its declared inputs and
checks that they did not change during the run. These are host tests, not a
complete firmware source manifest, native runtime, GUI acceptance, timing
qualification or vehicle qualification. Prior finite passes and the initial
unsupported metadata-unit failure remain in their own `/var/tmp` directories.
New C++ files use the repository astyle configuration; changed tracked files
pass whitespace checks. No hardware was operated or firmware flashed.

## Architecture findings and next implementation

The six mode-message schemas inspected (`RegisterExtComponentRequest/Reply`,
`SetpointConfig/Reply`, `UnregisterExtComponent`, `ArmingCheckReply`) have no
diff between the working hardware baseline `e8ea98b404` and this checkout.
Both expose direct-actuator setpoint configuration. Commander implementation
and complete resource compatibility still require a real build.

Individual `param_get` calls do not produce a coherent multi-parameter
transaction. The backend has a recursive POSIX mutex / NuttX interrupt
critical-section `AtomicTransaction`. A native snapshot facility must use the
backend's synchronization, avoid allocation inside it, and measure its cost.
Do not substitute two unguarded scans or an asynchronously published
`parameter_update` count for an atomic snapshot. An explicit user apply
workflow is also needed to distinguish completed GUI edits from an in-progress
multi-field edit. Active edits must invalidate the session before new data is
used; the existing immutable store is ready for that integration.

Next implement that native parameter/apply adapter and the `slaaps` supervisor,
then the airframe/startup and named-mode lifecycle. Reuse the existing control
libraries; the legacy observer and bench executables intentionally reject
being enabled together. Provide servo-bench mode entry/exit through its
existing controller/module integration, one shared canard mapping, independent
output expiry, a single EDF owner and a deliberately unavailable acceleration
port. Complete mode requirements, stock-mode exclusion/failsafe behavior,
native integration tests, working-baseline board build/resource checks and
QGC validation remain required by the original goal.

Do not regard this structurally valid single-condition model as proof of
trim bias, robust authority, flight envelope, calibration provenance or
operational gains. The forthcoming measured data and production admission
policy remain separate gates. No additional user answer is needed to continue
the software integration.

## Atomic publisher-lifetime binding (2026-10-20)

The uORB advertisement revision/payload critical section is now enabled for
MODULES_SLAAPS as well as the existing research module. Ordinary builds with
neither option retain the original path. `BoundSubscription<T>` requires an
explicit expected revision supplied at admission, checks payload size, copies
payload and revision atomically, and latches rejection on missing or replaced
publishers. Re-admission is explicit; caller output remains unchanged on failure.
It cannot authenticate other writers sharing a uORB publisher handle and grants
no actuator permission. Production supervisor/driver attachment remains pending.

Final host evidence `/var/tmp/slaaps-configuration-attaje33` passes 1,879 checks
in each Debug/Release/ASan-UBSan configuration and 175 generated PX4/QGC parameter
metadata checks; all 81 input hashes match the final working sources.
Native build `/var/tmp/slaaps-native-uxm29537` passes compile/link; binary SHA256
`14d5658da8b232c3f8556de2429af3ec028afe70368e403f6ac71d9a6a267bfc`.
The first compile's topic-identifier mismatch was corrected; its log is retained
as `compile-attempt1.log`, and the source manifest records explicit revisions.
Runtime `/var/tmp/slaaps-native-runtime-uhnifmw3` passes 1,900 checks and 116
supervisor/runtime commands. Fourteen added checks cover invalid/zero binding,
payload size, withdrawal, replacement before first publication, identical-payload
hidden replacement, latched rejection, explicit re-admission and revoke. The
seven previous actual uORB round-trip checks remain included. All five modified
native source anchors match the sealed snapshot. No physical outputs, target
firmware rebuild, QGC GUI acceptance or flight qualification are claimed.

The goal record now explicitly captures the requested future geometry/mass/
constraints/wiring GUI workflow and its model-derivation boundary. Current
parameters describe a measured effectiveness model; geometry-derived allocation
and automatic controller tuning remain future implementation, not implied by
parameter metadata or configuration validation.

## Native manual bank transport integration (2026-10-20)

`ManualBankTransport<0/1>` now connects the combined command uORB topic to the
independent MAIN/AUX consumers and their separate acknowledgment topics. The
endpoint requires a supervisor-supplied command advertisement revision and the
existing explicit configuration/routing/permission grant. It preserves its
consumer across sessions and rejects copying of the endpoint object.

Every dispatch first polls permission, live routing and the original deadline,
including initial silence and unchanged latest traffic. Initial advertisement
without a payload yields no PWM candidate and can wait only until the existing
consumer deadline. `BoundSubscription::read` distinguishes that waiting state
from a changed lifetime; the older strict `copy` interface retains its rejection
semantics. Queue sizes other than one cannot bind.

A new frame is decoded and accepted once. Re-reading the exact wire snapshot
neither re-accepts it nor extends its lease; changing any byte under the same
sequence revokes the endpoint. Command and reply advertisement replacement are
checked before preparation/confirmation. Handling confirmation rechecks current
transport, permission and routing, then requires the exact sampled output ticket
before encoding and publishing the bank reply. Failed handling, invalid traffic,
failed publication or a changed ticket revoke the endpoint.

This is a native transport adapter, not a PWM/PX4IO hardware attachment. Its
caller must serialize dispatch, the actual driver write, and confirmation with
permission/routing updates, and supply a qualified recovery policy. Advertisement
revisions are lifecycle observations, not authentication of shared publisher
handles. Production supervisor admission and physical driver attachment remain
pending; mode availability stays zero.

Verification: host `/var/tmp/slaaps-configuration-xlqxumxs` passes 1,879 checks
in each Debug/Release/ASan-UBSan configuration and all 175 parameter definitions;
all 82 source hashes match. Native `/var/tmp/slaaps-native-92q_qjy_` compiles and
links, binary SHA256
`ff33119cb42975440f335a9e1fd9338fe303f2b0f7b0cd7a5f888c140628cb0d`.
Runtime `/var/tmp/slaaps-native-runtime-culvq__8` passes 1,982 checks, including
82 added checks across nine transport scenarios, and 114 successful runtime
commands. The verifier now requires at least 1,982 checks and labels the scope
`native_bank_transport_synthetic_no_physical_driver`. Both changed headers and
the native test source match the sealed build snapshot. No new vehicle firmware
build, physical outputs, QGC GUI acceptance or flight qualification is claimed.

## PWMOut handling result correction (2026-10-20)

Before attaching SLAAPS handling acknowledgments to physical drivers, review found
that `PWMOut::updateOutputs` returned true while uninitialized and discarded
`up_pwm_servo_set` errors. It now reports false for either condition. It still
attempts the remaining hardware-mask channels after a write error and retains
existing disabled-channel, rounding and OneShot-trigger behavior. This reports
register handling only; a partial write is not rolled back and electrical output
is not verified. The eventual owner must apply its qualified recovery policy.

`Tools/slaaps/verify_pwm_output.py` extracts the actual method verbatim, injects
register calls, and binds the method, fixture, verifier, low-level declaration and
STM32 implementation by hash. Evidence `/var/tmp/slaaps-pwm-output-ep6_7lz3`
reproduces nine false-success failures in committed baseline `472a7d6786` and
passes all 25 checks in each corrected Debug/Release/ASan-UBSan build. Every
channel is independently fault-injected; writes to later channels still occur.
This is source-method host verification, not a board firmware or hardware test.

Separate board verification is in progress in
`build/slaaps_firmware/pwm-handling-20261020`, using the sealed current native
snapshot plus explicitly recorded production-source refreshes. Do not treat that
stage as a successful firmware build until its board receipt is present and
verified. The previous routing-20261020 board artifact remains unchanged.

The first PWM-handling board link passed code/package integrity with a 1,952,468 B
image, 13,612 B flash headroom and 111,356 B linked AXI SRAM. Independent ELF/bin
comparison found two matching flash load segments and all current changed
production paths matched the source manifest. However, the container omitted the
original Git alternate-object path, and CMake embedded its fallback `v0.0.0` tag.
That artifact is retained for diagnosis, not staged as the accepted candidate.
Regenerating the header with the same fallback tag reproduced it; that comparison
alone does not validate the version choice.

The build helper now verifies the same resolved Git description used by CMake,
rejects fallback tags, reconfigures derived builds and verifies the generated
version header before sealing. `Tools/slaaps/verify_board.py` independently checks
artifact hashes, package/ELF/bin equivalence, board/linker capacity, embedded tag
and current changed production-source hashes. ELF comparison was checked against
the preserved earlier board image and rejects payload corruption, wrong machine
and truncation. The old successful receipts are not modified.

A separate derived volume `px4-slaaps-pwm-metadata-20261020` and stage
`build/slaaps_firmware/pwm-handling-20261020-metadata` preserve the first build and
repair the missing original Git mount. Build exec handle 76054 is now terminal with exit zero. Final package verification
is recorded below; no build process remains active.

### Corrected board result

Stage `build/slaaps_firmware/pwm-handling-20261020-metadata` and Docker volume
`px4-slaaps-pwm-metadata-20261020` retain the successful derived build. Target
`px4_fmu-v6x_slaaps`, dirty `slaaps-sitl` checkpoint `472a7d6786`, embeds
`v1.18.0-beta1-679-g472a7d6786f`. Image 1,952,524 B; flash headroom 13,556 B;
linked AXI SRAM 111,356 B. Package size 1,851,204 B, SHA256
`46f36c861a7dad99f6c082193d739b98b3ad0eeb6dcd03cf37cf67b331c4c7ae`.
The independently checked package equals the binary, both flash ELF load segments
match it, the expected version string is present, and 83 current production and
board-configuration source hashes agree. Board receipt SHA256
`8b58c866f12ac4130ff3d3e2c51de6c50aa36f701cb0cf220f9854ce4e0727aa`.
The exact verifier is preserved beside its receipt. The corrected build log has
no unresolved-alternate or fatal Git messages. PWMOut::updateOutputs is linked.

Package: `build/slaaps_firmware/pwm-handling-20261020-metadata/artifacts/px4_fmu-v6x_slaaps_472a7d6786_dirty_output.px4`.
Distinct temporary copy:
`/tmp/px4-firmware/px4_fmu-v6x_slaaps_472a7d6786_dirty_pwm_handling_metadata_20261020.px4`.
No flashing, boot verification, physical driver attachment, electrical feedback,
flight qualification or working-baseline port is claimed. Original baseline,
routing and first diagnostic-build artifacts remain preserved.

## Native manual worker transport (2026-10-20)

`ManualWorkerTransport` now connects the actual portable controller and common
EDF owner to the combined uORB command publisher and the two bank reply topics.
Entry requires explicit command/reply advertisement revisions and the existing
configuration, selected-input/state, routing and permission grant. It validates
all participating reply transports before advancing the controller, drains each
new reply once, monitors the controller while waiting, and publishes only a fresh
`Command` result. Waiting/NotDue/fault paths do not republish retained output or
renew a deadline. Transport faults stop the worker and revoke its EDF ownership.

Same-publisher session re-entry uses prior-session command/reply snapshots only
as an ignored baseline. A retained expired command cannot drive the newly granted
bank, nor can an old reply complete the new session. New/current/future session
traffic at entry is rejected. An admitted worker's destruction stops ownership;
an unadmitted object's destruction does not revoke another owner. The supervisor
must still serialize ownership handoff and stop the old admitted worker before
transferring the shared owner.

Native tests now exercise the actual controller, uORB command transport, both
bank consumers, exact-ticket handling confirmations and uORB replies together.
They cover stick release, subsequent nonzero EDF slew, separate/simultaneous bank
replies, publisher replacement, missing/late replies, explicit stop, expired
retained traffic on re-entry and destruction. Their handling sink remains
memory-only. Production supervisor admission, selected sensor/input attachment,
PWMOut/PX4IO attachment and flight-loss policy are still pending. The most recent
board artifact predates this producer adapter; its resource figures do not yet
include an active production control path.

Final evidence: host `/var/tmp/slaaps-configuration-yvr4scwg` passes 1,879 checks
each Debug/Release/ASan-UBSan plus 175 parameter metadata definitions, with all 83
input hashes matching. Native `/var/tmp/slaaps-native-c5za5i48` compiles/links;
ELF SHA256 `e0d97bcf084ff49b48a21232c1af12e4ff8f6c85497055fc893c5974bda7e80d`.
Runtime `/var/tmp/slaaps-native-runtime-i1y50e_t` passes 2,034 checks and 115
successful supervisor/runtime commands. The 52 added checks cover the complete
worker/transport/bank path and lifecycle cases described above. Three changed
native source anchors match the sealed snapshot. The earlier producer build
`/var/tmp/slaaps-native-5v2lwsgw` is retained as compile-only evidence before the
final lifecycle changes; it is not the final runtime-qualified candidate.

Next verify the active controller/transport headers with the NuttX target
compiler, then attach the production supervisor, selected sensor/input path and
physical driver transaction. The last board image does not include this active
control path, so its remaining flash cannot be assumed sufficient for attachment.

## Active-path NuttX portability and resource probe (2026-10-20)

The active controller/transport headers were compiled with the retained
`px4_fmu-v6x_slaaps` compiler command, including Cortex-M7 hard-float, NuttX flat
configuration, `-nostdinc++`, disabled exceptions/RTTI and production warnings.
The first probe failed because `<algorithm>` is unavailable. ManualEdfInput,
ManualOutputBank and ManualWorker now use PX4 math minimum helpers and UINT64_MAX
instead of unavailable standard-library algorithm/limits facilities. Deadline
selection and overflow rejection semantics are unchanged.

`Tools/slaaps/probe_active_target.py` stages the current SLAAPS headers and a
forced-interface probe while mounting the retained board source/build read-only.
Final evidence `build/slaaps_active_target/20261020-verified` compiles producer
creation/entry/cycle/stop/destruction and both bank endpoint interfaces, including
construction/destruction, with ARM GCC 13.2.1. Its 22 input source hashes, 213
compiler dependencies and resulting object rehash successfully. The initial
failure and intermediate probe remain in separate attempt directories.

Final unlinked object: 9,362 B text, zero static data/BSS. Target `sizeof` values:
EDF owner 848 B; worker transport 1,632 B; MAIN and AUX transport 1,336 B each.
Their 5,152 B combined object storage excludes dynamic uORB allocations, supervisor
state and stacks. Largest individual compiler-reported stack frame: 712 B;
this is not call-chain stack use or runtime high-water evidence.
Object SHA256 `6e40de9ec85e46b182025ad73628819c0006eac9d6286adc21ce4fdaddf912c0`.

This is an unlinked API/resource probe, not a new firmware artifact or proof that
the active path fits the remaining flash. A production link must also include
out-of-line controller dependencies, supervisor and physical driver attachment.
The prior accepted board artifact remains unchanged and predates this fix.

Post-fix regression: host `/var/tmp/slaaps-configuration-dwx_y5zi` passes 1,879
checks in each Debug/Release/ASan-UBSan configuration and all 175 parameter
metadata definitions; all 83 input hashes match. Native build
`/var/tmp/slaaps-native-1da46ouc` links, ELF SHA256
`fb32f1532b14b3ee8a0c045f58648bdf66c771d6700fb0e5ba45c501416903ea`.
Runtime `/var/tmp/slaaps-native-runtime-t37a43n4` passes all 2,034 checks and
117 successful supervisor/runtime commands. The three portability-corrected
headers match the native sealed source. All verification processes are terminal.
Next production supervisor/selected-state/driver attachment and a linked active
resource check; no firmware fit or physical operation is inferred from the probe.

## Shared selected-input observation (2026-10-20)

`SelectedStateObservation.hpp` now owns the common single-estimator capture in
`src/lib/research_stabilized_manual`. It preserves original sensor/manual sample
timestamps, selected gyro/accelerometer identity, reset counter, atomic publisher
revision capture and a monotonically changing source epoch. Estimator alignment,
selected-device agreement and status freshness are metadata prerequisites only.
The raw `actuator_armed` observation and its freshness are separate from sensor
metadata validity, so armed sensor observation does not grant output permission.
Numerical checks, manual sender/range/freshness checks, epoch binding across a
control session and flight-health/condition admission remain required.

The existing module `SelectedStateInput` delegates capture to the shared library
and retains its disarmed, no-kill/lockdown/termination/ESC-calibration gates. Its
prearmed servo condition is unchanged. `PublisherBinding` also moved into the
library with a compatibility include at the original module path.

The production SLAAPS supervisor now captures these observations for applied
manual configurations at its existing 100 ms diagnostic cadence. `slaaps_status`
and `slaaps status` expose metadata validity and source epoch. The one-second
status freshness bound is diagnostic, not a control-loop timing contract. The
new fields are internal uORB diagnostics; no MAVLink parameter or QGC protocol
was changed. All mode availability masks remain zero, and no worker or physical
driver is admitted by these observations.

Native tests run before commander or sensor modules start to own the synthetic
publishers exclusively. They restore estimator parameters before the normal
supervisor lifecycle tests. Verification results are recorded below.

Initial native link `/var/tmp/slaaps-native-viqvcw26` passed, but its runtime
`/var/tmp/slaaps-native-runtime-xm494ju0` failed the new restart fixture. The
fixture incorrectly called `advertise()` on an explicitly unadvertised
`uORB::Publication`, whose stored handle remains non-null. The corrected test
uses a distinct publication object, as the existing transport lifecycle tests
do. The failed evidence is retained and is not a passing qualification.

Corrected native build `/var/tmp/slaaps-native-_vc0agyo` passes compile/link and
source sealing. Runtime `/var/tmp/slaaps-native-runtime-2o2ql6u_` passes all
34 selected-input checks, 2,034 existing control checks, and 117 successful
registration/configuration/restart/routing commands. ELF SHA-256:
`b75875b449f2d981f7a5ccfa18dc8ba84cd1fe338c4fc484bf5d89b4a757cab2`.
Seven changed production/test sources independently match the sealed manifest.
The verifier checks initial input metadata invalid/epoch zero and retains zero
mode availability. All test processes are terminal. No board build, hardware
operation or QGC GUI acceptance is claimed for this refactor.

## Selected-input session binding (2026-10-20)

`SelectedManualInput` binds all seven observed publisher revisions, selected
gyro/accelerometer IDs and source epoch at worker admission. Each cycle checks
that those identities remain unchanged, metadata timestamps/capture time do not
regress, and estimator/flags/permission samples remain fresh at consumption.
It requires observed armed state and rejects kill, lockdown, termination and
ESC-calibration. Freshness limits are explicit supervisor policy inputs with no
permissive defaults. The observation now retains the original flags timestamp.

`ManualWorkerTransport` accepts the complete selected snapshot through new
entry/cycle overloads. Metadata faults stop the actual controller and shared EDF
owner before another publication, including while bank replies are pending. A
raw-sample cycle cannot bypass an already selected-input session. Failed entry
consumes the common-owner session, and corrected inputs cannot recover a faulted
worker without deliberate new admission. Existing controller/manual numerical
and sample-continuity checks remain in the real worker, without duplication in
the metadata guard. This does not authenticate publishers or establish a flight
permission: the supervisor must still supply independently admitted permission,
output bindings and advertisement identities. Production scheduling/admission
and physical-driver attachment remain outstanding.

ARM probe `build/slaaps_active_target/20261020-selected-input-verified` forces
compilation of both selected-snapshot worker methods and both bank transports
with retained Cortex-M7/NuttX board flags. Unlinked object text is 10,116 B;
worker object 1,736 B, EDF owner 848 B, each bank 1,336 B. Largest individual
static stack frame is 712 B. This is neither incremental linked flash cost nor
full call-chain/runtime stack measurement. Initial probe
`20261020-selected-input` failed because its overlay omitted a sibling library
header; the corrected probe includes the current shared library headers.
All 32 probe source bindings and 234 compiler dependencies independently rehash.
Corrected method routing was captured before the native snapshot was sealed.
Native build `/var/tmp/slaaps-native-0b9tf4_a` and runtime
`/var/tmp/slaaps-native-runtime-u6w9qkp4` pass 2,157 control checks (123 new),
34 input-observation checks and 115 successful supervisor commands. ELF SHA-256:
`a326a5c18f7aa266f0cbdf8c4178ecd3e746a2e17cbe2da5617ea426c8298898`.
The four changed native production/test sources match the sealed manifest.
All jobs are terminal; no hardware operation or complete firmware link occurred.
The supervisor still only observes inputs until flight/output admission is attached.

## Supervisor-owned inactive endpoint (2026-10-20)

The production `Slaaps` supervisor now owns one `EdfOwner` and one
`ManualWorkerTransport`, declared in that order so the worker is destroyed
before its shared propulsion owner. Startup advertises the command endpoint
after parameter-schema initialization and fails if the endpoint cannot be
created. It publishes no command and performs no worker admission. Shutdown
stops an active manual worker and revokes the EDF owner before unregistering
modes. Configuration ineligibility stops an active manual worker.

The endpoint pins its own advertisement revision. A replacement cannot be
adopted merely by calling `advertise()` again, and entry must match that owned
revision. The supervisor exposes the live revision (zero if lost) and worker
active state in its internal status. These fields grant neither driver nor
flight permission. Allocation, boot-time object construction and advertisement
are now production code; active session scheduling/admission remain pending.

Synthetic worker/bank tests now finish before commander and the supervisor
start, because their test publishers must not replace the supervisor's live
endpoint. A separate read-only native test binds the actual supervisor revision
and requires `Waiting` (no current-lifetime payload), inactive worker and zero
mode availability. Startup plus three restarts exercise this boundary. The
standalone link receipt no longer claims whether ownership is attached; runtime
lifecycle evidence must establish that separately.
Native build `/var/tmp/slaaps-native-g2703s75` and runtime
`/var/tmp/slaaps-native-runtime-06gm3u6e` pass 2,157 control checks, 34
input-observation checks, all four supervisor endpoint lifecycles (16 checks),
and 120 successful runtime commands. Each restart produces a distinct owned
revision and no current-lifetime command payload. ELF SHA-256:
`8b0c0c3cddf8aabf128694f5cb56466aa24a3fbd6a98f7c2b9774dd3a2818e4b`.
All four changed production/test sources match the sealed native snapshot.
All jobs are terminal. This increment is native verified; the preceding ARM
probe and firmware artifacts do not qualify the new supervisor ownership.
Next integration must generate fresh admission session IDs across supervisor
restarts, attach the driver handshake and qualified flight permission, and
schedule selected-snapshot cycles at the configured control period.

## Serialized manual execution scheduling (2026-10-20)

The supervisor now contains the active selected-snapshot cycle path and switches
its requested work-queue interval to the lesser of the validated minimum control
interval and the 100 ms supervisor interval. Registration, routing diagnostics
and status publication remain on a separate 100 ms maintenance cadence. Arming
check replies and parameter invalidation are serviced on every run. Shutdown
returns to the slow interval before draining mode unregister requests.

An active cycle captures inputs, then vehicle status, then reads decision time.
It requires the configured manual mode to be selected and armed with fresh
vehicle status, an eligible configuration, and the owned command endpoint. The
shared execution gate revokes the worker on lost mode/configuration/endpoint;
otherwise it calls the selected-input transport path with the existing permission
and binding. It never renews permission or command expiry by polling. Slow
maintenance may still cause scheduling latency; requested intervals are not
proof of deadline compliance or board stack/CPU margins.

The production admission transaction is still absent. Its permission, binding
and input-policy fields remain empty, no worker `begin` is invoked, and all mode
availability masks remain zero. Completing admission must atomically establish
these fields, fresh session identity, commander/driver ownership and the qualified
flight-loss/condition policy. The full active `Slaaps::Run` path cannot yet be
claimed exercised by a real admitted supervisor. Native tests exercise its shared
execution gate with the actual worker/controller/EDF and verify idle lifecycle
cadence in the actual module.

Timing validation now requires cycle maximum, output lease and initial manual
request age to extend strictly beyond the earliest permitted integration tick.
Expiry checks reject at the deadline itself; equality previously admitted a
configuration that could never issue its first valid command. Rejections identify
`SLA_CYCLE_MAX`, `SLA_OUT_LEASE` or `SLA_IN_AGE` respectively. Bench validation
remains unchanged.
Verification results follow.

A recovery-mechanism question is pending from 2026-10-20: whether loss of
attitude estimation/control authority in powered flight should invoke a
parachute, another recovery system, or propulsion cutoff. Do not infer an
airborne policy from the existing bench inhibit or `RecoveryRequired` status.
Independent software integration can continue while this decision and measured
vehicle data are pending.

Host `/var/tmp/slaaps-configuration-1rjyj4wi` passes 1,882 checks each in
Debug, Release and ASan/UBSan, plus generation of all 175 PX4 parameters; all
87 input hashes match current sources. Earlier host `aud5bmif` is retained.
Intermediate native `/var/tmp/slaaps-native-g4_him5o` and runtime
`/var/tmp/slaaps-native-runtime-y3v5zq3q` pass 2,182 control checks, 34 input
checks and four endpoint lifecycles, but precede two native test-only changes
that exercise successful cycles through the execution gate. The final native
snapshot `/var/tmp/slaaps-native-7sgk_m1l` includes those changes.

Final native build `/var/tmp/slaaps-native-7sgk_m1l` and runtime
`/var/tmp/slaaps-native-runtime-5kg4gbbd` pass all 2,182 control checks, 34 input
checks, four endpoint lifecycles with requested idle-cadence checks, and 130
successful runtime commands. ELF SHA-256:
`49168973a334912c400675a3d7588bc13a4f8654faf88a36d4bdb1d54babfdbf`.
Five changed production/test sources match the sealed snapshot. Full active
supervisor scheduling is still not exercised without admission. All jobs are
terminal; no hardware was operated. Because `Slaaps::Run` now references the
active controller path, the next board link must assess actual flash and stack
feasibility rather than reuse the earlier inactive-supervisor firmware evidence.

## Full active-loop board link and flash capacity (2026-10-20)

Using the build-px4 workflow, a new read-only-parent-derived Docker volume
`px4-slaaps-active-loop-20261020` refreshed 15 source paths and built the current
`px4_fmu-v6x_slaaps` target. Stage `build/slaaps_firmware/active-loop-20261020`
passes independent package/bin/ELF/version/source verification, but the image
uses 1,965,132 B of 1,966,080 B flash: only 948 B remained. This is retained
evidence, not the selected development artifact.

The dedicated SLAAPS board variant now omits `CONFIG_MODULES_VTOL_ATT_CONTROL`.
Its startup explicitly does not run stock control/allocation modules, and the
SLAAPS commander check rejects stock flight modes. Stock winged-VTOL transition
control is not a supported recovery mode for this fixed-EDF vehicle. No stock
`default.px4board`, baseline, driver, estimator or manual-input configuration was
changed. This is a dedicated SLAAPS target, not a replacement stock VTOL image.

Derived volume `px4-slaaps-active-loop-headroom-20261020` builds successfully.
Stage `build/slaaps_firmware/active-loop-headroom-20261020` independently matches
all 88 changed production anchors and the package's decompressed image to both
the bin and two ELF flash-load segments. The correct embedded version is
`v1.18.0-beta1-679-g472a7d6786f` at dirty checkpoint `472a7d6786`. Image
1,943,368 B; headroom 22,712 B; linked AXI SRAM 111,356 B. The 21,764 B reduction
comes from excluding that unused controller and associated generated content.
Runtime heap/stack use is not included in linked static RAM figures.

The final package is
`artifacts/px4_fmu-v6x_slaaps_472a7d6786_dirty_output.px4` under the final stage,
1,842,548 B, SHA-256
`094353fb3f385383579edacb3e22d634bdbcfe047d7689875e1a8360d7e7fdd6`.
An additional retained effective `boardconfig` and symbol check prove the actual
stabilized cycle and EDF sampling code are linked, SLAAPS is enabled, stock
VTOL control is absent, and native test commands are excluded. The first config
lookup assumed `.config`; its failed log is retained, and the successful check
uses this PX4 build's actual `boardconfig` file.

The package was staged and rehashed at
`/tmp/px4-firmware/px4_fmu-v6x_slaaps_472a7d6786_dirty_active_loop_headroom_20261020.px4`.
That path is temporary; the workspace package and receipts are the retained
artifacts. All processes are terminal. Nothing was flashed or boot-tested.
Flight admission remains disabled; driver integration, qualified recovery policy,
measured vehicle data and runtime scheduling/stack qualification remain required.
The new headroom must be remeasured when those paths are linked.

## Non-actuating bank preparation (2026-10-20)

`ManualBankPreparation` stores a bounded, one-use proposal tied to the full
validated configuration, exact routing plan, session/source, command publisher
revision and every participating bank's reply publisher revision. Preparation
produces no PWM candidate, EDF permission or handled acknowledgment. Failed,
expired and consumed proposals require a new session token. Activation rechecks
routing and identity before the existing bank independently checks armed flight
permission. A proposal cannot disturb an already active bank.

`ManualBankTransport` pins its own reply advertisement lifetime and exposes
`prepare` / `beginPrepared`. A replacement bank rejects a retained proposal
addressed to its predecessor; an existing object cannot adopt a replacement
publisher. Shared routing equality includes channel assignment, protocol, rate,
calibration, disarmed/failsafe values and reversal. Raw trusted `begin` remains
for existing core callers; production driver integration must use the prepared
path. No prepare/ready wire protocol or physical driver attachment exists yet.

Final host evidence `/var/tmp/slaaps-configuration-xiqp8bo_` passes 1,932 checks
in Debug, Release and ASan/UBSan plus 175 generated parameters; all 88 inputs
rehash. The negative bank-set test first failed in
`/var/tmp/slaaps-configuration-_j2orxk6`, exposing a missing validation loop;
that failure is retained and the corrected contract passes. Earlier preparation
receipts are intermediate evidence, not the final revision.

Native build `/var/tmp/slaaps-native-f67d12gr` and runtime
`/var/tmp/slaaps-native-runtime-df4yx30j` pass 2,276 control checks, 34 input
checks and four actual supervisor endpoint lifecycles. Five changed production/
test files match the sealed snapshot. ELF SHA-256:
`42dde9e51e711b1e39080e9556d52103aad9d1790b76f0626e7470310eec6097`.
This exercises synthetic in-process preparation and memory handling, not a
physical driver, active production admission or QGC GUI acceptance.

Final ARM evidence `build/slaaps_active_target/20261020-prepared-bank-verified`
compiles both bank templates with actual board/NuttX flags. All 34 source bindings
and 235 compiler dependencies independently rehash. Unlinked object text is
11,500 B; EDF owner 848 B, worker transport 1,736 B, each bank transport 1,552 B.
Largest individual static frame is 712 B, not total stack usage. Object SHA-256:
`b12636173c42cf87b03b4872f46876b2dac98faa2a9fe0aea30bf5303a131ffb`.
The previous full board package predates this preparation API; its flash headroom
cannot stand in for a future link with admission and driver attachment.

Next work is the prepare/ready transport and supervisor transaction, followed by
serialized PWMOut/PX4IO attachment and actual handled acknowledgments. Duplicate
wire proposals must be deduplicated by that adapter; the typed core deliberately
rejects replayed session tokens. Measured vehicle data and the requested recovery
mechanism remain human inputs. Independent transport/software work can continue.
No hardware was operated, no flight permission was enabled, and no commit was made.

## Preparation/readiness message contract (2026-10-20)

`SlaapsManualPreparation` defines separate proposal, MAIN readiness and AUX
readiness topics. The common payload echoes every proposal identity field,
including peer acknowledgment lifetimes and original deadline. A proposal uses
REQUEST_BANK and zero prepared time; replies identify their actual bank and the
original preparation time. This is an internal protocol with no MAVLink or QGC
changes and no activation grant. `ManualPreparationTransport` validates message
role, publication ordering, bounded expiry, participating bank identities and
exact proposal equality. Failed decode preserves both caller outputs. Shared
request validation is also used by the bank preparation core.

Host evidence `/var/tmp/slaaps-configuration-po3p369q` passes 1,959 checks each
Debug/Release/ASan-UBSan and 175 generated parameters; all 90 source bindings
rehash. The 27 new checks exercise proposal/reply round trips, cross-bank and
role rejection, malformed timestamps and identities, original expiry and exact
peer/deadline matching. These are generated-header codec checks, not a supervisor
transaction. The transport adapter must still bind actual publisher lifetimes,
deduplicate retained proposals and compare readiness against the active proposal.

The previously verified ARM preparation probe predates this shared validation
refactor and the new wire schema. Neither that probe nor the earlier firmware
package establishes current protocol attachment or flash fit. Physical driver
consumers and supervisor admission remain pending; availability remains zero.

Native build `/var/tmp/slaaps-native-vy_7mdmv`, runtime
`/var/tmp/slaaps-native-runtime-9pa4j6i9`, passes 2,309 control checks, 34 input
checks and four supervisor endpoint lifecycles. Six new native checks publish
and decode all three preparation topics through atomic lifetime-bound
subscriptions. Five changed production/test sources match the snapshot.
ELF SHA-256 `5c13371d88782d361ac106592b72c83a1d6770ece00139c469a44585fdaa4c5d`.
No driver was attached or operated. All test jobs are terminal.

## Bank preparation endpoint (2026-10-20)

`ManualPreparationEndpoint<Bank>` now connects a lifetime-bound proposal
subscription to the existing bank preparation and readiness publication. The
trusted caller supplies the expected proposal advertisement revision. The endpoint
pins its own readiness publisher lifetime; replacement cannot be silently adopted.
The driver must serialize endpoint, bank, inventory and configuration operations,
and preserve the bank instance across preparation sessions.

The first valid proposal prepares once and publishes an exact readiness echo.
Retained or republished duplicates preserve the original preparation/publication
times and deadline. Changed same-session content, malformed roles, expired data,
backward polling time, changed configuration/routing and replaced proposal,
command, handled-reply or readiness publishers cancel preparation. Every ready
poll rechecks live bank state. Cancellation and consumption publish a zero-identity
invalidation payload that fails the readiness decoder, replacing retained positive
readiness. This invalidation is not a negative flight grant or recovery command.

`activate` polls once more and then consumes preparation through the bank's
independent trusted armed/flight permission checks. Later preparation traffic or
cancellation does not revoke an already active flight bank. No physical outputs,
supervisor admission transaction or flight availability are enabled by this API.
The complete routing inventory and eventual activation binding must come from
the trusted driver/supervisor integration; a decoded proposal is not provenance.

Host `/var/tmp/slaaps-configuration-putueyok` passes 1,959 checks each Debug,
Release and ASan/UBSan plus 175 parameters; all 91 source bindings rehash.
The host run covers existing core/codec regressions; the uORB endpoint itself is
exercised by the native fixture. ARM and full-board evidence remain earlier
milestones and must be refreshed after the new endpoint is attached.

Native build `/var/tmp/slaaps-native-u2hg106n`, runtime
`/var/tmp/slaaps-native-runtime-q9kubd_z`, passes 2,443 control checks, 34 input
checks and four supervisor endpoint lifecycles. Four changed source files match
the sealed snapshot. The 134 added checks exercise MAIN/AUX endpoints, initial
silence, non-actuating readiness, duplicate preservation, one-use activation,
active-bank isolation, configuration/routing edits, expiry, backward time,
same-session mutation, publisher replacement, wrong topic role, denied flight
permission and explicit cancellation. Native ELF SHA-256:
`a1a73ba7e75cda3ed924aac298eb331abfdc66b7692f2bf15337ca8ec07996e0`.
All jobs are terminal; no physical driver has been attached or operated.
Next is the supervisor transaction requiring readiness from every participating
bank, followed by serialized driver attachment and qualified activation/recovery.

## All-bank preparation coordinator (2026-10-20)

`ManualPreparationCoordinator` owns the proposal publisher and binds every
participating readiness publisher before publishing a request. The caller supplies
trusted expected readiness revisions. Begin validates measured configuration,
complete routing, request metadata and live command/handled-reply lifetimes;
failed entry consumes its session token. Exact pre-proposal reply payloads are
captured as baselines and cannot satisfy the new transaction. All participating
banks must echo the entire proposal before the coordinator reports Ready.

Polling continues to validate configuration/routing, deadline, monotonic time,
proposal/command/handled-reply/readiness lifetimes and exact reply identity.
An already accepted reply cannot change its preparation/publication time or
return to a pre-proposal baseline. Cancellation publishes an invalid proposal so
listening banks discard preparation. A new inactive transaction does not emit a
transient cancellation before its proposal, avoiding an unnecessary listener
cancellation race. Replacing a pending transaction explicitly cancels it; callers
must coordinate listening again before a fresh proposal.

This API collects readiness only. It is not attached to production Slaaps::Run,
does not grant flight permission and does not implement distributed activation
or rollback. Keep the proposal live during the separate trusted activation phase;
bank activation consumes/invalidates its readiness. The supervisor must transition
out of readiness collection when activation starts, track activation completion
separately and implement failure handling before any physical output attachment.
No physical driver, QGC behavior or availability mask is changed here.

Host evidence `/var/tmp/slaaps-configuration-6a_4weui` passes 1,959 checks each
Debug/Release/ASan-UBSan and 175 parameters; all 92 input hashes revalidate.
Native build `/var/tmp/slaaps-native-0dimfwqb`, runtime
`/var/tmp/slaaps-native-runtime-gm4ecx43`, passes 2,567 control checks, 34 input
checks and four supervisor endpoint lifecycles. Four changed source files match
the sealed snapshot. Native ELF SHA-256:
`bf738dcc3091b4d144d8f66510c0ef2b5428c07503e3447af88a7e6afb96eced`.
The 124 added native checks cover both-bank coordination, partial readiness,
retained baseline exclusion, silent-bank expiry, independent activation,
configuration/routing edits, clock reversal, cancellation, publisher replacement,
same-publisher identity/time/role mutation and failed-entry replay rejection.
Three payload-mutation cases use controlled fixture publishers to isolate payload
validation from advertisement replacement; the normal path uses real endpoints.

Production Slaaps::updateRouting currently assembles diagnostic inventory in a
local variable from ordinary multi-instance subscriptions; it does not yet retain
a coherent admission snapshot with driver tokens/publisher bindings. Implement
that boundary and a distinct activation/completion phase before attaching the
coordinator to production control. Do not interpret the fixture's sequential
trusted bank activation as distributed activation/rollback qualification.
All jobs are terminal. No physical output or flight permission was enabled.

## Driver report observation binding (2026-10-20)

`OutputRoutingObservation` now enumerates every uORB output-bank instance and
atomically copies each report with its advertisement revision. It validates the
complete MAIN/AUX inventory, then rechecks all instances and captured publication
generations before accepting a snapshot. A changed enumeration, revision or
generation rejects the observation pass with UnstablePublisher. Successful
snapshots retain driver tokens, report times, publisher revisions and topic
instances indexed by physical bank, plus the complete effective inventory.

`Slaaps::updateRouting` now uses this collector and retains its snapshot. A failed
capture leaves the old snapshot untouched while clearing routing-valid state;
callers must check the current result. `sameReportedDrivers` compares captured
physical driver/publisher/instance identities independently of heartbeat times.
It is an identity comparison, not freshness, routing equivalence or permission.

This is observation consistency, not an atomic physical-driver transaction or
publisher authentication. Final driver handling still needs independent live
routing/permission checks. The new snapshot must be bound into the future
admission/activation transaction; this change does not populate flight permissions
or enable any mode. ARM/full-board resource evidence predates this new production
observation path and must be refreshed before vehicle deployment claims.

Host `/var/tmp/slaaps-configuration-t0ou9zq1` passes 1,959 checks per Debug,
Release and ASan/UBSan build plus 175 parameters; all 93 input hashes revalidate.
The shared host suite is regression evidence; native tests exercise the actual
multi-instance collector and production supervisor routing diagnostics.

Native build `/var/tmp/slaaps-native-zm0ehra4`, runtime
`/var/tmp/slaaps-native-runtime-2k17w7l4`, passes 2,585 control checks, 34 input
checks and four supervisor endpoint lifecycles. Four changed inputs match the
sealed snapshot. Native ELF SHA-256:
`efba15f7d50e3de7b662275ec00c851c7de1f38278ad04817df414bb328449f9`.
Eighteen new checks cover physical-bank/instance mapping, complete inventory,
heartbeat identity, changed device tokens, identical publisher restart, missing
and extra instances, stale/unready/duplicate/future reports and preserved snapshot
on failure. Existing production supervisor routing-fault checks also pass with
the new collector. The second-pass generation logic was reviewed against uORB's
atomic payload/generation/revision copy; no deterministic inter-pass race injection
was performed in this fixture. No physical output operation occurred.

Next, bind the captured driver identities and current inventory into preparation
and activation so a successful fresh observation cannot silently replace an
admitted driver's identity. Complete activation acknowledgments/rollback before
physical attachment; the snapshot alone is not an admission decision.

## Observation-bound preparation (2026-10-20)

`ObservedRoutingBinding` now binds validated configuration/routing and both
physical driver identities from a successful capture. The proposed participating
device tokens must match the actual report tokens. Explicit, nonzero capture-age
and report-age limits are required; report age cannot exceed the collector's
one-second ceiling. No vehicle timing policy is supplied by default. Checks
require current successful capture status, bounded age, monotonic capture/report
times, stable device tokens, advertisement revisions and topic instances, and
unchanged configuration/effective routing. A fault latches until explicit entry.
A subsequent fresh observation cannot repair an existing failed binding.

`ManualPreparationCoordinator::beginObserved` performs this binding before
publishing a proposal. `pollObserved` rechecks it throughout readiness collection.
The raw inventory polling interface rejects an observed transaction, preventing
a caller from bypassing capture/identity checks. Failed observed entry consumes
the proposal session token, as other coordinator failures do. Existing raw entry
remains a trusted core/fixture API; production integration must use observed entry.
The actual supervisor currently captures reports but does not yet start either
preparation path or grant flight permission. Activation completion/rollback and
physical output attachment remain outstanding.

Host `/var/tmp/slaaps-configuration-bobhr10t` passes 1,997 checks each Debug,
Release and ASan/UBSan build plus 175 generated parameters. The 38 added checks
cover heartbeat acceptance, cancellation, capture errors, driver and publisher
replacement, instance reassignment, future/zero/backward/stale timestamps,
configuration/routing mutation, explicit policy bounds and device-token mismatch.
These are controlled software fixtures, not measured timing qualification.

All 94 host source hashes independently revalidate. Native build
`/var/tmp/slaaps-native-3_biy2k9`, runtime `/var/tmp/slaaps-native-runtime-yybdmne_`,
passes 2,651 control checks, 34 input checks and four supervisor endpoint
lifecycles; five changed production/test sources match the sealed snapshot.
Native ELF SHA-256:
`8ae8279e318686c41d95ab7f3bdba8fdf4dbbc6efd8ac038553d961702a9e5e9`.
The 28 new native-only checks combine observed entry with both actual bank
preparation endpoints, and reject capture failure, replacement identity, future
capture time and raw-poll bypass. Failed observed entry cannot reuse the repaired
session. Snapshot values in those cases are explicit fixtures; real report capture
has separate native evidence from the preceding milestone. No physical driver was
attached or operated, and no production permission was populated.

All validation jobs are terminal. Complete activation acknowledgments and
partial-failure handling before attaching observed preparation to the production
supervisor and physical outputs. The previously requested recovery mechanism
and measured vehicle inputs remain pending; independent software work continues.

## Activation completion barrier (2026-10-20)

`ManualActivationBarrier` collects exact acknowledgments from every required bank
before allowing one-use consumption of the earliest original first-command
deadline. Its timing budget is explicit and bounded by the configured cycle
limits. Duplicate replies cannot extend activation or command deadlines.
`ManualOutputBank::activationAck` confirms only the initial command-wait state;
`ManualBankTransport` adds the actual command and handled-reply revisions.
These acknowledgments are not actuator-handled acknowledgments or electrical
feedback. Permission loss, invalid identity, missing peers, expiry and reversed
time prevent completion. On rejection the caller must cancel every required bank,
including a bank whose successful activation reply was lost.

Host evidence `/var/tmp/slaaps-configuration-ne9t1jdd` passes 2,046 checks per
Debug/Release/ASan-UBSan build and 175-parameter metadata validation. Native
`/var/tmp/slaaps-native-ecgq_q0f`, runtime
`/var/tmp/slaaps-native-runtime-5fnlz8fi`, passes 2,721 control checks, 34 input
checks and four supervisor lifecycles. The increment adds 49 shared checks and
21 native-only checks. Native fixtures exercise both actual bank transports,
synthetic first-command handling, lost replies and permission loss.

The barrier is not attached to production admission. Activation request/reply
publisher binding, cancellation transport, current health/observation checks and
physical driver execution remain integration work. The fixture's 5,000 us budget
is not a qualified vehicle default. No new flight availability is enabled;
current board resource evidence must be refreshed before deployment claims.
