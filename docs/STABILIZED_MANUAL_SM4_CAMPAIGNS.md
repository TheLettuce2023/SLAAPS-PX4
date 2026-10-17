# Stabilized manual SM4: estimated-state campaigns

**Runtime-profile erratum, 2026-10-05:** the intended bias overrides were absent
from these startup scripts. Numeric results remain intact; matched-prior and
prior-induced-improvement claims below are withdrawn. Behavior belongs to the
actual startup/default profile. See the [configuration defect and requalification](STABILIZED_MANUAL_SM4_RUNTIME_PROFILE_FIX.md).

2026-10-05, Codex. Follows the [SM3 connection smoke](STABILIZED_MANUAL_SM3_SITL_INTEGRATION.md)
and the [confirmed manual-mode objective](STABILIZED_MANUAL_SITL_GOAL.md).
Exact result: **12 estimated-state rate/release/impulse cases and six
fault-and-explicit-recovery cases PASS at declared synthetic scope**.
This record distinguishes incomplete runs from subsequent profile verification.
The vehicle plant, flight-mode admission and hardware acceptance remain open.

## Implemented connection and supervisor

The private POSIX external module uses PX4's selected manual input and normal
estimated attitude/filtered body rates. One owner generates limited body rates,
captures a full quaternion once on centered release, runs the existing PX4 rate
PID and allocates four virtual canards with box/slew limits and directional
residual feedback. Simulator truth is never a control input.

Commands have an independent 100 ms consumer lease. Every active cycle checks
single-EKF/manual-source settings and unassigned stock Gazebo output functions.
Commander permission freshness is 600 ms (2 Hz publication plus the fixture's
100 ms budget). Invalid state, input/source loss, permission loss, a competing
output assignment or a scheduling gap revokes the session. CLI enable now
returns an error while Fault is latched; readiness is cleared on revoke.
Recovery requires a deliberate owner stop/start and a newer consumer session.
Logs include raw selected commands/timestamps, captured reference, estimated
state, requested/bounded/achieved moments, integrals, residuals, saturation,
virtual surface targets/states and supervisor events.

This remains a restrained rotational fixture at prescribed 20 m/s flow and
10 N enabled thrust. It is not a free-flight plant, aerodynamic authority
model or qualified vehicle-health supervisor. Epoch 1 and bound device IDs
do not establish detection of every fast same-ID estimator restart. Actual
estimator-reset/candidate-switch and broader transport-mutation campaigns remain
separate work. The tested estimator-loss case stops EKF for 300 ms.

## Magnetic sensor protocol correction

Gazebo 8.15 supports explicit corrected sensor output: ENU earth field, FLU
sensor axes and tesla units. The fixture requests both legacy options false.
The checkout's stock GZBridge uses a legacy reflected body mapping, so a private
relay converts the actual Gazebo sensor packet into that bridge's expected
compatibility packet. Decoding through the unchanged bridge yields physical
FRD gauss: `(raw_x, -raw_y, -raw_z) * 10000`.

The relay consumes only the magnetometer packet, including its original sample
timestamp. It has no estimated-attitude or truth input. Its separate magnetic
CSV permits scoring the measured field's rigidity in the true world frame.
Gazebo's upstream [correction](https://github.com/gazebosim/gz-sim/pull/2460)
and the official [PX4 bridge fix](https://github.com/PX4/PX4-Autopilot/pull/28877)
document the legacy frame/unit incompatibility. No stock bridge, baseline
world, EKF source or original CAD was changed.

## Retained incomplete run

Evidence `/var/tmp/research-stabilized-sitl-edg6vbe0`, manifest SHA-256
`6f0150500d522026de761d9d1e4dcfa68bd096e3697569853dc18774ae822c16`.
The manifest explicitly records `accepted: false`. All 574 artifact hashes
were independently rechecked with zero failures. The audit covers 38,663
published cycles and preserves 299 checkpoint anchors; the sole intended
checkpoint change was the test harness's explicit recovery extension.

Eleven of twelve level/body-X-up, three-axis, both-sign rate/release/impulse
cases pass. The latest nose-up body-Z negative case
`rate-up-2--1-ev2ge3yf` fails physical hold error: **0.0200047373 rad**, above
the unchanged **0.02 rad** limit. Its estimated error is 0.000884258 rad and
final physical rate 0.000632368 rad/s. Reference capture/retention, tracking,
impulse, peak-error, magnetic rigidity, thrust and consumer disable checks pass.
The physical error must not be rounded down or replaced with an older pass.

All six fault-and-explicit-recovery cases pass: selected input loss, wrong
MAVLink source, EKF loss, Commander permission loss, competing output routing
and stopped producer. Enable returns 255 while Fault remains latched. Each
deliberate restart obtains a newer session, holds for one simulated second
and disables to zero consumer thrust. With PX4 suspended while Gazebo runs,
the consumer expires exactly 100,000 us after the last command timestamp.

Earlier `/var/tmp/research-stabilized-sitl-dxsc2alo` retains the uncorrected
magnetic transport attempts and initial CSV-observer defect. It is exploratory,
not accepted or substituted into the corrected run.

## Estimator diagnosis and declared synthetic prior

The failed case's estimated-versus-physical orientation offset changes about
0.0202 rad after release. This is consistent with an estimator-frame change
rather than a recaptured reference. Read-only `listener` diagnostics show
on-ground heading fusion (`cs_mag_hdg=true`, `cs_mag_3d=false`) and initial
body-X gyro-bias variance near 0.01 (rad/s)^2. During rotation the estimated
body-X bias changes despite the fixture's known zero-bias IMU model.

Exploratory nose-up body-Z negative runs retain exact firmware/plugin identity:

| Evidence suffix | Declared EKF setup | Final physical error, rad |
| --- | --- | --- |
| `diagnostic-db2e06_5` | Stock initial bias priors, magnetic AUTO | 0.004852979 |
| `diagnostic-gvricpvw` | Stock priors, magnetic INIT only | 0.016590134 |
| `diagnostic-edoc5j8v` | Explicit synthetic priors, magnetic AUTO | 0.000766186 |

These are diagnostics with extra observation calls, not acceptance replacements.
One successful repeat cannot close the retained failing matrix.

The revised campaign profile declares `EKF2_GBIAS_INIT=0.003394 rad/s` and
`EKF2_ABIAS_INIT=0.04 m/s^2`: ten times the known synthetic white-noise
standard deviations, with zero simulated switch-on bias. It retains stock
AUTO magnetic fusion, process noise, all controller gains, command/lease
limits and scoring thresholds. This prior is a modelling input for the
zero-bias synthetic sensor, not installed Pixhawk calibration or a recommended
physical default. Real sensor bias, latency, mounting and confidence admission
still require evidence.

## Quantitative campaign contract

Each positive case initializes level or body-X-up, enables centered hold,
commands one body-axis rate for two seconds, releases for one second, then
applies a physical body impulse and settles for seven seconds before disable.
The +/-0.4 raw stick command yields +/-0.375 rad/s after the declared deadzone.
Impulses 0.024/0.064/0.064 N m s nominally produce 0.8 rad/s on the selected axis.
The fixed seed is 42; initialization and transport scheduling remain asynchronous.

Tracking error must stay below 0.08 rad/s over the final 300 ms of Rate.
Quaternion capture error is below 0.00002 rad and reference change below
0.000001 rad. Final physical and estimated hold errors are each below 0.02 rad;
physical rate is below 0.02 rad/s, peak post-release physical error below
0.5 rad, and measured magnetic world-direction change below one degree.
Absolute initial magnetic heading offset is not an acceptance metric: physical
hold is scored relative to the physical attitude at the captured estimate's
sample time. Both estimated and physical errors are reported.

## Final verified profile and evidence

The full revised-profile matrix and six fault/recovery cases pass in fresh
evidence `/var/tmp/research-stabilized-sitl-r27tx0nr`. Manifest SHA-256:
`d60c0465e69c7838b76d59cb14fbf039544338dee53122b80a5e549afa8cbac1`.
The sealer audits **38,617 published cycles** and preserves **302 frozen
checkpoint anchors**, with no exceptions for changed campaign inputs.
All **546 sealed artifact hashes** were independently rechecked using Windows
SHA-256 with zero failures. Compiled inputs are unchanged: native ELF SHA-256
`5db3ecab94ba9b61beee7b9efb267e4bf6aa7cb33f2d2dffef1c6efebecd661e`,
plugin `87db87c34a92a254d8a07e0578e7ef371739b608eec9ea9492ee6a6b55feb638`.
`native-origin.json` binds the verified parent manifest and exact reused
artifacts; it does not claim another fresh firmware build. Each new case
archives the new runner/profile, runtime sources, binary identity and actual
startup script/world. This closes the declared 18-case synthetic campaign
checkpoint; broader SM4 state/reset/switch/mutation coverage remains open.

| Maximum across the twelve positive cases | Measured | Limit |
| --- | --- | --- |
| Final physical hold error | 0.006600963 rad | 0.02 rad |
| Final estimated hold error | 0.001514855 rad | 0.02 rad |
| Late rate-tracking error | 0.046566725 rad/s | 0.08 rad/s |
| Final physical rate magnitude | 0.004810093 rad/s | 0.02 rad/s |
| Post-release physical error | 0.167474734 rad | 0.5 rad |
| Measured magnetic world-direction change | 0.068686692 degrees | 1 degree |

The previously failing nose-up body-Z negative scenario now measures
0.004603679 rad physical hold error under the declared revised profile. All
six faults latch with readiness zero and reject enable with return code 255.
Explicit restarts hold and disable successfully with strictly newer sessions.
The final producer-stall case expires at exactly 100,000 us. These are one
seeded matrix and six fault runs, not statistical noise/bias/latency robustness
or a qualification of installed sensors.

## Continuing work and human inputs

[Follow-on native state-barrier verification](STABILIZED_MANUAL_SM4_STATE_BARRIERS.md)
passes eight level/nose-up reset, publisher-withdrawal, rapid same-ID restart
and unsupported multi-estimator-profile cases with explicit recovery. This
does not qualify general publisher-epoch/candidate identity or sensor-diverse
failover. Next independent software work is that broader qualification, larger
sensor-bias/delay/transport perturbations and repeatability. Preserve the sealed
18-case profile and its incomplete predecessor when preparing those experiments.

No new human input is required for these synthetic software campaigns.
Vehicle-supported tuning requires already-pending valid mass/CG/full inertia,
airflow-dependent canard effectiveness and installation signs, loaded servo
calibration/dynamics, measured EDF thrust/lag/wash, physical limits and accepted
loss/recovery behavior. H10 bench, stock Cessna, negative-only C1E-N1 and all
physical gates retain their scopes. No commit, staging, flashing or physical
operation is part of this milestone.

## Reproduction tools

Run in WSL with Gazebo Sim 8.15, normal PX4 lockstep build dependencies and
`pymavlink`. No GUI, flight controller or physical output is used. The baseline
runner prepares/builds a private source copy; the campaign runner can also
prepare a new parameter campaign using an independently verified unchanged
native artifact set:

```text
python3 Tools/research_sitl/verify_stabilized_sitl.py prepare
python3 Tools/research_sitl/verify_stabilized_sitl.py build --evidence <prepared-directory>
python3 Tools/research_sitl/verify_stabilized_campaigns.py --evidence <prepared-directory>
python3 Tools/research_sitl/verify_stabilized_campaigns.py --evidence <prepared-directory> --case failures
python3 Tools/research_sitl/seal_stabilized_campaigns.py --evidence <prepared-directory>
```

The sealer chooses the latest applicable attempt for each scenario, including
failed attempts. It rejects missing coverage, failed latest cases, exploratory
results, changed mapped compiled/profile inputs and changed checkpoint anchors. Its
explicit `--incomplete` mode records failure without granting acceptance.
Normal sealing of the retained `edg6vbe0` matrix was checked to reject its
failed latest nose-up case before writing anything.
