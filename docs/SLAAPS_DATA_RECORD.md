# SLAAPS data record 20261110, revision 1

| Field | Value |
| --- | --- |
| `SLA_DATA_REC` | 20261110 |
| `SLA_DATA_REV` | 1 |
| `SLA_DATA_STATE` | 1 (Calibrated) |
| Declared by | Kael Spry, project lead |
| Date | 2026-11-10 |

The project lead signed off the vehicle data as Calibrated on 2026-11-10. The firmware does not check this
record. It only requires a positive record ID and revision with state Calibrated or Qualified before flight
admission (`validateFlightConfiguration`). Calibrated is not Qualified. It is not flight acceptance, and it grants no
flight, session or actuator permission.

## What the record covers

- QGC parameter file `Tools/slaaps/parameters/slaaps_vehicle.params`: the SLAAPS profile values in it on 2026-11-10, with
  the canard centre correction (`SLA_Cn_ZERO`) set on the board.
- Source data: `src/lib/slaaps/Master_Vehicle_Data_Sheet_Slaaps.xlsx`
  (SHA-256 `92fb4791ab291db2b63a294bf027a1150f0285afc8ab8586cf6f7e48d9e78702`).
- Board checks accepted by the lead:
  - Servo bench on the Pixhawk 6X: canard directions, plug order and bench mix (2026-11-05/06).
  - Canard mechanical centring with `SLA_Cn_ZERO` (2026-11-10).
  - Stick mapping and signs verified as configured: `SLA_R/P/Y_INPUT` 0/1/2, `SLA_R/P/Y_SIGN` +1/-1/+1
    (2026-11-10).

## Provenance of the values (unchanged by the sign-off)

Calibrated declares that the lead accepts this data set for the next test stage. It does not turn modeled values into
measured ones.

- **Measured or lead-supplied:**
  - Mass, CG and inertia diagonal.
  - EDF thrust curve at 42 V.
  - Canard and fin geometry, IMU position.
  - Servo travel, rate, lag and delay.
  - Power range.
- **Declared assumptions:**
  - Zero products of inertia, by symmetry.
  - Sea-level air density.
- **Modeled, not measured:**
  - Canard force and moment derivatives, their uncertainties, and the per-axis moment scales.
  - Rate, attitude and stick gains.
  - Timing and allocator values, including the per-mode activation budgets of 2026-11-10.
  - EDF slew limits.
  - The rear-fin force model (specified, not implemented).

Expect retuning from captive tests.

## Not covered by this record

- **`SLA_EDF_BMAX`**, the EDF commissioning ceiling: set to 1 (full command, about 39 N) by the project lead on 2026-11-12,
  after this revision. It goes into revision 2 together with the updated CAD and thrust data.
- **Acceleration-mode data:** the measured schedule, wrench model and companion parameters.
- **Launcher-stage airspeed source:** the pitot.

## Changing the record

Any change to the profile values above needs a new `SLA_DATA_REV`, an entry in this file, and a deliberate disarmed
re-apply (`SLA_CFG_APPLY`).

## Revision 2 (data sheet of 2026-11-12): Calibrated

Declared Calibrated by the project lead, Kael Spry, on 2026-11-12 (`SLA_DATA_REV` 2, `SLA_DATA_STATE` 1). As with
revision 1, this is not Qualified and not flight acceptance. Source data:
`src/lib/slaaps/Master_Vehicle_Data_Sheet_Slaaps.xlsx` (SHA-256
`1de75cc2f9752ebbc371020690c403f9f7cfcd2827e9da86936f2893603bc5fe`).

| Quantity | Revision 1 | Revision 2 | Parameter |
| --- | --- | --- | --- |
| Mass | 2.1 kg | 2.05 kg | `SLA_MASS` |
| CG from the tail end | 0.2716 m | 0.233 m | `SLA_CG_X` |
| IMU from the tail end | 0.32375 m | 0.268 m (the sheet labels it mm; read as m) | `EKF2_IMU_POS_X` 0.035 |
| Roll inertia | 0.0072 | 0.0027886 kg m^2 (CAD Izz) | `SLA_I_XX` |
| Pitch, yaw inertia | 0.0595 | 0.0337643 / 0.0337507 kg m^2 | `SLA_I_YY` / `SLA_I_ZZ` |
| Products of inertia | 0, assumed | 8.4e-6 / 8.47e-5 / -7.7e-6 kg m^2 | `SLA_I_XY/XZ/YZ` |
| Canard COP forward of CG | 0.2334 m | 0.166 m | modeled derivatives |
| Rear fin COP aft of CG | 0.1516 m | 0.164 m | fin model (not implemented) |
| Vehicle length | 0.636 m | 0.49 m | none |

Recomputed modeled values (same method as revision 1; still modeled, not measured):
- **Canard moment derivatives:** `SLA_Cn_DMP` / `DMY` are 0.06513 N m/rad, with signs as before. `SLA_Cn_DUP` / `DUY` are
  0.03256 (50%). `DMR`, `DFY` and `DFZ` are unchanged.
- **Moment scales:** `SLA_P_M_SCALE` and `SLA_Y_M_SCALE` are 0.0546 N m. `SLA_R_M_SCALE` stays 0.019.
- **Rate loop:** with b = M_SCALE / I, b is 6.81 for roll and 1.62 for pitch and yaw. The crossover is 5 rad/s for roll
  and 4 rad/s for pitch and yaw.

| Axis | `RATE_P` | `RATE_I` | `SP_ACC` |
| --- | --- | --- | --- |
| Roll | 0.734 | 0.734 | 4.77 |
| Pitch, yaw | 2.47 | 1.98 | 1.13 |

`ATT_P` and the stick rates are unchanged. Roll inertia fell by 2.6 times, so roll gains fell by the same factor. The
revision 1 roll gains would be about 2.6 times too aggressive on this vehicle.

Lead decisions recorded with revision 2 (2026-11-12):
- **Centre of mass:** the CAD moments were taken about the centre of mass.
- **Thrust curve:** `SLA_EDF_T0..T4` stay as they are (0 to 39.23 N, linear, 42 V).
- **Canard centring:** `SLA_Cn_ZERO` stay 0 in the parameter file. The canards are trimmed manually on the board after
  the file is loaded.

Notes for revision 2:
- **CAD frame:** mapped by the project lead (2026-11-12). +Z is forward. Viewed from behind, +Y lies between right and
  up (45 deg from right) and +X between left and up (225 deg from down). The sheet's matrix (tensor entries, kg mm^2) is
  rotated into body FRD.
- **`SLA_EDF_BMAX`:** 1, set by the lead on 2026-11-12.
- **Sheet cells:** the sheet's sign-off and frame cells are blank.

## Revision 3 (wind tunnel 18 to 40 m/s): modeled values only, pending the lead's declaration

The QGC parameter file carries `SLA_DATA_REV` 3 with `SLA_DATA_STATE` 0. The measured data is unchanged from revision 2.
The tunnel envelope is 18 m/s (minimum steady level flight) ramping to 40 m/s (project lead, 2026-11-12).

`SLA_AIRSPD` declares the single airflow at which the derivatives apply; the firmware does not rescale them with speed.
Canard authority grows with dynamic pressure (V^2), so this revision models every derivative at the top speed, 40 m/s
(q = 980 Pa, 9.47 times the 13 m/s values). The fixed gains are then at their design crossover at 40 m/s and lower at
every slower speed: stable across the range in the model, but sluggish at 18 m/s. They are not retuned while armed.

| Value | Revision 2 (13 m/s) | Revision 3 (40 m/s) |
| --- | --- | --- |
| `SLA_AIRSPD`, `SLA_Cn_WASH` | 13 | 40 |
| `SLA_Cn_DFY` / `DFZ` | +-0.39233 | +-3.71437 N/rad |
| `SLA_Cn_DMR` | +-0.02219 | +-0.21012 N m/rad |
| `SLA_Cn_DMP` / `DMY` | +-0.06513 | +-0.61658 N m/rad |
| `SLA_Cn_DUR` / `DUP` / `DUY` | 0.0111 / 0.03256 | 0.10506 / 0.30829 (50%) |
| `SLA_R_M_SCALE` / `P` / `Y` | 0.019 / 0.0546 / 0.0546 | 0.176 / 0.5165 / 0.5165 N m |
| `SLA_R_RATE_P` / `RATE_I` / `SP_ACC` | 0.734 / 0.734 / 4.77 | 0.0792 / 0.0792 / 44.2 |
| `SLA_P,Y_RATE_P` / `RATE_I` / `SP_ACC` | 2.47 / 1.98 / 1.13 | 0.261 / 0.209 / 10.7 |

Signs, `ATT_P` and the stick rates are unchanged.

Modeled crossover with these fixed gains:

| Airspeed | 18 m/s | 25 m/s | 32 m/s | 40 m/s |
| --- | --- | --- | --- | --- |
| Roll | 1.0 rad/s | 2.0 | 3.2 | 5.0 |
| Pitch, yaw | 0.8 rad/s | 1.6 | 2.6 | 4.0 |
| Roll moment available (max) | 0.036 N m | 0.069 | 0.113 | 0.176 |

The EDF reaction torque is unmeasured. If it exceeds the roll authority at low speed, roll cannot be held, so keep the
EDF low until it is measured.

