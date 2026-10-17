# C1H output-disabled host experiment

This approved experiment measures an isolated MAVLink loopback path in the existing
Ubuntu 24.04 WSL environment. Both endpoints bind `127.0.0.1` on OS-assigned ports.
It has no PX4, Gazebo, QGC, TELEM, serial, hardware, controller-objective or actuator
connection. Read the [reviewed design](../../../docs/C1H_HOST_LOOPBACK_DESIGN.md)
for the authority boundary and predeclared criteria.

One native context contains the existing owner's single by-value admission guard.
Generated MAVLink code verifies signatures and CRCs before admission; replay state
is committed only after current context checks. Establishment and revocation need
independent operator prepare/confirm actions. The sender acknowledges an actual
returned barrier before requests can become eligible. Guidance receives no native
owner or operator handle. This is cooperative same-process capability separation,
not protection against hostile same-user code.

Arm/mode/state changes and acceleration bounds are explicitly synthetic test
inputs. Request access only returns logged host data. Python and native code use
the qualified shared `CLOCK_MONOTONIC` domain: identity offset and `U=1 us` describe
integer representation uncertainty, not physical-clock accuracy, scheduling bounds,
Pi synchronization or an operational clock qualification.

## Run from the repository root in WSL

Use the existing installed Python, CMake, C++ compiler and generator dependencies.
The entry point checks pinned MAVLink/pymavlink revisions and cleanliness, validates
the private XML schema, generates bindings, builds the standalone library and runs
its host checks. A missing dependency or pin mismatch is a failure; these commands
do not install packages.

```sh
python3 -B Tools/research_sitl/live/run_host_loopback.py --build-only --build-type Debug
python3 -B Tools/research_sitl/live/run_host_loopback.py --run-live --build-type Release
```

Select exactly one of `--build-only` or `--run-live`; `Debug` is the default build
type. Build-only runs prerequisite checks without the live campaign. Live mode
creates fresh in-memory signing material and exercises nominal traffic, faults,
latch clearing, stale requests and restart. It performs no firmware/simulator build
or repository publication.

The nominal campaign records its local GC policy: defer cyclic garbage collection
for a short window, retain at most 20,000 trace rows, and restore the prior GC
setting in `finally`. Reference counting remains active. Exceeding two seconds
fails the campaign retrospectively; it is not a preemptive timeout or worst-case
real-time guarantee. Request deadlines and scoring limits remain unchanged.
Adding `--diagnostic-gc` observes GC intervals under this recorded policy; it does
not measure the earlier policy with cyclic collection enabled throughout.

## Retained evidence

Each invocation prints its owned `/var/tmp/c1h_host_*` directory. `checks.json`
retains prerequisite results; completed or failed live attempts retain `trace.json`
and `report.json`, with source/trace hashes and clock provenance. Generated bindings
and build artifacts remain available there. Preserve failed runs alongside later
runs; a functional pass does not imply timing passed.

Recheck a recorded trace without opening a link:

```sh
python3 -B Tools/research_sitl/live/check_live_evidence.py /var/tmp/c1h_host_EXAMPLE/trace.json --mutations
```

Replace the example directory with the printed path. The checker validates nominal
evidence completeness, correlation and timing; the runner separately verifies
functional scenario completion. `--mutations` tests corrupted copies without
changing the artifact. Exit code `1` can represent a legitimate measured timing
miss: inspect the printed result and `misses`. Missing or malformed evidence also
fails. Host results do not establish flight control, plant validity or operational
real-time performance.
