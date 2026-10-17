# C1B-I: finite offline acceptance matrix

2026-09-20. Companion contract: C1B_OFFLINE_API_CONTRACT.md. At the C1B-I design
gate these cases were specified, not executed. The subsequent C1B-G guard
slice now executes the 1,024 predicate product, R01-R13/R16 and applicable
private-guard isolation/recovery checks; see C1B_GUARD_IMPLEMENTATION_VERIFICATION.md
for exact coverage and exclusions. The subsequent C1B-A adapter/barrier tests
are recorded in C1B_ADAPTER_IMPLEMENTATION_VERIFICATION.md; bounded owner/broker
execution is in C1B_OWNER_IMPLEMENTATION_VERIFICATION.md (1,124 checks at that gate).
Subsequent local JSON evidence and additive coverage execution are recorded in
C1B_EVIDENCE_IMPLEMENTATION_VERIFICATION.md and C1B_OFFLINE_EXECUTION_LEDGER.md.
This specification adds no PX4,
Gazebo or flight evidence.
Fixtures are synthetic software values, not recommended command timing,
acceleration limits, physical properties or operational requirements.

## 1. Harness and independent oracle

Build the unchanged ordinary library/test suite and a separately opted-in
diagnostic library/test suite. Never link both definitions into one executable.
Exercise the real single guard/adapter and real owner/broker, not mocks standing
in for positive admission. A dedicated offline test peer may inject otherwise
unreachable invalid enums and near-exhaustion counters; label those records
test_peer_injection. Such injection cannot be available through the facade.

Record the contract's local JSON evidence. An independent checker replays the
explicit state machine, counter arithmetic and request arithmetic from inputs;
it must not import production predicate helpers or trust summary pass flags.
Check every report and the operation index, not only the final snapshot.
Require eligible=false, no active demand and unchanged Flight accepted history
after every operation, including failures and terminal paths. Getter sentinels
must remain byte-identical. All 64-bit integer evidence uses decimal strings.

## 2. Guard predicate product: 1,024 cases

Case ID is T-arm-mode-selector-bits-policy, with dimensions:

- arm: Disarmed, Armed, Unknown, invalid underlying value 255 (four).
- mode: NotSelected, Selected, Unknown, invalid 255 (four).
- selector: Disabled, SelectedForValidation, Faulted, invalid 255 (four).
- bits: each combination of clock_mapping_valid, transport_connected and
  required_state_valid (eight).
- policy: ValidateOnly or Reject (two).

Run each case with otherwise valid construction, a previously established
session, no latch, valid monotonic clock and a valid canonical request. Inject
the selector/context directly through the private offline guard peer to test
predicate precedence independently of adapter closure. Reject policy means
diagnostic construction is NotConfigured; no request predicate runs.
Reject-policy cases therefore skip the session-establishment setup entirely.

For ValidateOnly, independently select the first matching expected outcome:

| Priority | Condition | Expected primary cause/result |
| --- | --- | --- |
| 1 | Clock false | ClockMappingInvalid |
| 2 | Connection false | Disconnected |
| 3 | Required state false | RequiredStateInvalid |
| 4 | Arm unknown or invalid | ArmStateUnknown |
| 5 | Mode unknown or invalid | ModeStateUnknown |
| 6 | Arm is Armed | NotDisarmed |
| 7 | Mode is Selected | FlightModeConflict |
| 8 | Selector invalid | SelectorInvalid |
| 9 | Selector Faulted | SelectorFaulted |
| 10 | Selector Disabled | SelectionRequired |
| 11 | Otherwise | RequestValidatedDisarmed, common ValidatedDisarmed |

Exactly one tuple passes. No tuple can produce Accepted/eligible control.
Adapter-level cases below separately prove that its retained selector cannot
be supplied by a packet, and that closure can prevent guard evaluation entirely.

## 3. Hand-calculated reference trace

Fixture only: wire protocol=65536 (kWireProtocolVersionV1), source=7,
session=100, controller=11, owner=13,
run=17, operator=19; valid target/channel bindings. Bounds [-10,10] per axis,
norm limit=10; validity min=20 us/max=200 us, maximum age=50 us,
future skew=10 us, maximum uncertainty=2 us, quaternion tolerance=0.001.
Request acceleration is (1,0,0) FRD inertial at CG. This is numerical test data,
not an aircraft envelope. Contract configuration validation must accept it.
The earlier literal protocol=1 was a transcription from the private canonical
guard fixture; it is not the frozen packed wire-v1 value. No wire version changes.

Generation=1, continuous=true, actual Disarmed/NotSelected, all prerequisite
booleans true. Prospective map: source=7/session=100/controller=11, epoch=1,
offset=+1000 us, uncertainty=2 us, valid raw interval [8000,10000].
All following raw creation and expiry values lie strictly inside that interval.
Validator time=9950 us for steps 1-3, 9965 us for step 4, 10010 us for step 5,
then 10011 us for step 6; changing only validator time is permitted without
a new physical generation. Every packet
uses the current transition and map epochs. Authentication is injected fixture
fact, not measured signing/parser evidence. Supervisor events are 1 then 2.

P=physical barrier, S=selector barrier, B=session barrier, O=operation.
Operation indexes below are zero-based within that turn.

| Step and operation | Report IDs/kinds | Final transition / selector epoch | Operation index/result |
| --- | --- | --- | --- |
| 1: Observe first valid physical sample | 1:P, 2:O | 1 / 1 | 1 / Observed; Disabled |
| 2: Confirm enable, event 1, exact current binding | 3:S, 4:O | 2 / 2 | 1 / SelectionEnabled |
| 3: Independently establish session 100, event 2 | 5:B, 6:O | 3 / 2 | 1 / SessionEstablished; ACK absent |
| 4: ACK current session/transition/map, raw time=8960 | 7:O | 3 / 2 | 0 / Acknowledged |
| 5: Request seq=1, raw creation=9000/expiry=9100 | 8:O | 3 / 2 | 0 / RequestValidatedDisarmed |
| 6: Request seq=2, raw creation=8999/expiry=9099 | 9:O, 10:S | 4 / 3 | 0 / Rejected, ProducerTimeRegressed; Disabled |

Establishment uses operation=1, wire operation sequence 1, zero producer-ACK
time and acknowledged-status fields. ACK uses operation=3, operation sequence 2
and acknowledged_status_sequence=5 (the current session barrier). Its mapped
time=9960, earliest=9958>barrier time 9950 and <=receive time 9965, latest=9962.
Step 5 raw creation 9000>ACK raw 8960 and earliest creation 9998>ACK latest 9962.
These are mandatory ACK-ordering checks, not optional timing annotations.
Both packets use zero flags/reserved fields and matching nonzero targets and
source/session/controller/transition/map identities. No field is a don't-care.

Step 5: mapped creation=10000, mapped expiry=10100; earliest creation=9998,
earliest expiry=10098, latest creation=10002, future limit=10020. Conservative
age=12, duration=100; valid, no demand cached. Sequence floor=1, producer floor=
9000. Step 6 independently calculated age=14 and expiry=10097>10011, so it
reaches producer regression and latches; floors remain 1/9000. The actual
unchanged guard latch Decision reports age_valid=false/age=0; do not fabricate
an age field from the independent arithmetic. Report 9 captures selected/latch state before
report 10 closes it; the final record is NOT the request result. Session gate
after report 10 is EstablishmentRequired; ACK cleared. No session or request
success may be inferred from the closure record. Subsequent clear must be
independently authorized and cannot restore selection or reuse session 100.

## 4. Finite adapter, ownership and recovery sequences

Each row is a required case or explicitly enumerated family. Restore a fresh
fixture per variant; do not let an earlier latch mask the fault under test.
Rows referring to positive setup use steps 1-5 above unless stated otherwise.

| ID | Input/change | Expected independent assertion |
| --- | --- | --- |
| A01 | Zero each construction identity; invalid config; Reject policy | Inert NotConfigured, zero reports, no guard mutation; five variants |
| A02 | Every ordinary adapter method on diagnostic; every diagnostic method on Flight | Domain mismatch, no mutation/output copy; enumerate all public methods from compiled header |
| A03 | Guidance request, ACK, heartbeat or status without operator enable | None selects; no validation success; four variants |
| A04 | Prepare only, then request | Prepare grants no authority; request fails |
| A05 | Mutate one binding field before confirm | Reject stale context/no event consumption; fields: owner, run, controller, operator, generation, transition, selector epoch, each of ten map fields |
| A06 | Duplicate, older, zero, gapped or max-exhausted supervisor event | Duplicate/older reject without consumption; zero/gap/exhaustion follow trusted stream fault/terminal rules; never wrap |
| A07 | Wrong-domain confirm method; unrelated ticket | Matching wrong-domain ticket canceled with no event; unrelated ticket cannot cancel pending action |
| A08 | Confirm after explicit configured ticket age, or regressed owner time | Expired rejection or authority quarantine respectively, no enable |
| A09 | Reuse consumed confirmation ticket | No second action/event; one pending ticket shared across all intents |
| A10 | Disable from selected; repeat disable while disabled | First closes session/ACK, second idempotent with no selector/transition increment; no replay-floor reset |
| A11 | Authorized revoke after step 5; authorized clear after step 6; clear with no latch after step 5 | Revoke: B,O,S; latched clear while already Disabled: B,O and validator-clock/latch reset; no-latch clear: ClearNotLatched rejection without B/S; three variants |
| A12 | Unauthorized revoke; stale clear grant; wrong session revoke; unauthenticated revoke | Rejected admin alone cannot close valid selection; four variants |
| A13 | Disarmed clear while clock/map/state prerequisites absent | Clear may recover latch but stays Disabled; no session/ACK or positive validation; three variants |
| A14 | Establish same previous session after disable/clear | Reject; floors unchanged; distinct established session is the only request/producer-floor reset |
| A15 | Stage prospective new-session map while Disabled, enable, establish same session | No map rebind on establishment; selection retained; new ACK required |
| A16 | Change any map field while selected using next generation | Structurally valid change: physical then selector barrier; inconsistent map/controller/validity: terminal; no automatic recovery; ten variants |
| A17 | Valid next generation with otherwise unchanged state | Physical barrier clears ACK; unchanged prerequisites do not close selector; request requires new ACK |
| A18 | Arm Armed/Unknown/invalid or mode Selected/Unknown/invalid at next generation | Known Armed/Selected and Unknown: Disabled; invalid enum: terminal Faulted; never validation; six variants |
| A19 | Clock (with map.valid=false), connection or required-state false at next generation, then restored | Disable before request/confirm; restoration cannot auto-enable; three variants |
| A20 | Same generation with changed state/map; skipped generation; regressed generation; continuity false; zero generation | Terminal Faulted with no manufactured physical generation; six variants (state/map separately) |
| A21 | Regress validator time at poll/request/confirm/clear boundary | Latch closes selection before requested action; only authorized disarmed clear can recover latch, never selection; four variants |
| A22 | Physical safety prefix followed by stale grant | Completed rejected operation/action=false, not NotSubmitted; prefix evidence retained |
| A23 | Detach or injected operator loss | Synchronous closure before return, session/ACK invalidated, tickets/queued ingress canceled, no supervisor event fabricated; two variants |
| A24 | Detach with no valid retained receipt; unknown close outcome | Terminal/quarantine respectively; never successful detach followed by usable authority |
| A25 | Owner NotSubmitted, Unknown, malformed Completed | No-side-effect guarantee, quarantine, quarantine respectively; three variants |
| A26 | Broker result with wrong operation index, missing barrier, wrong counter delta or false event-consumed flag | Independent owner validator rejects/quarantines; four variants |
| A27 | Access/poll after valid request then arm transition | No request ever copied, no old demand promoted; two variants |
| A28 | Lifecycle close with already Disabled selector | Session/ACK remain invalidated; no invented selector epoch; no readiness prerequisite |

For A05/A16 the ten map fields are valid, source, session, controller_instance_id,
map_epoch, validator_minus_producer_us, uncertainty_us, valid_from_producer_us,
valid_until_producer_us, and the physical clock_mapping_valid flag. The last is
a context field, tested separately from map.valid; both participate in readiness.
Tests distinguish malformed structure from known invalid health; they may not
turn a terminal structural defect into a recoverable healthy observation.
For A16 choose nonzero alternate source/session/epoch, uncertainty 2->3, and
offset/interval changes that retain a valid interval for recoverable rebind
variants. A different controller or an unpaired validity-bit change is terminal.
A13's missing clock/map variants likewise use matching false validity flags;
clear cannot recover a structurally terminal adapter.

## 5. Request and arithmetic boundaries

Use the same common S1 oracle/expectations as existing tests, plus diagnostic
isolation assertions; do not change old expected outcomes to make a test pass.
The private guard peer isolates arithmetic from adapter envelope rejections.

| ID | Explicit variants | Expected primary outcome |
| --- | --- | --- |
| R01 | Wrong protocol/source/session/frame/quantity/reference (six) | Corresponding existing rejection, no floors advanced |
| R02 | NaN, +Inf, -Inf on each axis (nine) | NonFinite |
| R03 | Just below min / at min / at max / just above max per axis (twelve) | Outside axis rejects; endpoints subject to independent norm constraint |
| R04 | Norm just below / equal / above limit (three) | First two valid when axes valid; last NormOutOfBounds |
| R05 | Duration min-1/min/max/max+1 (four) | Reject/pass/pass/reject |
| R06 | U=max / max+1 (two) | Boundary passes otherwise valid; ClockUncertaintyExceeded beyond |
| R07 | Duplicate / lower / strictly greater sequence (three) | DuplicateSequence / OutOfOrderSequence / reach later checks |
| R08 | Raw or mapped expiry below creation; unequal durations (three) | TimeArithmeticInvalid |
| R09 | Earliest creation underflow, earliest expiry underflow, latest creation overflow, now+skew overflow (four) | TimeArithmeticInvalid, no undefined arithmetic |
| R10 | Earliest creation=now and now+1; latest creation=future limit and limit+1 (four) | Boundary passes otherwise valid; beyond rejects ProducerTimeInFuture |
| R11 | Conservative age=max and max+1 (two) | Boundary passes; RequestTooOld beyond |
| R12 | Earliest expiry=now-1/now/now+1 (three) | RequestExpired / RequestExpired / passes otherwise valid |
| R13 | Raw producer time=floor-1/floor/floor+1 with higher seq and valid age (three) | ProducerTimeRegressed+latch / ProducerTimeNotIncreasing / valid |
| R14 | Missing session / invalidated session / missing ACK / stale ACK (four) | Existing respective session/ingress rejection; no guard request success |
| R15 | Wrong ingress owner/run; false each of four existing authentication facts (six) | Reject before request evaluation, no replay refresh |
| R16 | Two simultaneous faults: stale sequence+expired; wrong frame+NaN; excessive uncertainty+duplicate sequence; old+producer regression (four) | First matching existing ordered check wins; no invented latch from an unreached predicate |

## 6. Capacity, evidence and build-isolation cases

| ID | Required variants | Expected result |
| --- | --- | --- |
| C01 | Each of the four contract report shapes at zero/exactly sufficient/one short normal capacity | Twelve cases; no mutation before reservation, exact record order/index; exhaustion safely terminalizes |
| C02 | Transition increments needed 1/2/3 at exact capacity and one short | Six cases; no wrap or partial admin success |
| C03 | Selector epoch MAX-1 then MAX; change vs idempotent disable | Checked final increment; change beyond capacity terminal; idempotent no increment |
| C04 | Report sentinel emitted once, then repeated terminal calls | No repeat terminal report; retained terminal state, no ingress |
| C05 | Fault before any valid state sample | Epoch zero/state_reference_valid=false represented honestly |
| C06 | Admin physical+preclose+session+operation; physical+session+operation+postclose | Exact four-record bound, operation index 3 versus 2; no two selector transitions |
| C07 | Rejected evaluate newly latches | Actual rejection record precedes close; operation index not final index |
| C08 | Blocked or missing evidence sink | State closure independent of sink; campaign incomplete, never an inferred pass |
| E01 | Remove/change domain; set eligible true; advance Flight history; change actual mode | Five one-field corruptions all rejected |
| E02 | Remove/reorder/duplicate a report; truncate input/output; substitute final snapshot for earlier report | Six corruptions rejected (input/output separately) |
| E03 | Wrong index; wrong barrier delta; replay identity; stale ACK; fabricate request success from poll/admin | Six corruptions rejected (poll and admin separate) |
| E04 | Unknown version/key/enum; duplicate JSON key; float for integer; integer out of range; false measured provenance | Seven corruptions rejected |
| B01 | Ordinary build; opted-in host build | Ordinary contains no diagnostic public types; host target remains isolated and output-disabled |
| B02 | Opt-in with PX4/NuttX/hardware/lockstep/proxy target definitions | Five intentional configure/compile failures; no production target enabled |
| B03 | Diagnostic result passed to frozen v1 mapper | Compile rejection; new internal mismatch enum maps false without modifying destination |
| B04 | Complete ordinary regression suite | Existing assertions/expected wire bytes unchanged, all pass |

C06 uses direct adapter-only synthetic grants bound to the state AFTER the
prefix, not a stale console ticket. For P,S,B,O: next physical generation plus
new validator regression from selected state, then an authorized disarmed clear
whose expected transition/selector epochs include both prefix barriers. For
P,B,O,S: next generation with otherwise identical valid state, then authorized
revoke bound to that post-physical-barrier transition. An actual owner/broker
confirmation retaining the old view must instead reject (A22). This fixture
does not permit a deployed console to predict or overwrite physical state.

Before implementation acceptance run the ordinary and diagnostic suites in
fresh Debug, repeated Release and ASan/UBSan configurations, with source,
compiler/configuration and executable hashes. Reconcile every specified variant
against named executed cases; report failures/skips, do not count specification
rows as executed assertions. Independent review checks code and oracle separately.
Passing this suite can release only an offline diagnostic implementation.
Qualified actual PX4 state/clock/operator-loss inputs, live diagnostic namespace,
estimator freshness, no-output integration and the Phase 2 exit audit remain
required before any live promotion. Geometry is not required for these tests.
