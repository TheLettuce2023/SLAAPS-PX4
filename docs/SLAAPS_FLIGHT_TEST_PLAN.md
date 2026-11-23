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
  The diagnostic is in every image since commit 73a739b5de; the armed-Manual bench run has not been done yet.
- [ ] Timing windows final and committed: `SLA_ACT_ACK_US`, `SLA_CYCLE_MAX`, `SLA_OUT_LEASE` (the board still carries temporary 120 ms / 200 ms / 200 ms).
  The armed-Manual bench test reports the real bank reply times (`activation flight reply main=... aux=...`).
- [ ] Joystick link fixed (observed 4.8 Hz) or the input age and activation reserve proven to cope with it. Likely closed: the 4.8 Hz
  matches the logger's 200 ms sampling of `manual_control_setpoint` (`logged_topics.cpp`, default profile), not the link. Confirm the
  live rate with `uorb top manual_control_setpoint`. Measured on the board 2026-11-23: 24 Hz (about 42 ms between samples), about
  seven samples within `SLA_IN_AGE` 300 ms. Ready for the lead to close.
- [ ] Logger stack warning removed; logging rates raised for the tests below (canard commands at 50 Hz or faster). Still seen
  2026-11-23: `log_writer_file low on stack! (296 bytes left)`.
- [ ] The image on the board is built from a committed tree; hash recorded. Partly: the board image (`flight-readiness-20261123-axis-bound`)
  has the sources of fe2cc90837, but was built from a working copy, so `ver all` shows the parent hash. One clean rebuild closes it.
- [ ] Manual entry after a Bench session fails until a reboot (`entry rejected at line 1810`, since 2026-11-18); root cause not found.

## G1. Ground measurements
- [ ] EDF thrust curve and **reaction torque** on a thrust stand with a torque arm, at several throttle values. Record the supply current against the power module limit.
- [ ] Pitot zeroed at launch temperature and checked against a reference (fan and anemometer, or GPS speed on G4).
  2026-11-23: read 6.15 m/s indicated in still air (an offset of about 23 Pa). The true/indicated ratio 1.124 matches the 0.975 kg/m^3
  site density. Zeroed the same day with `commander calibrate airspeed` at a sensor temperature of 33 C: `SENS_DPRES_OFF` 23.0085, and still
  air now reads -0.69 to +0.93 m/s. The value is in `Tools/slaaps/parameters/slaaps_sensor_cal.params`. The reference check is still open,
  and the zero is redone at the launch temperature.
- [ ] CG and inertia checked against the data sheet. Mass and CG confirmed by the lead 2026-11-23: 1.979 kg (weighed; was 2.05 CAD)
  and 0.233 m from the tail end. `SLA_MASS` updated in data revision 7 (base) and 8 (profile C). The inertia is still the CAD value.
- [ ] Basis for `SLA_LAUNCH_AS` (20 m/s indicated, set by the lead 2026-11-21) recorded: canard authority with margin over stall, and the reaction torque it must beat.

## G2. Bench fault injection (EDF off first, then low throttle restrained)
Each decided response (docs/SLAAPS_RECOVERY_POLICY.md) is triggered on purpose and judged from the log:
- [ ] D1: pilot input removed with a healthy estimator: attitude hold, EDF hold then ramp (`recovery_state` 1).
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
