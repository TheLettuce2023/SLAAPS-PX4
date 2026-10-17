# SM4 unequal-axis pilot-command probe

2026-10-05, Codex. Goal active. Four **exploratory** native SITL maneuvers pass
at a declared synthetic input. This extends the observed pilot-command coverage
of the [31-case accepted corrected-profile checkpoint](STABILIZED_MANUAL_SM4_RUNTIME_PROFILE_FIX.md)
without enlarging that campaign's accepted scope.

The selected manual command uses body FRD stick values `(roll, pitch, yaw) =
(+1.0, -0.7, +0.4)` and then its exact negative, each for eight simulated
seconds. Both command orders run at level and body-X-up poses. The 0.04 stick
deadzone and declared sign map generate target body rates
`(+1.0, -0.6875, +0.375)` rad/s, then their negatives. On release, all sticks
center, the owner captures and retains the estimated quaternion. After one
second, the fixture applies a three-axis physical angular impulse
`(+0.024, -0.064, +0.064)` N·m·s with the first command sign; seven seconds
of hold follow. The manual throttle message stays at its test value while the
fixture's fixed simulated EDF thrust remains 10 N.

The exact qualified ELF
`a2c8229b30a93434f6d6aae3170fc67f354227e5ce236eca83720d7b79397f7d`
and plugin
`795a826f771dfd5cfe3b56c719f0f036f897184457fbe4c132b917d4c5c2f044`
were reused in a new isolated evidence directory. Every case compared actual
PX4 gyro/accel bias priors and magnetic-fusion type before enable. The control
path used selected manual input and real PX4 attitude/rate estimates; Gazebo
truth was used only for physical scoring and the declared impulse.

| Pose | First command sign | Audited cycles | Max physical rate error, rad/s | Final physical hold error, rad | Peak hold error, rad |
| --- | ---: | ---: | ---: | ---: | ---: |
| Level | + | 6,466 | 0.015353 | 0.003256 | 0.299739 |
| Level | - | 6,466 | 0.015560 | 0.003156 | 0.308656 |
| Body-X-up | + | 6,460 | 0.015363 | 0.006917 | 0.300249 |
| Body-X-up | - | 6,459 | 0.015467 | 0.002664 | 0.307879 |

All four pass the unchanged 0.08 rad/s tracking, 0.02 rad final physical and
estimated attitude, 0.02 rad/s final rate and 0.5 rad peak limits. Logged
body-rate setpoints match the pilot/deadzone map within `1e-7` rad/s. Each
case has 137–138 box-saturated control cycles, nonzero directional saturation
feedback on all three axes and zero directional anti-windup violations. The
three-axis impulse appears in the physical truth log, and disable reaches the
consumer.

The independent audit requires the exact four pose/order combinations, closed
source copies, live parameter checks, unique finite control cycles, physical
rate tracking aligned to sensor sample time, fixed hold-reference scoring,
actual impulse, allocator achieved moments and source/binary hashes. Its
manifest is `/var/tmp/research-stabilized-sitl-m2n0a0ev/mixed-manifest.json`,
SHA-256 `e81ee37b5f1850de9f01f2a6274b3792fe438fd6d10d19d4b867b694eb48a937`.
All 114 listed artifacts were independently rehashed from Windows with zero
failures. The four cases total 25,851 audited control cycles and preserve 335
source graph anchors. The runner and receipt label these observations
exploratory; no vehicle authority or flight acceptance follows.

The present probe exercises one unequal signed vector and its reversal. Other
stick magnitudes, transition sequences, sensor/transport delay and general
publisher-epoch identity remain open. CAD and measured canard effectiveness,
loaded-servo behavior and EDF thrust/wash are still unavailable for vehicle
allocation or tuning. The stock Cessna baseline, H10 servo-only bench and
negative-only C1E-N1 gates retain their existing scope.
