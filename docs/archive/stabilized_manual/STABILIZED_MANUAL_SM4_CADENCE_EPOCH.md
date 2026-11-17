# SM4 manual-link cadence and publisher-epoch boundary

2026-10-05, Codex. Goal active. Three **exploratory** live-clock manual-link
cadence cases pass at their exact synthetic inputs. The
[31-case corrected-profile qualification](STABILIZED_MANUAL_SM4_RUNTIME_PROFILE_FIX.md)
remains the accepted checkpoint; these observations do not define an installed
radio or companion-link timing budget.

The source-checked fixture reused the same qualified PX4 ELF
`a2c8229b30a93434f6d6aae3170fc67f354227e5ce236eca83720d7b79397f7d`
and Gazebo plugin
`795a826f771dfd5cfe3b56c719f0f036f897184457fbe4c132b917d4c5c2f044`.
Only the wall-clock MAVLink `MANUAL_CONTROL` sender period changed from the
previous 20 ms test to 60 or 80 ms. The 100 ms selected-manual freshness limit,
body-rate command map, quaternion hold, synthetic plant, EKF settings, fixed
10 N simulated thrust and existing physical scoring limits stayed unchanged.
The [mixed-axis maneuver](STABILIZED_MANUAL_SM4_MIXED_AXES.md) used the same
unequal signed command and reversal, release, three-axis impulse and hold.

| Pose | Send period | Audited cycles | Selected-manual age, p95 / max | Max physical rate error | Final physical hold error |
| --- | ---: | ---: | ---: | ---: | ---: |
| Body-X-up | 60 ms | 6,459 | 60 / 68 ms | 0.015348 rad/s | 0.005229 rad |
| Body-X-up | 80 ms | 6,472 | 80 / 84 ms | 0.015226 rad/s | 0.004247 rad |
| Level | 80 ms | 6,477 | 80 / 84 ms | 0.015877 rad/s | 0.001906 rad |

All three pass the original mixed-maneuver setpoint, physical/estimated rate,
capture/retention, final attitude/rate, peak, actual impulse, saturation,
anti-windup, magnetic-rigidity and consumer-disable checks. Actual runtime
gyro/accel bias priors and magnetic-fusion type were compared before enable.
The independent audit recalculates selected-manual ages, physical rate tracking
and hold error from closed logs and checks source, profile and binary identity.
Evidence: `/var/tmp/research-stabilized-sitl-rgz2nu44/transport-manifest.json`,
SHA-256 `0354b1817e97aa1942537bcd20203c87e16676a739e5de4a06954e9a4c5ab2ab`.
Its 88 artifacts were independently rehashed with zero failures; 19,408
control cycles and 338 pre-run graph anchors are preserved. At 80 ms cadence,
the measured maximum sample age leaves 16 ms to the unchanged 100 ms limit.
This finite observation is not a guaranteed jitter envelope. Full input loss
and explicit recovery remain covered by the accepted negative cases.

An independent **frozen-clock publisher-epoch attempt did not reach its intended
condition**. In `/var/tmp/research-stabilized-sitl-4ah9yo8u/epoch-pause-level-d_qw5j4f`,
the stock `ekf2 stop` client timed out after 10 wall seconds while Gazebo was
paused; no `ekf2 start` command ran. The fixture unpaused and closed. The
preserved `result.json` SHA-256 is
`e2042072f10ba6395f9074c28c7933300b2f9fca8639a18873bc40aceee50eef`.
This attempt proves no completed between-poll restart or owner response to one.

The private adapter still supplies constant `source_epoch=1`. Public uORB
`Subscription` exposes a message generation and current advertisement state;
the underlying node is reused across unadvertise/re-advertise. Those values do
not by themselves establish a persistent publisher-restart identity if the
entire lifecycle occurs between owner observations. The existing eight native
[state-barrier cases](STABILIZED_MANUAL_SM4_STATE_BARRIERS.md) prove their
observed stop, restart and reset paths, while general same-ID publisher epoch
qualification remains open. A future test needs a reliable publisher epoch
source or controlled owner scheduling, plus exact positive and negative
transition evidence. Sensor transport delay and broader manual-link jitter
also remain open.

No new vehicle input is needed for those synthetic software tests. Vehicle
allocation and tuning still require the pending CAD, measured canard
effectiveness/signs, loaded-servo behavior and EDF data. The stock Cessna
baseline, H10 bench gate, negative-only C1E-N1 and physical acceptance gates
remain unchanged.
