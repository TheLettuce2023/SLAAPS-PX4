# SM4 combined near-limit timing with manual-input loss

2026-10-05, Codex. Two **exploratory** native PX4/Gazebo input-loss cases pass
at level and body-X-up with an 80 ms wall-clock pilot send period and exact
80 ms raw IMU packet delay. This extends the accepted input-loss safety test
to the combined timing condition; it does not expand the accepted
[31-case corrected-profile checkpoint](STABILIZED_MANUAL_SM4_RUNTIME_PROFILE_FIX.md)
or establish a packet-jitter, radio-link or vehicle envelope.

The [loss runner](../Tools/research_sitl/verify_stabilized_combined_loss.py)
starts in centered quaternion hold, stops its MAVLink `MANUAL_CONTROL` sender,
waits 300 ms of simulated time, checks the native owner latches manual-input
fault 5, and verifies a new `enable` request is rejected. It then stops the
owner, resumes valid input, starts a new owner session, explicitly enables
hold, and finally disables it. The synthetic rotational plant, fixed 10 N
simulated EDF thrust, controller, allocator, 100 ms freshness threshold and
consumer lease were unchanged. The isolated child reuses the exact PX4 ELF
`a2c8229b30a93434f6d6aae3170fc67f354227e5ce236eca83720d7b79397f7d`
and private relay plugin
`ee093faddf9979b9ba756c300b25d8d27f912a2d8ba3f3e1e2d07a8990780ada`.

| Pose | Audited control cycles | Last published manual age | Consumer off after last manual sample | Consumer-off interval before explicit recovery | Relayed IMU packets |
| --- | ---: | ---: | ---: | ---: | ---: |
| Level | 515 | 100 ms | 108 ms | 560 ms | 2,066 |
| Body-X-up | 499 | 100 ms | 108 ms | 664 ms | 2,113 |

The [independent sealer](../Tools/research_sitl/seal_stabilized_combined_loss.py)
recalculates source and binary identities, verifies every relayed IMU packet
has an 80 ms sample-to-delivery offset, checks native control-cycle freshness,
and audits the fault, rejected re-enable, old/new session IDs and consumer
output. No old-session control publishes after the consumer turns off. While
faulted, all four surface targets and EDF thrust are zero in the fixture;
the consumer lease remains faulted. A new explicit session restores hold.
These are **simulated safety transitions**, not a flight recovery policy.

Evidence: `/var/tmp/research-stabilized-sitl-jnlgzcsy/combined-loss-manifest.json`,
SHA-256 `a78b02f86b118e31848a7a8729d1d6d9505e5945b99c14491761d23e211d888b`.
The 111 artifacts independently rehash with zero mismatches; 354 preparation
graph anchors are preserved, and all fixture processes closed. The sender's
wall-clock phase was not fixed and the blackout was deliberately longer than
the freshness limit. Therefore the 108 ms consumer-off observations do not
prove bounded jitter tolerance or a universal reaction-time guarantee.

Next synthetic work can control send phase and test measured bounded jitter
or a single missed packet at the near-limit setting. General same-ID estimator
publisher-epoch identity also remains open. No new human input is needed for
those software tests. Vehicle-supported allocation and tuning still require
the pending CAD, measured canard effectiveness/signs, loaded-servo dynamics,
and EDF thrust/lag/wash. Stock Cessna, H10 servo-only bench, negative-only
C1E-N1 and all physical gates remain unchanged.
