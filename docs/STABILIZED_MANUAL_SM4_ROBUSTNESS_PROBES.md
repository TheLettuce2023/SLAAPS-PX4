# SM4 synthetic noise and gyro-bias probes

2026-10-05, Codex. Goal active. This record extends the
[31-case corrected-profile checkpoint](STABILIZED_MANUAL_SM4_RUNTIME_PROFILE_FIX.md)
with five **exploratory observations**. It does not enlarge that checkpoint's
accepted campaign or qualify an installed vehicle.

The exact previously qualified native PX4 ELF
`a2c8229b30a93434f6d6aae3170fc67f354227e5ce236eca83720d7b79397f7d`
and Gazebo plugin
`795a826f771dfd5cfe3b56c719f0f036f897184457fbe4c132b917d4c5c2f044`
were reused. The fixture remains the declared 20 m/s body-local airflow,
10 N fixed thrust at the CG, synthetic canard moments and constrained three-axis
rotation. Stock GNSS bridge noise and estimator aiding remain active. PX4's
actual gyro/accel initial bias priors, 0.0003394 rad/s and 0.04 m/s², plus
`EKF2_MAG_TYPE=0` were compared live before each enable.

The original nose-up, negative-first full-axis case passed with seed 42 in the
accepted checkpoint. The same maneuver also passed with Gazebo seeds 43 and 44.
A diagnostic then injected a constant ±0.0010182 rad/s mean on *each Gazebo gyro
axis* (three times the declared initial gyro-bias sigma); it retained the same
PX4 prior and passed at seed 42 for each sign. The positive-bias case also
passed at seed 43. This is a deliberately perturbed synthetic sensor, not a
calibration claim about the Pixhawk installation.

| Seed | Gazebo gyro mean, rad/s | Audited cycles | Final physical hold error, rad | Peak, rad |
| ---: | ---: | ---: | ---: | ---: |
| 43 | 0 | 6,455 | 0.005148 | 0.490893 |
| 44 | 0 | 6,455 | 0.001957 | 0.490857 |
| 42 | +0.0010182 | 6,502 | 0.019261 | 0.491215 |
| 42 | -0.0010182 | 6,487 | 0.019376 | 0.490506 |
| 43 | +0.0010182 | 6,471 | 0.013129 | 0.491170 |

The existing limits remain 0.02 rad final physical hold error, 0.5 rad peak,
0.02 rad/s final physical rate and 0.08 rad/s tracking. The closest biased case
has only **0.000624 rad** final-error margin. Five passing observations do not
establish a uniform bias/noise envelope.

Each biased case has a closed standard ULog with zero logger dropouts. An
offline truth-versus-`sensor_gyro` comparison measures mean body-rate residuals
near the injected magnitude: positive Gazebo means map to roughly
`[+0.00101, -0.00102, -0.00102]` rad/s in body FRD, and negative means reverse
that sign. The audit checks the generated world, actual ULog parameter values,
logged sensor means, control cycles, source copies, binary hashes and
physical scores. The two additional seeds have no continuous ULog and make no
claim about raw-sensor residuals beyond the declared fixture input.

Evidence: `/var/tmp/research-stabilized-sitl-probes-6wu2ah41/manifest.json`,
SHA-256 `9bf864c0f4db9f97dfedc018161abac624e57765d2fe1fce18a410251b58f0de`.
Its 219 referenced files were independently rehashed from Windows with zero
failures; the five cases total 32,370 audited control cycles. The manifest
points to the exact closed directories and archived source versions. The seed
43/44 observations predate the explicit `exploratory_only` metadata flag; their
non-default seeds classify them as probes. The coupled acceptance sealer now
requires the declared seed 42, while the runner labels later seed changes
exploratory. Existing 31-case receipts are preserved unchanged.

Next software work is unequal-sign/magnitude pilot inputs, sensor latency and
transport perturbations, and broader publisher/candidate epoch identity.
Vehicle-supported authority and tuning still require the pending CAD, measured
canard effectiveness/hinge signs, loaded servo behavior and EDF data. No
hardware output, baseline flight mode, H10 bench gate or C1E-N1 positive
admission change follows from these probes.
