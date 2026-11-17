# Acceleration Interface and PX4 Source Trace

## Scope and Conclusion

This combines the Wave 1 source audit of checkout `e880f1cd79` with subsequent
offline contract decisions recorded in PHASE_GATE_STATUS.md. The vehicle has
four independently actuated X canards, one rear fixed-axis EDF, no thrust
vectoring/no main wing, and a Raspberry Pi 4B command producer. MAVLink and
inertial FRD acceleration-only requests are selected. The offline wire schema
and synthetic SITL profile have separate accepted records; this source audit
does not approve deployment, operational timing/limits, a flight controller,
allocator or physical model.

## Current Research Contract Decisions

- Only three-component inertial acceleration expressed in FRD is commanded in the research Offboard mode. Position, velocity, attitude, angular rate, torque and thrust are not companion control inputs. Liveness/mode/session metadata is separate from control objectives.
- Host guidance script plus MAVLink is the SITL path; the Pi translator uses MAVLink over FC TELEM serial for hardware. The translator preserves meaning and metadata; PX4 owns stabilization, allocation, mode supervision, outputs and command loss.
- NED is the local world reference. For body-to-NED rotation `R_NB`, `a_B = R_NB^T a_N` and `f_B = a_B - R_NB^T g_N`. Zero inertial acceleration means constant inertial velocity, not zero thrust. Numerical gravity is an explicit supplied input, not a guessed vehicle constant.
- CG reference and body-held FRD interpretation were explicitly confirmed by the user during C0b.2 review (2026-09-17). Held components follow the rotating body axes until superseded or expired, not a world direction frozen at receipt. IMU lever-arm handling and live wire integration still need review.
- DroneCAN pitot, GPS and Pixhawk 6X onboard sensors are planned. Scalar pitot airspeed does not establish angle of attack/sideslip or full air-relative velocity. No operational input may subscribe directly to Gazebo truth.
- Geometry-independent validation/math software may proceed with synthetic fixtures. No operational command deadline, acceleration bound, controller gain, actuator mapping or launch/capture threshold is supplied by a fixture. Required configuration has no flight-ready defaults.

The source audit below describes STOCK behavior, not an already implemented research interface. In particular, `TrajectorySetpoint.acceleration` remains NED; body vectors must never be inserted into it unchanged. Existing BODY_NED reception rotates a request at receipt, which is different from preserving a body-following demand through its validity interval. Any opt-in research adapter must preserve original frame, generation time, expiry, source/session and ordering metadata. Wire message choice/custom-extension need and interoperability tests are unresolved; no MAVLink dialect or receiver change is made in this increment.

The two candidate transports can deliver a `TrajectorySetpoint` containing an acceleration vector and can make Commander select the generic trajectory control mode.
That reception must not be described as full-vector acceleration tracking for the configured fixed-wing path.
The fixed-wing conversion marks position or horizontal-velocity setpoints valid; it parses acceleration only as an optional horizontal-curvature calculation when horizontal velocity and all three acceleration components are finite. Executing the path using that curvature additionally requires position-control mode and a position-plus-velocity request.
It does not consume acceleration-only requests, forward acceleration, or vertical acceleration as a direct tracking command.
The missing component is a reviewed research controller that maps a feasible, bounded vehicle acceleration objective to attitude, EDF thrust, and canard moments before the existing rate-control/allocation/output chain.

## Common uORB Contract Observed in This Checkout

`msg/versioned/TrajectorySetpoint.msg:1-18` defines the common reception target.
It declares position, velocity, and acceleration in the local-world NED frame, with units metres, metres/second, and metres/second-squared, respectively; its `timestamp` is PX4 time since system start in microseconds.
The message states that a `NaN` value means that state is not controlled and that jerk is logging-only.

The message does not establish whether `acceleration` means inertial acceleration, accelerometer specific force, or force divided by mass, nor does it name a reference point.
Its use as a kinematic trajectory input, plus MAVLink's explicit rejection of the `FORCE_SET` interpretation, supports treating the stock field as acceleration rather than force (`src/modules/mavlink/mavlink_receiver.cpp:1294-1298`).
It does not resolve the gravity convention.
No source in this trace removes or adds gravity at reception.

**Physical convention, selected quantity:** define `a_CG_N = dv_NED/dt`, and `f_CG_N = a_CG_N - g_NED`. The selected companion request is `a_CG_B = R_NB^T a_CG_N`, in FRD m/s^2. Constant inertial velocity has zero acceleration; free fall has `a_CG_B = R_NB^T g_NED` and zero specific force. No separate gravity offset is added to the requested vector.
An IMU measures specific force at its own, possibly offset, location. Exact rigid-body/lever-arm conversion remains pending approved geometry, attitude/rate estimates and fidelity requirements; it must not be omitted by treating IMU specific force as CG acceleration.
CG reference, body-held interpretation, inertial quantity and FRD frame are user-confirmed. This is not a claim about stock-controller implementation or a measurement of CG location.

NED axes are North, East, Down; body FRD axes are Forward, Right, Down.
Therefore a producer with ROS ENU values must convert `(x_E, y_N, z_U)` to NED `(y, x, -z)` before populating this uORB message, and a FLU body vector must become FRD `(x, -y, -z)` before a body-to-world rotation.
The DDS mapping does not perform either conversion: it exposes the uORB type directly (`src/modules/uxrce_dds_client/dds_topics.yaml:173-174`).

## Candidate 1: MAVLink Setpoint and Offboard Mode

`MavlinkReceiver` dispatches `MAVLINK_MSG_ID_SET_POSITION_TARGET_LOCAL_NED` to the relevant handler (`src/modules/mavlink/mavlink_receiver.cpp:176-178`).
The handler requires the external-setpoint option and matching/broadcast target system and component (`src/modules/mavlink/mavlink_receiver.cpp:1212-1215`).

For `MAV_FRAME_LOCAL_NED`, it copies unmasked `afx`, `afy`, and `afz` into the NED acceleration array and writes `NaN` for each ignored field (`src/modules/mavlink/mavlink_receiver.cpp:1219-1232`).
The only other accepted frame in this handler is `MAV_FRAME_BODY_NED`.
For that frame it obtains current attitude and rotates the body acceleration vector with `R` into the local setpoint (`src/modules/mavlink/mavlink_receiver.cpp:1234-1275`).
Other frames, including ENU and `MAV_FRAME_BODY_FRD`, take the unsupported-frame rejection path in this checkout (`src/modules/mavlink/mavlink_receiver.cpp:1280-1285`).
Do not label the implemented body conversion as a general FRD conversion merely because FRD is the desired future contract.

The handler derives position, velocity and acceleration mode booleans using `!isAllNan()`, and attitude/body-rate booleans using finite yaw/yaw-rate checks (`src/modules/mavlink/mavlink_receiver.cpp:1194-1203`). The vector test is not all-finite validation: infinity can activate ingress flags although downstream fixed-wing checks use `PX4_ISFINITE()`/`isAllFinite()`.
It rejects the MAVLink `FORCE_SET` bit if acceleration is present (`src/modules/mavlink/mavlink_receiver.cpp:1294-1298`).
It then publishes `offboard_control_mode` with a newly assigned PX4 HRT receipt timestamp.
It publishes `trajectory_setpoint`, also timestamped with HRT, only if `vehicle_status.accepts_offboard_setpoints` is true (`src/modules/mavlink/mavlink_receiver.cpp:1301-1312`).

Consequences:

- The MAVLink path's live-mode timestamp is reception/publication time, not the producer's original command time.
- The standard message contains no PX4-consumed sequence number or expiry/deadline; this handler does not preserve an end-to-end producer-age field.
- Target match, supported-frame, force-bit, and all-fields-unused checks are transport validation; they are not evidence that a controller can follow the requested acceleration.
- The path has no typed per-setpoint acceptance acknowledgement. Its rejection reporting is MAVLink log/event based, so status must be independently observed.

## Candidate 2: ROS 2/XRCE-DDS Topics and Offboard Signalling

The generated DDS topic configuration declares two independent input topics:

- `/fmu/in/offboard_control_mode` with type `px4_msgs::msg::OffboardControlMode` (`src/modules/uxrce_dds_client/dds_topics.yaml:155-156`);
- `/fmu/in/trajectory_setpoint` with type `px4_msgs::msg::TrajectorySetpoint` (`src/modules/uxrce_dds_client/dds_topics.yaml:173-174`).

It also exports `/fmu/out/timesync_status` (`src/modules/uxrce_dds_client/dds_topics.yaml:51-53`).
The configuration is a uORB/DDS type exposure, not a frame or gravity conversion layer.
DDS applies time conversion: the receive template passes `session->time_offset` into each deserializer (`src/modules/uxrce_dds_client/dds_topics.h.em:225-240`). The CDR template converts nonzero `timestamp`/`timestamp_sample` to `min(received_timestamp - time_offset, hrt_absolute_time())`; zero becomes HRT receipt time (`Tools/msg/templates/ucdr/msg.h.em:156-170`). XRCE timesync maintains the offset, or sets it to zero when synchronization is disabled (`src/modules/uxrce_dds_client/uxrce_dds_client.cpp:75-89`). With synchronization enabled, the producer clock must agree with the Agent domain expected by the bridge; pre-converting to PX4 HRT risks double correction. Pin synchronization configuration and test offset convergence, zero substitution, future-time capping and clock discontinuities. The converted timestamp does not independently preserve original producer age.

Because mode liveness and the trajectory payload are separately published, a fresh `OffboardControlMode` can coexist with an old, repeated, or absent `TrajectorySetpoint` unless the companion and a future PX4 consumer separately enforce payload freshness.
This differs from the MAVLink handler, which emits its mode update while handling a setpoint packet, but it does not make MAVLink end-to-end freshness-safe.
Neither stock topic carries the required application sequence, validity interval, producer timestamp, requested reference point, or explicit gravity convention.

## Commander: Mode, Liveness, and Ownership Boundary

When navigation state is Offboard, Commander gives the booleans strict precedence: position, then velocity, then acceleration, then attitude, body rate, thrust-and-torque, and direct actuator (`src/modules/commander/ModeUtil/control_mode.cpp:111-141`).
The acceleration branch selects generic trajectory control, explicitly has no dedicated acceleration flag, enables velocity control, and disables position, altitude, and climb-rate control (`src/modules/commander/ModeUtil/control_mode.cpp:121-128`).
Thus mixed non-`NaN` fields do not express an independently selectable acceleration control mode: position or velocity wins.

Commander marks the offboard signal lost by default, accepts a recent mode message only if at least one mode boolean is true, and compares its timestamp against `COM_OF_LOSS_T` (`src/modules/commander/HealthAndArmingChecks/checks/offboardCheck.cpp:38-54`).
It separately requires a valid attitude estimate for acceleration/attitude offboard (`src/modules/commander/HealthAndArmingChecks/checks/offboardCheck.cpp:86-99`).
This is offboard-heartbeat liveness, not a freshness test for the particular trajectory command, its sequence/order, or its physical feasibility.

Commander obtains `accepts_offboard_setpoints` from mode management; Offboard accepts these setpoints (`src/modules/commander/Commander.cpp:2143-2150`; `src/modules/commander/ModeManagement.cpp:767-779`). On offboard signal loss, Failsafe maps `COM_OBL_RC_ACT` to generic position/altitude/stabilized/RTL/land/hold/terminate/disarm choices (`src/modules/commander/failsafe/failsafe.cpp:264-310,770-782`). Source defaults are `COM_OF_LOSS_T = 1 s` and Position mode for `COM_OBL_RC_ACT` (`src/modules/commander/commander_params.yaml:185-224`). These are source defaults, not observed runtime settings or approved vehicle fallback. Mode entry also depends on Commander health/transition checks; test accepted/rejected transitions and reconnect rather than infer entry from topic publication.

The companion is an intent producer only.
PX4 must remain the owner of estimator selection, mode transitions, control, allocation, actuator outputs, and command-loss response.
Only one producer may be selected for each command stage; publishing both MAVLink and DDS trajectory topics (or allowing a stock fixed-wing manager and a research controller to publish the same downstream setpoint) creates an unreviewed ownership conflict.

## Configured Fixed-Wing Consumer: What Actually Happens

The stock `gz_rc_cessna` airframe declares itself fixed-wing and sources `rc.fw_defaults` (`ROMFS/px4fmu_common/init.d-posix/airframes/4003_gz_rc_cessna:1-7`).
Vehicle setup starts `rc.fw_apps` for `VEHICLE_TYPE=fw` (`ROMFS/px4fmu_common/init.d/rc.vehicle_setup:11-14`), which starts control allocation, fixed-wing rate control, attitude control, mode manager, lateral/longitudinal control, and airspeed selection (`ROMFS/px4fmu_common/init.d/rc.fw_apps:8-20`).

The fixed-wing mode manager does subscribe to `trajectory_setpoint` (`src/modules/fw_mode_manager/FixedWingModeManager.hpp:185-188`).
In Offboard it clears the prior triplet and copies the trajectory timestamp (`src/modules/fw_mode_manager/FixedWingModeManager.cpp:2189-2205`), but its conversion is not a three-axis acceleration controller:

- It sets `valid_setpoint` from NED position X/Y only when the local projection exists (`src/modules/fw_mode_manager/FixedWingModeManager.cpp:2207-2218`), or from finite velocity X/Y (`:2220-2224`).
  An acceleration-only trajectory therefore remains invalid.
- It looks at acceleration only inside that velocity-X/Y branch, only when **all three** acceleration fields are finite (`:2226-2229`).
  It normalizes horizontal velocity, removes the parallel acceleration component, normalizes the horizontal residual for direction, and divides by its norm to derive loiter radius (`:2227-2234`). Zero horizontal velocity and zero normal residual require tests; no runtime outcome is asserted here.
  It does not turn forward acceleration into EDF thrust or a longitudinal objective.
- It processes NED Z position and Z velocity independently (`:2241-2249`), then enables gliding if both are absent (`:2251-2253`).
  There is no acceleration-Z consumer in this conversion.

Conversion validity is not execution. Velocity plus acceleration without position enters Commander's velocity branch and disables the position flag (`src/modules/commander/ModeUtil/control_mode.cpp:117-128`), while AUTO_PATH selection requires that flag (`src/modules/fw_mode_manager/FixedWingModeManager.cpp:387-399`). Position X/Y plus velocity X/Y and full acceleration retain the flag through Commander position precedence; with an initialized projection, AUTO_PATH reads the radius, calls `navigatePathTangent`, and publishes course/lateral-acceleration feed-forward (`src/modules/fw_mode_manager/FixedWingModeManager.cpp:1060-1091`). This is navigation feed-forward, not full-vector tracking. A parsed radius alone does not establish an active path.

Downstream, the lateral/longitudinal controller combines its dedicated lateral-acceleration setpoint, constrains it, and maps it to roll as `atan(lateral_acceleration / g)` (`src/modules/fw_lateral_longitudinal_control/FwLateralLongitudinalControl.cpp:290-302,866-867`).
It then publishes a vehicle-attitude setpoint with a body-X thrust request (`src/modules/fw_lateral_longitudinal_control/FwLateralLongitudinalControl.cpp:311-333`).
Its fixed-wing inputs are a lateral acceleration plus altitude/height-rate/equivalent-airspeed/pitch-direct/throttle-direct interface, not the received acceleration vector (`src/modules/fw_lateral_longitudinal_control/FwLateralLongitudinalControl.hpp:87-88,127-140`).

The fixed-wing attitude controller consumes attitude/rate state and publishes `vehicle_rates_setpoint` (`src/modules/fw_att_control/FixedwingAttitudeControl.hpp:124-137`); rate control consumes it and publishes torque and thrust setpoints (`src/modules/fw_rate_control/FixedwingRateControl.hpp:111-126`).
The rate controller constrains torque to `[-1, 1]`, sends only body-X thrust, and republishes both with fresh controller timestamps based on the angular-velocity sample (`src/modules/fw_rate_control/FixedwingRateControl.cpp:408-439,468-479`).
Those are controller-output timestamps, not the original companion command time.

## Allocation and Physical-Output Boundary

`vehicle_torque_setpoint.xyz` is normalized body-axis torque (not N·m) (`msg/VehicleTorqueSetpoint.msg:3-5`).
`vehicle_thrust_setpoint.xyz` is normalized body-axis thrust in `[-1, 1]`; `NaN` stops motors affecting that axis (`msg/VehicleThrustSetpoint.msg:1-9`).
No physical newton or newton-metre scaling may be inferred from either message.

Control Allocator sets `do_update` only on a new torque setpoint; a thrust update caches thrust and applies motor-stop handling but does not itself trigger reallocation (`src/modules/control_allocator/ControlAllocator.cpp:382-398`). Torque-triggered allocation uses cached thrust in its six-axis vector (`:405-425`); actuator publication follows (`:451-456,736-795`). A new publication of an unchanged torque value still counts as an update. This cadence requires an integration test, particularly for a single EDF.
The mixer output functions subscribe to these two topics (`src/lib/mixer_module/functions/FunctionMotors.hpp:51-65`; `src/lib/mixer_module/functions/FunctionServos.hpp:49-68`) for board/simulator output-function mapping.

For gz SITL, `GZMixingInterfaceESC` publishes mixed ESC outputs to `/<model>/command/motor_speed` (`src/modules/simulation/gz_bridge/GZMixingInterfaceESC.cpp:47-90`). `GZMixingInterfaceServo` maps configured output values to radians and publishes `/model/<model>/servo_0` through `servo_7` (`src/modules/simulation/gz_bridge/GZMixingInterfaceServo.cpp:106-152`). The separate Cessna model declares matching servo-topic joint controllers and a motor plugin consuming `command/motor_speed` (`Tools/simulation/gz/models/rc_cessna/model.sdf:779-915`). Those are stock mappings; research output signs, joints and resulting forces require approved plant data and open-loop tests.

For the research vehicle, the unimplemented mapping is therefore:

`inertial acceleration request -> feasible force/attitude/moment objective -> normalized body thrust/torque -> canard/EDF effectiveness matrix -> actuator_motors/actuator_servos -> configured PWM/simulator outputs`.

The Cessna airframe's one rotor and six stock control-surface settings (`ROMFS/px4fmu_common/init.d-posix/airframes/4003_gz_rc_cessna:56-80`) are evidence of the baseline configuration, not evidence of canard/EDF effectiveness, signs, authority, actuator ownership, or compatibility with the no-main-wing vehicle.

## Transport Decision Matrix

Observed dependency pins: MAVLink `1b1e896aa3be0d591166e086e6b9d8613bd03716` (`1.0.12-952-g1b1e896a`); Micro-XRCE-DDS-Client v2 `711aef423edd1820347b866d1e4164832df35d04`; v3 `c88fb537e89a1095c49a6a535a55378d4c14cf2c`. `CONFIG_UXRCE_DDS_CLIENT_USE_DDS_V3` selects the client tree (`src/modules/uxrce_dds_client/CMakeLists.txt:38-44`); runtime selection is not established by both directories existing. Pi MAVLink library/dialect, OS, ROS 2, `px4_msgs` and Agent versions remain unselected. Pin matching versions and verify message format compatibility before implementation; the current TrajectorySetpoint message version is 0.

| Criterion | MAVLink `SET_POSITION_TARGET_LOCAL_NED` | ROS 2/XRCE-DDS `TrajectorySetpoint` + `OffboardControlMode` |
| --- | --- | --- |
| PX4 reception in this checkout | Explicit handler maps fields and creates mode signal. | Both uORB types are exposed as inbound DDS topics. |
| Frame handling at reception | Local NED direct; legacy BODY_NED rotated; other frames rejected. | No conversion indicated; publisher must supply NED. |
| Mode/liveness relationship | Handler creates mode update while processing accepted setpoint. | Separate topics; a live mode signal may hide a stale payload. |
| Original producer time / sequence | HRT receipt timestamp replaces trajectory time; no sequence/expiry enforcement. | Bridge applies Agent/PX4 clock offset, substitutes zero and caps future timestamps; no independent producer-age, sequence or expiry field. |
| Pi 4B integration burden | Small, mature packet-level interface; no ROS 2 agent/toolchain required for an initial plumbing test. | Requires matched ROS 2, `px4_msgs`, XRCE agent, topic/QoS, and timesync verification. |
| Typed setpoint acknowledgement | None in the traced handler. | No typed trajectory acceptance acknowledgement in the exposed input declaration. |
| Fixed-wing full-vector tracking | Not implemented. | Not implemented; both converge on the same uORB consumer. |

**Transport decision, 2026-09-17:** MAVLink is selected by the user for SITL and hardware. The earlier NED plumbing-message recommendation is superseded by the FRD acceleration-only requirement. `SET_POSITION_TARGET_LOCAL_NED` remains a stock-path test candidate, not an approved research wire contract: it lacks preserved session/sequence/expiry metadata in this receiver path and its frame handling differs from the proposed held-FRD semantics. DDS remains comparison evidence only. Research Offboard activation waits for reviewed receiver/consumer integration, command-age enforcement, publisher ownership and physical loss policy.

## Contract Requirements and Remaining Operational Approvals

| Item | Required approved value / rule |
| --- | --- |
| Producer and ownership | One configured logical producer (host script or Pi translator); PX4 exclusively owns estimation, mode, control, allocation, actuator output, and loss response. Source/session labels provide routing and replay bookkeeping, not authentication. Link authentication/security remains an integration decision. |
| Quantity and reference point | Requested inertial CG acceleration, not IMU specific force and not force. Specify the CG estimate and any IMU lever-arm treatment. |
| Frame and units | Selected: inertial acceleration expressed in FRD, m/s^2; NED estimator world. Frame conversion occurs in a reviewed PX4 adapter, not through relabelling stock NED fields. |
| Gravity | `a_B = R_NB^T dv_NED/dt`, `f_B = a_B - R_NB^T g_NED`. Request `a_B` without a separate gravity offset. Test constant-velocity/free-fall cases and state the gravity model. |
| Fields and precedence | All three acceleration components must be finite, including explicit zero for zero demand. No companion position/velocity/attitude/rate/thrust objective. Stock unused fields are masked/NaN only in separate stock-path tests. The approved offline policy gives feasibility/stabilization priority and prefers minimum rotation/residual-axis rate among feasible candidates; quantitative tradeoffs and executable resolution remain later control-design work. |
| Time and ordering | PX4-time receipt time, producer monotonic timestamp, sequence number, validity interval/deadline, maximum command age, and replay/out-of-order policy. Stock candidate messages do not carry all of these; add enforcement only after an approved design. |
| Update and deadline | Set command rate, heartbeat rate, payload deadline, jitter and latency budget from the controller/vehicle data. `COM_OF_LOSS_T` is a separate offboard-liveness bound, not the payload deadline. |
| Bounds and feasibility | Per-axis request bounds, vector/jerk/slew limits, envelope/state-validity gates, saturation reporting, and policy for unavailable directions; numerical values remain unresolved vehicle requirements. |
| Entry/exit and loss | Preconditions, arming/mode-entry acknowledgement, transition behavior, stale/replayed/non-finite rejection, reconnect policy, and an airframe-feasible fallback. Do not assume stock loiter/RTL/landing works for this airframe. |
| Status and evidence | Report selected mode, control mode, command sequence/age, acceptance/rejection reason, estimator validity, limiting/saturation, allocator residuals, and final actuator commands. Retain both producer and PX4 logs. |

## Focused Tests Required Before Implementation

1. Unit-test NED/ENU and FRD/FLU signs, local and legacy body MAVLink paths, masked fields/`NaN`, infinities that activate ingress flags but fail downstream finite checks, and `FORCE_SET` rejection.
2. Show Commander precedence for position+acceleration, velocity+acceleration, and acceleration-only mode flags; prove the required attitude-estimate gate and `COM_OF_LOSS_T` transition.
3. Test acceleration-only invalidity; velocity-plus-acceleration conversion without active AUTO_PATH; and position-plus-velocity-plus-acceleration AUTO_PATH feed-forward. Cover normal, parallel, zero, partial-`NaN`, Z-only acceleration, zero horizontal velocity and zero normal residual through the normalize/divide path. Do not claim forward/Z tracking.
4. Measure end-to-end timestamps separately: producer creation, wire receipt, `offboard_control_mode` publication, `trajectory_setpoint` publication, controller input, torque/thrust publication, allocation, and actuator output. A fresh heartbeat with stale trajectory must fail the research contract.
5. Inject duplicate, reordered, replayed, delayed, jittered, dropped, and non-finite requests. Verify the specified rejection/fallback and that neither prior actuator output nor a stale setpoint is silently retained beyond its deadline.
6. Add controller/allocation tests only after plant data exist: canard/EDF sign, effectiveness rank/conditioning, unavailable directions, normalized-versus-physical scaling boundary, saturation/anti-windup, servo/EDF rate limits, and actuator output mapping.
7. Integrated research tracking SITL requires an approved contract and controller slice; stock baseline and isolated interface tests can proceed under their own approved procedures. Compare estimates with truth for scoring but do not feed simulator truth into operational control.
8. Verify DDS timestamp conversion, zero replacement, future-time cap, offset convergence/loss, disabled-sync configuration and absence of double correction. Test torque-gated allocation with thrust-only updates followed by coordinated torque/thrust publications.

## Phase 2 Contract Gaps Versus Later Integration Gates

1. Quantity/frame, CG/body-held semantics and MAVLink are confirmed. The offline
   wire contract, profile and residual-attitude preference have been approved
   for inactive testing. PHASE_GATE_STATUS.md records executable and review-only
   evidence separately. Operational control/link budgets and applicable contract
   signoff remain open; fixtures do not supply measured flight requirements.
2. The complete stock source trace proves navigation/trajectory conversion,
   not acceleration-vector control. The intended custom trace reaches the
   tested adapter accessor and explicitly marks missing live receiver/state/
   clock and controller links. Phase 2 may review that design without falsely
   claiming it is executable end to end or requiring completed Phase 4 first.
3. Live attachment requires independently approved identity/authentication,
   supervisor authority, clock/state provenance and deployment procedures.
   The custom offline wire/admission tests do not authorize that attachment.
4. Supplied geometry remains unvalidated. Effectiveness, physical bounds,
   feasible envelope and flight fallback require plant/control evidence in
   later gates, not invented numbers or a geometry prerequisite for codec tests.
5. Later research-mode integration must preserve Cessna and enforce one owner
   per control/output stage with an explicit four-canard/EDF mapping. Stock
   torque-triggered allocation caches thrust, so publications must be coordinated.
   Its normalized demands are not physical newtons or newton-metres.
