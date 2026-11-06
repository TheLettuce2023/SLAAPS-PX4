# SLAAPS Pixhawk 6X: flash and first boot

Prepared 2026-10-28. The project lead flashes and powers the board; nothing here is run automatically. This
procedure covers flashing and boot acceptance only: no outputs, no EDF, no arming.

## Images

| Role | File (under `build/`) | SHA-256 |
| --- | --- | --- |
| New SLAAPS image | `slaaps_firmware/flight-readiness-20261028/px4_fmu-v6x_slaaps.px4` | `5a0ee385ffa0363c5db55c2a35a352442520bc8c7cd81915ab08793331674fd6` |
| Rollback (stock, known to boot) | `bench_commissioning/stock-e8ea98b404-nuttx11-reference/px4_fmu-v6x_default_stock_e8ea98b404_nuttx11_clean_20261019T212447Z.px4` | `315f014c6acf50106fa51b1b345c6c9f3833dc8a3126ec9a69d1461ba6cf6334` |
| Rollback (bench port) | `bench_commissioning/bench-e8ea98b404-step3/px4_fmu-v6x_default_bench_e8ea98b404_nuttx11_dirty_20261019T225405Z.px4` | `a45cf99374fa7ccfd4f6349ca9a6341bacfcfa6199254f71ab2b04f0cb2aed31` |

The new image declares board ID 53 and is 1,865,324 B, under the bootloader slot (1,966,080 B). Its embedded git identity
reads `v1.18.0-beta1-690-g9469f2356be`. That is the build workspace's base commit, not the full source. The SHA-256 above
is the identity; the source is described in that folder's README.

Check the hashes from PowerShell in the checkout before flashing:

```powershell
$images = @{
  'build/slaaps_firmware/flight-readiness-20261028/px4_fmu-v6x_slaaps.px4' = '5a0ee385ffa0363c5db55c2a35a352442520bc8c7cd81915ab08793331674fd6'
  'build/bench_commissioning/stock-e8ea98b404-nuttx11-reference/px4_fmu-v6x_default_stock_e8ea98b404_nuttx11_clean_20261019T212447Z.px4' = '315f014c6acf50106fa51b1b345c6c9f3833dc8a3126ec9a69d1461ba6cf6334'
}
foreach ($path in $images.Keys) {
  $actual = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
  if ($actual -ne $images[$path]) { throw "Hash mismatch: $path" } else { "OK $path" }
}
```

## Known risk

The board carries an ArduPilot ChibiOS bootloader (board ID 53). A stock image on this newer PX4/NuttX 12 base previously
programmed successfully but did not start, while the NuttX 11 rollback does. This image includes the bounded RNG/HSI48
startup waits, a candidate fix for that failure. It is not a proven one.

No debug connector is available, so there is no serial console. Boot output is read after boot over USB: QGC > Analyze
Tools > MAVLink Console runs NSH commands, and `dmesg` replays the boot log retained in the console buffer (both are in this
image). A board that fails before USB enumerates gives no log. In that case the evidence is what QGC shows, plus the
rollback result.

## Before flashing

1. Bench setup: USB power only. No propulsion battery, EDF ESC unpowered, servo horns free or unloaded.
2. In QGC on the current firmware: Parameters > Tools > Save to file, as a backup. Note the current firmware version.
3. Note exactly what QGC shows on each boot: connected vehicle, bootloader only, or nothing. That is the only evidence
   if the board does not come up.

## Flash

1. QGC > Vehicle Setup > Firmware. Unplug the board's USB, then plug it back in when prompted.
2. Choose PX4, tick Advanced settings, select Custom firmware file, and pick the new SLAAPS image.
3. Wait for QGC to report programming and verification complete. Do not unplug during erase or write.

## First boot acceptance

Unplug USB, wait 5 s, plug in. Repeat three cold starts.

Run the commands below in QGC > Analyze Tools > MAVLink Console, and copy each output into a text file.

| Check | Pass |
| --- | --- |
| QGC | Connects over USB within 30 s and stays connected for at least 60 s, on each of three cold starts |
| `dmesg` | Boot log from reset with no hard fault or repeated reset; save it in full |
| `ver all` | Prints the PX4 identity above and a NuttX 12 version |
| `ls /dev/random /dev/urandom` | Both present. Absent means RNG initialization refused (HSI48 not ready); report it, do not continue |
| `dmesg` content | No `ERROR` from sensors or `px4io` beyond what the stock rollback also shows |
| `slaaps status` | The SLAAPS supervisor is running and reports its three modes |

## Airframe and parameters

Do these only after boot acceptance passes.

1. QGC > Airframe: select "SLAAPS X-canard EDF" (SYS_AUTOSTART 22001), apply and reboot. Selecting an airframe resets
   parameters to its defaults.
2. Parameters > Tools > Load from file: `Tools/slaaps/parameters/slaaps_vehicle.params`. Reboot.
3. Leave the vehicle profile un-applied. Output wiring, data-record declaration and the profile apply are the next
   procedure.

## If it does not boot

1. If QGC shows only the bootloader, or nothing, after a cold start: unplug and replug USB, then flash the stock rollback
   image (SHA-256 `315f014c...`) the same way and confirm it reconnects.
2. Keep the QGC log (Application Settings > Console, save) and note what QGC showed on each attempt.
3. Do not try other images or change the bootloader.

## Send back

The output of `dmesg`, `ver all`, `ls /dev/random /dev/urandom` and `slaaps status`, and the result of the three cold
starts. On a failure, the QGC log, what QGC showed, and the rollback result.

## Result 2026-10-28

The SLAAPS image (`5a0ee385...`) programmed and verified under the ArduPilot ChibiOS bootloader (protocol 5, board ID 53,
flash 1,966,080). QGC never reconnected to PX4 after "Rebooting board". The stock rollback still connects. This
reproduces the earlier newer-base failure, so the RNG/HSI48 fix is not the (whole) cause. Decision (lead): confirm the
cause first by flashing a stock image built just before the NuttX 12.12 upgrade (`bbcd20abe3`). If that boots, rebuild
SLAAPS on this branch with the pre-upgrade NuttX/apps kernel, keeping the ArduPilot bootloader.

## Cause confirmed and fix (2026-10-28)

The stock image at `7aa85790d9` (the last commit before the NuttX 12.12 upgrade; SHA-256 `4a27f48f...`) boots under the
ArduPilot bootloader and stays connected. So the NuttX 12.12 upgrade is the cause. The mechanism is the one PX4 fixed in
its own bootloader (`b4c9d109af`). NuttX 12 runs thread mode on the process stack. An application entered from a bootloader
that is itself on its process stack (ChibiOS, like the ArduPilot bootloader) keeps that stack, while its startup sets up a
main stack the CPU never uses. It then crashes once the bootloader's old stack memory is reused.
Fix (lead's choice over rebasing onto the old kernel): the armv7-m reset stub (`arch/arm/src/armv7-m/arm_vectors.c`) now
loads MSP from vector zero and clears `CONTROL.SPSEL` before `__start`, as a hardware reset does. The image no longer
depends on how the bootloader jumps. Test image: `build/slaaps_firmware/flight-readiness-20261028-msp/`. Fallback if it
does not boot: rebuild SLAAPS on the pre-upgrade NuttX/apps kernel.

## Boot accepted and next: output wiring (2026-10-28)

Image `0286ab59...` (NuttX 12.12 with the MSP reset fix) passed three cold starts; the SLAAPS airframe (22001) loads,
the supervisor registers Manual 23 / Acceleration 24 / Bench 25, and after calibration EKF2 reports tilt and yaw aligned
with magnetometer heading (no GPS, inertial dead reckoning). The QGC parameter file is loaded; the profile is not applied.

Output wiring. Battery disconnected and the ESC unpowered.
MAIN is the PX4IO rail (bank 0, PWM only; timer groups MAIN1-2, MAIN3-4, MAIN5-8). AUX is the FMU rail (bank 1): AUX1-4
share Timer 5 and AUX5-6 share Timer 4, both DShot-capable. AUX7-8 (Timer 12) cannot run DShot.
QGC's parameter editor enforces PX4IO's ranges (disarmed and failsafe 800-2200), but SLAAPS requires 0 (no pulse) there.
Enter these in Analyze Tools > MAVLink Console, one line at a time, as the earlier bench procedure did. Stop on any
rejection, and read every value back with `param show` after the reboot.

```text
param set PWM_MAIN_FUNC1 10100
param set PWM_MAIN_FUNC2 10101
param set PWM_MAIN_FUNC3 10102
param set PWM_MAIN_FUNC4 10103
param set PWM_MAIN_MIN1 1333
param set PWM_MAIN_MIN2 1333
param set PWM_MAIN_MIN3 1333
param set PWM_MAIN_MIN4 1333
param set PWM_MAIN_CENT1 1500
param set PWM_MAIN_CENT2 1500
param set PWM_MAIN_CENT3 1500
param set PWM_MAIN_CENT4 1500
param set PWM_MAIN_MAX1 1667
param set PWM_MAIN_MAX2 1667
param set PWM_MAIN_MAX3 1667
param set PWM_MAIN_MAX4 1667
param set PWM_MAIN_DIS1 0
param set PWM_MAIN_DIS2 0
param set PWM_MAIN_DIS3 0
param set PWM_MAIN_DIS4 0
param set PWM_MAIN_FAIL1 0
param set PWM_MAIN_FAIL2 0
param set PWM_MAIN_FAIL3 0
param set PWM_MAIN_FAIL4 0
param set PWM_MAIN_REV 0
param set PWM_MAIN_TIM0 333
param set PWM_MAIN_TIM1 333
param set PWM_AUX_FUNC5 10104
param set PWM_AUX_TIM0 0
param set PWM_AUX_TIM1 -3
param set PWM_AUX_TIM2 0
param set PWM_AUX_TIM3 0
```

| Values | Why |
| --- | --- |
| FUNC 10100-10103 | SLAAPS Canard 1-4 on MAIN1-4; 10104 SLAAPS EDF on AUX5 |
| MIN/CENT/MAX 1333/1500/1667 | Must equal SLA_Cn_PWM_NEG/CTR/POS exactly (swap MIN and MAX roles only with PWM_MAIN_REV) |
| DIS/FAIL 0 | Canards revoke to no pulse; any other value is rejected |
| PWM_MAIN_TIM0/1 333 | 333 Hz servo rate for MAIN1-4 (1667 us x 333 Hz < 1 s) |
| PWM_AUX_TIM1 -3 | DShot600 for AUX5-6; DShot is accepted only for the EDF |
| PWM_AUX_TIM0 0, PWM_AUX_TIM2 0, PWM_AUX_TIM3 0 | AUX1-4, AUX7-8 and the ninth FMU output (AUX9/capture, Timer 1) unused, so pwm_out drives nothing. Otherwise pwm_out and dshot both report the AUX bank and SLAAPS rejects the inventory (`inventory_fault=4`, duplicate bank). Needs the image in `flight-readiness-20261029-edf` |

The DShot driver sets its own range (1-1999) and disarm frame and ignores PWM_AUX_MIN/MAX/DIS/FAIL. Before the
2026-10-28 DShot fix it reported an unresolved failsafe marker, which SLAAPS rejects. The EDF channel therefore needs
the image in `build/slaaps_firmware/flight-readiness-20261028-dshot/`. Afterwards `slaaps status` should show
`routing snapshot=1` and `routing_fault=0`; a remaining fault names the exact bank and channel.

Routing accepted on the board (2026-10-29, image `e515e07f...` in `flight-readiness-20261029-edf`). After the MAIN
limits were entered in the console, saved and the board rebooted, `listener slaaps_output_bank` shows exactly two banks:
PX4IO bank 0 with MAIN1-4 at 1333/1500/1667 us, disarmed and failsafe 0, 333 Hz; DShot bank 1 with AUX5 only
(mask 16, DShot600, range 109-1999, disarm and failsafe 0, ready). `slaaps status`: `routing snapshot=1
inventory_fault=0 routing_fault=0`, `configuration valid=1 use=1 apply=2`, `flight unavailable=6` (flight admission
absent while disarmed on USB power, as designed). The first attempt had only the FUNC values set (QGC Actuators page);
the bank then reported PX4IO defaults and `routing_fault=8` (invalid calibration) on MAIN1.

Then declare the data record (SLA_DATA_REC, SLA_DATA_REV, SLA_DATA_STATE = Calibrated), set SLA_CFG_KIND = 1 and increase
SLA_CFG_APPLY while disarmed. `slaaps status` should show `configuration valid=1 use=1`.

## Servo bench (2026-11-05 and 2026-11-06)

The servo bench runs on the board (image `flight-readiness-20261105-benchdiag5`). It needs, disarmed:
`COM_PREARM_MODE` set so `actuator_armed.prearmed` is true; SLA_CFG_KIND 0 with an increased SLA_CFG_APPLY; and
SLA_IN_INST set to the joystick's MAVLink instance (2 for USB). After any rejection, leave and re-enter the mode and
re-apply. Bench-only timing used with the QGC joystick over USB, whose packets arrive with gaps up to about 100 ms:
SLA_OUT_LEASE 250, SLA_BENCH_GAP 100, SLA_FIRST_CMD_US 40000, SLA_ACT_ACK_US 35000. Restore and re-derive these
before Manual. The first enable crashed the board: PX4IO runs on `wq:ttyS5`, whose 2 KiB stack overflowed; the board
file now reserves 4 KiB.

The combined test found the servo 3 and 4 plugs swapped and canards 2 and 4 turning opposite to the recorded positive
sense (see `docs/PROJECT_CONTEXT.md`, Canards and servos). Both are fixed; the parameter file carries the new slopes and
bench mix.

From image `flight-readiness-20261106-benchsec`, SLA_BENCH_SEC accepts up to 3600 s for servo targets
(SLA_BENCH_CH 0-4) so one session can run for a whole check. The EDF target keeps 60 s; a longer duration waits at
bench entry=1. `SLA_BENCH_EN 0` and leaving the mode stop a session at once; input loss stops it SLA_IN_AGE after the
last sample.

Current image: `flight-readiness-20261106` (`444f2e5f...`), without the diagnostic status lines. In it a pilot request
lives for SLA_IN_AGE after its stick sample, independent of SLA_OUT_LEASE, so the QGC joystick no longer needs the
250 ms output lease: SLA_IN_AGE 300 with SLA_OUT_LEASE 100 covers its gaps. The bench still needs SLA_BENCH_GAP 100,
SLA_FIRST_CMD_US 40000 and SLA_ACT_ACK_US 35000; restore 50 / 8000 / 3000 before Manual, whose first-command budget
must stay below SLA_CYCLE_MAX.

Mechanical centring (from image `flight-readiness-20261106-trim2`): SLA_Cn_ZERO, in degrees, per canard, up to +-10.
It shifts SLA_Cn_PWM_NEG/CTR/POS by the same whole number of microseconds (about 11.1 us/deg here), so command zero, the
Bench physical zero and the full +-15 deg travel are centred on the true zero. Positive moves toward SLA_Cn_PWM_POS,
which turns every canard counter-clockwise viewed from outboard: a canard resting 1 deg clockwise of true zero needs
SLA_Cn_ZERO = +1. On each successful apply SLAAPS writes the shifted pulses to PWM_MAIN_MIN/CENT/MAXn (or PWM_AUX_...)
of the output that carries the canard, and the routing check requires the output to report them; it also rewrites them
if they were edited by hand. The shifted pulses must stay within 500-2500 us. Leave SLA_Cn_PWM_NEG/CTR/POS at the servo's
own calibration (1333/1500/1667). Procedure: leave the SLAAPS mode, set SLA_Cn_ZERO, increase SLA_CFG_APPLY (the console
prints "canard centre correction: N output limits written"; a rejection names the parameter), check `slaaps status` for
`routing_fault=0`, re-enter Bench with SLA_BENCH_CH n and the stick centred, and repeat until centred.
SLA_Cn_TRIM is the modeled zero-moment flight trim, not a mechanical offset.
