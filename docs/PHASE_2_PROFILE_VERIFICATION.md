# P2T1: approved offline timing-profile verification

Follow-up: P2T2/P2T3 are now accepted at their bounded scope; see
PHASE_2_OFFLINE_COVERAGE.md for current campaign status and the next authority
boundary. The P2T1 evidence and next-step record below are historical.

Scope: a deterministic host fixture calls the existing WireAdapter, RequestGuard
and status encoder. It does not change their source/defaults, attach a parser,
run Gazebo/PX4, open a link, arm hardware or publish controller/actuator values.
All event times, state, authentication verdicts, maps and acceleration bounds
are injected test inputs, not observed host/Pi/link/physical measurements.

## Predeclared gate

- Use the user-approved 50 Hz / 24 ms release-gap, 12 ms transport, 4 ms queue,
  20 ms nominal conservative admission age, 60 ms hard age, 80 ms raw validity,
  4 ms uncertainty and 8 ms future-skew profile without changing guard defaults.
- Exercise periodic and jittered streams through actual admission and accessor;
  preserve CG inertial-FRD components and raw creation/expiry/sequence fields.
- Check at/below/above nominal budgets separately from hard guard decisions.
- Check inclusive 60 ms age and first rejected microsecond, conservative expiry
  in a separate explicitly non-profile isolation fixture, stale payload while
  heartbeat remains alive, duplicates/reorder/foreign packets, missing state,
  clock faults/intervals and epoch/session/restart barriers.
- Encode real request and access reports; independently verify their bytes,
  correlation, decisions and time arithmetic using the existing Python codec.
- A strict evidence checker must reject missing, duplicated, altered or
  unexplained timestamps/records and prohibited output events. Deliberate
  packet/status loss must be identified as injected loss, not fake receipt.
- Strict Debug, optimized and ASan/UBSan host tests and unchanged regression
  tests must pass. Independent review is required before acceptance.

The fixture's synthetic +/-10 m/s^2 axis and 12 m/s^2 norm bounds, and all test
identities, are existing unit-test scaffolding only, not approved vehicle limits.
One fixture adapter owns every command stage exercised here; no new publisher
or operational controller exists. Ground truth is not an operational input.

## Boundaries and later coverage

P2T1 measures deterministic software outcomes against synthetic timestamps.
It is not the complete seven-group measurement campaign in the approved profile.
Existing C0b/O1/O2 regressions retain the broader session/restart/replay tests.
Status-delivery loss/reordering/backpressure instrumentation and residual-attitude
policy review fixtures must be separately accounted for, not credited from a
payload round trip or guard's required-state Boolean. No residual-attitude
controller or physical unavailable-direction response is tested here.
Accessor snapshots encoded on each request are local evidence, not transmitted
diagnostic traffic. P2T1 does not implement or verify the <=1 Hz diagnostic
emission throttle, 40 ms no-fresh diagnostic or status-return delay. Receipt,
emission and return timestamps are explicit synthetic schedule inputs.
The nine cache-fault and 2001 heartbeat-access assertions are C++ test evidence;
the Python oracle independently verifies the 113 timed request/access records,
not per-fault traces that this increment does not emit.

Full roadmap Phase 2 remains open until the approved contract has the required
control/link budget evidence and remaining design/test signoff. This does not
require implementing Phase 4 first. Geometry remains deferred for offline work.

## Results

**P2T1 bounded gate PASS (2026-09-19): master review and independent source
review accepted. Full Phase 2 and the full profile campaign remain open.**

- Debug: 14/14 CTest targets pass.
- Release: 14/14 targets pass, repeated three times.
- ASan/UBSan: 14/14 targets pass, no sanitizer diagnostics.
- Every configuration independently checks 113 timed request/access records
  (100 periodic/jittered requests and 13 boundary cases), rejects 27 malformed
  evidence packages, and correctly distinguishes four deliberate nominal-budget
  violations from hard guard rejection. The fixture also checks nine cache-fault
  cases and 2001 accesses while three injected 1 Hz heartbeat events occur.
- Existing C0b/O1/O2/frame/map regressions are included in those 14 targets.
  Unchanged Python payload codec: 8 tests PASS; evidence checker: 17 tests PASS.
- New C++ file formatted with repository AStyle configuration; final dry-run
  unchanged. `git diff --check` passes.

Review correction: the first Python oracle compared only selected status
fields. It now checks the complete decoded status dictionary and rejects
codec-valid alternate rejection counts, flags and trusted-state claims. No
admission/guard/controller source or default was changed to make a test pass.
Execution counters count exercised cases rather than printing static totals.
Windows/WSL command quoting initially prevented configuration; corrected explicit
arguments succeeded. These were tooling failures, not relaxed test criteria.

Observed toolchain: GCC 13.3.0, Python 3.12.3, Ubuntu-24.04 WSL. Parent PX4 pin
remains `e880f1cd79e2453ef3dfaf68ee1c067b98ea9a53`.
Retained build root: `/var/tmp/research-p2t1.sia8NQPT`, with `debug`, `release`
and `sanitize` subdirectories. Each retains its executable, CMake flags and
`Testing/Temporary/LastTest.log`, including the synthetic trace and checker JSON.
Identical trace SHA-256 across configurations:
`e10295e1606cb071c04d2d1dba97ec6d14578e65e9eea7acf3d359e36da9605a`.

Source SHA-256:

- TimingProfileTest.cpp: `63a8ee4c7a5a0d3f3faa319342400adcff4f7ec68ff2672e8afeff9dbd245e4b`.
- check_timing_profile.py: `0eacdebaacd581d4fa50ec842c867c9b7b09359032622066ad02fdfeae49abf8`.
- Standalone CMakeLists.txt: `bdced802e46b1991da7b1fb840c414fe10e92be430ef2cb9190898917f593aa5`.

CTest log SHA-256, Debug/Release/sanitizer respectively:

- `b0efeb3082ce8b8d0f0b312fe0738026b42be28ab17343a3fbe88e030dfdacf8`.
- `2e7ce20109442585261b78bb3d90235ecda0bed0ae15a765fd585be7d6854e68`.
- `30e2e18d232da578b635c2be07663a433456d8f47b28599d37bcdf01e9c674a6`.

## Reproduction and next gate

From the checkout root in an existing WSL shell, without installing packages:

```sh
task_build=$(mktemp -d /var/tmp/research-p2t1.XXXXXXXX)
cmake -S src/lib/research_acceleration -B "$task_build/debug" -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug '-DCMAKE_CXX_FLAGS=-Wall -Wextra -Wpedantic -Werror'
cmake --build "$task_build/debug" --parallel 2
ctest --test-dir "$task_build/debug" --output-on-failure
python3 -B Tools/research_sitl/profile/check_timing_profile.py \
  "$task_build/debug/research_sitl_timing_profile_test"
```

Release uses a separate `release` directory, `CMAKE_BUILD_TYPE=Release` and
CTest `--repeat until-fail:3`. Sanitizer uses a separate `sanitize` directory,
Debug, appends `-fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie`
to CXX flags and sets
`-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined -no-pie`.
These are standalone host tests, not simulator or board firmware builds.

Next independent offline increment: design/review status-delivery fault and
diagnostic scheduling instrumentation, then test explicit drops/reorder/bounded
backpressure without blocking eligibility checks. Define deterministic ordering
for an admission coincident with the 40 ms diagnostic threshold, and verify the
<=1 Hz diagnostic rate. Account separately for residual-attitude policy-only
fixtures and inherited transition/restart coverage. No geometry or additional
human approval is required for that inactive design/test work.
Operational control/link budgets, clock provenance and live authority remain
separate decisions; this gate does not authorize live ingress or flight control.
