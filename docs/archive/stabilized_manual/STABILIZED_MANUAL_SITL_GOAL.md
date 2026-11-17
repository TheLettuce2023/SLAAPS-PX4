# Stabilized manual SLAAPS goal: SM0 scope and inputs

2026-10-05, Codex; updated 2026-10-06. Current isolated entry point:
[qualified revision-aware manual console](STABILIZED_MANUAL_ISOLATED_MODE.md),
with 31 declared cases passing and actual operator controls verified.
The prior [31 corrected-profile native cases](STABILIZED_MANUAL_SM4_RUNTIME_PROFILE_FIX.md)
remain preserved, with actual parameter comparisons before enable. Historical matched-prior claims
are corrected after a startup-script construction bug. Broader robustness and
vehicle-supported work remain open; the goal is blocked on missing vehicle inputs.
[A private publisher-revision variant](STABILIZED_MANUAL_SM4_PUBLISHER_REVISION_VARIANT.md)
now passes fourteen native uORB checks and eleven synthetic SITL cases,
including hidden advertisement-cycle rejection, frozen-clock revocation and
two mixed-axis control regressions. It adds atomic payload/revision binding in
a separate POSIX build. [Eight actual EKF2 lifecycle/reset/profile cases](STABILIZED_MANUAL_SM4_PUBLISHER_LIFECYCLE.md)
now pass on that exact binary. Its full declared campaign and live console also
pass. General publisher identity, restarts entirely between owner polls,
vehicle-supported inputs remain open. The
[fresh checkout workflow](STABILIZED_MANUAL_CHECKOUT_QUALIFICATION.md) now passes
all 31 cases freshly over 68,813 cycles and actual console controls; its pair is
the current default. Source-only build/qualification packaging is complete at
this declared synthetic fixture scope.
[A separate generated synthetic allocation SITL build](STABILIZED_MANUAL_SM5_GENERATED_SITL.md)
passes two exploratory mixed-axis cases over 12,953 cycles with the full
source-profile hash observed at runtime; it does not expand the accepted
31-case campaign or qualify SLAAPS vehicle allocation.
[Its follow-up admission guard](STABILIZED_MANUAL_SM5_ADMISSION_GUARD.md)
rejects a changed report matrix with an unchanged claimed input hash and
emits byte-identical C++ for the sealed synthetic fixture.
[A private C++ binding probe](STABILIZED_MANUAL_SM5_BINDING_PROBE.md) confirms
that changed copied header values change the configuration and allocator
commands; it does not add an admitted profile or SITL maneuver.
[An offline uncertainty rank screen](STABILIZED_MANUAL_SM5_INTERVAL_RANK.md)
now supplies a sufficient one-condition full-row-rank test for reported
elementwise intervals; it does not qualify usable moments or flight.
[A static incremental moment-box screen](STABILIZED_MANUAL_SM5_STATIC_AUTHORITY.md)
adds a declared synthetic demand and canard command margins. Net trim,
transient authority and vehicle-supported requirements remain open.
[A static net-moment screen](STABILIZED_MANUAL_SM5_NET_AUTHORITY.md)
now accounts for source-bound bias at trim, uncertainty and demanded physical
net moment. The fixture's zero-bias example passes; measured SLAAPS bias and
reviewed net demand remain unavailable.
[A compiled allocator finite-grid diagnostic](STABILIZED_MANUAL_SM5_ALLOCATOR_GRID.md)
checks the implemented C++ allocation at 1 s and 10 ms from trim over the
synthetic box. Slew limits visibly restrict one-step tracking; vehicle inputs
and closed-loop qualification remain pending.
[A sequential allocator diagnostic](STABILIZED_MANUAL_SM5_ALLOCATOR_SEQUENCE.md)
checks eight synthetic corner hold/reversal/release histories with preceding
commands and 10 ms slew. It does not include actuator lag or expand native
SITL and vehicle acceptance.
[An audit of the sealed generated-profile SITL traces](STABILIZED_MANUAL_SM5_NATIVE_ACTUATOR_LAG.md)
confirms timestamped PX4 canard commands reach the fixture targets and its
logged surfaces follow the declared synthetic 50 ms lag. Loaded physical
servo behavior and vehicle response remain unknown.
[A separate constant-bias fixture variant](STABILIZED_MANUAL_SM5_STEADY_BIAS_SITL.md)
passes two native mixed-axis cases with fixed synthetic torque while simulated
thrust is on, without changing the accepted PX4 binary or 31-case campaign.
This does not substitute for measured EDF/aerodynamic trim bias.
[Five subsequent noise and gyro-bias probes](STABILIZED_MANUAL_SM4_ROBUSTNESS_PROBES.md)
pass at their exact synthetic inputs but do not expand the accepted campaign.
[Four unequal-axis/reversal probes](STABILIZED_MANUAL_SM4_MIXED_AXES.md) likewise
pass at their exact level/nose-up inputs with PX4 estimates.
[Three live-clock manual-link cadence probes](STABILIZED_MANUAL_SM4_CADENCE_EPOCH.md)
pass at 60/80 ms send periods, while a separate paused-physics publisher-epoch
attempt never completed a restart. Both remain outside the accepted 31 cases.
[Six magnetic-transport-delay probes](STABILIZED_MANUAL_SM4_MAGNETIC_DELAY.md)
pass at exact measured packet delays; the two level 40 ms outcomes differ
substantially, so no robust delay margin follows.
[Seven raw-IMU transport cases and a 100 ms negative boundary](STABILIZED_MANUAL_SM4_IMU_DELAY.md)
show exact selected-state timing and rejection before output at the freshness
limit. These remain exploratory outside the accepted 31-case checkpoint.
[Four simultaneous pilot/IMU timing cases](STABILIZED_MANUAL_SM4_COMBINED_TIMING.md)
pass with 80 ms pilot cadence and 80 ms IMU delay in both poses and orders;
they do not define a jitter allowance.
[Eight seed 43/44 combined-timing repeats](STABILIZED_MANUAL_SM4_COMBINED_REPEATS.md)
also pass, bringing this exact setting to 12 finite cases/77,625 cycles across
three seeds. Wall-clock sender phase was uncontrolled, so timing jitter and
statistical reliability remain open. The accepted checkpoint stays 31 cases.
[Two combined-timing manual-loss cases](STABILIZED_MANUAL_SM4_COMBINED_LOSS.md)
also pass at level and body-X-up. Both latch manual fault 5, reject re-enable
in the faulted session, turn off the consumer 108 ms after the last manual
sample, and regain hold only after an explicit new session. These exact
observations do not define a jitter or vehicle reaction-time guarantee.
[Four phase-controlled pilot-send cases](STABILIZED_MANUAL_SM4_PHASED_JITTER.md)
also pass at observed 76/84 ms intervals and exact 80 ms IMU packet delay.
One nose-up case nevertheless reaches the exact 100 ms selected-rate boundary
for four cycles, so no state-age margin follows. The failed first attempt,
which used a 100 ms-flushed clock and latched manual fault, is retained.
[Four controlled single-packet omissions](STABILIZED_MANUAL_SM4_PHASED_SKIP.md)
pass across level/body-X-up and hold/rate. Each 160 ms send gap causes manual
fault 5 and consumer shutdown 108 ms after the last manual sample. Resumed
centered packets do not clear the latch; explicit new sessions restore hold.
The first recovery attempt, which enabled before selected input centered,
failed `NotCentered` and remains preserved.
[Read-only boundary diagnosis](STABILIZED_MANUAL_SM4_RATE_BOUNDARY.md) finds
the one 100 ms selected-rate event: a 20 ms angular-rate publication gap while
five raw IMU packets were relayed and selected attitude advanced. The current
logs cannot locate the gap within the stock bridge/gyro/rate path.
[Three stock-ULog repeats](STABILIZED_MANUAL_SM4_RATE_TRACE.md) pass 19,423
cycles at the same positive nose-up setting with rate age at most 84 ms;
the stall did not recur. The ULog omits hundreds of owner-selected rate sample
timestamps per case despite zero reported dropouts.
[A separate direct gyro-epoch probe](STABILIZED_MANUAL_SM4_GYRO_EPOCH.md)
then corrected a queued-subscription capture error and passed 6,478 active
cycles with both gyro and selected-rate ages at most 84 ms. This method can
narrow a future recurrence; the original cause is still unresolved. Further
private bridge/rate timing work needs no new human input.
User direction: progress from direct manual servo control
to a stabilized manual mode as the new goal. Initial SM0 decisions are preserved
below. [SM2 and host SM3 verification](STABILIZED_MANUAL_SM2_SM3_VERIFICATION.md)
now records the implemented output-disabled control/allocation core and synthetic
rotational feedback tests. [SM3 estimated-state integration smoke](STABILIZED_MANUAL_SM3_SITL_INTEGRATION.md)
now passes in a private PX4/Gazebo fixture. The [SM4 declared campaign checkpoint](STABILIZED_MANUAL_SM4_CAMPAIGNS.md)
passes twelve estimated-state rate/release/impulse cases and six fault/recovery
cases. [Eight actual native state-barrier cases](STABILIZED_MANUAL_SM4_STATE_BARRIERS.md)
also pass at level/body-X-up, including reset, withdrawal, rapid restart and
unsupported-profile rejection. General publisher/candidate identity, broader
robustness and vehicle-supported work remain open. The user paused and resumed
this goal; it is now blocked after three consecutive input-dependency audits. The [earlier coupled-control checkpoint](STABILIZED_MANUAL_SM4_COUPLED_CONTROL.md)
is retained **incomplete**: that synthetic candidate passes three of four full-axis
rate/reversal/hold cases but fails physical hold at body-X-up/- (0.026301 rad
against 0.02). The corrected 31-case checkpoint above establishes a new synthetic
scope and preserves that failed evidence; historical prior claims are corrected.

## Objective and current decisions

Develop an isolated SLAAPS SITL mode in which pilot commands become attitude
or angular-rate objectives, and feedback from PX4 attitude/rate estimates drives
the four canards. Preserve the stock Cessna baseline and H10 bench evidence.
The new manual mode is distinct from the companion acceleration protocol;
C1E-N1 remains negative-only and is not promoted by manual-mode work.

The user confirmed these three first-demonstration decisions on 2026-10-05:

| Decision | Confirmed first demonstration | Status |
| --- | --- | --- |
| Stick/center semantics | Body-rate commands with quaternion attitude hold on release | Confirmed |
| Initial scenario | Synthetic rotational fixture at a declared airflow condition | Confirmed |
| Thrust | Fixed simulated EDF thrust | Confirmed; synthetic fixture declares 10 N along body +X at CG |

Implemented core release detail: capture the current valid supplied full attitude on
entry to centered-stick hold, then retain that reference for disturbance
rejection. Host tests cover the transition from body-rate commands, all-axis
centering/hysteresis and estimator-reset rejection; the PX4 estimated-state
adapter now runs in the private SITL fixture. Body-axis labels and signs remain defined
at body +X-up attitudes. A fixture can verify three-axis feedback only if its
mechanical degrees of freedom and configured effectiveness provide those axes.
An all-axis immobile model cannot demonstrate stabilization.

Synthetic model constants, gains, freshness limits and test thresholds will be
explicit versioned fixture inputs. They are not physical vehicle defaults.
Existing 20 ms / 500 ms / +/-13 degree bench candidates do not define the
stabilized mode's estimator, controller or actuator timing requirements.

## Proposed feedback architecture

Stock selected manual input -> manual command/reference generator -> quaternion
attitude feedback -> body-rate setpoint -> rate PID -> normalized moment demand
-> constrained four-canard allocation -> simulated servo dynamics/joints ->
simulated sensors -> PX4 estimated attitude and body rates.

The manual reference and control path must have one explicit owner. Stock
fixed-wing/multirotor publishers must not simultaneously own its setpoints,
torque or actuators. Mode entry, exit, arming permissions, estimator resets,
input loss and re-entry require an explicit supervisor and recorded transitions.
The stabilized owner must not run concurrently with the direct bench owner.
Simulator truth is reserved for scoring and controlled disturbance injection;
using truth in an early host mathematical fixture is labelled separately and
does not satisfy the estimated-state SITL checkpoint.

Reuse candidates and constraints:

- `src/lib/rate_control/rate_control.hpp/.cpp`: three-axis PID with normalized
  torque outputs, integrator reset and directional saturation inputs. Caller
  gains, moment normalization, estimator timing and coupled allocation feedback
  remain required; the update result is not an automatic [-1,1] clamp.
- PX4 matrix/quaternion utilities: full-attitude reference/error representation
  without an Euler heading singularity at nose-up attitudes. Shortest-rotation,
  near-180-degree errors, q/-q equivalence and reference/reset continuity need tests.
- `src/modules/fw_att_control/FixedwingAttitudeControl.hpp`: its
  `computeAttitudeError` removes yaw; it is not a ready-made all-axis controller.
- `src/modules/mc_att_control/AttitudeControl/AttitudeControl.cpp`: its world-yaw
  feed-forward and body-Z attitude prioritization require review before reuse
  for a body-X propulsion vehicle. Selecting it unchanged is not the plan.
- H2/H3/H4 research foundations evaluate effectiveness/residuals and structural
  authority; they do not solve allocation. The separate SM2 manual core now has
  a bounded four-canard weighted least-squares solver and directional residual
  feedback, verified against synthetic host inputs only.
- H8 MAIN1-4 routing and calibrated PWM semantics differ from Gazebo's
  `SIM_GZ_SV` outputs. Define an explicit simulation sink and angle mapping;
  preserve hardware routing restrictions instead of treating parameters as
  permission to bypass them. Fault output policy is scenario-specific; neutral
  servos are not an established flight recovery behavior.

## Required inputs and their timing

User update on 2026-10-05: **measurements not yet available**. Canard
geometry/hinge/effectiveness measurements, loaded servo calibration/dynamics and
EDF thrust/lag/wash data remain pending alongside the already-pending CAD.
No further human input is needed for the next synthetic estimator/timing
diagnostics; vehicle-specific allocation and tuning await these measurements.
The [SM5 one-condition allocation-input contract](STABILIZED_MANUAL_SM5_ALLOCATION_INPUT_CONTRACT.md)
now specifies exact body-FRD moment derivatives, signed loaded-servo mapping,
CG inertia, airflow/EDF/wash condition and source records. Its offline
compiler reconstructs the synthetic fixture matrix, but no SLAAPS measured
profile has been supplied or admitted to PX4. The generated-profile
integration now runs only this sealed synthetic example in a private SITL
build. The actual vehicle inputs remain the human gate; bounded synthetic
control and estimator work can continue without them.

| Input | Needed for | Can software proceed without it? |
| --- | --- | --- |
| Body-rate commands, attitude hold on release, synthetic rotational fixture and fixed simulated EDF thrust | Reference generator, fixture and behavioral tests | Confirmed, implemented and verified for the declared fixture |
| Stabilized axes and fixture degrees of freedom | Plant fixture, reference/envelope constraints and scoring | Three body axes verified with explicitly synthetic full-rank moment authority |
| Numerical reference/rate/error limits and disturbance/settling targets | A measurable SITL acceptance contract | Explicit fixture values qualified; physical limits require human requirements and evidence |
| Original CAD mass, CG and full inertia with axes/units/component scope | Vehicle-specific rotational/translational plant | Independent software and a separately labelled synthetic plant can proceed; CAD is already pending |
| Canard geometry, hinge/sign conventions, force/moment effectiveness versus airflow, trim and damping | Vehicle-supported allocation, tuning and minimum controllable airspeed | Synthetic effectiveness can test algorithms; no physical authority claim follows |
| Loaded servo calibration, bounds, dynamics and latency | Hardware-representative limits and servo model | Use explicit synthetic limits for software tests until installation evidence exists |
| EDF thrust/lag, battery dependence and wash reaching the canards | Any propelled or from-rest scenario | First fixture uses a declared constant simulated thrust and independent prescribed airflow; thrust must not silently generate canard wash |
| State validity, sensor mounting/latency and estimated-rate signal definition | State admission and hardware robustness | Synthetic invalid/stale/reset cases can be tested; deployment limits remain open |
| Source/link timing, mode entry/exit and loss response | Operator/source supervision and eventual flight use | Deterministic synthetic failure cases can proceed; flight recovery cannot be guessed |

NACA 0012 and the four independent X-canards are already confirmed. Those facts
do not supply aerodynamic coefficients, moment authority or a calibrated mixer.
The original intake inertia is invalid and its 3.291337 kg total conflicts with
provisional 3.041337 kg all-up mass. Preserve the supplied model and use a separate
synthetic fixture rather than invent a CAD correction.

## Ordered software milestones

The first independent implementation contract should specify:

- Roll, pitch and yaw sticks map to body FRD angular rates in rad/s. Throttle
  does not alter the fixed simulated EDF thrust in the chosen demonstration.
  Source identity and freshness are checked on PX4's selected input.
- While any attitude stick is outside the configured centering hysteresis,
  use limited body-rate commands. On entry to all-axis centered hold, capture
  one fresh estimated quaternion and freeze it; the attitude loop then generates
  bounded rate setpoints. A controlled disturbance must not recapture the hold
  reference each cycle. Transition smoothing and capture timing need tests.
- Treat estimator reference resets explicitly. Apply a validated reset delta to
  the hold reference or revoke; never continue across an unexplained reset or
  mix attitude and rate observations with an unbounded time skew.
- Frame/sign tests include body +X-up, q/-q equivalence, all three axes, near
  180-degree errors and invalid/stale/future/reset state. Heading is not an Euler
  yaw target near vertical; rotation about each body axis remains explicit.
- The controller returns finite bounded normalized moments; units and positive
  signs agree with the configured canard effectiveness. Allocation enforces
  magnitude/rate limits and reports achieved moments, residual and directional
  saturation back to the rate integrator. A rank diagnostic alone is not a solver.
- Fixed thrust acts along body +X with an explicitly configured line of action
  and fixture constraint. Prescribed airflow, canard effectiveness, damping and
  rotor reaction torque are separate declared synthetic inputs. Fixing thrust
  does not establish canard wash, static-launch authority or free-flight trim.
- The simulation profile contains complete inertia, actuator bounds/dynamics,
  moment normalization/effectiveness, controller gains, sample/loop timing,
  state/manual freshness, source identity, fixed thrust and quantitative scoring
  thresholds. Reject missing/nonfinite values instead of supplying vehicle defaults.
- Logs bind commands/reference, attitude/rates, requested/achieved moments,
  actual simulated surface angles, integrator/saturation and supervisor state
  to exact profile/source identity. Report both successful and failed scenarios.

1. SM0 (this record): establish objective, architecture candidates, decision
   questions and input dependencies. No stabilized code or acceptance asserted.
2. SM1: retain the confirmed pilot/scenario/thrust semantics; write the finite reference, state,
   timing, supervisor, normalization and synthetic acceptance contracts. Review
   the allocation authority of the chosen fixture before tuning.
3. SM2: implement an output-disabled single-owner reference/attitude/rate core
   and allocation seam. Verify rotations, controller reset, saturation/windup,
   stale/future/invalid input and mode/reset/source transitions with host tests.
4. SM3: close a synthetic rotational feedback loop with actuator dynamics,
   bounded allocation and disturbances. Bind exact parameters and quantitative
   results to source. Separate all-axis tests from deliberately weak/failed axes.
5. SM4: integrate selected manual input and real PX4 estimated attitude/rates
   into an isolated Gazebo SITL target and output adapter. Demonstrate commands,
   disturbance rejection, loss, estimator reset, stop/restart and source changes;
   log references, estimates, actual angles, residuals and saturation.
6. SM5: replace fixture assumptions with accepted vehicle plant data; validate
   trim, finite-airflow authority, gain scheduling and flight requirements before
   vehicle-supported free-flight or launch claims. Hardware acceptance is separate.

The [current completion audit](STABILIZED_MANUAL_COMPLETION_AUDIT.md) maps
these requirements to actual code and records. SM0-SM4 have finite synthetic
evidence; SM5 remains unachieved. The known input location has no measured
profile, and a fresh structural intake still rejects the supplied vehicle
inertia. The qualified console remains usable while accepted vehicle data and
operating requirements are pending.

Closed software work can progress without further CAD requests. Changes to the
confirmed pilot behavior or simulation purpose require an explicit decision.
Hardware flashing, powered tests and flight
remain subject to their separate physical acceptance and authorization records.

## Resume

Read `AGENTS.md`, `docs/PROJECT_CONTEXT.md`, this record and only the matching
research graph anchors, then the runtime-profile correction/requalification record.
Current run instructions and the qualified revision-aware console are in
`docs/STABILIZED_MANUAL_ISOLATED_MODE.md`; all 31 declared cases and the actual
operator-console check pass. The fresh checkout-built pair is now the default,
with 31 fresh cases and corrected transition-row console capture verification.
Next is accepted measured SM5 input and vehicle-supported allocation/tuning;
general publisher identity and production flight-health policy remain open.
The three initial choices are confirmed; the declared 18-case estimated-state checkpoint
passes, followed by the eight native state-barrier cases on their earlier
artifacts; matched-prior claims for those earlier scripts are withdrawn.
The current corrected synthetic profile passes 31 accepted native cases,
including full-axis nose-up hold and deterministic duplicate dispatch. Five
separate noise/gyro-bias probes pass at their exact inputs with little final
physical-error margin. Four unequal-axis pilot/reversal cases and three
60/80 ms manual-link cadence cases also pass at their exact inputs. Six
magnetic-delay cases pass with pronounced level 40 ms repeat spread. Seven
raw-IMU-delay cases also pass; an actual 100 ms delay is rejected before
control publication. Four combined 80 ms manual/IMU cases pass at their exact
inputs. Continue timing-phase repeatability, packet jitter/loss and general
publisher-epoch/candidate identity and robustness work while preserving all
earlier passing, incomplete and exploratory records. Vehicle-specific
plant/tuning inputs remain open. Preserve H10, the stock Cessna baseline, original CAD/model inputs and
the separate negative-only acceleration runtime.
