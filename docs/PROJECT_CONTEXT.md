# Research vehicle: start here

Updated 2026-10-19 by Codex. Navigation snapshot, not flight acceptance.
Checkout: `slaaps-sitl`, based on PX4 `e880f1cd79`; previous source checkpoint
`bde4aea321`. This successor source checkpoint includes the October 19 diagnostics,
working-baseline port tooling, observer, portable output contract and handoff.
It is a local commit checkpoint, not a new remote publication. Firmware binaries
and retained WSL/build evidence remain local and require separate preservation.
The previous checkpoint included required PX4
formatting and whitespace cleanup in 23 files, including an equivalent split of
the clock harness's compile-time type assertions to avoid formatter oscillation.
Its seven affected graph anchors and handoff were refreshed after review.
The current checkpoint also revalidates graph anchors and formats changed C/C++;
the unrelated empty `timestamp` file remains outside the commit.
Historical evidence retains its original source bindings and acceptance scope.
Current default isolated mode: [31-case revision-aware qualification and runnable console](STABILIZED_MANUAL_ISOLATED_MODE.md).
Latest stabilized software: [portable output contract](STABILIZED_MANUAL_OUTPUT_CONTRACT.md)
adds explicit producer/consumer sessions, calibrated four-servo mapping, matching
acknowledgments and independently expiring leases. Permission loss suppresses
fallback until deliberate recovery. Debug/Release/ASan-UBSan each pass 126,013
checks, including 2,737 new protocol checks and an actual control-core mapping
sequence. A separate ARM/NuttX 11 object compile passes: 2,068 text bytes, no
static data/BSS; no new firmware link or runtime driver integration is claimed.
Evidence is `/var/tmp/research-stabilized-host-lx60q8nq` and
`build/stabilized_output/20261019/verification.json`; all 234 host files,
111 ARM dependency snapshots and three prior observer firmware artifacts
independently rehash with zero mismatches. Next software work is the runtime
supervisor/transport, independent driver consumer, actual application/revoke
acknowledgments and scheduled transition/loss verification. No hardware operated.
Latest staged hardware candidate: [G9 output-disabled hardware observer](STABILIZED_MANUAL_HARDWARE_OBSERVER.md)
now builds on the working `e8ea98b404`/NuttX 11 baseline. Final Debug/Release/
ASan-UBSan each pass 123,083 control plus 193 profile checks; native module
input/fault/recovery verification passes 9,015 checks with no actuator publication.
The separate Pixhawk image fits with 4,376 bytes of flash remaining, contains the
controller/allocator, and excludes the bench owner and test publishers. It has
not been flashed or hardware-tested. Explicit profiles, diagnostic status and
latched observation lifecycle are implemented; active outputs, production health/
publisher identity, measured runtime resources and vehicle-supported tuning remain
open. Existing bench firmware and sealed 31-case SITL default remain intact.
Current user task: progress the manual servo bench toward hardware commissioning,
then vehicle-supported stabilized manual control. The new
[flash and test procedure](MANUAL_BENCH_FLASH_AND_TEST_PROCEDURE.md) defines G0-G10,
measured exit conditions and the separate stabilized hardware integration.
Latest milestone: [step 3 working-baseline bench port](MANUAL_BENCH_WORKING_BASE_PORT.md)
passes fresh software qualification and has a new ARM image. It uses the exact
working PX4 `e8ea98b404` / NuttX 11/apps pair in isolated source views; the active
newer branch remains unchanged. The user now reports the changed candidate's cold
boots passed and `research_bench_control status` reports not running. The latest
topic confirms disarmed state and buttons zero. The user corrected the last roll
pair to right then left and explicitly verified left=-1/right=+1; the earlier
reversal conclusion is superseded and calibration stays unchanged. USB telemetry
over 60.062797 seconds shows no RX parser/buffer/drop indicator changes or new
TX overruns. The user applied the ordered setup and its console-reboot readback
matches the requested MAIN routing/rates/limits/centers and FMU off-values;
AUX1..9 are zero, REV0 and safety-button prearm1. Actual armed=false/prearmed=true,
owner stopped and IO FMU-data OK are confirmed. IO first-four failsafe entries
initially remained1000 after console reboot despite FMU/mixer0. The subsequent
requested cold-check response now reports actual first-four failsafe/disarmed
zeros and saved MAIN_FAIL1..4=0, with the expected configuration and disarmed/
prearmed state. This passes the current table check; baseline zero-write-ignore
semantics remain unchanged and future configuration/reset changes require recheck.
The user executed the USB profile recipe: its complete five-line/44-token board
readback matches the staged fixture, start reports Ready/state0, both permission
snapshots remain disarmed/prearmed with fault flags clear, and IO reports current
PWM zero and first-four disarmed/failsafe zeros. Stop reports not running. The
[hardware Ready-only result](../build/bench_commissioning/bench-e8ea98b404-step3/hardware-usb-profile-ready-result-20261019.json)
passes that subset; no enabled session or pin measurement is claimed. No scope/
pulse instrument is currently available (scope possible later), so the next
stage is profile byte-integrity evidence and G4 signal capture preparation.
Enable, full lifecycle and physical timing/fault tests remain pending equipment.
A USB-bound 44-token synthetic profile is staged and
passes a native Ready-only parse/start/stop check without enable or hardware.
The fresh console matches candidate runtime identities
and build strings and confirms USB instance 2 / sender 255:190. PX4IO responds;
the earlier all-disabled routing explained its stale PWM-data flag; the applied
configuration now reports FMU-data OK. This is not output
acceptance. Retain remaining startup/configuration evidence alongside G3.
The existing H10 candidate is staged under
`build/bench_commissioning/h10-e880f1cd79-dirty/`; its 726 retained artifacts and
58 compiled source bindings were revalidated for the October 13 checkpoint. The
new port and extended fixture have separate fresh evidence. Candidate preparation did not
rebuild or operate hardware. A blank acceptance record is under `docs/templates/`.
The procedure's local links, code blocks and command/source contracts validate;
the blank record matches the existing checker and correctly remains incomplete.
Initial hardware event: the user reports successful H10 programming but no normal
QGC connection; rollback restores it. H10 is withdrawn from routine commissioning.
[Startup diagnosis](MANUAL_BENCH_H10_BOOT_DIAGNOSIS.md) records the working
PX4 `e8ea98b4`/NuttX 11.0.0 baseline versus H10's `e880f1cd79`/NuttX 12.12.0.
Package/startup/source checks are consistent, but a failed-image startup trace is
still needed to localize the newer failure; no root cause or bootloader
modification is established, and no physical bench outputs are accepted.
The SD listing has no top-level `fault_*.txt` dump. The supplied 7,542-byte
`param_import_fail.txt` identifies PX4 1.16.1 and the working rollback, including
corrupt storage followed by successful SD-backup import; it has no H10 identity.
The working boot separately loads valid parameters. Corruption timing/cause are
unknown. The board's MTD partition manifest is unchanged across the bases.
With no debug adapter available, a clean stock comparison build of H10's exact
base is staged under `build/bench_commissioning/stock-e880f1cd79-diagnostic/`.
Its `.px4` SHA256 is `35945a63a8cf64bd1f80175cb975ac8063994b66fd7e9a29997c93545c065218`;
build exit 0, matching GCC 13.2.1 package, board ID 53, decoded image/bin match,
USB/console entry points present and research bench entry point absent. The
receipt pins eleven submodules plus Micro-CDR; image headroom is 18,676 bytes.
The user now reports this stock image also fails to reconnect, as `.px4` or
`.bin`. QGC Daily's supplied log reports verification/upgrade complete using an
ArduPilot ChibiOS bootloader, protocol 5, board 53, slot 1,966,080 bytes; rollback
still reconnects. Failure therefore occurs without the bench changes. The
ArduPilot label alone is not a cause. Cold flash persistence and exact loader
build were initially unmeasured. `Tools/bench_test/inspect_bootloader.py` reads
stored vectors, slot CRC and optional loader version with no flash/boot commands;
16 standalone serial-fixture tests and the candidate/uploader CRC comparison pass.
Expected stock CRC `0xde5dd361`, or `0xf573da68` with just the lead 32 bytes erased.
The user then reported COM14 appears briefly and vanishes. Codex's query-only
inspection completed on COM14 at 21:01:49 UTC on October 19: stored vectors and
full slot CRC match the stock candidate, with no erased lead bytes. Read
`cold-readback-20261019T210117038214Z.json` in the stock artifact directory. The
first port-open race is preserved separately. No flash, boot or parameter command
was issued. This confirms the measured stock payload was finalized and retained;
application handoff/startup remains unlocalized. The optional loader build query
got no reply. Identification now holds the loader until a power cycle; this is
an inspection effect. Next obtain a low-level trace or prepare a separately
qualified bench variant on the complete working rollback baseline. The detached
worktree/source volume and both original images remain preserved; those failed
images remain unqualified for application startup.
The user has now completed recovery step 1 and requested step 2. A clean stock
rebuild of the complete working rollback baseline is staged under
`build/bench_commissioning/stock-e8ea98b404-nuttx11-reference/`; read the
[reference build and cold-boot check](MANUAL_BENCH_STOCK_ROLLBACK_REFERENCE.md).
Its `.px4` SHA256 is `315f014c6acf50106fa51b1b345c6c9f3833dc8a3126ec9a69d1461ba6cf6334`.
Build exit 0, board 53, 989 compile entries without research source, matching
PX4/NuttX identities and image/bin hashes verify; 17,736 bytes of flash remain.
The private NuttX clone's release tags are frozen at 11.0.0 so both generated
version records match the rollback; no tracked source or host tags changed.
The first rebuild with newer tag-derived labels is separately marked superseded.
The user now confirms successful flashing and all three full cold-start checks:
QGC connection within 30 seconds and stability for at least 60 seconds each.
The supplied console matches PX4 `e8ea98b404` / NuttX `49f3b29c92`, reaches
sensor/PX4IO initialization and USB MAVLink with a valid GCS heartbeat and zero
USB transmit errors/receive loss. A valid 1,394-byte parameter document loads
with `SYS_AUTOSTART=2100` and `SYS_USB_AUTO=2`. The user deliberately reset
settings to stock and did not require a parameter backup; no preflash recovery
or retention comparison is required for this run. Step 2 startup is complete.
Read `hardware-startup-acceptance-20261019.json` beside the final package; it
supersedes the preclarification settings-change concern. One transcript set and
threshold confirmation were supplied, not exact per-cycle timing captures.
Step 3 now ports the bench onto this complete baseline with the H10 clock fixes,
configured-range mixer center validation, manual message v1 sender IDs plus the
retained v0 definition, and two older-receiver sender-ID assignments. The older
filter/work-queue/kernel/apps code remains pinned. Portable Debug, Release and
ASan/UBSan each pass 2,431 checks; 28 stock GTests and 19 acceptance-checker tests
pass. Three native runs each pass 20 scenarios / 3,632 checks on the same binary,
including the new center-range regression. Disabled-startup smoke also passes.
The ARM image is in `build/bench_commissioning/bench-e8ea98b404-step3/`; `.px4`
SHA256 `a45cf99374fa7ccfd4f6349ca9a6341bacfcfa6199254f71ab2b04f0cb2aed31`.
It is 1,854,271 package bytes, 1,957,752 image bytes, with 8,328 bytes remaining.
The independent check validates 70 mapped production files and 250 native
evidence files. The complete source patch reproduces all mapped files. The user
reports this image's cold-boot tests passed and the bench command is available but
stopped. Read the initial `hardware-boot-report-20261019.json` and subsequent
`hardware-usb-input-assessment-20261019.json` beside the candidate. The later
console matches base/OS/board/build identity, saved input parameters and valid
USB manual input from instance 2 / sender 255:190. Eight snapshots show sample
ages 2.1..40.7 ms and near-zero center, not continuous cadence or isolated-axis
acceptance. Seven initially had buttons bit 11 set; all nine follow-up samples
have buttons zero, and actuator_armed confirms armed/prearmed/ready-to-arm false,
with kill/lockdown/termination/calibration false. Read the separate
`hardware-usb-input-followup-20261019.json`: its roll-order conclusion is superseded
by `hardware-usb-input-correction-and-telemetry-20261019.json`. The user corrected
the final pair to right then left and verified left=-1/right=+1; leave calibration
unchanged. Two USB telemetry timestamps span 60.062797 seconds, adding 1,570
received messages with RX parser/overrun/drop indicators zero and TX-overrun
count unchanged at 391. Sequence misses increase by 15,045; cause and worst-case
input gaps remain open. These finite checks support disconnected configuration
preparation, not physical-output acceptance. The USB signal-only profile has
44 tokens and SHA256 1bf090fc08a058d00be6fb0f80f70a32f218c3df17695059faa6381524ea3c7f.
Only the qualified fixture's instance token changes 0->2; synthetic values stay
unchanged. Its separate native ready-only receipt records exact parsing, Ready
and stop on the same qualified binary, without enable/Commander/physical drivers.
Two mounted-path argument failures are preserved; a byte-identical whitespace-
free native copy passes. No process remains running. The subsequent initial
output readback and exact unapplied plan are retained in
`hardware-output-configuration-plan-20261019.json`. Actual MAIN timers0..2=400,
MAIN functions1..8=0, AUX4=101/Motor1, other AUX1..9=0 and prearm0 require setup.
`usb-signal-only-setup.txt` has 30 parameter writes and one AUX4 readback gate;
it disables AUX4 first and sets MAIN1..4 MIN500/MAX2500/CENT1500/DIS0/FAIL0,
REV0, timers0..2=50, functions10000..10003 and prearm1/safety button. Current
MAIN5..8 and other AUX functions are retained zero, including advertised AUX9
beyond the module's AUX1..8 guard. No start/enable/arm/reboot is included.
The user applied it and supplied the readback assessed in
`hardware-output-configuration-result-20261019.json`. FMU configuration matches,
owner stopped, armedfalse/prearmedtrue and IO init/data OK, but actual IO failsafe
table first4 is1000 while FMU/mixer failsafe is0. Baseline registers.c ignores
zero writes on the failsafe page (disarmed zero is accepted); the owner validates
FMU parameters and cannot detect that IO register mismatch. User clarifications
in `hardware-io-failsafe-followup-20261019.json` confirm console reboot and no
current scope/logic/PWM instrument. Its requested cold-check response is recorded
in `hardware-io-failsafe-cold-result-20261019.json`: actual IO first4 failsafe and
disarmed entries zero, MAIN_FAIL1..4 saved0, persisted MAIN configuration, owner
stopped, IO data OK and armedfalse/prearmedtrue with fault flags clear. This closes
the current table mismatch, consistent with prior retained IO state; it does
not change zero-write-ignore semantics or qualify physical fault waveforms.
Recheck the actual IO table after future configuration/reset changes. The user used
`usb-profile-create-ready-console.txt` and its `usb-profile-create-ready-plan.json`
for five echo lines, complete cat readback, start/status/permission/IO and stop.
The local 13-command recipe reconstructs the exact host profile bytes; linked
mkdir/echo/cat and pinned NSH source behavior were verified. The subsequent
`hardware-usb-profile-ready-result-20261019.json` records the matching complete
board cat output, Ready/state0, two armedfalse/prearmedtrue captures, IO PWM zeros
and first-four disarmed/failsafe zeros, then stopped status. The Ready-only subset
passes. Board file bytes are not independently hashed; obtain host/card readback
hash or verified transfer evidence before active acceptance. No enable occurred;
all physical gates remain open for scope, with the module now stopped.
The two new USB snapshots add 1,015 received / 9,435 missed messages. Source
review found `rx_message_lost_rate=missed/received`, printed directly with a
percent sign, so displayed 5.6%/8.1% are not valid packet-loss percentages.
Growing sequence counts remain unexplained; the later finite USB (type 3)
`telemetry_status` check above reports no new parser/buffer faults. Do not assume
its uORB instance always equals MAVLink instance 2. Smoothed MANUAL_CONTROL
rates 27.4/18.1 Hz show reception, not a worst-gap bound. PX4IO initially reported
all MAIN functions/PWM zero with stale FMU-data; the applied functions now refresh
FMU-data OK while present PWM still reports zero. Raw-PWM hardware enable is
not vehicle arming. Routing/rates and actual prearm state match; current cached
IO first4 failsafes now match zero. Installed profile/lifecycle and physical
behavior remain open. Listener counts
require `-n`; the procedure's positional-count commands were corrected. Build
receipts and the initial boot report remain immutable; no physical output is
accepted and no hardware command was issued by Codex.
Read the new port record, candidate/qualification receipts and source manifest.
Retain `/var/tmp/research-bench-step3-e8ea`, Docker volume
`px4-bench-e8ea-step3-20261019` and metadata worktree
`.agents/worktrees/bench-e8ea-step3`. The private native build excludes Gazebo;
tests capture actual PX4 behavior in memory without physical drivers/Commander.
Original H10 had 9,268 bytes of image headroom and remains withdrawn. Installed profiles,
actual PWM/fault captures and powered/wind reviews remain open; hardware owner-
stall/IO-loss test methods and the stabilized ARM integration still need development.
The prior [runtime-profile correction and 31-case checkpoint](STABILIZED_MANUAL_SM4_RUNTIME_PROFILE_FIX.md) remains preserved at its original artifact scope.
Recent bounded follow-up: [five noise and gyro-bias probes](STABILIZED_MANUAL_SM4_ROBUSTNESS_PROBES.md),
all observed checks pass but campaign acceptance remains at 31 cases.
[Four unequal-axis pilot-command probes](STABILIZED_MANUAL_SM4_MIXED_AXES.md)
also pass at level/body-X-up and both command orders; they remain exploratory.
[Three manual-link cadence probes](STABILIZED_MANUAL_SM4_CADENCE_EPOCH.md)
pass at 60/80 ms send periods; a separate frozen-clock EKF-stop attempt timed
out before restart and does not resolve general publisher-epoch identity.
[Six delayed-magnetic-packet probes](STABILIZED_MANUAL_SM4_MAGNETIC_DELAY.md)
pass at their exact 0/20/40 ms fixture settings, with notable level-case
run-to-run spread at 40 ms; they do not define a delay margin.
[Seven raw-IMU-delay probes and a 100 ms negative boundary](STABILIZED_MANUAL_SM4_IMU_DELAY.md)
exercise selected estimated-state timing; the positive runs pass at exact
0/20/40/80 ms inputs, and the 100 ms case rejects before any output.
[Four combined pilot/IMU timing probes](STABILIZED_MANUAL_SM4_COMBINED_TIMING.md)
pass at an 80 ms pilot send period and 80 ms IMU delay in both poses/orders;
the observed manual-age maximum is 88 ms, without a jitter guarantee.
[Eight repeated combined-timing probes](STABILIZED_MANUAL_SM4_COMBINED_REPEATS.md)
also pass under Gazebo seeds 43 and 44. Together with seed 42 these are 12
finite cases, not a reliability estimate or a timing envelope.
[Two combined-timing manual-loss probes](STABILIZED_MANUAL_SM4_COMBINED_LOSS.md)
pass at level/body-X-up: the consumer turns off 108 ms after the last manual
sample, and an explicit new session restores hold. Send phase remains uncontrolled.
[Four phase-controlled send probes](STABILIZED_MANUAL_SM4_PHASED_JITTER.md)
pass at alternating 76/84 ms pilot intervals and exact 80 ms raw-IMU delay;
one nose-up case reaches the exact 100 ms selected-rate boundary.
[Four single-packet omission probes](STABILIZED_MANUAL_SM4_PHASED_SKIP.md)
pass in hold/rate and level/body-X-up: one 160 ms send gap latches manual
fault 5, with explicit new-session recovery after centered packets resume.
[Selected-rate boundary diagnosis](STABILIZED_MANUAL_SM4_RATE_BOUNDARY.md)
locates a 20 ms angular-rate publication stall while relay IMU packets and
selected attitude continued; the gap's exact downstream cause is unresolved.
[Three stock-ULog rate-path repeats](STABILIZED_MANUAL_SM4_RATE_TRACE.md)
pass 19,423 active control cycles with selected-rate age at most 84 ms;
the earlier stall did not recur. Incomplete ULog publication coverage and
possible logger timing effects leave its cause and frequency unresolved.
[A separate direct gyro-epoch diagnostic](STABILIZED_MANUAL_SM4_GYRO_EPOCH.md)
corrects a queued-uORB capture error, then passes 6,478 nose-up cycles with
selected gyro and rate ages at most 84 ms. It can narrow a future recurrence,
but the original stall did not recur and remains unexplained.
[SM5 allocation-input preparation](STABILIZED_MANUAL_SM5_ALLOCATION_INPUT_CONTRACT.md)
now checks a one-condition body-FRD moment/servo data contract. The synthetic
fixture reconstructs its existing 3×4 matrix exactly; no measured vehicle
profile or physical acceptance is claimed.
[A separate generated-allocation SITL variant](STABILIZED_MANUAL_SM5_GENERATED_SITL.md)
now consumes the sealed synthetic profile and reports its full input hash at
runtime. Two exploratory mixed-axis cases pass 12,953 cycles; the accepted
31-case binary and scope are unchanged.
[A subsequent admission guard](STABILIZED_MANUAL_SM5_ADMISSION_GUARD.md)
recompiles the synthetic input and rejects a changed matrix even if a report
retains the old hash field. Its output header is byte-identical to the one
used in those two cases; no SITL or vehicle acceptance scope changed.
[A compiled C++ binding probe](STABILIZED_MANUAL_SM5_BINDING_PROBE.md)
then changes only copied diagnostic header values and observes distinct
allocator commands, proving the private configuration consumes them. It is
not a new SITL case or vehicle profile.
[A one-condition uncertainty rank screen](STABILIZED_MANUAL_SM5_INTERVAL_RANK.md)
now checks whether every matrix in a reported elementwise interval box has
full row rank. The synthetic zero-uncertainty fixture passes; this does not
establish vehicle moment authority or an operating envelope.
[A static incremental moment-box screen](STABILIZED_MANUAL_SM5_STATIC_AUTHORITY.md)
adds declared canard command margins and a source-bound synthetic demand.
Its local one-condition box passes; net trim, slew and vehicle authority
remain open.
[A one-condition static net-moment screen](STABILIZED_MANUAL_SM5_NET_AUTHORITY.md)
then includes a source-bound trim moment bias, uncertainty and physical net
demand. The zero-bias synthetic fixture passes its conditional existence
check; actual SLAAPS trim bias, reviewed demand and envelope remain missing.
[A compiled allocator finite-grid check](STABILIZED_MANUAL_SM5_ALLOCATOR_GRID.md)
observes the actual private C++ allocator at 2,662 synthetic requests from
trim. All output rows satisfy the declared bounds and report-consistency
checks; 10 ms slew limits substantially reduce one-step tracking. This is
host evidence, not a new SITL case or vehicle authority result.
[A command-history check](STABILIZED_MANUAL_SM5_ALLOCATOR_SEQUENCE.md)
then carries allocator output into the next 10 ms call for all eight synthetic
moment-box corners, reversal and release. Its sampled allocator residuals
fall below 0.0011 within 4/8/4 calls respectively; actuator lag and physical
moment settling are outside this host probe.
[A source-bound native actuator trace audit](STABILIZED_MANUAL_SM5_NATIVE_ACTUATOR_LAG.md)
then checks the sealed generated-profile SITL cases: all 12,953 enabled
fixture rows match timestamped PX4 commands, and logged synthetic surfaces
follow their declared 50 ms lag. It is not measured servo or vehicle evidence.
[A separate steady-bias SITL plugin](STABILIZED_MANUAL_SM5_STEADY_BIAS_SITL.md)
adds declared synthetic body torque only while simulated EDF thrust is on.
Two level/body-X-up mixed-axis cases pass the existing 14 checks over 12,931
control cycles; the accepted binary and 31-case campaign remain unchanged.
[A publisher-epoch API audit](STABILIZED_MANUAL_SM4_PUBLISHER_EPOCH_AUDIT.md)
shows that the isolated owner cannot distinguish a same-instance withdrawal
and re-advertisement entirely between polls when subsequent state remains
plausible. The core already rejects a changed supplied epoch; the adapter
currently supplies a constant. A real publisher-incarnation signal is needed
before claiming general source continuity. The unchanged host suite passes.
[A separate POSIX publisher-revision variant](STABILIZED_MANUAL_SM4_PUBLISHER_REVISION_VARIANT.md)
now binds selected payloads to uORB advertisement revisions under the node lock.
Fourteen native uORB checks and eleven synthetic SITL cases pass over 18,384
control cycles: eight hidden advertisement-cycle barriers, one frozen-clock
guard and two mixed-axis regressions. This covers observable advertisement
transitions; restarts entirely between owner polls and general publisher
identity remain open, and the accepted 31-case scope is unchanged.
[Actual EKF2 lifecycle checks on that exact variant](STABILIZED_MANUAL_SM4_PUBLISHER_LIFECYCLE.md)
now pass eight state-barrier cases over 3,968 cycles. Real stop/start advances
attitude/estimator/flags revisions while leaving the rate publisher unchanged;
heading resets and invalid-profile requests leave the tuple unchanged and
still revoke. Explicit fresh sessions recover; consumer shutdown is 4 ms.
These observed stop/start paths span owner polls and do not qualify every
restart hidden between polls or general publisher identity.
[The current fresh revision campaign and live console](STABILIZED_MANUAL_CHECKOUT_QUALIFICATION.md)
now pass: all 31 cases fresh, 68,813 control cycles and 1,475 independently
rehashed qualification artifacts. The default
console supports body-axis sticks, quaternion hold on release, disable and
explicit-session restart on separate ports/instance. Its live input, recovery
and cleanup check also passes. A [fresh build from checkout](STABILIZED_MANUAL_CHECKOUT_BUILD.md)
now builds both artifacts from 57,547 inputs and passes 14 native uORB checks
plus five focused SITL cases over 14,375 cycles. Its 387 receipt artifacts
independently rehash clean. That pair is now fully admitted and the console
default; its live check passes after correcting an observation race to check
the actual hold-entry row and reference retention separately. Five check and
41 session files rehash clean. Measured vehicle-supported allocation/tuning
remains open; the earlier 23-fresh/eight-inherited package is preserved.
The [completion audit and vehicle-data gate](STABILIZED_MANUAL_COMPLETION_AUDIT.md)
finds no measured profile in the known input location and rechecks the supplied
model's invalid inertia/mass conflict. SM5 remains data-dependent; no live
fixture process remains. The full goal is blocked on missing vehicle inputs, with its synthetic software
boundary closed and accepted physical inputs/requirements pending.
A script-construction bug omitted bias settings while metadata claimed them.
Historical numeric results remain, but matched-prior claims are withdrawn.
Live parameter comparisons now precede enable. The calibrated synthetic profile
passes all 31 declared native cases, including full-axis nose-up hold,
deterministic dispatch and fault/state-barrier recovery. The earlier
[three-of-four incomplete candidate](STABILIZED_MANUAL_SM4_COUPLED_CONTROL.md)
remains preserved with its failed physical score and corrected prior claims.
The user reports
vehicle measurements are not yet available; CAD is already pending.

## Resume with minimum context

Read root [AGENTS.md](../AGENTS.md), this file, then only the task's row below.
Use [RESEARCH_CODE_GRAPH.json](RESEARCH_CODE_GRAPH.json) for exact source anchors,
dependency edges, scope boundaries and SHA-256 freshness checks. It is a curated
component graph, not an exhaustive call graph. Existing
[architecture diagrams](RESEARCH_VEHICLE_ARCHITECTURE_DIAGRAMS.md) describe intent.

Read matching scoped instructions before code review/editing. Search symbols and
bounded line ranges; do not preload historical ledgers, all translated docs,
CAD inventories or full source files. Retain the model within a chat; reread
only changed dependencies. Show small diffs for changes and explain only
requested/modified code. End each completed turn with one sentence of codebase
state and whether a fresh chat is safe.

## Confirmed objective and constraints

Four independent X-canards, one fixed rear EDF, no main wing or thrust vectoring.
Companion commands inertial acceleration at CG in rotating body FRD, in m/s^2;
the vector stays body-fixed between updates. NED is the navigation frame.
Specific force differs by gravity. PX4 owns estimation, stabilization,
allocation and command-loss handling. Simulator truth is for scoring.
Pi 4B translator/TELEM MAVLink and Pixhawk 6X are intended deployment targets.
Launch (+X-up from rest) and net recovery are objectives, not validated behavior.
Provisional all-up mass is 3.041337 kg; expected 5.5 kgf thrust is unmeasured.
Synthetic software limits must not become physical controller defaults.

## Current implementation and evidence

Current user priority is **manual servo bench hardware commissioning followed by
vehicle-supported stabilized manual control**; follow the G0-G10 procedure above.
The existing isolated SITL work is preserved. [SM0 goal and input record](STABILIZED_MANUAL_SITL_GOAL.md)
captures the new direction and candidate feedback architecture. User confirmed
body-rate commands with attitude hold on release, a synthetic rotational fixture
at declared airflow and fixed simulated EDF thrust. [SM2 and host SM3](STABILIZED_MANUAL_SM2_SM3_VERIFICATION.md)
now implement an output-disabled quaternion-hold/body-rate owner, actual PX4 rate
PID and bounded four-canard allocator. Strict Debug/Release/ASan-UBSan each pass
123,083 checks, including 12 synthetic truth-feedback campaigns at level/nose-up
and all three axes. Final evidence `/var/tmp/research-stabilized-host-0bqkxvm9`,
manifest `a519c5feb3e00456ac5ac58867d2e46cebaa021665bc7f3ff016004673dff808`.
All 281 earlier mapped anchors were preserved in that host milestone.
[Follow-on SM3 estimated-state integration](STABILIZED_MANUAL_SM3_SITL_INTEGRATION.md)
now builds an isolated external PX4 module/Gazebo rotational fixture. Normal
sensors/EKF/rate feedback sustains centered hold for 1,255 cycles over 5.016
simulated seconds with fixed 10 N thrust; disable reaches the consumer. Physical
three-axis fixture rotation also passes. Evidence
`/var/tmp/research-stabilized-sitl-6ayynqfq`, manifest
`69d9aa65a7931c7273425180a043e42e2407512f7428bbb645d0160744ef5970`.
286 unrelated earlier anchors are preserved; new-core native float-warning
compatibility changes retain 123,083 checks each in refreshed Debug/Release/
ASan-UBSan (`research-stabilized-host-pue0bgy9`, manifest
`eea5cc3083c413893346d4e80e3f666fd58cdd00db14578b60a8b35a976f6dfe`).
[Earlier SM4 campaign checkpoint](STABILIZED_MANUAL_SM4_CAMPAIGNS.md) records
12 level/body-X-up, three-axis, both-sign rate/release/impulse cases and six
fault-and-explicit-recovery cases using PX4 estimates. Audit: 38,617 published
cycles and 302 preserved checkpoint anchors. Evidence
`/var/tmp/research-stabilized-sitl-r27tx0nr`, manifest
`d60c0465e69c7838b76d59cb14fbf039544338dee53122b80a5e549afa8cbac1`.
Worst physical hold error is 0.006601 rad against the unchanged 0.02 rad limit;
independent stalled-producer expiry is exactly 100 ms. Private sensor relay fixes
legacy magnetic transport without changing stock GZBridge. The zero-bias synthetic
IMU profile intended initial gyro/accel bias sigma 0.003394 rad/s and
0.04 m/s^2, but the construction bug omitted those settings. Matched-prior
claims are withdrawn; see the corrected-profile record above. Earlier run
`research-stabilized-sitl-edg6vbe0` is sealed **incomplete**, retaining its
11/12 positive cases and failed nose-up physical error 0.0200047373 rad.
[SM4 native state barriers](STABILIZED_MANUAL_SM4_STATE_BARRIERS.md) additionally
pass eight level/body-X-up reset, publisher-withdrawal, rapid same-ID restart
and unsupported multi-estimator-profile cases with explicit recovery.
Audit: 3,994 cycles / 313 preserved anchors; evidence
`research-stabilized-sitl-zacmmnz6`, manifest
`fe6c5d26ad54dd8b732ba7a97a77246651b45dd7f3824edb483dadf4f65971f6`.
Withdrawal/restart revoke with samples only 20-24 ms old; actual heading resets
advance counter 1 -> 2 without publishing across the reset. Consumer off follows
each revoke by 4 ms. Public uORB copying already rejects withdrawn publishers;
firmware remains unchanged. These tests do not qualify every between-poll
publisher restart or positive sensor-diverse candidate handoff.
The [current corrected-profile checkpoint](STABILIZED_MANUAL_SM4_RUNTIME_PROFILE_FIX.md)
passes 31 native cases with 68,726 audited cycles: one deterministic dispatch,
four full-axis cases, 12 individual-axis/six fault-recovery cases and eight state
barriers. Actual priors 0.0003394 rad/s and 0.04 m/s^2 are compared live before
enable; stock GNSS Markov noise remains. Worst coupled physical hold error is
0.009438 rad < 0.02 and tracking 0.028713 rad/s < 0.08; no threshold changed.
Five frozen-clock dispatches publish no extra control; independent lease expiry
remains exactly 100 ms. Native/plugin builds, source audit and independent
artifact rehashing pass. Evidence `research-stabilized-sitl-rjr0eutz`, campaign
manifest `447823f2cdb88e22759d58b9365cd0508370053738011aa966fc452b1c050da3`;
barriers use identical artifacts in `research-stabilized-sitl-qmf78glt`, manifest
`1536d2f78598e942746b4a9e52e2194eb1a151c705e908aca82450f432698948`.
Preserve `mm7_zto8` and every failed/exploratory predecessor with their corrected
historical profile interpretation. Next: repeatability, mixed-axis inputs,
bias/delay/transport perturbations and broader publisher/epoch identity.
The [noise/bias probe record](STABILIZED_MANUAL_SM4_ROBUSTNESS_PROBES.md)
adds two other seeds and three logged ±0.0010182 rad/s gyro-bias observations
of the hardest nose-up maneuver. All five observed checks pass across 32,370
cycles; the closest biased physical hold result is 0.019376 rad against 0.02.
The 219-file exploratory receipt independently rehashes clean, and the
31-case accepted scope is unchanged. Continue unequal-axis input, timing and
transport probes; real sensor bias uncertainty is still unknown.
The [unequal-axis command record](STABILIZED_MANUAL_SM4_MIXED_AXES.md)
adds a distinct signed body-rate vector and its reversal in both poses and
orders. Four source-bound exploratory cases pass over 25,851 cycles; worst
physical tracking is 0.015560 rad/s and final hold error 0.006917 rad. The
114-file receipt rehashes clean, with the 31-case accepted scope unchanged.
The [cadence/epoch record](STABILIZED_MANUAL_SM4_CADENCE_EPOCH.md)
adds three exploratory live-clock cases over 19,408 cycles at 60 and 80 ms
manual send periods. Maximum selected-manual age is 84 ms against the unchanged
100 ms limit; worst physical rate tracking is 0.015877 rad/s and final hold
error 0.005229 rad. Its 88-file receipt independently rehashes clean. The
frozen-clock publisher-restart attempt stopped at an `ekf2 stop` timeout while
physics was paused; no restart occurred, so general between-poll identity
remains open. The [magnetic-delay record](STABILIZED_MANUAL_SM4_MAGNETIC_DELAY.md)
adds six source-bound mixed maneuvers over 38,799 cycles with measured 4/20/40 ms
magnetic sample-to-delivery timing. The exact 20/40 ms cases pass, but two
identical-seed level 40 ms runs end at 0.016142 and 0.004134 rad physical hold
error; their spread prevents a general margin claim. The private plugin alone
was rebuilt against the unchanged accepted PX4 ELF; 211 artifacts independently
rehash clean. The [raw-IMU timing record](STABILIZED_MANUAL_SM4_IMU_DELAY.md)
adds seven exploratory mixed maneuvers over 45,284 cycles. Direct and exact
20/40/80 ms IMU delivery yield selected-rate ages up to 84 ms; worst peak
physical hold is 0.426482 rad < 0.5. At exact 100 ms, the native owner rejects
state before publishing any control, and the consumer never enables. Independent
positive and negative receipts rehash 242 and 62 files clean. This does not
define a jitter envelope; repeatability and general publisher identity remain
open. The [combined-timing record](STABILIZED_MANUAL_SM4_COMBINED_TIMING.md)
adds four exploratory 80 ms pilot/IMU mixed maneuvers over 25,878 cycles in
both poses and both command orders. Selected-manual age reaches 88 ms and
selected-rate age 84 ms; worst peak physical hold is 0.437710 rad < 0.5.
The 161-file receipt rehashes clean, with accepted scope still 31 cases.
The [combined-timing repeat record](STABILIZED_MANUAL_SM4_COMBINED_REPEATS.md)
adds eight exploratory seed 43/44 cases over 51,747 cycles with the same 80 ms
pilot cadence and exact 80 ms raw-IMU delay. Worst physical peak/final hold is
0.439332/0.006657 rad; selected-manual/state ages reach 88/84 ms. Its 269-file
receipt independently rehashes clean. With the earlier seed 42 matrix this is
12 finite cases over 77,625 cycles, with uncontrolled wall-clock sender phase.
Next synthetic work is selected-state age margin and general same-ID estimator
publisher-epoch identity; accepted scope remains 31 cases.
The [combined-timing input-loss record](STABILIZED_MANUAL_SM4_COMBINED_LOSS.md)
adds two exploratory level/body-X-up negative cases using the same 80 ms pilot
cadence and raw-IMU delay. Both latch manual fault 5; the last control sample
age is 100 ms and the consumer turns off 108 ms after that sample. Re-enable
fails until an explicit new session. The 111-file receipt rehashes clean over
1,014 control cycles; this is not a jitter or reaction-time guarantee.
The [phase-controlled send record](STABILIZED_MANUAL_SM4_PHASED_JITTER.md)
adds four exploratory mixed-axis cases over 25,860 cycles with 76/84 ms
observed pilot-send intervals and exact 80 ms raw-IMU packet delay. All pass
the unchanged physical checks; manual age peaks at 88 ms, but one nose-up
case has four selected-rate cycles up to exactly 100 ms. Its 169-file receipt
rehashes clean. The first 100 ms truth-log scheduler attempt failed with
manual fault 5 and remains preserved separately. No general timing margin
or accepted-case expansion follows.
The [controlled single-packet omission record](STABILIZED_MANUAL_SM4_PHASED_SKIP.md)
adds four exploratory hold/rate cases at level/body-X-up over 5,983 audited
control cycles. Each exact 160 ms pilot gap latches manual fault 5; the last
control uses a 100 ms manual sample and the consumer turns off 108 ms after
that sample. Centered packets do not auto-recover; explicit new sessions hold.
The 185-file receipt rehashes clean. The first recovery script attempted
enable before selected input centered and failed `NotCentered`; it is retained.
The [selected-rate boundary record](STABILIZED_MANUAL_SM4_RATE_BOUNDARY.md)
rechecks the sealed phase-controlled logs: the sole body-X-up/+ 20 ms
`vehicle_angular_velocity` publication gap reaches 100 ms rate-sample age.
Five raw IMU packets were relayed at their exact 4 ms spacing during the gap,
while attitude samples advanced; bridge reception, gyro publication and rate
work-queue timing are not distinguished. Its four-file derived receipt
rehashes clean. The [stock ULog rate-path record](STABILIZED_MANUAL_SM4_RATE_TRACE.md)
adds three exact-setting nose-up/+ repeats over 19,423 cycles, all physically
passing with selected-rate age at most 84 ms. Active logged gyro/rate sample
gaps stay at most 4/8 ms, and its 152-file receipt independently rehashes
clean. The earlier stall did not recur; the ULog omits hundreds of selected
rate sample timestamps per case despite zero reported dropouts, and logger
timing may perturb the run. The [direct owner gyro-epoch record](STABILIZED_MANUAL_SM4_GYRO_EPOCH.md)
adds a separate diagnostic ELF. Its first single-copy probe was invalidated
by the eight-entry uORB queue; the corrected bounded-drain probe passes
6,478 cycles, matches selected gyro device/raw relay samples, and observes
at most 84 ms gyro and selected-rate ages. The 56-file corrected receipt
rehashes clean. The original stall did not recur, so its cause remains open.
Next software diagnostic if it recurs: correlate direct owner gyro/rate
epochs, then add private bridge-callback capture if both are stale.
The [offline SM5 allocation-input contract](STABILIZED_MANUAL_SM5_ALLOCATION_INPUT_CONTRACT.md)
defines signed physical moment derivatives, loaded-servo slope/bounds/dynamics,
full CG inertia, test airflow/EDF/wash condition and source hashes. The
compiler reproduces the synthetic fixture matrix; five tests pass and its
13-file receipt rehashes clean. A separate private PX4 build consumes a
generated header from that sealed synthetic output; two mixed-axis cases
pass over 12,953 cycles, and the 88-file receipt rehashes clean. The first
attempt exposed status-line truncation before command phases; a fresh build
reports the complete profile hash. This does not admit a measured profile,
choose vehicle gains or establish SLAAPS authority. Actual input records
are still unavailable.
The header generator now checks the entire report against a fresh compile of
the exact source input. Five tests pass; its 14-file admission receipt
rehashes clean. The generated C++ header is byte-identical to the prior SITL
header, so those runtime cases were not rerun for this guard change.
The compiled binding probe uses the private owner's exact C++ flags and
allocator source. Six separate perturbations of effectiveness, trim, bounds
and signed slew each change the relevant allocator command; its 53-file
receipt rehashes clean. This confirms generated-header data flow, not
physical authority or a vehicle profile.
The separate exact-rational interval screen gives a sufficient rank result
only for the reported one-condition matrix box. Six host tests distinguish
positive, inconclusive and rank-deficient-box cases; its 11-file synthetic
receipt rehashes clean. Physical uncertainty coverage, usable bounded
moments and the full operating envelope remain open.
The separate static screen checks whether a requested incremental moment
box fits a conservative centered actuator ball using the rank lower bound.
The declared synthetic +/-0.1-axis box passes six host tests; its 13-file
receipt rehashes clean. This does not prove actual zero-moment trim, slew,
actuator dynamics, EDF bias or any vehicle operating envelope.
The net-moment extension adds uncertain moment bias at declared trim and
physical net demand, each with separate source identity. The zero-bias
synthetic +/-0.04 N.m box passes a one-condition sufficient static check;
six tests and its 16-file source-bound receipt pass and rehash clean. A
measured bias/requirement record, valid local linearization and full
envelope still need evidence.
The private compiled C++ allocator now passes a complete 11x11x11 finite
grid at both 1 s and 10 ms from trim, with independently recomputed achieved
moments, residuals, objective, bounds and slew. Worst normalized residual
norms are 0.00000693 and 0.129904 respectively; the latter is slew-limited.
Its 13-file receipt rehashes clean. This does not establish dynamic tracking
or a continuous operating envelope.
The compiled allocator now also passes eight held-corner, reversal and
release command histories at 10 ms. All 288 rows preserve previous-command
continuity, signed slew, bounds and recomputed moments/objective; the final
13-file receipt rehashes clean. This concerns allocator command history only,
not servo lag, controller feedback or vehicle dynamics.
The prior generated-profile native traces have now been audited for actuator
response: every enabled row matches the timestamped PX4 canard command, all
truth rows follow the synthetic 50 ms surface lag, and logged angle agrees
with the surface state. The 5-file audit and 88-file parent rehash clean.
These two finite native cases do not provide loaded-servo or vehicle evidence.
A separately built private fixture plugin adds constant synthetic moment bias
`(+0.010, -0.015, +0.012) N.m` only with simulated thrust. Two mixed-axis
native cases pass 14/14 existing checks over 12,931 control cycles. The
source-bound trace audit confirms 12,944 enabled physics rows applied the
bias, and final mean canard moment nearly counters it; the 133-file variant
receipt and unchanged 88-file parent rehash clean. This is exploratory,
not an EDF measurement or accepted campaign expansion.
Broader SM4/vehicle-supported SM5 remain open. No checkout
default registration or hardware action was added. Synthetic work can proceed
without new human input while CAD and the explicitly unavailable vehicle
measurements remain pending.
The earlier **servo-only manual wind-tunnel bench path** remains at H10 below.
Latest fix: [H10 snapshot ordering and scoped integration verification](MANUAL_BENCH_CONTROL_H10_FIX.md).
H8 now copies selected input before reading its evaluation clock; stock
ManualControl snapshots all three candidate inputs before reading its live
clock. Deterministic regressions preserve genuine future-time rejection and fault
latching; no timeout, priority or physical policy was loosened. This closes the
[reproduced H9 software blocker](MANUAL_BENCH_CONTROL_H9_TESTS.md), whose failed
evidence remains intact. Exact gate advanced: H10 snapshot-ordering defect closure
and scoped capture-only manual bench integration verification, not physical acceptance.
Strict portable Debug/Release/ASan-UBSan: **2,431 checks each**. Stock compatibility:
28 tests; acceptance checker: 19 tests. **Three full runs pass 19/19 integration
cases, 3,541 checks each**, using actual selector/owner/mixer and local MAVLink.
Fresh integrated SITL and Pixhawk 6X builds pass; linked ARM text/data/BSS:
1,948,768/8,044/105,596 bytes. No physical signals, full-native sanitizers, runtime
stack/timing budgets or QGC/gamepad/SiK hardware are qualified.
Evidence `/var/tmp/research-bench-h10`, manifest
`b8176eaf9f23098b9800e8ab4c33bc8b0e41adf0015aa222310672631acb9c43`.
Next is the documented physical-acceptance sequence with measured profile,
signals, link/scheduling, power, independent stop and wind/fixture evidence;
software success does not authorize flashing or powered operation. H10's recorded
tree snapshot was 15 tracked changes / 1,021 untracked. At this SM4 handoff the
same 15 tracked changes remain; 480 files are reported untracked. No new commit
or staging action was performed.
The bench architecture remains [H8's explicit module and dedicated output path](MANUAL_BENCH_CONTROL_H8.md).
User authorized registration/publication for this default-off servo-only bench
path, superseding the earlier restriction only at that scope. The single-owner
module requires an explicit complete profile and separate enable; it publishes
four leased servo commands to MAIN1-4-only consumers. No startup script, output
assignment, arming change, operational profile, EDF publication or hardware action
was added. Deadzone and calibrated PWM conversion are now implemented explicitly.
H8 baseline portable suites passed 2,413 checks each (H1-H7 2,180 plus H8 233).
H8 requires actual integrated ELF linkage, not the earlier excluded-library sizes.
See its verification record for final artifacts, native-source cleanup and scope.
H8 evidence `/var/tmp/research-bench-h8`, manifest SHA-256
`0b934dde9c339d08e5fc295ec4087dd41fcf99b80224db785a8fac6695f6ac0f`.
Integrated SITL/ARM and private disabled-only SITL lifecycle smoke pass. ARM
linked text/data/BSS: 1,948,680/8,044/105,596; BIN 1,956,724 bytes. No runtime
resource or active manual-to-driver/physical qualification follows from this.
[Physical acceptance](MANUAL_BENCH_PHYSICAL_ACCEPTANCE.md) remains open; its
read-only 19-test checker never authorizes flashing or outputs.
User accepted the proposed bench policy and explicitly confirmed **NACA 0012**.
See [H7 decisions, contract and physical inputs](MANUAL_BENCH_CONTROL_H7.md).
Latest follow-up records nominal 5 V, documented MATEKSYS 12S Pro BEC 5 A
continuous/9 A peak (manufacturer default 5.2 V), TELEM1/QGC daily and assumed
MAVLink instance 0 / GCS 255:190 / vehicle 1:1. Ideal direct-link 175-degree
travel and interpolated 473.6842 deg/s no-load slew are model assumptions only.
Assumed 5% deadzone is available through explicit H8 profile configuration, not a
default. Wind intent is 15 to >40 m/s,
without a finite qualified upper bound. Independent hardwired stop contract is
documented; hardware verification remains open. Those earlier documentation-only
assumptions remain separate from the subsequent H8 implementation evidence.
H7 adds an inactive single bench owner and PX4 selected-manual scheduling adapter:
four coupled servo candidates, uniform mix saturation, measured piecewise PWM
calibration/reversal, explicit configure/centered-enable/disable/reset, fault latch
and separate neutral intent. It ignores throttle and inhibits EDF. Accepted
candidate schedule is 20 ms, sample age 500 ms, and surface envelope +/-13 degrees.
Actual servo calibration, slew rates, mix signs/scales, joystick center tolerance,
source identity, measured scheduling/link budget and output neutral behavior are
still required. No operational profile or hardware output is configured. H6's
four-axis default contract remains; H7 explicitly selects three axes. C1E-N1,
positive admission and all physical gates remain intact. Verification evidence
for H7 is recorded in the linked decision record and control foundation.
H7 evidence: `/var/tmp/research-bench-h7`, manifest SHA-256
`ec260f2d798ef98eccbd9bb3141f1993ac315f8f4a5ca24c667a96ed0a84bc03`.
Scoped AStyle and 2,180 strict checks per Debug/Release/ASan-UBSan pass; fresh
default SITL/Pixhawk 6X and separate research libraries compile. Ten unlinked
ARM objects total 7,501 text/0 data/0 BSS. Linked default flash 1,947,412 bytes
excludes H1-H7; no enabled bench output, hardware test or flight acceptance.

Latest admission implementation: **C1E-N1 private negative diagnostic ingress**;
see the [implementation and verification record](C1E_N1_IMPLEMENTATION_VERIFICATION.md)
and [transaction contract](C1E_N1_TRANSACTION_CONTRACT.md). The separate signed
parser, bounded worker/mailbox, source-thread owner, private Commander overlay,
output-disabled profile and independent checker are implemented. The
corrected-source build, stack review, literal zero-datagram native run, two
fresh source-bound signed negative runs, four hostile/replay runs, finite
source/mailbox fixtures, corruption sweep and sink-failure probes passed.
Independent review accepts **C1E-N1 only at its bounded negative diagnostic
scope**; N06 operator administration and native N09/N10 trusted publisher/map
injections are structurally inapplicable to this frozen profile. A prior
checker-only count error and its corrected read-only interpretation are
retained in the verification record. Earlier verified **C1E-W2** offline
generated C/Python wire conformance remains recorded in
[W2 verification](C1E_GENERATED_WIRE_CONFORMANCE.md).
Debug, optimized and ASan/UBSan each pass 53 roundtrips (including 38 semantic
negatives), plus separately classified malformed-frame/dispatch/wrapper checks.
W1 and Flight payload suites each pass 8/8. W2 alone proved no signing or
native integration; C1E-N1 now provides bounded negative-path native evidence.
The latest accepted native admission increment is **C1E-N1, 2026-09-25**.
A private Commander source view feeds a single owner; C1E runtime stays
disarmed/output-disabled, with diagnostic ingress but no operator session,
healthy estimator qualification or eligible demand. Full C1 and roadmap Phase 2
are open.
The active acceleration flight mode, control law, X-canard allocator, validated
plant and deployed Pi command path remain unfinished. H2 provides a
caller-configured 6-by-5 normalized effectiveness evaluator for the EDF plus
four canards; H3 provides caller-prioritized row-norm/rank/independence analysis.
H4 now composes H1-H3 in one allocation-free, single-owner state machine that
returns a limited normalized candidate and diagnostics without publishing it.
H5 adds the one default-off owner, explicit configure/start/prime/cycle/stop
lifecycle and an inert PX4 `ScheduledWorkItem` boundary. H6 adds an optional,
default-off input boundary that consumes only PX4's already selected
`manual_control_setpoint`, admitting either RC, one exact GCS/MAVLink identity,
or the stock-selected transition between them under explicit caller policy. It
requires complete caller-supplied normalized demand and EDF/four-canard command
maps, sample age and identity; it invents no mix, gain, priority or fallback.
Strict host, sanitizer, fresh PX4 SITL and Pixhawk 6X compile checks pass. No
validated positive acceleration input, operational map, controller, allocator
or output is selected; see
[the control foundation](EDF_CANARD_CONTROL_FOUNDATION.md).
P2J now advances the acceleration interface's actual multi-instance selector
prerequisite. A private output-disabled POSIX profile starts two real EKF2
instances and one real `EKF2Selector`, binds both IMU and publisher paths, then
uses stock `ekf2 stop 0` after both raw selector-health bits are true. The stock
selector makes one unrequested timeout handoff from instance zero to one while
actual selected attitude/local-position payloads and reset barriers are
preserved. Debug, Release and ASan/UBSan normal/restart evidence passes. Both
instances deliberately consume duplicated copies of one hash-pinned recorded
sensor stream, so P2J is not sensor-diverse or failover-qualified. Complete
mutation/deployment-sensor coverage and an admissible health policy remain open;
every qualification, admission, control and output field stays false. C1E-N1
remains unchanged and negative-only; see the P2J/P2I/P2H/P2G sections in
[the control foundation](EDF_CANARD_CONTROL_FOUNDATION.md) and the prior
[P2F record](C1E_P2F_SELECTED_STATE_PROVENANCE.md).

[C1D verification](C1D_NATIVE_INTEGRATION_VERIFICATION.md) records 37/37 host
tests in Debug/Release/sanitizers, default-off 17/17, 27 native profile probes,
two native processes and rejection of 26 corrupted exports. Those native
processes were **not rerun for this increment**; C1D host regression was rerun
on 2026-09-24 with 23/23 CTest and 17/17 checker cases passing. Earlier native
artifacts were recorded at
`/var/tmp/c1d_px4_58c0t5nm` in Ubuntu-24.04 WSL; availability was not rechecked.
Host-loopback authentication/status uses synthetic state and is separate from
the C1E negative native ingress. CommandLimiter is a standalone
foundation, not an implemented canard allocator.

The [first-operator stock Cessna checkpoint is complete](SITL_BASELINE_FIRST_OPERATOR_CLOSURE.md).
Fresh GUI `operator1-gui-05` and headless `operator1-headless-02` both pass
all approved revision-2 checks. The project user confirmed all five QGC
items and no Comms Lost while the GUI run was live. Full Phase 1 remains
incomplete solely for second-operator reproduction, explicitly deferred.

The new package archives 848 dirty/untracked or pinned inputs and 47 repository
records. Source/build/parameter identity matched before and after both runs;
both closed normally with no simulator processes or owned mounts left.
The required acceptance checks passed without broad regression testing.
The exact checker and verified 48-file inventory are preserved under
`/var/tmp/slaaps-stock-cessna-phase1-acceptance-02-f` in Ubuntu-24.04 WSL.
Earlier automated-only evidence remains in `-02-e`; incomplete attempts remain
in `-02-b/-02-c/-02-d`. See the closure record for hashes and interval metrics.

The older [four diagnostic ULogs](SITL_BASELINE_DIAGNOSTIC_20260927.md) and
32-file inventory remain under `/var/tmp/slaaps-baseline-20260927-01`.
Their earlier measurement/auditor results remain incomplete and are not
retroactively accepted by the newer limits or runs.

## Ordered next steps

[P1 design](C1E_POSITIVE_ADMISSION_DESIGN.md) passed independent requirements
review. [P2A](C1E_P2A_PROVENANCE_COLLECTOR.md) verified 60,397 archived
observations. [P2B](C1E_P2B_NATIVE_VERIFICATION.md) now builds and independently
verifies a separate output-disabled EKF profile: 1200 real source observations,
all attitude-invalid, with no collector/runtime faults. [P3A](C1E_P3A_CLOCK_DEPLOYMENT.md)
retains before/after clock deployment identities and 16 executable/library
objects from another closed process; independent checks and five corruption
probes pass. No publisher health or clock map is qualified. Full P2/P3/P4/P5
remain open; preserve the separate negative-only N1 admission runtime.
[P2C](C1E_P2C_CLOSED_RECEIPT_MERGE.md) now implements a separate closed host
merge: fixed per-source queues, immutable identity/sequence checks, cyclic owner
order and reserved terminal evidence. Debug/ASan/UBSan cases and 1200-row P2B
offline replay pass independent review. No live publisher or readiness is qualified.
[P2D](C1E_P2D_NATIVE_TYPED_MERGE.md) now integrates typed EKF observations and
one-shot constructor/destructor-entry facts with the bounded merge in a closed
native profile. All 1200 normal rows match the parallel raw capture; an actual
restart is rejected. Independent review and seven corruption probes pass.
This qualifies neither estimator health nor a producer-to-validator clock map.
[P3B](C1E_P3B_CHECKED_CLOCK_LIFECYCLE.md) independently verifies checked Linux
host clock reads, raw nanosecond ordering and candidate expiry/replacement.
Debug/sanitizer contracts and a real 12-event trace pass. The candidate interval
is not a provider proof. [P3C](C1E_P3C_NATIVE_CHECKED_RECEIPT_CLOCK.md) now
checks receipt-clock reads in a separate native profile: normal 1,200 observations
match, and synthetic source read 26 closes capture with 25 retained observations
and real destructor evidence. Independent review accepts the frozen native
package with a separately retained corrected verifier. Legacy sample-clock
handling, provider continuity and map qualification remain open.

0. **Current priority: stabilized manual SITL:** retain the confirmed body-rate /
   attitude-hold, synthetic rotational fixture and fixed simulated EDF choices;
   core/host feedback, private estimated-state integration and the declared
   18-case rate/hold/impulse/loss/lease/explicit-recovery checkpoint pass.
   Eight actual level/nose-up reset/withdrawal/rapid-restart/unsupported-profile
   barriers also pass at their original binary/profile scope. The current
   corrected profile now passes 31 native cases, including full-axis nose-up
   hold and deterministic duplicate dispatch. Prior-related historical claims
   are corrected after the startup-string bug. Five separate noise/gyro-bias
   probes pass their observed checks with a narrow final-error margin. Four
   additional unequal-axis/reversal probes pass at level and nose-up. The
   accepted 31-case scope is unchanged. Continue timing/transport and
   broader publisher identity; the private POSIX variant now detects hidden
   advertisement cycles with atomic payload/revision binding, and eight real
   EKF2 lifecycle/reset/profile cases now pass on the exact variant. Source
   identity interface review remains open; the full 31-case revision campaign
   now passes and a tested console is the default isolated entry point. A fresh
   checkout-only build now passes five focused SITL cases plus 14 native uORB
   checks; its full 31 cases then pass freshly and the actual console passes.
   This fresh pair is now the default. Next obtain accepted SM5 vehicle inputs
   and review vehicle-supported allocation/tuning and deployment policy.
   Preserve the
   sealed passing profile and retained failed predecessor; new experiments
   require new source/config-bound evidence.
   Synthetic plant/gain/timing values are explicit; original CAD and aerodynamic
   evidence remain necessary for vehicle-supported tuning and flight claims.
   Keep stock Cessna, H10 bench scope and negative-only C1E-N1 intact.
1. **Baseline checkpoint closed for the first operator:** preserve the passing
   package and resume second-operator reproduction only when the user chooses.
   Repeated assistant captures do not satisfy that deferred full Phase 1 gate.
   The next independent software checkpoint is trusted receipt/owner integration
   described in step 3; the accepted runtime remains negative-only meanwhile.
2. **Plant inputs and packaging (can overlap):** obtain original CAD CG/inertia
   report with units, axes, reference point and per-component mass scope.
   Intake SDF has invalid inertia and sums to 3.291337 kg versus provisional
   3.041337 kg all-up. Repair a derivative package under the already-selected
   `models/research_vehicle/` (packaging-only derivative now present), preserve supplied originals,
   and validate hinge/EDF signs, local flow, servo dynamics and sensor models.
3. **Positive admission gates:** P2J now extends P2I with two actual EKF2
   lifecycles, two actual IMU/publisher bindings and one stock selector timeout
   handoff from instance zero to one. It preserves actual selected attitude,
   local-position and reset-barrier payloads, but both instances consume
   duplicated data from one recorded sensor identity. Complete mutation and
   deployment-sensor coverage remain open; the observation does not qualify
   sensor diversity, failover or an admissible health predicate, and all
   admission/control authority remains false.
   P3C checks the native receipt clock;
   complete legacy sample-clock handling, provider semantics/whole-interval
   continuity and map lifetime binding using P3A/P3B as foundations. Exact
   operator/transport capability
   and liveness, immutable evidence-backed budgets and a distinct positive
   protocol remain open. These software tasks do not require CAD. Retain N1.
4. **Control and active mode:** H4 composes H1 command limiting, H2 normalized
   effectiveness/residual evaluation and H3 prioritized structural authority in
   an output-disabled library seam. H5 adds the default-off single owner and PX4
   scheduling boundary without actuator publication. H6 adds a default-off
   PX4-selected RC/GCS manual source and explicit caller-configured mapping into
   that owner; it is not started or linked by default. H7 is the separate
   servo-only bench candidate owner, using H1/H6 without invented H2/H3 plant
   data. Finish its physical calibration and qualified output-neutral integration
   before any bench enablement; see the H7 record. For acceleration, after the
   outstanding positive-admission provenance/clock/operator/transport gates,
   add a reviewed output-disabled validated acceleration-input/status boundary.
   After physical and interface gates, implement stabilization, feasible
   acceleration tracking and four-canard/EDF allocation. Define residual-attitude
   policy, normalized torque/thrust semantics, operational axis priority and
   thresholds, authority limits, saturation and loss behavior; verify open-loop
   signs before tuning.
5. **Complete system:** Pi-in-the-loop timing/loss tests, quantitative SITL
   regression, linked Pixhawk runtime/resource checks, then separately
   authorized human-led bench/flight validation. The current full Pixhawk 6X
   baseline build passes, but the default-off H1-H6 library is not linked into
   its firmware resource total.

The first-operator reproducible Cessna baseline is complete. CAD
evidence is required before plant acceptance or control feasibility claims.
Operational rate/age/clock budgets and physical fallback need evidence; neither
a received MAVLink packet nor passing diagnostic tests proves acceleration tracking.

## Open only the relevant sources

| Task | Entry documents | Source anchor |
| --- | --- | --- |
| Stabilized manual SITL (current priority) | [Goal/inputs](STABILIZED_MANUAL_SITL_GOAL.md), [Accepted 31-case checkpoint and profile erratum](STABILIZED_MANUAL_SM4_RUNTIME_PROFILE_FIX.md), [publisher-epoch API audit](STABILIZED_MANUAL_SM4_PUBLISHER_EPOCH_AUDIT.md), [SM5 allocation-input contract](STABILIZED_MANUAL_SM5_ALLOCATION_INPUT_CONTRACT.md), [generated synthetic SITL](STABILIZED_MANUAL_SM5_GENERATED_SITL.md), [interval rank screen](STABILIZED_MANUAL_SM5_INTERVAL_RANK.md), [static authority screen](STABILIZED_MANUAL_SM5_STATIC_AUTHORITY.md), [net authority screen](STABILIZED_MANUAL_SM5_NET_AUTHORITY.md), [compiled allocator grid](STABILIZED_MANUAL_SM5_ALLOCATOR_GRID.md), [allocator command history](STABILIZED_MANUAL_SM5_ALLOCATOR_SEQUENCE.md), [native actuator lag audit](STABILIZED_MANUAL_SM5_NATIVE_ACTUATOR_LAG.md), [steady-bias native diagnostic](STABILIZED_MANUAL_SM5_STEADY_BIAS_SITL.md) | `stabilized_manual_core`, `stabilized_manual_sitl_owner`, `stabilized_manual_publisher_epoch_audit`, `stabilized_manual_campaign_runner`, `stabilized_manual_allocation_profile_compiler`, `stabilized_manual_rank_screen`, `stabilized_manual_static_authority_screen`, `stabilized_manual_net_authority_screen`, `stabilized_manual_allocator_grid_verifier`, `stabilized_manual_allocator_sequence_verifier`, `stabilized_manual_native_actuator_lag_auditor`, `stabilized_manual_bias_sitl_sealer`; 31 native cases accepted, two generated-profile cases and two steady-bias cases exploratory; one-condition synthetic rank, static incremental/net box, finite allocator checks and fixture-lag/steady-bias traces, publisher identity and real plant pending |
| Selected-state publisher revision (private SITL) | [API gap audit](STABILIZED_MANUAL_SM4_PUBLISHER_EPOCH_AUDIT.md), [private revision variant](STABILIZED_MANUAL_SM4_PUBLISHER_REVISION_VARIANT.md) | `stabilized_manual_publisher_epoch_preparation`, `stabilized_manual_publisher_revision_subscription`, `stabilized_manual_publisher_revision_selftest`, `stabilized_manual_publisher_epoch_runner`, `stabilized_manual_publisher_epoch_frozen_runner`, `stabilized_manual_publisher_epoch_sealer`; 14 native uORB checks / 11 synthetic SITL cases pass; see actual-lifecycle row; general publisher identity open |
| Actual publisher lifecycle (private SITL) | [EKF2 lifecycle verification](STABILIZED_MANUAL_SM4_PUBLISHER_LIFECYCLE.md) | `stabilized_manual_publisher_lifecycle_preparation`, `stabilized_manual_publisher_lifecycle_runner`, `stabilized_manual_publisher_lifecycle_sealer`; eight actual EKF2/reset/profile cases pass on the exact revision binary, general identity open |
| Default runnable stabilized manual mode | [Run/commands](STABILIZED_MANUAL_ISOLATED_MODE.md), [Fresh build](STABILIZED_MANUAL_CHECKOUT_BUILD.md), [Full fresh qualification](STABILIZED_MANUAL_CHECKOUT_QUALIFICATION.md) | `stabilized_manual_console`, `stabilized_manual_console_verifier`, `stabilized_manual_default_profile`, `stabilized_manual_checkout_builder`, `stabilized_manual_checkout_qualification`, `stabilized_manual_profile_creator`; fresh default's 31 cases and live console pass; source-build/run workflow complete for finite synthetic scope; measured vehicle inputs and deployment policy open |
| Latest gate | [N1 verification](C1E_N1_IMPLEMENTATION_VERIFICATION.md), [W2 verification](C1E_GENERATED_WIRE_CONFORMANCE.md), [C1D N2 verification](C1D_NATIVE_INTEGRATION_VERIFICATION.md) | `c1e_parser`, `c1e_owner`, `c1e_bridge`, `c1e_checker`; `native_owner` remains C1D-closed |
| C1E native boundary | [N1 contract](C1E_N1_TRANSACTION_CONTRACT.md), [C1E design](C1E_NATIVE_INGRESS_DESIGN.md), [finite matrix](C1E_NATIVE_INGRESS_TEST_SPEC.md) | `c1e_parser`, `c1e_owner`, `c1e_worker`, `c1e_bridge`, `c1e_capture` |
| Native ingress design | [Attachment design](C1_PX4_ATTACHMENT_DESIGN.md), [closed owner](C1D_NATIVE_CLOSED_OWNER_DESIGN.md), [state contract](C1C_STATE_RECEIPT_CONTRACT.md) | `native_owner`, `diagnostic_adapter`, `host_owner` |
| Wire/session/clock | [Wire contract](ACCELERATION_WIRE_CONTRACT.md), [state contract](C0B2_STATE_CONTRACT.md), [host loopback](C1H_HOST_LOOPBACK_DESIGN.md) | `wire`, `adapter`, `guard`, `clock`, `signed_parser` |
| Positive-admission gates | [P1 design](C1E_POSITIVE_ADMISSION_DESIGN.md) | `positive_admission_design`, `native_clock_provider`, `estimator_selector`, `estimator_publisher` |
| Closed provenance collector | [P2B contract](C1E_P2B_NATIVE_RECEIPT_CONTRACT.md), [native verification](C1E_P2B_NATIVE_VERIFICATION.md), [P2A audit](C1E_P2A_PROVENANCE_COLLECTOR.md) | `native_observation_runner`, `native_observation_collector`, `native_observation_checker`; offline `provenance_collector` |
| Closed receipt merge | [P2C contract/verification](C1E_P2C_CLOSED_RECEIPT_MERGE.md) | `closed_merge_api`, `closed_merge_core`, `closed_merge_harness`, `closed_merge_runner`, `closed_merge_checker` |
| Typed native observation merge | [P2D contract/verification](C1E_P2D_NATIVE_TYPED_MERGE.md) | `native_merge_api`, `native_merge_bridge`, `native_merge_runner`, `native_merge_checker` |
| Committed estimator/reset/sensor observation | [P2E contract/verification](C1E_P2E_COMMITTED_STATE_PROVENANCE.md) | `committed_state_api`, `committed_state_bridge`, `committed_state_runner`, `committed_state_checker` |
| Direct selected-state/status observation | [P2F contract/verification](C1E_P2F_SELECTED_STATE_PROVENANCE.md) | `selected_state_api`, `selected_state_bridge`, `selected_state_runner`, `selected_state_checker`, `selected_state_variant_checker` |
| Selector-enabled acceleration observation | [P2G foundation/handoff](EDF_CANARD_CONTROL_FOUNDATION.md) | `selector_runtime_api`, `selector_runtime_bridge`, `selector_runtime_runner`, `selector_runtime_checker`, `selector_runtime_feeder` |
| Selector output-disposition observation | [P2H foundation/handoff](EDF_CANARD_CONTROL_FOUNDATION.md) | `selector_disposition_api`, `selector_disposition_bridge`, `selector_disposition_runner`, `selector_disposition_checker`, `selector_disposition_tests` |
| Valid selected-attitude observation | [P2I foundation/handoff](EDF_CANARD_CONTROL_FOUNDATION.md) | `selected_attitude_api`, `selected_attitude_bridge`, `recorded_system_state_api`, `recorded_system_state_bridge`, `selected_attitude_runner`, `selected_attitude_checker`, `selected_attitude_tests` |
| Actual two-EKF selector handoff observation | [P2J foundation/handoff](EDF_CANARD_CONTROL_FOUNDATION.md) | `selector_handoff_api`, `selector_handoff_bridge`, `selector_handoff_feeder`, `selector_handoff_runner`, `selector_handoff_checker`, `selector_handoff_tests` |
| Checked clock lifecycle | [P3B contract/verification](C1E_P3B_CHECKED_CLOCK_LIFECYCLE.md) | `checked_clock_api`, `checked_clock_core`, `checked_clock_runner`, `checked_clock_checker` |
| Native checked receipt clock | [P3C contract/verification](C1E_P3C_NATIVE_CHECKED_RECEIPT_CLOCK.md) | `native_checked_clock_api`, `native_checked_clock_core`, `native_checked_clock_runner`, `native_checked_clock_checker` |
| Clock provider | [source proof candidate](C1E_NATIVE_CLOCK_QUALIFICATION.md), [P3A deployment](C1E_P3A_CLOCK_DEPLOYMENT.md) | `clock_deployment_capture`, `clock_deployment_checker`, `native_clock_provider`, `native_clock_representation`, `native_clock_alias` |
| Geometry/plant | [Intake review](GAZEBO_GEOMETRY_INTAKE_REVIEW.md), [requirements](RESEARCH_VEHICLE_REQUIREMENTS.md) | `Tools/research_sitl/geometry/check_sdf_intake.py` |
| Control/allocation | [Control architecture](CONTROL_ARCHITECTURE.md), [foundation](EDF_CANARD_CONTROL_FOUNDATION.md) | `frames`, `fixed_array`, `limiter`, `effectiveness_model`, `authority_analysis`, `control_pipeline`, `runtime_owner`, `px4_control_runtime`, `manual_control_input`, `px4_manual_control_input`, `manual_control_schema`, `manual_control_selector`, `manual_control_owner`, `mavlink_receiver`, `actuator_command_build`; future validated-acceleration-input and active-control seams in architecture |
| Manual wind-tunnel bench | [Working-baseline bench port](MANUAL_BENCH_WORKING_BASE_PORT.md), [stock reference](MANUAL_BENCH_STOCK_ROLLBACK_REFERENCE.md), [startup diagnosis](MANUAL_BENCH_H10_BOOT_DIAGNOSIS.md), [commissioning procedure](MANUAL_BENCH_FLASH_AND_TEST_PROCEDURE.md) | `bench_working_base_port_record`, `bench_rollback_hardware_boot_report`, `bench_rollback_io_cold_result`, `bench_rollback_usb_ready_result`, `bench_rollback_usb_ready_console`, `bench_rollback_usb_ready_plan`, `bench_rollback_output_configuration_result`, `bench_rollback_io_failsafe_followup`, `bench_rollback_io_cold_readback`, `bench_rollback_output_configuration_plan`, `bench_rollback_usb_setup_commands`, `bench_rollback_usb_readback_commands`, `bench_rollback_usb_input_correction`, `bench_rollback_usb_signal_profile`, `bench_rollback_usb_profile_validation`, `bench_rollback_usb_input_assessment`, `bench_rollback_usb_input_followup`, `bench_rollback_runtime_listener`, `bench_rollback_runtime_mavlink_main`, `bench_rollback_runtime_telemetry_schema`, `bench_rollback_runner`, `bench_rollback_candidate_receipt`, `bench_rollback_qualification_receipt`, `bench_rollback_port_source_manifest`, `bench_rollback_port_patch`, `bench_rollback_artifact_validation`, `bench_rollback_reference_hardware_acceptance`, `bench_active_fixture`, `bench_px4_module`, `bench_output_provider`; software qualified, startup/USB input/disarmed/buttons-zero recorded, corrected roll sign accepted; finite parser check complete; FMU setup/current IO zeros match and hardware profile-content/Ready/start/stop subset passes; module stopped; next profile byte-integrity evidence and G4 signal captures; scope unavailable, enable/physical outputs pending |
| Baseline/evidence | [first-operator closure](SITL_BASELINE_FIRST_OPERATOR_CLOSURE.md), [2026-09-27 diagnostic](SITL_BASELINE_DIAGNOSTIC_20260927.md), [baseline procedure](SITL_BASELINE_REPRODUCIBILITY.md), [test specification](SITL_TEST_SPECIFICATION.md) | `baseline_source_packager`, `baseline_capture`, `baseline_interval_scorer`, `baseline_acceptance_checker`, `baseline_checkpoint_checker` |
| Phase exit criteria | [Roadmap](CUSTOM_VEHICLE_SITL_ROADMAP.txt), [Phase 2 audit](PHASE_2_EXIT_AUDIT.md) | Read the relevant phase, not historical agent assignments |

## Freshness and preservation

Before work, inspect Git status including untracked files. For graph nodes touched
by the task, compare `Get-FileHash -Algorithm SHA256 <path>` (PowerShell) or
`sha256sum <path>` (WSL) with the graph. If different, inspect those files and
adjacent edges and update hashes only after checking semantics. Adding a component
requires adding its node/edges; unchanged hashes do not detect new integration.
Recheck the latest current-increment heading when resuming after other work.
Resolve conflicts using current source and accepted evidence, not this summary.

Keep this file short: replace current state/next actions when a milestone changes,
link detailed verification elsewhere, and update affected graph entries in the
same increment. Preserve historical evidence. Retained research artifacts in
ignored build directories and WSL survive chat clearing but are outside the
source checkpoint and require separate preservation.
Keep stock Cessna and the separate `Tools/simulation/gz` repository intact.
Use isolated WSL build/cache paths; do not share concurrent simulator caches.

Orchestration preference: primary agent integrates/reviews; use Astra for
math-intensive work, smaller models for scoped coding/testing. W1 used Astra
for clock/schema review, Luna for coding and Sol for independent tests/review.

W2 reports are under /var/tmp/c1e_w2_mavlink_{1rt7zfws,zo5l2buz,8ouk91k2}
in Ubuntu-24.04 WSL (Debug, optimized, sanitized). Use command-local
TMPDIR=/var/tmp with --keep-dir; earlier /tmp artifacts disappeared between calls.

2026-09-30 control handoff: H4 composes H1 limiting, H2
effectiveness/residual evaluation and H3 prioritized authority analysis. H5
adds the allocation-free `ControlRuntimeOwner` and inert PX4
`ScheduledWorkItem`. H6 adds `ManualControlInput` and
`Px4ManualControlInputSource`, consuming only stock PX4's selected
`manual_control_setpoint`. The caller supplies source policy, exact GCS/MAVLink
identity, sample age and both complete affine maps into H5. Construction remains
off; there is no module, Kconfig, startup, parameter, airframe, arming,
allocator, publication or output. Strict Debug, Release and ASan/UBSan counts
are 843 H1, 182 H2, 233 H3, 68 H4, 73 H5 and 132 H6 checks: 1,531 per
configuration.

Retain `/var/tmp/research-manual-h6`: host builds are `debug`, `release` and
`sanitize`; the clean WSL-native source is `px4-source`; fresh complete PX4
builds are `px4-sitl-build` and `px4-v6x-build-final`. ARM GCC 13.2.1 compiles
all eight research objects. Their 5,197 bytes of text and zero data/BSS are
object compilation evidence only. The default SITL/Pixhawk ELFs contain zero
`research_actuator_command::` symbols. The complete Pixhawk baseline link emits
ELF/BIN/PX4 and reports 1,947,428 bytes of flash; it excludes H1-H6 and is not
integrated runtime-cost evidence. Exact hashes are in the foundation and graph.
Use a WSL-native clean source view for NuttX because the Windows path/line
endings break direct board configuration.

H6 advances the default-off PX4-selected RC/GCS manual ingress and explicit
mapping boundary only. It does not qualify live GCS operation,
scheduler/provider continuity, positive acceleration admission, operational
maps/timing/source identities, stabilization, allocation, fallback, loss
response or concurrent stop coordination. Next control work is the
output-disabled validated-acceleration-input/status boundary after positive-
admission provenance, clock, operator and transport gates. Physical control
remains blocked on plant evidence. C1E-N1 and P3C retain their accepted bounded
scopes.

P2J actual two-EKF timeout-handoff evidence is
`/var/tmp/p2j-handoff-u4up5z6_` (Debug),
`/var/tmp/p2j-handoff-t7z5qo9k` (Release) and
`/var/tmp/p2j-handoff-6ziwnuyt` (ASan/UBSan). Manifest hashes are
`e445b041336a1b703af9559d0ada68ca2397f89e8f93653689a1ec7d7370559c`,
`8a352498e57c2549ba78d4b6958190120b96da6a13219bebd4e5114367aedaa2`
and `6502503cfc4ce40b6a3eccc877e9ee7bee4b9a4fd720be38ab49b7b316ec3523`.
Every normal/restart case binds two actual EKF/IMU/publisher paths, publishes
2,400 messages from 1,200 duplicated inputs and retains one unrequested stock
timeout handoff, both selected-output sides and reset barriers. All health,
failover, mutation, deployment-sensor, admission, control and output fields are
false. Eleven corruption tests and unchanged 1,531-check H1-H6 strict suites
pass. Preserve P2I `/var/tmp/p2i-attitude-{vkyya591,2_co807l,zxtllgub}` as the
accepted inherited single-instance selected-attitude evidence.

P2H closed disposition evidence is `/var/tmp/p2h-disposition-bv29hkud` (Debug),
`/var/tmp/p2h-disposition-gnzmttte` (Release) and
`/var/tmp/p2h-disposition-ps2oxj5a` (ASan/UBSan). Manifest hashes are
`e015e46c659be1ce9fc4aef7a0b5ce2845c0ea44738acc742894acdb06928b06`,
`f495ad5f68e794126cad16015cf6b59269d2d8a645b82e40caaf4d0ecb1d9f6f`
and `4112944c84edaf34bbd78a15dfa50db2d967dd8128859fc1a1f39ff91d8e1adb`.
Each case records 1,200 invalid EKF attitude entries, no publication attempt or
selector attitude candidate, and exact local candidate-to-selected-publication
correspondence. Debug/Release retain 589/588 publications per case;
ASan/UBSan retains 590 with explicit absent-candidate calls. All authority is
false. Preserve P2G `/var/tmp/p2g-selector-{4ek3kb0n,5n54u5ds,9b5xbf6q}`
and final P2F `/var/tmp/p2f-selected-vl0q1aop` as inherited evidence.

Current fresh default builds and strict H1-H6 regression logs are under
`/var/tmp/research-selector-handoff-p2j`; default ELFs contain zero P2J
`research_handoff` symbols, their module lists contain no P2J module and actual
build logs contain no P2J compile line. The complete Pixhawk default link
reports 1,947,428 bytes of flash and excludes P2J. This is linked default-
firmware resource evidence, not P2J/H6 linked-cost evidence. Use this clean
WSL-native source view for NuttX; the Windows checkout path and CRLF scripts do
not support direct board configuration. Preserve earlier P2I/P2H default
evidence as inherited evidence.

Frozen native evidence `/var/tmp/p3c-native-8oh4gg31`; corrected verifier bundle
`/var/tmp/p3c-verifier-lbz2xeg4`; final review `/var/tmp/p3c-final-review-an2v_qgt`.
Normal 1,200 raw/merged rows match all checked clock uses. Explicit injected
source read 26 retains 25 rows and sticky failure through real destructor entry;
both processes close normally. Preserve preliminary review
`/var/tmp/p3c-independent-review-dsxq8m8q` and both source-only review views
listed in the P3C record. The terminal-type fix changed only the verifier after
capture, so use its separate hash binding; native sources/artifacts are frozen.

Next: complete mutation and deployment-sensor evidence, then define the
admissible estimator-health predicate from approved requirements before
qualified provider/sample-clock and operator/transport lifecycle integration.
P2J's real two-instance timeout handoff does not qualify failover because both
instances use duplicated data from one recorded sensor. Checked receipt reads
do not repair legacy scheduler/sample HRT or qualify provider continuity, VM
pauses or maps. Full P2/P3/P4/P5 remain open; no new human input is needed for
evidence-only closed software work, but health/timing authority cannot be
invented.

P3B remains accepted: `/var/tmp/p3b-provider-5vpaar72`, final review
`/var/tmp/p3b-final-review-uv_rdov5`; preserve preliminary `-_u4r36lf` and
`/var/tmp/p3b-independent-review-cf87d_bp`. Its experiment intervals are not
operational budgets. P2D remains accepted: `/var/tmp/p2d-native-od002jmn`, review
`/var/tmp/p2d-independent-review-hp9h4jgq`; preserve configure-only failure
`/var/tmp/p2d-native-7io33289`. Original P2D/P3B sources remain unchanged.

P2C host merge remains accepted; preserve `/var/tmp/p2c-merge-vvtmfdr0`,
`/var/tmp/p2c-final-review-6rx0877y`, preliminary `-0_bq8qdf` and
`/var/tmp/p2c-independent-review-miv1c44v`. Original P2C host sources are unchanged;
P2D uses a private native guard overlay with the identical merge algorithm.

P1 requirements review, P2B native observation and P3A deployment observation
remain accepted at their bounded scopes. Preserve `/var/tmp/p2b-native-30xfeeqc`,
exploratory `-4mdkxnjg`, `/var/tmp/p3a-clock-pds1ofii` and
`/var/tmp/p3a-independent-review-1zerkhz4`. P2B/P3A retain 1200/1200 observations
per process, all attitude-invalid; no readiness or clock map is qualified.

User confirmed CAD is still in process. The derivative plant package fixes
six mesh URIs only; invalid inertia and unresolved mass scope still block
physical acceptance. Do not ask for CAD again until new evidence is available.
First-operator baseline PASS and revision 2 unchanged; second operator deferred.
C1E-N1 remains the accepted admission runtime at negative-only scope. That
research increment performed no physical operation. Preserve P2A evidence
`/var/tmp/c1e-p2a-verified-re2idmzc`, baseline
`/var/tmp/slaaps-stock-cessna-phase1-acceptance-02-f`, referenced earlier packages
and `/var/tmp/slaaps-baseline-20260927-01`. Source checkpoint status is above.
Clearing history or starting a fresh chat is safe with workspace and WSL evidence
preserved. For the current bench task, resume with "Read AGENTS.md and
docs/PROJECT_CONTEXT.md, then docs/MANUAL_BENCH_FLASH_AND_TEST_PROCEDURE.md and
its two graph nodes. The retained H10 candidate and receipt are staged under
build/bench_commissioning/h10-e880f1cd79-dirty; 726 evidence artifacts and 58
compiled anchors revalidated on 2026-10-13; initial staging reused the retained
build. No flashing or powered test was performed by Codex. The user reports successful H10
programming but no normal QGC connection; rollback PX4 e8ea98b4/NuttX 11.0.0
works. Read docs/MANUAL_BENCH_H10_BOOT_DIAGNOSIS.md; H10 is withdrawn. The
7,542-byte SD failure text has historical PX4 1.16.1 and rollback parameter
recovery captures, without an H10 identity. No debug adapter is available.
A new clean stock e880f1cd79/NuttX 12.12.0 build now passes with the matching
GCC 13.2.1 package and is staged in build/bench_commissioning/stock-e880f1cd79-diagnostic.
Read comparison-receipt.json and graph node bench_stock_usb_comparison_receipt;
the .px4 SHA256 is 35945a63a8cf64bd1f80175cb975ac8063994b66fd7e9a29997c93545c065218.
Its four artifact hashes, image/bin consistency and startup entry points verify;
no research source was compiled. On October 19 the user reports the same missing
connection with stock .px4/.bin. QGC Daily verifies and completes the upload via
ArduPilot ChibiOS loader protocol 5, board 53, flash slot 1966080; rollback still
works. Read hardware-comparison-failure-20261019.json and the updated diagnosis.
Tools/bench_test/inspect_bootloader.py is prepared in the private
bootloader-readback-venv, guarded to query only USB 1209:5741 / board 53 /
protocol 5. Sixteen fixture tests pass. Codex's query-only COM14 readback
cold-readback-20261019T210117038214Z.json matches the stock vectors and CRC
de5dd361; the first 32 bytes are present. No flash, boot or parameter command
was issued. Upload finalization is confirmed for this attempt; early application
startup/handoff is unresolved. Actual loader build ID got no reply. Identification
holds the loader until power cycle, an inspection effect. The user completed
step 1 rollback recovery. Step 2 stock firmware is built at exact e8ea98b404 /
NuttX 49f3b29c92 / apps e37940d85, with private NuttX tag metadata frozen at
11.0.0; see docs/MANUAL_BENCH_STOCK_ROLLBACK_REFERENCE.md. The final software
export is in build/bench_commissioning/stock-e8ea98b404-nuttx11-reference;
.px4 SHA256 315f014c6acf50106fa51b1b345c6c9f3833dc8a3126ec9a69d1461ba6cf6334.
The initial stock-e8ea98b404-reference export is superseded due to its newer
tag-derived version labels. All four final hashes, source/archive/recipe pins,
11.0.0 version records and 989 stock-only compile entries validate. The user has
now flashed the final stock reference successfully: the supplied console has
matching identities, a valid parameter load, PX4IO CRC match and USB GCS heartbeat.
All three cold starts connected within 30 seconds and stayed connected at least
60 seconds, user-confirmed. The user deliberately reset to stock and did not
require a parameter backup; no old-settings recovery is pending. Step 2 startup
passes. Read hardware-startup-acceptance-20261019.json and its supplied console;
the earlier settings-reset concern was superseded by that clarification. Step 3
is now software-qualified on the complete working base. Read
docs/MANUAL_BENCH_WORKING_BASE_PORT.md and graph nodes bench_working_base_port_record,
bench_rollback_candidate_receipt, bench_rollback_qualification_receipt and
bench_rollback_port_source_manifest. New image directory:
build/bench_commissioning/bench-e8ea98b404-step3; .px4 SHA256
a45cf99374fa7ccfd4f6349ca9a6341bacfcfa6199254f71ab2b04f0cb2aed31.
Portable 2,431 checks per configuration, 28 stock GTests, 19 checker tests and
three repetitions of 20 native cases / 3,632 checks all pass. The new image
links owner/lease, excludes the fixture, starts no bench owner automatically
and has 8,328 bytes flash headroom. Source patch round-trip and 70 mapped / 250
native evidence hashes validate. Native source /var/tmp/research-bench-step3-e8ea,
Docker volume px4-bench-e8ea-step3-20261019 and metadata worktree bench-e8ea-step3
are retained; original stock/H10 evidence remains intact. The user now reports the
changed image's cold-boot tests passed and research_bench_control reports not
running. Read hardware-boot-report-20261019.json beside the candidate and graph
node bench_rollback_hardware_boot_report. A subsequent supplied console is
transcribed/assessed in hardware-usb-input-assessment-20261019.json, graph node
bench_rollback_usb_input_assessment. Runtime identity/build time match and valid
USB input selects instance 2 / sender 255:190, with saved input settings. MAIN
functions and reported PWM are all zero; source review explains the stale IO
FMU-data flag and raw-PWM hardware-enable flag without claiming output health.
The follow-up hardware-usb-input-followup-20261019.json confirms actual disarmed
state and buttons zero. Its roll-order interpretation is superseded by
hardware-usb-input-correction-and-telemetry-20261019.json: the user corrected the
final pair to right then left and verified left=-1/right=+1. Keep calibration
unchanged. Later USB telemetry spans 60.062797 seconds, adds 1,570 received and
15,045 missed messages, with no new RX parser/overrun/drop indicators or TX
overruns (391 unchanged). A USB 2 / 255:190 synthetic signal profile is staged,
44 tokens, hash 1bf090fc08a058d00be6fb0f80f70a32f218c3df17695059faa6381524ea3c7f;
the same native binary parses it, reports Ready and stops without enable or
physical drivers. Read usb-profile-ready-validation/profile-validation.json;
two rejected mounted-path attempts and final pass are retained. No process runs.
The initial parameter readback and setup are retained in
hardware-output-configuration-plan-20261019.json and usb-signal-only-setup.txt.
The user applied them; hardware-output-configuration-result-20261019.json records
matching FMU routing/rates/limits/off-values, AUX1..9 zero, REV0, owner stopped,
armedfalse/prearmedtrue and IO data OK. Actual IO failsafe first4 remains1000
despite FMU/mixer0. Source ignores zero failsafe writes; prior values may survive
while only FMU reboots. hardware-io-failsafe-followup-20261019.json confirms console
reboot and no instrument now (scope later). The later cold-check response in
hardware-io-failsafe-cold-result-20261019.json reports actual IO first4 zeros,
saved FAIL1..4 zeros and expected persisted MAIN configuration. Owner stopped,
IO init/data OK and actual armedfalse/prearmedtrue with fault flags clear; current
register-table check passes without any firmware fix. Recheck actual table after
future configuration/reset changes. The user executed usb-profile-create-ready-console.txt
(13 commands) creates/prints the USB signal profile then starts Ready and stops
without enable. usb-profile-create-ready-plan.json retains preparation-time
source/ELF command support and exact local echo reconstruction. The later
hardware-usb-profile-ready-result-20261019.json confirms matching complete cat
content, Ready/state0, two armedfalse/prearmedtrue snapshots with fault flags clear,
IO current PWM zero and first4 disarmed/failsafe zeros, then not running. This
passes the hardware Ready-only subset. No installed-byte hash or pin measurement
is claimed. Next obtain host/card hash or verified transfer evidence and prepare
G4 MAIN1..4 plus AUX4 capture with calibrated equipment; retain pre-enable pin
baselines. Scope unavailable, so enable/full lifecycle and physical tests remain
pending. No hardware command issued by Codex; module is stopped.
Earlier status snapshots add 1,015 received
and 9,435 missed messages, but source proves the percentage is misformatted:
missed/received is printed directly with %. The counter's cause is unresolved.
The subsequent finite parser/buffer check above supports configuration work,
while sequence-counter cause and worst-case timing remain open. The
displayed MANUAL_CONTROL 27.4/18.1 Hz is a smoothed interval estimate, not a gap
guarantee. No firmware change is needed for this calibration/diagnostic stage.
Use listener count flag -n; earlier positional counts were corrected in the
procedure. Keep loads disconnected and owner stopped; no physical output tests
or profile/routing acceptance yet. Retain remaining G2 boot/configuration evidence.
No hardware command was issued by Codex for either rebuild. Detached build metadata
worktrees and Docker source volumes are retained; do not prune the metadata-only
build-e8ea98b404 worktree while its source volume is retained. No build remains running. No bootloader
update or physical-output test is accepted. Proceed through measured-profile and actual-output gates before
the separately required stabilized ARM integration; preserve all earlier evidence."
For the prior isolated-SITL work, resume with "Read AGENTS.md and docs/PROJECT_CONTEXT.md, then
docs/STABILIZED_MANUAL_SITL_GOAL.md. Continue the new stabilized manual SITL
goal from docs/STABILIZED_MANUAL_SM4_RUNTIME_PROFILE_FIX.md,
docs/STABILIZED_MANUAL_SM4_ROBUSTNESS_PROBES.md and
docs/STABILIZED_MANUAL_SM4_MIXED_AXES.md and
docs/STABILIZED_MANUAL_SM4_PUBLISHER_EPOCH_AUDIT.md and
docs/STABILIZED_MANUAL_SM4_PUBLISHER_REVISION_VARIANT.md and
docs/STABILIZED_MANUAL_SM4_PUBLISHER_LIFECYCLE.md and
docs/STABILIZED_MANUAL_ISOLATED_MODE.md. The corrected synthetic
profile passes 31 accepted native cases with live parameter comparisons; five
noise/bias and four unequal-axis observations pass but are exploratory.
Preserve research-stabilized-sitl-rjr0eutz and exact-artifact barrier child
research-stabilized-sitl-qmf78glt, plus probe receipt
research-stabilized-sitl-probes-6wu2ah41 and mixed-axis evidence
research-stabilized-sitl-m2n0a0ev. Continue delay/transport and broader
publisher/epoch identity from the private POSIX advertisement-revision variant
in research-stabilized-sitl-rmk764xm. Fourteen native uORB checks and eleven
synthetic SITL cases pass, and eight actual EKF2/reset/profile cases now pass in
the exact-artifact child research-stabilized-sitl-ob5jrceo. The complete revision
campaign previously passes in research-stabilized-sitl-og0lfuxu (23 fresh plus
eight inherited, 68729 cycles), with its preserved console check q4jfno7x.
The current fresh default qualifies all 31 cases / 68813 cycles in
research-stabilized-sitl-0crhkf9n, receipt
899a1ddd82e3226f715e74be1b4a90fa30c714b8759f276978a8cb904f3bfc3a,
1475 artifacts independently rehashed. Its actual console passes in
research-stabilized-console-check-__heq56u, receipt
c65771974a65c8601a520d802e038adab825e82d41227a14a7d9ff1e5207a76c.
The earlier failed console check avatikwn is preserved: the controller captures
exactly, while the old verifier selected a later evolving estimate. The fixed
verifier checks the entry row plus retained reference at unchanged tolerance.
Read stabilized_manual_default.json
and use stabilized_manual_console.py for the current isolated mode. A fresh
checkout-only pair now passes five native cases / 14375 cycles and 14 uORB
checks in research-stabilized-sitl-q90g9o8f; its 387-file checkout-build-manifest
95e129cbcad336fce318bdb47817e9a8e3ea96a464757997be0606e4e3ea493b
rehashes clean. Read STABILIZED_MANUAL_CHECKOUT_BUILD.md and the two checkout
builder/verifier graph nodes, then STABILIZED_MANUAL_CHECKOUT_QUALIFICATION.md
and the qualifier/profile-creator nodes. Next obtain accepted measured SM5 data
for vehicle-supported allocation/tuning and review deployment/source policy before broader
claims. The unmodified checkout owner remains the earlier constant-epoch template.
Preserve the earlier
incomplete research-stabilized-sitl-mm7_zto8 and its failed physical score,
and the earlier numerical
18-case checkpoint in docs/STABILIZED_MANUAL_SM4_CAMPAIGNS.md and both profiles
research-stabilized-sitl-r27tx0nr (numeric cases pass, prior claims withdrawn) and research-stabilized-sitl-edg6vbe0
(incomplete), plus research-stabilized-sitl-zacmmnz6 (eight native state barriers)
and all later incomplete/exploratory diagnostics. Read
docs/STABILIZED_MANUAL_SM4_STATE_BARRIERS.md for the earlier eight-case scope.
General publisher-epoch/candidate identity and robustness remain open. No live
fixture/build process remains. The goal is blocked after three consecutive input-dependency audits. The user explicitly reports
measurements are not yet available; CAD remains pending. No new human input is
needed for synthetic work. Keep synthetic
fixture assumptions separate from vehicle authority. Preserve H10 bench evidence,
the stock Cessna baseline, C1E-N1 negative-only admission, P2J's unqualified
duplicated-sensor observation scope, and all physical/positive-admission gates."
