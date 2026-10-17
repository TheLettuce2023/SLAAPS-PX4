# SITL Supervisor Broker Test Specification

Status: O1 offline oracle implemented and verified, 2026-09-19. The user selected the cooperative
trusted-local research-harness profile from
[SITL Supervisor Console Design](SITL_SUPERVISOR_CONSOLE_DESIGN.md). This
specification covers the first pure synchronous broker core only. It neither
claims hostile same-user isolation nor authorizes live MAVLink, keys, hardware,
arming, control publication, or physical operation.

## Scope And Evidence Layers

The candidate implementation is confined to
`Tools/research_sitl/supervisor/SupervisorBroker.hpp` and `.cpp`, exercised by
an isolated C++ test. It has no CLI, terminal/UI rendering, network/serial I/O,
MAVLink framing, key handling, background thread, timer, controller, PX4
startup, or actuator path.

The oracle records three strictly separate facts:

| Layer | What is proved | What is not proved |
| --- | --- | --- |
| Pure broker | Local/event floor arithmetic, opaque pending-ticket lifecycle, one-pending limit, request/attachment shape checks, and fail-closed result handling. | Authentication, OS peer identity, packet parsing, state truth, or adapter behaviour. |
| Owner fixture | The injected owner captured trusted state and dispatched exactly one real offline `WireAdapter` administration call, returning its actual `AdminResult`. | That a deployed state source, guidance ingress, or console transport is trustworthy. |
| Runtime excluded | Nothing. These capabilities must remain absent from the first source slice. | CLI/UI attachment, authentication, signing, live ingress, scheduling, restart persistence, PX4/SITL execution, control output, and hardware authority. |

The test uses only explicit non-operational fixtures. It must not invent IDs,
timing limits, session packet fields, map values, or an operational security
claim beyond the injected fixture values.

## Frozen API And Ownership

The API under test is exactly:

```cpp
SupervisorBroker(BrokerConfig, SupervisorOwner &)
bool attach(uint64_t operator_instance) noexcept;
bool detach() noexcept;
ConsoleResult prepare(Intent, const TrustedSession &) noexcept;
ConsoleResult confirm(uint64_t ticket) noexcept;
bool quarantined() const noexcept;
bool copyLastAdminResult(AdminResult &) const noexcept;
```

`SupervisorBroker` is noncopyable/nonmovable and has no reset API.
`SupervisorOwner::snapshot(OwnerView &) noexcept` and
`SupervisorOwner::submit(const Submission &, AdminResult &) noexcept` are the
only owner callbacks. The fixture owner, not the broker, owns the real
unchanged `WireAdapter`, continuous observation, guidance records and the one
serialization domain. The broker receives neither packet/parser evidence nor
an adapter pointer.

`attach(operator_instance)` is a cooperative-harness attachment seam, not an
authentication claim. The UI-facing result is exactly `ConsoleResult` with
`disposition`, `code`, `ticket`, `has_adapter_code`, `adapter_code`, and
`event_consumed`. It contains no `AdminResult`, state, epoch, event ID, map, or
parser evidence. `copyLastAdminResult` is a separate owner-diagnostic seam;
the UI test must not use it as a UI result.

`prepare` obtains one current owner snapshot, then creates the only opaque
pending ticket after all current-view checks. It never submits or allocates an
event. `confirm` consumes that matching ticket before its confirmation snapshot
or any owner submit. The next
event candidate is committed-supervisor-floor plus one, with first value one.
It is not committed until a validated `OwnerOutcome::Completed` result reports
the exact event as consumed. `OwnerOutcome::NotSubmitted` drops the ticket and
does not advance the event floor. `OwnerOutcome::Unknown`, an unconsumed
completed result, or an invalid completed result quarantines authority.

## Exact Result Rules

| Condition | Required disposition / broker code | Owner submit | Pending / floors |
| --- | --- | --- | --- |
| Zero `maximum_confirmation_age_us` | NotSubmitted / `NotConfigured` | none | unchanged |
| Detached, repeated attach, non-increasing attachment identity | NotSubmitted / `NotAttached` or attach false | none | unchanged |
| Invalid intent/session shape | NotSubmitted / `InvalidIntent` | none | unchanged |
| Valid prepare | NotSubmitted / `Ready` with nonzero ticket | no submit; one snapshot | local ticket floor advances only |
| Pending ticket on prepare | NotSubmitted / `Busy` | none | ticket retained |
| Wrong/zero ticket on confirm | NotSubmitted / `InvalidTicket` | none | correct ticket retained |
| Snapshot unavailable/not ready | NotSubmitted / `OwnerUnavailable` or `OwnerNotReady` | none | matching ticket consumed |
| Changed bound context | NotSubmitted / `ContextChanged` | none | matching ticket consumed |
| Inclusive age exceeded | NotSubmitted / `Expired` | none | matching ticket consumed |
| Owner returns NotSubmitted | NotSubmitted / `OwnerDeclined` | once | ticket dropped; event floor unchanged |
| Valid completed, consumed result | Submitted / `Submitted` | once | ticket dropped; event floor advances once |
| Completed but unconsumed result | Quarantined / `AdapterUnconsumed` | once | no floor advance |
| Unknown outcome | Quarantined / `OutcomeUnknown` | once | no floor advance |
| Impossible/mismatched completed result | Quarantined / `InvalidOwnerResult` | once | no floor advance |
| Counter exhaustion or authority discontinuity | Quarantined / `CounterExhausted` or `AuthorityDiscontinuity` | none | no wrap or replacement |

Every local failure has `has_adapter_code == false` and must not display a
default `AdminCode`. A validated completed result alone may set
`has_adapter_code`, copy the actual adapter code, and expose its actual
`event_consumed` value. Successful prepare returns its newly allocated ticket;
failed prepare returns zero. Every confirm result echoes its input ticket,
including rejection; that echo does not make a consumed ticket reusable.

## Required Oracle Cases

The completed test matrix must cover at least the following behaviours using
the frozen API names.

### Pure Broker Cases

- A valid prepare obtains exactly one owner snapshot, creates one nonzero
  opaque ticket, and advances only the local intent floor; it does not submit
  or allocate an event.
- A second prepare while a ticket is pending fails without changing either
  floor, owner-call count, pending ticket, or adapter state.
- First successful prepare binds one nonzero controller identity and requires
  no pre-existing supervisor floor. Missing identity/generation/transition/map
  epoch or inconsistent history/floor is `OwnerNotReady`. Later structurally
  valid competing controller/floor or owner-time regression quarantines with
  `AuthorityDiscontinuity`; it never silently binds a competing/restarted stream.
- Malformed/unknown intent, malformed requested session shape, clear carrying a
  session, and invalid cooperative attachment fail before owner capture. They
  leave every floor and pending state unchanged.
- Confirming a zero, stale, wrong, duplicate, or already-settled ticket fails
  before owner capture and cannot settle the real pending ticket. A correct
  live opaque value is intentionally bearer confirmation; a byte-for-byte copy
  before first use is indistinguishable and must be accepted once.
- A definitive returned result with `supervisor_event_consumed == true` settles
  the ticket, commits exactly one next event floor value, and reports the
  returned adapter code without rewriting it.
- A definitive returned result with consumption false settles according to the
  frozen API's fail-closed disposition but cannot advance the event floor,
  manufacture a later event, or expose an adapter code where none was returned.
- An owner completion that is unknown/fails to return quarantines the broker,
  consumes no further ticket, and forbids later prepare/confirm on that broker
  object. It never treats absence of a result as a false consumption result.
- Local and event counters start at their structural first values, never wrap,
  and become fail-closed at exhaustion. Ticket failure, cancellation, or a
  rejected adapter action cannot create an event-ID gap.
- Confirmation accepts an age exactly equal to
  `maximum_confirmation_age_us` and rejects one unit older using checked
  subtraction; it never computes an overflowing deadline. Its exact-context
  comparison rejects controller, generation, transition epoch, map epoch,
  session-history tuple, or supervisor-floor change while allowing time to
  advance within the inclusive age bound.
- Reentrant `attach`, `detach`, `prepare`, or `confirm` from an owner callback
  returns its frozen busy/failure result without mutating attachment, ticket,
  floor, or dispatch count.

### Owner Fixture And Actual Adapter Cases

Fixtures inject a real `WireAdapter` and verify its existing C0b.2 behaviour;
the broker oracle must not duplicate S1/session logic.

- Establish and revoke need an independently injected matching guidance record;
  clear dispatches without one. The negative fixture deliberately supplies a
  rejected ingress verdict to the actual adapter to exercise consumed rejection;
  this is fault injection, not a deployed authentication service. The broker never
  creates packet bytes, parser facts, or binding fields.
- The owner captures current trusted state and atomically binds the event to
  the resulting controller, generation, and transition epoch. A changed or
  invalid state makes the owner return its defined pre-dispatch outcome; no
  event is committed.
- A successful/rejected real administrative result is preserved verbatim for
  audit. Establish, revoke, and clear retain the existing adapter's session,
  latch, ACK, report, epoch, and consumption semantics.
- Completed-result validation rejects zero/out-of-range report count, a
  mismatched consumed supervisor ID, wrong establish/revoke packet correlation,
  nonzero clear packet correlation, incoherent session supervisor floor, or a
  uninitialized snapshot or consumed terminal result. These quarantine with
  `InvalidOwnerResult` and expose no adapter code. A coherent unconsumed terminal
  result instead yields `AdapterUnconsumed` with the actual returned code.
- Rejected action with a consumed supervisor event advances the broker event
  floor exactly once and cannot be retried through the settled ticket. A
  rejected action without consumption does not become a broker acceptance.
- Capacity/state/supervisor terminal results enter the defined broker
  fail-closed outcome. The test proves no second dispatch follows and no
  `RequestGuard`/adapter reconstruction occurs.
- The owner-call fixture counts snapshots and dispatches separately: malformed
  prepare and invalid confirm cause neither; valid prepare causes one snapshot
  and no dispatch; valid confirm causes one further snapshot and exactly one
  dispatch.
- `copyLastAdminResult` retains any `OwnerOutcome::Completed` callback value,
  including an invalid one, for owner diagnostics only. Before any completed
  callback and after a failed copy, its output sentinel remains byte-for-byte
  unchanged; UI results expose an adapter code only after result validation.

### Non-Claims And Regression Boundaries

- No test may assert authentication from the attachment. Under the selected
  cooperative profile, same-user hostile guidance isolation is explicitly
  outside the claim.
- No test may create a socket, serial port, MAVLink frame, key, thread, timer,
  CLI command, PX4 module, controller request, or actuator output.
- The broker must not suppress external trusted periodic state observation;
  that scheduling/runtime property is not implemented by this synchronous core.
  Owner fixtures may prove only that one confirmed transaction dispatches once.
- A zero-report adapter result creates no fabricated status/report evidence.
  The broker can retain its local audit disposition but cannot claim a PX4
  status emission.

## Verification Rules

Tests use byte sentinels for unchanged-copy and wrong-ticket broker-state cases,
and explicit field/call-count assertions elsewhere. They assert no mutation
unless the specified transaction step permits it. Each successful owner fixture records the actual
`AdminResult` and compares its reported consumption/correlation rather than
deriving one from an `AdminCode`.

The C++ oracle is a standalone host test only. It may use the existing
isolated research-acceleration library and C0b.2 fixtures but must not add root
build wiring or launch a simulator. A passing result is evidence for the
offline broker state machine only.

## Implementation Gate

See PHASE_GATE_STATUS.md for master acceptance and verification evidence.
SupervisorBrokerTest.cpp has 22 checks; SupervisorBrokerMatrixTest.cpp has 188.
These include helper assertions, not independent scenarios or coverage claims.
The matrix injects synthetic malformed and terminal callback records; the
separate adapter fixture exercises genuine establish/revoke/clear results.
Exact rejected-action session-gate correctness remains owner-authoritative;
the broker checks known enum values and exact successful-action gates.
There is no broker reset API. A later offline new-run fixture may construct a
new broker only with a new adapter/controller identity; it must refuse an
existing authority stream/floor rather than resume it. The implementation slice
remains limited to the two broker files plus an independently reviewed test and
minimal standalone registration. Existing wire/adapter/S1 files stay unchanged.
