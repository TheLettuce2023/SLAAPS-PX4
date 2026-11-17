# H10 manual snapshot ordering fix and scoped software verification

2026-10-02, Codex. This follows the user's authorization to fix the H9 failure
and continue automated verification. No flashing, physical output, startup,
airframe, operational profile, arming policy, EDF command or positive acceleration
admission is added. Earlier failed H9 evidence remains intact.

## Fix and exact release boundary

H9 proved that `ResearchBenchControl::Run()` could capture its evaluation clock
before a newer selected publication arrived, then reject that valid publication
as future-dated and latch neutral. H10 copies the selected message first and
reads the evaluation clock afterward, through the allocation-free
`copyManualSnapshot` helper. Stop/disable/error paths still timestamp their own
revocation. Missing snapshots are initialized invalid rather than reusing data.

The stock `ManualControl` publisher also captured time before copying its three
candidate subscriptions. It now captures those candidates in fixed-capacity
local arrays before reading its live evaluation/publication clock, then runs the
existing selection and processing logic in the same order. Its protected test
seam retains fixed-time calls; a supplied clock callback allows deterministic
ordering tests. There is no operator-selectable clock or new runtime owner.

Neither message timestamp is clamped or repaired. Future-time, stale/replayed
sample, identity/source, centering, cycle-gap, lease and fault-latching checks
remain intact. No input timeout, rate, priority, calibration, slew or physical
coefficient was loosened to obtain a passing result. The selected source still
belongs to PX4's stock manual-control module.

Exact gate advanced: **H10 snapshot-ordering defect closure and scoped
capture-only manual bench integration verification**. This closes the reproduced
H9 software blocker, not physical/manual-powered acceptance or flight readiness.
The older inactive H5/H7 generic adapters are not newly qualified for deployment
by tests of the H8 owner path. Their caller-time contracts remain separate.

## Regression and integration evidence

- New portable deterministic regression: 18 checks simulate a publisher moving
  time forward during copy, reproduce rejection with the old clock, accept the
  corrected clock for enable/cycle, preserve genuine future-time rejection and
  the last accepted report, retain fault latching and reject a missing snapshot.
- Strict Debug, Release and ASan/UBSan: **2,431 checks per configuration** across
  nine executables (prior 2,413 plus 18). Sanitizers halt on error and enable leak
  detection. This is portable-library sanitizer coverage, not full-PX4 ASan or
  concurrency-sanitizer qualification.
- Existing stock compatibility tests: **28 tests pass** — ManualControl 11,
  ManualControlSelector 13, MovingDiff 4. Their fixed-time call sites remain
  compatible. This is a scoped selection, not all PX4 tests.
- The capture-only fixture includes a deterministic test of the actual stock
  selector: publish on all three input instances inside the clock callback,
  verify the current snapshot remains unchanged, then consume that publication
  in the next cycle without restamping its sample time. A separate runtime case
  injects genuinely future-dated input and verifies latched neutral.
- The H9 direct-input, MAVLink-loopback, mixing, deadzone, reversal, throttle,
  source/loss, lease/replay, safety, routing, stop/restart and actual owner-queue
  stall scenarios are retained. All outputs terminate at a memory-only
  `OutputModuleInterface`; no physical driver or Commander is started.
- The independent physical-acceptance checker still passes **19 tests**.

**Three consecutive full integration runs pass: 19/19 cases and 3,541 checks
per run, zero failures, all using the same retained binary.** These are repeated
scoped scenarios, not 10,623 unique tests or exhaustive concurrency coverage.
No lingering test PX4 process remains.

Evidence manifest SHA-256:
`b8176eaf9f23098b9800e8ab4c33bc8b0e41adf0015aa222310672631acb9c43`.
Full-run result SHA-256 (first/second/final):

- `results.json.1`: `edfdb070b3f24f77da513f92c65f05e64acfa1a6d19e8e7602d1873ca0ad7db5`
- `results.json.2`: `a19e893c3066f4f0ed811d0ce4a86b58e689a477d7f9cfb544275dbc3f06420e`
- `results.json`: `c8e02565ef8dd1fe4e692f53c7eb707ff6452a0f4aa079ee4a333819d3acdad6`

Test-log SHA-256:

- Debug: `580a9d7c413c4503adc4c20ab38256adc08bca0c7a1878c36ee9fb574476a4ae`
- Release: `38fdedaa0fc0991642a59d22462b0a2ee98ba5e2e4212065217b09e1090a3f99`
- ASan/UBSan: `129928022ef92b860f4743b2f32125f6a522baae672b9bdc6dc16186cef09c66`
- Stock compatibility: `a23fbd0e33e0154035ed58ea8078126921919aed59ec62627e7b4c1380f24425`

| Final artifact under H10 evidence root | Bytes | SHA-256 |
| --- | ---: | --- |
| `sitl-build/bin/px4` | 58,977,592 | `f5d504fe37cd0314a4d215dc250b3332e13883b294ea2345ef7916d1c8c8d677` |
| `v6x-build/px4_fmu-v6x_default.elf` | 49,467,692 | `db0f5c6dfb071feb0eecdd2f47999f3936cff57ebbbca9b83344ce42a23eb9a4` |
| `v6x-build/px4_fmu-v6x_default.bin` | 1,956,812 | `29ecbece069e3a1344d08de1cd56e36150b867cd46a36dd00d58fad447bd2d60` |
| `v6x-build/px4_fmu-v6x_default.px4` | 1,864,146 | `7163791e111aadc4f1abfafa7679749c12a93c056c2d89fab25b0efa0d68634a` |

Linked ARM text/data/BSS: **1,948,768 / 8,044 / 105,596 bytes**.
Private SITL text/data/BSS: **5,065,580 / 134,541 / 36,472 bytes**, including the
capture fixture. These are complete linked images, not excluded-object sizes or
an isolated component cost. Final SITL logs include incremental correction of
the initial fresh-build fixture diagnostics; this is not repeated clean builds.

The fixture remains a private POSIX overlay, absent from checkout module/startup
registration and the ARM executable. Its synthetic parameters/profiles are not
deployable configuration. Loopback traffic exercises the actual MAVLink receiver,
not QGroundControl, a physical gamepad, SiK, TELEM1 or installed source IDs.
See H9 for the local UDP socket and test-scope limitations.

## Reproduction and preservation

Use installed Ubuntu-24.04 WSL and ARM GCC 13.2.1. From the checkout:

```
python3 -B Tools/research_sitl/verify_bench_h10.py snapshot
python3 -B Tools/research_sitl/verify_bench_h10.py host
python3 -B Tools/research_sitl/verify_bench_h10.py prepare
python3 -B Tools/research_sitl/verify_bench_h10.py sitl
python3 -B Tools/research_sitl/verify_bench_h10.py board
python3 -B Tools/research_sitl/verify_bench_h10.py stock
python3 -B Tools/research_sitl/verify_bench_h10.py test
python3 -B Tools/research_sitl/verify_bench_h10.py manifest
```

The evidence root is `/var/tmp/research-bench-h10`; skip the non-overwritable
snapshot action when resuming. Repeated test logs/results are archived and bound
to the retained binary by SHA-256. H3/H7/H8/H9 trees were not overwritten. Fresh
SITL and Pixhawk 6X build directories use a separate native source view; inherited
NuttX generated objects are cleaned there, and the apps archive must contain
exactly one current H10 built-in table. Board defaults in the checkout remain
untouched; only private build views explicitly opt in to H8.

Checkpoint: 1,391 bound files; **1,377 non-exempt files remain hash-identical**.
The final portable/native production inputs and fixture match the checkout.
The original 13 tracked changes and 1,017 untracked files were preserved. Final
tree: **15 tracked changes and 1,021 untracked files**; the two newly modified
tracked files are `ManualControl.cpp/.hpp`. No staged diff, commit, hardware flash
or powered actuation. Documentation and affected graph hashes are updated
separately from the build manifest, avoiding circular evidence hashes. Use the
H10 runner for continuation; do not overwrite the sealed failed H9 evidence.

Stock tests use GoogleTest v1.16.0, commit
`6910c9d9165801d8827d628cb72eb7ea9dd538c5`, with PX4's existing GTest-only helper
instead of fetching fuzzing dependencies. The private test CMake scaffold is
retained and the native original is restored afterward. No checkout test build
configuration was changed. No fuzz-test execution is claimed.

Failed setup attempts are retained: the initial portable regression had an
invalid incomplete synthetic mix; the corrected complete mix passes. The fresh
SITL fixture exposed MAVLink INTERFACE generated-header scope and packed-header
warning issues hidden by H9's incremental build. Its private CMake now depends
explicitly on header generation and treats those generated headers as system
headers; project source warning checks are not disabled. These were fixture/build
diagnostics, not discarded production-control failures.

## Resource and remaining physical gates

Final ELF symbols prove that the H8 owner/lease path is linked on both targets;
compilation alone does not prove runtime behavior. Static whole-image text/data/
BSS and package sizes do not establish runtime heap, stack high-water marks or
worst-case scheduling latency. The selector's bounded three-message local
snapshot adds stack storage; no hardware stack/timing margin is asserted.

Remaining requirements are the physical-acceptance sequence: reviewed installed
profile and canard signs/calibrated angles/loaded slew, signal-only PWM captures,
actual QGC/gamepad/SiK identity and loss tests, measured scheduler/link/lease
budgets, BEC voltage/load/thermal evidence, finite wind upper bound, independent
stop and fixture qualification, then separately authorized no-wind and staged-wind
tests with named human review. Whole-FMU/PX4IO loss and stopped-pulse behavior
remain unmeasured. C1E-N1 stays negative-only and every acceleration positive-
admission/flight gate remains intact.
