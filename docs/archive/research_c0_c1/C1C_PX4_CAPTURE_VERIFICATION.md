# C1C-S2 capture verification

2026-09-20. PASS: master verification and independent review accept the host
tests and two fresh actual PX4 captures. See C1C_PX4_CAPTURE_DESIGN.md for the predeclared
capture-only boundary. No live admission or Phase 2 completion is claimed.

## Implemented scope and review

New standalone producer helper and thin PX4 publisher, source-owned exact patch
for private copies of three Commander files, external receipt/summary topics,
early-subscription collector, bounded in-memory capture, post-stop export,
isolated runner and independent strict evidence/configuration tests.
Original Commander files are not edited by this experiment. No estimator,
MAVLink receiver, controller, allocator, output driver or research owner is
selected. No Gazebo or physical hardware is involved.

Initial captures the actual constructor pair; hooks observe every pinned arm/
raw-nav assignment; Sample detects unexplained pair changes. Counters distinguish
delivery from actual pair changes. Final summary separates last attempted,
successfully emitted and actual final state. Queues are finite (32 receipts,
2 summaries), with loss detected rather than assumed impossible. The independent
checker rejects gaps and a missing final tail even with a rewritten footer.
Bootstrap and source-hook completeness remain explicit reviewed source facts,
not something inferred solely from a normal trace with no state transitions.

The collector uses 4,096 bounded receipt slots and writes only after Commander
stop/final drain. An unavailable file cannot stall the source commit hook or
Commander stop. No admission owner consumes the stream; the source-change versus
admission race remains unsolved and cannot be promoted by this capture result.

Clock timestamps are source/observer readings of native PX4 hrt within one
process. They do not qualify a guidance cross-process clock map or establish
operational state freshness. The two-second capture window is a diagnostic
duration, not a command, estimator or control budget. No physical values added.

## Host verification

Root: `/var/tmp/c1c_capture_host_XD9uRqit`, native Ubuntu-24.04 WSL.
Installed GCC 13.3.0/CMake 3.28.3/Ninja 1.11.1/Python 3.12.3/AStyle 3.1.

| Configuration | Result | Log |
| --- | --- | --- |
| Debug | 3/3 pass | ctest-startup-final-debug.log |
| Release, run 1 | 3/3 pass | ctest-startup-final-release-1.log |
| Same Release, run 2 | 3/3 pass | ctest-startup-final-release-2.log |
| Debug, ASan/UBSan | 3/3 pass; no sanitizer failure reported | ctest-startup-final-sanitize.log |
| Fresh default-OFF | Configure/build succeeds, no capture-helper target | default-off/build.ninja |

Producer helper: 424 checks, zero failures. Evidence checker: 29 unittest
methods; configuration/hash binding: 15 methods, with additional subcases.
All of these are synthetic offline fixtures, not live state changes. The
synthetic arm/disarm and double-nav tests never issue actual vehicle commands.
Default-off target absence is not described as a zero-test qualification pass.

Independent review found and corrected exact compile-marker/source-path checks,
foreign launch-command acceptance, unchecked normalized-base hashes, queue-depth
contradictions and premature success on interrupted final audits. New semantic
corruption tests rehash changed evidence before rejection. They do not rely on
a stale hash to detect malformed chronology. Source/artifact hashes associate
these fresh builds/runs; they are not cryptographic build attestation.

The final campaigns also cover owned-view configuration paths with
spaces/apostrophes in the checkout and exact resolved external-source identity,
including rejection of a symlink retargeted to an unrelated source. Strict
compile-marker parsing remains unchanged. Scoped AStyle dry-run reports all six
capture C++ files unchanged; parent git diff --check is clean.

Host executable SHA-256, Debug/Release/sanitizer order:

```text
be82e5522ea26c7c4afd080836cce9cc5733f9beab777898da68c1a1d84e7ce5
97a93338ba6207e4577022c97f22997357c5ba029830d266da5721b361a698d9
b8d5f523f775f0746d2283f25ab894aab4b8c394b1cc5b77d7206307293578a0
```

Final host log SHA-256, four tested rows above in order:

```text
ceb7076a4dbf5b5541ba5c437cb2529da27d008dc13ff7106834130f857ab66b
735cc7f16b85fe6c621075ea671f93a1fd9c5fe7ff843f749c3779d10c91d86d
652d410dd1a2fe46104e2ecb5a62f6489b3a47cf0ee8042a671f5d37b3662f74
8e181781718d3ee592c37d9d9decf2db3123184f98ddfbcd1400b596dac69b06
```

## Actual build and capture record

Accepted fresh area: `/var/tmp/c1c_px4_vuhwhzex`. Configuration and all 475 build
steps completed successfully. Both new disarmed processes exited zero without
forced shutdown. Independent review replayed complete traces and final summaries,
not just checker success flags. Manifest status is `two_runs_checked`.

| Measurement | Run 1 | Run 2 |
| --- | --- | --- |
| Instance / PID | 50087 / 2730 | 16563 / 2764 |
| Receipts: Initial + Transition + Sample | 1 + 1 + 191 = 193 | 1 + 1 + 192 = 194 |
| Actual raw nav transition / retained arm | 2 to 4 / 1 (disarmed) | 2 to 4 / 1 (disarmed) |
| Observed receipt age min/max, us | 7 / 3282 | 10 / 14341 |
| Source span, us | 2008287 | 2015948 |
| Final summary age, us | 9579 | 3843 |

Attempted/emitted/received totals agree; transition epoch ends at two and
retained/emitted/final actual state agree. Final drains are complete and no
producer fault is reported. The six lifetime identifiers are distinct/nonzero.
These observed ages are NOT operational freshness limits or clock qualification.

All 505 compile commands parse strictly; only the intended 20 units carry the
capture marker. The configuration has Commander and the capture helper, ten
reviewed builtins and no drivers. Both startup copies are exact regular files.
The 8,886-entry source registry reports no changed inputs. Original Commander
files, separate Gazebo revision/status and stock Cessna hash remain unchanged.
The post-stop topic snapshot is advertised-only, not an all-run topic inventory.
Missing-sensor/power/LED, safety and capped arming-event warnings are retained;
this intentionally minimal configuration does not establish vehicle health.

SHA-256 bindings (complete source/overlay/run records are retained in manifest):

```text
manifest.json 8d845143f21f8566dfc684f283c4bb11c2a9b621db492b7a3fe0178487468ab3
source_hashes.json caeb457f58aa9ba5aed713129a20f8eb0c5cb86d1f9bbb5bc21142eb0c947e88
build/bin/px4 e90cdc9838e55aadced5291a238998d58afe7f4366faceb3204eb4dd69f19eae
configure.log a2b5c3a27d6895e9fc7120b9691ec5e6636bfc223f0ed499fe39d832702db672
build.log ff211eaa8a0ade21cce2d7d985c9df1713df0f7935e6f0c59ed526b4716e1d5c
checked-run-1.json 39dade3a7931f7058c156c181886b698bfba612d4ab9198cb0a8a7daf227e2e8
checked-run-2.json 1ba4d0cafa1a71e591ef8d5786240616ad14da19861deb127801acb07fdd09ac
run-1/capture.jsonl c3792c6ecf1973dbf1e9e679aaf02d3da7e3842591aadd72cede89c01260506d
run-2/capture.jsonl e8d968cf0e6028995312ea05b0f1c4b6bddddbd4777e613ed38591bde97ee851
```

### Retained earlier failures and corrections

First configured area `/var/tmp/c1c_px4_6imc6e3j` is retained as FAIL before
compilation/runtime. Exact private-copy/patch inspection and CMake configuration
succeeded; the strict compiler-command audit rejected an unmatched quotation.
CMake's compilation database left the apostrophe in an external-source-derived
object path unquoted. Read-only Ninja command inspection confirmed the actual
build command escaped it, but the runner correctly refused ambiguous audit
input. The scoped correction supplies external/config paths through the existing
owned source view, preserving resolved checkout identities and strict token
checks. It neither changes Commander behavior nor weakens the audit. No PX4
process ran in this failed area. Source hashes and separate Gazebo identity
remained stable at its final audit.

Second fresh area `/var/tmp/c1c_px4_k5jynh_c` configured and compiled all 475
build steps successfully. Independent review parsed all 505 compile-database
entries; exact capture opt-in appeared on the 20 intended target units, with
no forbidden profile flags. Run 1 exited 255 before Commander started because
PX4's run_startup_script concatenates the absolute script path into a shell
command without quoting it. The original workspace apostrophe broke that
command. No capture file was produced. The failed manifest/logs remain intact.

The runner now stages exact reviewed startup bytes as a regular file inside
each owned runtime and records its hash. The independent checker binds both
command and bytes, rejecting missing, changed/rehashed, foreign or symlinked
startup files. No stock PX4 launcher change is made.

An explicitly exploratory run 2 in that retained area used the unchanged,
hash-verified executable to diagnose staging before another fresh build. It
exited normally and retained 201 receipts, one actual disarmed raw-nav change
(2 to 4), and a complete final summary. This is NOT the required fresh-source
two-run acceptance campaign. Its initial checker result rejected missing
ordinary state topics in the post-stop snapshot.

Master and independent source review traced that rejection to an incorrect
checker expectation: platforms/common/uORB/Publication.cpp:57-65 unadvertises
queue-one publications on destruction, and DeviceNode::print_statistics omits
unadvertised topics. Commander's vehicle_status/actuator_armed publishers are
destroyed before this snapshot. The reviewed correction requires the queued
capture receipt/summary topics after stop; ordinary state topics remain allowed
but not mandatory then. Full topic allowlisting, instance/duplicate/queue and
marker checks, actual source Initial/continuity/disarmed/final-tail requirements
are unchanged. Regressions cover ordinary-topic disappearance and mandatory
capture-topic presence. No blocking pre-stop sink is added. This is a documented
lifecycle-check correction, not evidence of healthy estimation or a relaxation
of a timing/flight-performance requirement.

Earlier attempt `/var/tmp/c1c_px4_4g0udvft` was canceled during its initial
read-only WSL `git status --short` dirty-submodule traversal. Only its verified
owned git child was interrupted; no source view, build or PX4 process had been
created. The empty owned directory and tool traceback were retained. Runner now
saves an incomplete manifest before inventory and uses parent/gitlink status
with dirty-submodule traversal excluded; separate Gazebo identity/status and
full build-input hashes remain mandatory. This is not a runtime-test failure
or a skipped safety check.

## Reproduction and remaining gates

Standalone helper tests use the new subtree as CMake source with explicit
`RESEARCH_C1C_HOST_TESTS=ON`; build Debug/Release/sanitizer separately. Sanitizer
flags/runtime are the same as C1C_STATE_RECEIPT_VERIFICATION.md. Capture command,
only within the approved isolated SITL scope:

```sh
python3 -B Tools/research_sitl/px4_attachment/c1c_capture/run_capture.py --run
```

Without --run it builds only. The runner creates fresh native /var/tmp paths,
never reuses simulator mounts/caches, installs packages, or manages an existing
PX4 instance. Failures and incomplete evidence are retained, not overwritten.

Even after capture acceptance, positive live diagnostic validation still needs
serialized trusted-input integration, actual estimator validity/freshness,
qualified guidance clock lifetime, operator-loss detection and explicit
diagnostic schema/profile binding. Flight mode/control/normalized allocation,
custom plant validation and physical testing remain later independent gates.
