# SM5 one-condition static net-moment screen

2026-10-06, Codex. The earlier [incremental moment-box screen](STABILIZED_MANUAL_SM5_STATIC_AUTHORITY.md)
checks a moment change about declared canard trim. The
[net screen](../Tools/research_sitl/screen_stabilized_net_authority.py) adds an
explicit **body-FRD moment bias at that trim**, its absolute uncertainty, and
a desired net moment box. It remains a sufficient offline local-linear test,
not a measurement or SLAAPS vehicle authority claim.

The [input record](../Tools/research_sitl/stabilized_net_authority_synthetic.json)
binds the exact allocation-profile hash, airflow/EDF/wash condition and
ordered canard trim commands. Separate source hashes identify the bias basis
and [demand basis](../Tools/research_sitl/stabilized_net_demand_synthetic.txt).
All bias and demand entries are physical N·m. A future vehicle record must
include measured EDF/aerodynamic moment at the declared trim and condition,
its uncertainty, and a reviewed net-moment requirement with their actual
source files; a hash alone does not establish measurement validity or review.

For each body axis `i`, let `D_i` be the maximum absolute desired **net**
moment, `b_i` the nominal moment at canard trim, `beta_i` its absolute
uncertainty, and `S_i` the positive N·m-per-normalized-moment scale from the
source profile. Every needed normalized incremental correction lies within
the centered box with bound

`c_i = (D_i + |b_i| + beta_i) / S_i`.

The screen recomputes the compiler output from the exact source profile,
checks both provenance roles, then uses exact rational arithmetic on the
serialized values. If the reported matrix intervals have Gram eigenvalue
lower bound `L > 0`, and `r` is the smallest canard trim-to-static-bound
margin, `sum(c_i²) <= r² L` is sufficient: for any matrix and bias inside
their **reported** intervals and any requested net moment in the declared
box, a static canard command exists within the declared bounds. A larger
demand or nonpositive rank bound is inconclusive, not proof of impossibility.
The argument depends on the local linearization and uncertainty ranges
covering the real plant throughout the required command displacement.

The synthetic fixture has zero declared trim bias and zero bias uncertainty,
fixture moment scale `0.4 N·m` per axis, and a demonstration demand of
`+/-0.04 N·m` net moment per axis. The correction box is `+/-0.1`
normalized per axis; `sum(c_i²) = 3/100`, below `r² L = 1/4`. The output is
`sufficient_static_net_box_reachable` for this synthetic condition only.
The zero-bias basis is the ball-constrained fixture with EDF thrust at the CG,
zero canard displacement and no injected impulse at rest; it is not a
measured zero-moment SLAAPS trim.

Six [host tests](../Tools/research_sitl/test_screen_stabilized_net_authority.py)
cover signed nonzero bias, uncertainty, an exact guarantee boundary,
inconclusive rank/command margin, measured-schema uncertainty requirements,
invalid condition/trim/input, and compiled-report or bias-source tampering.
Even a mock measured-schema sufficient result has
`vehicle_qualified=false`. The
[source-bound verifier](../Tools/research_sitl/verify_stabilized_net_authority.py)
rehashes the compiled-allocation and static-screen parents, checks 408 graph
anchors, runs the tests and CLI, and seals 16 files at
`/var/tmp/research-stabilized-allocation-dmgchf0d`. Its
`net-authority-manifest.json` SHA-256 is
`ff487d1ad8ef830a9e456f5df8789d0c0e6e7c1a717ba8e765169e93835fe00d`;
an independent Windows rehash found zero mismatches.

This theorem does not show that the implemented regularized allocator will
track every net request, that the canards have measured authority over the
full airflow/deflection/EDF range, or that servo slew and lag, controller
transients, sensor validity and hardware output are qualified. Actual
moment-bias data, uncertainty and reviewed demand are not yet available.
The later [private steady-bias SITL diagnostic](STABILIZED_MANUAL_SM5_STEADY_BIAS_SITL.md)
uses a nonzero synthetic torque with the unchanged PX4 controller. Its two
finite native passes add dynamic fixture evidence, not measured authority.
The accepted 31-case synthetic SITL campaign, stock Cessna, H10 bench and
negative-only C1E-N1 path remain unchanged.
