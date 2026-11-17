# C0b.2 Admission Design: Epoch Acknowledgement And Request Boundary

## Scope and release boundary

This document designs the next **offline-only** extension of the accepted C0b.2
state core and session administration.  It is not implementation approval.
It adds a current-epoch acknowledgement gate before a decoded acceleration
request may be submitted to the existing S1 `RequestGuard`.

It does not add MAVLink framing, a dialect, parser/signing implementation,
network or serial I/O, a status publisher, uORB, a controller, allocation,
actuator output, SITL, firmware build, or physical operation.  An S1-accepted
request remains only a cached inertial-FRD-at-CG demand; it is not evidence of
physical feasibility, tracking, or output.

This design reuses one private `RequestGuard`, the immutable `ContractConfig`,
the C0b.1 `TrustedClockMap`, and the C0b.2 serialized state/session owner.  It
does not duplicate S1 or reconstruct its guard to reset a latch or replay
floor.  The caller serializes every listed method with state observation and
the one validator-time authority.

## Public types and API

`DecodedSessionV1` and `SessionIngressVerdict` remain the C0b.2 types.
`SessionIngressVerdict` is intentionally reused for both packet families: all
of `mavlink2`, `parser_authenticated`,
`configured_header_channel_source_binding`, and `configured_target_binding`
must be true.  It is a trusted ingress-boundary result, never an authority
copied from a packet.

```cpp
struct DecodedRequestV1 {
    WireRequestV1 wire;
    uint8_t target_system;
    uint8_t target_component;
    uint8_t flags;
    uint8_t reserved[4];
};

enum class IngressPacketKind : uint8_t { None, Acknowledgement, Request };
enum class IngressReportKind : uint8_t { ExogenousBarrier, Operation, Terminal };

enum class IngressCode : uint16_t {
    Observed, Completed, NotConfigured, TrustedStateFault, CounterExhausted,
    PrestateRejected, MalformedPacket, IngressRejected,
    UnsupportedSessionOperation, WireOperationReplay, SessionGateRejected,
    BindingMismatch, AcknowledgementRequired, AcknowledgementFrozen,
    AcknowledgementStatusMismatch, AcknowledgementOrdering,
    AcknowledgementTimingInvalid, MapInvalid, MapIntervalInvalid,
    MapArithmeticInvalid, WireVersionInvalid, WireEnumInvalid, WireTimeInvalid,
    WireNonFinite, S1Rejected
};

struct IngressCorrelation {
    IngressPacketKind packet_kind;
    SourceId source;
    SessionId session;
    uint64_t controller_instance_id;
    uint64_t transition_epoch;
    uint64_t map_epoch;
    uint64_t packet_sequence;
    uint64_t acknowledged_status_sequence;
};

struct IngressReport {
    uint64_t report_sequence;
    uint64_t transition_epoch;
    TimeUs validator_time_us;
    IngressReportKind kind;
    IngressCode code;
    bool has_s1_decision;
    Decision s1_decision;
    IngressCorrelation correlation;
};

struct AdmissionSnapshot {
    bool acknowledgement_current;
    uint64_t acknowledgement_operation_sequence;
    uint64_t acknowledged_status_sequence;
    TimeUs acknowledgement_raw_producer_time_us;
    TimeUs mapped_acknowledgement_time_us;
    TimeUs earliest_acknowledgement_time_us;
    TimeUs latest_acknowledgement_time_us;
    SourceId source;
    SessionId session;
    uint64_t controller_instance_id;
    uint64_t transition_epoch;
    uint64_t map_epoch;
    TrustedClockMap map;
};

struct IngressResult {
    IngressCode code;
    bool admission_closed;
    AdapterSnapshot snapshot;
    SessionSnapshot session;
    AdmissionSnapshot admission;
    bool has_prestate_s1_decision;
    Decision prestate_s1_decision;
    bool has_operation_s1_decision;
    Decision operation_s1_decision;
    uint8_t report_count;
    IngressReport reports[2];
};

IngressResult processAcknowledgement(const TrustedStateSample &sample,
                                     const DecodedSessionV1 &packet,
                                     const SessionIngressVerdict &ingress);
IngressResult processRequest(const TrustedStateSample &sample,
                             const DecodedRequestV1 &packet,
                             const SessionIngressVerdict &ingress);
```

`DecodedRequestV1` is the complete C0a request after payload decoding, not a
wire struct and not a permissive convenience object.  Its `wire` member is
the exact C0b.1 subset; the enclosing target, flags, and reserved fields are
what C0b.1 intentionally omits.  Defensive C++ validation repeats all frozen
C0a facts needed here: nonzero source/session/controller/transition/map and
target identities; zero flags and reserved bytes; and the v1 logical payload
rules.  It does not normalize a packet.  `WireRequestV1` / C0b.1 retains
version, enum, finite-axis,
map-interval, raw expiry-later-than-creation, and checked endpoint mapping
responsibility.  Thus a typed request with raw expiry less than or equal to
creation reaches C0b.1 and reports `WireTimeInvalid`, not `MalformedPacket`.

`IngressCode` is local adapter evidence, not a new frozen wire decision value.
`Completed` means an ACK was frozen or S1 evaluated the request as `Accepted`.
`S1Rejected` means S1 `evaluate()` was reached but did not accept the request;
the accompanying decision is authoritative.  The `Map*`, `Wire*` codes are a
lossless local mapping of the corresponding `CanonicalizeCode`.  No status
message allocation is implied.

`IngressCorrelation` always identifies the supplied packet; an ACK stores its
operation sequence and acknowledged status sequence, while a request stores
its request sequence and zero acknowledgement-status sequence.  A barrier
report has an all-zero correlation with packet kind `None`. The final operation or terminal report
carries the packet correlation.  `IngressResult` has no supervisor fields:
ACK processing cannot receive, accept, or consume a supervisor event.

`AdmissionSnapshot` is the one immutable private/current ACK record exposed
for offline assertions; no second `AcceptedAcknowledgement` type is needed.
It never stores a second request cache.  The full trusted-map value is retained
and compared, including `valid`, all bindings,
signed offset, uncertainty, and inclusive raw interval.  A packet cannot
replace or edit a frozen ACK.

The public calls added in this slice are the sole ACK-admission entry point.
The existing `processSession(... operation == 3 ...)` remains
`UnsupportedOperation` for its packet action, but retains its accepted C0b.2
administrative prefix: an immediate well-formed exact-next grant may already
have consumed the supervisor floor before that final result.  This slice does
not change that legacy administrative behavior.  `processAcknowledgement` has
no grant parameter and cannot consume a grant or supervisor floor.
There is no ACK "grant" parameter.

## Current-ACK invariant and truthful ordinary access

The admission gate is open exactly when all of the following hold:

1. `AdmissionSnapshot::acknowledgement_current` is true;
2. `SessionGate` is `Established`;
3. its source, session, controller instance, wrapper transition epoch and map
   epoch still equal the current retained state; and
4. the complete immutable map equals the current trusted map.

Every exogenous barrier, successful administrative establish/revoke/clear,
or terminal transition clears the ACK before the next packet decision.  A
source/session map rebind already revokes the session gate in the state core
and therefore clears the ACK.  An S1 `poll()` or `evaluate()` decision with
`latched == true` also clears it immediately; no extra epoch is created.  A
later authorized clear creates its existing administrative barrier.

The ACK is intentionally retained across an old cached-request expiry or
`RequestTooOld` poll result.  It is not a request freshness cache.  A later
fresh request must independently meet S1 timing and the ACK ordering proof.

The existing ordinary API remains exactly two reports maximum and has exactly
one S1 state observation per valid call:

- `observeTrustedState` and `poll` call `RequestGuard::poll` once.
- `getEligibleRequest` calls only `RequestGuard::getEligibleRequest` once;
  that S1 getter performs its internal poll.  It uses a local
  `CanonicalRequest` and copies it to the caller only if the ACK gate is open
  and the getter actually returned an eligible request.  It must not call a
  preceding explicit poll or create a second adapter getter/cache.

Append `AdapterCode::EligibleRequest` without changing existing numeric order.
The exact ordinary mapping is: observation returns `Observed` for each normal
prestate (`Accepted`, `NoEligibleRequest`, or `ValidatedDisarmed`); poll
returns `Observed` only for S1 `Accepted` (it does not copy a request), and
`NoEligibleRequest` for S1 `NoEligibleRequest` or `ValidatedDisarmed`; either
returns `GuardRejected` for every other S1 decision.  Access returns `EligibleRequest`
only when it copies the local S1 getter result to the caller and returns
`NoEligibleRequest` for its normal no-output `ValidatedDisarmed` result.  If
the ACK gate is closed access returns
`NoEligibleRequest` even if S1's local getter found an old cache.  If an ACK is
current but S1 has no eligible cache, access returns `NoEligibleRequest` and
`admission_closed == false`.  Fault results preserve the truthful final ACK
state, which is closed when the fault/latch rule cleared it.

The newly reachable `poll`/S1 `Accepted` case intentionally supersedes the
closed state-core placeholder mapping: it reports `Observed`, with the real
eligible S1 decision, rather than falsely naming an eligible cache absent.
The pre-existing no-cache and disarmed mappings are unchanged. Historical
state/session tests must pass without edits to their expectations.

For ordinary, ingress, and administration results,
`admission_closed == !acknowledgement_current` after the call's effects.  This
supersedes the earlier session-design placeholder that hard-coded all
`AdminResult` values closed: a rejected administrative packet truthfully
reports open when its current ACK survived; a successful administrative action
clears it and reports closed.  A wrapper terminal reports closed.  This flag
describes the ACK barrier only, never cached-request eligibility or controller
permission.

## Checked ACK point mapping and acceptance

ACK uses `DecodedSessionV1::producer_acknowledgement_monotonic_us` only for
operation three.  Operations one and two keep the frozen rule: their ACK
fields are ignored and non-authorizing.  C0b.1's endpoint converter is not
called with a fabricated request.  A private pure checked point mapper instead
does the following against the current trusted map:

1. require a valid map with all nonzero bindings and positive uncertainty;
2. require raw ACK time inside the inclusive `[valid_from, valid_until]`;
3. add `validator_minus_producer_us` with checked unsigned arithmetic;
   for a negative `INT64_MIN` offset use the safe magnitude
   `uint64_t(-(offset + 1)) + 1`, then checked subtraction;
4. derive `earliest = mapped - uncertainty` and `latest = mapped + uncertainty`
   with checked subtraction/addition; and
5. calculate `future_limit = receive + maximum_future_skew_us` with checked
   addition and `age = receive - earliest` with checked subtraction.

No underflow/overflow is clipped.  A raw ACK outside its inclusive map
interval is `MapIntervalInvalid`; invalid map identity/validity is
`MapInvalid`; an inverted interval is `MapIntervalInvalid`; and failed signed
offset conversion is `MapArithmeticInvalid`.  Underflow/overflow deriving
mapped minus/plus uncertainty, `future_limit`, or age is
`AcknowledgementTimingInvalid`, as are a configured-uncertainty excess,
earliest-after-receive, excessive age, or latest-after-future-limit.  These
rules keep map conversion distinct from ACK time-window arithmetic.

After packet/binding checks, accept an ACK only if all are true:

```text
packet.acknowledged_status_sequence == snapshot.barrier_report_sequence
earliest > snapshot.barrier_validator_time_us
earliest <= sample.request_guard_context.validator_receive_time_us
receive - earliest <= configuration.timing.maximum_command_age_us
latest <= receive + configuration.timing.maximum_future_skew_us
map.uncertainty_us <= configuration.timing.maximum_clock_uncertainty_us
```

The strict first comparison prevents a queued pre-barrier acknowledgement.
The raw ACK need not be nonzero; C0a permits zero raw timestamps, but its
mapped, uncertainty-expanded interval must pass every check.  A first/new
generation call creates a barrier whose validator time equals that call's
receive time; the simultaneous ACK necessarily fails strict ordering because
its earliest instant must also be at or before receive time.  It emits the
exogenous-barrier and operation reports, but accepts no ACK.  A later
same-generation call is required.

On success the wrapper stores the complete `AdmissionSnapshot`/private record
atomically and advances the existing successful wire-operation floor to the
ACK operation sequence.  It creates neither an administrative barrier nor an
epoch advance.  The frozen record cannot be superseded: a later otherwise
fresh ACK returns `AcknowledgementFrozen`; a duplicate or older operation
sequence returns `WireOperationReplay` first.  Failed ACKs never move the
wire-operation floor or alter a valid ACK.

## Packet order and gate precedence

Both ingress methods run the following shared prefix.  It makes a trusted
state/time fault primary over untrusted packet content while ensuring foreign
traffic cannot itself cause a barrier or session mutation.

1. Return `NotConfigured` or the retained terminal code before any S1 call.
   A retained shared `AdapterCode::CounterExhausted` maps to local
   `IngressCode::CounterExhausted`; every other retained shared terminal,
   including a prior administrative `SupervisorStreamFault` represented by
   `AdapterCode::TrustedStateFault`, maps to `IngressCode::TrustedStateFault`.
   It emits no new report, moves no counter, and leaves the single ACK record
   closed.  The shared terminal report/`UINT64_MAX` policy is never repeated.
2. Validate `TrustedStateSample`, observer continuity, generation continuity,
   retained-state equality, configured identity, and map/context agreement
   using the existing state-core rules.  A failure is terminal
   `TrustedStateFault`; counter/epoch exhaustion is terminal
   `CounterExhausted`.
3. Before mutation, reserve capacity without consuming IDs: two reports and
   one epoch on first/new generation, otherwise one report and no epoch.  On
   failure terminal-close using the existing `UINT64_MAX`-once policy.
4. Commit an exogenous barrier if required, revoke S1's active request, clear
   ACK, update retained state, and emit its report.  Do not use unused reserved
   IDs.
5. Call S1 `poll()` once and retain it as `prestate_s1_decision`.
6. If the decision is latched, `ValidatorTimeRegressed`, or shows trusted
   context loss (`ClockMappingInvalid`, `RequiredStateInvalid`,
   `ArmStateUnknown`, `ModeStateUnknown`, or `Disconnected`), clear ACK and
   finish `PrestateRejected`; do not evaluate a received request.

`RequestExpired` and `RequestTooOld` from that pre-poll concern S1's *old*
cache and are explicitly benign for this call: a valid replacement proceeds to
S1 `evaluate()`.  So do `Accepted`, `NoEligibleRequest`, `ValidatedDisarmed`,
`DisarmedRejected`, and `ModeNotSelected`; the latter policy results are left
to S1 and cannot make control eligible by themselves.  Other pre-poll
rejections, including `TimeArithmeticInvalid`, are `PrestateRejected` and do
not call `evaluate()`.  Apart from the listed latching/clock/context cases,
packet rejection does not clear a valid ACK.  The wrapper never infers whether
an earlier request cache survived from `Decision::eligible_for_control`; only
the later S1 getter determines that.

After this prefix, `processAcknowledgement` uses this exact first-match order:

1. malformed C0a typed session object -> `MalformedPacket`;
2. a false ingress verdict -> `IngressRejected`;
3. operation not equal to three -> `UnsupportedSessionOperation`;
4. when the shared wire-operation floor exists, equal/older operation sequence
   -> `WireOperationReplay`; without a floor, zero is permitted by C0a and
   reaches the following session-gate check;
5. gate not `Established` -> `SessionGateRejected`;
6. source/session/current retained session, configured source, controller,
   transition epoch, map epoch, and current map bindings disagree ->
   `BindingMismatch`;
7. invalid/inverted current map -> `MapInvalid`/`MapIntervalInvalid`;
8. an already-current immutable ACK -> `AcknowledgementFrozen`;
9. wrong barrier report sequence -> `AcknowledgementStatusMismatch`;
10. raw interval, signed point-map, then expanded-time arithmetic/age/future/U
    checks -> the exact `Map*` or `AcknowledgementTimingInvalid` code above;
11. `earliest <= barrier_validator_time_us` -> `AcknowledgementOrdering`;
    otherwise freeze it and return `Completed`.

`processRequest` then uses this exact first-match order:

1. malformed complete C0a request -> `MalformedPacket`;
2. false ingress verdict -> `IngressRejected`;
3. not `Established` -> `SessionGateRejected`;
4. controller, transition epoch, configured/retained source-session, current
   map epoch, or current map binding disagreement -> `BindingMismatch`;
5. no current immutable ACK -> `AcknowledgementRequired`;
6. invoke C0b.1 `canonicalizeRequestV1` and map its precise result to the
   matching `Map*`/`Wire*` code;
7. raw request creation not strictly later than stored raw ACK ->
   `AcknowledgementOrdering`;
8. failure to subtract canonical uncertainty from canonical creation ->
   `MapArithmeticInvalid`; otherwise an earliest creation not strictly later
   than stored `latest_acknowledgement_time_us` -> `AcknowledgementOrdering`;
9. call S1 `evaluate(canonical, context)` exactly once.  Return `Completed`
   only for S1 `Accepted`; otherwise return `S1Rejected` with that decision.
   In particular, S1 `ValidatedDisarmed` is structurally valid but not
   admitted/cached for control, so this local result remains `S1Rejected`; it
   makes no flight-success claim.

The request's exclusive expiry, complete S1 age/future/uncertainty/replay
checks, acceleration bounds, FRD/quantity/reference semantics, and S1's
disarmed policy remain entirely S1-owned.  If `evaluate()` latches, clear ACK
after recording that operation decision.  Other S1 rejection leaves the ACK
as-is and makes no assertion about an older cache.

## Reports, counters, and evidence cases

Ingress has at most two reports.  A first/new generation records an
`ExogenousBarrier` then the packet `Operation`; unchanged state records only
the operation.  An ACK success and a request success are operation-only and
do not increment epoch.  A pre-mutation terminal has one `Terminal` report.
The final operation selects S1's evaluate decision if called; otherwise it
carries the pre-poll decision.  It always carries the packet correlation.

Counter allocation is chronological and contiguous for normal records.  The
common capacity check reserves worst-case room but consumes only reports
actually emitted.  It is two reports/one epoch for a first or new generation,
one report/no epoch when unchanged; no ACK/request result needs a third
record.  Terminal exhaustion uses the existing reserved `UINT64_MAX` one-time
notice and permanently closes admission without wrap.  Existing administration
capacity remains three reports/two epochs; this slice changes neither its
supervisor-floor semantics nor its report capacity.

The first offline test slice must cover at least:

- known establishment with operation sequence zero, a new arm-state generation
  barrier, then ACK operation sequence one at a later same generation;
- the invariant that an ACK cannot pass on the same call that installed its
  barrier;
- checked positive/negative/`INT64_MIN` offsets; raw map endpoints; mapped and
  uncertainty underflow/overflow; age/future/uncertainty violations; and exact
  barrier status correlation;
- frozen ACK duplicate/replay and an ACK that is well-formed but wrong
  source/session/controller/transition/map binding;
- request raw and conservative post-ACK ordering, C0b.1 error mapping, then
  S1 acceptance/rejection without an invented second cache;
- old cache expiry/too-old pre-poll followed by a fresh valid replacement;
  an S1 latch, map/state barrier, successful session administration, and
  terminal all clearing ACK; and
- ordinary poll/access reports showing an open ACK gate even with no S1 cache,
  plus `EligibleRequest` only when the S1 getter copied an output.

The agreed non-operational timing fixture is: establish at generation 51,
validator time 1000 (reports barrier epoch 1, administrative barrier epoch 2,
operation); arm transition generation 52 at time 1100 (barrier epoch 3 then
operation); ACK raw 105, offset +1000, uncertainty 2, receive time 1110,
operation sequence 1; request sequence 1, raw `[110, 130]`, receive time 1115;
access at 1116; then an expiry poll at 1128.  It demonstrates operation-only
ACK/request reporting, strict post-ACK ordering, and retained open ACK after
the old request expires.  These are fixtures, not operational timing limits.

## Next implementation boundary and remaining blockers

After design review, the only proposed implementation paths are existing
`src/lib/research_acceleration/WireAdapter.hpp` and `.cpp`, a new independent
`WireAdapterAdmissionTest.cpp`, and minimal standalone CMake registration.
`RequestGuard.*`, `TrustedTimeMap.*`, the wire codec, generated dialects, and
all live/flight paths remain unchanged.  Existing C0b.1/state/session tests
must regress.

Implementation remains blocked pending root approval of this design and its
independent oracle.  Live ingress separately remains blocked on approved
dialect allocation, signing/authentication and endpoint binding, supervisor
authority, serialized trusted state/map source, and a deployment-supported
whole-lifetime clock uncertainty policy.  Acceleration limits, timing budgets,
plant feasibility, control law, allocation, outputs, SITL, board builds, and
all physical operation remain human/evidence gated.
