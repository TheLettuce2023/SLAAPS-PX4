# SM4 one omitted pilot packet at controlled phase

2026-10-05, Codex. Four **exploratory** native PX4/Gazebo negative cases pass:
level and body-X-up, each during centered quaternion hold and active mixed-axis
body-rate control. The pilot sender uses the [4 ms observation clock and
alternating 76/84 ms schedule](STABILIZED_MANUAL_SM4_PHASED_JITTER.md) with
exact 80 ms raw-IMU packet delay, then omits **scheduled packet index 50 only**.
Later packets resume on schedule. This is a synthetic loss boundary, not an
installed radio-link or vehicle qualification. The accepted
[31-case corrected-profile checkpoint](STABILIZED_MANUAL_SM4_RUNTIME_PROFILE_FIX.md)
is unchanged.

The [runner](../Tools/research_sitl/verify_stabilized_phased_skip.py) logs both
actual send invocations and the one omitted target. Each closed-log trace has
exactly one missing index and a **160 ms** gap between adjacent sends; all
other observed intervals stay within the auditor's 68–92 ms bound.
The [independent audit](../Tools/research_sitl/seal_stabilized_phased_skip.py)
checks every raw IMU relay packet has an 80 ms sample-to-delivery offset,
verifies selected manual samples against the send log, audits native fault 5,
checks neutral surface targets/zero EDF thrust while the consumer is off,
and requires an explicit newer owner session before hold resumes. The
controller, allocator, synthetic plant, fixed 10 N simulated EDF thrust and
100 ms input/state freshness limits did not change.

| Pose | Mode at omission | Audited control cycles | Single send gap | Last published manual age | Consumer off after last manual sample |
| --- | --- | ---: | ---: | ---: | ---: |
| Level | Hold | 1,495 | 160 ms | 100 ms | 108 ms |
| Level | Rate | 1,509 | 160 ms | 100 ms | 108 ms |
| Body-X-up | Hold | 1,488 | 160 ms | 100 ms | 108 ms |
| Body-X-up | Rate | 1,491 | 160 ms | 100 ms | 108 ms |

In each case the native owner latches **manual-input fault 5**, rejects
re-enable in the faulted session, and stays faulted when centered pilot
packets resume. A new explicit session then regains centered hold. The
consumer remains off with neutral targets and zero thrust between those
sessions. The rate cases had maximum pre-omission physical rate errors
0.035508 and 0.036009 rad/s, below the unchanged 0.08 rad/s criterion; the
hold cases retained their quaternion reference. The largest selected-rate
age before fault was 84 ms. These are exact observed transitions, not a
universal reaction-time guarantee or a flight recovery policy.

The first active-rate attempt is preserved as **failed**. Its one-packet
omission correctly caused fault 5, but the test tried to enable the new owner
before PX4 had selected a centered pilot packet. Native reason 8
(`NotCentered`) rejected that entry. The corrected runner sends centered
packets while the old session remains faulted, verifies no implicit recovery,
then starts and enables the new session. Failed evidence root:
`/var/tmp/research-stabilized-sitl-i_1128le`; `result.json` SHA-256
`6c18cc6182a167b60607383cd87617825dee1dec19374b12a9e71fbde813c9`,
omission-log SHA-256
`b00ef4164303ec469b4189b5a1e32ed24dad7125b16c94184fa958ae0b0870d5`.
The native `px4.log` recording reason 8 has SHA-256
`74f4610f9ed613dde11f0ea8f87dccb28c22ba95dd218a750328020d9db0e2ef`.
The corrected result uses a separate source-bound root.

Passing evidence:
`/var/tmp/research-stabilized-sitl-mz0gffgm/phased-skip-manifest.json`,
SHA-256 `f7a4e9431f4464cc2d5faf1e1235441b9945fda447a12eea93d0a2f19ce21307`.
All **185 artifacts** independently rehash with zero mismatches, **5,983
published control cycles** were audited and all **360 preparation graph
anchors** remain byte-identical. The child reuses exact PX4 ELF
`a2c8229b30a93434f6d6aae3170fc67f354227e5ce236eca83720d7b79397f7d`
and private relay plugin
`ee093faddf9979b9ba756c300b25d8d27f912a2d8ba3f3e1e2d07a8990780ada`.
All fixture processes closed.

Next synthetic work can investigate the selected-state 100 ms boundary and
general same-ID estimator publisher-epoch identity. No new human input is
needed for those software tests. Vehicle-supported allocation and tuning
still need pending CAD, measured canard effectiveness/signs, loaded-servo
dynamics and EDF thrust/lag/wash. Stock Cessna, H10 servo-only bench,
negative-only C1E-N1 and all physical gates remain unchanged.
