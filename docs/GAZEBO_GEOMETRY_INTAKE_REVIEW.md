# Gazebo geometry intake G1 — 2026-09-19

Outcome: supplied assets inventoried; plant acceptance BLOCKED. The files are
preserved as inputs, not installed, corrected, launched or approved as physics.
This is Phase 3 preparation, not completion of the Phase 3 open-loop plant gate.

## Inputs and provenance

`docs/vehicle_geometry/GAZEBO FILES/` contains SDF 1.9 model `net_interceptor`
and three meshes. The legacy model name does not change the research-only scope.

| File | SHA-256 |
| --- | --- |
| model.sdf | 7AB3A524B8F7815CA893F63611B5E41F741096BB7E3F10716D2C0F5C5AFA00ED |
| frame_fixed.stl | 6656AB3BA5AAFD07B0C273391B00065E7ACF3F9D260CE46A4B960F42FEE74228 |
| fin_airfoil.stl | B7693A3BE244FB1FE439309B701CDF24E8D38F0A7EF02170C00261EEB29FAC4B |
| edf_rotor.stl | 5760555497A1911754CA3A61659D1FF22B91F77AE34F278DC5D81B61911DBA80 |

User direction in this review: assume approximately 3.04 kg. Working
interpretation, explicitly communicated to the user: **3.041337 kg total vehicle
mass including the moving canards and EDF rotor**. This supersedes the older
approximately 2.5 kg planning estimate for provisional mass accounting only.
It is not a new measurement, component breakdown, inertia correction or
authorization to tune a controller. Expected 5.5 kgf thrust remains unmeasured.

## Observed content versus missing evidence

- Six links: base, four movable canards and rotor; five revolute joints.
  The canard Y/Z positions are the four diagonal sign combinations of
  approximately 0.09051 m: geometrically an X layout despite legacy plus-pattern
  comments and elevon/rudder names. This is not a validated control mixer.
- Body/rotor mesh scales are 0.001; the generated fin mesh scale is 1.
  Per-part units and CAD-to-body FRD transforms need provenance. Body X-forward
  intent alone does not establish Gazebo FLU/FRD conversion or positive hinge signs.
- The base visual includes passive rear fins per the supplied comments, but
  there are no explicit body/passive-tail aerodynamic plugins. Four LiftDrag
  plugins cover moving fins only. Geometry appearance does not add those forces.
- One IMU sensor is declared at 250 Hz without explicit mounting/noise details.
  No explicit GNSS, pitot, magnetometer or barometer model is supplied here;
  no PX4 sensor/actuator bridge or four-servo command system is configured here.
- Moving-fin mass, inertia, area, hinge placements, aerodynamic coefficients
  and motor time/constants are placeholders or lack supporting evidence.
  Neither static-launch authority nor a stall/net-entry speed follows from them.
- Motor plugin and thrust/drag axes need checked-version compatibility and
  open-loop force/sign tests. A +X joint axis alone does not prove plugin thrust
  direction. No powered physical test is authorized.

## Blocking findings

1. **Invalid inertia:** base `Iyy=0.1059419` exceeds
   `Ixx+Izz=0.0048332+0.0105940=0.0154272 kg m^2`. This necessary rigid-body
   inequality fails independent of a coordinate rotation. Do not guess a decimal
   correction, rescale or discard cross terms. The original CAD mass-properties
   report, units, reference point, axes and component scope are needed.
2. **Mass accounting:** SDF sums to `3.291337 kg`: base `3.041337` plus five
   `0.05` links. The user-directed provisional all-up mass is `3.041337 kg`.
   Do not count the components twice or subtract arbitrary placeholder masses
   to manufacture a valid per-link inertia model. A component breakdown is needed.
3. **Package layout:** six mesh URI instances refer to
   `model://net_interceptor/meshes/...`, but all three mesh files are alongside
   the SDF, not under `meshes/`; `model.config` is absent. A future derivative
   package can repair paths without editing these supplied inputs. No external
   resource-path configuration was assumed during this local-package check.
4. **Physics/sign/sensor completion:** exact canard/EDF transforms, hinge and
   normal directions, passive-body forces, EDF thrust/lag, servo dynamics and
   representative sensors remain unvalidated. These block a plant-supported
   controller even after package and inertia errors are fixed.

## Verification and limitations

New read-only checker:

```sh
python3 -B Tools/research_sitl/geometry/check_sdf_intake.py \
  "docs/vehicle_geometry/GAZEBO FILES/model.sdf"
python3 -B -m unittest discover -s Tools/research_sitl/geometry \
  -p test_check_sdf_intake.py -v
gz sdf -k "docs/vehicle_geometry/GAZEBO FILES/model.sdf"
```

Observed in Ubuntu-24.04 WSL: intake returns structured `readiness=blocked`,
seven errors (inertia plus six unresolved URI instances); eight independent
test methods pass, including positive-definite but physically unrealizable
inertia, nonfinite/overflow, malformed XML and path escape. These are synthetic
software tests, not physical measurements. `gz sdf -k` independently reports
`Error Code 19: A link named base_link has invalid inertia.` No simulation ran.
An initial independent Windows-PATH probe found no validator; master checked
WSL explicitly and corrected that environment-specific conclusion.

The intake checker is deliberately not a full SDFormat parser, mesh validator,
plugin compatibility checker or force model. Its only non-blocked label is
`structural_checks_only`; it can never certify flight readiness. An initial
quoting-induced Python syntax error was corrected before final tests and the
actual structured audit; exit code alone was not accepted as an audit result.

## Minimum next input and gate

Mass assumption is recorded; the immediate physical input still needed is the
original CAD mass/CG/inertia report with units, axes/reference point and which
components it includes. After that: resolve component partition and frames,
assemble a separately named derivative model under `models/research_vehicle/`,
validate packaged SDF, complete sensor/actuator/force contracts, and run reviewed
open-loop tests with parameter provenance. The stock Cessna submodule remains
unchanged. Geometry-independent interface work may continue meanwhile.
