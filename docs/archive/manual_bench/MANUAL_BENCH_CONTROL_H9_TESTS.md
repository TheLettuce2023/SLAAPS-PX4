# H9 capture-only integration testing — acceptance blocked

2026-10-02, Codex. Requested automated software testing only. Production H8
control code, board defaults, startup configuration and all physical gates are
unchanged. No staging, commits, flashing, output drivers or physical actuation.

## Result and blocker

**H9 software acceptance does not pass. H8 remains the last advanced control
implementation gate.** Automated testing now reaches the actual active path
and exposes an intermittent timestamp-ordering defect. A successful individual
run must not erase this failure or authorize a powered test.

The debugger reproduced `BenchStatus::InputRejected` (5) with
`ManualControlStatus::InvalidTime` (4). One retained trace records:

```
H9_TIMES now=551303538 publication=551303560 sample=551303435
```

`ResearchBenchControl::Run()` samples the clock before routing/safety checks
and copying `manual_control_setpoint`. A newer selected publication can arrive
between those operations. The strict input contract correctly rejects a
publication later than the supplied evaluation time, so a valid asynchronous
stream can cause nuisance revocation. Captured outputs then request calibrated
neutral and do not recover automatically. This is software fault evidence, not
a measured hardware/link timing budget. Production behavior was not changed in
this testing-only turn.

Next software work: correct snapshot/time ordering without relaxing future-time,
source, age, replay or fault-latching checks; inspect the analogous selector
clock-before-copy ordering; add a deterministic interleaving regression; rerun
the complete suite and build evidence. The selector issue is a review target,
not a separately reproduced defect in this record. Stop/restart needs repeated
qualification after the timing fix.

## Scope and reproduction

`Tools/research_sitl/verify_bench_h9.py` uses retained H8 sources and creates a
separate WSL-native `/var/tmp/research-bench-h9/px4-source`. The four files in
`Tools/research_sitl/bench_active/` are copied only into its private POSIX example
directory. No checkout `src/`, board or startup registration is added. The test
module is absent from the ARM ELF.

The fixture runs the actual stock `manual_control` selector, H8 owner,
`FunctionBenchServos` and `MixingOutput`; its `OutputModuleInterface` stores
values in fixed-capacity memory. It starts no PWM, PX4IO, DShot, simulator-output
or motor driver. Synthetic prearm is confined to this process, without Commander
or an arming command. It verifies no `actuator_motors` publication. MAIN5-8 are
checked disabled in nominal cases. This is not evidence of physical AUX silence.

Stock SITL lacks the board-style MAIN pulse and AUX routing parameter banks.
Private YAML supplies those metadata only for capture tests, preserving H8's
production routing checks. Synthetic mix coefficients, 1400/1500/1600 endpoints,
one reversed channel, 10-degree calibration, high fixture slew, 200 ms cycle-gap
and 500 ms lease are **test vectors, never an installed profile or default**.
The final stop assertion waits 600 ms, beyond this fixture's lease, rather than
incorrectly requiring a terminal publication to arrive within 300 ms.

Four cases send generated MAVLink `MANUAL_CONTROL` packets to local UDP 19759
with target 1 and sender 255:190. Actual MAVLink receiver instance 0 supplies the
selected input. Dataman uses its RAM backend; no serial device or radio is opened.
Destinations are loopback, broadcast is not enabled. PX4's stock UDP socket binds
INADDR_ANY; this is a local test fixture, not a network-isolated security test.
Neither QGroundControl nor a physical gamepad/SiK link is exercised.

Seventeen cases cover direct and wire nominal 27-pose mixing, calibration
reversal, uniform saturation, single deadzone/rescaling, ignored throttle,
actuator-test exclusion, wrong sender, nonfinite axis, input loss, consumer lease
expiry/replay, wrong MAIN routing, prearm loss, armed, kill, termination,
lockdown, an actual one-second owner-queue stall with independent consumer
expiry, explicit stop/restart and wrong MAVLink target. These are selected
scenarios, not exhaustive mutation, timing or concurrency coverage.

From the checkout in Ubuntu-24.04 WSL:

```
python3 -B Tools/research_sitl/verify_bench_h9.py snapshot
python3 -B Tools/research_sitl/verify_bench_h9.py host
python3 -B Tools/research_sitl/verify_bench_h9.py prepare
python3 -B Tools/research_sitl/verify_bench_h9.py sitl
python3 -B Tools/research_sitl/verify_bench_h9.py test
python3 -B Tools/research_sitl/verify_bench_h9.py debug --scenario nominal
python3 -B Tools/research_sitl/verify_bench_h9.py board
python3 -B Tools/research_sitl/verify_bench_h9.py manifest
```

The existing snapshot is non-overwritable; skip `snapshot` when resuming this
evidence tree. Tests return nonzero on failed assertions. Logs and results are
archived on rerun. Final executions retain the exact tested binary and record its
SHA-256 in per-case results. Historical fixture-setup failures remain diagnostic
evidence, not active-path acceptance. Initial setup errors were C parameter
declarations incompatible with this YAML-based tree, absent MAIN metadata and
missing dataman before MAVLink. Debugger SIGCONT handling was also corrected.

## Verification evidence

Scoped PX4 AStyle was run on the fixture and H8 scope. Portable strict Debug,
Release and ASan/UBSan each pass **2,413 checks** across eight executables;
the acceptance-checker suite passes **19 tests**. Sanitizers cover the portable
library, **not the complete native PX4 process**. SITL runtime results must not
be added to those portable totals or advertised as sanitizer coverage.

Final uninstrumented run: **11/17 cases pass; six fail**. Failed cases are
`nominal`, `routing`, `termination`, `stall`, `restart`, `wire_nominal`.
Routing/termination/restart failed their Active-entry prerequisite; stall failed
its pre-stall active-demand assertion, so these final runs do not qualify their
intended fault behavior. The two nominal cases latched neutral during valid
input sequences. The final debugger separately reproduced InvalidTime with a
publication 22 us newer than the cycle clock, in the same retained binary.
Do not infer that every failed assertion has been individually diagnosed.
Earlier runs passed 15/17 cases, including the owner-stall and wire-nominal cases;
those successes do not override this intermittent failure. Final runs attempt
3,225 assertions, with 90 failures; prerequisite failures skip scenario bodies,
so this is not a fixed coverage total.

Evidence root: `/var/tmp/research-bench-h9`. Manifest SHA-256:
`58d787ec77bd36c775ac5452caa37a4624345776727a26907fe525e2e1e8511f`.
`results.json` SHA-256:
`b4ec5973c868c2c99b596c226a14e2ab5f4839478c421b3f3b3d91c0305976c9`.
Final `nominal-debug.log` SHA-256:
`482faf433765d1f0e5956be478300b1ab876135f63ffa9a1dc6fd3ca7dae2e5c`.
The original 1 us ordering example remains in an archived debugger log; the
22 us trace above belongs to the final binary.

Portable test-log SHA-256:

- Debug: `65bb3843ad4c3fdea8a559054a9f6812e707647425f9597cca3e7c967a69d4a6`
- Release: `a62cdc811d904dd84ce5b441ec8c80f6f5bfb17e35b0a312b071968dba09b83f`
- ASan/UBSan: `ca213d1e35e8d797213df58d3d04147817e4834539e4004678bbb5060dd06dbd`

| Final artifact under evidence root | Bytes | SHA-256 |
| --- | ---: | --- |
| `sitl-build/bin/px4` | 58,939,760 | `9b12a3848e9ec018dc6df07bb6ea8d7cdf34fa0861f1bb274dd8b258c4737a92` |
| `v6x-build/px4_fmu-v6x_default.elf` | 49,446,640 | `17281c5419deaca16a60918dbd6b64e3f4399b8778ba0bed4c4bbd2ab7f674a7` |
| `v6x-build/px4_fmu-v6x_default.bin` | 1,956,724 | `04aa83c781e978e9419d45ad216bb3d1b6bf45840203fd8a233a24e94efdddd2` |
| `v6x-build/px4_fmu-v6x_default.px4` | 1,864,226 | `104e9752f56421c1242fb58224c829f6e2335b805ee0b22564b350cfa6b18d09` |

Final linked ARM sections: **1,948,680 text / 8,044 data / 105,596 BSS**.
Private SITL sections: **5,060,103 text / 134,349 data / 36,472 BSS**, including
the test fixture. These are whole-image static linked resources, not isolated
H8 cost, runtime heap/stack high-water marks or timing qualification. Fresh
builds were followed by incremental fixture rebuilds and final revalidation;
the final incremental logs alone are not a claim of repeated clean builds.

The checkpoint binds 1,387 files; 1,378 non-exempt files are hash-preserved.
Production source views, portable test sources, MAVLink and work-queue anchor
dependencies match the checkout byte-for-byte. Original tracked dirty changes
remain unchanged: 13 tracked entries; final tree has 1,017 untracked files
(1,011 before this increment, plus five fixture/runner files and this record).
The index remains empty. No test PX4 process remains. Graph/document freshness
is separately bound in `RESEARCH_CODE_GRAPH.json`; documents are not cyclically
hashed into this build manifest.

Fresh Pixhawk 6X cross-compilation uses installed Ubuntu-24.04 WSL ARM GCC
13.2.1 and a clean native NuttX source view. Prior H3/H7/H8 evidence is untouched.
The inherited apps archive is retained before cleaning generated native products;
verification requires exactly one current H9 built-in table. Both final ELFs
contain H8 owner and lease symbols. Compilation alone does not prove these
runtime scenarios pass, and native runtime tests do not prove physical signals.

## Remaining acceptance

Resolve the reproduced software race first. Then complete signal-only hardware
captures, installed canard mapping/directions/angles and loaded slew, BEC voltage
and load/thermal evidence, measured scheduler/link/lease budgets, finite wind
upper bound, independent stop and fixture qualification, and authorized no-wind
then staged-wind tests with named human review. Whole-FMU/PX4IO loss and servo
response to stopped pulses remain physical acceptance items. The acceleration
interface, C1E-N1 negative-only admission and all positive/flight gates are
unchanged. No flight-readiness or powered-manual authorization follows.
