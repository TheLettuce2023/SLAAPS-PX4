# H8 explicit servo-only PX4 bench integration

2026-10-01, Codex. User authorized an explicitly started, default-disabled bench
module and MAIN1-4 output path. This supersedes the earlier no-registration /
no-publication restriction **only for this servo-only bench path**. No automatic
startup, airframe defaults, arming actions, EDF output, flashing or powered tests
are authorized by this software increment. Physical acceptance remains open.

## Implemented ownership and output path

`research_bench_control` is a default-n Kconfig module. It owns one H7
`BenchControlOwner` directly; it does not instantiate the older H7 scheduling
adapter as a second owner. A 20 ms work item serializes control state. CLI
requests use one fixed-capacity atomic mailbox; disable takes priority over a
pending enable. Status is an atomic snapshot. State objects and vectors have
fixed capacity; object/uORB setup allocations occur at startup, not per command.
There is no controller, estimator, acceleration admission or EDF path here.

Startup requires a complete explicit profile, validates it, advertises the topic
without sending a command and remains Ready. A separate enable request requires
fresh, correctly identified, centered PX4-selected manual input, matching output
routing and stock prearm permission while **disarmed**. It never changes arming
or safety state. Throttle remains ignored. Deadzone shaping is optional and
explicit in the profile; configure it in one location, not both QGC and PX4.
Entry checks raw axes before shaping. No operational profile is supplied.

The dedicated `bench_servo_command` topic contains four bounded controls,
timestamp, explicit session identity, lease duration and enable flag. PWM
candidates are converted to driver-normalized controls using each calibrated
neutral and the user-specified 500..2500 us servo range. Driver centering is
extended to the dedicated functions; reversal remains in calibration only.
Whole-vector conversion failures preserve the previous caller vector.

`FunctionBenchServos` is compiled only with the bench module. Functions
10000..10003 are reserved as Research_Bench1..4. The consumer only allocates
them on matching PWM_MAIN channels 1..4, never AUX, CAN, a different bank or
another MAIN channel. The module also requires MAIN5-8 and all AUX1-8 function
assignments disabled, MAIN reversal mask zero, calibrated integer centers,
500/2500 endpoint parameters, and disarmed/failsafe pulse values zero. It only
reads parameters; no assignments or operational defaults are installed.
These values are prerequisites for this implementation, not a claim that a
connected servo safely responds to loss of pulses.

The shared `actuator_test` path was deliberately not used: its timeout is shared
across functions and unrelated test commands can extend it. Dedicated bench
outputs are restored after that override and excluded from actuator-test UI
selection. They remain subject to downstream stock lockdown, kill, termination,
prearm and PX4IO handling. No motor topic or DShot command is published.

## Failure behavior and limits of the software claim

- No accepted session at boot: consumer returns NaN/disarmed, with required
  disarmed pulse setting zero. Starting a module does not send surface demand.
- Normal disable, stop, owner input/source/timing failure or publication failure:
  the owner revokes, stops renewing and attempts a disabled frame. Consumer
  requests calibrated neutral after a previously accepted session.
- Owner stall: consumer checks lease expiry independently; expiry is inclusive
  at the deadline and latches neutral. A late update cannot skip expiry by
  arriving before the next value read. Same-session updates cannot recover.
- Armed, loss of prearm, kill, lockdown, termination or ESC calibration permission:
  consumer revokes the session and uses the stock disabled/failsafe path. Restored
  permission cannot resume that same consumer session. The owner may not yet
  know a very brief consumer-side interruption occurred; status is not an output
  acknowledgement. Explicit stop/start and a newer enable session are required.
- Stop/restart alone does not enable. A consumer that previously enabled may
  continue requesting neutral while prearm remains permitted; this is not
  electrical isolation. Use the independent physical stop for energy removal.

Lease duration is caller-supplied, at least the configured maximum owner gap and
no more than the accepted 500 ms input-age envelope. Those are configuration
constraints, not measured timing margins. Existing mixer callback scheduling has
a 50 ms backup schedule; it is not a hard real-time deadline or an independent
hardware watchdog. Neutral detection can be delayed by consumer scheduling.
Whole-FMU failure, PX4IO communication loss, reboot and servo reaction to stopped
pulses still require physical scope capture and acceptance. No safety override,
parameter race, servo response, loaded slew or independent stop is certified.
Do not change routing/calibration parameters during an enabled session.

## Explicit profile format (not a deployable example)

The reader accepts exactly 44 whitespace-separated finite numeric tokens, with
no comments or trailing tokens. Unknown, missing, nonfinite, out-of-range,
fractional integer fields and invalid H7 configurations reject without replacing
the caller's profile. It does not fetch defaults from prior documents.

First eight tokens, in order:

1. Schema version: 1.
2. MAVLink instance (0..5).
3. Source system ID (1..255).
4. Source component ID (1..255).
5. Maximum owner cycle gap, integer microseconds.
6. Raw neutral-entry tolerance, normalized axis units.
7. Deadzone, normalized axis units (0 disables shaping).
8. Consumer lease duration, integer microseconds.

Then nine values for each of C1, C2, C3, C4:
negative/neutral/positive PWM microseconds, maximum deflection degrees,
positive/negative slew degrees per second, and roll/pitch/yaw mix coefficients.
Pitch preserves PX4 forward-stick/nose-down convention. Installed mix signs are
not inferred from an unspecified local TED arrow. Ideal user assumptions remain
in [H7](MANUAL_BENCH_CONTROL_H7.md), not silently installed as a profile.

CLI lifecycle is `start <profile>`, separate `enable`, `disable`, `status`, `stop`.
Enable/disable return mailbox acceptance, not proof of physical action. Revocation
requires stop/start before a new enable. These interfaces are documented for
review; no hardware command was issued in this increment.

## Verification record

Evidence is retained at `/var/tmp/research-bench-h8`; manifest SHA-256:
`0b934dde9c339d08e5fc295ec4087dd41fcf99b80224db785a8fac6695f6ac0f`.
The runner creates fresh
WSL-native source/build directories derived from retained H7, overlays current
sources, and opts the module in **only in the build source view**. Checkout board
defaults remain unchanged and no startup script references the module. The
private SITL build also opts into NOLOCKSTEP for wall-clock scheduling without
a simulator; this does not alter checkout SITL defaults.

Scoped PX4 AStyle passes. Strict `-Wall -Wextra -Werror -Wpedantic` Debug,
Release and ASan/UBSan (halt on error; leak detection) each pass eight
executables with **2,413 checks**: H1 843, H2 182, H3 233, H4 68, H5 73,
H6 132, H7 649, H8 233. The separate acceptance checker passes 19 tests.
Final portable/native library source views match the checkout byte-for-byte;
1,360 unrelated checkpoint files retain their hashes. Existing dirty/staged
work was preserved; this increment staged or committed nothing.

Final test-log hashes:

- Debug: `0ece2de0c4adc5511fc89f4a52ff71fb790b477b45cd079eb16372b6674d597b`
- Release: `42f007972938de5974a35428112ed67ee6a670ce3dafb9311415ffe2cffc7274`
- ASan/UBSan: `3f9a8f075dfc0cbbf4aa68b6685ff4b251148d8454bea5e86fc18eb3786e383d`
- Disabled-only SITL smoke: `04387780a227e7abe6f05ad3fb654b217749202d32cfd76600a99f84f7f7ba40`

Fresh integrated SITL and Pixhawk 6X compilation and final revalidation pass.
ARM compiler: `(15:13.2.rel1-2) 13.2.1 20231009`, installed Ubuntu-24.04 WSL.
Both ELFs contain `research_bench_control_main`, `BenchControlOwner::cycle` and
`BenchOutputLease::update`; these are **linked integration artifacts**, unlike
H7's excluded-library evidence. Not every unrelated research component is linked.

| Artifact under evidence root | Bytes | SHA-256 |
| --- | ---: | --- |
| `sitl-build/bin/px4` | 58,704,680 | `b258c3e083d627402f0eca21754ff6f8f17d0b6d7088b9bb966e9847b974d7c6` |
| `v6x-build/px4_fmu-v6x_default.elf` | 49,446,640 | `e2031217d80813f284444259708a948e335f928c8f8c97e98b327874b67502ce` |
| `v6x-build/px4_fmu-v6x_default.bin` | 1,956,724 | `8c2477eef73312f1fe7a7b1f59059684442ae5f726f800a679f39e8bf3b361cf` |
| `v6x-build/px4_fmu-v6x_default.px4` | 1,864,226 | `31ba699d3de6c05b7542e520fab8b2946c8d9cea541ecf23bd5b2b1c0a03fa35` |

Linked ARM ELF: **1,948,680 text / 8,044 data / 105,596 BSS**, with text+data
1,956,724 bytes. These are whole-image static sections, not runtime heap, stack
high-water marks or an isolated H8 cost. The module object is allocated at start;
do not infer zero runtime RAM from unchanged BSS. SITL reports 5,035,954 text /
133,645 data / 35,768 BSS. No runtime timing/stack budget is qualified.

The first ARM artifact was rejected despite successful compilation: copied
`libapps.a` contained old path-named built-in-table members from retained trees,
so the new module entry point was not linked. `rejected-stale-libapps.a` and
its member list preserve that diagnosis. Generated apps/NuttX products were
cleaned only inside the H8 WSL source view (explicit TOPDIR; ARCHSCRIPT excluded
from cleanup filenames), then rebuilt. Final archive verification requires one
current H8 built-in table. Prior H7 and other evidence trees were untouched.
No simple before/after firmware-size difference is claimed as H8-only cost.

Initial compiler diagnostics also identified double-promotion and unavailable
NuttX C++ numeric headers; final code uses explicit promotion and `float.h`.
The first private smoke attempt lacked a simulator clock under lockstep and
used `set -e` after PX4's `set` alias; it did not prove lifecycle success.
Those logs remain as failed evidence. The final private wall-clock smoke starts
no output drivers, rejects missing profiles, starts Ready, rejects enable without
prearm/routing prerequisites by entering revoked state, and stops cleanly.
It intentionally never enters Active. Active manual-to-driver end-to-end testing
and physical waveforms remain open. Final build logs include incremental
revalidation after fresh compilation; they are not repeated clean builds.

Portable tests cover strict profile parsing, every truncated/nonfinite field,
failure preservation, deadzone edges, calibrated PWM conversion, whole-vector
rejection, lease expiry/replay/revoke/clock regression and newer-session recovery.
They are synthetic software evidence, not a hardware profile or a live test of
the module/driver. Compilation and ELF symbol checks establish linkage, not
runtime timing or successful surface movement. Firmware is not yet accepted for
powered manual operation or flight.

Remaining physical gate inputs: installed mix/reversal and calibrated angles;
approved measured scheduler/link/lease budgets; actual BEC voltage/load/thermal
results; finite wind upper limit; independent stop/fixture verification; signal-only
capture and authorized no-wind then staged-wind testing. A complete acceptance
record still requires named human review under
[the physical acceptance contract](MANUAL_BENCH_PHYSICAL_ACCEPTANCE.md).
