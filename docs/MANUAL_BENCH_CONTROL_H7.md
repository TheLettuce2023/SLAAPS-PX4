# H7 servo-only manual bench boundary

Historical H7 record: [H8](MANUAL_BENCH_CONTROL_H8.md) adds the subsequently
authorized default-off registered module and dedicated servo-output software
path. H7's no-publication statements describe this earlier component/evidence,
not the later opt-in integration. Physical qualification remains open.

2026-10-01, Codex. User accepted the preceding bench proposals and reconfirmed
**NACA 0012**. This record carries those decisions across chats. It does not
qualify physical operation, positive acceleration admission or flight.

## Accepted scope and assumptions

- QGroundControl gamepad in Normal/MANUAL_CONTROL mode over SiK, with PX4
  retaining sole source selection, runtime supervision and future output ownership.
- First powered scope is four servos with wind and the integrated airframe mass
  and propulsion electronics installed. EDF remains electrically inhibited;
  software development/compilation does not authorize flashing or running hardware.
- C1 MAIN1 right/up (+Y,-Z); C2 MAIN2 right/down (+Y,+Z);
  C3 MAIN3 left/down (-Y,+Z); C4 MAIN4 left/up (-Y,-Z), body FRD.
  Physical labels and the CAD-to-body transform still require measurement.
- Positive surface deflection uses trailing-edge-down (TED), recorded with a
  local motion arrow for each inclined hinge. Neutral is provisional zero degrees.
  A maximum of +/-13 degrees is a provisional surface command envelope, not a
  claim of attached flow or aerodynamic angle-of-attack qualification.
- Current airfoil is NACA 0012; older NACA 0010/main-wing sizing is superseded.
  Four independent X-canards, fixed rear EDF, no main wing or thrust vectoring.
  Existing acceleration-at-CG/body-FRD and navigation-NED contracts remain intact.
- EMAX ES3059MD; user supplied 500-2500 us and 175 degrees total travel.
  Accepted retrieved sheet specifies 180 +/-10 degrees, center 1500 us,
  4.8/6 V performance points, 0.8/1.0 A maximum current, 1.8/2.1 kg.cm stall
  torque and 0.13/0.11 s per 60 degrees no-load speed. These do not determine
  installed linkage calibration, continuous load capability or safe slew rate.
- EDF: Ejets Jetfan90/Hacker E50-M-1.5Y; OddityRC Vortex F160HV, DShot600,
  AUX4, no reverse thrust. AUX4 is reserved and inhibited in this increment.
  EDF wash is assumed separate from canards except airspeed, pending validation.
- Coupled roll/pitch/yaw mix with uniform scaling of the mixed vector into the
  surface envelope before independently calibrated slew limiting. Slew limiting
  can temporarily alter ratios. Throttle has no bench command effect.
- Accepted candidate schedule 50 Hz (20 ms), servo refresh candidate 50 Hz,
  manual sample age 500 ms. These are explicitly configured bench candidates,
  not measured execution, link latency or servo response guarantees.
- Source change/loss/invalid input revokes the session, requests neutral and
  requires explicit reset/configuration/enable. No automatic recovery.
  Software source changes are authorized; arbitrary live controller takeover is
  not implemented. One observed MAVLink instance/sysid/compid is admitted per session.

Sources: [EMAX specification](https://flymod.net/download/emax_es3059md_12g_digital_serv_specifications),
[QGC joystick](https://docs.qgroundcontrol.com/master/en/qgc-user-guide/setup_view/joystick.html),
[OddityRC ESC](https://oddityrc.com/collections/fc-esc/products/oddityrc-vortex-f160hv-12s-160a-single-esc).
QGC documents 25 Hz changing-input and 5 Hz unchanged-input transmission.
The stock receiver timestamps receipt; a 500 ms sample age does not detect all
upstream radio buffering. IDs are routing identities, not authentication.

## Implementation contract

`BenchControlOwner` is fixed-capacity, noncopyable and nonmovable. Configuration
requires all four installed servo calibrations, coupled mix, observed sender
identity, joystick center tolerance and maximum scheduling gap. None is silently
filled from a nominal servo datasheet or old design analysis. Successful
configuration enters Ready; explicit enable requires fresh centered input;
successful cycles produce bounded normalized, angle and floating PWM candidates.
The EDF candidate is always zero. Boot/construction remains Disabled.

H6 now has an explicit required-axis mask, defaulting to its original four axes.
Disabled columns must be zero in both affine maps. Disabled values, including
NaN throttle, are excluded from arithmetic and held-sample equality. H7 uses only
roll/pitch/yaw and cannot map throttle into the EDF. H6's original tests remain.

H7 uses H1 amplitude/slew limiting; it does not require invented H2/H3 plant
coefficients. It produces no aerodynamic residual, authority or acceleration
tracking claim. Mix signs preserve PX4 manual input conventions: positive pitch
input is forward stick/nose-down, distinct from positive body pitch rotation.

Calibration contains pulse values at negative, zero and positive surface
deflection, using monotonic increasing or decreasing sequences. This encodes
reversal once and permits separate linear slopes on the two sides of neutral.
The candidate is not an actuator driver command; integer rounding and actual
PWM timing remain in the future qualified output integration.

All rejected active cycles latch Fault except NotDue. Faults preserve the last
successful command/report and caller report. Input acceptance and command-cycle
commit are separate: an early valid observation may update H6 input chronology
without producing a new command. Neutral intent is available independently of
the last successful candidate; stored commands are never permission to hold
physical outputs after a fault. Disable cannot clear a latched fault.

`Px4BenchControlRuntime` subscribes only to the stock selected
`manual_control_setpoint`, owns one H7 owner, and schedules only after explicit
enable. It shares the H6 message conversion and stops scheduling on a fault.
All lifecycle calls and destruction must be serialized with Run by a future
enclosing owner. There is no module registration, startup entry, parameter,
airframe, arming action, output publisher or hardware sink. Default builds exclude
the library; separately compiling it is not linked firmware resource evidence.

## Provisional bench assumptions, 2026-10-01 follow-up

These user-authorized assumptions support design and ideal-model work only;
they do not configure H7, enable outputs or qualify hardware. NACA 0012 remains
the airfoil. Direct linkage is modeled as 1:1 with ideal surface tracking.

- Rail: user declares nominal 5 V. The PO #1 `getFPV_Power_Order_PO1.xlsx`
  lists MATEKSYS 12S Pro BEC; the V2 propulsion/FC specification names it,
  and the wiring drawing labels UBEC 5V/5A. Manufacturer rating is 5 A
  continuous, 9 A peak (duration unspecified), with **5.2 V default**, not
  regulated 5.0 V. Distinguish the standalone BEC from the FCHUB PDB's own
  regulator; installed wiring and loaded voltage are unobserved. Do not parallel
  their outputs. Four servo datasheet maximum currents total 3.2 A at 4.8 V or
  4 A at 6 V; this is not measured simultaneous demand or thermal qualification.
- Wind: user intends 15 m/s through speeds above 40 m/s. The stated 15 m/s stall
  velocity is a design input, not measured acceptance. An explicit finite upper
  test speed remains unset. At equal density, dynamic pressure at 40 m/s is
  7.11 times that at 15 m/s. Surface deflection +/-13 degrees is not airfoil AoA
  and does not prove stall avoidance or adequate holding torque.
- QGC: user reports latest daily, without an exact installed build identifier.
  SiK is on TELEM1. Assume PX4 MAVLink instance 0, sending GCS system 255 /
  component 190, destination vehicle system 1 / autopilot component 1.
  Instance is a PX4 process/channel selection, not a MAVLink wire ID; TELEM1
  does not prove instance 0. MANUAL_CONTROL targets a system, not a component.
  These are provisional expected identities, not observations or authentication.
- Deadzone: use +/-0.05 of normalized full-scale R/P/Y as an explicitly assumed
  project starting point, not a claimed universal QGC default. Future shaping:
  zero for |x| <= 0.05, otherwise sign(x)*(|x|-0.05)/0.95; apply once, not twice
  in QGC and PX4. H7 currently has centered-entry tolerance, not this nonlinear
  deadzone implementation. No parameter or code behavior changed here.
- Timing: retain accepted 20 ms candidate period and 500 ms input expiry.
  Neither is measured timing evidence. Maximum scheduler gap remains unset;
  no scheduling/link measurement is fabricated. QGC idle updates may be 5 Hz.

The EMAX/Yinyan ES3059MD sheet specifies no-load 0.13 s/60 degrees at 4.8 V
and 0.11 s/60 degrees at 6 V, with +/-10% speed tolerance. These correspond to
461.54 and 545.45 deg/s. Linear interpolation of **transit time** at the declared
5.0 V gives 0.1266667 s/60 degrees, or **473.6842 deg/s**. Use that only as the
provisional ideal rise/fall model; it is neither a published 5 V specification
nor an approved loaded slew limit. Actual 5.2 V supply would require revisiting
this model. At this ideal rate a 26-degree traverse takes 54.9 ms, excluding
communication delay, control latency and load. Direct coupling removes linkage
ratio uncertainty, not torque, compliance, backlash or aerodynamic uncertainty.

Preserve the user's 175-degree total travel assumption (manufacturer nominal is
180 +/-10 degrees). With ideal centered 1500 us and 500..2500 us over 175 degrees:
`PWM_us = 1500 + s * angle_deg * 2000/175`, where `s` is installed reversal.
For model sign s=+1, -13/0/+13 degrees maps to
1351.428571/1500/1648.571429 us; s=-1 swaps the endpoints. This is an affine
model, not measured calibration. Positive PWM is not evidence of positive TED
on a mirrored installation; channel-specific signs and coupled mix remain
unqualified. No pulse is sent and integer output rounding remains unspecified.

### Independent stop arrangement: proposed hardware contract

Normal stop requests neutral while retaining servo power, then reduces/stops
tunnel wind. This software request is not the independent emergency stop.
Provide a local hardwired, latching mushroom and an energized-to-permit safety
circuit independent of QGC, SiK, PX4 and its scheduling. A broken permit circuit
must remove permission. Its hardware outputs invoke the tunnel's approved stop
interface and isolate the dedicated servo positive supply, with PWM isolation
to prevent signal backfeeding. Keep flight-controller/logging power separate
where the verified power topology permits. Do not switch only the ground return.
EDF propulsion power stays physically isolated for these servo-only tests.

Power removal **does not center or hold the canards**, and tunnel airflow does
not disappear immediately. The fixture/mechanical restraint must tolerate
residual wind and free/unpowered surface motion; software neutral is no substitute.
Facility personnel must approve the tunnel interlock and qualified DC switching,
protection and wiring for actual load/inrush. This is a design contract, not a
certified circuit or authorization to modify tunnel mains/VFD wiring. Resetting
the mushroom cannot restart wind or outputs: local deliberate reset and a fresh
PX4 centered-enable session are separate requirements. Hardware implementation,
independence, residual-wind restraint and restart inhibition require verification.

Sources (read 2026-10-01):

- [MATEKSYS BEC12S-PRO manufacturer specification](https://www.mateksys.com/?portfolio=bec12s-pro).
- [EMAX/Yinyan ES3059MD specification, 2021-11-30, distributor-hosted original](https://flymod.net/download/emax_es3059md_12g_digital_serv_specifications).
- [QGC joystick guide](https://docs.qgroundcontrol.com/master/en/qgc-user-guide/setup_view/joystick.html),
  [GCS system-ID default](https://raw.githubusercontent.com/mavlink/qgroundcontrol/master/src/Settings/Mavlink.SettingsGroup.json),
  [GCS component selection](https://raw.githubusercontent.com/mavlink/qgroundcontrol/master/src/Comms/MAVLinkProtocol.h).
- Under `C:/Users/diggy/OneDrive - Colostate/Classes/Senior Design SNC`:
  `3 - Build and Test Phase/Budget and Purchasing/Purchase Orders/PO #1/getFPV_Power_Order_PO1.xlsx`,
  SHA-256 `0ec09cf861848c85ebde2dcc129e2e07896472b715d8f8a11140254daed94bf2`;
  `2 - Detailed Design Phase/Subsystems/Flight Control System + Propulsion subsystem/SpecSheet_propulsion&FC_V2.docx`,
  SHA-256 `0fac6402376da5e09aa1b8b7de2c74ee847322d6d7fadfad059779096fd0004a`;
  same folder `Wiring Diagram Senior Design.drawio`, SHA-256
  `126e22ef1447e49109c5818adbd18933b07690cc1cbe0fc2203631319606b67a`.

Documentation-only follow-up: source/test/build evidence below is unchanged;
no new implementation or physical gate advanced and no build was rerun.

## Physical inputs still required before output qualification

1. Per canard: verified channel/label, hinge position and axis in CG-relative FRD,
   zero chord direction, TED motion arrow, measured negative/zero/positive pulse
   and angle pairs, absence of binding, and approved rise/fall slew rates.
2. Actual servo rail voltage, BEC/current capacity under simultaneous load, and
   approved wind/load envelope and test restraint/independent stop arrangement.
3. QGC/gamepad version and calibration, observed MAVLink instance/sysid/compid,
   measured center tolerance, SiK latency/queueing under telemetry load and
   scheduler gap budget. Measure source and joystick loss as separate failures.
4. Verified neutral response through PX4IO on source loss, owner failure and
   reboot, plus a physical stop independent of GCS. Neutral return dynamics are
   not established by the candidate math. Output integration remains blocked here.
5. For later EDF operation: actual ESC firmware/configuration, stopped and
   windmilling behavior, minimum stable installed command, and permitted maximum
   from measured current/temperature/RPM/duct/battery limits. Existing incomplete
   PWM bench notes do not characterize DShot600.

The requested Senior Design folder was reviewed read-only. Its two NACA 0012
Fin_Sizing.m copies use 0.15 m and 0.18 m CG-to-canard distances; an older script
uses NACA 0010/main-wing assumptions. The propulsion specification reports an
11-degree stall-related XFLR result, while sizing scripts use other deflections.
These are design assumptions, not measurements resolving the inputs above.

## Verification

Final evidence is retained in Ubuntu-24.04 WSL at `/var/tmp/research-bench-h7`.
`manifest.json` SHA-256:
`ec260f2d798ef98eccbd9bb3141f1993ac315f8f4a5ca24c667a96ed0a84bc03`.
It binds final sources, strict test logs, compiler/build logs, compile commands,
firmware artifacts, unlinked ARM objects, the procedure and preservation check.
Host and native PX4 library source views match the final checkout byte-for-byte.
990 unrelated checkpoint files retain their hashes; original H1-H6 source
anchors were recovered from the retained H6 tree and checked against the
pre-H7 graph. No original source, geometry or evidence package was deleted.

Scoped PX4 AStyle passes. Strict `-Wall -Wextra -Werror -Wpedantic` Debug and
Release and ASan/UBSan (halt on error, leak detection) each pass seven executables:
H1 843, H2 182, H3 233, H4 68, H5 73, H6 132, H7 649 = **2,180 checks**.
H7 covers configuration rejection, explicit centered entry, EDF exclusion,
disabled NaN/infinite throttle, per-channel reversal, uniform coupled saturation,
asymmetric slew, source/identity/range/time failures, sticky faults, unchanged
caller/last-good reports, reset, 500 ms held-input expiry, scheduling and 27
roll/pitch/yaw combinations. Synthetic fixture data is not an operational mix.

Final test-log SHA-256 values:

- Debug: `d9b3ebe06eb26158adb6b4dd5cde87e728a2a2116570282d80650aa916343cc3`
- Release: `039e268310b86c8435dc89d0f81c7f8a7c1ea82e81fab283777a5c9d236ff37b`
- ASan/UBSan: `5a82a8b9da2c9ac0563b70428ded41c4f7923845aa8202c8638a2fe82b6d90c0`
- AStyle: `84bb5a77c54c1cf733175adf650d5eba3e48f19993afa7290e58b71579c6ab33`

Fresh full default SITL and Pixhawk 6X builds passed in a new WSL-native source
view derived from the retained H6 tree, overlaid with current tracked changes
and the complete research library. MAVLink generation was explicitly ordered.
Separate library compilation also passes in POSIX and NuttX using installed
ARM GCC `(15:13.2.rel1-2) 13.2.1 20231009`. Initial review fixed a test's decimal
rounding expectation, a PX4 float-equality warning, and unavailable NuttX
`std::fmax`; final suites and both target libraries were rerun after correction.
The final SITL invocation includes incremental revalidation after the first full
build; retained logs must not be read as repeated from-scratch builds.

Artifacts:

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `sitl-build/bin/px4` | 58,587,800 | `a783a04db9ea9d4a316e4acdc8d81263def5da5e82ffe97d75b967c632c1b659` |
| `v6x-build/px4_fmu-v6x_default.elf` | 49,260,580 | `f7460b0ee1c168d2997ec0206dfe510207c74f7591e30d380318f57d497361eb` |
| `v6x-build/px4_fmu-v6x_default.bin` | 1,947,412 | `e9b4d41abca9771fa95b9ab5d7c7f85035b845c9c041891eadfb7aab0d59a5cb` |
| `v6x-build/px4_fmu-v6x_default.px4` | 1,854,006 | `37262c835cf4beeda84cf2c1765f297407ceed13e9386530f55cb5e9ddd2a9ff` |

The ten **unlinked** research ARM objects total 7,501 text bytes and zero
data/BSS. H7 owner and scheduling adapter contribute 1,768 and 418 text bytes.
This is compilation evidence, not linked firmware cost or zero runtime RAM.
The default linked Pixhawk ELF reports 1,939,400 text, 8,012 data and 105,596 BSS;
flash is 1,947,412 bytes. Both default ELFs contain zero
`research_actuator_command::` symbols, so these linked resource figures exclude
H1-H7. No H7 runtime, PWM timing, scheduler deadline or physical neutral action
was exercised on hardware. No flash, actuator operation, commit or Git staging
occurred. Gate advanced: **H7 output-disabled servo-bench candidate/lifecycle
foundation only**. Physical output enablement and flight readiness remain open.
