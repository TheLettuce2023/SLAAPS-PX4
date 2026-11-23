# SLAAPS gate-closing bench session

Written 2026-11-23 for one bench session that closes as many flight-test plan items
([SLAAPS_FLIGHT_TEST_PLAN.md](SLAAPS_FLIGHT_TEST_PLAN.md)) as the bench allows. Image `flight-readiness-20261123-evidence`
or later: it logs pilot input, outputs and recovery states at 50 Hz and prints each bank's activation reply time into the
log. Evidence comes from the logs: `python Tools/slaaps/bench_evidence.py <log folder>` (needs `pip install pyulog`).
It reports; it closes nothing. The lead closes each item.

**Safety for every armed step:** vehicle restrained, EDF throttle at the bottom (1000 us is the stop pulse, so the EDF does
not turn), intake and exhaust clear, a hand on the pack disconnect. Nothing here needs throttle above zero.

## 0. Before arming (each closes an item or sets a value)

| Step | Command or action | Closes |
| --- | --- | --- |
| Joystick rate | `uorb top manual_control_setpoint` for 10 s with the QGC joystick connected; note the rate | G0 joystick item, if the rate is well above 1 / `SLA_IN_AGE` (3.3 Hz) |
| Airspeed instance and rate | `uorb top -1 airspeed` and `listener airspeed` | G3 `SLA_LAUNCH_AS_I` (the instance), `SLA_LAUNCH_AGE` (above the publish interval) |
| Pitot zero | EDF off, pitot shielded from draughts, QGC airspeed calibration (sets `SENS_DPRES_OFF`); then `listener airspeed` reads about 0 | G1 pitot zero (the reference check stays open) |
| Mass and CG | Weigh the vehicle as flown; find the balance point and measure it from the tail end | G1 CG and mass. Done 2026-11-23: 1.979 kg and 0.233 m, from the data sheet (data revision 7) |

## 1. Armed Manual soak (G0 control-lost count and timing windows)

1. Reboot (always after any Bench session). Load or check the profile: `SLA_CFG_KIND 1`, raise `SLA_CFG_APPLY`.
   `slaaps status`: `configuration valid=1`, `routing_fault=0`, `power ready=1`.
2. Select SLAAPS Manual (`commander mode ext1`), sticks centred, throttle at the bottom. Arm from QGC.
3. Move the sticks gently through their range now and then (canards respond); keep the throttle at the bottom.
4. Stay armed as long as practical. Every armed minute with the fault record counts toward the lead's 30 minutes.
   Disarm and re-arm a few times: each entry logs one `SLAAPS: activation flight ... reply main=... aux=...` line.
5. Any "SLAAPS: control lost" message: note the time, keep going if safe, send the log.

The script then prints the armed Manual minutes, every control-lost event with its fault record, and the largest MAIN and
AUX reply times, from which the timing windows (`SLA_ACT_ACK_US`, `SLA_CYCLE_MAX`, `SLA_OUT_LEASE`) are set and committed.

## 2. Fault injection, EDF at zero (G2)

Start each from armed SLAAPS Manual, running (`manual entry=12`). Log the start time of each trigger in a note.

| Response | Trigger on the bench | Expected in the log |
| --- | --- | --- |
| D1 pilot loss | Disable the joystick in QGC (or unplug the gamepad) | `recovery_state` 0 to 1 about `SLA_IN_AGE` - 2 x `SLA_CYCLE_MIN` (280 ms with 300 / 10 ms) after the last pilot sample; canards hold; after `SLA_REC_HOLD` (2 s) the EDF command ramps to off |
| D5 re-entry | After D1, re-enable the joystick: sticks centred, throttle at the bottom | Recovery ends, Manual resumes with a fresh session |
| D3 mode left | Switch QGC to another flight mode | `recovery_state` 2, canards frozen, EDF ramps off |
| D3 power lost | Unplug the main pack with the board on USB power | `recovery_state` 2, `fault_reason` 2 (power lost) |
| Termination (do it last) | `commander termination`. It always sends termination on (the `off` argument is ignored) and PX4 latches it until a reboot | Outputs go to their failsafe values: canards no pulse, EDF 1000 us. Reboot afterwards |
| Estimator loss, bank loss | Not triggered on the bench | Covered by the native suite only; say so in the G2 record |

Pass criteria (response time, expected states) are the lead's to set; the script prints the measured time from the
preceding input, mode or arming change to each recovery change, and how long the EDF output took to reach the stop pulse.

## 2b. Manual right after a Bench session (G0: "Manual fails after Bench until a reboot")

Run an EDF Bench session (throttle at zero is enough), end it, then **without rebooting** set `SLA_CFG_KIND 1`, raise
`SLA_CFG_APPLY`, select SLAAPS Manual and arm. Since 2026-11-23 the native suite runs this sequence and reaches Running, so
the 2026-11-18 failure (`entry rejected at line 1810 mask 0x900000d5`, then 60 ms `SLA_CYCLE_MAX`) does not reproduce in
simulation. If it fails on the board, send `slaaps status` (its `last rejection line` and `mask`) and the log: from image
`bc0868faba`'s successor on, the worker's first-cycle guard reports mode, binding and deadline on separate lines.

## 3. If a fan is available (G3)

Launch profile C loaded and declared, airspeed instance set. Arm in the launcher stage (EDF at zero), push airflow through
the pitot past 20 m/s indicated, and see `launch airspeed reached, attitude authority on`. `slaaps_flight_status`
(`launch_gate_closed`, `launch_airspeed_m_s`) is logged at 50 Hz.

## Send back

The ulogs, the `uorb top` and `listener` outputs from step 0, the mass and balance-point numbers, and the trigger times
from step 2.
