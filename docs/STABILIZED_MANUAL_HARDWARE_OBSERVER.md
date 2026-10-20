# Stabilized manual hardware integration: output-disabled observer

2026-10-19, Codex. First G9 software increment following the user's instruction
to proceed. The observer builds for Pixhawk 6X on the verified working
PX4 `e8ea98b404baf50623a519ac67ec1c03aa240859` / NuttX 11 baseline.
It is **not an active stabilized hardware mode** and has not been flashed or
tested on the board. Existing bench firmware and the 31-case synthetic SITL
package remain separate and unchanged.

## Implemented

- Default-off `research_stabilized_manual` module, explicitly started with an
  external profile. Construction enters Ready; `observe` begins calculations
  only after the control core accepts centered input and supplied state.
- Actual selected manual, attitude/rate, sensor selection, estimator status,
  status flags and permission subscriptions. Inputs are copied before reading
  the evaluation clock. The diagnostic adapter requires the declared single-EKF
  configuration and rejects observed device changes, reset counters, stale or
  future samples, permission loss and alignment faults.
- Existing body-rate, quaternion hold, rate PID and bounded canard allocation
  core reused. Zero simulated thrust is now permitted for wind-only/observation
  configurations; negative and nonfinite thrust still reject. Float/double
  min/max use equivalent C math entry points supported by NuttX 11.
- One diagnostic publisher, `stabilized_observer_status`. Successful records
  include reference, rate target, requested moment, integral, allocation residual
  and four normalized candidates. `candidate_valid` distinguishes calculated
  rows from Ready/fault/NotDue records; invalid candidates are cleared.
  `outputs_enabled` is always false. There is no servo, motor, bench-command or
  flight-setpoint publisher and no `enable` command.
- `disable` and faults latch observation off. Stop/start with an explicit
  profile is required before a new centered session. Only the work-queue task
  mutates controller state; CLI requests use a bounded atomic slot.
- Dedicated 6,000-byte work-queue stack reservation, isolated from the flight
  controller queues. Actual hardware high-water usage and timing remain unknown.
- The module's CMake rejects simultaneous inclusion of the bench owner. The
  isolated ARM configuration excludes that owner. This is a build separation;
  runtime ownership transfer to a future active output path is not implemented.
- Root library registration is guarded so the earlier private SITL external
  module can continue registering the same library before `src/lib` is processed.

The adapter's alignment/status checks are diagnostic conditions, not a reviewed
production health predicate. A constant diagnostic source epoch does not detect
all same-instance publisher replacements between polls. The earlier POSIX
publisher-revision work has not been ported to NuttX by this increment.

## Explicit observer profile

The reader accepts exactly 86 whitespace-separated tokens. It supplies no tuning
defaults and preserves the caller's profile on any error. Overlong tokens,
embedded NULs, missing/extra values, nonfinite values, integer overflow/fractional
syntax and invalid control/allocation configurations reject.

Order:

1. Literal `SLAAPS_STABILIZED_OBSERVER_V1`; MAVLink instance, system and component.
2. Seven positive integer microsecond limits: manual age, state age, state skew,
   minimum cycle, maximum cycle, estimator/status age, permission age.
3. Quaternion norm tolerance, centered-enter and centered-exit thresholds.
4. Eleven body-axis triples: stick sign, maximum commanded rate, setpoint slew,
   maximum measured rate, maximum measured angular acceleration, attitude gain,
   rate P/I/D gains, integrator limit and maximum normalized moment.
5. Twelve effectiveness coefficients in row-major order, then four entries each
   for trim, minimum, maximum, positive slew and negative slew.
6. Three axis weights; regularization, minimum row norm, minimum independence
   and saturation tolerance.

Thrust is fixed to zero by this adapter. This diagnostic profile contains no
physical calibration or approval flag. The test profile is explicitly synthetic
and is not an installed-vehicle configuration.

## Verification and artifacts

Final host/native evidence:
`/var/tmp/research-stabilized-observer-e8ea-20261019`.
`observer-verification-manifest.json` SHA-256:
`c7eaf8171b937254c1047df9a6719d3ed6177b27e7c98b1bea35754d8cc51a66`.
Independent Windows-side rehashing verifies all 156 host/native receipt files
and all three staged ARM artifacts with zero mismatches.

- Debug, Release and ASan/UBSan each pass the original 123,083 control checks
  plus 193 observer-profile/zero-thrust checks, using the final portable sources.
- Final native test `observation-dm9expwc` passes 9,015 checks, with actual PX4
  module scheduling and uORB inputs. It covers Ready without automatic entry,
  unavailable output enable, rate/hold/release, explicit disable, stop/restart,
  attitude reset, gyro-source change, kill, status fault, manual loss, future
  input and unsupported estimator configuration. No actuator publication is
  observed. Its inputs are test publishers; this is not an EKF/physics campaign.
- Actual module CMake rejects the combined bench/observer build. Native and ARM
  symbol checks exclude the bench command; the ARM image also excludes the
  native input-test command and Gazebo stabilized adapter.
- The first native assertions sampled a NotDue diagnostic as if every record
  contained a candidate. The revised verifier requires a successful calculation
  within the current observation interval and checks the current state separately.
  Failed captures and launcher/compiler failures are retained; no controller
  threshold was loosened.

ARM staging:
`build/stabilized_observer/e8ea-20261019/`.
The package is
`artifacts/px4_fmu-v6x_default_e8ea98b404_dirty_observer.px4`, SHA-256
`4b3e706ab4130461e1085134d1dca4813ec168c5948c9209caec9f5d36362226`.
The package is 1,857,767 bytes. `observer-arm-receipt.json` SHA-256 is
`3f841d1fff9fd263296874d4ede40bce8794bdabf15e30bc8e4baf53062ee507`.
Build exit 0, board 53, source guards before/after build, linked core/allocator,
decoded image/bin equality and pinned PX4/NuttX versions pass. Docker image is
`px4io/px4-dev:v1.17.0`, with ARM GCC 13.2.1. Source is a dirty isolated overlay;
the base commit alone does not identify it.

Image size is **1,961,704 bytes**, leaving **4,376 bytes** in the current flash
slot. Linked text/data/BSS are 1,955,880 / 5,824 / 104,816 bytes. These are whole
image totals, not measured runtime consumption or a guarantee that active output
integration will fit. No boot, hardware stack/timing, PWM or physical acceptance
is claimed for this image.

Reproduction tooling is `Tools/research_sitl/verify_stabilized_observer.py`:
use a new `/var/tmp/research-stabilized-observer-*` directory with `prepare`,
then `host`, `native`, `test`, `seal`. It uses the preserved qualified older bench
source archive/overlay and native dependencies; it never changes that parent.
`refresh` preserves the initial failed ARM inputs for the one diagnosed C-math
portability correction and is unnecessary for fresh preparation. ARM staging
retains explicit Docker argument lists, overlay hashes, preparation/build/export
scripts, logs, compiler/source records and all three firmware artifacts.

## Remaining G9 and physical work

The G9 gate remains incomplete. Next development is an acknowledged actuator
driver integration with reviewed mode permissions and deliberate transition/reset
behavior. The subsequent [portable output contract](STABILIZED_MANUAL_OUTPUT_CONTRACT.md)
now implements software acceptance acknowledgments, independent expiry and
calibrated command-to-PWM candidate mapping, with host and unlinked ARM
verification. It is not connected to this observer or to a physical driver.
Any combined
bench/stabilized firmware needs a real shared ownership protocol; removing the
current build guard is not sufficient. Resolve or explicitly bound publisher
identity and selected-estimator health before authorizing outputs. Budget flash
space and measure scheduler/stack costs before adding the active path.

The current hardware bench remains at its recorded Ready-only subset: profile
byte-integrity evidence, signal instrumentation/captures, installed calibration,
power and wind/load acceptance remain pending. G8 still needs accepted CAD,
canard authority, loaded servo dynamics and operating requirements. Wind-only
restrained stabilization may keep EDF isolated; propulsion adds its own data
and acceptance requirements. G10 physical rate/hold/disturbance/loss tests and
free-flight work remain later steps.
