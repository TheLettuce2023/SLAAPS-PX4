# C1E-N1 private native diagnostic ingress: implementation and verification

2026-09-24 to 2026-09-25, Codex (GPT-6). Branch `slaaps-sitl`, PX4 HEAD `e880f1cd79`.
Research files are substantially untracked, so HEAD alone is not a complete
source identity. This record covers only the opt-in, negative-only,
disarmed/output-disabled C1E profile; it grants no flight authority.

## Implemented boundary

The C1E profile compiles a separate generated-dialect MAVLink 2 signed parser,
one process-lifetime UDP worker, a single-slot bounded mailbox and a private
Commander source-thread owner. The owner keeps one by-value diagnostic adapter
and retains actual poll and request results, source records, first closure and
worker delivery/replay records. A source-thread atomic checkpoint projects the
observed disarmed epoch and sequence to a control command so each native test
request binds its expected epoch to actual source chronology. It is read-only
and conveys no operator or session authority.

The parser stages authentication before source binding and commits replay only
after a completed owner request turn. Normal native request projection is
`Rejected/SelectionRequired` (wire reason 4); map, estimator, admission
transport, operator, selection, ACK and eligible demand remain unavailable.
The profile chooses only Commander and research diagnostic/inspection commands,
with no output driver or control publisher.

## Offline and regression evidence

- C1E synthetic owner tests: 2/2 CTest targets passed in Debug, Release and
  ASan/UBSan. Retained under `/var/tmp/c1e_owner_tests_{debug,release,sanitized}`.
- C1E independent sender, capture gate and archive corruption tests: 13/13
  passed; `/var/tmp/c1e_offline_tests.log`.
- C1D closed-owner regression on this source snapshot: 23/23 host CTest targets
  and 17/17 independent checker cases passed. Retained under
  `/var/tmp/c1d_regression_20260924_tests` and
  `/var/tmp/c1d_checker_tests.log`.

These fixture results do not substitute for a linked PX4 build or source-bound
native trace. Exact generator and build provenance, stack review, native trace
results and any failed attempts are recorded below after the source-stable gate.

## Build and native evidence

The first source-stable compile-only build at `/var/tmp/c1e_px4_k8g3pwna`
linked 485/485 PX4 build steps, had zero changed source inputs, passed 29/29
native profile probes, and enabled only Commander with zero drivers. Its
independent synchronous source stack bound was 31,296 bytes under the
predeclared 32,768-byte ceiling. The reviewed first disarmed capture at
`/var/tmp/c1e_capture_6cofaih6/run-1` sent one signed request and received
one signed status. Its initial checker failed on the full request snapshot's
`rejection_count`: the independent oracle omitted the guard's pre-poll count
increment during the Disabled-selector request. Source inspection established
the adapter's actual increment; the checker was corrected, and the retained
run then passed the complete read-only `check_run` with 193 source records,
one completed negative transaction, actual `Rejected/SelectionRequired`,
signed status outcome 1/reason 4 with `eligible_for_control=0`, zero worker
rejects, no authority and no qualified guidance clock map. The checker fix
also passed 13/13 offline tests, including a new explicit request snapshot
count assertion. The first capture
was not promoted to a two-run campaign result.
Independent source review confirmed the request pre-poll increments that count
once; the retained request snapshot differs from the preceding poll snapshot
only by that count (6 to 7) and its normal report sequence (8 to 9).

Because that checker belongs to the build's full source inventory, its edit
invalidated the old review for further capture. Preserve the original failed
result and corrected read-only interpretation together.

The corrected-source compile-only build at `/var/tmp/c1e_px4__s2weiwh`
linked 485/485 steps with zero source drift, 29/29 native profile probes,
513 compile units (30 C1E native units), Commander as its only enabled module,
zero enabled drivers, and the exact inspection/diagnostic builtin allowlist.
The independent compiled stack review at
`/var/tmp/c1e_px4__s2weiwh/independent-stack-review.md` rehashed eight
object/stack-use pairs and bounded the deepest synchronous source path at
31,296 bytes, below the 32,768-byte ceiling. The manifest SHA-256 is
`0bc734623fd8886ecd21a09cd143685ce007d99075101609a5bc6fb0e8724ff6`;
the reviewed executable SHA-256 is
`c452ea21b1885b4c209399ff7d43deb8fb4fc8a2160fb2bc90931f7e3b5df1fb`.
The source, generated and stack registries and startup script are bound in
`technical_review.json`; the read-only release gate returned
`REVIEWED_NOT_RUN` before the runtime command. Generated diagnostic wire bytes
matched the earlier build except the generated `MAVLINK_BUILD_DATE` in
`version.h`, which changed from 24 to 25 September.

The fresh campaign `/var/tmp/c1e_capture_nh9fol62` passed the independent
two-run checker (`checked-campaign.json` SHA-256
`e648bafb7a8a42bb9e87cb13276f7d6590e5e21f50771303bf40f26c45c454bd`).
Both PX4 processes exited normally without forced shutdown, used distinct
run/owner/transport/producer/clock identities, ports and signing keys, and
retained matching repeated read-only exports. Each sent exactly one signed,
checkpoint-bound W1 request and received one signed status. The actual
request result was `Rejected/SelectionRequired`; both statuses carried
outcome 1/reason 4 with `eligible_for_control=0`. Owner stop closed each run;
no operator/session authority, eligible request, qualified guidance clock map,
worker reject or busy drop was observed. The checker verified allowed advertised
topics and the linked output-free configuration.

| Fresh run | Source records | Transactions | Adapter reports | Trace SHA-256 | Status bytes SHA-256 |
| --- | ---: | ---: | ---: | --- | --- |
| 1 | 195 | 1 | 200 | `9ae293f4a7013f40fdea17c1ee27e405f3de72d559b1086998a28614f91fcd33` | `aa7c187bc567f562f3e4793ce520296dfc7f5208dca9e7ce4f5f6c9cc6467c11` |
| 2 | 193 | 1 | 198 | `1e2569ba419a7d7e6d1235ede9269906f7518cd1a169eb67c190c1538fdfd036` | `531d24f2ffd80a2db79fd91a4fc6180042c6ec4c6a1a452fdcf93e2de9239b2f` |

A separate literal N02 zero-datagram reviewed PX4 run at
`/var/tmp/c1e_n02_zero_v4tjw2c1` created no sender socket and sent no UDP
datagram. The source-review gate revalidated the same manifest, source
registry and executable. The process exited normally after OwnerStop with
192 source records, 195 adapter reports and zero transactions, worker
arrivals, rejects, deliveries, busy drops, status attempts or sends.
Independent archive checking passed with no authority or qualified map;
repeated exports were byte-identical (SHA-256
`004a098cf317a163910d3528ea7ad3350e853f37cfefcbd16384a0f2b5ae7c56`).
`run.json` SHA-256 is
`66a711f787d887599089d51675646218cb84bf421eef7c2cb24ce0161574e8c4`;
`checked.json` SHA-256 is
`795f74a8f2773012b95f34a5fee8c18b44ed247b7ea2cd7c417c4ea7bd8fa5c5`.

An isolated N12 export-refusal run at `/var/tmp/c1e_n12_sink_q4mfxhy8`
used the same reviewed binary and byte-identical startup, one signed
checkpoint-bound request, and normal OwnerStop. An interactive read-only
`export /dev/full` failed because the path already exists; a subsequent export
to a new regular file succeeded. Both initial exports and the later export
were byte-identical (SHA-256
`1af359737c2eedfe0e8bb94c9689cf7879917fe4f5cc431602ec51642fca57eb`),
the archive checker passed with 192 source records and one negative
transaction, and PX4 exited normally. The retained `probe.json` SHA-256 is
`1179abece2e32921d63b32347530e69fde3d6957fdd1f61f3f28c5f25b43446c`.
This checks refusal of an existing/nonregular sink, not a mid-write ENOSPC
failure or status-send backpressure.

A separate test-only interposer at `/var/tmp/c1e_n12_midwrite_20260925`
allowed the first 4096 bytes of an export to a new `trace-fail.json`, then
returned ENOSPC on the next write. The export command failed, and the partial
file independently failed checking as malformed/truncated JSON
(`check-partial.log` SHA-256
`66ee436e0876ee040c0b7d6676e0283858caf534f81983182a248ccf61b2b500`).
Exports before and after the fault were byte-identical (SHA-256
`a8eea8ae2aa709629eec93ecb81769a58c6ed08bf19b0850aeae81e1cfe30f98`),
the complete archive checked with 199 source records and one negative
transaction, and the reviewed output-disabled process exited normally.
`probe.json` SHA-256 is
`f8227ac7dd9d6149bb4bfe846ed234dc4ce1a16a23a77356d2f140e1a71ea3dd`.
The interposer matched only that exact export path; this demonstrates the
failure path under injected short write and ENOSPC, not real kernel exhaustion.

Four more fresh reviewed output-disabled processes at
`/var/tmp/c1e_n04_n05_fkn1oal4` exercised live N04/N05 traffic. The retained
temporary harness SHA-256 is
`551173aefa5296e22985915318bb9fd15f8e62ab0af9170827b9f885a751bf71`;
`campaign.json` SHA-256 is
`946c581fc1f193eaae29b5bce5d907fdaa1bdad1d45e98459ddc6f1fa640c581`.
Each process exited normally, with no forced shutdown, and independently
checked its source archive as negative-only with no authority or busy drop.

| Run | Live stimulus/result summary | Parser rejects | Owner transactions | Signed statuses |
| --- | --- | ---: | ---: | ---: |
| N04a | Unsigned/MAVLink 1, bad MAC/key/CRC, flags/ID, short/trailing/concatenated/zero/oversize datagrams and wrong peer: all 14 rejected before an owner transaction. | 14 | 0 | 0 |
| N04b | Split, wrong link/header/target/source/session/controller and reserved payload rejected; one authenticated stale epoch discarded without replay commit, then a valid request completed. | 9 | 1 | 1 |
| N05a | Bad-MAC and authenticated wrong-identity high timestamps rejected; authenticated high stale epoch discarded without replay commit; lower and newer valid requests completed, while duplicate/equal/older signing times rejected as Replay. | 6 | 2 | 2 |
| N05b | Old-key request rejected by MAC, old status rejected at inbound message ID, old identity re-signed under fresh key rejected; fresh request completed and its duplicate rejected as Replay. Prior signed status also failed MAC under the fresh receiver key before identity decode. | 4 | 1 | 1 |

All completed requests still returned `Rejected/SelectionRequired` reason 4
and no eligible demand. The worker's channel is fixed by the native profile,
so wrong-channel input remains a parser fixture case; the live campaign does
not prove every possible packet ordering or a real link-loss detector.

A temporary host-only C1E owner/source fixture at
`/var/tmp/c1e_owner_fault_audit_20260925` passed 187 existing and 29 added
checks with zero failures (`run.log` SHA-256
`e109219f3d4f5a37a910e1e878669b77e2a8daf0012c36ad581eab995b9fb617`).
It ordered Ready before transition and stale discard, completed a request
before later transition/loss, closed queued Ready work on source loss with
reason 6, closed invalid raw nav and HRT regression without fabricated
status, and preserved the 256-record source prefix on record 257 closure.
Late owner calls did not reopen. Exact compile command, fixture source and
hashes are retained there. These are synthetic owner facts, not live
Commander or UDP timing observations. A valid source sequence cannot reach
64-bit wrap before the 256-record archive closes.

A separate deterministic host-only worker fixture at
`/var/tmp/c1e_n07_WqF47x` recompiled the unchanged reviewed Worker.cpp with
its exact native compile flags, linked the reviewed parser object, and used
test-only wrappers for native time, receive and send. Its 124-, 150- and
176- and 252-check results are retained; the final extension passed **270
checks** with zero failures (`results-270.log` SHA-256
`f06e16a621107287fdd9563587db6bd74082ab41d343f0d6aa9557c4231d4591`).
The 65th malformed arrival kept the first 64 rejection records, counted 65
and raised sticky `WorkerResourceExhausted`. Busy Ready work preserved the
staged token; stale and terminal Ready discards left replay uncommitted.
Terminal during Preparing discarded its staged token before publication;
terminal during Processing left the owner-held token alone. A later scripted
Resolved/DiscardedClosed was consumed before receive, discarded replay,
released the slot and left a late datagram unread. A simulated nonblocking
`sendto` EAGAIN after a completed request retained replay commit, recorded
one failed status, raised `TransportFault` and released the slot. The fixture
does not prove a real concurrent scheduler race or kernel send saturation.
Near-maximum token injection published/discarded token `UINT64_MAX`, then
rejected the next valid frame as `ResourceFault` without a pending token.
This counter state is unreachable from zero in a normal native lifetime before
bounded delivery/closure stops staging. `provenance-176.json` SHA-256 is
`620cae95be4c37384738424d604087de277a3ca81539b4ed91a4881e867152ac`;
it records exact commands and confirms the worker/parser source bytes match
the reviewed build inventory.
The later extensions also covered terminal before receive, terminal during
receive before staging, a fault at Empty, an actual Ready-to-Processing claim,
terminal waiting on the owner-held slot, Resolved cleanup before another
receive, and busy count at `UINT64_MAX` without wrap. The final 270-check
matrix also covers the race winner where terminal Ready cleanup makes the
later owner claim fail, and the converse where a completed owner resolution
commits replay and sends status before terminal is consumed. Its
`provenance-270.json` SHA-256 is
`abdc86839cdcf2e3f2bdae187bdc6fa3d4707eb4cf2b2660688afc9b6465ffdf`.

An additional test-only owner fixture at
`/var/tmp/c1e_owner_fault_audit_20260925` passed the 187 baseline checks,
29 earlier loss/source checks and **18 arm-specific checks**, with zero
failures (`arm-run.log` SHA-256
`850e899eb0b11f3d5eac9d199a098b84adf6b08e756babcb5ea245cba1722e21`).
A queued epoch-1 candidate became stale after disarmed-to-armed change and
after arm ABA; source epoch/generation and the actual arm transition were
retained. An arm change after a completed negative request did not rewrite its
historical disarmed status. An unobserved raw arm mismatch closed before the
request turn and projected a reason-6 status from the last valid source.
The fixture source SHA-256 is
`6c975d8d6b1a07c52c786237a4dde33f58c4ab47472c58b8116a9fbbe95193fd`.
These are serialized synthetic owner events, not an armed PX4 run.

An independent read-only N06-N08 source/build audit is retained at
`/var/tmp/c1e_n06_n08_api_review_20260925_v2.md` (SHA-256
`b4c0eb7d90c879db399e12e2f7fcd06d34ed6db7ed044420b09bb7af77292fe6`).
The reviewed CLI exposes only bootstrap, checkpoint, wait, stop/export and
export; the parser admits only the signed diagnostic request, and the private
owner exposes no operator/session administration method. Attach, prepare,
confirm, detach and revoke therefore have no callable route into C1E.
Commander raw arm/nav changes remain possible and are covered as source
events. The audit maps the scripted mailbox phases and queued arm/source
checks to the exact N06-N08 cells without claiming simultaneous scheduler
races or an armed PX4 test.

An N11 read-only corruption sweep under `/var/tmp/c1e_n11_sweep_c8xyyriv`
rejected 65/65 mutations: 40 copies of a checked native archive and 25
signed-status wire cases. It covered version/domain, identity/correlation,
selection/session/map/estimator/eligibility, counters/lengths, duplicate and
malformed JSON, CRC/MAC/link, and frozen Flight cross-feed. The source native
trace and status hashes match the passing first campaign run. Status wire
mutations were re-signed with a disposable public fixture key, never the
unlinked native key. The full mutation-to-rejection table is `results.md`
(SHA-256 `32cb4827bb6755c68326ae59c5916f7c1c8f0e8cf2e7c82d6a48e398da6e9413`);
`summary.json` SHA-256 is
`082bdc9aa999075c883fd232fa0adb8bbd2d2b37054d7582de7f491b1fea7025`.
An initial supposed truncation removed only a final newline and remained valid
JSON; that fixture mistake was retained, corrected to remove the closing
brace, and rejected on rerun. No checker weakness was inferred from the
newline case.
An expanded sweep under `/var/tmp/c1e_n11_sweep_y7dht2qf` rejected **80/80**
cases, adding 14 explicit null/missing-map JSON cases across candidate, poll,
request and status and one valid-MAC all-ones unavailable-map marker. Its
`results.md` SHA-256 is
`7d0fc5cb05b2e355ad34538a48971687b3f34cc12f539ef145b1e0cc3574b147`;
`summary.json` SHA-256 is
`d3f4bbd9fe4acd853de31c44c2d4721399222f7a4972dbe9edf3038a7c75dbed`.
Binary status fields are mandatory, so JSON null/omission has no direct wire
form. The original 65-case script and results remain retained separately.

An N09/N10 read-only audit at `/var/tmp/c1e_n09_n10_audit_20260925`
ran ten existing host executables for **19,415 checks with zero failures**:
RequestGuard, TrustedTimeMap, WireAdapter state/session/admission,
DiagnosticGuard, DiagnosticAdapter, C1E owner, StateReceiptChecker and C1C
Producer. The retained commands and source inventory are in that area
(`source-sha256.txt` SHA-256
`e27c3ae3cd95383eaced1e75db5b5af41d987cae65bb47a7f8629185e6795965`).
Fixtures cover synthetic stale/contradictory source receipts, lifetime/clock
restart, map boundaries and changed fields, producer/validator regression and
interval endpoints. Read-only inspection of both accepted native archives
found all actual required-state, admission transport and map readiness false,
with `ClockMappingInvalid` poll and `SelectionRequired` request results.
The C1E owner receives only raw arm/nav and native hrt and rejects nonzero
W1 map epoch, so native stale/restarted estimator or transport publisher facts
and qualified/replaced maps have no trusted input seam in this profile.
The code-cited, exact-matrix non-applicability proof is retained at
`/var/tmp/c1e_n09_n10_audit_20260925/non-applicability-proof.md` (SHA-256
`bdccf05cc308884b2059f894d3fd10a756f73fddb10636013d99b6a034e1a52e`).
Publisher-specific stale/restart and transport-continuity cases remain for a
future trusted seam; generic host guard and map fixtures are synthetic analogues.

## Finite matrix coverage and applicability

| Matrix cases | Evidence exercised | Remaining scope |
| --- | --- | --- |
| N01-N03 | Opt-in build/profile probes, C1D regression, one literal zero-datagram native run, two source-bound disarmed negative transactions and signed statuses. | Positive guidance admission is outside N1. |
| N04-N05 | Independent malformed/binding/signature and replay fixtures plus four fresh live hostile-traffic runs, including wrong peer, oversize/zero datagrams, stale authenticated discard, equal/lower replay and old-run key/identity/status checks. | Wrong channel is fixture-only; exhaustive timing/order injection and a real link-loss detector remain open. |
| N06-N08 | No public operator/admin API; 29 added synthetic source/loss checks, 18 arm change/ABA/mismatch checks, live stale epoch discard, and 270 deterministic worker phase/capacity/send-failure/near-overflow checks. | Actual concurrent Commander lifetime loss and kernel send saturation remain unproven; source/token wrap is unreachable before bounded closure in normal runs. |
| N09-N10 | Actual native estimator/transport/map readiness absent with distinct poll/request causes; ten host executables passed 19,415 synthetic guard, receipt, clock, map and time checks; code-cited non-applicability proof records native input limits. | Native stale/restarted publisher facts and qualified/replaced maps require a future trusted input seam; operational clock qualification remains a later positive gate. |
| N11-N12 | 80/80 native-archive and synthetic signed-status corruptions rejected, including explicit absent-map cases; output-free linked audit, two fresh process traces, native artifact/topic checks, existing-path refusal, injected mid-write ENOSPC and recovery; deterministic worker fixture covers EAGAIN status-send failure. | Actual kernel ENOSPC/status-send saturation remain unproven. |

The nominal two-run campaign alone did not close the matrix. The independent
source/build review, literal no-ingress process, hostile native campaigns,
bounded phase/source fixtures, corruption sweep and sink-failure probes now
explain every *applicable negative* N01-N12 cell for the frozen C1E-N1 profile.
N06 diagnostic attach/prepare/confirm/detach/revoke calls have no callable
CLI, parser or private owner API. N09/N10 stale estimator/transport publisher
facts and qualified or replaced native maps have no trusted input seam; their
generic guard/map behavior was exercised only in bounded synthetic fixtures.
These are explicit non-applicability decisions for N1, not evidence that a
future positive-input implementation would pass those cases.

**Gate decision, 2026-09-25:** C1E-N1 is accepted at the negative-only,
disarmed/output-disabled native diagnostic scope defined in the finite test
matrix. The independent reviewer agreed after the final N02 run and N06-N10
source/applicability audits. This accepts authenticated, source-bound ingress
that produces actual negative results and signed diagnostic status without
authority or output; it does not accept positive acceleration admission.

## Limits and next gate

This profile cannot validate a positive acceleration request or demonstrate
control, estimation, physical plant, link resilience or flight behavior. The
scripted phase fixtures do not certify simultaneous scheduler interleavings,
and injected ENOSPC/EAGAIN does not certify real kernel exhaustion or
send saturation. Future trusted estimator, admission transport and producer
map inputs must get their own publisher-specific stale/restart, replacement
and endpoint tests. Positive work also requires physically supported mass,
CG/inertia and effectors, operator provenance and a separately reviewed
active control mode.
