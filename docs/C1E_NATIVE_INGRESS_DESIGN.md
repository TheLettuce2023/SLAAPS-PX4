# C1E: native diagnostic ingress

2026-09-24. Architecture increment under the existing disarmed/output-disabled
C1 authorization. W1 below is an offline payload implementation; N1 native
integration is a subsequent technical gate. Full C1 and Phase 2 remain open.

## Source findings and chosen progression

The [C1D owner](../src/lib/research_acceleration/NativeClosedDiagnosticOwner.hpp)
has no packet/operator API. Its physical snapshot explicitly lacks transport,
clock and estimator readiness. Preserve that closed experiment and its evidence.
[SignedMavlink](../Tools/research_sitl/live/SignedMavlink.hpp) provides reviewed
authentication/staged-replay algorithms, but is host-only and its current-context
check requires a nonzero map epoch. Its status encoder represents frozen Flight
v1, not native diagnostics. HostOwner's synthetic-state ABI cannot enter PX4.

Implement the distinct diagnostic payload contract first (W1). Follow with a
reviewed negative-only native path (N1): authenticated request, bounded handoff,
single owner diagnostic rejection, authenticated correlated status. Keep native
map/estimator/transport unavailable, selector Disabled and admission session/ACK
absent. An experiment transport binding is not an established guard session.
No enable, establishment, ACK, clear, revoke or control-demand API is exposed in N1.

W1 is useful source work but proves payload shape only. Generated MAVLink
conformance, authentication, runtime serialization and real returned status must
be verified separately before N1 acceptance. Private message IDs are not global
reservations and do not modify the existing Flight dialect.

## N1 ownership and serialization requirements

Retain one by-value adapter/guard and source-derived arm/nav chronology. Use a
separate opt-in C1E owner/profile, preserving the C1D API/build behavior and its
regressions. Share reviewed algorithms where appropriate, not a fixture owner.

The transport worker parses/authenticates outside Commander hooks. It exclusively
owns parser/replay state and may have at most one staged candidate outstanding.
An immutable, bounded candidate crosses to the single Commander execution context.
The worker cannot authenticate another candidate until that candidate is resolved.
It must not expose caller-supplied authentication booleans or mutable parser state.

Commander commits intervening raw state transitions before the candidate turn.
At a defined loop boundary, validate current run/owner/controller/source/channel,
transport lifetime, transition and candidate identity; reserve result/evidence
capacity before mutation. Use actual evaluation hrt, not queued arrival time.
Only the owner creates the internal NativeDiagnosticIngress verdict from the
authenticated candidate and evaluates one DiagRequest. Record its actual result;
do not replace the real prerequisite order with a desired rejection reason.
In the current Disabled selector state, DiagRequest's operation result is
SelectionRequired (wire reason 4), even when the preceding poll reports
ClockMappingInvalid. Preserve both in full evidence; do not report reason 1
as the request operation's result merely because the clock is unavailable.

A final disposition transfers the candidate back to the worker: consume replay
only for a current, fully authenticated, completed transaction (even when its
application result is rejection); discard stale/foreign/aborted candidates.
After disposition, sign/send the immutable result outside the source path.
A completed result is historical evidence, never continuing authority. Candidate
and result ownership need explicit acquire/release publication and acknowledgment;
the source path must never wait for parsing, acknowledgment, socket or sink I/O.
At most one outstanding candidate is a diagnostic capacity choice, not a
qualified throughput or control latency.

Stop/source/transport loss wins over dependent work once observed by the owner.
Foreign threads may publish sticky loss, never invoke adapter mutation. Define
the owner linearization point and both loss-before/loss-after cases in the exact
N1 API review; do not promise instantaneous knowledge of remote failure.
If state/context changes before the turn, discard the candidate. If it changes
after the turn, retain that historical rejection; later changes cannot relabel it.
Queue/evidence overflow closes or rejects with explicit retained evidence, never
silently overwrites state transitions. Reserve independent terminal capacity.
Normal owner stop and source loss retain distinct causes; neither is operator loss.

N1 does not create an operator attachment. A later positive-diagnostic increment
must provide an independently owned local operator capability, serialized
attach/selection/grants/detach and qualified liveness; a guidance datagram cannot
establish or refresh operator authority. Key/run restart invalidates all old
candidates and parser state. Signing timestamps remain replay metadata.

## Clock and estimator boundaries

Separate arrival_native_us, evaluation_native_us and source_checked_native_us.
Arrival and source checked time must not exceed evaluation; no ordering between
arrival and source checked time is assumed. Queue delay counts toward command
age once a qualified map exists. Equal native timestamps are permitted; regression
and arithmetic overflow fail closed.

For a later qualified map, let M(p)=p+O, U>0, creation p_c, expiry p_e,
evaluation n, age limit A and future limit F. Existing checked guards require:
c_minus=M(p_c)-U <= n; c_plus=M(p_c)+U <= n+F;
n-c_minus <= A; e_minus=M(p_e)-U > n.
Both raw endpoints must be inside the frozen map interval and mapped duration
must equal raw duration. ACK requires a_minus > barrier_time; request creation
requires c_minus > a_plus, hence raw request-after-ACK gap > 2U for one map.
Equality at age/future limits is permitted; expiry and ACK equalities reject.
These restate current guards, not new timing defaults.

A clock service must establish offset/uncertainty for the entire interval,
binding provider, native profile, boot/namespace, identities and reset lifetime.
C1H's same-clock U=1 us proof does not automatically qualify PX4 hrt.
No request-supplied offset, signature, RTT sample or copied fixture establishes
trust. W1/N1 clock_mapping_epoch=0 explicitly means unavailable.

Later estimator readiness requires actual selected estimator provenance,
sample/publication times, freshness budgets, resets, finite values and continuity.
A fresh read of a cached topic is not a fresh sample. VehicleAttitude has no
standalone validity flag and groundtruth uses the same schema; neither topic
presence nor a normalized quaternion is sufficient. Expiration, instance switch,
reset or continuity loss must create an owner-observed barrier. Latest-value
subscriptions cannot prove every intermediate validity transition was observed.
Keep this missing readiness false throughout N1.

## Exact N1 release dependencies

Before native code/runtime release, review the exact candidate/disposition API,
atomic ownership and shutdown sequence, private owner/profile changes, parser
context-binding/replay rule for unavailable maps, diagnostic status projection,
queue/resource/stack bounds, startup allowlist and evidence schema.
The current C1D native macro requires its C1D target marker; do not merely reuse
or bypass it for a different profile. Preserve mixed-profile rejection.

Then run ordinary/offline/native-contract regressions, generated wire and
authentication tests, build-isolation and corruption checks, native stack review,
and two fresh disarmed native runs with honest source-bound rejection/status.
Runtime must remain free of control/output modules and eligible demand.
The [finite test matrix](C1E_NATIVE_INGRESS_TEST_SPEC.md) separates W1 and N1
acceptance. Geometry is not a prerequisite for either software increment.

## C1E-W1 exact payload contract

Own only Tools/research_sitl/diagnostic_wire/{codec.py,diagnostic.xml,README.md,
test_codec.py}. Do not link these into PX4 or alter the Flight wire library.
Protocol version 0x00010000, domain=1 (SitlDisarmedDiagnostic), profile=1
(c1e-negative/v1). Message IDs 200010 REQUEST and 200011 STATUS are local test IDs.
These discriminator values are new wire values, not C++ enum casts.

Both codecs accept exact field dictionaries, reject extra/missing fields, bools
as integers/floats, out-of-width values, binary32 conversion overflow/nonfinite output,
nonzero reserved/flags, unknown versions/enums and invalid time relationships.
Normal IEEE binary32 rounding is permitted, including finite underflow to signed
zero; exact mathematical representability is not required (for example 0.1).
Decoders accept bytes only, validate every field and return no partial object.
Reserved is exactly four zero bytes. Encode is untrimmed by default; optional
trim=True removes only allowed trailing zeros. Decode pads only within the
declared minimum/full lengths. Structural success is not authentication,
freshness, provenance or authority; a caller can fabricate structurally valid
state labels, which later native projection/evidence checks must prevent.

Request is little-endian <10Q3fI8B4s, full 108 bytes, logical minimum 103:

| Type | Fields in wire order | Constraints |
| --- | --- | --- |
| uint64 x10 | native_run_id, owner_lifetime_id, controller_instance_id, source_id, transport_session_id, request_sequence, expected_transition_epoch, producer_monotonic_us, producer_expiry_monotonic_us, clock_mapping_epoch | All nonzero except map MUST equal zero; expiry > creation |
| float x3 | acceleration_x_frd_m_s2, acceleration_y_frd_m_s2, acceleration_z_frd_m_s2 | Finite binary32; bounds remain an admission concern |
| uint32 | protocol_version | 0x00010000 |
| uint8 x8 | domain, profile, target_system, target_component, frame, quantity, reference_point, flags | 1,1,1..255,1..255,1,1,1,0 |
| uint8[4] | reserved | All zero |

Frame=FRD, quantity=inertial acceleration, reference=CG; body-held semantics
remain unchanged. Producer time is unqualified evidence, never converted to
native age in W1/N1. transport_session_id names a disposable experiment
transport binding and MUST NOT manufacture an admission session.

Status is little-endian <14QIH14B4s, full 136 bytes, logical minimum 132:

| Type | Fields in wire order | Constraints |
| --- | --- | --- |
| uint64 x14 | native_run_id, owner_lifetime_id, controller_instance_id, source_id, transport_session_id, request_sequence, status_sequence, transition_epoch, source_sequence, state_generation, arrival_native_us, evaluation_native_us, source_checked_native_us, clock_mapping_epoch | Nonzero except map MUST zero; arrival <= evaluation; source_checked <= evaluation; source_sequence >= state_generation |
| uint32 | protocol_version | 0x00010000 |
| uint16 | reason | Explicit mapping below |
| uint8 x14 | domain, profile, execution_origin, outcome, arm_state, raw_nav_state, research_mode_state, selector_state, estimator_valid, clock_map_valid, eligible_for_control, flags, target_system, target_component | Constraints below |
| uint8[4] | reserved | All zero |

domain/profile=1; origin=1 SyntheticNativeContract or 2 NativePosixSitl.
outcome=1 Rejected for reasons 1..5, or 2 Closed for reasons 6..8.
arm_state=1 Disarmed or 2 Armed; raw_nav_state=0..30 excluding 9,11,16,
matching the pinned native owner. Research mode, selector, estimator-valid,
map-valid, eligibility and flags MUST all be zero. Targets are 1..255.
source_sequence >= state_generation is specific to this source-only negative
profile; a later combined estimator/transport/map generation domain needs its
own invariant and must not inherit that comparison automatically.
An Armed status can truthfully describe a rejected observation; a successful
disarmed runtime campaign must separately require Disarmed throughout.

Reasons: 1 ClockMappingInvalid, 2 TransportDisconnected, 3 StateEstimateInvalid,
4 SelectionRequired, 5 ContextChanged, 6 SourceContinuityLost, 7 OwnerStopped,
8 ResourceExhausted. These are explicit diagnostic wire categories, not raw
DecisionCode ordinals. Native projection must map actual results without
inventing prerequisites; unknown/unrepresentable results fail serialization.
Status is a correlated response only after an initialized source and a known
request; startup/unsolicited closure evidence stays in the separate native archive.
This payload is not a complete encoding of DiagnosticResult. Evidence must retain
the full original result and check the projection against it.

W1 verification: independent hard-coded payload bytes/offsets; every field's
encode/decode invalid cases; integer/float/time boundaries; every permitted tail
length; unknown versions/domains/profiles/origins and true eligibility rejection;
XML field/type/order/ID agreement; old Flight payloads rejected; prior Flight
codec regression. These tests do not substitute for generated-code, framing,
signature, native projection or live-owner conformance.

## Review record

Source tracing: GPT-6 Luna; clock/state mathematics: GPT-6 Astra; implementation
and independent test work are attributed in the W1 verification record.
GPT-6 Astra reviewed the W1 contract and clock/state mathematics: payload-only
release approved, with explicit request-vs-poll rejection, binary32 rounding and
profile-specific generation clarifications incorporated. Native N1 exact API,
serialization, generated wire and runtime gates remain open. W1 implementation
verification is recorded in C1E_WIRE_VERIFICATION.md; this is not a native release.

W2 generated C/Python payload/frame conformance now passes Debug, optimized and
ASan/UBSan checks; see C1E_GENERATED_WIRE_CONFORMANCE.md. This closes the offline
generated-wire prerequisite only. Signing/replay, exact native transaction API,
status projection and runtime ownership remain N1 work.

The [N1 transaction contract](C1E_N1_TRANSACTION_CONTRACT.md) fixes the
single-slot handoff, parser/replay split, Commander linearization, actual
result projection, closure and evidence rules. The private parser, owner,
worker, bridge and output-disabled build profile now implement that design.
The source-stable build, stack review, literal zero-datagram process, two
source-bound disarmed negative traces, live hostile traffic and bounded
injection fixtures passed. C1E-N1 is accepted only at its negative diagnostic
scope; see [N1 verification](C1E_N1_IMPLEMENTATION_VERIFICATION.md) for the
finite-case evidence, structural non-applicability decisions and remaining
operational limits. Positive admission remains a separate gate.
