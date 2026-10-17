# SM5 private synthetic steady-bias SITL diagnostic

2026-10-06, Codex. A separate Gazebo fixture plugin adds a **declared
constant body-FRD moment only while fixed simulated EDF thrust is enabled**:
`(+0.010, -0.015, +0.012) N·m`. This is a synthetic torque-disturbance
exercise, not measured EDF reaction torque or SLAAPS aerodynamic data. The
generated-profile PX4 binary, controller gains, pilot mapping, actuator
allocation, accepted 31-case campaign, stock Cessna, H10 bench and
negative-only C1E-N1 path were unchanged.

The [private preparer](../Tools/research_sitl/prepare_stabilized_bias_sitl.py)
rehashes the sealed [generated-profile parent](STABILIZED_MANUAL_SM5_GENERATED_SITL.md),
checks 414 source-graph anchors, and builds a separate plugin from a copied
fixture source. It applies the bias after damping in the plugin's torque path
and logs the applied bias on each truth row. The existing case runner's
`source-inputs` snapshot still records the **unmodified** fixture; the actual
plugin source is separately preserved under `bias-source` and identified by
the private origin record and binary hash. The PX4 ELF SHA-256 remains
`59c8b61f53a98a45fd06f46365af9d132a41d8610cad5803286145d5e4f77527`;
the bias plugin SHA-256 is
`7e8e1153d599f556628218901f3fa3b74d54d282ac7bc3cf0d5fd636f85e3a02`.

The [native runner](../Tools/research_sitl/verify_stabilized_bias_sitl.py)
reuses the previously declared unequal-axis body-rate, reversal, release,
three-axis impulse and disable maneuver. It runs once level with negative
first and once body-X-up with positive first, with 20 ms pilot packets and
Gazebo seed 42. PX4 status reports the complete generated allocation profile
hash before enable. Both cases pass all 14 existing mixed-axis checks with
unchanged thresholds: 12,931 audited control cycles, maximum physical rate
tracking error `0.015732 rad/s` versus `0.08`, and maximum final physical
hold error `0.003078 rad` versus `0.02`. These are finite exploratory cases,
not an accepted-campaign expansion or a robustness margin.

The [independent sealer](../Tools/research_sitl/seal_stabilized_bias_sitl.py)
checks every truth row's applied bias, virtual-servo recurrence and deflection
scale, and matches each enabled target to the PX4 publication at its logged
`command_time`. All 12,944 enabled physics rows match. There are 161 PX4
control publications without their own enabled target observation because
physics and control publications are asynchronous; the audit does not claim
every publication was separately applied. The maximum target mismatch is
below `5e-10` normalized command. In the final 300 ms of each case, the mean
canard moment opposes the declared bias; the largest component of mean
canard-plus-bias moment is `0.0002193 N·m`. This is a trace observation of
canard moment and injected bias, not a total rigid-body torque measurement
including damping or an installed-vehicle trim result.

For this exact synthetic bias and the earlier `+/-0.04 N·m` per-axis
demonstration demand, the [static net screen](STABILIZED_MANUAL_SM5_NET_AUTHORITY.md)
reports squared normalized correction `8229/160000` (about `0.05143`), below
the synthetic static guarantee `1/4`. This conditional existence result and
the two native observations have different scopes; neither qualifies
unknown bias uncertainty, full airflow/EDF conditions, actuator hardware or
free flight.

Evidence is `/var/tmp/research-stabilized-sitl-mfn4yrcb`; its 133-file
`bias-manifest.json` SHA-256 is
`706ff1e6b19cb81d1d29cd74d656bd73c4a9025d07235a380c432dafcdbc893a`.
Independent Windows rehashes found zero mismatches here and in the 88-file
generated-profile parent. Actual canard, loaded-servo, EDF/wash, trim-bias,
CG/inertia and approved net-demand measurements remain pending.
