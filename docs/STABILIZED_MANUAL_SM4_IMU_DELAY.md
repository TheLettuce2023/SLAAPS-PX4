# SM4 raw IMU transport timing in isolated SITL

2026-10-05, Codex. Seven **exploratory** estimated-state mixed-axis maneuvers
pass with direct or delayed raw Gazebo IMU transport. A separate actual 100 ms
delay probe rejects state before a control cycle or consumer enable. These finite
observations do not expand the accepted [31-case corrected-profile checkpoint](STABILIZED_MANUAL_SM4_RUNTIME_PROFILE_FIX.md)
or establish a vehicle sensor/link timing envelope.

For positive delay, the private [rotational fixture](../Tools/research_sitl/stabilized_manual/RotationalFixture.cpp)
routes the raw 250 Hz IMU topic through a bounded simulation-time queue, then
publishes the **unchanged packet and original sensor stamp** to the topic stock
PX4 `GZBridge` consumes. At zero delay the sensor publishes directly to that
topic. The [SDF profile](../Tools/research_sitl/stabilized_manual/world.sdf.in)
declares both routes and delay. Normal PX4 sensors, EKF and selected estimated
attitude/rates feed the owner; fixture truth remains only for scoring. The
accepted PX4 ELF SHA-256 stays
`a2c8229b30a93434f6d6aae3170fc67f354227e5ce236eca83720d7b79397f7d`.
Only the private Gazebo plugin was rebuilt, SHA-256
`ee093faddf9979b9ba756c300b25d8d27f912a2d8ba3f3e1e2d07a8990780ada`.
The synthetic plant, fixed 10 N simulated EDF thrust, canard allocation,
controller gains, manual 20 ms send period and scoring limits did not change.

| Pose | Declared IMU delay | Selected rate sample age | Control cycles | Peak physical hold error | Final physical hold error |
| --- | ---: | ---: | ---: | ---: | ---: |
| Body-X-up | direct | 0–4 ms | 6,479 | 0.305960 rad | 0.001401 rad |
| Body-X-up | 20 ms | 20–24 ms | 6,462 | 0.327447 rad | 0.003312 rad |
| Body-X-up | 40 ms | 40–44 ms | 6,471 | 0.354441 rad | 0.004246 rad |
| Body-X-up | 80 ms | 80–84 ms | 6,472 | 0.426482 rad | 0.004394 rad |
| Level | direct | 0–4 ms | 6,470 | 0.303444 rad | 0.002355 rad |
| Level | 40 ms | 40–44 ms | 6,473 | 0.350287 rad | 0.002379 rad |
| Level | 80 ms | 80–84 ms | 6,457 | 0.421503 rad | 0.003533 rad |

The independent [closed-log audit](../Tools/research_sitl/seal_stabilized_imu_delay.py)
verifies the direct/relay SDF routes, exact sample-to-delivery offsets for
7,905–7,953 relayed packets per delayed run, selected-rate sample ages,
source/ELF/plugin identity and physical maneuver scores. All seven pass the
unchanged 0.08 rad/s rate-tracking, 0.5 rad peak and 0.02 rad final physical
hold limits, including three-axis impulse, saturation/anti-windup and consumer
disable. Worst measured physical rate error is 0.017212 rad/s. At 80 ms, the
maximum selected-rate age leaves 16 ms to the configured 100 ms state-age
limit; the largest peak leaves about 0.074 rad to its 0.5 rad limit. This is
not a guarantee under jitter or different seeds, commands or physical plants.

The distinct [100 ms boundary diagnostic](../Tools/research_sitl/diagnose_stabilized_imu_limit.py)
delivered 1,588 IMU packets exactly 100 ms after sampling. The native owner
latched `StateRejected` (fault 6) during initial hold; no control cycles were
published, and all simulated consumer frames stayed disabled with zero thrust
and surface commands. The consumer lease-fault flag stayed clear because no
command had ever been enabled. An earlier probe of the same boundary is
retained: its physical observation agreed, but its result field incorrectly
treated a never-enabled lease as a lease fault. The
[independent boundary audit](../Tools/research_sitl/seal_stabilized_imu_boundary.py)
preserves both source-bound attempts and scores the corrected negative result.

Positive evidence: `/var/tmp/research-stabilized-sitl-z7vo0s62/imu-delay-manifest.json`,
SHA-256 `7f82292dea39a624657629a3de4a03813163297495a30749e4d3992872a2254b`;
45,284 audited control cycles, 343 preparation graph anchors and 242 artifacts
independently rehashed with zero failures. Boundary evidence:
`/var/tmp/research-stabilized-sitl-z7vo0s62/imu-boundary-manifest.json`, SHA-256
`f96cc4f8038eb9e2e2b8019ce5bca8e5747ef0570988e30e9e69eb3738aa2c35`;
62 artifacts independently rehashed with zero failures. The [variant builder](../Tools/research_sitl/prepare_stabilized_magnetic_delay.py)
binds both receipts to the exact accepted PX4 ELF and the changed private
plugin/SDF source. All fixture processes closed.

Next synthetic work should repeat the 80 ms observation across seeds and input
orders, combine bounded input and state timing variation, and resolve the
general same-ID publisher-epoch question. Those tests need no new human input.
Vehicle-supported allocation/tuning still await the pending CAD and measured
canard effectiveness/signs, loaded-servo behavior and EDF thrust/lag/wash.
Stock Cessna, H10 servo-only bench, negative-only C1E-N1 and all physical gates
remain unchanged.
