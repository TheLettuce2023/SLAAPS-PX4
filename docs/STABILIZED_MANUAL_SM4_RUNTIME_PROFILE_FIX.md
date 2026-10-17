# SM4 runtime-profile correction and requalification

2026-10-05, Codex. Goal active. Measurements remain unavailable and CAD is
pending. This work preserves stock Cessna, generic bridge/EKF sources, H10
bench and negative-only C1E-N1.

## Configuration defect and historical scope correction

`Fixture.__init__` concatenated the startup script from three strings, but
`.replace(...)` applied only to the final suffix. The parameter marker was in
the prefix, so **bias overrides were absent** while metadata claimed otherwise.
The correction applies replacement to the whole concatenated script.

Inspection finds no explicit gyro-prior lines in any of the 18 startup files in
`research-stabilized-sitl-r27tx0nr`, eight in `...-zacmmnz6`, or 19 retained
attempts in `...-edg6vbe0`. A continuous trace confirms actual initial values
0.10000000149 rad/s and 0.20000000298 m/s^2, agreeing with stock definitions.
Historical sealed files and numeric results are preserved. **Claims that these
runs verified matched bias priors or that prior changes caused improvement are
withdrawn.** Recorded behavior belongs to the actual startup/default profile.

New runs call native read-only `param compare` for both priors and magnetic
fusion type before enable and record `runtime-profile-check.json`. The unchanged
float comparison tolerance is 1e-7. Campaign, coupled and state-barrier sealers
now require this proof. Metadata alone cannot establish runtime configuration.

## Continuous observation

Standard PX4 logger records real sensor, integrated IMU, estimator, aiding and
bias topics in each new isolated case's `fs` directory. Truth is only used
offline. The trace `research-stabilized-sitl-coupled-diagnostic-3atw8w78` has
7,620,388 bytes and zero logger dropouts. Gyro residual RMS against physical
rates is [0.000338, 0.000341, 0.000337] rad/s, agreeing with declared 0.0003394;
accel RMS is [0.003994, 0.003958, 0.003970] m/s^2, agreeing with declared 0.004.
Public attitude sample age is at most 16 ms. The delayed internal EKF state is
a different signal, about 152-168 ms old. Calibration counts stay gyro 1/accel 0.

Gazebo NavSat adds no noise here, but the unchanged bridge always adds Markov
position/velocity noise. Its velocity process is
`v_noise = 0.85*v_noise + 0.2*0.05*N(0,1)` (vertical amplitude x1.2).
Describing the complete GNSS path as noise-free was incorrect. Actual restrained
GNSS observations span approximately [-0.068, -0.047, -0.069] to
[0.059, 0.056, 0.059] m/s. This noise and GNSS aiding remain enabled.

Four same-clock dispatcher events occur in this trace. Offline audits find
unique control publications and no consumer replay/lease fault around them.
The maneuver fails physical final/peak limits; component observations cannot
turn it into a passing maneuver. Audit source/report versions are hash-labelled.
The initial audit's weaker intermediate assertion is superseded; its source
was recovered and checked against original SHA-256
`9f0a03f26bd8f77714f23a89bc9926a2e7cfbe55442c51c5fca0d230c22bdf37`.

## Exploratory comparisons and next qualification

All cases below use the unchanged ELF
`d581f60212711fbd50d31bd28bb47b90e9529ac79a46c8631b3da24d1fbae44f`
and plugin `f99655e5e9780179f85fc812deae71640335f4f908f734cbd765c60ac50bb466`.
They cannot replace the failed source-bound matrix.

| Directory suffix | Actual gyro / accel prior | Nose-up, negative-first result |
| --- | --- | --- |
| 3atw8w78 | 0.1 / 0.2, ULog confirms | Final 0.022516 rad and peak 0.509026 rad fail |
| akdk4jzr | 0.003394 / 0.04, live checks and ULog agree | Final 0.024065 rad fails |
| f77xbk4z | 0.0003394 / 0.04, live checks agree | All numerical checks pass; final 0.003671 rad |

The finite gyro prior at the raw-noise scale is an unaccepted candidate for this
known zero-bias fixture. Accel remains ten times raw noise. These are declared
synthetic calibration-confidence assumptions, not installed sensor calibration.
Raw noise alone cannot establish an installed sensor's bias uncertainty.

A private `dispatch` diagnostic calls the real work queue's `ScheduleNow()`
without granting enable or output permission. The deterministic regression pauses
physics and repeats dispatches at a frozen clock, requiring no publication,
healthy Hold, clean resume and consumer disable. No fake state or clock is
published. Core freshness/regression/delay checks remain unchanged.

Requalification below completes the fresh private build, deterministic dispatch
proof, four coupled cases, 12 nominal/six fault cases and eight state barriers.
Limits stay 0.08 rad/s tracking, 0.02 rad final angle, 0.02 rad/s final rate and
0.5 rad peak. The declared corrected-profile synthetic scope now passes.
Preserve all predecessors, including aborted `...-wa47tscg`, which exposed the
missing startup parameter before any processes started.

## Qualified corrected-profile checkpoint

**31 actual native cases PASS**, with 68,726 audited published control cycles:
one frozen-clock dispatch regression (335 cycles), four coupled full-axis
rate/reversal/release/impulse cases (25,873), 12 individual-axis plus six
fault/recovery cases (38,574) and eight native state barriers (3,944).
Each case verifies actual gyro prior 0.0003394 rad/s, accel prior 0.04 m/s^2
and magnetic type 0 before enable. GNSS bridge noise remains enabled.
All scoring thresholds are unchanged.

| Coupled case | Final physical error, rad | Final estimated error, rad | Peak physical error, rad |
| --- | ---: | ---: | ---: |
| Level / + first | 0.009438 | 0.003447 | 0.490874 |
| Level / - first | 0.003143 | 0.002993 | 0.491083 |
| Body-X-up / + first | 0.003280 | 0.002958 | 0.490897 |
| Body-X-up / - first | 0.008674 | 0.003585 | 0.496677 |

Worst coupled rate tracking is 0.028713 rad/s against 0.08. Its 1,263
box-saturated cycles and blocked-axis counts [777, 2062, 2062] have zero
anti-windup violations. Individual-axis final physical error is at most
0.005685 rad and rate tracking 0.021816 rad/s. Peak margin in the strongest
disturbance is small; this is finite declared-case evidence, not a universal
robustness claim.

Five deliberately repeated real work-queue dispatches at one frozen controller
clock leave published count 276 -> 276; Hold stays healthy and resume passes.
The recorded physics-time field is the last flushed truth-row observation,
which can lag the frozen controller clock; equality of actual event clocks and
absence of publication establish the tested dispatch behavior.
Suspending PX4 while physics remains live expires the independent lease exactly
100,000 us after its last command. All six faults reject re-enable (255) and
require a deliberate new owner session before Hold recovery.
State-barrier tests revoke withdrawal/restart at sample ages 20-24 ms, detect
actual reset counter 1 -> 2 without publishing across it, and deliver consumer
off 4 ms after each revoke. Same-ID/between-poll and sensor-diverse handoff
qualification remain open.

Fresh strict plugin and PX4 linkage pass; scoped AStyle and Python syntax pass.
ELF: `a2c8229b30a93434f6d6aae3170fc67f354227e5ce236eca83720d7b79397f7d`.
Plugin: `795a826f771dfd5cfe3b56c719f0f036f897184457fbe4c132b917d4c5c2f044`.
Private owner: `838ed83c2f62136775f73b71318d27aa17436c8e6690bfa4cae277a862ea1d4b`.
The core, allocator, stock PID, original model and hardware defaults remain
unchanged. A verifier-only cleanup correction before any tests is archived
with its before/after checkpoint; compiled inputs did not change.

| Evidence under /var/tmp | Manifest | SHA-256 | Independently rehashed artifacts |
| --- | --- | --- | ---: |
| research-stabilized-sitl-rjr0eutz | dispatch-manifest.json | fc7fe2f45cf093ccb5473d1d3c2e93ad53e96a4b500fcf9f4c8029a64192fd8e | 26 |
| research-stabilized-sitl-rjr0eutz | coupled-manifest.json | 40753f83d103cfca58cc65f2f2481102185521fa20bf0d3671ac72037a3c2aa2 | 232 |
| research-stabilized-sitl-rjr0eutz | campaign-manifest.json | 447823f2cdb88e22759d58b9365cd0508370053738011aa966fc452b1c050da3 | 598 |
| research-stabilized-sitl-qmf78glt | state-barrier-manifest.json | 1536d2f78598e942746b4a9e52e2194eb1a151c705e908aca82450f432698948 | 356 |

All four receipts are accepted at their named synthetic scopes with zero hash
failures and physical authority false. Each campaign checkpoint preserves 332
hash-bearing graph anchors. The eight-case child reuses exact parent binaries
and source; it is not a second build. No commit, staging or physical action.

Next software work: repeatability, mixed-axis inputs, bias/delay/transport
perturbations and broader publisher/epoch identity qualification. Vehicle-supported
allocation/tuning still needs CAD, canard effectiveness/signs, loaded servo and
EDF measurements; the user says they are unavailable. Preserve all predecessors,
including their corrected historical scopes. Goal active; no live test/build
process remains. Clearing history is safe with workspace and WSL evidence kept.
