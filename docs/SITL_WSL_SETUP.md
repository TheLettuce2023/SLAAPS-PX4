# PX4 Gazebo SITL on Windows 11 and WSL2

This project runs PX4 and Gazebo natively in Ubuntu 24.04 under WSL2. QGroundControl runs natively on Windows 11. Docker and Gazebo Classic are not part of this baseline.

## Verified baseline

- Ubuntu 24.04.5 LTS, WSL 2.7.3.0, kernel 6.6.114.1-microsoft-standard-WSL2
- WSLg 1.0.73 with X11 and Wayland available
- CMake 3.28.3, Python 3.12.3, Git 2.43.0, Ninja 1.11.1
- Gazebo Harmonic / Gazebo Sim 8.15.0
- PX4 `e880f1cd79e2453ef3dfaf68ee1c067b98ea9a53`
- Stock `gz_rc_cessna`, PX4 autostart 4003
- QGroundControl 5.0.3 on Windows

## Filesystem note

PX4 recommends building in the native WSL filesystem, such as `~/projects`. This checkout is intentionally in the requested current Windows directory and appears in Ubuntu below `/mnt/c`, a DrvFS/9p mount. That location is slower and CMake writes can fail with `Operation not permitted`.

The launcher preserves the Git checkout in this directory while bind-mounting only generated `build/` and `.vscode` output onto native ext4 storage under `~/.cache/slaaps-px4`. It asks for the Ubuntu sudo password when mounts must be created. Mounts disappear when WSL shuts down and are safely recreated by the launcher. No source is relocated or deleted.

For best long-term performance, a future migration to a native Linux directory may be considered explicitly; do not silently move this checkout.

## Initial setup and dependencies

Initialize the exact submodule revisions pinned by this checkout:

```bash
git submodule sync --recursive
git submodule update --init --recursive
```

Use only the setup script from this checkout. SITL does not need the NuttX cross-toolchain:

```bash
bash ./Tools/setup/ubuntu.sh --no-nuttx
```

If running the setup script directly from this repository's spaced `/mnt/c` path triggers its unquoted-path failure, expose this exact checkout through a temporary bind mount and run the same checked-in script:

```bash
sudo mkdir -p /tmp/slaaps-px4-mount
sudo mount --bind "$(git rev-parse --show-toplevel)" /tmp/slaaps-px4-mount
cd /tmp/slaaps-px4-mount
bash ./Tools/setup/ubuntu.sh --no-nuttx
```

Log out/in or restart WSL if the setup script requests it.

## Repeatable startup

From Ubuntu/WSL2 in the project directory:

```bash
./scripts/sitl/check_environment.sh
./scripts/sitl/get_wsl_network_info.sh
./scripts/sitl/run_fixed_wing_sitl.sh
```

The launcher transparently ends with:

```bash
make px4_sitl gz_rc_cessna
```

For an isolation test without rendering, use the underlying command through the prepared mount:

```bash
cd /tmp/slaaps-px4-mount
HEADLESS=1 make px4_sitl gz_rc_cessna
```

Headless operation is diagnostic only; the validated target uses the WSLg Gazebo GUI.

## Runtime checks

At the PX4 `pxh>` prompt:

```text
commander status
commander check
gz_bridge status
control_allocator status
mavlink status
listener vehicle_status -n 1
listener vehicle_local_position -n 1
listener vehicle_global_position -n 1
listener sensor_gps -n 1
listener estimator_status_flags -n 1
listener actuator_motors -n 1
listener actuator_servos -n 1
```

A correct baseline reports `SYS_AUTOSTART=4003`, Gazebo model `rc_cessna_0`, `vehicle_type: 2`, fixed-wing estimator status, valid GNSS/global/local position, and fixed-wing control allocation. Motor output is `NaN` while disarmed; servo topics and bridge channels still demonstrate configured actuator outputs.

At a second Ubuntu shell:

```bash
ss -lunp
./scripts/sitl/get_wsl_network_info.sh
```

## Windows QGroundControl

Start QGroundControl natively on Windows. In QGroundControl open:

```text
Application Settings
→ Comm Links
→ Add
→ UDP
```

Name the link `PX4 WSL SITL`. Add the current WSL IPv4 address as the server address with server port `18570`; retain QGC's normal local/listening port `14550`, save, and connect. Always rerun `get_wsl_network_info.sh` after WSL restarts instead of hard-coding an address.

After connection, `mavlink status` instance 0 must show a valid GCS heartbeat and nonzero RX traffic. In QGC verify parameter loading, live HUD attitude, GPS/map position, current flight mode, and traffic in MAVLink Inspector. These GUI observations require a person viewing QGroundControl; Linux-side counters alone do not prove them.

## Basic fixed-wing test

Do not disable safety or estimator checks. Once `commander check` passes and QGC has loaded parameters:

1. In QGC Plan view, create a short mission containing a fixed-wing takeoff, safe-altitude waypoint/loiter, and landing sequence.
2. Upload the mission and confirm the home position and mission altitude.
3. Arm normally, start the mission, and observe the automatic launch/takeoff behavior.
4. Confirm attitude, position, airspeed, mode, and actuator telemetry during flight.
5. Command a normal mode change such as Hold/Loiter, then resume only if conditions are healthy.
6. Complete the landing or use the simulator's normal safe termination procedure.

This test requires explicit human supervision in the QGC GUI and is not implied by a successful build.

## Troubleshooting order

1. Run `mavlink status`; confirm the Normal instance is UDP local 18570, remote 14550, and inspect TX/RX.
2. Run `ss -lunp`; confirm PX4 owns UDP 18570.
3. Rerun `get_wsl_network_info.sh`; update the QGC server address if WSL changed it.
4. Check whether WSL uses NAT or mirrored networking and whether Windows can reach the WSL address.
5. Confirm QGC is targeting the observed address and port, not a stale address.
6. Check localhost/broadcast behavior. PX4's default no-broadcast warning is expected when QGC initiates the link to 18570.
7. Inspect Windows Defender Firewall rules for the QGroundControl executable. Never disable the firewall globally; use only a narrowly scoped rule if evidence shows it is needed.
8. Diagnose rendering separately: inspect `DISPLAY`, `WAYLAND_DISPLAY`, `XDG_RUNTIME_DIR`, the Wayland socket, and Gazebo/OpenGL output before changing packages.

The stock model may emit SDF `gz_frame_id` compatibility warnings at spawn. Repeated bridge, transport, sensor timestamp, or rendering errors are not baseline success and should be reproduced and diagnosed rather than hidden by model edits.
