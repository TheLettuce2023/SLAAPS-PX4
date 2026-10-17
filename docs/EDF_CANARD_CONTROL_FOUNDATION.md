# EDF and four-canard control foundation

Review date: 2026-10-02. Current maintainer: Codex.

## Latest increment: H10 snapshot ordering and scoped verification

[H10 fix and exact evidence](MANUAL_BENCH_CONTROL_H10_FIX.md) closes the reproduced
H9 clock-before-copy failure. H8 captures selected input before reading time;
the stock manual module captures all three candidates before its live clock.
Fixed-capacity storage, single-owner control, strict timestamp/source validation,
lease/neutral latching and physical/positive-admission gates remain intact.
Strict portable suites pass 2,431 checks each; stock compatibility 28 and evidence
checker 19 tests pass. Three consecutive capture-only native runs pass 19/19 cases
and 3,541 checks each, including deterministic selector interleaving and genuine
future-input rejection. Fresh SITL and Pixhawk 6X builds pass with actual H8
linkage; ARM text/data/BSS are 1,948,768/8,044/105,596 bytes. Compilation, scoped
runtime verification and whole-image static resource evidence remain distinct;
physical signals and runtime heap/stack/timing margins are not qualified.
Evidence `/var/tmp/research-bench-h10`, manifest SHA-256
`b8176eaf9f23098b9800e8ab4c33bc8b0e41adf0015aa222310672631acb9c43`.
Exact gate advanced is H10 defect closure and scoped capture-only manual bench
integration verification. No flashing, physical output or flight readiness.

## Latest testing: H9 active-path acceptance blocked

[H9 capture-only integration evidence](MANUAL_BENCH_CONTROL_H9_TESTS.md) exercises
actual PX4 manual selection, the H8 owner, output consumer/mixer and local MAVLink
ingress without physical drivers. Final result is **11/17 cases passing, six
failing**, not software acceptance. Debugger confirms clock-before-copy ordering
can supply a selected publication newer than H8's evaluation time and latch
`InputRejected/InvalidTime`. Production code is unchanged; a time/snapshot fix,
deterministic regression and full rerun are next. No gate beyond H8 advances.
Strict portable Debug/Release/ASan-UBSan remain 2,413 checks each; checker 19 tests
and fresh linked SITL/Pixhawk 6X builds pass. Native PX4 was not sanitizer-qualified.
Final ARM linked text/data/BSS are 1,948,680/8,044/105,596 bytes; static whole-image
resources are distinct from compilation, runtime safety or heap/stack evidence.
Evidence `/var/tmp/research-bench-h9`, manifest SHA-256
`58d787ec77bd36c775ac5452caa37a4624345776727a26907fe525e2e1e8511f`.
Physical outputs, installed calibration, independent stop, wind qualification,
positive admission and flight readiness remain unaccepted.

## Latest increment: H8 explicit bench module/output integration

[H8 contract and exact verification](MANUAL_BENCH_CONTROL_H8.md) supersedes the
H7 no-registration/no-publication restriction following explicit user approval.
An opt-in, manually started module owns H7 control and publishes a dedicated
four-servo lease to MAIN1-4-only mixer consumers. Expiry/revocation latches,
stock safety gating, bounded profile parsing and explicit deadzone/PWM conversion
are implemented. No default profile, automatic start, EDF, arming action or
physical operation is added. Portable strict suites pass 2,413 checks each.
Integrated firmware linkage is distinguished from historical excluded H7 objects;
physical output acceptance and flight remain open.
Evidence `/var/tmp/research-bench-h8`, manifest SHA-256
`0b934dde9c339d08e5fc295ec4087dd41fcf99b80224db785a8fac6695f6ac0f`.
Integrated board ELF text/data/BSS 1,948,680/8,044/105,596; linked flash 1,956,724
bytes. Required module/owner/lease symbols are present. Disabled-only native SITL
lifecycle smoke passes; active-path hardware acceptance and runtime resources
remain unqualified. Exact artifact hashes and rejected stale-NuttX-archive
diagnosis are retained in H8; old excluded-library sizes are not H8's resource cost.

Acceptance-preflight increment: [ordered physical acceptance record](MANUAL_BENCH_PHYSICAL_ACCEPTANCE.md)
and a read-only, hash-checking evidence audit are implemented; 19 Python unittest
cases pass in Ubuntu-24.04 WSL. This adds no firmware/runtime authority. Existing
H7 compiled-but-excluded and default linked-resource evidence is unchanged.

H7 follow-up: [provisional bench assumptions and independent stop contract](MANUAL_BENCH_CONTROL_H7.md#provisional-bench-assumptions-2026-10-01-follow-up)
records the documented 5 A continuous BEC, its 5.2 V default discrepancy,
ideal 5 V servo slew/angle model and assumed telemetry identities/deadzone.
No firmware changed, no builds were rerun and no physical gate advanced.
Existing linked default firmware figures still exclude the research library.

## Latest increment: H7 servo-only manual bench boundary

The user accepted the proposed QGC/SiK bench scheme and reconfirmed NACA 0012.
[H7 contract, decisions and verification](MANUAL_BENCH_CONTROL_H7.md) is the
authoritative record for this increment; historical H1-H6 evidence below remains.
H7 implements one fixed-capacity bench owner and an inert PX4 selected-manual
scheduling adapter. It applies coupled roll/pitch/yaw, uniform saturation, H1
slew limiting and caller-calibrated PWM candidates, ignores throttle and inhibits
EDF. Explicit centered enable, fault latching and separate neutral intent are
tested. H6's original four-axis contract remains the default; H7 explicitly
selects three axes. No physical effectiveness coefficients, source identity,
servo calibration, slew rate or scheduling-gap budget is invented.

Scoped AStyle and strict Debug/Release/ASan-UBSan pass **2,180 checks each**:
H1 843, H2 182, H3 233, H4 68, H5 73, H6 132, H7 649. Fresh default SITL and
Pixhawk 6X builds and separate POSIX/NuttX library compilation pass using the
installed WSL ARM GCC 13.2.1 and a new native source view. Evidence is
`/var/tmp/research-bench-h7`; manifest SHA-256
`ec260f2d798ef98eccbd9bb3141f1993ac315f8f4a5ca24c667a96ed0a84bc03`.
The H7 record gives exact source/test/artifact bindings and notes the corrected
preliminary test/compiler issues. 990 unrelated checkpoint files are unchanged.

The ten unlinked research ARM objects total 7,501 text and zero data/BSS bytes;
this does not establish linked cost, stack usage or runtime RAM. The default
linked board reports 1,939,400 text, 8,012 data and 105,596 BSS bytes, flash
1,947,412 bytes. Both default ELFs have zero research-actuator symbols, so the
linked figures exclude H1-H7. No module/startup, output publisher, arming or
operational profile is installed. Installed servo calibration, signs/scales,
source/link/timing measurements and qualified neutral output behavior block
enabled bench testing. C1E-N1 remains negative-only and every positive admission
and physical gate remains intact.

## Release decision H1

The user requested the next gate and the start of EDF/four-servo flight-controller
code. This releases a narrow, inactive command-domain math primitive after
master review, not a flight law or output-producing PX4 module. The existing
plant-supported Gate D and output-integration Gate E remain closed.

Independent preliminary review rejected implementing physical-wrench scaling
now: an arbitrary set of six scales must not be mistaken for a coupled feasible
force/moment envelope. Physical-to-normalized allocation semantics remain a
required later design deliverable, not an implicit identity conversion.

## Intended control scheme and ownership

The intended chain remains acceleration-only companion input, PX4 request and
state validation, a future feasible acceleration/attitude objective resolver,
attitude/rate stabilization, a coupled EDF/canard allocator, then actuator
outputs. PX4 owns every control, allocation, supervision and output stage.

One fixed-axis EDF supplies non-reversing propulsion. Four independent canard
servos supply aerodynamic forces and moments only where local flow supports
them. The X arrangement does not determine servo signs, mixer coefficients,
roll authority or a feasible launch envelope. Those require geometry and tests.
PID/rate-control library reuse is a candidate, not a selected or tuned law.
The acceleration vector cannot generally be mapped directly to five independent
actuator values without resolving attitude, coupling and limited authority.

No physical wrench, attitude, torque or thrust target is produced by H1.
It operates only on already proposed normalized actuator commands. It is not
connected to the request guard or allocator and cannot make a request eligible.

## H1 command-domain contract

- Five fixed logical channels: index 0 EDF, indices 1 through 4 canard servos.
  These are not PWM pins or geometry positions. The future output mapping must
  define each physical device, servo sign, trim, linkage and calibration once.
- EDF commands use the mathematical domain [0, 1]; servos use [-1, 1]. A servo
  zero is a normalized coordinate, not evidence of neutral deflection or safety.
  EDF zero is not an approved in-flight loss response. Values are not newtons,
  newton-metres, radians, PWM widths or thrust percentages established by data.
- Every channel requires explicit minimum, maximum, rising slew rate and
  falling slew rate. Bounds are finite, ordered and inside its domain. Equal
  bounds are permitted. Rates are finite and nonnegative; a zero directional
  rate mathematically holds against change in that direction and does not
  diagnose a failed actuator.
- Maximum timestep is mandatory, finite and strictly positive. Actual dt must
  be finite, strictly positive and no greater than this maximum. No large-dt
  clipping, elapsed-time substitution or operational timing default exists.
- Previous commands must be finite and within the current configured bounds.
  Changed bounds that exclude the previous command cause failure, not an
  implicit recovery jump. Previous means the command last issued by the future
  sole owner; it is not measured servo position or achieved EDF thrust.
- Desired commands must be finite but may exceed configured bounds. First
  constrain each desired value to amplitude bounds, then to the reachable
  interval from previous command plus/minus directional rate times dt, intersected
  with those bounds. Use wide intermediates and conservative representable
  rounding so the returned float stays inside the reachable interval.
- Separate per-channel bit masks report amplitude and slew limiting. A slew
  bit compares the amplitude-constrained target with the reachable interval;
  quantization alone is not labelled physical saturation. Bits 0..4 follow
  logical channel order; other bits are zero.
- The API returns an explicit status. Configuration/input/dt/previous-command
  or arithmetic failure leaves the entire output/report object unchanged.
  A caller must check status and must not interpret that unchanged object as
  a newly valid output. No fallback command is manufactured.
- All configuration is supplied per call. There is no hidden state, allocation,
  integrator, heap allocation, clock, I/O, publisher, arm/mode logic or truth
  input. A caller can test aliases, but the implementation must validate and
  calculate into a local result before committing it once.

H1 does not establish joint attainable wrench, actuator tracking, rate dynamics,
latency, anti-windup, fault handling or safe fallback. A future allocator must
use actuator constraints consistently and evaluate residual wrench after all
limiting; adding this helper as an unobserved downstream limiter would be wrong.
The PX4 allocator already has limiting infrastructure. H1 is an isolated testable
contract/reference, not a mandate to duplicate or bypass that infrastructure.

## Assigned paths and predeclared tests

Implementation: `src/lib/research_actuator_command/CommandLimiter.hpp`,
`CommandLimiter.cpp` and standalone-only `CMakeLists.txt`. Independent master
tests: `CommandLimiterTest.cpp`. No edits to S1, root build/startup, modules,
airframes, messages, Gazebo resources or output drivers are released.

Acceptance requires strict-warning C++17 Debug and Release host builds, repeated
Release tests, and an available sanitizer run. Tests must cover all five channels,
asymmetric rates, both bound directions, equal bounds, zero directional rates,
dt boundaries, invalid configuration/types representable in C++, NaN/infinity,
invalid previous values, output preservation, float extrema/rounding, masks,
and alias safety. All numerical fixtures are explicitly non-operational.
Run existing S1 and C0a/E1 regression tests. Use a newly allocated native WSL
host-build directory, never the simulator build/cache mounts. No installation,
firmware build, simulator run, commit, flash or physical test is released.

## Evidence and next gate

H1 implementation is accepted after independent review and master verification:
843 non-operational checks pass Debug, Release (three repeats) and ASan/UBSan
host builds. Existing S1 (231 guard and 165 frame checks), C0a (8 methods) and
E1 (17 methods) regressions pass. Detailed source hashes, commands, failures and
scope are in `PHASE_GATE_STATUS.md`. These are software checks, not flights.

Master review reproduced and corrected an outward-rounded double endpoint that
could violate a float slew boundary. The implementation now uses an error-free
sum residual to round endpoints inward before float conversion; durable upper,
lower and opposite-sign subnormal regressions cover the failure. The verified
numeric contract assumes IEEE binary32/binary64, round-to-nearest and gradual
underflow without fast-math/reassociation. A future firmware port must verify
those conditions and execution cost; no board compilation/runtime is claimed.

The concurrent C0b adapter design is separately recorded in
`ACCELERATION_ADAPTER_C0B_DESIGN.md`. Neither artifact completes the acceleration
path. Masks describe command-domain limiting only; a caller can compute
desired-minus-output command residual, but physical wrench residual needs the
future validated effectiveness model and is not produced here.

The next control-law gate still needs measured mass/CG/inertia, canard geometry
and signs, local-flow/effectiveness data, installed EDF/servo response and limits,
an approved attainable envelope, attitude-resolution policy, numerical tracking
requirements, normalization/allocation semantics and flight-loss behavior.
The user's approximate mass/thrust remain estimates, not configuration constants.
No measurement is needed to test the mathematical H1 contract itself.

## H2 normalized-effectiveness evaluation foundation

Implementation date: 2026-09-28. This increment adds
`EffectivenessModel.hpp/.cpp` and `EffectivenessModelTest.cpp` beside H1. It also
makes `research_actuator_command` available as an excluded-by-default PX4
library while retaining its standalone host build. No module, parameter,
airframe, startup entry, topic, allocator selection or output publisher was
added.

H2 fixes the logical actuator order as EDF followed by four independent canards
and uses PX4's six normalized control axes in roll, pitch, yaw, thrust-X,
thrust-Y, thrust-Z order. The caller must supply the full 6-by-5 effectiveness
matrix, trim and command bounds. The evaluator rejects non-finite coefficients,
invalid EDF/canard command domains, trim outside bounds, all-zero actuator
columns, non-finite demand, out-of-bounds commands and unrepresentable achieved
or residual control. Success reports achieved normalized control, demand
residual, exact lower/upper saturation masks and the structurally configured
axis mask. Every failure preserves the caller's report.

The evaluator deliberately does not solve allocation or decide coefficients.
Matrix values map normalized command displacement from trim to normalized PX4
control axes; they are not newtons, newton-metres or an approved physical
normalization. A structurally configured row does not establish independent
authority, rank, conditioning or feasibility. The future PX4 control allocator
remains the allocation owner, and future plant-supported code must evaluate its
residual after final limiting.

Strict-warning C++17 Debug and Release builds pass the existing 843 limiter
checks and 182 new effectiveness checks. ASan/UBSan passes both executables.
An isolated `px4_sitl_default` CMake graph compiles the
`research_actuator_command` target under PX4's warning policy. The later H4
portability work also compiles H1 through H4 for `px4_fmu-v6x_default` with
ARM GCC 13.2.1. The retained earlier H2 build root is
`/var/tmp/research-effectiveness-h2`.

H2 is a tested software foundation, not a flight-controller release.
It still needs independent review and later integration with a single research
control owner. Plant-supported effectiveness, scheduling, rank/conditioning,
the feasible acceleration-to-wrench policy, stabilization gains, anti-windup,
loss behavior and actuator output remain open.

## H3 prioritized authority analysis foundation

Implementation date: 2026-09-28. This increment adds
`AuthorityAnalysis.hpp/.cpp` and `AuthorityAnalysisTest.cpp` to the same
standalone/PX4 library. It consumes H2's caller-supplied normalized 6-by-5
matrix and produces a structural diagnostic only. It does not allocate a
command, change the matrix, publish a topic or control an actuator.

The caller must provide a permutation of all six axes, a finite positive
minimum row norm and a finite independence threshold in `(0, 1]`. The analyzer
processes rows in that priority order. It classifies an axis as weak when its
Euclidean row norm is below the caller threshold. For every remaining row, a
twice-reorthogonalized modified Gram-Schmidt pass calculates the squared
residual fraction after removing higher-priority selected directions. Values
below the caller threshold are dependent; the others are selected. The report
contains row norms and independence values in fixed axis order, disjoint
selected/weak/dependent masks and rank. At most five axes can be selected from
five actuator dimensions.

Scaled norm calculation and double intermediates keep finite binary32 matrix
extremes finite. The analyzer rejects non-finite coefficients, invalid
thresholds, duplicate/missing/out-of-range priority entries and non-finite
arithmetic. Every failure preserves the caller's report. It intentionally
accepts zero actuator columns so a caller can diagnose an unavailable-actuator
matrix; H2's complete nominal-configuration checks remain separate.

The reported independence is priority-dependent and scale-invariant for rows
that remain above the weak-axis threshold. It is not a singular-value condition
number, a physical force/moment measure or proof of controllability over a
flight envelope. H3 defines no default priority or threshold, and the numeric
test policies are explicitly non-operational fixtures. Operational values still
need plant evidence, loss-policy review and the future single control owner.

Strict-warning C++17 Debug and Release builds pass 843 H1 limiter checks, 182
H2 effectiveness checks and 233 H3 authority checks. ASan/UBSan passes all
three executables. The `px4_sitl_default` graph compiles the combined library
under PX4's stricter warning policy. Host evidence is retained under
`/var/tmp/research-authority-h3`; the earlier PX4 target is in
`/var/tmp/research-effectiveness-h2/px4-sitl`. The later H4 portability work
also verifies H3 with ARM GCC 13.2.1 for Pixhawk 6X. H3 has primary-agent
verification; independent review remains open.

## H4 output-disabled control pipeline foundation

Implementation date: 2026-09-28. This increment adds
`ControlPipeline.hpp/.cpp` and `ControlPipelineTest.cpp`. The stateful,
allocation-free object composes H1 command limiting, H2 normalized
effectiveness evaluation and H3 structural authority analysis without adding a
PX4 module, clock, topic, controller, allocator or output. It is noncopyable and
nonmovable so one future owner can hold the previous normalized command state.

The caller supplies the complete limiter configuration, 6-by-5 matrix, trim,
authority policy and required-axis mask. Configuration validates the nominal H2
matrix and command domains, then H1 limits and trim, then H3 policy and required
structural authority. A failed initial or replacement configuration preserves
the existing state. A successful configuration caches the H3 report and starts
the prior-command state at caller trim.

Evaluation limits a desired normalized actuator command from the owned prior
command, evaluates achieved normalized control and demand residual, and returns
the cached structural-authority report. State advances only after the whole
evaluation succeeds. Every failure preserves state and the caller's report.
The returned candidate remains diagnostic library data; no code publishes or
applies it. H4 does not create the desired command, solve allocation, decide
coefficients, supply gains or define command-loss behavior.

NuttX lacks the standard C++ containers used by the first host-only drafts, so
H1 through H4 now use the local fixed-capacity `FixedArray` type and avoid
unsupported standard-library headers. The WSL toolchain now contains
`gcc-arm-none-eabi`, newlib and ARM libstdc++ (ARM GCC 13.2.1). A fresh
WSL-native source view is required because the Windows checkout path contains
spaces and its script line endings are unsuitable for a direct NuttX build.

Strict-warning Debug, Release and ASan/UBSan builds pass 843 H1, 182 H2, 233 H3
and 68 H4 checks, 1,326 total per configuration. Fresh `px4_sitl_default` and
`px4_fmu-v6x_default` configurations compile the H1-H4 library under PX4's
warning policy. The complete Pixhawk 6X firmware build also succeeds and emits
ELF, BIN and PX4 artifacts. Its reported 1,947,412-byte flash use (99.05% of
1,920 KiB) excludes this `EXCLUDE_FROM_ALL` library, so it is baseline board
resource evidence rather than H4 linked-cost evidence. The four ARM objects
contain 3,041 bytes of text and no data/BSS before link-time elimination.

Retain `/var/tmp/research-authority-h3/px4-v6x-local` and
`/var/tmp/research-authority-h3/px4-v6x-local-build` for the clean board source
view and build, and `/var/tmp/research-authority-h3/px4-sitl-local` for the
fresh SITL target. The next control increment is a default-off PX4 runtime owner
that consumes validated inputs and schedules this seam without output. Physical
coefficients, operational priority/threshold values, allocation, stabilization,
loss policy and actuator publication remain gated.

## H5 default-off runtime owner and PX4 scheduling boundary

Implementation date: 2026-09-28. H5 adds the allocation-free,
noncopyable/nonmovable `ControlRuntimeOwner` and the PX4-only
`Px4ControlRuntime` scheduling adapter. Construction is inert. The caller must
supply the complete H4 configuration and a nonzero scheduling interval no
greater than the caller-supplied H1 maximum timestep, then explicitly configure,
start and prime the owner. There is no rate, period or timing default.

`ControlRuntimeOwner` is the sole owner of `ControlPipeline`. Its lifecycle is
`Unconfigured -> Ready -> Running`; `stop()` revokes runtime configuration so a
new successful configuration is required before restart. The first scheduling
timestamp primes the owner without evaluating H4. Each later cycle derives its
timestep from the last completely successful cycle. Non-increasing time, an
unavailable/invalid input, excessive elapsed time or any H4 failure leaves the
timestamp, H4 previous-command state and caller report unchanged. H5 neither
clips elapsed time nor selects recovery or fallback behavior.

`Px4ControlRuntime` is an inert `px4::ScheduledWorkItem` library type. A future
caller must inject its work queue, interval, complete H4 configuration and an
input source. There is no `ModuleBase` entry point, Kconfig or board selection,
airframe, parameter, startup registration, uORB subscription/publication,
arming action, allocator connection or actuator/output type. The report built
inside `Run()` is local and discarded. No class is instantiated by default, and
the whole library remains under `EXCLUDE_FROM_ALL`. The future enclosing owner
must serialize configure/start/stop against `Run()`; H5 does not claim that
`ScheduleClear()` proves an already-running callback has finished or add a
cross-thread command path.

Strict-warning C++17 Debug, Release and ASan/UBSan configurations pass the
unchanged 843 H1, 182 H2, 233 H3 and 68 H4 checks plus 73 H5 checks: 1,399
checks with zero failures per configuration. Scoped PX4 AStyle passes on all
five new C++ files. Retain the host builds under
`/var/tmp/research-runtime-h5/{debug,release,sanitize}`.

A fresh WSL-native source view at
`/var/tmp/research-runtime-h5/px4-source` produced a complete
`px4_sitl_default` build at `/var/tmp/research-runtime-h5/px4-sitl-build` and a
complete `px4_fmu-v6x_default` build at
`/var/tmp/research-runtime-h5/px4-v6x-build`. ARM GCC 13.2.1 compiled all six
library objects, including `ControlRuntimeOwner.cpp` and
`Px4ControlRuntime.cpp`, under the NuttX/PX4 warning policy. The six objects
contain 3,745 bytes of text and zero data/BSS: 692 authority, 1,048 limiter,
533 pipeline, 440 runtime owner, 768 effectiveness and 264 PX4 scheduler bytes.

That 3,745-byte figure is **object compilation evidence**, not linked firmware
cost. Both final SITL and Pixhawk ELFs contain zero matching H1-H5/runtime
symbols because the library remains excluded. The complete Pixhawk linker report
is the unchanged baseline 1,947,412-byte flash image (99.05% of 1,920 KiB);
it is **linked baseline firmware resource evidence that excludes H1-H5**. The
first full board pass exposed an existing generated-header ordering issue when
stock UAVCAN `remoteid.cpp` ran before `mavlink/common/mavlink.h` existed.
Building PX4's existing `mavlink_c_generate` and
`mavlink_c_generate_uavionix` prerequisites, without source changes, allowed
the unchanged graph to finish; the final no-op rebuild passes.

Exact H5 source SHA-256 values are:

- `ControlRuntimeOwner.hpp`:
  `8263a52ebfc42cd5ea86a674080c157edeffb1ea5d3aaf027b0b689caa614dea`
- `ControlRuntimeOwner.cpp`:
  `822e14bd4a6c968688c1e3d6858c8b62ac79f77da581f418277f795ca85795a3`
- `ControlRuntimeOwnerTest.cpp`:
  `0db03e82705ba21e70d1f2f41ae0daf9d0677c9c1f124b30df69ccef3e83aeab`
- `Px4ControlRuntime.hpp`:
  `89b44ce3077b29d6518b433fa68c2274a2a5788f3b39946271ef3c0bb37129f0`
- `Px4ControlRuntime.cpp`:
  `5471a2f2b20ebbe26a5db726107a4dc6a98c2ce00ee32ce80cb6d7d049c1f488`
- updated library `CMakeLists.txt`:
  `c770d37582bff893997ba07431bd4858b1af1cd0421531b2496afd48d6bd1346`

Retained artifact SHA-256 values are:

- SITL `bin/px4` (58,591,648 bytes):
  `910e6733178fab03578943f4177ee4430ca56d208ded9f0e26175f3af2850869`
- Pixhawk ELF (49,297,664 bytes):
  `863d3675fa77138a1917835df41d695b489e747b42ebd13d1b69dce640cbb35e`
- Pixhawk BIN (1,947,412 bytes):
  `7bc4784a8cb3bdacc166c2a7ba9cf0e29c0db775c1211740bca517971b82bf08`
- Pixhawk PX4 package (1,853,994 bytes):
  `2a3137b0ca32a6c04938b872ffa9378be1d71af3371e4d8b35316a7f440454d8`
- excluded ARM research archive (172,034 bytes):
  `ad8560bf64904e3750804b006ad3243a2e8cba3032b805f67bd7837289413a88`

H5 advances only the default-off ownership, lifecycle and PX4 work-queue seam.
It does not connect C1E-N1, create positive admission, qualify the scheduler or
HRT provider, establish an operational deadline, select a matrix/policy/control
law/allocator, prove concurrent stop coordination, or produce output. The next
control gate is a reviewed,
output-disabled validated-input adapter/status boundary after the outstanding
positive-admission clock, estimator, operator and transport gates. Physical
control and all actuator publication remain blocked on the existing plant and
Gate-D/Gate-E evidence.

## H6 default-off PX4 manual-control and actuator-command boundary

Implementation date: 2026-09-30. H6 adds an optional, output-disabled manual
input source for H5. It does not add a new manual-input protocol or compete with
PX4 source selection. A GCS joystick continues through stock
`MAVLINK_MSG_ID_MANUAL_CONTROL`, `MavlinkReceiver`, multi-instance
`manual_control_input`, and the stock `manual_control` selector before H6 reads
the selected `manual_control_setpoint`. RC uses that same selected topic. Thus
PX4 remains the sole owner of RC/GCS selection and its existing
`COM_RC_IN_MODE` policy; H6 neither changes that parameter nor supplies a
priority or fallback.

The portable, allocation-free `ManualControlInput` requires a complete caller
configuration: `RcOnly`, `MavlinkOnly`, or `RcOrMavlink`; a nonzero maximum
sample age; the exact MAVLink instance/system/component identity when MAVLink
is allowed; a full four-axis-to-six-axis normalized-demand affine map; and a
full four-axis-to-EDF-plus-four-canard desired-command affine map. No mapping,
gain, trim, age, identity, source policy or actuator behavior is defaulted.
Every roll, pitch, yaw and throttle axis must be structurally used by at least
one caller-supplied map. These maps are software-boundary values only: they do
not establish physical effectiveness, coupled feasibility or an allocator.

The boundary accepts finite PX4-normalized axes in `[-1, 1]`, validates sample
and publication chronology against H5's exact scheduling timestamp, checks
caller freshness and source identity, and distinguishes a new raw sample from
an identical still-fresh held sample. Regressed samples, reused raw timestamps
with changed source/identity/axes/buttons, non-finite or out-of-range axes, and
unrepresentable affine arithmetic are rejected. Any failure preserves both
caller outputs and the last accepted raw-sample state. Source changes under
`RcOrMavlink` are accepted only after the stock PX4 selector has made them;
H6 invents no independent source priority.

`Px4ManualControlInputSource` translates only the selected uORB message into
the portable boundary and implements H5's injected `RuntimeInputSource`.
`RuntimeInputSource::read()` now receives the H5 scheduling timestamp so sample
age and the pipeline cycle share one clock observation. The wrapper publishes
nothing. It has no `ModuleBase`, command, Kconfig, board selection, parameter,
airframe, startup entry, arming action, actuator-function mapping or output
driver, and no H5/H6 object is instantiated. The entire library remains
`EXCLUDE_FROM_ALL`. This is an available future manual-control option, not an
enabled PX4 or GCS mode and not a live QGroundControl integration test.

Strict-warning C++17 Debug, Release and ASan/UBSan configurations use
`-Wall -Wextra -Werror -Wconversion -Wsign-conversion -Wshadow -Wpedantic` and
pass 843 H1, 182 H2, 233 H3, 68 H4, 73 H5 and 132 H6 checks: 1,531 checks with
zero failures per configuration. Release was repeated until three consecutive
passes; the sanitizer log contains no ASan, UBSan or LeakSanitizer diagnostic.
Scoped PX4 AStyle reports all seven changed C/C++ files unchanged. Retain host
evidence at `/var/tmp/research-manual-h6/{debug,release,sanitize}`.

A fresh WSL-native source view at
`/var/tmp/research-manual-h6/px4-source` compiles the excluded library for both
SITL and `px4_fmu-v6x_default`. The Pixhawk build uses Ubuntu-24.04 WSL
`arm-none-eabi-gcc (15:13.2.rel1-2) 13.2.1 20231009` and compiles all eight
research objects, including
`ManualControlInput.cpp`, `Px4ManualControlInput.cpp`, and the timestamp-updated
PX4 runtime adapter. Their pre-link sizes are 692 authority, 1,048 limiter, 533
pipeline, 440 runtime owner, 768 effectiveness, 1,080 manual boundary, 268 PX4
scheduler and 368 PX4 manual adapter bytes: 5,197 text and zero data/BSS total.
This is **object compilation evidence**, not linked firmware resource cost.

The complete default SITL and Pixhawk 6X builds also pass. Both final ELFs have
zero `research_actuator_command::` symbols and their full-build logs contain no
H1-H6 compile line because the library is excluded. The Pixhawk link reports a
1,947,428-byte BIN and 1,947,428 bytes of flash use (99.05% of 1,920 KiB).
This is **linked baseline firmware resource evidence that excludes H1-H6**, not
the resource cost of an enabled manual-control path. The first parallel board
pass exposed the same stock generated-header dependency order seen at H5:
`uavcan/remoteid.cpp` ran before `mavlink/common/mavlink.h` existed. Explicitly
building PX4's existing `mavlink_c_generate` target, without source changes,
then resuming the build completed the image and package.

Exact H6 and changed-adapter source SHA-256 values are:

- `ManualControlInput.hpp`:
  `bd0a032f7c5aa66457fa789ec57adbda146bb3fc30d91fb27918f9b169ec00b4`
- `ManualControlInput.cpp`:
  `377307bca4d2e3680168246a8f6917eb8598f7d87568cd56e5eed2edc2d19fb1`
- `ManualControlInputTest.cpp`:
  `2c9453f36b581a83c51cff7fffc802d44666f1a52fda4d4edbf2a4c365b25bc3`
- `Px4ManualControlInput.hpp`:
  `466c18b57573d4b26b903e2e203b2c40d0bf51e999764832ad0a674083caf538`
- `Px4ManualControlInput.cpp`:
  `4d7f075c15fe93e9cf21e514b22ce3764409bcc4ae589045f670b4f7c9b71555`
- changed `Px4ControlRuntime.hpp`:
  `c045224fb6419802097402c2b5f5a7ad5d11dba86307020a3e55e48749428a97`
- changed `Px4ControlRuntime.cpp`:
  `8cd2db322f66d22d02d99b6f849fa9e66322007f08c6ffbea368b1c300a7d215`
- updated library `CMakeLists.txt`:
  `563485e12b6d6095a786e180d56f36bc8eb7df94d16632359b15c5348a90f0af`

Retained artifact SHA-256 values are:

- SITL `bin/px4` (58,590,368 bytes):
  `04bdde6374cbab8623154239e85aa5468e78b81f139ab107c75e4c659a87d462`
- Pixhawk ELF (49,292,732 bytes):
  `b184b88151ca5eaeb6a066c88dae0fc56e4bb1ae372ced2cc2c39488ef0a8248`
- Pixhawk BIN (1,947,428 bytes):
  `0eabe2a2dc7a793185e55ca2497253810d5bfe0605f793566c34b2b10328f872`
- Pixhawk PX4 package (1,854,014 bytes):
  `7ee4d3abbbc9f116f3722ae8e3811f48ddc6def79bda76a13fa86c94a137f37b`
- excluded ARM research archive (261,520 bytes):
  `cfd0d6c500adaf1959ad44571a2ebbbf69cdda265ffd576c36f9113b96a551a1`
- Debug/Release/sanitizer `LastTest.log` values respectively:
  `cc3be2fcfe3bf88b00493d5f8b5aa80a86e368778110d776e8780fc0e6646c8a`,
  `4537d3e2225eaf4e7e13fc2879e29d053c6fbcf356f00a3aa81775785adfaa16`,
  `5ada59625c175e524f86cad0cf3ed13b5ce47fc03114153898d3523853fae2ec`

H6 advances exactly the default-off PX4-selected manual ingress and explicit
manual-to-H5 mapping boundary. It does not activate manual control, connect the
negative-only C1E-N1 runtime, define an acceleration controller, solve
allocation, select operational source identity/timing/maps, or publish an
actuator command. The next control work is a reviewed output-disabled status
and validated-input integration seam after positive admission, followed only
after plant evidence by stabilization/allocation design. Startup, arming and
output-producing integration remain separate later gates.

## P2E acceleration-interface estimator provenance prerequisite

Implementation date: 2026-09-28. P2E does not change H1-H5 or connect their
runtime. It advances a prerequisite for the future validated-input/status
boundary by observing, in a separate closed native profile, the actual EKF
successful-update result, corresponding output payload presence, reset counters
and hash-bound recorded accel/gyro identities. The observed local-position
acceleration is the PX4 NED body-origin publication value; it is not converted
into or substituted for the contract's commanded inertial-at-CG acceleration
in rotating body FRD.

The final `/var/tmp/p2e-committed-zs4snisq` Debug cases each retain 1,200 rows,
600 successful commits and one reset transition. Every attitude validity value
is false, so P2E deliberately produces no health-qualified bundle or control
eligibility. Release and ASan/UBSan normal/restart cases also retain 1,200 rows
and 600 commits. Final review added a nonblocking destructor writer guard and
regenerated all P2E evidence. Seven checker tests reject authority, sensor,
payload, reset and P2D-binding corruptions. See
`C1E_P2E_COMMITTED_STATE_PROVENANCE.md` for exact native evidence and source
hashes.

The unchanged H1-H5 strict-warning Debug, Release and ASan/UBSan suites again
pass 843 H1, 182 H2, 233 H3, 68 H4 and 73 H5 checks: 1,399 total in each
configuration. A fresh native source view under
`/var/tmp/research-acceleration-p2e` completes default SITL and Pixhawk 6X
builds; ARM GCC 13.2.1 links the board. Both default ELFs contain zero P2E or
H1-H5 symbols. Consequently the board's 1,947,428-byte flash result is linked
default-firmware resource evidence that excludes this research code, not an
integrated P2E/H5 resource measurement.

Latest default-build artifact SHA-256 values are:

- SITL `bin/px4` (58,599,328 bytes):
  `852e75fb6b6d44ead498d8134dce57891d961468ba5fe1613fcdb32c919fa1dc`
- Pixhawk ELF (49,391,532 bytes):
  `9c2008f3145818def83db211bfcab51fdad3a22b8f24b5ca6de8d8bcff1f568b`
- Pixhawk BIN (1,947,428 bytes):
  `abfd70d2720dd44ec17565d80bf94e59d8b289d485061fa5dfdd24b4be404a65`
- Pixhawk PX4 package (1,853,994 bytes):
  `7c41a047bd6d37788efdb3a5948731f5ae5d0e2cdbada2e80a6a9a49746291f6`

The next interface gate is an explicit admissible estimator-health and
selected-instance contract with complete reviewed mutation/sensor provenance,
followed by the still-open clock, operator, transport and distinct positive
protocol gates. No physical coefficient, gain, limit, priority, timing budget,
fallback, startup, arming, actuator publisher or operational default is added.

## P2F direct selected-state provenance prerequisite

Implementation date: 2026-09-29. P2F still does not change H1-H5 or connect
their runtime. It extends P2E in an exact closed `p2f-default` profile by binding
the direct EKF instance-zero path to the actual attitude, local-position and
status publisher instances after topic advertisement. It captures the result of
the real status publication and checks each control/fault counter transition and
publisher-cache value at the completed EKF iteration boundary. It excludes
replay and multi-instance operation and does not claim that the EKF2 selector
selected instance zero.

Final evidence `/var/tmp/p2f-selected-vl0q1aop` passes the independent inherited
P2D/P2E checks plus the P2F checker. Debug normal/restart retain 1,199/1,197
rows; Release retains 1,195/1,197; ASan/UBSan retains 1,197/1,195. All six cases
bind `[0,0,0,0]`, observe six status publications, one control transition, zero
fault transitions and one initial stale-cache row, while health policy,
qualification, positive admission and control eligibility remain false. Native
input coalescence explains the finite row-count variation; no record is
synthesized for an unobserved iteration. Eight checker tests pass.

The P2F bridge retains fixed 2,048-record storage, one owner, sticky first
failure and bounded finish. Its Debug POSIX object is 10,357 text, 8 data and
245,903 BSS bytes, with a largest reported 320-byte dynamic/bounded export
frame. This is private host diagnostic compilation evidence, not embedded
acceptance or linked cost. See `C1E_P2F_SELECTED_STATE_PROVENANCE.md` for exact
source, package, checker and object hashes.

The unchanged H1-H5 strict-warning Debug, Release and ASan/UBSan suites each
pass 1,399 checks. A fresh WSL-native source view under
`/var/tmp/research-acceleration-p2f` completes default SITL and Pixhawk 6X
builds; ARM GCC 13.2.1 links and packages the board. Both default ELFs contain
zero P2F/H1-H5 research symbols and their logs contain no corresponding compile
line. The default board reports 1,947,412 bytes of flash, so the linked firmware
resource evidence excludes the research code rather than measuring P2F/H5.

Latest default-build artifact SHA-256 values are:

- SITL `bin/px4` (58,599,328 bytes):
  `9d2eff414f5b116621e07ccad94e7c605d3153343f7d26bf4282c5c2e00b5b81`
- Pixhawk ELF (49,391,556 bytes):
  `7f3dcd1ca526e64cf5b02e9fd032147da1ff4e58a0e4ac298cc0d21829cd76dd`
- Pixhawk BIN (1,947,412 bytes):
  `e89a56a2775c70814712af66f39a6ed7952f09730d3cb434d596ce6910367af9`
- Pixhawk PX4 package (1,853,994 bytes):
  `aaa6f46f38feddeb90a2fed41db6663728818107a5e5ef564a5c597e8c382829`

At P2F, the next estimator gate was the actual selector-enabled
selected-instance lifecycle, followed by an explicit admissible health contract
with complete reviewed mutation/deployment-sensor provenance. P2G below advances
only the first of those items. Clock, operator, transport and distinct positive
protocol gates remain separate. No physical coefficient, gain, limit, priority,
timing budget, fallback, startup, registration, arming, actuator publisher or
operational default is added.

## P2G selector-enabled acceleration provenance boundary

Implementation date: 2026-09-29. P2G advances the next acceleration-interface
prerequisite without connecting H1-H5 or creating authority. A private, closed,
non-lockstep POSIX profile runs the actual `EKF2Selector` with one actual EKF
instance, observes the selection transition from 255 to instance 0, and binds
the selected `vehicle_attitude`, `vehicle_local_position`, selector-status and
source-`vehicle_imu` instances as `[0,0,0,0,0]`. Hooks cover selector and EKF
construction/destruction, sensor-selection publication, selector-status
publication, and selected-output republishing. Workspace EKF2 source is not
modified, and the profile is not registered in any startup or default board.

The feeder first independently rechecks the accepted P2F package, then converts
its 1,200 recorded 4,000-us angular-rate/acceleration samples to the corresponding
`vehicle_imu` delta-angle/delta-velocity fields. It publishes exactly 1,200
messages on instance 0 with recorded accelerometer/gyroscope device ID 1310988
and no actuator topic. This is a source-derived representation change, not a
new physical coefficient, filter, timing budget or sensor qualification.

Final Debug evidence `/var/tmp/p2g-selector-4ek3kb0n` passes the independent
checker and eight focused checker tests. Normal/restart each preserve all 1,200
feeder publications, selection 255 -> 0 and 589 selected local-position
acceleration payloads; they observe 5/6 selector-status publications and reject
the actual second-selector constructor with sticky fault 4 only in restart.
Release `/var/tmp/p2g-selector-5n54u5ds` preserves 588 selected acceleration
payloads per case, and ASan/UBSan `/var/tmp/p2g-selector-9b5xbf6q` preserves 590
per case with no sanitizer diagnostic. Native coalescence accounts for the
bounded count difference. Exact manifest SHA-256 values are respectively
`46ddb231f4613014c3e7ce50693a0dbe2473c527023afce6e4d84338d5c040eb`,
`275ddf7b43e085ce22973b0ce0781ce9c521907cbe228704394a395c58eb19d5`
and `18e4dabcc063e3a6a72a9d2af772924ef1655066ce45b5beecb0ecfee3bfd4cb`.
The corresponding independent-check hashes are
`8b0a922656eb525d4ed48c88afd68e91e06546689f21b6f6c3fab0932f70e3d7`,
`ad7e4e97e438b90eb4e2d0fcbad523aed012048f9cf857ff46b809975e4d0abe`
and `e8e8989275db263b8f85ee0be1c3ba25e733e351be57890b4aefdf89ff028e58`.
Additional closed-profile identities are:

- Debug/Release/ASan executable SHA-256:
  `1840be8c3457bb4fc1f3ce738b5bc32f1108b3cc355ea19284c0bf65ba53653f`,
  `95f6b499a14276b083855b57c5303615646d1e3273341d1873a922cce806029b`
  and `eeb8a3226350607a7e1781213195a2448f3bd2d0c0e049d2a1d1dcd5b4675c3c`.
- Debug/Release/ASan `compile_commands.json` SHA-256:
  `a7401964d19d97268d0e9385438f895228b210f388eb4b905d47e8ef962f06ee`,
  `77808d8a8b26f563ce617da2cad602ec299b5a49b22f01db8770fd50353b7789`
  and `d6694cdd315679e1ea6703b5bc6f323958704e89e6d4cf8cc3ccc97573bc4755`.
- The common board-config SHA-256 is
  `a32fe87cf0f3992fe5378d139a48081201bfa2036bdfb85188842dd11d507423`;
  the eight-test Debug log SHA-256 is
  `698d16d7b93c4bd9707d3cfacda6fb1e0fea99ed0cda6e69bedc42772940540a`.

All six cases export `health_policy_configured`, `health_qualified`,
`failover_qualified`, `positive_admission` and `eligible_for_control` as false.
The selector's raw `healthy[]` observation is evidence only and is never treated
as admission. P2H subsequently showed that the EKF supplied no attitude
candidate: every one of the 1,200 `PublishAttitude()` entries per case had
`attitude_valid == false`, so no attitude publication was attempted and the
selector's stock 10-ms gate was never reached. All six P2G cases therefore
contain zero selected attitude republications; P2G qualifies neither selected
attitude provenance nor health. One instance cannot exercise failover. Selected local-position
acceleration is the PX4 NED body-origin publication value and is not converted
to, or substituted for, the commanded inertial-at-CG vector in rotating body
FRD.

The bridge uses fixed 2,048-output and 64-status records, one explicit owner,
sticky first failure and bounded finish. Its Debug POSIX object is 17,521 text,
8 data and 265,386 BSS bytes; the feeder object is 5,788 text, 8 data and 67,258
BSS bytes. Their hashes are
`cfa174528e505ef0eb2a9caec93e612eff95e6cb0b00a5b65174cb20604a2e48`
and `06a24192defc45765f490446bbbd5b74c63c78582d250d46276ba6e1b4258adc`.
The largest reported observer self-frame is the 576-byte dynamic/bounded finish
frame. These are closed host compilation diagnostics, not linked firmware
resource or NuttX acceptance evidence.

The unchanged H1-H5 strict Debug, Release and ASan/UBSan suites each pass 843,
182, 233, 68 and 73 checks: 1,399 total. Their `LastTest.log` hashes under
`/var/tmp/research-acceleration-p2g/{debug,release,sanitize}` are
`4f704af75eb73a19dd13977c99c053c9198a56f0f81acc8bf239d7f275fc2c8a`,
`5089c059898ffb95c5f9b3f50e523e0dcf2af7e4b981d0fc4eab45ba2c9a05d0`
and `bd11bbb0936b40bc3a9ea145681ad25b066bef09a44a956dcfc4e771d8217879`.
Scoped PX4 AStyle passes from a no-space source view.

A fresh clean WSL-native source at
`/var/tmp/research-acceleration-p2g/px4-source` completes default SITL and
Pixhawk 6X builds. ARM GCC 13.2.1 produces the board ELF, BIN and PX4 package.
Both default ELFs contain zero P2G/H1-H5 symbols and their logs contain no
corresponding compile line, so the following are linked default-firmware
resource evidence that **excludes** the research code, not P2G/H5 linked-cost
evidence:

- SITL `bin/px4` (58,599,328 bytes):
  `1ea254cbbe403d0d0ab96727ceda8363a1ecd061f54a4fa8dd186f3d37011b31`
- Pixhawk ELF (49,391,644 bytes):
  `28424d600ca58220539be2628a8fdf5f15ea83ccf14f48c12ea73d435701b929`
- Pixhawk BIN (1,947,412 bytes):
  `50d46a16e0b6cba0ac8f1ba03946ec344d8a6a9599e5e7c042326ec9730c8899`
- Pixhawk PX4 package (1,853,994 bytes):
  `89f4730beb2a7e9fc5f0cf934c72034805c5e0af9c68efddfde3d10a50c72932`

The fresh SITL build-log SHA-256 is
`58996bc35b1cf63d219ccb945384696cd764e2786086c3b7103a2e48a0b4b884`.
The Pixhawk ELF-build and corrected package-target log hashes are
`13c3508b99237921680b90efb4cf0f18b83cdea84228d6af8a0441efa16d9819`
and `99697489650ef3e420e337827827a5165012307ca82ca1770542dd33af7876e0`.
An initial request for the nonexistent target `px4_fmu-v6x_default` failed before
packaging; the correct `px4_fmu-v6x_default.px4` target then succeeded. The
resulting board size is 1,939,400 text, 8,012 data and 105,596 BSS bytes; flash
is 1,947,412 of 1,920 KiB (99.05%). Those values still exclude P2G/H1-H5.

P2G closes only selector-enabled single-instance lifecycle and selected
local-position acceleration provenance. Full P2 remains open for an admissible
health predicate, complete mutation coverage, selected attitude coverage, a
real multi-instance failover/handoff case, deployment-sensor qualification and
immutable freshness/skew/latency budgets. P3 clock/map continuity and P4
operator/transport/distinct-positive-protocol authority remain separate. No
startup registration, airframe, arming behavior, actuator publication,
controller, allocator, fallback or operational default is added; C1E-N1 stays
negative-only.

## P2H upstream-validity and selected-output disposition provenance

Implementation date: 2026-09-29. P2H advances the closed acceleration-interface
evidence boundary without changing any PX4 gate or creating authority. It extends
the accepted P2G private source view with observation hooks at the actual EKF
attitude-validity/publication branch and at each actual selector attitude and
local-position candidate decision. The hooks record the stock monotonic tests and
the existing 10-ms attitude and 20-ms local-position freshness decisions; those
values are observed source behavior, not adopted operational timing budgets.
Workspace EKF2 source remains unchanged.

Each of the six Debug, Release and ASan/UBSan normal/restart cases records 1,200
actual EKF attitude-publication entries. All 7,200 entries have invalid attitude,
no publication attempt and no publication success. Consequently the selector
records zero attitude candidates and zero selected attitude publications: the
10-ms selector gate is not reached. This corrects the earlier P2G attribution of
the zero count to selector freshness, but it does not qualify a valid selected
attitude payload.

The local-position path is fully disposition-bound for the retained fixture.
Debug normal/restart each record 589 checks, candidates and publications;
Release records 588 of each. ASan/UBSan records 594/593 checks for
normal/restart, with 590 candidates and publications in each case; the remaining
calls are explicitly recorded as candidate-absent. Every candidate in all six
cases is monotonic and fresh under the unchanged stock branches, with zero stale
or nonmonotonic disposition. Each `publish_requested` row corresponds exactly
to a successful P2G selected publication and preserves all three acceleration
float bit patterns. The P2H binding `[0,0]` and inherited P2G binding
`[0,0,0,0,0]` retain one actual selector and one actual EKF only.

Normal explicitly stops and exports the first lifecycle. Restart invokes a
second real selector constructor, records sticky fault 4 in both P2G and P2H,
and preserves the first-lifecycle evidence. The implementation uses three fixed
2,048-record arrays, lock-free reservation, one administrative owner, sticky
first fault and bounded finish. `health_policy_configured`, `health_qualified`,
`failover_qualified`, `positive_admission` and `eligible_for_control` remain
false in every trace. Nine focused checker tests accept the frozen upstream-
invalid case and reject authority, invented absent-candidate disposition,
invalid-attitude publication, unexpected valid source, freshness, publication,
acceleration and lifecycle corruptions.

Final evidence packages and SHA-256 bindings are:

- Debug `/var/tmp/p2h-disposition-bv29hkud`: manifest
  `e015e46c659be1ce9fc4aef7a0b5ce2845c0ea44738acc742894acdb06928b06`,
  independent check
  `60da4346331923e1d64c643eea7b9c6495d5a38877099acbd0387d5377dde3d5`,
  executable
  `835093da55ad8ef0c95c27222a7ffcfb96506e8842277006cdc2d9efbb024d82`,
  configure log
  `8328327174bf9c870f72f2994fcb49693e9bcf39cc29c7e5a4b91900b8ec75ef`
  and build log
  `d4f768f908d160d463c9273a11dc9654da98ab0382d18432a5c1e95c2686d59b`.
- Release `/var/tmp/p2h-disposition-gnzmttte`: manifest
  `f495ad5f68e794126cad16015cf6b59269d2d8a645b82e40caaf4d0ecb1d9f6f`,
  independent check
  `f3f21e312ea8b7c02fb405c4a09436406d35ab1fd968eb22d1f5e4f32a2e0ef2`,
  executable
  `6dc30d5ac74d7e6c1731cda5c762e9ab9d1680c8385dc003fb668baf63397518`,
  configure log
  `a57cfbc1a9fb32e507b091945be1e832e3f44b36b40c8d557128d45c740374be`
  and build log
  `7e1c825298d5282fae2db4ce551ad92059d26a715a347fdf42fbea75cf7fc247`.
- ASan/UBSan `/var/tmp/p2h-disposition-ps2oxj5a`: manifest
  `4112944c84edaf34bbd78a15dfa50db2d967dd8128859fc1a1f39ff91d8e1adb`,
  independent check
  `cca7398eed7d26ee265d286913257f673f6c57c66cb45ddc647034741a5f944f`,
  executable
  `c9ef11e92162456a19d4d5093f7ada470c6535519c2b421774790fff3974534e`,
  configure log
  `7882bfe430fd21241e285f7dad12d41de079727467e4703277ca54fb527304d8`
  and build log
  `bd723a0b1a5f72f4d20f62e91f1b9a6cc2d75e47428f50a6964298b719c1c84b`.

The common board-config SHA-256 is
`82f8656145fe10ee0944bb91310129883d15bd81a75e34c6ebe7ade5af157374`.
Debug/Release/ASan `compile_commands.json` hashes are
`50a10495b1c449005b8c4cc10f49afee8da00fb0cf6778a91b670ec31e1e7fba`,
`2d57b162c435e932af13658f117e36e2459d5e23ef602da2677d8a175dad725c`
and `1f5afe6a8f591c2407c9a17c4217a1334b5937a5476f223406eb259c573ea597`.
The nine-test Debug log hash is
`d9d734ee1c2de67b748aeef1c4126433aa438ad8375ca953916f12b1df11414d`.
Exact hashes for all eleven P2H source anchors are in
`RESEARCH_CODE_GRAPH.json`.

Scoped PX4 AStyle passes. The unchanged H1-H5 strict Debug, Release and
ASan/UBSan configurations pass respectively 843, 182, 233, 68 and 73 checks,
1,399 total per configuration. `LastTest.log` hashes under
`/var/tmp/research-acceleration-p2h/{debug,release,sanitize}` are
`fb6b06c02e091ce332ed07dbeb836d63c9fc6c4a7911747dc134643e7ae3e665`,
`64a82ceadeb9f8d4aa122a9e577fec91ec9a5d47dd73e94d47e67a9592b156d8`
and `47020c8f2721472f60a51c93d5955b792e5039d2cd0cef900fc9f02bf0712a44`.
The corresponding wrapper-log hashes are
`260bc2bb6c2094ed85d99695cea0b28e1efb18f4a543d5920ab38d82a4e3b14a`,
`79f7bccc1a22cfc7f41b24c9c87fe7d324f96da5b1e797aa4207b42d1f6a380f`
and `f9b8e6a931a162626fe189f690cccf25967551b00b4ff3280f407bb9ceecf5b8`.
No sanitizer diagnostic is present.

The P2H observer's Debug POSIX object is 15,915 text, 8 data and 360,564
BSS bytes, SHA-256
`373219eade91c683512d14b637e9dcb570b0dd8866cce601f6f0b60c9081d599`.
Its largest reported self-frame is the 448-byte dynamic/bounded finish frame;
the stack report hash is
`01833f4807daf4542ebf3177241423d2141d4aa3a4cac18cb8bc3d3af9fb0726`.
These are closed host **compilation diagnostics only**, not NuttX acceptance or
linked firmware resource evidence.

A fresh clean WSL-native source at
`/var/tmp/research-acceleration-p2h/px4-source`, pinned to PX4
`e880f1cd79e2453ef3dfaf68ee1c067b98ea9a53`, remains clean after complete
default SITL and Pixhawk 6X builds. The SITL ELF is 58,599,328 bytes, SHA-256
`e30d6b1cb8266f051d2f525462f8b1d683aa1d087b87ef109d2c5917baf46031`;
its build-log hash is
`88e83f071e0c7cf9426e93585e862865b93fbc3ef03e4745eeec2fab84a8cea2`.
Ubuntu-24.04 WSL ARM GCC `(15:13.2.rel1-2) 13.2.1 20231009` produces:

- Pixhawk 6X ELF, 49,391,644 bytes:
  `5626034f1432d8e93e38261c3347c2a87fd01f411d13001e21089957a61a35ae`
- Pixhawk 6X BIN, 1,947,428 bytes:
  `31e32501530670a278dc8874e05bcbf570e4e42ace60814777476ac9f4f9ef6f`
- Pixhawk 6X PX4 package, 1,853,994 bytes:
  `2ca04ee63f438d8259866847e3c9393a0b9c441e50b011f74265152296c1072c`

The Pixhawk ELF-build and package log hashes are
`11baeacd26619a3bdca403a687674e79a99745c0b3fa30a69fd38761ac1c7ae1`
and `84b64977d36a5cc949413f76a39ab1a1a96ef3628d459b473ea7dbef5c84addf`.
The linked default firmware reports 1,939,416 text, 8,012 data and 105,596
BSS bytes; flash is 1,947,428 of 1,920 KiB (99.05%). Both default ELFs contain
zero P2H/P2G/H1-H5 symbols and the logs contain no corresponding compile line.
These size figures are therefore linked default-firmware **resource evidence
that excludes all research code**, not P2H/H5 linked-cost evidence.

P2H closes upstream-attitude-validity and selected-output disposition provenance
for this frozen single-instance fixture. Full P2 remains open for a valid
selected-attitude payload path, complete mutation/deployment-sensor coverage,
real multi-instance failover/handoff, an approved health predicate and immutable
freshness/skew/latency budgets. P3 clock/map continuity and P4
operator/transport/distinct-positive-protocol authority remain separate. No
startup registration, airframe, arming behavior, actuator publication,
controller, allocator, fallback or operational default is added; C1E-N1 stays
negative-only and no flight readiness is claimed.

## P2I source-derived valid selected-attitude provenance

Implementation date: 2026-09-30. P2I closes the valid selected-attitude payload
gap left by P2H without changing an estimator, selector or admission decision.
The private non-lockstep POSIX profile still runs one actual `EKF2Selector` and
one actual EKF2 instance. It now replays the complete `vehicle_status` and
`vehicle_land_detected` scalar payloads that overlap the accepted 1,200-sample
IMU fixture. All state comes from the same pinned stock Cessna ULog, SHA-256
`62665d68f92ceb47068a5b5f16a1407998e0f2cf08049e98fc55cf61e90c8143`;
no state, physical coefficient, gain, limit or fallback value is synthesized.

The fixed fixture spans source time 5,520,000 through 10,316,000 us at the
existing recorded 4,000-us IMU interval. It retains the latest pre-window record
and every in-window record for each system-state topic: 14 complete
`vehicle_status` payloads and six complete `vehicle_land_detected` payloads,
20 events total. Their recorded ordering is preserved while only each uORB
publication timestamp is transformed to fresh HRT, matching the existing IMU
replay transform. The fixture SHA-256 is
`130175ecef2110ceb184db35cd47942b654476c70747dff349e1e5b29604ac68`.
The independent checker reconstructs all 20 rows directly from the pinned ULog
and rejects any field, count, order, source timestamp or authority difference.

That recorded landed/at-rest context causes the unchanged EKF attitude-valid
branch to transition during the fixture. Final Debug normal/restart retain
1,111/1,109 actual EKF attitude publication successes and the same number of
selector candidates/publications; Release retains 1,111/1,111 and ASan/UBSan
1,111/1,111. Every candidate passes the unchanged stock monotonic and 10-ms
freshness branch with zero stale or nonmonotonic selected publication. The P2I
observer captures the quaternion, reset delta and reset counter before selector
reset handling and after the successful `vehicle_attitude` publication. All
6,664 selected quaternion payloads and all 6,664 reset payloads are bit-identical
to their actual `estimator_attitude` sources. The inherited local-position path
also retains 589 publications per Debug and Release case and 590 per sanitizer
case, with all acceleration bits preserved.

Normal stops and exports the first actual selector/EKF lifecycle. Restart
constructs a second actual selector and retains sticky fault 4 in the inherited
P2G/P2H observers and the P2I observer while preserving the first lifecycle.
P2I uses one fixed 32-record state store and one fixed 2,048-record attitude
store, one explicit administrative owner, sticky first failure, bounded finish
and failure-preserving close behavior. Eleven focused corruption tests reject
mutated quaternion/reset payloads, nonfinite values, altered counters,
chronology, invented authority, actuator publication and recorded-state source
changes. Scoped PX4 AStyle, Python syntax and whitespace checks pass.

Final evidence packages and SHA-256 bindings are:

- Debug `/var/tmp/p2i-attitude-vkyya591`: manifest
  `7bba3b578be0d3aa730970512926fface7e191bcc20ddcd49075694a1024cc0b`,
  executable
  `e7466128938842710324aaa29f1fb7930427ec7d157a1f3c090807a375f962af`,
  configure log
  `c850908383e7eafd182b1757e60ac6b0dcddb6a150f9454a0d560fe32457fc97`
  and build log
  `4edb95c44d1a98777e3c4fcd3b559da322230dea76df3797e9ff1be7d32af80c`.
- Release `/var/tmp/p2i-attitude-2_co807l`: manifest
  `303b9f863b514928b1fef5e6cb8655a42d1272d4fec944f8f7ff881dbf1fcc79`,
  executable
  `f267e8a1e5f93a4bb76b5e56e3ea862002cf20e79fc7ffaadaaec5ae36d206d8`,
  configure log
  `26ff9f1291ae6e884a1263f0484c97a60989c30e46420db054c8f4f7648c3575`
  and build log
  `6515976990c4c7174543a5a68314f7a6536929d2825db7c921c885e01b1bed90`.
- ASan/UBSan `/var/tmp/p2i-attitude-zxtllgub`: manifest
  `3a6a7fb29c6831fa3066ceb22d752b40fb8561404822423d5e6e02ac982ce0cb`,
  executable
  `d3015790bd68ecf536917c6cadf78d160b94e3f7418bc4efd3d98ba85bd92278`,
  configure log
  `77438392eb963675dbd9e1141a716d232b2f8ef0fd9b5bef6a2c2aa1bd2bf227`
  and build log
  `00810d012c1c200ee104e344d5a1af9d329932484ec3114f629ea8909a762dc2`.

All three packages bind the same source registry
`ae0acf5628eedd7f97542691b6c436f9577156ef5c449e71cf521b62429602de`
and board configuration
`65f51bddbd8862a63aaca2f0f281bb25426fcd64f6e414518c05060787cd61e9`.
Their Debug/Release/ASan `compile_commands.json` hashes are
`02c89adac63c528946af1a58b4b4e96e357961f2bd6b19ac78b27ee000f91cb4`,
`a010c17359617c6803ce0679186299548bf2916244849dab4366d5f15bca7f07`
and `15184727b04e6910e948f178e37fe77fe63968b9098c7f07b653323b5770fc2c`.
The 11-test log hash is
`4c3b3b2f85f7f81206f2731efca1a5c61f58b2f6c54d820b4003b988757b84ec`.
Exact hashes for all 14 P2I source anchors are in
`RESEARCH_CODE_GRAPH.json`.

The unchanged H1-H6 strict suites pass 843 H1, 182 H2, 233 H3, 68 H4,
73 H5 and 132 H6 checks: 1,531 per Debug, each of three explicit consecutive
Release passes, and ASan/UBSan. Retained log hashes under
`/var/tmp/research-selected-attitude-p2i/regression` are
`ea540e2a494878a5adb2597c24a6a65a740dd43fec97258e0951177ad7c22e12`,
`055a60c1fd1a2e247f95a4b488763c69fc6b49b346257af00752ab1fdad90f4b`
and `69204f4342ead7b5a293b006db58b31c297ed5fd4b24030c9f3a79bb158c111f`.
No sanitizer diagnostic is present.

P2I's final Debug host objects are compilation diagnostics only. The inherited
P2H disposition observer is 15,915 text/8 data/360,564 BSS bytes; the P2I
selected-attitude observer is 10,442/8/229,445; inherited selector state is
17,521/8/265,386; the derived feeder is 6,073/8/67,258; and recorded system
state is 7,948/16/11,110. Together they are 57,899 text, 48 data and 933,763
BSS bytes. These POSIX diagnostic objects are neither NuttX acceptance nor
linked flight-firmware resource cost.

A fresh WSL-native source view and builds are retained at
`/var/tmp/research-selected-attitude-p2i`. The complete default SITL ELF is
58,605,728 bytes, SHA-256
`e5184b24efffcb11caf9574d6bc200aa941c2b0dc3e458f959328cd48b2afeae`;
its build-log hash is
`c6c0d4398fcab8fc78c6ff002ec2687a7710d24a4cf4df5607daeaf616fda093`.
Ubuntu-24.04 WSL ARM GCC `(15:13.2.rel1-2) 13.2.1 20231009` produces:

- Pixhawk 6X ELF, 49,469,444 bytes:
  `80d0c843dc6f4f792d1bb0618f4cc112e79769ec17883e1ff2cf716c93355998`
- Pixhawk 6X BIN, 1,947,412 bytes:
  `6c0be3d5b331f27b93d73fb9e9319cf0b7837b53871444f13cc0e2d9f4e0d8de`
- Pixhawk 6X PX4 package, 1,854,002 bytes:
  `424ff1bfe14150532a646440a107905f2b98cd93e2ad6f0d5a833cb196265132`

The Pixhawk ELF-build and package log hashes are
`aa73904c69e5fe5361743ee600ec2710c18de1d07f2c35a588c85e95a68913a9`
and `a94edc2ade04a4ae2ac8442660d753730a35fa4123dbf45a0c8babbca954ad6f`.
The linked default firmware reports 1,939,400 text, 8,012 data and 105,596
BSS bytes; flash is 1,947,412 of 1,920 KiB (99.05%). Both default ELFs contain
zero P2I/P2H/P2G/H1-H6 symbols. The default build and package logs contain no
P2I or H1-H6 compile line. These are complete linked baseline-firmware
**resource measurements that exclude all research code**, not P2I or H6 linked
cost. The POSIX object figures above are separate compilation evidence.

P2I advances exactly the source-derived valid selected-attitude path. It does
not qualify an estimator-health predicate, deployment sensors, complete
mutation coverage, real multi-instance failover/handoff, immutable operational
timing budgets, provider/sample-clock continuity, operator/transport authority
or the distinct positive protocol. No validated acceleration input is admitted
to H5/H6, and no controller, allocator, startup registration, airframe, arming
behavior, actuator publication, fallback or operational default is added.
C1E-N1 remains negative-only and no flight readiness is claimed.

## P2J actual two-EKF selector timeout-handoff observation

Implementation date: 2026-10-01. P2J advances the real multi-instance path
left open by P2I without changing a stock estimator, selector gate, admission
decision or output path. A private, non-lockstep POSIX profile starts two actual
EKF2 instances and one actual `EKF2Selector`. Two `vehicle_imu` uORB instances
receive duplicated copies of each of the 1,200 hash-pinned recorded IMU
payloads; the complete P2I `vehicle_status` and `vehicle_land_detected` replay
is retained. Actual bindings are `[0,0,0,0,0]` and `[1,1,1,1,1]` for EKF,
multi-EKF, attitude publisher, local-position publisher and IMU subscription.
This intentionally provides no sensor diversity and does not represent
deployment hardware.

The feeder waits for the unchanged selector to report both instances available,
primary instance zero and both raw `healthy[]` bits true. It then invokes the
stock `ekf2 stop 0` command and continues the finite fixture. The stock selector
times out the stopped primary and makes one unrequested `0 -> 1` selection in
each normal and restart case. P2J records the old instance unhealthy with the
stock timeout condition, the new instance healthy, the selected attitude and
local-position publications on both sides of the switch, one attitude reset
barrier and all six local-position reset-counter barriers. Every selected
quaternion, reset and acceleration payload is bit-identical to its actual
source. The observed stock timeout is a source fact, not a project timing
budget or fallback policy.

One fixed-capacity evidence owner retains selector lifecycle, two separate EKF
lifecycles, at most eight selections, 128 status records and 2,048 records for
each selected output. Lock-free lifecycle facts, a sticky first fault, bounded
finish and failure-preserving export are enforced. The restart case preserves
the first completed lifecycle and rejects a second actual selector with sticky
fault 4. No dynamic evidence storage, startup registration, airframe,
parameter, arming behavior, controller, allocator or actuator publication is
added. Eleven focused checker tests reject payload, binding, chronology,
handoff, barrier, raw-health and authority corruption. Scoped PX4 AStyle and
Python syntax checks pass.

Final independently verified packages are:

- Debug `/var/tmp/p2j-handoff-u4up5z6_`: manifest
  `e445b041336a1b703af9559d0ada68ca2397f89e8f93653689a1ec7d7370559c`,
  independent check
  `e3d4d63356ea6a1713a00bc6c080cf4ad7f6f74224473aa1a283d7a905dad8fe`,
  executable
  `1fbd0efb002caa2dcfe64dc1d8e9ecb75f41516f5ff64d5940b7a746f3184636`,
  configure log
  `305a0b034eb214659ffd1af32459cc0eb4cb92526271e4549ee1be65f9b57bab`
  and build log
  `cb725df538145f1d76727322435ca8a2a7977c8de47263879067bc4e91b7dd17`.
- Release `/var/tmp/p2j-handoff-t7z5qo9k`: manifest
  `8a352498e57c2549ba78d4b6958190120b96da6a13219bebd4e5114367aedaa2`,
  independent check
  `7d9a222c1c72554c89a8e3e96c6bf2ff024c1dbfecb60313acb59cf858b8af13`,
  executable
  `788f96aaa18971d83941bce1aefa46c05fd63e3023c0e9f4faa4d00e64e26b42`,
  configure log
  `cea5ae9df0b2c2a967e33a95627edd0dd8bca30f6266a50daf46960136ec97bb`
  and build log
  `f43bc7767a7982fca3ddcb012145d590c4dc952fc162f0bc865afc309e37f82b`.
- ASan/UBSan `/var/tmp/p2j-handoff-6ziwnuyt`: manifest
  `6502503cfc4ce40b6a3eccc877e9ee7bee4b9a4fd720be38ab49b7b316ec3523`,
  independent check
  `e25a843e906c909db59c3963b7d0eccd2bba0968264c0e9a05a0de06b92c74e4`,
  executable
  `c23443a3629619be0ddaaed7a844203797dd197ef551485cf415a4129179caae`,
  configure log
  `c9e6bde252a1987a609fd569680b2bada25503a9596cb0fd7a809188e0fe2a59`
  and build log
  `1227634234da421173c2215a0a3b2d8e29ca9a26764dab436c0c12e01dbf0ad5`.

All packages bind source registry
`ed9cf07aa70b00bfb4b06385e5081bc3083285b16e2dd9e5685839b173aa1204`
and board configuration
`fddccb4c52fab7787ed669087ea6cc8bf93530df556ea3f70498fa2c47c7e89d`.
Their Debug/Release/ASan `compile_commands.json` hashes are
`c08c3163b4512f88e8f99faac5fa930ff72b28bc8b22e9cce0210f987b0de99b`,
`e247db1b8286f3d45dd52a6f71e34bc5e13e52fcc5dd02e33d217383dbf993ee`
and `892794dc5499cbcd0317b9ea83cadc6d2147647717a4d0b2c18c331baec04a6d`.
The scoped AStyle and 11-test log hashes are
`2d3d1fc5019c42fd742df64803da2171ebff4ac2120353ffad5b39c1c3f23d8e`
and `4c3b3b2f85f7f81206f2731efca1a5c61f58b2f6c54d820b4003b988757b84ec`.
Exact hashes for all 12 P2J source anchors are in
`RESEARCH_CODE_GRAPH.json`.

Debug normal/restart split the fixture at samples 264/264 and retain
1,106/1,104 selected attitudes and 587/587 selected local positions. Release
splits at 262/265 and retains 1,107/1,107 attitudes and 588/587 local positions.
ASan/UBSan splits at 262/262 and retains 868/867 attitudes and 469/469 local
positions. Every case publishes 2,400 IMU messages from 1,200 duplicated
inputs, observes exactly one automatic timeout handoff, one attitude reset
barrier and one complete six-counter local-position barrier. Runtime count
variation is scheduler coalescence; the checker requires both sides of the
handoff and does not synthesize an unobserved output. No sanitizer signature is
present.

The unchanged H1-H6 suites pass 843 H1, 182 H2, 233 H3, 68 H4, 73 H5 and
132 H6 checks: 1,531 per Debug, each of three explicit Release passes, and
ASan/UBSan. Debug and sanitizer test-log hashes are
`3a880a887cb8ecb42f7cbfcd4bee69e92ceb8c77c1ac9c848b336ff96ff554c4`
and `cca1fefbdf5eaf0b0c0617ce2d5d98ab6ac8c5f3ccfdeb279bfc94be8f4e496e`;
the three Release log hashes are
`8e594ea733ac2c17c0806100b87c2072073dd122510cf22369a0c86427e1dd2b`,
`c18353823d5aa0a5cb985695631a643cc76b53cf54d7727204b281a82acd16d7`
and the same second-pass hash for pass three. Build-log hashes are
`24223514df2cde728f2f51a3caec53e4ed5ef54377066e162d99cbbe6effe9ff`,
`3987b998baab9a148a81c814890723e3a3444f76ef8b57f3ae6422638786d7f5`
and `52e1e1804030cf23c1dab28cbb0c3a434614f3b00df8b12493c83162e2461089`.

P2J's final Debug objects are compilation diagnostics only. The selector
handoff observer is 20,483 text/8 data/401,942 BSS bytes, the two-instance
feeder is 9,042/24/77,220 and inherited recorded-state replay is
7,948/16/11,110. Their SHA-256 values are respectively
`78bd4dcaf32d6075016627457307957d84a70318144c06429f9d6a3f96a28e27`,
`7b0f1b64ff8fbb5fd08edebe9b65de6a9a871fc660493d629bbabeade3240ee2`
and `cc334a1dff4bf422dbe21af1deae8db230aeb50881820cee98cc4d1a05935861`.
Compiler reports bound the handoff export at 656 bytes, feeder export at
368 bytes and inherited recorded-state load at 1,120 bytes. These POSIX
diagnostic objects are neither NuttX acceptance nor linked flight-firmware
resource cost.

A fresh WSL-native default source view and builds are retained under
`/var/tmp/research-selector-handoff-p2j`. The complete default SITL ELF is
58,604,448 bytes, SHA-256
`e95be055a9e55ad1a3ef9a9cc70348efe05b8257e5a7efbb4872045711320a22`;
its configure/build log hashes are
`13af9288efbe9cd418c05ce8bafc493a898e1e2904fd78bfe0c54d8d0af08c20`
and `114c589988e2b2dfffc89f7432f92bbc12d051bae502ecedbb97f33ce3ca46ad`.
Ubuntu-24.04 WSL ARM GCC `(15:13.2.rel1-2) 13.2.1 20231009` produces:

- Pixhawk 6X ELF, 49,453,880 bytes:
  `68e4767da16defb93af2814e32dec987e773825f97b288c71b2238b3d1952142`
- Pixhawk 6X BIN, 1,947,428 bytes:
  `dd5afe7c6483d2c2c7651fe2db785a38e1fc98ca544799ca899c74cbd796d850`
- Pixhawk 6X PX4 package, 1,854,014 bytes:
  `267896810a457483a0a628aa6a2a65c3a7e5716c25fda0c92837710f9887c5ba`

The board configure, ELF-build and package log hashes are
`2ab68d3ec8f65859f6ba60c4b7aa251fe11570624b4fd1db1f18a2f541fb1104`,
`994e3771bf6488195fa57ff17354e13492cc7e28118a1bd01bb2c4bcc7b11dee`
and `a803964b3a32a21d6f19be079c1b7fcda84bd19f46591ce3efd4dd525f33b7d5`.
The linked default board reports 1,939,416 text, 8,012 data and 105,596 BSS
bytes; flash is 1,947,428 of 1,920 KiB (99.05%). Default SITL and board build
logs contain no P2J compile line, their module lists contain no P2J module, and
both ELFs contain zero `research_handoff` symbols. These are complete linked
baseline-firmware **resource measurements that exclude P2J**, not P2J linked
cost. They are distinct from the POSIX object compilation evidence above.

P2J advances exactly actual two-EKF lifecycle, binding and stock timeout-handoff
observation. Because both instances consume duplicated payloads from one
recorded sensor identity, it does not qualify sensor diversity, deployment
sensors or failover behavior. It also does not establish complete mutation
coverage, an admissible health predicate, immutable operational timing budgets,
provider/sample-clock continuity, operator/transport authority or the distinct
positive protocol. `health_policy_configured`, `health_qualified`,
`failover_qualified`, `complete_mutation_coverage`,
`deployment_sensors_qualified`, `positive_admission`, `eligible_for_control`
and `actuator_publications` remain false in every export. C1E-N1 stays
negative-only; no validated acceleration reaches H5/H6, and no startup,
airframe, arming, controller, allocator, output, fallback, operational default
or flight-readiness claim is added.
