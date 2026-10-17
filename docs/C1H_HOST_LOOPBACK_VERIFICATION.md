# C1H host-loopback verification and master gate

2026-09-20. **PASS at the bounded, output-disabled host-experiment scope.**
Master review and independent parser/clock/owner/evidence reviews accepted the
implemented corrections. This does not close full roadmap Phase 2, qualify
operational deadlines, or authorize PX4/Gazebo/hardware attachment.

## Implemented path and evidence

The new Tools/research_sitl/live package generates the pinned private dialect,
sends genuinely signed MAVLink UDP datagrams over owned 127.0.0.1 endpoints,
authenticates inside the native raw-frame boundary, and uses the existing
owner's single WireAdapter/RequestGuard. An independently scheduled cooperative
operator capability prepares/confirms session operations; guidance cannot grant
itself authority. It acknowledges the actual returned transition barrier.

Requests reach the real native accessor with unchanged sequence, creation,
expiry and CG inertial FRD vector. Actual native reports become signed status
frames, return over UDP, and are independently authenticated and decoded.
This closes the host request-to-access path, not the full flight path:
PX4 estimator/state binding, controller, allocation, outputs and validated
plant response are still absent. No sensor truth becomes a controller input.

The four predeclared final campaigns used identical executable-source hashes:
one Debug and three Release, run sequentially. Each checked 20 nominal requests,
27 returned nominal statuses, command silence and a real 40-ms diagnostic.
Separate runner assertions exercised missing ACK, guidance-only establishment,
bad signature, signing replay, actual wrong UDP peer, malformed/split frames,
duplicate commands, status loss/backpressure, trusted simulated state loss,
independent revoke, producer-regression latch/clear, delayed stale demand,
invalid map and fresh-key/context restart. All five scenarios completed in
each final run. State and acceleration bounds remain synthetic test inputs.

The independent nominal oracle checks complete report inventories, exact
returned payloads, immutable accessor values, full lifecycle timestamps and
all declared observation intervals. It rejects 20 corrupted-copy mutations
per artifact. Its scope is nominal evidence; it does not independently replay
the fault scenarios, whose result comes from the runner's explicit assertions
and native/parser unit tests.

## Measurements and retained failures

Initial Debug and Release experiments were functionally successful but failed
the unchanged 2-ms observer maximum: 2.967 ms and 2.161 ms. Both delays were
between native call brackets, not measured native execution time.

A diagnostic run with unchanged GC policy measured a 2.279-ms gap:
651877423000..651879702000 ns. The generation-1 collection interval
651878575444..651879626289 ns (1.050845 ms) lay entirely inside that gap.
The remaining interval was 1.228155 ms. This proves collection contributed to
that observation, not that it was the sole cause or a future bound.

The reviewed refinement collects before the finite nominal campaign and
defers cyclic collection during it; reference counting remains active.
Finally restores the prior process policy even after failures. Trace storage
is capped at 20,000 rows; a two-second overrun is rejected retrospectively,
not preemptively interrupted. No deadline, clock uncertainty or acceptance
threshold was loosened. Six focused cleanup/clock/overrun tests pass.

| Final campaign | Retained WSL directory | Maximum observer gap | Result |
| --- | --- | --- | --- |
| Debug | `/var/tmp/c1h_host_g_sw76sr` | 1.552 ms | PASS |
| Release 1 | `/var/tmp/c1h_host_fccxb_yz` | 1.381 ms | PASS |
| Release 2 | `/var/tmp/c1h_host_m6k49yvu` | 1.544 ms | PASS |
| Release 3 | `/var/tmp/c1h_host_3hdaqic4` | 1.450 ms | PASS |

Across these four campaigns, measured maxima were:

- Creation to receive observation: 0.479605 ms (limit 12 ms).
- Receipt to native admission completion: 0.166038 ms (limit 4 ms).
- Conservative admission-completion age: 0.647132 ms (nominal limit 20 ms).
- Release gap: 20.108843 ms (limit 24 ms).
- Observer gap: 1.552 ms (limit 2 ms).
- First observation after the 40-ms no-fresh threshold: 609 us late
  (limit 2 ms). Threshold history uses last actual accepted admission.
- Status report sample to authenticated return: 0.159214 ms, measured only;
  no unapproved return-latency limit is inferred.

These are finite host-clock observations, not worst-case guarantees, Pi/serial
measurements or physical timing qualification. Identity offset 0/U=1 us is a
whole-lifetime representation bound on a shared CLOCK_MONOTONIC timeline,
not oscillator accuracy or scheduler allowance. Linux suspend and VM-pause
behavior are not operationally qualified.

Earlier failed artifacts remain unchanged:

- /var/tmp/c1h_host_v3j6e6lf: initial Debug trace SHA256
  08e871fcb9dc8ac01a27eed95e5338d2329a167b797935d3afaa96261a9b2d52.
  Its build directory was later reused for final native Debug tests; the
  historical trace/report retain their original execution-source hashes.
- /var/tmp/c1h_host_1_xyr17c: initial Release trace SHA256
  e8d84ccbe6d3aa5ef69eed9af058e31821beadcb829d265a5c3e55b0eecbbc63.
- /var/tmp/c1h_host_mst37pm2: unchanged-policy GC diagnostic trace SHA256
  3dcd6d9b19d70c650b7228155c76ac493847390c1af378f8f199592adae79adf.

Final trace SHA256 values:

```text
Debug: e556fff371451cbff8040aade987e68eb6c18cd50a30f9b3f708eb5670187723
Release 1: 31332feaaf016b6552d60fc9e27b4567b151704b87dc22694a9884dc753217e8
Release 2: f15f77c51b93c1bbf4ef56ddf2b177e57d4de0713bd6facab3dc477a16397a48
Release 3: 1992375a40a19f45a7691525f0999d7417f84aa778801c81f877fda320b13758
```

Each final directory has checks.json, report.json and trace.json, generated
bindings and its standalone build. Report files contain dependency/source
hashes and clock provenance. No key or raw signed frame is logged. Retained
local paths are evidence locations, not a durable external archive.

## Verification and corrections

- Existing 17-target offline suite: fresh strict Debug, Release repeated three
  times, and ASan/UBSan all PASS. Builds/logs:
  /var/tmp/research-c1h-regression.Zg3JgaiT/{debug,release,sanitize}.
- New native parser and owner: 2/2 tests PASS in strict Debug, repeated Release,
  and ASan/UBSan. Sanitized build:
  /var/tmp/research-c1h-regression.Zg3JgaiT/live-sanitize.
- New Python tests: clock 17, reverse authentication 7, bridge refusal 16,
  evidence oracle 12, bounded campaign window 6: all PASS.
- Existing payload codec 8 and offline evidence checker 17: PASS.
- Scoped repository C++ formatting applied; no warning suppression added.

Review closed these defects before release:

1. Pinned Python replay-state mutation before digest verification: transactional
   reverse parsing prevents bad-signature/foreign high-time poisoning.
2. Native parser requires configured signing, genuine verification, strict
   atomic envelopes and bindings. Replay commit is staged until a fresh owner
   context/map check; expired context cannot commit the candidate.
3. Injected clocks cannot qualify a live map. Clock/provenance/ABI failures
   permanently close the Python-owned native context; generic lifecycle dispatch
   is prohibited. C ABI result headers are checked before output writes.
4. Missing endpoint, diagnostic-return and observer evidence, and overwritten
   reserved-field evidence, now fail the independent checker.
5. The original single-argument owner tick symbol is retained as an overload.
   A reused old build cache initially exposed the changed-symbol issue; clean
   builds and full regressions now pass. A strict enum/integer warning in the
   new ABI canary test was corrected without weakening warnings.
6. An initial /tmp build directory disappeared between WSL invocations. Durable
   local evidence now uses owned /var/tmp directories; no package was installed.

## Preservation and gate release

The entry hash inventory covered 685 existing files, including all 574 geometry
files. The only existing implementation files changed are the bounded
HarnessSupervisorOwner.hpp/.cpp extension. Named gate/proposal/timeline docs
are updated; existing guard, frame/map/status/codec libraries, other tests,
launchers, supplied geometry and stock Cessna resources are preserved.
Tools/simulation/gz remains a separate clean repository at
bb0b9cf974acf4f1bcb5f5fcf80b88841562dea9. No commit, push, PR, package install,
PX4 firmware/simulator build, hardware link, flash or physical operation occurred.

C1H permits proposing C1 output-disabled PX4 attachment; it does not authorize
that new connection. See C1_PX4_ATTACHMENT_PROPOSAL.md for the next minimum
authority decision. Full Phase 2 operational requirements and later
plant/control/normalized-allocation/whole-path validation remain open.
