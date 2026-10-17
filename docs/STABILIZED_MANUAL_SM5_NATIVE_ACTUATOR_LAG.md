# SM5 native synthetic actuator-lag trace audit

2026-10-06, Codex. This audit reads the **existing sealed generated-profile
SITL cases**; it does not run a new simulation or expand the accepted 31-case
campaign. It connects timestamped PX4 allocator outputs to the Gazebo
rotational fixture's actuator targets and logged surface motion at the
declared synthetic condition: prescribed 20 m/s airflow, fixed simulated 10 N
EDF thrust and a 50 ms virtual-servo time constant.

The [auditor](../Tools/research_sitl/audit_stabilized_native_actuator_lag.py)
rehashes the 88-file [generated-profile SITL parent](STABILIZED_MANUAL_SM5_GENERATED_SITL.md),
checks 406 source-graph anchors and the case copies of
[`RotationalFixture.cpp`](../Tools/research_sitl/stabilized_manual/RotationalFixture.cpp)
and [`FixtureConstants.hpp`](../Tools/research_sitl/stabilized_manual/FixtureConstants.hpp).
For each enabled truth row it matches `command_time` to exactly one PX4
`control.csv` publication and compares all four fixture targets to that
publication's canard commands. Across all 15,817 successive truth-row
transitions it independently reconstructs each normalized surface update
from the prior surface, current target, actual time step and
`1 - exp(-dt/0.05)`. It also checks that logged
surface angle equals normalized surface times the declared synthetic
`0.22689280275926285 rad` maximum deflection.

| Sealed case | Enabled rows with timestamped command match | Truth rows | Maximum target-to-surface gap |
| --- | ---: | ---: | ---: |
| Level, negative-first mixed axes | 6,479 | 7,909 | 0.238309 |
| Body-X-up, positive-first mixed axes | 6,474 | 7,910 | 0.238310 |

All 12,953 enabled rows matched a source command. The largest target mismatch
was below `5e-10` normalized units; the maximum reconstructed update error
was `1.11e-16`, and logged angle agreed with normalized surface at the
serialized precision. This verifies that the **synthetic fixture trace**
applied the declared lag and that its targets came from the PX4 control
publication. It does not establish a loaded physical servo's dynamics,
aerodynamic moment response, vehicle trim or free-flight stability. The
[compiled command-history probe](STABILIZED_MANUAL_SM5_ALLOCATOR_SEQUENCE.md)
and this native audit have different scopes: the former holds constant
incremental moment requests in the allocator, while these native cases run
pilot body-rate and attitude-hold commands with estimator feedback.

The 5-file audit receipt is
`/var/tmp/research-stabilized-actuator-lag-435cvsze`; its
`actuator-lag-manifest.json` SHA-256 is
`d4821661c7fdc0ea23960105b58ec1c6c666add1e319c75319c61cd33ccee127`.
Independent Windows rehashes found zero mismatches in it and the 88-file
native parent. The stock Cessna, H10 bench, negative-only C1E-N1 path and
accepted 31-case SITL scope remain unchanged. Actual canard, loaded-servo,
EDF/wash and CG/inertia measurements remain pending.
