# SLAAPS manual servo bench commissioning and stabilized control procedure

2026-10-13, updated 2026-10-20 | Codex | Pixhawk 6X, `px4_fmu-v6x_default`

Current hardware disposition: the original H10 candidate remains withdrawn after
failed startup. Its [startup diagnosis](MANUAL_BENCH_H10_BOOT_DIAGNOSIS.md) and
software evidence remain preserved. A new working-baseline bench port has fresh
software qualification and is now the G1 candidate. The user reports its cold-boot
tests passed and the bench module is available but not running, as intended.

Recovery step 2's clean stock rebuild of the working rollback baseline now has
matching runtime identity and three user-confirmed successful cold starts. Its
[reference-build and cold-boot record](MANUAL_BENCH_STOCK_ROLLBACK_REFERENCE.md)
records the deliberate reset to stock settings, accepted without a preflash
parameter backup. The [step 3 bench port](MANUAL_BENCH_WORKING_BASE_PORT.md) now
passes fresh qualification on that complete baseline. The user now confirms
R01..R08, including oscilloscope-verified 1500 us/50 Hz, signed/intermediate
joystick signals and stop/loss/recovery. The latest
[R09/R10 operator report](MANUAL_BENCH_R09_R10_OPERATOR_REPORT_20261020.md) records **R09 PASS** and **R10 PASS**:
all servos closely matched manufacturer documentation, according to the operator.
Numerical readings, raw captures, exact tested profile and load configuration
were not supplied with that acceptance. The remaining G4 fault matrix is separate.

Commission the existing servo-only manual controller through firmware identification,
USB flashing, disconnected-output measurements, restrained servo tests and staged
wind tests. Use the resulting calibration and load evidence to develop a separate
vehicle-supported stabilized manual controller: body-rate commands while sticks
are displaced, with full quaternion attitude hold when all three axes center.

The bench firmware commands surface positions and ignores throttle. Stabilization
requires the additional software and plant acceptance steps in gates G8-G10.
Selecting a stock PX4 flight mode in QGroundControl does not implement the SLAAPS
controller or its canard allocation.

## Gate sequence and present status

Complete each gate, archive its evidence and record the reviewer before proceeding
to its dependent gate. A failed or unmeasured requirement remains open. The existing
[physical acceptance contract](MANUAL_BENCH_PHYSICAL_ACCEPTANCE.md) requires
separate authorization for flashing, powered tests and wind tests. Writing this
procedure and preparing a candidate do not perform those operations.

| Gate | Result needed to proceed | Status on 2026-10-20 |
| --- | --- | --- |
| G0 | Named operators, installation record and numerical test limits | Open |
| G1 | Exact bench firmware and retained software evidence | Working-baseline port freshly qualified and staged; original H10 remains withdrawn |
| G2 | USB flash verified on the identified controller, all loads disconnected | Bench cold boots/stopped module user-confirmed; fresh runtime identity and PX4IO captured; boot/configuration evidence remains open |
| G3 | Actual selected QGC input, output routing and prearm behavior verified | R01..R08 user-confirmed for the signal fixture, including Active and normal stop/loss/recovery; measured installed profile still pending |
| G4 | Signal-only normal and failure captures reviewed | Center 1500 us/50 Hz, signed/intermediate axes and R06/R07 user-confirmed; remaining fault matrix and numerical timing review stay open |
| G5 | Measured servo calibration and reviewed installed profile | R09 **PASS**, operator-reported: all servos closely matched manufacturer documentation; numerical calibration and exact tested profile not supplied |
| G6 | Restrained, powered no-wind manual bench operation accepted | R10 **PASS**, operator-reported; exact load/envelope and raw captures not supplied |
| G7 | Manual bench accepted within a finite measured wind/load envelope | Depends on G6 |
| G8 | Vehicle plant and stabilized allocation/tuning supported by measurements | Missing vehicle inputs |
| G9 | Dedicated stabilized hardware firmware and output ownership verified | Output-disabled observer builds/tests pass; active output integration and hardware qualification remain open |
| G10 | Restrained stabilized rate/hold operation accepted in its measured envelope | Depends on G8-G9 |

The current user-requested [wider unloaded R09 fixture](MANUAL_BENCH_ES3059MD_WIDE_R09.md)
uses1250..1750 us and50 us/s, with an optional high-rate step file. Its retained
preparation evidence is native Ready-only. The later R09/R10 operator passes do
not identify which prepared file was used; preserve that distinction. No firmware
or parameter change is made by recording the acceptance.

G2-G4 can use a clearly labelled electrical test profile while servos and EDF are
disconnected. G5 can use an independent calibrated PWM source to establish the
installed measurements needed by the PX4 profile; it does not require guessing
calibration to enable the PX4 controller. G8 may proceed alongside G5-G7 as data
becomes available. Free flight, launch from rest and EDF operation require later
test plans.

## G0 Establish the installation and test limits

1. Name the firmware operator, person at the independent stop, and test authority.
   Record UTC date/time, controller serial and board/baseboard revision, servo
   serials, wiring revision, instrument identifiers and fixture revision.
2. Identify the physical board as Pixhawk 6X/FMUV6X. This candidate has board ID
   **53**. A 6X-RT, 6C or another board requires a separately verified target.
   Photograph connector labels and confirm the installed manufacturer's pinout.
3. Label the four canards in body FRD coordinates, with +X forward, +Y right and
   +Z down. Record a positive trailing-edge-down motion arrow on each hinge.

   | Canard | Position | Bench output |
   | --- | --- | --- |
   | C1 | Right/up, +Y/-Z | MAIN1 / IO_CH1 |
   | C2 | Right/down, +Y/+Z | MAIN2 / IO_CH2 |
   | C3 | Left/down, -Y/+Z | MAIN3 / IO_CH3 |
   | C4 | Left/up, -Y/-Z | MAIN4 / IO_CH4 |

4. Draw the installed power and signal circuit. MAIN is the Pixhawk I/O PWM bank;
   AUX is the FMU PWM bank. Verify signal and ground on the actual breakout.
   Provide servo power through its approved separate distribution/BEC circuit.
   USB/controller power does not establish servo power. Verify polarity, grounding
   and absence of regulator backfeed before connection. [PX4 wiring reference](https://docs.px4.io/main/en/assembly/quick_start_pixhawk6x)
5. Electrically isolate the EDF energy supply and command connection, including
   AUX4. Photograph the isolation and measure the relevant supply. Removing a
   software function assignment alone is insufficient.
6. Review the independent stop circuit and the restrained fixture. Verify which
   rails the stop removes and how restoration is inhibited. Loss of servo power
   does not guarantee centered surfaces; restraint must tolerate free motion and
   residual wind. During signal-only tests, actuator power stays disconnected.
7. Obtain the exact installed QGC build and gamepad model. Back up controller
   parameters and SD-card contents. Obtain a known compatible rollback `.px4` and
   its hash; a parameter export is not a firmware backup.
8. Fill and review the limits below before marking the associated measurement
   passing. Specify duration, repetitions, measurement uncertainty, and the
   allowed result for each test. Use installed measurements and component limits.

   | Quantity | Required declaration |
   | --- | --- |
   | Electrical outputs | Pulse-width error, refresh period/jitter, boot glitches and stopped-pulse behavior |
   | Surface position | Angle error, neutral error, hysteresis/backlash and mechanical clearance |
   | Dynamics | Positive/negative loaded slew and permitted step size |
   | Power | Rail voltage range, peak/continuous current, temperature and allowed droop |
   | Timing | Input delivery/age, maximum owner gap, consumer delay and maximum fault-to-output response |
   | Stop | Energy-removal time, retained restraint and restart procedure |
   | Wind | Finite maximum speed, speed uncertainty, density and approved fixture/load range |
   | Stabilization | Body-rate limits, tracking/error/settling limits, disturbance and recovery requirements |

The current bench code has a 20,000 us control interval, a 500,000 us selected-input
age limit and a maximum configured deflection of 13 degrees. These are software
bounds. The installed profile can use a smaller angle envelope. The proposed
50 Hz PWM refresh and prior 5% deadzone remain choices to verify, rather than
measured hardware performance. An intended speed above 40 m/s has no finite upper
bound and cannot define a wind-test envelope.

Pass G0 when the installation record, required stage limits, EDF isolation and
stop/fixture review are signed for the next operation. Retain incomplete later
limits explicitly; they prevent those later gates from passing.

## G1 Identify and verify the firmware candidate

### Current working-baseline candidate

The [working-baseline port](MANUAL_BENCH_WORKING_BASE_PORT.md) has fresh results:
2,431 portable checks per Debug/Release/ASan-UBSan configuration, 28 stock
compatibility GTests, 19 acceptance-checker tests and three passing repetitions
of 20 native cases (3,632 checks per run). Native integration uses a memory
output sink. Physical PWM response must be measured in G4.

The retained [H10 software record](MANUAL_BENCH_CONTROL_H10_FIX.md), original
manifest and source/build trees remain historical evidence. Their withdrawn
image is not selected for this commissioning attempt. The replacement was
compiled on the verified older PX4/NuttX/apps baseline and has its own source,
artifact and qualification receipts.

The Windows staging directory, relative to the checkout, is:

```text
build/bench_commissioning/bench-e8ea98b404-step3/
  px4_fmu-v6x_default_bench_e8ea98b404_nuttx11_dirty_20261019T225405Z.px4
  candidate-receipt.json
  qualification-receipt.json
  port-source-manifest.json
  artifact-validation.json
  production-port-e8ea.patch
```

| Item | Identity |
| --- | --- |
| Target | `px4_fmu-v6x_default`, board ID 53 |
| Source base | `e8ea98b404baf50623a519ac67ec1c03aa240859`, explicit dirty port |
| Kernel/apps | NuttX 11.0.0 `49f3b29c92` / apps `e37940d85` |
| Firmware SHA-256 | `a45cf99374fa7ccfd4f6349ca9a6341bacfcfa6199254f71ab2b04f0cb2aed31` |
| Firmware package bytes | 1,854,271 |
| Retained WSL evidence | `/var/tmp/research-bench-step3-e8ea` |

From PowerShell in the checkout:

```powershell
$benchStage = Join-Path (Get-Location) 'build/bench_commissioning/bench-e8ea98b404-step3'
$benchFirmware = Join-Path $benchStage 'px4_fmu-v6x_default_bench_e8ea98b404_nuttx11_dirty_20261019T225405Z.px4'
$benchExpectedHash = 'a45cf99374fa7ccfd4f6349ca9a6341bacfcfa6199254f71ab2b04f0cb2aed31'
if ((Get-FileHash -LiteralPath $benchFirmware -Algorithm SHA256).Hash.ToLowerInvariant() -ne $benchExpectedHash) {
    throw 'Bench firmware hash mismatch'
}
$benchReceipt = Get-Content -Raw -LiteralPath (Join-Path $benchStage 'candidate-receipt.json') | ConvertFrom-Json
$benchQualification = Get-Content -Raw -LiteralPath (Join-Path $benchStage 'qualification-receipt.json') | ConvertFrom-Json
if ($benchReceipt.source_base -ne 'e8ea98b404baf50623a519ac67ec1c03aa240859' -or $benchReceipt.board_id -ne 53) {
    throw 'Unexpected source base or board'
}
if ($benchQualification.firmware_sha256 -ne $benchExpectedHash) {
    throw 'Qualification does not bind the selected firmware'
}
if ((Get-FileHash -LiteralPath (Join-Path $benchStage 'port-source-manifest.json') -Algorithm SHA256).Hash.ToLowerInvariant() -ne $benchReceipt.source_manifest_sha256) {
    throw 'Port source manifest hash mismatch'
}
$benchReceipt
```

1. Recheck `ported-source/` against `files` in `port-source-manifest.json` and the
   independent artifact check. Archive `git status`, including untracked files,
   with the commissioning record. The active newer checkout is the donor, not
   this older port's entire compiled source; a commit alone is insufficient.
2. Review the new linked-module and resource evidence. The uncompressed image
   is 1,957,752 bytes against a package limit of 1,966,080 bytes: only **8,328 bytes**
   remain. This is a package size comparison, not runtime stack or timing margin.
3. Confirm the candidate includes `research_bench_control_main`, the bench owner
   and output lease. Its private SITL test module is excluded from the ARM image.
   The bench module has no automatic startup, enable or installed profile.
4. Bind the chosen `.px4`, parameters, electrical test profile and planned hardware
   configuration to the flash review. Retain the rollback file and recovery method.

Pass G1 when the selected file and sources match the retained evidence and the
test authority accepts this exact candidate for the disconnected hardware test.
This does not pass G4 or authorize connected servo operation.

### Building after a source change

An ordinary `make px4_fmu-v6x_default` uses the checkout's board defaults, which
exclude the default-off bench module. In a new isolated Linux-native source view,
the bench build must explicitly add:

```text
CONFIG_MODULES_RESEARCH_BENCH_CONTROL=y
```

The target is confirmed by [PX4's Pixhawk 6X build instructions](https://docs.px4.io/main/en/flight_controller/pixhawk6x).
Use the project's [build workflow](../.agents/skills/build-px4/SKILL.md) for the
container/toolchain and artifact handling. The current port's ARM image was built
in the pinned PX4 Docker image; native capture tests used Ubuntu-24.04 WSL.
Do not build NuttX directly in the Windows checkout or silently
replace dirty/untracked research inputs with a clean Git checkout.

The H10 verification runner has a fixed `/var/tmp/research-bench-h10` output path.
Do not run its snapshot, prepare, build, test or manifest actions over the sealed
package. For a new increment, first give the runner and its imported H8/H9 helpers
a new output root, retain a complete source snapshot, and preserve the earlier
evidence. H10 preparation also depends on the retained H8 native source tree; it
is not a standalone checkout-only build recipe.

Before a replacement candidate advances, require scoped formatting, the strict
host/sanitizer regressions, stock compatibility tests, all native integration
cases, a fresh linked ARM build, exactly one current NuttX built-in table, and a
new artifact/source manifest. Review `.px4` board ID and size, module symbols,
stack/memory margins and any changed failure behavior. The later stabilized image
requires this same replacement-candidate process.

## G2 Flash with all actuator loads disconnected

Perform this gate after the separate flash authorization recorded under the
physical acceptance contract.

1. Disable joystick transmission. Disconnect servos, their power rail and all EDF
   connections. Remove flight/propulsion batteries and other controller supplies.
   Disconnect telemetry USB links. Use USB only for the firmware upload.
2. Open desktop QGC and select Vehicle Setup, then Firmware while the controller
   is disconnected. Connect the controller directly to the PC's USB port.
3. Select PX4, enable Advanced settings and choose the custom firmware file
   option. Select the exact staged `.px4` whose hash passed G1. Do not select a
   downloadable stock release. Verify the detected hardware before accepting.
4. Allow erase, programming and verification to finish. Keep the USB connection
   stable. Retain the upload log and the file's hash in the test record.
5. Wait for reboot and reconnection. If board detection, upload verification or
   reboot fails, stop this gate and use the documented USB recovery/rollback
   route with the same disconnected-load precautions.

These UI steps follow [QGC's firmware loading procedure](https://docs.qgroundcontrol.com/master/en/qgc-user-guide/setup_view/firmware.html);
labels can vary with the installed QGC version.

In QGC's Analyze Tools / MAVLink Console, capture:

```text
ver all
dmesg
mavlink status
px4io status
research_bench_control status
param show SYS_AUTOSTART
param show SYS_USB_AUTO
param show COM_PREARM_MODE
param show COM_RC_IN_MODE
param show PWM_MAIN_FUNC*
param show PWM_AUX_FUNC*
```

6. Complete three full power-off/reconnect cycles, checking QGC connection within
   30 seconds and at least 60 seconds of stable connection per cycle. Check board
   identity, the expected base/NuttX 11 identity, normal boot and PX4IO status.
   At this point `research_bench_control status` should report that the module is
   not running. An unknown command instead indicates the required module is absent.
7. Review retained SD startup files and airframe configuration. Remove unintended
   automatic actuator/test startup through a reviewed configuration change before
   continuing. Recheck routing after any airframe selection or reboot.
8. Export a post-flash parameter snapshot and boot log. Version strings identify
   the source base, not every dirty-tree byte. Bind installed identity to the
   hashed upload file, successful uploader verification and subsequent module tests.

Pass G2 when the correct candidate boots, PX4IO is operational, the bench module
is available but stopped, and the recorded configuration contains no unexpected
startup/output activity. Loads remain disconnected.

## G3 Verify input, profile parsing and output routing

### Select the actual QGC input

Current entry point: the user reports the bench cold boots passed and the module
is stopped. A fresh console matches the candidate's PX4/NuttX identities and
October 19 22:51:17 build strings. Valid selected USB input has `data_source=4`,
hence MAVLink instance **2**, and sender **255:190**. The two input parameters
read back saved as requested. Read the
[runtime assessment](MANUAL_BENCH_WORKING_BASE_PORT.md#usb-input-and-px4io-observation).
The next console confirms disarmed state and released buttons. The user corrected
the final roll test order to **right then left**, explicitly verifying left=-1
and right=+1. The earlier roll-reversal conclusion is superseded; leave its
calibration unchanged. Two USB telemetry samples show no reported RX parser or
buffer faults over 60.062797 seconds. Continue signal-only profile and routing
preparation below, retaining the open sequence-accounting finding.
Keep loads disconnected and the module stopped; retain remaining G2 boot and
configuration evidence without repeating the reported successful cold boots.

1. With loads disconnected, configure QGC joystick calibration and Normal mode,
   which sends `MANUAL_CONTROL`. Record axis mapping, inversion, center noise,
   axis frequency and button actions. Remove arm actions from the bench gamepad.
   [QGC joystick reference](https://docs.qgroundcontrol.com/master/en/qgc-user-guide/setup_view/joystick.html)
2. Use the reviewed bench settings below, then verify their readback. Do not arm.

   ```text
   param set COM_RC_IN_MODE 1
   param set MAN_ARM_GESTURE 0
   param set COM_PREARM_MODE 1
   ```

   In this checkout, these select MAVLink-only input, inhibit stick arming and
   make prearm depend on the safety button. Do not copy newer online safety
   parameter names into this older source revision. Keep the physical safety
   configuration intact; do not defeat it with a circuit breaker to obtain output.
3. Observe input initially over a direct link. Capture `mavlink status` and
   `listener manual_control_setpoint -n 10` while centering and moving each axis.
   Record `valid`, timestamps, `data_source`, `source_system_id` and
   `source_component_id`. `data_source` 2..7 represents MAVLink instance 0..5.
4. Repeat over the intended SiK/TELEM1 path. Observe the actual instance and IDs
   on that path and use them in its profile. TELEM1 does not prove instance zero;
   the expected GCS 255:190 identity must be measured. IDs are routing identity,
   not authentication. Other QGC connections must not provide an unintended input.
5. Confirm roll/right and yaw/positive rotation mapping. PX4's selected positive
   pitch input means forward stick/nose-down; it is opposite positive body pitch
   rotation. Record this explicitly in the mix and later rate-command mapping.
   Sweep throttle independently and verify the bench has no throttle command effect.

This firmware requires `-n` for the sample count; the earlier positional `10`
instruction printed only one snapshot. Multi-sample mode clears/redraws the
display, so a retained last frame does not establish sample rate or maximum gap.
Use plain `listener manual_control_setpoint` for one labelled held position:
center, roll-left/right, pitch-forward/back and yaw-left/right, with other axes
centered. Then release all controls and capture center again. Mixed-axis samples
do not verify precise axis independence. The initial `buttons=2048` cleared to
zero in all nine follow-up samples; retain the QGC button-action review.

The physical labels, including the user's latest roll-order correction, give:

| Physical movement | Selected response | Assessment |
| --- | --- | --- |
| Left stick up / down | throttle +0.960 / -1 | Bench ignores throttle; no full endpoint acceptance |
| Left stick left / right | yaw -1 / +1 | Expected sign |
| Right stick up / down | pitch +1 / -1 | Expected manual forward/nose-down sign |
| Right stick left / right | roll -1 / +1 | Expected sign, user corrected/verified |

The latest correction establishes the expected roll sign without recalibration.
Keep the current pilot-axis calibration. Earlier reports retain their original
physical labels and conclusions for history; the new
[correction and telemetry record](../build/bench_commissioning/bench-e8ea98b404-step3/hardware-usb-input-correction-and-telemetry-20261019.json)
supersedes that roll interpretation. Precise axis isolation, center-noise bounds
and input-gap behavior still require the applicable measured checks.
The actual `actuator_armed` capture has armed/prearmed/ready-to-arm false and
kill/lockdown/termination/calibration false. Keep the owner stopped; obtain the
reviewed prearm permission later during the output lifecycle, without force arming.

For the observed USB RX-loss counters, use one QGC instance and one direct USB
path, with other command routes inactive. Two supplied status snapshots show
1,015 additional received and 9,435 additional missed messages; exact elapsed
time was not supplied. Source review found the printed `8.1%` is actually the
missed/received ratio rounded to one decimal with a percent sign appended. It is
not a valid packet-loss percentage. The sequence-counter growth remains
unexplained; displayed MANUAL_CONTROL rates of 27.4 and 18.1 Hz are smoothed
receive-interval estimates, not a maximum-gap bound.

The subsequent `telemetry_status` captures identify USB by `type: 3` (instance 2
in these captures). Their timestamps differ by **60.062797 seconds**. RX messages
increase by 1,570; RX overruns, parse errors and parser-drop indicators remain
zero. The TX-overrun count stays at 391, so no new TX-overrun events are reported
in this window; its earlier history is unlocalized. Sequence misses increase by
15,045 and remain an open accounting/transport finding. In this receiver the
packet-drop field counts observed changes in the parser's drop counter, not
necessarily the number of lost packets. These finite observations support
disconnected configuration preparation; they prove no worst-case input gap or
physical-output behavior. Keep the finding in the G4 timing/fault review.

QGC documentation describes 5 Hz idle transmission and a default 25 Hz changing
input rate. Measure the installed build over the actual radio, including buffering
and loss. This is especially relevant to the later stabilized input deadline;
the bench's 500 ms age limit and isolated SITL's 100 ms limit are different contracts.

### Prepare the profile

The [USB signal-only profile](../build/bench_commissioning/bench-e8ea98b404-step3/usb-signal-only-profile.txt)
is now staged for the observed instance **2**, sender **255:190**. SHA256:
`1bf090fc08a058d00be6fb0f80f70a32f218c3df17695059faa6381524ea3c7f`.
It reuses the qualified synthetic 1400/1500/1600 us fixture (C2 reversed), its
mix, 10-degree model scale and synthetic slew values. These are electrical test
values, not installed actuator calibration or a vehicle allocation. All servo
and EDF energy must remain disconnected. Its 44 tokens were parsed by the same
qualified native binary, which reported Ready and stopped without an enable
request; see the [ready-only check](../build/bench_commissioning/bench-e8ea98b404-step3/usb-profile-ready-validation/profile-validation.json).
This does not establish card installation, hardware routing or output acceptance.

The user supplied the following initial configuration readback: MAIN timer groups
0..2 are 400 Hz, MAIN1..8 functions are zero, AUX4 is function 101 (Motor 1), all
other advertised AUX1..9 functions are zero, and prearm mode is zero. The exact
ordered setup below addresses these values. Its later FMU readback matches.
The initial nonzero IO failsafe table is zero on the first four channels in the
subsequent cold-check response. These commands retain the initial readback form
for comparisons:

```text
param show PWM_MAIN_TIM*
param show PWM_MAIN_FUNC*
param show PWM_AUX_FUNC*
param show COM_PREARM_MODE
```

Keep the owner stopped during this preparation. Copy/read back the profile using
the method in the lifecycle section only after its identity/scope are reviewed.

The module accepts a plain text file with exactly 44 numeric tokens: eight header
values and nine values for each canard. Whitespace and line breaks separate tokens.
Comments, labels and trailing tokens are rejected. Store it on the SD card,
for example `/fs/microsd/bench/bench-profile.txt`, and retain its host copy/hash.

| Header token | Meaning and software constraint |
| --- | --- |
| 1 | Schema version `1` |
| 2 | Observed MAVLink instance, integer 0..5 |
| 3-4 | Observed source system/component, integers 1..255 |
| 5 | Maximum owner cycle gap, integer microseconds, 20,000..500,000 |
| 6 | Raw centered-entry tolerance, finite and 0 <= value < 1 |
| 7 | Deadzone, finite and 0 <= value < 1; apply shaping once |
| 8 | Consumer lease, integer microseconds, gap <= lease <= 500,000 |

For each of C1-C4, append negative/neutral/positive PWM in microseconds, maximum
deflection in degrees, positive/negative angle slew in degrees/second, then
roll/pitch/yaw mix coefficients. PWM values must be 500..2500 and strictly
monotonic through neutral, either increasing or decreasing. Maximum deflection
must be positive and no greater than 13 degrees; both slew values must be finite
and positive. Each mix row must contain a nonzero coefficient. Neutral PWM must
be an integer for the module's routing check. Encoded pulse reversal belongs in
this calibration; the downstream MAIN reversal mask stays zero.

Before G5, use only a labelled signal-test profile with declared electrical
fixture values and the observed sender identity. It can verify pulses, parsing
and the software contract. It cannot authorize servo movement. After G5, replace
it with measured installed values and repeat affected G3-G4 checks.

### Configure only the dedicated bench outputs

Make these changes with loads disconnected and the module stopped. They are exact
prerequisites checked by `routingValid()`, rather than general PX4 defaults:

| Parameters | Required values |
| --- | --- |
| `PWM_MAIN_FUNC1` .. `PWM_MAIN_FUNC4` | 10000, 10001, 10002, 10003 respectively |
| `PWM_MAIN_FUNC5` .. `PWM_MAIN_FUNC8` | 0 |
| `PWM_AUX_FUNC1` .. `PWM_AUX_FUNC8`, plus advertised AUX9 | 0, including AUX4; the module guards 1..8, and this commissioning record also verifies AUX9 off |
| `PWM_MAIN_REV` | 0 |
| `PWM_MAIN_MIN1` .. `PWM_MAIN_MIN4` | 500 |
| `PWM_MAIN_MAX1` .. `PWM_MAIN_MAX4` | 2500 |
| `PWM_MAIN_CENT1` .. `PWM_MAIN_CENT4` | Corresponding measured or signal-fixture integer neutral PWM |
| `PWM_MAIN_DIS1` .. `PWM_MAIN_DIS4` | 0 |
| `PWM_MAIN_FAIL1` .. `PWM_MAIN_FAIL4` | 0 |

The 500/2500 parameters define the software transport mapping. The profile's
calibrated angle endpoints bound actual commands. Do not sweep a connected servo
through 500..2500 merely because these transport endpoints are configured.
QGC metadata may restrict these fields to narrower conventional ranges; use
verified console parameter writes and readback for this custom implementation,
and treat any rejection or clamp as a failed configuration step.

Configure the MAIN timer groups containing channels 1-4 to the reviewed PWM
protocol/refresh rate. The candidate is 50 Hz, meaning 20 ms period. Inspect
the installed `PWM_MAIN_TIM*` parameters and group mapping; do not assume a
`PWM_MAIN_RATE` parameter exists. Reboot for timer changes and read back every
assignment afterward. Keep AUX functions disabled and verify absence of any
EDF control signal in G4.

#### Apply the observed USB signal configuration

The [configuration plan and initial readback](../build/bench_commissioning/bench-e8ea98b404-step3/hardware-output-configuration-plan-20261019.json)
bind this step to the staged USB signal profile. The same commands are retained
in [usb-signal-only-setup.txt](../build/bench_commissioning/bench-e8ea98b404-step3/usb-signal-only-setup.txt).
The user subsequently applied them and supplied a console-reboot readback; the
FMU/mixer values match. The initial IO cached failsafe mismatch is followed by
a passing current-table check below.
The sequence is retained for reproducibility; do not repeat it to clear the IO
table. No hardware changes were made by Codex. Keep servo/EDF signal connections and power
disconnected, the vehicle disarmed and the bench module stopped.

Execute one line at a time in MAVLink Console. The first two commands are a
gate: verify AUX4 reads **0** before continuing. Stop on any parameter rejection,
missing name or clamped value. MAIN5..8 and all other AUX functions are already
zero in this readback; retain and verify those values after reboot.

```text
param set PWM_AUX_FUNC4 0
param show PWM_AUX_FUNC4
param set PWM_MAIN_DIS1 0
param set PWM_MAIN_FAIL1 0
param set PWM_MAIN_MIN1 500
param set PWM_MAIN_MAX1 2500
param set PWM_MAIN_CENT1 1500
param set PWM_MAIN_DIS2 0
param set PWM_MAIN_FAIL2 0
param set PWM_MAIN_MIN2 500
param set PWM_MAIN_MAX2 2500
param set PWM_MAIN_CENT2 1500
param set PWM_MAIN_DIS3 0
param set PWM_MAIN_FAIL3 0
param set PWM_MAIN_MIN3 500
param set PWM_MAIN_MAX3 2500
param set PWM_MAIN_CENT3 1500
param set PWM_MAIN_DIS4 0
param set PWM_MAIN_FAIL4 0
param set PWM_MAIN_MIN4 500
param set PWM_MAIN_MAX4 2500
param set PWM_MAIN_CENT4 1500
param set PWM_MAIN_REV 0
param set PWM_MAIN_TIM0 50
param set PWM_MAIN_TIM1 50
param set PWM_MAIN_TIM2 50
param set PWM_MAIN_FUNC1 10000
param set PWM_MAIN_FUNC2 10001
param set PWM_MAIN_FUNC3 10002
param set PWM_MAIN_FUNC4 10003
param set COM_PREARM_MODE 1
```

The three actual MAIN timer parameters are set to 50 Hz; unused outputs remain
disabled. The synthetic profile's centers are 1500 us. The 500/2500 transport
limits do not authorize that pulse range on installed servos. Prearm value 1
uses the safety button; leave the button alone during this configuration step.
No start, enable, arm or automatic reboot is included in the command file.

Allow parameter autosave to finish and check for persistent unsaved `*` markers
or save errors. Then run `reboot`, reconnect QGC and capture the following
[readback commands](../build/bench_commissioning/bench-e8ea98b404-step3/usb-signal-only-readback.txt):

```text
research_bench_control status
param show PWM_MAIN_TIM*
param show PWM_MAIN_FUNC*
param show PWM_AUX_FUNC*
param show PWM_MAIN_REV
param show COM_PREARM_MODE
px4io status
listener actuator_armed -n 1
```

Verify timers 0..2=50, MAIN1..4=10000..10003, MAIN5..8=0, AUX1..9=0, REV=0,
and prearm mode=1. In PX4IO's channel configuration, MAIN1..4 must show
min=500, max=2500, center=1500, disarmed=0 and failsafe=0. Independently verify
actual `px4io_status.pwm_failsafe[0..3]` is zero; the mixer's channel configuration
alone does not verify the IO register table. The module must remain
not running and actual armed must be false; prearmed follows the real safety
state and is not forced here. Capture any unexpected PWM/status activity for
review. Software readback does not establish electrical silence at the pins.
Stop this stage after the readback; profile installation, Ready-only start and
measured output enable follow their own checks below.

#### Resolve the retained IO failsafe table before enable

The [post-setup result](../build/bench_commissioning/bench-e8ea98b404-step3/hardware-output-configuration-result-20261019.json)
shows MAIN functions 10000..10003, timers 50 Hz, min/max/center 500/2500/1500 us
and FMU disarmed/failsafe 0, with all AUX functions zero. IO initialization and FMU-data
OK are true. The owner is stopped; actual armed=false, prearmed=true and kill,
lockdown, termination and calibration=false. However, actual IO `pwm_failsafe`
is 1000 on the first four channels. Its present reported PWM is zero.

The retained baseline source explicitly **ignores zero writes** to IO's failsafe
register page; disarmed zero writes are accepted. Thus the parameter/mixer
value zero can coexist with a prior IO failsafe entry of 1000. The owner checks
FMU parameters and does not detect this register mismatch. Repeating FAIL=0 is
not a method of clearing it.

The user confirms the reset was a console `reboot`, and that no scope/pulse
instrument is currently available. Read the [clarification and cold-check plan](../build/bench_commissioning/bench-e8ea98b404-step3/hardware-io-failsafe-followup-20261019.json).
IO may remain powered across an FMU restart. The source initializes its failsafe
array to zero on IO startup, so a full power removal tests retained state; it
does not guarantee the cold startup will leave the required zeros.

1. Keep all servo/EDF connections and energy disconnected, and the owner stopped.
2. Remove USB and every other controller supply/backfeed path, including any
   powered battery/BEC or telemetry connection. Allow at least five seconds for
   board power to discharge, then reconnect **USB only** and wait for QGC.
3. Capture these [cold readback commands](../build/bench_commissioning/bench-e8ea98b404-step3/io-failsafe-cold-readback.txt):

   ```text
   research_bench_control status
   param show PWM_MAIN_FAIL*
   px4io status
   listener actuator_armed -n 1
   ```

4. Verify MAIN_FAIL1..4=0 **and actual IO pwm_failsafe first four entries=0**.
   Unused channels5..8 need not have zero failsafe entries. The owner stays
   stopped and actual armed must be false; prearm follows the real safety state.
   If any first-four IO entry is nonzero, retain the cold boot/status evidence
   and investigate startup/firmware initialization before enable.

Do not enable or connect actuator energy while that mismatch remains open.
Profile copying and a disabled Ready-only parse can be prepared independently;
all active electrical tests wait for calibrated capture equipment and review of
the IO failsafe result. A zero table readback alone grants no physical acceptance.

The [subsequent cold-check result](../build/bench_commissioning/bench-e8ea98b404-step3/hardware-io-failsafe-cold-result-20261019.json)
now shows saved MAIN_FAIL1..4=0 and actual IO `pwm_failsafe=[0,0,0,0,1000,1000,1000,1000]`.
MAIN1..4's disarmed values are also zero; functions, limits and centers persist.
IO init/data OK are true, the owner is stopped and actual armed=false/prearmed=true
with fault flags clear. This passes the **current first-four register-table
check**. The zero-write-ignore behavior is unchanged; verify actual IO entries
again after future configuration/reset changes. The physical fault response and
worst-case timing still require measurements. Do not repeat the completed cold
check merely to perform the Ready-only step below.

### Current USB profile: create, inspect and check Ready only

For the complete sequence from module start through joystick signal captures,
loss/stop/recovery, required reports and later measured-profile servo movement,
use the [joystick operator run sheet](MANUAL_BENCH_JOYSTICK_RUN_SHEET.md) and its
[report form](templates/MANUAL_BENCH_JOYSTICK_TEST_REPORT.md). It keeps the current
synthetic signal fixture separate from the measured powered-servo profile and
does not close the remaining G4 fault cases.

Keep all servo/EDF connections and energy disconnected. No instrument is
currently available, so the current scope ends at profile inspection and
Ready/start/stop. Use the staged USB fixture only; its synthetic values do not
qualify installed servo calibration. The source-checked
[console recipe](../build/bench_commissioning/bench-e8ea98b404-step3/usb-profile-create-ready-console.txt)
and [recipe validation record](../build/bench_commissioning/bench-e8ea98b404-step3/usb-profile-create-ready-plan.json)
retain the exact steps. They have not been executed on the controller by Codex.

The user has now executed this recipe successfully; see the
[hardware Ready-only result](../build/bench_commissioning/bench-e8ea98b404-step3/hardware-usb-profile-ready-result-20261019.json).
The complete board readback matches all five lines/44 tokens, start reports
Ready/state0, both actual permission snapshots are armed=false/prearmed=true
with fault flags clear, and IO reports present PWM zeros and first-four
disarmed/failsafe zeros. Stop reports not running. This subset passes; retain
the commands below for reproducibility without repeating the completed check.

Execute these lines individually to create the directory and write the five
profile lines. The first `>` replaces this named signal-fixture file; later
`>>` lines append to it. `mkdir -p` preserves an existing directory.

```text
mkdir -p /fs/microsd/bench
echo "1 2 255 190 200000 0.05 0.05 500000" > /fs/microsd/bench/usb-signal-only-profile.txt
echo "1400 1500 1600 10 1000 1000 1 1 1" >> /fs/microsd/bench/usb-signal-only-profile.txt
echo "1600 1500 1400 10 1000 1000 1 -1 -1" >> /fs/microsd/bench/usb-signal-only-profile.txt
echo "1400 1500 1600 10 1000 1000 -1 1 -1" >> /fs/microsd/bench/usb-signal-only-profile.txt
echo "1400 1500 1600 10 1000 1000 -1 -1 1" >> /fs/microsd/bench/usb-signal-only-profile.txt
cat /fs/microsd/bench/usb-signal-only-profile.txt
```

Stop on any write/read error. Compare the complete `cat` output with the
[host profile](../build/bench_commissioning/bench-e8ea98b404-step3/usb-signal-only-profile.txt)
before proceeding: five lines, 44 numeric tokens, observed USB instance2 and
sender255:190. The local echo payloads reconstruct its exact bytes by hash, but
that check is not an independent hash of the board file. Retain the console
readback; obtain a host/card readback hash or verified transfer receipt before
active acceptance. No onboard checksum command is assumed.

Once the content matches, capture this disabled lifecycle:

```text
research_bench_control start /fs/microsd/bench/usb-signal-only-profile.txt
research_bench_control status
listener actuator_armed -n 1
px4io status
research_bench_control stop
research_bench_control status
```

Expect **`state=0 (0 ready, 1 active, 2 revoked)`** before stop, and **not running**
afterward. Actual armed must stay false, fault flags clear, and actual IO
first-four failsafe entries zero. Start parses/configures and advertises the
topic without enabling a session. Ready/status alone does not accept routing,
pin silence, slew, timing or fault behavior. Stop this run here; enable and G4
measurements wait for calibrated capture equipment. The full lifecycle below is
retained for that later measured stage, not an instruction to enable now.

### Exercise the module lifecycle

1. Hash and copy the profile using a card reader or an available verified file
   transfer method. Read back the card file; confirm the same bytes and 44 tokens.
2. Capture startup and permission state:

   ```text
   research_bench_control start /fs/microsd/bench/bench-profile.txt
   research_bench_control status
   listener actuator_armed -n 1
   ```

   Expected module state is `0 ready`. Start parses/configures the profile but
   does not enable an output session. Invalid profiles must reject without Active.
3. Use the installed safety button to enter the reviewed prearm state while
   keeping `armed=false`. Confirm `prearmed=true` and `lockdown`, `kill`,
   `termination`, `in_esc_calibration_mode` all false in the actual topic.
   If Commander cannot provide this state, investigate it; do not force arm or
   publish synthetic permission messages on the hardware.
4. First verify the actual IO failsafe table and signal-capture equipment checks
   above. Center the three axes and observe fresh valid input from the configured source.
   While capturing outputs, issue:

   ```text
   research_bench_control enable
   research_bench_control status
   listener bench_servo_command -n 5
   ```

   Expected module state is `1 active`, with a new session and enabled frames.
   CLI success means a request was accepted; PWM measurements establish output.
5. Record normal disable and reset behavior:

   ```text
   research_bench_control disable
   research_bench_control status
   research_bench_control stop
   research_bench_control start /fs/microsd/bench/bench-profile.txt
   research_bench_control status
   ```

   Disable must produce `2 revoked`; restarting returns Ready and requires a
   separate centered `enable`. Restored input or permission cannot resume the
   old consumer session. Do not change routing/profile parameters while enabled.

Pass G3 when actual source identity, calibration/sign convention, routing readback,
prearm state, profile parsing and the explicit lifecycle agree with the record.
Proceed to G4 with the same exact configuration and loads disconnected.

## G4 Measure disconnected PWM and failure behavior

Connect calibrated capture equipment to MAIN1-4 signal/ground on the actual I/O
breakout. Monitor EDF/AUX4 separately and retain raw waveforms. Record a common
event reference for commands and failures, instrument sample rate and uncertainty.
Console output or an `actuator_outputs` topic alone cannot replace pin measurements.

For every case, record firmware/profile/parameter hashes, permission/input state,
event time, pulse widths and periods, maximum response time, result, reviewer and
raw evidence path/hash. Repeat in the counts set at G0. Restore only by the
explicit lifecycle; perform configuration faults while stopped or in an approved
disconnected fault-injection case.

| Test | Action | Required observation |
| --- | --- | --- |
| S01 Cold boot | Reboot with previous bench parameters retained | No automatic module/session enable; record every pin transient |
| S02 Start | Start a valid profile without enable | Ready; no accepted-session command |
| S03 Bad profile | Try missing/truncated/nonfinite profile | Start rejects; no Active session or unintended waveform |
| S04 Entry offset | Try enable with one uncentered axis | Revocation, then explicit reset needed |
| S05 Centered entry | Prearm, center, enable | Fresh session; each MAIN channel matches its declared neutral |
| S06 Axis mapping | Apply each sign on each axis, then small combined commands | Correct channels/signs; bounded pulses and declared slew |
| S07 Throttle | Vary throttle with identical R/P/Y | No bench pulse change attributable to throttle; no EDF signal |
| S08 Disable/stop | Disable and separately stop from a nonneutral command | Neutral request after accepted session while prearm remains permitted; measured latency within reviewed limit |
| S09 Manual loss | Stop joystick transmission; separately remove radio link | Revoked/expired output response within approved end-to-end limit; recovery traffic cannot resume session |
| S10 Source change | Use reviewed test sender/connection with a different identity | Rejection of selected-source change or continued original source with correct attribution; no takeover; old faulted session cannot resume |
| S11 Permission loss | Remove prearm or exercise reviewed kill/lockdown cases | Stock disabled/failsafe electrical behavior; no same-session revival |
| S12 Owner stall | Use a reviewed hardware-capable injection that stalls only the owner | Independent consumer response measured while owner is stalled |
| S13 FMU/IO failure | Exercise approved FMU loss and FMU-to-IO communication-loss fixtures | Actual stopped/retained/failsafe pulse behavior captured and judged against the fault contract |
| S14 Reboot/power restoration | Reboot and restore each reviewed power path | No automatic enabled session; no unauthorized pulse/movement opportunity |
| S15 Routing exclusion | With loads disconnected, test approved remap/override negatives | Only MAIN1-4 in order can carry bench output; AUX/EDF excluded |
| S16 Recovery | Restore link, then stop/start and centered enable | No recovery before explicit new session; new session works after prerequisites return |

S11-S13 require identified, reviewed injection methods. The existing private
`bench_active_test` produces synthetic permission topics and a memory output sink;
it is not an ARM test command. It must not be copied onto the controller as a
substitute for Commander or actual I/O fault testing. There is currently no
reviewed hardware owner-stall CLI. Keep S12/S13 open until suitable tooling or
fixture methods are developed; `research_bench_control stop` tests normal stop,
not a scheduling stall or whole-FMU failure.

Separate three fault outcomes in the record: firmware neutral demand, electrical
pulse cessation/failsafe, and actual surface response. Before any accepted session
the consumer has no command and the required disarmed/failsafe pulse values are
zero. After normal disable or lease expiry, with prearm permission intact, it
requests calibrated neutral. Loss of permission uses the stock disabled/failsafe
path. Neither behavior establishes the response of an installed servo to lost
pulses or controller power.

For manual loss, measure last transmitted packet, last delivered packet, last
accepted sample, revoke/expiry and first changed pin waveform separately. A local
500 ms sample limit does not bound upstream radio buffering. For lease expiry,
bound the extra consumer scheduling/transport delay from measurement; the mixer
backup schedule is not an independent hard real-time watchdog.

Pass G4 only after all applicable normal/fault waveforms satisfy their declared
limits and the test authority accepts the resulting fault contract. An open
hazardous failure requirement prevents advancement to the powered stage it affects.

## G5 Measure servo calibration without wind

Use a separately authorized restrained calibration setup, with EDF isolated and
the independent stop proven. Prefer an independent, measured PWM source before
connecting servos to the PX4 bench output. Disconnect the PX4 signal while that
source is attached; never drive a signal wire from both devices.

For the current ES3059MD setup without an independent tester, the
[R09 alternative](MANUAL_BENCH_ES3059MD_R09_R10.md) uses the previously verified
PX4 signal as a separately bounded source: 1450..1550 us and 5 us/s normal slew,
one unloaded servo with linkage detached, independent scope/angle/power
measurements and a physical power cut. Treat this as characterization of that
servo/fixture, not independent controller verification or assembled powered
acceptance. Complete installed linkage/load/mix measurements before installed
R10, and retain the applicable G4 fault review.

1. Verify the individual servo model, allowable supply and signal rate. Remove
   load or disconnect the linkage where the approved calibration setup requires.
   Establish an approved narrow starting pulse interval and current limit from
   the servo/installation review. Do not begin with a blind full-travel sweep.
2. Connect and calibrate one servo at a time. Measure actual pulse width and actual
   surface angle with the sign arrow established in G0. Approach each point from
   both directions. Record clearance, current, backlash and uncertainty.
3. Find the pulse for physical neutral and each approved negative/positive angle
   endpoint. Use a maximum deflection no greater than 13 degrees, or a smaller
   reviewed envelope. Select an integer neutral pulse and measure the resulting
   neutral angle error; do not assume rounding preserves the required tolerance.
4. Measure positive/negative loaded step response over the approved angle range.
   Record delay, angle versus time, overshoot, rail droop and current. Derive
   command slew limits compatible with the observed response and reviewed margin.
   No-load datasheet transit time is not an installed loaded limit.
5. Verify the signed coupled mix against the actual hinge geometry and intended
   roll/pitch/yaw response. Record both axis input convention and TED direction
   for every coefficient. Electrical servo reversal cannot establish aerodynamic
   moment sign; verify that in the restrained load/flow tests.
6. Generate the installed 44-token profile from these records. Record units,
   reviewer, profile hash and allowed conditions. Keep the signal-test profile
   separately labelled. Repeat G3 parsing/readback and G4 normal captures with
   the installed profile before applying it to connected servos.

Pass G5 when all four installed calibration records and the complete profile are
reviewed. Missing loaded slew, mix signs or source identity cannot be filled with
the old ideal 175-degree/500-2500 us model or the SITL fixture coefficients.

## G6 Accept powered manual bench operation without wind

1. Verify the G4/G5 review and separate powered-test authorization. Restrain the
   airframe, clear the surface sweep areas, isolate EDF, and put the stop operator
   in position. Begin with actuator power removed and the bench module stopped.
2. Connect MAIN1-4 signal/ground and the approved servo power distribution. Check
   channel labels and polarity before energizing. Power the controller, confirm
   stopped/Ready state as appropriate, center QGC input, then apply the reviewed
   servo-power sequence. Observe startup behavior against the G4/G5 record.
3. Enter prearm without arming. Start the measured profile and enable centered.
   Compare commanded neutral, measured PWM and all four measured surface angles.
4. Apply the approved small positive/negative commands on each axis. Verify actual
   surface direction, angle, slew and current. Expand only through predeclared
   steps to the accepted profile envelope; then exercise combined commands and
   reversal. Watch for binding or a saturating servo rail.
5. Sweep throttle and confirm no change from the bench owner and no EDF activity.
6. Repeat the approved no-wind loss, normal disable/stop and explicit recovery
   tests. Measure physical neutral arrival separately from software/pulse response.
   Do not perform hazardous whole-controller fault cases powered unless their
   disconnected captures and mechanical response have been reviewed first.
7. Verify the independent stop removes its intended actuator energy. Confirm no
   automatic active recovery on restoring energy. Record any servo movement caused
   by restoration of existing neutral pulses; stop the module and follow the
   reviewed reset sequence before restoring powered test conditions.
8. Run the approved simultaneous-load/dwell test. Log voltage, current, temperature,
   angle tracking and resets. Obtain hardware `top`/`work_queue status` and available
   stack/resource diagnostics under the actual logging/radio load. Review measured
   headroom; static ELF sizes do not establish stack or deadline margin.
9. End by centering, disabling, removing actuator energy and stopping the module.
   Archive parameters/profile hashes, console/telemetry/ULog where available,
   instrument captures, angle/power records and deviations. Inspect fasteners,
   horns, wiring and connectors before the next test.

Pass G6 when directions, bounded coupled motion, loaded power/dynamics,
stop/loss/recovery behavior and runtime margins satisfy the predeclared limits
for this exact configuration. This accepts open-loop manual surface control.

## G7 Accept a finite manual wind envelope

1. Obtain the named authority's wind-test release, finite speed steps, upper bound,
   density range, load limits, dwell times and independent shutdown plan. Keep EDF
   isolated. Review fixture load capacity and residual-wind behavior.
2. Recheck G6 at zero wind before each session. Start at the lowest approved flow
   condition with centered commands. Measure actual local flow rather than using
   fan setting as airspeed.
3. At each released step, measure neutral holding, each signed single-axis command,
   selected coupled/reversal commands and loaded slew. Record hinge/load response,
   surface angles, rail voltage/current and temperature over the declared dwell.
4. Compare each result with the limits before releasing the next step. Any loss of
   holding, binding, limit exceedance, unexpected motion, overheating or controller
   reset ends the increment using the reviewed normal/independent stop sequence.
5. Perform loss/stop tests only at wind conditions already authorized for those
   fault cases. Wind decay and de-energized free surfaces must stay restrained.
6. Inspect the installation after each increment. Record the accepted finite range
   and prohibited conditions; changes to profile, servo, linkage, power, firmware
   or fixture require the affected checks to be repeated.

Pass G7 when named review accepts the measured servo-only envelope. A rigid
fixture can measure control forces and actuator performance; it cannot demonstrate
body-rate or quaternion-hold control of a freely rotating vehicle.

## G8 Build the measured plant for stabilized control

1. Accept assembly mass, CG and the full inertia tensor with units, axes, reference
   point and component scope. Resolve the current model's nonphysical inertia and
   3.291337 kg declared mass versus 3.041337 kg provisional all-up mass.
2. Measure signed canard moment effectiveness and uncertainty at declared airflow,
   angle/trim and EDF conditions. Record aerodynamic damping and installation
   asymmetry. Include trim moment and its uncertainty.
3. Use G5-G7 servo mapping, limits, delay and loaded dynamics. Add sensor mounting,
   noise/bias/timing records and selected-estimator health requirements.
4. If any stabilized test will use EDF, first establish a separate propulsion
   scope: measured thrust/lag, canard wash and their uncertainties, energy isolation,
   fixture loads, commands and independent stop. The servo bench supplies no EDF
   acceptance. A wind-only rotating fixture may keep EDF isolated.
5. Supply the data to the [SM5 allocation contract](STABILIZED_MANUAL_SM5_ALLOCATION_INPUT_CONTRACT.md)
   with source hashes, ordered canards, units and uncertainty. Screen rank, net
   trim/moment authority, actuator margins and achievable time-dependent demand.
   A full-rank static matrix alone cannot establish loaded or transient authority.
6. Agree on operational body-rate/error/settling limits, entry/exit, estimator reset,
   source-change and loss/recovery behavior. Tune a measured-condition model and
   qualify finite operating conditions before introducing gain scheduling.

Pass G8 when the plant, uncertainty, allocation, requirements and tuning are
reviewed for a specified range. The isolated fixture's 20 m/s airflow, 10 N thrust,
synthetic inertia/effectiveness and gains cannot become vehicle defaults.

## G9 Implement and verify the stabilized hardware firmware

October 19 update: the [output-disabled observer](STABILIZED_MANUAL_HARDWARE_OBSERVER.md)
now builds for Pixhawk on the working baseline and passes host/native software
tests. It publishes diagnostic candidates only and has not been flashed. Its
separate image excludes the bench owner and leaves 4,376 bytes of flash headroom.
This is partial G9 implementation; the active output contract and physical gates
below remain open.

The subsequent [portable output boundary](STABILIZED_MANUAL_OUTPUT_CONTRACT.md)
passes host fault/mapping tests and a separate ARM object compile. It implements
explicit sessions, software acknowledgments and independent lease expiry; runtime
supervision, actual driver application/revoke acknowledgments and integrated
firmware verification remain open. It does not change the staged observer image.

The later [scheduled native prototype](STABILIZED_MANUAL_SCHEDULED_OUTPUT.md)
passes 16 fresh-process loss/driver/transition cases through actual PX4 work
queues, uORB and mixer into memory capture. It adds confirmed disarm before
handoff and driver-handled acknowledgments, without production I/O integration
or electrical feedback. G9 and physical timing/acceptance remain incomplete.

The current active stabilized owner/consumer live in a private POSIX SITL integration;
the bench ARM candidate has no deployable SLAAPS stabilized mode. The existing
library supplies reusable rate PID, quaternion reference handling and allocation,
with [31-case SITL evidence](STABILIZED_MANUAL_CHECKOUT_QUALIFICATION.md).
Use it as a regression baseline while performing the following development.

| Development task | Verifiable exit condition |
| --- | --- |
| Hardware module and configuration | NuttX/Pixhawk module explicitly configured and linked; no synthetic plant defaults or automatic enable |
| Single output owner | Bench and stabilized paths cannot run concurrently or race at transition; old leases revoke and integrator/reference state reset |
| Actual selected state | Fresh attitude/rate and reviewed estimator-health inputs; reset/source/lifecycle barriers tested; general publisher identity limitations resolved or bounded by explicit policy |
| Pilot mapping | Real QGC/SiK axis convention converted to declared body-FRD rates; all-axis centering captures quaternion once and retains it; real idle cadence satisfies its reviewed deadline |
| Physical allocation | Accepted condition-dependent moment/trim profile, signed PWM calibration, slew/position limits and saturation/antiwindup behavior |
| Hardware output contract | Servo-only restrained profile initially; fresh session/lease, disable/kill/loss/reset behavior verified through actual I/O; propulsion separately gated |
| Operating state policy | Explicit bench disarmed/prearmed versus later flight arming behavior; mode entry, exit, loss and recovery reviewed without bypassing safety |
| Observability and resources | Logged rate/reference/reset/source/session/allocation/fault state; measured timing and stack headroom; image fits target flash |
| Verification | Strict host/sanitizer suites, retained synthetic SITL regressions, new measured-plant cases, integrated ARM build and disconnected hardware failure captures |

A consumer lease must retain independent expiry at the output boundary. A brief
consumer-side revoke must not be mistaken for owner readiness; status and output
acknowledgement need an explicit reviewed contract. Do not loosen deadlines simply
to accommodate an unmeasured link. Select and verify an input update strategy that
meets the stabilized requirement, including QGC's centered/idle state.

Produce a new source/configuration manifest and `.px4`, including linked symbol,
size and runtime resource review. The current bench image has 8,328 bytes of flash
headroom; the separate observer now has 4,376 bytes. Budget further additions
before assuming the active path fits. Repeat G2-G4 for the new image with loads disconnected.
Keep the original H10 candidate, bench results and synthetic SITL qualification
immutable. No flight-mode dropdown or firmware parameter completes these tasks.

Pass G9 when the separate stabilized firmware satisfies the signed hardware
contract and its disconnected output/transition/failure tests pass.

## G10 Accept restrained stabilized manual control

Use an approved fixture that permits the rotations being tested while constraining
translation and other hazardous motion. Declare allowed angles/rates, mechanical
limits, energy and disturbance loads. Qualify one axis first, then combined axes.
Keep EDF isolated unless its separate scope has passed; ensure measured finite
airflow gives the canards authority in the selected wind-only configuration.

1. Recheck mounting/frame signs and actual selected attitude/rate against independent
   fixture instrumentation. With actuators disconnected, rotate the fixture by hand
   and verify the predicted restoring command sign for each axis.
2. At a reviewed low-energy airflow condition, connect actuators and enter the new
   stabilized mode with centered input. Verify its captured quaternion and initial
   bounded outputs. Abort on reversed feedback or uncommanded acceleration.
3. Apply the smallest approved signed body-rate commands. Measure physical rate
   tracking independently of the estimator. Increase commands only through the
   reviewed sequence, including reversals and combined-axis requests.
4. Center all axes. Verify the reference equals the estimate at the actual hold-entry
   event and remains fixed afterward. Measure physical attitude error and settling
   against that reference; do not compare a later evolving estimate with the original
   capture as if it were the same instant.
5. Apply calibrated bounded disturbances, then examine hold recovery, allocation
   residual, saturation, integrator behavior, loaded servo response and power.
6. Exercise the approved source/link loss, disable, estimator-reset and session
   recovery cases. Verify old sessions remain revoked and new centered sessions
   recover only after valid selected state and permissions return.
7. Expand to additional poses and airflow/load conditions only after the previous
   condition passes. Level and body-X-up can be separate declared cases; fixture
   geometry and thrust/load support must qualify the pose before execution.
8. Archive raw command/state/physical measurements, exact firmware/profile/parameter
   identities and every failed attempt. Obtain named acceptance for the finite
   rate/hold/disturbance/loss envelope, including residual limitations.

Pass G10 when restrained stabilized body-rate control and attitude hold meet the
physical requirements throughout the approved measured conditions. Launch,
translation, net recovery and free flight remain later milestones.

## Evidence record and shutdown

Use the accompanying [blank acceptance record](templates/MANUAL_BENCH_ACCEPTANCE_RECORD.json).
Copy it into the session evidence directory before editing it. Its twelve
requirements correspond to firmware, regression, output faults, source identity,
timing, canard calibration, power, load envelope, independent stop, EDF isolation,
disconnected capture and final physical review. Link every reviewed entry to
artifacts relative to that directory with SHA-256, reviewer, UTC review time and
measured result versus the predeclared limit.

From a WSL shell in the checkout, check the completed session record:

```bash
python3 -B Tools/research_sitl/check_bench_acceptance.py \
  /absolute/path/to/session/acceptance-record.json \
  --evidence-root /absolute/path/to/session
```

Exit 1 means missing/mismatched evidence; exit 2 means malformed input or invocation;
exit 0 establishes record completeness and byte integrity only. The checker always
leaves flash/output/physical authority false. Keep the named authority's release
and acceptance decisions as evidence; do not change those flags to manufacture
authorization. Stabilized G8-G10 records require their own plant, firmware and
physical test acceptance beyond this servo-bench schema.

For each normal end of test, center input, disable the active owner, remove actuator
energy through the reviewed circuit, stop the module, stop wind through its approved
sequence, and verify the fixture is safe before approaching it. For an unexpected
motion or limit exceedance, use the independent stop and reviewed tunnel shutdown;
do not wait for a CLI or radio response. Record the failure before any restart.

The bench startup/USB/input/routing checks remain retained. The later
[R01..R08 user report](../build/bench_commissioning/bench-e8ea98b404-step3/hardware-joystick-signal-report-20261020.json)
confirms profile/readbacks, Active centered1500 us/50 Hz on the scope, all signed
axes/intermediate positions and normal stop/loss/recovery/closeout. Next perform
the bounded ES3059MD R09 characterization and derive the measured operating
profile before R10. Remaining G4 fault review, installed loaded calibration,
powered/wind acceptance and separate stabilized firmware retain their own gates.
