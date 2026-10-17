# SM5 synthetic allocation in isolated SITL

2026-10-06, Codex. This is a software integration checkpoint for the
body-rate-stick, quaternion-hold stabilized manual mode. It is **not** a
measured SLAAPS profile, a free-flight result, or an expansion of the accepted
31-case synthetic campaign.

The [one-condition allocation compiler](../Tools/research_sitl/compile_stabilized_allocation.py)
turns the source-hashed [synthetic fixture input](../Tools/research_sitl/stabilized_allocation_synthetic.json)
into a normalized four-canard matrix and command bounds. A
[synthetic-only header generator](../Tools/research_sitl/generate_stabilized_allocation_header.py)
renders that sealed report as C++ data. The
[private adapter patch](../Tools/research_sitl/generated_allocation_adapter.patch)
feeds the generated matrix, trim, bounds and slew limits to the existing
controller configuration and prints the 64-character input hash in the owner
status. The fixture plant, controller gains, prescribed 20 m/s airflow and
fixed 10 N simulated EDF thrust retain their previous synthetic values.
The [preparer](../Tools/research_sitl/prepare_stabilized_generated_allocation.py)
rehashes the accepted native and offline-allocation parent receipts, checks
379 source-graph anchors, applies the patch only in a separate PX4 source
copy, generates the header twice for byte identity, and builds a fresh SITL
binary. The stock checkout's fixture source is unchanged.

The generated-profile PX4 SHA-256 is
`59c8b61f53a98a45fd06f46365af9d132a41d8610cad5803286145d5e4f77527`,
distinct from the accepted 31-case binary
`a2c8229b30a93434f6d6aae3170fc67f354227e5ce236eca83720d7b79397f7d`.
Both use the same source-bound Gazebo plant plugin
`795a826f771dfd5cfe3b56c719f0f036f897184457fbe4c132b917d4c5c2f044`.
The generated header contains synthetic input SHA-256
`661ab9d9a49fd791111b7e055cedd8ce631bf75979134f58b683da641ab69d82`;
PX4 status reported that complete hash before enable and during both cases.

The [runner](../Tools/research_sitl/verify_stabilized_generated_allocation.py)
used the established mixed-axis maneuver with 20 ms pilot packets, no added
sensor transport delay and Gazebo seed 42. At level orientation it commanded
negative-first unequal body rates, reversal, release to attitude hold, a
three-axis impulse, then disable. The body-X-up case used positive-first
order. The [independent trace auditor](../Tools/research_sitl/seal_stabilized_generated_allocation.py)
recomputed physical rates, quaternions, allocation products, impulse, saturation,
anti-windup and consumer shutdown. Both cases passed 14 declared checks over
12,953 control cycles. Maximum physical rate error was 0.015638 rad/s and
maximum final physical hold error was 0.005852 rad; the accepted limits remain
0.08 rad/s and 0.02 rad, respectively. The observed results establish only
these two finite synthetic cases.

Evidence is `/var/tmp/research-stabilized-sitl-oou0sind`; its
`generated-allocation-manifest.json` SHA-256 is
`cf2068502959c952a5d4ec858ebe4aa905ebba7fc771c4016313807dce53d3be`.
All 88 manifest entries independently rehashed from Windows with zero
mismatches. The first private build in
`/var/tmp/research-stabilized-sitl-y6rwr5es` reached readiness but its
one-line status truncated the profile hash; the runner rejected it before
command phases. The corrected private build prints the hash on a separate
line. This was a status-output defect, not evidence of a control failure.

The [header boundary tests](../Tools/research_sitl/test_generate_stabilized_allocation_header.py)
pass synthetic rendering and rejection of measured/qualified, wrong-hash,
wrong-shape/order and nonfinite inputs. The adapter patch passes
`git apply --check --whitespace=error`; the isolated PX4 build and linked
private owner pass. The accepted source and 31-case receipt remain unchanged.

No new human input is needed to continue bounded synthetic testing or to
review the general profile-integration seam. Vehicle-specific allocation
and gains still require dimensioned CAD with CG/inertia and canard geometry,
signed three-axis canard moment derivatives and uncertainty across the intended
airflow/EDF/wash range, loaded servo command-to-deflection calibration and
dynamics, measured EDF thrust/lag/wash, and approved control requirements and
physical limits. Those measurements were reported unavailable. A measured
single-condition profile would still need review, robust authority and trim
assessment over the operating envelope, retuning, and separate hardware and
flight acceptance; this synthetic-only header generator deliberately rejects
it.

The [subsequent admission guard](STABILIZED_MANUAL_SM5_ADMISSION_GUARD.md)
closes a report-content binding gap in the header generator. It emits the
same header bytes used by this sealed SITL binary; the two runtime cases were
not repeated.

The [C++ binding probe](STABILIZED_MANUAL_SM5_BINDING_PROBE.md) later changed
only copied diagnostic header values and observed distinct commands from the
same private configuration and allocator. It adds data-flow evidence, not a
new SITL case or admitted plant profile.
