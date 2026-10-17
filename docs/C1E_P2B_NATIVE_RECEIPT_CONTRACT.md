# P2B native EKF output-boundary observation contract

2026-09-27, Codex (GPT-6). Separate closed observation profile; not positive
admission, an accepted flight estimator or the full P2 gate. The accepted N1
profile is unchanged. No admission adapter, Commander, control module, output
driver, MAVLink or simulator bridge is selected in this profile.

## Commit boundary and source scope

A private source view adds one synchronous value-copy hook immediately before
`EKF2::PublishAttitude(now)` in the IMU-updated branch of `EKF2::Run`. This point
follows the attempted `_ekf.update()` and its successful-update publications.
The hook executes for every IMU-updated iteration, including false attitude
validity and an update that produces no attitude publication. It records the
actual attitude/local-position validity predicates and control/fault bitfields
from that EKF instance, with input sample time and fresh native hrt.

This is deliberately **committed output-boundary observation**, not a claim to
observe every internal filter mutation. P1's eventual live authority contract
must choose and review its admissible commit boundary explicitly. These records
cannot certify the earlier publications in the same iteration atomically, or
prove no intermediate control/fault transition occurred inside `_ekf.update()`.
No readiness Boolean, state generation, operator grant or clock map is derived.

Compile-time single-instance EKF, normal time mode and one immutable producer
address are required. Runtime replay/multi flags or a different instance fault
the collector. The address is used only internally for continuity checks and
is not exported as identity. A fresh random nonzero run identity binds export;
there is no source restart/rebind/reset operation in one process. Repeated
bootstrap/feed/export fails. Source sequence is the checked archive count + 1.
Stop and freeze end the observation window; later callbacks cannot append.

## Storage, ownership and export

2048 fixed global records and 1200 fixed input records are diagnostic resource
bounds, not timing/health budgets. Source path is allocation-free and does no
I/O. An atomic writer flag rejects concurrent writers without waiting. A
sequentially consistent active flag and in-flight counter allow export to
freeze first, then wait outside the source path for an already-entered copy.
An entrant after freeze sees inactive and never accesses record bytes. The
writer flag serializes ordinary archive access; export reads only after freeze
and in-flight zero. Required atomics are compile-time checked lock-free.

Capacity exhaustion, instance/profile contradiction, sample/observation time
regression, invalid time and concurrent writing preserve the first fault and
deactivate recording. Prior records remain intact. No overflow overwrites a
record. Export preserves the fault even when observations exist. Export waits
at most 1000 one-ms sleep iterations and reports failure on timeout; this is
a cleanup bound, not measured source latency. A sink error prevents completion.
Exclusive no-follow output creation refuses to replace an existing export.
The source hook's counter cannot approach unsigned overflow in the supported
single-instance finite process; counter wrap is not a supported lifetime.

The isolated shell executes load, EKF start, finite feed, EKF stop, freeze/export
in order. In-flight accounting also protects export if source shutdown has not
finished; it does not prove scheduler teardown by itself. The runner owns only
its new process group and records normal versus forced shutdown. A forced or
partial run cannot be accepted as complete evidence.

## Recorded sensor stimulus

The fixture uses the first 1200 `sensor_combined` samples from the preserved
headless baseline ULog, whose independently retained SHA-256 is required.
Gyro/accelerometer values, integration durations, clipping and calibration
counters are preserved through float32-roundtripping text. Relative accel time
must be zero and both integration durations 4000 us. No fake validity flag,
quaternion, navigation solution or simulator truth enters the EKF.

Publication timestamps are replaced with actual native hrt and nominal releases
are paced at 4000 us. Scheduling jitter, missed subscription updates and lack
of external aiding are explicit experiment limitations. This is a recorded
sensor stimulus to the real EKF in normal time mode, not current sensor evidence,
EKF replay mode or a qualified simulation of elapsed physical dynamics. No
claim of estimator health, calibrated sensor identity or viable flight follows.
The record count need not equal input count; differences must be retained,
never filled with invented source observations.

## Build and evidence gate

The runner checks original EKF/CMake hashes before making two private copies;
all original sources are preserved. The exact hook location must occur once.
Only the private copies add the hook include/call and private collector linkage.
The default research library and N1 owner are not linked. Record source hashes,
derived-source hashes, board configuration, compiler commands, builtin allowlist,
executable, startup, stimulus, run identity, stdout, raw observations and final
independent checker output. Review stack-use output for the source callback.

The independent checker must validate exact schema/types, contiguous source
sequence, time ordering, flags, no authority, input/record bounds, actual source
identity and successful closure. Report observed state changes and gaps, without
creating a freshness threshold or replacing source predicates with desired ones.
Source hash drift during build/run makes evidence incomplete.

## Remaining boundary before positive diagnostics

These records can support review of actual source seams and collector mechanics.
P2 still needs the reviewed committed-state definition, real selected-estimator
and sensor lifetimes, freshness/continuity/health budgets, ordered owner merge,
operator/transport receipts, and stale/restart/terminal behavior. A source-bound
common-clock proof is separate from approval to install a map. Independent
P1/P2 review and a distinct positive wire/profile remain prerequisites.
Original CAD evidence remains mandatory for physical plant/control acceptance.
