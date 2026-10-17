# SLAAPS SITL Architecture

```text
             ┌───────────────┐
             │    Gazebo     │
             └───────┬───────┘
                     │
                simulation
                     │
             ┌───────▼───────┐
             │   PX4 SITL    │
             └───────┬───────┘
                     │
                 MAVLink
                     │
                UDP / WSL2
                     │
             ┌───────▼──────────┐
             │ QGroundControl   │
             │    Windows       │
             └──────────────────┘
```

## Interfaces

Gazebo Sim 8 runs the `default` world and the stock `rc_cessna` model. Its 250 Hz physics loop supplies simulated IMU, GNSS, barometer, and magnetometer data. PX4's `gz_bridge` sends actuator commands back to Gazebo and receives the simulated sensor and model state.

PX4 SITL selects airframe autostart 4003 for `gz_rc_cessna`. The airframe identifies itself as fixed-wing, uses synthetic airspeed, assigns one motor, and exposes six fixed-wing control surfaces. PX4 estimation, navigation, control allocation, and MAVLink run as native Ubuntu processes.

The normal GCS MAVLink instance listens in WSL2 on UDP 18570 and nominally uses remote port 14550. Native Windows QGroundControl initiates a UDP connection to the current WSL IPv4 address on port 18570. WSL2 NAT addresses can change after a restart, so discover the current address with `scripts/sitl/get_wsl_network_info.sh`.

WSLg carries the Gazebo GUI and GPU rendering from Ubuntu to the Windows desktop. It does not carry MAVLink; QGroundControl traffic follows the WSL2 IP network path.

## Regression and future model path

```text
gz_rc_cessna
      ↓
known-good PX4/Gazebo regression baseline

gz_advanced_plane
      ↓
advanced aerodynamic coefficient reference

separate SLAAPS model and PX4 airframe
      ↓
EDF propulsion, validated mass/inertia and CG,
canard aerodynamics and allocation, guidance integration
```

`gz_advanced_plane` is a better coefficient scaffold than the simple Cessna model because it exposes longitudinal/lateral stability derivatives, rate derivatives, stall terms, wing geometry, and explicit surface coefficients. It is still a demonstration model with placeholder mass properties and a Cessna mesh, so it is a reference—not the final interceptor. The Cessna must remain unchanged and runnable throughout future development.
