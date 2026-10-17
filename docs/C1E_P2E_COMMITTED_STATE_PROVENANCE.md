# P2E committed estimator output, reset and sensor provenance

2026-09-28, Codex (GPT-5). Closed native observation increment accepted at
the bounded evidence scope below. It does not qualify estimator health, positive
admission, an acceleration controller, or any output. C1E-N1 remains the only
accepted runtime admission profile and remains negative-only.

## Boundary advanced

P2E extends the accepted P2D private non-lockstep POSIX source view. It observes
the actual Boolean returned by `Ekf::update()`, the `vehicle_local_position`
payload formed only on a successful update, the `vehicle_attitude` payload
formed only when `attitude_valid()` is true, five native reset counters, the
current control/fault flags, and EKF2's bound accelerometer/gyroscope device and
calibration identities. The P2D raw and typed hooks remain present and provide
an independent row-by-row comparison.

The observed local-position acceleration is PX4's NED acceleration of its body
origin output. It is retained as publication evidence only. It is not the
vehicle's commanded inertial acceleration at the CG in rotating body FRD, does
not perform a CG/IMU lever-arm correction, and is not connected to H5 or an
actuator path. The companion contract remains acceleration-only at the CG in
body FRD; NED remains the navigation frame.

`CommittedState` owns one fixed 2,048-record archive and a single pending
publication bundle. Constructor, output, iteration and destructor hooks are
allocation-free and guarded by lock-free source/lifecycle bookkeeping. An
iteration is retained only when publication presence, sample time, validity and
reset counters agree with the actual successful-update predicate. The first
fault is sticky, recording closes, and no later event repairs it. A second EKF2
constructor in the same owner is rejected. Export occurs only after explicit
stop intent and observed destructor entry.

The normal fixture publishes the exact `sensor_selection` device identities
from the hash-pinned headless baseline ULog before replaying its existing 1,200
`sensor_combined` values. The retained identity is `1310988` for both accel and
gyro, with recorded calibration counters 0 and 1 respectively. These are
recorded SITL sensor facts, not hardware qualification or invented physical
values. P2E does not authorize device substitution, selector/multi-instance
operation, replay mode, or groundtruth.

## Independent verification

Final package: `/var/tmp/p2e-committed-zs4snisq` in Ubuntu-24.04 WSL. Manifest
SHA-256:
`796c31ce4d8476a429475be645c792e4dffb72eafbd873b783762338792f49c4`.
Independent check SHA-256:
`83dd71d5fa81abf2fb868e7df255ade67aad156ad5fd52860fe7f4b566e897b3`.

The fresh Debug profile compiled 439 targets. Normal and restart processes both
closed without forced termination and retained 1,200 P2D/P2E rows. Each run
observed 600 successful EKF commits and one reset-counter transition. Every P2E
sample, control/fault value and applicable validity predicate matches P2D; every
successful update has its matching local-position payload. The second
constructor is rejected in the restart case. All 1,200 attitude predicates are
false, so there are zero attitude publications and therefore zero admissible
health bundles. `health_qualified`, `positive_admission` and
`eligible_for_control` are false in every artifact.

Seven focused checker tests pass: the valid bound trace, plus rejection of
invented authority, sensor rebinding, missing committed payload, reset
rebinding, P2D payload mismatch and an attitude payload when none was published.
The first test run exposed only a harness exception-type mismatch: the inherited
checker raises `ValueError`, while the harness expected `RuntimeError`. The
corruptions were already rejected with the intended reasons; the corrected
harness then passed 7/7.

Fresh optimized and ASan/UBSan P2E builds also pass normal and restart native
cases with no sanitizer diagnostics. All four validation processes retain 1,200
observations and 600 commits. A final concurrency review found that destructor
entry inspected the pending bundle without the source writer guard. The
framework normally serializes these callbacks, but P2E now rejects unexpected
overlap with sticky `ConcurrentSource` failure instead of permitting a C++ data
race. All final evidence was regenerated after that fix. Earlier packages remain
historical development evidence only.
The four-run validation record SHA-256 is
`aafa4adaf14563f3f1a4813e910cdc421f6b5e29a0af60140ec15e1d6101491a`.

The Debug P2E object contains 11,538 bytes text, 8 bytes data and 327,909 bytes
BSS. Its fixed record array is 327,680 bytes. Compiler self-frame reports are
64 bytes for attitude, local-position and iteration hooks, 128 bytes for source
start, 160 bytes for source end, and 416 dynamic/bounded bytes for export. These
are diagnostic POSIX object/resource measurements, not embedded acceptance or
peak-stack proof. Object SHA-256 is
`659dd5a5dcbe9ae9e7e5908fdb8dfcd388e4fa515aade1584c6865bde19e8f65`;
stack report SHA-256 is
`9da0b1e40554b55ebf7969ee35d19345044c46f1d468d299ec440150dd7e49a6`.

## Compilation versus firmware evidence

Strict H1-H5 Debug, Release and ASan/UBSan regression remains 1,399 checks per
configuration. A fresh native source view at
`/var/tmp/research-acceleration-p2e/px4-source` produced complete default SITL
and Pixhawk 6X builds. ARM GCC 13.2.1 completed the board compile and link.

The default SITL and Pixhawk ELFs contain zero P2E or
`research_actuator_command` symbols. Thus these builds prove that the surrounding
PX4 source still compiles; they do not measure linked P2E/H1-H5 firmware cost.
The complete Pixhawk image reports 1,947,428 bytes of flash (99.05% of 1,920
KiB). Its artifacts are linked default-firmware resource evidence that excludes
all research code in this increment:

- SITL ELF, 58,599,328 bytes:
  `852e75fb6b6d44ead498d8134dce57891d961468ba5fe1613fcdb32c919fa1dc`
- Pixhawk ELF, 49,391,532 bytes:
  `9c2008f3145818def83db211bfcab51fdad3a22b8f24b5ca6de8d8bcff1f568b`
- Pixhawk BIN, 1,947,428 bytes:
  `abfd70d2720dd44ec17565d80bf94e59d8b289d485061fa5dfdd24b4be404a65`
- Pixhawk PX4 package, 1,853,994 bytes:
  `7c41a047bd6d37788efdb3a5948731f5ae5d0e2cdbada2e80a6a9a49746291f6`

## Remaining gates

P2E identifies actual successful commits, reset changes, publication presence
and recorded sensor bindings; it does not define or release an estimator-health
predicate. The retained run has no valid attitude. Exact finite-state/health
masks, selected-instance/selector lifetime, complete mutation-source coverage,
real deployment sensor qualification, freshness/skew/latency budgets and a
qualified ready-state owner remain open. P3 legacy sample-clock/provider/map
continuity and P4 operator/transport capability plus a distinct positive
protocol also remain open. No startup, airframe, arming, uORB control publisher,
actuator output, fallback, physical coefficient or operational default was
added. No flight readiness follows.
