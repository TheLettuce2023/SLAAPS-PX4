# Preliminary Research Control Architecture

Status: geometry-independent design and first-slice specification.

This document is not full Prompt H approval.
It does not select a flight law, choose gains, approve a plant or operating envelope, or authorise any actuator-producing PX4 module.
It defines software ownership, request semantics, frame mathematics, failure boundaries, and a bounded pure-C++ implementation slice that can proceed while geometry optimisation and empirical testing remain outstanding.

The source references are pinned to checkout `e880f1cd79e2453ef3dfaf68ee1c067b98ea9a53`.
No simulator run, build, model edit, controller implementation, hardware operation, or physical test supports this document.

## Decision Summary

Adopt a staged hybrid architecture.

1. Implement a PX4-independent request-contract library first.
   It validates identity, session, sequence, time, freshness, finite values, frames, bounds, disarmed permissions, reconnects, and failure latches.
   It performs truth-free quaternion frame mathematics but has no uORB dependency, publication, controller, gain, geometry default, autostart entry, or actuator output.
2. Later place a thin, opt-in PX4 research wrapper around that library.
   The wrapper will be the sole consumer of the research MAVLink request and, only after separate approval, the sole owner of the research objective stage.
3. Reconsider PX4's generic `RateControl` library and control-allocation algorithms after plant evidence exists.
   Do not reuse the stock fixed-wing mode, lateral/longitudinal, attitude, rate, or fixed-wing effectiveness modules as the complete research controller.
4. Delay acceleration-to-attitude/thrust/moment control, effectiveness scheduling, gain selection, and actuator allocation until the plant and feasible-envelope gates pass.

This preserves a portable seam that can be tested now without disguising software-contract correctness as physical feasibility.

## Current Facts, Estimates, And Unresolved Evidence

The following user facts supersede older unknowns or contrary proposals in Wave 1 documents.

| Item | Classification | Architectural consequence |
| --- | --- | --- |
| Four X-arranged canards, each with a separate servo | Confirmed fact | Four independent commands exist electrically; independent or full-rank force/moment authority is not established. |
| One rear fixed-axis EDF, no thrust vectoring, no main wing | Confirmed fact | EDF force direction is fixed in FRD; body and canard aerodynamic force remains possible but must be measured or modelled from evidence. |
| Research input is inertial acceleration expressed in FRD; estimator world is NED | Confirmed fact; user additionally confirmed CG reference and body-held FRD during C0b.2 review (2026-09-17) | The request is neither specific force nor a NED trajectory field. Held FRD components follow the rotating body axes and use the current-attitude transform. Actual CG location and IMU lever arms still require evidence. |
| The Pi sends acceleration only and sends no additional attitude setpoint | Confirmed fact | PX4 must resolve attitude and all remaining degrees of freedom; transport metadata is not another control objective. |
| MAVLink is the transport | Confirmed fact | The host script is the SITL producer. The Pi-side translator uses a TELEM serial link on hardware. Exact wire encoding remains an interface-gate decision. |
| Pixhawk 6X onboard sensors, GPS, and a DroneCAN pitot are planned | Confirmed fact | Operational control may use only their validated measurements and PX4 estimates; simulator truth is excluded. |
| Launch is intended from rest with the nose up, body `+X` vertical, under EDF power | Confirmed intent | This is a required test condition, not proof of attitude authority at zero freestream. No artificial initial launch speed may be assumed. |
| Recovery aims for net head-first slowdown | Confirmed intent | Capture speed, deceleration authority, attitude, and abort conditions are unresolved requirements. The fixed EDF alone does not establish negative body-X force. |
| Mass near `2.5 kg` and thrust near `5.5 kgf` | Unmeasured estimates | They may label non-operational fixtures only. They are not controller constants, plant parameters, limits, gains, or evidence of launch margin. |

Still missing are as-built canard locations, orientations, hinges, areas, signs, linkage and servo dynamics; CG and inertia; installed EDF axis, line of action, thrust map and dynamics; body/canard aerodynamics and local flow; sensor installation and timing; a feasible state/acceleration envelope; control and capture speeds; numerical timing and performance requirements; and an approved loss response.

Consequently, Phase 0 and the plant gate remain incomplete.
The architecture may identify data flow and mathematical obligations, but it may not claim stabilisability, controllability, trim, launch feasibility, recovery feasibility, or tracking performance.

## Ownership And Software Boundaries

The intended chain is:

`research producer -> MAVLink adapter -> request contract -> feasible-objective resolver -> attitude/rate control -> normalisation boundary -> allocation -> outputs`

Each arrow is a typed boundary with one active owner.

| Stage | Sole owner in research mode | Output and restriction |
| --- | --- | --- |
| Research intent | Host guidance script in SITL or Pi-side translator on hardware | One FRD inertial-CG acceleration vector plus contract metadata. No position, velocity, attitude, rate, thrust, torque, or actuator objective. |
| MAVLink decoding | One PX4 transport adapter | A canonical request. Transport acceptance is not control acceptance. |
| Contract validation | Portable request library called by one research wrapper | Accepted/rejected decision and status only; no uORB or actuator output. |
| Feasible objective | Future research control wrapper | A jointly feasible force/attitude/moment objective and explicit residuals. Not part of the first slice. |
| Attitude/rate demand | One future research control path | Quaternion/vector and body-rate objectives. Stock fixed-wing publishers must be inactive for these topics in research mode. |
| Normalised wrench demand | The same future rate/control owner | Coordinated normalised body-axis thrust and torque publications. Never N or N m on PX4's normalised topics. |
| Allocation | PX4 Control Allocator with a later approved effectiveness provider | Four canard servo commands and one EDF command, with residual and saturation status. |
| Physical/simulated output | Selected PX4 output driver or gz bridge | Exactly one mapping for each servo and EDF channel. |
| Supervision and loss response | PX4 mode/failsafe owner | Mode entry, arming, estimator gates, command loss, latch clear, and fallback. The actual fallback is unresolved. |

The host script and Pi translator are alternative deployments of the same logical producer, not simultaneous sources.
A source identifier is an application identity, not merely a MAVLink system/component number or serial port.
QGroundControl is an operator/observation endpoint and must not become a competing research-request publisher.

## Canonical Request Contract

### Controlled Quantity

The only control objective from the producer is

`a_req_B = [a_x, a_y, a_z]`,

where `B` is body FRD, the vector is inertial acceleration of the declared CG, and units are `m/s^2`.
All three components are required and finite.
There is no field-masking or `NaN means unused` convention in the canonical research contract.
Force, specific force, position, velocity, attitude, angular rate, thrust, torque, and actuator fields are forbidden.

Metadata is mandatory but is not a control objective:

- protocol version;
- configured source identifier;
- producer boot/session identifier;
- strictly increasing unsigned 64-bit sequence within that session;
- raw producer monotonic creation time;
- producer time mapped into the validator's monotonic clock domain, with mapping validity and uncertainty;
- original raw producer expiry and its mapped validator-clock expiry, both derived from the original creation rather than receipt or heartbeat;
- frame `FRD`;
- quantity `INERTIAL_ACCELERATION`;
- reference point `CG`.

The stock MAVLink `SET_POSITION_TARGET_LOCAL_NED` handler cannot by itself satisfy this final contract.
It converts accepted body values into local NED using attitude at reception and replaces command time with PX4 receipt time (`src/modules/mavlink/mavlink_receiver.cpp:1219-1275,1301-1312`).
It also carries no application session, sequence, expiry, CG, or gravity semantics.
The approved MAVLink wire representation must therefore either add a versioned research payload or provide an equally testable metadata envelope.
That wire choice is deliberately not made here.

### Body-Held Semantics

The accepted numerical FRD vector is zero-order held in the rotating body frame until superseded or expired.
At control evaluation time `t`, using the current valid attitude quaternion,

`a_req_N(t) = R_N_from_B(q_NB(t)) a_req_B`.

The command is not rotated once at packet arrival and then held in NED.
As the vehicle rotates, the NED direction of a held request rotates with it.
The attitude sample associated with receipt is retained for evidence, but it does not freeze the command in world coordinates.

This policy must be visible in logs and tests because it differs materially from the stock MAVLink body-frame conversion.
If a future experiment needs a world-held acceleration vector, that is a new interface version, not an implicit reinterpretation.

### Gravity And Rotating-Frame Mathematics

Let `q_NB` rotate FRD coordinates into NED and let `g_N = [0, 0, +g]`, with `g` supplied by the caller's approved gravity model.
For a body-held request:

`a_req_N = R_NB a_req_B`

`f_req_N = a_req_N - g_N`

`f_req_B = R_BN f_req_N = a_req_B - R_BN g_N`.

`f_req` is required non-gravitational specific force, not an accelerometer measurement at an offset IMU and not a second command.
The later physical force target would be `F_non_grav = m f_req`, but mass must come from approved plant state and is absent from the first slice.

Pure coordinate rotation of a vector does not add an angular-rate cross term.
However, an implementation that obtains inertial acceleration by differentiating body-resolved velocity must use

`a_B = d(v_B)/dt |_B + omega_B cross v_B`.

Here `v_B` is inertial/ground-relative velocity resolved in FRD, not the user's air-relative velocity state. Differentiating air-relative velocity would additionally require the material wind-velocity derivative to recover inertial acceleration. The helper is coordinate mathematics, not an approved air-data estimator.

Omitting that term would confuse a rotating-coordinate derivative with inertial acceleration.
Offset-IMU acceleration also needs lever-arm, angular-acceleration, and centripetal terms; the first slice intentionally does not implement that geometry-dependent conversion.

All frame operations use normalised quaternions and vector constructions.
Euler angles are prohibited in this contract path, particularly because the intended launch is near vertical.
Quaternion norm acceptance tolerance, gravity magnitude, clock uncertainty, and every numeric bound are required caller configuration with no operational defaults.

For S1, quaternion tolerance is finite and strictly between zero and one; accept only a finite nonzero quaternion with `abs(norm(q)-1) <= tolerance`, then normalize that accepted quaternion using the existing matrix representation. `q` and `-q` must yield the same rotation. Gravity is a finite NED vector `[0,0,+g]` with `g > 0`; any other gravity input yields `GravityInvalid`. Non-finite inputs or unrepresentable outputs return failure without changing the output argument. The velocity derivative and body-relative-to-NED angular rate must refer to the same instant.

### Time, Freshness, And Liveness

Three conditions are independent:

1. MAVLink/offboard link liveness;
2. freshness and ordering of the particular acceleration request;
3. validity of the state/attitude used to execute it.

A fresh heartbeat cannot refresh a stale request.
Repeating a prior payload cannot extend its original validity.
Receipt time cannot replace producer creation time for end-to-end age.
The validator rejects unknown clock mapping, excessive configured mapping uncertainty, future timestamps beyond configured skew, expired validity, excessive age, duplicates, replay, and out-of-order sequence.
It never clips age or silently substitutes receipt time.
For mapping uncertainty `u`, validation is conservative: maximum possible age uses `mapped_creation - u`, earliest possible expiry uses `mapped_expiry - u`, and the future-time check uses `mapped_creation + u`.
The trusted adapter must supply an uncertainty bound covering both endpoints over the whole request lifetime, including clock drift, and invalidate the context if that guarantee is lost. The library cannot establish clock synchronization or authenticity from labels/timestamps alone.
Every addition, subtraction, duration, and comparison must be checked before unsigned arithmetic; underflow, overflow, or an indeterminate interval is rejection, not saturation.

Exact S1 timing oracle: raw and mapped expiry must each exceed creation, and their checked durations must be equal and within the inclusive configured validity bounds. Reject if `earliest_creation > now` even when a nominal future-skew allowance would otherwise permit the interval; never clip a negative age to zero. Check `latest_creation <= now + maximum_future_skew` with overflow detection. Maximum possible age and mapping uncertainty have inclusive upper bounds; expiration is half-open: `now >= earliest_expiry` is expired. Strict sequence and raw producer creation time must increase; equal raw time is rejected without refresh, while a lower raw time on an otherwise valid newer owner packet latches. Sequence exhaustion needs trusted establishment of a different session, never integer wraparound.

All time limits are mandatory configuration supplied by an approved requirement set.
The library has no built-in command rate, deadline, validity, age, skew, uncertainty, reconnect delay, or heartbeat value.

### Source, Session, Reconnect, And Disarmed Permissions

Only a trusted supervisor/adapter call may establish the configured logical owner and session.
A new session is established explicitly and only while PX4 reports `DISARMED`; it is never created or reset by an untrusted packet, a lower sequence number, a producer label, or a transport reconnect.
A source, MAVLink system/component, and session label provide routing and correlation, not authentication; no source authentication mechanism is established by this document.
A disconnect invalidates the usable request immediately from the caller's perspective, even if the last validity interval has not elapsed.
An armed reconnect or source/session replacement is rejected and reported without adopting it.
The strictly increasing sequence and producer-time replay floors are preserved for the trusted session through ordinary revoke, disconnect, mode/state loss, and reconnect.
Only supervisor-authorised establishment of a different nonzero session while disarmed starts new replay floors; attempting to re-establish the same session cannot reset them.

The required disarmed policy is one of:

- `REJECT`: reject all requests while disarmed; or
- `VALIDATE_ONLY`: validate and report them but mark them ineligible for control and never carry them across arming.

There is no `ACCEPT_FOR_CONTROL_WHILE_DISARMED` option.
Arming clears every validate-only sample, so a fresh post-arm request is required.
Mode selection, arming, and transport liveness remain external PX4 gates; request validation cannot arm the vehicle or enter research mode.

Any observed disarmed context revokes an armed cached request under either policy, before packet validation; a malformed disarmed packet cannot preserve it. For S1, fresh post-entry/post-recovery means a newly accepted request satisfying age and preserved replay floors after the observed transition, not proof that the producer generated it after a transition timestamp. The serialized caller must report every arming/mode/state/transport transition through polling or explicit revocation. The library cannot infer unobserved transitions. A stricter producer-generation epoch belongs to later adapter/supervisor review.

### Bounds And Failure Latching

The contract validator enforces syntactic/administrative bounds, not aerodynamic feasibility.
Required caller configuration includes finite per-axis minimum and maximum acceleration, vector-norm maximum, validity bounds, timing bounds, and quaternion-norm tolerance.
The protocol/source identifiers, every timing requirement, maximum norm, and quaternion tolerance must be explicitly valid and nonzero; axis minima may be zero or negative where the approved contract permits it.
An out-of-bounds request is rejected, never silently clipped.
These values may be permissive software-test fixtures before physical evidence exists, but fixture values must be visibly non-operational and cannot populate an airframe or controller parameter.

Failure action is fixed by this contract rather than selected through configurable masks:

- wrong-source, wrong-session, malformed, non-finite, out-of-bounds, stale-on-arrival, duplicate, and out-of-order packets are rejected and reported but never replace, revoke, refresh, or extend a still-valid trusted-owner request;
- an accepted request reaching its original expiry, transport disconnect, invalid clock mapping, required-state invalidity, unknown arm/mode state, mode deselection, arming transition from validate-only state, or explicit revoke removes eligibility and the cached request cannot silently become eligible again;
- producer-clock regression within a trusted session revokes eligibility and latches that session because it can indicate an unannounced producer restart;
- validator-clock regression observed by `evaluate()`, `poll()`, or `getEligibleRequest()` revokes eligibility and latches the guard; local time cannot be reset except through an explicit supervisor-authorised disarmed latch clear and trusted-session establishment;
- an unauthorised or armed trusted-session change is rejected and reported without adopting the proposed session; and
- a latch may be cleared only by an explicit supervisor-authorised call while disarmed, followed by explicit trusted-session establishment.

Transport reconnection, a valid packet, timeout, sequence advance, or later clock/estimator/mode recovery cannot clear a latch or resurrect a revoked/expired request.
The decision record retains the first latched reason and counts later rejections without replacing the cause.

Check precedence is normative: configuration and current validator-clock/context transitions are processed independently of packet content; clock regression latches and invalid context revokes even if the arriving packet is foreign or malformed. Then enforce existing latch/session state and validate packet protocol, owner/session, frame/quantity/reference, finite bounds, sequence and time interval. Only an otherwise valid newer trusted-session packet can trigger a producer-time-regression latch. Ordinary packet rejection leaves the still-valid owner cache unchanged, but does not suppress independent context/expiry transitions. Latch/revoke are intentional failure-path mutations, not successful packet acceptance.

An explicit authorized disarmed latch reset may establish a new validator-clock baseline, but it also revokes the cache and requires trusted establishment of a different nonzero session. No packet, clock catch-up or reconnect clears a latch. The trusted supervisor must avoid reusing earlier session identifiers; S1 retains current-session replay floors, not an unbounded history or cryptographic replay database.

## Geometry-Dependent Control Architecture Reserved For Later Review

The following structure is a design obligation, not a selected control law.

### Acceleration To Feasible Wrench

For approved mass `m`, attitude, velocity, air state, and actuator state, the requested non-gravitational force begins with

`F_req_B = m (a_req_B - R_BN g_N)`.

The actual feasible wrench depends jointly on EDF command and all four canard deflections:

`[F_B, M_B] = W(x_est, u_edf, delta_1, delta_2, delta_3, delta_4)`.

Canards cannot be represented as pure moments without evidence because each surface creates force and a moment through its lever arm.
EDF force can also create a moment if its measured line of action misses the CG.
The later resolver must project force and stabilising-moment objectives into one coupled attainable set, subject to servo/EDF magnitude, rate, dynamic, local-flow, power, and state constraints.
It must report rejected or residual force and moment rather than claim independent six-axis authority.

The approved offline profile gives feasibility and stabilization priority over
acceleration tracking. Quantitative tradeoffs within the feasible stabilizing
set remain an unresolved flight-law decision; no weight or plant authority is
inferred from the profile.
No pseudoinverse, optimiser, dynamic inversion, PID cascade, or other method is selected by this document.

### Residual Attitude And Roll Policy

Acceleration-only input leaves rotational degrees of freedom unresolved.
The Pi may not fill them with an attitude or yaw setpoint.
PHASE_2_SITL_PROFILE_PROPOSAL.md now records the user-approved offline preference
for minimum rotation from current estimated attitude and minimum residual-axis
rate demand among feasible stabilizing options. This is a policy requirement,
not a resolver implementation. Conflicting preferences, exact ties, continuity
and quantitative limits still need plant-supported design; see the declarative
review cases in PHASE_2_POLICY_VERIFICATION.md.
PX4 must select a continuous residual-attitude policy that:

- maintains state and aerodynamic-envelope validity;
- gives stabilisation moments explicit priority where needed for safety;
- avoids Euler yaw/roll definitions near vertical;
- avoids discontinuities when the desired force is zero, parallel, or antiparallel to body `+X`;
- limits attitude and rate commands using approved requirements;
- preserves or transports a prior transverse reference vector to resolve roll continuously; and
- reports when force alignment and attitude stabilisation cannot both be achieved.

A candidate for later evaluation is shortest-arc force-axis alignment with quaternion/vector parallel transport of the prior transverse axis.
It is not approved because the plant may require angle-of-attack, sideslip, roll, or canard-local-flow objectives that dominate this geometric choice.

The intended nose-up launch makes a conventional world-yaw target poorly conditioned and potentially meaningless.
At zero or low freestream, four servos do not imply useful moments: launch attitude control requires measured inherent stability, EDF wash over the canards, another aerodynamic mechanism, or sufficient acquired speed.
Until that evidence exists, an EDF-only from-rest launch is a scenario to evaluate, not a controllable mode claim.

Head-first recovery slowdown likewise needs evidence for negative acceleration along the motion direction.
A non-reversing fixed EDF cannot be credited with braking thrust; body/canard drag and attitude changes may help only over a measured feasible speed range.

### Inner Loop And Scheduling

The eventual inner loop must use body angular-rate and attitude estimates available on Pixhawk 6X.
Controller structure, bandwidth, gains, and scheduling are deferred.
Any scheduled model must be supported across the approved envelope and must expose validity and interpolation/extrapolation status.

The DroneCAN pitot supplies differential pressure or a calibrated scalar airspeed along its probe model.
It does not measure the full three-dimensional air-relative velocity, angle of attack, sideslip, or local flow at each canard.
GPS supplies ground-relative motion, not air-relative motion.
Wind estimation can relate them only within its estimator assumptions and validity.
Therefore scalar pitot speed alone cannot populate a full air-vector scheduler or prove canard authority.
Low-speed fallback to a guessed trim airspeed is prohibited for the research path.

Operational inputs may include validated Pixhawk IMU/magnetometer/barometer data, GPS, DroneCAN pitot, and PX4 estimates derived from hardware-representative sensor paths.
Gazebo pose, velocity, link state, aerodynamic wrench, or other truth is excluded from operational control and may be consumed only by plant validation, diagnostics, and scoring.

### Normalisation And Allocation Boundary

PX4 defines `vehicle_torque_setpoint.xyz` as normalised body-axis torque and `vehicle_thrust_setpoint.xyz` as body-axis values in `[-1, 1]` (`msg/VehicleTorqueSetpoint.msg:2-5`; `msg/VehicleThrustSetpoint.msg:3-8`).
They are not N m and N.

A later approved normalisation model must map physical objectives and current effectiveness into dimensionless values with documented sign, scale, range, conditioning, validity, and inverse/residual interpretation.
It must not use the estimated `2.5 kg` or `5.5 kgf` as hidden scaling constants.
The future publisher must publish thrust and torque as a coordinated pair each cycle, with thrust written before the torque publication that triggers allocation.
Control Allocator currently triggers on torque, caches thrust, and then constructs the six-axis demand (`src/modules/control_allocator/ControlAllocator.cpp:382-425`; `ControlAllocator.hpp:197-209`).

The stock fixed-wing surface provider adds each surface with a configured torque vector and a zero force vector (`src/modules/control_allocator/VehicleActuatorEffectiveness/ActuatorEffectivenessControlSurfaces.cpp:205-215`).
That constant pure-torque abstraction is insufficient unless plant evidence proves force coupling can safely be handled elsewhere.
A later dedicated effectiveness provider may be required, but its matrix, scheduling, signs, and update conditions cannot be written before plant identification.
Existing allocator residual and saturation reporting is reusable in principle (`src/modules/control_allocator/ControlAllocator.cpp:657-707`).

## PX4 Reuse Assessment

| Candidate | Source-backed assumptions | Preliminary disposition |
| --- | --- | --- |
| Fixed-wing mode manager and lateral/longitudinal control | Acceleration-only requests remain invalid in the stock fixed-wing conversion; acceleration is used only in a position/velocity path as curvature feed-forward. Lateral acceleration is mapped to roll and the controller publishes body-X thrust (`src/modules/fw_mode_manager/FixedWingModeManager.cpp:2189-2255`; `src/modules/fw_lateral_longitudinal_control/FwLateralLongitudinalControl.cpp:290-333,866-867`). | Reject for the research acceleration consumer. Keep only as stock Cessna baseline. |
| `fw_att_control` | Its quaternion error explicitly removes yaw because it assumes fixed-wing aircraft have no direct yaw authority (`FixedwingAttitudeControl.hpp:76-89`). Coordinated-turn feed-forward contains a high-tilt-sensitive denominator and is faded between 70 and 75 degrees (`FixedwingAttitudeControl.cpp:275-287`). | Do not reuse unchanged for near-vertical launch or residual-attitude resolution. Quaternion utilities and tested patterns may be referenced later. |
| `fw_rate_control` module | It falls back to trim airspeed, schedules trim and gains with airspeed, adds roll-to-yaw feed-forward, applies an empirical two-percent motor stop rule, and publishes normalised torque/thrust (`FixedwingRateControl.cpp:155-203,347-439,456-479`). | Do not reuse unchanged. Its fixed-wing assumptions, parameters, and publication ownership conflict with a dedicated research path. |
| `RateControl` library | It is a three-axis rate PID/feed-forward calculation returning normalised torque and supports allocator saturation flags and integrator limiting (`src/lib/rate_control/rate_control.hpp:65-97`; `rate_control.cpp:71-116`). | Candidate for later reuse after plant-supported gains, scaling, scheduling, reset rules, and coupled-saturation review. Not part of the first slice. |
| Fixed-wing effectiveness provider | It supports fixed-forward motors and configurable control surfaces, but each surface currently contributes torque only and no force (`ActuatorEffectivenessFixedWing.cpp:38-62`; `ActuatorEffectivenessControlSurfaces.cpp:205-215`). | Existing configuration is not evidence of canard/EDF suitability. Reuse allocator infrastructure, not this effectiveness model without proof. |
| Custom effectiveness provider | The existing custom source adds motors and direct torque actuators (`ActuatorEffectivenessCustom.cpp:38-59`), not four aerodynamic canards with coupled force/moment. | Reject as a ready-made topology; a research provider remains a later option. |
| Control Allocator algorithms/status | Existing flow supports effectiveness matrices, slew limiting, clipping, residuals, and saturation (`ControlAllocator.cpp:430-456,657-707`). | Strong reuse candidate after an approved scheduled effectiveness contract and tests. |
| Dedicated research wrapper | Can own canonical validation and later objective generation while preventing stock publishers from competing. | Recommended architecture seam. Only its independent request library is released as the first code slice. |

The recommended long-term shape is therefore a dedicated research wrapper around selectively reusable libraries, not a fork of the entire stack and not a parameter-only use of the stock fixed-wing pipeline.

## Mode, Invalid State, Saturation, And Loss Behaviour

The exact flight fallback remains unresolved because no feasible unpowered, hover, loiter, return, land, or capture mode has been demonstrated.
The following behaviours are architectural requirements:

- No stale request, invalid required state, unapproved trusted-session change, or latched contract failure may retain an eligible objective silently.
- Invalid foreign or malformed traffic cannot mutate, revoke, refresh, or extend the valid owner's accepted command; the owner's original expiry continues to run.
- Contract loss and offboard heartbeat loss are separate events and must be logged separately.
- The future wrapper must invalidate its objective before another publisher is permitted to take ownership.
- Mode entry requires a fresh post-entry request, valid required estimates, configured contract, no latch, and positive one-publisher verification.
- Mode exit, disarm, estimator invalidity, and actuator-authority loss reset future controller integrators and revoke setpoints according to one reviewed transition sequence.
- Limiting and feasibility projection occur before integration; allocator saturation/residual feedback must prevent windup in the same coupled directions actually unavailable.
- Servo deflection and rate limits, EDF bounds and dynamics, and state-envelope limits must come from evidence and be enforced at their owning layers.
- A non-finite state or arithmetic result fails closed before publication.
- Reconnect does not resume a pre-loss objective or clear a latch.
- Disarmed validation never produces torque, thrust, servo, or motor output.

The first pure library can verify request-side state transitions only.
It cannot implement or test the physical fallback.

## First Code Slice: Portable Request Contract Library

### Proposed Files

The master may approve the following exclusive implementation paths:

```text
src/lib/research_acceleration/CMakeLists.txt
src/lib/research_acceleration/RequestGuard.hpp
src/lib/research_acceleration/RequestGuard.cpp
src/lib/research_acceleration/FrameMath.hpp
src/lib/research_acceleration/FrameMath.cpp
src/lib/research_acceleration/RequestGuardTest.cpp
src/lib/research_acceleration/FrameMathTest.cpp
```

Production headers and sources shall include only fixed-width integer types, basic standard-library facilities supported by PX4, the existing header-only matrix/quaternion types, and their own headers.
They shall not include uORB, HRT, parameters, module/work-queue APIs, MAVLink, estimator topics, Gazebo, or actuator types.
Use the existing header-only PX4 matrix/quaternion library for rotation operations. Compile evidence identified its transitive `px4_platform_common/defines.h` dependency; the standalone host build explicitly uses the existing minimal header under `platforms/ros2/include/`, containing constants/finite predicates only. This is a platform-header dependency, not a ROS middleware or PX4 runtime dependency. No time, parameter, uORB or module runtime is linked. The standalone CMake entry rejects inclusion as a firmware subdirectory; future PX4 integration must use its real platform definitions and a separately reviewed build entry.
Tests may use the repository's unit-test framework.
`CMakeLists.txt` may build the library and standalone tests only; it shall not add a module, board configuration, airframe, startup command, or autostart entry.

### Proposed API

Names may receive mechanical style adjustment during implementation, but semantics may not change without master review.

```cpp
namespace research_acceleration
{

using TimeUs = uint64_t;
using SourceId = uint64_t;
using SessionId = uint64_t;

using Vec3f = matrix::Vector3f;
using QuaternionNedFromFrd = matrix::Quatf;

enum class ArmState : uint8_t { Disarmed, Armed, Unknown };
enum class ResearchModeState : uint8_t { NotSelected, Selected, Unknown };
enum class DisarmedPolicy : uint8_t { Reject, ValidateOnly };
enum class Frame : uint8_t { Frd };
enum class Quantity : uint8_t { InertialAcceleration };
enum class ReferencePoint : uint8_t { Cg };

enum class DecisionCode : uint16_t {
    Accepted,
    ValidatedDisarmed,
    NotConfigured,
    WrongProtocolVersion,
    WrongSource,
    NoSession,
    WrongSession,
    SessionChangeWhileArmed,
    DuplicateSequence,
    OutOfOrderSequence,
    ClockMappingInvalid,
    ClockUncertaintyExceeded,
    ProducerTimeInFuture,
    ProducerTimeRegressed,
    ValidatorTimeRegressed,
    RequestTooOld,
    ValidityOutOfRange,
    RequestExpired,
    RequiredStateInvalid,
    WrongFrame,
    WrongQuantity,
    WrongReferencePoint,
    NonFinite,
    AxisOutOfBounds,
    NormOutOfBounds,
    DisarmedRejected,
    ModeNotSelected,
    ArmStateUnknown,
    ModeStateUnknown,
    Disconnected,
    Latched
};

struct AccelerationBounds {
    Vec3f minimum_m_s2;
    Vec3f maximum_m_s2;
    float maximum_norm_m_s2;
};

struct TimingRequirements {
    TimeUs minimum_validity_us;
    TimeUs maximum_validity_us;
    TimeUs maximum_command_age_us;
    TimeUs maximum_future_skew_us;
    TimeUs maximum_clock_uncertainty_us;
};

struct ContractConfig {
    uint32_t protocol_version;
    SourceId permitted_source;
    DisarmedPolicy disarmed_policy;
    AccelerationBounds acceleration_bounds;
    TimingRequirements timing;
    float quaternion_norm_tolerance;
};

struct TrustedSession {
    SourceId owner;
    SessionId session;
};

struct CanonicalRequest {
    uint32_t protocol_version;
    SourceId source;
    SessionId session;
    uint64_t sequence;
    TimeUs producer_monotonic_us;
    TimeUs producer_time_in_validator_domain_us;
    TimeUs producer_expiry_monotonic_us;
    TimeUs expiry_time_in_validator_domain_us;
    TimeUs clock_mapping_uncertainty_us;
    Frame frame;
    Quantity quantity;
    ReferencePoint reference_point;
    Vec3f acceleration_frd_m_s2;
};

struct EvaluationContext {
    TimeUs validator_receive_time_us;
    ArmState arm_state;
    ResearchModeState mode_state;
    bool clock_mapping_valid;
    bool transport_connected;
    bool required_state_valid;
};

struct Decision {
    DecisionCode code;
    bool syntactically_valid;
    bool eligible_for_control;
    bool latched;
    bool conservative_age_valid;
    TimeUs conservative_age_us;
    uint64_t rejection_count;
};

class RequestGuard {
public:
    explicit RequestGuard(const ContractConfig &config);
    bool configured() const;
    RequestGuard(const RequestGuard &) = delete;
    RequestGuard &operator=(const RequestGuard &) = delete;
    Decision establishTrustedSession(const TrustedSession &session,
                                     bool supervisor_authorised,
                                     const EvaluationContext &context);
    Decision evaluate(const CanonicalRequest &request,
                      const EvaluationContext &context);
    Decision poll(const EvaluationContext &context);
    Decision clearLatch(bool supervisor_authorised, ArmState arm_state);
    void revoke();
    bool getEligibleRequest(const EvaluationContext &context,
                            CanonicalRequest &request,
                            Decision &decision);
};

enum class MathStatus : uint8_t {
    Ok, NonFinite, QuaternionNormInvalid, GravityInvalid
};

MathStatus rotateFrdToNed(const QuaternionNedFromFrd &q_nb,
                          float norm_tolerance,
                          const Vec3f &v_frd,
                          Vec3f &v_ned);
MathStatus rotateNedToFrd(const QuaternionNedFromFrd &q_nb,
                          float norm_tolerance,
                          const Vec3f &v_ned,
                          Vec3f &v_frd);
MathStatus heldInertialAccelerationFrdToNed(
    const QuaternionNedFromFrd &current_q_nb,
    float norm_tolerance,
    const Vec3f &a_req_frd_m_s2,
    Vec3f &a_req_ned_m_s2);
MathStatus specificForceFrdFromInertialAccelerationFrd(
    const QuaternionNedFromFrd &current_q_nb,
    float norm_tolerance,
    const Vec3f &gravity_ned_m_s2,
    const Vec3f &a_req_frd_m_s2,
    Vec3f &specific_force_frd_m_s2);
MathStatus inertialAccelerationFrdFromBodyVelocityDerivative(
    const Vec3f &body_velocity_derivative_m_s2,
    const Vec3f &angular_rate_frd_rad_s,
    const Vec3f &velocity_frd_m_s,
    Vec3f &inertial_acceleration_frd_m_s2);

} // namespace research_acceleration
```

Construction validates all configuration once; invalid configuration leaves the object permanently inert and `configured()` false rather than supplying defaults.
There is no runtime reconfiguration method that can bypass an active request, trusted-session replay floor, or latch.
`establishTrustedSession()` succeeds only through a supervisor-authorised call for the configured owner while disarmed; `evaluate()` can never create or reset a session.
`evaluate()` accepts/replaces a request only after every packet check passes. Independent context/clock failures still revoke/latch as specified above.
Foreign-source, foreign-session, malformed, and replay rejection cannot refresh or replace the trusted owner's cache or replay floors; independently observed context/expiry/latch transitions take precedence.
`poll()` expires/revokes eligibility based on the original mapped creation and expiry without extending either, and revoked/expired state requires a newly accepted request before eligibility can return.
`getEligibleRequest()` performs the same full context/freshness poll before copying; there is no timeless `hasEligibleRequest()` accessor that can expose a stale cached value.
`revoke()` removes eligibility while preserving the trusted session's monotonic sequence/time replay floors; it never pretends that a transport reconnect is a new authorised session.

The implementation must use overflow-safe unsigned-time comparisons and define exact equality at each configured boundary.
It must never throw, allocate dynamically in steady state, normalise an invalid quaternion silently, clip an acceleration, infer a frame, or retain an eligible request after expiry, disconnect, required-state/mode failure, or producer-clock regression.

Decision eligibility describes the operation result, not a global claim about a preserved earlier owner command. After a packet rejection, retrieve any still-valid owner demand only through `getEligibleRequest(context, ...)`, which polls again. A caller must ignore an unchanged output argument on failure. Age has an explicit validity indicator; the rejection counter saturates at the uint64 maximum, persists for the object lifetime and is diagnostic rather than an operational limit. Concrete public names and complete status enumerators are authoritative in the reviewed header.

The later wrapper must pass the approved immutable quaternion tolerance consistently to the independent frame helpers; the request guard validates configuration but does not itself bind every helper call to that tolerance. S1 enforces per-call mathematical validity, not that future caller policy.

### Standalone Acceptance Tests

The first slice is complete only when deterministic host tests cover:

1. configuration rejection for every missing, non-finite, inverted, zero/overflow-invalid, or internally inconsistent required field;
2. exact acceptance and one-unit-beyond rejection at every axis, norm, validity, age, future-skew, clock-uncertainty, and quaternion-norm boundary;
3. `NaN`, positive/negative infinity, signed zero where relevant, largest integers, and arithmetic overflow/underflow attempts;
4. wrong source, wrong protocol, wrong enum, missing session, wrong session, duplicate sequence, out-of-order sequence, producer-time regression, and replay after reconnect, including proof that foreign or malformed traffic does not mutate the valid owner's command, expiry, sequence, session, or latch;
5. disconnect, stale payload with otherwise live transport, expiration through `poll()`, and proof that repeated polling does not refresh age;
6. disarmed `REJECT` and `VALIDATE_ONLY`, clearing validate-only state on arm, unknown arm/mode states, required-state invalidity, mode deselection, and mandatory fresh request after any eligibility recovery;
7. authorised trusted-session establishment while disarmed, rejection without authorisation, proof that packets cannot establish/reset sessions, rejected session change while armed, restart with low sequence, and explicit trusted-session re-establishment after disarm;
8. the fixed producer- and validator-clock-regression latches, monotonic validator time across `evaluate()`, `poll()`, and `getEligibleRequest()`, first-cause retention, continued rejection while latched, failed clear while armed/unauthorised, successful authorised clear while disarmed, and proof that reconnect/state recovery cannot resurrect a revoked request;
9. identity and inverse FRD/NED rotations, 90-degree basis rotations, round-trip properties, non-unit/zero/non-finite quaternions, and no Euler conversion;
10. the nose-up case where body `+X` maps vertically upward, opposite NED `+Z`, without assuming an acceleration magnitude;
11. body-held semantics: the same accepted FRD vector evaluated under two attitudes produces two NED vectors while its FRD components remain unchanged;
12. gravity cases: zero inertial acceleration, free fall, constant-velocity interpretation, and specific-force conversion with caller-supplied gravity;
13. rotating derivative identity `a_B = dv_B/dt + omega cross v_B`, including zero-rate and nonzero-rate basis cases;
14. Gate-B review evidence (not a unit-test claim): dependency/build inspection that the library and its matrix-header use have no uORB, PX4 time, MAVLink, Gazebo, parameter, module, publisher, or actuator dependency; and
15. Gate-B evidence record (not a unit-test claim): repeatable native results with source/configuration identity, while explicitly reporting that no controller or physical behaviour was tested. The full Wave 2 schema/checker remains a separate implementation gate.

No test may use the mass/thrust estimates as an operational pass threshold.
Fixture bounds and times must be labelled non-operational in test names or fixture declarations.

## Later PX4 Integration Slice And Release Gates

Passing standalone tests releases only review of an inactive PX4 adapter.
It does not release a controller.

### Current default-off manual branch (H6)

H6 implements an inactive manual-input option without changing the canonical
acceleration request or its ownership. RC or GCS joystick input is decoded and
selected entirely by stock PX4; the research adapter subscribes only to the
resulting `manual_control_setpoint`. GCS `MANUAL_CONTROL` is therefore an
operator-manual branch, not a second research-acceleration publisher. PX4 keeps
source selection, and the research runtime remains the single future owner of
its H4 state.

The boundary requires explicit caller policy, exact permitted GCS/MAVLink
identity, freshness and complete affine mappings into the normalized demand and
EDF-plus-four-canard desired-command contracts. It supplies no physical mixer,
gain, priority, timing budget or fallback. It is compiled only in the excluded
library, has no startup or output path and is not instantiated. This advances a
testable input/lifecycle seam only; live GCS mode integration remains a later
explicit gate.

### Gate A: Architecture And API Approval

Required before first-slice implementation:

- master accepts this canonical quantity, body-held rule, gravity equations, state machine, and API;
- interface/test documents are reconciled to the body-held current-attitude rule;
- exact file ownership and test command are assigned;
- all operational numeric configuration remains absent;
- independent review confirms no source publisher or autostart is included.

Outcome: portable library and standalone tests may be implemented.

### Gate B: Standalone Contract Evidence

Required before any PX4 wrapper:

- every standalone test above passes with a versioned evidence record;
- malformed configuration and evidence cannot pass;
- static dependency review confirms portability and no output capability;
- an independent reviewer checks time arithmetic, quaternion convention, latching, and state transitions.

Outcome: an inactive adapter may be designed; no uORB publication is yet allowed.

### Gate C: Inactive PX4 Adapter

Proposed later paths, not authorised by this document, are a dedicated wrapper under `src/modules/research_accel_control/` plus any explicitly approved versioned message/transport adapter.
The first PX4 integration must remain opt-in, not autostart, and publish status only.
It must prove:

- the MAVLink wire format preserves source, session, sequence, producer time, validity, FRD, inertial quantity, and CG semantics;
- SITL host-script and Pi/TELEM translator use the same canonical conformance vectors;
- liveness is independent of payload freshness;
- mode/arming/reconnect/disarmed behaviour matches the state machine;
- current estimator attitude is associated and logged without simulator truth;
- acceleration-only input contains no attitude or velocity objective; and
- there is still no torque, thrust, servo, or motor publication.

Outcome: interface Phase 2 may be reconsidered; control implementation remains blocked.

### Gate D: Plant-Supported Control Design

Required before adding force/attitude/moment control or allocation:

- Phase 0 mass/CG/inertia, geometry, signs, actuator data, aero/local-flow data, EDF data, sensor configuration, and uncertainties are approved;
- open-loop plant signs, equilibrium, timestep, EDF, servo, and sensor tests pass;
- a joint feasible force/moment envelope is established for launch, flight, and recovery regimes;
- residual-attitude/roll policy and loss fallback are selected from evidence;
- pitot/air-state scheduling observability and validity are demonstrated;
- a physical-to-normalised contract and scheduled effectiveness design are reviewed; and
- gains, limits, thresholds, and anti-windup behaviour have traceable sources.

Outcome: the master may issue a new, bounded control-law implementation assignment.

### Gate E: Output-Producing PX4 Integration

Before any actuator-producing research mode:

- startup configuration proves the research wrapper is the sole publisher at each selected stage;
- stock fixed-wing mode, attitude, and rate publishers cannot compete in research mode;
- coordinated thrust-before-torque publication and allocator triggering are tested;
- canard/EDF signs, rank, conditioning, coupled force/moment residuals, limits, slew, saturation, and faults are tested;
- estimator invalidity, mode transition, stale request, link loss, restart, reconnect, and latch paths are reviewed;
- no truth input reaches operational control; and
- compile-only Pixhawk 6X portability and SITL estimator-in-the-loop tests pass before any separately authorised bench activity.

## Rejected Alternatives And Reconsideration Evidence

| Alternative | Reason rejected now | Evidence that could reopen it |
| --- | --- | --- |
| Treat stock fixed-wing acceleration reception as full-vector tracking | Source trace shows no acceleration-only consumer and no forward/Z tracking. | A reviewed dedicated consumer and end-to-end tests, not message reception alone. |
| Reuse `fw_att_control` unchanged | It removes yaw error under a fixed-wing authority assumption and its turn coordination is not designed as a vertical residual-attitude policy. | Demonstrated authority/envelope compatibility and near-vertical singularity/continuity tests. |
| Reuse `fw_rate_control` unchanged | It embeds trim-airspeed fallback, fixed-wing gain/trim schedules, adverse-yaw feed-forward, and an empirical motor-stop rule. | Plant-supported equivalence of every assumption plus ownership and transition tests. |
| Configure four stock control surfaces and declare allocation complete | Stock surface columns contain torque and zero force; canard coupling and variable local flow are unknown. | Identified coupling shown negligible or a reviewed effectiveness model that represents it. |
| Convert FRD request once on receipt and hold NED | Violates the selected body-held semantics. | A new versioned world-held interface requirement and migration tests. |
| Let the Pi provide attitude/yaw to resolve roll | Violates acceleration-only input and PX4 stabilisation ownership. | Explicit user/interface revision followed by a new architecture review. |
| Use pitot speed as the full air vector | A scalar probe cannot establish angle of attack, sideslip, wind, or canard-local flow. | Validated multi-axis air-data/estimation and local-flow model with stated envelope. |
| Tune with estimated `2.5 kg` and `5.5 kgf` | They are unmeasured planning estimates and omit inertia, installed thrust dynamics, aero, and authority. | Configuration-controlled measurements with uncertainty and independent validation. |
| Use Gazebo truth in the controller | No equivalent operational hardware source exists; it would invalidate portability evidence. | None for operational control. Truth remains valid only for scoring and diagnostics. |
| Assume stock RTL, loiter, hover, land, or disarm is a safe fallback | The no-wing, fixed-EDF vehicle has no approved fallback envelope. | Human-approved fallback supported by plant and transition/loss testing. |

## Phase-Gate Recommendation

Prompt H remains **incomplete** because its required plant and Phase 0 evidence are unavailable.
No flight law, gain set, effectiveness matrix, operating envelope, launch controller, or recovery controller is approved.

The recommended immediate implementation scope is only the portable request-contract and frame-math library under the seven proposed files, after master approval of Gate A.
That slice is geometry-independent, contains no outputs, and produces useful evidence for source/sequence/time/frame/reconnect/disarmed/latching semantics.

The next control-related approval after that slice is an inactive PX4 adapter review under Gate C.
Actual acceleration-to-wrench, attitude/rate control, normalisation, allocation, uORB output, autostart, SITL tracking, Pixhawk build integration, and physical operation remain blocked by their later gates.
