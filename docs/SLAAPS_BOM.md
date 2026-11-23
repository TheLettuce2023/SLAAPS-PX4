# SLAAPS parts list (bill of materials)

Status: draft, 2026-11-22. Part identities were given by the project lead on 2026-11-22. Everything else is taken from
`docs/PROJECT_CONTEXT.md`, `docs/RESEARCH_VEHICLE_REQUIREMENTS.md` and `docs/SLAAPS_6X_FLASH_AND_BOOT.md`. Cells marked
"not recorded" have no source yet. Mass, cost and vendor columns are intentionally empty until measured or purchased.

| Subsystem | Item | Qty | Part | Notes from the project docs |
|---|---|---|---|---|
| Flight controller | Pixhawk 6X | 1 | Pixhawk 6X (carrier and variant not recorded) | MAIN rail is PX4IO (bank 0), AUX is FMU (bank 1). Onboard IMU, baro, magnetometer. |
| Propulsion | EDF unit | 1 | EJets Jetfan 90 | Measured thrust 0 to 39.2 N (about 4.0 kgf) at 42 V and about 70 A (about 2.9 kW). Earlier user estimate 5.5 kgf. |
| Propulsion | EDF motor | 1 | Hacker E50-M 2.5D | |
| Propulsion | EDF ESC | 1 | TBS Lucid 12S 90A | DShot600 on AUX5. The pack is 10S and the measured draw is about 70 A. |
| Power | Battery packs | 2 | Ovonic Rebel 5S 1300 mAh 18.5 V 130C LiPo, XT90 | Two in series for 10S (`SLA_PWR_VMIN` 30 V, `SLA_PWR_VMAX` 42 V). In series the capacity stays 1.3 Ah. |
| Power | Power module (`POWER1`) | 1 | Holybro PM08-CAN | Measures the EDF pack. Written "PMO8-CAN" in the request; assumed PM08-CAN. |
| Actuation | Canard servos | 4 | EMAX ES3059MD | Identical. 500 to 2500 us (+-90 deg), 333 Hz, slew 0.11 s per 60 deg. MAIN1 to MAIN4. |
| Aero | Canards | 4 | In-house, NACA 0012 | X layout, 40 mm from the centerline, +-15 deg vehicle limit, 12 deg aerodynamic limit. |
| Sensing | Airspeed (pitot on boom) | 1 | Holybro High Precision DroneCAN DLVR sensor | Nose mounting read about 20%; the boom fixed it (2026-11-20). Indicated about 0.89 of true. |
| Sensing | GPS | 1 | Holybro M9N GPS | |
| Comms | Telemetry radio | 1 pair | Holybro SiK 915 MHz | Instance 0 on `/dev/ttyS6` at 57600. |
| Companion | Raspberry Pi 4B | 1 | Raspberry Pi 4B | Planned for acceleration requests; not confirmed as fitted. |

## Connections

| From | To | Interface | Source / status |
|---|---|---|---|
| Canard servos 1 to 4 | Pixhawk 6X MAIN1 to MAIN4 (PX4IO, bank 0) | PWM 333 Hz, 1333/1500/1667 us | `SLAAPS_6X_FLASH_AND_BOOT.md`. Servos 3 and 4 plugs were swapped and fixed on 2026-11-05. |
| EDF ESC (TBS Lucid) | Pixhawk 6X AUX5 (FMU, bank 1) | DShot600 | `SLAAPS_6X_FLASH_AND_BOOT.md`. AUX1-4 and AUX7-8 are unused. |
| EDF motor | EDF ESC | Three-phase motor leads | Not documented. |
| Battery packs (2x 5S) | Each other | Series, XT90 | Gives 10S. Series lead and connector arrangement not documented. |
| Battery stack (10S) | Power module (PM08-CAN) input | Main power | Stated by the project lead 2026-11-22. The module measures the EDF pack (`SLA_PWR_INST 0`, `SLA_PWR_ID 1`). |
| Power module (PM08-CAN) pass-through | EDF ESC (TBS Lucid) | Main power | Stated by the project lead 2026-11-22. The full EDF current (about 70 A average) flows through the module. |
| Power module (PM08-CAN) | Pixhawk 6X CAN bus | DroneCAN | `UAVCAN_ENABLE 2`, `UAVCAN_SUB_BAT 2` in the params file. CAN port (CAN1 or CAN2) not documented. |
| Pitot (Holybro DLVR) | Pixhawk 6X CAN bus | DroneCAN | `UAVCAN_SUB_DPRES 1`. Shares the CAN bus with the power module. |
| GPS (Holybro M9N) | Pixhawk 6X GPS1 | Serial | GPS1 stated by the project lead 2026-11-22. No `UAVCAN_SUB_GPS` setting in the params file. Variant not confirmed. |
| SiK radio | Pixhawk 6X TELEM1 | MAVLink at 57600 | TELEM1 stated by the project lead 2026-11-22. `mavlink status` instance 0 on `/dev/ttyS6`. |
| QGC and joystick | Pixhawk 6X USB | MAVLink over USB CDC | `mavlink status` instance 2 (`/dev/ttyACM0`). Source of the manual input (sysid 255, compid 190). |
| Raspberry Pi 4B | Pixhawk 6X TELEM2 | MAVLink serial (planned) | TELEM2 stated by the project lead 2026-11-22. Baud rate and the `MAV_1_CONFIG` setting are not recorded. |
| Power module (PM08-CAN) POWER1 output | Pixhawk 6X (flight controller) | Regulated supply | Stated by the project lead 2026-11-22. |
| Power module (PM08-CAN) POWER2 output | Servo power rail (4x EMAX ES3059MD) and Raspberry Pi 4B | Regulated supply | Stated by the project lead 2026-11-22. Same PM08-CAN as POWER1. How the rail is distributed to the servos is not recorded. |

## Known figures
- All-up mass 1.9 kg (current, stated by the project lead 2026-11-22). This replaces the 3.04 kg assumed from the 2026-09-19 SDF model.
  The vehicle parameter file carries `SLA_MASS 2.05` and the SDF model still uses the older figure; both need review. Not a weighed breakdown.
- CG on the centerline. IMU 10 mm up and 52.15 mm forward of the CG.

## Open items
- Weigh each item and the assembled vehicle, then fill a mass column. Record how the 1.9 kg was measured (scale, configuration, battery fitted or not).
- Update `SLA_MASS` (file value 2.05) and the inertia and CG data if the 1.9 kg is confirmed.
- Runtime: 1.3 Ah at the measured 70 A is about 67 s, and less with a usable-capacity limit (for example about 50 s at 80%). Confirm the burn rate on the bench
  and set the bench or flight time limits to match. 70 A is about 54C, inside the 130C rating.
- Power budget (requirement `PWR-02`): one PM08-CAN carries everything. The battery stack feeds it, it passes the EDF current through to the ESC,
  and its POWER1 output feeds the flight controller while POWER2 feeds the servo rail and the Raspberry Pi.
  Check the module's pass-through current rating (about 70 A average, peak unmeasured) and its regulated output voltage and current against the servo supply
  range, the servo stall and peak current, and the Pi load. Record a brownout test with the EDF at full throttle and the canards moving.
  A fault or sag on the module takes out the flight controller, servos, Pi and EDF together, so there is no isolation between them.
- Confirm the power module model (PM08-CAN) and the Holybro M9N variant (UART or DroneCAN).
- Check the ESC (90 A) and PM08-CAN pass-through current limits against the measured peak, not just the 70 A average.
