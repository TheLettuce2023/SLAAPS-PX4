# SITL Supervisor Console Design

Status: design only, 2026-09-19. This document does not release an
implementation, a MAVLink dialect, a transport, a controller, a PX4 startup
change, or an authority to arm or actuate a vehicle.

Master and independent design review: PASS. The user selected option 1,
cooperative trusted-local SITL, on 2026-09-19. The bounded O1 broker core is
specified in SITL_SUPERVISOR_BROKER_API.md; its gate supersedes the historical
implementation hold below. Interactive UI and runtime owner remain unimplemented.

## Purpose and boundary

The proposed console is a **host-local, SITL-only** operator aid for the three
existing C0b.2 administrative intentions: establish a trusted session, revoke
one, and request a latched-fault clear. It is not an acceleration-request
producer, a remote command service, or a MAVLink endpoint. Its authenticated
local operator intent is the proposed SITL supervisor authority, but only a
separate serialized broker may turn that intent into an adapter action.

The only proposed handoff into the adapter is the already-defined typed
`SupervisorEvent` in
[`WireAdapter.hpp`](../src/lib/research_acceleration/WireAdapter.hpp). The
adapter remains the sole owner of `RequestGuard`, transition epochs, report
sequences, session gate, supervisor event floor, acknowledgement state, and
S1 calls. The console never edits an `AdapterSnapshot`, `SessionSnapshot`,
`AdmissionSnapshot`, `RequestGuard`, `TrustedClockMap`, or a cached request.

The console is inactive by default and has no permitted path to:

- arm, disarm, select a research mode, create an acceleration demand, or
  publish a controller/actuator value;
- establish signing, select MAVLink IDs, open serial/network I/O, load keys,
  generate a dialect, or alter a PX4 parameter/startup configuration;
- treat a received session packet, status, GUI state, or operator prompt as
  independent authorization.

The relevant adapter entry points are `processSession(sample, packet, ingress,
grant)` and `clearLatchedFault(sample, grant)`. They are deliberately offline,
typed entry points today; their existing administrative checks are specified in
[C0B2 session administration](C0B2_SESSION_ADMIN_DESIGN.md). The console does
not bypass the required packet-plus-grant conjunction for establish/revoke.

## Architecture

The console client and broker are separate roles. Exactly one serialized
`SupervisorConsoleBroker` owns the supervisor stream, validates a local
operator intent, captures current state, reserves a fresh `SupervisorEvent`,
and makes the one adapter call. The console client has no `SupervisorEvent`
or adapter capability. The broker's injected **offline adapter seam** is a test seam,
not a live adapter discovery mechanism:

```text
local operator -> Console client -> Broker -> injected state/adapter seam
                                              |                  |
                                              +-- audit result <- typed AdminResult
```

The broker is the only event-stream owner. Its adapter seam supplies one atomic
transaction against a fixed local adapter identity/configuration view. It does
not accept state values, event IDs, or parser verdicts from the UI. A later
implementation must keep the seam in a host-only test harness;
it must not connect the console to vehicle links or let a console process
fabricate trusted state.

The broker owns an in-memory, monotonic local intent ID for operator
correlation only. It is not the adapter's supervisor event ID. The broker,
not the console client, reserves the next positive `SupervisorEvent.event_id`
from the single ordered stream that the adapter validates. A client cannot
choose, rewind, reuse, inspect, or submit an event ID.

## Concrete proposed boundary

These are descriptive types, not a released source API:

```cpp
enum class LocalSupervisorIntent : uint8_t {
    EstablishSession, RevokeSession, ClearLatchedFault
};

struct LocalOperatorIdentity {
    uint64_t local_operator_instance_id; // injected nonzero local audit identity
};

struct LocalSupervisorRequest {
    uint64_t local_intent_id;            // owner-assigned, starts at 1, no wrap
    LocalSupervisorIntent intent;
    TrustedSession requested_session;    // required only for establish/revoke
};

struct AuthenticatedGuidanceSession {
    DecodedSessionV1 packet;
    SessionIngressVerdict authenticated_ingress;
    bool fresh_for_transaction;
};

enum class ConsoleDisposition : uint8_t { NotSubmitted, Submitted, Quarantined };

struct ConsoleResult {
    uint64_t local_intent_id;
    ConsoleDisposition disposition;
    bool has_adapter_code;              // false: adapter_code must not be displayed
    bool event_consumed;
    AdminCode adapter_code;
};

```

The only public design boundary is conceptually
`SupervisorConsoleBroker::handleIntent(LocalSupervisorRequest) ->
ConsoleResult`. Internally it is one owner-domain transaction, not a chain of
callable state/grant/submit APIs. The owner obtains a coherent trusted sample,
computes the checked post-observation epoch (`1` on first observation, next
epoch for a proved generation transition, otherwise the retained epoch), binds
and reserves the immediate event, and invokes the one existing adapter method.
The UI never sees the sample, computed epoch, reservation, or event.

The complete AdminResult is broker-internal audit evidence, not a UI return
value. The UI receives the redacted ConsoleResult only; NotSubmitted has no
adapter outcome, and Quarantined may have none if completion is unknown.
has_adapter_code is set only from an actual returned AdminResult, never from
a default enum value. A separately redacted state display is diagnostic only.
LocalOperatorIdentity comes from the selected broker channel/OS peer context,
never an identity claimed in the UI request. Offline fixtures explicitly inject
that evidence and do not claim to authenticate a real local operator.

For establish and revoke, the broker accepts an
`AuthenticatedGuidanceSession` only from an independent authenticated guidance
ingress. The console never creates a `DecodedSessionV1`, sets a parser verdict,
or supplies source/target IDs, epochs, map epochs, or operation sequences.
Offline fixtures may explicitly inject a synthetic authenticated packet; that
test seam is not a live-console architecture claim. For clear, the broker
calls only `clearLatchedFault`; there is no packet.

AuthenticatedGuidanceSession is an immutable internal ingress record, not a
public bag of caller-set trust bits. Only the ingress owner supplies its parser
authentication/binding evidence and owns packet freshness/replay checks.
The broker can consume a matching record once; it cannot set freshness true,
extend its lifetime, retry it, or turn a console click into ingress evidence.
Concrete ingress lifetimes and real authentication remain a later live gate.

## Intent validation and sequencing

The broker handles one request at a time within the single adapter-owner
serialized executor. Periodic observation and every broker/ingress adapter call
share that same serialization domain; there is no second broker mutex that
would permit polling or another state/action call to interleave between the
final sample, reservation and administrative call. If the transaction cannot
reach the adapter in that turn, it
emits a local `not submitted` audit result and drops the pending UI intent. It
never queues an old state sample, packet, or event for later use.

Trusted periodic state observation is a separate adapter-owner activity and
continues during UI delay, disconnect, malformed intent, authorization refusal,
or broker quarantine. A console rejection never suppresses polling, expiry,
revocation, or barrier observation. UI confirmation and any local display I/O
occur outside the owner-domain transaction; the broker admits at most one
already-confirmed intent into a bounded queue and performs no blocking I/O
while it holds the serialization domain.

The required order for every request is:

1. Reject a malformed local intent (zero local ID, unknown enum, wrong
   session shape, or clear carrying a session) locally, with no broker action.
2. Authenticate and authorize the locally attached operator intent according
   to the selected cooperative-harness policy. A UI string, localhost address, or same
   operating-system user is not authentication or authorization.
3. For establish/revoke, require one fresh independently authenticated guidance
   session packet that exactly names the selected action/session. For clear,
   require no packet. Missing or stale guidance stops before reservation.
4. Within the owner-domain transaction, obtain a coherent trusted sample.
   Missing, discontinuous, structurally invalid, or terminal state stops before
   reservation and before an adapter call.
5. Immediately before the one adapter call, reserve the next event ID and bind it
   to that just-sampled controller, generation, epoch, action, and session.
   Reservation alone does not advance the committed broker event floor.
6. Call the matching existing adapter method once. On a returned `AdminResult`, commit the reservation
   only when `correlation.supervisor_event_consumed` is true. A returned false
   result retains the identical reservation as unresolved; the broker emits no
   later event and requires a harness resolution rather than creating a gap or
   guessing whether the adapter observed it. A transport/process failure with
   no result likewise quarantines the broker immediately.

The broker binds every reservation to the post-observation adapter state:
`expected_controller_instance_id`, `expected_state_generation`, and
`expected_transition_epoch` must match the sample/adapter observation current
for that turn. It supplies a binding for establish/revoke and follows the
existing clear rule: no binding only before any successful session; otherwise
the exact retained session. It never derives a grant generation or epoch from
an old status display.

The adapter remains authoritative for the detailed ordering: its state
observation occurs before packet/action rejection, consumes an immediate valid
event as defined by its session contract, reserves report capacity before
mutation, and may reject after consumption. The broker records that distinction
rather than reinterpreting `AdminCode` as an event failure.

## Lifecycle, loss, and restart

Console startup begins **disconnected and non-authoritative**. It displays no
remembered session as current until a successful fresh broker state read. It
does not replay prior local intents, grants, `AdminResult`s, or packet values.

Closing the local UI, losing its attached terminal, losing the broker's adapter
capability, an
owner restart, a counter fault, an adapter terminal state, or an unknown/stale
state observation closes the console's submission gate. It clears pending UI
confirmation and shows retained audit history as historical only. It does not
revoke, clear a latch, or otherwise call the adapter merely because the
console disappeared; that would make availability a command source.

After restart, the owner obtains a new injected local audit identity and starts
its local intent sequence at one. This has no relationship to adapter event,
report, session, request, status, or epoch counters. The trusted supervisor
stream itself must preserve its own monotonic floor across a console restart;
if it cannot prove continuity, the broker refuses grants until its explicit
SITL harness reset procedure creates a new adapter instance.

That reset is an operator-requested new test run, with a new controller
instance identity and cleared transport queues. It archives the old run as
terminal and never resumes its session/ACK/request history under the old
identity. UI reconnect alone never performs a reset.

If `AdminResult` reports `CounterExhausted`, `TrustedStateFault`, or
`SupervisorStreamFault`, the submission gate closes permanently for that
console attachment. If a result has zero reports, it creates no status claim.
If adapter reports terminal state, subsequent local displays are diagnostic
only. No console path reconstructs the guard or adapter to erase a latch.

## SITL trust-model profiles

The design supports exactly one selected profile for a harness instance:

- **Cooperative trusted-local research harness:** all local processes are in
  scope as cooperative test components. This is not authentication or hostile
  same-user guidance isolation. Supported guidance interfaces expose no event
  creation or direct adapter call; malicious bypass by another same-user
  process is outside this profile's claims.
- **OS-isolated operator authority:** the broker has a distinct host operating-
  system principal and accepts intents only through an OS-supported local
  peer-credential boundary for a configured operator principal. Loopback,
  process names, UI labels, and same-user execution are not authorization.

In both profiles, the supported guidance role is unprivileged: it may contribute an authenticated
session packet through the separate guidance-ingress seam, but cannot submit
an operator intent, create an event, choose a binding, or advance a floor.
Conversely, the broker cannot create a guidance packet or declare parser
authentication. Both profiles are SITL-only and do not authorize live MAVLink,
hardware, or physical operation.

## Operator interaction model

The local surface presents only three explicit choices and a read-only current
state summary supplied by the broker. Each action requires an operator review
of its exact intent and session tuple where applicable. The surface must show
that a submitted request can be rejected after state observation and that an
accepted administrative result still does not arm, open the ACK barrier, admit
a data request, publish control, or prove physical execution.

There is no auto-establish, auto-revoke, auto-clear, retry loop, remote API,
clipboard/import script, status-triggered action, or response to producer
guidance. A received remote message may be displayed only as untrusted
diagnostic text and may never enable a button or satisfy authorization.

## Offline test oracle

An eventual standalone test fixture injects broker-owned trusted state,
guidance, and adapter seams and uses the actual `WireAdapter` / `RequestGuard`
offline objects. It asserts:

- establish/revoke require the exact broker-authorized matching grant and
  current state; a console-entered tuple alone makes no adapter call;
- clear reaches `clearLatchedFault` only with a broker-issued clear grant and
  preserves the adapter's unlatched, armed, and binding rejections;
- one console turn causes at most one adapter administration call; duplicate
  clicks, stale confirmations, disconnect/restart, and grant duplication do
  not replay a supervisor event;
- state generation, identity, map/session binding, observer continuity, and
  terminal/counter faults block before submission; no UI cache is substituted;
- valid grants consumed by an adapter rejection are reported as consumed and
  never retried; invalid/missing grants are not misreported as adapter
  consumption;
- reports/epochs/session gate/ACK/cache match the existing C0b.2 tests, and
  no operation creates an eligible request or an actuator/controller output;
- status/audit correlation retains local intent ID separately from the adapter
  event ID and packet operation sequence, with no fabricated report for a
  zero-report result.
- confirmation against a changed controller, generation, epoch, map or selected
  session is rejected without reserving an event; a dropped UI intent never
  reappears after reconnect. A new test run cannot reuse the old controller identity.
- a deliberately stalled console or audit consumer never delays trusted-state
  polling; bounded queue overflow is visible and cannot turn into a supervisor action.

## Human decisions still required

The user has selected the cooperative trusted-local research harness. This
does not provide hostile same-user isolation. Event ownership, atomic transaction, fail-closed
reservation handling, reset/lifetime semantics, and audit behavior are fixed
engineering requirements of this design, not additional policy choices.

They do not authorize live TELEM, MAVLink signing, hardware, arming, PX4
control, or physical testing. Those remain blocked by the deployment decisions
in the wire contract and the later plant/control gates.

## Proposed next gate

This design may be reviewed independently. The smallest possible later slice
is an offline broker state-machine test with fake trusted-state and guidance
seams; it would exercise no live I/O and would not modify
`WireAdapter`, `RequestGuard`, wire codecs, MAVLink, PX4 modules, or control
paths. O1 approval/evidence is now recorded separately in PHASE_GATE_STATUS.md.
