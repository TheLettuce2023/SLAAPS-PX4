# SLAAPS configuration and routing implementation

## October 23: actionable configuration and routing rejection

The supervisor retains the exact rejected-apply parameter while that same
configuration fingerprint, scope and apply counter remain current. Edits clear
old attribution until another apply; successful apply clears the fault. Invalid
inertia no longer incorrectly attributes a tensor failure to SLA_MASS.
Current routing errors expose the actual inventory index and one-based MAIN/AUX
channel only when a fresh inventory identifies it. Missing/stale observations
clear the pin attribution, and missing assignments do not invent a physical pin.

Commander supplies native scalar-argument events with parameter-group guidance.
The supervisor also publishes exact parameter STATUSTEXT once per rejected apply
and changed routing text at most once per second; suppressed newer routing detail
remains eligible to report. These direct notifications handle stock Report's
suppression of argument-only changes. Pending disarm retains priority. This adds
no output authority and does not establish QGC GUI acceptance.

Independent review, formatting, whitespace and event extraction pass (13 new
scalar-only events). Host `/var/tmp/slaaps-configuration-31m8u6ae` passes Debug,
Release and ASan/UBSan (3,082 each) and actual metadata for 308 parameters.
The first native compile exposed the ULog format-description limit. Compact new
field names `cfg_param`, `io_index`, `io_bank`, `io_channel` reduce the generated
maximum to 1,440 bytes: 1,440 + 150 reserved bytes < the existing 1,600-byte buffer.
No existing fields or global logger buffers changed. Native `3eg3u4yv` links;
binary SHA256 `687e4156a6785ec9d6ba24844e59e851a622dee658a6a4067cfb3fe7e03d0446`.
Runtime `uq8klqnm` completed 1,930 isolated actual-source checks, then its Python
marker assertion failed because PX4 truncated the long informational marker.
The retained failure is not a passing runtime receipt. The harness-only correction
uses the complete logged evidence prefix. Full runtime `gjoks2mh` passes on the
same binary with harness SHA256
`dfb63bf04562b9b1319bb54a2feb032e47ebecf5c0caa1e6e91093eda63a9d17`.
The corrected Python harness is an uncompiled delta from the sealed firmware.
Native control/mixer/supervisor checks are 4,742/2,126/1,930. Actual mavlink_log
messages, unchanged-attempt persistence/no-repeat, changed-parameter supersession,
AUX8-to-AUX6 rate-bounded routing messages and stale-detail clearing pass. Existing
manual/bench/power/companion paths and seven lifecycle/export-restore checks pass.
The portable host test prefix is byte-identical after native-only field renaming.
Capture-only and explicit synthetic-authority/forced-test-arm limits remain;
QGC GUI rendering and physical outputs were not tested.

Board checkpoint `build/slaaps_firmware/diagnostics-20261023` passes source/ELF/
package/resource checks: image 1,907,220 B (+3,752), flash headroom 58,860 B;
AXI RAM 111,100 B and SRAM4 2,048 B are unchanged. Nine probe units were compiled
fresh (six affected prior units and three reporting callees); 26 unchanged units
were reused only after exact dependency-content/include/flag comparison.
Identified MAIN/AUX stacks are 2,868/2,812 of 4,096 B; supervisor 3,984 of 6,500 B;
the new Commander diagnostic chain is 1,212 of 3,250 B. These are static identified
chains, not runtime high-water or whole-program timing qualification. Supervisor
object size is 12,480 B (+48), endpoints 8,592 B each (+16 each).
Resource receipt SHA256 `383e4da9da0463cb2f325680a1342cfef2db857723cd801ca786291322ee93c1`.
Source closure SHA256 `0e649f42200c22d4321378eced130eb0ccdb00a96e3895289cf59f8d2d98417f`.
All jobs are terminal. No hardware operation, boot or flight qualification is claimed.

## October 23: selected propulsion power integration

Manual and EDF commissioning now require an explicitly configured `battery_status`
instance, ID, source, original-report age limit and voltage band (`SLA_PWR_*`).
Defaults remain unconfigured; servo-only commissioning can operate independently.
The enabled manual schedule must use the same propulsion battery instance/ID.
The QGC battery selectors expose only the three supported message instances.

The supervisor binds the publisher lifetime and selected identity when minting a
preparation session. Disconnection, warning, fault, imminent power-off, invalid or
out-of-band voltage, stale/future/backwards timestamps, held-report voltage mutation
and publisher replacement deny continuation. Original battery expiry caps output
permission and ready-to-arm deadlines, including external replies and Commander.
EDF commissioning uses its existing stop/ordinary-disarm path; restored telemetry
cannot revive the rejected session. Existing deliberate exit/re-entry and inactive
configuration apply remain required. A concurrent explicit normal stop retains
its established normal-stop semantics and never restores output automatically.

This is selected report-health validation, not a calibrated thrust/voltage model,
sensor authentication or flight admission. `BatteryStatus` supplies neither a
separate measurement timestamp nor hardware device ID; neither is fabricated.
Operator voltage bounds and the physical feed/monitor assignment still require
vehicle definition and acceptance. No airborne recovery policy was invented.

Host `/var/tmp/slaaps-configuration-lka3nypt` passes Debug, Release and ASan/UBSan
(3,082 each) and actual PX4 metadata generation for 308 parameters. Native snapshot
`/var/tmp/slaaps-native-fk9aazlr` links. Runtime `slaaps-native-runtime-3qbzsnvv`
passes the isolated actual-source supervisor (1,912) and reaches actual Commander
power-fault output inhibition/disarm, then fails the new Python re-entry sequence:
it retained EN=1 from the no-restart check rather than observing EN=0 first.
The rejected entry preserves the production deliberate-enable requirement.
The corrected harness explicitly observes EN=0 before leave/re-entry and uses
REJECTED=12 rather than FIRST_COMMAND=9 in the terminal assertion.

Full runtime `/var/tmp/slaaps-native-runtime-ghh0mrhs` passes with unchanged native
binary SHA256 `d112941ab5636304427f33572f63d384588abd3c3129145c2bc2deb69219518a`
and separately recorded corrected-harness SHA256
`a3b95a9ddbe8b1e1f3cf33fd8439d07c60cdb58255ec03c19f11b61f696e1b33`.
Native control/mixer/supervisor checks are 4,742/2,126/1,912. Actual Commander
readiness rejects stale and out-of-band power; an active battery fault produces
fresh inhibited output captures and ordinary disarm. Restored power and reapply
do not restart output; a deliberate second session succeeds and stops normally.
All prior manual/schedule/mixer, seven lifecycle/persistence and signed-companion
checks pass. Synthetic input/power and capture-only outputs remain explicit;
no-plant EDF tests still require recorded internal forced arming because ordinary
vehicle health is unqualified. QGC GUI acceptance and hardware were not exercised.

Board/resource verification passes in `build/slaaps_firmware/power-20261023`.
The image is 1,903,468 B (+4,016 B), leaving 62,612 B flash. AXI RAM remains
111,100 B and SRAM4 2,048 B. All 32 probe units compiled fresh: identified
MAIN/AUX stacks are 2,868/2,812 of 4,096 B; supervisor 3,976 of 6,500 B.
The 49,699-entry firmware source closure matches the sealed native snapshot.
Resource receipt SHA256: `abd43c98d34996855e9cb2920e9ac5860af74b791cda0048a404d4b7799d5ec5`.
PX4 package SHA256: `ef6c622fca3bb0ac387b7f73052c2c81521ad8f1e00fa3b639374173240cb588`.
All jobs are terminal. No boot, runtime timing/stack or physical qualification is claimed.
It uses frozen `fk9aazlr` firmware inputs and records the corrected Python harness
as an uncompiled delta, rather than attributing that correction to a rebuilt binary.

## October 23: companion receipt and preparation integration

The SLAAPS targets now select the repository-owned MAVLink dialect with private
IDs 200010/200011/200012 and inherited development messages. The actual receiver
publishes bounded raw payload, receipt time, event sequence, receiver/source IDs,
signing generation and parser signature-verification evidence on four per-link
queues. Six native parameters configure the selected link/source and receipt-age
bound. The supervisor monitors authenticated receipt and latches publisher/key
replacement or sequence gaps until a new accepted inactive apply. Receipt does
not admit commands, establish producer-clock validity, grant a session, or enable
acceleration control. Wire status return is still missing.

Portable configuration evidence `/var/tmp/slaaps-configuration-frcvrnt7` passes
Debug/Release/ASan-UBSan (3,050 each), including 302 generated parameters.
The first consumer snapshot `/var/tmp/slaaps-native-bhbg3s6c` links after fixing
cross-directory generated-header dependencies. Its first configuration failure
is preserved. Runtime `slaaps-native-runtime-uuzs2dth` failed the pre-existing
EDF disarm retry assertion before companion cases. Instrumented quiet runtime
`slaaps-native-runtime-7trm42ns` recorded three requests with WAIT status on the
same observation as the final request. The supervisor now publishes exhausted
retry status immediately; actual disarming remains the only stop completion.
No retry limit, force policy or timing bound was relaxed.

Fix snapshot `/var/tmp/slaaps-native-f07znuwy` links, and runtime
`/var/tmp/slaaps-native-runtime-f1uftbjf` passes the isolated supervisor (1,821).
The first companion positive case then exposed a production integration defect:
`copyBound` deliberately rejects queues larger than one. The new
`copyQueuedBound` API copies ordered queue data and its publisher revision in
the same critical section, using a capped current-lifetime sample count to
exclude earlier advertisers' buffered data. Queue-one behavior is unchanged.
Derived native snapshot `/var/tmp/slaaps-native-nrgcpc4s` links. Runtime
`/var/tmp/slaaps-native-runtime-id206wl9` passes the queue/control checks (4,531),
mixer (2,108), isolated supervisor (1,821), and existing production lifecycle
cases. The new loopback test then failed at MAVLink startup. Isolated reproduction
`/var/tmp/slaaps-mavlink-start-qnpku7tl` showed its mission-manager constructor
waiting one second for absent dataman, beyond MAVLink's startup wait. The harness
now starts RAM dataman before MAVLink and stops it afterward.

Review also found key setup/reload mutating signing state outside the send mutex
used by parsing and transmission. Both operations now use that existing recursive
mutex; dirty-key notifications use atomic compare/exchange so a concurrent later
notification is retained. Key/timestamp/generation policy is unchanged.

Final native snapshot `/var/tmp/slaaps-native-_r7ah9m7` links; binary SHA256
`5af84dd5904975d1c5c652c98b4bb53894387b24884d454292f71615bda5cae2`.
Runtime `/var/tmp/slaaps-native-runtime-phom4snz` passes the full integration,
including actual MAVLink parsing and actual supervisor receipt monitoring with
a synthetic signing key over loopback UDP. It verifies signature replay,
unsigned/wrong-key rejection, header/payload source and target binding, original
receipt expiry, and same-key reload requiring deliberate inactive reapply.
All seven lifecycle and seven restart/export-restore scenarios pass. Native
control/mixer/supervisor checks are 4,531/2,108/1,821 respectively. Harness SHA256
`b46620e5a29026b4578eb37a1f4b71869b49e7418b179800602732d45f155c84`.

An earlier run of the same binary, `slaaps-native-runtime-6buyl3gj`, rejected an
isolated EDF preparation before WAIT_ARM, before MAVLink was instantiated. Its
supervisor and fixture bytes match the preceding passing queue snapshot. The
failure did not recur in the quiet full run; its root cause is not established.
Retain it as an unresolved timing/fixture limitation, not evidence of qualified
real-time behavior. No deadlines or admission protections were relaxed.

The earlier companion board link in
`build/slaaps_firmware/companion-20261023` predates the disarm and queue fixes.
Keep it as attempt evidence, not final current-source qualification. A derived
receipt-specific board check now passes in `companion-20261023/final` for
`_r7ah9m7`: image 1,899,300 bytes, headroom 66,780; AXI 111,100/SRAM4 2,048.
Identified static chains are MAIN 2,876/AUX 2,820 of 4,096, supervisor 3,976
of 6,500, and receiver publication 2,012 of its actual ARM reservation 5,360.
These sums exclude unmeasured libc/allocator/interrupt/callee contributions.
Each active companion queue allocates 144 x 8 = 1,152 payload bytes, up to
4,608 for four links, plus 76-byte nodes. The node lifetime counter adds four
bytes to each allocated node; receiver objects grow by 56 bytes per link.
Resource receipt SHA256:
`d7ab77592b43c95fd04748bed5b1651b75b01cf5e24ab8898f7ee051d2953f40`.
This receipt predates the following coordinator correction. All native
output checks capture callbacks only; loopback signing uses a public synthetic
key and creates no command or physical-output authority.

Read-only follow-up found a concrete preparation interleaving: the coordinator
publishes its request before a driver calls `listen()`, whose normal cancellation
publishes a fresh zero-identity readiness tombstone. A supervisor poll between
that tombstone and the first positive reply previously rejected preparation.
The coordinator now treats only the exact canonical initial unready shape from
the already-bound bank as Waiting, before any positive readiness for that bank.
Fieldwise checks exclude padding; original deadline, routing and lifetime gates
remain, and revocation after positive readiness still rejects. Deterministic
native regression follows the actual begin/listen/poll order. This establishes
a real defect independently; the old intermittent log cannot prove attribution.
The correction passes native snapshot `/var/tmp/slaaps-native-kywjnp4q`, binary
SHA256 `b187ef31828a54415acaf26e7af9a0032646ae3a8952b279f99458aee64b679d`.
Full runtime `/var/tmp/slaaps-native-runtime-k5zzs5a3` passes the deterministic
production-order regression, original deadline/routing/lifetime/malformed-reply
and post-positive revocation checks, existing manual/bench/EDF lifecycles, mixer
capture, and actual signed companion loopback. Native control/mixer/supervisor
checks are 4,653/2,108/1,821; seven lifecycle and seven persistence cases pass.
Current-source FMUv6X checkpoint `companion-20261023/readiness-final` passes:
image 1,899,452 bytes of 1,966,080, headroom 66,628; AXI 111,100/SRAM4 2,048.
The identified static stack sums and dynamic queue costs above are unchanged.
Supervisor probes are fresh; 29 unchanged translation-unit probes are reused only
after equality of actual Ninja include dependencies and compiler commands (1,525
unique dependency paths). All 49,696 firmware/build manifest entries match native
`kywjnp4q`, with 222 changed anchors directly rehashed. Metadata472a versus current
36a dirty content remains explicitly recorded. Earlier failed runs are retained.

Final evidence under `build/slaaps_firmware/companion-20261023/readiness-final`:

- `resource-comparison.json`: `c3f022e09a1e990d63feaaa36868c3bd2c923c18b769ac945c838dd068002473`.
- `board-result.json`: `3ded059112d7c1304d43df7703379e60ac293ea550fc47d72f14a1233fb9e74e`.
- `source-closure.json`: `444f597303a41b85f08b553bbfee4c70d7a5c7d05cbc16a2aa658b5bc9a7ac54`.
- PX4 package: `99a10d247e7cdcc24c1d86f820b232b345b903ad648423adbabfed4f1d4067b4` (1,776,384 bytes).

Staged WSL artifact:
`/tmp/px4-firmware/px4_fmu-v6x_slaaps_36a17722ef_dirty_companion-20261023_20261024T014539Z.px4`.
All jobs are terminal. No target boot, physical operation, runtime stack high-water,
timing or flight qualification is established. Admission/control remains unfinished.

The separate, previously unlinked acceleration-library ARM check exposed absent
`<limits>` and `<initializer_list>` support. Six production files now use exact
C limit constants and a fixed-array status helper with unchanged semantics.
Nine existing focused host tests pass in
`/var/tmp/slaaps-acceleration-cmake-kJrgun` (`portability-ctest.log` and XML).
All five production translation units also compile with strict board flags in
the separate `companion-20261023/final/unlinked-portability-followup` probe;
result SHA256 `778a97a237783c66146f1c78201f03da475c4efe929566589f3e562d9fec0259`.
Its first probe-command failure is retained alongside the original header failure.
The firmware volume was read-only throughout. This proves ARM portability, not
supervisor use or acceleration control.

## October 23: scheduled manual control

The optional empirical 2x2 table uses selected raw indicated airspeed and the shared
EDF owner's normalized command. The same bounded, filtered interpolation updates
PID gains and signed moment effectiveness; no inferred wash law or extrapolation
is provided. Integral-output compensation preserves P/D output across feasible gain
transitions; infeasible transitions and invalid interpolated rank reject operation.
Original sensor expiry caps command expiry, including while bank ACKs are pending.
Native observation requires positive airspeed confidence and binds publisher,
selected instance, barometer/calibration, temperature-source and battery identity.
Upstream fallback temperature is explicitly not measured-density qualification;
vehicle_air_data does not expose its separate temperature sample clock/device.

The supervisor checks the initial zero-EDF point and controller matrix before
preparation, without obtaining propulsion permission. WAIT_SCHEDULE gives a
Commander error for unusable conditions. A nonzero minimum EDF coordinate does
not authorize running propulsion to reach that range. Ground spool/takeoff policy,
local flow angles/wash and a measured admissible envelope remain separate gaps.

Portable evidence: `/var/tmp/slaaps-configuration-bsxx03zy/result.json`, SHA256
`fd464c4bb79f03b25a68ba5c923a2fc8aa18ec674a8d72b3e1c53f64b2c3bc7f`.
Debug, Release and ASan/UBSan pass the shared configuration, actual PID/controller,
allocator and worker checks (2,968 each); native parameter generation validates
296 parameters. This does not compile or verify the native supervisor/adapter.
The initial reusable Debug run failed a test's Report object-padding comparison:
case 0 correctly returned InvalidSchedule and revoked eligibility. Replacing the
memberwise comparison copy with a byte snapshot fixed the test without production
changes; the full matrix then passed. An initially suspected off-center binding
bug was ruled out by existing provisional-binding cleanup; no redundant fix remains.

Native build `/var/tmp/slaaps-native-sl0rv9ry` and runtime
`/var/tmp/slaaps-native-runtime-k9xpcx7h` PASS. Runtime receipt SHA256:
`58b63654ea82dbc0a61e0ded5c20c92f99321472d45cc1e40fdb2a1a081cb58f`;
binary SHA256 `ac10af1844f015a86e9619f44924234b5ea63d1fbae220a8b202956780e8fcae`.
The actual adapter rejects invalid confidence, stale/original clocks and replaced
publishers while honoring selected nonzero instances. Actual-source supervisor
checks reject out-of-range IAS and an interior rank loss, then accept centered
in-range disarmed preparation without any output or flight permission. Scheduled
commands cross the real worker, transport and mixer to capture-only driver callbacks.
Input/control/mixer/supervisor checks pass (34/4,287/2,090/1,797), as do seven
lifecycles and seven restart/export-restore scenarios. Real Commander servo and EDF
commissioning regressions pass; EDF uses the explicitly recorded internal forced
arm because normal no-plant health denies arming. Normal vehicle health is unqualified.

The native first attempt failed on a duplicate subscription name in the new test
fixture. It was renamed without production changes; the same unsealed build resumed.
`compile-attempt-1-result.json` and manifest revisions retain the failure/correction;
the retry replaced compile.log, so its full original text remains only in tool output.
Final fixture SHA256: `00d252546fafac073fd4e7d14381f6b94f82af9a1b1835bccb134b56045928bc`.

FMUv6X target build, package/ELF/source checks, six fresh ARM probe groups, 237 active
probe dependencies and production test-seam exclusion PASS. Evidence is in
`build/slaaps_firmware/scheduling-20261023`. Image 1,892,652 bytes (+31,368 from EDF),
flash headroom 73,428, AXI SRAM 111,100 (+64), SRAM4 2,048. Endpoint objects are
8,416 bytes each (+1,040); supervisor 12,096 (+2,224). Identified static MAIN/AUX
chains are 2,860/2,804 of 4,096; supervisor maximum 3,976 of 6,500. The new initial
schedule/rank prearm chain is 2,240 (2,352 through activation), and scheduled worker
allocation is 3,568. These sums exclude unmeasured runtime/libc/uORB overhead and
are not stack high-water or timing qualification. No stack reservation was increased.
The first board attempt failed because three adapter calls used metadata pointers
instead of topic IDs; corrected adapter SHA256:
`1748f374ce77b028c98580e4f713acba28a549376a9f8ce43bf34a636d37cff1`.
The same unsealed build resumed; build-attempt-1.log and old source bytes are retained.

The staged package is
`/tmp/px4-firmware/px4_fmu-v6x_slaaps_36a17722ef_dirty_scheduling-20261023_20261023T201832Z.px4`,
1,770,368 bytes, SHA256 `6c0a0ffe8e35b532ab543fab3e530a02346b978ca162ee5bc503628ba995f446`.
It retains baseline embedded Git metadata 472a7d6786; the dirty manifest identifies
its content. Expanded committed-plus-dirty source closure finds no firmware mismatch:
board 127 existing matches/2 correct deletions, with 42 unused Tools differences;
native 164 matches/2 deletions, with five unused Tools differences and current build
runner verified. Differing tools are not referenced by the respective compile commands
or build.ninja. See source-content-closure.json and
`build/slaaps_native/scheduling-20261023/native-current-source-comparison-final.json`.
This is current changed firmware-content evidence, not whole-checkout byte identity.

Current supervisor SHA256:
`a92555dc2dcf67fe9c8f32173cc9aef663ad3705f8bbb1e3e46d9c67bcac9564`.
No physical output, QGC GUI acceptance, target boot, measured tuning, manual flight
admission or vehicle qualification is established. Companion acceleration remains
incomplete; the initial zero-airflow/zero-EDF authority and ground spool/takeoff policy
remain unresolved for the actual vehicle.

## October 23: armed EDF commissioning

Production revision `7a8ad9ff` adds explicit armed EDF commissioning to the shared
Bench path: accepted scope2, target5, zero-default `SLA_EDF_BMAX`, low raw throttle
entry, first-zero handling, capped slew, parked canards, immutable duration and
ordinary disarm requests. Partial arming can retain only the original denied
permission before its first grant. Loss of agreement after a grant terminates
that session; the latch cannot block a later deliberate disarmed re-entry.

Current board/resource checkpoint
`build/slaaps_firmware/shared-edf-bench-final-20261023/resource-comparison.json`
passes (SHA256 `e08dd57228193e6575bc1e1ee7021d04413abbcee0e2821d7e404f70e18d433f`).
Image 1,861,284 bytes leaves 104,796 bytes flash; AXI SRAM 111,036 and SRAM4 2,048
are unchanged. Fresh ARM probes identify MAIN/AUX maxima 2,812/2,756 within 4,096
and supervisor 3,960 within 6,500, before unmeasured overhead. Endpoint objects
are 7,376 bytes each; supervisor 9,872. All 54 compiled production hashes match;
later fixture/harness edits are uncompiled board inputs. A canceled slow WSL Git
scan is recorded separately from the successful unchanged Windows verifier.

Final native build `/var/tmp/slaaps-native-7tlcez9j` and runtime
`/var/tmp/slaaps-native-runtime-uofe3loo` PASS: 34 input, 3,686 control, 1,780
mixer and 1,096 isolated supervisor checks, seven lifecycles and seven restart/
export-restore scenarios. Binary SHA256
`4de0b27d1aecd6e06e6846419f161d48b15ab18ee9980f6fab984f976ae31b90`.
Runtime receipt SHA256
`07a68f27abb4a6b3874e7580081c29fc76b53b2e86b5a2ba8a01993c0425c902`;
local closure `build/slaaps_native/edf-commissioning-20261023/final-verification.json`
SHA256 `2c06e11e5fb38ee7cb3fe9cb2d4d96cf03044d0e0198b5795d719f474a3eff4f`
binds all 63 current relevant source hashes. All owned jobs are terminal.
The normal production supervisor, real Commander and actual mixer callbacks
demonstrated disarmed non-actuating preparation, armed initial-zero/capped EDF,
parked canards, all-bank zero stop, ordinary disarm and no automatic revival.
Accepted scope2 survives export/restore; saved EN=1 restores to EN=0 without
arming or output permission. The no-plant environment denied ordinary arming;
the capture-only test explicitly used stock internal forced arming. It neither
injected SLAAPS permission nor qualified normal vehicle health or flight admission.

Preserve the earlier failed attempts and their corrections:

- `slaaps-native-38ky2la6`: preceding supervisor `ca688ca2`, link only.
- `slaaps-native-bitwxwed`: final production link; runtime `db4h18bd` failed a
  fixture that checked pre-write saved markers. Fixed by recapturing parameters.
- `slaaps-native-rj25pgu_`: corrected fixture link; runtime `o8y7uvhj` exposed an
  unsupported 150 ms idle-maintenance reset assumption. Replaced with observed
  reset, continuous inactive/session checks and unchanged-command verification.
- Reruns `gcriiz7v` and `h7o9g09c` passed isolated supervisor/mixer and real
  Commander servo commissioning. The latter demonstrated capped EDF PWM, neutral
  canards, all-bank zero stop and ordinary disarm. Harness command-name and
  legitimate mode-exit assumptions were corrected after these retained failures.
- Final fixture/harness build `slaaps-native-7tlcez9j` and runtime `uofe3loo`
  close these software verification failures; physical qualification remains open.

Native paths above are under `/var/tmp`, with runtime prefix
`slaaps-native-runtime-`. All callbacks capture outputs only; no hardware was
operated. Neither synthetic calibration nor static stack analysis proves flight
readiness, physical handling, target boot or runtime stack/timing qualification.

## October 22: shared servo-only Bench integration

The production Bench path now uses the shared accepted vehicle profile, logical
canards, asymmetric travel bounds, mapped PWM banks and exact activation/handled
ACKs. It requires genuine disarmed/prearmed authority and a fresh, deliberate
`SLA_BENCH_EN` edge after readiness. `SLA_BENCH_CH` selects combined or individual
canard operation; `SLA_BENCH_SEC` bounds the session. Stock QGC parameter metadata
contains these controls (192 parameters total). EDF commands remain explicitly
inhibited; this is not the requested future armed EDF commissioning capability.
Dedicated-airframe output ownership also blocks stock actuator-test bypasses while
inactive/prepared and keeps endpoint servicing scheduled without stock publishers.

Current integrated software evidence:

- Portable `/var/tmp/slaaps-configuration-vplb804w/result.json` passes 2,361 checks
  each in Debug, Release and ASan/UBSan, plus 192-parameter metadata validation.
  All 102 recorded inputs match at completion. Receipt SHA256
  `42b00c57178afd3b623c6cd94206f49ca35320c743b1b42c73ab455774e73081`.
- Native `/var/tmp/slaaps-native-14eh2_6k` and runtime
  `/var/tmp/slaaps-native-runtime-6mvmc_9r` pass 34 input, 3,616 control, 1,113 mixer,
  and 714 isolated actual-source supervisor checks, seven endpoint lifecycles and
  six restart/BSON scenarios. Binary SHA256
  `7b8d2388ae3b8a1b5e7dcc06bae66ed877f20e185c96560220fe0d275d9c2835`.
- The normal production supervisor has no test admission seam. Real Commander
  selects registered Bench mode 25 and supplies genuine disarmed/prearmed authority.
  A deliberate enable drives the configured individual canard through both actual
  mixer callbacks; captured AUX8 exceeds neutral while EDF AUX4 stays zero. Explicit
  stop produces fresh zero frames in both banks, completed entries cannot revive,
  and ordinary arming remains denied. Configuration/input publishers are synthetic;
  the callbacks capture outputs and do not operate hardware. GUI acceptance remains open.
- The actual-source supervisor fixture also covers combined/individual operation,
  early disable before full preparation, fixed duration, raw loss, profile retention
  on unchanged normal stop and deliberate re-entry. Its authority topics are synthetic
  in a separate process, isolated from the real-Commander production scenario.

The stop-race correction recognizes GUI disable between the two coherent command
snapshots, rejects simultaneous bound target/duration edits, and rechecks current
routing/configuration before retaining an active profile. Shared pure validators
replace disposable owner objects and preserve the original raw-source/mapping checks.
Commander CLI now exposes exact `ext1..ext8` names through its existing mode-command
handler, with invalid names returning failure; this does not change admission.
Final board `build/slaaps_firmware/shared-servo-bench-final-20261022` passes source,
ELF/package, test-seam exclusion, six fresh ARM groups and active-interface probes.
All 152 frozen inputs and 54 current production source hashes match. Image 1,856,060
bytes leaves 110,020 bytes of flash; AXI SRAM 111,036 and SRAM4 2,048 are unchanged.
The pure-validator extraction reduces Bench activation MAIN/AUX chains by 948 bytes
each, to 2,692/2,636. Overall identified physical maxima are now Manual activation
2,812/2,756, leaving 1,284/1,340 in the 4,096-byte queue before unmeasured overhead.
The new supervisor normal-stop routing check is explicitly included: 3,904 bytes
against its 6,500-byte stack. Endpoint objects remain 7,344 bytes each; supervisor
9,808. These are static identified paths, not runtime stack/timing qualification.
Final `resource-comparison.json` SHA256 is
`30c1a56880fe4315aba8c3c4925b77096e6a629391507bc50d26b01697266552`.
The sealed artifact is
`/tmp/px4-firmware/px4_fmu-v6x_slaaps_36a17722ef_dirty_shared-servo-bench-final-20261022_20261023T014701Z.px4`
(1,741,387 bytes), SHA256
`85563aef1045fb2d9f0e2ce6e0ad767431ea72c4c6db53e93a95ceeeb2985dd3`.
Generated configuration and all 11 linked queue constants confirm 4,096 bytes;
the global stock default remains 2,800. All build/probe jobs are terminal. No firmware
was flashed. Earlier evidence is retained below.

Intermediate evidence and failures before those corrections:

- Portable `/var/tmp/slaaps-configuration-c626bqvt`: 2,361 checks each in Debug,
  Release and ASan/UBSan, plus 192-parameter metadata validation. Later native-only
  supervisor fixture additions are not covered by that source receipt.
- Native build `/var/tmp/slaaps-native-rczxflzn` passes. Independent runtime
  `/var/tmp/slaaps-native-mixer-independent-j3s24s12` passes 34 input, 3,616 control
  and 1,113 actual mixer checks, including shared Bench handling and stop cases.
  Binary SHA256 `df12bcd834579cc2af0e75f0889aec0cfa6eeb0c448f330f588cbb55e7a43cd9`.
- Runtime `/var/tmp/slaaps-native-runtime-etgnus5j` failed using the obsolete
  sealed-source harness: its Bench fixture omitted required travel bounds.
  Current harness supplies explicit bounds. Do not treat that run as a pass.
- Current harness run `/var/tmp/slaaps-native-runtime-4bb02ywx` fails the isolated
  actual-source supervisor's ordinary GUI-disable case. A disable between two
  command-parameter snapshots was incorrectly classified as a fault. The corrected
  current runtime above passes; the original failing run remains unchanged.
- Board `shared-servo-bench-20261022` build, source/ELF/package verification,
  test-seam exclusion and six fresh ARM probe groups pass. Image 1,855,892 bytes,
  headroom 110,188; AXI SRAM 111,036 and SRAM4 2,048. Supervisor object 9,808 bytes
  (+888), each physical endpoint 7,344 (+64). Activation chains 3,640/3,584 leave
  only 456/512 bytes in the 4,096-byte queue before unmeasured overhead. This is
  unresolved; removing disposable validation objects is the next correction.
  `resource-comparison.json` explicitly records the superseded source snapshot;
  subsequent stop/fixture/harness changes are not covered by this board result.

All outputs in native tests are capture-only. No hardware was operated. Flight
admission, measured qualification, armed EDF commissioning, scheduled effectiveness
and companion acceleration remain incomplete. Prior evidence below is retained
for its exact source checkpoint, not as acceptance of these later changes.

## October 22: supervisor activation and live observation clocks

The production supervisor now connects prepared banks to activation, the selected
manual worker, shared EDF ownership and sustained command/handled-ACK processing.
It reports ACTIVATING, FIRST_COMMAND and RUNNING separately; RUNNING requires the
first exact command to be handled by every participating bank. Both staggered
arming orders retain the original preparation bound. Partial activation, input or
authority loss, configuration/routing changes and output failure cancel ownership
and require deliberate re-entry. Normal disarm can retain only the unchanged
accepted profile after both fresh disarmed observations; faults require a real
disarmed reapply. Four reply callbacks complement the periodic expiry check.

`SLA_STATUS_AGE`, `SLA_ARM_AGE`, `SLA_ACT_ACK_US` and `SLA_FIRST_CMD_US` are typed,
QGC-editable timing fields. All-zero timing permits inactive profile staging only;
partial timing is invalid and manual preparation requires complete valid timing.
The activation timeout and first-command budget must fit the configured cycle
bound. This adds four parameters, bringing native metadata to 189. Portable host
`/var/tmp/slaaps-configuration-a5522egg` passed 2,314 checks each in Debug,
Release and ASan/UBSan, plus metadata validation. Later clock changes are native.

Actual-source testing exposed a startup deadlock: activation-reply lifetimes were
advertised only after a proposal, while the supervisor required them before its
proposal. Endpoints now advertise both reply lifetimes at initialization, without
publishing authority. Partial advertisement and publisher replacement still reject.
Repeated operation also exposed valid messages being rejected as future-dated
because callers sampled time before copying concurrently published messages.
Live routing, permission, preparation, activation, command and ACK readers now use
post-copy HRT. Existing explicit-time APIs remain strict. Final selected-input,
configuration, identity and expiry checks remain; original wire/sample times and
deadlines are never renewed. Supervisor permission expiry is capped by its copied
Commander/arming freshness, and driver-local permission expiry is further capped
by copied vehicle, actuator, supervisor and routing-report freshness. Existing
bank guards enforce that bound after later reads and before handled ACKs.

Intermediate native `/var/tmp/slaaps-native-jox96upl`, runtime
`/var/tmp/slaaps-native-runtime-ylddtqyj`, passes 3,569 control, 34 input, 626 mixer
and 357 actual-source supervisor checks, seven lifecycles and six restart/BSON
scenarios. This includes both arming orders, first-command handling, sustained
synthetic commands, cancellation, normal disarm and deliberate re-entry. The
positive supervisor fixture runs in a separate PX4 process with capture-only
callbacks and a compile-time test seam. The normal module has no seam; real
Commander ordinary arming remains denied, availability masks remain zero and
production permission flags remain false. Final native
`/var/tmp/slaaps-native-ads0tcog`, runtime
`/var/tmp/slaaps-native-runtime-_hc67oph`, also passes the complete suite with the
two-file authority-expiry clamp. Binary SHA-256 is
`3286242615cfc22325ed2ddf285eb0585ada66b638b3dce6ed900d29a5700fac`;
harness SHA-256 is `0e386a1c1e24eff866a3b6ddc816cc7b0659f78c5ce527d9f107f0d2d2961382`.

Retained failed runs explain the corrections: `eswkm_aq` found the endpoint
advertisement deadlock; `lfcvyvzx` passed the positive scenario but exposed retained
synthetic registration contaminating later Commander tests; `tr8v7zc9` exposed an
inappropriate 100-ms synthetic authority age for real Commander's 2-Hz unchanged
status publication; `44p5iiue` exposed the live ACK clock race. All are native
runtime directories under `/var/tmp/slaaps-native-runtime-`. The real-Commander
fixture now explicitly applies its 1-s synthetic age through parameters; no
production default or qualification claim was introduced.

Board `manual-supervisor-replies-final-20261022` verified the earlier activation
and timing baseline: image 1,834,740 bytes, flash headroom 131,340 bytes, AXI SRAM
111,036 and SRAM4 2,048 bytes. It excludes subsequent live-clock and expiry fixes.
Expiry-clamp board `manual-live-expiry-final-20261022` passes current source,
ELF/package/isolation and static resource checks: image 1,841,708 bytes, flash
headroom 124,372, unchanged static RAM and endpoint/supervisor heap sizes. Fresh
ARM analysis found MAIN/AUX activation chains of 2,680/2,624 bytes, leaving only
120/176 on the previous 2,800-byte hp_default stack before unmeasured overhead.
The dedicated SLAAPS board now uses the existing Kconfig stack setting at 4,096
bytes, without changing stock targets. Final board
`manual-hp-stack-final-20261022` verifies that setting in the generated configuration
and all 11 linked ELF hp_default constants; the global default remains 2,800.
Image size is 1,841,700 bytes with 124,380 flash headroom; static RAM and heap
object sizes remain unchanged. The one shared runtime queue requests 1,296 more
stack bytes. Identified MAIN/AUX margins are 1,416/1,472 before unmeasured overhead;
the supervisor chain is 3,336 of 6,500. All six ARM groups were freshly compiled
because generated configuration changed their dependencies. The active-interface
probe covers 235 compiler dependencies. Current production recheck matches 46
production sources and the full 150-file freeze, with unchanged fixture and no
synthetic authority in the board. See
[`resource-comparison.json`](../build/slaaps_firmware/manual-hp-stack-final-20261022/resource-comparison.json)
for bound receipts, including queue configuration, independent verification,
resource summary, stack chains and test isolation. Firmware is staged at
`/tmp/px4-firmware/px4_fmu-v6x_slaaps_36a17722ef_dirty_manual-hp-stack-final-20261022_20261023T003844Z.px4`,
1,725,915 bytes, SHA-256
`85abc45a7a6f26bff31a1867ca59596b31f6cff5638e95be316279386791abc7`.
Source manifests identify the dirty checkout despite the retained older embedded
tag. No boot, runtime stack/timing or electrical qualification is claimed.
No hardware operated. Production
measured-condition/recovery admission is still absent, so these connected callers
and synthetic successes are not a completed operational flight mode.

## October 22: prepared-bank first-grant ordering

Prepared physical-bank endpoints now have a bounded non-actuating wait for the
first permission grant. Commander may report arming before the supervisor grant,
or vehicle_status and actuator_armed may arrive separately. Both messages must
remain fresh, with valid selected mode, configuration, routing and publisher
bindings; both must report armed before grant acceptance. Waiting never produces
PWM, activation acknowledgment or handled-command acknowledgment. It cannot begin
from an already-armed unprepared bank and cannot extend the original preparation
deadline. The previously captured stop frame remains owned during this interval.

The new explicit `awaitGrant` path leaves ordinary strict permission reads intact.
Only an exact all-denied disarmed decision may remain pending after its lease;
it is never authority. An armed denial, inhibition, stale positive, identity or
publisher change, sequence mutation/regression, or original preparation expiry
rejects. A fresh positive observed before local arming agreement keeps its own
expiry. After the first accepted grant, disagreement or permission loss remains
a fault and cannot return to waiting.

The existing activation endpoint may defer a validated ACTIVATE until permission
arrives. Its exact original publication time and proposal stay fixed; cancellation
and publisher checks remain live. Substitution with an older-session transition
after a deferred current ACTIVATE also rejects. No wire message or runtime flight
admission bypass was added. Production supervisor armed activation remains absent.
Root and independent review covered the two publication-order races and pending
mutation/cancellation behavior. Native `/var/tmp/slaaps-native-3vgutxj5`, runtime
`/var/tmp/slaaps-native-runtime-1t807zj5`, passes 3,386 control, 34 input and
622 actual-mixer checks. The real mixer is pumped between separate arming,
permission and ACTIVATE publications; no PWM or activation/handled ACK occurs
until both arming messages agree and permission is accepted. Existing productive
worker sessions and seven bounded-wait fault scenarios pass, including post-grant
disagreement that cannot resume. Real Commander disarmed entry/re-entry, seven
supervisor lifecycles and six restart/BSON scenarios also pass. Positive flight
authority remains explicitly synthetic and callbacks capture-only. The portable
host behavior did not change in this native-only increment; its prior matrix was
not rerun.

Board `manual-first-grant-final-20261022` passes independent source/ELF/package/
capacity verification, six current ARM stack groups and the active-interface
probe (235 compiler dependencies). The image is 1,825,796 bytes (+712), with
140,284 bytes flash headroom; AXI SRAM remains 111,036 and SRAM4 2,048 bytes.
Endpoint/supervisor heap sizes remain 7,240/7,576 bytes. MAIN/AUX identified
chains are 2,520/2,464 of 2,800, leaving 280/336 bytes before unmeasured overhead.
The final production recheck matches 45 current source hashes and the exact
fixture, with three production refreshes and no drift. See that stage's
`resource-summary.json`, `stack-chain-review.json`, `production-recheck.json`
and `active-interfaces/result.json`. Dirty-labeled firmware is staged under
`/tmp/px4-firmware/` with suffix
`manual-first-grant-final-20261022_20261022T222826Z.px4`, SHA-256
`3ed2bb6a2a60a70a586ebe3a3d825039eb69d6b171d449d7172134d577234fd6`.
Source manifests identify the dirty checkout despite its retained older embedded
tag. Static resource evidence does not establish boot, runtime stack/timing,
electrical behavior or flight qualification. No hardware operated.

## October 22: asynchronous first-command delivery and bank stack reduction

The shared EDF input, owner, worker and output-bank consumer now distinguish the
immutable preparation/session-entry timestamp from a later permission observation
and control decision. A fresh post-entry pilot sample may precede permission and
bank activation. Its raw timestamp and deadline remain unchanged. Samples before
entry still fail; a calculated actuator frame must still postdate actual bank
activation. Activation replies retain actual bank activation time. Renewed
permission cannot extend a held raw sample or the original no-request timeout.
This fixes a real production integration defect hidden by equal-time fixtures;
it does not grant production flight admission or choose airborne recovery behavior.

Each serialized bank transport now owns typed read/decode scratch, separate from
its retained command baseline and the consumer's pending handling ticket. This
removes automatic wire/frame copies on activation and dispatch paths without
changing ownership, replay, expiry or successful-driver-handling requirements.
Root and a second implementation owner reviewed the buffer lifetimes with no
aliasing findings. ARM measurement shows +256 bytes per endpoint and a 136-byte
reduction in the largest identified physical-driver chains.

Host `/var/tmp/slaaps-configuration-8liblhst` passes 2,266 checks in each Debug,
Release and ASan/UBSan configuration; 185 metadata entries validate. New coverage
uses session < raw sample < permission < decision, checks both banks and handled
ACKs, original sample expiry, pre-entry rejection, no-request timeout, actual
activation time, and rejection of frames calculated before bank activation.
The existing native worker/mixer scenario now captures the first pilot sample
before permission and bank activation, then delivers it at a later decision.
Native `/var/tmp/slaaps-native-f4ia3lc_`, runtime
`/var/tmp/slaaps-native-runtime-hepov45a`, passes 3,194 control, 34 input and
446 actual-mixer checks. Three actual worker/mixer sessions deliver the earlier
raw first sample through capture callbacks and true handled acknowledgments,
retaining partial-failure and callback-time authority-revocation coverage. Real
Commander disarmed readiness/cancellation/re-entry and six restart/BSON scenarios
also pass. Active-worker authority remains explicitly synthetic; production
supervisor activation remains disabled. No physical outputs are exercised.

Board `manual-first-command-final-20261022` passes independent source/ELF/package/
capacity checks, all six ARM stack groups and the active-interface ARM probe.
The image is 1,825,084 bytes (+360), with 140,996 bytes flash headroom; AXI SRAM
is 111,036 and SRAM4 2,048 bytes. Endpoints are 7,240 bytes each, supervisor
7,576 bytes (+8 for the owner start clock), capture workspace 1,560 and mixer
1,200 bytes. MAIN/AUX identified maxima are 2,512/2,456 of 2,800, leaving
288/344 bytes before unmeasured overhead. The supervisor chain is 2,504 of
6,500. See `resource-summary.json`, `stack-chain-review.json`,
`production-recheck.json` and `active-interfaces/result.json` in that stage.
All 45 changed production sources match; the fixture hash also matches. These
static measurements do not establish runtime high-water, timing, boot or vehicle
acceptance. `resource-comparison.json` links the final receipts. The dirty-labeled
firmware is staged under `/tmp/px4-firmware/` with suffix
`manual-first-command-final-20261022_20261022T220911Z.px4`, SHA-256
`04386a3f048d3569b9ca343de167c524e7fbf2c5aaffe66f9ccb4e6fed57bfd5`.
The embedded ancestor tag is retained in receipts; source manifests identify the
current dirty checkout. No hardware has been operated.

## October 22: deliberate disarmed manual entry

The supervisor now uses fresh Commander `nav_state_user_intention` and disarmed
state to require an observed departure followed by deliberate SLAAPS Manual
selection. Applying or restoring configuration alone no longer prepares outputs.
Startup already selected, stale Commander state, or rejected bound input cannot
silently establish a new entry. Initial missing configuration, routing, selected
input, centered sticks, low normalized throttle or valid numerical state produces
an actionable readiness status. Once bound, input loss, publisher replacement,
configuration/routing changes and the original preparation deadline cancel the
session; repaired data or another apply cannot revive it without mode re-entry.

The same selected-input observer binds all seven publisher lifetimes, sensor IDs
and source epoch. Disarmed observation is explicit and cannot be used through the
armed guard API. Raw selected throttle is validated alongside attitude axes before
shared endpoint normalization. Initial input must postdate the entry. No output
permission is granted: all availability masks remain zero and the production
supervisor still does not start the manual worker. The 100 ms diagnostic cadence
and original one-second preparation lifetime do not establish a persistent
ready-to-arm state or qualified active-control timing.

Commander emits ten specific entry/readiness events through native PX4 event
metadata, including configuration, routing, selected input, centering, throttle,
state, cancellation/re-entry and the remaining flight-admission boundary. Generated
metadata contains every event; actual QGC GUI operation remains unverified.

Host `/var/tmp/slaaps-configuration-r0klcf4k` passes 2,234 checks each in Debug,
Release and ASan/UBSan, with 185 parameter metadata entries validated. The first
native run `/var/tmp/slaaps-native-runtime-0xtfdrzp` exposed a fixture defect after
publisher withdrawal: `Publication::unadvertise()` retains its handle, making
subsequent `advertise()` a no-op. Production correctly rejected the unavailable
publisher. The fixture now reconstructs the publication in serialized execution,
verifies a changed nonzero lifetime and refuses to count unadvertised publishes.
Corrected native `/var/tmp/slaaps-native-1uyz9w8n`, runtime
`/var/tmp/slaaps-native-runtime-i8ve5y_t`, passes 3,162 control, 34 input and
443 actual-mixer checks, seven supervisor lifecycles and six restart/BSON cases.
The production preparation scenario uses real Commander, supervisor and mixer
endpoints with six synthetic pilot/estimator topics; the fixture never publishes
Commander authority. It verifies centered/low-throttle/numerical-state gating,
input loss, publisher replacement, configuration/routing/expiry cancellation,
explicit mode re-entry and apply not retrying a rejected entry. Prepared sessions
remain disarmed with zero admission masks and zero physical outputs. Existing
active-worker tests use separate explicit synthetic authority and capture-only
callbacks. The initial failed evidence is retained.

Board `manual-entry-final-20261022` links and passes independent source/package/
ELF/capacity verification: image 1,824,724 bytes, flash headroom 141,356 bytes,
AXI SRAM 111,036 bytes and SRAM4 2,048 bytes. Existing five ARM probe groups and
an additional supervisor probe pass. Endpoint objects are 6,984 bytes each;
`sizeof(Slaaps)` is 7,568 bytes. Exact supervisor frames are Run 304 bytes,
updatePreparation 400 bytes and observePrearmInput 160 bytes, on the separate
6,500-byte supervisor queue. These are static measurements, not runtime stack or
timing qualification. The conservative identified supervisor chain is 2,496 bytes
of 6,500; physical MAIN/AUX chains remain 2,648/2,592 of 2,800 before unmeasured
overhead. See `resource-summary.json`, `stack-chain-review.json` and
`production-recheck.json` in that board stage: 41 current changed production
files match, and both old module-path removals remain absent. The board snapshot
retains the original native-only fixture hash; its correction is explicitly
recorded separately and does not participate in the board target. The staged
package SHA-256 is `c42fb41f442f1c23da6287e4675ae1fb11a1e860d8625c7204ee55e42c5ac309`.
Source manifests identify the dirty checkout; the embedded tag remains older.
The corrected build also revalidates all ten event definitions in
`manual-entry-events.json`; actual QGC GUI operation is unverified. No hardware
has been operated.

## October 22: shared pilot input configuration

The shared profile now explicitly selects RC-only or pinned MAVLink-only with
`SLA_IN_SOURCE`; no automatic cross-source fallback is introduced. RC requires
identity -1/0/0, while MAVLink requires the selected instance/system/component.
`SLA_R_INPUT`, `SLA_P_INPUT` and `SLA_Y_INPUT` select a unique permutation of the
three PX4-selected attitude axes. Existing body-axis signs, rate limits, deadband
and centering hysteresis remain in effect. Native metadata exposes source and
axis selectors as QGC enums; an actual QGC GUI session has not been verified.

`SLA_THR_LOW` and `SLA_THR_HIGH` define the raw selected throttle values for zero
and full EDF request. Descending endpoints implement reversal. Manual control
requires finite unequal endpoints in [-1,1] and rejects samples outside their
calibrated span. Raw source, timestamps, duplicate mutation and freshness are
checked before conversion; transformed values cannot conceal a changed raw sample.
Low-throttle entry is evaluated after conversion by the existing shared EDF owner.

The same permutation/signs feed the bench body-axis mix, and `SLA_IN_AGE` now
controls bench input freshness instead of an ignored parameter with a fixed
500 ms timeout. Legacy 44-token bench profiles retain their old identity mapping,
signs, MAVLink policy and 500 ms default. Shared SLAAPS profiles require explicit
source, axis and sign choices; existing accepted markers cannot silently authorize
new defaults. Servo-only bench validation does not require EDF throttle endpoints.
This connects configuration to existing controllers and the worker; it does not
add production supervisor admission or complete operational commissioning.

Host evidence `/var/tmp/slaaps-configuration-_8wwi952/result.json` passes 2,178
checks in each Debug, Release and ASan/UBSan build. The real PX4 metadata pipeline
validates 179 vehicle fields and six apply/acceptance fields, including enum labels.
Tests cover source/identity rejection, mapping consistency between bench and
flight, configured bench-age boundaries, reverse throttle and deliberate low entry.
Existing shared bench/controller CMake suites also pass Debug, Release and
sanitizers at `/var/tmp/slaaps-input-compat-jidoil3y`; no legacy defaults changed.
Native `/var/tmp/slaaps-native-3f1_conj`, runtime
`/var/tmp/slaaps-native-runtime-jlnmav1u`, pass 2,983 control, 34 input and 443
actual mixer checks, seven supervisor lifecycles and six accepted-profile restart
scenarios with BSON export/import. The three actual-worker/mixer sessions use
RC-only input, permutation {2,0,1}, and descending throttle endpoints {.75,-.5}.
Assertions verify raw yaw produces the expected body-roll canard signs and low
then raised throttle produces stopped then slewed EDF PWM. All output callbacks
are capture-only with synthetic authority; production supervisor still grants none.

Current [board verification](../build/slaaps_firmware/input-mapping-final-20261022/independent-verification.json)
passes source/ELF/package/capacity checks: image 1,820,732 bytes, flash headroom
145,348 bytes, linked AXI SRAM 111,036 bytes and SRAM4 2,048 bytes. Each bank
endpoint is 6,976 heap bytes, including the unchanged 1,560-byte capture workspace;
MixingOutput is 1,200 bytes. All five exact ARM probe groups were rerun. The
[stack-chain review](../build/slaaps_firmware/input-mapping-final-20261022/stack-chain-review.json)
identifies MAIN/AUX activation chains of 2,648/2,592 bytes against 2,800, leaving
152/208 before unmeasured runtime/libc overhead. Runtime stack and timing remain
unqualified; these figures cannot establish safe flight operation. No hardware
was operated, and no flight mode availability was enabled.

The next supervisor step can observe deliberate disarmed manual intent without
weakening availability: UserModeIntention already allows disarmed selection,
while fresh `vehicle_status.nav_state_user_intention` identifies the requested
mode. This does not supply measured-condition or recovery admission.


Updated 2026-10-22 by Codex toward the [airframe goal](SLAAPS_AIRFRAME_GOAL.md).
The current milestone is below; earlier dated sections retain their original
evidence and limitations. Software progress is not vehicle or flight acceptance.

## October 22: production manual output endpoints

Real MAIN/AUX `MixingOutput` endpoints now connect existing preparation,
activation/cancellation, worker commands and handled acknowledgments at the
serialized output boundary. The bank checks its physical driver token, complete
observed routing, exact accepted parameter configuration, selected mode and
independent current Commander/local guards. A separate manual-permission topic
binds the supervisor publisher lifetime and original bounded expiry; preparation,
activation, command and reply identities remain bound to their own publishers.
Replacement, malformed/stale data, configuration/routing changes and partial
activation cancel or latch the session; a repaired snapshot cannot resume it.
The original preparation deadline cannot be renewed by duplicate proposals.

Calibrated PWM microseconds go directly to the existing driver's `updateOutputs`
callback. A claimed SLAAPS bank suppresses ordinary output processing. The
endpoint validates authority immediately before the callback and again afterward,
including elapsed time, current configuration/inventory and the exact saved ticket.
A true callback plus successful final validation is required before a handled ACK
is published. A false callback is rejected; callback success is software handling,
not electrical feedback. Already handled retained commands produce no repeated
write or ACK, while their original expiry continues to be checked.

Fresh explicit disarm, kill, lockdown or termination uses the stop frame retained
from the accepted preparation: configured disarmed PWM, or failsafe PWM on
termination. Armed configuration corruption cannot replace that frame. A failed
stop callback remains pending and can retry only under a later fresh explicit
inhibition. Kill release while still armed cannot resume the cancelled session;
a new deliberate preparation is required. Missing or denied permission while
still armed remains `RecoveryRequired`, without synthesizing disarm or inventing
a cutoff/held-thrust policy. Worker ownership, expiry and all-bank handling remain
independent requirements.

The production supervisor owns/advertises this permission lifecycle but publishes
denied permission only. It still does not admit or start the manual worker. All
three registered mode availability masks remain zero. Its actual disarmed
preparation path remains connected to both real mixer endpoints after accepted
manual apply or unchanged accepted-profile restore. Positive permission,
selected-input entry and deliberate mode/arm activation are the next production
boundary; the output endpoint implementation does not complete stabilized manual
flight. Measured condition/recovery policy and broader acceleration/commissioning
requirements remain unresolved.

Recorded host package `/var/tmp/slaaps-configuration-gjk_4u_e` passes 2,075 checks
each in Debug, Release and ASan/UBSan, with native metadata for 179 parameters.
It predates later native endpoint/subscription edits and is not a fresh rerun of
all final sources. The current native build `/var/tmp/slaaps-native-q82yxwli`,
runtime `/var/tmp/slaaps-native-runtime-7uoaroz6`, passes 2,880 control checks,
34 selected-input checks, 419 actual-mixer-output checks, seven supervisor
lifecycles and six accepted-profile restart/BSON export-import scenarios.
Native ELF SHA256:
`aa67d92288f826cb6cea75f5bdc9de6aa94380079a9edd8670a69e94a4dde4e8`.

The native output scenarios use the actual worker and actual MAIN/AUX
`MixingOutput` with labeled synthetic authority and capture-only callbacks.
They cover both-bank activation/handling, exact calibrated frames, rejected
handling, final-write faults, expiry/replacement, retained-command suppression,
accepted stop-frame retention, failed-stop retry and deliberate re-entry.
The actual production supervisor preparation scenarios remain non-actuating and
observe no output callbacks. Native PWM metadata comes from actual driver YAML;
this fixture does not compile the physical PX4IO driver or exercise IO registers.
The later IO-only correction therefore does not supersede its native-compiled evidence.

The PX4IO correction independently forces configured or pending SLAAPS EDF
DISARMED and FAILSAFE register writes to zero, including the high DIS=MIN case.
Readiness additionally reads each EDF channel's actual DISARMED/FAILSAFE register
and requires both to be zero alongside mixer disarmed/actual failsafe values.
PX4IO firmware ignores zero FAILSAFE writes: an old nonzero register can persist,
and readiness now rejects that state. Read failures and mismatches also reject it;
the raw FAIL=65535 sentinel remains invalid. This changes neither the IO protocol
nor airborne recovery policy. Added register-read timing is unqualified.

Focused source-extracted PX4IO evidence
`/var/tmp/slaaps-px4io-disabled-final-uk6akqrd/manifest.json` passes 658 checks each
Debug/Release/ASan-UBSan using the actual update methods and exact Run inventory
block with injected IO semantics. Eight-channel fixtures cover current/pending
EDF assignment, high DIS=MIN, ordinary output preservation, ignored zero FAILSAFE
writes, actual readback/parameter mismatches and read failure. The fixture compiles
with `-Wfloat-equal -Werror`; the tested physical-driver source SHA256 is
`f04792da673021ad17e5eff24ae566d3c814e70375d95fdc0162ac69160a870e`.
An initial board compile rejected a float equality; its retained
[failed attempt](../build/slaaps_firmware/production-manual-output-io-guard-final-20261022/failed-attempt-1.json)
predates the exact-zero range comparison and this final fixture, which supersedes
the earlier 642-check fixture. No physical IO or PWM operation is claimed.

Final board stage
[production-manual-output-io-guard-final-20261022](../build/slaaps_firmware/production-manual-output-io-guard-final-20261022/independent-verification.json)
passes independent current-source, ELF/package and capacity checks, including the
physical PX4IO driver: image 1,818,740 bytes, flash headroom 147,340 bytes, linked
AXI SRAM 111,036 bytes and SRAM4 2,048 bytes. This adds 120 bytes flash versus the
[retained predecessor](../build/slaaps_firmware/production-manual-output-final-20261022/independent-verification.json).
Board receipt SHA256:
`d8a61bcd2543a4531c49ef0975aa46564c78437d766bef4a6251da5df537e4b4`.
Each heap bank endpoint is 6,920 bytes, including its 1,560-byte capture workspace;
`MixingOutput` is 1,200 bytes. Dynamic allocations are additional to linked SRAM.
The final [stack review](../build/slaaps_firmware/production-manual-output-io-guard-final-20261022/stack-chain-review.json)
records identified MAIN/AUX activation chains of 2,632/2,576 bytes against the
2,800-byte `hp_default` stack, leaving 168/224 bytes before unmeasured
runtime/libc/uORB overhead. The fresh physical-driver probe finds PX4IO Run at
144 bytes, 24 more than its predecessor; known workqueue runner frames remain
included. Resolve or qualify this pressure before enabling production outputs;
these sums are not runtime high-water or timing qualification.

The dedicated board variant disables unused stock FW/MC controllers and the
hover-thrust estimator, which SLAAPS startup does not start. Control allocator
remains compiled to supply stock QGC actuator metadata, without being started as
an output owner. The first manual-output build failed because removing it omitted
the metadata mixer section; the next compile caught a shadowed local. Both
[attempt logs](../build/slaaps_firmware/production-manual-output-20261022/) and the
intermediate stack/final stages remain retained. Earlier preparation and board
failures below remain evidence of their original revisions, not current passes.
Dirty source manifests and independent verification identify the overlay; the
retained older embedded Git tag alone does not identify current firmware sources.
No hardware was flashed, energized or operated; no boot, electrical, QGC GUI or
flight acceptance is claimed. Changes remain uncommitted pending user DCO review.

## Historical record

2026-10-20, Codex. First software increment toward the
[airframe goal](SLAAPS_AIRFRAME_GOAL.md); no native mode or physical output
integration is claimed by the host evidence below.

## October 22 previous checkpoint: production supervisor and mixer preparation

Recorded scope at this checkpoint; the current milestone above supersedes its integration boundary.

The production supervisor now discovers real MAIN/AUX preparation-ready and
handled-ack publisher lifetimes, binds the observed physical driver tokens and
routing, and submits one all-bank preparation transaction after an accepted
manual configuration apply or disarmed restore. `MixingOutput` owns a bank
endpoint on each supported PWM bank; the endpoint checks its physical token,
fresh disarmed Commander state, local kill/lockdown/termination guards, current
routing and the exact accepted parameter snapshot before replying ready.
The parameter capture/acceptance adapter moved from `src/modules/slaaps` into
`src/lib/slaaps` so supervisor and driver use the same typed configuration.

Preparation polls even without ordinary output-function traffic. Duplicate
proposals cannot renew their one-second deadline. Expiry, configuration or routing
changes, arming, publisher replacement and supervisor shutdown cancel readiness;
a rejected transaction requires deliberate new apply. Status reports the exact
session, deadline, bank mask and preparation state. The existing one-second
non-actuating transaction bound is not qualified controller/output timing.
Aligning preparation with deliberate mode/arm entry remains necessary; this
diagnostic transaction is not persistent ready-to-arm permission.

This connects production supervisor and mixer preparation, not activation or
actuator operation. Neither registered modes, accepted markers nor ready replies
grant output authority. All mode availability masks remain zero. The next boundary
is a lifetime-bound trusted permission snapshot between queues, existing bank
activation/cancellation, selected-worker entry, and exact handling acknowledgment
around `MixingOutput`'s physical write. `RecoveryRequired` must not silently become
cutoff or held thrust. Measured-condition admission and recovery policy remain
unresolved, alongside the broader manual/acceleration/commissioning requirements.

Current host `/var/tmp/slaaps-configuration-cq5n5q_f` passes 2,066 checks each in
Debug, Release and ASan/UBSan, including native metadata for 179 parameters.
Native `/var/tmp/slaaps-native-45pspsnu`, runtime
`/var/tmp/slaaps-native-runtime-x96410g0`, pass 2,803 control checks, 34 input
checks and seven supervisor lifecycles. The real supervisor and both actual
`MixingOutput` instances complete preparation, reject configuration/routing
changes and expiry, and re-enter after deliberate apply. Capture-only driver
interfaces observe zero outputs throughout; the fixture cannot exercise physical
PWM. It uses synthetic vehicle data and native PWM parameter metadata generated
from the actual physical-driver YAML. The initial native attempt failed because
the native target lacked those PWM parameters; the metadata fixture repaired that
missing test dependency, and the final run above passes.

Final board stage [production-preparation-final-20261022](../build/slaaps_firmware/production-preparation-final-20261022/independent-verification.json)
independently passes current-source, ELF/package and capacity checks: image
1,960,040 bytes, flash headroom 6,040 bytes, linked AXI SRAM 111,356 bytes and
`SRAM4` 2,048 bytes. Each bank endpoint allocates 5,568 bytes on the heap, including
its 1,560-byte capture workspace; these dynamic allocations are not included in
the static SRAM figures. Driver-queue scratch reuse reduces the bank poll frame
from 440 to 176 bytes; direct control-configuration validation reduces its frame
from 1,272 to 848 bytes. The [stack-chain review](../build/slaaps_firmware/production-preparation-final-20261022/stack-chain-review.json)
includes the retained 160-byte workqueue runner frame: known deepest MAIN/AUX
call chains total 2,664/2,632 bytes against the 2,800-byte `hp_default` stack,
leaving only 136/168 bytes before unmeasured runtime/libc overhead. This is tight;
resolve and qualify the stack pressure before enabling outputs. These are static
estimates, not measured runtime high-water marks or timing qualification.
Final source manifests account for the shared adapter move and
deleted module-local files; the older embedded Git tag alone is not the source
identity. No board boot, electrical acceptance or hardware operation is claimed.

## October 22 previous checkpoint: activation transaction and accepted-profile persistence

The preparation coordinator now sends activation/cancellation over the separate
`slaaps_manual_transition` topic using the existing preparation identity schema.
Bank endpoints bind its advertisement lifetime, require independently supplied
trusted permission, publish exact activation replies, and revoke on exact-session
cancellation or publisher replacement. Partial activation cannot complete; timeout
or a rejected peer cancels every participating bank. Preparation readiness is
consumed before activation so the first bank's withdrawn ready reply cannot
invalidate the other bank's activation. Historical activation deadlines are not
ongoing output leases after consumption; live worker/bank dispatch still owns
health, command expiry and handling acknowledgments.

The supervisor now restores an unchanged accepted configuration on fresh disarmed
startup. Four native parameters retain fingerprint words, validation scope and
accepted apply counter. Restoration revalidates the whole typed configuration;
unapplied edits and incomplete markers fail closed. Armed startup cannot defer
restoration until later disarm. Explicit apply is still required for later edits.
These markers record accepted software configuration, not calibration or vehicle
qualification. Ordinary PX4 autosave supplies durability; the marker write is not
an immediate media flush. Export/restore must include all SLAAPS parameters.
Native metadata now contains 179 parameters (173 vehicle fields, two apply/scope
controls, four acceptance markers). Stock QGC parameter support is implemented;
a custom graphical configuration editor and actual GUI acceptance are not.

Fresh native build `/var/tmp/slaaps-native-4y_gqklw`, runtime
`/var/tmp/slaaps-native-runtime-w29p1az1`: 2,803 control checks, 34 selected-input
checks and seven supervisor lifecycles pass. Six new activation scenarios cover
both-bank completion, partial cancellation, missing reply timeout, transition
publisher replacement, cancellation after proposal expiry, and independent AUX
permission denial after MAIN activation. Profile checks cover three accepted
restarts, persisted unapplied edits, incomplete acceptance markers and actual
PX4 parameter BSON save/load. This is module restart plus storage export/import,
not electrical power-loss qualification. Native ELF SHA256:
`6e4358702ef08f07f52f12e2e718f71dcc93600fcf8cc7640a00674efe2c1309`.
The first native compile caught mismatched generated topic names; its unsealed
snapshot was explicitly refreshed and the incremental build/runtime pass is final.

Final host package `/var/tmp/slaaps-configuration-z4au8guu` passes 2,066 checks
each in Debug, Release and ASan/UBSan, with all input hashes checked after the run.
Current board stage [activation-profile-final-20261022](../build/slaaps_firmware/activation-profile-final-20261022/independent-verification.json)
passes ELF/package/source/capacity verification: 1,944,928-byte image, 21,152 bytes
flash headroom, 111,356 bytes linked AXI SRAM (runtime heap/stack not qualified).
This is +1,560 bytes flash versus the prior active-loop-headroom image.
Package SHA256 `fcb9d84117804e14e11120c9a604d55c2ad18d5143733976ea07a1bb068e812d`.
The incremental source manifest derives from the retained `472a7d6786` build
and explicitly overlays current files; the embedded tag retains that parent
identity. The dirty build manifest, not that old tag alone, identifies sources.
Volume `px4-slaaps-activation-profile-final-20261022` and preliminary stage are
retained. Transaction endpoint headers are not yet instantiated by production
drivers; this resource result does not predict their eventual integrated cost.
All validation jobs are terminal; no board boot or hardware acceptance is claimed.

Production admission and physical driver attachment remain absent. The native
transaction scenarios use synthetic permission and memory-backed uORB, not
physical drivers. Final handling must attach at MixingOutput's serialized write
boundary, with coherent configuration/inventory, independent trusted permission,
physical driver token comparison and actual completion acknowledgment. No mode
availability flag was enabled. Manual flight, acceleration, scheduled airflow
control and the expanded EDF commissioning path remain incomplete. No hardware
was flashed, energized or operated.

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

## Activation reply wire contract (2026-10-20)

`SlaapsManualActivation.msg` defines separate `slaaps_manual_active_main` and
`slaaps_manual_active_aux` topics. `ManualActivationTransport.hpp` encodes and
validates exact transaction metadata and original activation/first-command expiry.
Publication time cannot renew either timestamp. Codec rejection preserves the
caller's output. Receiving bank role is supplied independently, and the payload's
command/handled-reply revisions do not authenticate this activation publisher.
The existing bound subscription must check the actual activation-topic lifetime;
the activation barrier must separately check transaction identity and time budget.

Host `/var/tmp/slaaps-configuration-i5fq2ify` passes 2,066 checks per build and
175 parameters; all 97 input hashes match. Native `/var/tmp/slaaps-native-lxes_0o5`,
runtime `/var/tmp/slaaps-native-runtime-q2m41or6`, passes 2,751 control checks,
34 input checks and four supervisor lifecycles. Four changed native inputs match
the sealed snapshot. Twenty shared checks cover wire faults and original expiry;
ten native checks cover generated topic delivery, wrong-bank interpretation and
latched publisher-replacement rejection for both banks.

These topics/codecs are not yet published by production bank endpoints or consumed
by production supervisor admission. Request/cancellation transport, lifetime-bound
completion collection and driver attachment remain. No flight availability or
physical outputs were enabled, and full-board resource evidence is still older.
