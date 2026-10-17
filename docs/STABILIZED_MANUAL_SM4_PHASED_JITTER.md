# SM4 simulation-clock pilot-send phase and bounded jitter observation

2026-10-05, Codex. Four **exploratory** native PX4/Gazebo mixed-axis maneuvers
pass at level and body-X-up, both command orders, with 80 ms exact raw-IMU
packet delay and a pilot-send schedule alternating **76 and 84 ms**. The
scheduled ±4 ms variation is a synthetic input. This is four finite cases at
Gazebo seed 42, not a radio-link, statistical jitter or vehicle envelope. The
accepted [31-case corrected-profile checkpoint](STABILIZED_MANUAL_SM4_RUNTIME_PROFILE_FIX.md)
is unchanged.

The [phase-controlled runner](../Tools/research_sitl/verify_stabilized_phased_jitter.py)
reuses the full [mixed-axis maneuver](../Tools/research_sitl/verify_stabilized_mixed.py):
signed three-axis body-rate command/reversal, centered quaternion hold,
three-axis impulse, saturation/anti-windup and consumer disable. Startup uses
the declared 80 ms wall-clock sender; after native hold becomes ready, the
sender takes its time from the private relay's **4 ms flushed IMU delivery
log** and records each intended and observed simulation-time send invocation.
The [independent audit](../Tools/research_sitl/seal_stabilized_phased_jitter.py)
checks those intervals, matches selected manual sample timestamps to the
send log within 4 ms, verifies every raw IMU packet's exact 80 ms offset,
and recalculates the physical maneuver scores from closed logs. The fixture
plant, controller, allocator, fixed 10 N simulated EDF thrust, freshness
thresholds and physical scoring limits were unchanged.

| Pose | First sign | Control cycles | Observed send interval | Max manual / selected-rate age | Peak / final physical hold error |
| --- | ---: | ---: | ---: | ---: | ---: |
| Level | + | 6,453 | 76–84 ms | 88 / 84 ms | 0.424013 / 0.003674 rad |
| Level | − | 6,476 | 76–84 ms | 88 / 84 ms | 0.418816 / 0.003933 rad |
| Body-X-up | + | 6,459 | 76–84 ms | 88 / **100 ms** | 0.419392 / 0.003486 rad |
| Body-X-up | − | 6,472 | 76–84 ms | 88 / 84 ms | 0.420266 / 0.003433 rad |

All 1,262 logged phase-controlled send invocations hit their scheduled time
in the 4 ms observation clock; selected manual samples were within 4 ms of
those invocations. Worst physical rate tracking was 0.017210 rad/s against
the unchanged 0.08 rad/s limit. Worst peak/final physical hold errors were
0.424013/0.003933 rad against 0.5/0.02 rad limits. The body-X-up/+ case has
**four selected-rate cycles aged 88, 92, 96 and 100 ms**. The owner remains
active at the exact freshness boundary, but there is **no demonstrated
selected-state age margin** in that case even though raw IMU transport was
exactly 80 ms. Additional estimator scheduling/delivery variation could
therefore change the outcome; these runs do not qualify an installed timing
budget.

The first phase-scheduler attempt is preserved as **failed**, not included
in the four passing cases. It read the fixture's truth CSV, which flushes only
every 100 ms, so observed host send lateness reached tens of milliseconds
and the native owner latched manual-input fault 5. Evidence root:
`/var/tmp/research-stabilized-sitl-99qle3x9`; failed `result.json` SHA-256
`f9b0124574871a0ee34c62bfa306ed659fb14ba38353f7d2ff843230b6661334`,
send-log SHA-256
`1c8cdd84eba3e642efd89a8fc49b0f2899dff487f838f65100e046250d2d945b`.
The corrected runner uses the 4 ms flushed relay log and a separate
source-bound root.

Passing evidence:
`/var/tmp/research-stabilized-sitl-kmupr1nc/phased-jitter-manifest.json`,
SHA-256 `3e2e43a4ba4f205a3bc7e3e284323e5044f974a259d13164f98798ecd727724d`.
All 169 artifacts independently rehash with zero mismatches. The child
reuses the byte-identical PX4 ELF
`a2c8229b30a93434f6d6aae3170fc67f354227e5ce236eca83720d7b79397f7d`
and private relay plugin
`ee093faddf9979b9ba756c300b25d8d27f912a2d8ba3f3e1e2d07a8990780ada`.
Of 357 preparation graph anchors, 356 remain byte-identical; the receipt
records the boundary-aware sealer's preparation and final hashes explicitly.
All fixture processes closed.

Next synthetic work: probe a deliberately skipped pilot packet and the
selected-state boundary with controlled phase, then address general same-ID
estimator publisher-epoch identity. No new human input is needed for those
tests. Vehicle-specific allocation/tuning still need pending CAD, measured
canard effectiveness/signs, loaded-servo dynamics and EDF thrust/lag/wash.
Stock Cessna, H10 servo-only bench, negative-only C1E-N1 and all physical
gates remain unchanged.
