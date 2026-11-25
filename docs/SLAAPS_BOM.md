# SLAAPS parts list (bill of materials)

Status: draft, 2026-11-22. Part identities were given by the project lead on 2026-11-22. Everything else is taken from
`docs/PROJECT_CONTEXT.md`, `docs/RESEARCH_VEHICLE_REQUIREMENTS.md` and `docs/SLAAPS_6X_FLASH_AND_BOOT.md`. Cells marked
"not recorded" have no source yet. Mass, cost and vendor columns are intentionally empty until measured or purchased.

| Subsystem | Item | Qty | Part | Notes from the project docs |
|---|---|---|---|---|
| Flight controller | Pixhawk 6X | 1 | Pixhawk 6X (carrier and variant not recorded) | MAIN rail is PX4IO (bank 0), AUX is FMU (bank 1). Onboard IMU, baro, magnetometer. |
| Propulsion | EDF unit | 1 | EJets Jetfan 90 | Measured thrust 0 to 39.2 N (about 4.0 kgf) at 42 V and about 70 A (about 2.9 kW). Earlier user estimate 5.5 kgf. |
| Propulsion | EDF motor | 1 | Hacker E50-M 2.5D | |
| Propulsion | EDF ESC | 1 | TBS Lucid 12S 90A | PWM 400 Hz on AUX1, 1000-2000 us (moved from DShot600 on AUX5 on 2026-11-22: it never armed on DShot under the SLAAPS image). The pack is 10S and the measured draw is about 70 A. |
| Power | Battery packs | 2 | Tattu R-Line 5.0 5S 2200 mAh 18.5 V 150C LiPo | Since 2026-11-25. Two in series for 10S (`SLA_PWR_VMIN` 30 V, `SLA_PWR_VMAX` 42 V), 2.2 Ah (`BAT1_CAPACITY` 2200). Replaced the Ovonic Rebel 5S 1300 mAh 130C packs, whose voltage sag (41.8 to 36.7 V at 45 A) halved the EDF thrust in the first flight (log 66). |
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
| EDF ESC (TBS Lucid) | Pixhawk 6X AUX1 (FMU, bank 1) | PWM 400 Hz | `SLAAPS_6X_FLASH_AND_BOOT.md`. AUX2-8 are unused. |
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
- All-up mass 2.238 kg with the Tattu packs (data sheet 2026-11-25, `SLA_MASS` from data revision 14). Before: 1.979 kg, weighed
  (confirmed by the project lead 2026-11-23), which replaced the 1.9 kg stated 2026-11-22 and the 3.04 kg of the 2026-09-19 SDF model.
  The SDF model still uses the older figure.
- Bench thrust 4.402 kg (43.2 N) on the Tattu packs (data sheet 2026-11-25): thrust to weight 1.97.
- CG on the centerline, 0.268 m from the tail end. IMU 0.260 m from the tail end (8 mm aft of the CG, `EKF2_IMU_POS_X` -0.008) and 10 mm up.

## Open items
- Weigh each item, then fill a mass column. The assembled vehicle is 2.238 kg on the 2026-11-25 sheet. Record whether the battery was fitted.
- The inertia is still the CAD value (updated for the new packs on the 2026-11-25 sheet).
- Runtime: 2.2 Ah at the measured 70 A is about 113 s, and less with a usable-capacity limit (for example about 90 s at 80%). Confirm the burn rate on the bench
  and set the bench or flight time limits to match. 70 A is about 32C, inside the 150C rating.
- Power budget (requirement `PWR-02`): one PM08-CAN carries everything. The battery stack feeds it, it passes the EDF current through to the ESC,
  and its POWER1 output feeds the flight controller while POWER2 feeds the servo rail and the Raspberry Pi.
  Check the module's pass-through current rating (about 70 A average, peak unmeasured) and its regulated output voltage and current against the servo supply
  range, the servo stall and peak current, and the Pi load. Record a brownout test with the EDF at full throttle and the canards moving.
  A fault or sag on the module takes out the flight controller, servos, Pi and EDF together, so there is no isolation between them.
- Confirm the power module model (PM08-CAN) and the Holybro M9N variant (UART or DroneCAN).
- Check the ESC (90 A) and PM08-CAN pass-through current limits against the measured peak, not just the 70 A average.
