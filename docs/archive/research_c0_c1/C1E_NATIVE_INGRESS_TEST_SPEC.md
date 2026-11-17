# C1E native diagnostic ingress: test specification

2026-09-24 to 2026-09-25. **W1 payload matrix verified; C1E-N1 accepted at
its bounded negative-only, disarmed/output-disabled diagnostic scope.** The
applicable injected/stress cases, structural non-applicability decisions and
limits are recorded in [N1 verification](C1E_N1_IMPLEMENTATION_VERIFICATION.md);
offline W1 evidence is in [W1 verification](C1E_WIRE_VERIFICATION.md).
The [N1 transaction contract](C1E_N1_TRANSACTION_CONTRACT.md) fixes the private
API, single-slot state machine and result/replay order for this matrix.
The exact C1E-W1 payload contract is in
[C1E_NATIVE_INGRESS_DESIGN.md](C1E_NATIVE_INGRESS_DESIGN.md); W1 is its isolated
schema/codec prerequisite. C1E-N1 is the subsequent output-disabled native ingress increment
after accepted C1D-N2. N1 is **negative-only**: it may authenticate and account
for diagnostic traffic, but cannot establish a positive native request result,
an eligible request, or control authority. The companion C1E design and N1
transaction contract fix the implemented parser/event staging.

## Fixed boundary and release dependencies

Preserve the separate C1D closed-owner experiment, real raw arm/nav chronology,
native hrt time, retained closure, and output-disabled private source view.
N1 uses its own opt-in owner/profile with one by-value diagnostic adapter and
guard; no second admission owner, independent parser-owned adapter, public mutable adapter access,
control publication, estimator invention, or research-mode inference from
Offboard. The native process stays disarmed and maps every known raw nav to
research `NotSelected` in this increment.

For N1 release, review the exact bounded ingress queue and
event order, single serialized owner turn, parser candidate/commit API, transport
loss source, signed endpoint/key/identity lifecycle, local and
returned status ownership, evidence schema, and stack/build effect. The current
host `SignedMavlink::authenticate` requires nonzero expected map identity; C1D has
no valid map. A fake map, synthetic estimator-valid bit, or bypass of parser
binding checks is not an admissible test seam. The separate diagnostic parser
has an explicitly bound authentication stage while absent map remains a
distinct closed rejection. A successful signature is never a map
qualification or session authorization.

Frozen Flight wire/status v1 has no diagnostic-domain or selector identity.
W1 must specify a separate versioned diagnostic wire/status schema and endpoint
profile before native status transmission, with explicit domain, native run,
controller and owner lifetime, source/session, transition/map (including absent
map), arrival versus evaluation correlation, actual source sequence/generation,
actual rejection, and `eligible_for_control=false`. A codec does not authenticate
a frame or certify any source, clock or estimator fact. Unknown-schema consumers
must fail closed. Never reinterpret Flight v1 reserved bits, decision codes or
`Selected`; do not serialize native results as `research-c1b-offline/v1` or
`research-c1d-native-owner/v1` if the C1E shape differs. A local evidence-only
stage may precede transmission, but silence or a dropped status is not success.

## W1: isolated diagnostic schema/codec prerequisite

W1 has no PX4 attachment, endpoint, key, parser replay state, operator action,
session administration, source-truth claim or native runtime result. Exact
fields, enum/reason mapping, version and canonical encoding are defined by the
reviewed payload contract; the cases below are its
finite acceptance matrix.

| ID | Stimulus | Required result |
| --- | --- | --- |
| W01 | Encode/decode canonical boundary vectors for every defined diagnostic status class, rejection type and absent-map/state form. | Byte-level round trip is exact; identity, source sequence/generation, arrival/evaluation correlation and `eligible_for_control=false` survive unchanged. |
| W02 | Feed wrong version/domain, unknown enum/flag, unsupported extension, inconsistent optional, length, reserved value, out-of-range number, noncanonical encoding and truncated/trailing input. | Strict rejection with no partial output or fallback to Flight v1. |
| W03 | Cross-feed diagnostic values through frozen Flight v1 codecs and Flight values through diagnostic codec; compare Flight golden vectors and existing offline evidence. | Cross-domain rejection and byte-identical Flight behavior; no reserved-bit or numeric-code reinterpretation. |
| W04 | Check within-payload arrival <= evaluation, source-checked <= evaluation, source sequence >= state generation, nonzero identity/counters and zero unavailable-map marker, including equality boundaries. | Invalid scalar relationships reject. Cross-message provenance, replay and actual result correlation remain N1 checks; codec acceptance never asserts authentication or physical truth. |
| W05 | Inspect isolated Python/XML changes and run existing Flight codec regression. | No build/startup registration, network, PX4, adapter, output or signing/key dependency is introduced; frozen Flight payloads remain unchanged. Generated MAVLink conformance is a separate N1 prerequisite. |

W1 acceptance requires reviewed exact schema, independent positive/negative
codec vectors, frozen Flight regression and source review.
Passing W1 authorizes only the isolated codec. It does not satisfy N1, prove
signed ingress or release a native endpoint.

## W2: generated C/Python wire conformance

Accepted at its offline scope; see C1E_GENERATED_WIRE_CONFORMANCE.md for exact
counts, source hashes, retained reports and limits. W1 and frozen Flight v1 are
preserved. Acceptance requirements:

| ID | Check | Required result |
| --- | --- | --- |
| G01 | Pinned clean MAVLink/pymavlink, XSD, import origin and dialect collisions | Exact revisions, schema and two-message allocation; no installed-generator substitution or CRC-bypass environment |
| G02 | Explicit field/offset table, independent X25/CRC-extra and full-frame oracle | Generated C layout/constants and Python/C bytes agree; generated base MIN_LEN is distinguished from W1 logical trimmed minimum |
| G03 | Canonical request/status, all permitted input tail lengths, integer/float boundaries | Python encode -> C decode/reencode and C encode -> Python decode agree with independently constructed bytes |
| G04 | Bad CRC, truncated/oversized/concatenated frame, unsupported flags, unknown ID and MAVLink 1 | No known diagnostic delivery through the declared fixture boundary; parser behavior and wrapper checks are distinguished |
| G05 | Correctly framed invalid domain/profile/map/eligibility, nonfinite values and other W1 semantic violations | Generated C/Python parse the payload; unchanged W1 validator rejects it. Transport decode alone never means admission |
| G06 | Debug, optimized and ASan/UBSan fixture runs with source/tool/flag hashes | Same finite cases pass; failures/nonzero execution cannot become a pass |
| G07 | W1/Flight preservation and independent source review | Existing payload suites pass; schema, codecs, native owner and firmware configuration remain unchanged |

G04 checks an offline single-frame fixture, not a production parser, signature,
replay, channel binding or real-time transport. W2 acceptance does not release
N1 runtime. Exact case counts and retained outputs belong in the W2 verification
record, including any corrected failed attempts.

## N1: finite cases for the negative-only native release

Each case requires exact input identity, ordered source/owner/parser events,
actual diagnostic result and retained snapshot/record counts. Native runtime
cases use fresh disarmed processes; injected races and malformed traffic use
bounded host/native-contract fixtures first. Fixture facts are labelled
synthetic, and cannot be promoted to native estimator or clock evidence.

| ID | Stimulus and boundary | Required result/evidence |
| --- | --- | --- |
| N01 | Compile default, offline, native-contract and native POSIX profiles; try mixed macros, proxy/lockstep/board targets and capability spoofing. | Explicit opt-in only; ordinary Flight and offline behavior unchanged; one native owner/guard; forbidden combinations fail build. |
| N02 | Start, sample, stop and export without ingress; separately rerun C1D closed-owner regressions. | New native raw state/generation/hrt, immutable config and closure remain truthful; C1D behavior remains intact. No session, operator, ACK, positive request or output appears. |
| N03 | Read a consistent disarmed Commander source checkpoint, bind its observed epoch into one well-formed signed frame, and feed the bound endpoint while map, estimator and admission transport remain unavailable and selector Disabled. | The checkpoint matches a retained source record and precedes request arrival. Actual `DiagRequest` rejects `SelectionRequired` (wire reason 4) after the preceding poll can report `ClockMappingInvalid`; both are preserved distinctly. No `RequestValidatedDisarmed` or eligible demand is invented. Arrival and evaluation are separately correlated. |
| N04 | Feed unsigned/MAVLink 1, bad MAC/key/CRC, unsupported flags or ID, short/trailing/concatenated/split datagrams, wrong link/channel/header/target/source/session/controller/transition, and noncanonical reserved payloads. | Rejected at the documented stage with no packet-specific adapter operation or durable replay commit where binding is incomplete; a real trusted observation/poll may still precede rejection. Malformed traffic cannot alter trusted source, operator, session, ACK or selection. |
| N05 | Offer a forged high signing timestamp, equal/lower replay time, duplicate frame and then a valid newer frame; repeat across a fresh process with new identities/key. | Authentication candidate and current binding checks precede durable replay commit; only a completed transaction consumes replay. Invalid candidates cannot poison replay; duplicates of committed transactions reject; old-run frames and status reject after restart. Signing time is not freshness or a guidance clock map. |
| N06 | Race authenticated packet, attempted operator attach/prepare/confirm, detach, unexpected loss and revoke against a Commander source transition. | One serialized order is recorded. N1 enables no operator grants, selection or session administration; attempts reject without authority mutation. Safety/source/lifecycle loss closes before later ingress; packet arrival never creates an attachment or grant. |
| N07 | Flood the bounded ingress/status queues; stop or close at enqueue/dequeue/owner-turn boundaries; retry a late frame. | Declared overflow policy rejects/terminalizes without skipping safety events, advancing authority floors, blocking Commander hooks or creating a second owner. Closure is sticky, late work cannot reopen it, and status backpressure cannot delay closure. |
| N08 | Exercise initial, unchanged assignment, arm/nav change, ABA transition, unknown raw value, timestamp regression, source counter exhaustion and source lifetime loss while packets wait. | Commander source chronology and physical generation follow C1D rules; malformed source closes before an ordinary adapter turn; queued packets cannot overwrite the retained last-valid reference. No synthetic arm/nav or Flight `Selected` state is accepted. |
| N09 | Verify actual native estimator and transport inputs remain unavailable; inject stale/contradictory/restarted publisher facts only in bounded fixtures. | Native evidence names actual source and absence. No fixture Boolean, simulator truth or packet field supplies `required_state_valid` or transport continuity; selector remains Disabled and ACK absent. |
| N10 | Exercise explicit absent map and unqualified clock natively; use zero/unknown epoch, stale/replaced map, regressing time and out-of-interval endpoints only in bounded fixtures. | Actual primary clock/replay reason is retained; no positive validation. Native hrt is validator chronology only; signed timestamp and host clock proximity do not qualify a producer-to-validator map. |
| N11 | Corrupt or cross-feed diagnostic and frozen Flight status/schema versions, domains, selector fields, correlations, identities, null/absent map values, lengths, duplicate JSON keys and counter ordering. | Strict decoder and independent evidence checker reject corruption/cross-domain replay; output is never decoded as Flight success or accepted merely because a signature is valid. |
| N12 | Inspect source view, linked builtins, startup, publisher paths and two fresh process traces; inject incomplete export/status sink. | No actuator/control/vehicle-command publisher or output driver is selected; disarmed/output-disabled state and one-owner lifetime reconcile. Sink failure marks evidence incomplete and cannot block owner closure or imply a passed run. |

## Evidence and acceptance rule

For N01-N12, the expected transaction dispositions and mutation boundaries are:

| Cases | Disposition and state change | Returned status |
| --- | --- | --- |
| N01-N02, N08-N10 without a valid frame | No candidate; only valid source chronology/polls or first closure may mutate the owner. Parser replay is unchanged. | No correlated status. |
| N03 | `Completed`; reserve two results, poll then one actual request turn; commit replay after owner result. | Signed `Rejected/SelectionRequired` reason 4 from the request operation. |
| N04, N05 invalid/stale frame | Local parser reject or `Discarded`; no packet-specific adapter turn or replay commit. A separate real source poll may occur. | None for unknown/unauthenticated identity. |
| N05 current valid retry | `Completed` once; duplicate of that committed frame rejects at replay stage. | Exactly one correlated status for the completed turn, subject to recorded send failure. |
| N06-N08 loss before owner turn | `Discarded`; first loss closes through the independent terminal slot before ingress. No request turn or replay commit. | `Closed` reason 6/7/8 only for an initialized source and known authenticated request; delivery failure is retained. |
| N06-N08 loss after owner turn | Earlier `Completed` result/replay remain historical; later first loss closes separately. | Earlier status retains its actual operation reason. |
| N07 busy slot | Drop before authentication; checked local busy count only. Counter/evidence exhaustion closes through terminal capacity. | No response for an unknown dropped frame. |
| N11-N12 | No parser/owner mutation from corrupted export or sink. An actual send/export failure marks evidence incomplete. | Corrupt or missing status never counts as delivery. |

The mailbox capacity is one candidate and one result in the same slot; the
archive reserves 256 source records, 32 two-result transactions and one
independent terminal record. The worker retains 64 pre-authentication rejection
records and checked per-reason/busy-drop counts; the 65th rejection or counter
exhaustion is a sticky resource fault. `Empty/Preparing/Ready/Processing/Resolved/Empty`
publication, token match and worker acknowledgment are mandatory. The worker
commits replay only on a current authenticated `Completed` disposition after
the real request turn, including application rejection. It discards staged
replay on stale, foreign, pre-turn loss or closure. It cannot authenticate the
next frame while any candidate/result is outstanding. No source hook waits for
the worker. The only normal N1 request status is reason 4; close reasons 6-8
require matching first closure evidence. Absent map never becomes a parser
shortcut or reason 1 substituted for the actual request operation.

Verify ordered records against an independent oracle, including
negative corruption tests and repeated read-only post-stop export. A host
fixture result cannot stand in for the two source-bound native runs, and a
native trace cannot prove an unobserved transition or accurate clock map by
itself. Preserve failures and incomplete runs.

N1 passes only if every applicable negative case is explained by
actual source-bound evidence, ordinary/offline/C1D regressions and build
isolation pass, and independent source/schema review agrees. It must report zero
`Accepted`, zero `RequestValidatedDisarmed`, zero eligible request, and zero
control/output publication, with Disabled selector, no operator/session grant,
and no ACK. Positive diagnostic validation requires separately
qualified estimator, transport/operator provenance, producer clock map,
selection/session/ACK progression, and reviewed wire identity. Operational
timing, plant, controller and flight acceptance remain later gates.
