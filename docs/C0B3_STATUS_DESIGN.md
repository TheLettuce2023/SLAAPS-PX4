# C0b.3 Local Status Mapping Design

Status: reviewed design and offline implementation accepted, 2026-09-19.
Verification and tested hashes are recorded in PHASE_GATE_STATUS.md. This is an offline,
payload-only design; it adds no dialect ID, MAVLink frame/CRC/signing, sender,
rate, transport, controller, output, SITL, firmware, or hardware work.

## Boundary and ownership

The status mapper converts one already-emitted adapter report into the frozen
106-byte `RESEARCH_ACCELERATION_STATUS` value.  It never receives a
`WireAdapter`, `RequestGuard`, mutable snapshot, cached request, map service,
or clock.  It cannot poll, evaluate, allocate a sequence, repair a missing
epoch, or infer whether a prior demand survived a rejection.  The existing S1
guard remains the only request cache and eligibility authority.

The status sequence is the report's already-assigned `report_sequence`, not a
new counter.  A report with zero count does not produce a retrospective status.
Status transport backpressure or conversion failure never delays state polling,
revocation, epoch mutation, or report emission.

The only candidate implementation paths are a report-evidence addition in
`WireAdapter.hpp/.cpp`, new pure `WireStatus.hpp/.cpp`, independent
`WireStatusTest.cpp`, minimal standalone CMake registration, and an
independent Python/C++ payload-vector check.  `RequestGuard.*`,
`TrustedTimeMap.*`, the codec, and all live paths remain unchanged.

## Report-time evidence

The final result snapshot is not evidence for earlier records.  C0b.3 appends
the following immutable value to every `AdapterReport`, `AdminReport`, and
`IngressReport`, and populates it immediately before that individual report is
stored.  A mapper consumes only the report plus this value.

```cpp
enum class StatusDecisionOrigin : uint8_t { None, S1, Local };

struct ReportEvidence {
    bool captured;
    uint64_t controller_instance_id;
    bool state_reference_valid;
    uint64_t clock_mapping_epoch;
    ArmState arm_state;
    ResearchModeState mode_state;
    SessionGate session_gate;
    bool transport_connected;
    bool clock_mapping_valid;
    SourceId trusted_source;
    SessionId trusted_session;
    bool packet_mavlink2;
    bool packet_parser_authenticated;
    bool packet_header_source_bound;
    bool packet_target_bound;
    bool acknowledgement_current;
    bool has_last_accepted_sequence;
    uint64_t last_accepted_sequence;
    bool last_accepted_authenticated;
    bool latched;
    DecisionCode first_latched_code;
    uint64_t rejection_count;
    bool syntactically_valid;
    bool conservative_age_valid;
    TimeUs conservative_age_us;
    StatusDecisionOrigin decision_origin;
    uint16_t local_wire_decision_code;
};
```

`local_wire_decision_code` is present only for a local primary whose existing
aggregate code cannot retain the precise frozen cause.  It is computed by the
adapter when reporting, not guessed by the mapper, and must be a permitted
nonzero v1 decision value.  `StatusDecisionOrigin::S1` means the report's S1
decision is the primary decision.  `Local` means this captured code is primary.
`None` is valid only for a healthy class-5 snapshot; it serializes decision 0.
Any other absent/unknown origin fails conversion without writing output.

Capture order is binding:

- An exogenous barrier captures after its own state mutation/revoke/ACK clear
  and before an S1 call or later session action.  Its captured ACK gate must be
  closed.  In establish, the exogenous barrier therefore sees the old session;
  the administrative barrier and action see the established session.
- Update latch, first cause, rejection count, syntax, and age from each actual
  S1 poll, evaluate, establish, or clear decision before storing that associated
  operation report.  A barrier retains the previously observed diagnostic
  values; it never fabricates a poll.
- A successful administrative action precedes its administrative barrier.
  That barrier captures the actual action's updated diagnostic state: after a
  successful clear it is non-latched. The earlier exogenous barrier remains
  latched, and neither historical record is rewritten.
- Update `last_accepted_sequence` only after S1 data `evaluate()` returns
  `Accepted`; record the request sequence and its authenticated ingress origin.
  Reset it only after a successful distinct trusted-session establishment.
  Revoke, expiry, ordinary polling, ACK success, `ValidatedDisarmed`, and a
  rejected request do not move it.  It is history, never eligibility.
- Packet ingress facts are captured only on the report correlated to that
  packet.  An ordinary snapshot may use `last_accepted_authenticated` only for
  a real S1-eligible cached request.  An unsolicited barrier/fault never
  inherits a packet's authentication.
- A structural terminal captures `state_reference_valid = false`; state wire
  fields become unknown/faulted.  If its controller or transition epoch is
  zero, no valid v1 status can represent it and conversion fails closed.  It
  must not substitute one or copy a later epoch.

## Pure API and payload value

`WireStatusV1` is a typed value in frozen payload order, never a packed C++
struct.  `StatusRecipient` has no defaults.

```cpp
enum class StatusDelivery : uint8_t { Directed, OperatorBroadcastSnapshot };
struct StatusRecipient {
    uint8_t target_system;
    uint8_t target_component;
    StatusDelivery delivery;
};

struct WireStatusV1 {
    uint64_t controller_instance_id, transition_epoch, clock_mapping_epoch;
    uint64_t source_id, session_id, correlation_sequence, last_accepted_sequence;
    TimeUs validator_time_us, status_sequence;
    uint64_t rejection_count;
    TimeUs conservative_age_us;
    uint32_t protocol_version;
    uint16_t decision_code, first_latched_code, status_flags;
    uint8_t target_system, target_component, status_class, arm_state;
    uint8_t research_mode_state, session_state, transport_state, clock_mapping_state;
};

enum class StatusBuildCode : uint8_t {
    Ok, InvalidRecipient, MissingEvidence, InvalidIdentity, InvalidReport,
    UnrepresentableDecision, InvalidStateCombination
};
enum class StatusEncodeCode : uint8_t { Ok, InvalidStatus };

StatusBuildCode buildStatusV1(const AdapterReport &, const StatusRecipient &, WireStatusV1 &);
StatusBuildCode buildStatusV1(const AdminReport &, const StatusRecipient &, WireStatusV1 &);
StatusBuildCode buildStatusV1(const IngressReport &, const StatusRecipient &, WireStatusV1 &);
StatusEncodeCode encodeStatusV1Payload(const WireStatusV1 &, uint8_t (&output)[106]);
bool mapDecisionCodeV1(DecisionCode, uint16_t &output);
```

All five functions are pure and leave output byte-for-byte unchanged on every
failure.  The three builders first validate report kind/correlation/evidence,
then construct a local value, validate every frozen C0a status rule, and assign
only on success.  The encoder accepts an already-built typed value only after
the same structural validation and writes exactly 106 little-endian payload
bytes.  It has no trim option because byte 105 is necessarily nonzero in a
valid status.  No overload accepts a final `AdapterResult`, `AdminResult`, or
`IngressResult` because that would fabricate earlier report state.

mapDecisionCodeV1 exposes the exhaustive named S1 mapping for direct independent
conformance tests, including S1 failures preempted by the wrapper. Synthetic
report-value tests may check mapper rejection, but must not claim those values
were emitted by a real adapter; historical integration fixtures use real reports.

Directed delivery requires both target IDs nonzero.  Broadcast delivery requires
both zero and class 5 only.  Mixed zero/nonzero targets and a broadcast packet,
session, transition, or fault report fail.  Protocol is the fixed v1 value.

## Classes, correlation, and state fields

| Report | Class | Correlation |
| --- | ---: | --- |
| Adapter observation/poll/access | 5 snapshot | zero |
| Adapter barrier | 3 transition | zero |
| Adapter terminal | 4 fault | zero |
| Admin operation/terminal with packet | 2 session | captured packet `(source, session, operation_sequence)` |
| Admin operation/terminal without packet | 2 session | zero |
| Admin exogenous/administrative barrier | 3 transition | zero |
| Ingress ACK operation/terminal | 2 session | captured ACK tuple |
| Ingress request operation/terminal | 1 request | captured request tuple |
| Ingress exogenous barrier | 3 transition | zero |

Packet-triggered terminal reports retain their packet class and correlation;
they do not become an uncorrelated class-4 fault.  Class 3/4/5 always has zero
correlation.  Source/session are the correlated packet values for classes 1/2;
otherwise they are captured trusted ownership when known, or both zero.  A
rejected packet never supplies ownership to a snapshot.

Captured enums map exactly as follows: arm `Disarmed/Armed/Unknown -> 1/2/3`,
mode `NotSelected/Selected/Unknown -> 1/2/3`, session
`NoTrustedSession/Established/EstablishmentRequired/Revoked -> 1/2/3/4`, and
any captured latch -> session state 5.  Transport is connected 1/disconnected
2/unknown 3; a state-reference fault is unknown.  Clock mapping is valid 1,
invalid 2, and faulted/unknown 3.  A captured valid map supplies its nonzero
epoch; a map-invalid state uses zero epoch.  Controller and transition epochs
must both be nonzero in every serialized v1 status.

`LATCHED` is set exactly from captured latch state; its first cause is the
explicit S1 mapping of captured producer-time regression (18) or validator-time
regression (19), and zero otherwise.  Any other latch cause is unrepresentable
and fails closed.  `CONSERVATIVE_AGE_VALID` and age are copied only from a
captured S1 decision with valid age.  `HAS_LAST_ACCEPTED_SEQUENCE` is copied
only from captured history; its value is otherwise zero.

`SYNTACTICALLY_VALID` and conservative age are copied only when
`decision_origin == S1` and the report's actual `s1_decision` is the operation
being reported.  A local rejection carrying a benign prestate decision may not
copy syntax, age, or authentication from an older cache.  `AUTHENTICATED_LINK` is
set for a packet only when both MAVLink2 and parser authentication were true;
for a snapshot/access it is set only for a real eligible S1 cache with captured
`last_accepted_authenticated`.  Neither header binding nor a supervisor grant
sets it.  `ELIGIBLE_FOR_CONTROL` is set only when the captured primary S1
decision is `Accepted` and that actual `s1_decision.eligible_for_control` is
true, class is request or snapshot, syntax/authentication are
true, ACK is current, arm is armed, mode selected, session established,
transport connected, map valid, source/session nonzero, and no latch.  It is
never set for ACK, session administration, a barrier, a terminal, or merely an
old cache.  It means no execution, tracking, allocation, or output claim.

## Decision conversion

S1 conversion is an explicit exhaustive switch, never an enum cast:

| S1 `DecisionCode` | Wire code |
| --- | ---: |
| Accepted, ValidatedDisarmed, NotConfigured, WrongProtocolVersion, WrongSource, NoSession, WrongSession | 1, 2, 3, 4, 5, 6, 7 |
| UnauthorizedSessionEstablishment, SessionChangeWhileArmed, InvalidTrustedSession, SessionEstablishmentRequired | 8, 9, 10, 11 |
| DuplicateSequence, OutOfOrderSequence, ProducerTimeNotIncreasing, ClockMappingInvalid, ClockUncertaintyExceeded, ProducerTimeInFuture, ProducerTimeRegressed, ValidatorTimeRegressed | 12, 13, 14, 15, 16, 17, 18, 19 |
| RequestTooOld, ValidityOutOfRange, RequestExpired, TimeArithmeticInvalid, RequiredStateInvalid | 20, 21, 22, 23, 24 |
| WrongFrame, WrongQuantity, WrongReferencePoint, NonFinite, AxisOutOfBounds, NormOutOfBounds | 25, 26, 27, 28, 29, 30 |
| DisarmedRejected, ModeNotSelected, ArmStateUnknown, ModeStateUnknown, Disconnected, NoEligibleRequest, Latched | 31, 32, 33, 34, 35, 36, 37 |

For a report with primary S1 origin, this mapping is its decision code even if
the local report code is a wrapper aggregate.  For local origin, the adapter
captures precisely one frozen code; the builder validates it is 1--37 or
64--83 and applies the following required mapping policy.

| Local report code | Captured local wire code or rule |
| --- | --- |
| Any healthy barrier | 70 `EPOCH_ACK_REQUIRED`, only if it is a real class-3 barrier and captured ACK is closed; otherwise fail. |
| Adapter `Observed` with S1 decision / `NoEligibleRequest` / `GuardRejected` / `EligibleRequest` | S1 mapping. |
| Adapter `Observed` without S1 decision | 0 only for class-5 healthy snapshot; barrier uses 70. |
| Adapter `TrustedStateFault`, `CounterExhausted`, `NotConfigured` | 79, 78, 3. |
| Admin `Completed` | 1; successful revoke is local acceptance, not an invented S1 return. |
| Admin `NotConfigured`, `TrustedStateFault`, `CounterExhausted` | 3, 79, 78. |
| Admin `SupervisorMissing`, `SupervisorDuplicate`, `SupervisorStreamFault`, `MalformedPacket`, `UnsupportedOperation`, `PacketOperationReplay` | 8, 75, 79, 74, 73, 75. |
| Admin `IngressRejected`, `BindingMismatch`, `EstablishmentContextRejected`, `ContextRejected`, `NoEstablishedSession`, `ClearNotLatched` | required captured precise local code; no aggregate fallback. |
| Admin `S1ActionRejected` | captured action S1 mapping. |
| Ingress `Completed` | 1; for a request this additionally requires captured S1 Accepted. |
| Ingress `PrestateRejected`, `S1Rejected` | captured S1 mapping. |
| Ingress `MalformedPacket`, `UnsupportedSessionOperation`, `WireOperationReplay` | 74, 73, 75. |
| Ingress `IngressRejected`, `SessionGateRejected`, `BindingMismatch`, `AcknowledgementTimingInvalid`, `WireEnumInvalid`, `WireTimeInvalid` | required captured precise local code; no aggregate fallback. |
| Ingress `AcknowledgementRequired`, `AcknowledgementFrozen`, `AcknowledgementStatusMismatch` | 70, 71, 80. |
| Ingress `AcknowledgementOrdering`, `MapInvalid`, `MapIntervalInvalid`, `MapArithmeticInvalid`, `WireVersionInvalid`, `WireNonFinite` | 71, 15, 77, 82, 4, 28. |
| Ingress `TrustedStateFault`, `CounterExhausted`, `NotConfigured` | 79, 78, 3. |

The adapter captures the first matching detail below at report emission.  These
tables are part of the contract; the mapper never recomputes them from a later
snapshot.

| Aggregate family | Ordered captured detail -> frozen code |
| --- | --- |
| `IngressRejected` or Admin `IngressRejected` | MAVLink2 missing -> 64; parser authentication missing -> 65; header/source binding missing -> 5; target binding missing -> 66. |
| `BindingMismatch` | controller instance -> 68; transition epoch -> 69; map epoch -> 72; payload source differing from configured source -> 5; session differing from retained trusted session -> 7; otherwise current trusted-map source/session binding -> 81. |
| Admin grant-side `BindingMismatch`, before packet/map bindings | wrong expected controller -> 68; generation -> 79; transition epoch -> 69; wrong action or required binding presence -> 8; grant owner -> 5; grant session -> 7. For clear, owner/session comparisons use the retained successful-session history; an unexpected binding with no history -> 10. Remaining packet/map checks use the preceding binding row. |
| `EstablishmentContextRejected` | same last successful session -> 7; arm unknown -> 33; armed -> 9; invalid map -> 15; disconnected -> 35; required state invalid -> 24; mode unknown -> 34; latched prestate -> 37. |
| `ContextRejected` for revoke or clear | arm unknown -> 33; armed -> 9. |
| `NoEstablishedSession` / ingress `SessionGateRejected` | gate none -> 6; revoked -> 76; establishment required -> 11. |
| `AcknowledgementTimingInvalid` | checked mapped-minus/plus-U, future-limit, or age arithmetic -> 23; uncertainty over configuration -> 16; earliest after receive -> 17; age over limit -> 20; latest beyond future limit -> 17. |
| `WireEnumInvalid` | frame -> 25; quantity -> 26; reference point -> 27. |
| `WireTimeInvalid` | raw expiry not later than creation -> 21; any retained checked-time arithmetic failure -> 23. |

The table ordering is also the primary-failure ordering.  It is used only after
the report type's existing high-level predicate order (malformed, ingress,
operation replay, gate, binding, map, ACK, conversion) has selected that
aggregate family.  Thus a false ingress verdict wins before a packet binding
mismatch, and a map interval failure wins before ACK timing.

`SupervisorMissing -> 8` uses the existing S1
`UnauthorizedSessionEstablishment` wire meaning as the shared missing
administrative-authorisation outcome.  It does not assert MAVLink-link
authentication.  `TrustedStateFault` and `SupervisorStreamFault -> 79` mean
the complete referenced generation is untrustworthy, including map/context or
supervisor-stream faults; they intentionally do not claim one specific lower
level cause.

`ClearNotLatched` is a non-packet local action with no truthful frozen decision
code.  It is always `UnrepresentableDecision`; the builder leaves output
unchanged.  It must not be relabelled as `Latched`, an authorisation failure,
or acceptance.  Any aggregate fact outside the tables is likewise
unrepresentable rather than relabelled as malformed or accepted.

The builder checks failure in this exact order: (1) report-kind/class and
correlation shape (`InvalidReport`); (2) captured evidence presence
(`MissingEvidence`); (3) recipient/delivery shape (`InvalidRecipient`); (4)
nonzero controller and transition epoch (`InvalidIdentity`); (5) decision
origin, required actual S1 decision, and required captured local detail
(`UnrepresentableDecision`); then (6) all remaining frozen status field/flag
relationships (`InvalidStateCombination`).  A zero-epoch terminal therefore
fails at identity even when its local fault code is otherwise known.  This
order is deterministic for every overload and encoder prevalidation.

All terminal reports use state_reference_valid false: a capacity failure may
precede commitment of a newly observed state, so the retained epoch must not be
paired with an uncommitted newer context. Wire arm/mode/transport are unknown,
map state is faulted and map epoch zero; separately retained session/latch/history
facts remain attributable. This is diagnostic uncertainty, not an inferred
physical vehicle state.

Historical integration vectors are emitted by WireStatusTest's explicit
`--emit-vectors` mode and checked by a new offline Python test script against
independently specified fields and the oracle's fixed golden hex. No expected
answer is generated from the mapper under test. The ordinary C++ test mode
checks direct mapping/encoding and invalid typed inputs; all older suites remain
unchanged. New host fixtures/scripts stay under Tools/research_sitl/wire.

Primary local failure wins over a benign prestate `Accepted`; an actual S1
decision wins only for the rows explicitly marked S1.  Unknown enum values,
decision code 0 outside healthy class 5, a success code inconsistent with its
captured operation, or any codec-invalid combination fail conversion.

## Offline evidence and gate

The independent oracle must verify report-time evidence across establish's
three distinct records, arm barrier, ACK, accepted/rejected request, access,
expiry, revoke/re-establish, producer/validator latch and clear, invalid-first
terminal, counter exhaustion, and repeated terminal with zero reports.  It must
prove mapper failure does not mutate S1, adapter counters, ACK, request output,
or the report.

One independently transcribed vector set must assert every named S1/local map,
all classes/correlations, 64-bit extrema, absent age/last-sequence fields,
barrier code 70, latch causes, eligibility false-positive rejection, and exact
106-byte C++/Python payload equality.  Passing remains offline report/payload
evidence only.  It does not establish dialect interoperability, live signing,
deployment authority, clock provenance, vehicle feasibility, control behavior,
or physical operation.
