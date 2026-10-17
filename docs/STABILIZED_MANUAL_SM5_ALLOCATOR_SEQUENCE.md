# SM5 compiled synthetic allocator command-history check

2026-10-06, Codex. This host diagnostic carries each **actual C++ allocator**
output into the next call as its previous command. It builds on the
[independent from-trim grid](STABILIZED_MANUAL_SM5_ALLOCATOR_GRID.md) using the
same sealed private generated profile, declared body-FRD incremental moment
box, and recorded PX4-owner compiler flags. It is an allocator-command
sequence, not servo motion, rate feedback, or a new SITL maneuver.

The [probe](../Tools/research_sitl/probe_stabilized_allocator_sequence.cpp)
starts at canard trim for each of the eight sign combinations at the
`(+/-0.1, +/-0.1, +/-0.1)` synthetic demand-box corners. It holds that request
for twelve 10 ms calls, reverses all three moment signs for twelve calls,
then requests zero for twelve calls. Every call uses the preceding allocator
command as its history. The [verifier](../Tools/research_sitl/verify_stabilized_allocator_sequence.py)
checks all 288 rows, exact phase order and command continuity, recomputed
achieved moment/residual/objective, static and signed slew bounds, and the
generated input hash. It rehashes the private SITL and grid parents and 403
source-graph anchors before compilation.

| Phase | Largest first-step residual norm | Largest final residual norm | Most steps to residual norm <= 0.0011 |
| --- | ---: | ---: | ---: |
| Hold corner | `0.129904` | `0.00000693` | 4 |
| Reverse corner | `0.303102` | `0.00000693` | 8 |
| Release to zero | `0.129897` | `0` | 4 |

The allocator encountered 480 10 ms command-slew bounds. Its largest command
change was `0.050000004` normalized units, within the float-tolerant check of
the declared `5/s` limit. These are **observed sampled results** for constant
corner requests. The 4/8/4 step counts describe requested-minus-
**allocator-achieved** moment, not physical moment settling. The fixture's
separate 50 ms servo lag, aerodynamic response, controller behavior and
estimated-state timing are absent from this host diagnostic. Existing native
SITL campaigns exercise some of those effects, but this sequence adds no
vehicle or full-envelope acceptance.

The final 13-file evidence directory is
`/var/tmp/research-stabilized-allocation-ajw47u5g`; its
`sequence-manifest.json` SHA-256 is
`009e14b45ade7890b67ce429cb7b58d3b34216105ef93a316b1c9058ffcf8f57`.
An independent Windows rehash found zero mismatches. The first probe receipt
was superseded after tightening the verifier's final-row coverage check;
observations were unchanged. The accepted 31-case native SITL binary, stock
Cessna, H10 bench and negative-only C1E-N1 path were unchanged. Vehicle
measurements and approved demand remain unavailable.
