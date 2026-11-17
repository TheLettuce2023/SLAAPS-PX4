# C0b.3 Status Test Specification

Status: reviewed offline oracle implemented and accepted, 2026-09-19; see
PHASE_GATE_STATUS.md for verification and limitations. This is payload evidence only: it
does not add MAVLink framing, signing, a sender, a controller, output, SITL,
hardware, or flight qualification. All numeric identities and times below are
synthetic fixtures, not operational settings.

## Boundary and independent observation

Each test obtains one already-stored historical AdapterReport, AdminReport, or
IngressReport with its immutable ReportEvidence. It calls one pure
buildStatusV1 overload followed by encodeStatusV1Payload. It never passes a
final result, WireAdapter, RequestGuard, map service, or mutable snapshot to
the mapper.

The test must retain a byte copy of the report/evidence and initialized
sentinel WireStatusV1 plus 106-byte output. Every build or encode failure must
leave both sentinel outputs byte-for-byte unchanged. It must also prove the
source report/evidence are unchanged and that no adapter report counter,
epoch, ACK record, S1 cache, request output, or rejection count changes during
mapping. A zero-report result produces no mapper call and no retrospective
status.

Use independent frozen-table little-endian byte observation at offsets
0,8,16,24,32,40,48,56,64,72,80,88,92,94,96,98 through 105. A successful
encoder always writes exactly 106 bytes; byte 105 is a nonzero valid
clock-mapping-state value, so trimming is forbidden.

## Common historical sequence

Use the accepted C0b.2 synthetic identities I=11, S=21, A=31, M=41 and
protocol 0x00010000. Establish at G=51/time 1000, arm at G=52/time 1100,
accept ACK raw 105 at 1110, accept request sequence 1 raw [110,130] at 1115,
access at 1116, and expire it at 1128. The historical records include:

| ID | Record | Class / sequence / epoch | Required evidence facts |
| --- | --- | --- | --- |
| HIST-001 | establish exogenous barrier | 3 / 1 / 1 | ACK closed; old session state; no packet correlation; no fabricated S1 decision. |
| HIST-002 | establish administrative barrier | 3 / 2 / 2 | ACK closed; session established; zero correlation. |
| HIST-003 | establish operation | 2 / 3 / 2 | packet correlation S,A,0; S1 Accepted with its actual establishTrustedSession syntactically-valid field false; authenticated only if captured packet MAVLink2 and parser facts were true. |
| HIST-004 | arm barrier | 3 / 4 / 3 | local decision 70; ACK closed; zero correlation. |
| HIST-005 | ACK operation | 2 / 6 / 3 | packet correlation S,A,1; local decision 1; never eligible. |
| HIST-006 | accepted request | 1 / 7 / 3 | correlation S,A,1; S1 Accepted; ACK current; armed/selected/established/connected/valid map; age valid 7; last accepted 1 authenticated. |
| HIST-007 | access | 5 / 8 / 3 | zero correlation; S1 Accepted; eligible only if the real accessor copied the cached request. |
| HIST-008 | expiry access | 5 / 9 / 3 | zero correlation; S1 RequestExpired; ACK may remain current but eligible is false. |

For HIST-006, use directed synthetic recipient system/component 1/1. Required
status fields are controller 11, transition 3, map epoch 41, source 21,
session 31, correlation 1, last accepted 1, validator time 1115, status
sequence 7, rejection count 1, conservative age 7, protocol 0x00010000,
decision 1, first cause 0, class 1, arm 2, mode 2, session 2, transport 1,
map 1. Flags are 59: eligible, syntactically-valid, age-valid, last-sequence,
authenticated. The rejection count is one because the initial administrative
pre-poll increments it, establish's internal poll restores that count, and the
later healthy polls/evaluate do not increment it.

The independently hand-transcribed little-endian 106-byte vector is:

    0b00000000000000030000000000000029000000000000001500000000000000
    1f00000000000000010000000000000001000000000000005b04000000000000
    0700000000000000010000000000000007000000000000000000010001000000
    3b000101010202020101

The vector must be asserted at the frozen offsets and round-trip identically
through the existing Python status decoder; the C++ mapper must not call that
decoder or encoder.

## Class, recipient, correlation, and state matrix

| ID | Input | Required status result |
| --- | --- | --- |
| CLS-001 | Adapter observation, poll, or access | Class 5 and correlation zero. Directed target IDs both nonzero; operator broadcast is permitted only here with both IDs zero. |
| CLS-002 | Adapter/admin/ingress exogenous or administrative barrier | Class 3, zero correlation, code 70, ACK captured closed. A barrier with ACK current, packet correlation, or absent captured evidence fails without output. |
| CLS-003 | Adapter terminal without packet | Class 4, zero correlation. A packet-triggered admin/ingress terminal remains its class 2/1 and keeps its packet correlation. |
| CLS-004 | Admin packet operation/terminal and ingress ACK | Class 2 with captured source, session, and operation sequence. |
| CLS-005 | Ingress request operation/terminal | Class 1 with captured source, session, and request sequence. |
| CLS-006 | Directed recipient zero/mixed target; broadcast recipient with non-snapshot class; broadcast packet/barrier/fault | InvalidRecipient and no writes. |
| CLS-007 | Trusted ownership unknown on class 3/4/5 | source/session both zero. A rejected packet must not supply ownership to an uncorrelated snapshot. |
| CLS-008 | Valid map, invalid map, state-reference fault | Map state 1 with nonzero M; state 2 with map epoch zero; state 3 for fault. Controller/transition zero is InvalidIdentity, never repaired. |

State enum vectors independently assert arm Disarmed/Armed/Unknown as 1/2/3,
mode NotSelected/Selected/Unknown as 1/2/3, session
NoTrustedSession/Established/EstablishmentRequired/Revoked as 1/2/3/4, and
latched session state 5. Transport is connected/disconnected/unknown 1/2/3.
Unknown or out-of-range captured enum values are InvalidStateCombination.

## Decision and flag oracle

For primary S1 origin, test every DecisionCode in declaration order and demand
the exact wire value:

| S1 range | Required wire values |
| --- | --- |
| Accepted through WrongSession | 1 through 7 |
| UnauthorizedSessionEstablishment through SessionEstablishmentRequired | 8 through 11 |
| DuplicateSequence through ValidatorTimeRegressed | 12 through 19 |
| RequestTooOld through RequiredStateInvalid | 20 through 24 |
| WrongFrame through NormOutOfBounds | 25 through 30 |
| DisarmedRejected through Latched | 31 through 37 |

For local origin, demand the following exhaustive named maps: barrier 70;
adapter TrustedStateFault/CounterExhausted/NotConfigured 79/78/3; admin
Completed 1, SupervisorMissing 8, SupervisorDuplicate 75,
SupervisorStreamFault 79, MalformedPacket 74, UnsupportedOperation 73,
PacketOperationReplay 75; ingress Completed 1, MalformedPacket 74,
UnsupportedSessionOperation 73, WireOperationReplay 75,
AcknowledgementRequired 70, AcknowledgementFrozen 71,
AcknowledgementStatusMismatch 80, AcknowledgementOrdering 71, MapInvalid 15,
MapIntervalInvalid 77, MapArithmeticInvalid 82, WireVersionInvalid 4, and
WireNonFinite 28. For every local aggregate that requires a precise captured
wire cause, include each first-match cause: MAVLink2 64, parser auth 65,
header/source binding 5, target binding 66, controller 68, transition 69, map
epoch 72, map source/session 81, gate revoked/no-session/establishment 76/6/11,
ACK uncertainty/future/age/arithmetic 16/17/20/23, and request frame/quantity/
reference/validity/time 25/26/27/21/23. Missing, zero, unassigned, or
contradictory local capture is UnrepresentableDecision; it must never be
rewritten as malformed, accepted, or an S1 decision.

| ID | Captured facts | Required flags and fields |
| --- | --- | --- |
| FLG-001 | HIST-006 accepted request | Flags 59 and eligibility true, as the complete conjunction is met. |
| FLG-002 | Accepted request with unauthenticated or non-MAVLink2 packet fact | Syntactic may remain set, authenticated and eligible clear. |
| FLG-003 | Accepted request with ACK closed, disarmed, unselected, disconnected, invalid map, unestablished session, or latch | Eligible clear for each independently; no false positive from Accepted alone. |
| FLG-004 | Actual S1 age valid / age absent | Set age bit and exact age / clear bit and force age zero. |
| FLG-005 | Last accepted sequence present/absent | Set bit and history value / clear bit and force value zero. Revoke, expiry, ACK, ValidateOnly, and rejection do not rewrite the retained history. |
| FLG-006 | Producer-time and validator-time latch | LATCHED, session 5, first cause 18 or 19; local or other S1 latch cause is InvalidStateCombination. |
| FLG-007 | Barrier, ACK, administration, terminal, old-cache request rejection | Eligible always clear. A status never claims execution, tracking, allocation, or output. |

## Historical no-rewrite cases

| ID | Sequence | Required assertion |
| --- | --- | --- |
| NR-001 | Map establish's three reports after final result is established | HIST-001 remains old-session/closed; HIST-002 and HIST-003 show established state. Mapping earlier reports must not read the final snapshot. |
| NR-002 | Accepted request then rejected duplicate/too-old request | Last accepted sequence remains 1; rejected report decision is its own primary cause and does not claim cache loss. |
| NR-003 | Accepted request then expiry/access | Expiry report is RequestExpired/noneligible; ACK/history are represented only as that report captured them, not reconstructed afterward. |
| NR-004 | Revoke, re-establish different session, map rebind, latch/clear | Each pre-action barrier retains its pre-action state, then later records show the exact post-action state. No report is retroactively rewritten. |
| NR-005 | Invalid first structural terminal with zero controller or transition epoch | Mapper fails InvalidIdentity, leaves output unchanged, and does not substitute later valid identity. |
| NR-006 | Counter-exhaustion terminal and repeated zero-report terminal calls | The one historical terminal maps status sequence UINT64_MAX/decision 78; no status exists for a later zero-report call. |

## Failure and payload checks

STATUS-001 through STATUS-006 respectively cover InvalidRecipient,
MissingEvidence, InvalidIdentity, InvalidReport, UnrepresentableDecision, and
InvalidStateCombination on each builder overload. Every encoder-invalid typed
value returns InvalidStatus and preserves all 106 output bytes. Test protocol
wrong, status class/state enum wrong, undefined flag bits, mixed targets,
nonzero unsolicited correlation, invalid clock epoch, absent age/history with
nonzero values, latch/cause mismatch, and eligibility conjunction violations.

For each successful status, compare the C++ 106 bytes with an independently
transcribed frozen-table vector and Python codec encode_status/decode_status
as a second implementation check. Test fields at UINT64_MAX individually:
controller/transition/map only where structural rules still permit them,
correlation, history, validator time, report status sequence, rejection count,
and age. Check additions are not performed by the mapper: a zero epoch remains
InvalidIdentity and no status sequence is allocated or incremented.

## Pass condition and limitation

Pass requires every named mapping, flag/class/correlation case, historical
no-rewrite case, 106-byte vector, and unchanged-on-failure assertion. It is
offline software evidence only; it proves neither a live status transport nor
authentication, authority, clock provenance, vehicle control, or flight
behavior.
