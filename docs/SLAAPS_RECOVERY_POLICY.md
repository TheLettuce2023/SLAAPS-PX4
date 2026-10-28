# SLAAPS airborne recovery policy: decisions

Decided 2026-10-25 by the project lead, who is accountable for flight-test safety sign-off.
Source and option analysis: [SLAAPS Airborne Recovery Policy](https://claude.ai/artifact/T2pu5bwyatsrRN7um2amXh).
Implementation status (2026-10-27, second update). Host-verified (Debug/Release/ASan-UBSan); native build and runtime
verification of this increment pending. Not flashed, not bench- or flight-tested.

- **D3/D4** (earlier today, natively verified): canards freeze at the last handled pulses, EDF ramps off at SLA_EDF_FALL,
  hard 60 s limit, only `Recovery` frames admitted. Entered on estimator loss, a worker fault, leaving the SLAAPS mode,
  power or schedule-condition loss.
- **D1** done: `ManualWorker::hold/holdCycle`. Pilot input lost or unusable in Manual, companion lost in Acceleration, or
  an infeasible companion demand (allocator rejection) holds the current attitude with the stabilized controller in hold-only
  mode, in the same session with ordinary frames. EDF holds the current command for SLA_REC_HOLD, then follows the owned
  `EdfOwner::holdTarget` sequence toward SLA_REC_THR at SLA_REC_RAMP, below the line that reaches zero at the D4 limit. The
  lost input never resumes the session. A later fault escalates to D3 with D4 still counted from the hold start. Without
  complete D1 data (`validateRecoveryConfiguration`) command loss falls back to D3. Lead values: hold 2 s, ramp 0.5/s,
  thrust 0 (2026-10-27).
- **D2** done: companion loss always raises a critical QGC alarm and event. The vehicle holds (D1), and when the selected
  pilot input is fresh and centered the supervisor requests SLAAPS Manual from Commander (`VEHICLE_CMD_SET_NAV_STATE`, up to
  10 tries at 100 ms). The ordinary exclusive Acceleration-to-Manual transfer then takes over from the handled hold frame.
  Centered sticks are required because the Manual controller's own entry check rejects off-center sticks.
- **D4**: one 60 s limit across D1, D3 and escalations, counted from the first loss.
- **D6** done: SLA_NET_EN/N/E (outside the vehicle profile) with QGC's own position from FOLLOW_TARGET (QGC Application
  Settings, Stream GCS Position). Distance and bearing to the net appear in the recovery alarm and in `slaaps_flight_status`.
  Operator reference only.
- **Clarification 4** done: during D3 a bank that stops confirming while the other still does is dropped after one maximum
  cycle; the other bank keeps frozen canards. EDF on a dropped bank is assumed off and not relied on.
- **Commander failsafe** done: on a SLAAPS airframe a failsafe never selects a stock flight mode. It keeps the selected mode,
  so the supervisor applies its decided response, or it terminates (`Commander.cpp`, `failsafe_mode`).
- **Launch** done: SLA_FLT_STAGE 0 off, 1 captive (no airspeed gate), 2 launcher. In the launcher stage canards stay at trim
  with non-integrating controller history until the selected raw airspeed reaches SLA_LAUNCH_AS; the gate then latches open.
  A launcher-stage profile without a fresh airspeed source is not admitted.
- **Flight admission**: production now sets the flight flags. `flight_admitted` needs `validateFlightConfiguration` (stage 1
  or 2, complete D1 data, SLA_DATA_STATE Calibrated or Qualified with a record ID) plus the launcher airspeed source when
  staged; Acceleration also needs `validateAccelerationControlConfiguration`. `healthy` needs fresh selected estimator
  metadata, routing, an eligible profile and no registration fault. `measured_condition_valid` needs a valid power sample
  inside SLA_PWR_VMIN/VMAX.
- **D5** done (2026-10-28, natively verified): seamless bank handover, decided by the lead. With SLAAPS Manual selected and
  the returning pilot input fresh, centered and at or below SLA_EDF_ENTRY for 200 ms, during a D1 hold or D3 recovery, the
  supervisor ends the lost session without disabling the profile and prepares a fresh session while armed (preparation
  proposal flag `reentry`). The output driver accepts that proposal while its bank is latched or recovering. Nothing is
  written while preparing, so the hardware keeps the last handled pulses until the fresh session's first frame; no lease is
  renewed. Every admission check of a new session applies. If re-entry fails after the old session ended, outputs stay
  unwritten until disarm (canards hold, EDF receives no commands).

## Decisions

| ID | Situation | Decision |
| --- | --- | --- |
| D1 | Pilot or companion commands lost; estimator and both banks healthy | A: hold current attitude (existing quaternion hold) and last EDF throttle for a bounded time, then ramp to a measured recovery thrust |
| D2 | Companion lost in Acceleration mode | A: automatic handoff to Manual when pilot input is fresh, always with a QGC alarm; otherwise D1 |
| D3 | Attitude loop unusable (estimator invalid or a bank not confirming) | B: canards freeze at last position, EDF ramps down to off |
| D4 | Hold limit and end state | End state is EDF off; EDF is off no later than 60 s after recovery starts; no ceiling from launch |
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
