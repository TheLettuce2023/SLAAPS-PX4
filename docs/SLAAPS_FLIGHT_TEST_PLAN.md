# SLAAPS flight-test plan without the wind tunnel

Status 2026-11-21: draft for the project lead. Tunnel access has expired, so authority and stability evidence has to come from other
tests. This plan lists gated stages. Each gate has an entry condition, the tests, and the evidence that closes it. **Thresholds marked
[lead] are decisions for the project lead; nothing here is accepted until the lead signs it.** Calibrated data is not Qualified, and
passing a gate grants no flight permission by itself.

What the tunnel would have given, and what replaces it:

| Tunnel product | Replacement |
| --- | --- |
| Canard authority versus airspeed | Vehicle-mounted test on a closed road or runway (G4), then doublets in flight (G5) |
| Closed-loop stability and tracking | SITL with the identified model, plus `Tools/slaaps/stability_margin.py`, plus doublets in G4 and G5 |
| A free-pivot stability demonstration | Not replaced. The first launches carry this risk; keep gains conservative (G5) |

## First-flight go/no-go (lead, 2026-11-23: fly within a day)
Only items that could lose the vehicle gate the first flight. Everything else below is deferred, not closed.

Must close:
1. **Hold-start fault (G0):** mitigated, not fixed, by `SLA_IN_AGE` 500 ms in profile C (data revision 11). The 33-minute SiK soak
   would have had no holds; its largest gap was 336 ms against the new 480 ms trigger.
2. **Final profile on the board:** `slaaps_vehicle.params` (revision 9, with the approved timing windows), then the canard trims, then
   `slaaps_launch.params` (revision 11). Declare (`SLA_DATA_STATE 1`), raise `SLA_CFG_APPLY`, reboot. Always reboot after any Bench session.
3. **Closed-loop sign check:** armed in Manual, tilt the vehicle by hand in roll, pitch and yaw; the canards must deflect to oppose
   each tilt.
4. **Airspeed gate:** arm in the launcher stage, push the pitot past 20 m/s with a fan, see "launch airspeed reached, attitude authority
   on" and the canards respond. Zero the pitot on site first.
5. **10-minute armed soak over SiK on the final profile:** zero holds and zero control-lost events (`bench_evidence.py`).
6. **Lead's launch decisions:** launcher, release and abort criteria; net; range, wind and altitude limits; range-safety approval.

Deferred, with the risk accepted for a short first flight in Manual: the hold-start root cause (a real dropout over 480 ms can still
reach it); thrust curve and ESC endpoint calibration (full power at about 72% stick); barometer bias under power (height only);
inertia; pitot reference check; the deliberate G2 set (D1, D3 and D5 already seen on the board); the G4 road test; roll before the
gate opens (no sustained spin on the stand).

## Progress 2026-11-23
- EDF moved to PWM 400 Hz on AUX1 (1000-2000 us, zero throttle = stop); the Lucid never armed on DShot600 under the SLAAPS
  image. It now arms while disarmed (commits 29dd65208c, 0a911aa2e5).
- EDF Bench ran cleanly through full stick after the full-stick fix (commit fe2cc90837): QGC full stick arrived as throttle
  1.000000119 and the strict [-1, 1] check cancelled the session. Manual uses the same input path.
- Battery warning cleared with charged packs (`power ready=1` at 41.9 V).
- Output wiring values now live in `Tools/slaaps/parameters/slaaps_outputs.params`; a parameter reset is restored from files.

## G0. Software defects closed (before any hardware test that matters)
- [ ] "Control lost" events root-caused and removed. Build `flight-readiness-20261121-faultdiag`, run on the bench, read `fault_site`,
  `fault_core_line` and the margins from `slaaps_flight_status`, fix, and show zero events over [lead] armed minutes (proposed: 30).
  The diagnostic is in every image since commit 73a739b5de.
  **Armed Manual soak 2026-11-23** (image 31706e0c6d, `test_data/Logs/30 min run/log_53_2026-11-23-13-11-20.ulg`): 32.3 armed Manual
  minutes, no disarm, and **one control-lost event** at 1415.2 s. It recovered by itself, re-entering Manual 0.26 s later, and was not
  noticed at the bench. Cause: EKF2 reset its attitude (`quat_reset_counter` 2 to 3) when it switched from magnetometer heading fusion to
  3D magnetometer fusion (`cs_mag_aligned_in_flight` 0 to 1). That was a yaw correction of about 3 degrees. The stabilized controller's
  state check treats any attitude reset as a changed source (`StabilizedInputValidation.cpp`, `StateChanged`), so the worker rejected
  the cycle (core line 241, worker status 6) and D3 fired. Pilot age 20 ms, state age 5 ms, margins normal. EKF2 resets like this
  also happen in flight, so this is a real defect, not a bench artifact. Not met: zero events needed.
  Fixed 2026-11-23 (lead's decision): the Manual controller, including every D1 hold, accepts an attitude reset within the
  same estimator epoch. It re-bases the hold reference so the hold error is unchanged and the reset commands no correction.
  A new estimator epoch still faults. Acceleration mode, the companion link and the pre-arm check stay strict: the companion
  sends attitude in the estimator's frame, so a reset there needs companion-side handling first. Native tests cover the
  controller, the worker and an armed supervisor session. Next: a new image and another 30-minute soak, with a few
  disarm/re-arm cycles for the timing windows.
  **Second soak, started 2026-11-23** (image 3d7c2a7e01, `log_62_2026-11-23-18-11-08.ulg`): disarmed after 2.7 armed Manual minutes,
  so it does not count as the soak. No control-lost event. EKF2 reset the attitude at 44.5 s (`quat_reset_counter` 2 to 3) and Manual
  kept running: the first board confirmation of the fix. The run stopped after a pilot-input loss (D1, see the joystick item), not a
  control-lost fault. The 30-minute soak is still needed.
  **Third soak 2026-11-23** (image 3d7c2a7e01, joystick over SiK, `log_63_2026-11-23-19-51-28.ulg`): 32.7 armed Manual minutes, no
  disarm, **two control-lost events**, both a new defect (next item). No attitude reset happened during the run, so the reset fix was
  not exercised again. Not met.
- [ ] **D1 hold start can miss its output deadline (found 2026-11-23, log 63).** Three D1 holds (75.9, 280.0, 677.3 s), each 281 to
  291 ms after the last pilot sample. In two of them the banks wrote the first hold frame (a repeat of the last handled canards)
  and then nothing for 44 to 50 ms. The worker's next frame never went out, its last deadline passed, and it faulted: worker status
  9 (output rejected), `ManualWorker.hpp` line 620 (`now >= _deadline` in `holdCycle`), output margin -7.5 ms and -0.2 ms, state
  age 2.6 and 5.2 ms, last good cycle 10 ms before. D3 then froze the canards with the EDF at stop, and D5 re-entered Manual
  0.9 s and 0.16 s later. Bank outputs never pause over 35 ms anywhere else in the 33 minutes, except at the three re-entry
  handovers. In all three holds the pilot input came back 5 to 14 ms after the hold began, so that alone does not explain it.
  In log 62 a hold with no input for 5 s ran cleanly. Not yet known: whether the worker stopped staging hold frames (waiting for
  an acknowledgment, or not due) or a bank refused the second hold frame. Next: reproduce in the native supervisor suite with the
  board's timing, then fix. The same fault would follow any short pilot dropout in flight, so it blocks G0.
- [x] Timing windows final and committed: `SLA_ACT_ACK_US`, `SLA_CYCLE_MAX`, `SLA_OUT_LEASE` (the board still carries temporary 120 ms / 200 ms / 200 ms).
  **Approved by the lead 2026-11-23: 30000 us / 60000 us / 100 ms**, the values committed in `slaaps_vehicle.params` (unchanged, so no
  new data revision). They pass every timing rule: activation reserve 30 + 20 = 50 ms, below `SLA_OUT_LEASE` 100 ms, `SLA_IN_AGE`
  300 ms and `SLA_ARM_AGE` 800 ms; ACK 30 ms below `SLA_CYCLE_MAX` - `SLA_FIRST_CMD_US` = 40 ms. To do on the board: load the file
  (replacing the temporary 120 / 200 / 200 ms) and run the Manual-after-Bench check (session card step 2b) first. The 2026-11-18
  Manual-after-Bench failure ended in a 60 ms `SLA_CYCLE_MAX` rejection, and the temporary values were set after it.
  The armed-Manual bench test reports the real bank reply times (`activation flight reply main=... aux=...`). Soak 2026-11-23: one
  logged activation (the re-entry), MAIN 6.2 ms and AUX 3.3 ms against the 120 ms limit. The first entry came before the log started,
  so the sample is thin: disarm and re-arm a few times in the next run. Log 62 adds two: MAIN 7.2 / AUX 10.7 ms at the first entry
  and MAIN 8.6 / AUX 6.4 ms at the D5 re-entry. Log 63 adds three re-entries: MAIN 4.0 / 5.5 / 11.2 ms, AUX 8.9 / 8.7 / 8.8 ms.
  **Six activations, largest MAIN 11.2 ms and AUX 10.7 ms.** Proposed final values: the parameter file's `SLA_ACT_ACK_US` 30000
  (2.7 times the largest reply), `SLA_CYCLE_MAX` 60000 and `SLA_OUT_LEASE` 100, which already satisfy the timing rules
  (ACK + FIRST_CMD 50 ms < CYCLE_MAX 60 ms; CYCLE_MIN 10 < FIRST_CMD 20 < CYCLE_MAX). Loading `slaaps_vehicle.params` puts them on
  the board in place of the temporary 120 / 200 / 200 ms. For the lead to approve.
- [ ] Joystick link fixed (observed 4.8 Hz) or the input age and activation reserve proven to cope with it. Likely closed: the 4.8 Hz
  matches the logger's 200 ms sampling of `manual_control_setpoint` (`logged_topics.cpp`, default profile), not the link. Confirm the
  live rate with `uorb top manual_control_setpoint`. Measured on the board 2026-11-23: 24 Hz (about 42 ms between samples), about
  seven samples within `SLA_IN_AGE` 300 ms. **Reopened 2026-11-23: that rate was over USB.** In log 53, QGC was on the USB port
  (MAVLink instance 2), with 41 ms between samples and a largest gap of 144 ms. In log 62 the joystick came over the SiK radio on
  TELEM1 (57600 baud, instance 0): samples arrive in bursts, with 97 gaps over 150 ms and 10 over 200 ms (largest 240 ms) in 2.7
  minutes. D1 fires about 280 ms after the last sample, so the margin is about 40 ms. At 124.3 s the input stopped for 5.0 s. Both
  radios reported a strong link throughout (RSSI 118-166 at both ends, no receive errors, free transmit buffer), but QGC's
  heartbeat and nearly all uplink bytes stopped. So the ground side (QGC or the laptop's USB link to the radio) stalled, not the
  air link. The supervisor responded as designed: D1 hold 293 ms after the last sample, EDF already at the stop pulse, input back
  at 129.27 s, D5 re-entry with a fresh session at 129.47 s. Commander also raised its RC-loss failsafe (`NAV_RCL_ACT` 2). On a
  SLAAPS airframe that changes no mode (`Commander.cpp`, `failsafe_mode`), and the QGC indication stays on until disarm because PX4
  clears that failsafe only on a mode change or disarm. **The flight pilot link is the SiK radio (lead, 2026-11-23).** To close: show
  that the SiK joystick path has gaps well under 280 ms and no ground stalls over a full soak, or change `SLA_IN_AGE` [lead].
  **Full SiK soak 2026-11-23 (log 63, 32.7 minutes):** 21.1 Hz, no ground stall (QGC heartbeat never missing, uplink at least
  317 B/s), RSSI at least 104 at both ends, 2 receive errors. Gaps over 200 ms: 300; over 250 ms: 7; over 280 ms: 4; largest 336 ms.
  So at `SLA_IN_AGE` 300 the link itself triggers D1 about once every 10 minutes (3 holds in this run). The link is not fixed by
  itself. Options for the lead: `SLA_IN_AGE` 500 ms (D1 about 480 ms after the last sample; zero holds in this soak, 144 ms over
  its largest gap), at the cost of a later D1 and a longer-lived pilot EDF request; or a faster SiK setting (air rate, packet
  size, ECC off) and another soak.
- [ ] Logger stack warning removed; logging rates raised for the tests below (canard commands at 50 Hz or faster). Still seen
  2026-11-23: `log_writer_file low on stack! (296 bytes left)`. Soak 2026-11-23 on image 31706e0c6d (larger writer stack, 50 Hz evidence
  topics): no stack warning in 32 minutes, `actuator_outputs` at 50 Hz on both banks, 3 dropouts totalling 59 ms. Ready for the lead to close.
  Log 63 (33 minutes) agrees: no stack warning, 4 dropouts.
- [ ] The image on the board is built from a committed tree; hash recorded. Partly: the board image (`flight-readiness-20261123-axis-bound`)
  has the sources of fe2cc90837, but was built from a working copy, so `ver all` shows the parent hash. One clean rebuild closes it.
  2026-11-23: the soak log records `ver_sw` 31706e0c6d39ed6ed0386974cbe61b13dad52fef, the clean build of a committed and pushed tree.
  Ready for the lead to close.
- [ ] Manual entry after a Bench session fails until a reboot (`entry rejected at line 1810`, since 2026-11-18); root cause not found.

## G1. Ground measurements
- [ ] EDF thrust curve and **reaction torque** on a thrust stand with a torque arm, at several throttle values. Record the supply current against the power module limit.
  Reaction torque measured 2026-11-23 (lead): **0.05 N m in magnitude, constant across throttle** (first reported as 0.55, corrected
  the same day). The thrust curve is still open.
  **Powered EDF on the stand, 2026-11-23** (`test_data/Logs/Launch Bench Unrestrained/log_61_2026-11-23-17-39-38.ulg`, image
  31706e0c6d, EDF Bench mode, vehicle nose up, canards at trim, `SLA_EDF_BMAX` 1):
  - Supply current (steady segments, 10S pack): 1253 us 5.7 A; 1545 us 11 A; 1720 us 43.4 A (1.61 kW); 1880 us 47.0 A; 1920 us
    42.6 A; 2000 us 41.6 A. Peak 54.1 A and 2.0 kW; the pack sagged from 40.3 to 35.9 V. That is within the ESC's 90 A and the
    PM08-CAN's 70 A average. **The supply-current part closes**, for the lead.
  - **The ESC reaches full power by about 1720 us:** current does not rise above it, so the top 28% of the command does nothing.
    The thrust curve (`SLA_EDF_T0..T4`, linear 0 to 39.2 N over the full command) cannot be right as it stands. Calibrate the
    Lucid's throttle range to 1000 to 2000 us, or measure thrust against command as it is.
  - No thrust number: the measured acceleration stayed near 1 g throughout, so the stand carried the force. When the EDF cut at
    26.3 s the vehicle fell freely for about 0.1 s and landed on the stand (100 m/s^2 spike), so it had been held from above.
  - Roll about the thrust axis: within 6 degrees over the whole run, mean 0.3 deg/s at 1920 us (21 to 25 s). Throttle steps
    gave short spikes up to 116 deg/s that settled within about 0.3 s, likely the fan speeding up or slowing down. Whether the
    stand let the vehicle roll freely is not known from the log; if it did, the steady torque was far below 0.05 N m.
  - **The EDF's suction reaches the barometer:** pressure fell 88 Pa while it ran, and the estimator's height rose 9.4 m with the
    vehicle still on the stand. In flight, that would bias the height estimate by the same amount when the EDF is at full. New G1
    item below.
  Against the roll authority (`SLA_R_M_SCALE`, which scales with the square of airspeed), with isolated canards and with the
  body included (below):

  | Airspeed | 20 m/s (gate) | 25 m/s (profile C) | 32 m/s | 40 m/s |
  | --- | --- | --- | --- | --- |
  | Roll authority, isolated canards (revisions 7 and 8) | 0.044 N m | 0.069 N m | 0.113 N m | 0.176 N m |
  | Torque as a share of it | 114% | 73% | 44% | 28% |
  | Roll authority with the body (x 2.95, revisions 9 and 10) | 0.130 N m | 0.203 N m | 0.333 N m | 0.520 N m |
  | Torque as a share of it | 38% | 25% | 15% | 10% |

  The isolated-canard model reaches the torque only near 21.3 m/s; with the body, near 12.4 m/s.

  **Body correction (2026-11-23, lead: account for body lift; body diameter 85 mm at the canards).** The derivatives of 2026-10-27
  treat each canard as an isolated panel (aspect ratio 1.2, slope 1.742 /rad, `PROJECT_CONTEXT.md`) and put its force 40 mm from
  the axis, which is about the body surface (42.5 mm). With the body, by slender-body theory (Pitts, Nielsen and Kaattari):
  - The body is a wall at the canard root, so opposite canards act as one aspect-ratio-2.4 wing: slope 2.94 /rad, x 1.69 (all axes).
  - Pitch and yaw: the body's upwash on the canards and the lift the canards carry onto the body give k_W(B) + k_B(W) = K_W(B) =
    1.36 at a/s = 0.411 (radius 42.5 mm, canard span 60.8 mm). Total **x 2.30**, close to the unconfirmed tunnel hint of
    about 3 (revision 5 record), which came from pitch.
  - Roll: body lift adds nothing. Pressure on a round body acts through its axis, and the carried-over lift of an antisymmetric
    deflection cancels. The wall effect does apply, and each canard's force acts near 42.5 + 0.45 x 60.8 = 69.9 mm from the axis, not
    40 mm (x 1.75). Total **x 2.95**.
  - Not included: the canards' trailing vortices on the rear fins, which roll the vehicle against a canard roll command and on
    canard-controlled finned bodies can cancel much of it at low speed. Only a roll test settles this (G4 roll doublets).
  These are modeled factors, not measurements. Drafted as data revisions 9 (base) and 10 (profile C), pending the lead's
  declaration: derivatives and moment scales raised, rate gains lowered by the same factors (`SLAAPS_DATA_RECORD.md`).
  With the gate closed the canards sit at trim, so the torque acts unopposed. With the
  CAD `SLA_I_XX` 0.0027886 kg m^2 and no aerodynamic damping, that is 18 rad/s^2: about 18 rad/s and 9 rad of roll after the
  roughly 1 s to the gate (an upper bound, unless the launcher restrains roll). The firmware carries no EDF reaction moment
  (`SLA_WR_EM_X` 0, and the moment model is per newton of thrust, so a constant torque does not fit it exactly). Decision for the
  lead before a launch: the torque's sign, how to counter it (a canard roll trim, stator vanes, a roll-restraining launcher) and
  whether `SLA_LAUNCH_AS` 20 m/s stays.
- [ ] Pitot zeroed at launch temperature and checked against a reference (fan and anemometer, or GPS speed on G4).
  2026-11-23: read 6.15 m/s indicated in still air (an offset of about 23 Pa). The true/indicated ratio 1.124 matches the 0.975 kg/m^3
  site density. Zeroed the same day with `commander calibrate airspeed` at a sensor temperature of 33 C: `SENS_DPRES_OFF` 23.0085, and still
  air now reads -0.69 to +0.93 m/s. The value is in `Tools/slaaps/parameters/slaaps_sensor_cal.params`. The reference check is still open,
  and the zero is redone at the launch temperature.
- [ ] CG and inertia checked against the data sheet. Mass and CG confirmed by the lead 2026-11-23: 1.979 kg (weighed; was 2.05 CAD)
  and 0.233 m from the tail end. `SLA_MASS` updated in data revision 7 (base) and 8 (profile C). The inertia is still the CAD value.
- [ ] Basis for `SLA_LAUNCH_AS` (20 m/s indicated, set by the lead 2026-11-21) recorded: canard authority with margin over stall, and the reaction torque it must beat.
  The torque is now known: 0.05 N m (above). Roll authority at 20 m/s is 0.044 N m with isolated canards (break-even near
  21.3 m/s), about 0.130 N m with the body included (break-even near 12.4 m/s, modeled, before any rear-fin interaction).
- [ ] Barometer shielded from the EDF (found 2026-11-23, log 61): with the EDF at 1720 to 2000 us the static pressure at the board
  fell 88 Pa and the estimated height rose 9.4 m on the stand. Shield or vent the flight controller's barometer away from the
  intake path, or keep the EDF's effect out of the height estimate, then repeat the stand run and show under 1 m of change.

## G2. Bench fault injection (EDF off first, then low throttle restrained)
Each decided response (docs/SLAAPS_RECOVERY_POLICY.md) is triggered on purpose and judged from the log:
- [ ] D1: pilot input removed with a healthy estimator: attitude hold, EDF hold then ramp (`recovery_state` 1).
  Unplanned D1 then D5 on the board 2026-11-23 (log 62, a 5.0 s ground-side input stall): `recovery_state` 0 to 1 293 ms after the last
  pilot sample (expected about 280 ms), canards held attitude, and D5 re-entered Manual 0.2 s after input returned. The EDF was already
  at the stop pulse, so the hold-then-ramp was not exercised. The deliberate trigger is still needed.
  Log 63 adds three more unplanned D1 holds and three D5 re-entries, each with a fresh session. One hold ran cleanly to D5. The
  other two hit the hold-start defect (G0) and went through D3: canards frozen, EDF at stop, then D5. The EDF was at stop at
  every hold.
- [ ] D3: estimator or power lost, mode left, a core fault: canards frozen, EDF ramps off (`recovery_state` 2).
- [ ] Bank loss, termination, kill and lockdown paths as defined.
- Pass criteria: the response starts within [lead] ms and the log shows the expected states. Record the evidence per response.

## G3. Launcher-stage dry run on the ground
- [ ] Flash the A1 (or later) image, declare revision 6, load profile C.
  Airspeed is instance 0 at 10 Hz (measured 2026-11-23); profile C now sets `SLA_LAUNCH_AGE` 250 ms (was 100, which failed part of every period).
- [ ] Launcher-stage airspeed handling checked on the board. Found 2026-11-23: a stale sample (a CAN dropout longer than `SLA_LAUNCH_AGE`)
  or a negative one withdrew flight admission while armed. That cancels the session, so the canards went unpowered with no hold or ramp.
  A zeroed pitot reads slightly negative about half the time in still air (-0.69 m/s), so an arm at rest would have cancelled within
  a few samples. Fixed the same day (lead's decision): a sample down to -5 m/s is valid and below the gate speed. A valid sample is
  needed to admit an arming, and that arming then stays admitted. The gate keeps its state. Native tests cover the decision; the
  supervisor scenarios use synthetic admission, so the board check is: arm in the launcher stage at rest, stay armed 60 s with the
  pitot uncovered, then unplug the pitot's own CAN lead (not the bus segment the power module is on) and see the session keep running
  (the gate stays closed).
- [ ] Arm in the launcher stage with a fresh airspeed sample. Push the pitot through 20 m/s indicated with a fan or hand-held airflow and see the gate open
  ("launch airspeed reached, attitude authority on"). EDF off.
- [ ] Operator-stick EDF path with `SLA_EDF_BMAX` set low, vehicle restrained, then at the planned value on the thrust stand.
  Log 63 (armed Manual, not the launcher stage): the throttle stick drove AUX1 from 1000 up to 2000 us, about 47 s above stop in total,
  with `SLA_EDF_BMAX` 1. The EDF was not powered for that run (lead, 2026-11-23); battery current peaked at 0.4 A. The command path
  works in Manual. The powered run on the stand (log 61, G1) was in EDF Bench mode, so a powered Manual run is still needed.
  (EDF Bench on 2026-11-23 exercised the EDF output and the stick path in Bench mode, not in Manual.)

## G4. Vehicle-mounted captive test (closed road or runway, 20 to 30 m/s)
- [ ] Mount on a low-friction pivot or a force balance, the pitot on its boom, GPS speed logged as the reference.
- [ ] Doublets at three speeds; identify authority and latency; compare with the model. Update the derivatives and gains, then issue a new data revision and declaration.
- [ ] Pass: identified authority inside the stable band of the stability analysis for every planned speed ([lead] sets the margin).

## G5. Flight test (staged)
- [ ] Written test card per flight: purpose, EDF cap, abort criteria, range, wind and altitude limits ([lead]), net recovery set up (`SLA_NET_EN`).
- [ ] Flight 1: conservative gains, EDF capped, short run; no manoeuvres beyond doublets.
- [ ] Expand the envelope one step per flight, with doublet data and a log review before the next.
- Pass: no unexplained events, recoveries as designed, and identified authority within the G4 band.

## G6. Qualified declaration
- [ ] The lead defines what Qualified means for this vehicle (list of evidence, thresholds, range-safety approvals).
- [ ] An evidence index (commit hashes, image hashes, log names, data revisions) is written next to the declaration.

Not in scope here: range-safety and regulatory approvals, the launcher mechanism and its release procedure, and the abort criteria in G5.
