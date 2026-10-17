# Positive diagnostic admission: provenance and lifecycle design

2026-09-27, Codex (GPT-6). Design checkpoint P1; implementation and native
positive admission remain open. The accepted runtime is still C1E-N1,
disarmed/output-disabled and negative-only. This document does not change N1,
the frozen Flight wire contract, baseline revision 2, or physical acceptance.

## Result and source basis

The next coherent implementation should be a **closed provenance collector**:
retain real estimator, transport, operator and clock receipts and independently
check their chronology, while leaving selection Disabled and requests rejected.
Only after its evidence and the separate positive protocol/API review pass may
a distinct profile attempt `RequestValidatedDisarmed`. That result must still
have no eligible control demand or actuator publication.

Existing foundations and limitations, inspected at this checkpoint:

| Anchor | Finding | Design consequence |
| --- | --- | --- |
| `NativeIngressDiagnosticOwner.cpp`, `NativeIngressSourceBridge.cpp` | N1 owns actual Commander chronology but deliberately supplies unavailable prerequisites. | Preserve its API and build; do not add an enable flag to N1. |
| `DiagnosticAdapter.cpp`, `WireAdapter.hpp` | Separate diagnostic selection, operator attachment, session, ACK and request operations already exist behind private/offline boundaries. | Use actual diagnostic outcomes and bindings; do not transplant Flight administration semantics or expose an adapter pointer. |
| `TrustedTimeMap.cpp` | Conversion is a checked frozen offset with a nonzero uncertainty and inclusive producer interval. | The provider must prove this map; the converter cannot establish trust. |
| `platforms/posix/src/px4/common/drv_hrt.cpp`, `hrt_absolute_time()` | Lockstep reads scheduler time; the alternative reads `px4_clock_gettime(CLOCK_MONOTONIC)`. | Record compiled provider and clock namespace; the C1H host identity-map proof does not qualify either profile automatically. |
| `EKF2Selector.cpp`, `PublishVehicleAttitude/PublishVehicleLocalPosition` | Selected outputs preserve sample time, filter stale/non-increasing samples and replace publication timestamps. | Keep original sample and publisher chronology; selector publication age alone is insufficient. |
| `EKF2.cpp`, `PublishStatusFlags` | Status flags publish roughly once per second or on observed control/fault changes. | A generic short topic-age budget would reject ordinary operation; source qualification must specify per-input cadence and continuity. |
| `VehicleAttitude.msg` | Estimated attitude and groundtruth share a schema; quaternion has no standalone validity Boolean. | Qualify exact publisher/topic/instance and estimator health, never schema or quaternion norm alone. |

The graph records exact source hashes including untracked research files.
These are source findings, not native timing measurements or a claim that all
estimator mutation sites have been reviewed. See the existing
[N1 transaction contract](C1E_N1_TRANSACTION_CONTRACT.md) and
[diagnostic selection design](C1B_SITL_DIAGNOSTIC_SELECTION_DESIGN.md).
The later [P2A source audit](C1E_P2A_PROVENANCE_COLLECTOR.md) adds two constraints:
invalid attitude can result in no publication, and delayed status-flag sample
time cannot share a generic current-attitude freshness budget. P2A's implemented
archive collector does not satisfy the live P2 receipt/continuity gate below.

## Trusted receipts and one owner

Continue cooperative trusted-local SITL, as already selected in the
[console design](SITL_SUPERVISOR_CONSOLE_DESIGN.md). This is not isolation from
a malicious process with the same user credentials and is not Pi deployment.
Guidance has only its configured request/status endpoint and signing material.
It cannot publish estimator receipts, install maps, attach an operator or mint
grants. Source capabilities are established locally before ingress starts.

Every trusted receipt binds run, owner and controller lifetimes; publisher
identity and boot/lifetime; a nonzero checked sequence; source clock identity;
source observation time; owner receipt time; kind and immutable payload.
These envelope fields are a proposed collector contract, not current wire fields.
Unknown identities, repeat Initial, sequence gaps/regression, counter exhaustion,
contradictory payloads or lost source continuity close the collector lifetime.
Normal samples cannot overwrite a sticky fault. Restart requires fresh
identities and a new owner; it cannot repair a closed owner in place.

Commander remains the single admission mutator. Publishers submit bounded
receipts, never call the adapter. Source-side hooks must capture every change
that can revoke readiness, including invalid-to-valid ABA, before coalescing.
Each publisher has its own contiguous stream; the owner merges them in an
explicit recorded order into one adapter state generation. Do not compare
unmapped producer timestamps to invent a global order. Ordinary fresh samples
advance receipt sequence only; any admission-field/map change advances the
merged state generation, including a transition back to identical values.

Latest-value uORB polling alone cannot establish lossless continuity. A closed
collector may observe it and report missing proof; it must not mark readiness
qualified. Hook completeness, queue bounds and publication synchronization
need a source audit before positive use. Queue overflow or inability to reserve
all transaction and terminal evidence closes before mutation. Commander never
waits for sockets, console, parser, publisher acknowledgment or archive I/O.

At each owner boundary: commit available source receipts in their defined
order; detect stale leases using current qualified clocks; acquire sticky
faults; reserve all output/evidence capacity; then perform at most one bound
administrative or ingress transaction. Final fault acquisition and context
comparison immediately before mutation is the linearization point. A fault
already observed wins; one published afterward closes at the next boundary
without relabeling the historical result. Measure the observation latency;
do not claim instantaneous knowledge of another thread or remote process.

## Estimator qualification

Proposed diagnostic-required state is finite attitude, local position and
velocity with their valid flags, aligned tilt/yaw, and no applicable estimator
fault. Exact health-mask choice, reset policy, norm tolerance, freshness,
cross-topic skew and source-observation budgets remain a release gate. They
must be explicit immutable profile inputs with evidence, not new defaults.
This diagnostic predicate does not establish launch/control feasibility.

Bind the selected EKF instance, selector lifetime/change count, estimator
lifetime, sensor device identities and exact source topics. Single-instance
and selector-enabled builds need separately enumerated publisher paths. Reject
groundtruth, external-ins substitutes and replay-mode sources in the proposed
profile. Simulated sensor inputs may feed the actual estimator; simulator truth
remains scoring evidence only.

Retain sample time, original estimator publication time, selector publication
time when applicable, receipt time and owner evaluation time separately.
Validate chronology and mapped uncertainty for each clock domain. Cache reads
do not refresh sample age. Bundle only matching instance/lifetime inputs with
bounded sample skew; do not join an old status record to a new selected instance.
Status change counters and sample reset counters are evidence, not proof of
every internal mutation: enumerate source hooks or retain readiness false.

Instance switch, reset, sensor identity change or prerequisite loss installs
an observed barrier and clears old ACK/session authority according to the
diagnostic adapter. A known continuous transition may later recover through
explicit selection/session/ACK preparation; lost continuity or unobserved
restart is terminal. No request may span the old and new provenance tuple.
Qualification expires on owner polling even when no datagram arrives.

## Transport and operator lifecycle

Keep transport binding distinct from admission session. The transport publisher
owns socket/key/parser and records configured peer, directional link IDs,
header/target IDs, channel, key fingerprint and worker lifetime. It may report
local endpoint health and verified peer liveness under a reviewed lease; a
successful UDP bind/send does not establish connectivity or peer receipt.
Fresh signed guidance may prove only its narrowly bound peer liveness, never
operator presence. Silence expires a declared lease; it does not identify a
remote crash. Local fatal error, restart and key change invalidate old work.
Lease duration and worker-progress budget require measurement before release.

Use a separately inherited local operator capability owned by the runner and
console broker, unavailable to guidance. The concrete descriptor/process
bootstrap and EOF/heartbeat handling are required in the implementation API
review. Operator attach creates a fresh instance; attach itself grants nothing.
Broker-owned prepare/confirm captures an immutable binding to the current
run/owner/controller, operator, state generation, transition and selector
epochs, and full prospective map. UI input is intent only. Recheck that binding
at commit; a stale confirmation cannot be silently rebound.

Lifecycle sequence for a future positive profile:

1. Start closed with verified source lifetimes and no attachment/session/ACK.
2. Attach the independent operator; gather qualified estimator, transport and
   map receipts, still closed.
3. Explicitly prepare/confirm diagnostic selection against the current binding.
4. Pair authenticated session establishment with one matching independent
   broker grant in the owner turn. Selection and establishment are separate.
5. Return the actual post-establishment barrier/status. Accept a fresh mapped
   ACK naming that status and current identities, then a later mapped request.
6. Retain `RequestValidatedDisarmed` only when returned by the real adapter;
   access and status must continue to show no eligible control demand.

Use the diagnostic adapter's actual grant-consumed flag and floor semantics;
do not assume a rejected binding consumed a grant because an older Flight
administration document says so. The broker is the sole grant-stream owner.
Uncertain submission is quarantined rather than retried with a new grant or
reconstructed adapter. Preserve parser replay commit/discard rules and audit
packet replay independently from grant/session floors.

Explicit detach and unexpected capability/lease loss retain distinct first
causes and terminally close this diagnostic owner; reconnect needs a new owner.
Disable/revoke/clear remain explicit serialized operations with the existing
diagnostic predicates. Clear cannot undo terminal provenance loss. An armed or
unknown physical observation closes positive diagnostic operation; Offboard
never implies diagnostic selection. Loss of returned status grants no ACK and
cannot prolong authority. No operator timeout is measured solely by paused
simulation time: a real monotonic watchdog must publish sticky loss for the
next owner boundary, with its latency/budget separately qualified.

## Producer-to-validator clock map

For producer p, map M(p)=p+O with frozen signed offset O and conservative U>0.
The provider binds the entire inclusive producer interval [p0,p1] to run,
controller, producer lifetime, source/session, provider/clock identities and
map epoch. It retains raw qualification evidence and invalidation conditions.
Only trusted provider receipts install it. Packet offsets, signing timestamps,
one RTT or apparent clock proximity cannot create a valid map.

The first candidate qualification path is a separate non-lockstep POSIX SITL
profile with a source-proven common monotonic timeline. Audit its compiled
clock implementation, boot/time namespace, any clock interception/offset and
representation rounding; bracket reads to verify the deployment matches the
proof. C1H's O=0, U=1 us is not copied as a result. Lockstep and host monotonic
clocks do not have a frozen-offset relation through pauses/rate changes. A
future lockstep provider must independently prove its time contract or remain
unavailable. Pi-to-Pixhawk mapping is a separate future qualification.

Where clocks can drift, a finite interval needs an evidenced worst-case bound
covering initial synchronization error, relative drift, quantization and provider
error throughout that interval. Scheduling/queue delay counts as request age,
not a convenient reduction of uncertainty. Missing drift/pause/namespace proof
leaves the map invalid. Provider failure/expiry is polled without ingress.

At actual evaluation n, creation c, expiry e, age bound A and future bound F:

- Both raw endpoints and ACK time are inside the qualified producer interval.
- c_minus=M(c)-U <= n; c_plus=M(c)+U <= n+F.
- n-c_minus <= A; e_minus=M(e)-U > n.
- M(e)-M(c)=e-c, with checked arithmetic including negative offsets.
- ACK a_minus must be strictly after the current barrier time; request
  c_minus must be strictly after retained ACK a_plus. With one map this needs
  a raw request-after-ACK gap strictly greater than 2U.

Age/future equality is allowed; expiry/barrier/ACK equality is rejected. Arrival,
source-check and evaluation times remain distinct; both earlier times must be
<= evaluation, with no assumed order between arrival and source-check. A long
queue delay can reject a validly authenticated packet. Map installation is
trusted state, not session establishment; a session identity may be reserved
prospectively without granting any request authority.

Choose conservative map replacement: close old session authority while
disarmed, install a fresh immutable map epoch and state generation, then require
explicit selection as needed, a new session binding, establishment and new ACK.
Never rewrite a map in place or reinterpret queued candidates under a new map.
Every pending packet/grant/status retains its original epoch and is rechecked.
Loss of provider continuity is terminal, not a normal map replacement.

## Finite release gates and next increment

| Gate | Required result before positive runtime | Current status |
| --- | --- | --- |
| P1 source contracts | Provenance, lifecycle, map and separation requirements written and source anchored. | Independent requirements review passed 2026-09-27; implementation gates remain separate. |
| P2 closed collector | Actual publisher paths/hook completeness, bounded merge, stale/restart evidence, immutable budgets, stack/resource review. | P2A/P2B observations, P2C bounded host merge, P2D typed native transfer/one-shot EKF lifetime, P2E successful-update/output/reset/recorded-sensor provenance and P2F direct instance-zero publisher/status-change provenance are independently verified. P2F deliberately excludes the EKF2 selector and multi-instance path; all authority fields remain false. Full gate open: admissible health semantics, selector-enabled selected-instance lifetime, complete mutation/deployment-sensor coverage and qualified readiness/budgets remain absent. |
| P3 clock qualification | Provider proof and retained deployment evidence across whole interval, expiry/replacement/pause/restart cases. | P3A deployment observations, P3B host candidate lifecycle and P3C native checked receipt-clock propagation accepted. Legacy sample-clock handling, provider semantics/continuity, pause and qualified map/owner integration remain open. |
| P4 authority and protocol | Exact private API, local capability/liveness, broker quarantine, distinct positive wire/profile and independently checked status projection. | Open; W1/N1 accepts zero map and negative outcomes only. |
| P5 finite acceptance | Independently checked disarmed traces and gate-directed fault cases below; build excludes outputs and prevents mixed profiles. | Open; no positive run authorized by this document. |

Required case families, each with a retained owner order and explicit expected
closure/barrier/disposition: stale sample despite fresh republish; selected
instance/reset/ABA and dropped receipt; publisher restart; transport fault and
peer lease expiry; operator detach/loss versus request on either side of the
linearization point; stale confirmation and uncertain submission; map expiry,
replacement and pause; exact age/future/expiry/ACK endpoints and overflow;
queue delay; evidence exhaustion; status delivery failure. Positive success
must cite the qualified inputs and actual adapter result. Reuse existing
canonicalization and guard tests; add only cases closing these new gates.

Do not relax frozen N1 codecs to carry positive results. The new protocol must
specify session/ACK messages, nonzero map binding and unambiguous domain/profile,
generated conformance, replay, status correlation and unsupported-message
rejection before integration. Keep the Flight dialect unchanged.

Plant mass/CG/inertia does not block the closed collector or this design.
It does block physical plant acceptance and control claims. No baseline repeat
is needed; second-operator reproduction remains explicitly deferred.
