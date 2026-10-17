# Stabilized manual SM2 and host SM3 verification

2026-10-05, Codex. Implements the confirmed body-rate / attitude-hold behavior
from [the goal/input record](STABILIZED_MANUAL_SITL_GOAL.md). Exact milestone:
**output-disabled control/allocation core and synthetic host rotational feedback
verification**. [Follow-on SM3](STABILIZED_MANUAL_SM3_SITL_INTEGRATION.md) now
passes an isolated estimated-state hold/disable smoke. Full SITL campaigns remain
open. A native-warning compatibility revision retains the same 123,083 host
checks in all configurations; its exact evidence is in that follow-on record.

## Implemented contract

`src/lib/research_stabilized_manual/` is a separate library with standalone host
tests. It is absent from checkout module/Kconfig/startup/output registration.
Its PX4 library build entry is available for later explicit integration; no
SITL firmware, board firmware, Gazebo model or physical output was built/started
in this increment. H10, stock Cessna, original model inputs and negative-only
acceleration admission remain at their recorded scopes.

One noncopyable owner configures a complete explicit profile, starts Ready and
enables only on centered, valid selected manual input and valid state. This first
source policy admits one exact MAVLink instance/system/component tuple. H6's
existing raw-input validator supplies finite/range, freshness, timestamp replay
and same-sample conflict checks. Its unity validation maps are discarded; manual
sticks are not interpreted as actuator or wrench demands.

The owner has Disabled, Ready, Rate, Hold and Fault states. While outside the
configured centering exit threshold, sticks command FRD body rates. Entering the
all-axis centering entry threshold captures one current estimated full-attitude
quaternion; the captured reference is not recaptured by later disturbances.
Entry/exit hysteresis, per-axis rate limits and reference acceleration bounds are
explicit. Stock forward pitch is mapped to negative body-Y rate by the synthetic
profile; throttle, including NaN, is ignored and fixed simulated thrust retained.

Hold error is `canonical(q_current^-1 * q_reference)`, with body rate demand
`K_attitude * 2 * imaginary(error)`, followed by limits/smoothing. No Euler
heading target or body-Z thrust-priority assumption is used. Current attitude is
normalized only inside the configured norm tolerance. State publication/sample
times, age/skew, quaternion, rates and angular acceleration are checked before
every cycle. Regressed or conflicting same-sample payloads reject. Attitude-reset
counter or adapter source-epoch changes revoke instead of silently adapting.
The future PX4 adapter must establish that source binding; the host's epoch value
is synthetic, not estimator-health qualification.

The actual unmodified PX4 `RateControl` PID is compiled and reused. A real
`RateCtrlStatus` header is freshly generated from the real message schema and
templates; no fake status struct is used. Host build supplies an empty export
annotation and the existing minimal finite-predicate platform header, with no
uORB execution or ROS runtime. Gains, integrator limits and moment normalization
are explicit. PID demand is clamped to caller bounds and allocated; the residual
includes both controller limiting and unavailable actuator moment. Directional
saturation is fed to the PID on the next cycle. Integrators reset on explicit
disable/configure. Caller output is replaced only on a successful cycle.

The four-canard solver minimizes
`sum(weight_axis * (requested - E*(u-trim))^2) + regularization*||u-trim||^2`
over magnitude and asymmetric per-cycle slew bounds. It enumerates all 81
free/lower/upper active sets using fixed storage and double-precision normal
equations. Configuration checks row norm/independence and numerical regularizer
resolution; those are mathematical checks, not physical authority proof. The
regularizer-resolution guard prevents a numerically singular free solution from
being silently replaced by an unnecessary bound-only null-space motion.

Every input/state/timing/arithmetic/allocation fault latches and revokes
eligibility. There is no automatic enable/recovery. disable/configure/centered
enable is the reset sequence. The core does not publish, arm or implement a
physical fallback. A future output supervisor must honor current eligibility;
a preserved prior report is not permission to continue an actuator command.

## Explicit synthetic fixture

All values below are host fixture inputs, not vehicle/controller defaults:

| Property | Fixture value |
| --- | --- |
| Time step / maximum owner gap | 4 ms / 12 ms |
| Manual/state age; state skew | 100 ms / 20 ms; 8 ms |
| Stick center entry/exit | 0.04 / 0.06 normalized |
| Maximum rate / reference acceleration | 1 rad/s / 20 rad/s^2 on each axis |
| Attitude gain | [4,4,4] |
| Rate P / I / D | [0.4,0.8,0.8] / [0.05,0.1,0.1] / [0.003,0.006,0.006] |
| Integrator / normalized moment bounds | +/-0.2 / +/-1 |
| Effectiveness rows | 0.25 * [[1,-1,1,-1],[1,1,-1,-1],[1,-1,-1,1]] |
| Canard trim/bounds/slew | zero / +/-1 normalized / 5 normalized units/s |
| Allocation weights / regularization / feedback tolerance | [1,1,1] / 1e-5 / 0.001 |
| Diagonal inertia | [0.03,0.08,0.08] kg m^2 |
| Rotational damping | [0.03,0.04,0.04] N m s/rad |
| Physical moment scale / servo time constant | 0.4 N m per normalized moment / 0.05 s |
| Flow / fixed thrust | declared 20 m/s body-local condition / 10 N CG-aligned body-X force |

The synthetic effectiveness is frozen at the declared flow condition; it is not
derived from the vehicle CAD or NACA label. The ideal axial force is reacted by
the translational constraint and contributes zero CG moment. Rotor reaction and
EDF wash are explicitly absent. Translation, trim, aerodynamic scheduling and
vertical launch are not simulated or qualified by this rotational fixture.
Servo demand slew bounds and a first-order actual-servo lag are both exercised.
The rigid-body angular update includes `omega cross (J*omega)`; quaternion
integration is in body axes. Plant truth feeds this host mathematical test only.

## Evidence and results

Final WSL evidence: `/var/tmp/research-stabilized-host-0bqkxvm9`.
Manifest SHA-256:
`a519c5feb3e00456ac5ac58867d2e46cebaa021665bc7f3ff016004673dff808`.

Strict `-Wall -Wextra -Werror -Wpedantic` Debug and Release pass. ASan/UBSan
passes with leak detection and halt-on-error. Each configuration runs one
executable and **123,083 checks with zero failures**. Many checks repeat across
time steps; this is not 123,083 unique tests or exhaustive state/concurrency
coverage. Scoped PX4 AStyle ran only on the new C++ files.

- 250 deterministic weighted/coupled allocation cases pass independent convex
  KKT stationarity/complementarity checks, including moving slew bounds. Analytic
  interior and magnitude-saturated cases, dependent axes, nonfinite inputs,
  missing slew and insufficient numerical regularization are checked separately.
- Reference checks cover nose-up hold, pitch sign, entry hysteresis, reference
  acceleration, release capture, disturbance reference retention, restoring
  feedback on every axis, q/-q equivalence and near-180-degree errors.
- Fault checks cover invalid/source/future/stale/nonfinite manual input, replay
  and conflict; invalid/zero/stale/future/skewed/nonfinite/conflicting state;
  estimator reset/epoch change; cycle gap; unchanged output on failure; early
  NotDue; explicit reset/re-entry; and sustained saturation/windup bounds.
- Twelve 8-second truth-feedback campaigns pass: level and body-X-up, each
  body axis, for hold/impulse and pilot-command/release/impulse sequences. A
  0.8 rad/s rate impulse is applied; manual campaigns command 0.375 rad/s and
  require observed tracking within 0.08 rad/s before release. Declared limits
  are 0.5 rad maximum separation, 0.02 rad final attitude error and 0.02 rad/s
  final rate. Final errors are about 0.0014..0.0018 rad; final rates are about
  0.00018..0.00022 rad/s. Peak separation includes intentional pilot rotation
  before release and must not be described as disturbance-only overshoot.

The package archives actual project source/header/generator inputs, generated
headers, compilation records, binaries and logs, with pre/post source checks.
All **281 pre-existing mapped anchors remain hash-identical**. Failed configure/
compile attempts are preserved in `-yn6me5cu`, `-32p2qlk2`, `-pnd_ksn7`; earlier
passing increments remain in `-ucv1uhvt`, `-0g568eb5`, `-ijtwgmea`, `-vc3tqtaw`.
The initial diagnostics were missing generator-template arguments/path and use
of a nonexistent matrix `.data()` API; the final source uses the actual template
directory and element access. No warning checks were disabled.

Reproduce from Ubuntu-24.04 WSL:

```sh
python3 -B Tools/research_sitl/verify_stabilized_manual.py --format
```

Each invocation creates a new evidence directory and preserves earlier runs.
It does not start PX4/Gazebo, flash firmware or connect to outputs. Documentation
and graph updates occur after capture and are separate from the build manifest.

## Remaining integration and goal boundary

Next: isolated PX4/Gazebo integration with the real selected manual input,
real estimated attitude/body-rate/acceleration subscriptions, explicit lifecycle
binding, exclusive setpoint/output ownership and a simulation-only actuator sink.
The three-axis rotational fixture needs sensors and degrees of freedom, explicit
fixed-thrust/flow assumptions, source/config identity and quantitative logs.
Verify actual estimator initialization, input-loss, reset, source-change,
stop/restart and output-expiry behavior. Host truth-feedback success does not
prove any of those runtime properties.

Operational plant tuning still awaits already-pending CAD, valid inertia/mass,
geometry/signs, aerodynamic authority, loaded servo dynamics and deployment
sensor/link evidence. The overall stabilized manual goal remains active. No
additional physical measurement is needed to proceed with synthetic SITL work.
