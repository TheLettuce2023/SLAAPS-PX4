# Cooperative SITL supervisor broker and offline owner (O1/O2)

Offline host-only C++ core. User-selected option 1 assumes cooperative local
processes; it is not an OS security boundary or a hardware authority mechanism.
No executable console, sockets, serial, timers, threads, PX4 module, arming or
actuator outputs are implemented here.

The trusted harness attaches an operator instance. The UI-facing workflow is
`prepare(intent, session)` then explicit `confirm(ticket)`; `detach()` cancels
pending consent without revoking or clearing anything. Establish/revoke need
independent ingress evidence held by the owner; a UI tuple cannot supply it.
Clear uses retained session history, not a user-supplied binding.

`SupervisorOwner` is an injected synchronous seam, not a deployed runtime.
It owns the real adapter, trusted observation/ingress and one serialization
domain. It must compare the complete final view before dispatch, verify exact
ingress correlation, and continue polling independently of console availability.
`NotSubmitted` guarantees no administration/event side effect. Unknown or
inconsistent completion quarantines the broker; there is no reset/retry API.
Consumed administrative rejection advances the event floor just like success.

Full API/limits: [contract](../../../docs/SITL_SUPERVISOR_BROKER_API.md).
Evidence: [phase gate](../../../docs/PHASE_GATE_STATUS.md).

`HarnessSupervisorOwner` is the O2 offline concrete owner: one by-value adapter,
explicit fixture state/clock ticks and a copied one-slot ingress receipt. The
fixture driver owns `tick` and `offer`; `SupervisorConsoleFacade` exposes only
operator attach/detach and prepare/confirm. Neither facade nor broker samples
time or polls the adapter. Receipt IDs are one-use even after rejection;
provenance faults permanently disable administration but do not stop later
coherent fixture polls. Construction of a new owner is a new offline run, not
a recovery mechanism for an existing session.

All calls must be serialized on one thread. This is not a live scheduler or
freshness service: frozen fixture time cannot establish runtime freshness.
See [O2 design](../../../docs/SITL_SUPERVISOR_OWNER_O2_DESIGN.md) for the complete
receipt, provenance and action boundary. The private test peer can pre-latch
the real adapter for fault scenarios; it is not a public recovery API.

## Reproduce from the repository root in WSL

```sh
task_build=$(mktemp -d /var/tmp/research-supervisor.XXXXXXXX)
cmake -S src/lib/research_acceleration -B "$task_build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-Wall -Wextra -Wpedantic -Werror"
cmake --build "$task_build"
ctest --test-dir "$task_build" --output-on-failure
```

Use separate directories for Release and sanitizer configurations. Sanitizer
flags used at O1: `-fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie`
with executable linker flags `-fsanitize=address,undefined -no-pie`.
The broker and owner are registered only under the standalone project's
`BUILD_TESTING`; neither is linked into PX4 firmware.

The 22-check test uses the actual unchanged adapter plus lifecycle fixtures.
The 188-check matrix mutates synthetic callback records for rejection/boundary
testing; it does not prove a deployed owner or authentication implementation.
The O2 test adds 169 assertions using the concrete offline owner and real
adapter, including lifecycle, provenance, receipt-age and binding boundaries.
All timing, IDs and physical bounds in fixtures are non-operational test values.
Tests do not verify acceleration tracking or complete the roadmap.
