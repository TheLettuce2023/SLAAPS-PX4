# C1B-I: offline diagnostic API contract

2026-09-20. Implements the design decisions in
C1B_SITL_DIAGNOSTIC_SELECTION_DESIGN.md as an API/test contract, **not source
implementation or runtime evidence**. Latest user direction: proceed to the
offline API/test gate. Master and independent exact-design review: PASS.
No live link, PX4/Gazebo build, output, physical operation or wire change is
released here. Full C1 and roadmap Phase 2 remain open.

Subsequent implementation: the private guard portion now passes bounded C1B-G
verification; see C1B_GUARD_IMPLEMENTATION_VERIFICATION.md. The subsequent
C1B-A adapter implementation is recorded separately in
C1B_ADAPTER_IMPLEMENTATION_VERIFICATION.md. The owner/broker portion now passes
bounded C1B-O verification in C1B_OWNER_IMPLEMENTATION_VERIFICATION.md.
Local evidence tooling and its acceptance are recorded separately in
C1B_EVIDENCE_IMPLEMENTATION_VERIFICATION.md and C1B_OFFLINE_EXECUTION_LEDGER.md.
This design contract alone is not implementation acceptance; guard tests alone
do not satisfy the complete finite matrix.

## 1. Scope, compatibility and construction

Pinned source: PX4 e880f1cd79e2453ef3dfaf68ee1c067b98ea9a53. Existing
RequestGuard, WireAdapter, WireStatus and SupervisorBroker are authoritative for
all Flight behavior. Ordinary report bounds remain 2/3/2 for state/admin/ingress.
Normal constructors, payloads and existing enum ordinals must remain compatible.

Add an opt-in standalone Linux host-test target only, proposed option
RESEARCH_SITL_DIAGNOSTIC_TESTS=OFF by default. Only its isolated library and
consumers receive RESEARCH_SITL_DIAGNOSTIC_OFFLINE=1. Require Linux and reject
PX4/NuttX/hardware, lockstep and timestamp-proxy defines in this offline slice.
Do not link two differently compiled definitions of the same class into one
executable. Build ordinary regression executables against the ordinary library;
build diagnostic tests against their separate opted-in copy of shared sources.
A future PX4 SITL target needs a separate release, not removal of these guards.

Construction in the opted-in build:

```cpp
struct DiagnosticOfflineTag {}; // Absent from ordinary build headers.
struct DiagnosticIdentity {
    uint64_t owner_lifetime_id;
    uint64_t fixture_run_id;
};
WireAdapter(const ContractConfig &, const AdapterIdentity &,
            DiagnosticOfflineTag, const DiagnosticIdentity &);
```

Both new identities and the existing controller identity must be nonzero.
The constructor requires a valid existing contract and ValidateOnly policy;
invalid construction is inert, returns NotConfigured with no records, and
does not substitute defaults. It initializes exactly one guard, in immutable
SitlDisarmedDiagnostic domain. No runtime setter, purpose conversion, clone,
reset, mutable adapter reference or second guard is added. Ordinary construction
remains Flight even in an opted-in test executable.

New C++ local enums are not wire enums. Existing DecisionCode/AdapterCode/
AdminCode/IngressCode may append ApiDomainMismatch without renumbering prior
values; V1 mappers must reject it without writing output. Ordinary public APIs
called on a diagnostic instance return that mismatch, zero reports, no guard
call and no output copy; retained snapshots/ACK state remain truthful and
unchanged. Diagnostic APIs called on Flight return WrongDomain with no mutation.
These misuse cases must not reset floors or indirectly select a domain.

## 2. Single-guard seam

Diagnostic construction and guard entry points are private, friend-accessible
only by WireAdapter and a dedicated offline test peer. No UI/parser gets them.
Keep the public Flight RequestGuard API and EvaluationContext layout unchanged.
The adapter supplies its own retained selector value to private diagnostic
guard operations; callers cannot inject selector state via a physical sample.

Private contract signatures (names are binding; support type names below):

```cpp
DiagnosticGuardResult evaluateDiagnostic(const CanonicalRequest &,
    const EvaluationContext &, DiagnosticSelectionState);
DiagnosticGuardResult pollDiagnostic(const EvaluationContext &,
    DiagnosticSelectionState);
DiagnosticGuardResult establishDiagnosticSession(const TrustedSession &, bool authorized,
    const EvaluationContext &, DiagnosticSelectionState);
DiagnosticGuardResult clearDiagnosticLatch(bool authorized, ArmState);
void invalidateDiagnosticSession();
```

DiagnosticGuardResult contains an operation kind (Request/Poll/Establish/Clear),
a diagnostic local result, optional actual common Decision, and no canonical
request output. No implicit conversion to Decision or Flight report exists.
Its exact value representation is:

```cpp
enum class DiagnosticGuardOperation : uint8_t { Request, Poll, Establish, Clear };
struct DiagnosticGuardResult {
    DiagnosticGuardOperation operation;
    DiagnosticCode code;
    std::variant<DiagnosticLocalReason, DecisionCode> cause;
    std::optional<Decision> common_decision;
};
```

The single guard reuses shared payload/time/replay calculations; it does not
clone evaluate into a divergent permissive implementation. A private helper may
parameterize the context predicate without editing any supplied state value.

For diagnostic evaluate: configuration/domain, validator-time observation/latch,
existing session requirement, then context, then the existing ordered request
checks. Context first-match order is clock validity, connection, required state,
known arm, known mode, actual Disarmed, actual NotSelected, valid selector enum,
selector not Faulted, selector SelectedForValidation. The first five retain
their existing common rejection codes; later failures use diagnostic local
reasons NotDisarmed, FlightModeConflict, SelectorInvalid, SelectorFaulted,
SelectionRequired. Payload predicate order and bounds remain RequestGuard.cpp's.
No diagnostic request can reach the active_request_ assignment, regardless of
later arm/mode values or use outside the intended adapter path.

Success consumes existing request sequence and producer-time floors once,
returns RequestValidatedDisarmed with the actual common ValidatedDisarmed
decision, and leaves active request absent. It never changes Flight accepted
history. A valid poll returns Observed/NoEligibleRequest, not request success,
with no request sequence, syntax-success or request-age evidence invented.
Establishment success is SessionEstablished, not request success. Clear success
is LatchCleared, not readiness. Ordinary Flight poll behavior is not changed.

Establishment requires the common session authorization/identity/disarmed
checks AND healthy diagnostic context/selection. Session invalidation clears
active request, sets session_establishment_required_, and retains last successful
session and all floors. A distinct authorized establishment retains the existing
request-sequence/producer-floor reset; it does not reset validator time,
supervisor or signing-replay floors. Clear retains the existing authorized
disarmed latch recovery and validator-clock reset, never selects or establishes
a session. Required-state/clock checks are never skipped for request validation.
At the adapter boundary retain the existing ClearNotLatched rejection: an
authorized clear with no latch does not manufacture a session/selector barrier.

One adapter turn calls pollDiagnostic exactly once after capacity reservation.
The diagnostic establish implementation must not call pollDiagnostic again:
share the existing checked administration body, not its polling wrapper.
Evaluate/establish still check their supplied validator timestamp themselves;
the same timestamp is legal and does not reset or advance the time floor twice.
Only a clock/latch or physical prerequisite failure closes selection during
the prefix. SelectionRequired, NoSession, missing ACK and ordinary rejected
payloads are not safety-loss events and cannot prevent initial enable or
establishment. A disabled-state poll is not positive validation. Clear can
recover a latch with authorized actual Disarmed even when enable prerequisites
are absent; no operation following clear in that turn may re-observe the clock,
select, establish or validate. The next turn supplies a fresh observation.

## 3. Typed adapter operations

All declarations below exist only in the opted-in build. C++17 std::variant
provides exactly one operation payload; no bag of nullable pointers or ignored
extra grant is accepted. Observation inputs and grants are trusted offline
fixture/owner inputs, not authentication mechanisms or operational booleans.

```cpp
enum class DiagnosticSelectionState : uint8_t {
    Disabled = 0, SelectedForValidation = 1, Faulted = 2
};
enum class DiagnosticAction : uint8_t { Enable = 0, Disable = 1 };
struct DiagnosticBinding {
    uint64_t owner_lifetime_id, fixture_run_id, controller_instance_id;
    uint64_t operator_instance, state_generation, transition_epoch, selector_epoch;
    TrustedClockMap prospective_map; // Complete exact value, not epoch alone.
};
struct DiagnosticSelectionGrant {
    uint64_t event_id; // Existing shared supervisor floor + 1, never a new stream.
    DiagnosticAction action;
    DiagnosticBinding expected;
};
struct DiagnosticAdminGrant {
    DiagnosticBinding expected;
    SupervisorEvent event; // Existing Establish/Revoke/Clear action semantics.
};
struct DiagnosticIngress {
    uint64_t owner_lifetime_id, fixture_run_id;
    SessionIngressVerdict authentication;
};
struct DiagObserve {};
struct DiagPoll {};
struct DiagAccess {}; // No request output in the public diagnostic API.
struct DiagEnable { DiagnosticSelectionGrant grant; };
struct DiagDisable { DiagnosticSelectionGrant grant; };
struct DiagEstablish { DecodedSessionV1 packet; DiagnosticIngress ingress;
                       DiagnosticAdminGrant grant; };
struct DiagRevoke { DecodedSessionV1 packet; DiagnosticIngress ingress;
                    DiagnosticAdminGrant grant; };
struct DiagClear { DiagnosticAdminGrant grant; };
struct DiagAcknowledge { DecodedSessionV1 packet; DiagnosticIngress ingress; };
struct DiagRequest { DecodedRequestV1 packet; DiagnosticIngress ingress; };
using DiagnosticTurn = std::variant<DiagObserve, DiagPoll, DiagAccess,
    DiagEnable, DiagDisable, DiagEstablish, DiagRevoke, DiagClear,
    DiagAcknowledge, DiagRequest>;

DiagnosticResult diagnosticTurn(const TrustedStateSample &physical,
                                const DiagnosticTurn &operation) noexcept;
DiagnosticSnapshot diagnosticSnapshot() const noexcept;
```

The complete map binds the prospective source/session; no separate competing
session copy is authoritative. Install/qualify it while Disabled; confirm
against it, then independently establish that same distinct session. A map or
prospective binding change while selected closes selection. Establishing the
matching session advances the ordinary session barrier but not selector epoch.
Only selected, qualified, ACK-current diagnostic sessions may evaluate requests.
DiagnosticIngress must exactly match construction identities and all existing
MAVLink/authentication/channel/target facts; it is explicitly injected offline.
No parser or key is created here.

C1B-A implementation clarification: before any selection grant, only the trusted
owner/private offline test peer can call attachDiagnosticOperator(nonzero_id).
Construction starts unattached. Attachment requires Disabled, no established
session, and a strictly greater ID than the retained operator-lifetime floor;
it cannot replace an attached operator. It consumes no supervisor event and
does not select or validate anything. Private closeDiagnosticLifecycle accepts
an actual optional physical receipt and OperatorDetach/OperatorLost reason,
invalidates session/ACK synchronously, and removes attachment. Missing receipt
terminalizes; a valid already-Disabled close reports AlreadyDisabled without a
selector barrier. The future broker still owns attachment/ticket provenance.
Neither a packet nor a grant establishes operator attachment.

Diagnostic Establish/Revoke additionally require nonzero operation_sequence
and zero producer_acknowledgement_monotonic_us/acknowledged_status_sequence.
These diagnostic envelope constraints do not change the existing Flight parser.
ACK and request admission reuse the existing checked implementations.

State is authoritative inside the adapter; the owner holds it transitively.
No cached owner mirror may overrule it. DiagnosticSnapshot contains domain,
identities, initialized/terminal, selector state/epoch/reason, AdapterSnapshot,
SessionSnapshot and AdmissionSnapshot. It contains no second request cache.
Before first valid observation selector is Disabled, epoch 0, initialized=false.
First valid physical observation initializes epoch 1 and Disabled in its physical
barrier, not an enable action. Further actual selector changes increment once.
Idempotent disable does not advance selector or transition epochs.

Every selector transition explicitly sets session gate EstablishmentRequired,
clears ACK and invokes guard.invalidateDiagnosticSession, retaining session
history. Faulted terminal instead reports Revoked and no current ACK. No fake
physical generation is emitted for operator actions. Same physical generation
with different physical state/map is a structural fault; console transitions
mutate selector state through this administrative API, not the physical sample.

## 4. Ordered turn, reports and counter bounds

DiagnosticReportKind has local values PhysicalBarrier=0, SelectorBarrier=1,
SessionBarrier=2, Operation=3, Terminal=4. DiagnosticCode local values are
Observed=0, NoEligibleRequest=1, SelectionEnabled=2, SelectionDisabled=3,
AlreadyDisabled=4, RequestValidatedDisarmed=5, Rejected=6, Terminal=7,
NotConfigured=8, WrongDomain=9, SessionEstablished=10, SessionRevoked=11,
LatchCleared=12, Acknowledged=13. These numbers have no MAVLink allocation.

Each record contains its domain and fixture identity; report sequence, adapter
transition and selector epochs; actual operation kind/correlation; actual
physical state reference; operator/event consumed flag; post-record snapshots;
optional actual common Decision; tagged primary cause; and eligible=false.
Primary cause is a discriminated variant of diagnostic local reason, existing
DecisionCode, AdminCode or IngressCode, not an untyped integer guessed later.
Names from section 2 plus BindingMismatch, ContextChanged, GrantInvalid,
GrantReplay, CounterExhausted, TrustedStateFault and None are the diagnostic
local reasons; unknown values are rejected. Numeric storage is implementation
local and evidence serializes their exact names.

DiagnosticResult has code, final snapshot, record_count, records[4],
has_operation_record, operation_record_index, grant_consumed and action_performed.
Unused slots are zero-initialized; valid indexes are [0, record_count).
has_operation_record=false means the index is zero and no operation result is
asserted. action_performed refers to the requested operation, never a safety
closure in its prefix. Final snapshot is post-turn; earlier evidence is immutable.
For Request it means the guard actually evaluated the request, including a
rejected evaluation; it is not a success or control-authority flag. Successful
lifecycle close performs invalidation even if selection was already Disabled.

One turn performs these steps, all in the existing serialization domain:

1. Reject invalid construction/wrong domain, or return retained terminal state.
2. Validate physical stream structure/continuity and domain identities. Determine
   physical barrier and any pre-operation safety invalidation. Reserve worst-case
   reports and counters BEFORE any guard poll/evaluate/admin mutation.
3. Commit physical barrier if first/new physical generation; retain real input.
   A physical change clears ACK as before. Close selection on lost prerequisites,
   map rebind, arm/mode conflict or known guard latch, before any requested action.
4. Poll the single guard once with diagnostic context. A newly discovered latch
   or trusted-context failure closes selection before operation evaluation.
   A pre-operation closure forbids enable and request evaluation in this turn.
5. Match grant/domain/context, consume only a well-formed exact-next supervisor
   event, then run at most one operation. Consumption is distinct from successful
   action: later payload/session rejection may follow a consumed authorized event,
   matching existing broker semantics. Wrong/stale/foreign grants never consume.
   Preserve the trusted supervisor-stream rule: zero/malformed grants or a gap
   beyond the exact-next event terminalize, rather than silently skipping an
   event. Duplicate/older well-formed grants reject without consumption. Context
   mismatches reject without consumption; an independently observed safety loss
   can still close selection. Terminal handling never fabricates a consumed event.
6. Capture the actual operation report. If evaluate newly latched or successful
   revoke/clear requires closure, append a selector-close barrier AFTER that report.
   No external callback or later ingress can run between evaluation and closure.
7. Return the final snapshots and exact operation index. Evidence delivery occurs
   outside ownership; no sink can block the safety transition.

| Turn shape | Maximum records | Maximum transition increments |
| --- | --- | --- |
| Observe/poll/access/ACK/request/selector action | physical + selector + operation = 3 | 2 |
| Request discovers producer-time latch | physical + operation + selector-close = 3 | 2 |
| Admin after pre-operation closure | physical + selector-close + session + operation = 4 | 3 |
| Successful revoke/clear closes selected state afterward | physical + session + operation + selector-close = 4 | 3 |

There is at most one selector transition per turn: a pre-close blocks re-enable
and request evaluation; clear remains Disabled. An enable on an unchanged valid
Disabled context is the sole transition in that turn. Reserve the appropriate
table bound, including possible post-evaluation closure, before executing any
mutating guard code. Consume IDs only for emitted records. Each actual barrier
advances adapter transition once (first observation initializes it to one).
All operation records consume one report sequence, not a transition epoch.

Normal report IDs are 1..UINT64_MAX-1; UINT64_MAX is a once-only terminal sentinel.
Epochs are 1..UINT64_MAX after initialization, with checked subtraction before
increment. Capacity exhaustion closes guard/session/ACK and terminalizes selector
without wrapping epochs or inventing a physical sample, emitting the sentinel
once even if unused normal slots remain. Structural terminal faults use the
next normal ID if available, otherwise that sentinel. Preserve last valid
epochs/generation; terminal selector state is an explicit terminal exception
to normal epoch advancement. No repeat terminal reports. Invalid first state
may have epoch zero and state_reference_valid=false; local evidence represents
that honestly rather than manufacturing a v1-compatible epoch. Zero records
or an unavailable operation record never constitute positive validation.

## 5. Broker/owner additions and closure

Use separate diagnostic submission/result validators on the SAME broker/owner.
The current ordinary validator requires exactly two records/transition +1 for
successful admin, and assumes ordinary packet correlation. It must remain
unchanged for Flight, not be weakened to accept diagnostic record shapes.

DiagnosticOwnerView contains the exact binding above, validator time, selector,
terminal and existing supervisor/session-history facts, plus separate can_enable,
can_disable and can_admin predicates. DiagnosticSubmission contains that expected
view plus exactly one typed diagnostic command. Extend the trusted owner seam:

```cpp
bool diagnosticSnapshot(DiagnosticOwnerView &out) noexcept;
OwnerOutcome submitDiagnostic(const DiagnosticSubmission &, DiagnosticResult &out) noexcept;
DiagnosticConsoleResult prepareDiagnosticSelection(const TrustedSession &prospective) noexcept;
DiagnosticConsoleResult confirmDiagnosticSelection(uint64_t ticket) noexcept;
DiagnosticConsoleResult disableDiagnosticSelection() noexcept;
DiagnosticConsoleResult detachDiagnosticOperator() noexcept;
DiagnosticConsoleResult prepareDiagnosticAdmin(Intent intent,
    const TrustedSession &session) noexcept;
DiagnosticConsoleResult confirmDiagnosticAdmin(uint64_t ticket) noexcept;

// Trusted owner interface only; never exposed by the console facade.
enum class DiagnosticCloseReason : uint8_t { OperatorDetach, OperatorLost };
OwnerOutcome closeDiagnosticOwner(DiagnosticCloseReason,
    DiagnosticResult &out) noexcept;
```

The two snapshot method names belong respectively to adapter and owner; their
return types differ because the broker-facing view adds attachment/clock facts.
The redacted value type is:

```cpp
struct DiagnosticConsoleResult {
    ConsoleDisposition disposition;
    BrokerCode broker_code;
    std::optional<DiagnosticCode> diagnostic_code;
    uint64_t ticket;
    bool event_consumed;
    bool action_performed;
};
```

ContextChanged, Expired and quarantine reasons below refer to BrokerCode, not
DiagnosticCode. No adapter call means diagnostic_code is absent; a submitted
rejection has the actual diagnostic code Rejected. It exposes no mutable owner, map, packet,
guard, grant, report or eligible request. Full results go only to the owner audit.
prepareDiagnosticAdmin accepts the existing Establish/Revoke/ClearLatch intents
and retains their session-argument rules. confirmDiagnosticAdmin accepts only
that pending intent/domain; confirmDiagnosticSelection only a selection ticket.
A confirm with the wrong API cannot authorize either action: cancel the matching
ticket, return ContextChanged, and consume no event. A nonmatching ticket cannot
cancel someone else's pending action. Both routes create diagnostic grants and
use the diagnostic result validator, never ordinary AdminResult validation.

One pending ticket and the existing ticket/event/operator floors cover BOTH
domains and all intents. Domain is part of pending context. The broker cannot
attach a Flight and diagnostic owner concurrently or switch a bound owner
lifetime. Confirmation invalidates its matching one-shot ticket even on later
failure. Exact view comparison includes full prospective map, selector epoch,
operator and immutable identities. Ticket-age requirements remain the existing
explicit BrokerConfig, not a new operational default.

Disable is an attached independent operator closure action with a freshly
bound exact-next shared event, no enable-style estimator/map readiness gate.
It does not clear latches or reset time. Missing estimator/map cannot prevent
closing selection. Detach first cancels tickets and invokes a synchronous owner
lifecycle-close operation, then drops attachment. That trusted lifecycle close
does not impersonate a guidance packet or consume a supervisor grant. It uses
the same prevalidated offline state receipt as other owner calls and emits a
selector barrier/operation if representable; it cannot call legacy detach alone.
Add a separate trusted-owner lifecycle-close entry with reason OperatorDetach
or OperatorLost; both share the same closure implementation and report bounds.
closeDiagnosticOwner obtains the serialized owner's retained trusted receipt;
its caller cannot supply physical state or an expected-view gate that could
prevent closure. It cancels pending ingress, disables selection and invalidates
session/ACK even when already Disabled, without resetting any floors. When
already Disabled this invalidation adds no selector epoch/barrier. If no valid
receipt exists it terminalizes without inventing one. Its typed operation kind
is LifecycleClose (not a DiagnosticTurn ingress alternative), with the close
reason captured; no subsequent request runs between close and return. It may
emit physical + selector + operation (at most three records) under the same
reservation rules. Operator loss observation/provenance is injected offline
here; a deployed timeout/detection mechanism is not established by this API.
The necessary adapter seam is private, opted-in and friend-accessible only to
research_sitl::HarnessSupervisorOwner (and the offline test peer):

```cpp
DiagnosticResult closeDiagnosticLifecycle(
    const std::optional<TrustedStateSample> &retained_receipt,
    DiagnosticCloseReason reason) noexcept;
```

DiagnosticCloseReason is declared with the adapter diagnostic types, not in the
console facade. A missing receipt takes the terminal path; a supplied receipt
must be the owner's actual retained observation, subject to the same structural
checks. This seam performs closure directly, not by constructing a fake grant
or invoking a public DiagDisable. No owner friendship is added to normal builds.
Uncertain lifecycle completion quarantines the broker and blocks all ingress;
it cannot fall back to an ordinary detach or report successful disable.

OwnerOutcome::NotSubmitted still guarantees no adapter/event side effect.
If the unified turn already emitted a physical barrier/safety close before
rejecting a stale grant, return Completed with rejected operation/action=false,
not NotSubmitted. Unknown or an inconsistent result quarantines authority and
closes the diagnostic owner. Diagnostic broker validation checks operation index,
counter deltas derived from actual barrier records, binding and event consumption;
it never assumes the last record is the operation or every success advances +1.
Snapshot/readiness flags cannot override an actual adapter rejection.

## 6. Local evidence schema and release boundary

Schema name: research-c1b-offline/v1. Evidence is local JSON, not a new MAVLink
message. Campaign header records source hashes, executable hash, compile domain,
fixture run/owner/controller identity, profile ID and explicit
provenance=synthetic_offline. Each turn records input fixture observation ID,
full physical/context/map values, typed operation/grant/ingress facts, ordered
reports and final snapshots. uint64/int64 values serialize as decimal strings
with strict range validation; no JSON float approximation of identities/times.

Every report explicitly records domain=SitlDisarmedDiagnostic,
eligible_for_control=false, state-reference validity and actual mode/arm values.
Only Request operation + RequestValidatedDisarmed + actual common evaluation
ValidatedDisarmed + exact input correlation counts as positive validation.
Poll/admin readiness or a summary flag cannot count. Last validated correlation
is report history, never accepted-flight history or a second request cache.
Unknown versions/keys/types, duplicate keys, gaps/truncation and unbound reports
fail the oracle. No signing/key claims may be made from injected ingress facts.

Diagnostic records are not accepted by frozen WireStatus V1 overloads; add
negative compile/runtime tests for accidental conversion. No reserved bit,
existing Selected value or new numeric v1 decision conveys this domain.
Future live schema/profile binding, lossless PX4 state provenance, qualified
clock maps and estimator freshness remain separate unimplemented inputs.

The finite cases in C1B_OFFLINE_API_TEST_MATRIX.md are the acceptance oracle.
Master review permits the next isolated offline implementation under the
existing geometry-independent software authorization, within this contract.
Allowed implementation scope is the portable library's guarded diagnostic
seams, Tools/research_sitl/supervisor owner/broker, opt-in standalone tests and
local evidence tooling. This is not acceptance of unimplemented code. Require
fresh ordinary regressions and diagnostic Debug/Release/sanitizer evidence,
plus independent implementation review, before accepting that next gate.
No firmware, live endpoint/schema, default startup, actuator or geometry change
is released. No geometry measurement or additional objectives decision is
needed for the offline work; no physical coefficient or timing budget is chosen.

## 7. Design review record

Master and independent owner/API review completed 2026-09-20. Corrected the
draft's impossible ACK chronology, trusted-stream versus health-fault cases,
clear-without-latch expectation, four-record fixture provenance, missing private
lifecycle seam and untyped console-result reason. Final independent review
reports no remaining must-fix findings. Arithmetic checking confirms 1,024
predicate tuples, ACK interval [9958,9962], and request ages 12/14 us in the
synthetic trace. These are design calculations, not executed admission tests.
Existing source behavior is the pinned-source fact; future diagnostic behavior
is a design requirement; all fixture numbers are explicitly synthetic.
