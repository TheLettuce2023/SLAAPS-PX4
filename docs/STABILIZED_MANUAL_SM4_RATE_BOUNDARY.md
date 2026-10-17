# SM4 selected angular-rate age at the 100 ms boundary

2026-10-05, Codex. A read-only [closed-log diagnostic](../Tools/research_sitl/diagnose_stabilized_rate_boundary.py)
revalidated every artifact in the sealed [four-case phase-controlled matrix](STABILIZED_MANUAL_SM4_PHASED_JITTER.md).
It located the one 100 ms selected-rate observation precisely. This is a
correlation across existing logs, **not** a causal identification or a new
SITL acceptance case.

In the body-X-up, positive-first run, the selected
`vehicle_angular_velocity` publication and sample timestamps stayed at
17.144 s / 17.064 s while four control cycles occurred at 17.152, 17.156,
17.160 and 17.164 s. Their rate-sample ages were **88, 92, 96 and 100 ms**.
The next control cycle selected a rate sample at 17.084 s, published at
17.164 s, so the selected rate publication gap was **20 ms**. During the
four-cycle stall, selected attitude sample timestamps advanced from 17.064
to 17.080 s and selected manual ages were only 36–48 ms. The owner remained
active and the maneuver's physical checks passed.

The private relay log records **five consecutive raw IMU packets** with
sample times 17.068–17.084 s and delivery times 17.148–17.164 s, each
exactly 80 ms later and 4 ms apart. The plugin records successful Gazebo
publish calls; those records do **not** prove when the stock GZ bridge
received them or when PX4 published `sensor_gyro`. Stock
[GZBridge](../src/modules/simulation/gz_bridge/GZBridge.cpp) publishes gyro
samples in its IMU callback, and stock
[VehicleAngularVelocity](../src/modules/sensors/vehicle_angular_velocity/VehicleAngularVelocity.cpp)
publishes the selected angular-rate topic from gyro subscriptions. The
available logs locate the stall between relay publication and the owner's
copy of selected angular rate. They cannot distinguish bridge transport,
gyro publication, or angular-rate work-queue scheduling. No controller
threshold or production source was changed.

The other three phase-controlled cases had maximum selected-rate age 84 ms
and no cycle above that value. One event in four finite cases does not give a
probability, timing margin or installed sensor budget. The accepted
[31-case corrected-profile checkpoint](STABILIZED_MANUAL_SM4_RUNTIME_PROFILE_FIX.md)
is unchanged. A continuous isolated ULog of `sensor_gyro`,
`vehicle_angular_velocity` and attitude is the next diagnostic that could
separate the remaining layers if the event recurs. The subsequent
[three-repeat ULog record](STABILIZED_MANUAL_SM4_RATE_TRACE.md) did not
reproduce the stall and did not resolve its cause.

Derived evidence:
`/var/tmp/research-stabilized-sitl-h65mm5y1/rate-boundary-manifest.json`,
SHA-256 `cf76f66bec769fd731675b7e41317599484d20148b5dcf7a935e25856bb55b3c`.
The diagnostic rehashed all 169 parent artifacts, preserved 362 graph
anchors at preparation, and its four local artifacts independently rehash
with zero mismatches. The parent PX4 ELF, private relay plugin and all
physical/bench/acceleration gates remain as recorded in the phase-controlled
matrix. No new human input is needed for the next isolated logging test;
vehicle-specific allocation and tuning still await CAD, canard, servo and
EDF measurements.
