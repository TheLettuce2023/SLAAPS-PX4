# C1A passive PX4 attachment verification

2026-09-20. Scope: the user-approved C1 attachment, first passive state/clock
slice in C1_PX4_ATTACHMENT_DESIGN.md. **PASS at passive C1A scope**, based on
the fresh build, two independently checked runs and source/configuration review
below. Full C1 and roadmap Phase 2 remain OPEN; no admission or flight claim.

## Artifacts and provenance

New implementation: Tools/research_sitl/px4_attachment contains the explicit
minimal POSIX config, private startup, external passive probe, isolated runner,
independent compact-log checker and eleven synthetic unittest methods.

PX4 revision: e880f1cd79e2453ef3dfaf68ee1c067b98ea9a53.
Separate Gazebo revision: bb0b9cf974acf4f1bcb5f5fcf80b88841562dea9.
Installed Ubuntu-24.04 WSL tools only, GNU C/C++ 13.3.0, Python 3.12.3.
Build type Debug, strict repository warnings including -Werror. No hardware
build/flash, simulator plant, network endpoint or research admission owner.

The source view uses top-level symlinks and private .vscode/build entries under
a unique /var/tmp/c1a_px4_* directory. It does not relocate the checkout, create
mounts, use sudo, touch the old Cessna cache or modify global Git configuration.
Parent build inputs and the new tooling are content-hashed; Git submodule
contents are not recursively included in those hashes. Recursive revisions,
separate Gazebo status/revision and the stock Cessna SDF hash are recorded.
The full Windows-side root/separate-Gazebo Git status and 689 pre-existing
modified/untracked files, including 574 geometry files, were captured at entry.

## Pre-runtime review and failures retained

- Source review rejected carrying the simulated HostOwner ABI into PX4.
  The passive probe instantiates neither that ABI nor another admission guard.
- PX4 log rows are limited in length. Long JSON records were reduced to compact
  keys and separate counts/summary records before runtime. No fields or checks
  are silently dropped by the evidence checker.
- The initial new source folder name external was ignored by the repository's
  general ignore rule. It was renamed px4_external before build/provenance use.
  This moves only newly created probe files, not pre-existing project work.
- /var/tmp/c1a_px4_gk8dfqup: read-only inventory cancelled while traversing the
  unrelated NuttX submodule over the Windows mount; no configure/build/runtime
  occurred. The original runner did not yet save its initial incomplete record.
  It now saves that record before inventory, and parent status explicitly
  excludes nested dirty-tree scans. This is not a clean-submodule claim.
- /var/tmp/c1a_px4_ztadd1rd: configure succeeded, but master inspection found
  BOARD_TESTING had added extra test builtins outside the predeclared allowlist.
  The owned Ninja build was interrupted normally (exit 2, 74/533 steps logged);
  no PX4 runtime started. The failed manifest and logs remain retained. The
  new defconfig now explicitly disables BOARD_TESTING; the same strict builtin
  audit runs immediately after configure and again before runtime.
- /var/tmp/c1a_px4_qfboj5a4: corrected configuration passed the strict builtin
  audit, then compilation failed at Commander (369/471 logged steps) because
  its GF_ACTION parameter enum was absent from the minimal configuration.
  A compiled-source audit found six required missing enums in three existing
  parameter YAML files. The external CMake now registers only those metadata
  files, without selecting Navigator/sensor modules. The failed manifest,
  logs and configuration are archived in failed-attempt-qz232xea under that
  area. Its incremental retry built and ran both probes successfully, but
  runtime dependency errors prevent accepting that configuration. This is not
  a final clean-from-empty build. No runtime occurred in the archived compile
  failure itself.
- That retry's two runs each recorded 21 clock samples and three fresh
  disarmed publications per state topic, with normal exit. Strengthened
  independent state/clock checks passed; neither establishes clean integration.
  Each log also contains 184 missing-parameter error pairs. Source review
  identified RC1..RC18 MIN/TRIM/MAX, SDLOG_MODE and RTL_TYPE string lookups.
  Missing RC values remain zero and do not fail its comparisons, despite a
  contrary source comment: do not claim that all missing-parameter health
  checks fail closed. The new configuration registers their three existing
  YAMLs, without enabling RC, logger or Navigator runtime modules.
- /var/tmp/c1a_px4_k5lm_xnf: final clean-area verification passed after the
  metadata corrections. The final oracle also rejects those parameter
  diagnostics; the earlier runtime remains retained and fails that stricter
  configuration-acceptance rule. Expected missing-sensor/preflight warnings
  remain distinct from programming/configuration errors.

The only allowed generated shell commands are research_probe, commander,
param, shutdown, listener, uorb, ver, list_tasks, list_files and sleep. The last
three are the normal POSIX apps.cpp.in diagnostic builtins. Only Commander is
selected as a standard module; no drivers, estimator, controller, allocator,
MAVLink receiver, bridge or actuator-test module is selected. Generated uORB
message definitions do not constitute active publishers.

## Checks and limitations

Eleven evidence-checker unittest methods pass with synthetic malformed/missing/
contradictory records, false summaries, stale or armed observations, clock
regression, failed/forced exit, altered hashes, topic-snapshot corruption,
forbidden output topics, missing parameter diagnostics and reused/overlapping runs. These tests are not live
PX4 observations. Scoped C++ formatting is unchanged under repository AStyle.

Master/independent review found that the original Python fixture allowed a
different clock origin despite the native no-lockstep bracket predicate. After
the first runtime campaign finished, the oracle was tightened to recompute
uint64 conversion bounds, each bracket, first completion and native state-row
ordering. Raw logs/manifests were not altered. The new clean-area campaign
hashes the strengthened oracle from its start. Numerical diagnostic agreement
still does not qualify a cross-process clock map.

## Final measured result

Retained root: /var/tmp/c1a_px4_k5lm_xnf in Ubuntu-24.04. This final build used
a fresh area, not --resume-build: configure exited 0 (160.8 s reported), all
471 build steps completed and the final linked-builtin/configuration audit
passed. All 62 identified parameter dependencies are present in generated
metadata. Only Commander, five diagnostic system commands and the external
probe are selected; no drivers, estimator, controller, allocator or transport.

| Observation | Run 1 | Run 2 |
| --- | --- | --- |
| PX4 instance / PID | 15339 / 2837 | 44884 / 2867 |
| Valid clock samples | 21 | 21 |
| Fresh vehicle_status / actuator_armed | 3 / 3 | 3 / 3 |
| Observed state | DISARMED / armed=false | DISARMED / armed=false |
| Host span, ns | 2019683610 | 2024700556 |
| PX4 hrt delta, us | 2019684 | 2024700 |
| Maximum host bracket, ns | 1680 | 1467 |
| Exit / forced shutdown | 0 / false | 0 / false |

Both final logs have zero unresolved parameter diagnostics and zero ERROR
lines. Each retains an unavailable LED informational message, missing sensor/
power and safety-configuration preflight warnings, and an arming-event report
capacity warning. Those are not valid-estimator or complete health-report
evidence. C1A does not rely on health-report completeness for request admission:
there is no admission owner or request path. No warning was suppressed.

Master and independent source/configuration/runtime review accept this bounded
slice. The independent checker passes both logs and the two-run campaign. The
advertised-topic snapshots contain Commander state/diagnostics but none of the
predeclared forbidden demand/output topics. Native-range/bracket/order checks
are independently recomputed, not accepted from summary flags. Finite samples
do not prove continuous state history, and their timing is not a worst-case
deadline guarantee. Source/configuration review separately establishes the
bounded no-output construction. No owned PX4/build process remains running.

The producer manifest intentionally remains built_runs_unscored; raw run
records are not acceptance verdicts. This reviewed document and the independent
checker supply the bounded verdict without rewriting source logs/manifests.

SHA-256 fingerprints (lowercase; hashes of the retained final artifacts):

```text
manifest.json       e2eeeda5f128c00281aa5ed27803c0c5b49ef499b04601df23296008e7fb6013
source_hashes.json  d86efc18db5c41fc40e9d155f8a0381c37c36e12362a2debddf6df49f0692092
configure.log       7486001c3a50f9ed3b2e56b069d19f68510c1e61a210872c1e76b62d2c1c9924
build.log           aa1a8f9ffe034e325ca6a0f51230a62a7961e1d562ee1f338e0aba5279286a4c
build/bin/px4       a3504cd6bc5efeb42e4e915e0e6ecd72ae3166b38903526b10b00c6c78c6febc
run-1/stdout.log    c26d8ae6f2087e059c61b35214ccbde69e57b3989720ac057bb0cd5e9ba06ab2
run-2/stdout.log    d477240480b3051745b88546cdab767c0fa09a4b86597d0f5e0d85fe7bec714c
checker             cbb4f46a977810db419b3a3ee1c040c86aa80f1cc265ba19720936536e09d46c
checker tests       76a32f5b4d1014f17905e3f8a563ad7e8ff0cf74f47fdc66165ebbe37899b501
```

Reproduction with installed WSL tools, one build at a time:

```sh
python3 -B Tools/research_sitl/px4_attachment/run_probe.py --run
python3 -B -m unittest discover -s Tools/research_sitl/px4_attachment -p 'test_*.py'
python3 -B Tools/research_sitl/px4_attachment/check_probe_evidence.py /var/tmp/c1a_px4_k5lm_xnf/manifest.json
```

For a new campaign substitute its printed retained directory in the last
command. Eleven synthetic methods pass; these are separate from the two real
PX4 runs. The same final checker rejects the prior qfboj5a4 campaign for its
unresolved parameter dependency. No failing evidence was overwritten.

Final build/run source inventory has no changed inputs. Separate Gazebo
revision/status and stock Cessna SDF hash are identical before/after; SDF hash
78591ad19d7589fc202877c8830020f94925a2bd7fa1ea1a958a268591b8da7c.
Entry preservation covers 689 files, including all 574 geometry files; only
four existing approval/status documents are intentionally changed. No existing
flight-library, launcher, baseline, geometry or stock PX4 source was edited.

The runtime gate requires two fresh processes and private parameter/runtime
directories, actual fresh disarmed Commander publications, coherent separately
labelled native clocks, a complete topic snapshot without forbidden advertised
research/control/output topics, normal shutdown and independently checked logs.
Startup explicitly sets COM_ARM_ON_BOOT=0 and COM_ARMABLE=0; this is a defensive
configuration and never substitutes for observed disarmed-state evidence.

No positive/negative request-admission or full clock-map claim is possible:
those facilities are not instantiated. Commander generation deltas do not prove
lossless internal state transitions. An advertised-topic snapshot is finite
diagnostic evidence, supported by the source and strict build-configuration
audit, not an exhaustive trace of every instant. No acceleration path beyond
the previously tested standalone interface has yet been attached by C1A.
