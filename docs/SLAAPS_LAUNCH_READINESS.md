# SLAAPS launch to stabilized Manual: readiness checklist

Status on 2026-11-18: **launches are not possible with the current firmware.** Wind-tunnel and captive tests (`SLA_FLT_STAGE 1`)
come first. This file lists what must change, what must be measured, and the intended sequence. It is a plan, not an
acceptance record. Calibrated data is not Qualified, and nothing here grants flight permission.

## A. Firmware changes (each needs new tests, then a new image)

Status 2026-11-21: **A1 is implemented** (host tests and image `flight-readiness-20261121-launcher-a1`, not yet run on hardware). A2 and A3
were re-scoped after reading the supervisor: they are far larger than first listed, so the launcher stage now **runs fixed gains**
(`SLA_SCH_EN` must be 0 in the launcher stage; `validateFlightConfiguration` rejects it with `SLA_SCH_EN`). A4 waits for the lead's choices.

| # | Change | Where | Why |
| --- | --- | --- | --- |
| A1 (done) | Let Manual arm in the launcher stage | `ManualAvailability.hpp` (the `Captive` check), supervisor `manualAvailability()`, `manualAvailabilityTests` | Today the launcher stage always returns Denied, so Commander refuses to arm. Add the launcher's own conditions to `ManualAvailabilityState`: airspeed instance configured, a fresh sample inside `SLA_LAUNCH_AGE`, the gate-closed session kept as `Run`. Output permission stays a separate supervisor decision. |
| A2 (deferred) | Schedule tolerates the gate-closed phase | `ManualWorker.hpp` (`begin()` schedule start, `cycle()` gated branch), `ControlSchedule.hpp` | `begin()` needs airspeed in the table and EDF 0, and the gated cycle still calls `_schedule.evaluate()`. At rest the schedule rejects. Plan: while the gate is closed skip the schedule (the nominal corner is already used for the allocation); when the gate opens, start the schedule with the live airspeed and EDF command. |
| A3 (deferred with A2) | Table must cover the gate | `validateFlightConfiguration()` | Reject a launcher profile with the schedule enabled unless `SLA_SCH_AS_LO <= SLA_LAUNCH_AS`, otherwise the first scheduled cycle after the gate opens is outside the table. |
| A4 (decided) | EDF launch command on the operator's stick | none (existing throttle path), `SLA_EDF_RISE` | Section F: full throttle at launch, triggered by the operator pushing the throttle stick (lead's decision 2026-11-21). No firmware change: the stick already feeds the EDF owner during the gate-closed phase. |

Why A2 is larger than listed: besides the worker (`begin()` and the gated `cycle()`), the supervisor requires an in-envelope schedule
from entry onward: the prearm check needs valid sources, the entry probe starts a throwaway schedule at EDF 0 (fails at rest), the
armed "condition lost" D3 loss and the ordinary-disarm capture both need valid sources, and the condition capture rejects negative
indicated airspeed, which a pitot at rest reads about half the time. All of these need gate-closed handling, with new tests.
Until then the launcher stage runs fixed gains chosen for the launch-speed range (see `stability_margin.py`).

Tests to add: host cases for each new rule, a native signed-flight case that arms at 0 m/s with the gate closed, opens the
gate on airspeed, and loses each source after the gate opens. Run the host suite (Debug, Release, ASan/UBSan) and the native
verifier. Keep the existing captive behaviour unchanged and tested.

## B. Parameters and data

- [ ] `SLA_LAUNCH_AS_I`: the airspeed instance (the pitot published by the sensors module; confirm with `uorb top -1 airspeed`).
- [x] `SLA_LAUNCH_AS`: 20 m/s indicated, set by the lead on 2026-11-21 (launch profile C). Record the measurement behind it: canard authority versus indicated
  airspeed with margin over stall, and the EDF reaction torque it must beat. The main file's 12 is an unused placeholder.
- [ ] `SLA_LAUNCH_AGE`: confirm the pitot publish rate supports it.
- [ ] Pitot zero (`SENS_DPRES_OFF`) with the EDF off and the sensor shielded. The raw zero reads 3 to 7 m/s at rest and would open the gate early.
- [ ] Gains at launch speed: the fixed 40 m/s gains give about 9% of design authority at 12 m/s. Either the schedule (after A2/A3, with a corner at or below the launch speed) or a launch-specific gain set.
- [ ] Declare a data revision (rev 3 or later) as Calibrated: `SLA_DATA_STATE 1`, with its entry in `SLAAPS_DATA_RECORD.md`.
- [ ] Measure the EDF reaction torque and take thrust data for the installed curve. Torque measured 2026-11-23: 0.55 N m, constant
  across throttle, above the modeled roll authority at every speed (see section F and the flight-test plan, G1). Thrust data still open.

## C. Intended sequence (after A and B)

1. Ground, disarmed: load the parameter file, then the canard trims, then (if used) the schedule overlay. Apply with `SLA_CFG_APPLY` +1.
   Check `configuration valid=1`, flight available, pitot publishing, `SYS_HAS_NUM_ASPD 0`.
2. Select SLAAPS Manual with the sticks centred. Wait for PREPARED (inside `SLA_MAN_WAIT`). Arm.
3. Gate closed: canards held at their trim, no integration. Pilot input and the estimator are monitored.
4. Launch. When indicated airspeed reaches `SLA_LAUNCH_AS` the firmware logs "launch airspeed reached, attitude authority on",
   opens the gate, and the stick commands body rate.
5. Loss of pilot input, estimator, airspeed or power is handled by the D1 to D6 recovery responses (canards hold, EDF ramps off).
   Auto-disarm is disabled (`COM_DISARM_PRFLT -1`), so nothing disarms by timeout.
6. After landing: disarm, then re-check the profile. The profile is restored after any disarm.

## D. Not defined yet (project lead)

- The launcher mechanism, its release procedure and the abort criteria.
- The `SLA_EDF_RISE` value for a harder step (section F).
- Range, wind and altitude limits for a launch.
- Hardware tests of every recovery path, and what Qualified means for this vehicle.

## E. Order of work

1. Wind tunnel, Manual stabilized, schedule on (airspeed 18 to 40 m/s).
2. Captive tests (tow or rail) at `SLA_FLT_STAGE 1`, to measure the launch-speed authority and the launch threshold.
3. Firmware A1 to A3 with tests, B measured values, then a captive-to-launcher dry run on the ground with the EDF off.
4. Launcher flights only after D is written and the recovery paths are tested on hardware.

## F. EDF launch command (defined by the project lead, 2026-11-18)

**Definition: the EDF goes to full throttle at launch, not a gradual spool.** Full is normalized command 1.0 (`SLA_EDF_BMAX 1`,
the thrust curve's top point 39.2266 N at 42 V).

What the existing firmware does with that, from the parameter file:
- **Entry and arming.** Manual entry needs the throttle at low (`SLA_EDF_ENTRY 0.05`), so the EDF starts at 0. A step to full has to come after arming.
- **The rate limit makes it a 0.4 s ramp, not a jump.** `SLA_EDF_RISE 2.5` per second limits the command rise, so 0 to 1 takes 0.4 s.
  A true step needs a higher `SLA_EDF_RISE` (range 0 to 100 per second), a validation pass, and a check that `SLA_SCH_E_TRAN` and
  `SLA_SCH_E_RATE` (schedule) stay above it. Recovery is unchanged: `SLA_EDF_FALL 10` per second down, `SLA_REC_RAMP 0.5` per second.
- **Trigger.** Today the only path is the pilot pushing the throttle stick to full (`SLA_THR_LOW -1` to `SLA_THR_HIGH 1`). Automatic
  full throttle on release detection does not exist and would be new firmware (A4).

Modeled consequences (estimates from the file values, not measurements):
- Thrust to weight is about 2.0 (39.2 N against 1.979 kg x 9.81 m/s^2 = 19.4 N; weighed 2026-11-23).
- With the 0.4 s rise and a thrust-only estimate near 19 m/s^2, 12 m/s takes roughly 1 s. The canards are held at trim for that
  time because the gate is closed.
- **The EDF reaction torque is 0.55 N m, constant across throttle** (measured 2026-11-23). With the gate closed the canards sit at
  trim, so this torque rolls the vehicle unopposed. With the gate open the modeled roll authority is only 0.069 N m at 25 m/s and
  0.176 N m at 40 m/s. The torque has to be countered, or the authority shown larger, before any launch.
- Loss of control while the gate is closed still ramps the EDF off at the recovery rate, which is slower than a step on.

Open decisions: pilot-stick or automatic trigger, the `SLA_EDF_RISE` value, and the limit on the gate-closed time.

Decision 2026-11-21: **the operator triggers the EDF launch command with the throttle stick.** The operator keeps the stick low through
Manual entry and arming (entry needs it below `SLA_EDF_ENTRY`), then pushes it to full at release. The EDF owner shapes the command by
`SLA_EDF_RISE` (2.5 per second today, so 0.4 s to full); raising it for a harder step is the lead's decision after thrust-stand tests.
During the gate-closed phase the canards stay at trim, so the operator's throttle push is the only thing acting until the airspeed gate.
If pilot input is lost before the gate opens there is no flight to hold: the worker aborts with a D3 recovery (EDF ramps off at
`SLA_REC_RAMP`). Launch profile C (`slaaps_launch.params`, data revision 6, pending the lead's declaration) carries the launcher stage,
the airspeed source and fixed gains at 25 m/s indicated.
