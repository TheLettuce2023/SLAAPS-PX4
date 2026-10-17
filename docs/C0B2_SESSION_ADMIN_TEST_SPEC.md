# C0b.2 Session-Administration Test Specification

Status: candidate design-only offline oracle. It does not release
implementation, MAVLink transport, request admission, controller, allocation,
actuator output, SITL, hardware, or a flight claim.

## Candidate test boundary

The candidate session-administration surface is processSession(sample, decoded
session, ingress verdict, optional supervisor grant) for frozen C0a operations
1 establish and 2 revoke, plus clearLatchedFault(sample, grant). Existing
ordinary observeTrustedState, poll, and closed getEligibleRequest retain their
two-report AdapterResult behavior unchanged. The candidate administration result
has reports[3], chronological valid entries [0, report_count), a closed
admission snapshot, session/revoked state, supervisor-event floor, and
controller-lifetime packet-operation floor.

Every administration call orders its candidate work as follows: structural
trusted-state validation; terminal supervisor-stream classification; capacity
reservation; exogenous barrier commit; exact-successor grant consumption;
mandatory prestate S1 poll; then ordered packet/action checks. Reports are
exogenous state barrier, administration barrier, and final operation in that
order. A packet never contains a trusted generation or an authorization
Boolean. A grant is independently trusted; packet authentication and source
binding are injected ingress facts, not evidence of supervisor authority.

All values below are synthetic non-operational fixtures; they are not deployed
identity, timing, authority, or vehicle limits.

| Fixture fact | Value |
| --- | ---: |
| controller I | 11 |
| configured source S | 21 |
| session A / session B | 31 / 32 |
| map epoch | 41 |
| initial generation / time | 51 / 1000 |
| initial state barrier / wrapper epoch / last report | 1 / 1 / 2 |
| next generation / time | 52 / 1010 |
| initial supervisor floor | 0 |
| initial packet-operation floor | absent |

A valid establish prestate is disarmed, map-valid/current-bound, connected,
required-state-valid, nonlatched, and has a known NotSelected or Selected mode.
Pre-poll ModeNotSelected, DisarmedRejected, or ValidatedDisarmed can be
benign administration prestate observations only; none is a flight permission.
A valid packet has exact version/targets/reserved fields, current binding
values, and MAVLink2, parser-authenticated, configured-header/source, and
configured-target-binding verdicts.

## Common assertions and precedence

Each case asserts report count/order/sequence/kind/epoch, zero unused report
fields, snapshot generation/epoch/barrier/last-report/terminal values, closed
admission, session gate, revoked gate, event floor,
has_wire_operation_floor/wire_operation_floor, actual S1 calls/decisions, and
unchanged fields. It also asserts AdminCorrelation: barrier reports are all
zero; final operation and terminal reports identify every supplied packet tuple
and grant ID; supervisor_event_consumed is true only after an exact-next
well-formed event commits the floor. No test infers a request or actuator
output.

Precedence is trusted-state structural failure, supervisor-stream terminal
classification, counter/epoch capacity, exogenous state barrier, exact-event
consumption, mandatory prestate S1 poll, then ordered packet/action predicates.
A zero, gap, malformed, or otherwise terminal stream event therefore closes
before an exogenous transition commits. A valid immediate event is consumed
before packet/action rejection, but the prestate poll comes before those
predicates. Capacity is reserved before consuming that valid event or committing
any barrier. Thus an insufficient-capacity terminal does not consume a grant.
Reservation checks worst-case capacity but does not consume report IDs that are
not emitted: normal reports remain chronological and contiguous, except the
terminal jump to UINT64_MAX.

The exact count/epoch oracle is: state plus successful administration is three
reports/two epoch advances; state plus rejected action is two reports/one
advance; unchanged state plus successful administration is two reports/one
advance; unchanged state plus rejected action is one report/no advance; and a
pre-mutation terminal is one terminal report/no advance.

The final operation record distinguishes a prestate S1 Decision from whether an
administrative action was performed. In particular, RequestGuard revoke has no
returned S1 Decision: a successful revoke is represented by
action_performed=true, not by an invented decision value. Rejections before an
administrative call have action_performed=false. A final Operation report uses
the action decision only for establish/clear after action invocation; it uses
the action decision even if that invoked action rejects. Revoke and pre-action
rejections instead carry the prestate decision; revoke never fabricates a
returned action decision.

An event floor starts at zero. The only next valid identifier is floor + 1,
with checked arithmetic. A well-formed exact successor is consumed before
action checks. A positive old/duplicate event is rejected without moving the
floor. Zero, a gap, malformed event, or attempted successor after
UINT64_MAX terminally closes; it never wraps.

The packet-operation floor is per immutable controller I, not per session. It
has a presence bit: first operation sequence zero is allowed. Only a complete
successful establish or revoke advances it; all rejected and unsupported
operations leave it unchanged.

## Golden transition sequence

### ADMIN-001: closed baseline

At G=51/time 1000, ordinary state observation yields report 1 state barrier at
epoch 1 and report 2 state operation. Snapshot is G=51, epoch 1,
barrier=1/time=1000, last-report=2; session state none, revoked false,
has_supervisor_floor=false/supervisor_floor=0, operation floor absent, and
admission closed.

### ADMIN-002: one call installs two barriers

At G=52/time 1010, submit operation 1, sequence 0, S/A, controller I, packet
post-observation epoch 2, map epoch 41, valid ingress, and matching immediate
grant E=1. The grant cites post-observation G=52, I, epoch 2, S/A, and
establish action.

Expected reports are:

1. sequence 3: exogenous state barrier, epoch 2, time 1010;
2. sequence 4: successful administration barrier, epoch 3, time 1010;
3. sequence 5: establish operation with the actual S1 decision.

Expected post-state: G=52, epoch 3, barrier=4/time=1010, last-report=5,
event floor=1, packet-operation floor present at 0, A established, revoked
false, acknowledgement absent, and admission closed. The successful session
record is not an eligible request.

### ADMIN-003: revoke and distinct replacement

From ADMIN-002, submit operation 2 sequence 1 for A with E=2 but
parser-authenticated false. It produces one final rejection report sequence 6,
no administration barrier/epoch advance, consumes event floor 2, and preserves
has_wire_operation_floor=true/wire_operation_floor=0. It calls no S1
administrative method and remains admission closed.

Submit the same operation sequence 1 with valid ingress and the distinct
matching immediate E=3. It produces administration barrier sequence 7/epoch 4
and final operation sequence 8; it sets the revoked gate, clears
eligibility/acknowledgement, advances wire operation floor to 1 and event floor
to 3, but remains admission closed. It records action_performed=true and
action_kind=Revoke with no action S1 decision; its final operation record
retains the prestate S1 decision.

Operation 3 cannot reopen the revoked gate. A later establish must use distinct
session B and a distinct immediate grant. An attempt to establish A is rejected
without S1 establish, administrative barrier, or packet-floor movement.

### ADMIN-004: direct administration has no invented generation

After ADMIN-003, first observe the ordinary valid G=53 map binding configured
source S/session B transition. It installs only the ordinary state barrier and
operation report, revokes A, and leaves the administration gate revoked. At
unchanged G=53, a complete establish-B operation with a new strictly greater
operation sequence and immediate matching grant advances the wrapper epoch once
and creates one administration barrier plus its operation report. Packet and
grant bind the already observed current epoch. Administration itself neither
requires nor fabricates a further generation.

## Event and conjunction fixtures

### EVENT-001: bad authentication burns only the trusted grant

At event floor zero, present well-formed bound E=1 but parser-authenticated
false. Expected typed authentication rejection; event floor becomes 1, packet
floor remains absent, session state remains none, and no S1 establish/revoke or
administration barrier occurs. Replaying E=1 with a valid packet is an old
duplicate and cannot retry. A distinct E=2 is required.

### EVENT-002: exact event is consumed before mismatch/action failure

At floor zero, present exact E=1 with a packet/grant source, session, action,
controller, post-observation epoch, or grant generation mismatch; repeat with
armed establish and with invalid ingress. In each case E=1 is consumed, but
the packet floor and administration barrier remain unchanged. Apart from the
mandatory trusted-state observation/poll/revocation, no S1 administrative
method is called. No packet field substitutes for the independent grant.

### EVENT-003: missing or duplicate grant

A complete packet with no grant is nonterminal, does not consume event or
packet floor, and calls no S1 administration. After E=2 is consumed, an E=1 or
E=2 duplicate is nonterminal and moves neither floor. A well-formed successor
grant paired with an operation-3 packet is consumed but must not administer S1.
There is no absent-packet processSession overload; packet-free clear uses its
separate endpoint and predicates.

### EVENT-004: zero, gap, malformed, maximum

With floor 2, E=4 is terminal supervisor-stream fault: the administration
result reports SupervisorStreamFault while the shared wrapper terminal retains
TrustedStateFault, with one terminal report/no epoch advance, no packet-floor
change, no admin barrier/S1 administration, and no later grant recovers the
wrapper. E=0 and malformed or skipped-ID events have the same effect and zero
does not advance the floor.

Separately, floor UINT64_MAX-1 accepts E=UINT64_MAX once when all other
preconditions pass. The retained floor becomes UINT64_MAX. Repeated E=max is
not a duplicate result: because UINT64_MAX has no representable immediate
successor, any supplied later event terminally closes. No arithmetic wraps to
zero.

### EVENT-005: trusted state fault is primary

Pair E=0, malformed packet, or foreign packet with controller-mismatched,
discontinuous, or otherwise structural-invalid trusted state. Expected
TrustedStateFault, no grant parsing/consumption, no packet-floor change, no
administration barrier, and no S1 administration beyond required fault
revocation. This prevents packet/event reasons from hiding trusted loss.

## Session and operation-floor fixtures

### SESSION-001: establish conjunction and bootstrap prestate

Establish requires configured S, nonzero new session, current controller/map
binding, current packet epoch, matching grant post-state epoch/generation,
disarmed/map-valid/connected/required-valid/nonlatched state, valid ingress,
and a known mode. Test both NotSelected and Selected as valid bootstrap modes.
Its pre-poll decision may be ModeNotSelected, DisarmedRejected, or
ValidatedDisarmed, but the successful administrative result must remain closed
and non-flight-authorizing.

### SESSION-002: first sequence zero and controller lifetime

Successful establish A sequence 0 records a present floor 0. Successful revoke
A sequence 1 records floor 1. Successful establish B sequence 2 records floor
2. Session change does not reset this floor. After floor 2, sequence 1 is a
pure decrease and sequence 2 is a pure replay; neither calls S1
administration, creates a barrier, or moves the floor.

### SESSION-003: rejected packet does not burn packet floor

Before first success, reject sequence 0 for bad ingress, packet/grant mismatch,
wrong target/version/operation, armed establish, stale binding, or invalid
establish map. Floor stays absent, so a later complete sequence-0 operation
with a distinct immediate grant may succeed. After floor 1, reject sequence 2;
a later valid sequence 2 remains processable. This rule is independent of
event consumption.

### SESSION-004: revoke through consistent loss is close-only

Revoke A may proceed while disarmed through internally consistent map-invalid,
transport-loss, required-state loss, or mode loss. Packet and grant must still
match retained/current S/A, immutable I, post-observation epoch, and nonzero
current map epoch. Successful close-only revoke sets revoked and moves the
operation floor once; it never opens admission.

A source/session rebind at a valid contiguous trusted generation automatically
sets revoked. A subsequent explicit revoke is rejected as NoEstablishedSession,
with no S1 administrative revoke, admin barrier or wire-floor movement; an
exact-next well-formed grant is still consumed. The later required workflow is
new session B plus new matching grant.

### SESSION-005: operation 3 is deferred

Frozen operation 3 acknowledgement is typed unsupported in this slice. It
does not establish/revoke/clear, advance packet floor, install an admin
barrier, or authorize from raw acknowledgement time/status fields. A provided
well-formed immediate successor grant is nevertheless consumed before the
unsupported-operation result; an absent grant remains non-consuming. ACK
arithmetic and request admission remain a later gate.

## Latch-clear fixtures

### CLEAR-001: only a retained S1 latch may clear

Create an S1 latch with synthetic validator-time regression. A new immediate
trusted clear grant at disarmed current state records a pre-clear S1 decision
whose latched flag is true (including ValidatorTimeRegressed; its code need not
be Latched), calls clearLatch(true, Disarmed) once, and on success installs one
direct admin barrier. It leaves a closed session gate and requires a later
distinct establishment. The clear API accepts no packet; it cannot parse,
validate, or derive authority from one.

### CLEAR-002: prevent unlatched reset

With retained S1 decision not latched, a well-formed immediate clear grant is
consumed but calls no S1 clear, installs no barrier, and preserves session/S1
state. This blocks S1's otherwise unsafe unlatched clear behavior.

### CLEAR-003: context and tuple rules

Armed current state consumes an immediate clear grant but rejects before S1
clear. A latched disarmed state with map-invalid/context-map-invalid or
transport loss remains clearable: the required pre-poll has latched=true
(whether its code is Latched or ValidatorTimeRegressed), then clear is
attempted. Clear may carry a zero source/session tuple only before any
successful session; after one successful session, it must cite exact last
source/session. A mismatch consumes the exact event but performs no S1 clear
or barrier.

## Report, counter, and epoch fixtures

### REPORT-001: preserve ordinary two-record result

Repeat ordinary first/unchanged/transition state-core calls. They retain maximum
report_count=2 and expose no session-administration fields or implicit report.

### REPORT-002: reserve all three records

Seed last ordinary report UINT64_MAX-4 and run ADMIN-002. Expected sequences
UINT64_MAX-3 state barrier, UINT64_MAX-2 admin barrier, and UINT64_MAX-1
operation. Seed UINT64_MAX-3 and repeat: terminal CounterExhausted report
UINT64_MAX only; no state generation commit, event consumption, packet-floor
advance, S1 poll/administrative action, or partial barrier. Terminal safety
revocation still calls S1 revoke, as required by the state-core contract.

For a first/new-generation rejected action, reservation still requires all
three potential records and two epoch advances before mutation. Seed
UINT64_MAX-4: the state barrier and rejection operation emit
UINT64_MAX-3 and UINT64_MAX-2, leaving UINT64_MAX-1 unused; no report ID is
consumed for the absent administration barrier. Seed UINT64_MAX-3 instead:
capacity fails before state/event mutation and terminal UINT64_MAX is the sole
report. These fixtures prove worst-case precheck without consuming gaps for
absent administration barriers.

### REPORT-003: direct admin reservation and repeat terminal

Unchanged-state establish/revoke requires two ordinary reports. With space for
one but not two (last ordinary report UINT64_MAX-2), CounterExhausted uses
UINT64_MAX once with no event consumption or S1 action. With last ordinary
report UINT64_MAX-3, the two actual reports occupy UINT64_MAX-2 and
UINT64_MAX-1. Repeat terminal returns retained snapshot and zero reports; no
reuse/wrap.

### REPORT-004: checked epoch and floors

An administration barrier may reach wrapper epoch UINT64_MAX. Any required
later state/admin epoch advance terminates CounterExhausted without wrapping.
Packet-operation floor UINT64_MAX accepts no greater value; equal/lower values
reject without S1 administration or floor movement; the mandatory prestate
poll still occurs after successful capacity checks.

## Fixed administration code and correlation oracle

The final operation/terminal code must be exactly the indicated AdminCode:

| Fixture outcome | Required AdminCode |
| --- | --- |
| missing grant | SupervisorMissing |
| old positive event | SupervisorDuplicate |
| zero/gap/malformed/post-maximum stream event | SupervisorStreamFault |
| defensive decoded packet shape failure | MalformedPacket |
| any false ingress verdict | IngressRejected |
| operation 3 | UnsupportedOperation |
| equal/lower present wire operation sequence | PacketOperationReplay |
| packet/grant/current identity, epoch, generation, or map mismatch | BindingMismatch |
| establish prestate/gate failure | EstablishmentContextRejected |
| revoke with no established retained session | NoEstablishedSession |
| clear grant/action/current identity or retained tuple mismatch | BindingMismatch |
| armed revoke or clear after prior predicates pass | ContextRejected |
| unlatched clear | ClearNotLatched |
| establish or clear action S1 rejection | S1ActionRejected |

All normal successful observation/prestate handling uses Observed; a completed
establish, revoke, or clear uses Completed. Invalid construction, trusted-state
fault, and capacity exhaustion use NotConfigured, TrustedStateFault, and
CounterExhausted respectively. A triggering supervisor terminal result keeps
AdminCode::SupervisorStreamFault even though the shared ordinary wrapper
terminal is AdapterCode::TrustedStateFault.

For combined-fault fixtures use exactly the predicate order in
`C0B2_SESSION_ADMIN_DESIGN.md`, not the table's display order: packet shape,
ingress, unsupported operation, wire replay, duplicate grant, missing grant,
common binding, then the action-specific predicates. Clear uses duplicate,
binding, disarmed context, then latch flag. Revoke checks its established gate
before current-map binding. Pairwise fixtures must assert the first applicable
code without changing the mandated pre-poll/event-consumption behavior.

Explicitly exercise malformed grant shapes from the design (zero required
identity, invalid action and inconsistent binding-bit/tuple) against well-formed
nonzero mismatches: only the latter consume the immediate event. Exercise both
ordinary-to-admin and admin-to-ordinary terminal repeats. When no normal report
ID remains, a structural or supervisor terminal falls back to CounterExhausted
at UINT64_MAX, which becomes the retained first cause; later calls emit nothing.

Epoch capacity fixtures must distinguish unchanged state at UINT64_MAX-1
(one possible administration advance fits) from first/new-generation worst-case
two advances. For an initialized new-generation call, UINT64_MAX-2 fits both;
UINT64_MAX-1 fails before mutation even if the packet would later reject.
Also test an administrative call as the first observation: successful action
creates epochs one and two, whereas rejection creates only epoch one while
still requiring capacity for the whole possible call.

## Pass rule and limitation

Every case passes only when all stated report, snapshot, floor, barrier, S1
trace, and unchanged-output assertions hold. A later offline pass would not
prove supervisor provenance, deployed authentication, timing suitability,
transport, command admission, controller behavior, allocation, actuators,
simulation, hardware, or flight qualification.
