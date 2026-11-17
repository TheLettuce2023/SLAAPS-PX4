# C1B-G: offline diagnostic guard implementation and verification

2026-09-20. User requested concrete software development after the accepted
C1B-I contract. Master and independent code review: PASS for the **guard slice**.
This is implemented C++, not another design-only gate. Full C1B adapter/owner
implementation, C1 PX4 attachment and roadmap Phase 2 remain OPEN.

## Implemented scope and source facts

- New DiagnosticTypes.hpp and DiagnosticGuard.cpp provide private immutable
  diagnostic construction, typed evaluate/poll/establish/clear results, and
  session invalidation that preserves history and replay floors.
- RequestGuard.hpp/.cpp share the existing context prerequisites, payload/
  arithmetic/replay body, session checks/commit and latch-clear implementation.
  No physical observation is rewritten to Armed or Selected.
- The only active-request assignment is in the ordinary Flight evaluate wrapper.
  Diagnostic context requires actual Disarmed/NotSelected plus valid clock,
  connection, required state and separate SelectedForValidation input. That
  selector input is private adapter/test-peer data, not a public message field.
- Ordinary APIs on a diagnostic guard return ApiDomainMismatch without mutation;
  revoke is inert and access leaves the request sentinel unchanged. Diagnostic
  APIs on a Flight guard return WrongDomain without disturbing its active cache.
- Successful diagnostic requests advance the existing request/producer floors
  but never become eligible demand. Poll is NoEligibleRequest, not request
  success. Session/clear results cannot impersonate request validation.
- The local-only appended DecisionCode does not renumber prior values. The
  existing frozen V1 mapper rejects it without writing the destination.
- RESEARCH_SITL_DIAGNOSTIC_TESTS defaults OFF. The opted-in library and consumers
  use separate objects and class layouts; no executable links both variants.
  Ordinary headers omit diagnostic types. Explicit compile guards reject PX4,
  NuttX, board, lockstep, timestamp-proxy and non-Linux contexts.

These are source-backed facts and offline software invariants, not live
estimator/clock qualification. The private guard still relies on its future
single-owner adapter to provide authoritative observations and selection.
That adapter, its report barriers and the diagnostic operator broker are NOT
implemented by this slice. Ordinary WireAdapter/owner code remains unchanged.

## Executed verification

Owned WSL Ubuntu-24.04 build root: /var/tmp/c1b_guard_umY0izWL.
Each configuration has its own native Linux build directory; builds ran
sequentially, without baseline caches, mounts, simulator or firmware builds.
Tools observed: GCC 13.3.0, CMake 3.28.3, Ninja 1.11.1, Python 3.12.3,
AStyle 3.1 with Tools/astyle/astylerc.

| Configuration | Measured outcome | Retained summary log |
| --- | --- | --- |
| Final Debug, diagnostic option ON | 20/20 targets pass | ctest-debug-final.log |
| Release, diagnostic option ON, first run | 20/20 targets pass | ctest-release-1.log |
| Same Release binary, repeated run | 20/20 targets pass | ctest-release-2.log |
| Debug + ASan/UBSan, diagnostic option ON | 20/20 targets pass; no reported sanitizer failure | ctest-sanitizer.log |
| Fresh ordinary Release, option omitted/default OFF | Existing 17/17 targets pass; no diagnostic target enabled | ctest-default-off.log |

The first Debug campaign also passed before a final test-format-only change;
ctest-debug.log is retained, but the final Debug run above is the acceptance
evidence tied to the final formatted hashes. Existing Flight guard assertions
remain unchanged: 231 checks pass both in the ordinary library and through
ordinary construction in the opted-in library.

The new test executes 1,024 independent predicate tuples (exactly one positive
tuple), 88 request evaluations, and 12,163 checks with zero failures. The 88
evaluations include 62 specified R01-R13/R16 variants plus setup/recovery cases;
check counts include helper assertions and are not coverage percentages.
The separate compile-only test passes 18 positive/negative probes, including
private API access, no implicit diagnostic-to-Flight conversion and forbidden
build contexts. Symbol inspection finds no diagnostic symbols in the ordinary
Release library.

The independent test worker and independent code reviewer had disjoint scope;
master inspected the new tests and source changes and ran all builds/tests.
The final review found no must-fix issues. Master also compared the pre-edit
source snapshot: whitespace-normalized shared payload checks are identical
except removal of cache assignment into the Flight wrapper; the shared
latch-clear body is identical to the original.

No executed build/test failed in this increment. During test-source review,
the establishment helper was corrected to compare the prior rejection count
instead of assuming every instance had zero history. An explicit
ApiDomainMismatch mapping-sentinel assertion was added before final acceptance.
No threshold or old expected result was weakened.

## Coverage limits and next implementation

The independent guard oracle covers ordered state prerequisites, finite/bounds/
frame/quantity/CG checks, validity/age/future/expiry arithmetic, replay floors,
producer/validator regressions, session invalidation/recovery, no-cache
behavior and cross-domain misuse. R09 earliest-expiry-only underflow is
unreachable after positive-duration and earliest-creation checks: its combined
underflow fixture is explicitly labelled masked, not independent branch coverage.
Boundary fixtures that need wider synthetic uncertainty or axis bounds declare
those separate configurations; none is an operational requirement.

This does NOT execute the complete C1B_OFFLINE_API_TEST_MATRIX.md. Remaining:
diagnostic adapter identity/selector storage, unified turn/barrier/capacity
handling, session/ACK/authenticated ingress integration, owner/broker tickets
and lifecycle closure, and the local diagnostic evidence schema/oracle.
The current checks are direct private-guard fixtures. Existing ordinary adapter
and owner regressions do not count as tests of unimplemented diagnostic paths.

Next coding step is that single-adapter diagnostic integration using this guard,
then owner/broker and complete report/evidence verification under the accepted
contract. No new design approval or physical measurements are required for
those offline steps. Live PX4 diagnostic admission still needs qualified
state/clock/estimator/operator-loss provenance and explicit diagnostic transport
binding. Nothing here selects a flight mode, publishes uORB control demand,
arms, drives EDF/servos, connects Gazebo or demonstrates acceleration tracking.

## Reproduction and provenance

From the repository root inside WSL, use cmake -S src/lib/research_acceleration
with a fresh -B directory, -G Ninja, -DCMAKE_BUILD_TYPE=Debug or Release and
-DRESEARCH_SITL_DIAGNOSTIC_TESTS=ON; then cmake --build <directory> -j 4 and
ctest --test-dir <directory> --output-on-failure --output-log <log>.
Debug/Release acceptance used CMAKE_CXX_FLAGS=-Wall; diagnostic source/test
targets additionally enforce -Wall -Wextra -Wpedantic -Werror.
Sanitizer CXX flags: -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie.
Sanitizer executable linker flags: -fsanitize=address,undefined -no-pie.
Test environment: ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 and
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1.
Compile-isolation probes are syntax checks, not sanitizer-instrumented programs.
Detailed executable output remains in each build's Testing/Temporary/LastTest.log;
CMakeCache.txt and build.ninja retain effective configuration/build commands.

PX4 HEAD e880f1cd79e2453ef3dfaf68ee1c067b98ea9a53; separate Gazebo HEAD
bb0b9cf974acf4f1bcb5f5fcf80b88841562dea9 remains clean and untouched.
The entry inventory contains 704 pre-existing modified/untracked files,
including all supplied geometry. Only the three guard/build source files and
explicit current-gate documentation are intentionally edited. No install,
commit, push, PR, hardware build/flash or physical operation occurred.
Final SHA-256 preservation audit of all 704 entry files found exactly the eight
intended existing-file changes (three guard/build files and five gate documents).
The final inventory is 709 files: four new code/test files and this report.
Existing tests, all geometry and the separate Gazebo repository are preserved.

Final source SHA-256 (all paths under src/lib/research_acceleration):

| Input | SHA-256 |
| --- | --- |
| RequestGuard.hpp | `9fd8039c723eef08a9f9a3d229d3e968093984979116fbfa05f0368df231dfea` |
| RequestGuard.cpp | `18f9db6fa5696340ae7a98fb799a3ee2486f1b3f9fa46c917b540e59285e7ee8` |
| DiagnosticTypes.hpp | `d325242f3e3512b484dbcf7b502ffccc3bd71dd533453550b1b79485dfbf5b52` |
| DiagnosticGuard.cpp | `4ccfa2a57d8ecf3248d14a0e01330beb3ceceadac6896839e00227f5c2f4acab` |
| DiagnosticGuardTest.cpp | `ba2b71ead6be55f12a41760b80bd50cafe6c4b90df8276bc4ab544aa0a978b94` |
| DiagnosticBuildIsolationTest.py | `fc6474d86d5b5ae33004ad5241d88c344fb0b71d4421aa1dc3cd460eec8ae26d` |
| CMakeLists.txt | `1210dba73b7e18c6098837d51071de545fb02e64234f7cc36b023b5ca95068eb` |

Retained artifacts relative to the owned build root:

| Artifact | SHA-256 |
| --- | --- |
| debug/research_acceleration_diagnostic_guard_test | `bf1c23cece1518d790e506541e957333c8ed2517b1e3e1aab14afe1c22cfc55c` |
| release/research_acceleration_diagnostic_guard_test | `f12767d9ad61e65316aded0c858640a37d8642967e05f556660236a27f1d1126` |
| sanitizer/research_acceleration_diagnostic_guard_test | `7ce04107b1e2df12cad7d0b19995fc5b94910cc5a9112860e2fada2ccc68d198` |
| ctest-debug-final.log | `ffaba66d96d300f2e5d89a06166006559ab53b5f566074595333f7e96381c863` |
| ctest-release-1.log | `a1bbd484b567a50d98e5880ea915e50e54a61ad534952d324795e08d1be1a1f9` |
| ctest-release-2.log | `94890857f3259471392db0540e2062bb89a6ea10ff18cc1acf4351f421f10ce4` |
| ctest-sanitizer.log | `c96d0b06eeec1d27d1a2f8ca8a61c2db7e6672da5164f412ea83b03359bf536a` |
| ctest-default-off.log | `18ce8320e2400375b2f68cc0270ea65584e0aea26b43ecd8495dbf8707bce07c` |
