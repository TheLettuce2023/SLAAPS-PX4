# C1B-E: offline diagnostic evidence verification

2026-09-20. Master verification and independent review: PASS for the complete
finite OFFLINE C1B G/A/O/E contract, subject to the ledger's explicit seam and
fixture qualifications. This is not full live C1B, C1 or Phase 2 acceptance.
This document separates new execution from the historical C1B-O results.

## Implemented scope

The real single diagnostic owner/broker now has a C++ JSON exporter, a retained
artifact runner, an independent strict Python oracle and deliberate corruption
tests. The reference profile has 20 calls and 19 reports, including exactly one
validated-disarmed request, no eligible demand, a producer-time latch, authorized
clear, detach and unchanged ordinary Flight accessor output. It accepts only
`c1b-owner-reference/v1` in schema `research-c1b-offline/v1`.

The complete finite offline gate combines native guard/adapter/owner tests with
this representative JSON profile; it does not claim every native fixture was
serialized. See [execution ledger](C1B_OFFLINE_EXECUTION_LEDGER.md) for exact
matrix accounting and qualifications, and
[usage](../Tools/research_sitl/diagnostic/README.md) for executable commands.

Added tests independently mutate all 17 pending-confirmation binding dimensions
(seven identity/epoch fields, nine map fields and the separate physical clock
bit), assert exact rejection causes and capacity ordering, and prove the frozen
Flight status mapper leaves output unchanged on all new mismatch-code paths.
Compile probes establish heartbeat/status APIs are absent at this decoded seam;
they do not simulate runtime reception of those messages.

The sink test closes the actual owner before blocked delivery and proves a
missing sink cannot constitute complete campaign evidence. It is a controlled
thread-synchronization experiment, not an operational timing measurement.
Semantic corruption tests rehash modified campaigns before checking, preventing
a stale artifact hash from substituting for actual semantic rejection.

## Evidence classification and limits

Facts: diagnostic code remains opt-in and standalone-host-only. Ordinary Flight
encodings, guard/adapter implementations and default-off behavior are unchanged
in this E slice. There is no live receiver, mode selection or actuator publisher.

Measurements: standalone synthetic software execution only. Authentication,
physical state and clocks are fixture inputs. Source/executable hashes and the
recorded fresh builds associate these runs with the reviewed files; they do not
cryptographically attest arbitrary source-to-binary builds or system libraries.

Assumption: trusted callers serialize the owner. The local JSON profile is not
a live diagnostic wire protocol. Inference after final acceptance: finite offline
behavior is ready to inform live-attachment design, not positive live validation
or acceleration tracking. Phase 2 and full C1 remain open.

## Build and verification

Native WSL root: `/var/tmp/c1b_evidence_kwztBqUK`, Ubuntu-24.04, user kspry04.
Installed GCC 13.3.0, CMake 3.28.3, Ninja 1.11.1, Python 3.12.3, AStyle 3.1.
Fresh Debug/Release/sanitizer/default-off directories do not use simulator
mounts, caches or existing build artifacts. No packages were installed.

The first full Debug run passed 31/31. Final results:

| Configuration | Result | Log relative to build root |
| --- | --- | --- |
| Debug, diagnostic ON | 31/31 pass | ctest-debug-final.log |
| Release, diagnostic ON, run 1 | 31/31 pass | ctest-release-final-1.log |
| Same Release, run 2 | 31/31 pass | ctest-release-final-2.log |
| Debug, ASan/UBSan, diagnostic ON | 31/31 pass, no sanitizer failure reported | ctest-sanitize-final.log |
| Fresh Release, default diagnostic OFF | 17/17 pass | ctest-default-off.log |

All 33 compile-isolation probes and 11 evidence unittest methods pass. Required
corruption variants execute as individual subtests; eleven is not the total
number of corruptions. Final suite durations were 128.48, 122.22, 298.47 and
133.37 seconds respectively for the four opted-in runs, not timing requirements.
Native diagnostic counts: 12,163 guard checks (1,024 tuples, 88 request variants),
3,533 adapter checks and 1,209 owner checks. These include setup assertions,
not independent flight scenarios or exhaustive state-space coverage.

Configure standalone CMake as documented in the usage guide. The diagnostic
option is `RESEARCH_SITL_DIAGNOSTIC_TESTS=ON`; omit it for default-off. Sanitizer
CXX flags are `-fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie`,
linker flags `-fsanitize=address,undefined -no-pie`. Runtime uses
`ASAN_OPTIONS=detect_leaks=1:halt_on_error=1` and
`UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`.

## Retained failures and corrections

`ctest-adapter-initial.log` retains one failed assertion out of 3,518 in an
added R14 check. Its request was bound before a physical-state generation
change, so the correct first rejection is BindingMismatch, not the expected
AcknowledgementRequired. The correction preserves that original fixture with
its exact cause and adds a fresh-binding stale-ACK fixture to independently
reach AcknowledgementRequired. Existing Flight expected values were not relaxed.
The resulting adapter executes 3,533 passing checks.

An initial shell command combining test-name alternatives was rejected by shell
quoting before CTest executed; subsequent invocations used direct arguments.
This is not a test pass. Earlier O-gate failures remain in the separate owner
verification record and were not overwritten.

## Final acceptance record

Log SHA-256, in table order:

```text
78df4a79e4b36a18d527e6be2626acb01c2687899c5eb1a01fb2db6ecd1634a6
a4160f5af8c02a51ef237178981e397d11db1c165f33d6a4db2d9750a544832d
1835ed872e70487a1383a56186af58d6635f69de6a0ca48e353c4fdbb764505e
88b2dd1a5f90a40df367845bc9d7fb1c7d06519a0220b58f325b3a44f373b3a9
4bd9ae2b47139b6210e4e2d1399d943d31af473fc2e5544ac30f632c488562b4
```

Exporter executable SHA-256 for Debug, Release and sanitizer:

```text
cbe2a14258122c11a66eaf20ac44e6217294379f8eb94833c84d9c20e4236c7d
37b217ccd4dc0035706bef729579e674d4d1091c8a4c97f95f19937f69b90053
34e53ec03fc2b38558f6f89efa92e84ceb0be3812901fe7dd0243a77c9b68b32
```

Each package below contains complete source SHA-256 registry, staged executable,
campaign, manifest and independent checked summary. The runner checked current
source/binary bytes before and after execution and verified CMake source/cache
association. The manifest hash binds that full registry, not only changed files.

| Final package relative to root | manifest.json SHA-256 |
| --- | --- |
| debug/c1b-evidence-mdb9lafv | 461f8aab5129a9708036e4ead4a7c6f9cd4f9f5051ad2856e1dad3804b3ebc9f |
| release/c1b-evidence-noy3jasa | 0f2d44c6fa59930d605374fb59c63600a7c146f391d306acfe60da6b7eb05903 |
| release/c1b-evidence-0wvczdyh | 0f2d44c6fa59930d605374fb59c63600a7c146f391d306acfe60da6b7eb05903 |
| sanitize/c1b-evidence-5t6jango | f02271728c9fe42eebf4aeff6a73f76ff77ef28bbf1d3d791adba0d280af011d |

Independent source/oracle review found no remaining blocker for this finite
offline profile. Scoped AStyle dry-run left all five changed/new C++ files
unchanged; tracked git diff whitespace check passed. The 717-file E entry audit
found only six intentional existing changes before status updates: two contract/
matrix documents, CMake, adapter tests, isolation probes and owner tests. No
entry file was missing. Nine new E source/document files were added. Subsequent
status/timeline updates and the separate C1C slice are explicitly outside that
E source freeze. All original Flight tests, owner/broker/guard/adapter
implementation sources, geometry, C1A/C1H, and original setup files were retained.

PX4 HEAD remains e880f1cd79e2453ef3dfaf68ee1c067b98ea9a53; separate Gazebo remains
clean at bb0b9cf974acf4f1bcb5f5fcf80b88841562dea9. No simulator/board build, live
connection, commit, push, install, flash or physical test occurred in E.

Next released work is source-traced live-input qualification design and its
bounded standalone checks under the existing C1 authorization. See
C1C_STATE_RECEIPT_CONTRACT.md. Actual producer-hook completeness, estimator/
clock/operator-loss provenance, domain-bound live schema and output-disabled
integration remain required; the acceleration-to-controller-to-allocator path
has not been implemented or verified by admission/evidence tests.
