# Manual servo bench joystick test report

Use with the [operator run sheet](../MANUAL_BENCH_JOYSTICK_RUN_SHEET.md). Copy this
form to a new run folder. All results start **not run**; write pass, fail or
incomplete only from the associated measurements. Keep signal and powered
results separate.

## Run identity and declared limits

- Run ID and UTC date:
- Operator and person at independent stop:
- Reviewer and review date:
- Board model and GUID:
- Firmware filename, SHA-256 and upload receipt:
- Runtime hash and build date/time:
- QGC build, gamepad, calibration/settings evidence:
- Profile path, actual card-readback file and SHA-256:
- Parameter export and SHA-256:
- Connected power paths, wiring photograph and EDF isolation:
- Servo signals and energy disconnected for R01 through R08:
- Scope model/probe/sample rate/settings/uncertainty:
- Allowed pulse-width error, period range, jitter and startup transients:
- Allowed fault-to-output response, timing reference and measurement uncertainty:
- Declared dwell and repetitions:
- For powered tests: voltage/current/temperature/angle/clearance/slew limits:
- Independent stop action, measured energy-removal result and restoration plan:
- R09 source method and tested scope: independent PWM tester, or bounded
  scope-referenced controller characterization with one unloaded servo:
- R09 characterization profile/hash and changed-profile pin/slew check:

## Reports and results

| Report | Result | Evidence paths and hashes | Reviewer and UTC date | Deviations or limits |
| --- | --- | --- | --- | --- |
| R01 Configuration and identity | Not run | | | |
| R02 Profile and Ready baseline | Not run | | | |
| R03 Input and real permissions | Not run | | | |
| R04 Enabled neutral pins | Not run | | | |
| R05 Joystick axis signals | Not run | | | |
| R06 Normal disable and direct stop | Not run | | | |
| R07 Input loss and explicit recovery | Not run | | | |
| R08 Signal run closeout | Not run | | | |
| R09 Measured calibration and power review | Not run | | | |
| R10 Powered joystick test | Not run | | | |

## PWM measurements

For each row/repetition retain raw scope exports plus separate input/command/IO
timestamps. Add intermediate, deadzone, combined-input and throttle cases.
Expected values below apply only to the synthetic USB fixture; replace them for
the measured installed profile. Record actual axis values rather than assuming
the stick reached exactly +/-1.

| Case | Input timestamp and R P Y T | Session | Expected MAIN1..4 us | Measured MAIN1..4 us | Period and jitter | AUX4 result | Raw file and result |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Center | | | 1500 1500 1500 1500 | | | | |
| Roll +1 | | | 1600 1400 1400 1400 | | | | |
| Roll -1 | | | 1400 1600 1600 1600 | | | | |
| Pitch +1 | | | 1600 1600 1600 1400 | | | | |
| Pitch -1 | | | 1400 1400 1400 1600 | | | | |
| Yaw +1 | | | 1600 1600 1400 1600 | | | | |
| Yaw -1 | | | 1400 1400 1600 1400 | | | | |
| Throttle only with R P Y centered | | | 1500 1500 1500 1500 | | | | |

## Loss and recovery observations

- Exact injection method and power paths retained:
- Status/session and real permissions before event:
- Event reference and last identified delivered input:
- First input rejection, disabled command and changed pin waveform:
- Measured response and declared limit, including uncertainty:
- State after restoring transmission, with no new enable:
- Explicit stop/start/centered-enable and new session:
- Distinct raw captures for disable and direct stop from Active:
- Remaining G4 cases: boot/restoration, bad profile/uncentered entry, source change,
  permission loss, owner stall, FMU/IO communication failure, routing exclusion:
- Reviewed injection methods and any open coverage:

## Actual servo calibration and powered movement

Complete only for the later reviewed powered setup. Record physical angle signs,
units, supply/rate ratings and the full installed profile alongside this table.
For the current ES3059MD/no-tester setup use the
[bounded R09 procedure](../MANUAL_BENCH_ES3059MD_R09_R10.md); a fixture model angle
is not a measured physical angle. Identify bare-servo versus installed loaded
results explicitly.

| Channel and servo model | Negative neutral positive PWM us | Measured angles and direction | Rise and fall slew limits | Supply voltage and current | Clearance and uncertainty | Evidence and review |
| --- | --- | --- | --- | --- | --- | --- |
| MAIN1 | | | | | | |
| MAIN2 | | | | | | |
| MAIN3 | | | | | | |
| MAIN4 | | | | | | |

For each servo retain the underlying measured points; add rows and repetitions
as needed. Record both approach directions and the independent angle reference.

| Servo and approach direction | Target PWM us | Measured PWM us | Measured angle deg | Actual rail V | Current A | Delay settling clearance | Raw evidence |
| --- | --- | --- | --- | --- | --- | --- | --- |
| | | | | | | | |
| | | | | | | | |
| | | | | | | | |

- Installed profile/hash, mix review and repeated disconnected PWM results:
- Startup movement, independent stop and power-restoration behavior:
- Small signed joystick steps and return-to-neutral angle/time:
- Multi-channel load/dwell, rail droop, temperature and runtime resources:
- Final owner state, actual armed/prearmed state and servo energy removed:

## Closeout

- Signal-only result and incomplete cases:
- Powered result if separately performed:
- Deviations, corrective action and required repeat tests:
- Raw evidence index and SHA-256 list:
- Final `research_bench_control status`, actual permissions, IO and dmesg paths:
- Reviewer disposition and conditions for the next operation:

This report records this exact configuration and measured scope. It does not
complete wind or stabilized-control acceptance.
