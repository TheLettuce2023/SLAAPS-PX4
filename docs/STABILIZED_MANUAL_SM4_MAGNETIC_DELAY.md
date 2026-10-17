# SM4 delayed magnetic transport in isolated SITL

2026-10-05, Codex. Six **exploratory** estimated-state mixed-axis maneuvers
pass at the exact declared magnetic packet delays below. This is a finite
sensor-transport observation, not a sensor-delay envelope, vehicle-supported
tuning, or an expansion of the accepted [31-case corrected-profile checkpoint](STABILIZED_MANUAL_SM4_RUNTIME_PROFILE_FIX.md).

The private [Gazebo rotational fixture](../Tools/research_sitl/stabilized_manual/RotationalFixture.cpp)
now queues its corrected magnetometer packets until a declared simulation-time
deadline. It preserves their original sample stamp and logs both sample and
delivery times. The stock `GZBridge`, PX4 executable, estimator settings,
controller, synthetic moment map, prescribed airflow and fixed 10 N simulated
thrust were unchanged. The accepted PX4 ELF remains SHA-256
`a2c8229b30a93434f6d6aae3170fc67f354227e5ce236eca83720d7b79397f7d`;
only the private plugin was rebuilt, SHA-256
`3424a050c57b82c60c84c5ccfbf26fa3719b5a808b38e9f67dc987b132091456`.
The [source-bound variant preparation](../Tools/research_sitl/prepare_stabilized_magnetic_delay.py)
compares the accepted parent receipt, preserves the original native binary,
and records the two changed fixture inputs: relay source and SDF delay field.
It does not build or modify stock firmware or an output path.

| Pose | Declared extra delay | Measured sample-to-delivery | Audited cycles | Max physical rate error | Final physical hold error |
| --- | ---: | ---: | ---: | ---: | ---: |
| Body-X-up | 0 ms | 4 ms | 6,462 | 0.015487 rad/s | 0.001917 rad |
| Body-X-up | 20 ms | 20 ms | 6,478 | 0.015557 rad/s | 0.002171 rad |
| Body-X-up | 40 ms | 40 ms | 6,460 | 0.015620 rad/s | 0.003237 rad |
| Level | 0 ms | 4 ms | 6,465 | 0.015577 rad/s | 0.003208 rad |
| Level | 40 ms | 40 ms | 6,458 | 0.015519 rad/s | 0.016142 rad |
| Level, repeat | 40 ms | 40 ms | 6,476 | 0.015514 rad/s | 0.004134 rad |

The zero-extra-delay path incurs one 4 ms fixture tick because the relay
delivers from `PreUpdate` after the Gazebo sensor publishes. The independent
audit recalculates every packet offset and magnetic unit/sign conversion from
3,150-3,164 logged deliveries per run; the positive-delay offsets are exact at
20 or 40 ms. It also independently rescores the closed physical and controller
logs for both signed rate phases, quaternion capture/retention, three-axis
impulse, saturation/anti-windup, hold and consumer disable. All six pass the
unchanged 0.08 rad/s physical tracking and 0.02 rad final hold limits. The two
level 40 ms runs differ markedly despite the same declared seed and profile.
Consequently, this evidence does **not** establish a reliable 40 ms delay margin
or prove the delay alone caused the larger error; it warrants further repeated
timing analysis before any broader claim.

Evidence: `/var/tmp/research-stabilized-sitl-41oq2aje/magnetic-delay-manifest.json`,
SHA-256 `8f42ff4c6eef5da40313313d8415287680d457526a6491ab5aad0fa864f0702e`.
The independent audit records 38,799 control cycles, six complete cases and
341 preparation graph anchors; all 211 manifest artifacts were separately
rehashed with zero failures. The variant's preparation snapshot predates three
runner/sealer changes, which the audit identifies explicitly and binds to the
per-run source copies. The physical fixture remained isolated and all processes
closed after each run.

Next synthetic work: test IMU/estimated-state timing and repeated magnetic-delay
cases at multiple seeds, then address the general same-ID publisher-epoch gap.
No new human input is needed for those software tests. Vehicle-supported
allocation and tuning still await the pending CAD, measured canard authority and
hinge signs, loaded-servo behavior and EDF thrust/lag/wash. The stock Cessna
baseline, H10 servo-only bench, negative-only C1E-N1 and all physical gates are
unchanged.
