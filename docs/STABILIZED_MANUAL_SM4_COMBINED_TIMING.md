# SM4 simultaneous pilot and estimated-state timing in isolated SITL

2026-10-05, Codex. Four **exploratory** mixed-axis maneuvers pass with an
80 ms wall-clock `MANUAL_CONTROL` send period and an independently declared
80 ms raw IMU transport delay. Both level and body-X-up poses and both
signed command orders were exercised. This finite matrix does not expand the
[accepted 31-case corrected-profile checkpoint](STABILIZED_MANUAL_SM4_RUNTIME_PROFILE_FIX.md)
or establish a radio, sensor or vehicle timing budget.

The [source-bound child preparation](../Tools/research_sitl/prepare_stabilized_combined_timing.py)
reuses byte-identical PX4 ELF
`a2c8229b30a93434f6d6aae3170fc67f354227e5ce236eca83720d7b79397f7d`
and private Gazebo relay plugin
`ee093faddf9979b9ba756c300b25d8d27f912a2d8ba3f3e1e2d07a8990780ada`
from the [raw IMU timing record](STABILIZED_MANUAL_SM4_IMU_DELAY.md).
The pilot source, body-rate mapping, quaternion hold-on-release, controller,
four-canard synthetic moment map, estimator profile, fixed 10 N simulated EDF
thrust and physical scoring limits are unchanged. No stock firmware or output
path changed.

| Pose | First command sign | Audited cycles | Selected-manual age p95 / max | Selected-rate age min / max | Max physical tracking | Peak / final physical hold |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Body-X-up | − | 6,467 | 80 / 84 ms | 80 / 84 ms | 0.017374 rad/s | 0.422378 / 0.003403 rad |
| Body-X-up | + | 6,466 | 80 / 88 ms | 80 / 84 ms | 0.017526 rad/s | 0.422263 / 0.005246 rad |
| Level | − | 6,469 | 80 / 84 ms | 80 / 84 ms | 0.016968 rad/s | 0.434561 / 0.006555 rad |
| Level | + | 6,476 | 80 / 84 ms | 80 / 84 ms | 0.016931 rad/s | 0.437710 / 0.004013 rad |

The [independent closed-log audit](../Tools/research_sitl/seal_stabilized_combined_timing.py)
confirms every run's 80 ms pilot period, exact 80 ms IMU packet
sample-to-delivery offset, 80–84 ms selected-rate age and observed
selected-manual age. It separately rescores the signed rate phases, actual
three-axis impulse, quaternion capture/retention, physical tracking and hold,
saturation/anti-windup and consumer disable. All four pass the unchanged
0.08 rad/s tracking, 0.5 rad peak and 0.02 rad final physical limits. The
largest observed manual age leaves 12 ms to the configured 100 ms manual
freshness limit; the selected-rate maximum leaves 16 ms to the 100 ms state
limit. Neither is a guaranteed scheduling or jitter envelope. The earlier
[100 ms raw IMU case](STABILIZED_MANUAL_SM4_IMU_DELAY.md) rejects state before
output; this run does not broaden that exact boundary claim.

Evidence: `/var/tmp/research-stabilized-sitl-yoxu1tex/combined-timing-manifest.json`,
SHA-256 `913dd4b29e79a83bcf2c6109f5efd05ed3e98fe1308a7b943acaf1b7c8a0a6f3`.
The audit covers 25,878 actual control cycles and 348 preserved preparation
graph anchors; all 161 artifacts independently rehash with zero failures.
The parent positive and boundary receipts, binary and plugin identities are
checked before the child fixture starts. All fixture processes closed.

Next synthetic work: repeat near-limit cases across seeds and timing phases,
introduce explicitly bounded packet jitter/loss, and resolve general same-ID
publisher-epoch identity. Those tests need no new human input. Vehicle-specific
allocation and tuning still require the pending CAD and measured canard
effectiveness/signs, loaded-servo dynamics and EDF thrust/lag/wash. Stock
Cessna, H10 servo-only bench, negative-only C1E-N1 and all physical gates
remain unchanged.
