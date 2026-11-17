# C0b Research Acceleration Adapter Design

## Scope And Release Boundary

Current update (2026-09-19): C0b.1, C0b.2 state/session/ACK-request admission,
and C0b.3 historical status mapping have passed their isolated host gates.
The concrete C0B2_ADMISSION_DESIGN.md and C0B3_STATUS_DESIGN.md supersede the
unreleased/provisional descriptions below. PHASE_GATE_STATUS.md records tested
artifacts. No generated dialect, live receiver, PX4 mode, controller or output
path is implemented by these host libraries. Remaining text is historical
design context, not the current implementation-release boundary.

Session-administration review (2026-09-18): `C0B2_SESSION_ADMIN_DESIGN.md`
and `C0B2_SESSION_ADMIN_TEST_SPEC.md` now provide the concrete bounded session
API and offline oracle, superseding provisional session details here and in the
state contract. The dated master gate controls implementation release. This
design adds no runtime interface, ACK/request admission or actuator path.

This document designs the next inactive bridge between the frozen C0a payload shape and the completed S1 `research_acceleration::RequestGuard`.
C0b.1 and the closed C0b.2 state core are implemented and verified for their isolated offline scope. The current dated gate in `PHASE_GATE_STATUS.md` governs those releases, not the historical provisional APIs here. The state core never admits a request. C0b.2 session/ingress and C0b.3 remain unreleased.

C0b.2 review update (2026-09-18): `C0B2_STATE_CONTRACT.md` and
`C0B2_TRANSITION_TEST_SPEC.md` supersede the provisional C0b.2 API proposals
throughout this document, including the externally supplied guard/configuration
view and independent supervisor-transition generation. The new direction uses
one privately owned immutable configuration/guard pair and one exogenous state
generation. The state-core design now specifies bounded two-report returns,
one actual S1 poll per valid public operation, closed-only access and observable
counter boundaries. Only the current disposition in `PHASE_GATE_STATUS.md`
may release its named implementation paths. C0b.1 evidence is unchanged.
It does not allocate a MAVLink dialect or message ID, parse a MAVLink frame, connect a serial/network link, publish uORB, select a mode, publish status, or create torque, thrust, EDF, canard-servo, or other actuator output.

The research vehicle remains a four-independent-canard, fixed-axis-EDF vehicle with no main wing or thrust vectoring.
That configuration is context for the later controller and allocator, not an input to C0b.
C0b has no physical value, control gain, acceleration limit, command rate, deadline, fallback, plant model, or claim of tracking/feasibility.

C0a accepts only stateless payload bytes.
S1 owns configuration validation, request bounds, sequence/time replay floors, request eligibility, latching, and the sole authorised latch-clear behaviour.
C0b is deliberately a narrow trusted-input adapter that constructs an S1 `CanonicalRequest` and serializes calls to the existing S1 object.
It must not duplicate or replace S1.

The current roadmap remains incomplete at Phase 2, and Phases 3 onward are unreleased.
The limited C0b.1 acceptance does not approve the broader adapter or complete Phase 2.

## Design Facts And Boundaries

The frozen wire request contains raw producer creation and exclusive-expiry times, source/session identity, controller/transition/map epochs, three finite binary32 FRD acceleration components, and fixed v1 frame, quantity, and CG-reference enumerators.
It contains no mapped validator times, map offset, uncertainty, header identity, authentication verdict, supervisor grant, or state validity.

The future adapter receives those missing values only from trusted local inputs.
It never learns them from an untrusted payload, an unsolicited heartbeat, a decoder result, or simulator truth.
The Python C0a codec is a payload fixture tool; C0b does not embed, execute, or depend on Python at runtime.
Before a generated dialect exists, a pure C++ test fixture provides decoded v1 records.

The existing S1 guard is stateful and explicitly not thread safe.
It observes a single validator-time oracle in every administration, evaluation, poll, and eligible-request call.
It preserves replay floors across `revoke()`, latches validator and producer clock regressions, and accepts a trusted session only through a supervisor-authorised disarmed call.
S1 does not know the wire controller instance, transition epoch, trusted clock-map binding, header authentication, or producer acknowledgement barrier.
Those are C0b responsibilities.

## Proposed Source Ownership And Slices

The C0b.1 paths below are implemented; C0b.2/C0b.3 paths remain proposals for a later separately reviewed assignment.

| Slice | Proposed paths | Narrow responsibility |
| --- | --- | --- |
| C0b.1 | `src/lib/research_acceleration/TrustedTimeMap.hpp`, `TrustedTimeMap.cpp`, `TrustedTimeMapTest.cpp` | Stateless trusted-map validation and checked raw-time canonicalization only. |
| C0b.2 | `src/lib/research_acceleration/WireAdapter.hpp`, `WireAdapter.cpp`, `WireAdapterTest.cpp` | One serialized wrapper around a supplied S1 guard, trusted context, session administration, epoch barrier, acknowledgement, and polling. |
| C0b.3 | `src/lib/research_acceleration/WireStatus.hpp`, `WireStatus.cpp`, `WireStatusTest.cpp`, plus explicitly assigned host-vector fixtures | Local structured decision records and C0a/C++ cross-language payload vectors; no transport sender. |

None of these paths may include MAVLink generated headers, uORB, HRT, parameters, work queues, commander, estimator, Gazebo, output, allocator, actuator, board, or startup headers in C0b.1.
C0b.2 may be made a pure library too by accepting all time/state/auth inputs as values from its caller.
Only a later reviewed live-ingress wrapper can bind those values to PX4/MAVLink APIs.

## C0b.1 Trusted Time Map And Canonicalization

### Proposed Data API

```cpp
namespace research_acceleration
{

struct TrustedClockMap {
    bool valid;
    SourceId source;
    SessionId session;
    uint64_t controller_instance_id;
    uint64_t map_epoch;
    int64_t validator_minus_producer_us;
    TimeUs uncertainty_us;
    TimeUs valid_from_producer_us;
    TimeUs valid_until_producer_us;
};

struct WireRequestV1 {
    uint64_t source_id;
    uint64_t session_id;
    uint64_t sequence;
    TimeUs producer_monotonic_us;
    TimeUs producer_expiry_monotonic_us;
    uint64_t controller_instance_id;
    uint64_t transition_epoch;
    uint64_t clock_mapping_epoch;
    float acceleration_x_frd_m_s2;
    float acceleration_y_frd_m_s2;
    float acceleration_z_frd_m_s2;
    uint32_t protocol_version;
    uint8_t frame;
    uint8_t quantity;
    uint8_t reference_point;
};

enum class CanonicalizeCode : uint16_t {
    Ok, MapInvalid, MapBindingMismatch, MapIntervalInvalid, MapArithmeticInvalid,
    WireVersionInvalid, WireEnumInvalid, WireTimeInvalid, WireNonFinite,
    WireIdentityInvalid
};

CanonicalizeCode canonicalizeRequestV1(const WireRequestV1 &wire,
                                        const TrustedClockMap &map,
                                        CanonicalRequest &canonical);

} // namespace research_acceleration
```

`WireRequestV1` is an already C0a-structurally-valid decoded record.
The C0b.1 function does not accept bytes or parse fields that C0a owns, and it returns no partial `canonical` value on failure.
Its caller must discard its output object when the code is not `Ok`.
The deliberately trimmed C++ record omits C0a target IDs, flags, reserved bytes, payload lengths, and trailing-zero handling.
Those are mandatory upstream C0a preconditions, not C0b.1 checks; C0b.1 makes no C++ framing or complete structural-validation claim.

The map is valid only when `valid` is true, every binding identifier is nonzero, `uncertainty_us` is nonzero, and `valid_from_producer_us <= valid_until_producer_us`.
The request source/session/controller/map-epoch must exactly match the map bindings.
Both request endpoints must be inside the inclusive producer interval: creation must be at or after `valid_from_producer_us`; creation and the exclusive-expiry endpoint must be at or before `valid_until_producer_us`.
The request's raw expiry must be strictly later than creation.

### Checked Signed Offset Conversion

For each producer endpoint `p`, calculate validator time from the single frozen offset `d`:

`v = p + d`.

This must not cast a negative signed value to an unsigned value or negate `INT64_MIN`.
The concrete primitive is:

```cpp
bool addSignedOffset(TimeUs producer, int64_t offset, TimeUs &validator)
{
    if (offset >= 0) {
        const TimeUs magnitude = static_cast<TimeUs>(offset);
        return checkedAdd(producer, magnitude, validator);
    }

    const TimeUs magnitude = static_cast<TimeUs>(-(offset + 1)) + 1U;
    return checkedSubtract(producer, magnitude, validator);
}
```

`checkedAdd` rejects `producer > UINT64_MAX - magnitude`.
`checkedSubtract` rejects `producer < magnitude`.
The `offset + 1` form is mandatory for `INT64_MIN`; it produces magnitude `2^63` without signed overflow.
No saturating conversion, wrap, clamp, floating time calculation, or alternate offset is allowed.

Canonicalization maps both raw endpoints independently with this same offset, then checked-subtracts each pair to prove equal raw and mapped durations before populating S1.
It passes the trusted map uncertainty unchanged as `clock_mapping_uncertainty_us`.
S1 remains responsible for its conservative age, future-skew, expiry, and uncertainty-limit checks.
C0b.1 must not pre-accept a request because mapping succeeded.

The exact v1 mappings are checked, never `static_cast`: wire frame `1` to `Frame::Frd`, quantity `1` to `Quantity::InertialAcceleration`, and reference point `1` to `ReferencePoint::Cg`.
C0b.1 has no S1 configuration input, so it checks wire `protocol_version` only against frozen v1 value `0x00010000`.
C0b.2 separately checks compatibility with its explicitly supplied immutable S1 configuration view before it calls the guard; it must not infer configuration from `RequestGuard`, which exposes no getter.
The three wire accelerations must be finite before a `Vec3f` is formed.

## C0b.2 Serialized State, Session, And Acknowledgement Wrapper

### Trusted Inputs Remain Separate

The later wrapper receives four independent classes of input:

| Input class | Source | Packet authority | C0b use |
| --- | --- | --- | --- |
| Decoded wire record | C0a or generated-dialect decoder | None beyond structural content | Candidate request/session fields only. |
| Authenticated ingress identity | MAVLink parser and configured local link binding | Not a packet payload | MAVLink-2/authentication and header/channel/source-binding verdict. |
| Trusted state and map | PX4-side state observer/map service or pure fixture | Not a packet payload | Validator time, arm/mode/state validity, transport, observer continuity, controller/transition generations, map. |
| Trusted supervisor event | Independent local supervisor interface | Not a packet payload | Session establish/revoke and latch-clear authority. |

Header system/component identity and payload `source_id` are identities, not credentials.
An authenticated link is still not a supervisor grant.
A session ID is a replay namespace, not a credential.
A packet must never set a boolean called authorised, map-valid, or state-valid inside the wrapper.

### Proposed API

```cpp
struct AdapterIdentity {
    uint64_t controller_instance_id;
};

struct AdapterConfigurationView {
    uint32_t s1_protocol_version;
    TimingRequirements s1_timing;
};

struct TrustedIngressContext {
    EvaluationContext request_guard_context;
    uint64_t state_generation;
    uint64_t supervisor_transition_generation;
    bool observer_continuous;
    bool mavlink2_frame;
    bool parser_authenticated;
    bool header_source_binding_valid;
};

enum class SupervisorAction : uint8_t { EstablishSession, RevokeSession, ClearLatch };

struct TrustedSupervisorEvent {
    SupervisorAction action;
    bool independently_authorised;
    TrustedSession requested_session;
    uint64_t expected_controller_instance_id;
    uint64_t expected_transition_epoch;
};

enum class AdapterCode : uint16_t {
    Ok, Mavlink2Required, AuthenticationFailed, SourceBindingFailed,
    ControllerInstanceMismatch, TransitionEpochMismatch, ClockEpochMismatch,
    EpochAcknowledgementRequired, EpochAcknowledgementOrdering,
    SessionRevoked, ObserverContinuityLost, StateGenerationRegressed,
    MapInvalid, MapBindingMismatch, MapIntervalInvalid, MapArithmeticInvalid,
    WireVersionInvalid, WireEnumInvalid, WireTimeInvalid, WireNonFinite,
    SupervisorDenied, AdminSequenceReplay
};

struct AdapterDecision {
    AdapterCode adapter_code;
    bool has_s1_decision;
    Decision s1_decision;
    uint64_t status_sequence;
};

class WireAdapter {
public:
    WireAdapter(RequestGuard &guard, const AdapterIdentity &identity,
                const AdapterConfigurationView &configuration);
    AdapterDecision observeAndPoll(const TrustedIngressContext &context,
                                   const TrustedClockMap &map);
    AdapterDecision processRequest(const WireRequestV1 &request,
                                   const TrustedIngressContext &context,
                                   const TrustedClockMap &map);
    AdapterDecision processEpochAcknowledgement(const WireSessionV1 &session,
                                                const TrustedIngressContext &context,
                                                const TrustedClockMap &map);
    AdapterDecision applySupervisorEvent(const TrustedSupervisorEvent &event,
                                         const TrustedIngressContext &context,
                                         const TrustedClockMap &map);
    bool getEligibleRequest(const TrustedIngressContext &context,
                            const TrustedClockMap &map,
                            CanonicalRequest &request,
                            AdapterDecision &decision);
};
```

`AdapterIdentity::controller_instance_id` is nonzero and immutable for one wrapper lifetime.
It is supplied at construction and is the only controller-instance value compared with wire records.
It never comes from `TrustedIngressContext` or a packet.
The trusted observer instead supplies monotonically increasing `state_generation` and `supervisor_transition_generation` values.
`state_generation` covers state/map-observer changes; `supervisor_transition_generation` changes only for independently trusted establish/revoke/latch-clear events.
The wrapper owns a separate nonzero `wire_transition_epoch` and increments it after each observed trusted generation transition.
Only that wrapper-owned epoch is compared with `WireRequestV1::transition_epoch` and placed into a later local status record.

`AdapterConfigurationView` is an immutable explicit copy/view of the protocol and timing configuration that constructed the supplied S1 guard.
Its protocol must equal frozen v1 and its timing fields must satisfy the same nonzero/inversion rules as S1 before the wrapper begins admission.
The future construction test must prove that the guard and view came from the same immutable `ContractConfig` source; the wrapper cannot inspect or infer the guard's private configuration.
This is required so S1 age, future-skew, validity, and uncertainty limits are present and checked, rather than silently omitted by an adapter-only map success.

`WireSessionV1` has the frozen C0a administrative fields, including operation sequence and acknowledgement time/status sequence.
Session establish/revoke packet handling is only an intent check; it may call `applySupervisorEvent` only after a separately trusted matching event exists.
No C0b method accepts an arbitrary callback or a payload-derived supervisor Boolean.

The wrapper owns no alternative request cache and has no operation that recreates or reconfigures `RequestGuard`.
It retains one supplied guard for its lifetime.
It may call `guard.revoke()` on a required stateful fault, but only S1 `clearLatch()` may clear a latch and only through a trusted, disarmed event.
The wrapper alone owns local `status_sequence`, `epoch_barrier_status_sequence`, and `epoch_barrier_validator_time_us`.
They are never supplied in trusted context, from a state observer, or from a packet.

### Serialization And Transition Rules

All public methods execute under one wrapper serialization domain with one validator-time oracle.
That domain serializes trusted-state observation, observer-continuity handling, transition/barrier mutation, canonicalization, S1 `evaluate()`, S1 `poll()`, S1 eligible access, session administration, and local decision-record sequence allocation.
No status transport lock may be taken in this domain.

Every public operation first observes the full trusted current context and calls the equivalent of S1 `poll()` with it before decoding/admitting a new request.
This observation is required even for malformed, unauthenticated, or foreign traffic so an independently observed context loss cannot leave a prior owner request alive.
The untrusted packet itself never causes a transition, map replacement, supervisor action, replay-floor reset, or latch clear.

The wrapper compares the complete current frozen map snapshot with its retained snapshot: `valid`, source/session/controller/map-epoch bindings, signed offset, uncertainty, and both producer interval endpoints.
Any change closes admission and installs a barrier, including a change to offset, uncertainty, interval, or binding while map epoch is unchanged.

The wrapper installs a new epoch barrier and calls `guard.revoke()` when it observes any of these trusted changes:

- monotonic state generation or supervisor transition generation changes;
- arm/mode/required-state/transport/map-validity transition, including recovery;
- map binding, interval, validity, or epoch transition;
- lost/unknown observer continuity, wrapper restart, or a trusted session establish/revoke/latch-clear event.

It treats a decreasing state or supervisor generation as a fault, not a new state, and keeps the barrier closed.
An unchanged trusted context does not revoke an eligible request merely because foreign or malformed traffic arrived.
The adapter must keep context transition observation independent of status delivery; dropped or delayed local status records cannot postpone polling, expiry, or revocation.

### Epoch Acknowledgement Barrier

Each barrier allocates exactly one local transition status sequence and captures its validator time, retained as `epoch_barrier_status_sequence` and `epoch_barrier_validator_time_us`.
The status counter and wrapper-owned wire transition epoch are checked before increment.
If either is already `UINT64_MAX`, admission closes and the active request is revoked without wrap or a fabricated next sequence.
The record using the final available status sequence must carry frozen adapter decision `COUNTER_EXHAUSTED`; later events may preserve local diagnostic evidence without allocating another status sequence.
The producer can use an acknowledgement only when its frozen operation is `acknowledge current epoch`, its source/session/controller/transition/map identities match the current trusted state, and its `acknowledged_status_sequence` is the retained barrier sequence.
The acknowledgement raw producer time must be inside the retained inclusive map interval.
The adapter maps it with the same trusted map and computes checked conservative endpoints:

`earliest_acknowledgement = mapped_acknowledgement - uncertainty`

`latest_acknowledgement = mapped_acknowledgement + uncertainty`.

It requires `earliest_acknowledgement > epoch_barrier_validator_time_us`.

Acknowledgement admission also requires `earliest_acknowledgement <= now`,
checked `now - earliest_acknowledgement <= maximum_command_age_us`, checked
`latest_acknowledgement <= now + maximum_future_skew_us`, and uncertainty no
greater than the immutable configured maximum. These checks apply to the ACK
itself, not merely to a subsequent request. All arithmetic rejects overflow or
underflow. An already accepted ACK is frozen for the epoch: even a higher-sequence
second ACK cannot move its ordering barrier or administrative sequence floor.

Both arithmetic operations are checked; underflow or overflow is a map-arithmetic failure.
The accepted acknowledgement records its raw time and map epoch but neither establishes a session, opens a revoked-session gate, clears a latch, nor creates an eligible request.

Before C0b passes a data request to S1, it requires the source/session to be established and not revoked, a matching immutable controller instance and wrapper-owned wire transition epoch, and a matching frozen map binding.
It maps raw creation and expiry only after both endpoints are inside the retained map interval, and requires the conservative ordering proof:

`mapped_request_creation - uncertainty > latest_acknowledgement`.

It also requires raw request creation to be strictly later than the acknowledged raw producer time.
Thus queued data from before a disarm/rearm, mode/state recovery, map change, wrapper restart, or same-session transition cannot become eligible.
The immutable acknowledgement record applies only to its controller instance, transition epoch, complete map snapshot, source, and session.
An acknowledgement with the same or lower administrative `operation_sequence` is replayed/out of order and is rejected.
The wrapper advances its administrative sequence floor only after all acknowledgement checks pass and after it commits the immutable acknowledgement record.
Rejected administrative packets never advance that floor, move an accepted acknowledgement, alter a map snapshot, or reopen admission.
Only after these C0b checks may the wrapper call S1 `evaluate()` with canonical request and current context. S1 applies its existing immutable configured age, future-skew, validity and uncertainty limits; these limits are not new per-call arguments. Mapping/acknowledgement success never bypasses those S1 checks.

### Session And Latch Administration

An establishment requires a matching trusted supervisor event, S1 configured owner, current controller/transition/map bindings, authenticated/header-bound ingress, and disarmed state.
It invokes `RequestGuard::establishTrustedSession()` exactly once.
The post-establishment barrier remains closed until a new acknowledgement and later fresh request arrive.

A revoke requires a matching trusted disarmed supervisor event and current source/session/epoch binding.
It calls `guard.revoke()`, sets an adapter-owned revoked-session gate, and retains S1 replay floors.
An acknowledgement cannot reopen that gate; only a different supervisor-authorised session establishment may do so.

Latch clear requires a trusted disarmed supervisor event and calls S1 `clearLatch()`.
The adapter does not erase state by replacement construction.
After a successful clear, S1 requires establishment of a different session and C0b installs a fresh barrier.
Malformed, duplicate, delayed, or unauthenticated administrative packets cannot perform any of these actions.

## C0b.3 Local Status Records And Wire Mapping

C0b.3 creates a local immutable `AdapterStatusRecord` for every adapter decision, S1 decision, poll transition, and trusted state transition.
It is not a MAVLink sender and does not block C0b.2 on log or transport backpressure.
A later transport layer may serialize the record using the frozen C0a status payload only after a separate generated-dialect review.

```cpp
struct AdapterStatusRecord {
    uint64_t controller_instance_id;
    uint64_t transition_epoch;
    uint64_t clock_mapping_epoch;
    uint64_t source_id;
    uint64_t session_id;
    uint64_t correlation_sequence;
    uint64_t last_accepted_sequence;
    TimeUs validator_time_us;
    uint64_t status_sequence;
    uint64_t rejection_count;
    TimeUs conservative_age_us;
    uint16_t decision_code;
    uint16_t first_latched_code;
    uint16_t status_flags;
    uint8_t status_class;
    uint8_t arm_state;
    uint8_t research_mode_state;
    uint8_t session_state;
    uint8_t transport_state;
    uint8_t clock_mapping_state;
};
```

For a data request, the correlation tuple is its source, session, and request sequence.
For an administrative intent, it is source, session, and operation sequence.
An unsolicited state transition, fault, or snapshot has zero correlation sequence.
`status_sequence` is allocated inside the same serialization domain as the transition that it records.

S1 `DecisionCode` does not share the frozen wire numbers by C++ cast.
The explicit status conversion is a reviewed `switch` or fixed lookup table that maps each named S1 code to its frozen wire value 1 through 37.
Tests must enumerate the map and fail if an S1 code is added, removed, or mapped differently.
Wire code zero remains the C0a snapshot-only code, never S1 `Accepted`.
C0b adapter failures map only to their frozen values 64 through 83; unmapped internal failures must be represented as the frozen malformed or state-reference failure as reviewed, never as an invented value.

For a request-decision record, `ELIGIBLE_FOR_CONTROL` reports only that particular S1 decision.
It does not report whether an older request remains available after a rejection.
A snapshot can claim eligibility only after a fresh serialized S1 `getEligibleRequest()`/poll with complete trusted context.
When an age or last-accepted-sequence status flag is absent, its associated value is zero.
When latched is absent, first cause is zero; when present, the first cause is the explicit mapped S1 clock-regression cause.
All C0a status structural rules remain mandatory before a future payload serialization.

## Evidence And Offline Test Matrix

The future tests are deterministic library/fixture tests only.
They do not need a MAVLink parser, an authentication key, TELEM, PX4 task, simulator, model, EDF, canard, sensor, airframe, controller gain, actuator, board, or physical vehicle.
Trusted inputs are explicit non-operational fixtures.

| Area | Required cases |
| --- | --- |
| C0b.1 map validity | Invalid flag, zero every required binding, zero uncertainty, inverted map interval, source/session/controller/map mismatch, creation outside interval, expiry endpoint outside interval, raw expiry equal/before creation, frozen v1 protocol mismatch, and the explicit upstream-C0a-precondition boundary. |
| Signed offset | Zero, positive, negative, maximum positive, minimum negative `INT64_MIN`, additive overflow, subtractive underflow, and both endpoint conversions. Verify no signed overflow or wrap. |
| Duration and canonical fields | Equal raw/mapped durations after one offset, explicit wire-to-S1 v1 mappings, exact vector components including signed zero, protocol mismatch, enum mismatch, and non-finite vector rejection. |
| Context serialization | A valid request followed by every trusted loss/recovery transition; malformed/unauthenticated/foreign input processed while a trusted transition occurs; poll must revoke before request admission and status reporting cannot block it. Test immutable controller identity, state/supervisor generation ownership, and no context-provided barrier/status counter. |
| Frozen map mutation | Same map epoch with changed offset, uncertainty, interval, valid flag, or binding; every mutation closes the barrier and cannot be supplied by a packet. |
| Epoch barrier | Request before acknowledgement, wrong status sequence, acknowledgement outside map interval, acknowledgement before/equal barrier time, checked earliest underflow/latest overflow, request raw time not later than acknowledgement, conservative intervals that overlap/equal, and a valid strict-after sequence. |
| Session and latch | Establish without trusted grant, establish armed, duplicate session, revoke armed/foreign/stale, revoked-session acknowledgement, clear latch armed/unauthorised, producer and validator regressions, administrative-sequence duplicate/out-of-order/reject-no-advance, and proof no object reconstruction reset occurs. |
| Counter exhaustion | Status-sequence and wire-transition-epoch final increment, exhausted counter closes admission/revokes with no wrap, and later packet cannot restore it. |
| S1 preservation | Foreign/malformed traffic may produce a local rejection record but cannot replace a valid owner request, advance its replay floors, change map/context, clear a latch, or cause a transition without trusted state evidence. |
| Status records | Every S1 code maps to frozen value 1--37; all adapter codes map to 64--83; correlation, age, latch cause, decision/snapshot eligibility, status-sequence monotonicity, and all C0a structural combinations validate. |
| Cross-language vectors | C0a Python payload fixtures and C++ record fixtures agree on field ordering, binary32 values, allowed zero-tail decoding, and status fields. This is not generated MAVLink interoperability evidence. |

Tests must include boundary values for every unsigned input, `INT64_MIN`, `INT64_MAX`, `UINT64_MAX`, and arithmetic one-unit-beyond cases.
They must distinguish a pure mapping rejection, which leaves the existing S1 state unchanged, from a trusted-state fault path, which intentionally polls/revokes and may mutate state.

## Minimal Future Implementation Sub-Slice

The smallest useful implementation proposal is **C0b.1 only**: the pure `TrustedTimeMap` API, checked signed-offset conversion, canonical `WireRequestV1` to `CanonicalRequest` conversion, and focused unit tests.
It has no `RequestGuard` instance, no session operation, no latch call, no status stream, no MAVLink dependency, and no flight output.
Passing those tests establishes only arithmetic and field-preservation evidence.

**C0b.1** has passed its offline implementation gate as recorded below; this does not release C0b.2.
**C0b.2** remains provisional until controller identity, trusted generation, wrapper epoch/status ownership, configuration-view construction, and supervisor event APIs receive a dedicated source-path review.
**C0b.3** remains provisional until C0b.2 and host-vector ownership are reviewed.
Generated dialect, parser integration, live authentication/key use, PX4 module wrapping, status transport, active mode, uORB publication, controller/allocation, EDF/four-servo control, SITL execution, board build, flash, and physical operation remain outside C0b.

## Invariants For Any Later Review

1. A payload never supplies or changes trusted map, state, header binding, parser authentication, supervisor authority, or transition state.
2. Every map endpoint conversion and all conservative barrier arithmetic is checked without signed overflow, unsigned wrap, clamping, or hidden time conversion.
3. One S1 guard and one validator-time/serialization domain serve its full adapter lifetime; neither configuration nor object reconstruction resets replay/latch state.
4. Trusted state loss/recovery creates a barrier before a request can be admitted; foreign traffic alone never creates a barrier or changes a valid-owner cache.
5. A fresh epoch acknowledgement is strictly after its retained barrier under the whole-interval uncertainty bound, and every data request is strictly after that acknowledgement under the same map.
6. Session establishment, revoke, and latch clear require separate trusted supervisor actions; a packet cannot authorise itself.
7. C0b may produce local evidence records only. No C0b result is acceleration tracking, force/moment computation, control allocation, or an EDF/canard command.

## Remaining Decisions And Explicit Limitations

Deployment still needs a reviewed MAVLink dialect/parser path, link signing and identity binding, actual supervisor mechanism, map service/provenance, operational uncertainty and timing requirements, and a status transport policy.
The C0b.1 fixture map uses explicit non-operational values only; it neither proves a live clock bound nor selects a clock-synchronization algorithm.

Plant geometry, mass, CG, inertia, aerodynamics/local canard flow, EDF/servo dynamics and signs, sensor behavior, feasible envelope, output normalization/effectiveness, loss response, and all performance criteria remain unknown or unapproved.
Nothing here starts EDF or four-servo control code.

## Master design-gate disposition

Astra accepted the C0b.1 pure mapping design and subsequently released its three
source/test paths plus minimal standalone S1 CMake registration for implementation.
The completed result is recorded below.
The output argument must remain unchanged on any canonicalization failure.
Require nonzero transition epoch as a structural precondition/check, while its
equality to the current wrapper epoch remains C0b.2 responsibility. Mandatory
tests include every copied canonical field, both endpoint failures, signed
offset extrema, map binding/interval failures and unchanged sentinel output.

C0b.2 and C0b.3 are not approved APIs. Their outstanding review includes exact
state-generation semantics, construction-time configuration consistency, ACK
age/future limits and frozen-ACK duplicates, supervisor event de-duplication,
status counter reservation/exhaustion without reusing an emitted sequence,
and exhaustive internal-to-wire diagnostic mapping. A record cannot be rewritten
under an already emitted sequence. No design prose here proves these behaviors.

## C0b.1 implementation result (2026-09-17)

Implemented `TrustedTimeMap.hpp/.cpp` and independently authored
`TrustedTimeMapTest.cpp`, with standalone CMake registration only. There is no
PX4 module/startup integration. The header is authoritative for public names;
the local `WireIdentityInvalid` result diagnoses zero transition epoch and is
not a newly allocated wire diagnostic value.

Validation precedence is map validity, interval ordering, nonzero wire transition
epoch, map binding, protocol, enums, raw time ordering, endpoint containment,
finite vector, and checked mapping/duration arithmetic. All failure paths leave
the output unchanged. A successful conversion can still fail S1 uncertainty,
freshness, replay or bounds checks. Original wire controller/transition/map
metadata must remain available to the future adapter: `CanonicalRequest` does
not contain those fields and cannot replace their C0b.2 validation.

191 non-operational checks pass in strict-warning Debug, repeated Release and
ASan/UBSan host builds. Tests include exact field preservation, signed zero,
all local failure categories, inclusive interval endpoints, INT64_MIN/MAX,
UINT64_MAX, creation/expiry arithmetic failure and the unchanged S1 guard seam.
Independent review found no actionable correctness issue. Commands, hashes,
regression results and preservation limitations are in `PHASE_GATE_STATUS.md`.

Next gate: review the concrete C0b.2 state/ownership API and its transition test
oracle before implementation. No new physical input is required for that review.
Generated MAVLink interoperability, live clock-map provenance/authentication,
session/epoch integration and the complete acceleration-to-actuator path remain
unverified. This arithmetic helper is not a clock synchronization service.
