# SM5 compiled synthetic allocator finite-grid check

2026-10-06, Codex. This is an isolated host diagnostic of the **actual C++
allocator** using the private generated-allocation configuration. It checks
finite requests inside the [declared synthetic incremental moment box](../Tools/research_sitl/stabilized_static_demand_synthetic.json),
not SLAAPS vehicle authority or a continuous operating envelope.

The [probe](../Tools/research_sitl/probe_stabilized_allocator_grid.cpp) is
compiled with the private PX4 owner's recorded compiler flags and its exact
`CanardAllocator.cpp`. The configuration and generated header are copied from
the sealed private SITL variant. For each of 11 values per body-FRD normalized
moment axis (`-0.10` through `+0.10`, step `0.02`), it calls `allocateCanards`
once from declared canard trim at `dt = 1 s` and once at `dt = 10 ms`. These are
independent requests from trim, not a trajectory or controller-in-the-loop run.

The [verifier](../Tools/research_sitl/verify_stabilized_allocator_grid.py)
checks all 2,662 CSV rows against the sealed synthetic allocation report. It
recomputes achieved moment and residual from the reported 3x4 matrix and
commands, checks static command bounds and signed slew from trim, checks the
regularized objective, and requires complete grid coverage with the exact
input hash. It rehashes the private SITL and static-screen parents and 400
existing graph anchors before compilation.

| Independent mode | Requests | Worst residual norm | Largest absolute command |
| --- | ---: | ---: | ---: |
| `dt = 1 s` | 1,331 | `0.00000693` | `0.299988` |
| `dt = 10 ms` | 1,331 | `0.129904` | `0.050000` |

The 10 ms run encountered a slew bound 3,976 times across its four-channel
outputs. Its much larger residual is expected for requests too large to reach
in one step from trim. The 1 s result is an **observed finite-grid** result for
this synthetic matrix and regularization, not an exact-tracking proof for every
point in the continuous box. The prior [static screen](STABILIZED_MANUAL_SM5_STATIC_AUTHORITY.md)
proves a feasible bounded command exists under its reported matrix-interval
assumptions; this check observes what the implemented allocator chooses at the
sampled points. Neither result establishes a zero-net-moment trim, tracking
after successive commands, servo lag, EDF/aerodynamic bias compensation,
gain scheduling, free flight or hardware acceptance.

The 13-file evidence directory is
`/var/tmp/research-stabilized-allocation-38ewevkd`; its `grid-manifest.json`
SHA-256 is `c73cead601efd5fb796393648eca7c9cc1c72b35079107e22903a27cd815f005`.
An independent Windows rehash found zero file mismatches. The accepted
31-case native SITL campaign, stock Cessna, H10 bench and negative-only
C1E-N1 acceleration path were unchanged. Vehicle-specific geometry,
force/moment response, loaded servo, EDF/wash, CG/inertia and approved demand
measurements remain unavailable; synthetic software diagnostics can continue.
