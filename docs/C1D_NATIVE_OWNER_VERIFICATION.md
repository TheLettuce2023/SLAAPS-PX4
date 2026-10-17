# C1D-N1 closed diagnostic owner verification

2026-09-20. C1D-N1 ACCEPTED by master verification and independent review.
Prerequisite C1C-S2 is accepted. Design and exact scope are in
C1D_NATIVE_CLOSED_OWNER_DESIGN.md. No N1 result establishes native PX4 execution,
live request admission, estimator/clock readiness, control or Phase 2 completion.

## Preservation and build scope

Exact pre-edit library backup:
`/var/tmp/c1d_entry_mLHomkyH/research_acceleration`.
Fresh host build area reserved: `/var/tmp/c1d_native_3eUh9KxW`.
Neither directory is an existing simulator mount/cache or hardware target.

Ordinary, offline diagnostic and synthetic native-contract objects are separate
libraries and are never linked together in one executable. Both diagnostic test
options default OFF. Native-contract tests use an explicitly synthetic execution
origin, not fabricated PX4 platform definitions. Real native POSIX profile
compilation/runtime acceptance belongs to N2.

## Required campaign (completed)

- Fresh Debug, Release twice from the same build, and ASan/UBSan with both
  `RESEARCH_SITL_DIAGNOSTIC_TESTS=ON` and `RESEARCH_SITL_NATIVE_OWNER_TESTS=ON`.
- Existing ordinary/offline guard, adapter, broker, owner, evidence and isolation
  tests unchanged, plus ordinary Flight regressions in the native-contract build.
- Independent native owner tests: exact source/report chronology, ABA, invalid
  source before mutation, unchanged-assignment checked floor, false readiness,
  zero identities, permanent lifecycle closure, source lifetime loss, counter
  exhaustion and 255/256/257 storage boundaries.
- Native build-isolation probes, including missing/zero/mixed markers, hardware/
  proxy/lockstep rejection, private APIs, forbidden fixture conversion, native
  origin binding and no offline/frozen Flight serialization.
- Fresh default-OFF ordinary regression and absent diagnostic targets.
- Exact memory sizes and compiler stack-use files; scoped formatting, input
  hashes, source preservation and independent implementation review.

## Recorded results

Final campaigns used the exact 201-file native source copy at
`/var/tmp/c1d_native_3eUh9KxW/source`. Master and independent reviewer verified
the complete file set and every SHA-256 against the checkout before and after
testing. Seven pre-existing Python bytecode files were preserved but not used:
the launches set PYTHONDONTWRITEBYTECODE=1 and a separate absent
PYTHONPYCACHEPREFIX. Source binding is `source_binding.sha256` in that root.

| Build / campaign | Result | Elapsed | Log in evidence root |
| --- | --- | --- | --- |
| native-debug | 37/37 pass | 12.52 s | ctest-native-debug-1.log |
| native-release, first | 37/37 pass | 12.37 s | ctest-native-release-1.log |
| same Release executable, repeat | 37/37 pass | 10.76 s | ctest-native-release-2.log |
| native-sanitize, ASan/UBSan | 37/37 pass | 13.00 s | ctest-native-sanitize-1.log |
| native-default-off | 17/17 pass | 2.27 s | ctest-native-default-off-1.log |

New owner: 1,911 checks; build isolation: 61 probes. Existing diagnostic guard,
adapter and owner retain 12,163/3,533/1,209 checks and 33 original probes.
Ordinary native-contract regressions retain 907/361/603/231 checks. Existing
evidence tests pass all 11 methods. Counts are assertions, not flight scenarios.
No sanitizer errors were observed; sanitizer linkage was independently checked.
All opted-in compile databases contain 56 units (20 ordinary, 13 native-contract,
23 offline), without mixed profiles or actual native PX4 flags. Default-OFF has
20 ordinary units and no diagnostic target. Scoped AStyle dry-run reports all
11 changed/new C++ files unchanged; git diff --check passes.

Measured host sizes: owner 934,496 bytes, source record 3,632 bytes, closure
3,608 bytes. Debug compiler stack-use/disassembly tracing estimates the dominant
source-failure chain at 16,344 bytes (about 15.96 KiB). This includes outgoing
result-by-value space; adding the result size again would double-count it.
It is NOT a certified whole-task maximum: native bridge, Commander/runtime and
native compiler configuration remain N2 measurements. No task stack was enlarged.

Reproduction: configure the copied src/lib/research_acceleration with Ninja,
CMAKE_BUILD_TYPE=Debug or Release, CMAKE_EXPORT_COMPILE_COMMANDS=ON and both
diagnostic test options ON; build with -j3; run ctest -V --output-on-failure
with --output-log. Sanitizer Debug additionally uses
`-fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie` and linker
`-fsanitize=address,undefined -no-pie`, ASAN_OPTIONS=detect_leaks=1:halt_on_error=1
and UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1. Default-OFF omits both
options. Every final launch used the Python environment described above.

SHA-256 bindings (paths relative to evidence root):

```text
f6a821cc8cdb16527ce2d68d8761ba864800d02ad2bf89f32788402458e18bca  source_binding.sha256
e7274a90c845f09a9a09d680d4ce90eb70bc603588977cf6eaff720490775e7b  ctest-native-debug-1.log
5decb5aa52fe7f81c8589bd4f05cf25239e7575301c6099705a02ea02c372dfa  ctest-native-release-1.log
a16225b822318c21cbd03d28c83732f25b8e1f76622fe8f1919ec36f0daff2ec  ctest-native-release-2.log
103b21852e1678cf5f905ac20809f2039b50d1b30d8b7fc481cb806c5789051d  ctest-native-sanitize-1.log
0be75dd701b03ac02b4114deca6aa534d1edf0bd0dc15893e86b4fb20e8e495e  ctest-native-default-off-1.log
c1fae8d1aa861ce3f446c59e39933609908dfd28df8b55c984887f9682b755f2  native-debug/research_sitl_native_closed_owner_test
c1a101cbdf35a2074b2b30fc4228e74cf3ec016234b1ecd13d9adab6eb187d04  native-release/research_sitl_native_closed_owner_test
dd1dabe806281aad2ceb1e1ac7fc8af7ed101759be37bae9e3afa365c51c164d  native-sanitize/research_sitl_native_closed_owner_test
```

No final test failed or threshold was relaxed. An earlier source-direct Debug
campaign also passed 37/37 but took 363.74 s because of repeated Windows/WSL
source reads; it preceded final formatting/two added isolation probes and is
not the final acceptance evidence. Two read-only command-wrapper retries
(PowerShell find placeholder quoting and an unfinished hash-process result)
changed no source or test outcome.

Preservation review found no missing pre-existing file. Only the 12 explicitly
owned code/document files changed and six scoped files were added at N1
verification; subsequent acceptance documentation is intentional. Stock
Commander, existing offline tests/serializers, C1C-S2, geometry, separate Gazebo
repository and Cessna remain unchanged. Actual native execution is still pending.

## Next native integration constraint

Pinned source review found a relevant forced-stop exception. Normal Commander
construction/run/cleanup uses one task, but ModuleBase::stop_command can cancel
and delete from its caller after timeout; POSIX cancellation does not join.
Therefore N2 must close in the normal run-end source context, not the destructor.
The proposed hook is after buzzer_deinit() and before Commander::run() returns.
Only then may a release-published normal-completion flag be set. Destructor is
limited to a retirement marker; no owner turn, record copy, sink I/O or free.
Post-stop export needs acquired completion/retirement, unique valid bootstrap
and successful normal stop. Forced stop is incomplete/failure. Heap owner/trace
must remain alive for the process lifetime, including failed cleanup paths.

This is a source-backed design constraint, not executed N2 evidence. Source:
Commander.cpp run/task_spawn, platforms/common/module_base.cpp stop_command and
run_trampoline_impl, platforms/posix/src/px4/common/tasks.cpp task cancellation.
Requested POSIX stack adjustment and scheduler-permission fallback differ;
verify the actual native profile and deepest result-by-value call chain before
releasing a native run. N1 heap allocation alone cannot establish stack safety.
