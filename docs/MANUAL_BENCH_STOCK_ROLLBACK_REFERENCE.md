# Pixhawk 6X stock rollback reference and cold boot check

2026-10-19 | Codex | Step 2 of the startup recovery sequence

The user has completed step 1 and restored the working rollback. A clean stock
reference is now built from that rollback's exact PX4, NuttX and apps revisions.
Software artifact checks pass. The supplied console verifies successful startup
and USB MAVLink; the user confirms all three cold-start timing/stability checks
passed. Step 2 is complete with a deliberate reset to stock settings accepted as
the starting configuration. The [manual bench port](MANUAL_BENCH_WORKING_BASE_PORT.md)
now passes fresh software qualification on this complete baseline. The user also
reports its changed firmware passed the cold-boot tests and the module is stopped;
its separate startup report and next disconnected input checks are in that record.

## Recorded hardware result

The [final hardware record](../build/bench_commissioning/stock-e8ea98b404-nuttx11-reference/hardware-startup-acceptance-20261019.json)
retains the user's three-cycle confirmation and subsequent clarification:
"The reset was deliberate. Parameter backup was not required."
The [supplied console](../build/bench_commissioning/stock-e8ea98b404-nuttx11-reference/hardware-console-user-supplied-20261019.txt)
identifies the expected PX4/NuttX hashes and an October 19, 2026, 21:23:49 build.
It reaches the ICM45686 sensors, PX4IO CRC match, CDC/ACM and USB MAVLink with a
valid GCS heartbeat, zero USB transmit errors and zero reported receive loss.

All three full power-off/reconnect cycles met the requested connection within
30 seconds and stable connection for at least 60 seconds, as confirmed by the
user. One complete console transcript was supplied; exact per-cycle times and
three separate transcript sets were not supplied.

The logged boot loads a valid 1,394-byte parameter document with 22 integers and
41 floats, `SYS_AUTOSTART=2100` and `SYS_USB_AUTO=2`. The earlier rollback log
loaded 1,692 bytes. The user confirms near all nonstock settings were reset
deliberately. This is an intentional fresh configuration, not evidence of an
unexplained storage failure; no preflash parameter recovery is required for this
run. Calibration preservation and output setup are not established by this test.

The airspeed preflight warning, separate Ethernet errors and MAVLink file-stat
message remain in the retained transcript; they did not prevent the demonstrated
USB connection. No arming, calibration, actuator or stabilized-control gate
passes from this result. The build-time receipt and blank form remain unchanged;
the separate hardware record supplies the later startup result.

## Stock reference firmware

Use this [stock reference firmware](../build/bench_commissioning/stock-e8ea98b404-nuttx11-reference/px4_fmu-v6x_default_stock_e8ea98b404_nuttx11_clean_20261019T212447Z.px4).
Its directory relative to the project is:

```text
build/bench_commissioning/stock-e8ea98b404-nuttx11-reference/
```

| Item | Verified value |
| --- | --- |
| Target | `px4_fmu-v6x_default`, package board ID **53** |
| PX4 | `e8ea98b404baf50623a519ac67ec1c03aa240859` |
| NuttX | **11.0.0**, `49f3b29c92d7af6feb94747d4a11f348292cc41b` |
| NuttX apps | `e37940d8535f603a16b8f6f21c21edaf584218aa` |
| Compiler | `arm-none-eabi-gcc (15:13.2.rel1-2) 13.2.1 20231009` |
| Build source | Clean detached stock source; no research changes |
| Package size | 1,844,295 bytes |
| Decoded image / slot | 1,948,344 / 1,966,080 bytes; 17,736 bytes remaining |
| Package SHA256 | `315f014c6acf50106fa51b1b345c6c9f3833dc8a3126ec9a69d1461ba6cf6334` |

The [build receipt](../build/bench_commissioning/stock-e8ea98b404-nuttx11-reference/comparison-receipt.json)
pins eleven submodules, Micro-CDR, the compiler, recipes and four artifacts.
The [independent validation](../build/bench_commissioning/stock-e8ea98b404-nuttx11-reference/artifact-validation.json)
checks those hashes, package/bin equality, version headers and image bounds.
All 989 compile entries exclude research sources. The ELF defines NSH, board
initialization, MAVLink and CDC/ACM autostart; it excludes the research bench
entry point. These are software checks, not proof of application startup.

This rebuild matches the known working source revisions. Byte-for-byte identity
with the original rollback is unestablished because its original binary/hash
was not supplied. The detached branch name and build date may differ; use the
full PX4 and NuttX hashes to verify the running software.

## Build metadata and retained source

Both historical version generators select the newest fetched NuttX release tag.
The first rebuild therefore labelled the correct old kernel source as 12.12.0.
It was superseded before hardware testing. For this final build, the private
NuttX clone excludes release tags newer than `nuttx-11.0.0`; the retained
`nuttx-version-tag-snapshot.json` records the refs. The generated kernel version
file and PX4 version header both now report 11.0.0. No tracked PX4/NuttX source
or host repository tags changed.

The pinned Docker image is `px4io/px4-dev:v1.17.0`, image ID
`sha256:288dc1d7f5290855e86dbaf8b8f55cd17f46f27e9df2affb2056df6363bc6fb0`.
`build-recipe.json`, preparation/build/export scripts, logs, source archive,
configuration, compile commands, SBOM, version files and ELF/bin/map remain
beside the package. The successful build exited 0.

The source resides at `/work/px4-stock-e8ea` in Docker volume
`px4-stock-e8ea-reference-20261019-nuttx11`. Its detached metadata worktree is
`.agents/worktrees/build-e8ea98b404`; tracked files are materialized in the
Docker volume. Retain the worktree metadata while retaining that build volume.
The active `slaaps-sitl` checkout was not switched. A temporary firmware copy is
also under WSL `/tmp/px4-firmware/`; use the persistent project copy above.

## Flash and verify on the controller

This test establishes a stock USB/QGC boot reference. Keep servo and EDF power
disconnected throughout; the stock image does not implement the SLAAPS bench
controller or stabilized canard control. Codex has not flashed this package.

1. Retain the working rollback firmware. Record the controller identity, QGC
   version and intended starting values of `SYS_AUTOSTART` and `SYS_USB_AUTO`.
   For a test requiring configuration retention, retain the preflash parameter/SD
   backup. In this recorded run the user deliberately reset to stock and did not
   require a parameter backup or restoration of the previous settings.
2. In QGC's firmware page, follow its unplug/replug prompt and select the exact
   custom `.px4` linked above. Check board ID 53 and retain the entire upload
   log, including verification and upgrade completion.
3. After upload, perform **three full cold starts**. For each start, disconnect
   every controller power source, confirm its LEDs are off, then reconnect USB.
   Count the first post-upload cold start as cycle 1.
4. For each cycle, record elapsed time to QGC connection and observe the
   connection for at least **60 seconds**. Use **30 seconds** as this test's
   connection-time limit. These are comparison criteria, not a manufacturer
   timing guarantee. An upload-complete message alone does not pass the test.
5. On each connected cycle, open Analyze Tools → MAVLink Console. Run the
   following commands individually and save the complete outputs:

   ```text
   ver all
   dmesg
   mavlink status
   param show SYS_AUTOSTART
   param show SYS_USB_AUTO
   ```

6. Confirm `ver all` identifies `PX4_FMU_V6X`, the PX4 hash above, NuttX 11.0.0
   and the NuttX hash above. Confirm boot reaches sensor/PX4IO initialization
   and USB MAVLink. Check the intended starting parameter values, with no new
   parameter-storage errors or uncommanded resets during the observation.
   If configuration retention is required, also compare calibration and saved
   settings with the preflash backup; the deliberate reset in this run replaces
   that retention requirement with the accepted stock starting configuration.
7. Copy `cold-boot-record.template.json` beside the firmware to a separate result
   file and fill all three cycle records. Save QGC logs, console outputs and a
   parameter export with it. Existing airspeed/heading preflight warnings must
   be recorded; this startup test does not require arming or bypassing checks.

Pass step 2 when **all three cycles** meet the connection, identity, intended
configuration and stability criteria. Then develop and separately qualify the bench
port on this baseline. The [commissioning procedure](MANUAL_BENCH_FLASH_AND_TEST_PROCEDURE.md)
still requires disconnected PWM/fault measurements before powered tests.

If a cycle fails, retain the elapsed time, Device Manager observation and upload
log, then restore the known working rollback. Keep this rebuild's matching
ELF/map with the evidence. A failure here would require further build/startup
diagnosis before the port; bootloader modification and parameter reset are not
established fixes.
