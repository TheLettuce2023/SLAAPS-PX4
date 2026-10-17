# Research Vehicle Requirements And Physical Feasibility

Status: Phase 0 requirements and missing-data register; user decisions updated 2026-09-19. Geometry-independent architecture/software work is authorized while physical design is optimized; physical gates remain incomplete.

Latest mass/geometry update (2026-09-19): user directed assuming the new SDF's
approximately 3.04 kg value. Working interpretation communicated to the user is
3.041337 kg all-up, including canards and EDF rotor, not a measured component
breakdown. This supersedes the older approximately 2.5 kg estimate below for
provisional mass accounting. New SDF/three-mesh intake is documented in
GAZEBO_GEOMETRY_INTAKE_REVIEW.md. It supplies draft X-canard geometry but has
invalid base inertia, unresolved local mesh packaging and unvalidated dynamics.
No physical feasibility gate is passed by these additions.

This document records what is known, what must be measured or decided, and what evidence is required before a credible plant or controller can be implemented.
It does not establish that the vehicle is physically feasible, approve a flight envelope, or report a simulation or hardware test.
No absent coefficient, dimension, limit, acceptance threshold, or test result is assigned a provisional value here.

## Evidence Basis And Terminology

The following local sources were reviewed on 2026-09-16.

| Source ID | Source | Use And Limitation |
| --- | --- | --- |
| U1 | User-confirmed project direction supplied with this assignment | Authoritative for research intent, physical configuration, Raspberry Pi 4B role, and PX4 ownership boundaries; it supplies no physical measurements or performance limits. |
| U2 | User clarifications through 2026-09-17 | Four X-arranged canards with separate servos; acceleration-only FRD inertial requests in modified Offboard; MAVLink/TELEM; pitot/GPS/6X sensor scope; launch/recovery intent; approximate mass/thrust expectations. No empirical validation supplied. |
| R1 | `docs/CUSTOM_VEHICLE_SITL_ROADMAP.txt` | Planning requirements, phase dependencies, verified source seams, and cautions; it explicitly says the planning session did not repeat runtime tests. |
| A1 | `docs/SITL_ARCHITECTURE.md` | Project architecture and documented stock `gz_rc_cessna` baseline; it is not evidence for the custom vehicle's physics. |
| S1 | `docs/SITL_WSL_SETUP.md` | Recorded development environment, stock SITL procedure, and runtime checks; those checks were not run for this requirements task. |

In this document:

- **Confirmed** means explicitly supplied by U1/U2 or stated as project scope in R1; confirmed design intent is not measured performance.
- **Documented baseline** means recorded by A1 or S1 but not independently rerun in this task.
- **Requirement** means a condition that the project must satisfy even when its numerical criterion remains undecided.
- **Unknown** means no usable value or evidence was found in the reviewed sources.
- **Inference** means an engineering consequence that must be tested, not a measured property of this vehicle.

The scoped instructions under `.github/instructions/` do not apply to this file's `docs/RESEARCH_VEHICLE_REQUIREMENTS.md` path.
The nested `docs/scripts/get_mode_requirements/AGENTS.md` is not on this file's directory ancestry and does not apply.

## Confirmed Facts

| ID | Confirmed Fact | Evidence | Consequence |
| --- | --- | --- | --- |
| CF-01 | The project is purely for research and has no intended interception use. | U1; R1 section 1 | Requirements and test cases must remain research-oriented. |
| CF-02 | The vehicle is a canard-controlled body with no main fixed wing. | U1; R1 section 1 | Body and canard aerodynamic forces must be modelled from evidence; the absence of a main wing does not imply the absence of aerodynamic lift or side force. |
| CF-03 | Propulsion is one fixed-axis electric ducted fan (EDF). | U1; R1 section 1 | Thrust vectoring is unavailable; the fan's measured thrust axis, line of action, dynamics, and operating limits are essential. |
| CF-04 | There is no thrust vectoring. | U1; R1 section 1 | Attitude moments and non-axial force cannot be credited to a vectored propulsor. |
| CF-05 | A Raspberry Pi 4B requests acceleration. | U1; R1 sections 1 and 2 | The request contract, link, timing, and Pi resource behaviour remain to be specified and measured. |
| CF-06 | PX4 owns state estimation, stabilisation, control allocation, mode supervision, actuator output, and command-loss behaviour. | U1; consistent with R1 sections 2 and 4 | The Pi must not directly own surfaces, EDF output, stabilisation, allocation, or final failsafe action. |
| CF-07 | Gazebo/PX4 SITL is the intended initial simulation environment, and the stock `gz_rc_cessna` must remain a runnable regression baseline. | R1 sections 1-3; A1 | The research vehicle needs a separate model and airframe; stock Cessna physics cannot validate it. |
| CF-08 | Simulator ground truth is reserved for analysis, diagnostics, and test scoring rather than operational control input. | R1 sections 2-3 | Control feasibility and performance must use estimates or measurements available on eventual hardware. |
| CF-09 | `TrajectorySetpoint` is documented in R1 as local-NED acceleration in metres per second squared, with `NaN` denoting uncontrolled states; message acceptance does not prove fixed-wing full-vector acceleration tracking. | R1 section 4, Phase 2 | Reusing this interface remains an interface decision and requires a source-to-consumer trace. |
| CF-10 | PX4 vehicle thrust and torque setpoints are documented as normalised body-axis values, not forces in newtons or moments in newton-metres. | R1 section 4, Phase 4 | Any physical-to-normalised scaling must be explicit and compatible with allocation. |
| CF-11 | Four X-arranged canards, each driven by a separate servo; one rear fixed-axis EDF. | U2 | Four independently commanded surfaces are confirmed; geometry, hinge signs, torque authority and linkage/channel mapping remain unknown. |
| CF-12 | Companion-guided Offboard receives inertial acceleration expressed in FRD only. NED remains the world reference. | U2 | No companion velocity, attitude, rate or thrust setpoint in this mode. PX4 must resolve remaining attitude/roll objectives internally. |
| CF-13 | MAVLink carries requests: host guidance script in SITL, Pi translator through FC TELEM serial on hardware. | U2 | Share semantic validation between transports. Wire message/schema, freshness metadata, rate and serial configuration remain to be designed/tested. |
| CF-14 | DroneCAN pitot, GPS and Pixhawk 6X onboard sensors are planned. | U2 | Exact parts, mounting, performance, timing and estimator configuration remain unknown. A conventional pitot does not directly provide full air-relative velocity direction. |
| CF-15 | Intended launch is release from rest under EDF power with body +X upward; additional initial speed only if validation requires it. Intended recovery is headfirst net entry after slowing. | U2 | These are concepts, not validated operating conditions. Low-speed local flow and minimum controllable capture speed remain unresolved. |

User estimates: vehicle mass approximately 2.5 kg and maximum static thrust approximately 5.5 kgf (kgf is the stated interpretation of the user's kg-thrust wording). Configuration, test conditions and uncertainty are absent. Do not use these estimates as fixed controller gains, normalization constants or validated plant inputs. Earlier air-relative velocity discussion now concerns state/operating objectives; it is not an additional input in acceleration-only Offboard.

User-confirmed interface requirements (2026-09-17, explicit confirmation during C0b.2 review): CG is the acceleration reference point; FRD demand remains fixed in the rotating body axes between updates, until superseded or expired. These are confirmed command semantics, not a measurement of CG location or approval of flight feasibility. The adapter must not silently reinterpret FRD as world-held NED or as the time derivative of body-relative velocity.

A1 and S1 record Ubuntu 24.04 under WSL2, Gazebo Harmonic / Sim 8, PX4 revision `e880f1cd79e2453ef3dfaf68ee1c067b98ea9a53`, and stock airframe autostart 4003.
Those values describe the documented stock baseline only.
They are not custom-vehicle measurements, and this task did not rerun the baseline.

## System Boundary Requirements

| ID | Requirement | Verification Evidence Required |
| --- | --- | --- |
| SYS-01 | The Pi shall issue only the approved research request and liveness metadata; PX4 shall retain the ownership listed in CF-06. | Interface ownership trace plus tests for startup, competing publishers, stale request, disconnect, restart, and reconnect. |
| SYS-02 | Each command stage shall have one active publisher/owner. | Runtime topic/publication inventory for every supported mode and transition. |
| SYS-03 | Operational PX4 control shall consume only hardware-available estimates or measurements. | Input provenance review showing that Gazebo truth is absent from operational controller inputs. |
| SYS-04 | The research model and airframe shall be separately named and shall not alter the stock Cessna baseline. | Repository diff, model selection test, and successful independent stock-baseline procedure. |
| SYS-05 | Physical force and moment units shall not be mixed with PX4 normalised thrust and torque without an approved scaling contract. | Reviewed equations, units, bounds, allocation tests, and logged saturation status. |
| SYS-06 | Software verification and physical validation shall be reported separately. | Evidence manifests that label simulation, Pi-in-the-loop, compile-only hardware, bench, and flight results distinctly. |

## Required Geometry And Independent Actuator Definitions

Four X-arranged canards and four independent servos are confirmed by U2. The
new SDF supplies draft placements and passive-tail geometry, but exact
as-built positions/orientations, hinge signs, linkage/channel mapping and
aerodynamic effects remain unvalidated. Independent actuation does not
establish independent pitch/yaw/roll force or moment authority.

| ID | Required Input | Why It Matters | Required Provenance And Uncertainty | Human Owner | Blocks |
| --- | --- | --- | --- | --- | --- |
| GEO-01 | Body outer geometry and reference axes, origin, reference length and area, and station convention | Establishes wetted/projected areas, aerodynamic reference quantities, collision geometry, and a common coordinate system. | Dimensioned CAD or drawing, revision, measurement method, tolerances, and as-built differences | Human vehicle engineer | Plant; controller |
| GEO-02 | Four canards confirmed; physical identifiers and ordering remain required | Establishes effectors without assuming a sign/mixer convention. | U2 confirms count; as-built drawing/channel record still needed | Human vehicle engineer | Plant; controller; hardware |
| GEO-03 | Position and orientation of each canard relative to the declared reference and expected CG range | Determines moment arms, force directions, coupling, local flow, and sign conventions. | CAD/measurement, coordinate frame, tolerance, and configuration | Human vehicle engineer | Plant; controller |
| GEO-04 | Each canard's span, root/tip chord, planform, airfoil or section, thickness, sweep, dihedral, incidence, hinge line, and deflection convention | Required for aerodynamic force/moment modelling and actuator sign checks. | Drawing/CAD plus measured as-built values; source and uncertainty for aerodynamic section data | Human vehicle engineer / aerodynamicist | Plant; controller |
| GEO-05 | Minimum, maximum, neutral, and mechanically safe deflection for each surface | Defines the feasible set and protects hardware. | Bench measurement referenced to a defined sign convention, including uncertainty and hard-stop evidence | Human vehicle engineer / test lead | Plant; controller; hardware |
| GEO-06 | Independent actuator mapping: each logical surface, servo, linkage, electrical output, direction, mixing/coupling, and failure position | Establishes independent control degrees of freedom and the allocator topology. | Wiring/linkage diagram and unpowered or propulsion-isolated bench inspection | Avionics engineer / human vehicle engineer | Controller; hardware |
| GEO-07 | Linkage ratios, backlash, compliance, free play, and load-dependent deformation | Maps servo motion to surface angle and bounds control accuracy. | Bench measurement over relevant load and temperature, with repeatability/uncertainty | Human vehicle engineer / test lead | Plant fidelity; controller; hardware |
| GEO-08 | All passive lifting, stabilising, strake, fin, body-flap, or other aerodynamic surfaces | Passive surfaces may supply or remove trim and stability authority even without a main wing. | Configuration-controlled CAD and inspection record | Human vehicle engineer | Plant; feasibility |
| GEO-09 | EDF inlet, outlet, duct geometry, thrust-axis orientation, line of action, and location relative to the CG | Determines propulsive force and moment and possible inlet/body interactions. | CAD and as-built measurements with alignment tolerances | Propulsion owner / human vehicle engineer | Plant; controller |
| GEO-10 | Spatial relation between EDF discharge flow and each canard across configuration and operating state | Determines whether any low-speed canard authority may be credited to fan wash. | Flow measurement or validated analysis with uncertainty; geometry alone is insufficient to claim effective wash | Propulsion owner / aerodynamicist | Low-speed feasibility; plant; controller |
| GEO-11 | Sensor locations and orientations relative to the body frame and CG | Needed for frame transforms, lever-arm effects, and representative simulation. | Installation drawing and surveyed alignment/position uncertainty | Avionics engineer | Plant; estimator; hardware |

## Required Physical, Propulsion, Servo, Sensor, Power, Launch, And Recovery Data

All entries in this section are presently unknown unless stated otherwise.

### Mass Properties

| ID | Required Input | Why It Matters | Required Provenance And Uncertainty | Human Owner | Blocks |
| --- | --- | --- | --- | --- | --- |
| PHY-01 | Ready-to-operate mass for each intended configuration | Converts force to acceleration and affects launch, recovery, trim, and actuator loading. | Calibrated scale measurement, configuration list, repeated readings, and instrument uncertainty | Human vehicle engineer | Plant; controller; hardware |
| PHY-02 | Three-dimensional CG and its range as energy state or payload changes | Sets moment arms, trim, static behaviour, and allocation effectiveness. | Measured balance/suspension method or validated CAD, configuration and uncertainty | Human vehicle engineer | Plant; controller; feasibility |
| PHY-03 | Full inertia tensor about a declared point and axes, including products of inertia | Determines angular response and axis coupling; diagonal guesses can create false stability. | Physical identification or traceable CAD mass model, method, configuration, and uncertainty | Human vehicle engineer / dynamics reviewer | Plant; controller |
| PHY-04 | Mass, CG, and inertia variation across allowed configurations | Required for robust control and uncertainty sweeps. | Configuration-controlled measurements or CAD-supported bounds | Human vehicle engineer | Plant validation; robustness |

### Aerodynamics And Propulsion

| ID | Required Input | Why It Matters | Required Provenance And Uncertainty | Human Owner | Blocks |
| --- | --- | --- | --- | --- | --- |
| AER-01 | Body and canard force/moment data versus relevant angle of attack, sideslip, control deflection, and rate | Determines trim, stability, coupling, and achievable acceleration. | Wind-tunnel, CFD, system identification, or other approved data; method, independent validation split, range, and uncertainty | Aerodynamicist / human vehicle engineer | Plant; controller; feasibility |
| AER-02 | Stall, separation, hysteresis, and interaction limits within or near the intended envelope | Prevents unsupported extrapolation and defines loss-of-authority boundaries. | Test or validated analysis with range and uncertainty | Aerodynamicist / test lead | Envelope; plant; safety |
| AER-03 | Local-flow effects at each canard, including body shadowing and any EDF wash | Surface authority depends on local dynamic pressure and flow direction, not only vehicle airspeed. | Measurement or validated local-flow analysis across relevant conditions | Aerodynamicist / propulsion owner | Low-speed feasibility; plant |
| PROP-01 | EDF, motor, ESC, duct, and rotor identification and configuration | Makes performance data applicable to the installed system. | Part identifiers, firmware/configuration, installation record, and data-sheet revisions | Propulsion owner | Plant; hardware |
| PROP-02 | Installed thrust and electrical demand versus command, inlet airspeed, supply state, and relevant environment | Defines axial acceleration, saturation, endurance, and power limits. | Calibrated installed-system test data or justified manufacturer data, with uncertainty and applicability limits | Propulsion owner / test lead | Plant; controller; power |
| PROP-03 | EDF/ESC transient response, startup behaviour, command update characteristics, and shutdown behaviour | Determines achievable acceleration bandwidth and command-loss response. | Bench transient tests with timestamps and uncertainty | Propulsion owner / test lead | Plant; controller; hardware |
| PROP-04 | Whether reverse thrust exists, and the permitted command range | Defines whether negative body-axis propulsive force is available. | Installed hardware specification and bench verification | Propulsion owner | Feasible acceleration set; allocator |
| PROP-05 | Propulsor reaction torque, gyroscopic effects, inlet distortion, and other material coupled moments | These effects may consume canard authority or couple axes. | Measurement or justified analysis with uncertainty; materiality criterion to be approved | Propulsion owner / dynamics reviewer | Plant; controller |

### Servos And Control Surfaces

| ID | Required Input | Why It Matters | Required Provenance And Uncertainty | Human Owner | Blocks |
| --- | --- | --- | --- | --- | --- |
| ACT-01 | Servo identification, supply condition, command protocol, update rate, and resolution | Establishes command compatibility and quantisation. | Part data plus configuration and bench verification | Avionics engineer | Plant; hardware |
| ACT-02 | Surface-angle response versus command, including neutral, limits, deadband, backlash, and nonlinearity | Required for correct allocation and simulation. | Propulsion-isolated bench sweep under relevant supply and load, with uncertainty | Human vehicle engineer / test lead | Plant; controller; hardware |
| ACT-03 | Slew rate, delay, bandwidth, torque margin, and behaviour under aerodynamic load | Determines closed-loop bandwidth and saturation/loss of authority. | Loaded bench or component test across relevant operating conditions | Human vehicle engineer / test lead | Plant; controller; envelope |
| ACT-04 | Servo fault modes, power-loss state, jam/runaway detection, and safe mechanical range | Required for fault response and acceptance tests. | Hardware documentation and propulsion-isolated fault tests | Avionics engineer / test lead | Hardware; failure tests |

### Sensors And State Estimation

| ID | Required Input | Why It Matters | Required Provenance And Uncertainty | Human Owner | Blocks |
| --- | --- | --- | --- | --- | --- |
| SNS-01 | Planned flight sensor inventory and exact Pixhawk/avionics configuration | The simulator and controller must use states observable on hardware. | Bill of materials, bus topology, firmware configuration, and revision | Avionics engineer | Plant sensors; estimator; hardware |
| SNS-02 | Sensor frames, mount orientation/location, rate, noise, bias, latency, synchronisation, saturation, and dropout behaviour | Required for representative estimation and for lever-arm/time-base handling. | Data sheets plus measured installation and timing evidence, with uncertainty | Avionics engineer / estimator owner | Plant; estimator; robustness |
| SNS-03 | DroneCAN pitot is planned; model, installation, calibrated range and validity limits remain required | Scalar pitot airspeed is not a full air-relative velocity vector or proof of canard-local flow. | U2 confirms sensor category; calibrated installation and sensor-model evidence still required | Systems owner / avionics engineer | Controller scheduling; hardware |
| SNS-04 | State-validity criteria needed by control and the unavailable/invalid-state response | Prevents operation on stale or invalid estimates. | Estimator/interface review and approved mode/failsafe specification | Estimator owner / systems owner | Controller; interface |

### Electrical Power

| ID | Required Input | Why It Matters | Required Provenance And Uncertainty | Human Owner | Blocks |
| --- | --- | --- | --- | --- | --- |
| PWR-01 | Battery chemistry, series/parallel configuration, capacity, condition, and permitted voltage/current/temperature envelope | Bounds thrust, endurance, mass properties, and safe operation. | Battery and system records plus measured performance/uncertainty | Power-system owner | Propulsion model; envelope; hardware |
| PWR-02 | Power distribution, regulator/BEC ratings, grounding, protection, and loads for EDF, servos, Pixhawk, sensors, and Pi/link equipment | Required to assess brownout, resets, common-cause loss, and actuator authority. | Wiring diagram, component specifications, and measured load/brownout data | Power-system owner / avionics engineer | Hardware; failsafe |
| PWR-03 | Energy/endurance budget for each approved research profile | Defines experiment duration and recovery reserve. | Measured consumption and approved reserve policy with uncertainty | Power-system owner / test lead | Envelope; launch/recovery |

### Launch And Recovery

| ID | Required Input | Why It Matters | Required Provenance And Uncertainty | Human Owner | Blocks |
| --- | --- | --- | --- | --- | --- |
| OPS-01 | Launch method, initial position/velocity/attitude/rates, minimum control-ready state, and abort conditions | Low-speed canard authority may be inadequate before sufficient local flow exists. | Human-approved concept of operations and measured launcher/manual-release variability | Test lead / human vehicle engineer | Feasibility; plant scenarios; modes |
| OPS-02 | Recovery method, terminal-state envelope, energy isolation, and abort/fallback concept | A no-main-wing body may not support stock loiter, return, or landing assumptions. | Human-approved concept of operations and physical evidence | Test lead / systems owner | Modes; controller; hardware |
| OPS-03 | Test area/environment limits, supervision, containment, and pre/post-test inspection | Required for a bounded research validation process. | Human test plan and applicable institutional rules | Test lead | Bench/flight phase |

## Acceleration Request Semantics And Performance Requirements

The user selected acceleration-only inertial FRD requests and MAVLink. Timing, validity, wire metadata, physical bounds, residual-attitude policy and flight fallback are not yet a complete control contract.
Inertial acceleration and accelerometer specific force differ by gravity, and a single fixed thrust axis plus aerodynamic surfaces cannot be assumed to realise an arbitrary instantaneous three-axis vector.

| ID | Required Decision Or Requirement | Current State | Why It Matters | Human Owner | Blocks |
| --- | --- | --- | --- | --- | --- |
| ACC-01 | Three-component inertial FRD acceleration is the only companion control objective in research Offboard. | Confirmed U2; feasible attainable subset unknown | All axes must be represented explicitly; PX4 limits/rejects infeasible objectives. No companion speed/thrust objective. | Research/mission owner with controls lead | Physical bounds; controller |
| ACC-02 | Inertial acceleration: `a_B = R_NB^T a_N`; specific force is `f_B = a_B - R_NB^T g_N`. | Inertial semantics confirmed U2 | No added gravity bias in companion demand; local gravity/attitude are used when comparing against IMU specific force. | Controls lead / interface owner | Conversion tests; controller |
| ACC-03 | FRD command axes and NED world axes; `R_NB` maps FRD vectors into NED. | Confirmed U2; adapter conversion details pending | Body inertial acceleration is not the derivative of body velocity components. | Interface owner | Interface; tests |
| ACC-04 | Reference requests to CG; compensate any IMU lever arm explicitly. | CG reference confirmed by user during C0b.2 review; actual installation/CG location still requires evidence | Acceleration varies across a rotating rigid body. | Controls lead / dynamics reviewer | Sensor adapter; controller |
| ACC-05 | Use m/s^2 and require all three finite FRD components; no unused fields. Define physical bounds and infeasible-command handling later. | Canonical software contract selected by master; physical bounds and wire encoding remain unknown. Stock trajectory NaN semantics do not apply to this contract. | Transport semantics must agree with validation and controller limiting. | Interface owner / controls lead | Interface; controller |
| ACC-06 | Define heading, attitude, body-rate, speed, or thrust policy for degrees of freedom not fixed by acceleration. | Unknown | Acceleration alone may not determine a unique attitude or heading. | Research/mission owner with controls lead | Interface; controller |
| ACC-07 | Define clock, timestamp meaning, sequence/order rules, validity interval, update rate, deadline, and tolerated delay/jitter. | Unknown | Separates current requests from stale/replayed ones and sets link/control budgets. | Interface owner / controls lead | Interface; Pi tests |
| ACC-08 | MAVLink host-script transport for SITL, Pi translator and TELEM serial for hardware. | Selected U2; wire contract/library/version/rate still pending | Stock trajectory reception loses metadata needed for the proposed research guard. | Systems owner / interface owner | Transport implementation; interface |
| ACC-09 | Define mode-entry/exit conditions, arming relationship, liveness heartbeat, command freshness, competing-source policy, acknowledgement/status, and reconnect behaviour. | PX4 ownership is confirmed; detailed behaviour is unknown. | A live link is not proof of a fresh command, and reception is not proof of execution. | Systems owner / interface owner | Interface; modes; tests |
| ACC-10 | Define PX4 response to stale, invalid, infeasible, or lost requests and to loss of state/control authority. | PX4 must own the response; the safe response and timing are unknown. | Stock hover, loiter, return, or landing behaviour cannot be presumed feasible. | Systems owner / test lead / controls lead | Controller; modes; hardware |
| ACC-11 | Define requested-profile duration, repetition, boundary profiles, and permitted transitions. | Unknown | Enables power sizing and deterministic verification without tuning to an unspecified test. | Research/mission owner | Requirements; test specification |
| ACC-12 | Set per-axis tracking metrics and thresholds for RMS error, peak error, settling time, overshoot, steady-state error, and cross-axis response. | Metrics are required by R1; numerical thresholds and evaluation windows are unknown. | A test cannot pass objectively without predeclared limits. | Research/mission owner with controls lead | Test scoring; controller gate |
| ACC-13 | Set limits for command age, deadline misses, saturation duration, invalid-estimate duration, and loss-response timing. | Unknown | Quantifies communications and authority failures. | Systems owner / controls lead / test lead | Interface; robustness gate |
| ACC-14 | Define status that distinguishes received, validated, limited, active, saturated, rejected, and fallback states. | Unknown | Prevents message receipt from being mistaken for acceleration tracking. | Interface owner / controls lead | Interface; evidence |

## Intended Envelope And Required Control Authority

No flight envelope has been supplied.
The initial envelope must be a bounded set of jointly valid conditions, not independent maxima assembled into physically untested combinations.

| ID | Envelope Input Or Authority Requirement | Required Evidence / Pass Condition | Human Owner | Blocks |
| --- | --- | --- | --- | --- |
| ENV-01 | True airspeed range and how it is measured or estimated | Approved bounds supported by aerodynamic/propulsion data and a hardware-available airspeed provenance | Human vehicle engineer / estimator owner | Plant; controller |
| ENV-02 | Altitude, air density, temperature, wind, gust, and precipitation limits | Approved environmental envelope and model validity evidence | Test lead / aerodynamicist | Plant; robustness; flight |
| ENV-03 | Angle-of-attack and sideslip range | Evidence that force/moment data and state estimation remain valid throughout the range | Aerodynamicist / estimator owner | Plant; controller |
| ENV-04 | Attitude, body-rate, and angular-acceleration range | Feasible, observable bounds supported by inertia, actuator, and aerodynamic evidence | Controls lead / dynamics reviewer | Controller; modes |
| ENV-05 | Translational acceleration and speed range by axis | A force/authority analysis over the full joint envelope, with infeasible regions declared | Research/mission owner / controls lead | Interface bounds; controller |
| ENV-06 | Mass, CG, inertia, battery, and configuration range | Configuration-controlled measured bounds and sensitivity/robustness analysis | Human vehicle engineer / power-system owner | Plant; robustness |
| ENV-07 | Launch, transition, normal research, degraded, and recovery sub-envelopes | Each sub-envelope has entry, exit, abort, state-validity, and authority criteria | Test lead / systems owner | Modes; hardware |
| AUTH-01 | Trim authority for every approved steady operating point | Force and moment residuals fall within predeclared tolerances while all effectors retain the approved reserve | Dynamics reviewer / controls lead | Phase 0 feasibility; plant gate |
| AUTH-02 | Pitch, roll, and yaw moment authority in both directions where required | Worst-case achievable moments exceed approved disturbance and manoeuvre demands with predeclared reserve | Controls lead / human vehicle engineer | Controller architecture |
| AUTH-03 | Axial and non-axial force authority for the selected acceleration semantics | The attainable force set contains every accepted request after limits; unavailable directions are rejected or limited predictably | Controls lead / dynamics reviewer | Interface bounds; controller |
| AUTH-04 | Low-speed and launch authority | Measured or validated local flow supports required canard moments before the vehicle enters the normal envelope | Human vehicle engineer / aerodynamicist | Physical feasibility |
| AUTH-05 | Coupled-force and coupled-moment manageability | The effectiveness model includes material coupling and demonstrates feasible allocation without hidden cancellation assumptions | Dynamics reviewer / controls lead | Plant; allocator |
| AUTH-06 | Saturation, slew, and power authority | Expected manoeuvres remain within approved servo, EDF, ESC, and electrical limits, or are limited and reported | Controls lead / power-system owner | Controller; hardware |
| AUTH-07 | Loss-of-authority detection and response | Loss is detected using hardware-available signals and transitions to the approved response within its TBD timing limit | Systems owner / test lead | Modes; controller; hardware |

Groundspeed, physical airspeed, synthetic airspeed, and local canard flow are not interchangeable.
S1 records synthetic airspeed for the stock Cessna, but that does not decide the custom vehicle's sensor suite or prove aerodynamic authority.

## Assumptions, Unknowns, And Ownership Register

The register deliberately avoids assigning nominal values.
Human owners are roles that must be mapped to named people before approval.

| Register ID | Statement | Classification | Evidence / Provenance | Uncertainty | Human Owner | Phase Impact |
| --- | --- | --- | --- | --- | --- | --- |
| REG-01 | Body/canard aerodynamics produce some force when local flow exists. | Qualitative physical inference, not a quantified vehicle property | General mechanics plus CF-02; vehicle data absent | Magnitude, direction, range, nonlinearities, and coupling unknown | Aerodynamicist | Blocks quantified feasibility and plant |
| REG-02 | Canards may have insufficient authority at launch or low speed. | Risk / unresolved inference | R1 development principle; GEO-10 and AER-03 absent | Onset speed/local flow and all authority margins unknown | Human vehicle engineer / aerodynamicist | May be a physical-design blocker |
| REG-03 | EDF flow reaches the canards. | Unsupported assumption; shall not be used | No supplied evidence | Entire effect unknown | Propulsion owner / aerodynamicist | Blocks credit for powered low-speed control |
| REG-04 | The vehicle can track arbitrary three-axis acceleration. | Unsupported assumption; shall not be used | Fixed thrust-axis configuration and absent authority data | Feasible request set unknown | Controls lead / dynamics reviewer | Blocks interface and controller claims |
| REG-05 | Existing PX4 fixed-wing control supports the required vector request. | Unsupported assumption; shall not be used | R1 explicitly warns message reception does not prove consumer support | Consumer path and supported components unresolved by this document | Interface owner / controls lead | Requires Phase 2 source trace and Phase 4 review |
| REG-06 | Stock Cessna or `advanced_plane` coefficients represent this vehicle. | Rejected assumption | A1 and R1 define them only as baseline/reference | Custom coefficient set wholly unknown | Human vehicle engineer / aerodynamicist | Plant blocked |
| REG-07 | Diagonal inertia is adequate. | Unsupported assumption; shall not be used | No mass-property evidence | Full tensor unknown | Human vehicle engineer / dynamics reviewer | Plant and controller blocked |
| REG-08 | Stock fallback modes are safe for this configuration. | Unsupported assumption; shall not be used | R1 says hover, loiter, return, and landing cannot be presumed feasible | All fallback performance unknown | Systems owner / test lead | Mode and hardware phases blocked |
| REG-09 | Simulator truth is available to the operational controller. | Prohibited design assumption | CF-08 | Not applicable | Controls lead / independent reviewer | Must be checked in controller review |
| REG-10 | Pi 4B timing and load are adequate. | Unverified hypothesis | Only the platform role is confirmed | CPU, scheduling, transport, and link margins unknown | Companion/interface owner | Pi-in-the-loop gate blocked |
| REG-11 | The documented stock baseline remains reproducible in the current environment. | Documented but unverified in this task | A1 and S1 | Current runtime state unknown | Infrastructure owner | Phase 1 must verify separately |
| REG-12 | Four X canards/four independent servos and rear EDF are known; geometry and channel/sign mapping are not. | Partly confirmed | U2; no CAD/drawing/mapping supplied | Authority, dimensions and moment arms unknown | Human vehicle engineer | Plant, active control and hardware blocked; independent software work allowed |
| REG-13 | Mass is estimated near 2.5 kg; CG/inertia unknown. | User estimate, not measurement | U2; no CAD mass report or measurement | Configuration and uncertainty unknown | Human vehicle engineer | Quantified plant/control/feasibility blocked |
| REG-14 | Maximum EDF static thrust is expected near 5.5 kgf; installed performance and servo dynamics unknown. | User estimate, not measurement | U2; component/test data absent | Voltage/configuration, transient and load dependence unknown | Propulsion owner / test lead | Quantified plant/control/hardware blocked |
| REG-15 | Pitot/GPS/6X sensor scope and vertical EDF-only/net-capture intent are known. | Confirmed intent, unvalidated | U2; exact hardware, power system and tests absent | Authority, installation and operating limits unknown | Systems owner / test lead | Plant sensors, operational modes and hardware blocked |
| REG-16 | Quantitative tracking and safety thresholds are approved. | Unknown | R1 requires them but supplies none | Every acceptance value and scoring window unknown | Research/mission owner / test lead | Requirements and robustness gates incomplete |

## Measurable Acceptance Criteria Requiring Project Decisions

The test quantity and pass logic below can be specified now, but every `TBD` limit must be approved before the corresponding test is run for acceptance.
Limits must not be relaxed after observing results without recording a new requirement revision and rerunning the affected evidence.

| Criterion ID | Measurement And Pass Logic | Decision Still Required | Evidence Stage |
| --- | --- | --- | --- |
| AC-01 | For each approved trim point, measure net force and moment residuals and actuator operating points; pass only within approved residual and reserve limits. | Trim points, residual tolerances, and per-effector reserve | Open-loop plant |
| AC-02 | Compute the local control-effectiveness matrix over the joint envelope; pass only where required axes/directions are available with approved conditioning and authority margins. | Required axes, conditioning metric/limit, disturbance cases, and reserve | Feasibility / allocator |
| AC-03 | Compare model force, moment, mass-property, EDF, and servo responses against evidence withheld from calibration; pass within approved per-quantity error bands. | Validation dataset split, metrics, ranges, and tolerances | Plant validation |
| AC-04 | Apply each approved acceleration profile and calculate per-axis RMS/peak error, settling time, overshoot, steady-state error, and cross-axis response over predeclared windows. | Profiles, windows, metrics that apply per axis, and numerical limits | Estimator-in-the-loop SITL |
| AC-05 | Record command timestamp/sequence, receipt, validation, activation, limitation, rejection, and fallback; pass only when age, ordering, jitter, and deadlines meet approved limits. | Clock basis, request rate, validity interval, latency/jitter/deadline limits, sequence policy | Interface / Pi-in-the-loop |
| AC-06 | Inject non-finite, out-of-bounds, stale, replayed, and competing requests; pass only if no unapproved actuation occurs and the required status/fallback is observed. | Bounds, freshness limit, source-priority policy, status contract, and fallback timing | Interface / modes |
| AC-07 | Disconnect and restart the Pi while separately controlling liveness and command freshness; pass only if PX4 performs the approved response and recovers only under approved conditions. | Loss response, timeout, re-entry/reconnect criteria, and operator acknowledgement policy | Pi-in-the-loop / modes |
| AC-08 | Measure surface angle and EDF response against commands under approved supply and load cases; pass within approved static, delay, slew, bandwidth, and repeatability limits. | Test cases and limits for every actuator | Bench / plant validation |
| AC-09 | Exercise nominal and boundary conditions while logging actuator saturation/slew limiting and electrical state; pass within approved continuous and transient limits. | Saturation-duration, power, temperature, and reserve limits | SITL then hardware |
| AC-10 | Remove or invalidate each required estimate; pass only if authority/state loss is detected and the approved transition occurs within its limit. | Validity rules, detection thresholds/times, and safe response | Estimator-in-the-loop / hardware |
| AC-11 | Evaluate power and energy over each bounded profile; pass only if voltage/current/temperature and recovery reserve remain within approved limits. | Profile duration, environmental cases, electrical limits, and reserve policy | Bench / flight readiness |
| AC-12 | Repeat approved scenarios across justified uncertainty in mass, CG, inertia, aerodynamics, actuators, sensors, wind, and communications; pass every required case. | Evidence-backed uncertainty ranges, sample/design method, and numerical performance thresholds | Robustness regression |
| AC-13 | Inspect every operational input and scoring signal for publisher, frame, units, and time base; pass only if controller inputs are hardware-available and truth is confined to scoring/diagnostics. | Approved signal inventory and any diagnostic access controls | Architecture / integration |
| AC-14 | Run the same companion application on Pi 4B and measure CPU, scheduling, link delay/jitter/loss, and deadline misses; pass against approved margins. | Pi OS/dependencies, physical link, command rate, load case, and resource/timing margins | Pi-in-the-loop |
| AC-15 | Perform propulsion-isolated sensor, servo-direction, mode, disconnect, and restart checks before any powered test; pass every approved checklist item. | Exact bench procedure, safe mechanical states, abort criteria, and named approvers | Hardware readiness |

No test result exists for these criteria in this task.

## Physical Feasibility Checklist

`Not assessable` means essential measurements or decisions are absent; it does not mean the design has failed.

| Check | Current Disposition | Evidence Needed To Pass | Failure Or Design-Review Trigger |
| --- | --- | --- | --- |
| Mass-property sanity | Not assessable | Measured/configuration-controlled mass, CG range, and physically valid full inertia tensor | Non-physical tensor, undocumented configuration dependence, or CG outside the demonstrated controllable range |
| Unpowered trim, if required by the chosen operations or recovery concept | Conditional; operations decision pending | If unpowered flight is required, body/canard aerodynamic data and equilibrium solutions over its approved envelope | The approved concept requires unpowered flight but no equilibrium exists within approved surface and attitude limits |
| Powered trim | Not assessable | Installed EDF map, thrust line, coupled moments, local-flow data, and equilibrium solutions | Required thrust/surface command exceeds limits or leaves inadequate approved reserve |
| Pitch authority | Not assessable | Canard layout/effectiveness, CG range, inertia, disturbance/manoeuvre demand, and bidirectional moment margins | A required pitch direction or rate cannot be achieved over the approved envelope |
| Roll authority | Not assessable | Independent actuator topology and differential/coupled effectiveness data | No controllable roll direction, unacceptable coupling, or inadequate margin |
| Yaw authority | Not assessable | Passive surfaces, independent actuator topology, and yaw effectiveness data | No controllable yaw direction when required, unacceptable sideslip, or inadequate margin |
| Axial acceleration authority | Not assessable | Mass and installed EDF thrust/dynamic/power data across airspeed and supply state | Requested axial range exceeds the attainable set or response bandwidth |
| Non-axial acceleration authority | Not assessable | Validated body/canard force set and attitude policy over the joint envelope | Requests require unavailable instantaneous force or violate attitude/envelope limits |
| Coupled moments and forces | Not assessable | Full six-axis effectiveness and propulsor-interaction model compared with evidence | Unmodelled/material coupling defeats trim, allocation, or stability |
| Low-speed/launch control | Potential physical-design blocker | Launch state distribution and measured/validated local canard flow, including any claimed EDF wash | Required authority is absent before safe envelope entry |
| Actuator saturation and slew | Not assessable | Servo/EDF maps, dynamic limits, allocator behaviour, and saturation telemetry | Persistent/excessive saturation, rate limiting, or hidden demand clipping under an accepted profile |
| Allocation conditioning | Not assessable | Effectiveness matrix over configuration/envelope plus declared required axes | Rank loss or unacceptable conditioning for required objectives |
| Estimator observability/validity | Not assessable | Hardware sensor suite, model validity, and fault/state-validity tests | Required states are unavailable, stale, biased beyond tolerance, or based on simulator truth |
| Power and endurance | Not assessable | Installed electrical measurements for each profile and reserve policy | Brownout, reset, thermal/electrical limit violation, or inadequate recovery reserve |
| Loss of command or authority | Not assessable | Vehicle-appropriate fallback decision and tests using hardware-available signals | Undefined/unsafe fallback, delayed detection, conflicting publishers, or uncommanded actuation |
| Launch and recovery feasibility | Not assessable | Approved concept and bounded state/authority analysis with abort criteria | No demonstrated path into and out of the controlled envelope |

The most immediate feasibility risk is low-speed control.
Aerodynamic canards require adequate local flow, and EDF wash cannot be credited without geometry and flow evidence.
The other fundamental risk is control rank: the fixed-axis EDF supplies only one propulsive direction; four independent canards do not establish a full-rank effectiveness matrix over the intended envelope.

## Phase Blockers And Work That Can Continue

| Phase / Workstream | Status From This Document | Exact Blocker Or Permitted Work |
| --- | --- | --- |
| Phase 0 requirements and feasibility | Incomplete | Geometry/actuation, mass properties, aero/propulsion/servo data, envelope, operations, request semantics, and numerical acceptance criteria are absent. |
| Phase 1 stock baseline and evidence | Can proceed independently | This requirements gap does not prevent read-only inventory or stock Cessna reproducibility work; current runtime claims must still be verified separately. |
| Phase 2 interface/source trace | Partly unblocked | Source tracing and a decision matrix can proceed; final contract/bounds are blocked by ACC-01 through ACC-14 decisions and control-authority evidence. |
| Phase 3 custom plant | Blocked for validated implementation | GEO, PHY, AER, PROP, ACT, SNS, and PWR data are missing; model asset ownership must also be decided per R1. No substitute Cessna/reference coefficients are acceptable. |
| Phase 4 controller/allocation | Blocked | Requires approved interface semantics, a validated plant/effectiveness model, feasible envelope, actuator topology, and acceptance limits. No gains or allocation matrix can be justified yet. |
| Phase 5 companion prototype | Blocked for contract-compliant implementation | Requires transport and complete request/timing/liveness/status contract; desktop software structure may be planned after that choice. Pi adequacy requires Pi measurements. |
| Phase 6 robustness | Blocked | Requires an integrated nominal system, evidence-backed uncertainty ranges, scenarios, and predeclared numerical thresholds. |
| Phase 7 compile-only platform work | Not blocked by physical data for inventory/build planning, but qualification is blocked | Board/configuration can be inventoried separately; performance, I/O, resource, and hardware-simulation claims require the implemented system and hardware evidence. |
| Phase 8 bench/flight validation | Blocked | Requires reviewed prior evidence and a human-approved, propulsion-isolated bench and bounded research test procedure. |

## Minimum Questions For The Human Team

The following evidence packages are eventual physical-release inputs. U2 authorizes geometry-independent architecture/code now, so these are not blocking questions for that work; counts, command quantity/frame, transport, sensor categories and operating concept have already been answered above.
Attachments may answer multiple questions at once, but each must state revision, configuration, measurement or analysis method, units, frame, and uncertainty.

1. What is the as-built physical configuration?
   Provide body and canard CAD/drawings for the confirmed four independently actuated canards; positions, orientations, sections, hinges, deflection signs/limits, linkages; passive surfaces; EDF axis/inlet/outlet; and the geometric relation of EDF discharge to each canard.
2. What mass-property evidence exists?
   Provide ready-to-operate mass, three-dimensional CG and configuration range, and the full inertia tensor, identifying measured values versus CAD estimates and their uncertainties.
3. What installed force and actuator evidence exists?
   Provide body/canard aerodynamic data, installed EDF/ESC thrust and transient/power data, and servo surface-angle, slew, delay, load, and fault data, each with provenance and valid ranges.
4. What is the bounded operating concept?
   Define the initial joint speed/airspeed, angle-of-attack, sideslip, attitude/rate, acceleration, atmosphere/wind, mass/CG/power envelope; planned sensors; launch/recovery and abort concept; profile duration; and applicable evidence already available.
5. What must count as success for the confirmed acceleration-only FRD/MAVLink interface?
   CG reference and body-held FRD semantics are now user-confirmed. Eventually approve request rate/freshness/deadline, feasible bounds, status feedback, command-loss response, internal residual-attitude policy and numerical tracking/timing/saturation/state-validity thresholds. Command quantity, axes, transport and TELEM link category no longer need selection.

Phase 0 remains incomplete due to missing evidence; neither feasibility nor physical infeasibility has been established.
