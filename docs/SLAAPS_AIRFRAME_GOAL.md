# SLAAPS airframe and flight-mode integration goal

Updated 2026-10-23, Codex. The objective remains unfinished. Shared configuration,
manual activation/worker/mixer handling, deliberate armed EDF commissioning and
scheduled PID/effectiveness are implemented with synthetic software evidence.
Shared selected-power health and original-report expiry now gate Manual/EDF
readiness and permission; voltage limits and physical feed assignment are explicit.
Authenticated companion receipt now passes actual-parser/native integration;
command admission, acceleration control, manual flight admission and physical
qualification remain incomplete. See [current handoff](PROJECT_CONTEXT.md) and
[integration evidence](SLAAPS_CONFIGURATION_INTEGRATION.md) for exact boundaries.

## User direction

Create a SLAAPS airframe and startup behavior that runs the relevant modules.
Provide end-user QGroundControl flight-mode selection for stabilized manual
control, companion acceleration control, and bench commissioning using
the existing bench module. Physical output channels must be user-configurable
in QGC. Define EDF command and ownership. Vehicle-specific control data will
follow; do not request it again or substitute synthetic fixture values.
Follow-up direction: vehicle data must also be easily configurable and
changeable within the QGC GUI.

This direction settles the previous scope question: develop the flight-capable
implementation with propulsion ownership. The October 22 instruction expands
bench scope to deliberate armed EDF commissioning as well as disarmed servo work. It does not establish measured flight readiness.
The current request authorizes implementation and software verification, not
flashing or physical operation.

## Parameter-driven integration direction

The user has authorized continuing all software integration for the SLAAPS
vehicle, with a future QGC workflow for geometry, mass, physical constraints and
wiring assignments. Preserve the shared typed configuration and explicit apply
transaction as that workflow's boundary; do not add a second runtime profile
source that can disagree with QGC parameters.

The present 299 vehicle fields include mass, CG, inertia, measured canard moment
derivatives, calibration, controller limits and the EDF curve. They do not yet
include a geometry-to-effectiveness derivation or automatic controller tuning.
A future geometry model must specify body-FRD positions relative to a declared
origin/CG, surface orientation and hinge signs, aerodynamic reference area,
measured force derivatives and the airflow envelope. Derivation must produce the
same validated allocation inputs used by the controller and expose diagnostics
when rank, saturation or uncertainty constraints fail. Wiring remains logical
output-function assignment with actual driver-inventory validation.

Geometry and mass alone cannot identify servo dynamics, EDF response, local wash
or aerodynamic force coefficients. Automated calibration requiring actuator
motion is a separate commissioning procedure, not a side effect of editing or
applying parameters. Derived gains/models require bounded design criteria and
software/physical qualification; no implicit retuning while armed is permitted.

## Intended user behavior

| Mode | Input and behavior | EDF |
| --- | --- | --- |
| SLAAPS Manual | Selected PX4 manual input; body-rate commands with attitude hold on stick release; existing controller and four-canard allocator | Selected manual throttle through the common propulsion owner, subject to armed admission |
| SLAAPS Acceleration | Reserved interface for the existing body-FRD inertial-acceleration-at-CG contract; unavailable for activation until a validated producer and controller are integrated | Future acceleration controller requests thrust through the same propulsion owner |
| SLAAPS Bench / Commissioning | Shared logical routing and calibration; servo-only work may use disarmed/prearmed admission; EDF work requires deliberate visibly armed commissioning or an explicit actuator-test mechanism | Bounded enable/duration, stop/kill and input-loss handling; never silent operation while disarmed |

Mode selection is a request, not permission to bypass profile, health, arming,
input, routing or output checks. Flight modes without required vehicle data
must report an actionable unavailable reason. Bench must reject entry from active flight and must never be an airborne
fallback. Ordinary disarmed state inhibits EDF output. EDF commissioning is a
separate deliberate armed/test state, not ordinary flight arming.

Use named mode discovery through PX4/MAVLink AVAILABLE_MODES and CURRENT_MODE,
with ordinary QGC mode requests. Check the actual QGC build during acceptance.
Do not require ROS 2 or companion availability to select the onboard manual
and bench modes. Inspect the existing mode-registration mechanism versus
native mode integration before choosing the implementation; registration alone
does not establish the correct control flags, requirements or failsafe policy.

## Airframe and startup

Add a unique SLAAPS SYS_AUTOSTART definition and ROMFS registration with
metadata and versioned defaults. Preserve saved user output assignments on
ordinary boots. Define the SLAAPS startup path explicitly instead of inheriting
stock fixed-wing controllers and allocator ownership accidentally.

Retain the board startup services needed for sensors, estimation, commander,
manual-input selection, MAVLink/QGC, logging, power monitoring and outputs.
Add the SLAAPS mode/ownership supervisor and the mode-specific control workers.
Start workers disabled or waiting for valid configuration; boot and mode
registration must not enable actuators. Integrate the existing bench module
with mode entry/exit instead of requiring a separate console enable workflow.
Unavailable acceleration logic must have a documented interface and status,
not a synthetic executable flight implementation.

Define land detection and system vehicle classification intentionally for the
wingless, fixed-EDF vehicle; do not assume stock plane landing or position
control is a valid recovery mechanism. Determine which ordinary modes are
supported, hidden or rejected, and ensure failsafe decisions cannot select an
unimplemented stock controller.

Preserve the currently working e8ea98b404/NuttX 11 hardware baseline and its
staged bench image. Check compatibility of the chosen mode API on that baseline
separately from the newer active checkout. Build a separate SLAAPS target;
do not overwrite retained stock, bench or observer artifacts.

## Configurable actuator routing

Expose stable logical functions for SLAAPS Canard 1 through 4 and SLAAPS EDF.
The physical MAIN/AUX channel selection belongs to QGC output-function
parameters, not hard-coded channel indices. All modes use the same canard
identities so mode changes do not require rewiring or reassignment.

Validate the actual mapping before admission: every required function has
exactly one supported output, no duplicate ownership, compatible output
protocol/rate, and valid calibration and disarmed/failsafe values. Keep
calibration associated with the logical canard and validate its conversion at
the configured physical channel. Changes while active revoke ownership and
require deliberate re-entry. Unsupported banks/protocols must be reported
explicitly rather than silently accepting an assignment.

Existing limitations to remove or isolate: ResearchBenchControl requires
MAIN1-4 and disables other pins; the prepared stabilized output adapter is
MAIN-specific. Neither currently satisfies arbitrary QGC channel assignment.
Do not weaken those old paths globally to implement the new airframe.

## EDF command and ownership contract

One SLAAPS actuator supervisor owns the final propulsion command. Manual and
future acceleration modes supply requests, never independently publish to the
physical EDF output. Boot, unconfigured, ordinary disarmed and revoked states cannot acquire
propulsion ownership. A future explicit EDF commissioning state must use this
same owner with bounded commands and visibly indicated armed/test permission.

The internal propulsion request must identify the selected mode/session,
source, timestamp/deadline and command units. Manual throttle enters as a
bounded normalized request using the selected input's documented range and
neutral/low-throttle admission. The future acceleration controller supplies
physical thrust demand in newtons; a measured EDF/battery model converts that
to the actuator command. Do not infer that model from the synthetic 10 N
fixture or unmeasured nominal thrust.

The owner checks commander armed state, selected mode, input freshness,
configuration identity, health, kill/lockdown/termination, limits and slew
before granting or refreshing output. Mode handoff must invalidate the old
source and establish the new one without overlapping writers or accepting a
stale command. Define command-loss and armed-flight fallback explicitly;
do not assume either holding thrust or immediately cutting thrust is a
universally safe airborne policy. Unimplemented policies prevent flight
admission. The former blanket bench EDF prohibition is superseded by the October 22
commissioning requirement; the existing implementation still inhibits it until
that distinct permission and bounded operational path are implemented.

Independent driver-side expiry and revoke handling remain required. A
software acknowledgment is not proof of electrical output or motor response.
Test stalled producer and consumer scheduling separately.

## Development ports

Keep mode request/health/profile/output ownership separate from controller
algorithms. The stabilized manual adapter should reuse the existing core.
The future acceleration adapter must retain body FRD, inertial acceleration
at the CG in m/s^2, body-fixed behavior between updates, and the existing
source/session/expiry and command-loss requirements. It must not reinterpret
that vector as specific force or direct servo commands.

Vehicle profile loading must accommodate forthcoming measured mass, CG,
inertia, canard effectiveness/trim/limits/latency and EDF/airflow data.
Absent or invalid profiles yield a specific unavailable state. A placeholder
parameter or profile filename must never act as evidence of calibration.

## QGC-editable vehicle data

Make the supported vehicle model configurable through QGC's parameter GUI,
with clear groups, names, descriptions, units, bounds and enum choices.
Routine configuration must not require source edits, recompilation, shell
commands or hand-edited JSON. Cover mass, CG, the independent entries of the
inertia tensor, four logically identified canards and their effectiveness,
trim, calibration, travel/rate/latency limits, controller gains and limits,
and the supported EDF thrust/command and airflow model.

Use native PX4 parameter metadata and QGC parameter save/load workflows
where possible. QGC's standard parameter editor is the baseline GUI;
a dedicated graphical vehicle editor would require a separate QGC change
and must not be implied by firmware metadata alone. Bound any tabular model
representation to what the actual parameter GUI can edit meaningfully;
document unsupported model complexity instead of silently dropping data.

Define one typed configuration adapter shared by the control and output
paths. Validate cross-field constraints, finite values, positive-definite
inertia, calibration ordering, effectiveness/authority requirements and
model completeness. Separate editable values from measured provenance and
acceptance: saving a value does not qualify it for flight.

At admission, capture an immutable validated configuration and its identity.
Prevent a mode from using a partially edited set or a mixture of old and new
values. Detect changes while active, inhibit new admission and apply the
defined mode-specific controlled-disable/re-entry policy before using them;
do not retune a running controller implicitly. Exact armed-flight handling
belongs to the flight transition/failsafe implementation, not a blanket
instruction to cut propulsion.

Verify QGC-visible metadata, persistence across reboot, parameter export and
restore, invalid and incomplete configurations, edits during activation and
operation, and successful re-entry after a complete valid configuration.

## Milestones and completion criteria

1. Trace startup, commander/mode requirements, MAVLink mode discovery and
   output-function metadata on the active and working hardware baselines.
   Record the architecture choice and exact module list.
2. Add the airframe, build variant and startup wiring. Verify reset versus
   ordinary boot behavior, parameter persistence and disabled startup.
3. Implement named mode selection and lifecycle, bench module integration,
   inactive acceleration/manual development seams as needed, and explicit
   unavailable reasons. Validate selection, rejection, exit and re-entry.
4. Implement configurable logical actuator routing and the common EDF
   ownership boundary. Test remapping, duplicates, invalid calibration,
   stale inputs, mode changes, arming conflicts and command expiry.
   Implement and verify the QGC-editable vehicle-data interface, consistent
   configuration snapshots and controlled application of changed values.
5. Build and verify the complete target and relevant native/SITL integration.
   Measure linked resources; preserve reproducible evidence. Exercise QGC
   discovery and selection using the intended QGC version.
6. Update project context and code graph with implemented capabilities,
   remaining profile/physical gates and a concise operator procedure.

The software goal can finish with acceleration as an explicit unavailable
development port and flight activation gated on missing measured data.
It cannot finish merely by adding three mode names or an airframe script:
startup, ownership, configurable routing and the servo-bench lifecycle must
be implemented and verified. Actual flight qualification is a later milestone.

## Initial intake evidence

No SLAAPS airframe script was found in the current ROMFS. The existing
boards/px4/fmu-v6x/slaaps-research.px4board enables an output-disabled observer
and explicitly disables the stabilized output hook. The hardware observer
cannot currently activate flight control. Existing request-owner/native/SITL
work supplies reusable pieces, not a completed production owner.

Relevant sources:

- ROMFS/px4fmu_common/init.d/rcS, rc.vehicle_setup, rc.fw_apps and airframes/.
- src/modules/commander/ModeManagement.*, ModeUtil/ and HealthAndArmingChecks/.
- src/modules/mavlink/streams/AVAILABLE_MODES.hpp.
- src/modules/research_bench_control/ResearchBenchControl.cpp.
- src/modules/research_stabilized_manual/ and src/lib/research_stabilized_manual/.
- src/lib/mixer_module/output_functions.yaml and research output adapters.
- docs/STABILIZED_MANUAL_PRODUCTION_OWNERSHIP.md.
- docs/STABILIZED_MANUAL_SM5_ALLOCATION_INPUT_CONTRACT.md.

Checked graph fingerprints for the production ownership record, research
board variant, controller core, SITL owner and integrated qualification record
match the working tree at intake. This is a goal-definition milestone only;
no firmware, startup behavior or output assignment changed.

## Current implementation checkpoint

[Configuration and routing increment](SLAAPS_CONFIGURATION_INTEGRATION.md):
173 QGC parameter definitions pass the actual PX4 metadata toolchain;
typed profile adapters, immutable configuration lifecycle, EDF curve conversion
and configurable logical routing pass 1,347 checks each in Debug, Release and
ASan/UBSan. Logical output functions 10100..10104 are reserved but have no
driver provider yet. The library remains excluded from default firmware.
No airframe startup or native flight mode is implemented yet. Next implement
the synchronized native parameter/apply path and SLAAPS supervisor, then
startup, mode lifecycle and driver/EDF ownership. Goal remains active.

## October 22 completion requirements

Operating mode and arming state are separate. Complete manual control through
Commander, supervisor, selected input/state, controller/allocation, common EDF
ownership and configured MAIN/AUX driver handling first. Separate prearm
preparation from armed output permission; cover first command, cancellation,
partial activation, expiry, exit and deliberate re-entry. Registered names and
diagnostic reports do not complete a mode.

Commissioning must persist the same typed configuration consumed by flight,
with explicit apply/save, version/provenance and incomplete/calibrated/qualified
status. Stock QGC native parameters are the baseline GUI; custom graphical
editors require a separately identified QGC repository change. No hardware
operation is authorized by software implementation work.

Implement scheduled PID gains and matching allocation effectiveness over a
declared calibrated envelope: QGC-editable breakpoints/model data, bounded
interpolation, filtered scheduling, bumpless integral/derivative handling and
anti-windup. Vehicle airspeed and local canard airflow/EDF wash are distinct;
ground speed is not a substitute. Stale scheduling data and out-of-envelope
conditions require explicit behavior, without invented measured coefficients.

Companion acceleration remains body-FRD inertial acceleration at CG in m/s^2,
body-fixed between updates. The fixed EDF/canard force constraints and required
attitude/yaw command must make the controller well-posed. Implement source/time/
sequence/freshness limits and exclusive manual/acceleration handoff; do not
assume independent control of arbitrary forces. Recovery policy and actual
vehicle measurements remain pending; continue independent software integration.
