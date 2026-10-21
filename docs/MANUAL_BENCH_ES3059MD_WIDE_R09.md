# Wider EMAX ES3059MD R09 characterization profile

2026-10-20 | Pixhawk6X working bench image | Codex

**Current status: R09 PASS and R10 PASS (operator-reported).** The operator reports
all servos closely matched manufacturer documentation and explicitly accepts both
reports. See the [R09/R10 operator report](MANUAL_BENCH_R09_R10_OPERATOR_REPORT_20261020.md). Numerical readings, raw captures and the
exact tested profile/load were not supplied; the preparation below retains its
original software-only scope and does not identify the installed test file.

The operator requests more travel than the original1450..1550 us fixture.
This separately named profile changes normal demand to1250..1750 us: five times
that pulse excursion, with1500 us neutral. Normal command slew is50 us/s,
so center to either endpoint takes about5 seconds and endpoint-to-endpoint10.
Those are commanded PWM values and timing, not measured shaft angles or motion.

Use one servo at a time with its linkage detached, other servo signals and EDF
disconnected, and the physical servo-power switch available. This is a bare-servo
characterization fixture. All four channels carry the same waveform; it is not
the aircraft mix. Physical angles may exceed the eventual installed canard limit.

The [EMAX specification, page2](https://flymod.net/download/emax_es3059md_12g_digital_serv_specifications)
gives1500 us nominal neutral and operating travel at1000..2000 us. The selected
1250..1750 us interval lies inside that reference, but actual angle, clearance,
power and response still need measurement. No full datasheet-range sweep is proposed.

The [prepared profile](../build/bench_commissioning/bench-e8ea98b404-step3/es3059md-unloaded-wide-r09.txt)
SHA-256 is `674f2f8e8b8d9772ab7a105fed4389126043076cf331f0dad5a6583f25c44f9d`.
The [preparation receipt](../build/bench_commissioning/bench-e8ea98b404-step3/es3059md-wide-r09-preparation-20261020.json)
binds the profile, recipe, prior evidence and existing-native Ready-only check.
That check passes without enable, Commander, physical drivers or a new build.
The wider fixture has not yet been installed or physically tested by that check.

## Create the separate file

Switch servo energy off and stop the module before creating the file. Run these
commands individually in QGC MAVLink Console:

```text
research_bench_control stop
research_bench_control status
mkdir -p /fs/microsd/bench
echo "1 2 255 190 200000 0.05 0.05 500000" > /fs/microsd/bench/es3059md-unloaded-wide-r09.txt
echo "1250 1500 1750 10 2 2 1 1 1" >> /fs/microsd/bench/es3059md-unloaded-wide-r09.txt
echo "1250 1500 1750 10 2 2 1 1 1" >> /fs/microsd/bench/es3059md-unloaded-wide-r09.txt
echo "1250 1500 1750 10 2 2 1 1 1" >> /fs/microsd/bench/es3059md-unloaded-wide-r09.txt
echo "1250 1500 1750 10 2 2 1 1 1" >> /fs/microsd/bench/es3059md-unloaded-wide-r09.txt
cat /fs/microsd/bench/es3059md-unloaded-wide-r09.txt
```

Confirm the complete five-line readback exactly matches the commands. There are
44 tokens. The original narrow file remains available. The field10 is still a
model scale for the slew computation, not a measured10-degree shaft angle.
No firmware flash or parameter change is required: retain MAIN1..4 functions
10000..10003, MAIN_CENT1500, transport MIN500/MAX2500, DIS0/FAIL0, REV0,50 Hz,
unused MAIN/AUX functions off and the existing real prearm configuration.

## Verify the new profile with servo energy off

Keep all servo signals disconnected for this changed-profile pin check:

```text
research_bench_control start /fs/microsd/bench/es3059md-unloaded-wide-r09.txt
research_bench_control status
listener actuator_armed -n 1
listener manual_control_setpoint -n 1
px4io status
```

Require Ready/state0, actual armed=false/prearmed=true and clear kill/lockdown/
termination/calibration flags. Require fresh valid USB2 input from255:190 and
roll/pitch/yaw all within+/-0.05. Confirm first-four IO disarmed/failsafe zeros,
routing/centers and50 Hz. Keep the vehicle disarmed.

With a scope recording MAIN1..4:

```text
research_bench_control enable
research_bench_control status
listener bench_servo_command -n 1
px4io status
```

Require Active/state1, enabled fresh frames and measured1500 us neutral.
Move roll only, keeping pitch/yaw centered. Increase the excursion through the
table in order; return to measured1500 us between the first signed cases.
Actual pulse width is the reference when the other axes drift.

| Approximate absolute roll | Negative PWM us | Positive PWM us |
| ---: | ---: | ---: |
| 0.00 | 1500 | 1500 |
| 0.24 | 1450 | 1550 |
| 0.43 | 1400 | 1600 |
| 0.62 | 1350 | 1650 |
| 0.81 | 1300 | 1700 |
| 1.00 | 1250 | 1750 |

At full settled roll+1, expect controls[0.25,0.25,0.25,0.25], IO1750 us on MAIN1..4
and physical1750 us/50 Hz. Full negative roll gives controls-0.25 and1250 us.
Measure the normal ramp and check all four channels against the new bounds.
AUX4 remains without servo PWM. At centered input expect controls0 and1500 us.

With servo energy still off, verify normal disable returns the pins to1500 us:

```text
research_bench_control disable
research_bench_control status
research_bench_control stop
research_bench_control status
```

Disable is a latched revocation. Stop/start and centered enable are required for
the next session. Neutral fallback after disable/expiry is not constrained by
the50 us/s normal ramp and can return directly to1500 us. This changed-profile
check does not reopen unrelated completed R01..R08 cases or close the remaining
owner-stall/FMUIO fault review.

## Measure one unloaded servo

With servo energy off and the module stopped, connect one labeled servo to its
intended MAIN signal, supply and common ground. Detach the linkage and secure
the body. Record the actual voltage at its connector, supply/current measurement
method and power-switch result. Use5.0 V initially if adjustable and verify the
stated4.8..6.0 V range at the servo. Use the existing power/stop precautions in
[the R09 procedure](MANUAL_BENCH_ES3059MD_R09_R10.md).

Fit a light pointer and fixed protractor or equivalent independent angle
reference. Record viewing geometry and uncertainty. Repeat the start/real
permission/input checks above, enable while centered with servo energy off,
and confirm Active and1500 us at the actual servo connector before applying power.

Record the angle at1500 us. That is a measurement reference, not an installed
surface zero. Begin with1450/1550 us using about+/-0.24 roll, then return to neutral.
Only if motion, current, supply and clearance remain within the declared test
limits, extend through1400/1600,1350/1650,1300/1700 and1250/1750 us. Do not jump
straight to full stick on the first powered movement. Wait for PWM and angle to
settle at every point and record a proposed5-second dwell; repeat three times.
Once that range is established, approach each accepted point from both directions.

For each point capture:

```text
listener manual_control_setpoint -n 1
listener bench_servo_command -n 1
research_bench_control status
px4io status
```

| Channel/servo label | Measured PWM us | Angle deg | Approach | Connector V | Current A | Settling/clearance/notes |
| --- | ---: | ---: | --- | ---: | ---: | --- |
| | | | | | | |

Retain scope captures and angle photographs/video. Record idle, moving and
holding current, voltage droop, repeatability/backlash and angle-versus-time.
Stop on binding, unexpected travel, supply collapse or a declared current/
temperature limit. The dwell/repetitions are a collection protocol, not ratings
or application acceptance limits. The tested slow ramp does not establish a
faster maximum slew or loaded response.

Center and wait for measured neutral, disable, remove servo energy, then stop
and confirm not running. Change wiring only with servo energy removed. Repeat
for the remaining labeled servos/channels one at a time.

Return the first MAIN1 table and actual supply voltage before generating a
measured operating profile. No further widening is included in this file.
For installed R10, additionally establish surface zero, required angle range,
clearance, signed vehicle mix and response under the intended load. The current
all-identical fixture is not that operating profile. R09/R10 are now recorded as
operator-reported passes in the linked closeout; detailed numeric calibration and
exact tested-profile data were not supplied. For future measured runs use the
[report form](templates/MANUAL_BENCH_JOYSTICK_TEST_REPORT.md).

## Optional R09 step response: remove the visible software ramp

The operator requests an immediate PWM demand. Use the separately named
[step-response profile](../build/bench_commissioning/bench-e8ea98b404-step3/es3059md-unloaded-step-r09.txt).
Replace only the two rate fields in each wide row:

```text
1250 1500 1750 10 10000 10000 1 1 1
```

Zero is rejected by the profile validator; no slew-off flag exists. The high
finite rates give normalized1000/s. At the minimum due interval20 ms, the allowed
change is20, exceeding the entire normalized span2. Thus the limiter can admit
a full-range reversal in one due control update. This is a model-rate setting,
not a command for10000 physical degrees/s. The50 Hz PWM frame, joystick transport,
scheduled update, IO transfer and servo mechanics still determine actual timing.

The [creation recipe](../build/bench_commissioning/bench-e8ea98b404-step3/es3059md-step-r09-create-console.txt)
writes the same header and four copies of that row to
`/fs/microsd/bench/es3059md-unloaded-step-r09.txt`. Run it with servo energy off
and the module stopped, read back all five lines, then start using that exact path.
Check Ready, actual disarmed/prearmed and centered input before enable. Scope-check
1500 ->1550 ->1500 ->1450 first with energy off, then the wider steps as needed.
At full positive roll the new target remains1750 us/control+0.25; full negative
remains1250 us/-0.25. The change is step timing, not a wider range.

The [preparation receipt](../build/bench_commissioning/bench-e8ea98b404-step3/es3059md-step-r09-preparation-20261020.json)
binds the file, recipe, source/limiter reasoning and existing-native Ready-only
pass. No enabled native run or physical step/latency acceptance is claimed.
For powered measurement keep one servo with linkage detached, verify1500 us before
applying energy, and begin with the small signed steps while recording actual
PWM/angle/current/voltage. Normal disable/expiry already permits a direct neutral
fallback. No firmware or parameter change is required. The slower wide profile
remains available; this step-response fixture does not supply installed calibration.
