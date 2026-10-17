# C1C-S1 standalone state-receipt verification

2026-09-20. Master verification PASS; independent source review passed.
Scope: [C1C-S1 contract](C1C_STATE_RECEIPT_CONTRACT.md), not live PX4 capture,
admission, estimator/clock qualification or roadmap Phase 2 completion.

The new C++ checker enforces immutable producer/observer/clock identity,
Initial/Transition/Sample continuity, exact before/after state, checked uint64
delivery and transition counters, explicit freshness configuration and sticky
faults with no partial retained-state update. It has no admission/control output,
reset, rebind, copy or move API. Fresh includes structurally valid Armed/raw
Offboard observations; it means neither disarmed nor authorized.

All test identities, clocks and freshness budgets are synthetic. Counter-edge
fixtures use a private test peer; they are not billions of executed transitions.
Known native arm/nav definitions were checked against the pinned message source.
No operational default, measured scheduler budget or physical value was added.

## Verification

Native WSL build root `/var/tmp/c1c_state_qbdX7m1A`; installed GCC 13.3.0,
CMake 3.28.3, Ninja 1.11.1, Python 3.12.3 and AStyle 3.1. Fresh independent
Debug, Release and sanitizer builds; no package installation or simulator build.

| Configuration | Results | Retained log |
| --- | --- | --- |
| Debug | 2/2 pass | ctest-debug-final.log |
| Release, first run | 2/2 pass | ctest-release-final-1.log |
| Same Release, second run | 2/2 pass | ctest-release-final-2.log |
| Debug with ASan/UBSan | 2/2 pass; no reported sanitizer failure | ctest-sanitize-final.log |
| Fresh default-OFF | Configure/build succeeds; no checker library/executable/test targets | default-off/build.ninja and CMakeCache.txt |

Native test: **139 named finite variants, 815 checks, zero failures**. Build
isolation: **22 syntax-only probes, zero failures**. Totals include helper
assertions and declared boundary variants, not flight cases or exhaustive proof.
Default-off is target absence, not a passing zero-test campaign. Initial native
Debug test also passed; no failing compile/runtime case was observed in S1.
Scoped AStyle formatted the new cpp/test; final dry-run reports all three C++
files unchanged. Independent review found no blocking algorithm/test issue.

Reproduce from the repository root in WSL:

```sh
cmake -S Tools/research_sitl/state_receipts -B /var/tmp/<new-dir>/debug -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DRESEARCH_SITL_STATE_RECEIPT_TESTS=ON
cmake --build /var/tmp/<new-dir>/debug -j 4
ctest --test-dir /var/tmp/<new-dir>/debug --output-on-failure
```

Repeat with Release twice. Sanitizer CXX flags:
`-fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie`; linker flags:
`-fsanitize=address,undefined -no-pie`. Runtime:
`ASAN_OPTIONS=detect_leaks=1:halt_on_error=1` and
`UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`.

Source SHA-256 in Tools/research_sitl/state_receipts:

```text
340d609d6593ef577a4ff2b8efbcb6d02aa996168f461795c05266986c1a443b BuildIsolationTest.py
cbc95e03bbe6abccaf4d1cb9a0e839649bf152e7015c591f44a9447255ff5340 CMakeLists.txt
ccc55c48fd7ba5517c5f45be0a1039ef52937485a81dcda0f1e4ba5b511bb1ab StateReceiptChecker.cpp
28c21c84b5d8c1e62ccb76db6336b4c924f32890c66d7521fa190d9905b50ee6 StateReceiptChecker.hpp
60d40b8679edb17ced58e744c18cd261e1cc0b097e2d4ea5946bf311421ef759 StateReceiptCheckerTest.cpp
```

Executable SHA-256 for debug/release/sanitize state_receipt_checker_test:

```text
77e252c437099a28d734f874d9f965f0ea06eaf3292973e7d4bcc430e140a6ab
9ea65ecffa42f3b9d36d6337133a7b176cfc4e7267be26f54af413fe7d8d8111
d4cb74c93cba4a7661c72463d22e87813636d34af8ffc2105441739b8e25e401
```

Final log SHA-256 in the four tested rows' order:

```text
9b8588a4c0cd02e96af985246348aaaa74c0984bf71e4273fe7d2bade3c1e481
60404420f603ec238d1c4eb58f3887db1bd1538edc8817f72e95e3a8dc718d04
42b0c48e87001ef39b3f3187d93452c9f055e77b4f61c532095091fa033254bb
af9f8cc873270779820a75ce458bb30d41e0ece529ff7179f102aaea4661b42b
```

## Preservation and next gate

S1 adds five standalone source/build/test files and two documents; current
status/timeline are maintained separately. It does not change C1B sources,
Commander, C1A startup/probe, message definitions, geometry, stock Cessna or the
separate Gazebo repository. No commit, install, live endpoint, flash or physical
operation occurred. Existing C1B regression results remain their recorded scope,
not a claim that unrelated live software was retested by this two-target suite.

Next: technically review C1C-S2 actual producer capture using an isolated source
overlay, source-commit hooks, subscription before Commander construction,
loss-detecting queued delivery and final producer-summary reconciliation. S1
alone cannot prove all source writes were instrumented, detect a missing final
tail without an independent completion record, or solve a concurrent source
change versus admission race. S2 must remain disarmed and output-disabled and
must not load this host-only checker into PX4. No new human decision is needed
for that approved C1 development scope; positive live admission remains closed.
