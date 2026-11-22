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

## G0. Software defects closed (before any hardware test that matters)
- [ ] "Control lost" events root-caused and removed. Build `flight-readiness-20261121-faultdiag`, run on the bench, read `fault_site`,
  `fault_core_line` and the margins from `slaaps_flight_status`, fix, and show zero events over [lead] armed minutes (proposed: 30).
- [ ] Timing windows final and committed: `SLA_ACT_ACK_US`, `SLA_CYCLE_MAX`, `SLA_OUT_LEASE` (the board still carries temporary 120 ms / 200 ms / 200 ms).
- [ ] Joystick link fixed (observed 4.8 Hz) or the input age and activation reserve proven to cope with it.
- [ ] Logger stack warning removed; logging rates raised for the tests below (canard commands at 50 Hz or faster).
- [ ] The image on the board is built from a committed tree; hash recorded.

## G1. Ground measurements
- [ ] EDF thrust curve and **reaction torque** on a thrust stand with a torque arm, at several throttle values. Record the supply current against the power module limit.
- [ ] Pitot zeroed at launch temperature and checked against a reference (fan and anemometer, or GPS speed on G4).
- [ ] CG and inertia checked against the data sheet.
- [ ] Basis for `SLA_LAUNCH_AS` (20 m/s indicated, set by the lead 2026-11-21) recorded: canard authority with margin over stall, and the reaction torque it must beat.

## G2. Bench fault injection (EDF off first, then low throttle restrained)
Each decided response (docs/SLAAPS_RECOVERY_POLICY.md) is triggered on purpose and judged from the log:
- [ ] D1: pilot input removed with a healthy estimator: attitude hold, EDF hold then ramp (`recovery_state` 1).
- [ ] D3: estimator or power lost, mode left, a core fault: canards frozen, EDF ramps off (`recovery_state` 2).
- [ ] Bank loss, termination, kill and lockdown paths as defined.
- Pass criteria: the response starts within [lead] ms and the log shows the expected states. Record the evidence per response.

## G3. Launcher-stage dry run on the ground
- [ ] Flash the A1 (or later) image, declare revision 6, load profile C.
- [ ] Arm in the launcher stage with a fresh airspeed sample. Push the pitot through 20 m/s indicated with a fan or hand-held airflow and see the gate open
  ("launch airspeed reached, attitude authority on"). EDF off.
- [ ] Operator-stick EDF path with `SLA_EDF_BMAX` set low, vehicle restrained, then at the planned value on the thrust stand.

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
