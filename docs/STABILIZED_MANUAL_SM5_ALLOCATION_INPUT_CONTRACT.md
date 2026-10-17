# Stabilized manual: single-condition plant-to-allocation input contract

2026-10-06, Codex. This is **SM5 preparation**, not a measured SLAAPS
profile or physical acceptance. The offline
[compiler](../Tools/research_sitl/compile_stabilized_allocation.py) turns one
declared body-FRD, finite-airflow moment linearization and loaded canard
calibration into the `effectiveness`, trim, command bounds and slew fields
consumed by the [four-canard allocator](../src/lib/research_stabilized_manual/CanardAllocator.hpp).
It never installs the result in PX4 or marks it vehicle-qualified.

For body moment axis `i` and canard `j`, the conversion is

`A[i,j] = (dM_i/dδ_j) × (dδ_j/du_j) / S_i`,

where `dM_i/dδ_j` is measured N·m/rad about the vehicle CG in body FRD,
`dδ_j/du_j` is the **signed, loaded** local deflection slope in rad per
normalized command at trim, and `S_i > 0` is the reviewed N·m-per-normalized-
moment scale. The resulting `A` is normalized body moment per normalized
canard command displacement from trim. A negative hinge/servo sign must be
expressed in the signed slope; a negative aerodynamic response must be
expressed in the measured derivative. The compiler propagates declared
absolute derivative and slope uncertainty to a conservative elementwise
bound on `A` and carries the ordered canard IDs alongside its columns. This
is a local linearization, not a full deflection envelope or proof of hardware
output-channel routing.

The JSON input requires four separately hashed source roles: CAD, canard
moment tests, loaded servo calibration, and EDF/airflow condition. It also
requires:

- Mass, CG and a symmetric positive-definite full 3×3 inertia tensor at the
  CG in body FRD, with units kg, m and kg·m².
- The test airflow speed, angle of attack, sideslip, EDF thrust and measured
  local wash speed at each canard. The derivatives must already correspond to
  this exact condition; the compiler does not add free stream and wash or
  infer aerodynamic coefficients from thrust.
- Four ordered canard IDs; each signed 3-axis moment derivative and its
  uncertainty with the other surfaces held at trim; loaded deflection slope
  and uncertainty; normalized trim, limits, positive/negative command slew,
  lag time constant and command latency. Per-canard wash entries use the same
  order as the matrix columns and canard IDs.
- Three positive moment normalization scales. These are control-design
  choices that must be checked against demanded moments and measured
  authority, not inferred from the synthetic 0.4 N·m scale.

A reviewed zero-moment trim and moment-bias map, plus approved demanded
body-FRD moments over the intended airflow/EDF conditions, are also needed
before vehicle authority can be assessed. The compiler's local derivative
and command trim fields alone do not establish either.

For `measured_single_condition`, all derivative and slope uncertainties must
be positive and the slope's uncertainty cannot cross zero. Missing fields,
unknown fields, nonfinite values, an invalid inertia tensor, out-of-range
normalized commands, or numerically dependent moment rows are rejected.
The output reports row norms and the allocator's nominal modified-Gram-
Schmidt independence scores; it does **not** choose operational row-norm or
independence thresholds, prove rank under uncertainty, select gains, trim the
vehicle, model nonlinear surfaces, or qualify free flight. Source hashes in
a future external measured profile still need independent record review.
The separate [one-condition interval rank screen](STABILIZED_MANUAL_SM5_INTERVAL_RANK.md)
can now give a conservative sufficient full-row-rank result for the
compiler's reported elementwise uncertainty box, but does not change those
other authority and physical gates.
The [static incremental moment-box screen](STABILIZED_MANUAL_SM5_STATIC_AUTHORITY.md)
can also compare an explicitly declared demand box to conservative
one-condition command margins. Its synthetic example is not a SLAAPS
requirement or proof of net trim.
The later [net-moment screen](STABILIZED_MANUAL_SM5_NET_AUTHORITY.md) makes the
trim moment bias and its uncertainty explicit, then compares a source-bound
physical net demand against the same conservative rank and command margins.
It provides a sufficient static result for zero-bias synthetic fixture data;
real trim-bias and approved demand records are still required.
EDF thrust/lag/battery and wash curves, broader airflow/deflection maps,
physical control requirements, PWM routing and hardware acceptance remain
separate inputs and gates.

The [source-hashed synthetic example](../Tools/research_sitl/stabilized_allocation_synthetic.json)
uses the existing ball-constrained fixture: 1 kg, diagonal inertia
`(0.03, 0.08, 0.08)` kg·m², prescribed 20 m/s airflow, fixed 10 N
simulated thrust, zero EDF wash, 0.226892802759 rad per normalized canard
command and 0.4 N·m moment scale. Its physical derivative magnitude
0.440736765485 N·m/rad reconstructs the fixture's exact signed matrix:

```text
0.25  -0.25   0.25  -0.25
0.25   0.25  -0.25  -0.25
0.25  -0.25  -0.25   0.25
```

The three row norms are 0.5 and the nominal independence scores are 1.0.
Five offline tests cover this fixture round-trip, signed loaded-servo mapping,
uncertainty propagation, invalid inertia/rank, required measured uncertainty,
and malformed/nonfinite or duplicate-key input. The CLI also refuses to
overwrite an existing report. The [source-bound verifier](../Tools/research_sitl/verify_stabilized_allocation_profile.py)
revalidated 379 graph anchors, ran the tests and compiler, and sealed 13
artifacts. Its receipt is
`/var/tmp/research-stabilized-allocation-4qimab45/allocation-manifest.json`,
SHA-256 `d7f68d120f21543ddb70289a039189f6fed9d48db46d5f3f5a418ea551b0ed1c`;
an independent Windows-side rehash found zero mismatches.

The user reports the actual CAD and canard, loaded-servo and EDF measurements
are not yet available. The synthetic example must not be copied as a SLAAPS
vehicle profile. The accepted 31-case synthetic SITL checkpoint, stock
Cessna, H10 bench, negative-only C1E-N1 acceleration and physical gates are
unchanged.

The [generated synthetic SITL integration](STABILIZED_MANUAL_SM5_GENERATED_SITL.md)
subsequently compiled this sealed fixture output into a separate PX4 owner
and passed two exploratory mixed-axis cases. It does not admit a measured
vehicle profile or change the 31-case accepted checkpoint.
