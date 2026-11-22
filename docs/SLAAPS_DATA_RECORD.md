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
The sheet was updated on 2026-11-17 without changing any value: the IMU unit label now reads m, the Frames and Sign-off rows
are filled from the lead's statements, and a Revision log tab maps each cell to its firmware parameter. SHA-256 of the
updated sheet: `b895898386769df65f561c3147a6936cce01eff803bf9319b273477d56f61f8b`. Its formula cells hold no cached values
until the file is opened in a spreadsheet program.

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

## Revision 4 (tunnel profile A, fixed gains modeled at 20 m/s): modeled values only, pending the lead's declaration

Overlay `Tools/slaaps/parameters/slaaps_tunnel_20ms.params`, written 2026-11-19, `SLA_DATA_REV` 4, `SLA_DATA_STATE` 0. The measured
data is unchanged from revision 2. Reason: the 2026-11-19 tunnel logs show the pitot at 10 to 14 m/s (negative sign) while the
revision-3 gains were designed for 40 m/s, which leaves about 11% of the design authority at 13 m/s.

Method as revision 3, at 20 m/s: derivatives (`SLA_Cn_DF*`, `DM*`, `DU*`), `SLA_x_M_SCALE` and `SLA_x_SP_ACC` are the revision-3 values
x 0.25 ((20/40)^2); `SLA_x_RATE_P` and `RATE_I` are x 4. `SLA_AIRSPD` and `SLA_Cn_WASH` are 20; `SLA_SCH_EN` 0.

| Value | Revision 3 (40 m/s) | Revision 4 (20 m/s) |
| --- | --- | --- |
| `SLA_Cn_DMR` | +-0.21012 | +-0.05253 N m/rad |
| `SLA_Cn_DMP` / `DMY` | +-0.61658 | +-0.15415 N m/rad |
| `SLA_R_M_SCALE` / `P` / `Y` | 0.176 / 0.5165 / 0.5165 | 0.044 / 0.12912 / 0.12912 N m |
| `SLA_R_RATE_P` / `RATE_I` / `SP_ACC` | 0.0792 / 0.0792 / 44.2 | 0.3168 / 0.3168 / 11.05 |
| `SLA_P,Y_RATE_P` / `RATE_I` / `SP_ACC` | 0.261 / 0.209 / 10.7 | 1.044 / 0.836 / 2.675 |

Valid only when the true flow at the vehicle is about 20 m/s. It needs the lead's declaration (`SLA_DATA_STATE` 1) before Manual can arm.

## Revision 5 (tunnel profile B, control schedule on): Calibrated

Declared Calibrated by the project lead, Kael Spry, on 2026-11-21 (`SLA_DATA_REV` 5, `SLA_DATA_STATE` 1, `SLA_SCH_EN` 1). As with the earlier
revisions, this is not Qualified and not flight acceptance, and it grants no flight, session or actuator permission.

Source: overlay `Tools/slaaps/parameters/slaaps_tunnel_schedule.params` (low knot lowered from 18 to 16 m/s on 2026-11-21), loaded after
`slaaps_vehicle.params` and `slaaps_canard_trims.params`. It restates the revision-3 (40 m/s) base so it is correct regardless of load order, and
adds the 2x2 table (16 and 40 m/s indicated by EDF 0 and 1) described under revision 3, with the 16 m/s corners at (16/40)^2 = 0.16 of the 40 m/s
derivatives. Entry needs positive, fresh indicated airspeed from 16 to 40 m/s with the EDF at 0, density 0.9 to 1.35 kg/m^3 and 30 to 42 V.

| File | SHA-256 |
| --- | --- |
| `slaaps_vehicle.params` | `d82ee5b185de04653f9ffc1371bdf32f10e9952a207276bb42b4996a3ed8f1b6` |
| `slaaps_canard_trims.params` | `d3ec580718f69c10c866950585c7883665a523c9ea1ec8178eba0ceceb880a77` |
| `slaaps_tunnel_schedule.params` | `69204111ec4fe675bcb9865bde3ee50bec63131f32ff9ba3b99477bfabf5e4f8` |

Provenance: every table value is modeled, not measured. Evidence from the 2026-11-21 tunnel log `00_26_33` (pitot confirmed: 18.4 to 34.4
m/s indicated) suggests the real canard authority may be up to about 3 times the modeled one (a 3.9 Hz pitch mode at k = 3 on the 20 m/s
profile); this is a hypothesis from one point, not a result. Known open items: pitch and yaw clip at the 12 degree limit at the low end of the
table, yaw wanders about 20 degrees, the interpolation overstates authority by up to about 22% near 22 to 25 m/s, and the EDF reaction torque
is unmeasured. Revision 4 (profile A, 20 m/s fixed gains) remains pending the lead's declaration.

## Revision 6 (launch profile C, launcher stage): modeled values only, pending the lead's declaration

Overlay `Tools/slaaps/parameters/slaaps_launch.params`, written 2026-11-21, `SLA_DATA_REV` 6, `SLA_DATA_STATE` 0. The measured data is
unchanged from revision 2. It selects the launcher stage (`SLA_FLT_STAGE` 2, `SLA_LAUNCH_AS_I` 0, `SLA_LAUNCH_AGE` 100) with the control schedule
off, and the same q-scaling method as revision 3 at 25 m/s indicated: derivatives, `SLA_x_M_SCALE` and `SP_ACC` x 0.3906, `RATE_P` and `RATE_I` x 2.56.
`SLA_LAUNCH_AS` stays the unmeasured 12 m/s placeholder and must be set from captive tests. The EDF is operator-stick triggered
(`SLA_EDF_RISE` unchanged). It needs the lead's declaration before Manual can arm.
