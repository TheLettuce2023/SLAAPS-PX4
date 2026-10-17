# SM4 coupled-control checkpoint: incomplete

**Later correction:** these scripts omitted the claimed bias priors. Numeric
results remain, but prior-related descriptions below are superseded by the
[runtime-profile correction](STABILIZED_MANUAL_SM4_RUNTIME_PROFILE_FIX.md).
Continuous tracing now observes four duplicate-dispatch branch executions;
deterministic corrected-profile qualification remains pending.

2026-10-05, Codex. The goal is active. The user confirmed that vehicle
measurements are **not yet available**; CAD is already pending. Synthetic
software work can continue. Vehicle-specific allocation, tuning and deployment
remain dependent on those inputs.

The latest checkout contains an **unaccepted synthetic candidate**. The earlier
[18-case checkpoint](STABILIZED_MANUAL_SM4_CAMPAIGNS.md) and
[eight native state barriers](STABILIZED_MANUAL_SM4_STATE_BARRIERS.md) remain
preserved at their original artifact/profile scope; they do not qualify this
new candidate. No stock startup/default, original CAD/model, physical output,
H10 bench or positive-acceleration gate changed.

## What changed and why

The new native test commands all three body rates at +1 rad/s for eight seconds,
then -1 rad/s for eight seconds, or the reverse. It releases all sticks,
retains the captured quaternion, injects a physical three-axis angular impulse
and scores settling after seven seconds. Coverage is level/body-X-up times both
initial signs. Impulses are +/-[0.024, 0.064, 0.064] N m s. The test requires
actual canard magnitude saturation, directional residual feedback on every axis
and no integrator growth into the previous cycle's blocked direction.

The original synthetic integral gains [0.05, 0.1, 0.1] left full-axis tracking
errors of 0.1078 and 0.1105 rad/s, exceeding the existing 0.08 rad/s limit.
The candidate derives integral gains approximately [0.17716, 0.32688, 0.32688]
from the declared rigid-body/servo-lag scalar characteristic, placing its slow
pole at -0.4 s^-1. P, D, integral caps, allocation, reference bounds and test
limits remain unchanged. This scalar derivation does not prove the coupled
nonlinear response and supplies no installed-vehicle gains.

The declared static ENU magnetic field now matches the exact compiled PX4
geographic lookup at 47.39797/8.54616 degrees:
[1.2939844968976719e-6, 2.1743289520988562e-5, -4.304112466923969e-5] Tesla.
A standalone probe linked to the tested library reproduces it. This corrects
a geographic-input inconsistency; it uses neither simulator truth nor an
estimated attitude as sensor feedback. Paired exploratory runs suggested lower
drift, but the final matrix below shows that the field correction alone does
not resolve the physical hold discrepancy.

An initial periodic-adapter trial faulted at a repeated simulation timestamp.
The POSIX interval timer advances its intended deadline by its period and can
replay overdue callbacks at one lockstep time. A one-shot scheduling experiment
was discarded after three one-second gaps and one repeated timestamp.
The current private adapter keeps periodic scheduling and skips dispatcher
calls whose time equals the last successful control cycle or enable time.
It publishes nothing and refreshes no lease on that branch. Advancing clocks
still pass through the unchanged core's freshness, regression and 60 ms maximum
cycle checks. **The final matrix observed zero duplicate-dispatch events, so
this branch's native regression coverage remains open.**

## Latest source-bound result

Fresh strict Gazebo plugin and native PX4 builds pass. Scoped AStyle is clean;
Python syntax checks pass. Evidence:
`/var/tmp/research-stabilized-sitl-mm7_zto8`.

| Initial attitude / first sign | Tracking error, rad/s | Final physical hold error, rad | Final estimated error, rad | Result |
| --- | ---: | ---: | ---: | --- |
| Level / + | 0.028649 | 0.015166 | 0.002335 | Pass |
| Level / - | 0.028610 | 0.006466 | 0.002669 | Pass |
| Body-X-up / + | 0.028453 | 0.011852 | 0.003945 | Pass |
| Body-X-up / - | 0.028546 | **0.026301112829139852** | 0.003065 | **Fail: physical hold** |

Unchanged limits: tracking 0.08 rad/s; final physical and estimated attitude
error 0.02 rad; final angular-rate norm 0.02 rad/s; peak physical error 0.5 rad.
The fourth case's 0.026301 rad error is not rounded into acceptance.
All four pass capture/retention, final-rate, peak, magnetic-rigidity,
consumer-disable, magnitude-saturation and directional anti-windup checks.
There are 25,869 audited published cycles, 1,265 box-saturated cycles,
blocked-axis counts [778, 2063, 2064] and zero anti-windup violations.
Worst peak error is 0.496808 rad and worst final-rate norm 0.0037502 rad/s.

The candidate is sealed **incomplete**, `accepted=false`:

- Campaign manifest SHA-256: `1da51d0b2c27f71c71a316c8bad2e437cde7c5fdc11ead2f2eaca3ed5c0deeb2`.
- PX4 ELF: `d581f60212711fbd50d31bd28bb47b90e9529ac79a46c8631b3da24d1fbae44f`.
- Plugin: `f99655e5e9780179f85fc812deae71640335f4f908f734cbd765c60ac50bb466`.
- Private owner source: `1ebddffb6082921f477d3d91caa7571a5be1f074b3bf8e53cf43024938f2fee1`.
- Synthetic profile: `02e5657f44136e5500b273285f1e0035de537417e634d508a2b0c937b661c4ed`.
- Coupled runner: `61b5c1b4b6e741e2ed97a10b4261f88cbe32746a26957e4c2e4010bf69975e21`.

The 120 manifest artifacts are independently rehashed with zero failures,
including closed CSVs, exact tested
artifacts, source inputs, build records, geographic probe and the preparation
checkpoint. No accepted coupled manifest exists. The normal 18-case and eight
state-barrier regressions have **not** been run on this new binary.

## Diagnosis and preserved predecessors

Offline inspection of the failed body-X-up/- case shows the estimate-to-physical
world-frame offset changing by 0.005786, 0.008270 and 0.028419 rad at one, four
and seven seconds after the disturbance. The growing late offset is consistent
with estimator correction/drift; it is not established as a simple fixed delay.
No controller reference recapture or threshold relaxation was introduced.

A baseline with additional estimator observations,
`research-stabilized-sitl-coupled-diagnostic-u_60w6xn`, passes that same pose/sign
at 0.009281 rad physical error. It is explicitly exploratory and cannot replace
the failed source-bound case. Its flags show GNSS position/velocity aiding,
magnetic heading fusion, no in-air/3D magnetic alignment and no gravity-vector
fusion. Stock gravity-vector fusion is disabled while horizontal aiding is
active. Gyro-bias observability currently has a TODO in the unchanged stock
helper. These are investigation anchors, not a proven root cause.

An exploratory `EKF2_IMU_CTRL=4` fixed-zero-IMU-bias run,
`research-stabilized-sitl-coupled-diagnostic-dv8bxkar`, never establishes tilt/yaw
alignment or publishes attitude and fails readiness. It is not adopted.
The diagnostic tool now avoids masking such readiness failures with an empty
rate-log inspection error. Both exploratory inputs/results remain preserved.

Earlier incomplete evidence is also preserved and independently rehashed:

| Directory under /var/tmp | Retained result | Manifest SHA-256 |
| --- | --- | --- |
| research-stabilized-sitl-2ook70ga | Old integral gains fail full-axis tracking; 33 artifacts | aee553028eed950dfaca47d2eebe36f1c7fec8f05b5c6073992a3c2818e9d190 |
| research-stabilized-sitl-4aey1zr7 | New integral gains, original field; physical hold 0.025868 rad fails; 45 artifacts | 150cf12138115040d6cd85ffa5395e7e644d2b83ccc43f852255fbf6063aa556 |
| research-stabilized-sitl-cs1pwpfv | Exact binary reuse, runtime SDF-only field correction; four timing faults; 115 artifacts | 21f73e64cda3b1a74957e671d1b06b7b703ab1e8a30ec0898bd1542afbb84b16 |
| research-stabilized-sitl-5yol3fg3 | Discarded one-shot scheduling; four timing faults; 120 artifacts | 9670524b0291d2ef86e27273f35e28327dc6d968084eb2bc63d282a114972068 |

The earlier paired field diagnostics `...-ulho53yx` and `...-mmaxcbav` also
remain exploratory; their observation timing differs from acceptance runs.

## Next work and human dependencies

1. Trace sensor sample timing and GNSS/magnetic/gravity/bias corrections through
   the failed nose-up full-axis maneuver. Change only evidence-supported fixture
   inputs or actual software defects; keep physical scoring and limits intact.
2. Exercise the native duplicate-dispatch branch deliberately with a bounded
   scheduling backlog. Verify no extra output/lease refresh and preserve genuine
   clock-regression, excessive-delay, input and state faults.
3. Use a new source/config-bound evidence directory for any correction. Require
   all four latest coupled cases, the 12 nominal/six fault cases and eight native
   state barriers on the corrected artifacts before advancing that scope.
4. Continue repeatability and synthetic bias/delay/transport qualification.
   The already-pending CAD, canard geometry/hinge signs/effectiveness versus
   airflow, loaded servo calibration/dynamics and measured EDF thrust/lag/wash
   remain necessary for vehicle-supported allocation and tuning. The user says
   measurements are not available yet; do not request them again until new
   evidence arrives. Installed sensor/link and operational recovery requirements
   remain separate deployment inputs.

No live fixture/build process remains at this checkpoint. The goal remains
active and no physical or positive-acceleration acceptance is granted.
