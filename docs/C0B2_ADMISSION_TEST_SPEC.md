# C0b.2 Admission Test Specification

Status: candidate offline conformance oracle, pending master review. It does not
release ingress, transport, control, allocation, outputs, SITL, hardware, or
flight qualification. Every number below is synthetic test-fixture data, not an
operational timing, authority, or vehicle limit.

## Boundary under test

The proposed inactive wrapper adds separate processAcknowledgement for frozen
session operation 3 without a supervisor grant, and processRequest for a full
decoded 90-byte request plus all four SessionIngressVerdict booleans. The
existing getEligibleRequest opens only when the complete gate is open and uses
the one private RequestGuard accessor/internal poll. There is no wrapper
request cache.

The required result is IngressResult with IngressCode, IngressReportKind,
IngressCorrelation, AdmissionSnapshot, current adapter/session snapshots,
prestate and operation decisions, and reports[2]. A packet correlation is
exactly packet kind, source, session, controller, transition epoch, map epoch,
packet sequence, and acknowledged-status sequence. An ExogenousBarrier has
zero correlation with packet kind None. admission_closed equals logical-not
admission.acknowledgement_current; it is not a statement about an old S1 cache.

DecodedRequestV1 consists only of frozen WireRequestV1 plus target system,
target component, flags, and four reserved bytes. The four verdict facts are
MAVLink2, parser authentication, configured header/channel/source binding, and
configured target binding. Neither packet content nor a test may fabricate
these trusted facts, authority, a clock map, a session, a latch, or an S1
decision. No mock guard or second request cache is permitted.

## Common fixture

| Item | Synthetic value |
| --- | --- |
| controller, source, session | I=11, S=21, A=31 |
| target system/component | 1 / 1 |
| map binding | S=21, A=31, I=11, M=41 |
| map | valid, offset +1000, U=2, raw inclusive interval [0,1000] |
| timing | validity [10,100], maximum age 50, future skew 10, maximum U 5 |
| finite vector and bounds | vector (1,0,0), components [-10,10], norm 12 |
| normal context | continuous, armed, Selected, connected, required valid, map valid |
| ingress | all four facts true |

The immutable configuration uses ValidateOnly for disarmed fixtures. The
vector/bounds/timing values are only boundary-test arithmetic; they make no
aircraft claim.

## Numeric golden chain

The session setup below is an administration precondition, not admission.

| Step | Input | Required observation |
| --- | --- | --- |
| G0 | Establish A, operation sequence 0, disarmed G=51/time 1000 | Administration reports exogenous barrier 1/E1, administrative barrier 2/E2, operation 3. |
| G1 | Ordinary armed G=52/time 1100 | Ordinary reports barrier 4/E3 then operation 5; barrier is sequence 4/time 1100. Any old S1 demand and ACK are cleared. |
| G2 | Unchanged G=52/time 1110; ACK operation 3, operation sequence 1, raw ACK 105, acknowledged status sequence 4 | Map point 1105 and interval [1103,1107]; age 7; post-barrier. Completed, one Ingress Operation report 6/E3, no new barrier or epoch, ACK current, shared wire-operation floor 1. |
| G3 | Unchanged G=52/time 1115; request sequence 1, raw creation 110, exclusive expiry 130 | Canonical creation 1110, expiry 1130, creation [1108,1112], expiry [1128,1132], validity 20, age 7. Raw 110 is greater than 105 and earliest 1108 is strictly greater than ACK latest 1107. Completed, Operation report 7/E3, S1 evaluate Accepted, ACK retained. Request sequence is S1 per-session state and does not advance wire floor 1. |
| G4 | Getter at 1116 | It writes exactly the S1 canonical request through the accessor/internal poll and reports ordinary access sequence 8. It does not make an explicit second poll or read a wrapper cache. |
| G5 | Getter at 1128 | It leaves a caller sentinel unchanged, reports ordinary GuardRejected with S1 RequestExpired at sequence 9, and removes the S1 request. The ACK remains current and admission_closed remains false. |

An ACK in the G1 state-transition call cannot pass because its earliest time
must be strictly after a barrier installed at that same call time and no later
than that time. A request before G2 is AcknowledgementRequired. A request with
earliest creation equal to 1107 is AcknowledgementOrdering.

## Processing and precedence oracle

Every configured nonterminal ingress call applies this exact order:

1. retained terminal or invalid construction;
2. structural/generation validation of trusted state;
3. whole-call capacity and epoch reservation;
4. valid first/new-generation barrier commit and ACK clear;
5. exactly one S1 pre-poll;
6. latch, validator-time regression, or true trusted-context loss as
   PrestateRejected and required closure;
7. packet-specific predicates, then S1 evaluate only for an otherwise
   admissible request.

Structural fault is primary over capacity and all packet defects. Capacity is
primary over barrier, poll, evaluation, ACK/cache/floor mutation. A valid
barrier precedes the pre-poll. A benign pre-poll RequestExpired or RequestTooOld
does not veto an otherwise fresh request in that same call. A latch, time
regression, disconnect, invalid required state, or map/context loss vetoes it
and closes ACK where required. ValidateOnly/disarmed handling is a benign
prestate that reaches S1 evaluation.

ACK predicate precedence after a healthy pre-poll is: malformed session object,
any ingress false, operation not 3, shared wire-operation replay when that
floor exists, session gate,
current binding, valid map, ACK already current, status equals current barrier,
checked point mapping/U/age/future/strict barrier order, then Completed.

Request predicate precedence after a healthy pre-poll is: malformed request,
any ingress false, session gate/current binding, current ACK, canonicalization,
raw creation strictly after raw ACK, earliest creation strictly after latest
ACK, then S1 evaluate. Canonical conversion returns MapInvalid,
MapIntervalInvalid, MapArithmeticInvalid, WireVersionInvalid, WireEnumInvalid,
WireTimeInvalid, or WireNonFinite directly. A failed earliest-request
subtraction is MapArithmeticInvalid, not AcknowledgementOrdering. An S1
rejection is S1Rejected with the real DecisionCode.

A normal unchanged ingress result has one Operation report. First/new-generation
ingress has ExogenousBarrier then Operation. ACK/request success has no
administrative or ACK barrier and no epoch advance. The action report has the
prestate decision if no evaluate occurred, otherwise the evaluate decision.
Terminal has one Terminal report. No normal packet result claims execution,
control, tracking, physical truth, or actuator output.

## Acknowledgement cases

| ID | Fixture mutation | Required result and preservation |
| --- | --- | --- |
| ACK-001 | G2 | Completed; AdmissionSnapshot retains raw 105, mapped 1105, earliest 1103, latest 1107, binding/map/U, operation 1, status 4. |
| ACK-002 | Legacy processSession operation 3 with exact-next valid grant | It retains legacy AdminCode::UnsupportedOperation behavior and consumes the immediate valid grant before rejection. New processAcknowledgement takes no grant and does not alter that legacy rule. |
| ACK-003 | Session operation not 3; then stale/equal shared operation sequence after a floor exists; separately operation 0 before any floor/no session | UnsupportedSessionOperation then WireOperationReplay; neither evaluates a request nor moves ACK/cache/floor. Without a floor, sequence 0 reaches SessionGateRejected rather than a replay code. |
| ACK-004 | Each one of the four ingress facts false | IngressRejected. Each independently preserves a healthy old ACK and S1 cache/floor. |
| ACK-005 | Bad protocol, zero required identity, nonzero flags/reserved, or invalid typed enum | MalformedPacket before ingress/floor/binding, preserving ACK/cache. Typed C++ objects cannot express an on-wire width/length error; that belongs to frozen codec coverage. |
| ACK-006 | Wrong controller, source/session, transition epoch, or map epoch | BindingMismatch after healthy pre-poll, preserving ACK/cache/floor. A false configured target/header verdict instead takes ACK-004 IngressRejected. |
| ACK-007 | Revoked/no established gate | SessionGateRejected, not implicit reconnect or establish. |
| ACK-008 | Duplicate ACK operation sequence 1, then later otherwise-valid operation sequence 2 after ACK-001 | The duplicate is WireOperationReplay; only the newer packet reaches AcknowledgementFrozen. Both leave the original AdmissionSnapshot unchanged. |
| ACK-009 | Status sequence 3 or 5 with current barrier sequence 4 | AcknowledgementStatusMismatch; status must equal barrier sequence exactly. |
| ACK-010 | Raw 102 gives earliest 1100; fresh raw 103 gives earliest 1101 | Equality is AcknowledgementOrdering; the strict plus-one side passes in a fresh fixture. |
| ACK-011 | Age exactly 50 then 51; in a future-skew-2 fixture latest exactly now+2 then now+3 | Equal endpoints pass; one-unit excess is AcknowledgementTimingInvalid. |
| ACK-012 | Raw point outside map, U=6, INT64_MIN subtraction underflow, INT64_MAX addition overflow | MapIntervalInvalid, AcknowledgementTimingInvalid, or MapArithmeticInvalid respectively; never clip/wrap, replace ACK, or advance floor. A false map, U=0, or reversed valid map is a trusted-state/prestate or structural fault before ACK processing. |

## Request, cache, and S1 cases

| ID | Fixture mutation | Required result and preservation |
| --- | --- | --- |
| REQ-001 | G3 | Completed with S1 Accepted; only S1 owns the cache. |
| REQ-002 | No current ACK | AcknowledgementRequired; canonical caller output/cache unchanged. |
| REQ-003 | Raw creation 109 gives earliest 1107; separately raw creation 105 equals ACK raw | AcknowledgementOrdering in both cases. |
| REQ-004 | Each ingress false; every typed decoded request structural field bad; wrong controller/source/session/transition/map binding | IngressRejected, MalformedPacket, or BindingMismatch by fixed precedence; a foreign packet cannot revoke a healthy old demand/ACK. Frozen payload length/width errors are codec tests, not representable typed C++ inputs. |
| REQ-005 | Map/interval/arithmetic failure, protocol/enum/time/nonfinite conversion failure | Exact conversion code; canonical output unchanged and no S1 evaluate. |
| REQ-006 | Duplicate/decrease S1 request sequence, validity 9/101, age 51 after equality 50, expiry equal receive, future creation, component/norm bad | S1Rejected with the real matching DecisionCode, not a wrapper substitute. S1 uncertainty excess is unreachable after a current immutable ACK accepted under the same immutable map/configuration; retain that direct S1 regression rather than privately changing state. |
| REQ-007 | Disarmed ValidateOnly request | S1 ValidatedDisarmed, no active cache. Rearm is a new barrier that clears ACK, so a later ACK and fresh request are required. |
| REQ-008 | Cached old request raw 110/expiry 210 is too old at time 1161; same call sends fresh sequence 2 raw 160/expiry 190 | Benign pre-poll expiry/age may discard old cache but must not veto fresh conversion/evaluation; fresh request replaces it. |
| REQ-009 | At time 1159 retain old sequence 1/raw 120/expiry 220 (age 41); submit sequence 2/raw 110/expiry 210 (candidate age 51) | S1Rejected RequestTooOld is nonlatched and old cache survives. The candidate remains post-ACK, so a subsequent accessor proves preservation rather than earlier ACK rejection. |
| REQ-010 | At time 1150 retain sequence 1/raw 120/expiry 220; submit sequence 2/raw 110/expiry 210 | The candidate is fresh and post-ACK but its producer time regresses. Expect S1Rejected ProducerTimeRegressed; S1 latches, ACK clears, admission closes. It is not ACK ordering/replay. |

## Trusted state and public-method cases

| ID | Fixture | Required observation |
| --- | --- | --- |
| STATE-ACK-001 | Healthy unchanged state plus malformed/foreign ACK/request or rejected administration packet after G3 | Mandatory pre-poll runs; no trusted closure exists, so prior ACK/cache survive and any AdminResult keeps truthful admission_closed. |
| STATE-ACK-002 | New generation with disconnect, required-state/map/context loss, or observed latch, concurrent bad packet | PrestateRejected is primary; guard demand revokes and ACK clears before packet rejection. Output unchanged. A disarm/rearm generation still clears ACK through its barrier, but ValidateOnly is a benign prestate and a subsequent disarmed request reaches S1 as REQ-007. |
| STATE-ACK-003 | Disconnect G=53 then healthy reconnect G=54 | Both barriers clear ACK. Reconnect restores neither ACK nor cache; later post-barrier ACK/request are necessary. |
| STATE-ACK-004 | Same-G full-map mutation; discontinuity; zero/decrease/skip G; invalid enum/agreement; controller mismatch | One TrustedStateFault terminal, safety revoke/ACK clear, no packet/floor/evaluate. Repeats and cross-API calls report retained terminal with zero reports. |
| STATE-ACK-005 | Valid new-G map rebind A to B | Established gate becomes revoked and ACK/cache clear. A packets cannot reopen it; separate B establishment is required. |
| STATE-ACK-006 | State-prefix barrier committed through observe, poll, getter, administration, ACK, or request; successful establish/revoke/clear; S1 latch | Each actual state/admin barrier or latch clears ACK. Successful ACK acceptance alone creates no barrier and retains/opens ACK. Demand expiry alone only clears S1 cache as G5 proves. |

## Counters, floors, and unchanged output

| ID | Setup | Required result |
| --- | --- | --- |
| REP-001 | Unchanged versus first/new-G ACK/request, each success and post-state rejection | Reserve 1 versus 2 normal IDs respectively. New-G rejected action emits barrier plus operation but consumes no unused ID; ACK/request never creates an administrative barrier. |
| REP-002 | last report sequence MAX-3, then new G; separately MAX-2, then new G | First exactly fits MAX-2/MAX-1. Second fails before normal barrier/poll/evaluate/floor mutation and emits only MAX CounterExhausted. Its intentional terminal safety revoke clears cache and ACK. Apply both to later-rejected packets too. |
| REP-003 | last report sequence MAX-2 then unchanged ingress | It uses MAX-1; next normal call emits terminal MAX once. Repeated terminal calls return zero reports/no reuse. |
| REP-004 | Transition epoch reaches MAX then another barrier is needed | The later barrier terminals CounterExhausted without wrap. ACK at retained epoch does not advance it. |
| REP-005 | Establish successful operation 0; ACK successful operation 1; request sequence 1 | Wire floor starts with allowed 0 and becomes 1 at ACK. Request sequence is independent S1 state and never moves wire floor. |
| REP-006 | Caller request output is an initialized noncanonical sentinel | Every false getter, conversion failure, rejected packet, prestate/terminal/capacity path preserves bytes. Only G4 accessor writes canonical output. |
| REP-007 | Ordinary observe/poll/getter with healthy Accepted, NoEligibleRequest, and ValidatedDisarmed S1 states and with closed/open ACK gate | Observe maps normal states to Observed; poll maps Accepted to Observed and both NoEligibleRequest/ValidatedDisarmed to NoEligibleRequest. Only an open-gate accessor copy uses EligibleRequest. A closed gate never copies even if S1 has an old cache. |

## Reachability and arithmetic boundaries

Public ingress tests do not mutate private ACK, guard, session, or map state to
force a nominal converter code. They create the required preceding
establish/barrier/ACK sequence, or use the existing direct C0b.1 conversion
regressions for branches deliberately preempted by the wrapper.

| ID | Fixture | Required reachable outcome |
| --- | --- | --- |
| REACH-001 | Current map valid=false with matching context, map/context disagreement, U=0, or a reversed valid interval | Valid=false reaches PrestateRejected; disagreement, U=0, and reversed valid interval are TrustedStateFault before a request converter call. The public result is not a fabricated request MapInvalid/MapIntervalInvalid. |
| REACH-002 | Request controller/source/session/map binding or transition epoch differs from retained current state | Public request returns BindingMismatch before canonicalizeRequestV1. Direct C0b.1 tests, not private-state mutation, cover its MapBindingMismatch and WireIdentityInvalid branches. |
| REACH-003 | Healthy current map and current request bindings, but raw request endpoint plus signed offset overflows/underflows | MapArithmeticInvalid is public and canonical output remains unchanged. Raw expiry less than/equal creation and nonfinite/enum/version inputs likewise reach their documented WireTimeInvalid/WireNonFinite/WireEnumInvalid/WireVersionInvalid conversion paths. |
| REACH-004 | ACK point map with offset -1000, raw 2105, interval [2000,3000], U=2, receive 1110 | It maps to the same 1105/[1103,1107] golden ACK interval and passes only in a fresh established fixture. |
| REACH-005 | ACK point map offset INT64_MIN/raw 0 in [0,0], offset INT64_MAX/raw 9223372036854775809 in that singleton interval, and exact-fit offset INT64_MAX/raw 9223372036854775808 | The first two are MapArithmeticInvalid because mapping would leave uint64 range. Exact fit maps to UINT64_MAX, then any mapped-plus-U boundary is AcknowledgementTimingInvalid. No unsigned wrap occurs. |
| REACH-006 | ACK point map offset INT64_MIN/raw 9223372036854776913 in a matching singleton interval | Checked negative mapping yields 1105; with U=2 and receive 1110 it reproduces the golden [1103,1107] acceptance in a fresh established fixture. |
| REACH-007 | ACK mapped-minus-U, mapped-plus-U, receive-plus-future, or receive-minus-earliest underflow/overflow | AcknowledgementTimingInvalid. Fixture-specific interval/configuration changes must keep trusted map state structurally valid; no operational limit is inferred. |

## Pass rule and limitation

A future implementation satisfies this oracle only if every listed case observes
the exact primary code, S1 decision, report order/count, correlation,
ACK/session/floor snapshot, and cache/output preservation using real
RequestGuard and TrustedTimeMap behavior. Passing is offline software evidence
only; it does not authenticate a deployed link or establish authority, clock
provenance, physical feasibility, control performance, or flight qualification.
