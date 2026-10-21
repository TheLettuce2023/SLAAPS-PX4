# EMAX ES3059MD servo calibration and powered joystick test

2026-10-20 | Pixhawk 6X working bench image | Codex

**Current status: R09 PASS and R10 PASS (operator-reported).** All servos closely
matched manufacturer documentation, according to the latest
[R09/R10 operator report](MANUAL_BENCH_R09_R10_OPERATOR_REPORT_20261020.md). Numerical readings/raw captures and the exact tested
profile/load were not supplied. The procedure and diagnostic history below are
retained for reproduction and future measured runs.

**Earlier next R09 fixture:** the operator requested more observable travel.
Use the separate [1250..1750 us wider R09 procedure](MANUAL_BENCH_ES3059MD_WIDE_R09.md)
and file `es3059md-unloaded-wide-r09.txt` for one servo with linkage detached.
Normal demand slew is50 us/s, neutral remains1500 us. The wider file passes an
existing-native Ready-only check; hardware installation and measurements are
pending. No flash or parameter change is required. This explicitly replaces
the earlier provisional instruction to obtain narrow-range measurements before
preparing an expansion. The narrow procedure below and its evidence are retained;
its1450..1550 us targets and5 us/s rate do not describe the new wider fixture.

The user reports R01 through R08 complete, including 1500 us/50 Hz measured on
the scope, signed/intermediate joystick inputs and R06/R07 stop/loss/recovery.
The [signal report](../build/bench_commissioning/bench-e8ea98b404-step3/hardware-joystick-signal-report-20261020.json)
retains that result. Do not repeat those completed cases merely to begin R09.
Retest affected signals when changing the profile as described below.

The [operator now reports the no-motion issue resolved](../build/bench_commissioning/bench-e8ea98b404-step3/hardware-es3059md-motion-resolution-report-20261020.json):
travel with the bounded fixture was too small to notice by eye. The preceding
held-positive capture already verifies Active command+0.05, IO1550 us and a
user-measured MAIN1 pulse of1550 us. Proceed to step4 and collect physical R09
measurements; no new flash, parameter change or blanket R01..R08 repeat is needed.
The resolution report supplies no numerical angle/current or local supply data.

The identified servo is EMAX ES3059MD. The user reports a 4.8..6.0 V supply range,
12 A maximum supply capacity, a physical servo-power switch and no independent
PWM tester. The actual voltage setting must still be measured. The
[EMAX manufacturer specification, page 2](https://flymod.net/download/emax_es3059md_12g_digital_serv_specifications)
states 1500 us nominal neutral, 0.8 A maximum current at 4.8 V and 1.0 A at 6.0 V,
and no-load current maxima of 250/300 mA at those voltages. Those ratings are
references, not measurements of these installed servos. The PDF does not specify
refresh frequency; 50 Hz is the already verified controller waveform and will
be checked for stable servo response during characterization.

## R09 method and scope

Use the already scope-verified PX4 output as a bounded characterization source,
one unloaded servo at a time, with the linkage detached. The scope and an
independent angle measurement supply the reference values. This is an explicit
alternative to the independent PWM tester method in G5 because that tester is
unavailable. It does not independently validate the flight controller or close
unreported owner-stall/FMUIO fault tests. EDF remains physically isolated.

A separate [calibration profile](../build/bench_commissioning/bench-e8ea98b404-step3/es3059md-unloaded-calibration.txt)
limits normal pulse demand to 1450..1550 us and normal command slew to 5 us/s.
All four outputs carry the same calibration waveform; use roll only and keep
pitch/yaw centered. Connect only the servo being measured. This common waveform
is not the aircraft mix. The profile's 10-degree field is a model scale used in
the slew calculation, not a measured servo angle. The
[preparation receipt](../build/bench_commissioning/bench-e8ea98b404-step3/es3059md-calibration-preparation-20261020.json)
records parsing/Ready/start/stop with the existing private native binary. No
hardware install, enabled native run or powered result is claimed by that check.

## 1 Prepare power and measurement

1. Turn servo power off, keep the other servos disconnected, isolate EDF and
   detach the test servo's linkage. Secure the servo body; keep the horn's sweep
   clear. Label the servo and intended MAIN channel.
2. If the supply is adjustable, use 5.0 V as the initial test setting and measure
   it at the servo connector. A fixed supply must be verified inside the stated
   4.8..6.0 V range. Record actual voltage under load as well as at idle.
3. The reported 12 A is capacity, not an appropriate per-servo current setting.
   If the bench supply has adjustable current limiting, a 1 A initial ceiling
   for this single unloaded servo is a protective test setting. For a BEC,
   provide branch protection appropriate to the wiring. Neither replaces the
   physical power switch or establishes an acceptable continuous servo load.
4. Verify the switch removes servo energy. Connect supply negative, servo ground,
   scope ground and controller ground together. Connect supply positive to the
   reviewed servo power distribution, not to the PWM signal. Record polarity,
   the wiring photograph, current measurement method and power-cut result.
5. Prepare a protractor/angle fixture or equivalent independent position
   measurement. Identify positive motion and record measurement uncertainty.
   Keep an operator at the power switch during movement.

Record numerical limits for current, rail droop, angle error, clearance,
temperature, dwell and repetitions before marking results passing. Cut servo
power on binding, unexpected travel, excessive/persistent current or supply
collapse. Do not deliberately stall the servo to reproduce its rated maximum.

## 2 Install the new fixture while servo energy is off

Run individually in QGC's MAVLink Console. These commands write a new named
profile and leave the original signal-test file unchanged:

```text
research_bench_control stop
research_bench_control status
mkdir -p /fs/microsd/bench
echo "1 2 255 190 200000 0.05 0.05 500000" > /fs/microsd/bench/es3059md-unloaded-calibration.txt
echo "1450 1500 1550 10 1 1 1 1 1" >> /fs/microsd/bench/es3059md-unloaded-calibration.txt
echo "1450 1500 1550 10 1 1 1 1 1" >> /fs/microsd/bench/es3059md-unloaded-calibration.txt
echo "1450 1500 1550 10 1 1 1 1 1" >> /fs/microsd/bench/es3059md-unloaded-calibration.txt
echo "1450 1500 1550 10 1 1 1 1 1" >> /fs/microsd/bench/es3059md-unloaded-calibration.txt
cat /fs/microsd/bench/es3059md-unloaded-calibration.txt
```

Expect not running after stop; an already-stopped message is acceptable. Compare
the complete five-line readback with the [host fixture](../build/bench_commissioning/bench-e8ea98b404-step3/es3059md-unloaded-calibration.txt).
It has 44 tokens and exact host-file SHA-256
`08c5939868de46a9674fa508603fd7b04e58b87d5c5e0c329ccaf6fd8580549a`.
Retain a true card-file readback/hash or verified-transfer receipt; retyping
console text and hashing it is not a byte readback. The
[creation recipe](../build/bench_commissioning/bench-e8ea98b404-step3/es3059md-calibration-create-console.txt)
contains these commands and no enable or parameter writes.

Keep the measured R01 settings: MAIN1..4 functions10000..10003, MAIN_REV0,
MIN500/MAX2500/CENT1500/DIS0/FAIL0, MAIN timer groups50 Hz, unused MAIN/AUX
functions off and COM_PREARM_MODE1. This new fixture requires no parameter change.

## 3 Verify the changed profile at the pins with servos disconnected

With servo signals and energy still disconnected, start Ready and capture:

```text
research_bench_control start /fs/microsd/bench/es3059md-unloaded-calibration.txt
research_bench_control status
listener actuator_armed -n 1
listener manual_control_setpoint -n 1
px4io status
```

Expect Ready/state0, actual armed=false/prearmed=true, kill/lockdown/termination/
calibration=false, fresh valid USB2 input from255:190 and all three attitude axes
within +/-0.05. Actual first-four IO disarmed/failsafe entries must remain zero.
Use the real safety switch for prearm; keep the vehicle disarmed.

While recording the scope, enable centered:

```text
research_bench_control enable
research_bench_control status
listener bench_servo_command -n 1
px4io status
```

Confirm state1, enabled frames, a nonzero session and all MAIN1..4 at
1500 us/50 Hz. Hold a small positive roll, recenter, then a small negative roll.
After observing the narrow movements, check the +/-1 roll targets while still
disconnected. All four pins must remain in 1450..1550 us and mirror each other.
The intended normal slew is at most 5 us/s: center to either endpoint takes
about 10 seconds, and one endpoint to the other about 20 seconds. Measure actual
pulses and ramp timing; controller rounding and transport belong in the record.
Keep pitch/yaw centered; they are mapped in this fixture and can also affect it.
Verify AUX4 remains without servo PWM.

Normal disable, stop or lease expiry can request 1500 us directly after a valid
session. That fallback is not subject to the 5 us/s normal movement limit.
Record this behavior with signals disconnected before powering a servo.
Finish by centering, disabling, stopping and confirming not running.

Save this changed-profile check as an R09 attachment. It verifies the new
fixture only and does not require repeating unrelated completed R01..R08 work.

### If joystick movement does not move the servo

The [initial calibration readback](../build/bench_commissioning/bench-e8ea98b404-step3/hardware-es3059md-active-neutral-report-20261020.json)
confirms the new five-line profile, Ready then Active, real disarmed/prearmed
state and a fresh enabled neutral frame. The shown input is roll=-0.011,
pitch=-0.003, yaw=0, inside the0.05 deadzone. The later command is all zeros and
IO reports1500 us. These separately timed snapshots are consistent with neutral;
they do not show a held deflection. Earlier changing IO values precede the shown
file recreation and do not qualify the new profile's ramp.

Switch servo power off. Hold only roll fully right for at least12 seconds,
keeping pitch/yaw centered. Keep holding while capturing:

```text
listener manual_control_setpoint -n 1
research_bench_control status
listener bench_servo_command -n 1
px4io status
```

With reported roll=+1 and pitch=yaw=0 after settling, expect Active,
`control:[0.05,0.05,0.05,0.05]`, IO first-four PWM1550 and physical MAIN1..4
pulses1550 us/50 Hz. Record the scope, not only the console. This profile moves
slowly and has a small pulse range; a brief stick flick can be difficult to see.
All three axes contribute to this common-waveform fixture, so opposing axis
commands can cancel. Diagnose in order:

| Observation while held | Next check |
| --- | --- |
| Selected roll remains near zero | QGC selected joystick, Normal mode, axis assignment and active MANUAL_CONTROL transmission |
| Roll is+1 but state is revoked | Capture real permission/input/routing and resolve the cause before explicit stop/start/centered enable |
| Roll is+1, other axes centered, Active, command remains neutral | Retain the held snapshots and timestamps for owner/input diagnosis; do not change bounds or arm the vehicle |
| Command reaches+0.05 but IO/pin remains1500 | Trace the output consumer, routing/permission and actual pin measurement |
| IO says1550 but the servo plug does not | Check MAIN versus AUX, breakout/cable pinout and common ground |
| At the servo plug PWM changes correctly and supply is correct but no motion occurs | With linkage detached, inspect connector polarity, horn marker, servo and supply behavior; test one servo at a time |

IO now senses about6 V. That is not an independent voltage/polarity measurement
at the servo connector. Before restoring servo energy, use the prepared5.0 V
initial setting if adjustable and verify locally with a meter. Measure the PWM
at the actual servo plug relative to that plug's ground. Keep the characterization
bounds unchanged while localizing the fault. That capture ends Active; no final
stop or powered calibration result is inferred.

The [held-stick follow-up](../build/bench_commissioning/bench-e8ea98b404-step3/hardware-es3059md-revoked-report-20261020.json)
then shows roll=+1 arriving from the correct valid source, but owner state2 and
IO1500 us. Neutral is the intended latched revoked response. The trigger is not
reported: normal disable, an uncentered enable, or an earlier permission/input/
routing/timing rejection can lead to that state. Pitch=+/-0.082 while roll is
deflected does not itself violate the Active input range; neutral tolerance
applies at enable. Do not change calibration or widen the tolerance based on
those deflected samples.

With servo energy off, release all three axes to center and reset:

```text
research_bench_control stop
research_bench_control start /fs/microsd/bench/es3059md-unloaded-calibration.txt
research_bench_control status
listener actuator_armed -n 1
listener manual_control_setpoint -n 1
```

Require Ready, all three attitude axes inside+/-0.05 and the real disarmed/
prearmed predicate before submitting enable. Then capture:

```text
research_bench_control enable
research_bench_control status
listener bench_servo_command -n 1
```

Confirm state1 before moving any stick. Observe30 seconds centered, capture
status/command, then hold only roll right for12 seconds and capture them again.
Stop the diagnostic on the first revocation and record whether it happened at
entry, while centered or during movement. A fresh valid input after revocation
cannot clear the latch; never try to reset it by enabling with a deflected stick.

The [subsequent reset/command report](../build/bench_commissioning/bench-e8ea98b404-step3/hardware-es3059md-reset-command-report-20261020.json)
confirms centered entry, state1 and multiple fresh enabled command frames in
one session. The latest supplied frame is47.36 seconds after session start.
The commands change, so the earlier latch is cleared for this session. Their
range corresponds to only1498.61..1507.70 us using the transport conversion,
not an observed scope range. That report alone did not include a steady full-roll
sample or new pin measurement.

The [next held-positive result](../build/bench_commissioning/bench-e8ea98b404-step3/hardware-es3059md-held-pwm-report-20261020.json)
now confirms fresh roll=+1, pitch=+0.13, yaw=0 from255:190, Active in the same
session, enabled controls+0.05 on all four channels and IO1550 us. The user also
measures1550 us on the requested MAIN1 scope check. The positive input-to-pin
path is verified for this point. Pitch+0.13 adds to the common positive mix and
normalization bounds the target; it is not an Active-range rejection. The
snapshots are separate, and a new carrier/ramp trace or servo-connector reading
is not supplied. Do not repeat the centered reset unless state2 recurs.

Proceed to the one-servo connector and power checks in step4. First center and
wait for measured1500 us, disable, stop and remove servo energy before wiring.
At the actual servo connector confirm the signal relative to its ground, supply
polarity and locally measured voltage. Use5.0 V initially if adjustable, one
unloaded servo and the physical power switch. The IO6.015 V report is not that
independent connector measurement. This reply establishes neither powered horn
motion nor measured angle/current calibration. R09/R10 were still open at that
diagnostic point; the later operator closeout at the top records both as PASS.

## 4 Characterize one unloaded servo

Fit a light paper pointer to the horn and place a fixed protractor behind it,
with its center aligned to the shaft. Alternatively use a fixed camera normal
to the measurement plane and a calibrated angle reference. Record pointer length,
scale resolution, viewing geometry and estimated angle uncertainty. A100 mm
pointer gives about1.75 mm of arc travel per degree; it helps reveal small motion.
Keep this fixture light and clear of obstructions. Measure angles relative to a
marked reference at1500 us. That reference is not yet an installed surface zero.

With servo power off and owner stopped, connect only the labeled test servo to
its intended MAIN signal and common ground, plus the reviewed supply. The other
servo signals stay disconnected. Repeat the start, real permission/input checks
and centered enable in step 3 while servo energy remains off. Verify the actual
signal is stable at1500 us before applying servo energy through the switch.

Record startup motion, shaft/horn angle, rail voltage and current. A nominal
1500 us electrical neutral is not automatically the mechanical zero of a
linkage. For mechanical horn adjustment, remove servo energy first.

Start at the first small +/-10 us targets below. Hold roll steady and wait for
the pulse to settle before measuring the angle. The stick targets are a guide;
record actual pulse width and actual input instead of assuming an exact value.
Recenter between cases. Expand toward the +/-50 us caps only after the smaller
points show expected motion, current and clearance.

Use this initial collection sequence for each labeled servo:

1. Record measured neutral PWM/angle, supply voltage and idle/holding current.
2. Run1500 ->1510 ->1500 ->1490 ->1500 us, using the scope reading to set each
   point. After the PWM and angle settle, record a5-second dwell. Repeat three
   times and retain the actual dwell and variation. A change below the recorded
   angle uncertainty is unresolved, not zero travel or a passing repeatability result.
3. If motion, supply/current and clearance remain within the declared test limits,
   extend from neutral in10 us increments on each side:1520/1480,1530/1470,
   1540/1460 and1550/1450. Return to neutral between these initial cases. The
   normal demand changes at5 us/s, so center to a50 us endpoint takes about10 s;
   wait for actual settled measurements rather than assuming that timing.
4. Once that bounded range is established, collect an increasing and decreasing
   sweep in10 us steps, then repeat for three complete cycles. Retain each point's
   actual pulse/angle, approach direction, voltage/current and angle-versus-time
   record. Use the actual pulse as the independent variable when other axes drift.
5. Center to measured neutral, disable, remove servo energy and stop before
   swapping to the next servo/channel. Repeat the same measurement method.

The5-second dwell and three cycles are a collection protocol, not manufacturer
ratings or application acceptance limits. Declare the latter before marking R09
passing. This slow bounded sweep characterizes only its tested range and speed;
it does not establish a faster maximum slew. Keep the present profile bounds.
If more travel is required, first return the measured points and required angle
range so a separately named bounded expansion can be reviewed and checked at the
pins with servo energy off. Do not jump to the datasheet's full pulse span.

| Desired settled pulse us | Reported roll target with pitch=yaw=0 |
| ---: | ---: |
| 1450 | -1.00 |
| 1460 | -0.81 |
| 1470 | -0.62 |
| 1480 | -0.43 |
| 1490 | -0.24 |
| 1500 | 0.00 |
| 1510 | +0.24 |
| 1520 | +0.43 |
| 1530 | +0.62 |
| 1540 | +0.81 |
| 1550 | +1.00 |

At each steady point capture:

```text
listener manual_control_setpoint -n 1
listener bench_servo_command -n 1
px4io status
```

Record measured PWM, physical angle, approach direction, supply voltage/current,
clearance and any hunting or heating. Approach each accepted point from both
directions to expose backlash/hysteresis. Measure angle versus time on reviewed
small steps and derive the allowable command slew from the observed acceptable
response with margin. The narrow slow sweep does not prove the datasheet's
unloaded maximum speed or performance under an untested load.

For a normal finish, center and wait for measured neutral, then:

```text
research_bench_control disable
research_bench_control status
```

Remove servo energy with the physical switch, then:

```text
research_bench_control stop
research_bench_control status
```

Only change wiring/linkage with energy removed. Repeat for each MAIN channel,
recording each servo's label and orientation. A bare-servo record supports a
bare-servo follow-on test. For installed canards, complete the linkage/clearance,
signed mix and loaded-response measurements in G5 before installed operation.
Do not widen this fixture if the intended neutral/travel cannot be reached;
review horn/linkage alignment and a new bounded profile while stopped first.

## 5 Submit R09 measurements and create the operating profile

Return the table below for each channel, plus the voltage/current and angle
records. Use the same safe angle magnitude in each signed direction of a channel,
no greater than13 degrees and within the actually characterized range.

| Channel | Servo label | PWM at negative angle us | PWM at zero angle us | PWM at positive angle us | Angle magnitude deg | Rise and fall command slew deg/s | R P Y mix signs |
| --- | --- | --- | --- | --- | --- | --- | --- |
| MAIN1 | | | | | | | |
| MAIN2 | | | | | | | |
| MAIN3 | | | | | | | |
| MAIN4 | | | | | | | |

Also provide actual supply voltage, current at idle/moving/holding, observed rail
droop, the physical stop result, any nonlinearity/backlash and raw evidence
paths. Record the tested load and uncertainty. Do not fill angle or slew columns
from the fixture's model-scale fields or a datasheet speed conversion.

Generate a separately named measured
`/fs/microsd/bench/installed-servo-profile.txt` from those results. It is not
created by the characterization recipe. Each MAIN_CENT1..4 must equal that
channel's integer measured neutral. Prepare those exact parameter writes while
stopped with servo energy removed; no universal replacement values are supplied
before calibration. Retain broad transport MIN500/MAX2500, disarmed/failsafe0,
REV0, routing and rates. Recheck the actual IO failsafe table. The measured mix
must represent the intended surface directions; do not reuse the all-identical
calibration mix as the vehicle allocation.

Read back/hash the actual installed profile and repeat the affected disconnected
normal pin captures using its measured endpoint/neutral/slew expectations.
Complete the applicable G4 fault review for the powered scope. R06/R07 confirmation
does not by itself close owner-stall or full FMU/IO fault cases.

## R10 powered joystick verification

Begin only with the reviewed R09 profile, power circuit, physical stop and
applicable signal/fault review. Keep EDF isolated and no wind. Begin with one
connected servo and the characterized load/envelope; add channels only after
their individual results. With servo energy off, center input and start:

```text
research_bench_control start /fs/microsd/bench/installed-servo-profile.txt
research_bench_control status
listener actuator_armed -n 1
listener manual_control_setpoint -n 1
px4io status
```

Confirm Ready and actual measured routing/centers, then use real prearm while
remaining disarmed. With scope acquisition running and servo energy still off,
enable centered:

```text
research_bench_control enable
research_bench_control status
listener bench_servo_command -n 1
px4io status
```

Confirm state1 and measured neutral PWM before applying servo energy. Record
startup/neutral angle, then use the released small positive/negative commands,
returning to neutral each time. Record commanded input, actual PWM, angle,
direction, rise/fall response and rail/current. Expand only within the calibrated
range. Check throttle independence. Compare the intended per-channel mix rather
than the common-waveform characterization table.

Save R10 channel/angle/power results and the separately reviewed powered
disable/loss/recovery cases. Do not run hazardous whole-controller fault
injections powered merely because a normal stop test passed. During the released
steady multi-channel/dwell test capture:

```text
work_queue status
top once
```

For normal shutdown, center input, disable, remove servo energy, then stop and
record:

```text
research_bench_control disable
research_bench_control status
```

After the physical power switch is off:

```text
research_bench_control stop
research_bench_control status
listener actuator_armed -n 1
px4io status
dmesg
```

Use the independent switch immediately on unexpected motion or a limit
exceedance. Before restoring energy, stop the module and repeat the reviewed
reset/prearm/centered-enable sequence; restoring servo energy while neutral
pulses persist can itself move the servo. Final status must be not running and
servo energy removed.

R09 closes measured calibration for its stated load/envelope. R10 closes only
the powered manual conditions actually tested and reviewed. Wind and stabilized
control remain separate. Use the [report form](templates/MANUAL_BENCH_JOYSTICK_TEST_REPORT.md)
and the G5/G6 sections of the [parent procedure](MANUAL_BENCH_FLASH_AND_TEST_PROCEDURE.md).
