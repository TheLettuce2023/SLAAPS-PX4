# C0b.2 Session Administration Design

## Scope

This is a design-only extension to the accepted, permanently closed C0b.2
state core. It proposes session establishment, revocation, and latched-fault
clear administration using the same `WireAdapter`, immutable configuration,
and single private S1 `RequestGuard`. It does not approve implementation.

It does not add data-request admission, epoch acknowledgement, clock mapping
arithmetic, MAVLink framing or dialects, authentication implementation, status
transport, uORB, controller or actuator output, SITL, firmware build, or live
I/O. Operation three in the frozen session payload is deliberately unsupported
in this slice.

The existing `observeTrustedState`, `poll`, and `getEligibleRequest` API and
their `AdapterResult::reports[2]` behavior remain exactly as accepted. They
remain closed to admission.

## Proposed API And Immutable Inputs

The future header extends the existing class; it neither creates a second guard
nor accepts an externally constructed guard or mutable configuration view.

```cpp
struct DecodedSessionV1 {
    uint64_t controller_instance_id;
    uint64_t transition_epoch;
    uint64_t clock_mapping_epoch;
    SourceId source;
    SessionId session;
    uint64_t operation_sequence;
    TimeUs producer_acknowledgement_monotonic_us;
    uint64_t acknowledged_status_sequence;
    uint32_t protocol_version;
    uint8_t target_system;
    uint8_t target_component;
    uint8_t operation;
    uint8_t flags;
    uint8_t reserved[4];
};

struct SessionIngressVerdict {
    bool mavlink2;
    bool parser_authenticated;
    bool configured_header_channel_source_binding;
    bool configured_target_binding;
};

enum class SupervisorAction : uint8_t { Establish, Revoke, ClearLatchedFault };

struct SupervisorEvent {
    uint64_t event_id;
    uint64_t expected_state_generation;
    uint64_t expected_transition_epoch;
    uint64_t expected_controller_instance_id;
    SupervisorAction action;
    bool has_session_binding;
    TrustedSession session;
};

enum class SessionGate : uint8_t {
    NoTrustedSession, Established, Revoked, EstablishmentRequired
};

enum class AdminCode : uint16_t {
    Observed, Completed, NotConfigured, TrustedStateFault, CounterExhausted,
    SupervisorMissing, SupervisorDuplicate, SupervisorStreamFault,
    MalformedPacket, IngressRejected, UnsupportedOperation,
    PacketOperationReplay, BindingMismatch,
    EstablishmentContextRejected, ContextRejected, NoEstablishedSession, ClearNotLatched,
    S1ActionRejected
};

enum class AdminActionKind : uint8_t {
    None, EstablishTrustedSession, Revoke, ClearLatch
};

enum class AdminReportKind : uint8_t {
    ExogenousBarrier, AdministrativeBarrier, Operation, Terminal
};

struct SessionSnapshot {
    SessionGate gate;
    bool has_last_successful_session;
    TrustedSession last_successful_session;
    bool has_wire_operation_floor;
    uint64_t wire_operation_floor;
    bool has_supervisor_floor;
    uint64_t supervisor_floor;
};

struct AdminCorrelation {
    bool has_packet;
    SourceId packet_source;
    SessionId packet_session;
    uint64_t packet_operation_sequence;
    bool has_supervisor_event;
    uint64_t supervisor_event_id;
    bool supervisor_event_consumed;
};

struct AdminReport {
    uint64_t report_sequence;
    uint64_t transition_epoch;
    TimeUs validator_time_us;
    AdminReportKind kind;
    AdminCode code;
    bool has_s1_decision;
    Decision s1_decision;
    AdminCorrelation correlation;
};

struct AdminResult {
    AdminCode code;
    bool admission_closed;
    AdapterSnapshot snapshot;
    SessionSnapshot session;
    bool has_prestate_s1_decision;
    Decision prestate_s1_decision;
    bool has_action_s1_decision;
    Decision action_s1_decision;
    bool action_performed;
    AdminActionKind action_kind;
    AdminCorrelation correlation;
    uint8_t report_count;
    AdminReport reports[3];
};

AdminResult processSession(const TrustedStateSample &sample,
                           const DecodedSessionV1 &packet,
                           const SessionIngressVerdict &ingress,
                           const SupervisorEvent *grant);
AdminResult clearLatchedFault(const TrustedStateSample &sample,
                              const SupervisorEvent &grant);
SessionSnapshot sessionSnapshot() const;
```

`DecodedSessionV1` is an immutable value supplied only after C0a decoding. Its
complete fields obey the frozen 76-byte table: fixed protocol, nonzero
controller/transition/map/source/session/target identifiers, operation one,
two, or three, zero flags and reserved bytes, and the C0a logical-length rule.
The administrative API never normalizes or edits this object. A defensive
implementation still rejects a typed object that violates those invariants.

`SessionIngressVerdict` is a trusted boundary result, not packet-controlled
authority. `configured_target_binding` is separate because C0a establishes
only nonzero target IDs, not that they select this PX4 endpoint. All four
verdicts must be true for a packet action. Authentication, configured sender
binding, target routing, and supervisor authorization remain separate facts.

`AdminCode`, `AdminReportKind`, `AdminCorrelation`, and `AdminReport` are local typed administration evidence only.
They do not allocate or redefine any frozen wire decision value. Ordinary
state-core methods retain `AdapterCode` and `AdapterReport` unchanged. A
terminal supervisor-stream fault stores the existing
`AdapterCode::TrustedStateFault` in the shared wrapper terminal state, so a
later ordinary call reports that existing terminal code; the triggering
`AdminResult` retains the more specific `AdminCode::SupervisorStreamFault`.
No S1 enum or source change is needed.

`AdminCorrelation` always identifies the supplied packet tuple when one was
provided and the supplied grant ID when one was provided. Its consumed flag is
true only after an exact-next well-formed grant commits the supervisor floor.
Barrier reports use a zero correlation; the final operation or terminal report
carries the result correlation. `action_performed` means the named S1 action
was actually invoked, not that it succeeded. Establish and clear therefore
also expose their returned S1 decision. Revoke invokes S1 `revoke()` but that
method returns `void`, so its action decision is absent and no synthetic
`Decision` is created.

`sessionSnapshot()` reads retained wrapper state only. It performs no poll,
does not advance any floor, and never changes a gate. Historical closed-only
session slice always returned admission_closed=true. After the accepted
C0b.2 ACK/request extension, AdminResult.admission_closed reflects the actual
ACK gate: successful administration closes it, but rejected administration
may preserve an open ACK. Established session state alone is not ACK eligibility.

## Session And Replay State

The adapter retains one session gate and the most recently successful trusted
session. At construction the gate is `NoTrustedSession`. A successful
establishment sets `Established`; a successful administrative revoke or a
trusted map source/session rebind sets `Revoked`; a successful S1 latch clear
sets `EstablishmentRequired`.

The retained last successful tuple is not a growing session history. It is
needed because S1 itself rejects an establishment using the same session ID
after a revoke or clear. The adapter prechecks that case too, then still relies
on the returned S1 decision as the authority. A later successful different
session replaces this one retained tuple; no older-session blacklist is added.

The packet operation floor is one `has_wire_operation_floor` bit plus one uint64
value for the immutable controller-instance lifetime. The first successful
operation sequence may be zero, as frozen C0a permits. After the first
successful establish or revoke, every later successful administrative packet
operation must be strictly greater. A duplicate or lower packet operation is
rejected. The floor never resets on session change, map change, revoke, clear,
barrier, S1 latch, or disarm/rearm. Rejected packets, rejected S1 actions, and
unsupported operation three never advance it.

An exogenous trusted-state generation change uses the established state-core
comparison rules. A source/session binding rebind in an otherwise valid new
map generation closes active eligibility and changes an `Established` gate to
`Revoked`; ordinary state-core calls must make that update too, without
changing their two-report public behavior. An explicit revoke afterwards is
rejected as `NoEstablishedSession`. Other state transitions retain the gate
unless their structural fault terminals the wrapper.

The wrapper retains an `AdminCode` terminal cause separately from its existing
shared `AdapterCode` terminal cause. Supervisor-stream and structural-state
terminals map the shared cause to `AdapterCode::TrustedStateFault`; capacity or
epoch exhaustion maps it to `AdapterCode::CounterExhausted`. A terminal
administration repeat returns the retained local admin cause and snapshot with
no new report, floor movement, or S1 call. A subsequent ordinary state-core
call returns that corresponding shared terminal code. If an ordinary method
caused the terminal first, a later admin call maps its shared first cause to
the stable local `TrustedStateFault` or `CounterExhausted` respectively. This
preserves the existing ordinary API while retaining admin-specific evidence.

## Supervisor Stream

The supervisor source is a trusted, lossless, ordered stream. Its first valid
event ID is one. A retained floor has an explicit presence bit, so zero is not
a valid first event. If a floor exists below `UINT64_MAX`, only its checked
immediate successor is valid. `UINT64_MAX` may be consumed once; after that no
further event can be a successor and any supplied event terminally faults the
supervisor stream without wrapping.

A zero ID, skipped ID, out-of-enum action, or malformed trusted event shape is
a terminal supervisor-stream fault. An older/duplicate event is a nonterminal
local rejection and does not move the floor. For an exact-next well-formed
event, the adapter advances the supervisor floor before checking packet
authentication, packet replay, state/action binding, allowed context, or S1
action outcome. Thus a trusted grant is single-use even if its matching packet
is bad or the action is denied. A missing grant is a nonterminal rejection and
does not move the stream.

Malformed shape means an unknown action, zero expected controller/generation/
transition epoch, a present session binding with either tuple member zero, an
absent binding with either tuple member nonzero, or an establish/revoke action
without a binding. Clear permits either structurally valid binding shape;
whether it matches retained history is a later `BindingMismatch`, not a stream
fault. Nonzero wrong identities/epochs/generation or a well-formed wrong action
likewise remain consumed binding rejections. Malformed shape precedes duplicate
classification. The post-maximum terminal rule is the explicit exception to
the ordinary older/duplicate rejection rule.

An establish or revoke grant must carry `has_session_binding = true` and its
tuple must equal the packet source/session. A clear grant has no packet. Before
any successful session has existed, it must carry no tuple, represented by
`has_session_binding = false` and zero source/session. After a successful
session has existed, a clear grant must carry exactly the retained last
successful tuple. These shapes allow an initial validator-clock latch to be
cleared without inventing a session, while preventing an unrelated prior
session from being silently cleared.

The grant, not the frozen wire packet, binds `expected_state_generation`.
After any exogenous observation is committed, the packet and grant must both
name the resulting current transition epoch; the grant also names the resulting
current state generation and immutable controller identity. The frozen grant
has no map epoch: only the packet binds `clock_mapping_epoch` to the current
trusted map. A stale pre-observation epoch is rejected after its otherwise
valid exact-next grant has been consumed. No packet/grant mismatch invokes an
S1 administrative action beyond mandatory trusted observation, polling, or
safety revocation.

## Serialized Administration Order And Reports

Administrative entry points are serialized with the state core and call a
private state-observation helper directly; they never recursively call a public
ordinary operation. The exact ordered checks are:

1. invalid construction or retained terminal returns without a poll;
2. structural trusted-state validation; its fault revokes and terminals before
   event parsing;
3. supplied grant stream shape and ID classification; zero, gap, post-maximum,
   or malformed shape terminally faults before any state/barrier mutation;
4. whole-call capacity and epoch reservation; and then
5. commit a valid exogenous barrier, consume an exact-next grant, perform the
   mandatory prestate S1 poll, and decide packet/action predicates.

An older supervisor event is nonterminal `SupervisorDuplicate`, not a terminal
stream shape. It does not advance the floor or invoke an S1 administrative
action, but after the required prestate poll it gets an attributable final
operation report. Missing grant is likewise nonterminal. Thus a structural
state fault takes precedence, while a supervisor terminal at a new generation
emits one terminal record with no exogenous epoch advance.

As in the state core, structural/stream terminals use one available normal
report ID with no S1 decision. If none remains, the sole terminal report uses
UINT64_MAX and both retained causes become CounterExhausted instead. Capacity
or epoch exhaustion directly uses that reserved terminal ID. Terminal safety
revocation is required, but no S1 poll or administration is performed. Repeated
terminal calls emit no report and preserve the first retained cause.

Capacity is checked without consuming unused IDs. On unchanged state the
administrative worst case is two records and one epoch; on first or contiguous
new generation it is three records and two epochs, even if a later packet or
action is rejected. If this check fails, the wrapper revokes and terminals using
the existing counter-exhaustion policy before a barrier, floor consumption, or
S1 action. Actual chronological reports commit the normal sequences, remaining
contiguous except for the permitted terminal jump to `UINT64_MAX`.

The committed normal cases are exact: state transition plus successful action
emits three reports and advances two epochs; state transition plus rejected
action emits two reports and advances one epoch; unchanged state plus successful
action emits two reports and advances one epoch; unchanged state plus rejected
action emits one report and advances no epoch. Initial baseline plus a
successful action therefore installs epoch one then administrative epoch two.
A pre-mutation terminal emits one terminal report and advances no epoch. These
counts do not consume identifiers reserved only for a possible action.

The prestate decision is returned separately in `AdminResult` and is not
confused with a later action decision. A final `AdminReportKind::Operation`
report carries the local primary decision and S1 decision selected as follows:
the action decision for establish or clear whenever that action was called,
including an S1 action rejection; otherwise the prestate decision, including
successful revoke and all pre-action rejections. Revoke has no fictitious
action decision because S1 returns `void`. Barriers use `ExogenousBarrier` or
`AdministrativeBarrier`, and a pre-mutation failure uses `Terminal`. S1
`establishTrustedSession` itself polls, so this design makes no exact-one-poll
promise for an administration call.

An accepted establish or revoke reserves and commits an additional
administrative barrier only after all prechecks and the S1 action succeed. It
increments the wrapper epoch once, clears any future acknowledgement state,
and supplies the second barrier report before the final operation report.
Any rejected action has no administrative barrier and does not advance the
packet operation floor. The stream floor remains consumed when applicable.

## Establish, Revoke, And Clear Predicates

Operations one and two require one authenticated/bound packet and one matching
independent grant in the same serialized call. The packet and grant must agree
on operation/action, source/session, controller instance, and post-observation
transition epoch. Only the packet must name the current map epoch; the grant
does not carry a map-epoch field. The packet must name the immutable controller,
configured source, and a map binding compatible with the requested action. Its
operation sequence must pass the per-controller floor before S1 action is
attempted.

After the ordered observation/stream/capacity stages, the final-operation
primary predicate order for a packet call is: defensive C0a field validation
(`MalformedPacket`); all ingress verdicts (`IngressRejected`); operation three
(`UnsupportedOperation`); packet operation floor (`PacketOperationReplay`);
old/duplicate grant (`SupervisorDuplicate`); missing grant (`SupervisorMissing`);
grant versus packet/current controller,
post-observation generation/epoch (`BindingMismatch`);
then session-gate and action-specific context predicates. This order is stable
for offline evidence. An exact-next valid grant is already consumed before all
of these packet checks, but none of their rejections advances the wire floor or
creates an administrative barrier.

After those common checks, establish checks packet source/map tuple and current
map epoch (`BindingMismatch`), then reuse of the last session, disarmed state,
valid map/link/required state/known mode and nonlatched prestate
(`EstablishmentContextRejected`). Revoke checks `Established` first
(`NoEstablishedSession`), then retained/current source/session and nonzero
current packet/map epoch (`BindingMismatch`), then disarmed state
(`ContextRejected`). Thus a packet/grant otherwise mutually matching the
current wrapper identity after a map rebind returns `NoEstablishedSession`
without a competing obsolete-map-binding result.

For clear, after the same observation/stream/capacity/pre-poll stages, check
duplicate (`SupervisorDuplicate`), clear action/current controller/generation/
epoch and retained-session binding (`BindingMismatch`), disarmed state
(`ContextRejected`), then prestate `latched` (`ClearNotLatched`). Only then call
S1. A failed establish or clear S1 action returns `S1ActionRejected`; success
returns `Completed`. There is no missing-grant case on the clear reference API.

For establish, the packet session is nonzero and differs from the retained
last successful session if one exists. The current trusted map must be valid
and bind the same source/session/controller/map epoch. The context must be
disarmed, transport-connected, required-state-valid, and have a known mode
(`NotSelected` or `Selected`); the map must be valid. The prestate S1 decision
must not be latched. `ModeNotSelected`, `DisarmedRejected`, and
`ValidatedDisarmed` prestate decisions are explicitly administratively
allowable here, not permission to fly or admit a data request. This explicit
predicate prevents S1 establishment from silently ignoring a bad pre-poll
context result.

For revoke, the packet and grant must bind the current retained established
source/session, immutable controller, post-observation epoch, and a nonzero
current map epoch. Revoke is disarmed-only. It may proceed through a consistent
map-invalid state or transport, required-state, or known-mode loss because it
only closes state, provided the retained/current bindings still match. It never
bypasses packet authentication or target/source checks. A map source/session
rebind already sets `Revoked`, so a later explicit revoke is `NoEstablishedSession`.
On success the adapter calls S1 `revoke`, sets `Revoked`, and commits its
administrative barrier. It records `action_performed = true` and
`action_kind = Revoke`, but `has_action_s1_decision = false` because S1 revoke
returns `void`. S1 replay floors remain intact.

Operation three is structurally frozen C0a but unsupported by this session
administration slice. It returns a local unsupported-operation result and
never maps its acknowledgement time, checks its acknowledged status sequence,
opens a barrier, calls S1 administration, or advances the packet operation
floor. Its acknowledgement fields are likewise retained but ignored and
non-authorizing for operations one and two; no implicit freshness or ACK policy
is created by their nonzero values.

`clearLatchedFault` requires an exact-next valid clear grant, a disarmed
context, and a prestate S1 decision whose `latched` flag is true. The flag may
be true with current `ValidatorTimeRegressed`, not only with a `Latched` code.
It deliberately permits a consistent map-invalid state and transport/required
state/mode loss, because the S1 clear action is the only authorised clock-latch
recovery and checks only supervisor authority plus disarmed state. It must not
call S1 `clearLatch` when the prestate decision is unlatched: S1 would reset
session state even then. On S1 clear success it sets `EstablishmentRequired`,
keeps admission closed, and commits its administrative barrier.

## Deferred Work And Evidence

The next implementation proposal needs a distinct path assignment and an
independent oracle for packet/grant ordering, map rebind/revoke workflow,
zero-first operation sequence, event `UINT64_MAX`, report reservation,
prestate/action S1 decisions, unbound initial latch clear, and retained-tuple
clear after a session. It must regress the closed state core and S1.

The concrete next implementation scope, if a later gate approves it, is the
existing `src/lib/research_acceleration/WireAdapter.hpp` and `WireAdapter.cpp`,
new independent `WireAdapterSessionTest.cpp`, and minimal standalone CMake test
registration. `RequestGuard.hpp/.cpp` and every other S1 file stay unchanged.
A future test-only peer may seed administration floors and local
epoch/report-counter boundaries only; it may not create a session, alter the
guard, or clear a latch. Public administration methods are the only test route
for session and latch transitions.

This design does not select supervisor transport, credentials, target IDs,
clock provenance, timing limits, recovery policy, physical parameters, or a
flight/controller behavior. Only the project owner can approve live authority
and deployment mechanisms. This document neither approves its own gate nor
authorizes code.
