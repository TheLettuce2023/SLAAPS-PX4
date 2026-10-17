# Stabilized manual SM3: estimated-state integration smoke

2026-10-05, Codex. Follows the [host milestone](STABILIZED_MANUAL_SM2_SM3_VERIFICATION.md)
and [confirmed inputs](STABILIZED_MANUAL_SITL_GOAL.md). Exact result: **fresh
native PX4/Gazebo linkage, three-axis physical fixture check, and centered
estimated-state hold/disable smoke PASS**. Full stabilization/supervisor
campaigns remain open.

## Connection and scope

`Tools/research_sitl/stabilized_manual/` is a private external-module profile,
with no checkout Kconfig, board-default or startup registration. Its new native
source copy links the controller library and `research_stabilized_sitl`; neither
bench-owner nor bench-active entry point is linked. Stock flight controllers
are not started. All 16 Gazebo ESC, 8 servo and 4 wheel output functions must
be unassigned at module startup.

One work-queue owner snapshots selected manual input, normal `vehicle_attitude`,
`vehicle_angular_velocity`, selected IMU IDs, instance-zero EKF status/flags and
armed state before reading its clock. Feedback follows Gazebo sensors -> stock
GZBridge -> PX4 sensor processing -> EKF/filtered rates. The owner never consumes
truth. Centered enable binds IMU IDs; observed device/timestamp/reset changes,
rejected samples, disable and stop revoke. Re-entry requires a new module
lifecycle. The private profile requests a single EKF and MAVLink tuple 0/255:190.

Admission requires disarmed, non-killed/non-terminated state, aligned tilt/yaw
and matching selected estimator/IMU IDs. These are limited fixture checks,
not a qualified vehicle-health or flight-mode supervisor. Epoch 1 denotes one
explicit process/session; same-ID fast estimator restart detection is unqualified.
Recorded preflight compass/system-power warnings remain. No arming check was
silenced and no flight acceptance follows.

Only `/model/slaaps_fixture/stabilized_command` receives outputs. Its nine-value
`gz.msgs.Double_V` frame carries integer timestamp/session/lease, enabled flag,
fixed thrust and four normalized commands. The adapter is guarded for POSIX
test-only compilation and an explicit fixture name. Gazebo validates frames and
reuses the unchanged H8/H10 consumer lease. Disable/invalid/expiry targets neutral
surfaces and zero synthetic thrust; servo states decay through lag. Actual
producer-stall expiry over this new transport still needs a runtime campaign.

## Declared synthetic plant and timing

A separate SDF rigid body has a ball joint at CG: three rotational degrees of
freedom, translation restrained. Physics receives wrenches, not pose overwrites.
Virtual canards use first-order states, without physical hinge/joint aerodynamics.
Normalized states correspond to a declared +/-13 degree virtual angle range.
The original invalid SLAAPS model/CAD intake and stock Cessna/submodule remain
unchanged.

Inertia [0.03, 0.08, 0.08] kg m², damping [0.03, 0.04, 0.04] Nm s, frozen
full-rank effectiveness, 0.4 Nm moment scale, 50 ms actuator lag, prescribed
20 m/s body-local flow and fixed 10 N body +X thrust at CG match the host fixture.
The restraint reacts the force. No aerodynamic coefficients, EDF wash or rotor
reaction torque are computed.

The asynchronous SITL profile explicitly uses 100 ms state age, 50 ms sample
skew and 4–60 ms cycles, versus host 20 ms / 8 ms / 4–12 ms. Manual age and
consumer lease are 100 ms. EKF metadata age is 1.1 s: approximately 1 s flags
publication plus the 100 ms fixture budget. Fast attitude/rate limits remain
separate. All are synthetic test budgets, not hardware requirements.

SDF white Gaussian noise standard deviations: gyro 0.0003394 rad/s,
acceleration 0.004 m/s², magnetometer 0.0001 in Gazebo's noise convention,
pressure 0.01 Pa. GNSS retains Gazebo's noise-free default. These fixture values
do not qualify installed sensors.

## Evidence and limits

Evidence `/var/tmp/research-stabilized-sitl-6ayynqfq`; manifest SHA-256
`69d9aa65a7931c7273425180a043e42e2407512f7428bbb645d0160744ef5970` binds final
inputs, native ELF/plugin, configuration, compile commands, logs and retained
smoke/physics cases. 286 unrelated mapped anchors are preserved; only the new
core's two C++ files and CMake entry changed for PX4 float-warning compatibility.
No tracked code change was added.

- Strict plugin and actual PX4 builds pass, required owner/allocation symbols
  link, bench entry points are absent. Final native text/data/BSS:
  5,046,852 / 133,685 / 36,064 bytes; no runtime resource qualification.
- `physics-cg90_6l0`: peak FRD rates 0.8 / 0.8 / 0.781069 rad/s under impulses,
  zero recorded translation error. This checks degrees of freedom, not feedback.
- `smoke-uht2lbgi`: 1,257 Hold cycles at 9,192,000–14,216,000 us; status 3/1/0
  (Hold/ready/fault), constant 10 N thrust. Disable yields consumer thrust zero
  and latched lease fault.
- Asserted final repeat `smoke-y2zltvcj`: 1,255 Hold cycles at
  9,160,000–14,176,000 us; eligibility, constant thrust and consumer disable pass.
  Neither smoke includes a disturbance or rate-stick command.
- Refreshed Debug/Release/ASan-UBSan each pass 123,083 host checks after explicit
  double conversions and exact-sign validation changes. Evidence
  `/var/tmp/research-stabilized-host-pue0bgy9`, manifest
  `eea5cc3083c413893346d4e80e3f666fd58cdd00db14578b60a8b35a976f6dfe`.

Failed attempts remain recorded: bench-derived no-lockstep build used host
time and stale Gazebo sensor times; final build restores unchanged normal
lockstep defaults. Repeated noiseless values triggered stock stuck-data
rejection; explicit noise restored admission. The first enabled smoke revoked
at the initial metadata expiry boundary; the final guard accounts for actual
EKF publication cadence. Earlier source/build warnings were corrected before
final verification. These failures are not positive acceptance evidence.

## Next work and human inputs

Run source/config-bound level and body +X-up campaigns on all three axes:
pilot-rate tracking, reference capture, controlled disturbance rejection and
quantitative settling/error/rate limits. Extend logs to bind raw manual
timestamps/axes and all supervisor transitions. Exercise source/input loss,
stale/reset state, re-entry, competing output assignments and producer stall
through the real sink; verify independent consumer expiry. SM4 remains open.

No new human input is needed for this synthetic work. Vehicle tuning and
operational integration require already-pending valid CAD mass/CG/full inertia,
airflow-dependent canard effectiveness/installation signs, loaded servo
calibration/dynamics, measured EDF thrust/lag/wash, physical limits and accepted
loss/recovery behavior. Hardware actions remain outside this milestone. H10
and negative-only C1E-N1 retain their scopes.
