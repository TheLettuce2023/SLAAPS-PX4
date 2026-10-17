# P2T2: offline status delivery and diagnostic verification

Status: **P2T2 bounded gate PASS**: master/independent source review and strict
Debug, repeated Release and ASan/UBSan verification accepted (2026-09-19).

This bounded gate uses deterministic virtual time and calls the accepted
`WireAdapter`, `RequestGuard` and status encoder without changing them. It is
software-only evidence under the user-approved inactive profile. Authentication
verdicts, state, clocks and identities are injected scaffolding. The existing
synthetic +/-10 m/s^2 axis and 12 m/s^2 norm bounds remain test scaffolding, not
vehicle limits. No live I/O, key, controller, output, firmware, simulator or
geometry implementation is introduced.

## Predeclared design

The fixture has independent adapter and status-delivery state. A fixed queue
holds at most two immutable encoded reports. Enqueue never waits: a full queue
explicitly drops the new status with reason `backpressure`. Delivery service
can return a selected queued status or record an injected loss; it cannot call
or modify the adapter or diagnostic admission timer. Diagnostic snapshots use
the real current accessor report with broadcast recipient `(0,0)` and occur
at most once per 1,000,000 virtual microseconds. Local accessor evidence is
recorded on every access but is not transmitted diagnostic traffic.

Every scheduled request generates one real correlated status. The fixture
separately tracks the timestamp of the most recently admitted eligible request.
`no_fresh` becomes true at an inclusive 40,000 us since that admission. A
rejected duplicate, heartbeat, status emission, queue operation or return never
resets this timestamp. This Boolean is fixture diagnostic evidence, not a new
wire field or an adapter control input.

Events at the same timestamp are serialized as request/queue disposition,
access/diagnostic evaluation, heartbeat, snapshot/queue disposition, and finally
delivery. Thus a valid admission exactly at the diagnostic threshold resets
the timer before its evaluation. No asynchronous scheduler or wall clock is
used.

## Fixed schedules

All times are injected integer microseconds in the validator domain. Both
paired scenarios start at 1,216,000 us and use the existing setup fixture:
controller 11, source 21, session 31, clock map 41, offset 1,000,000 us,
uncertainty 4,000 us, transition epoch 3 and acknowledged barrier report 4.
The first request report sequence is 7. The approved 60,000 us hard age and
80,000 us raw validity are unchanged.

| Scenario | Schedule relative to start | Required result |
| --- | --- | --- |
| `baseline`, `status_faults` | Request sequence 1/raw creation 200,000 at 0; sequence 2/raw creation 240,000 at 40,000; duplicate sequence 2 at 60,000 | First two admitted; duplicate rejected and timer remains at the 40,000 admission. The 40 ms request gap is intentionally nonnominal. |
| Both paired scenarios | Access every 1,000 us from 0 through 2,007,000, plus 80,001 (2,009 accesses each) | Admission at 40,000 precedes diagnostic evaluation. At 80,000 the request remains eligible and diagnostic is true; at 80,001 eligibility is revoked. Observation continues through the final delayed return. |
| Both paired scenarios | Heartbeat and diagnostic snapshot at 0, 1,000,000 and 2,000,000 | Heartbeat does not refresh payload; snapshot spacing is exactly 1 second. |
| `baseline` | Enqueue all generated request/snapshot statuses; return FIFO after each timestamp's snapshot step | Every generated status returns, with queue depth never above 2. |
| `status_faults` | Initial request/snapshot fill the queue; request statuses at 40,000 and 60,000 drop on backpressure; at 60,000 return newest, at 65,000 oldest | Explicit loss and reordering; a historical sequence-1 status returns while sequence 2 is current. |
| `status_faults` | At 1,000,000 enqueue snapshot then inject loss; at 2,000,000 enqueue snapshot and return at 2,007,000 | Missing receipt remains absent; final delayed return carries unchanged historical payload. |
| `late_admission` | Start 1,256,000; admit sequence 1/raw creation 200,000 at offset 0; access every 1,000 through 40,000 plus 1; heartbeat/snapshot at 0; immediate FIFO returns | Admission age is exactly 60,000; eligibility ends at +1 us, but diagnostic remains false until +40,000. No age limit is weakened. |

The paired scenarios must have identical request/access payloads, canonical
request values, timer values and eligibility at every matching time. Delivery
faults must change only delivery records. Final queues must be empty.

## JSONL evidence schema

Every event has exact common fields `case`, `event`, `t_us`, and
`provenance` with value `injected-us-v1`. All times have injected virtual-clock
provenance; none are measured host/Pi/link times.
Event-specific fields are:

| Event | Additional fields |
| --- | --- |
| `request` | `sequence`, `created_us`, `sent_us`, `received_us`, `status_id`, `payload` |
| `access` | `eligible`, `sequence` (zero when absent), `last_admitted_us`, `no_fresh`, `payload` |
| `heartbeat` | none |
| `snapshot` | `status_id`, `payload` |
| `enqueue` | `status_id`, `depth` |
| `drop` | `status_id`, `depth`, `reason` (`backpressure` or `injected_loss`), `returned_us:null` |
| `return` | `status_id`, `depth`, `returned_us` (equal to event time), `payload` (actual queued bytes) |
| `summary` | `requests`, `accesses`, `heartbeats`, `snapshots`, `enqueued`, `dropped`, `returned`, `pending` (zero); event time is the final scenario time |

`payload` is canonical lowercase hexadecimal of the 106 bytes encoded from the
actual adapter report. `status_id` is that report's status sequence. A generated
request/snapshot event establishes immutable bytes and generation time;
queue/delivery records refer to that status. Return records include the actual
queued payload, which the independent checker must match byte-for-byte against
its immutable generated status. Every status has one queue
disposition and exactly one return/drop outcome. Access payloads are local
directed snapshots, not generated delivery events. Sent time is producer raw
creation; received time is admission minus 4,000 us. The duplicate request is
an injected delayed duplicate of the original sequence-2 packet: its original
sent/creation time remains 240,000 us, not a newly generated producer packet.
Setup reports are outside
the scored stream and remain explicitly inherited from the accepted fixture.

A terminal summary reports executed counters, not constants substituted
for exercised events. The independent Python checker must reconstruct the
fixed schedule, complete status dictionaries, diagnostic arithmetic and queue
history; reject unexplained missing/extra/reordered events, duplicate JSON
keys, fabricated receipts, alternate valid-but-incorrect status bytes,
noncanonical field types and prohibited output events; and run deliberate
evidence mutations to demonstrate those failures.

Expected executed counters are:

| Case | Requests | Accesses | Heartbeats | Snapshots | Enqueued | Dropped | Returned | Pending |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `baseline` | 3 | 2009 | 3 | 3 | 6 | 0 | 6 | 0 |
| `status_faults` | 3 | 2009 | 3 | 3 | 4 | 3 | 3 | 0 |
| `late_admission` | 1 | 42 | 1 | 1 | 2 | 0 | 2 | 0 |

The queue capacity, drop-newest rule and injected loss/reordering are test
policies only; they do not select a deployed status-retention or retry policy.

## Verification and limits

Acceptance includes strict Debug, optimized and ASan/UBSan standalone host
builds, existing regressions, deterministic traces, formatting and independent
evidence validation. No operational rate, baud,
status retry protocol, live ingress or physical-control response is selected.
This gate verifies synthetic software behavior only. Residual-attitude policy
and full Phase 2 operational control/link evidence remain separate work.

All 17 CTest targets pass in Debug, Release repeated three times, and
ASan/UBSan, with no sanitizer diagnostics. Each configuration independently
verifies 4,110 ordered events and rejects 36 corrupted packages/JSON cases.
All returned status bytes match their immutable generation history, including
delayed/reordered returns. Existing Python codec 8 and evidence-checker 17
tests also pass. AStyle dry-run is unchanged and `git diff --check` passes.

Two defects in the initial test implementation/evidence were corrected before
acceptance: return events initially exposed IDs but not actual queued bytes;
the final schema/oracle now checks those bytes and a validly encoded corruption
case. Strict optimized compilation then diagnosed a possible array overrun in
the queue shift; explicit depth invariants and a capacity-bounded loop fixed
it. Compiler warnings and the oracle were not relaxed. Independent review
rechecked both corrections. No accepted guard, codec or flight source changed.

Reproduction uses the standalone project and three build configurations in
PHASE_2_PROFILE_VERIFICATION.md. Add:

```sh
python3 -B Tools/research_sitl/profile/check_status_delivery.py \
  "$task_build/debug/research_sitl_status_delivery_test"
```

Retained build root: `/var/tmp/research-p2remaining.1yoKVCep` with `debug`,
`release`, `sanitize`; each contains flags, executable and
`Testing/Temporary/LastTest.log`. Toolchain: GCC 13.3.0/Python 3.12.3 in existing
Ubuntu-24.04 WSL. These are host-only tests, not firmware/simulator builds.
Trace SHA-256 in all three configurations:
`cf03f4dea9cc293e01cd1e705c5dad4826ba655e830975708a0a4607da864677`.

Source SHA-256:

- StatusDeliveryTest.cpp: `415c8730c1d685b40c7bbf476723a93f1a36633952b8f815318e785fe22931d0`.
- check_status_delivery.py: `4c4b015fac073964e8e38dab308993d1354f39701006e7716f7d52e27da1e29d`.
- Standalone CMakeLists.txt: `5caa980281098605a2b011d87191142d3f18134415dce018041efdbe680fabee`.

CTest log SHA-256, Debug/Release/sanitizer respectively:

- `07fb59a88416944be6036b4de6be6fd47163f0612f9e8f16ac13b50956bdc603`.
- `949ccff02fac45cf39ab0367794b68e2fdc4396196ce58c197c037a62430664f`.
- `1cc19c39f4dbd0af903dd4c7dbecc408a58d9656b54423f0edf94ce22743d919`.
