# SITL Supervisor Owner O2 Design

Status: master and independent design and isolated implementation review PASS,
2026-09-19. Verification: 169 O2 assertions; 12/12 host-suite targets in strict
Debug, repeated Release and ASan/UBSan. See PHASE_GATE_STATUS.md for limitations.
The implementation assignment is recorded in PHASE_GATE_STATUS.md. No process,
timer, live ingress, MAVLink link, key, authentication provider, PX4 change,
controller, arming or output is released.

## Boundary

O2 is the smallest deterministic offline owner for the existing O1 `SupervisorBroker` seam, plus a redacted facade for `attach`, `prepare`, `confirm`, and `detach`.
It inherits the user-selected cooperative trusted-local harness profile from [the console design](SITL_SUPERVISOR_CONSOLE_DESIGN.md).
That profile does not claim that localhost, a same-user process, a fixture, or an attachment authenticates an operator.

The concrete owner contains exactly one noncopyable `research_acceleration::WireAdapter` by value.
It constructs it from an immutable `ContractConfig` and `AdapterIdentity`; it never receives a shared mutable adapter reference or keeps a shadow controller identity.
It neither reconstructs the adapter/`RequestGuard` nor mirrors eligibility, acknowledgement, session, map, report, or floor state.
The adapter remains authoritative.

O2 has no callbacks from a UI, no sockets, serial ports, parser, signer, clock acquisition, generated dialect, CLI/GUI framework, scheduler, or simulator-truth sensor input.
State, clock, and ingress provenance are offline scenario values injected explicitly into the owner.

## Offline Compile Boundary

These are the reviewed signatures implemented in the isolated host-only
`Tools/research_sitl/supervisor/HarnessSupervisorOwner.hpp`. Acceptance evidence
is recorded separately in PHASE_GATE_STATUS.md; they are not runtime APIs.

```cpp
namespace research_sitl {

struct OwnerHarnessConfig {
    research_acceleration::ContractConfig adapter_contract;
    research_acceleration::AdapterIdentity adapter_identity;
    uint64_t fixture_run_id;         // Required nonzero identity.
    uint64_t maximum_receipt_age_us; // Required nonzero fixture bound.
};

struct FixtureClockProvenance {
    uint64_t fixture_run_id;
    uint64_t observation_id;
    uint64_t validator_time_us;
    bool continuous;
};

struct FixtureStateReceipt {
    research_acceleration::TrustedStateSample sample;
    FixtureClockProvenance clock;
    uint64_t fixture_state_stream_id;
};

struct ImmutableIngressReceipt {
    uint64_t fixture_run_id;
    uint64_t receipt_id;
    uint64_t received_validator_time_us;
    research_acceleration::DecodedSessionV1 packet;
    research_acceleration::SessionIngressVerdict verdict;
};

class HarnessSupervisorOwner final : public SupervisorOwner {
public:
    explicit HarnessSupervisorOwner(const OwnerHarnessConfig &config) noexcept;
    bool tick(const FixtureStateReceipt &receipt) noexcept;
    bool offer(const ImmutableIngressReceipt &receipt) noexcept;
    bool snapshot(OwnerView &out) noexcept override;
    OwnerOutcome submit(const Submission &submission,
                        research_acceleration::AdminResult &out) noexcept override;
};

class SupervisorConsoleFacade final {
public:
    explicit SupervisorConsoleFacade(SupervisorBroker &broker) noexcept;
    bool attachFixtureOperator(uint64_t injected_operator_instance) noexcept;
    bool detachFixtureOperator() noexcept;
    ConsoleResult prepare(Intent intent,
                          const research_acceleration::TrustedSession &session) noexcept;
    ConsoleResult confirm(uint64_t ticket) noexcept;
};

} // namespace research_sitl
```

All configuration identity/bound values are mandatory, nonzero, and have no defaults.
An invalid underlying adapter contract also permanently disables administration.
`BrokerConfig::maximum_confirmation_age_us` remains the separate O1 fixture configuration.
The trusted fixture driver alone holds `tick` and `offer`; the facade owns no owner reference, fixture capability, adapter pointer, callback, `OwnerView`, `Submission`, `SupervisorEvent`, `AdminResult`, ingress receipt, parser verdict, event ID, report, map, acknowledgement, or cached request.
It returns the existing redacted `ConsoleResult` unchanged, rather than duplicating a result type.

## Deterministic Owner Turn

The caller has a one-thread serialization precondition across `tick`, `offer`, and every broker call.
Detected re-entry fails closed, but O2 provides no mutex, scheduler, or concurrency proof.
O2 creates no thread, event loop, timer, period, deadline, queue, or wall-clock policy.
The scenario values are not real-time state sensing or simulator truth.

`tick` requires a configured run ID, nonzero stream ID, nonzero strictly increasing observation ID, matching clock/sample validator time, and no repeated observation.
The first accepted stream ID binds the owner; any later stream change is a
permanent provenance fault, not an implicit new stream or reset.
Its validator time must not regress from the retained view; a regression is an administrative provenance fault.
It never repairs, advances, synthesizes, or reads a clock.
For a structurally usable receipt it calls `WireAdapter::poll(receipt.sample)` exactly once, then derives and retains `OwnerView` only from that actual poll result and `sessionSnapshot()`.

Its exact order is: receive one supplied state receipt; validate immutable provenance; poll once when structurally usable; retain the post-poll view/audit fact; return.
It does not examine console input or ingress evidence.
It remains callable while the broker is detached, pending, rejected, or quarantined.

A structurally invalid receipt permanently disables administrative submission for this owner instance, while later structurally valid ticks may still poll.
When run/time structure is coherent but fixture continuity is false, the owner passes the supplied sample unchanged to the poll and permanently disables administration even if a later receipt claims continuity.
Neither condition creates a sample, clears a latch, resets an adapter, or restores authority.

`snapshot` only copies the retained view; it never polls, ticks, fetches state, or manufactures time.
It returns false before a retained valid view or after permanent provenance disable.
`submit` only compares `Submission::expected_view` against the retained current view; it never polls, ticks, fetches state, or invokes a fixture callback.
The comparison includes ready/terminal, controller identity, generation, transition epoch, map epoch, validator time, session-history bit/value, and supervisor-floor bit/value.
Any difference returns `NotSubmitted`, with no adapter administration call, event consumption, or event-floor advance.

External ticks may occur between prepare and confirm; the ordinary O1 comparison then rejects the changed view.
No external tick can interleave the synchronous confirm snapshot/submit sequence.

## Receipt Immutability And Dispatch

`offer` is the sole ingress-fixture entry and owns one private receipt slot, not a queue.
It copies the offered value into private storage only after validation; neither the fixture caller nor facade can mutate the stored receipt.
For every nonzero offered receipt ID above the retained receipt-ID floor, the owner advances that floor before all acceptance checks.
Thus a rejected, repeated, refreshed, or full-slot offer can never later be accepted with that same ID.
The counter never wraps.

An offer enters the empty slot only when its run ID matches and checked subtraction proves its `received_validator_time_us` is no later than the retained view time and no older than `maximum_receipt_age_us`.
The age rule is `current_time - received_time <= maximum_receipt_age_us`; no deadline addition is used.
Future, expired, wrong-run, repeated, out-of-order, or full-slot offers are discarded without an adapter call.

For establish/revoke, `submit` requires the private slot to match intent,
session tuple, decoded operation, all four positive ingress-verdict checks,
and current controller, transition epoch and clock-map epoch. The session
packet has no state-generation field: generation is checked through the
broker event and retained view, never invented as packet metadata.
It repeats the checked receipt-age relation against the retained current view.
A mismatched or stale present slot is dropped and causes `NotSubmitted`.
A session packet must have zero ACK-only fields, flags and reserved bytes;
all identity/target fields required by the adapter remain nonzero. Its tuple
and map binding must match the retained fixture sample. Clear requires a zero
requested tuple and the event binding derived from actual session history.
A matching slot is dropped immediately before the one `WireAdapter::processSession` call, so it cannot be retried even if a result is later unavailable.
For clear, no receipt is examined and the owner calls only `WireAdapter::clearLatchedFault` with the broker-created event.

The owner never fabricates packets, parser/authentication verdicts, freshness, source/target IDs, session operation sequence, map epoch, state generation, or event ID.
It passes the immutable matching packet/verdict and broker-created event unchanged to exactly one existing adapter administration call.

The deterministic concrete owner returns `Completed` only after receiving the actual `AdminResult`, and otherwise `NotSubmitted`.
`Completed` does not mean an establish/revoke/clear succeeded; O1 validates correlation and event consumption.
An ordinary unknown completion is not expected from this direct offline call;
the internal invariant-failure exception below is fail-closed, not a fabricated
adapter success or a claim that the action did not occur.
Before returning Completed, the owner checks actual packet operation-sequence
correlation against its immutable consumed receipt. An impossible mismatch is
an internal invariant failure: permanently disable owner administration and
return Unknown so O1 quarantines, never repair
the result or retry. The direct unchanged adapter is expected to satisfy this
invariant; a deliberately corrupted result is a separate fault-injection test.

## Redacted Facade And Lifecycle

The facade forwards only to the O1 broker and returns `ConsoleResult`.
It has at most O1's one opaque pending ticket.
Detach clears that UI path only; it does not issue an action, stop a tick, revoke a session, clear a latch, reset any counter, or recreate an adapter.
A returned adapter code is diagnostic only and never asserts eligibility, physical execution, or future permission.

An O1 quarantine, exhausted counter, terminal adapter view, or permanent provenance disable closes O2 submission.
Ticks may continue for diagnostics, but no automatic retry, event reissue, reset, reattach workaround, or authority recovery exists.
There is no O2 reset API.
A new offline test run constructs new config, adapter, owner, broker, and input values with a different run identity; it cannot import an old ticket, receipt, session/ACK/request history, or guard state.

## Predeclared Tests

The implementation test must use this concrete owner with its real unchanged `WireAdapter`.

| Case | Required result |
| --- | --- |
| Construction/redaction | Mandatory nonzero owner inputs; facade exposes only existing `ConsoleResult` and no authority-bearing object. |
| Tick order | One supplied usable receipt produces exactly one adapter poll; no facade call polls or reads an input receipt. |
| Provenance | Repeated/regressed observation, wrong run/stream, time disagreement, and discontinuity disable administration permanently; later usable ticks may poll. |
| Retained view | No first tick or changed expected view gives `NotSubmitted` with no administrative adapter call or event consumption. |
| Receipt slot | Immutable copy, one slot, strictly increasing IDs, checked inclusive age, future/expired/full/reused drop, and no retry after dispatch. |
| Action separation | Matching establish/revoke receipt reaches exactly one real `processSession`; clear reaches exactly one real `clearLatchedFault` and uses no receipt. |
| Adapter preservation | Real establish, revoke, clear, and consumed rejection preserve actual `AdminResult`, report chronology, session gate, latch, and event semantics. |
| Detach/quarantine/restart | No implicit action; ticks remain possible; old run evidence cannot be resumed. |

These tests are offline fixture evidence only, not evidence of live parser authentication, clock accuracy, operator authentication, link reliability, control eligibility, SITL dynamics, hardware, or physical operation.

## Authority And Next Gate

No new human policy choice is needed: O2 remains under the selected cooperative trusted-local profile.
Live guidance/authentication, OS isolation, keys, TELEM routing, operational clock bounds, arming, control, outputs, and physical work remain separately blocked by the wire contract and roadmap.

The completed implementation release allocates isolated owner/facade source
and tests only. It does not release any live integration.
Externally injected ticks establish only scenario-time freshness. A future
live owner must acquire current trusted state/time independently; repeated
calls against a frozen tick must never be presented as runtime freshness.
It retains O1 and adapter source unchanged and uses only explicit offline inputs;
it does not release a CLI/GUI, live I/O, scheduling, MAVLink parsing, PX4
integration, simulation run, controller, or output.
