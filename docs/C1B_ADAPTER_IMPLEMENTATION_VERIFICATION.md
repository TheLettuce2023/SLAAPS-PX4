# C1B-A: offline diagnostic adapter implementation verification

2026-09-20. Master verification: Codex. Independent static reviewer:
c1ba_review. Result: PASS for the bounded adapter slice, not full C1B/C1 or
roadmap Phase 2. Source changes remain uncommitted.

## Scope and evidence classification

Fact: the user approved separate SITL-only diagnostic selection and continued
offline software development. The adapter preserves supplied physical arm/mode
values; successful validation requires actual Disarmed/NotSelected. This
diagnostic selection never selects Offboard or grants control authority.
No PX4/Gazebo runtime, hardware or simulator truth input is used in this slice.

Implemented:

- One immutable diagnostic adapter/guard; private trusted operator attachment,
  monotonic operator lifetime, and synchronous detach/loss closure.
- Exact owner/run/controller/operator/physical-generation/transition/selector/
  full-map grant matching with one supervisor event stream.
- Separate selector/session/ACK barriers, unchanged shared ACK/request checks,
  diagnostic-only strict unused admin fields, distinct-session recovery.
- Four-record maximum, explicit operation index, immutable per-record snapshots,
  pre-mutation counter reservation, once-only terminal sentinel.
- No request cache/control eligibility/Flight accepted history; ordinary APIs
  reject a diagnostic instance without copying output or mutating state.
- Opt-in standalone build only, plus ordinary state/session/admission suites
  linked separately against the opted-in class layout.

Measurement: all results below are executed native-WSL host tests using
synthetic decoded packets, clocks, state and authentication facts. They do not
measure MAVLink signing, sensor readiness, scheduling, actual clock uncertainty,
physical stability, acceleration tracking or hardware portability.

Inference: the tested adapter is suitable for the next offline owner integration
step. No conclusion about operational readiness follows from these results.
Assumption: trusted callers serialize turns and provide honest physical receipts;
live provenance is explicitly unimplemented here.

## Verification

Build root: /var/tmp/c1b_adapter_nJxlBECC on Ubuntu-24.04 WSL, user kspry04.
GCC 13.3.0, CMake 3.28.3, Ninja 1.11.1, Python 3.12.3, AStyle 3.1.
No packages installed. Builds used distinct native-WSL directories, sequentially;
these are standalone host builds, not simulator or board firmware builds.

| Configuration | Result | Retained log relative to build root |
| --- | --- | --- |
| Debug, diagnostic ON | 24/24 pass | ctest-debug.log |
| Release, diagnostic ON, run 1 | 24/24 pass | ctest-release-1.log |
| Same Release, run 2 | 24/24 pass | ctest-release-2.log |
| Debug, ASan/UBSan, diagnostic ON | 24/24 pass, no reported sanitizer failure | ctest-sanitize.log |
| Fresh Release, default diagnostic OFF | 17/17 pass | ctest-default-off.log |

Detailed per-target output remains in each build directory's
Testing/Temporary/LastTest.log; Release retains the second run there.
Adapter test: 3,400 explicit checks, zero failures. Existing guard:
1,024 predicate tuples, 88 request evaluations, 12,163 checks, zero failures.
Compile isolation: 25 probes pass, including absent default-build diagnostic
types, private attachment/close, no implicit Flight result conversion and no
frozen-v1 status mapper overload. All counts include helper/setup assertions,
not independent flight scenarios or exhaustive state-space coverage.

Adapter coverage: A01/A02; exact binding fields; event replay/gap/exhaustion;
A10-A22; adapter-only lifecycle portions of A23/A24/A28; A27; R14/R15;
C01-C07. Includes independent golden chronology/arithmetic, 46 single-field
ACK/Establish envelope negatives, malformed authority after P and P/S prefixes,
and supervisor exhaustion before physical/clock mutation. A05 checks nine
TrustedClockMap fields plus seven other binding fields; the physical
clock_mapping_valid bit is separately checked as physical state, not a grant field.

Not covered by adapter acceptance: A03/A04 full facade semantics, A07-A09
tickets, full A23-A26 broker/owner outcomes, C08 evidence sink and E01-E04 local
JSON/evidence corruption cases. The dedicated adapter test does not directly
inspect private guard cache internals; prior guard tests verify no-cache behavior,
while adapter tests check unavailable request output/eligibility/history.
Existing Flight tests and expected values were not changed.

Reproduction (from the repository in WSL, using a fresh isolated directory):

~~~sh
cmake -S src/lib/research_acceleration -B <root>/debug -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DRESEARCH_SITL_DIAGNOSTIC_TESTS=ON
cmake --build <root>/debug -j 4
ctest --test-dir <root>/debug --output-on-failure --output-log <root>/ctest-debug.log
~~~

Use Release in a separate directory and execute twice. Sanitizer CXX flags:
-fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie; executable linker
flags: -fsanitize=address,undefined -no-pie. Runtime environment:
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1,
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1.
For default-off, omit RESEARCH_SITL_DIAGNOSTIC_TESTS entirely in a fresh Release
directory. Scoped AStyle run and subsequent dry-run report all changed C++
files unchanged.

## Failures and review corrections

The first build failed -Werror=switch for newly appended ApiDomainMismatch in
two WireStatus switches. Both now explicitly refuse serialization; no new wire
code or version was allocated. The initial Debug test log is retained as
ctest-debug-initial.log: 23/24 targets passed, with one of 2,470 initial adapter
checks failing. Its compound golden assertion incorrectly expected the
ProducerTimeRegressed latch Decision to contain age=14. Actual unchanged guard
behavior computes age before the latch but returns age_valid=false/age=0.
The corrected test independently calculates 14 and separately asserts the
actual missing age evidence; it does not change Flight/guard outcomes.

Independent review additionally found the already-Disabled lifecycle return
label mismatch, missing prefix-terminal/private-seam/envelope tests, and
inherited unused admin ACK-field permissiveness. Corrected and re-reviewed.
Master moved supervisor MAX handling into pre-mutation capacity reservation;
a dedicated new-generation/regressed-clock test proves no poll/latch mutation
before terminalization. The contract records private trusted attachment,
strict diagnostic-only admin fields, and action_performed meaning actual request
evaluation rather than acceptance. The matrix's literal protocol=1 was corrected
to the existing packed wire-v1 constant 65536; no protocol change.

Independent final static review: PASS, no unresolved blocker in this slice.
Master separately ran the configurations above. Static review is not treated
as runtime evidence or approval of unimplemented owner/operational code.

## Source and artifact identity

PX4 HEAD: e880f1cd79e2453ef3dfaf68ee1c067b98ea9a53.
Separate clean Gazebo HEAD: bb0b9cf974acf4f1bcb5f5fcf80b88841562dea9.
The worktree is dirty and these pins alone do not identify the new code.

SHA-256 of changed implementation/build inputs, relative to
src/lib/research_acceleration:

~~~text
DiagnosticAdapter.cpp E0C844CAFC88DB9A572371590ACCA8F6376B02B3F3DD76B093D1AD6BB4336D29
DiagnosticAdapterTest.cpp 01176B580B6A026E41801E4FC5196B30D9D50D4C0C17CDD6818D312E0244DD3F
WireAdapterChecks.hpp 6D22B234A3FD1639785744F66C4E7BAC1609A11F26FC82C7667722A20F53835C
WireAdapter.cpp 1B473255E929CD2834C31A4926B5C9532D688D3BD075C5ACD57CCC479D54AF8E
WireAdapter.hpp AD38D097BB74D2A2A41094E00634A2A509530F1AABD2DA78BBD6A6D1053C0942
WireStatus.cpp D9298B23658E906FAA3FBBD179CC0A9582BBACE85605C0CBB769B0460466D61E
DiagnosticBuildIsolationTest.py 046510E41892EEA589B2B503296F09469C08B1FEFB89DF1B8B52F6001EE11F12
CMakeLists.txt 6FBE35D807D2A070876B650CF7F62DA38C903C1EC8C8ACFFBAF90CC2C2D0BB13
~~~

SHA-256 of research_acceleration_diagnostic_adapter_test in each build:

~~~text
debug 292552a92670e646f145298087d0e8d444a4b918085d4c6e533081a4431a5c46
release a539b4259964c9576c88a4048e9aff96d57687494d6883450c3d21a9665fbab4
sanitize f437d18bf0682f780c828224846c9aedd8148d6491e765b8d6013d10a4e16a32
~~~

SHA-256 of final test logs:

~~~text
ctest-debug.log accfd20ca4940556721ba4a13e345866f646e3602cebe32139739548d23cfcf0
ctest-release-1.log 7eba99d55b09dc7083c5fc1b65b71871d5d2d4c9cb0e3375bf9ce1b532d43f43
ctest-release-2.log 5dd888519893483b1b4c2e0f0b4bbefef5e531a2fbb9bb80495f58fcd8142278
ctest-sanitize.log 4f7484e8704bd4baec6428cae4bedb32086f901729ac8126783c3ce019faa5dd
ctest-default-off.log e66564494a872cfefe8f07be2fd3f54c70ef314c5a9d6253884a7a4fc5d0f23c
~~~

Preservation: entry inventory 709 pre-existing dirty/untracked files. Intended
existing edits are five source/build files above (excluding the three new C++
files) and four documents: API contract, matrix, phase status and timeline.
No geometry, original setup, guard implementation/tests, ordinary tests, owner/
broker, generated wire, live C1A/C1H artifacts, stock Cessna or submodule edits.
Final SHA-256 comparison of all 709 entry files found exactly the nine intended
existing-file changes and no missing files; every other entry hash is unchanged.
The final inventory has 713 files, including only the four intended additions.
Default-off archive symbol inspection found no diagnostic entry points.

## Exact next gate

Implement diagnostic owner/broker submission, one-shot shared confirmation
tickets, operator-loss cancellation/closure and independent Completed-result
validation around this adapter. Then complete local evidence serialization/
checker negatives and the remaining full C1B matrix. Existing approval permits
that offline work; no human measurements or geometry are currently required.

A live PX4 diagnostic gate must independently establish honest physical state,
clock qualification, estimator readiness and operator-loss provenance with
outputs disabled. Phase 2 operational source/consumer qualification and Phase 4
flight control remain open. Missing geometry/physics becomes blocking before a
validated plant, effective control allocation or launch/stability claims.
