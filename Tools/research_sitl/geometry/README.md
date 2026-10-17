# Read-only SDF intake

Run from repository root with Python 3.9 or newer:

```sh
python3 -B Tools/research_sitl/geometry/check_sdf_intake.py path/to/model.sdf
python3 -B -m unittest discover -s Tools/research_sitl/geometry -v
```

The checker prints JSON and exits 1 for blocked input, 0 only for
`structural_checks_only`. It never certifies a flight model. It reads one direct
SDF model, requires explicit per-link inertials, checks mass/inertia arithmetic
and realizability, basic joint references/axes/limits, and same-model mesh URIs
relative to the supplied package directory. It does not search external Gazebo
resource paths, fetch assets, parse mesh geometry, load plugins or run simulation.
Full SDFormat validation (`gz sdf -k`) and physics tests are separate gates.
Unknown joint types, poses, mesh scale validity and STL topology are not checked
by this deliberately limited intake tool.

Input files are not modified. Inertia checks normalize arithmetic before
testing positive definiteness and second-moment constraints; the `1e-12`
tolerance is numerical only and does not invent physical uncertainty.

Eight synthetic unittest methods cover positive and deliberately invalid input,
path traversal/symlink escape, malformed XML, mass overflow, and source
preservation. These are software tests, not calibration or physical validation.

Current supplied model findings and hashes are recorded in
[geometry intake review](../../../docs/GAZEBO_GEOMETRY_INTAKE_REVIEW.md).
