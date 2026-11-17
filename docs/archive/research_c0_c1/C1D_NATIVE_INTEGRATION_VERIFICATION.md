# C1D-N2 native closed-owner verification

2026-09-21. ACCEPTED by master verification and independent final gate review.
Independent decision retained as `independent-n2-final-review.md` in the native area.
Scope: actual native PX4 execution of the closed diagnostic adapter. No ingress,
operator/session authority, research mode, estimator, controller, allocation,
actuator output, Gazebo execution or hardware validation is established.

## Implemented and reviewed

The parent-owned `Tools/research_sitl/px4_attachment/c1d_closed/` integration
links seven real portable core sources into a minimal POSIX PX4 build. Exactly
three pinned Commander files are copied and zero-fuzz patched in a private
source view. Original Commander and the separate Gazebo repository are untouched.

One heap-resident owner is claimed once. Commander source hooks synchronously
observe Initial/Transition/Sample and close at normal run end. Destructor only
publishes retirement; archive memory is retained for the process lifetime.
Export obtains the actual Commander stop return and requires acquired normal
completion/retirement before reading owner data or touching a sink. Source hooks
have no parser, network, sink I/O or control-lock dependency.

Native profile definitions are private to participating targets. Actual compile
commands, board configuration and linked builtins are checked; HOST/offline,
hardware, lockstep and proxy profiles cannot enter this native configuration.

## Measurements and retained evidence

Native area: `/var/tmp/c1d_px4_58c0t5nm` (Ubuntu-24.04 WSL).
Host regression area: `/var/tmp/c1d_host_portability_UqqJjYfk`.
PX4 revision: `e880f1cd79e2453ef3dfaf68ee1c067b98ea9a53`.
Installed GCC 13.3, CMake 3.28.3, Ninja 1.11.1 and Python 3.12.3 were used;
no package installation or global configuration change was needed.

| Check | Result |
| --- | --- |
| Host Debug | 37/37 |
| Host Release, two runs | 37/37 each |
| Host ASan + UBSan | 37/37 |
| Host default-off | 17/17 |
| Owner assertions / host native-profile probes | 1,911 / 61 pass |
| Actual native compile-profile probes | 27/27 |
| Synthetic checker and run-wrapper methods | 17/17 |
| Actual-export corruption variants | 26/26 rejected |
| Full native PX4 compile/link | PASS |

All 201 staged host input files matched the checkout before and after testing.
Exact host commands and configuration flags are in `matrix-transcript.log`;
individual configure/build/ctest logs remain alongside it. Sanitizer execution
used the prior non-PIE configuration with leak/error detection enabled.

| Actual native run | 1 | 2 |
| --- | ---: | ---: |
| PX4 instance | 58403 | 32615 |
| Source records | 186 | 186 |
| Actual adapter reports | 189 | 189 |
| Raw navigation transitions | 1 | 1 |
| Initial-to-stop span, microseconds | 2,008,415 | 2,014,672 |
| Native pthread stack, bytes | 8,388,608 | 8,388,608 |
| Reported guard, bytes | 4,096 | 4,096 |
| Minimum conservative marker offset, bytes | 8,379,531 | 8,379,531 |
| Process exit / forced shutdown | 0 / false | 0 / false |

Both runs remain disarmed and research NotSelected. Actual closed polls reject
ClockMappingInvalid; normal OwnerStop returns AlreadyDisabled. Neither has
operator attachment, diagnostic selection, a session/ACK, a qualified guidance
clock, healthy estimator facts or eligible demand. Full result slots, barriers,
source counters, closure and separately copied summary agree with Python replay.
Run records say `captured_unscored`; separate `checked-run-*.json` files contain
semantic PASS, and the final outer manifest says `two_runs_checked`.

Repeated exports are byte-identical. Missing-parent, existing device (`/dev/full`)
and FIFO paths are rejected before writing; these are NOT low-level write-error
injections. Actual-export mutations cover invented arming/readiness/eligibility,
source gaps/reordering, closure/retirement/stack/footer corruption and malformed
JSON. Originals were not mutated. Synthetic fixtures alone were not treated as
proof of actual C++/Python agreement.

Compiler stack reports and object disassembly support a conservative 28,672-byte
synchronous owner-path bound, below the predeclared 32,768-byte ceiling. This
includes a deliberately overcounted sum of core header frames and the actual
23-byte marker-to-SP gap. Every hook requires at least 49,152 bytes remaining.
See `independent-stack-review.md` and `native_review.json` in the native area.
This is not whole-Commander, asynchronous-signal, libc/pthread/clock-internal,
exception/corruption, exporter or hardware worst-case stack qualification.

## Failures and corrections retained

Earlier preparation areas and failed/interrupted manifests were retained rather
than relabelled as passes. In the final area, a resumed configure exposed an
overbroad Commander-helper audit and PUBLIC native-profile leakage; the audit
now identifies the actual Commander target and definitions are PRIVATE.
The first full native build then failed strict warnings. Four portable C++ files
received explicit lossless promotions, exact finite profile/zero comparisons,
and local-variable renames. No tolerances, thresholds or arithmetic association
changed. Scoped AStyle, independent review and the complete host matrix pass.

`manifest.before-resume.json`, `manifest.before-profile-fix.json`,
`manifest.before-portability.json`, old input registries and failure logs preserve
that history. The successful binary is an explicitly recorded incremental
corrected build, NOT a fresh clean-area build. Source rebinding required exact
enumerated changes. Final input comparison found zero changes during runs.

Missing-sensor, safety-configuration, LED and arming-event-capacity warnings are
retained. They prohibit a vehicle-health claim but do not contradict this closed
diagnostic experiment. Post-stop advertised topics are only a snapshot, not
proof of every instant of publication.

## Evidence identities (SHA-256)

- Executable: `f062861fd31a80d6d14dc3dc14cfeb13e5dd33e6c8e5746c67b81ed4927d00bd`.
- Source registry: `9f3b105f73698ee8de8b9df6ea1d01c4b7b65ea1485608c65b32614dc94b369c`.
- Stack registry: `e845d5f20f8f66c2bd8ed232b26f6bab7940b3aa38f0944c0ce70c145e5fbf2c`.
- Runtime release: `0bdfc8a04cc6a4b4670236489db70964c4a7c21326a646e085c9beed6076d4f6`.
- Host source registry: `6199b0802e9eea730e43b246c5ebb53dd36b80febe105d2652eb4a878dec80cc`.
- Run 1 trace/repeat: `1d850bfa077a495548b415b7cb363fa0d84e5c26179a66c5957a483851b54f4a`.
- Run 2 trace/repeat: `496548b18aa295fb182dd70f1f459e12728690784fcd48ee73d3fd89ac5f5fbe`.
- Actual native probes: `d4c020c469472395f972cefe91b7a875fca371219f2059591140869a18aa7d4d`.
- Actual corruption results: `5034ba253bab880f68ea2d9a4bc321f5eaa58150b47716f72a9b964bd41e7637`.

Separate Gazebo stays clean at `bb0b9cf974acf4f1bcb5f5fcf80b88841562dea9`.
Stock Cessna SDF remains `78591ad19d7589fc202877c8830020f94925a2bd7fa1ea1a958a268591b8da7c`.
No commit, push, PR, install, flashing, hardware or powered test occurred.

## Next boundary

N2 acceptance permits the next technical design review for native diagnostic
ingress/owner integration, not automatic activation of ingress. That review must
define one serialized owner for operator selection/session/loss, authenticated
parser/status, clock-map provenance and estimator validity; retain native
identity/schema separation and no control outputs. Positive validation cannot
be obtained by inventing unavailable state. Existing user direction suffices
for this design work; geometry remains deferred.

Full C1 and roadmap Phase 2 remain OPEN. Their exit still requires the complete
producer-to-consumer path and approved/measured interface behavior; subsequent
plant, control, robustness, Pixhawk and human experimental gates remain separate.
