# Stock SITL Baseline Reproducibility

Update 2026-09-27: a first serialized GUI/headless diagnostic was run and
recorded in [SITL_BASELINE_DIAGNOSTIC_20260927.md](SITL_BASELINE_DIAGNOSTIC_20260927.md).
Phase 1 remains incomplete. The inventory and "unexecuted" statements below
describe the original 2026-09-16 task, not the new diagnostic.

Status: inventory and procedure document created for Prompt C on 2026-09-16.
No Gazebo or PX4 process, build, installation, submodule operation, or GUI
smoke test was started by this task.

## Evidence Status And Scope

The vehicle baseline is the stock `gz_rc_cessna` airframe (autostart 4003).
The research vehicle is separate: one fixed-axis EDF, canard controls, no
thrust vectoring, and no main fixed wing. Nothing in this document validates
that research vehicle or changes the stock model.

The following labels are used throughout:

- **Observed** means read-only output collected in this checkout or the
  installed WSL distribution during this task.
- **Documented, unverified** means a value or expected result stated by the
  project documents or source comments but not demonstrated by a runtime test
  in this task.
- **Procedure** means a reproducibility step that remains unexecuted.
- **Present, unverified** means an existing artifact was inventoried without
  proving its producing revision, configuration, or validity.

The task inherited these pre-existing parent-worktree changes and preserved
them: six intent-to-add paths (`docs/PX4_FORK_WORKFLOW.md`,
`docs/SITL_ARCHITECTURE.md`, `docs/SITL_WSL_SETUP.md`, and the three
`scripts/sitl/*.sh` files), plus untracked `docs/CODEX_AGENT_PROMPTS.txt` and
`docs/CUSTOM_VEHICLE_SITL_ROADMAP.txt`. The parent revision and branch were
observed as `e880f1cd79e2453ef3dfaf68ee1c067b98ea9a53` and `slaaps-sitl`.

`Tools/simulation/gz` is a separate Git repository. It was observed clean,
detached, and at `bb0b9cf974acf4f1bcb5f5fcf80b88841562dea9` (`Rename terrain to
ridge (#144)`). Recursive `git submodule status` showed initialized entries at
their pinned revisions (no `-`, `+`, or `U` status markers). This does not
replace a manifest recording every submodule revision.

## Observed Environment

| Item | Observed value | Evidence status |
| --- | --- | --- |
| Windows WSL launcher | `wsl.exe` file version `10.0.26100.8737`; PowerShell command version `10.0.26100.9278` | Observed; two Windows version properties are reported separately |
| Distribution | `Ubuntu-24.04`, installed, WSL version 2; it was stopped before the read-only probe | Observed |
| Ubuntu | Ubuntu 24.04.5 LTS, `VERSION_ID=24.04` | Observed |
| Kernel | `6.6.114.1-microsoft-standard-WSL2` on `x86_64` | Observed |
| Git | `2.43.0` | Observed |
| CMake | `3.28.3` | Observed |
| Ninja | `1.11.1` | Observed |
| Python | `3.12.3` (`python3`) | Observed |
| GNU Make | `4.3` | Observed; not listed in `docs/SITL_WSL_SETUP.md` |
| Gazebo | `gz sim --versions` reported `8.15.0` | Observed; no simulation was started |
| WSLg | `DISPLAY=:0`, `WAYLAND_DISPLAY=wayland-0`, `XDG_RUNTIME_DIR=/run/user/1000` | Observed; GUI use unverified |
| Source mount | `/mnt/c` on `9p` (`C:\` DrvFS/9p), read-write | Observed |
| QGroundControl | Version `5.0.3` on Windows | Documented in `docs/SITL_WSL_SETUP.md`; not independently observed |

The observed WSL values agree with the versions called the “Verified
baseline” by `docs/SITL_WSL_SETUP.md`, but that document's runtime claims remain
unverified below.

## Locations And Existing Artifacts

| Purpose | Location or rule | Evidence status |
| --- | --- | --- |
| Parent source | `C:\CODEX PROJ'S\SNC SENIOR DESIGN\Flight Sim SITL` (WSL `/mnt/c/...`) | Observed checkout location |
| Stock model repository | `Tools/simulation/gz` | Observed separate clean submodule |
| Native launcher cache | `$HOME/.cache/slaaps-px4`; actual WSL user was `kspry04`, so `/home/kspry04/.cache/slaaps-px4` | Documented rule and observed directory |
| Native build cache | `/home/kspry04/.cache/slaaps-px4/build/px4_sitl_default` | Present, unverified prior artifact |
| Source path recorded by cache | `/tmp/slaaps-px4-mount` in `CMakeCache.txt` | Present in metadata, but currently absent/unmounted |
| Native VS Code overlay | `/home/kspry04/.cache/slaaps-px4/source-overlays/vscode` | Observed directory |
| Temporary source bind mount | `/tmp/slaaps-px4-mount` | Documented launcher path; absent during inventory |
| Checkout `build/` | `build/.last_target` only (`px4_sitl_default`) | Observed; no project-local binary tree |
| POSIX parameters | Runtime rootfs `fs/parameters.bson` and `fs/parameters_backup.bson` | Source-generated location; prior cache contains both |
| ULog root | PX4 logger uses `PX4_STORAGEDIR/log`; generated POSIX board metadata sets `CONFIG_BOARD_ROOT_PATH` to `./fs`, making the cache path `rootfs/fs/log` | Source evidence and present cache |
| Runtime stdout/stderr | The launcher does not archive it; the operator must capture the PX4 shell and Gazebo terminal output as manifest artifacts | Procedure requirement |

The native cache contains a prior CMake/Ninja `RelWithDebInfo` tree with 3,431
files and a `bin/px4` executable of 58,574,728 bytes, timestamped 2026-09-15
09:58 in WSL listing output. These facts do not prove it was built from the
current source revision, and the source bind mount recorded by CMake is not
currently mounted.

Six prior ULogs were present in the cache, all **present, unverified** because
no run manifest, source hash, parameter export, or runtime trace was attached
to them:

| Path relative to cache build | Size reported | WSL listing timestamp |
| --- | ---: | --- |
| `rootfs/fs/log/2026-09-15/15_59_29.ulg` | 231,316,074 bytes | Sep 15 10:16 |
| `rootfs/fs/log/2026-09-15/16_20_03.ulg` | 1,073,756,921 bytes | Sep 15 13:19 |
| `rootfs/fs/log/2026-09-15/19_19_26.ulg` | 1,073,755,092 bytes | Sep 15 15:24 |
| `rootfs/fs/log/2026-09-15/21_24_34.ulg` | 708,875,707 bytes | Sep 15 16:16 |
| `rootfs/fs/log/2026-09-16/20_47_24.ulg` | 154,564,043 bytes | Sep 16 14:57 |
| `rootfs/fs/log/2026-09-16/20_59_37.ulg` | 47,579,987 bytes | Sep 16 15:02 |

No ULog was observed in the parent checkout itself. The timestamps are
filesystem listing values, not proof of simulation duration or a valid clock.

## Source And Configuration Evidence

The stock airframe `ROMFS/px4fmu_common/init.d-posix/airframes/4003_gz_rc_cessna`
sets `PX4_SIMULATOR=gz`, world `default`, model `rc_cessna`, enables
`SIM_GZ_EN`, enables `SENS_EN_ARSPDSIM`, assigns one simulated rotor, six
control-surface functions, and maps simulated ESC/servo functions (lines
9-15 and 56-80).

The model `Tools/simulation/gz/models/rc_cessna/model.sdf` declares a 1.5 kg
base link and the stock inertia values (lines 5-18), a 250 Hz IMU, 30 Hz
NavSat, 50 Hz air-pressure sensor, and 100 Hz magnetometer (lines 83-185).
Its Gazebo air-speed sensor block is commented out (lines 213-226), while the
bridge's optional subscription names an `airspeed_link` topic
(`src/modules/simulation/gz_bridge/GZBridge.cpp:290-300`). The model link is
named `airspeed` (line 187), so whether a Gazebo airspeed topic exists is an
explicit runtime check, not a baseline assumption. The model includes stock
LiftDrag, joint-position, and multicopter-motor plugins near lines 764-915;
these are Cessna behavior and are not evidence for the research vehicle.

`GZBridge::init()` requires the simulation clock and pose-info topics before
subscribing to optional IMU, magnetometer, NavSat, barometer, lidar, airspeed,
and odometry topics (`GZBridge.cpp:65-134`). The bridge converts Gazebo sensor
messages into PX4 sensor uORB publications. Its pose callback publishes
explicit `vehicle_*_groundtruth` topics and computes NED position, velocity,
and acceleration (`GZBridge.cpp:461-546`). Its NavSat callback publishes GPS
ground truth before applying the simulated GPS noise model to `sensor_gps`
(`GZBridge.cpp:675-765`).

The POSIX startup script starts `sensor_airspeed_sim` when
`SENS_EN_ARSPDSIM=1` (`ROMFS/px4fmu_common/init.d-posix/px4-rc.gzsim:225-229`).
That module runs at 8 Hz, consumes `vehicle_local_position_groundtruth`,
`vehicle_global_position_groundtruth`, and `vehicle_attitude`, computes body
velocity and dynamic-pressure-like differential pressure, and publishes
`differential_pressure` (`src/modules/simulation/sensor_airspeed_sim/SensorAirspeedSim.cpp:55-58,111-169`; header lines 93-99).
The module uses ground-truth translational velocity but estimated `vehicle_attitude`, and the inspected calculation does not subtract wind before deriving body-X dynamic pressure. Its 8 Hz value is the scheduled maximum; publication additionally requires all three subscriptions to update. These limitations matter for wind cases and estimator coupling.
Consequently, the stock operational airspeed path is ground-truth-derived
unless an independently configured sensor path is demonstrated. This is a
provenance limitation, not evidence that the baseline is invalid for its
original stock SITL purpose.

All simulated sensors ultimately use plant truth. The research requirement is that truth enter through an explicit, hardware-representative sensor model with documented uncertainty, noise, latency and limits, and that operational controllers consume the resulting measurements/estimates. Direct truth subscriptions in flight control are prohibited. The stock synthetic airspeed module is not by itself evidence of such validated sensor fidelity or physical airspeed accuracy.

## Unexecuted GUI And Headless Procedures

These are procedures, not completed tests. A future operator must record the
manifest before launch and retain all terminal output.

### GUI smoke procedure

1. In Ubuntu/WSL, run `./scripts/sitl/check_environment.sh`. Record every
   PASS, WARNING, and FAIL; a nonzero exit is not a pass.
2. Run `./scripts/sitl/get_wsl_network_info.sh` and record the current WSL IPv4
   address, gateway, and relevant UDP sockets.
3. Run `./scripts/sitl/run_fixed_wing_sitl.sh`. On the Windows-mounted source,
   it creates or validates the exact source bind mount and binds only `build/`
   and `.vscode` to native ext4 cache paths, then executes
   `make px4_sitl gz_rc_cessna`. It must refuse a mount owned by another
   source. This requires `sudo` for mount creation.
4. Start QGroundControl on Windows. Add a UDP link named `PX4 WSL SITL`, using
   the observed WSL IPv4 as server address, server port `18570`, and the normal
   QGC listening port `14550`. Never reuse a stale WSL address after restart.
5. At `pxh>`, record `commander status`, `commander check`,
   `gz_bridge status`, `control_allocator status`, `mavlink status`, and one
   sample each from `vehicle_status`, `vehicle_local_position`,
   `vehicle_global_position`, `sensor_gps`, `estimator_status_flags`,
   `actuator_motors`, and `actuator_servos`.
6. In QGroundControl, have a person verify parameter loading, live attitude,
   GPS/map position, mode, and MAVLink Inspector traffic. A Linux-side counter
   cannot stand in for these GUI observations.
7. If a supervised simulated mission is authorized, use the documented
   fixed-wing takeoff/waypoint/loiter/landing sequence only after health
   checks pass. Record that this is a separate supervised test, not a smoke
   test or build result.

### Headless diagnostic procedure

First stop the GUI run normally and verify that its owned PX4/Gazebo processes have exited. After the launcher has prepared a reviewed mount, use a terminal at
that mount and run `HEADLESS=1 make px4_sitl gz_rc_cessna`. The checked-in
launcher itself does not implement a separate headless branch; the setup doc
describes this underlying command as a rendering-isolation diagnostic. Record
the same PX4, bridge, process, timing, and log checks. Headless success does
not establish WSLg GUI or QGroundControl connectivity.

For both procedures, a one-sample listener dump is only an identity/value check. Collect a timestamped series across a predeclared observation window to test freshness, continuity and validity. Use normal PX4 shell shutdown and normal Gazebo GUI/server termination, retain exit codes, confirm only the run's owned PIDs have exited, and preserve completed logs. Do not use broad process-name kills. Repeat the serialized procedure from a fresh process start, recording a separate manifest; a second operator must reproduce it before Phase 1 passes. Shutdown, duration, repeat count and acceptance thresholds remain to be specified before executing acceptance runs.

## Runtime Checks And Fail-Closed Evidence

Each run must classify `pass`, `fail`, or `incomplete`; missing evidence is
never silently converted to pass. The following checks are required before a
baseline claim:

| Area | Required evidence | Fail or incomplete condition |
| --- | --- | --- |
| Bridge and clock | `gz_bridge status`, required clock/pose subscriptions, and matching Gazebo/PX4 process records | Missing required topic, bridge error, or no clock; optional sensor absence is recorded and affects only claims that require it |
| Telemetry freshness | MAVLink instance 0 with GCS heartbeat and nonzero RX, plus timestamped PX4 listeners and QGC Inspector observation | No heartbeat, no RX, stale sample beyond the declared manifest deadline, or GUI result absent |
| Sensors/estimator | Valid IMU/GNSS/baro/mag evidence, estimator flags, and finite required fields | Missing/invalid estimator state, non-finite required value, timestamp age beyond deadline, or sensor source not identified |
| Airspeed | `airspeed_validated` source, timestamp, calibrated/true values, differential-pressure source, and whether source is synthetic or sensor | Airspeed source is unknown, stale, non-finite, or claimed as independent while it follows the ground-truth-derived simulator module |
| Groundspeed | `vehicle_local_position.vx/vy` provenance and corresponding MAVLink value | Groundspeed is compared directly to Gazebo truth without frame/time alignment or estimator validity |
| Actuators | `actuator_motors`/`actuator_servos`, bridge channels, armed/disarmed state, and model command reception | Required channel absent, sign/normalization not identified, or motor state is interpreted without accounting for disarmed `NaN` |
| Processes | PIDs/commands for processes started by the run, Gazebo world/model identity, and terminal exit status | An owned process exits unexpectedly, an unknown process is killed, or process ownership cannot be established |
| Shutdown | Normal PX4/Gazebo termination, final status, and preserved logs | Forced kill, orphaned process, partial log, or no final status; classify incomplete unless the failure is itself the tested scenario |
| Evidence integrity | Manifest, hashes, source/submodule revisions, parameter export, logs, stdout/stderr, and checker result | Any mandatory artifact missing, malformed, truncated, inconsistent, or not linked to the run ID |

For every time series, require finite values for fields that must be finite in the current mode, a declared time base and maximum age/deadline, and timestamp ordering appropriate to that signal. Predeclare semantic exceptions such as disarmed/unused actuator NaNs and unavailable HUD airspeed; these must not be treated as unexpected data faults. Distinguish a repeated observation of the same sample from a new measurement or replayed command. Unexpected non-finite values, out-of-order samples, replayed requests, unexplained discontinuities, missing intervals or staleness prevent nominal pass. Missing evidence or an unspecified threshold is incomplete; an observed violation of an approved criterion is fail. This document supplies no invented numeric thresholds.

## Signal Provenance

| Signal/value | Publisher or source | Transformation and frame | Operational use versus scoring |
| --- | --- | --- | --- |
| Gazebo pose/world truth | Gazebo `/world/<world>/pose/info` consumed by `GZBridge::poseInfoCallback` | ENU pose is converted to NED; velocity/acceleration are differentiated in the bridge (`GZBridge.cpp:471-523`) | Scoring/diagnostics only; must not be used by a research controller as an estimate |
| Gazebo NavSat truth | Gazebo NavSat message | Ground-truth GPS is published before bridge noise is applied (`GZBridge.cpp:687-705`) | Scoring/reference only |
| Simulated IMU | Gazebo `imu_sensor`, 250 Hz, noisy model declaration | Bridge converts FLU to FRD and publishes PX4 accelerometer/gyro (`GZBridge.cpp:436-458`) | Sensor input to PX4 estimator |
| Simulated GNSS/baro/mag | Gazebo model sensors and bridge callbacks | Noise/configuration and frame/unit conversion are applied in bridge | Sensor inputs to estimator; source and timestamps must be retained |
| Synthetic airspeed | `sensor_airspeed_sim` | Ground-truth local/global position and attitude produce body velocity and differential pressure at 8 Hz | Operational PX4 input in stock 4003, but not independent evidence; must be marked synthetic |
| PX4 estimates | EKF/estimator uORB topics such as `vehicle_local_position`, `vehicle_global_position`, `vehicle_attitude` | Estimator output from simulated sensor topics and configured parameters | Controller/navigation inputs and primary telemetry source; validity is required |
| MAVLink telemetry | PX4 MAVLink streams from uORB | Transport over WSL UDP (`18570` nominal local GCS port to QGC `14550`) | Transport observation, not a second physical truth source |
| QGroundControl display | Windows QGroundControl rendering received MAVLink | Display may be delayed, filtered, or unavailable; it has no independent sensor provenance | Human-facing observation only; never use HUD appearance as quantitative proof |

### Airspeed And Groundspeed Provenance Check

The checker's report must keep these values separate:

- Synthetic/calibrated airspeed: identify `airspeed_validated.airspeed_source`,
  `calibrated_airspeed_m_s`, `true_airspeed_m_s`, differential-pressure
  timestamps, and the upstream topic. For stock 4003, expect the
  ground-truth-derived `sensor_airspeed_sim` path unless a real Gazebo sensor
  path is explicitly enabled and observed.
- Groundspeed: calculate the expected MAVLink HUD value from the estimator's
  local NED horizontal velocity, `sqrt(vx*vx + vy*vy)`. The source implements
  this in `src/modules/mavlink/streams/VFR_HUD.hpp:75-93`; it is not airspeed
  and is not directly Gazebo pose truth.
- QGC HUD airspeed: the same source emits `NaN` when the validated source is
  not one of sensor sources 1–3 (`VFR_HUD.hpp:83-92`). A visible or missing
  HUD airspeed therefore cannot prove physical airspeed. Record the raw
  MAVLink field and source metadata.

## Run Manifest Schema

Every run must begin with a manifest marked incomplete and a frozen plan containing source/configuration identity, scenario and acceptance thresholds. Append runtime observations, results and artifact hashes after shutdown without changing that plan; finalize an immutable record. The following YAML is a conceptual schema template, not an implemented validator. Wave 2 must define field types, required fields, lifecycle and checker tests; numeric limits must come from approved requirements.

```yaml
schema: sitl-run-manifest/v1
run_id: <UTC timestamp plus unique suffix>
status: pass | fail | incomplete
created_utc: <RFC3339>
operator: <name>
source:
  parent_revision: <40-hex revision>
  branch: <branch or detached>
  worktree_status_before: <porcelain snapshot or hash>
  dirty_content_archive: <tracked diff plus untracked input contents and hashes>
  submodules:
    - path: Tools/simulation/gz
      revision: <40-hex revision>
      dirty: false
environment:
  host_os: <Windows build>
  wsl_distribution: Ubuntu-24.04
  wsl_version: 2
  kernel: <exact uname -a>
  tool_versions: {git: <>, cmake: <>, ninja: <>, python3: <>, make: <>, gz: <>}
  display: {DISPLAY: <>, WAYLAND_DISPLAY: <>, XDG_RUNTIME_DIR: <>}
  source_mount: {path: </mnt/c/...>, filesystem: <9p/other>}
build:
  target: px4_sitl
  model_target: gz_rc_cessna
  generator: <Ninja or other>
  build_type: <exact>
  command: <exact command>
  source_path_seen_by_cmake: <path>
  cache_path: <native cache path>
  executable: {path: <>, sha256: <>, size_bytes: <>}
configuration:
  airframe: gz_rc_cessna
  sys_autostart: 4003
  world: default
  model: rc_cessna_0
  parameter_export: {path: <>, sha256: <>}
  overrides: []
  model_assets: <repository commit plus hashes of model/world/resource files and external plugins>
  scenario: <versioned GUI/headless profile, initial conditions and duration>
  random_seed: <value or null; record unsupported explicitly>
transport:
  mavlink: {local_port: 18570, qgc_port: 14550, wsl_ipv4: <>, qgc_version: <>}
  dds: {used: false, details: null}
timing:
  realtime_factor: <observed>
  command_rate_hz: <observed or null>
  deadlines: {telemetry_us: <approved>, sensor_us: <approved>}
signals:
  - name: <topic or field>
    publisher: <process/topic>
    consumer: <process/topic or scorer>
    frame: <NED/FRD/other>
    units: <units>
    time_base: <clock>
    provenance: gazebo_truth | simulated_sensor | px4_estimate | mavlink | qgc_display
    source_is_synthetic: <true/false>
    expected_rate_hz: <source/measurement or unresolved>
    permitted_nonfinite_states: <explicit field/mode rules>
checks: [{name: <>, result: pass | fail | incomplete, evidence: <artifact>}]
artifacts:
  process_lifecycle: <owned PIDs, executable identity, start/end times and exit codes>
  ulogs: [{path: <>, sha256: <>, complete: <true/false>}]
  stdout: {path: <>, sha256: <>}
  stderr: {path: <>, sha256: <>}
  gazebo_output: {path: <>, sha256: <>}
  qgc_observation: {path: <>, operator: <>, complete: <true/false>}
  listener_dump: {path: <>, sha256: <>}
  network_dump: {path: <>, sha256: <>}
failure_reason: <required when status is fail or incomplete>
review:
  checker_version: <version/hash>
  reviewer: <name>
  notes: <limitations and unverified claims>
```

The checker must reject a `pass` when mandatory revision, configuration,
signal-provenance, timing, process, or artifact fields are absent; a ULog by
itself is never sufficient.

## Concurrency And Reproducibility Limitations

The launcher defaults all worktrees to the same `/tmp/slaaps-px4-mount` and
`$HOME/.cache/slaaps-px4` paths. Its safety check rejects a mount from a
different source, but shared cache/build paths still create races between
operators or worktrees. Concurrent runs can overwrite generated files, use
the wrong source through a stale mount, collide on PX4/Gazebo/MAVLink ports,
or interleave logs. WSL shutdown removes the bind mounts while native cache
contents remain.

Do not run concurrent simulator builds or runs with the defaults. Before any
parallel work, a reviewed launcher configuration must provide a unique source
mount root, native cache root, PX4/Gazebo/MAVLink ports, log directory, and
manifest/artifact directory per worktree. Record those values in the manifest.
The current task did not create such isolation and did not run the simulator.

## Reproducibility Blockers And Next Verification

The baseline cannot yet be called reproducibly demonstrated from this task:

1. The GUI and headless procedures, bridge status, estimator validity,
   telemetry freshness, process lifecycle, normal shutdown, and QGroundControl
   observations are all unexecuted.
2. The current source bind mount is absent, while the existing native CMake
   cache points at that path. The prior executable and ULogs therefore lack
   current-revision/configuration attribution.
3. No manifest, parameter export, captured terminal output, or checker result
   is attached to the six existing ULogs.
4. Stock 4003 enables an 8 Hz airspeed module fed by bridge-published Gazebo
   ground truth, and the model's independent air-speed sensor block is
   commented out. Airspeed independence must be resolved before using it as
   evidence for the research vehicle.
5. The exact QGC version/GUI path, current WSL network endpoint, runtime
   realtime factor, process IDs, and per-topic freshness have not been
   observed in a run.

The smallest next action is a single serialized stock Cessna run after the
operator records the manifest and recreates the documented mount through the
launcher. It must retain the source/submodule revisions, parameter export,
terminal output, listener dumps, network output, ULog, process/shutdown
records, and human QGC observations. Only after that evidence passes the
fail-closed checks should the result be used as the stock baseline; it remains
separate from the custom research-vehicle feasibility and controller claims.
