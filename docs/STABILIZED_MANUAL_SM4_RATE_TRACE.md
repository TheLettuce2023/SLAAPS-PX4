# SM4 stock ULog rate-path repeats

2026-10-05, Codex. Three isolated native SITL repeats revisited the
body-X-up, positive-first mixed-axis case that previously reached a 100 ms
selected-rate age. They used the same corrected synthetic profile, Gazebo
seed 42, alternating 76/84 ms scheduled pilot sends, exact 80 ms raw IMU
delivery offset, fixed simulated EDF thrust and accepted PX4 ELF/private
relay plugin. The private fixture also started the stock PX4 file logger with
`sensor_gyro`, `vehicle_imu`, `vehicle_angular_velocity` and
`vehicle_attitude`. Production C++ and controller thresholds were unchanged.

The [three-repeat runner](../Tools/research_sitl/verify_stabilized_rate_trace.py)
and [independent sealer](../Tools/research_sitl/seal_stabilized_rate_trace.py)
audited 19,423 active control cycles (6,481, 6,468 and 6,474). All three
passed the unchanged phase, raw-packet and physical checks. The largest
selected manual age was 88 ms, and the selected angular-rate age stayed at
80–84 ms in each repeat. No active control cycle repeated the earlier
88/92/96/100 ms sequence. Each ULog closed with zero reported logger
dropouts. In the active-control interval, the maximum logged sample gap was
4 ms for `sensor_gyro` and 8 ms for `vehicle_angular_velocity` in all three.
Every logged gyro sample timestamp matched a relayed raw IMU sample.

One repeat had 24 ms gyro, 68 ms `vehicle_imu` and 40 ms angular-rate sample
gaps near simulator startup, before the active control interval. They do not
describe the earlier active-control event. Moreover, 323–443 distinct rate
sample timestamps selected by the owner per repeat are absent from the ULog
despite zero reported dropouts. The logger record is therefore not an
every-publication trace. Logger activity can also perturb scheduling. These
finite repeats cannot assign a cause to the original 20 ms stall, establish
its frequency, or demonstrate a general age margin. A targeted bridge/gyro/
rate-publication epoch diagnostic in the private fixture remains software
work that can proceed without new vehicle inputs.

Evidence:
`/var/tmp/research-stabilized-sitl-sc7ckzfv/rate-trace-manifest.json`,
SHA-256 `d22c9328b24a9dbed643e8197f47652a73be598f1ce3e3f2493b08251f629227`.
The sealer revalidated 364 preparation graph anchors, the parent ELF and
relay receipts, fixture source, all three native cases, and 152 sealed
artifacts. A separate Windows-side rehash found zero artifact mismatches.
The accepted checkpoint stays at 31 corrected-profile native cases; these
three runs are exploratory and do not qualify the physical vehicle. The
stock Cessna, H10 servo-only bench, negative-only C1E-N1 acceleration, and
physical gates remain unchanged. CAD and measured canard, servo and EDF
inputs remain pending for vehicle-specific allocation and tuning.
