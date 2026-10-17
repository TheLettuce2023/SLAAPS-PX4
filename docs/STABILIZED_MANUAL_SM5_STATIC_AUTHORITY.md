# SM5 one-condition static incremental moment-box screen

2026-10-06, Codex. This is an **offline sufficient static check** for an
explicit synthetic demand, not a SLAAPS vehicle authority or flight result.
The [rank screen](STABILIZED_MANUAL_SM5_INTERVAL_RANK.md) proves that every
matrix in the reported one-condition uncertainty box has smallest singular
value squared at least `L > 0`. The
[static screen](../Tools/research_sitl/screen_stabilized_static_authority.py)
also checks a [declared demand box](../Tools/research_sitl/stabilized_static_demand_synthetic.json)
bound to the exact profile-input SHA-256. The demand is **incremental
normalized body-FRD moment about the declared canard trim**. It is not an
approved vehicle moment requirement or a total moment including trim bias.

Let `r` be the smallest distance from any declared canard trim command to
either of that canard's static command bounds. An actuator-displacement
Euclidean ball of radius `r` then fits inside the four-canard command box.
For any 3x4 matrix in the reported uncertainty box, the minimum-norm right
inverse can produce every incremental moment `m` with
`||m||^2 <= r^2 L` without leaving that actuator ball. A desired axis-aligned
moment box with absolute axis bounds `d_i` is therefore covered when
`sum_i d_i^2 <= r^2 L`. The screen compares those squared values using exact
rational arithmetic on the serialized inputs. A nonpositive rank bound or
larger request is **inconclusive**, not proof that a moment is impossible.

The synthetic fixture has `r = 1`, `L = 1/4`, and a declared demand of
`[-0.1, 0.1]` on each normalized moment axis. Its largest squared moment
norm is `3/100`, below the conservative guaranteed `1/4`; the result is
`sufficient_static_incremental_box_reachable`. Six
[host tests](../Tools/research_sitl/test_screen_stabilized_static_authority.py)
cover the exact boundary, larger demand, tighter actuator bounds,
inconclusive rank, a mock measured-schema case that remains unqualified,
invalid demand and compiled-report tampering. The mock measured-schema data
reuse fixture numbers and are not SLAAPS measurements.

The [source-bound verifier](../Tools/research_sitl/verify_stabilized_static_authority.py)
rehashes the offline compiler and rank-screen parents, checks 400 source-
graph anchors, runs the tests and CLI, and seals 13 files at
`/var/tmp/research-stabilized-allocation-x42ielic`. Its
`static-authority-manifest.json` SHA-256 is
`913cb7b27308dfa2a158e6a05b53ed1a08dd0a48b83f1c332e7f9ba981169f6a`;
an independent Windows rehash found zero mismatches.
The first 13-file receipt was superseded after adding an explicit
`implemented_allocator_tracking_qualified=false` flag; the mathematical
screen and synthetic demand did not change.

This guarantee assumes the reported elementwise matrix intervals cover the
true local response and that the declared linearization remains valid over
the needed canard displacements. It does **not** prove an actual zero-moment
trim, total torque authority against EDF or aerodynamic bias, or that PX4's
regularized allocator chooses an exact-moment solution for every request. It
also does not prove actuator slew or lag, saturation recovery, gain choice,
airflow/EDF/deflection scheduling, or free flight. Exact arithmetic on the
serialized report does not certify
the source measurement uncertainties or floating-point propagation into that
report. Actual vehicle use still needs reviewed canard and loaded-servo data,
EDF/wash and CG/inertia measurements, a measured trim and bias map, and an
approved moment-demand envelope over intended conditions. The accepted
31-case SITL binary, generated two-case variant, stock Cessna, H10 bench and
negative-only C1E-N1 acceleration path are unchanged.
