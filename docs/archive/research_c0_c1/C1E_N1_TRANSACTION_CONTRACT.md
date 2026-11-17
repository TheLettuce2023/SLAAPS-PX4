# C1E-N1 native transaction contract

2026-09-24. Exact design for the next negative-only, disarmed, output-disabled
native experiment. This is an implementation contract, not native acceptance.
The [C1E design](C1E_NATIVE_INGRESS_DESIGN.md) fixes the wire values and safety
boundary; the [finite matrix](C1E_NATIVE_INGRESS_TEST_SPEC.md) fixes acceptance.
The C1D closed profile and its accepted evidence remain separate.

## Build and endpoint boundary

Add an opt-in `c1e-negative-native/v1` profile with its own target marker and
private Commander source overlay. The build-profile header must accept that
marker only with native Linux POSIX PX4 SITL, reject it with C1D/offline/host
markers, hardware, lockstep and timestamp proxies, and derive diagnostic
capability internally. A separate explicit synthetic native-contract marker is
allowed for host tests. No ordinary PX4 or C1D target gains the C1E code.
Compile the C1E source view, one owner, one endpoint worker, the generated
diagnostic dialect and the diagnostic adapter into the private profile only.
Audit linked builtins, startup commands and publication paths before runtime.

Bootstrap exactly one process-lifetime owner and worker with immutable nonzero
native run, owner lifetime, controller, source, transport binding, producer
lifetime and source clock identities. The transport binding is a disposable
endpoint identity, never an admission session. Bind one explicitly configured
IPv4 loopback UDP receive endpoint and one loopback return endpoint, distinct
nonzero configured ports, MAVLink sender system/component 42/191 and native
target 1/1, incoming/outgoing link IDs 7/8, one configured channel and a
per-run 32-byte signing key. A private runner creates this key from the OS
cryptographic random source and supplies it in an owner-only 0600 regular
bootstrap file. The native module opens it once with no symlink following,
checks exact length and ownership, reads it before endpoint activation and
unlinks it; the path, but never key bytes, may appear in startup arguments.
`recvfrom` verifies the exact configured `127.0.0.1:peer_port` tuple before
parsing. A different peer tuple is a local pre-authentication rejection; the
W1 transport ID remains correlation, not proof of operator authority.
Reject missing, reused or contradictory bootstrap values. Key material is
supplied through a restricted local bootstrap channel, never logged, exported
or accepted from a datagram; wipe worker-owned copies on retirement. A restarted
process requires new run, owner, transport identity and key. The run manifest
records endpoint identities and a key fingerprint, never the key. UDP silence
does not prove peer or operator loss. Local bind/receive fatal error and
explicit endpoint shutdown are transport faults; the worker release-publishes
a sticky fault bit. An unreported worker crash is a process/lifecycle failure,
not an invented operator-loss observation. Only Commander may close the adapter.

## Private API and ownership

The public production interface has no owner constructor, adapter reference,
authentication Boolean, request turn, attach, enable, grant, ACK, clear, revoke,
reset, demand or mutable-state accessor. The private bridge alone constructs
and calls the owner. The worker alone owns parser, key, incoming replay streams,
outgoing signing floor and socket. A private, single-slot mailbox owns only
bounded values; it never lends references into parser or adapter storage.
The private interface uses these operations. `ByteSpan` is a pointer plus
bounded length, not a borrowed parser buffer. `ChannelId` is the configured
uint8 channel. `RawState` is the C1D raw arm/nav pair. Every operation is
`noexcept` and allocation-free on the source path:

```cpp
// Worker-only; all fields are immutable once Ready is published.
StageOutcome stageDatagram(ByteSpan one_datagram, ChannelId channel) noexcept;
// Commander-only, at a source loop boundary, after source assignments.
void resolveReady(RawState current, uint64_t evaluation_hrt_us) noexcept;
// Worker-only; consumes one owner disposition, then retires the slot.
void finishDisposition() noexcept;
// Commander-only; sticky closure and a separately reserved terminal record.
void stopOrFault(CloseCause cause, RawState current, uint64_t hrt_us) noexcept;
```

`StageOutcome` is one of `Published`, `Busy`, `Closed`, `NotConfigured`,
`MalformedEnvelope`, `WrongMessage`, `WrongChannel`, `BadCrc`, `BadSignature`,
`Replay`, `WrongHeader`, `WrongLink`, `InvalidPayload`, `WrongIdentity`,
`WrongTarget` or `ResourceFault`, with an optional token only for `Published`.
`CloseCause` is `OwnerStop`, `SourceContinuityLost`,
`OwnerEvidenceExhausted`, `OwnerConfigurationInvalid`, `TransportFault` or
`WorkerResourceExhausted`; it
is never `OperatorLost`.
The C1E profile needs a distinct internal `DiagnosticCloseReason::TransportFault`
whose adapter closure is terminal. It must not relabel transport failure as
`SourceContinuityLost`, `OwnerStop` or `OperatorLost`, and C1D's result values
and behavior must remain unchanged.
The C1E profile also needs a distinct terminal
`DiagnosticCloseReason::WorkerResourceExhausted` for worker ledger, count or
token failure. It must not be recorded as owner archive exhaustion. W1 wire
reason 8 is available for a known authenticated request after source
initialization; unsolicited resource closure stays in native evidence.
The owner disposition is `Completed`, `DiscardedStale`, `DiscardedForeign`,
`DiscardedClosed` or `Faulted`. Only `Completed` carries an actual request
operation result and may consume incoming replay. A closure disposition can
carry one actual closure result and a correlated status, but cannot consume
incoming replay. The source API receives no packet bytes or caller-built
authentication verdict: `resolveReady` reads only the private mailbox.

`StageOutcome` can publish one `Candidate` or return a bounded local parse
failure. A candidate contains a monotonically increasing nonzero slot token,
the fully decoded W1 request, exact frame/signature metadata, configured
endpoint/channel identity, arrival native hrt and parser stage code. The worker
retains its staged replay image privately, indexed by the same token. Candidate
byte size is compile-time bounded. Only the worker may create it after full
authentication. The mailbox contains no trusted physical,
estimator, map, operator or admission state. The candidate's authentication
evidence is private to the owner/worker bridge; it is not a caller-settable
`SessionIngressVerdict`. Commander creates `NativeDiagnosticIngress` internally
only from the acquired authenticated candidate and compiled native origin.

The slot states are `Empty -> Preparing -> Ready -> Processing -> Resolved ->
Empty`. The worker alone writes `Preparing` and `Ready`; Commander alone claims
`Ready` and writes `Resolved`; the worker alone reads `Resolved` and empties it.
After acquiring terminal phase, the worker may cancel `Preparing` or use a CAS
to retract an unclaimed `Ready` slot and return it to `Empty`. If Commander has
already claimed `Processing`, that CAS fails and the worker must consume its
eventual `Resolved` disposition. Neither side reads or writes slot bytes while
the other owns that state.
The worker publishes candidate bytes with release `Ready`; Commander acquires
them before reading. Commander publishes immutable result/disposition bytes
with release `Resolved`; the worker acquires them before signing, sending or
altering replay. A generation/token match is required on both transfers.
Neither source calls nor shutdown wait on a slot, lock, parser, acknowledgment,
socket or sink. The worker cannot stage/authenticate another frame until the
prior slot reaches `Empty`. A datagram arriving while occupied is dropped
before authentication, increments a checked local busy counter and cannot
advance replay. Checked counter exhaustion is retained as a resource fault.
The slot token is checked before increment; overflow is the same sticky
resource fault and cannot wrap to an older token.
There is no second ingress or status queue. One result remains in the slot until
the local UDP send returns success or a classified send failure is retained;
then the worker releases `Empty`. A successful local send is not proof of peer
receipt. A missing response is never counted as successful delivery.
The worker retains the first 64 pre-authentication rejection records, plus
checked per-reason counts and a checked busy-drop count. The 65th rejection
sets a sticky resource fault and preserves the prefix; counter exhaustion does
the same. It cannot overwrite a source or transaction record. The datagram
receive buffer is `MAVLINK_MAX_PACKET_LEN + 1` bytes so oversize is detected
without truncating a valid maximum frame. All slot and ledger storage is fixed
before endpoint start; a failed allocation prevents owner/endpoint activation.

## Authentication and replay

Implement a separate diagnostic parser accepting request ID 200010 and signing
status ID 200011; do not cast the
W1 payload into the frozen Flight dialect or relax `SignedMavlink`'s nonzero-map
rule. The worker accepts exactly one complete MAVLink 2 signed frame per UDP
datagram, with exact length, known ID, flags, CRC, signature, incoming link,
source header, channel, configured targets and W1 logical payload shape. It
rejects MAVLink 1, unsigned, fragments, concatenation, trailing bytes and
unsupported extensions. The key, signer and replay candidate are private
copies until final disposition. W1 run/owner/controller/source/transport IDs
must match immutable bootstrap; W1 map epoch must be exactly zero. Transition
epoch must be nonzero but is checked against the live source only by Commander.
Signing time is a 48-bit replay value, not a producer clock or age proof. No
parser input can set a valid map, estimator, operator or session bit.
The worker samples actual native hrt on receipt and requires nonzero,
nonregressing arrival time within its lifetime; failure publishes the local
transport fault and stages no request.

Do not call the existing parser's `authenticate(...expected_map=0)`: it rejects
that context. The new authentication-only stage checks absent-map identity
explicitly and stages replay without committing it. An invalid frame, failed
binding, duplicate/old signer timestamp or busy slot leaves durable replay
unchanged. A forged high signer timestamp with a bad MAC cannot poison replay.
`Completed` means Commander accepted a current, fully authenticated candidate,
executed one real adapter request turn and retained the actual result, even if
that result is terminal or cannot be projected to W1 status. On
`Completed`, the worker first commits the staged incoming replay state, even if
the application result is `Rejected`, then prepares the returned status. A
stale, foreign, closed or otherwise aborted candidate is `Discarded`; the
worker erases staged replay state without committing it. A result projection
or send failure after an actual turn cannot undo that turn or its replay commit.
Outgoing signing uses a separate checked 48-bit monotonic floor and local
signing clock; a wrap/overflow fails closed. Its timestamp grants no freshness.

## Commander serialization and linearization

The private Commander overlay captures Initial and every actual arm/raw-nav
assignment in order, including ABA changes, plus Sample and Stop. A changed
pair advances source sequence, transition epoch and physical generation exactly
as C1D; a Sample advances only sequence. An unchanged assignment advances only
the checked-time floor. Invalid state, contradiction, timestamp regression,
source counter exhaustion or source lifetime loss closes before a normal
adapter call, preserving the last valid source. Research mode remains
`NotSelected` for every known raw nav, including Offboard. The owner supplies
actual hrt and permanently false required-state, transport and map readiness;
map fields stay zero except matching controller identity. Selector remains
Disabled, no operator is attached, and no admission session/ACK exists.

At each Commander loop boundary, first commit all preceding source assignments.
Then acquire sticky source/owner/transport faults and process stop/closure before
ingress. The linearization point for an ingress turn is the final acquire read
of those faults and the claimed `Ready` token immediately before adapter
mutation. Check current run, owner, controller, source, channel, transport
lifetime, source transition, token and current hrt chronology. Require nonzero
arrival and evaluation times, `arrival <= evaluation` and current source
checked time `<= evaluation`; equal timestamps are legal. Queue delay is
`evaluation - arrival` for evidence, but no unqualified map converts producer
time to request age. A change before this point discards the candidate with no
packet-specific adapter turn. A fault published after this point is observed at
the next boundary; the completed rejection remains historical evidence.
There is no claim of instantaneous remote-failure knowledge.

Reserve the source, transaction and independently held terminal evidence slots,
plus the mailbox result, before any adapter mutation. A private adapter
preflight also reserves its worst-case three normal reports (first physical
barrier plus poll and request operations) and one transition epoch before the
poll; an owner archive slot does not reserve adapter capacity. One transaction reserves
two full `DiagnosticResult` slots: a real `DiagPoll` using current physical
state followed by exactly one `DiagRequest` using the same source generation
and one fresh actual evaluation hrt sampled immediately before the combined
owner turn. Both operations use that same hrt; equality is permitted. A
terminal poll retains a named `PollOnlyFault` transaction, discards staged
replay and prevents the request operation. The request packet
maps W1 source, transport binding, sequence, controller, transition, zero map,
producer times, acceleration and target into `DecodedRequestV1`; the transport
binding is used only for correlation, never as an established session. The
owner-created ingress verdict records actual verified MAVLink 2, signature,
header/channel/source and target checks. Preserve both full results. With the
current Disabled selector the poll can report `ClockMappingInvalid`, while the
request operation must report `Rejected` with `SelectionRequired` (wire reason
4). Do not replace the request's result with a desired clock/transport reason.
Any `Accepted`, `RequestValidatedDisarmed`, eligible demand, selection, grant,
ACK or successful session is a terminal invariant failure, never a transmit
success. No request can reach a controller or actuator publication.

Keep 256 normal source records, 32 two-result transactions and one independent
terminal record in fixed heap-owned storage. These are diagnostic capacity
choices, not timing or throughput claims. Source record 257 or transaction 33
closes with retained `OwnerEvidenceExhausted`/wire reason 8 before installing
the attempted turn; the first 256/32 records remain intact. Reserve result and
terminal space even when the normal arrays are full. If the actual adapter
returns Terminal, WrongDomain or NotConfigured, retain it as the sole actual
result at that point and close; never make a second closure turn to replace it.
If it is the request result, the disposition remains `Completed` and replay
commits, while wire serialization fails closed. If the poll is terminal, the
transaction is `PollOnlyFault` and replay is discarded. Both paths mark native
evidence incomplete and cannot report a passed run.
Compiler size checks and native stack analysis must validate these bounds
before a build or run is released.

## Result, status and shutdown

The owner assigns a checked, strictly increasing status sequence only after
retaining an actual correlated request or closure result. A later closure does
not create a second response for an already completed request. Project a
request status from its operation record, not the preceding poll or a
synthesized prerequisite. Copy actual native run, owner, controller, source,
transport, request sequence,
transition, source sequence, state generation, arrival/evaluation/source-checked
times and zero map. Origin is compiled `NativePosixSitl`; raw arm/nav come from
the same retained source snapshot. Selector, estimator/map validity and
eligibility are zero. `Rejected` reasons 1-5 may be emitted only when the
corresponding actual operation cause and evidence support them; current normal
N1 ingress supports reason 4. `Closed` reasons 6-8 require the matching first
source loss, normal owner stop or resource closure. Unknown or unrepresentable
results fail serialization and make the run incomplete. A signed status is
allowed only after an initialized source and a known authenticated request.
Pre-authentication errors and unsolicited startup closure remain local evidence.

Normal Stop, source loss and local transport fault retain different first
causes. Once Commander sees a sticky fault or stop, it closes the owner and
marks the profile terminal before later ingress. A `Ready` candidate may
receive a correlated `Closed` disposition only for an actual source loss, owner
stop or resource closure, with known request identity and initialized source;
its replay is discarded because no request turn completed. A transport or
configuration fault has no representable W1 `Closed` reason, so it remains
native evidence without a fabricated wire status. If a loss arrives
while the worker is `Preparing`, the worker observes terminal phase before
publishing or after publication, erases staged state and never waits for a
Commander response. If a completed request precedes loss, its result and replay
commit stand; the later closure cannot relabel it. Transport loss may prevent
status delivery, and the archive records that failure rather than claiming a
response. Neither local endpoint loss nor normal stop is operator loss.
The socket is nonblocking. The worker makes one send attempt, with at most one
bounded retry for `EINTR`; `EAGAIN`, short send and other errors are retained as
send failure. It never waits for peer acknowledgment or a writable socket.

The owner archive retains source records, both transaction results, candidate
identity/token, final disposition, status projection, first closure, lifecycle,
counts and checked time floors. A separate worker ledger retains parser stage,
bounded pre-authentication rejects, busy drops, replay disposition, signing and
send state without inventing adapter results or mutating the owner. Export joins
both after Commander completion and worker retirement, via acquired
completion/retirement publication, with no
source-path I/O. The C1E JSON schema is distinct from C1D and Flight; use exact
fields, canonical unsigned decimal strings, duplicate-key rejection, full
result replay and independent projection checks. Repeated post-stop export is
read-only. Sink failure or missing status makes evidence incomplete; neither
can block closure. Key bytes and full signed frames are excluded from export.

Before the one native test request, a control command may print one read-only
checkpoint of the source thread's committed disarmed state: transition epoch,
raw arm/nav, source sequence and checked native hrt. The source thread publishes
these fields as one consistent atomic snapshot after an actual source turn;
the control command neither calls the owner nor changes admission state. Wait
for a source sequence of at least three, bind the signed request's expected
epoch to that observed value, and check the marker against the retained source
record and request chronology. A later source transition causes the ordinary
stale-candidate disposition; the marker grants no session or control authority.

## Release gate

The [N01-N12 matrix](C1E_NATIVE_INGRESS_TEST_SPEC.md) must exercise every slot
state and loss ordering, exact replay commit/discard boundaries, result/status
projection, overflow, build isolation and two fresh disarmed native runs.
Review the exact code/API, generated signing implementation, endpoint lifetime,
source hook completeness, evidence schema, resource size and compiled stack
before enabling the private runtime profile. W1/W2 conformance alone does not
verify any of these native properties.
