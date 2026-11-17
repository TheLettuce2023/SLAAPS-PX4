# PX4IO output acknowledgment correction

Updated 2026-10-19. Software verification only; no hardware operation.

The scheduled stabilized output prototype requires a truthful driver-handled
result before acknowledging a command or confirmed disarm. Production
`PX4IO::updateOutputs()` previously returned true even when `io_reg_set()` failed
or `_test_fmu_fail` suppressed the write. It now returns true only when the
direct-PWM register write returns `OK`. PWM conversion, channel assignments,
transport retries and downstream safety settings are unchanged.

The actual mixer uses this return value to decide whether to publish handled
actuator outputs and update latency statistics. The correction prevents a failed
write from being reported as handled. It does not connect the research controller
to production outputs.

## Verification

`Tools/research_sitl/verify_px4io_output.py` extracts the actual output and
pointer-based register-write method bodies into a narrow C++ fixture. Mixer
assignments, device transport, logging and performance hooks are injected.
The fixture tests all 256 assignment masks for one through eight channels,
rounding, transport errors, short/zero/over-count responses, transfer limits and
intentional suppression. It also connects these methods to the real stabilized
output driver to check command rejection, failed disarm, recovery and fresh
session acknowledgment.

- Old methods from pinned commit `472a7d6786`: 22,672 checks, 76 failures, as expected.
- Corrected methods: 22,672 checks, zero failures in Debug, Release and ASan/UBSan.
- Existing control/profile/output regression: 126,064 checks pass in each build.

Host evidence: `/var/tmp/research-px4io-output-0tdar6r9`; manifest SHA-256
`b4fbed528b2103d420b2e6b123c957097a8b64e3934347f3749b0a79aef42575`. All 150 listed files and current source bindings
independently rehash. Local combined receipt: `build/stabilized_io/20261019/verification.json`.

Run from WSL in the repository:

```sh
python3 Tools/research_sitl/verify_px4io_output.py
```

The complete PX4IO translation unit from working baseline
`e8ea98b404baf50623a519ac67ec1c03aa240859` was compiled twice with the pinned
ARM GCC 13.2.1 environment, applying only this method replacement for the corrected
object. The original source matches the previously retained baseline SHA-256
`4cab7e86fefe5c4d5b04c581eff30460b304696971eeb861ce5b5eb79efaebd5`.
Object text changes from 9,801 to 9,809 bytes; both have 20 data bytes and zero BSS.
This is not a linked firmware size or runtime measurement.

Recipe, exact compiler commands, dependency snapshots and objects are retained
under `build/stabilized_io/20261019`. Its `arm-receipt.json` SHA-256 is
`7e4dcd4cc881ade070ec9dc9440a444ff608840c424e00c8889596f1fd7098cd`;
all 258 listed files independently rehash. Existing firmware artifacts remain
separate; no replacement image was linked or flashed.

## Remaining boundary

A successful return means the register transport accepted the write. It does
not establish electrical PWM, servo position, or whole-UART fault behavior.
The IO firmware's FMU-input timeout is greater than 500,000 microseconds,
distinct from the prototype's 150 ms lease. Repeated direct-PWM writes refresh
that downstream timer even if the producer command is stale. A stalled FMU
consumer cannot enforce its own lease until it runs again.

Next software work is the production sink/supervisor connection with explicit
ownership, actual selected-state health and bounded scheduling/fault behavior.
Physical output timing and vehicle-specific calibration remain measurement
gates. The existing synthetic SITL qualification and Ready-only bench acceptance
retain their prior scope.
