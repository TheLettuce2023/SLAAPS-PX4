# SM4 near-limit combined-timing repeats across Gazebo seeds

2026-10-05, Codex. Eight **exploratory** level/body-X-up, both-order mixed-axis
maneuvers pass with an 80 ms pilot send period and exact 80 ms raw IMU packet
delay under Gazebo seeds 43 and 44. The earlier [seed 42 four-case matrix](STABILIZED_MANUAL_SM4_COMBINED_TIMING.md)
also passed. Together, these are 12 finite observations over 77,625 control
cycles, not a statistical reliability estimate or vehicle timing envelope.
The accepted [31-case corrected-profile checkpoint](STABILIZED_MANUAL_SM4_RUNTIME_PROFILE_FIX.md)
is unchanged.

The [seed-aware runner](../Tools/research_sitl/verify_stabilized_mixed.py)
passes the declared seed to Gazebo while preserving the same pilot command,
release/hold, three-axis impulse, synthetic plant, fixed 10 N simulated EDF
thrust, controller, allocator and physical limits. The child evidence root
reuses byte-identical PX4 ELF
`a2c8229b30a93434f6d6aae3170fc67f354227e5ce236eca83720d7b79397f7d`
and private relay plugin
`ee093faddf9979b9ba756c300b25d8d27f912a2d8ba3f3e1e2d07a8990780ada`.
The independent audit checks that seeds 43 and 44 produced distinct initial
magnetic sensor samples for each matching pose/order. Wall-clock sender phase
was not held fixed, so score differences cannot be attributed solely to the
Gazebo random seed.

| Seed | Pose | First sign | Audited cycles | Max selected-manual age | Peak / final physical hold error |
| ---: | --- | ---: | ---: | ---: | ---: |
| 43 | Body-X-up | − | 6,467 | 88 ms | 0.428103 / 0.004423 rad |
| 43 | Body-X-up | + | 6,477 | 88 ms | 0.432555 / 0.006657 rad |
| 43 | Level | − | 6,466 | 84 ms | 0.428317 / 0.004023 rad |
| 43 | Level | + | 6,463 | 88 ms | 0.429769 / 0.004576 rad |
| 44 | Body-X-up | − | 6,474 | 84 ms | 0.434115 / 0.005394 rad |
| 44 | Body-X-up | + | 6,474 | 84 ms | 0.428105 / 0.005321 rad |
| 44 | Level | − | 6,469 | 84 ms | 0.439332 / 0.005791 rad |
| 44 | Level | + | 6,457 | 84 ms | 0.426484 / 0.003140 rad |

The [independent sealer](../Tools/research_sitl/seal_stabilized_combined_repeats.py)
recalculates each closed-log pilot and selected-rate sample age, verifies the
80 ms IMU packet offset, and reuses the physical rate/hold, quaternion
capture/retention, actual impulse, saturation/anti-windup and consumer-disable
audit. Selected-rate age is 80–84 ms in every run; maximum selected-manual age
is 88 ms. Worst physical rate-tracking error is 0.017599 rad/s versus the
unchanged 0.08 rad/s limit; peak and final physical hold errors remain below
0.5 and 0.02 rad. No freshness or physical threshold was relaxed. The closest
observed manual-age margin is 12 ms, without a guaranteed jitter allowance.

Evidence: `/var/tmp/research-stabilized-sitl-360l01c2/combined-repeats-manifest.json`,
SHA-256 `279793a3bc0eb399db517c8a07a3b6869d7aa59273676c90be4083c3cc4d0ab0`.
All 269 artifacts independently rehash with zero failures; 350 preparation
graph anchors were preserved. The audit verifies the original seed 42 receipt
and both parent IMU positive/boundary receipts, source snapshots and exact
binary/plugin identities. All fixture processes closed.

Next synthetic work: control pilot-send phase and bounded packet jitter/loss,
then resolve general same-ID estimator publisher-epoch identity. Those tests
need no new human input. Vehicle-supported allocation and tuning still need
the pending CAD and measured canard authority/signs, loaded-servo dynamics
and EDF thrust/lag/wash. Stock Cessna, H10 servo-only bench, negative-only
C1E-N1 and all physical gates remain unchanged.
