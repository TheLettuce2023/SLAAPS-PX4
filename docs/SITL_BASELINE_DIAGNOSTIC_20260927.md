# Stock Cessna SITL baseline diagnostic — 2026-09-27

**Result: incomplete for roadmap Phase 1.** This is the first serialized GUI
and headless stock `gz_rc_cessna` diagnostic in this checkout with a recorded
PX4 revision and executable hash.
Both runs stayed disarmed. The recorded build, runtime, ULogs and shutdowns are
useful evidence, but the Phase 1 acceptance criteria and human GUI observation
are not complete. No research vehicle, custom controller or flight mission ran.

## Source, build and evidence identity

| Item | Recorded value |
| --- | --- |
| Parent branch/revision | `slaaps-sitl` / `e880f1cd79e2453ef3dfaf68ee1c067b98ea9a53` |
| Separate Gazebo repository | `Tools/simulation/gz` at `bb0b9cf974acf4f1bcb5f5fcf80b88841562dea9`, clean afterward |
| Stock airframe/world/model | autostart 4003 / `default` / `rc_cessna_0` |
| Host | Windows `10.0.26200.0`, Ubuntu 24.04.5 on WSL2, Gazebo Sim 8.15.0 |
| Source/build locations | isolated `/var/tmp/slaaps-baseline-20260927-01/{source,build}` bind mounts; build output on native ext4 |
| Build | `make px4_sitl_default`, 1,232/1,232 Ninja steps, `RelWithDebInfo` |
| Executable | `build/px4_sitl_default/bin/px4`, 58,602,888 bytes, SHA-256 `b7920bc1c205318fde658a07f2510d038e2e21eca9c2f0a1a8abf45777487ea4` |
| Evidence directory | `/var/tmp/slaaps-baseline-20260927-01/evidence/`, with a 32-file SHA-256 inventory and retained ULogs |

`plan.json` was written before build/runtime. `git-status-before.txt`,
`git-status-after.txt` and `git-status-delta.txt` show no checkout status change
from the simulator runs, before this record was written. The curated research
graph's 39 source anchors matched its
recorded hashes before the run. The linked executable and ULog `ver_sw` bind
the observations to the parent revision; the worktree still contains extensive
untracked research files. A complete immutable dirty-content archive was not
created, so the source package is not yet independently reproducible.

The first compile attempt failed immediately because a one-shot WSL mount
setup ended before `make` ran, leaving it in an empty directory. Its
`build.log` is retained. The corrected run-scoped `with_mount.sh` kept the
source and build mounts alive through the command, and `build-02.log` records
the successful build. The root-owned wrapper unmounted both paths after each
run; the preflight script itself did not finish because its full Git diff over
the Windows 9p mount stalled. Targeted environment, source, submodule, process
and network checks were recorded separately.

## Serialized runs

| Observation | GUI | Headless |
| --- | --- | --- |
| Command | `make px4_sitl gz_rc_cessna` | `HEADLESS=1 make px4_sitl gz_rc_cessna` |
| Gazebo processes | server and GUI | server only; no GUI process in the process snapshot |
| ULog duration / logger dropouts | 9 min 1 s / 0 | 2 min 2 s / 0 |
| ULog size | 124,423,120 bytes | 28,359,375 bytes |
| ULog SHA-256 | `192a876bd3ffb2e8b9006a33bc994636cb618eed52035eacf1b79279a02d97dc` | `98f2c04481a494d0d2bf7cdb0e81c6ab3295be130218738852cee68d01f9be7f` |
| Selected ULog topics | 14/14 present | 14/14 present |
| GCS link | valid heartbeat, partner `172.22.32.1`, inbound MAVLink; preflight changed from failed to OK | valid heartbeat and inbound MAVLink; preflight OK |
| Shutdown | PX4 shell `shutdown`; graceful SIGINT to recorded Gazebo PIDs 15291/15290 | PX4 shell `shutdown`; graceful SIGINT to recorded Gazebo PID 824 |
| Cleanup | wrapper exit 0; no owned PIDs or mounts remained | wrapper exit 0; no owned PIDs or mounts remained |

The GUI and headless transcripts contain Commander, bridge, allocator and
MAVLink status plus timestamped listeners for position, GNSS, estimator flags,
IMU, barometer, magnetometer, attitude, actuators, airspeed and differential
pressure. Logged position and GNSS timestamps advanced across separate
observations. `vehicle_status` remained disarmed, and disarmed motor `NaN`
values were treated as the declared mode behavior. The `ulog_info` outputs
report zero logger dropouts. The measurement-only `analyze_ulog.py` reports
zero nonfinite values for selected position, attitude, GNSS, barometer and
magnetometer fields; it does not apply an approved freshness deadline.

The GUI ULog contains **29 equal consecutive `vehicle_attitude` timestamps**
and no backward steps; headless contains neither. Every equal-timestamp pair
has a different `timestamp_sample` and different quaternion, so these are not
identical records repeated by the logger. A follow-up ULog read found that all
29 pairs have consecutive sample times exactly 4,000 us apart: at publication
time `T`, the first sample is `T - 4,000 us` and the second is `T`. All
135,257 GUI attitude sample timestamps increase strictly; the 135,272 logged
`sensor_combined` timestamps also have no equal or backward step. The equal
attitude publication timestamps span 29.884 to 537.384 simulated seconds.
PX4's EKF2 takes the attitude sample time from `sensor_combined.timestamp`
and separately stamps publication with `hrt_absolute_time()`; Gazebo's clock
callback advances that monotonic clock. These source and ULog facts are
consistent with two successive estimator outputs during one simulated clock
tick. The exact scheduling trigger and acceptance policy remain unresolved;
the isolated GUI `vehicle_imu` timestamp error at 81.796 s does not explain
all 29 events. The maximum logged local-position timestamp gap was
12 ms in GUI and 8 ms headless; the maximum logged GNSS gap was 1.024 s in
both. These are intervals between *logged* records, not proof of source
publication rates or an approved operational deadline. Each run began with
nonfinite calibrated/true airspeed while `airspeed_source=-1` (two GUI and five
headless records), then reported source 1. `sensor_airspeed_sim` was running.
Gazebo listed the optional airspeed topic because the bridge subscribed, but
`gz topic -i` reported **no publisher** on it. The operational stock airspeed
path therefore remains synthetic rather than an independent Gazebo sensor.

QGroundControl 5.0.3 was launched visibly and PX4 received its heartbeat, but
the requested human checks of parameter loading, live attitude, GPS/map, mode
and MAVLink Inspector are **pending**. The GUI run saved an 85-byte BSON
parameter file. The headless startup imported that file; both saved BSON
snapshots have SHA-256
`198fdccbc5950c2c5715970f4c6897649a49a3a9811178d8b7b4c87cb9e1fdf1`.
The ULog parameter exports differ in a few runtime values, including magnetic
declination. The next reproducibility campaign must freeze or explicitly
reset the per-run parameter image rather than infer identical startup state
from the common executable.

## Phase 1 closure work

The read-only [v0 diagnostic auditor](../Tools/research_sitl/baseline/README.md)
now verifies all 32 inventoried evidence files and both ULogs by byte count
and SHA-256, plus selected build/run/QGC/measurement cross-links. It returns
`incomplete` on this preserved package; four synthetic integrity and false-pass
regressions pass. Its `integrity: verified` result covers stored bytes and
links, not independent recalculation of ULog contents or authentic runtime
provenance. It cannot issue Phase 1 pass, and it is not the integrated
acceptance checker below.

1. Record the human QGroundControl observations against this GUI run; a GCS
   heartbeat alone cannot close those checks.
2. Approve numerical observation windows, maximum ages/deadlines and semantic
   exceptions from requirements, then implement a fail-closed integrated-SITL
   manifest/checker. The current ULog analyzer is measurement-only.
3. Determine why GUI scheduling produced 29 same-tick attitude publication
   pairs and approve how a baseline checker treats ordered sample timestamps
   when publication time is equal. Preserve both timestamps in scoring.
4. Pin a clean parameter image and complete dirty-source/model artifact
   provenance. Repeat GUI/headless from fresh processes under that plan, then
   have another operator reproduce the baseline.

The artifact inventory and full logs remain under `/var/tmp`, outside Git;
their hashes are recorded there and above. Preserve that directory until the
evidence is archived. This diagnostic does not advance the custom vehicle's
plant, control or flight gates.

## Follow-up GUI observation run (same operator)

A separately planned, disarmed stock Cessna GUI observation ran later on
2026-09-27 from the same executable hash and the pinned 85-byte parameter
image (SHA-256 `198fdccbc5950c2c5715970f4c6897649a49a3a9811178d8b7b4c87cb9e1fdf1`).
The plan, terminal log, parameter snapshot, measurement JSON and result are in
the campaign root as `gui-observation-02-*`, outside the original `evidence/`
directory, so its 32-file inventory and auditor result remain unchanged. This
was an observation opportunity, **not** the required second-operator repeat or
a Phase 1 acceptance run.

PX4 was disarmed in Hold and reported a valid GCS heartbeat with 19 inbound
messages at the recorded status check. The GUI ULog
`2026-09-27/22_37_35.ulg` is 61,491,080 bytes with SHA-256
`e3df819f6adf1495a82305533a1746457e9f5a8cccb37a0005a13212eeba4eb2`.
The measurement-only analyzer found 14 selected topics, zero logger dropouts,
and 10 equal attitude publication timestamps. Independent raw-ULog inspection
confirmed all 10 have distinct, increasing samples exactly 4 ms apart with
the same `T - 4 ms`, then `T` pattern. Logged `sensor_combined` timestamps had
no equal or backward step. This reproduces the phenomenon in GUI, but does
not establish its scheduling trigger or an approved acceptance exception.

PX4 exited via shell `shutdown`, verified owned Gazebo PIDs 848 and 849
received SIGINT, the wrapper exited 0, and no owned PX4/Gazebo process or bind
mount remained. The parameter image had the same hash after the run.

The observer later reported that QGroundControl showed **“Comms Lost.”** It is
not yet known whether that warning appeared during the live simulator interval
or after the documented shutdown. PX4's `vehicle_status` marked GCS lost only
in its first two startup records (5.332 and 5.836 simulated seconds), then
false through the last status at 272.836 s; MAVLink instance 0's inbound count
rose from 1 to 285. Those PX4-side facts do not resolve the QGC-side warning.
The five requested visual checks and event timing remain incomplete. The
original `gui-observation-02-result.json` is preserved, with the human report
and log comparison in `qgc-user-report-02.json` and
`gui-observation-02-addendum.json`, linked by hashes outside Git.

Candidate logged-data limits and the live-QGC warning policy are in the
[diagnostic-only proposal](SITL_BASELINE_DIAGNOSTIC_LIMITS_PROPOSAL.md). None
are approved for Phase 1 acceptance.

## Timed live QGroundControl recheck (same operator)

A third GUI run, `slaaps-baseline-20260927-03-gui-recheck`, used the same
executable and pinned parameter-image hashes. While PX4/Gazebo were explicitly
live, the user reported **connected** QGroundControl with all five requested
items visible: loaded parameters, live attitude, GPS/map, mode, and MAVLink
Inspector traffic. The report is saved as `qgc-user-report-03.json` with the
run ID and observation time. This establishes the human visual observation
for this separate run; it does not resolve when the prior run's “Comms Lost”
warning appeared.

PX4 remained disarmed in Hold. At final status, MAVLink instance 0 had a valid
GCS heartbeat and 179 received messages. The ULog
`2026-09-27/22_52_25.ulg` is 38,621,894 bytes, SHA-256
`f10dd002c0f6c681e575ff1a61d1ada23b5429060ed67b400bdd596bd86db5b0`.
It contains 14 selected topics, zero logger dropouts, zero GCS-loss status
samples and five equal attitude publication timestamps with distinct samples.
PX4 exited via shell `shutdown`; verified owned Gazebo PIDs 829 and 830
received SIGINT; the wrapper exited 0, with no PX4/Gazebo processes or bind
mounts left. The parameter image hash was unchanged. The plan, terminal log,
parameter snapshot, ULog measurements, user observation and hash-linked
`gui-observation-03-result.json` are in the campaign root outside the original
32-file inventory. The result remains **Phase 1 incomplete**: the draft limits
were not yet approved when this run began, source provenance is incomplete,
no integrated checker has
scored this run, and no second operator has reproduced the baseline.

## Independent raw ULog reinspection

The [raw ULog inspector](../Tools/research_sitl/baseline/README.md) now
recomputes timestamp order, maximum logged gaps, finite-field counts and
equal attitude publication-time pairs from each ULog. It verifies a supplied
ULog SHA-256, reads 15 selected topic instances including `sensor_combined`,
and compares the 14 common topics with the archived measurement JSON. All
four retained ULogs matched the archived reports' selected timing,
finite-count and attitude-pair fields: original GUI **29**
attitude pairs, headless **0**, first follow-up GUI **10**, timed live-QGC GUI
**5**. Each had 15/15 selected raw topics, zero logger dropouts, no missing
topics, strictly increasing attitude sample timestamps, and no equal or
backward `sensor_combined` timestamps. The four final reports and
`raw-inspection-campaign-summary.json` are in the campaign root, outside the
original 32-file inventory; the summary SHA-256 is
`2cb3fffe8ae19b95ec0785de6ae3f031069bf7369d14660e5b9efc4838b0b297`.

Eleven focused auditor/inspector regressions pass. This is a measurement and
consistency milestone, not acceptance scoring: there is no predeclared scored
interval, approved timing registry, complete dirty-source archive or
second-operator campaign. Both measurement scripts use PyULog; this is
independent metric recomputation, not an independent ULog decoder. The
inspector returns `incomplete` for matching ULogs and cannot grant Phase 1
pass.

## Subsequent diagnostic-only approval

On 2026-09-27, the project user approved the
[draft diagnostic limits](SITL_BASELINE_DIAGNOSTIC_LIMITS_PROPOSAL.md) as
written for future stock Cessna campaign
`slaaps-stock-cessna-phase1-acceptance-01`. The
[approval record](SITL_BASELINE_DIAGNOSTIC_LIMITS_APPROVAL.md) binds the exact
proposal and revision-1 registry hashes. The approval accepts equal attitude
publication timestamps only with ordered distinct sample times, finite
quaternions, both publication ages within 8 ms, and pair reporting. It treats
live QGC “Comms Lost” as a failure and unknown warning timing as incomplete.
The original and follow-up ULogs remain diagnostic evidence; none had a
predeclared scoring interval or complete provenance. No Phase 1 pass is claimed.
