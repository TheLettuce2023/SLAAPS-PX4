# H10 hardware startup failure diagnosis

2026-10-13, Codex. The user reports successful QGC programming of the staged H10
image, followed by no normal QGC connection and a device labelled as a bootloader.
Reflashing the rollback and cycling USB restores normal QGC operation. H10 is
withdrawn from routine commissioning while the failure is diagnosed. Preserve its
binary and software evidence; no replacement image or bootloader change is accepted.

**2026-10-19 update:** The user also reports the clean stock comparison fails to
connect after programming and power cycling, using either the `.px4` or a `.bin`.
The supplied QGC Daily log reports successful programming/verification on an
ArduPilot ChibiOS bootloader, protocol revision 5, board ID 53 and application
flash size 1,966,080 bytes. The working rollback still reconnects after this
latest attempt. The subsequent independent readback now matches the stock
image's stored vectors and full application-slot CRC. This rules out missing
upload finalization for the measured attempt; application handoff/startup remains
unlocalized. The readback issued no flash, boot or parameter commands; the
bootloader was not modified.

The user subsequently completed rollback recovery and requested recovery step 2.
A [stock reference on the working rollback baseline](MANUAL_BENCH_STOCK_ROLLBACK_REFERENCE.md)
is now built and software-validated at PX4 `e8ea98b404`, NuttX `49f3b29c92`
and apps `e37940d85`. Its private version metadata is pinned at NuttX 11.0.0;
the initial rebuild with newer tag-derived labels was superseded before testing.
The final package SHA256 is
`315f014c6acf50106fa51b1b345c6c9f3833dc8a3126ec9a69d1461ba6cf6334`.
The supplied console now confirms matching PX4/NuttX identities, sensor/PX4IO
initialization and USB MAVLink with a valid GCS heartbeat. The user confirms
three cold starts each connected within 30 seconds and stayed connected for
at least 60 seconds. Step 2 startup acceptance is recorded separately from the
immutable build receipt. The user deliberately reset settings to stock and did
not require a parameter backup; this is the accepted fresh configuration.
This stock build excludes the bench controller. Calibration/output acceptance
is not established, and the newer baseline's startup failure remains unlocalized.
The successful run also used the deliberate settings reset, so these attempts
do not isolate a software-baseline regression from a configuration effect.

Recovery step 3 has now produced a [freshly qualified bench port](MANUAL_BENCH_WORKING_BASE_PORT.md)
on that complete working baseline. Its `.px4` SHA256 is
`a45cf99374fa7ccfd4f6349ca9a6341bacfcfa6199254f71ab2b04f0cb2aed31`.
The port preserves H10's clock ordering and includes the older-base sender-ID
and center-validation dependencies. All three 20-scenario native suites pass;
the ARM candidate fits flash and excludes the private fixture. The user now reports
its cold-boot tests passed and its bench module is available but stopped. A later
console matches the candidate's base/OS/build identities, reaches PX4IO and
confirms selected USB input instance 2 / sender 255:190. Axis/button/link review,
remaining startup/configuration evidence and G3-G4 output checks remain open;
the original withdrawn images are not reinstated.

## Known working and failed configurations

| Item | Working rollback | H10 candidate |
| --- | --- | --- |
| Board | PX4_FMU_V6X, V6X, FMUM ID 0x008, base ID 0x000 | Target px4_fmu-v6x_default, package board ID 53 |
| MCU | STM32H7[4\|5]xxx, revision V | Same target family; runtime identification not reached |
| PX4 commit | e8ea98b404baf50623a519ac67ec1c03aa240859 | e880f1cd79e2453ef3dfaf68ee1c067b98ea9a53 plus research changes |
| NuttX | 11.0.0, 49f3b29c92d7af6feb94747d4a11f348292cc41b | 12.12.0, 0288ea632c598d3b40fe8ab847b41e9e7a018b8f |
| NuttX apps required by PX4 | e37940d8535f603a16b8f6f21c21edaf584218aa | 4319b404bcfd5df1d363cb6456ed6e8977199b67 |
| Compiler | GNU GCC 13.2.1 | GNU GCC 13.2.1 |
| Hardware outcome | Normal sensor, PX4IO and USB MAVLink startup | Programming reported successful; normal application connection not established |

The working rollback's supplied `dmesg` reaches three ICM45686 sensors, a PX4IO
firmware CRC match, CDC/ACM autostart and Onboard MAVLink on `/dev/ttyACM0`, with
`SYS_USB_AUTO=2`. Airspeed-invalid, heading-invalid and magnetic-interference
warnings occur after startup. They do not explain failure to reach USB/MAVLink
startup in the other image.

The difference in PX4/NuttX baseline is established. It is not yet proven to be
the cause of the failed connection. There is no failed-image boot trace, crash
address or confirmed point of startup failure.

## SD-card evidence from the working rollback

The user subsequently ran `hardfault_log check` with no visible shell output.
The supplied top-level `/fs/microsd` listing contains no `fault_*.txt` file. It
does contain `param_import_fail.bson`, `param_import_fail.txt`,
`parameters_backup.bson`, `dataman`, `log/`, `ufw/`, `uavcan.db/`,
`System Volume Information/`, `APM/`, `mavlink/` and `logdata.txt`.
These observations do not supply a crash dump or prove that no hardfault occurred.
The check reports status through system logging, so silent shell output alone
does not establish its return status or a fault address.

Both the working rollback and H10's `ROMFS/px4fmu_common/init.d/rcS` use the same
parameter-load failure handler: when `param load-or-init` fails, debug capture
copies the failed storage into `param_import_fail.bson`, attempts backup recovery,
and appends `dmesg` to `param_import_fail.txt`. This handler does not exit startup.
The filenames do not identify which firmware or boot created them; existing
files may predate H10 and the text may include multiple captures. The working
rollback boot log supplied earlier reports parameters loaded from storage.

The user supplied the existing file's text and an `ls -l` size of 7,542 bytes;
the listing did not include a date. The captured identities and recovery are:

| Captured build | Parameter observation |
| --- | --- |
| PX4 1.16.1, `94cb2012792b2ae89f0b147cfee53ee31ae550be`, NuttX 11.0.0 `886acbbdb4f061e5c0ce1a76afbcfa7cb7df9849`, built January 21, 2026, GCC 9.3.1 | Two captures reject a BSON size of 0 with 5 bytes decoded; one reaches a network-settings reboot, another board defaults. |
| Working rollback PX4 1.18.0, `e8ea98b404baf50623a519ac67ec1c03aa240859`, NuttX 11.0.0 `49f3b29c92d7af6feb94747d4a11f348292cc41b` | Rejects a BSON size of 409936 with 5 bytes decoded, then successfully imports a 1300-byte SD backup with 19 integers and 39 floats. |

The rollback recovery section includes these relevant lines:

```text
ERROR [parameters] BSON document size (409936) doesn't match bytes decoded (5)
ERROR [param] parameter storage is corrupt (-1)
ERROR [init] param load-or-init failed
[init] importing from parameter backup
INFO  [parameters] BSON document size 1300 bytes, decoded 1300 bytes (INT32:19, FLOAT:39)
```

That backup contains sensor calibration and `SYS_AUTOSTART=4001`. The separately
supplied working boot log loads a valid 1692-byte parameter document and airframe
2100. These are distinct captures, and their order relative to the H10 attempt
is not established. No supplied section identifies H10 or NuttX 12.12.0.
The log proves stored-parameter failures and rollback backup recovery; it does
not establish which image caused corruption or where H10 stops booting.
Preserve both failure files and the backup; a parameter reset is not a proven fix.

The exact V6X `src/mtd.cpp` partition manifest is unchanged between rollback and
H10's base. Its parameter device is SPI FRAM at `/fs/mtd_params`. Common MTD code
and kernel storage drivers do change with the OS transition; an unchanged layout
does not qualify those drivers on the installed board.

The user has no FMU debug adapter available. A stock comparison build on H10's
exact base commit is now compiled and staged as a diagnostic alternative. It
excludes the research changes, uses target `px4_fmu-v6x_default`, pins the required
submodules, and records its compiler and artifacts. On October 19 the user
reports the same missing connection with this stock image. This establishes
that the failure also occurs without research code; it does not identify an OS,
driver, upload-finalization or bootloader handoff root cause.

| Stock comparison artifact | Verified value |
| --- | --- |
| Source | Clean `e880f1cd79e2453ef3dfaf68ee1c067b98ea9a53`, detached build checkout |
| Toolchain | `arm-none-eabi-gcc (15:13.2.rel1-2) 13.2.1 20231009`, same package/version string as H10 |
| Container | `px4io/px4-dev:v1.17.0`, digest `sha256:288dc1d7f5290855e86dbaf8b8f55cd17f46f27e9df2affb2056df6363bc6fb0` |
| Target/package | `px4_fmu-v6x_default`, board ID 53, 1,854,454-byte `.px4` |
| Firmware SHA256 | `35945a63a8cf64bd1f80175cb975ac8063994b66fd7e9a29997c93545c065218` |
| Linked/decoded image | 1,947,404 bytes; 18,676 bytes below the 1,966,080-byte limit; decoded package equals the linked `.bin` |
| ELF checks | NSH, board initialization, MAVLink and CDC/ACM entry points present; research bench entry point absent |
| Build/hardware status | Build exited 0; source remained clean; user reports no normal QGC connection after successful upload; no manual output or physical acceptance claimed |

The [stock comparison firmware](../build/bench_commissioning/stock-e880f1cd79-diagnostic/px4_fmu-v6x_default_stock_e880f1cd79_clean_20261013T200117Z.px4)
and [receipt](../build/bench_commissioning/stock-e880f1cd79-diagnostic/comparison-receipt.json)
are retained with matching ELF, binary, map, build configuration and logs.
The receipt records all eleven required submodule commits and the separately
fetched Micro-CDR v2.0.1 commit `3d1b17703c7cf4f22def2910bc845bdb5152d7b5`.
Initial preparation failures from path spaces and a missing nested/downloaded
dependency are preserved separately from the successful final build log.

The detached worktree `.agents/worktrees/build-e880f1cd79` and isolated Docker
source volume are retained for diagnosis. The build used a space-free Linux
copy of the exact committed source with private dependency clones; it did not
overwrite the user's branch, research files or original submodule sources.

### October 19 upload evidence and independent flash readback

The user supplied this QGC sequence for the exact stock comparison package:
PX4 FMU V6X on COM8, bootloader re-enumeration as ArduPilot ChibiOS on COM14,
protocol 5, board ID 53, flash size 1,966,080, erase/program/verify, reboot and
upgrade complete. The full report is retained in
[hardware-comparison-failure-20261019.json](../build/bench_commissioning/stock-e880f1cd79-diagnostic/hardware-comparison-failure-20261019.json).
The `.bin` symptom is user-reported; its selected file/hash was not captured.
The user confirms that the original rollback still reconnects after these tests.

Protocol revision 5 is not an exact bootloader firmware build ID. Windows' cached
USB records also contain the ArduPilot bootloader VID/PID `1209:5741` on COM14
and the PX4 USB VID/PID `3185:0035` on COM8. An ArduPilot label alone does not
make PX4 the wrong application: these bootloaders implement a PX4-compatible
protocol and the reported target/flash layout match the candidate.
[ArduPilot protocol source](https://github.com/ArduPilot/ardupilot/blob/master/Tools/AP_Bootloader/bl_protocol.cpp),
[QGC interoperability discussion](https://github.com/mavlink/qgroundcontrol/issues/7318).

The upstream ArduPilot implementation buffers the first eight words (32 bytes)
while programming, substitutes those buffered words into its upload CRC, and
commits them during the final BOOT command. Consequently, upload verification
alone does not establish the stored reset vectors after a power cycle. This is
a testable distinction, not a finding that finalization failed on this board.
The installed bootloader's exact build remains unknown. The independent readback
result below now confirms the expected stored image for the measured attempt.

Both retained failing images contain the same valid-range first eight vector
words: initial stack `0x2401b62a`, reset entry `0x080202a1`, then six
`0x080202ab` entries. None of these first eight words is erased. This check
rules out an erased word already present in the candidate file; it does not
read what the controller retained or demonstrate correct CPU startup.

The [inspection tool](../Tools/bench_test/inspect_bootloader.py) queries only
GET_SYNC, GET_DEVICE, READ_MULTI, GET_CRC and optional GET_VERSION. It reads the
first 32 bytes, obtains a slot CRC and compares both with the retained stock
package. It issues no erase, program, finalize, boot, reboot or parameter command.
It refuses the PX4 application USB identity and any board/slot/protocol mismatch.
The optional version query may be unsupported by an older protocol-5 build.
Identification can hold the loader until the next power cycle.

Prepare the measurement with actuator/EDF energy disconnected:

1. If the rollback is installed, reproduce the known stock failure using the
   same identified stock `.px4` through QGC. This repeat is for readback only.
2. Exit QGC completely. Disconnect every controller supply, including USB,
   then reconnect USB with the failed stock image still installed. This clears
   upload-session RAM; do not enter QGC's firmware screen again before the read.
3. Confirm the ArduPilot bootloader's current COM port in Device Manager. Use
   COM14 below only if that remains its assigned port. Do not select COM8 merely
   because it was used by the working application.
4. From the repository root in PowerShell, run the already-prepared private
   environment and inspector:

   ```powershell
   & .\build\bench_commissioning\bootloader-readback-venv\Scripts\python.exe .\Tools\bench_test\inspect_bootloader.py --port COM14 --wait-seconds 60
   ```

5. Preserve the JSON printed and the automatically named `cold-readback-*.json`
   under the stock comparison directory. An unavailable/busy port or protocol
   error is a failed inspection, not a flash mismatch. Recover using the retained
   rollback after collecting evidence if needed.

For the exact stock package, the expected application-slot CRC is `0xde5dd361`;
the CRC with only its first 32 bytes erased is `0xf573da68`. The tool pads the
image to the actual 1,966,080-byte slot with `0xff` using the existing uploader's
CRC convention. Its result distinguishes:

| Readback | Implication |
| --- | --- |
| Expected vectors and expected slot CRC | The measured flash contents match the candidate; investigate launch/early application startup next. It still does not prove that the application executes. |
| Erased first 32 bytes and `0xf573da68` | Candidate body matches while the lead vector line is missing; investigate upload finalization before changing firmware or bootloader. |
| Other mismatch | Save actual values; do not identify a particular failing subsystem from a checksum mismatch alone. |
| CRC matches but vector read is unavailable | Weaker evidence; preserve the unsupported-query result and do not claim the vectors were observed. |

Sixteen hardware-free serial fixtures pass, including fragmented/truncated
replies, erased-vector versus buffered-CRC cases, rejected optional version
queries, incorrect USB/board/layout refusal, brief USB discovery, transient port
open retry and the query-only command guard.
The real candidate CRC also matches `Tools/px4_uploader.py`'s calculation.
[Offline validation](../build/bench_commissioning/stock-e880f1cd79-diagnostic/readback-tool-offline-validation.json)
records these checks. The broad unittest discovery command imports unrelated
MAVLink bench dependencies; run this standalone test directly instead:

```powershell
& .\build\bench_commissioning\bootloader-readback-venv\Scripts\python.exe .\Tools\bench_test\test_inspect_bootloader.py -v
```

The private environment contains pyserial 3.5. On a fresh setup, create it with
`python -m venv build/bench_commissioning/bootloader-readback-venv` and install
`pyserial==3.5` using that environment's Python. Preparation used only fixtures;
the subsequent controller inspection below used query commands only. No upload,
application boot command or parameter write was issued by Codex. All application
boot and physical acceptance flags remain false.

### Measured stock flash result, October 19

The user clarified that COM14 appears briefly after reconnection and then
disappears. A host enumeration check afterward found no PX4/ArduPilot USB port.
This does not demonstrate continuous bootloader retention; it is consistent
with a launch attempt or reset, without identifying which occurred.

The first readback attempt detected `1209:5741` but Windows lost COM14 before
opening it; no protocol command was sent. It is preserved as
`cold-readback-20261019T205938820815Z.json`. After adding a bounded transient-open
retry, the user was prompted to reconnect USB with the failed stock image and
QGC closed. The second inspection completed at 21:01:49 UTC:

| Measured item | Result |
| --- | --- |
| USB/loader | COM14, VID/PID `1209:5741`, protocol 5, board ID 53, revision 0 |
| Application slot | 1,966,080 bytes |
| Stored first eight vector words | All match the stock candidate, including initial stack `0x2401b62a` and reset `0x080202a1` |
| Full slot CRC | Expected and reported `0xde5dd361` |
| Missing lead vector line | False; the stored first 32 bytes are present and correct |
| Optional loader build string | No reply; exact installed build remains unidentified |
| Application boot / physical acceptance | Not verified / not granted |

The complete [successful readback report](../build/bench_commissioning/stock-e880f1cd79-diagnostic/cold-readback-20261019T210117038214Z.json)
records the actual query sequence. The tool does not independently observe a
physical power cycle; reconnect preparation was requested from the user. The
stored vector observation and matching slot CRC confirm that the measured
stock payload was finalized and retained. The failed stock connection therefore
requires application handoff/startup investigation rather than treating this
upload as incomplete.

Identification intentionally holds the loader's session open. Its continued
presence after this inspection is an effect of the query session, not evidence
that the original power-on sequence remains in the loader. Power cycling releases
that hold; the retained working rollback remains the recovery image.

Do not infer an exact OS defect from these measurements. A low-level console
trace or a separately qualified bench build on the complete known-working
`e8ea98b4`/NuttX 11 baseline is the next development route. Keep the installed
bootloader and original images preserved while selecting that next experiment.

### Original USB comparison procedure without a debug adapter

The user has now completed this comparison and reports failure. Use the
October 19 readback procedure above for the next diagnostic reproduction.

Use only the comparison artifact identified by a successful
`build/bench_commissioning/stock-e880f1cd79-diagnostic/comparison-receipt.json`.
Compilation and artifact verification are complete for the artifact above. This stock
image contains no research bench controller; its purpose is startup diagnosis.

1. Keep actuator and EDF power disconnected. Start from the verified working
   rollback, export the current QGC parameters, and retain the exact rollback
   firmware file. Preserve the SD failure records and backup as they stand.
2. Verify the comparison file's SHA256 against its receipt. Confirm target
   `px4_fmu-v6x_default`, board ID 53 and clean source commit
   `e880f1cd79e2453ef3dfaf68ee1c067b98ea9a53`. Do not select the withdrawn H10
   package or change the bootloader.
3. Follow the QGC custom-file upload steps in G2 of the
   [commissioning procedure](MANUAL_BENCH_FLASH_AND_TEST_PROCEDURE.md). Save the
   programming and verification result. Successful upload alone is not a pass.
4. Remove USB and every other flight-controller supply, then reconnect USB.
   Record whether QGC establishes a vehicle connection within a 60-second
   observation window. This window is a defined test interval, not proof of a
   specific failure mechanism if there is no connection.
5. If connected, collect the following from QGC's MAVLink Console. Require
   the expected V6X architecture, full source commit, NuttX 12.12.0, and USB
   MAVLink startup/status. Preserve all warnings; preflight warnings alone do
   not fail this communications test.

   ```text
   ver all
   dmesg
   mavlink status
   param status
   ```

6. If it remains disconnected, preserve the upload result and observed USB
   device information. Reflash the retained rollback and cycle USB as before.
   Confirm its exact identity and connection. Read `param_import_fail.txt` again
   and compare its size/content to the supplied 7,542-byte record; any new
   section needs its own firmware attribution. An unchanged file does not
   locate the failure or rule out parameter/storage initialization problems.
7. Report the comparison's result and logs before continuing bench outputs.
   A successful stock boot narrows the next fix toward research changes or
   build differences. A matching stock failure supports an isolated bench
   implementation on the verified rollback base, or deeper startup diagnosis;
   it does not justify a bootloader update or an unreviewed kernel substitution.

Record separately: upload/verification result, elapsed connection observation,
runtime identity, USB MAVLink status, parameter-storage result, and rollback
recovery if required. No manual, PWM or stabilized gate advances from this test.

## Static checks completed

- The staged package still hashes to
  `7163791e111aadc4f1abfafa7679749c12a93c056c2d89fab25b0efa0d68634a`.
- Its decoded image is 1,956,812 bytes, within the 1,966,080-byte package limit.
  The linked image starts at the target application address, `0x08020000`.
- The vector reset entry is inside application flash. The initial stack address
  is in AXI SRAM; the same vector prefix appears in the retained earlier board builds.
- The ARM ELF contains the bench module, NSH, board initialization, CDC/ACM
  autostart and MAVLink entry points. The NuttX apps archive contains one current
  built-in table and NSH startup objects.
- The native board initialization, defconfig, linker script and startup inputs
  compared here match the checkout. Packed `rcS` and `rc.sysinit` exist without
  CRLF line endings.
- H10 uses the NuttX/apps submodule revisions required by its PX4 base commit;
  an accidental mismatch of those Git revisions was not found.

These checks establish image identity and static consistency. They do not
establish reset-to-application execution, correct hardware initialization or
normal application heartbeat.

The candidate's application and its in-tree bootloader use the same USB VID/PID,
`3185:0035`, with different product strings. A Windows friendly-name or hardware
ID alone cannot distinguish bootloader retention from an application that has
enumerated but has not started MAVLink. Successful programming also does not
prove successful application startup.

## Next hardware evidence

Keep the working rollback installed and actuator/EDF energy disconnected while
collecting evidence. Do not reset parameters, clear crash records, format the card
or update the bootloader to try to conceal the symptom.

1. Save the user-supplied rollback identity and boot messages with the failed
   upload's QGC log. Retain the exact working `.px4` and its hash if available.
2. Inspect the SD card for `fault_*.txt` from the failed attempt. Copy existing
   files before further experiments. The rollback startup may already have
   committed a retained crash to disk. On the working rollback, the status command
   `hardfault_log check` and `ls /fs/microsd` can help identify remaining evidence.
   A missing fault file does not rule out a failure before crash capture.
3. For a separately released reproduction of the withdrawn image, connect the
   low-level FMU debug console and capture from power-on. This is distinct from
   QGC's MAVLink Console, which requires successful application communications.
   The H10 defconfig selects USART3 console at **57600 baud, 8N1**.
4. Use the verified installed board's debug pinout and a **3.3 V UART adapter**.
   For the documented Pixhawk 6X Debug Full connector, board TX is pin 2, board
   RX is pin 3 and ground is pin 10. Cross TX/RX; leave the adapter's power wire
   disconnected. Confirm the physical connector/orientation before wiring.
   [PX4 board debug reference](https://docs.px4.io/main/en/flight_controller/pixhawk6x)
5. Capture the complete log, without sending input during initial boot. Console
   input can intentionally halt this source's retained-hardfault review path.
   Note whether it reaches a NuttX banner, board initialization, a hardfault,
   NSH startup, `cdcacm_autostart`, or USB MAVLink.
   [PX4 system-console reference](https://docs.px4.io/main/en/debug/system_console)
6. Recover with the known-working rollback after the capture. Preserve the failed
   trace, any crash dump, exact failed image and matching ELF/map. Decode a crash
   address against H10's ELF, rather than the rollback ELF.

Do not modify or reflash the bootloader during this diagnosis. The existing
bootloader still permits recovery. Application upload verification, application
boot and actuator acceptance remain separate gates.

## Replacement development and acceptance

The three-cycle stock boot reference in the
[rollback reference procedure](MANUAL_BENCH_STOCK_ROLLBACK_REFERENCE.md) now
passes with matching runtime identity and the user-accepted intentional stock
reset. The isolated bench variant now has fresh software qualification and a
new ARM artifact on that complete baseline, with reviewed source adaptations
and pinned submodules. The user now reports successful cold boots and an available
but stopped module. Continue disconnected input/identity verification and retain
the remaining changed-image console/configuration evidence. A low-level
failed-image trace remains the route to a focused fix for the newer baseline.
Do not substitute only the old kernel underneath newer PX4 code or relabel the
old software qualification as qualification of a different build.

Any replacement must retain the H10 manual snapshot ordering fix and bench
contracts, pass the appropriate portable/native regressions, link the intended
ARM module/output path, fit flash and have a new source/artifact manifest.
Require actual cold-boot/QGC/console verification on the identified hardware
before resuming disconnected PWM tests in G3-G4 of the
[commissioning procedure](MANUAL_BENCH_FLASH_AND_TEST_PROCEDURE.md).

The failure report and rollback identity report are retained beside the candidate
under `build/bench_commissioning/h10-e880f1cd79-dirty/`. The original H10 software
qualification remains preserved at its software scope; hardware startup is failed
or unestablished, and no powered/manual/stabilized physical gate has advanced.
