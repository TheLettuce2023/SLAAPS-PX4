# C1B-D: separate SITL diagnostic selection

2026-09-20. User approved **design only** of a separately labelled SITL
diagnostic state for disarmed request validation. This document does not
release implementation, builds, links, mode selection or outputs. C1A remains
accepted at its passive scope; full C1 and roadmap Phase 2 remain open.
Master and independent design review: PASS at this architecture-only scope.
The subsequent exact API/test design gate C1B-I now passes master/independent
review; see C1B_OFFLINE_API_CONTRACT.md and C1B_OFFLINE_API_TEST_MATRIX.md.
That later contract releases bounded offline implementation, not live operation
or acceptance of unimplemented behavior. This document retains its original
architecture-only scope and historical release boundary.

## Evidence and decision

Facts from the pinned local source (PX4 e880f1cd79e2453ef3dfaf68ee1c067b98ea9a53):

- `RequestGuard.hpp` defines flight-mode `ResearchModeState` independently of
  `ArmState` and `DisarmedPolicy`. `RequestGuard.cpp::contextDecision` checks
  clock, connection, required state, arm/mode knowledge, then `Selected`, before
  reaching `ValidateOnly`. An owner-local flag alone cannot enable validation.
- `evaluate` advances sequence/producer-time floors for a valid disarmed
  request, returns `ValidatedDisarmed` and stores no active request. `revoke`
  clears only the active request; it is not session revocation or a floor reset.
- `WireAdapter` owns one guard and immutable configuration. Generation changes
  revoke its request and clear its ACK, but do not always revoke its session.
  Repeating a generation with changed state is a trusted-state fault.
- `processRequest` reports `S1Rejected` for `ValidatedDisarmed`. Its normal ACK
  gate can be open without any eligible request. `WireAdapter` updates accepted
  history only on actual `Accepted` requests, not disarmed validation;
  `WireStatus` only converts captured evidence.
- C1A measured disarmed publications and native clock brackets. It did not
  qualify estimator validity, lossless arm/mode transition provenance or a
  cross-process clock map. Its missing-sensor and event-capacity warnings remain.

Design decision: add an **immutable validation domain** in a future reviewed
guard/adapter extension, with `Flight` as the unchanged existing path and
`SitlDisarmedDiagnostic` as a distinct opt-in path. This is an additive contract
change, not something the existing implementation already supports. Never set
flight `Selected` to make the diagnostic path work. Do not create a second
guard, shadow request cache, parallel replay floors or a permissive validator.

The proposed extension shares existing structural, identity, session, ACK,
frame/quantity/reference, bounds, checked-time, expiry, uncertainty, sequence
and latch rules. Its one deliberate predicate difference is replacing *flight
mode selected* with *diagnostic selection authorized*, while requiring actual
flight mode **NotSelected** and actual **Disarmed**. Required-state and clock
checks are not weakened. FRD/NED, CG reference, body-held inertial acceleration
and acceleration-only commands are unchanged.

## State and ownership

The future single serialized PX4 admission owner holds one adapter/guard,
one diagnostic selector and one operator broker. Only it mutates selection,
sessions, ACKs, epochs, replay floors and reports. The UI submits intent; it
cannot supply trusted state, clock validity, grants or a guard reference.
The guidance process cannot select, renew, clear or revoke operator authority.

Conceptual types, not released ABI or wire numbers:

- `ValidationDomain`: `Flight` or `SitlDisarmedDiagnostic`, fixed at
  construction and never switchable in that lifetime.
- `DiagnosticSelectionState`: `Disabled`, `SelectedForValidation`, `Faulted`.
  Default is `Disabled`; unknown enum values fail closed.
- `DiagnosticSelectionEvidence`: domain, selector state, nonzero owner-lifetime
  identity, selector epoch, reason, operator attachment/grant correlation and
  report-time trusted-state reference. It is owner-produced, never packet data.
- A prepare ticket belongs to the broker, not a fourth usable selection state.
  Preparing a ticket grants no validation authority.

Existing `ResearchModeState::{NotSelected,Selected,Unknown}` and ordinary
flight API behavior remain unchanged. Diagnostic evidence has its own typed
field and names in samples/reports; it is not packed into unused wire flags,
an Offboard heartbeat, a flight-mode enum or `required_state_valid`.

| Domain and actual state | Diagnostic selection | Permitted result |
| --- | --- | --- |
| Flight, any state | Absent (ordinary Flight construction) | Existing contract, unchanged |
| Flight | Any diagnostic grant/evidence | Reject wrong domain; never enable diagnostics |
| Diagnostic, Disarmed and NotSelected, all prerequisites valid | SelectedForValidation | Fully check request; at most diagnostic validated-disarmed, eligible=false |
| Diagnostic, Disarmed and NotSelected | Disabled | Diagnostic-not-selected rejection; no request validation success |
| Diagnostic, Armed, Unknown, inconsistent or stale arm state | Any | Close diagnostic scope before request processing; never cache/copy demand |
| Diagnostic, Selected or Unknown actual flight mode | Any | Close diagnostic scope; never reinterpret it as diagnostic selection |
| Diagnostic, invalid clock/estimate/connection or lost continuity | Any | No positive validation; invalidate authority as specified below |

## Operator actions and transitions

Reuse the approved cooperative trusted-local operator model, not a claim of
hostile same-user isolation. Add separately typed `PrepareDiagnosticSelection`,
`ConfirmDiagnosticSelection` and `DisableDiagnosticSelection` intents. Do not
overload session-establish or latch-clear actions. Existing grant counters and
one-shot broker sequencing remain authoritative; do not add an unsynchronized
grant allocator. Preparation binds the owner/controller lifetime, domain,
operator attachment, actual state generation, adapter transition epoch,
selector epoch and map identity/epoch. Confirmation uses a fresh owner view,
the existing configured ticket-age checks and exact binding equality.

Enable preconditions: compiled diagnostic capability, explicit diagnostic
domain with ValidateOnly policy, independent attached operator, no unresolved
latch/terminal fault, qualified clocks/transition observer, fresh consistent
actual disarmed and NotSelected state, required estimator evidence valid and
bound transport identity connected. A missing prerequisite rejects enable;
there is no automatic wait-to-enable or queued deferred confirmation.

| Event | Next selector state and required effect |
| --- | --- |
| Construction/restart | Disabled, new lifetime, no inherited ticket/session/ACK |
| Prepare | No selector change; bounded one-shot broker ticket only |
| Valid confirm while Disabled | SelectedForValidation; barrier and session invalidation; no implicit session establishment |
| Duplicate/stale/foreign confirm | Reject; no duplicate effect, epoch advance or replay-floor reset |
| Confirm while already selected | Reject; no renewal or lifetime extension |
| Authorized disable or orderly operator detach | Disabled; invalidate pending tickets, session and ACK before later ingress |
| Trusted state/map/connection invalidation, stale/inconsistent arm observation, valid-context rebind, or recoverable guard latch | Disabled with reason; revoke session/ACK/tickets; no automatic re-enable after recovery |
| Armed or flight Selected observed | Disabled and close diagnostic session before later ingress; do not send disarm or mode commands |
| Structural observer/owner continuity failure, regressing state generation, overflow or terminal adapter failure | Faulted; terminal for this owner lifetime, no UI clear/reset |
| Guard latch clear | Existing independent authorized clear only; selection remains Disabled; distinct session and fresh confirmation still required |

Selection is not a lease refreshed by guidance, heartbeat, status delivery or
requests. It lasts only within its confirmed operator/owner/context lifetime.
Unexpected operator loss must be observable through the approved broker
attachment mechanism; until that mechanism is qualified, live enable is closed.
Same-user process authentication is not inferred from localhost or an ID.
The current broker detach only clears attachment/pending tickets; it does not
notify or revoke the owner. Implementing the table therefore requires an
explicit serialized selection-off notification, not merely reusing detach.
Repeated disable while already Disabled is idempotent for selector state and
epoch; it cannot reset replay floors. Rejected or foreign administrative traffic
does not disable an otherwise valid selection unless a real trusted-state fault
was independently observed.

## Atomic barrier and session rules

Within one owner turn: capture/validate trusted state first; apply safety
invalidation before any queued console or ingress operation; then at most one
ordered operation. State loss concurrent with confirmation wins. Backpressure
or a stalled status sink never holds this serialization domain or delays closure.

A committed selector change reserves report/epoch capacity before mutation,
advances its own monotonic epoch, invokes a diagnostic-specific adapter barrier,
invalidates the current ACK, explicitly invalidates session establishment, and
calls the existing guard revoke without resetting floors. This requires a
reviewed adapter operation: a synthetic physical-state generation or merely
calling `RequestGuard::revoke()` is insufficient. No adapter reconstruction.

The physical trusted-state generation is not fabricated for console actions.
Selection epoch is separately included in retained-state equality and grant
bindings; adapter transition epoch/report sequence carries the resulting
ordering barrier. Combined physical and selection changes use reserved,
chronologically ordered records, never silently overflow the current two/three
report capacities. The implementation gate must define the exact bounded result
types, counter allocation, first-state behavior and non-reportable terminal case.
All existing observation/access, administration and ingress transition paths
must apply the same new comparison/barrier rules; patching only ingress is
insufficient.

After enable, require a new independently authorized session establishment
with a distinct session identity, then the existing strictly post-barrier ACK,
then a freshly created request after that ACK. Selection alone cannot establish
a session. While Disabled, stage and qualify the prospective diagnostic
source/session map first; bind prepare/confirm to that same prospective session.
Mapping a candidate session is not authorization to establish it. After confirm,
establish that distinct session without rebinding the map. If the map or candidate
changes, selection closes and setup restarts; do not create a circular requirement
to establish a session before obtaining selection. Session establish advances
its normal adapter barrier but does not silently disable an otherwise unchanged
selected diagnostic context. Ordinary
session establishment does not change the selector epoch. Successful authorized
revoke/clear closes selection and advances its epoch if the selector changes;
new enable never resurrects prior session/ACK/request data. Selector transitions
themselves preserve floors. Successful authorized establishment of a distinct
session retains the existing guard's session-scoped request-sequence and
producer-time floor reset; it does not reset parser-signing replay or supervisor
operation floors. No validator clock floor is reset except by the existing
specifically authorized latch-clear procedure.

For every diagnostic request, recheck current selection, actual state, clock,
map, connection and ACK within the single owner turn. A successfully validated
request consumes the existing replay/time floors once, but creates no eligible
cache, leaves accepted-flight history unchanged and can never become a demand
on later arming. All diagnostic getters leave caller output unchanged and report
no eligible request. Runtime purpose switching is forbidden, including after
clear/restart-with-reused-identity. Session/admin success is labelled as such,
never mistaken for an accepted acceleration command.

## Guard and report implementation seam (not released code)

One guard uses shared validation logic; diagnostic context checks must be
consistent in evaluate, poll, administration and getter paths. The normal Flight
path retains its current precedence/results and regression expectations. Unknown
domain or diagnostic evidence supplied to Flight is rejected, not ignored.
The diagnostic branch cannot reach the active-request assignment or return an
eligible request, even if its wrapper is misused. Do not implement it by copying
the context and replacing mode/arm/validity fields.

Diagnostic success has a distinct typed result `DiagnosticValidatedDisarmed`
and explicit `domain=SitlDisarmedDiagnostic`, actual arm/mode, selector epoch,
source/session/controller/map/transition bindings, request/report correlation,
age evidence and `eligible_for_control=false`. Underlying shared check results
may retain `ValidatedDisarmed`, but are not flight-domain success reports.
Poll/admin success cannot invent a request-validation result without an actual
correlated request evaluation. Every rejection records its actual primary
cause; do not replace clock/state failures with a cheerful diagnostic-ready flag.

The frozen v1 wire contract has no diagnostic-domain/selector field. Therefore
the first implementation slice is **offline typed/local evidence only**. Never
silently emit new-domain results via the unchanged v1 status mapper, use its
reserved flags, or label actual NotSelected as Selected. A later live diagnostic
status/schema and endpoint-profile review must explicitly carry this namespace
and fail closed for unaware consumers. Existing v1 Flight encoding is unchanged.
No new MAVLink ID, enum value, dialect or transport is allocated by this design.
Future diagnostic identities, sessions, keys and endpoint profiles must be
explicitly domain-bound and separate from flight qualification. Reject
cross-domain replay and negotiation failure rather than silently falling back
to v1. No identity/key generation or endpoint opening occurs in this design.

## Build boundary and unresolved integration inputs

The diagnostic capability must be opt-in, compile-excluded from ordinary/hardware
targets, and unavailable to normal Flight construction. A dedicated Linux host
test target may exercise it with explicitly synthetic provenance; a dedicated
PX4 Linux SITL target is a later release. Build guards must reject hardware,
unqualified lockstep/proxy clocks and accidental default enablement. Portable
flight behavior must not acquire a runtime diagnostic enable parameter.

Still required before positive live validation: reviewed producer-side arm/mode
transition provenance; operational state freshness budgets and estimator signal
inventory; producer/validator clock lifetime/map evidence; operator attachment
loss detection; explicit endpoint/domain binding and diagnostic return schema.
The C1A probe alone supplies none of these proofs. C1H shared-process fixture
state/clock assumptions cannot be imported into PX4 as facts. No simulator truth
may satisfy operational estimator inputs. No new timing/physical values are
chosen here; request TTL is not a state freshness budget.

## Gate and next step

Design acceptance requires master and independent review against
`C1B_SITL_DIAGNOSTIC_SELECTION_TEST_SPEC.md`. This authorizes no implementation.
That architecture review passed after correcting rejected-admin invalidation,
recoverable versus terminal continuity handling, prospective-session map order,
and explicit session-scoped floor semantics. These are design findings, not
measured test results.
Next proposed gate: C1B-I, a narrowly scoped offline implementation/API review
covering immutable purpose, selector/barrier/admin operations, report capacities
and domain-labelled evidence, followed by explicit implementation release.
Live attachment requires its separate provenance/schema gates above. No geometry
input or further objective clarification is needed to complete this design.
