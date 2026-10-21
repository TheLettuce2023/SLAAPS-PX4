# Manual servo bench joystick test run sheet

2026-10-20 | Pixhawk 6X | Codex

Use this sequence with the working `research_bench_control` image to start the
module, verify joystick commands at the physical PWM pins, and prepare the first
powered servo test. Steps 1 through 9 use disconnected servo and EDF signals and
disconnected actuator power. Step 10 requires the measured servo profile and the
G4/G5 review in the [commissioning procedure](MANUAL_BENCH_FLASH_AND_TEST_PROCEDURE.md).
The vehicle remains disarmed throughout.

The user now reports R01 through R08 passed, including Active entry,
1500 us/50 Hz confirmed on the scope, all signed axes/intermediate positions,
R06/R07 stop/loss/recovery and closeout. The
[signal-test report](../build/bench_commissioning/bench-e8ea98b404-step3/hardware-joystick-signal-report-20261020.json)
retains that scope. No repeat of those completed cases is requested. The latest
[R09/R10 operator report](MANUAL_BENCH_R09_R10_OPERATOR_REPORT_20261020.md) records **R09 PASS** and **R10 PASS**, with all servos
reported close to manufacturer documentation. Numerical/raw evidence and the
exact tested profile/load were not supplied in the closeout. The
[ES3059MD calibration and powered procedure](MANUAL_BENCH_ES3059MD_R09_R10.md)
remains available for reproduction and future measured runs.
The remaining steps below retain reproducible commands; their presence alone
does not mark an unperformed test passing.
Copy the [report form](templates/MANUAL_BENCH_JOYSTICK_TEST_REPORT.md) for each run.
Record failures and incomplete measurements as such, and stop advancing on an
unexpected result. Declare numerical tolerances, response-time limits, dwell and
repetitions before judging the associated measurements.

## 1 Prepare the fixture and evidence folder

Keep the EDF supply and control signal physically disconnected. Keep all servo
signals and servo power disconnected during signal measurements. Connect the
Pixhawk and calibrated gamepad to the laptop, and keep QGC connected over USB.
Use the existing joystick calibration, Normal joystick mode using MANUAL_CONTROL,
and the established left=-1/right=+1 roll convention. Keep arm/mode actions out of
the test button mapping. Record the actual QGC build, joystick model and settings.
[QGC joystick instructions](https://docs.qgroundcontrol.com/master/en/qgc-user-guide/setup_view/joystick.html)
describe selecting the joystick, enabling it and checking its axis monitor.

Connect scope probes to MAIN1 through MAIN4 signal and GND on the I/O PWM OUT
breakout. Monitor AUX4 on FMU PWM OUT for unintended output. If channels are
limited, repeat the same test for each pin and label each separate capture; do
not describe sequential captures as simultaneous. MAIN1 corresponds to IO_CH1,
through MAIN4/IO_CH4. AUX4 is FMU_CH4.
[PX4 wiring reference](https://docs.px4.io/v1.18/en/assembly/quick_start_pixhawk6x)

Use high-impedance scope inputs, DC coupling and a rising-edge trigger appropriate
to the observed signal voltage. About 5 ms/div initially shows the 20 ms period;
zoom in for pulse-width measurements. Use automatic acquisition or a suitable
single capture to retain the no-pulse baseline and first enable. Record probe
ratio, sample rate, voltage/time scales, trigger and measurement uncertainty.
The current signal test leaves the servo rail unpowered; later servo movement
requires an external supply matched to the actual servos and a common ground.

On the Windows host, create a run folder in PowerShell:

```powershell
$benchStamp = (Get-Date).ToUniversalTime().ToString('yyyyMMddTHHmmssZ')
$benchRunDir = Join-Path 'C:\Projects\SNC SENIOR DESIGN\FC Software + SITL\build\bench_commissioning\operator-runs' $benchStamp
New-Item -ItemType Directory -Path $benchRunDir
```

Save the console text, raw scope exports and screenshots there using the report
names below. Commands in `text` blocks run individually at QGC's MAVLink Console
`nsh>` prompt; omit the prompt itself. PowerShell blocks run on the laptop.
Capture the whole console output, not just the last status line.

## 2 Record firmware identity and configuration

The image for this run is
`px4_fmu-v6x_default_bench_e8ea98b404_nuttx11_dirty_20261019T225405Z.px4`
from the [working baseline port](MANUAL_BENCH_WORKING_BASE_PORT.md). Its host-file
SHA-256 is
`a45cf99374fa7ccfd4f6349ca9a6341bacfcfa6199254f71ab2b04f0cb2aed31`.
This run sheet does not apply to the separate output-disabled observer or newer
SLAAPS integration candidates. Retain the upload receipt identifying the exact
file; `ver all` alone does not prove a dirty image's complete byte identity.

With the module stopped, capture:

```text
research_bench_control status
ver all
param show COM_RC_IN_MODE
param show MAN_ARM_GESTURE
param show COM_PREARM_MODE
param show PWM_MAIN_TIM*
param show PWM_MAIN_FUNC*
param show PWM_AUX_FUNC*
param show PWM_MAIN_REV
param show PWM_MAIN_MIN*
param show PWM_MAIN_MAX*
param show PWM_MAIN_CENT*
param show PWM_MAIN_DIS*
param show PWM_MAIN_FAIL*
px4io status
```

Expected values for this signal fixture:

| Item | Expected result |
| --- | --- |
| Module | Not running; if it is running, stop it with loads disconnected and resolve the prior session before starting this run |
| Runtime | PX4 hash `e8ea98b404baf50623a519ac67ec1c03aa240859`, NuttX 11.0.0, build Oct 19 2026 22:51:17, HW PX4_FMU_V6X |
| Input and prearm | COM_RC_IN_MODE=1, MAN_ARM_GESTURE=0, COM_PREARM_MODE=1 |
| Routing | MAIN1..4 functions 10000,10001,10002,10003; MAIN5..8 and all advertised AUX1..9 functions zero |
| Rates and reversal | MAIN_TIM0..2=50; MAIN_REV=0 |
| MAIN1..4 transport values | MIN=500, MAX=2500, CENT=1500, DIS=0, FAIL=0 |
| Actual IO table | First four `pwm_disarmed` and `pwm_failsafe` entries zero; init and FMU-data OK true |

The 500..2500 us values define the transport conversion, not a safe servo travel
range. Check the actual IO arrays separately from the channel-configuration
printout. A previous console-only reboot retained nonzero IO failsafe entries;
the later all-source cold check cleared the observed mismatch. Recheck after
configuration/reset changes. Stop if the first-four table values are nonzero.
Do not blindly rewrite settings while the module is running.

Save **R01-configuration.txt**, the firmware/upload identity and a current QGC
parameter export. The deliberate earlier reset remains accepted; recovery of a
pre-reset backup is not required.

## 3 Verify the selected profile and start Ready

Use the existing card file:

```text
cat /fs/microsd/bench/usb-signal-only-profile.txt
```

It must contain exactly these five lines and 44 numeric tokens:

```text
1 2 255 190 200000 0.05 0.05 500000
1400 1500 1600 10 1000 1000 1 1 1
1600 1500 1400 10 1000 1000 1 -1 -1
1400 1500 1600 10 1000 1000 -1 1 -1
1400 1500 1600 10 1000 1000 -1 -1 1
```

The header fields are version, MAVLink instance, system ID, component ID,
maximum owner cycle gap in us, centered-entry tolerance, deadzone and consumer
lease in us. Each servo row contains negative/neutral/positive pulse widths in
us, maximum angle in degrees, rise/fall slew in deg/s, then roll/pitch/yaw mix
coefficients. Here the owner gap is 200 ms; it is separate from the 500 ms input
age and consumer lease. These configured values are not measured timing results.

Before active acceptance, read the actual card file back to the host using a card
reader or verified file transfer. Save it as `usb-profile-card-readback.txt` in
the run folder, and hash that file in PowerShell:

```powershell
Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $benchRunDir 'usb-profile-card-readback.txt')
```

The expected exact-byte hash is
`1bf090fc08a058d00be6fb0f80f70a32f218c3df17695059faa6381524ea3c7f`.
A hash of retyped console text does not prove the card file's bytes. Investigate
any mismatch before enable. No onboard checksum command is assumed.

After the file and configuration checks, start the module:

```text
research_bench_control start /fs/microsd/bench/usb-signal-only-profile.txt
research_bench_control status
```

Expect `state=0 (0 ready, 1 active, 2 revoked); no physical acceptance`. Start
parses the profile and advertises the command topic; it does not enable a
session. Ready is not proof that all routing and permission checks passed.

On a fresh cold boot before any accepted session, retain MAIN1..4 and AUX4
baseline traces showing no servo PWM. After a prior accepted session, the
consumer can retain neutral pulses across module stop/start while prearm stays
permitted. Do not confuse that later state with the initial boot baseline.
Do not require a populated `bench_servo_command` sample before first enable.

Save **R02-profile-ready.txt**, the real file readback/hash and
**R02-baseline** scope exports. If no scope is available, finish this disabled
run with `research_bench_control stop` and confirm not running; continue at the
measured stage when equipment is available.

## 4 Confirm prearm and centered joystick input

With loads still disconnected, use the installed safety button as needed to
reach the real prearm state. Do not arm the vehicle. Record:

```text
listener actuator_armed -n 1
listener manual_control_setpoint -n 1
mavlink status
```

Required actual permission fields are `armed: False`, `prearmed: True`, and
`lockdown`, `kill`, `termination`, `in_esc_calibration_mode`: False.
`ready_to_arm: False` is not an instruction to bypass arming checks; this bench
owner requires a disarmed, prearmed vehicle.

The selected input must be fresh and valid with `data_source: 4`,
`source_system_id: 255`, `source_component_id: 190`. Roll, pitch and yaw must each
be within +/-0.05 at entry. Center the sticks and release action buttons.
Throttle is ignored by the bench owner. The `sticks_moving` flag is not a test
of whether a stick is deflected; use the numeric axis values.

Check input both centered and while moving each axis before enabling, then
return all three attitude axes to center. Expected signs are right roll +,
left roll -, forward/up pitch +, backward/down pitch -, yaw right + and left -.
Use the reported axis values to label every measurement. Keep QGC joystick
transmission enabled while using the console.

The legacy displayed MAVLink `rx loss` percentage is misformatted in this
baseline. It and the smoothed message-rate display do not establish worst-case
input gaps. Preserve unexpected link behavior for the timing review rather than
interpreting either number as a measured pass.

Save **R03-input-permission.txt**, the QGC joystick/settings screenshots and the
physical movement order. Stop if the sender, validity or permissions differ.

## 5 Enable and measure centered PWM

Confirm the scope is acquiring and sticks remain centered. Run:

```text
research_bench_control enable
research_bench_control status
listener bench_servo_command -n 1
listener actuator_armed -n 1
px4io status
```

`enable` queues a request. Poll status again if necessary to observe the work
queue result; do not send repeated enable requests. Proceed only when status is
`state=1`. The command sample must have `enabled: True`, a nonzero session,
`lease_us: 500000`, and approximately zero `control[0..3]` at center. Sample
timestamps must advance during the active run. Actual vehicle armed stays false.

Measure all four MAIN outputs at approximately **1500 us positive pulse width**
and **20 ms period (50 Hz)**, within the declared tolerances. AUX4 must have no
servo PWM. Check IO `pwm[0..3]` agrees with the steady pin measurements. IO
`status_outputs_armed: True` is a hardware/raw-PWM flag, not Commander armed state.

If status becomes `2 revoked`, keep loads disconnected, save the input,
permission, configuration and status evidence, and use the reset sequence in
step 9 after finding the cause. A successful CLI return alone is not a pass.

Save **R04-enabled-neutral.txt** and **R04-neutral** raw scope captures, including
the transition from Ready to Active. Record each pin's width, period and jitter.

## 6 Test joystick axes with signals only

Use the current synthetic profile with all actuators disconnected. Start with
small single-axis commands, then intermediate values, and finally the reported
+/-1 endpoints. Keep the other two attitude axes centered and return to center
between cases. Hold each position for the declared dwell while saving:

```text
listener manual_control_setpoint -n 1
listener bench_servo_command -n 1
px4io status
```

These are separate snapshots. Record their timestamps and keep the input steady;
do not claim synchronized command-to-pin latency from their console print order.
Save a corresponding raw scope trace for each row and repeat on all four MAIN
pins. The settled exact-endpoint targets are:

| Case | Selected input | MAIN1 us | MAIN2 us | MAIN3 us | MAIN4 us |
| --- | --- | ---: | ---: | ---: | ---: |
| Center | R=0, P=0, Y=0 | 1500 | 1500 | 1500 | 1500 |
| Roll right | R=+1, P=0, Y=0 | 1600 | 1400 | 1400 | 1400 |
| Roll left | R=-1, P=0, Y=0 | 1400 | 1600 | 1600 | 1600 |
| Pitch forward | R=0, P=+1, Y=0 | 1600 | 1600 | 1600 | 1400 |
| Pitch backward | R=0, P=-1, Y=0 | 1400 | 1400 | 1400 | 1600 |
| Yaw right | R=0, P=0, Y=+1 | 1600 | 1600 | 1400 | 1600 |
| Yaw left | R=0, P=0, Y=-1 | 1400 | 1400 | 1600 | 1400 |
| Throttle only | R=P=Y=0; vary throttle | 1500 | 1500 | 1500 | 1500 |

These targets apply to reported +/-1 values after settling. At other values,
the 0.05 deadzone is removed and the remaining range is rescaled: for a single
axis, `a = sign(x) * (abs(x)-0.05)/0.95` outside the deadzone, otherwise zero.
The endpoint's signed pulse offset from 1500 us is multiplied by `abs(a)`.
For example, a reported roll +0.5 gives approximately
`[1547.4,1452.6,1452.6,1452.6] us`. Driver quantization and instrument uncertainty
belong in the comparison. MAIN2's reversed pulse calibration is intentional.

`bench_servo_command.control[]` contains transport-normalized values, not pulse
widths or stick values. With center 1500 and transport bounds 500..2500,
`PWM_us = 1500 + 1000 * control[i]`. Thus 1400/1600 us correspond to -0.1/+0.1.
The MAIN1..4 function assignments map `control[0..3]` in order.

With signals still disconnected, also record small mixed-axis inputs and the
5% deadzone behavior. Combined inputs are mixed and scaled together if needed;
the synthetic profile should remain within 1400..1600 us. Check period remains
20 ms and AUX4 remains inactive throughout, including throttle changes.

Save **R05-axis-console.txt**, **R05-axis-results.csv**, and raw captures keyed
to each case. Suggested CSV columns are:

```csv
case,repetition,input_timestamp,roll,pitch,yaw,throttle,session,main1_us,main2_us,main3_us,main4_us,period_us,max_jitter_us,aux4_observation,capture_file,result
```

This completes normal signal correlation only, not powered-servo acceptance.

## 7 Verify normal disable and stop

With a small nonzero command held and the scope recording, test disable:

```text
research_bench_control disable
research_bench_control status
listener bench_servo_command -n 1
listener actuator_armed -n 1
px4io status
```

Expect `state=2`, a disabled command frame after an accepted session, and neutral
PWM while prearm remains permitted. The consumer is intended to request 1500 us
here; zero pulses are not the normal-disable target. Measure the transition and
confirm moving the joystick cannot reactivate the old session.

Then stop and capture:

```text
research_bench_control stop
research_bench_control status
px4io status
```

Expect not running. The consumer may continue neutral PWM while permitted.
To test stopping directly from Active as a separate case, follow step 9, enable
centered, hold the reviewed small offset and issue `stop` while recording.
Measure that case separately; disabling first would not test stop from Active.

Save **R06-disable-stop.txt** and distinct disable/stop transition captures.
Measure response against the declared limit with a recorded event reference;
manual CLI print order alone is not an end-to-end timing measurement.

## 8 Verify joystick loss and recovery with signals only

Use step 9 to begin a new centered Active session, then hold a small nonzero
command. Record the scope and stop joystick MANUAL_CONTROL transmission using
QGC's joystick enable control while leaving USB and the MAVLink Console connected.
Record the exact UI action used. Keep servo/EDF power and signals disconnected.

After the loss, capture:

```text
research_bench_control status
listener manual_control_setpoint -n 1
listener bench_servo_command -n 1
listener actuator_armed -n 1
px4io status
```

Expect rejection/revocation and neutral demand after the accepted session while
prearm remains permitted. Record actual input age/validity and the pin response.
The profile has a 500 ms selected-sample age bound and a 500 ms consumer lease;
neither proves a 500 ms end-to-end response. Input transport, invalidation,
scheduling and IO transport must be included in the measured result.

Restore joystick transmission and center the sticks. Repeat the status/input
captures. Status must remain revoked and the old session must not resume. A new
session requires the explicit stop/start/centered-enable sequence in step 9.

Save **R07-loss-recovery.txt**, the UI state before/after, raw transition captures
and any packet/event timestamps. If last packet delivery cannot be identified,
report the timing limitation instead of assigning the UI click as packet-loss
time. Confirm transmission actually stopped; an Active state with continuing
valid input is not a successful loss injection.

This USB case does not test a radio link. Permission-loss, source-change,
cold-boot, restoration and routing-exclusion cases still follow G4. Owner-only
stall and FMU/IO failure tests need reviewed injection methods; no approved
owner-stall CLI currently exists. Normal `stop` is not a substitute. List these
open cases in the report and complete their review before the powered stage
they affect; steps 5 through 8 alone do not pass all of G4.

## 9 Reset deliberately and close the signal run

After disable or a fault, resolve its cause, restore real input and permission,
and center roll/pitch/yaw. With loads disconnected, reset using:

```text
research_bench_control stop
research_bench_control status
research_bench_control start /fs/microsd/bench/usb-signal-only-profile.txt
research_bench_control status
listener actuator_armed -n 1
listener manual_control_setpoint -n 1
```

Expect not running, then Ready/state0 with no automatic Active session. If the
module was already stopped, the first stop can report that fact; retain it.
Enable only after checks and capture are restored, using step 5. Each accepted
new session must have a new session value. Do not keep retrying enable in state2.

At final shutdown center input, disable if Active, stop the owner and record:

```text
research_bench_control status
listener actuator_armed -n 1
px4io status
dmesg
```

Save **R08-closeout.txt**, final state, deviations and links to each raw evidence
file. Report normal signal testing separately from incomplete timing/fault
coverage. No actuator power is applied during this signal run.

## 10 Proceed to powered joystick movement after calibration

The current USB fixture contains synthetic endpoints, a synthetic mix and
1000 deg/s model slew values. It is not the installed-servo profile. Before
installed R10, complete the applicable G4 signal/fault review and G5 calibration.
The preferred calibration source is an independent measured PWM source with PX4
signal disconnected; the bounded R09 alternative below applies to the current
setup without that source. Record one servo at a time: model/rated supply and rate, safe
neutral and endpoints, physical angle/sign, clearance, current, loaded slew and
rail droop. Choose an initially narrow envelope from those measurements and the
installation review. Keep EDF isolated and verify the independent power stop.

For the current ES3059MD setup the user has no independent tester, but does have
the scope-verified controller output and a physical power switch. Use the
[bounded unloaded characterization alternative](MANUAL_BENCH_ES3059MD_R09_R10.md)
for R09: a separate 1450..1550 us fixture with 5 us/s normal pulse slew, one
unloaded servo at a time, scope/angle/current references, and an explicit limit
on the calibration scope. This replaces the independent-source requirement for
that characterization only; it does not independently validate the controller
or close the remaining G4 fault review for installed powered operation.

Save **R09-servo-calibration** with four channel records, the power/stop review
and the installed 44-token profile/hash. The project's maximum configured angle
is 13 degrees; a smaller measured envelope can be used. Missing calibration or
slew values remain missing. Recheck physical mix signs against the hinge geometry.

Install that separately named measured profile at
`/fs/microsd/bench/installed-servo-profile.txt` only after it has been generated and
reviewed. This path is a future operator artifact, not an already staged file.
With the module stopped and actuators disconnected, set each MAIN_CENT1..4 to its
integer measured neutral through the reviewed setup; the routing guard requires
exact agreement with the profile. Retain the other required transport/off-value
settings and verify actual IO failsafe entries. Read back/hash the real card file
and repeat the affected configuration, parsing and normal pin measurements using
this profile. The synthetic table in step 6 no longer supplies its expected PWM.

For the reviewed powered test, begin with actuator energy removed and owner
stopped. Connect the approved external servo supply/distribution and MAIN signal/
ground wiring; check polarity, shared ground and current limits. Restrain the
installation, begin with one connected unloaded servo where the reviewed setup
allows, and keep the others disconnected. Center the gamepad, start Ready and
check the measured configuration:

```text
research_bench_control start /fs/microsd/bench/installed-servo-profile.txt
research_bench_control status
listener actuator_armed -n 1
listener manual_control_setpoint -n 1
px4io status
```

Apply servo energy through the reviewed power sequence while recording startup
movement, supply voltage/current and pin PWM. Enter real prearm while keeping
armed=false, recheck permissions/input, and enable centered:

```text
listener actuator_armed -n 1
listener manual_control_setpoint -n 1
research_bench_control enable
research_bench_control status
listener bench_servo_command -n 1
px4io status
```

Verify state1, actual PWM at measured neutral and actual neutral surface angle.
Apply the reviewed small positive/negative joystick steps, recenter between
them, and record angle, direction, pulse width, current and rail voltage. Stop
on unexpected motion, binding, overcurrent, rail droop or reset. Add channels
and combined inputs only through the reviewed expansion sequence. Confirm
throttle has no servo/EDF effect. Use only fault cases released for the powered
setup; do not repeat hazardous controller/IO faults merely because the signal
run included them.

For normal finish, center input and run:

```text
research_bench_control disable
research_bench_control status
```

Remove servo energy through the independent circuit, then run:

```text
research_bench_control stop
research_bench_control status
listener actuator_armed -n 1
px4io status
```

Save **R10-powered-joystick** with channel/angle/power results, stop and recovery
observations and final stopped state. For an abnormal event use the independent
energy stop immediately; a CLI command is not the emergency stop. G6 also needs
the reviewed simultaneous-load/dwell and runtime-resource checks in the parent
procedure. During the reviewed steady test, capture:

```text
work_queue status
top once
```

Record actual stack/work-queue diagnostics available on this image and any
unsupported report rather than substituting static ELF sizes. This is manual
surface control; stabilized control has separate gates.

## Required report package

| Report | Contents to retain |
| --- | --- |
| R01 | Firmware/upload identity, runtime console, full routing/rate/limit/off-value readback, actual IO arrays, parameter export, instrument/installation limits |
| R02 | Actual profile readback/hash, complete board cat, Ready status, initial scope baseline |
| R03 | Selected input identity and age, centered and moving axes, real permission topic, QGC/gamepad settings |
| R04 | Active status/session, enabled frames, centered pin widths/periods/jitter, AUX4 observation |
| R05 | Each signed axis and intermediate/mixed/deadzone/throttle case, console snapshots, numerical table and raw scope exports |
| R06 | Separate disable and direct-stop captures from Active, permission state and response time |
| R07 | Actual joystick-loss injection, input/command/pin transition, no automatic recovery, explicit new session, open fault cases |
| R08 | Final stopped state, actual permissions/IO, dmesg, deviations and evidence index |
| R09 | Before powered tests: measured servo calibration, safe limits/slew/mix, installed profile/hash, supply and independent-stop review |
| R10 | Powered joystick tests: actual angle versus PWM/input, rail/current/load behavior, stop/recovery and closeout |

Copy the [report form](templates/MANUAL_BENCH_JOYSTICK_TEST_REPORT.md), attach raw
files and record reviewer/date/result for each report. The existing
[physical acceptance record](templates/MANUAL_BENCH_ACCEPTANCE_RECORD.json) remains
the parent evidence contract; this run sheet and report form do not replace its
G0 through G6 review or mark unperformed cases passing.
