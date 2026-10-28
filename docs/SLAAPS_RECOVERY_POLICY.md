# SLAAPS airborne recovery policy: decisions

Decided 2026-10-25 by the project lead, who is accountable for flight-test safety sign-off.
Source and option analysis: [SLAAPS Airborne Recovery Policy](https://claude.ai/artifact/T2pu5bwyatsrRN7um2amXh).
Implementation status (2026-10-27): D3/D4 library layer done and host-tested (not called by the supervisor yet):
`EdfOwner::recover/abandon` (EDF ramp at SLA_EDF_FALL, hard 60 s ceiling), `ManualOutputBank` recovery (canards hold at the
last handled pulses, only `Recovery` frames admitted, bank enforces the ceiling itself), wire value `EDF_RECOVERY = 3`,
`ManualWorker::recoverCycle`, bank/worker transport recovery states. Supervisor wired (`Slaaps::updateRecovery`): a worker fault after a handled frame holds recovery while armed with no
kill/lockdown/termination, publishes a non-granting armed permission, logs a critical QGC message; disarm or any integrity
loss still cancels. Compiles/links native and 6X (image 1,948,260 B, 17.4 KB free); native supervisor run verifies recovery
frames after an Acceleration allocator rejection, held canards, non-rising EDF and disarm ending recovery.
An allocator rejection now enters D3 recovery as a stand-in until D1/D2 exist. Leaving the SLAAPS mode (pilot or
failsafe) and power-source loss while armed now also enter recovery, verified natively through the real driver path
(recovery frames acknowledged by both banks, canards frozen, EDF ramped off, critical QGC message, disarm ends it).
The driver-side `SlaapsOutputPreparation` now hands a lost grant to the bank's recovery instead of revoking the bank.
Estimator loss also enters recovery (verified natively, three consecutive supervisor runs). Its last frame can expire
first (estimator-capped lease), so a bank may recover before the supervisor notices; a recovering bank therefore never
re-enters the driver's granted flow, and a pre-loss normal frame or ticket arriving after recovery starts is ignored,
never applied or acknowledged. Pilot input loss also enters D3 recovery (stand-in until D1). Not done: D1, D2, D5, D6,
Commander failsafe path, a failed bank that carries the EDF channel. Until all land and are verified, `flight_admitted` stays false.

## Decisions

| ID | Situation | Decision |
| --- | --- | --- |
| D1 | Pilot or companion commands lost; estimator and both banks healthy | A: hold current attitude (existing quaternion hold) and last EDF throttle for a bounded time, then ramp to a measured recovery thrust |
| D2 | Companion lost in Acceleration mode | A: automatic handoff to Manual when pilot input is fresh, always with a QGC alarm; otherwise D1 |
| D3 | Attitude loop unusable (estimator invalid or a bank not confirming) | B: canards freeze at last position, EDF ramps down to off |
| D4 | Hold limit and end state | End state is EDF off; EDF is off no later than 60 s after recovery starts (no ceiling from launch) |
| D5 | Regaining control in flight | B: re-entry into Manual is allowed when the lost input returns and every admission check passes again |
| D6 | Recovery hardware | Net landing area near the ground control station; its position is set in QGC relative to the GCS location |
| Launch | Entering flight | Captive (tethered/rig) powered tests first, then a launcher/rail |

The lead reports that all measurements in the policy doc have been taken and asked for a document to
implement them. The measured values are not yet in the repository.

## What each decision requires in firmware

- **D1:** replace the bare `RecoveryRequired` latch for command loss with an owned sequence in
  `EdfOwner` and the output banks: attitude hold, bounded EDF hold, then ramp to recovery thrust.
  Hold time, ramp rate and recovery thrust are measured-provenance parameters; while unset, flight
  admission is refused.
- **D2:** on companion loss in Acceleration, use the existing exclusive Acceleration-to-Manual transfer
  when selected pilot input is fresh. Emit a critical QGC-visible alarm (`mavlink_log_critical` plus an
  event) on every companion loss, whether or not the handoff succeeds.
- **D3:** on estimator loss or a non-confirming bank, stop closed-loop commands, keep canards at their
  last handled position and ramp EDF to off through the common owner.
- **D4:** enforce a hard 60 s limit from the start of recovery, ending in EDF off, across every recovery
  path. Any recovery hold is clipped by it. There is no flight-time ceiling from launch.
- **D5:** add an explicit armed re-entry path into Manual from a recovery state. It starts a fresh
  session with full admission checks; it never revives a lease or the old session. Kill, lockdown and
  termination remain non-revivable.
- **D6:** add QGC-editable parameters for the net position relative to the GCS location, plus the source
  of the GCS location.
- **Launch:** captive tests run with flight flags off. Launcher entry gives the attitude loop authority
  only above a measured minimum airspeed.
- **Commander failsafe:** close the automatic `modeFromAction` path so failsafes map to these responses or
  termination, never a stock mode.

## Clarifications (answered 2026-10-25 by the project lead)

1. **D4:** the 60 s EDF-off limit counts from the start of recovery, not from arming or launch release.
2. **D6:** no active steering toward the net. The net position is a reference for the operator only; no
   guidance, position control or geofence is implied.
3. **D6:** the ground-station location is QGC's own position, received over MAVLink.
4. **D3:** if the failed bank carries the EDF channel, treat it as a pure servo recovery and assume zero EDF
   thrust. No EDF ramp-down is attempted or relied on.
5. **Measurements:** compiled in `src/lib/slaaps/Master_Vehicle_Data_Sheet_Slaaps.xlsx`; the data
   compilation document maps each value to its SLAAPS parameter.

6. **D4 (follow-up, 2026-10-26):** there is no 60 s ceiling from launch. The limit applies only to recovery.
   Update the D4 table row accordingly when implementing.

## Vehicle facts recorded 2026-10-26

- The EDF is driven by DShot600, not PWM. The firmware's routing supports only PWM
  (`OutputProtocol { Pwm, Unsupported }` in `ActuatorRouting.hpp`), so the EDF channel is rejected as
  `UnsupportedProtocol` today. DShot support in routing, EDF command conversion and the output banks is
  required before any EDF operation, including recovery.
- Rear fins exist and are static. The firmware should model their stabilizing effect.
