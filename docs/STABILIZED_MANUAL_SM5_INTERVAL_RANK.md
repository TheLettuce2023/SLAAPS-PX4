# SM5 one-condition allocation uncertainty rank screen

2026-10-06, Codex. This is an **offline sufficient rank screen**, not
SLAAPS vehicle authority or flight acceptance. The existing
[allocation compiler](../Tools/research_sitl/compile_stabilized_allocation.py)
reports a nominal normalized 3x4 matrix `A` and nonnegative elementwise
absolute uncertainty `U` for one declared airflow/EDF/wash condition. Its
nominal row-independence check alone cannot show that rank survives those
intervals. The new
[screen](../Tools/research_sitl/screen_stabilized_allocation_rank.py)
requires the exact input and compiled report to match a fresh compile,
checks all four referenced source roles against their SHA-256 records, and
rejects source paths outside the declared evidence root.

For every matrix `A + D` with `|D[i,j]| <= U[i,j]`, the screen bounds the
Gram-matrix perturbation by

`E[i,j] = sum_k (|A[i,k]| U[j,k] + U[i,k] |A[j,k]| + U[i,k] U[j,k])`.

It then computes the Gershgorin lower bound

`L = min_i (G[i,i] - E[i,i] - sum_{j != i} (|G[i,j]| + E[i,j]))`,
where `G = A A^T`. If `L > 0`, every matrix in the **reported elementwise
interval box** has full row rank. The arithmetic on the compiled JSON
decimals is exact rational arithmetic. A nonpositive `L` is **inconclusive**;
it is not evidence that the real vehicle loses rank. If the relaxed box
contains the zero matrix, the screen says so explicitly. Shared physical
uncertainties may correlate its entries, so that box can be wider than the
actual plant possibilities.

Six [host tests](../Tools/research_sitl/test_screen_stabilized_allocation_rank.py)
cover the zero-uncertainty fixture, a mock measured-schema profile with
small nonzero uncertainties, a large interval box containing the zero
matrix, a full-rank matrix for which this conservative bound is inconclusive,
invalid uncertainty/provenance, and CLI rejection of a modified compiled
matrix retaining the original input-hash field. The mock measured-schema
cases use synthetic fixture numbers only and are not vehicle measurements.
The sealed fixture result has exact `L = 1/4`; the zero-uncertainty matrix
therefore has a 0.5 lower bound on its smallest singular value. It remains
synthetic and unqualified.

The [source-bound verifier](../Tools/research_sitl/verify_stabilized_allocation_rank_screen.py)
rehashed the 13-file offline compiler parent, checked 394 current graph
anchors, ran the tests and CLI, and sealed 11 artifacts at
`/var/tmp/research-stabilized-allocation-3ttfw2an`. Its
`rank-screen-manifest.json` SHA-256 is
`3c648e090fb2c5edcaa12c7d92348470a9a61a0d27997341fa466811613cd443`;
an independent Windows rehash found zero mismatches.

A positive result assumes that the reported `U` covers the true local
effectiveness error at that **single condition**. Exact rational arithmetic
on the serialized report does not certify the compiler's floating-point
uncertainty propagation or the completeness of the underlying measurements.
This screen does not
establish a full airflow/EDF/deflection envelope, usable moment authority
under command bounds and slew, trim feasibility, actuator or EDF dynamics,
controller tuning, or hardware routing. The original 31-case accepted SITL
binary, two-case generated SITL variant, stock Cessna, H10 bench and
negative-only C1E-N1 acceleration path are unchanged. Actual CAD, canard
moment, loaded-servo and EDF/wash measurements remain the human inputs for
vehicle-supported allocation and tuning.

The subsequent [static incremental moment-box screen](STABILIZED_MANUAL_SM5_STATIC_AUTHORITY.md)
combines this rank lower bound with declared canard command margins and a
separate synthetic demand. It remains a one-condition local linear check.
