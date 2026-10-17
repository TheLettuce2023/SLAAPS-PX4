# C1B-O: offline diagnostic owner/broker verification

2026-09-20. Master verification and independent static review: PASS for this
bounded owner/broker slice. Full C1B, C1 and roadmap Phase 2 remain open.
Changes are uncommitted.

## Scope and evidence classification

Fact: the approved offline contract now has a concrete single-owner diagnostic
implementation, redacted console facade, shared one-shot tickets and independent
broker result validation. The immutable diagnostic domain is host-test-only and
default-off. No PX4 mode, transport endpoint, controller or actuator publisher
is introduced. Actual fixture arm/mode values remain unchanged.

The owner retains its actual observation, checks immutable ingress receipt
provenance, consumes each receipt once, and performs exact-view administration.
Disable, detach and injected loss synchronously invalidate selection/session/ACK
without inventing a guidance packet or supervisor event for lifecycle closure.
Bad receipt provenance closes with the last valid retained observation and
permanently disables further administration by that owner.

The broker independently checks indexed operation results, immutable report
snapshots, correlation, event consumption, per-barrier counter deltas, latch
continuity and actual cause/common-decision consistency. Unknown or inconsistent
completion quarantines the broker and closes the owner. Ordinary Flight result
validation is not widened to accept diagnostic reports.

Measurement scope: native-WSL standalone host tests with synthetic clocks,
physical-state receipts, decoded packets and injected authentication facts.
These tests do not measure signing, real estimator freshness, live operator loss,
link deadlines, acceleration tracking, actuator response or physical feasibility.

Assumption: trusted callers serialize all owner/broker/ingress calls. The test
fixture is not an operational state or clock authority. Inference, conditional
on final verification: this code can support the next offline evidence-tooling
increment; it cannot establish readiness for live PX4 admission.

## Important scope distinctions

- Owner submission uses an already-observed exact retained physical receipt.
  A new PhysicalBarrier in its submitted result is inconsistent and rejected.
  The four-record P/S/B/O and P/B/O/S cases remain direct-adapter test evidence,
  not owner/broker traces. Real owner revoke is B/O/S; latched clear is B/O.
- Direct owner loss immediately removes authority, clears pending ingress and
  makes a retained broker ticket unusable through its changed binding. The
  broker's private pending bookkeeping clears on matching confirm or detach;
  there is no synchronous owner-to-broker cancellation callback. Broker detach
  cancels its pending ticket before calling lifecycle close.
- A diagnostic request may be validated while disarmed, but there is no eligible
  request cache/output and no Flight accepted history. Admission alone is not
  the producer-to-controller-to-allocator acceleration path.
- Ticket/receipt ages in tests are explicit synthetic fixture values, not new
  operational timing requirements. Control allocator normalization and one-owner
  actuator semantics remain requirements of the later control gate.

## Verification record

Build root: `/var/tmp/c1b_owner_f7FwVSQQ`, Ubuntu-24.04 WSL, user kspry04.
GCC 13.3.0, CMake 3.28.3, Ninja 1.11.1, Python 3.12.3 and AStyle 3.1.
No package installation, simulator build, board build or live connection.

| Configuration | Result | Log relative to build root |
| --- | --- | --- |
| Debug, diagnostic ON | 28/28 pass | ctest-debug-final.log |
| Release, diagnostic ON, run 1 | 28/28 pass | ctest-release-final-1.log |
| Same Release, run 2 | 28/28 pass | ctest-release-final-2.log |
| Debug, ASan/UBSan, diagnostic ON | 28/28 pass, no reported sanitizer failure | ctest-sanitize-final.log |
| Fresh Release, default diagnostic OFF | 17/17 pass | ctest-default-off.log |

The owner suite executes 1,124 explicit checks with zero failures, including
24 added cause/latch/authentication/precondition corruption cases and two real
latched-disable checks. The adapter retains 3,400 checks; the guard retains
1,024 predicate tuples, 88 request evaluations and 12,163 checks. All 31 compile
isolation probes pass. Counts include setup assertions, not flight scenarios
or exhaustive state-space coverage. Ordinary test sources/expected values are
unchanged, including their separately linked opted-in-layout regressions.

Coverage: positiveAndNonAuthority, ticketsAndDomains, lifecycleAndReceipts,
realOwnerRefusalAndProvenance, corruptedAdministration,
remainingCauseAndLatchCorruptions, malformedSuccessPreconditions and
uncertainOwners in DiagnosticSupervisorOwnerTest.cpp. These cover the owner
portions of A03/A04, A07-A11, A23-A28 and selected binding/provenance negatives;
they do not claim every variant of the complete matrix. C08/E01-E04 remain the
next evidence-tooling gate. Adapter-only report/capacity cases and private guard
cache checks retain their earlier documented scope.

Reproduce from the repository in WSL using a fresh native directory:

~~~sh
cmake -S src/lib/research_acceleration -B <root>/debug -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DRESEARCH_SITL_DIAGNOSTIC_TESTS=ON
cmake --build <root>/debug -j 4
ctest --test-dir <root>/debug --output-on-failure
~~~

Use separate Release and sanitizer directories and run Release twice. Sanitizer
CXX flags: -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie;
linker flags: -fsanitize=address,undefined -no-pie. Runtime:
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 and
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1.
Omit the diagnostic option in a fresh Release directory for default-off tests.
All seven changed C++ files pass scoped AStyle dry-run after formatting.

## Failures and review corrections

An early compile observed an unfinished worker test file; no such partial build
is acceptance evidence. A later owner run failed one of 910 checks because its
callback test expected NotSubmitted while quarantine was already active. The
correct result is Quarantined/Busy with no action/event/result. The assertion
was corrected without weakening the exactly-one-close requirement; the failure
is retained in `ctest-owner-review.log`.

Independent review found and corrected missing exact consumed-receipt result
correlation, incomplete common-decision checks, rejected-operation session/ACK
mutation acceptance, terminal detach mislabelling and a quarantine callback
reentry path. Follow-up review found contradictory success causes, malformed or
changing latch evidence, impossible success preconditions, false authentication
facts and inconsistent barrier causes. Negative tests deliberately corrupt
results from the real owner; they are not positive mock admission evidence.
Final independent re-review confirms all reported findings corrected; the
master separately inspected the changed code/tests and ran the configurations
above. Earlier Debug/Release passing logs predate the last review fixes and
are not substituted for the final logs.

## Source and artifact identities

The Git pins alone do not identify uncommitted code. Final SHA-256:

~~~text
Tools/research_sitl/supervisor/SupervisorBroker.hpp 07E3BD2DB2CCB427AE7177897D3D324BDEAF035430ABC5AA84EC6D1A8877C877
Tools/research_sitl/supervisor/SupervisorBroker.cpp 552219A109747F0C8C7C8349F7DB3AAC37DFCC2BE42025EF455DB39B2DCE6109
Tools/research_sitl/supervisor/HarnessSupervisorOwner.hpp FB4E95FFF2EC7EB3F11BD5C635B5B02C32E7A771E535C5989E625DCAD25D4166
Tools/research_sitl/supervisor/HarnessSupervisorOwner.cpp 76D5430C62AEBF8AF015A4BACDC05DB83D764CC3C3B17F4E1AB4D553A9E2C852
Tools/research_sitl/supervisor/DiagnosticSupervisorBroker.cpp 3E078BE765589F0003468570AFB7CDA8AC44B203863C8BFA64D00D81B1A44229
Tools/research_sitl/supervisor/DiagnosticHarnessSupervisorOwner.cpp 53B521550C6CE906FBD61A17E76E2C2120933231B68F14A988F6B4C55ACFD7B1
Tools/research_sitl/supervisor/DiagnosticSupervisorOwnerTest.cpp 9CB11D17AD8EEC6B150935A1557669A31CC0B8C12DDB6F6BB42CEA576CB8AB70
src/lib/research_acceleration/CMakeLists.txt E28A30AA5FBE8BDFF62903DF36B656DEE3E2FE22293C47F523FA20823CCDB4C5
src/lib/research_acceleration/DiagnosticBuildIsolationTest.py A5D86B334CA1BF20A41F6DC0C841437B4B7F258F0260F5AFD4F4741FCC80488D
~~~

Owner test executables and complete-suite logs, relative to build root:

~~~text
f629a6abeb6fe39f6864b217a202b6c8ab325118ba0faa05b9a112819b3b06b5  debug/research_sitl_diagnostic_owner_test
04f3942d0c5c279b3caf43c85a9b9aed4a121f3efa47d1409976d8aaedd2568c  release/research_sitl_diagnostic_owner_test
7f434ba9b3cd09296ae4bd7a30fdf886024ce53470bca77afecc1900019e7ea2  sanitize/research_sitl_diagnostic_owner_test
11041054ab30b5ba56b1019e3b2cb3d3d13722556f7c362a00efc04832391a32  ctest-debug-final.log
8d8d7215c91fdaa99e2f6feac5868445638cb4ce473b7c1d9ab20b148cf09c2f  ctest-release-final-1.log
bb9b47641823200557df21c135f7193519578f3d25a98fb100bb1d292582e94d  ctest-release-final-2.log
d2e649a7aa34572923fd6965cc88ca001770c59ac9bd6ab410937bdb4f6fa4af  ctest-sanitize-final.log
0de83a19b50ce2261ceb6ab9472d788d4cc6796b1b74493eda9ad2fe29c530b7  ctest-default-off.log
~~~

## Preservation and next gate

Entry inventory: 713 pre-existing dirty/untracked files. Intended existing
source edits: SupervisorBroker.hpp/.cpp, HarnessSupervisorOwner.hpp/.cpp,
research_acceleration/CMakeLists.txt and DiagnosticBuildIsolationTest.py.
New source/test files: DiagnosticSupervisorBroker.cpp,
DiagnosticHarnessSupervisorOwner.cpp and DiagnosticSupervisorOwnerTest.cpp.
Current gate documentation is also in scope. Geometry, original tests, C1A/C1H
artifacts, original setup files and the separate Gazebo/Cessna baseline are not.

PX4 HEAD: e880f1cd79e2453ef3dfaf68ee1c067b98ea9a53. Separate Gazebo HEAD:
bb0b9cf974acf4f1bcb5f5fcf80b88841562dea9; observed clean.

Next permitted gate after bounded owner acceptance: implement local
`research-c1b-offline/v1` export/checking, corruption tests, evidence-sink
independence and an honest variant-level coverage ledger. No geometry or new
human design decision is required for that offline work. Live diagnostic
schema/profile binding and actual state/clock/estimator/operator-loss provenance
remain separately reviewed inputs; host-only build guards must not be removed
as an incidental integration step.
