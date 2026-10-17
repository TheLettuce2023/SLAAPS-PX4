# SM5 generated allocation C++ binding probe

2026-10-06, Codex. This is an isolated host diagnostic of the private PX4
configuration and allocator. It does **not** add a vehicle profile, plant
model, SITL maneuver, accepted campaign case or physical qualification.

The previously sealed [generated SITL build](STABILIZED_MANUAL_SM5_GENERATED_SITL.md)
uses allocation numbers equal to the fixture's former hard-coded constants.
Its source patch and runtime hash show the intended path, but that numerical
equality alone cannot distinguish a live generated-value connection from a
stale constant. The [C++ probe](../Tools/research_sitl/probe_stabilized_allocation_binding.cpp)
therefore executes the actual private `configuration()` and
`allocateCanards()` on a fixed requested moment. The
[runner](../Tools/research_sitl/verify_stabilized_allocation_binding.py)
rehashes the 88-file parent SITL receipt, copies its patched configuration,
fixture constants and generated header, and uses that owner's exact native
compile flags and allocator implementation. Only the copied header is changed
between seven host executables. Each diagnostic changes one first-canard
field only, so a command difference has an unambiguous input cause:

| Changed field | Baseline -> diagnostic | Request used for comparison | Observed command difference |
| --- | --- | --- | ---: |
| Effectiveness, body X row | 0.25 -> 0.35 | Moderate three-axis moment | 0.016392305 max across four commands |
| Trim | 0 -> 0.1 | Moderate three-axis moment | 0.100000001 max across four commands |
| Minimum | -1 -> -0.8 | Large negative three-axis moment | 0.199999988 on canard 0 |
| Maximum | 1 -> 0.7 | Large positive three-axis moment | 0.300000012 on canard 0 |
| Positive slew | 5 -> 4 per s | Large positive moment, 0.01 s step | 0.009999998 on canard 0 |
| Negative slew | 5 -> 6 per s | Large negative moment, 0.01 s step | 0.010000002 on canard 0 |

Each variant reports its actual C++ configuration fields and allocator
command vectors. The altered headers have explicit `DIAGNOSTIC_*` identifiers;
they are deliberately **not** produced or admitted by the synthetic profile
generator and were never installed in PX4 SITL or a vehicle. The nonzero
command changes prove that the compiled configuration consumes each copied
header field and passes its effect into the allocator under the stated host
request. They do not establish performance or authority for changed plant
conditions. The first runner attempt used exact decimal equality for `float`
output and stopped after the executables ran; the corrected runner uses a
1e-6 absolute representation tolerance. An intermediate three-executable
probe passed but grouped limits together, so the final seven-executable
diagnostic supersedes that narrower receipt.

Evidence is `/var/tmp/research-stabilized-allocation-b3ki_rzd`;
`binding-manifest.json` SHA-256 is
`d0f781066bf19909468a3088fd812c5a50401cf2b73ad3ef9455015dd5d35e06`.
It records 391 checked source-graph anchors and 53 files, independently
rehashed from Windows with zero mismatches. The accepted 31-case PX4 binary,
the two-case generated SITL binary, stock Cessna, H10 bench and negative-only
C1E-N1 path remain unchanged. Measured SLAAPS geometry, canard authority,
loaded-servo and EDF data are still required for vehicle-supported allocation
and tuning.
