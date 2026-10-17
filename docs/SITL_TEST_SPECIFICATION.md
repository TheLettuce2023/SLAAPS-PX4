# SITL Evidence And Test-Harness Specification

Status: Wave 2 planning specification only.

This document defines evidence that a later harness, checker, scenario library, and scorer must produce and validate.
It does not implement a runner, execute SITL, modify a Gazebo model, approve a flight envelope, or establish acceleration-tracking performance.
It is deliberately fail closed: absent data, unverifiable provenance, or an unapproved threshold cannot become a pass.

## Scope, Authority, And Current Facts

This specification uses the confirmed project facts supplied after the Wave 1 review.
Those facts supersede contrary earlier unknowns in the Wave 1 documents; the master is responsible for bringing the requirements, interface, and gate documents into alignment.
This file neither edits nor replaces those artifacts.

| Item | Status for this specification |
| --- | --- |
| Vehicle | Research vehicle; four X-arranged canards, each with an independent servo; one rear, fixed-axis EDF; no main wing and no thrust vectoring. |
| Command authority | PX4 owns estimation, validation, control, allocation, actuator outputs, supervision, and command-loss response. One owner must be active at every command stage. |
| Research request | The producer sends only requested **inertial acceleration**, resolved in FRD at the master working CG reference. It does not command specific force, velocity, position, attitude, angular rate, thrust, torque, or actuators. Required timing, sequence, and validity metadata are not additional control objectives. |
| CG reference | Master working convention for this contract; it is not a user-approved geometry datum or measured CG location. |
| Estimator world frame | PX4's estimator world is NED. A held FRD request follows the current body frame until it expires: PX4 must transform it at each control/evaluation instant with the current valid estimator attitude, rather than freezing the request-sample orientation. Adapter source-attitude association is logged separately; it is not an attitude setpoint. |
| SITL transport | A host guidance script is the research-command producer and uses MAVLink for SITL. It must emulate the approved producer contract and send no velocity or attitude setpoint. |
| Hardware transport | A Pi-to-flight-controller translator carries the Pi request over a TELEM serial link. Port, baud rate, translator protocol/version, framing, and time behaviour are unresolved evidence items. |
| Onboard sensing | Pixhawk 6X onboard sensors, DroneCAN pitot, and GPS are planned hardware inputs. Their placement, calibration, rate, latency, estimator configuration, and validity criteria remain unapproved. |
| Launch and capture intent | Launch is nose-up, body `+X` vertical, from rest under EDF power. Additional launch speed may only be introduced after evidence shows it is needed. Recovery is intended as head-first net capture with a slowdown objective; minimum control speed and capture speed are unknown. |
| Provisional planning inputs | The user estimates `2.5 kg` mass and `5.5 kgf` thrust. `kgf` is a force unit; neither value is a measured installed property, operational limit, or prescribed software fixture. |

The estimates are provenance to retain in planning records only.
They are not plant parameters, operational limits, proof of vertical-launch margin, a required test load, or authority to tune a controller.
Every actual plant run must cite configuration-controlled mass, installed-thrust data, uncertainty, and an approval record.

The custom model's intended owner is a project external resource directory outside the `Tools/simulation/gz` submodule.
No model asset is created or changed by this specification.
Each future run must record the external resource directory identity, revision, content hashes, and lookup configuration; the stock `gz_rc_cessna` baseline remains separately runnable.

`CG` is the master's current working reference for the request and scoring contract, not user-approved geometry or a measured location.
Its actual location, range, and uncertainty remain required configuration evidence before plant-dependent qualification.

## Normative Terms And Result States

`MUST` denotes a checker or harness requirement.
`SHALL` denotes a requirement on a future implementation or test procedure.
`MAY` denotes an optional diagnostic.
An approved requirement is a versioned requirement with an approver, approval date, and immutable content hash in the manifest.

Each required case has exactly one terminal result:

| Result | Meaning |
| --- | --- |
| `pass` | The case was completely executed, all mandatory evidence validates, all predeclared applicable criteria evaluate, and every criterion passes. |
| `fail` | Execution occurred and a criterion, integrity check, safety/ownership rule, or required signal check failed. |
| `incomplete` | The case cannot be scored or qualified: a prerequisite, required artifact, approved threshold, required signal, or valid sample interval is absent. It is not a pass. |
| `not_run` | The case was intentionally not started. It is permissible only before a test campaign; a required campaign with this state is incomplete. |

A run is `pass` only if its manifest is valid and every applicable required case is `pass`.
The scenario registry declares each case's applicability before execution, with a named rationale and hash; an inapplicable case is omitted from that run's required set.
`not_applicable` is a registry applicability disposition, never a case result and never a substitute for a failed or missing applicable case.
A corrupt manifest is a `fail`; a valid manifest whose supplied evidence cannot support a calculation is `incomplete`.

## Frames, Gravity, Reference Point, And Signal Provenance

The following conventions are mandatory in every scenario, log schema, and score report.

| Name | Definition and required treatment |
| --- | --- |
| NED | Estimator world frame: north, east, down. `g_NED = [0, 0, +g]` with the declared gravity model/value. |
| FRD | Vehicle body frame: forward (`+X`), right (`+Y`), down (`+Z`). The body origin and axes must be configuration controlled. |
| Requested acceleration | `a_req_FRD` is inertial CG acceleration resolved in FRD, in `m/s^2`. While a validated request is active, its components are held in the current body frame until expiry. It is a requested kinematic quantity, not an accelerometer output and not a force. |
| NED conversion | At each control or scoring instant `t_eval`, `a_req_NED(t_eval) = R_NED_from_FRD(t_eval) * a_req_FRD_active(t_eval)`, using the current valid estimator attitude under a declared interpolation policy. The log shall identify that attitude sample, timestamp, validity, and transform version. Any source-attitude association used by a producer/adapter is recorded separately. A request carries no attitude setpoint. |
| Specific force | `f_CG_NED = a_CG_NED - g_NED`; equivalently resolve this vector to FRD only when required by a sensor or estimator comparison. A checker must reject a run that labels `f` as `a`, adds gravity twice, or omits the declared gravity treatment. |
| Reference point | Requested and scored acceleration uses the CG working reference declared for the run. IMU acceleration at an offset is not interchangeable; any conversion shall record lever arm, angular-rate/angular-acceleration terms, source, and uncertainty. The actual CG remains unqualified until supplied configuration evidence is accepted. |
| Vertical launch convention | At the declared initial nose-up attitude, body `+X` is vertically upward and is opposite NED down. The transform test must demonstrate this sign; it must not assume an acceleration magnitude. |

Every operational-control input shall carry provenance `hardware_available_estimate` or `hardware_available_measurement` and a real-hardware acquisition path.
Direct Gazebo truth and scorer output are prohibited as controller inputs.
Gazebo truth may legitimately drive a documented simulated sensor model, which then produces a hardware-representative synthetic measurement for PX4 estimation; the manifest must identify the sensor model, source truth signal, frame, noise, bias, latency, dropout behaviour, rate, calibration/validation status, and hardware counterpart.
The manifest must list all controller subscriptions and provide a negative provenance assertion that no direct truth topic or scorer output reaches operational control.
QGroundControl display values and MAVLink telemetry are observational outputs, not primary truth.

The planned pitot is a DroneCAN sensor, GPS is a sensor input, and Pixhawk sensors are onboard measurements.
The manifest shall name their physical/simulated counterpart separately and shall not represent any as direct truth.
The current stock Cessna truth-plus-estimated-attitude synthetic-airspeed path is not automatically a qualified research air-data model or evidence of control authority.

## Immutable Scenario And Requirement Registry

Before a campaign starts, the test owner shall issue a versioned scenario registry and requirement registry.
The manifest records their paths and SHA-256 hashes.
The checker shall reject results scored with a registry changed after scenario execution or with thresholds inferred from the observed result.
Changing a criterion, plant parameter, model resource, score algorithm, or scenario requires a new registry revision and a new campaign identity; prior results remain attributable only to their original revision.

### Named Requirements With No Approved Numeric Value

The following names are placeholders for human-approved values, not default constants.

| Requirement ID | Required later decision |
| --- | --- |
| `REQ_CMD_RATE_HZ`, `REQ_PAYLOAD_DEADLINE_S`, `REQ_HEARTBEAT_DEADLINE_S`, `REQ_MAX_COMMAND_AGE_S`, `REQ_MAX_JITTER_S` | Producer, translator, PX4, and control timing budget. Payload freshness is separate from Offboard heartbeat liveness. |
| `REQ_AXIS_ACCEL_MIN_MAX`, `REQ_VECTOR_ACCEL_MAX`, `REQ_ACCEL_SLEW_MAX` | Feasible requested acceleration set and rate limits for the approved envelope. |
| `REQ_TRACK_RMS_MAX`, `REQ_TRACK_PEAK_MAX`, `REQ_SETTLE_ERROR_BAND`, `REQ_SETTLE_MAX_S`, `REQ_OVERSHOOT_MAX` | Tracking criteria by axis/case, including qualification interval and overshoot applicability. |
| `REQ_SATURATION_MAX_S`, `REQ_ESTIMATOR_VALID_FRACTION_MIN`, `REQ_RUNTIME_MAX_S`, `REQ_DEADLINE_MISS_MAX` | Evidence qualification limits. |
| `REQ_LAUNCH_READY`, `REQ_MIN_CONTROL_SPEED`, `REQ_CAPTURE_ENTRY_SPEED`, `REQ_CAPTURE_SLOWDOWN`, `REQ_CAPTURE_ATTITUDE`, `REQ_CAPTURE_ABORT` | Launch, authority, net-capture, and abort criteria. No current value is implied. |
| `REQ_SENSOR_FAULT_PROFILE`, `REQ_ACTUATOR_FAULT_PROFILE`, `REQ_UNCERTAINTY_BOUNDS` | Fault injections and physically justified uncertainty distributions/ranges. |

Software fixtures may use literals only under a `fixture_*` namespace, marked `non_operational: true` and excluded from all operational pass/fail comparisons.
No `fixture_*` value may populate an `REQ_*` field or an airframe/controller parameter; the mass and thrust estimates above are not fixture values.

## Machine-Readable Evidence Contract

The canonical manifest format shall be UTF-8 JSON or YAML that validates against a versioned JSON Schema.
The schema itself, validation tool version, and scorer version shall be hashed.
Unknown schema versions and unknown required-field semantics fail validation.
The following logical shape is normative; field names can be made concrete by the later schema implementation without reducing these requirements.

```yaml
schema: {name: sitl_evidence_manifest, version: <semver>, sha256: <hash>}
run:
  id: <unique immutable id>
  campaign_id: <immutable campaign id>
  profile: integrated_sitl | hardware | software_contract
  result: pass | fail | incomplete | not_run
  started_monotonic_ns: <integer>
  ended_monotonic_ns: <integer>
  failure_reasons: [<nonempty when fail or incomplete>]
registries:
  scenario: {path: <>, sha256: <>}
  requirements: {path: <>, sha256: <>, approvals: [<id>]}
  scoring: {path: <>, sha256: <>}
identity:
  px4: {git_sha: <>, dirty: <bool>, diff_sha256: <or null>}
  gz_submodule: {git_sha: <>, dirty: <bool>, diff_sha256: <or null>}
  model_resource: {external_path_id: <non-secret logical id>, revision: <>, files: [{path: <>, sha256: <>}]}
  launcher: {path: <>, sha256: <>}
  host_guidance: {path: <>, sha256: <>}
  translator: {implementation: <>, revision: <>, configuration_sha256: <>}
  environment: {os: <>, wsl: <>, tools: {px4: <>, gz: <>, mavlink: <>}}
configuration:
  vehicle_configuration_id: <>
  mass_properties: {source: <>, sha256: <>, qualified: <bool>}
  propulsion: {source: <>, sha256: <>, qualified: <bool>}
  sensor_configuration: {source: <>, sha256: <>, qualified: <bool>}
  parameters: {path: <>, sha256: <>}
  model_lookup: {resource_paths: [<logical ids>], sha256: <>}
scenario:
  id: <registry id>
  sha256: <>
  class: nominal | boundary | invalid_command | stale_command | connection_loss | sensor_fault | actuator_fault | uncertainty
  initial_state: {position_ned_m: <>, velocity_ned_m_s: <>, attitude: <>, rates: <>}
  requested_acceleration:
    quantity: inertial_acceleration
    frame: FRD
    units: m_s2
    reference_point: CG
    equation: a_req_NED(t_eval) = R_NED_from_FRD(t_eval) * a_req_FRD_active(t_eval)
  launch_or_capture: {phase: <none | launch | capture>, requirements: [<REQ ids>]}
timing:
  clocks: [{name: <>, epoch: <>, monotonic: <bool>, synchronization: <>}]
  payload_deadline_requirement: REQ_PAYLOAD_DEADLINE_S
  command_age_requirement: REQ_MAX_COMMAND_AGE_S
signals: [<one record per required signal; see provenance record below>]
cases: [<case id, required criteria, result, reasons, metric artifact hash>]
artifacts: [<path/logical id, kind, sha256, byte_count, complete>]
processes: [<owned process identity, pid, parent, start/end, exit, cleanup result>]
review: {checker: <version/hash>, scorer: <version/hash>, reviewer: <>, notes: <>}
```

A signal record shall include at least `name`, `publisher`, `consumer`, `units`, `frame`, `reference_point`, `clock`, `timestamp_field`, `sample_timestamp_policy`, `provenance`, `hardware_counterpart`, `rate_requirement`, `permitted_nonfinite_states`, `artifact`, and `sha256`.
Command records additionally require producer monotonic creation time, sequence, validity interval, ingress receipt time, validator decision/reason, associated estimator-attitude sample identifier, and the final mode/control-stage correlation identifiers.
Logs must preserve raw records as well as derived metrics.

For a hardware run, a missing simulator-truth signal is expected, and the primary tracking reference must instead be a declared independent measurement method.
For a SITL run, truth-based scoring is allowed but must remain outside the controller process boundary.

For `profile: software_contract`, the registry shall predeclare the fields that are inapplicable, including any plant/model resource identity, Gazebo/PX4 executable identity, vehicle mass/propulsion/sensor configuration, runtime process record, ULog, and simulator signal that the test does not exercise.
The software-contract profile still requires schema/registry/scorer identity, host source identity, input/output artifact hashes, case applicability, deterministic fixture inputs, result reasons, and the evidence needed by its declared metric or contract assertion.
An exemption not predeclared in the hashed registry is a validation failure; this profile never permits an integrated-SITL or hardware claim without its normally required evidence.

## Required Scenarios And Preconditions

Each scenario must declare an initial state, command profile, duration bound, enabled components, fault injection, expected state transition, required evidence, and the predeclared criteria it invokes.
No scenario may substitute a velocity, position, attitude, or actuator request for the acceleration-only research request.

| Class | Required cases | Required outcome rule |
| --- | --- | --- |
| Nominal | A bounded acceleration-only profile inside the approved envelope; separate launch profile starts from rest, nose-up, `+X` vertical. | Incomplete until plant, control authority, launch-ready conditions, and `REQ_*` limits are approved. No added launch speed is allowed unless a separately approved scenario documents why it is required. |
| Boundary | Each approved acceleration, state, attitude, speed, and actuator boundary; include the edge of the approved launch/capture envelope. | Incomplete until the feasible envelope and limits exist. Attempting an unapproved request must be treated as invalid-command, not as a boundary pass. |
| Invalid command | Wrong frame, missing frame, non-finite acceleration, malformed metadata, forbidden velocity/attitude/force field, duplicate/replayed/out-of-order sequence, invalid validity interval, and out-of-bounds request. | PX4 ingress/validator must reject or enter the approved fallback with an attributable reason. It must not activate a new rejected demand. A previous valid demand may continue only through its original expiry or the approved loss-response policy, with that state explicitly logged; a missing decision record fails. |
| Stale command | A fresh heartbeat/liveness stream combined with a stale, repeated, delayed, or absent acceleration payload. | Payload deadline and sequence policy must independently reject it. Offboard heartbeat alone is insufficient evidence. |
| Connection loss | Producer disconnect, translator/TELEM loss, MAVLink loss in SITL, restart, and reconnect. | Show detection, sole-owner preservation, and the approved PX4 response. A stock generic fallback is not presumed feasible. |
| Sensor fault | Planned IMU/GPS/pitot faults: missing, stale, discontinuous, biased, delayed, saturated, and invalid status. | Fault profiles and response limits require `REQ_SENSOR_FAULT_PROFILE`; without them the case is incomplete, but evidence integrity checks are still executable. |
| Actuator fault | Each independent canard servo separately and the rear EDF: stuck, delayed, saturated, loss, or erroneous response as approved. | Do not infer controllability from four servos. Use `REQ_ACTUATOR_FAULT_PROFILE` and approved plant effectiveness; otherwise incomplete. |
| Parameter uncertainty | Mass/CG/inertia, aero, EDF, servo, sensor, link, wind, and initial-condition sweeps with provenance and sampled seed. | Each range/distribution must derive from `REQ_UNCERTAINTY_BOUNDS`; arbitrary sweeps are incomplete, not robustness evidence. |
| Capture | Head-first net entry and intended slowdown, including command termination and post-capture output state. | This is incomplete until capture speed, attitude, net model/measurement, slowdown, and abort requirements are approved. It must not claim a minimum control/capture speed. |

All plant-dependent cases require the external model resource and its data provenance to be identified.
They do not authorise model construction with guessed geometry, inertia, aerodynamics, servo authority, EDF dynamics, or gains.

## Metrics And Exact Scoring Rules

The scorer must evaluate only intervals declared before execution, retain the sample selection artifact, and never time-shift request and response after inspecting results.
Let `S_i` be the valid, aligned scoring samples for axis `i` in a declared frame at the CG.
Let `a_req,i,k` be the accepted request transformed using the current valid estimator attitude at the same evaluation instant as `a_obs,i,k`, the scored achieved inertial acceleration.
For SITL, `a_obs` must identify its truth source and differentiation/filtering method; for hardware it must identify the independent measurement method.
Estimator acceleration, if available, is a separate diagnostic and is never silently substituted for the scored quantity.

| Metric | Formula / required rule | Pass criterion |
| --- | --- | --- |
| Per-axis error | `e_i,k = a_obs,i,k - a_req,i,k` after the predeclared causal alignment policy. | The aligned signal provenance and sample count must validate. |
| RMS tracking error | `RMS_i = sqrt((1 / |S_i|) * sum(e_i,k^2))`. | `RMS_i <= REQ_TRACK_RMS_MAX[i, case]`. |
| Peak tracking error | `PEAK_i = max(abs(e_i,k))`. | `PEAK_i <= REQ_TRACK_PEAK_MAX[i, case]`. |
| Settling time | For a declared step at `t0`, the earliest `t_s >= t0` for which `abs(e_i,k) <= REQ_SETTLE_ERROR_BAND[i, case]` for every valid sample from `t_s` through the predeclared hold horizon or next command transition. `t_settle = t_s - t0`; no such `t_s` is a failure. | `t_settle <= REQ_SETTLE_MAX_S[i, case]`. Insufficient valid samples are incomplete. |
| Overshoot | For a nonzero declared step `Delta_i`, `OS_i = max(0, max_k(sgn(Delta_i) * (a_obs,i,k - a_req,i,final))) / abs(Delta_i)`, over the predeclared transient window. | `OS_i <= REQ_OVERSHOOT_MAX[i, case]`. A zero/small step requires predeclared `not_applicable`, never divide by zero. |
| Command age | `age_k = t_validator_accept,k - t_producer_create,k`, after a recorded cross-clock conversion. Also record transport and controller-stage ages separately. | `age_k <= REQ_MAX_COMMAND_AGE_S` for all required payloads. Unknown/cross-clock-unverifiable age is incomplete. |
| Saturation time | For each limiter, allocator, servo, and EDF indicator `q`, `T_sat,q = sum(delta_t_k for q_active,k)`. Report union and each component; do not infer saturation solely from a commanded value near a bound. | Each applicable value is within `REQ_SATURATION_MAX_S` and flags/logs are present. |
| Estimator validity | `V = valid_required_samples / eligible_samples`, using predeclared estimator flags, finite checks, covariance/health criteria, and time continuity checks. | `V >= REQ_ESTIMATOR_VALID_FRACTION_MIN`; missing eligibility/validity records are incomplete. |
| Runtime | `T_runtime_s = (ended_monotonic_ns - started_monotonic_ns) * 1e-9`; record real-time factor separately. | `T_runtime_s <= REQ_RUNTIME_MAX_S` where applicable. A forced external kill is a failed cleanup. |
| Deadline misses | The registry declares schedule horizon `[t_start, t_schedule_end]`, observation end `t_observe_end`, rate `REQ_CMD_RATE_HZ`, and deadline `D = REQ_PAYLOAD_DEADLINE_S`. It must collect through `t_observe_end >= t_schedule_end + D`; otherwise any unexpired slot makes the result incomplete. Build expected payload slots `E = {t_start + n / REQ_CMD_RATE_HZ | t_start + n / REQ_CMD_RATE_HZ <= t_schedule_end}`. Chronologically match each accepted arrival to at most one earliest unmatched slot when it arrives in `[slot, slot + D]`; `N_deadline_miss` is the count of unmatched mature slots. This includes initial and tail slots after their deadlines mature. Report interarrival gaps as a separate diagnostic and report command-age violations separately; neither is added to `N_deadline_miss`. | `N_deadline_miss` and its fraction must satisfy `REQ_DEADLINE_MISS_MAX`; a fresh heartbeat does not reset payload misses. |

The scenario registry must state whether every reported tracking axis is scored in FRD, NED, or both, and must record the transform source for every conversion.
The metrics report must distinguish unavailable direction, request rejection, estimator invalidity, actuator saturation, allocator residual, and model limitation; none may be misreported as tracking performance.

## Fail-Closed Evidence Validation Tests

The evidence checker shall have deterministic fixtures that deliberately make each condition below occur.
These are software tests and do not require vehicle geometry, a plant model, a simulator, a controller gain, or a hardware link.

| Fixture defect | Required checker result |
| --- | --- |
| Missing manifest, unsupported schema version, invalid JSON/YAML, duplicate run ID, or absent terminal state | `fail`. |
| Missing revision, dirty-diff attribution, registry hash, model-resource identity, parameter hash, process record, artifact hash, or required provenance record | `fail`. |
| Referenced file missing, changed hash, zero/truncated required log, unreadable log, or raw/derived log mismatch | `fail`. |
| Required signal absent, unit/frame/reference point missing, timestamp non-monotonic outside declared reset, unsupported non-finite sample, or incompatible clocks | `incomplete` unless it demonstrates a prohibited direct-truth-to-control path, which is `fail`. |
| Direct truth topic or scorer output listed as an operational controller input; or a synthetic sensor lacks declared model/provenance/noise/latency/hardware counterpart | `fail`. |
| Command record omits producer time, sequence, validity, validator decision, or associated attitude sample | `incomplete`; a falsely marked accepted command is `fail`. |
| Fresh heartbeat paired with stale/replayed payload but case marked pass | `fail`. |
| Threshold missing, threshold from `fixture_*`, approval/hash mismatch, or changed after execution | `incomplete`; evidence of post-result relaxation is `fail`. |
| Formula input has insufficient samples, zero-step overshoot lacks approved applicability, or metric artifact hash is absent | `incomplete`. |
| Harness attempts to remove, signal, or claim ownership of a process not recorded as started by that run | `fail` and preserve the external process. |

The test suite shall include positive fixtures as well as these negative fixtures, proving that a complete, internally consistent evidence package can validate without relying on hidden defaults.

## Bounded Execution And Retention

A future runner shall be opt-in and shall record every command used to start its processes.
It may terminate only processes whose executable identity, parent relationship, start time, and ownership token were recorded by the current run.
It shall use a per-run directory, ports, mount/cache locations, model resource path, and cleanup token; shared WSL launcher defaults are not safe for concurrent runs.
Timeout, startup failure, log flush failure, or cleanup failure must produce `fail` or `incomplete` with retained partial evidence, never deletion or a silent pass.

Retain an immutable evidence bundle containing the manifest, registries, raw producer/translator/PX4/Gazebo logs, ULog, score input/output, process lifecycle record, terminal output, parameter export, source/model identities, and SHA-256 inventory.
Retention directory, access control, duration, and any handling of serial/network captures remain project decisions, but their selected policy must be recorded before qualified runs.

## Pure Software And Interface-Contract Testing

The following focused implementation slice is unblocked by missing plant data and is the recommended next coding scope after explicit file ownership is assigned:

1. Implement a schema validator and deterministic evidence checker for the manifest, hash inventory, result-state rules, requirement-registry immutability, and malformed-evidence fixtures in a dedicated host tooling path.
2. Implement pure frame/gravity/reference-point contract tests using synthetic rotations and declared fixtures: FRD/NED signs, nose-up vertical `+X` launch orientation, `a` versus `f = a - g`, CG versus offset-IMU conversion inputs, finite checks, and clock/sequence/validity handling.
3. Implement host-guidance and translator serialization/validation tests that prove the emitted research command has only FRD inertial acceleration plus metadata, rejects forbidden velocity/attitude/specific-force fields, and records an attributable decision. These tests must not claim the stock fixed-wing path provides full-vector acceleration tracking.
4. Implement offline metric calculations and their deterministic positive/negative fixtures, with all operational `REQ_*` comparisons returning incomplete until approved values are supplied.

This slice does not need CAD, inertia, aerodynamic coefficients, actuator authority, controller gains, capture speed, model edits, a SITL runner, a build, a simulator execution, or physical operation.
It must remain separate from plant-dependent tracking results.

## Limitations And Release Conditions

This is an evidence specification, not evidence that any scenario passes.
Physical geometry/inertia/CG, aero/local flow, installed EDF and servo data, sensor installation/timing, authority/gains, command timing limits, operating envelope, fallback policy, minimum control/capture speed, capture limits, and all numerical acceptance thresholds remain unresolved.
The estimated `2.5 kg` and `5.5 kgf` values do not close any of those gaps.

The current stock PX4 fixed-wing path's receipt of acceleration fields is not proof of a research acceleration-only consumer or of full-vector tracking.
No result may use the planned MAVLink SITL path or TELEM translator as proof of the eventual hardware contract until end-to-end freshness, ordering, validation, ownership, and loss behaviour are implemented and evidenced.

This specification unlocks fail-closed evidence tooling and pure software/interface fixtures only.
It does not release a custom plant, controller/allocation, companion deployment, integrated SITL, Pixhawk build, net capture test, or physical operation.
