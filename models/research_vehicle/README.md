# Research vehicle derivative package

2026-09-27: package layout repaired; **physical acceptance remains blocked**.
This package is not connected to PX4 startup, a Gazebo resource path or a world.
No simulation was launched. Original inputs remain under
`docs/vehicle_geometry/GAZEBO FILES/`.

Only the SDF model name and six mesh URI prefixes changed. Three meshes were
copied byte-for-byte into `meshes/`, and `model.config` was added. All physical
values and inherited source comments are preserved; comments claiming measured
or verified physics do not override the intake findings.

`provenance.json` records original and derivative SHA-256 hashes and the exact
transformation. `intake_report.json` records the read-only structural audit:
all six mesh references resolve; the base inertia still fails rigid-body
realizability. Declared link mass remains 3.291337 kg against provisional
3.041337 kg all-up. Packaging has not fixed or accepted either physical issue.

Recheck from repository root:

```sh
python3 -B Tools/research_sitl/geometry/check_sdf_intake.py models/research_vehicle/model.sdf
gz sdf -k models/research_vehicle/model.sdf
```

Both checks are expected to reject the current inertia. A non-blocked intake
label would still mean structural checks only, not plant acceptance.

## Required original CAD evidence

Provide the native CAD mass-properties export/report and its assembly revision,
configuration and suppressed/included component list. It must identify:

- Total and per-rigid-link mass, units and uncertainty/source of material density.
- CG coordinates, coordinate axes/handedness and origin/reference point.
- Full symmetric inertia tensor, units, axes, tensor reference point, and the
  report's product-of-inertia sign convention; retain all cross terms.
- Scope of frame, fixed tail, electronics, batteries, duct/stator, each movable
  canard and rotor so that each component belongs to exactly one link.
- CAD-to-body FRD and mesh/link transforms with rotation convention and units;
  distinguish reference-point translation from visual mesh rotation.

Do not obtain component properties by subtracting placeholder 0.05 kg masses.
Reconcile all-up and per-link values, transform tensors using the documented
axes and parallel-axis relation, and check positive definiteness and physical
realizability before changing SDF physics. Original inputs remain immutable.

After those inputs, remaining plant gates are measured hinge/sign and EDF-axis
validation, local flow/body/passive-tail aerodynamics, servo and motor dynamics,
sensor mounting/noise/latency, and plugin/version compatibility. The future
resource path must be explicit and keep the stock Cessna repository intact.
See [intake review](../../docs/GAZEBO_GEOMETRY_INTAKE_REVIEW.md) and
[positive-admission design](../../docs/C1E_POSITIVE_ADMISSION_DESIGN.md).
