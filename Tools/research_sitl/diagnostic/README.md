# Offline diagnostic evidence

Standalone Linux host tests only. Nothing here opens a vehicle link, selects
Offboard, arms, or publishes control/actuator output. No packages are required
beyond the existing C++17/CMake/Ninja/Python standard-library toolchain.

Build from the repository root in WSL, using a new native build directory:

```sh
cmake -S src/lib/research_acceleration -B /var/tmp/<unique-build> -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DRESEARCH_SITL_DIAGNOSTIC_TESTS=ON
cmake --build /var/tmp/<unique-build> -j 4
ctest --test-dir /var/tmp/<unique-build> --output-on-failure
```

The option is OFF by default. Existing build guards exclude PX4, firmware,
hardware, lockstep and timestamp-proxy targets. Do not remove them to promote
these fixtures into a live owner.

## Evidence path

`DiagnosticCampaign.cpp` runs the actual single owner, broker and adapter.
An audit-only forwarding wrapper captures real submissions/results; it makes
no admission decisions. Effective typed grants reconstructed from submissions
and immutable offered receipts are explicitly labelled `owner_derived`, not
separately observed. All state, clocks and authentication facts are synthetic.

`DiagnosticJson.cpp` serializes complete typed inputs and immutable per-report
snapshots. Every 64-bit signed/unsigned value is a decimal string. Local enums
use exact names; unknown enums and nonfinite floating-point values fail export.
Decoded wire numeric fields retain their wire numeric representation. The
fixed profile does not include nonfinite input vectors; native guard tests do.

The executable writes its JSON only after synchronous detach has completed.
Its `--sink-test` mode independently checks that the actual owner is closed
before and while a controlled evidence sink is blocked, and that a missing
sink cannot yield a complete campaign. Exit zero in that mode means the sink
isolation assertions passed, not that a missing artifact became valid evidence.

`run_campaign.py` stages the exact executable in a new retained directory,
records source/executable hashes before and after execution, checks its build
cache/source association, then adds artifact bindings to the header. It does
not rewrite input facts, outcomes or reports. It writes `campaign.json` and
`manifest.json`, runs the independent checker, and retains failures/partial
output instead of overwriting earlier evidence. Timeout is a bounded test
runner limit, not a command, control or link timing requirement.

```sh
python3 -B Tools/research_sitl/diagnostic/run_campaign.py \
  --executable /var/tmp/<unique-build>/diagnostic_campaign \
  --build-dir /var/tmp/<unique-build> --source-root .
```

An optional `--output-dir` must name a new directory with an existing parent.
Without it, a uniquely named retained directory is created under the build.
Artifact/source hashes detect mismatches; they are not cryptographic proof that
arbitrary supplied source produced an arbitrary executable. The recorded fresh
build and unchanged-input audit supply that association for accepted runs.
Compiler identity is recorded; toolchain/system-library bytes are not attested.

## Strict finite profile

Schema: `research-c1b-offline/v1`. Profile: `c1b-owner-reference/v1`.
Top-level keys are `schema`, `header`, `trace`. The runner-bound header records
domain, synthetic provenance, owner/run/controller identity, full configuration
and source/executable/build artifacts. Calls contain full API inputs, actual
outcomes, optional actual adapter result/submission/owner outcome and labelled
derived turns. `trace.end` must match the complete fixed chronology.

`check_campaign.py` independently constructs the expected state transitions and
arithmetic; it does not import C++ predicates or learn expected results from
exported data. Its executable schema checks every field/type/key, report order,
operation index and snapshot. Duplicate keys, unknown variants, truncation,
wrong hashes and a shortened trace with a rewritten footer fail.

The reference has 20 calls and 19 reports: explicit operator attachment,
observation, enable, establishment, ACK, one validated request, no-demand access,
duplicate rejection, producer-regression latch/closure, authorized clear,
idempotent disable, detach, and an unchanged Flight-access output sentinel.
ACK interval [9958, 9962] and request age 12 microseconds are synthetic arithmetic
checks, not measured link behavior. Poll/admin success never counts as request
validation. All diagnostic reports remain ineligible for control.

```sh
python3 -B Tools/research_sitl/diagnostic/check_campaign.py \
  /var/tmp/<evidence>/campaign.json \
  --manifest /var/tmp/<evidence>/manifest.json --source-root .
```

`test_check_campaign.py` obtains an actual exporter baseline in the registered
integration test. Semantic corruptions are rehashed before checking, so a stale
file hash cannot substitute for detecting malformed evidence. Artifact-negative
filesystem mutations use disposable synthetic files, never repository files.
The optional standalone unittest mode uses only synthetic artifacts and claims
no C++ execution.

This oracle accepts exactly one profile. It is not an arbitrary-trajectory or
all-matrix JSON replay engine. The native guard/adapter/owner suites and
`docs/C1B_OFFLINE_EXECUTION_LEDGER.md` supply the broader finite matrix coverage.
Neither those tests nor this evidence establish live PX4 input provenance,
MAVLink signing, mode/control integration, acceleration tracking or flight safety.
