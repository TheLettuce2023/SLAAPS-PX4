# Manual servo bench port on the working Pixhawk baseline

2026-10-19 | Codex | Recovery step 3 software qualification

The manual bench controller is ported onto the complete PX4/NuttX baseline that
passed the stock three-cold-start check. Fresh portable, stock compatibility and
native capture tests pass, and a new Pixhawk 6X image is staged. **The user now
reports that this bench image passed the cold-boot tests and that
`research_bench_control status` reports not running, as intended.** Continue
with disconnected G3 input verification. This startup report does not establish
output routing, PWM behavior or powered servo acceptance.

## Candidate and source identity

Use the [new bench firmware](../build/bench_commissioning/bench-e8ea98b404-step3/px4_fmu-v6x_default_bench_e8ea98b404_nuttx11_dirty_20261019T225405Z.px4).
The persistent project directory is:

```text
build/bench_commissioning/bench-e8ea98b404-step3/
```

| Item | Value |
| --- | --- |
| Target / board ID | `px4_fmu-v6x_default` / **53** |
| PX4 base | `e8ea98b404baf50623a519ac67ec1c03aa240859` |
| NuttX | **11.0.0**, `49f3b29c92d7af6feb94747d4a11f348292cc41b` |
| NuttX apps | `e37940d8535f603a16b8f6f21c21edaf584218aa` |
| Source state | Base plus explicit bench port and compatibility backports; **dirty** |
| ARM compiler | GNU GCC 13.2.1, same executable hash as the verified stock reference |
| Package size | 1,854,271 bytes |
| Decoded image / slot | 1,957,752 / 1,966,080 bytes |
| Flash headroom | **8,328 bytes** |
| Package SHA256 | `a45cf99374fa7ccfd4f6349ca9a6341bacfcfa6199254f71ab2b04f0cb2aed31` |

The [ARM receipt](../build/bench_commissioning/bench-e8ea98b404-step3/candidate-receipt.json)
pins the archive, overlay, compiler, eleven submodules, Micro-CDR and four linked
artifacts. The [source manifest](../build/bench_commissioning/bench-e8ea98b404-step3/port-source-manifest.json)
binds each mapped production file. The
[complete production patch](../build/bench_commissioning/bench-e8ea98b404-step3/production-port-e8ea.patch)
was applied to a fresh base-file view and reproduced every mapped file by hash.
The materialized `ported-source/` directory beside it supports direct review.

The active `slaaps-sitl` checkout remains on `bde4aea321`; it was not rebased or
switched. The actual port is retained in the isolated native source and Docker
volume. Build identification requires this overlay and artifact receipt in
addition to the PX4 base hash. The withdrawn H10 image and failed newer stock
comparison remain preserved.

## Ported behavior and compatibility changes

The bench library, module, output provider and H10 clock-ordering fixes are
retained. Both the bench owner and stock manual selector capture input before
reading the evaluation clock. Genuine future-time, stale input, source identity,
centering, lease expiry, fault latching, MAIN-only routing and stock safety checks
remain enforced. Throttle remains unused by the servo bench.

Three upstream dependencies were carried onto the older base:

- Mixer center validation uses each channel's configured minimum/maximum. This
  is the two-line upstream fix already present in H10's original base.
- `ManualControlSetpoint` version 1 carries sender system/component IDs; the
  version 0 definition is retained. Changing the layout requires the version
  increment. External consumers of the versioned uORB/DDS interface must use
  compatible definitions; external DDS operation is not qualified here.
- The older MAVLink receiver assigns those two IDs from the received packet.
  Its existing target and link-instance handling remain in place. MAVLink
  `MANUAL_CONTROL` on the wire is unchanged and is exercised by loopback tests.

The older `MovingDiff`, `AlphaFilter`, work-queue implementation, kernel and apps
remain at the working base. The research module is selected only in the private
ARM board view. No ROMFS startup script, automatic module start, default profile,
output assignment or automatic parameter write was added. Its CLI starts the
owner disabled and still requires explicit configuration and enablement.

## Fresh software qualification

The [qualification receipt](../build/bench_commissioning/bench-e8ea98b404-step3/qualification-receipt.json)
and [independent artifact check](../build/bench_commissioning/bench-e8ea98b404-step3/artifact-validation.json)
bind the final software results to this exact candidate.

| Check | Fresh result |
| --- | --- |
| Portable Debug | 9 executables, 2,431 checks, pass |
| Portable Release | 9 executables, 2,431 checks, pass |
| Portable ASan/UBSan with leak detection | 9 executables, 2,431 checks, pass |
| Stock compatibility | ManualControl 11, selector 13, MovingDiff 4: 28 GTests across 3 executables, pass |
| Physical-acceptance checker | 19 tests, pass |
| Native capture integration | 20 scenarios, 3,632 checks per run; 3 repetitions pass using the same binary |
| Disabled-startup lifecycle | Missing profile rejected, explicit start disabled, unconfigured enable revoked, stop succeeds |
| ARM linkage | Bench owner/lease, NSH, board initialization, CDC/ACM and MAVLink present; private fixture absent |

The native scenarios retain all 19 H10 cases and add a 91-check center-range
case. That case covers valid synthetic centers of 650/2350 us and invalid centers
of 400/2600 us for both bench and stock servo functions, including endpoints and
interpolation. These values are test fixtures, not an installed servo profile.

Native tests exercise the actual selector, MAVLink receiver, bench work item and
mixer. Outputs terminate in memory; Commander and physical output drivers are
not started. The private native configuration excludes unused Gazebo modules.
This is capture integration, not a physics simulation, hardware timing bound,
full-PX4 sanitizer run or stabilized-control acceptance.

Initial missing-sender-field and protected-test-access compile failures remain
in the logs alongside the final passing builds. The first overlay/source binding
and incomplete patch-packaging attempt are retained. Portable production source
did not change after its tests; the ARM image excludes the private fixture.

## Retention and verification

The isolated native root is `/var/tmp/research-bench-step3-e8ea`, including its
`px4-source/` tree and tested executables. The staged `native-tests/` copy and
`native-evidence-manifest.json` retain logs, three result sets, scripts, synthetic
profiles, build configuration and binaries. ARM source is at
`/work/px4-bench-e8ea` in Docker volume `px4-bench-e8ea-step3-20261019`.
Retain `.agents/worktrees/bench-e8ea-step3` metadata while using these sources.
A temporary firmware copy is also under WSL `/tmp/px4-firmware/`.

The workflow is implemented in
`Tools/research_sitl/verify_bench_rollback.py` and
`Tools/research_sitl/bench_rollback/`. Existing preparation and result receipts
are sealed against accidental overwrite. The retained
[build recipe](../build/bench_commissioning/bench-e8ea98b404-step3/build-recipe.json)
records the environment, mounts and command arrays; logs retain the actual build
results. Preserve the existing evidence directories when rerunning.
To recheck retained source and suite bindings without operating hardware:

```sh
python3 -B Tools/research_sitl/verify_bench_rollback.py verify
```

The artifact validator checks the staged hashes and payload independently. Its
original output is already saved; it refuses to overwrite that evidence.

## Reported hardware startup and next operation

The [user-reported startup result](../build/bench_commissioning/bench-e8ea98b404-step3/hardware-boot-report-20261019.json)
records: "The cold boot tests passed. Status of research_bench_control reports
not running." This refers to the three-cycle check requested below. Exact
per-cycle timings and a fresh console transcript were not supplied with that
initial report. The earlier
stock console is not substituted for this changed image's runtime evidence.
Build-time receipts remain unchanged; their unverified-hardware fields describe
the state when the software artifacts were sealed.

The flash/cold-boot sequence is retained for reproducibility; do not repeat the
completed cycles solely to progress. Capture the remaining G2 console/PX4IO and
configuration evidence alongside the first G3 input check, with loads disconnected.

1. Keep servo and EDF power disconnected. Retain the verified
   [stock reference firmware](MANUAL_BENCH_STOCK_ROLLBACK_REFERENCE.md) for rollback.
   The intentional stock parameter reset remains the accepted starting point;
   no restoration of preflash settings is required for this recovery run.
2. Flash only the exact `.px4` linked above using QGC custom firmware. Save the
   upload/verification log. Codex has not performed this operation.
3. Perform three full power-off/reconnect cycles, checking QGC connection within
   30 seconds and a stable connection for at least 60 seconds per cycle.
4. Save `ver all`, `dmesg`, `mavlink status`, `param show SYS_AUTOSTART` and
   `param show SYS_USB_AUTO`. Expect the base PX4 hash and NuttX 11/hash above,
   normal sensor/PX4IO/USB startup and the intended starting settings. Record the
   new build datetime. Stock and port share base hashes, so identify the image
   using its exact file/hash and the additional module check below.
5. Run **`research_bench_control status`**. The command must exist and report the
   module is not running. Do not start or enable it during this cold-boot check.
   A missing command or unexpected running owner fails the check.
6. After successful startup verification, continue G3-G4 in the
   [commissioning procedure](MANUAL_BENCH_FLASH_AND_TEST_PROCEDURE.md): actual
   selected input/identity, explicit electrical test configuration and measured
   disconnected PWM/fault behavior. Installed servo calibration and a reviewed
   profile remain necessary before powered tests. Stabilized ARM integration
   remains separate development.

The subsequent input console and next checks are assessed below. Complete
routing, profile and prearm checks before enabling the owner. The reported
cold boots grant no physical-output acceptance.

## USB input and PX4IO observation

The [structured runtime assessment](../build/bench_commissioning/bench-e8ea98b404-step3/hardware-usb-input-assessment-20261019.json)
retains the supplied observations and source-review bindings separately from the
earlier startup report. `ver all` matches PX4/NuttX, board GUID and the staged
BIN's October 19 22:51:17 build strings. This is runtime identity consistency,
not an independent readback of every installed firmware byte. The requested
`COM_RC_IN_MODE=1` and `MAN_ARM_GESTURE=0` read back saved. All eight selected
manual samples are valid with `data_source=4`, hence USB MAVLink instance **2**,
and sender **255:190**. Center samples are near zero; observed sample ages are
about 2.1..40.7 ms. This establishes neither continuous cadence nor axis/sign
acceptance: moving samples combine several axes and have no pose labels.

PX4IO responds with initialization OK and reports all MAIN functions and PWM
values zero. In this baseline, the mixer sends a final disabling sample then
stops direct PWM writes when every function is disabled. IO clears its FMU-data
OK flag after 500 ms without those writes, making `status_fmu_ok=false`
consistent with this configuration. Raw-PWM mode can keep IO's PWM hardware
enabled while FMU arming is false, so `status_outputs_armed=true` alone does
not show that Commander armed the vehicle. The subsequent actual `actuator_armed`
capture confirms disarmed state, as recorded below. Reported zeros do not replace
physical pin measurements.
The 400 Hz rates and 1000 us disarmed/failsafe values are not the required bench
configuration; review and read back the G3 settings before output tests. A false
FMU-data flag with configured active outputs requires investigation.

The USB snapshot reports 5.6% RX loss and cumulative 10,455 missed / 1,854 received
messages. The receiver counts sequence gaps per sender; these counters alone
cannot establish joystick packet loss or a faulty cable. Compare two snapshots
about 60 seconds apart on a single direct USB/QGC path before output tests.
Seven samples, including the final center, have `buttons=2048` (bit 11 set).
Identify a held control and review its QGC action; no action is identified by
that bit alone. `sticks_moving` is a thresholded change/override indicator,
not an indication that all axes are centered.

The earlier listener command omitted the required `-n` count flag. The active
procedure now uses `listener manual_control_setpoint -n 10`; one labelled held
pose can use the command without a count. Multi-sample mode redraws its display,
so a pasted final frame alone cannot establish timing. Next capture labelled
single-axis directions and all-controls-released center, actual arming state,
and the two link-status snapshots. These initial requests are followed up below.
Keep the module stopped and loads disconnected.

## Follow-up: disarmed state and loss-display defect

The [follow-up assessment](../build/bench_commissioning/bench-e8ea98b404-step3/hardware-usb-input-followup-20261019.json)
records the supplied `actuator_armed` topic: armed, prearmed and ready-to-arm
false, with kill, lockdown, termination and ESC calibration false. All nine new
manual snapshots have `buttons=0`, valid input and the same USB 2 / 255:190 source.
The released center is roll 0, pitch 0.003, yaw 0. Samples were about
3.2..42.7 ms old when observed; this still supplies no continuous gap bound.

The initially supplied order for the final roll pair indicated reversal, but
the user subsequently corrected that order to right then left and explicitly
verified **left=-1, right=+1**. The earlier order-based reversal conclusion and
calibration instruction are superseded by the correction below. Leave the
existing calibration unchanged. Expected yaw -1/+1 and manual pitch +1/-1
observations remain recorded. Same-stick orthogonal offsets in manually held
poses do not establish a firmware coupling defect or precise axis isolation.

The two USB status snapshots increase received messages from 22,638 to 23,653
and missed messages from 183,090 to 192,525: deltas 1,015 and 9,435. The actual
capture interval is unknown. Source review confirms a metric/display defect:
the receiver sets `rx_message_lost_rate = missed / received`, and the status
command prints that value directly with a percent sign. Both displayed `8.1%`
values reproduce this ratio, not a packet-loss percentage. This also qualifies
the earlier `5.6%` observation, whose raw value is retained unchanged. The
sequence-accounting growth remains unlocalized; this finding proves neither
physical loss nor a healthy transport. No firmware change was made.

MANUAL_CONTROL's displayed 27.4/18.1 Hz values use a smoothed reciprocal receive
interval, not a guarantee of cadence or maximum gap. The later telemetry captures
are assessed below. Keep the owner stopped, loads disconnected and vehicle
disarmed during preparation. Installed routing/profile, prearm behavior and all
physical gates remain open.

## Corrected roll order, finite telemetry check and next configuration

The [correction and telemetry record](../build/bench_commissioning/bench-e8ea98b404-step3/hardware-usb-input-correction-and-telemetry-20261019.json)
preserves the user's correction separately from the earlier report: the final
roll samples were right then left, and left=-1/right=+1 was verified. No roll
calibration change is needed. The earlier report remains unchanged as historical
evidence, with its roll conclusion explicitly superseded.

USB (`type=3`, observed instance 2) timestamps span 60.062797 seconds. Received
messages increase by 1,570; RX buffer overruns, parse errors and parser-drop
indicators remain zero. TX overruns stay at 391, with no growth in this window.
Sequence misses grow by 15,045; this remains an open accounting/transport finding,
and the finite parser check does not prove absence of valid-packet loss or bound
worst-case input gaps. It supports proceeding with disconnected configuration
preparation, not accepting physical outputs.

The [USB signal-only profile](../build/bench_commissioning/bench-e8ea98b404-step3/usb-signal-only-profile.txt)
has 44 numeric tokens and SHA256
`1bf090fc08a058d00be6fb0f80f70a32f218c3df17695059faa6381524ea3c7f`.
Only the qualified native fixture's MAVLink instance token changes from 0 to 2;
sender 255:190 and all synthetic calibration/mix/timing values stay unchanged.
It is for disconnected electrical tests only, not installed servo calibration.
The [native ready-only check](../build/bench_commissioning/bench-e8ea98b404-step3/usb-profile-ready-validation/profile-validation.json)
uses the same qualified binary, parses the exact file, observes Ready and stops.
No enable, Commander or physical driver is used. Two rejected mounted-path
attempts are retained; a byte-identical copy at a whitespace-free native path
passes. No native process remains running.

The subsequent actual configuration is recorded below for the next signal-only
setup before installation/prearm and measured G4 outputs.

## Output readback and ordered setup plan

The [initial parameters and unapplied plan](../build/bench_commissioning/bench-e8ea98b404-step3/hardware-output-configuration-plan-20261019.json)
record MAIN timers 0..2 at 400 Hz, MAIN1..8 functions disabled, AUX4 function 101
(Motor 1), other AUX1..9 functions zero and COM_PREARM_MODE=0. This is not ready
for the bench routing contract. AUX9 is advertised; the finite commissioning
record verifies it off in addition to the module's AUX1..8 guards.

The [ordered console commands](../build/bench_commissioning/bench-e8ea98b404-step3/usb-signal-only-setup.txt)
contain 30 parameter writes and the immediate AUX4 readback. They first disable
Motor 1 on AUX4, then set MAIN1..4 disarmed/failsafe=0, transport min/max=500/2500
and the synthetic profile's centers=1500. They set MAIN reversal=0, all three
actual MAIN timer groups to 50 Hz, MAIN1..4 functions to 10000..10003 and prearm
mode to 1 (safety button). Existing MAIN5..8 and other AUX functions stay disabled.
The exact older-base metadata supports 50 Hz and requires reboot for timer changes.
No profile start, enable, arm or reboot command is included in the file.

Keep all servo/EDF signal connections and energy disconnected and the owner
stopped. Apply one line at a time, confirm AUX4=0 before continuing, and stop on
any error or clamp. After autosave completes, reboot and retain the
[post-reboot readback](../build/bench_commissioning/bench-e8ea98b404-step3/usb-signal-only-readback.txt).
The detailed procedure defines expected values, including actual PX4IO channel
limits and armed=false. No parameter was changed on hardware by Codex. The user
has now supplied application and Ready-only profile evidence below; physical-
output acceptance remains open.

## Applied FMU setup and retained IO failsafe table

The [post-setup console assessment](../build/bench_commissioning/bench-e8ea98b404-step3/hardware-output-configuration-result-20261019.json)
matches the requested MAIN functions/rates/centers/limits and FMU disarmed/failsafe
values. AUX1..9 are zero, REV=0 and COM_PREARM_MODE=1. The owner remains stopped;
actual armed=false/prearmed=true with kill/lockdown/termination/calibration=false.
PX4IO init/data OK are true and present reported PWM is zero. This verifies the
FMU configuration subset, not all fault behavior.

Actual IO `pwm_failsafe` first four entries are **1000**, although the FMU mixer
shows failsafe 0. The retained baseline `registers.c` ignores zero writes on the
failsafe page, leaving previous values; its disarmed page accepts zero. The
owner's routing guard checks FMU parameters, not the cached IO table. Parameter
readback alone therefore cannot establish the intended IO fallback.

The [user clarification and cold-check plan](../build/bench_commissioning/bench-e8ea98b404-step3/hardware-io-failsafe-followup-20261019.json)
confirm a console reboot and no measurement instrument currently available (a
scope may be available later). Source initialization sets the IO failsafe array
to zero. A full all-source power removal is the next diagnostic because IO may
retain its table while the FMU alone restarts. A zero result is not assumed:
capture the [cold readback](../build/bench_commissioning/bench-e8ea98b404-step3/io-failsafe-cold-readback.txt)
and verify the actual first four table entries. If nonzero persists, investigate
cold-start initialization/firmware behavior before enable. Keep all servo/EDF
energy disconnected and owner stopped. No firmware change or new flash is made.
The subsequent current-table result is recorded below. Ready-only profile
preparation is independent; enable and physical-output tests still require
calibrated signal capture and their own timing/fault acceptance.

## Cold IO table passes; hardware Ready-only profile check passes

The [cold-check response](../build/bench_commissioning/bench-e8ea98b404-step3/hardware-io-failsafe-cold-result-20261019.json)
shows MAIN_FAIL1..4 saved as zero and actual IO first-four failsafe/disarmed
entries zero. Unused channels retain 1000 defaults. MAIN functions/limits/centers
persist, IO init/data OK are true, owner stopped and actual armed=false/prearmed=true
with fault flags clear. IO and permission snapshots have their own timestamps;
no simultaneous capture or pin measurement is claimed.

The current mismatch is cleared in this observation, consistent with retained
IO state during the earlier console reboot. The firmware's zero-write-ignore
semantics remain unchanged. Recheck the actual table after future configuration
or reset changes; this is not qualification of the physical fault response.

The [console creation and Ready-only recipe](../build/bench_commissioning/bench-e8ea98b404-step3/usb-profile-create-ready-console.txt)
in the main procedure creates the named signal fixture, prints it for
comparison, starts without enable, records state/permission/IO, then stops.
The [preparation record](../build/bench_commissioning/bench-e8ea98b404-step3/usb-profile-create-ready-plan.json)
binds the recipe to the 44-token USB2/255:190 profile. Local echo payloads reproduce
its exact hash; pinned NSH sources and linked mkdir/echo/cat support were checked.
That preparation record retains its local-only scope and remains unchanged.

The later [hardware Ready-only result](../build/bench_commissioning/bench-e8ea98b404-step3/hardware-usb-profile-ready-result-20261019.json)
records the user's completed recipe. The complete five-line/44-token `cat` output
matches the fixture, start reports state0/Ready with no physical acceptance, both
actual permission snapshots are armed=false/prearmed=true with fault flags clear,
and IO reports all present PWM zero and first-four disarmed/failsafe zeros. MAIN
functions/limits/centers persist and IO init/data OK are true. Stop reports not
running. This passes profile-content/parse and the hardware Ready/start/stop
subset; no enable command or pin measurement was performed in the transcript.

The normalized displayed-text hash is not an independent hash of the board file.
Host/card readback hash or verified transfer evidence is still needed before
active acceptance. Keep the module stopped and all actuator connections/energy
disconnected. At that checkpoint, G4 captures and enable still awaited a scope.
The later result below supersedes that equipment/testing status.

## Joystick signal tests confirmed and R09 is next

The [later user report](../build/bench_commissioning/bench-e8ea98b404-step3/hardware-joystick-signal-report-20261020.json)
confirms R01..R08: readbacks/profile/input/prearm, Active centered1500 us/50 Hz
measured on an oscilloscope, signed/intermediate axis commands and normal
disable/stop/loss/recovery/closeout. Raw captures and numerical timing values were
not supplied in that reply; retain this as the user's reported result, without
claiming independent review of the whole G4 fault matrix. Earlier receipts remain
unchanged and the referenced run-sheet version is preserved with the report.

The servo model is EMAX ES3059MD; the user reports 4.8..6.0 V, maximum supply
capacity12 A, no independent PWM tester and an available physical power switch.
The [R09/R10 procedure](MANUAL_BENCH_ES3059MD_R09_R10.md) prepares a separate
1450..1550 us, 5 us/s normal-slew fixture for one unloaded servo, with scope and
angle/current/voltage measurements. This is a bounded alternative characterization
source, not installed calibration or independent controller validation. Its
existing-native Ready-only parse check passes; the original signal profile and
ARM image are unchanged. R09 measurements and the measured R10 profile remain
pending; no powered test has been performed by Codex.

The [new calibration-profile console](../build/bench_commissioning/bench-e8ea98b404-step3/hardware-es3059md-active-neutral-report-20261020.json)
now confirms file-content match, Ready then Active, actual disarmed/prearmed,
USB2/255:190 centered input and enabled neutral controls with IO1500 us. The user
reports no servo motion. No held-deflection sample or new-profile physical ramp
capture is supplied. Next trace a power-off roll+1 held for12 seconds through
input, owner, IO and physical pin before changing wiring or configuration.
IO senses about6 V; actual servo-connector voltage/polarity remains to be measured.
R09/R10 remain open; latest supplied owner state is Active, not a final stopped
state. Firmware, parameters and prior receipts are unchanged.

The [held-roll follow-up](../build/bench_commissioning/bench-e8ea98b404-step3/hardware-es3059md-revoked-report-20261020.json)
confirms valid roll+1 but state2/revoked and IO1500us. Neutral is the intended
latched response, not evidence that the joystick failed. Exact revocation cause
is not known; pitch+/-0.082 while deflected is not itself an Active-range fault.
Next reset while centered with servo energy off, confirm state1 before movement,
observe30 seconds centered and12 seconds held roll, retaining status/command.
No parameter, source or firmware change is made.

The [centered-reset result](../build/bench_commissioning/bench-e8ea98b404-step3/hardware-es3059md-reset-command-report-20261020.json)
now confirms state1 and fresh changing enabled frames in one session, latest
47.36 seconds after its start. The requested pulse range calculated from those
frames is1498.61..1507.70 us. No held full-axis/current pin result accompanies
them, so the next check is a steady12-second full-roll input-to-pin capture with
servo energy off. The old revoked condition is cleared in this observed session;
its original trigger and actual powered servo response remain unestablished.

The [subsequent held-positive capture](../build/bench_commissioning/bench-e8ea98b404-step3/hardware-es3059md-held-pwm-report-20261020.json)
verifies roll+1/pitch+0.13 from255:190, same-session enabled controls+0.05,
Active state1, IO1550 us on MAIN1..4 and user-measured1550 us on MAIN1. Positive
input-to-pin delivery works for this point. Next center to measured1500 us,
disable/stop and check one unloaded servo's actual connector signal, ground,
polarity and measured supply before the documented powered characterization.
No powered motion, angle/current data or R09/R10 acceptance is inferred; firmware
and parameters remain unchanged.

The [operator subsequently resolves the no-motion concern](../build/bench_commissioning/bench-e8ea98b404-step3/hardware-es3059md-motion-resolution-report-20261020.json):
travel was too small to notice by eye. R09 now proceeds to physical measurement
with the same1450..1550 us fixture, a light pointer/fixed angle reference and one
labeled servo at a time. Collect actual PWM/angle, both approaches, repeatability,
voltage/current and slow response; no numerical calibration data accompanies the
resolution report. A proposed5-second settled dwell/three-cycle collection
sequence is added to the procedure. R09/R10 remain open; no wider profile,
firmware or parameter change is made.

The operator explicitly requests a wider profile because the narrow fixture's
travel is insufficient for practical observation. The separate
[wider R09 procedure](MANUAL_BENCH_ES3059MD_WIDE_R09.md) stages
es3059md-unloaded-wide-r09.txt at1250/1500/1750 us with50 us/s normal slew.
It retains USB2/255:190, all1 common R/P/Y mix and1500 us neutral; no flash or
parameter change is needed. Existing-native Ready-only parse/start/stop passes
without enable/Commander/drivers/build. The preparation receipt binds that log,
profile/recipe, old profile and prior operator evidence. This user-requested
preparation supersedes the provisional narrow-measurement prerequisite.
Next scope-verify the wider profile with servo energy off, then one servo with
linkage detached, beginning at+/-0.24 roll and increasing excursion in stages.
No new physical waveform, angle/current data, install or R09/R10 pass is claimed.

Latest operator closeout: **R09 PASS; R10 PASS**. The operator reports all servos
closely matched manufacturer documentation and explicitly requests both results
marked passed. The [operator report](MANUAL_BENCH_R09_R10_OPERATOR_REPORT_20261020.md)
and structured hardware-r09-r10-operator-pass-20261020.json retain that statement,
prior context and pre-update procedure snapshots. R01..R10 now have operator-
reported passes for the stated manual servo bench scope. No numerical angle/
PWM/power/timing readings, exact tested profile/hash or load/linkage state were
provided with this closeout. No data, calibration profile or separate G4/wind/
stabilized hardware result is fabricated. Firmware, parameters, profiles and
prior diagnostic/software receipts remain unchanged; the blank form remains a
future-run template.
