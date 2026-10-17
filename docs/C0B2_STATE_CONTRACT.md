# C0b.2 Adapter State Contract

## Scope And Release Boundary

Session-administration review (2026-09-18): `C0B2_SESSION_ADMIN_DESIGN.md`
and `C0B2_SESSION_ADMIN_TEST_SPEC.md` supersede the provisional session and
supervisor API/details in this document. They define post-observation bindings,
ordered grant consumption, explicit target authorization, separate three-record
admin results and capacity checks. The implemented closed state-core API and
its two-report bound remain unchanged. Only the current dated master gate in
`PHASE_GATE_STATUS.md` determines the next implementation release; ACK/request
admission below remains deferred.

This contract resolves the provisional C0b.2 state-wrapper design around C0b.1 canonicalization and the existing S1 `RequestGuard`.
It is a design gate only.
Only a dated Astra acceptance in `PHASE_GATE_STATUS.md` may release the state-core implementation paths below. This document does not release later session/ingress slices, a generated MAVLink dialect, a receiver, authentication key use, serial/network I/O, status transport, uORB, a mode, a controller, an allocator, EDF control, canard-servo control, SITL, board build, flash, or physical operation.

The eventual full C0b.2 adapter passes a structurally decoded C0a request to S1 only after trusted local state observation and C0b.1 canonicalization. The first state-core slice accepts no packet, request, session, acknowledgement or supervisor input; its admission gate always remains closed.
It does not parse payload bytes and does not duplicate C0a structural checks.
It does not replace S1 request bounds, freshness, replay, expiry, latching, or disarmed policy.
No C0b result establishes acceleration tracking, force/moment computation, or an actuator output.

## Ownership Model

The future wrapper privately owns exactly one immutable `ContractConfig` and exactly one S1 guard constructed from that same member.
The following is the **C0b.2 state-core** public surface accepted for the bounded implementation assignment by the 2026-09-18 master gate in `PHASE_GATE_STATUS.md`. It deliberately excludes packet ingress and supervisor administration, which remain design dependencies rather than a released API.
`WireRequestV1`, `CanonicalRequest`, `ContractConfig`, and `RequestGuard` come from the reviewed C0b.1/S1 headers.

```cpp
struct AdapterIdentity { uint64_t controller_instance_id; };
struct TrustedStateSample;

enum class AdapterCode : uint16_t {
    Observed, NoEligibleRequest, GuardRejected, TrustedStateFault,
    CounterExhausted, NotConfigured
};
enum class AdapterReportKind : uint8_t { Observation, Poll, Access, Barrier, Terminal };
struct AdapterReport {
    uint64_t report_sequence;
    uint64_t transition_epoch;
    TimeUs validator_time_us;
    AdapterReportKind kind;
    AdapterCode code;
    bool has_s1_decision;
    Decision s1_decision;
};
struct AdapterSnapshot {
    bool initialized;
    bool terminal;
    uint64_t state_generation;
    uint64_t transition_epoch;
    uint64_t barrier_report_sequence;
    TimeUs barrier_validator_time_us;
    uint64_t last_report_sequence;
};
struct AdapterResult {
    AdapterCode code;
    bool admission_closed;
    AdapterSnapshot snapshot;
    uint8_t report_count;
    AdapterReport reports[2]; // valid range [0, report_count), chronological
};

class WireAdapter {
public:
    WireAdapter(const ContractConfig &configuration, const AdapterIdentity &identity);
    WireAdapter(const WireAdapter &) = delete;
    WireAdapter &operator=(const WireAdapter &) = delete;
    WireAdapter(WireAdapter &&) = delete;
    WireAdapter &operator=(WireAdapter &&) = delete;
    bool configured() const;

    AdapterResult observeTrustedState(const TrustedStateSample &sample);
    AdapterResult poll(const TrustedStateSample &sample);
    AdapterResult getEligibleRequest(const TrustedStateSample &sample,
                                     CanonicalRequest &request);

private:
    friend struct WireAdapterStateTestPeer;
    const ContractConfig configuration_;
    RequestGuard guard_{configuration_};
    const AdapterIdentity identity_;
};
```

The excerpt specifies the public API and ownership members; implementation adds private snapshot/counter/terminal state. `configured()` requires a nonzero immutable controller identity, configuration protocol equal to `kWireProtocolVersionV1`, and `guard_.configured()`. It is a construction predicate, not a health or admission predicate, and stays true after a runtime terminal fault. Invalid construction returns `NotConfigured`, closed admission, an all-zero snapshot and zero reports from every mutating public call, with no runtime S1 calls.

All three public operations first run one private observation/reservation helper, not the public `observeTrustedState` method. If this succeeds, each calls S1 `poll` exactly once in this slice. This includes standalone `observeTrustedState`, so validator-clock regression cannot hide in an observation-only sequence. None calls establish, evaluate, clear, or the S1 accessor in this slice. `getEligibleRequest` always leaves its output byte-for-byte unchanged: no session/ACK/request input exists that could open its gate. No second request cache exists.

Each successful observation path emits exactly one operation report (`Observation`, `Poll`, or `Access`). Initial observation or a contiguous generation transition additionally emits a preceding `Barrier` report: at most two reports per call, both returned by value. Even an unchanged observation consumes one report sequence intentionally, for auditable polling; only `configured()` is side-effect-free. The caller owns persistence of returned reports. No callback, queue, backpressure or later report-fetch API exists. The wrapper retains the current barrier identity/time and snapshot needed for future matching, not a report backlog.

For S1 `Accepted`, `ValidatedDisarmed`, or `NoEligibleRequest`, the operation code is `Observed` for observation and `NoEligibleRequest` for poll/access. Any other S1 result yields `GuardRejected`, retaining the actual S1 decision. The result code equals the operation code on a normal call; barrier code is `Observed`. A structural terminal uses `TrustedStateFault`, or `CounterExhausted` if no normal report slot remains. Terminal report and result codes agree. S1 latching remains S1-owned and does not itself set the wrapper's terminal flag; no clear action is available in this slice. `admission_closed` is always true. Zero-initialize results and unused report slots; `has_s1_decision` is false for barriers/adapter-terminal reports, true only for the operation report after an actual S1 poll. Never infer eligibility from a prior result.

The snapshot starts all zero. On a valid first sample, initialize epoch one and retain its generation and barrier identity/time. Normal results expose the post-operation snapshot. On a terminal structural fault, preserve the last valid generation/epoch/barrier, set `terminal`, revoke the guard, and return one terminal report. Subsequent calls return the retained terminal code/snapshot and zero reports, with no S1 calls. Invalid first observation leaves `initialized=false`, generation/epoch/barrier zero, but records the terminal flag/report sequence. Terminal recovery/reset is not in this API.

The construction order is material: `configuration_` is initialized before `guard_` and copied once from the reviewed caller input.
No externally supplied guard, configuration view, setter, reset, move, copy, replacement construction, or reconfiguration method is permitted.
An invalid configuration is inert; it never supplies defaults.

`AdapterIdentity` contains a nonzero controller-instance identifier, supplied only at wrapper construction and immutable for that wrapper lifetime.
It is compared to every accepted wire/session record but is not taken from trusted state samples or a packet.
A controller reboot requires a new wrapper instance under an explicitly new higher-level lifetime; it is not a reset operation on a live wrapper.

The wrapper is single-threaded/non-reentrant by contract.
One serialization domain owns trusted-state observation, supervisor-event application, C0b.1 invocation, S1 administration/evaluation/poll/access, all local epochs, all administrative floors, and local report reservation.
No report sink or future transport may hold this serialization domain.

## Trusted Inputs And Packet Inputs

The four inputs below are deliberately distinct.

| Input | Trusted producer | Packet-controlled? | Authority |
| --- | --- | --- | --- |
| `WireRequestV1` / decoded C0a session record | C0a decoder or test fixture | Content only | Candidate values only. |
| `IngressVerdict` | Future MAVLink/parser/link-binding boundary | No | Whether the record may reach C0b canonicalization. |
| `TrustedStateSample` | One lossless state/map observer | No | Current state, map snapshot, validator time, state generation. |
| `SupervisorEvent` | Independent local supervisor boundary | No | Establish/revoke/latch-clear authority and deduplication identity. |

```cpp
struct IngressVerdict {
    bool mavlink2;
    bool parser_authenticated;
    bool configured_header_source_binding;
};

struct TrustedStateSample {
    uint64_t generation;
    bool observer_continuous;
    EvaluationContext request_guard_context;
    TrustedClockMap map;
};

// C0a structural validation supplies this complete logical session record.
struct DecodedSessionV1 {
    SourceId source;
    SessionId session;
    uint64_t controller_instance_id;
    uint64_t transition_epoch;
    uint64_t clock_mapping_epoch;
    uint64_t operation_sequence;
    TimeUs producer_acknowledgement_monotonic_us;
    uint64_t acknowledged_status_sequence;
    uint32_t protocol_version;
    uint8_t operation;
};

enum class SupervisorAction : uint8_t { EstablishSession, RevokeSession, ClearLatch };

struct SupervisorEvent {
    uint64_t event_id;
    uint64_t expected_generation;
    SupervisorAction action;
    TrustedSession session;
    uint64_t expected_controller_instance_id;
    uint64_t expected_wire_transition_epoch;
};

```

`IngressVerdict` is produced before C0b but is not a state transition.
An unsigned, bad-signature, foreign-header, malformed, or unsupported packet can produce an attributable local report, but it cannot by itself revoke a valid owner request, change a map, advance a floor, create a barrier, or invoke supervisor authority.
Authentication is separate from source authorization, and source/session identity is separate from both.

`SupervisorEvent` is an authenticated local fact before it reaches the wrapper; no wire field is copied into an authorisation Boolean.
It is a grant for exactly one requested establish, revoke, or clear action, has a nonzero unique event identifier, and cites the current pre-event trusted state generation, immutable controller identity, and wrapper transition epoch.
Its source/session/action must equal the decoded session operation when one is supplied.
The wrapper records the trusted stream floor rather than accepting a packet-derived authority value.

## Single Lossless State-Generation Domain

`TrustedStateSample::generation` is the sole ordered *exogenous* state-transition domain.
The producer increments it for every externally observed change relevant to C0b admission: arm state, research-mode state, required-state validity, transport connectivity, full clock-map snapshot, or observer-continuity condition.
No independent map, arm, or mode generation is permitted.
Successful adapter-owned supervisor actions are deliberately not exogenous state-generation changes; each applies atomically against its cited current pre-event generation and directly installs its own barrier.

The observer must deliver every generation in order and explicitly set `observer_continuous` false upon known subscription loss, restart, or unknown state, even if its last counter value is unchanged.
Generation zero is invalid.  A sample is usable only when `observer_continuous` is true, its `request_guard_context.clock_mapping_valid` exactly agrees with `map.valid`, and the map controller binding equals the immutable adapter identity.
These predicates apply on first, unchanged and contiguous-generation observations alike: controller mismatch is always terminal, never an ordinary map rebind. When `map.valid` is true, source/session/map epoch and uncertainty must be nonzero and the producer interval must not be reversed. A false map-valid flag is permitted with false context-map validity and the same immutable controller binding; other map fields remain compared but are not considered usable. This does not approve a map's physical clock accuracy or replace C0b.1 arithmetic or S1 uncertainty limits.

Compare all `EvaluationContext` admission fields (arm state, mode state, map-valid, connected and required-state-valid), but exclude `validator_receive_time_us` from generation equality. Ordinary time progression needs no generation increment; S1 checks time on every structurally valid nonterminal call. Named `Unknown` arm/mode values are legitimate loss states for S1 to reject; out-of-enum values are malformed trusted observations and terminal. Generation `UINT64_MAX` may be retained and repeated, but no successor/wrap is valid. No unchecked `generation + 1` is permitted.
A map source/session change across a valid new generation is a coherent loss of binding for any active session: the generation transition revokes that session and leaves the session gate revoked, rather than treating the new map as an observer fault.
The only supported replacement workflow is disarmed revoke of session A, trusted map generation binding session B, then a later matching packet-plus-grant establishment of B.
An unchanged generation with changed map fields is an observer fault. Invalid context/map agreement, malformed valid-map shape, controller mismatch or observer discontinuity is an observer fault at any generation and terminally closes this wrapper lifetime.
On wrapper construction, the first trusted sample establishes a baseline but also installs a closed barrier.
Afterward the only valid unchanged observation repeats the current generation; the only valid transition is the immediate next generation.
A same-generation sample with any changed `EvaluationContext` admission field or any complete-map field is also an observer fault, never an implicit transition.
A new generation with equal latest values is still a transition and installs a barrier because it may represent a hidden intervening toggle.
A regressed, skipped, unknown, or discontinuous generation is a trusted-observer fault: the wrapper revokes S1 eligibility, closes admission, and preserves the fault/barrier for the wrapper lifetime.
It does not guess the missed transition.

For every observed new generation, before inspecting packet verdict/content, the wrapper:

1. reserves a new local transition epoch and transition report sequence;
2. calls `guard_.revoke()`;
3. replaces the retained trusted state/map snapshot;
4. clears the accepted acknowledgement for the prior generation; and
5. installs a closed current-generation barrier at the trusted validator time.

The complete trusted map snapshot is compared field-for-field: valid flag, source/session/controller/map-epoch bindings, signed offset, uncertainty, and producer interval endpoints.
Any otherwise coherent map mutation requires the generation transition above, even if the map epoch did not change. Structural faults take precedence over ordinary transitions; they do not allocate a new barrier/epoch.
An untrusted record never mutates this snapshot.

## Local Epoch And Counter Reservation

The wrapper owns `wire_transition_epoch`, `last_report_sequence`, and the retained barrier pair `(barrier_report_sequence, barrier_validator_time_us)`.
Zero is reserved; initial usable local epochs/counters begin at one.
They are not supplied by the state observer, supervisor, packet, or report consumer.

Before committing a normal transition, barrier, or report, the wrapper checks capacity for the entire public call: two normal report identifiers for initial/new-generation calls, one for unchanged calls or a structural-terminal record. Use checked subtraction against `UINT64_MAX - 1`, not an increment that can overflow. Reserve the required epoch advance at the same time. Revocation/closure never waits for capacity. No partial transition or S1 poll occurs when reservation fails.
Counter exhaustion cannot be cleared by an acknowledgement, packet, session, supervisor event, or S1 latch clear; it requires a separately reviewed wrapper-lifetime recovery.

The normal report range is one through `UINT64_MAX - 1`.
`UINT64_MAX` is reserved for exactly one terminal `COUNTER_EXHAUSTED` report.
The wrapper pre-reserves sufficient normal report slots for every normal barrier plus operation result before it mutates optional state.
If insufficient normal capacity exists, it revokes/closes first and emits the terminal report once if that reserved terminal slot remains available; gaps in normal report values are allowed.
No subsequent report allocation or identifier reuse is valid. Store the terminal `CounterExhausted` code, `terminal=true`, and `last_report_sequence=UINT64_MAX`; keep the previous valid epoch/barrier/generation snapshot. A structural fault is classified before considering normal transition capacity and uses one terminal record, not two. If even that normal slot is unavailable, the returned primary code becomes `CounterExhausted` and the reserved terminal record takes precedence. Both paths revoke; neither admits a request. Terminal repeated calls allocate nothing, even if their sample differs.
The transition epoch may reach `UINT64_MAX`, but the next required advance terminally closes admission as `AdapterCode::CounterExhausted`, emits the reserved `UINT64_MAX` terminal report once, and preserves the prior snapshot epoch/barrier rather than wrapping.

## Observe-Before-Reject Processing Order

Every public operation begins with the private trusted-observation helper in the serialization domain, never by recursively calling the public observation operation. Invalid construction and already-terminal lifetimes return immediately; structural faults revoke and terminate; otherwise reserve the complete call, commit any barrier/revoke, and poll S1 once. The operation report follows the barrier report and contains that actual S1 decision. Repeating an unchanged sample still polls S1, including validator-time checks.
Observation validates observer continuity/sample identity and processes the exogenous generation transition before that poll.
Therefore a trusted state/map/time fault is the primary decision for that call, even when the packet is also malformed or unauthenticated.

If the trusted observation is unchanged and S1 polling preserves the valid owner request, an ingress failure is only an ingress failure report.
It does not call `guard_.revoke()` merely because a packet is foreign or bad-signature.
This precedence prevents a forged packet from suppressing a valid owner while ensuring that a real trusted context loss is never hidden behind a packet decode error.

`getEligibleRequest` never returns a wrapper-cached request. While the wrapper gates are closed it invokes only S1 `poll` and leaves output unchanged. In a future separately reviewed open-gate slice it may instead call S1 `getEligibleRequest(context, request, decision)`, whose internal poll is the single poll for that access. It must not explicitly poll and then invoke that accessor. No S1 API modification is needed. The future packet/admin path must likewise observe trusted state and poll before rejecting an ingress record; its subsequent evaluate/establish internals are not covered by an exact-one-poll promise for the state-core public operations.
`poll` follows the same state/map observation path even if no packet has arrived.

## Session, Supervisor, And Latch Contract

This section is a required design dependency for later C0b.2 slices, not part of the state-core public API released above.

The trusted supervisor source is a lossless ordered event stream with positive event identifiers beginning at one.
The wrapper retains an event floor that starts before the first identifier.
The only valid next event is the immediate successor to the floor.
For a positive immediate successor, the wrapper first checks that the event is structurally complete and then consumes that identifier by advancing the floor before checking its action predicates.
It therefore consumes a controller-, generation-, epoch-, source-, session-, or action-mismatched immediate successor as a rejected event.
An older/duplicate identifier is already below the floor and is rejected without moving it; a skipped, zero, malformed, or otherwise non-successor identifier closes admission as a trusted-supervisor-stream fault.
Neither kind can later be re-presented for a different outcome.

For a valid next event, the wrapper advances the supervisor event floor before attempting its action.
Thus a trusted well-bound grant is single-use even if its requested S1 action fails due to arm state, current session, or latch state.
The trusted source must issue a distinct later event for any later action.
This consumes no packet sequence or request replay floor.

`processSession` is the sole establish/revoke entry point.  Operations one and two require both an authenticated, fully C0a-decoded session packet and one matching independently trusted immediate-successor supervisor grant in the same serialized call.
The packet and grant must agree on action, source, session, controller instance and expected wrapper transition epoch; the grant alone binds the current exogenous generation (the frozen packet has no generation field). Their identity must also match the active trusted map where applicable.
The grant is not an authority boolean in the packet and neither an authenticated packet nor a grant alone invokes S1 administration.

For establish, the common values must use the configured source, a nonzero session different from the currently established session, current controller instance/generation/epoch, and a disarmed context.
Only after all conjunction checks does the wrapper call `guard_.establishTrustedSession(packet_session, true, current_context)`.
Required counters are reserved before calling S1. On success it commits the reserved local transition epoch, revokes any active demand, sets the session gate to established, clears any prior acknowledgement, and installs a closed barrier without requiring an external state generation change.

For revoke, the common values must cite the active trusted session/current identity and a disarmed context.
On success it calls `guard_.revoke()`, directly installs a new barrier, and sets a revoked-session gate.
An acknowledgement cannot reopen that gate; only a later, distinct successful establish action can do so.

A clear-latch action is valid only when the retained latest S1 decision reports `latched` and the current trusted arm state is disarmed.
This wrapper predicate is necessary because S1 `clearLatch()` also resets session state when it is called while not latched.
The wrapper invokes `guard_.clearLatch(true, ArmState::Disarmed)` only after the predicate passes.
It remains reachable after the required S1 poll reports `Latched`; a latched poll is not a reason to discard a valid trusted clear event.
On successful clear, it installs a direct barrier, leaves the session gate closed, and requires S1's distinct later session establishment.

`clearLatchedFault` is the limited exception: it accepts an independently trusted clear grant without a packet, but still requires the current controller/generation/epoch, a disarmed context, and retained S1 `latched` state.
No packet can clear a latch, and no packet without its matching trusted grant can establish or revoke a session.
Mismatching grant/packet values cannot invoke S1 administration. Mandatory
trusted-state observation, polling and loss revocation still apply; a valid
immediate-successor grant is consumed even when its paired action is rejected.

## Request And Acknowledgement Admission

For a decoded request/session record, after observe-before-reject succeeds, C0b.2 requires `IngressVerdict.mavlink2`, `parser_authenticated`, and `configured_header_source_binding` all true.
Those verdicts are necessary ingress preconditions only; they neither establish a session nor turn mapping success into authentication.

The current map must be valid and match the immutable controller identity, active source/session, and record map epoch.
C0b.1 receives the full map and returns its own typed failure before S1 is called.
C0b.2 retains the complete map snapshot associated with an accepted acknowledgement.
Any current-generation map difference makes that acknowledgement unusable and leaves the barrier closed.

The only session operation that can open a current barrier is the frozen acknowledgement operation.
It must have an authenticated/bound ingress verdict, an established/non-revoked session, current controller/transition/map bindings, the currently retained transition-barrier report sequence, and a raw acknowledgement time inside the inclusive map producer interval.
The wrapper applies C0b.1 checked signed-offset arithmetic and then checked conservative endpoints:

`earliest_ack = mapped_ack - uncertainty`

`latest_ack = mapped_ack + uncertainty`.

It requires all of the following checked inequalities from the same immutable `ContractConfig::timing` used by S1:

`earliest_ack > barrier_validator_time_us`

`earliest_ack <= validator_receive_time_us`

`validator_receive_time_us - earliest_ack <= maximum_command_age_us`

`latest_ack <= validator_receive_time_us + maximum_future_skew_us`

`map.uncertainty_us <= maximum_clock_uncertainty_us`.

Every add/subtract is checked; underflow/overflow is a map-arithmetic failure, never a clipped time.
The acknowledgement `operation_sequence` floor is strictly increasing for the immutable controller instance, never per session and never reset by establish, revoke, clear, barrier, or acknowledgement.
A rejected session operation does not advance this packet-operation floor; successful establishment/revoke/acknowledgement advances it exactly once after its complete operation succeeds.

Once accepted, the acknowledgement record is immutable: source, session, controller instance, local transition epoch, entire map snapshot, raw/mapped time, conservative endpoints, and operation sequence.
A duplicate or later acknowledgement cannot move it.
Before an authenticated canonical request reaches S1, it must match that immutable record and prove both raw and conservative order:

`request_raw_creation > accepted_ack_raw_time`

`mapped_request_creation - uncertainty > accepted_ack_latest`.

The latter subtraction is checked.
S1 then performs its own required conservative uncertainty, validity, age, future-skew, expiry, bounds, request-sequence, producer-time, and latching checks from the private immutable configuration.
Successful mapping or acknowledgement never bypasses S1.

## Typed Local Reports

C0b.2 creates typed local `AdapterReport` values only; it does not allocate a MAVLink message, serialize a status payload, or transmit anything.
Each state-core report has a reserved local report sequence, wrapper-owned transition epoch, offered trusted validator time, kind/code, and S1 decision only when S1 was actually called. The offered timestamp in a terminal/S1-clock-fault report is diagnostic, not certification of monotonicity. The result additionally provides the post-call snapshot and closed-admission flag. Input correlation, retained ACK details, administrative floors and event-consumption fields belong to later APIs and are not state-core report fields.

One state-core call can require both a transition/barrier report and an observation/poll/access report. Later packet/supervisor report capacity must be separately reviewed, especially a call combining an exogenous transition with an administrative transition; the two-record state-core bound does not approve that future API.
The wrapper reserves both normal report sequences before the optional state mutation so report identifiers remain attributable.
The counter-exhaustion safety exception described above takes precedence over normal report completeness.
S1 `Decision::eligible_for_control` is recorded only as the result of that S1 call; it never claims survival of an earlier request after a rejection.

## Verifiable Future Sub-Slices

The proposed C0b.2 implementation is intentionally split:

1. **State core:** private configuration/guard construction, lossless exogenous observation, counter reservation, barrier install, S1 poll/access, and local reports; no decoded request/session inputs.
2. **Session administration:** ordered supervisor events, atomic authenticated session-packet plus trusted-grant establish/revoke conjunction, independent clear predicate, direct admin barriers, and session/revoked gates; no acknowledgement or data request.
3. **Ingress admission:** `IngressVerdict`, C0b.1 canonicalization handoff, acknowledgement floor/immutability, request ordering, and typed report correlations.

Each later slice needs an explicit source-path assignment and tests before the next is proposed. Only the dated master gate may approve the state-core paths below.

## Bounded Implementation And Test Seam

The next possible assignment is limited to new `src/lib/research_acceleration/WireAdapter.hpp`, `WireAdapter.cpp`, and independent `WireAdapterStateTest.cpp`, plus standalone `src/lib/research_acceleration/CMakeLists.txt` test registration. Existing S1/C0b.1 public APIs and source remain unchanged. No root firmware build wiring, startup, parameters, modules or outputs are in scope.

Define the forward-declared friend `WireAdapterStateTestPeer` only in the standalone test translation unit. It may inspect private retained adapter state/guard address and seed only local epoch/report counters for boundary fixtures; it must not replace/configure the guard, insert a request/session, open admission or provide any production setter. It does not gain access to S1's private members. No conditional class layout, test constructor or reset API. Production code never calls the peer. Ordinary tests use public results/snapshots; the peer checks counter exhaustion and stable guard identity. Source review must additionally confirm no alternate cache or session/request entry point. S1 rejected-context fixtures expose rejection-count increments, so double polling is observable without a mock guard; standalone-observe time regression also proves the actual S1 clock path is exercised.

Predeclared verification for implementation: strict-warning C++17 Debug/Release and ASan/UBSan standalone host tests, unchanged S1/C0b.1 regression, and compile-time assertions forbidding copy/move. Use a newly allocated native-WSL host-test directory, never shared simulator mounts/caches. Test limits and identifiers are synthetic only. This design gate itself runs no build or executable test.

## Required Offline Tests

This table covers the eventual adapter. Only private-lifetime, structural observation, closed poll/access and report/counter cases are in the state-core gate; packet, supervisor, clear-latch and ACK/request cases are deferred. `C0B2_TRANSITION_TEST_SPEC.md` separates the executable state-core assignment from those later design cases.

| Area | Required evidence |
| --- | --- |
| Private S1 lifetime | Configuration is copied before guard construction; copy/move/reconfigure/reset are unavailable; invalid configuration stays inert. |
| Lossless observation | First sample creates closed epoch one barrier; zero generation, false `observer_continuous`, context/map-valid disagreement, controller mismatch, same-generation changed field, skipped/regressed/unknown generation terminally close admission. A new generation with unchanged values still creates one barrier. |
| Packet precedence | A trusted observation fault is primary over a bad packet; an unchanged trusted sample plus bad signature/foreign header does not revoke a valid owner. Every poll/access rechecks current state/map. |
| Supervisor stream | Positive immediate-successor identifiers only; an immediate successor with wrong controller/generation/epoch/source/session/action consumes its grant but cannot create a barrier; an older duplicate is rejected without moving the floor; skipped/zero/malformed identifier terminally closes. Establish/revoke require one matching authenticated decoded packet plus independent grant, and successful actions directly create a barrier without an external generation transition. |
| Clear latch | A valid trusted clear reaches S1 while latched/disarmed; no clear call occurs while S1 is not latched; post-clear requires a distinct establishment and fresh acknowledgement. |
| Map/acknowledgement | A valid new-generation map rebinding follows revoke-A, map-B, establish-B workflow; same-generation complete-map mutation terminally closes. Test raw acknowledgement interval, checked earliest/latest, strict barrier ordering, age/future/uncertainty inequalities, immutable accepted acknowledgement, duplicate/later no-move, per-controller (not per-session) successful operation floor, rejected operation no floor advance, and request strict raw/conservative ordering. |
| Reports/counters | Normal sequences use one through `UINT64_MAX - 1`; terminal exhaustion uses `UINT64_MAX` once; pre-reservation for barrier plus decision, gaps allowed, no wrap/reuse, epoch final value followed by required advance closes admission. |
| S1 division | C0b cannot accept map/ACK as authentication or eligibility; all S1 age/future/expiry/replay/bound/latch decisions remain observed from real S1 calls. |

Fixtures may use structural counter/type boundary values and explicitly labelled synthetic S1 configuration/context values.
They may not become operational command rates, deadlines, acceleration limits, physical values, controller settings, or flight criteria.

## Human Approval Still Needed Before Live Attachment

The state-core slice can use injected offline fixtures and needs no physical-airframe or control-law choice.
Before any live ingress is proposed, the project owner must approve the independent supervisor authority and its lossless ordered event source, the authenticated MAVLink/header-source binding and key lifecycle, the trusted map publisher/provenance and its approved whole-lifetime timing bounds, and the deployment owner for local status retention/transport.
Those are operational/deployment approvals, not open implementation choices for this document.

## Design Status

Implementation disposition (2026-09-18): the closed state-core slice has passed
its host implementation gate after independent source/test review and Debug,
repeated Release and ASan/UBSan verification. `PHASE_GATE_STATUS.md` records
the exact source/log identities and scope. The later session/ingress material
below and above is still not an implementation release; no command is admitted.

Revised 2026-09-18 for the state-core gate. Only the dated master disposition in `PHASE_GATE_STATUS.md` determines approval. Session administration and ingress admission remain design dependencies; recovery from a terminal observer/supervisor/counter fault by constructing a replacement wrapper is explicitly outside this contract. A state-core pass would establish only observation, revocation and reporting behavior, never request admission or flight capability.
