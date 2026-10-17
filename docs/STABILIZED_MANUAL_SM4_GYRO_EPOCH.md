# SM4 direct gyro and selected-rate epoch probe

2026-10-05, Codex. The earlier [20 ms selected angular-rate stall](STABILIZED_MANUAL_SM4_RATE_BOUNDARY.md)
could not be localized by relay publish logs or incomplete stock ULog capture.
This follow-up built a **separate private PX4 diagnostic ELF** from the sealed
accepted source. A [reviewable patch](../Tools/research_sitl/gyro_epoch_probe.patch)
adds a `sensor_gyro` snapshot to the synthetic owner immediately before its
normal selected angular-rate copy and evaluation clock, then appends the
gyro publication/sample time and device ID to `control.csv`. The controller's
eligibility, commands, output path, thresholds and stock modules were not
edited. The exact relay plugin and 80 ms raw-IMU delivery setting were reused.

The first probe used one `uORB::Subscription.copy()` per control cycle. That
was a **capture-method error**: `SensorGyro.msg` declares an eight-entry queue,
and [uORB's queued copy](../platforms/common/uORB/uORBDeviceNode.hpp) advances
one unread generation rather than jumping to the newest publication. Its
6,479-cycle run passed physical checks but appeared to show gyro age up to
108 ms while selected-rate age stayed at most 84 ms. The old probe followed
the gyro queue about 24 ms behind; this observation does not show a bridge
or gyro-publication delay. The closed first-attempt receipt remains at
`/var/tmp/research-stabilized-sitl-b4v9sp91/gyro-epoch-manifest.json`,
SHA-256 `2fed619c00f43ba815c32d83ee9b2a440558d0e271d6107de569fdbe9d90957b`;
all 56 artifacts independently rehash clean.

The corrected private probe drains at most the declared eight queued gyro
updates before each rate copy and records the drain count. Its
[builder](../Tools/research_sitl/prepare_stabilized_gyro_epoch.py),
[runner](../Tools/research_sitl/verify_stabilized_gyro_epoch.py) and
[independent audit](../Tools/research_sitl/seal_stabilized_gyro_epoch.py)
bind the accepted source, exact patch, separately compiled ELF and relay
plugin. The body-X-up, positive-first seed-42 mixed-axis case passed 6,478
active cycles at alternating 76/84 ms pilot-send intervals and exact 80 ms
raw IMU delay. The selected gyro device ID matched the selected estimator
gyro in every cycle; each gyro sample timestamp matched the raw relay log.
At most two queued entries were drained in a cycle, with no full-queue
drains. Both gyro and selected-rate sample ages peaked at **84 ms**; the
gyro was at most one 4 ms sample ahead of the selected rate. The unchanged
physical checks passed: maximum rate-tracking error 0.017180 rad/s, peak
hold error 0.423110 rad and final hold error 0.007496 rad.

The original active 20 ms stall did not recur. The corrected probe can
distinguish a fresh gyro from a stale selected rate on owner cycles if it
does recur, but this one case cannot identify the original cause, quantify
its frequency or establish a timing margin. If a future trace shows both
gyro and rate stale, a private bridge-callback publication epoch is needed
to separate bridge delivery from gyro publication. Diagnostic sampling may
itself alter scheduling; this ELF is not the accepted controller binary.

Corrected receipt:
`/var/tmp/research-stabilized-sitl-xf5j8eh2/gyro-epoch-manifest.json`,
SHA-256 `768bd8f151c5f7f1edc107fefadfcf79c9986c0cdd6e38ab000f3c4143c7cbba`.
The independent audit preserved 370 preparation graph anchors and rehashed
56 local artifacts; a separate Windows-side rehash found zero mismatches.
The accepted 31-case corrected-profile checkpoint, stock Cessna, H10
servo-only bench, negative-only C1E-N1 acceleration and physical gates are
unchanged. Vehicle-specific allocation and tuning still await pending CAD
and measured canard, servo and EDF data.
