# C0b.2 State-Core Transition Test Specification

Status: design-only, non-operational offline oracle accepted for the bounded
C0b.2 state-core implementation assignment by the 2026-09-18 master gate.
No executable tests are implemented or run by this document. It does not release packet/session/ACK administration, MAVLink,
uORB, status transport, a controller, allocation, actuator output, SITL,
hardware, or a flight claim.

## Accepted state-core design boundary

The only accepted implementation surface is a private one-guard state core with
three public operations:

- observeTrustedState(sample);
- poll(sample);
- getEligibleRequest(sample, request).

It retains one private S1 RequestGuard and the immutable configuration used to
construct it. The state core remains admission_closed=true: it never
establishes/evaluates a request, accepts an ACK, or opens access. A getter
leaves its request output unchanged and returns an AdapterResult with
admission_closed=true while gates are closed.

Every structurally valid, configured, nonterminal public call with sufficient
counter capacity performs exactly one S1 poll and emits one operation report. The first observation or a valid
contiguous generation transition additionally emits its preceding barrier
report, for report_count=2. The internal trusted-observation helper is not a
public observe call and must not produce an extra poll/report. A future open
access design must use S1 getEligibleRequest, whose internal poll replaces
this explicit poll; that behavior is not part of this state-core gate.

AdapterResult has fixed reports[2], report_count from 0 through 2, and a
snapshot. There are no sinks, callbacks, queues, or transport dependencies.
The snapshot exposes initialized, terminal, observed generation, transition
epoch, barrier report sequence/time, and last report sequence.

## Fixture convention

All following numbers are non-operational fixture labels, not vehicle,
timing, uncertainty, or control limits. Use a matching nonzero controller
identity, a valid map with nonzero source/session/map epoch and uncertainty,
ordered interval, observer_continuous=true, and a current context/map-validity
agreement. Use one validator-time oracle that S1 can observe.

The test-only friend WireAdapterStateTestPeer may seed counters in the test
translation unit only. There is no runtime setter. Ordinary report sequences
are 1 through UINT64_MAX-1. UINT64_MAX is reserved for exactly one terminal
CounterExhausted report. Every normal report sequence is unique and increases.

## Structural and reporting precedence

Precedence is:

1. structural trusted-observer fault;
2. report-capacity/counter exhaustion;
3. valid generation transition and its required barrier;
4. exactly one S1 poll/operation report.

Invalid construction/configuration returns NotConfigured with report_count=0
and performs no runtime guard action. A structural observer fault revokes and
terminals the wrapper, emits one Terminal report at the next available normal
sequence, and does not poll S1 (revocation is the allowed S1 action). Repeating the terminal operation returns the
same terminal code/snapshot with report_count=0 and no guard action.

## STATE fixtures

### STATE-001 - invalid construction is inert

Create each malformed immutable configuration fixture, including zero controller
identity and protocol mismatch. Call all three public operations. Expected:
NotConfigured, report_count=0, uninitialized/nonterminal snapshot as specified
by construction, no S1 poll/revoke, and getter output byte-for-byte unchanged.
Construction is non-copyable and non-movable; compile-only assertions enforce
that ownership cannot duplicate or transfer the private guard.
After construction, mutate the original caller-held configuration object. The
subsequent public-call behavior must still use the copied construction-time
configuration, proving the private guard's immutable member was not aliased.

### STATE-002 - first observation establishes a closed barrier

Start uninitialized with valid sample generation G=51 and validator time 1000.
Call observeTrustedState.

Expected reports, in order:

1. barrier report sequence 1, transition epoch 1, barrier time 1000;
2. operation report sequence 2 containing exactly one S1 poll decision.

The snapshot is initialized, nonterminal, generation 51, epoch 1, barrier
sequence/time 1/1000, last sequence 2, and admission_closed=true. S1 has no
eligible request. No packet/session/ACK floor exists or is implied.
All unused report entries are zero-initialized.

### STATE-003 - unchanged observation is auditable

Reuse the identical valid sample at generation 51. Call observeTrustedState.

Expected: report_count=1 with operation sequence 3; no barrier; epoch and
barrier snapshot values unchanged; exactly one S1 poll. This report is
intentional auditing, not a no-op elision.

### STATE-004 - contiguous generation advances once

Provide valid sample G=52 with the same visible state/map and validator time
1100. Call poll.

Expected reports:

1. barrier sequence 4 at time 1100 with transition epoch 2;
2. operation sequence 5 with exactly one S1 poll.

The state core revokes before the operation poll, remains admission closed, and
does not infer a request/session/ACK transition from this generation change.

### STATE-005 - controller mismatch is terminal at any generation

From STATE-004, supply G=53 with a map controller binding different from the
immutable adapter identity. Expected: structural terminal report sequence 6,
epoch remains 2, guard eligibility revoked, report_count=1, and no S1 poll. Repeat the
same call: same terminal snapshot/code, report_count=0, no guard action.

Repeat with controller mismatch at unchanged G and at a skipped generation:
both are structural terminal faults, never normal barriers.

### STATE-006 - generation continuity and same-generation consistency

From a fresh valid instance:

- G to G+1, even when visible values are unchanged, is a valid transition:
  barrier then operation, one S1 poll;
- G to G+2 is AdapterCode::TrustedStateFault terminal, with no S1 poll;
- G=0, G decreasing, G=UINT64_MAX followed by G=0, or
  observer_continuous=false is AdapterCode::TrustedStateFault terminal;
- same G with changed context or complete map snapshot is terminal observer
  fault, not a valid barrier;
- an invalid enum representation is TrustedStateFault with no S1 poll, while
  named ArmState::Unknown or ResearchModeState::Unknown is structurally valid:
  one S1 poll/report returns GuardRejected with the matching S1 unknown-state
  decision and remains wrapper-nonterminal.

For all terminal branches, eligibility is revoked and a repeated call produces
zero reports/actions.

### STATE-007 - valid map rebind is an ordinary barrier

At contiguous G+1, rebind map source/session from A to B while map remains
structurally valid and controller identity still matches. Expected: ordinary
barrier plus operation report, one S1 poll, new epoch, closed admission, and
no terminal state. This is a recoverable state workflow only: revoke A, map B,
then a later separately reviewed establishment of B. It does not establish B
in this gate.

### STATE-008 - map/context valid-state combinations

Map valid=false and context clock-mapping-valid=false is structurally allowed
when controller identity matches. Establish that pair in the retained sample
through a contiguous generation transition, then repeat the same G: it emits
one operation report and exactly one S1 poll; test the S1 context rejection
and its rejection_count delta. Changing retained map/context true to this pair
at same G is TrustedStateFault; changing it at G+1 adds barrier then operation.

Conversely, map valid=true with zero source, zero session, zero map epoch,
zero uncertainty, or reversed interval is structural terminal. The state core
does not infer an operational uncertainty threshold. Context/map-validity
disagreement is structural terminal.

### STATE-009 - getter is closed and preserves caller output

Seed a recognizable CanonicalRequest output sentinel. For each configured
nonterminal valid sample, call getEligibleRequest.

Expected: an AdapterResult with admission_closed=true and sentinel unchanged;
report_count=1 unless that call is the first observation or observes
a valid contiguous generation, when it is 2 with barrier then operation;
exactly one direct S1 poll; admission remains closed. This does not call S1
getEligibleRequest in the state-core slice.

### STATE-010 - validator time is checked independently of generation

With unchanged G and otherwise identical trusted sample, regress validator
time from 1000 to 999. Expected: one operation report with
AdapterCode::GuardRejected, S1ValidatorTimeRegressed, and the matching latched
first cause. Wrapper terminal remains false. S1 retains the pre-fault
high-water: a repeated time below 1000 still reports
S1ValidatorTimeRegressed, while a subsequent time at or above 1000 performs
its one S1 poll and reports S1Latched. There is no reset. Generation is not a
substitute for validator-time checking.

### STATE-011 - normal counter capacity

Using the test-only friend, seed last_report_sequence=UINT64_MAX-2 (so the
next ordinary sequence is UINT64_MAX-1). An unchanged nonterminal observe
needs one ordinary report and therefore emits UINT64_MAX-1 successfully. The
next unchanged observe cannot allocate an ordinary report: revoke/close, emit
UINT64_MAX once with
CounterExhausted, and no partial operation poll/report.

Seed last_report_sequence=UINT64_MAX-2 before a valid generation
transition. It needs two ordinary reports, so capacity fails before barrier or
S1 poll; emit only the terminal UINT64_MAX CounterExhausted report. This
proves pre-reservation rather than partial transition emission.

### STATE-012 - epoch maximum cannot wrap

Seed transition epoch UINT64_MAX-1 with report capacity available. A valid
contiguous transition reaches epoch UINT64_MAX normally. An unchanged observation
at that epoch still emits one operation if report capacity remains. The next transition
revokes/closes and terminals as CounterExhausted without wrapping, reuse, or a
fabricated barrier. Repeating an unchanged call returns the same terminal
snapshot with zero reports/actions.

### STATE-013 - structural fault precedes capacity

Seed last_report_sequence=UINT64_MAX-2 and submit a structural controller
mismatch. Expected: AdapterCode::TrustedStateFault terminal using normal
sequence UINT64_MAX-1; it is not relabelled CounterExhausted. Separately seed
last_report_sequence=UINT64_MAX-1: no normal sequence remains, so structural
fault uses the reserved UINT64_MAX CounterExhausted fallback once. The snapshot
is terminal and later calls allocate no report. This proves structural-fault
precedence and its capacity fallback.

## Deferred packet and supervisor oracle

This appendix preserves the stable future case IDs and golden fixture from the
earlier packet/administration review. It is explicitly **deferred**: none of
these cases is required by, observable from, or implementable in the bounded
state-core surface above. In particular, they do not authorize a state-core
test to claim packet admission, trusted-session establishment, administrative
sequence behavior, ACK ordering, or request eligibility.

All numeric values below are non-operational fixtures only. They are not
vehicle timing, uncertainty, authority, or control limits.

### Deferred stable cases

- **C0B2-SEQ-001 -- establishment is one barrier.** A later authenticated
  establishment paired with the independently trusted matching grant must
  reserve exactly one wrapper-owned administrative barrier/epoch. It does not
  require an exogenous generation update.
- **C0B2-SEQ-002 -- golden pre-ACK replay ordering.** The concrete fixture is
  retained below.
- **C0B2-SEQ-003 -- strict ACK/request ordering.** A request created before
  the required ACK is never admitted merely because it is delivered after the
  ACK; a request whose creation satisfies the floor may be considered only
  after all later validation.
- **C0B2-SEQ-004 -- frozen ACK.** A later or duplicate ACK for the same
  barrier is rejected without moving the accepted ACK or administrative floor.
  The accepted ACK must not be reused across a later barrier.
- **C0B2-TRN-001 -- malformed or foreign packet with trusted loss.** A trusted
  loss fault has priority over a simultaneously presented packet-format or
  foreign-binding reason.
- **C0B2-TRN-002 -- bad authentication without trusted loss.** An
  authentication failure is independently rejected and does not impersonate a
  trusted-link-loss observation.
- **C0B2-TRN-003 -- hidden loss/recovery generations.** A skipped trusted
  generation is terminal; a later ordinary grant does not recover a terminal
  wrapper.
- **C0B2-TRN-004 -- full map mutation.** A changed trusted map at a contiguous
  generation is a barrier; a map/controller mismatch is terminal, including
  when it accompanies a packet-oriented later case.
- **C0B2-ADM-001 -- supervisor event consumption.** The trusted event stream
  starts at 1. A positive older duplicate is a nonterminal rejection without
  moving the event floor. First presentation of the exact next positive ID
  consumes it even if its requested action is denied. A gap, malformed event,
  or event ID zero is terminal; zero does not advance the floor and no later
  grant recovers that terminal wrapper.
- **C0B2-ADM-002 -- revoke gate.** A later authenticated revoke requires the
  prescribed packet/grant pairing and creates its designated administrative
  barrier; it does not depend on an external administrative generation.
- **C0B2-ADM-003 -- latched clear.** A later clear reaches S1 only while the
  S1 state is latched and the independently trusted conditions permit it; an
  unlatched clear is rejected rather than resetting state.
- **C0B2-ACK-001 -- identity, ordering, and arithmetic.** ACK floors are per
  controller instance, not per session, and must validate controller binding,
  barrier, interval ordering, and checked arithmetic.
- **C0B2-RPL-001 -- S1 replay floor survives faults.** Later replay handling
  preserves S1's duplicate floor; a rejected packet operation does not advance
  a successful operation sequence.
- **C0B2-EXH-001 -- report counter exhaustion.** Later packet status reporting
  uses ordinary IDs 1 through UINT64_MAX-1 and reserves UINT64_MAX for one
  terminal CounterExhausted notice, without reuse.
- **C0B2-EXH-002 -- epoch exhaustion.** A wrapper epoch may reach
  UINT64_MAX; a required subsequent advance terminals rather than wrapping.

### C0B2-SEQ-002 numeric golden sequence

This deliberately reordered transport fixture tests correlation and replay,
not runtime timing suitability:

1. A valid disarmed trusted state G=51 at validator time 1000 installs closed
   epoch 1.
2. A paired authenticated establishment and independently trusted grant E=1
   at 1001 installs epoch 2 and administrative packet floor 1.
3. Trusted armed G=52 at 1100 installs epoch 3 and barrier time 1100.
4. For controller I=11, source S=21, session A=31 and map epoch M=41,
   map raw interval [0, 1000] has offset 1000 and
   uncertainty 2. ACK operation sequence 2 has raw time 105, maps to 1105,
   has earliest/latest [1103, 1107], and is received at 1110.
5. Request sequence 1 has raw creation 110 and expiry 150. It is
   first received at 1109, before the ACK arrival, and is rejected as
   ACK-required. Replaying that identical request at 1115 after the ACK may
   pass the ACK-floor portion with conservative age 7: raw creation 110 is
   strictly after ACK raw time 105, and earliest mapped creation 1108 is
   strictly after ACK latest time 1107. If all remaining S1 checks accept,
   a further identical replay must be rejected by S1 duplicate handling.

The deferred fixture also fixes maximum map uncertainty 3, maximum age 30,
maximum future allowance 5, and request validity interval 10 through 100.
If a later vector-bound case is needed, use per-axis bounds [-10, 10], maximum
acceleration norm 12, and vector [1, 0, 0], all in m/s^2 (not normalized actuator
units). These values are test inputs only and must not be
promoted to operational constraints.

## Pass rule and limitation

Each STATE case passes only when all report order/count/sequence, snapshot,
single-poll, revoke, unchanged-output, and terminal non-mutation assertions
hold. Passing these fixtures is not a completed C0b.2 gate and does not prove
live authentication, operational clock uncertainty, source authorization,
vehicle feasibility, controller behavior, allocation, or EDF/canard command.
