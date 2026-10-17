# Research Acceleration S1 Verification

Review completed: 2026-09-17 17:58 UTC. Scope: inactive standalone software contract only.

## Outcome

Astra accepts the geometry-independent S1 library and focused standalone tests after independent Prompt K review. This is not an active PX4 controller, an operational Offboard interface, a plant validation, a board build, or a physical flight result.

Two executables passed: request guard (231 executed checks) and frame mathematics (165 executed checks). Check counts include fixture/setup assertions; they are not 396 independent flight scenarios or proof of exhaustive state-space coverage.

| Final configuration | Compilation | CTest result | Repetition |
| --- | --- | --- | --- |
| Debug | GCC 13.3, C++17, `-Wall -Wextra -Wpedantic -Werror` | 2/2 passed | One final run |
| Release | Same warnings, optimized/NDEBUG | 2/2 passed | Three consecutive runs per executable |
| Debug + AddressSanitizer/UndefinedBehaviorSanitizer | Same warnings, `-fsanitize=address,undefined -fno-omit-frame-pointer` | 2/2 passed | One final run; no sanitizer diagnostics |

All results above were rerun after the final rejection-counter fix and test additions. The custom checks remain active under NDEBUG. No physics, gains, mass, inertia, installed thrust, servo dynamics, operational timing or flight-envelope thresholds are supplied by these tests.

## Source identity

Parent HEAD: `e880f1cd79e2453ef3dfaf68ee1c067b98ea9a53`. The seven new files are uncommitted/untracked at this handoff; HEAD alone does not identify this implementation. SHA-256 below identifies the final tested content under `src/lib/research_acceleration/`.

| File | SHA-256 |
| --- | --- |
| CMakeLists.txt | `BF8241FED4C64BF22BE33613B59F2C72C1D8AE10709586CCCCD607B0CD8CBE04` |
| FrameMath.cpp | `5E43BAC85B1E16E7CC03B7708F865E24639FB3B684913F430A4E14A515F6E989` |
| FrameMath.hpp | `DFA56A16CFFCD853C03C6E038F763B19AD48BB8582409B33DEF935B2332C998A` |
| FrameMathTest.cpp | `A5F87FAE2D7190DE2D25686BA896EB230B3711024DCDD5FCCBEC4D7B4F0A1AB8` |
| RequestGuard.cpp | `740BF51EC6C268851C5B09BFEED530483CC11D12894E8D48E16164C5847B9208` |
| RequestGuard.hpp | `246BAFD2B276AC18E544D883FAEEAD544F54DB7A1F45D14E925A6816B9A0DE9D` |
| RequestGuardTest.cpp | `63DC296C42088815AA53C56E9A98A3A36412F2489E96F538481381BA24417A8B` |

Reused matrix and minimal definitions headers belong to the pinned parent revision, not to the new library. The Gazebo submodule remains clean at `bb0b9cf974acf4f1bcb5f5fcf80b88841562dea9`, with its parent gitlink unchanged.

## Commands and retained evidence

Observed host: WSL Ubuntu-24.04; GCC `13.3.0-6ubuntu2~24.04.1`; CMake 3.28.3; Ninja 1.11.1; Artistic Style 3.1. No tool/package was installed.

All host builds used a newly allocated native directory, `/var/tmp/research-acceleration-s1.b3JJ9r0v/`, with separate `debug`, `release`, and `sanitize` children. No stock SITL mount, cache or build directory was used. Artifacts are retained there, not deleted; they are local development evidence, not a durable archival service.

Exact successful command pattern, run through `wsl -d Ubuntu-24.04 --exec` from PowerShell:

```text
cmake -S "/mnt/c/CODEX PROJ'S/SNC SENIOR DESIGN/Flight Sim SITL/src/lib/research_acceleration" -B /var/tmp/research-acceleration-s1.b3JJ9r0v/debug -G Ninja -DCMAKE_BUILD_TYPE=Debug "-DCMAKE_CXX_FLAGS=-Wall -Wextra -Wpedantic -Werror"
cmake --build /var/tmp/research-acceleration-s1.b3JJ9r0v/debug --parallel 2
ctest --test-dir /var/tmp/research-acceleration-s1.b3JJ9r0v/debug --verbose
```

Release used the same configure command with `release` and `-DCMAKE_BUILD_TYPE=Release`; final CTest added `--repeat until-fail:3`. Sanitizer configuration used `sanitize`, Debug, appended `-fsanitize=address,undefined -fno-omit-frame-pointer` to CXX flags and supplied `-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined`; final CTest used `--verbose`.

Final logs are `<configuration>/Testing/Temporary/LastTest.log` under that unique directory:

| Configuration | Log SHA-256 |
| --- | --- |
| debug | `3552a0e1232e1ff312d6c4884ef6d4756449d26ec0922798166971ddb1edcc71` |
| release | `0b82f437e1568e816ff9b0ed7e4ca54ec531565311ed20fb245551b2161b40fd` |
| sanitize | `ae10c8feff62b3ff7195cdbfb47974cd3f7428e7c3927c4d4dee8c2507181356` |

The run tree also retains CMake caches/configuration logs, Ninja build state and executables. This human-readable, source-hashed record is not the machine-validated manifest/schema/checker specified for later campaigns in `SITL_TEST_SPECIFICATION.md`; that tooling is still unimplemented.

## Independent review and corrections

Implementation: `gpt-5.6-terra`/high under bounded Prompt I. Architecture and independent Prompt K review: separate `gpt-5.6-sol`/high assignments. Astra reviewed source, revised contract details, expanded math and final request regressions, formatted, compiled and ran tests.

Review corrected disarm-plus-malformed-packet cache retention; ambiguous context-versus-packet rejection precedence; explicit clock-epoch reset and session replay floors; invalid enum diagnosis; misleading empty-cache expiration; nominal administrative rejection counting; and double counting of an already-latched administrative call. Final independent review found no remaining correctness finding for the seven-file S1 scope.

Math tests cover FRD/NED basis signs, nose-up body +X to NED -Z, quaternion sign equivalence/inverses/norm boundaries, current-body held demand, gravity and free fall, ground-relative rotating-coordinate derivatives, nonfinite input, arithmetic overflow and output preservation. Request tests cover configuration, bounds, protocol/source/session/order, conservative time uncertainty, age/expiry/overflow, reconnect/replay, observed state/mode/arming changes, fixed latches, revocation and diagnostic semantics.

## Failures and limitations retained

- Initial compilation failed because matrix transitively includes `px4_platform_common/defines.h`. The standalone build now explicitly reuses the existing minimal `platforms/ros2/include/` header; this is a header dependency, not ROS middleware or PX4 runtime linkage. No invented shim or alternate quaternion implementation was added.
- A subsequent attempt found the initial `/tmp/research-acceleration-s1.gFjobqLe` build absent following WSL lifecycle changes. Cause beyond the observed disappearance was not proven. Verification moved to the unique `/var/tmp` directory; no launcher/shared-cache change was made.
- The first request-test run failed a test whose alleged empty axis-box/norm intersection was actually feasible. The fixture was corrected to exercise a genuinely empty intersection; production validation was not loosened. Generic failure labels were replaced and coverage expanded.
- Final scoped AStyle dry-run reported all six C++ files unchanged. `git diff --check` and separate direct-file whitespace/Markdown-fence checks passed; the direct checks include untracked additions. All seven recorded source hashes matched final files. Hashes also matched all nine protected setup/launcher/planning/baseline files; the index was not staged and the gz submodule remained clean.
- Release archive undefined symbols were only standard math/stack-protection symbols (`sqrt`, `__stack_chk_fail`). Source/dependency review finds no uORB, MAVLink, HRT, parameter, Gazebo, actuator publisher or runtime firmware dependency.
- The CMake entry is deliberately standalone-only. Nothing was added to the root PX4 build, an airframe, startup script, parameter set, receiver, allocator or output driver. Pixhawk compilation/resources and scheduling are not tested.

## Scope of approval and next gate

The source/session identifiers provide routing and replay bookkeeping, not authentication. The caller must serialize access, provide valid hardware-representative state/context, preserve and validate clock mapping with uncertainty over the whole request interval, observe every eligibility transition, avoid reusing old session identifiers, and use the approved quaternion tolerance consistently. S1 cannot infer omitted transitions or establish transport authenticity.

CG reference and current-body-held FRD interpretation are explicit master working conventions. Accepted-after-recovery means a newly accepted, age-valid request with increasing replay floors; it does not prove producer generation after a supervisor transition epoch. No physical fallback has been selected.

This acceptance releases design review of an inactive MAVLink adapter/wire contract, not its automatic implementation or any actuator-producing control law. A versioned wire format, producer/translator conformance, trusted clock/source/session mapping, supervisor integration and status evidence must be approved before adapter implementation. Full tracking additionally needs the plant/effectiveness/normalization and physical-loss-response gates. Roadmap Phases 0, 1 and 2 remain incomplete.
