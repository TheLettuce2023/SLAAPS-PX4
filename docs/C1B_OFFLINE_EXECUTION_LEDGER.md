# C1B offline execution ledger

2026-09-20. Independent source/log reconciliation by Codex. This ledger records
the accepted C1B-G/A/O baseline and the final C1B-E reconciliation. Baseline
tables retain their historical partial/pending labels; the final section below
explicitly supersedes those entries using new execution, not old test counts.
Companion requirements are
[the contract](C1B_OFFLINE_API_CONTRACT.md) and
[the finite matrix](C1B_OFFLINE_API_TEST_MATRIX.md).

All inputs are synthetic offline fixtures; authentication facts are injected,
not measured parser/signing evidence. Counter/invalid-enum injections use the
private test peer. No PX4/Gazebo run, physical clock qualification, live
transport, vehicle geometry, actuator output, or flight validation is claimed.

## Historical C1B-O execution and interpretation

Read-only inspection of `/var/tmp/c1b_owner_f7FwVSQQ` verified these completed
logs; this reviewer did not run builds or tests:

| Log | Recorded outcome |
| --- | --- |
| `ctest-debug-final.log` | 28/28; zero failures |
| `ctest-release-final-1.log` | 28/28; zero failures |
| `ctest-release-final-2.log` | 28/28; zero failures |
| `ctest-sanitize-final.log` | 28/28; zero failures |
| `ctest-default-off.log` | 17/17; zero failures |

The Debug `Testing/Temporary/LastTest.log` records 1,024 predicate tuples,
exactly one positive tuple, 88 guard request variants/12,163 checks, 3,400
adapter checks, 1,124 owner checks, and 31 compile-isolation probes, all with
zero failures. Check totals include helper assertions, not distinct matrix
variants. The Release and sanitizer counts above are suite outcomes, not a
claim that per-case transcripts were emitted. Source inspection connects the
named functions below to each executable's unconditional `main()` calls; the
logs do not separately print a pass line for every loop iteration.

Source/compiler/configuration/executable provenance is recorded separately in
[owner verification](C1B_OWNER_IMPLEMENTATION_VERIFICATION.md). This ledger
does not independently certify hashes it has not recomputed. Later source
additions do not inherit these baseline execution results.

Status terms: **Executed** means the named baseline test ran and contains the
specified assertion; **Partial** identifies an explicit missing assertion or
layer; **Pending** means no accepted execution is recorded here. An absent API
is not simulated runtime reception.

Source abbreviations used in the tables:

- G: [DiagnosticGuardTest.cpp](../src/lib/research_acceleration/DiagnosticGuardTest.cpp).
- A: [DiagnosticAdapterTest.cpp](../src/lib/research_acceleration/DiagnosticAdapterTest.cpp).
- O: [DiagnosticSupervisorOwnerTest.cpp](../Tools/research_sitl/supervisor/DiagnosticSupervisorOwnerTest.cpp).
- I: [DiagnosticBuildIsolationTest.py](../src/lib/research_acceleration/DiagnosticBuildIsolationTest.py).

## Guard predicate and request variants

G `predicateProduct()` implements `T-arm-mode-selector-bits-policy`: four arm
values x four mode values x four selector values x eight prerequisite-bit
combinations x two policies = 1,024 executed tuples. Invalid enums are 255.
Reject-policy construction remains unconfigured and does not run establishment
setup. The independent ordered expectation permits exactly one tuple. No
tuple grants eligible control or creates active demand.

Unless noted otherwise, request rows use G `payloadBoundaries()`; its
`expectRequest()`/isolation helpers check actual cause and retained state.

| ID | Named executed variants | Status / limits |
| --- | --- | --- |
| R01 | `R01-0..5`: protocol, source, session, frame, quantity, reference | Executed: six distinct causes; extra canonical-zero-session check is not counted as a seventh required variant. |
| R02 | `R02-axis0..2-kind0..2`: NaN, +Inf, -Inf per axis | Executed: nine. |
| R03 | `R03-axis0..2-boundary0..3`: below min, min, max, above max | Executed: twelve. |
| R04 | `R04-0..2`: norm below/equal/above ten | Executed: three; axis bounds widened to reach the norm check independently. |
| R05 | `R05-19/20/200/201` | Executed: four duration boundaries. |
| R06 | `R06-2/3` | Executed: two uncertainty boundaries. |
| R07 | `R07-4/5/6` after floor five | Executed: lower, duplicate, greater; three. |
| R08 | `R08-0..2`: raw expiry below creation, mapped expiry below creation, unequal durations | Executed: three. |
| R09 | `R09-earliest-creation-underflow`, `R09-earliest-expiry-underflow-masked-by-creation`, `R09-latest-creation-overflow`, `R09-now-plus-skew-overflow` | Executed: four input fixtures, not four independently reached arithmetic branches. Positive duration means earliest-expiry underflow necessarily also underflows earliest creation; the earlier check masks it. |
| R10 | `R10-earliest-10012/10013`, `R10-latest-10010/10011` | Executed: four; latest-creation fixture uses uncertainty ten to avoid the earlier predicate masking that boundary. |
| R11 | `R11-9962/9961` | Executed: maximum age and one beyond. |
| R12 | `R12-20/21/22` | Executed: earliest expiry now-1, now, now+1. |
| R13 | `R13-8999/9000/9001` | Executed: regression/latch, equality rejection, increase; rejected floors retained. |
| R14 | A `admissionAndAccess()`, loop variants 0..3: missing session, invalidated session, missing ACK, stale ACK | Partial: all four inputs execute and assert Rejected/no latch/no positive eligibility, but this baseline does not assert each exact respective cause. Additive cause assertions are pending execution. |
| R15 | A `admissionAndAccess()`, six `R15 foreign ingress` variants | Executed: owner/run mismatch and four false authentication facts; following same-sequence valid request proves no request-floor consumption. |
| R16 | `R16-0..3`: stale sequence+expired, frame+NaN, uncertainty+duplicate, old+producer regression | Executed: four precedence cases with no latch from an unreached predicate. |

G `recoveryAndAdministration()` additionally checks poll versus request
semantics, arm/mode non-promotion, retained floors on invalidation, rejected
unauthorized/non-disarmed clear, clock reset without session restoration,
same-session rejection, distinct-session floor reset, validator-time latch,
and eight establishment prerequisite failures. G `domainIsolation()` checks
cross-domain methods, byte-identical getter output, preservation of an actual
Flight cached request, and three invalid configurations.

## Adapter, owner, and lifecycle variants

| ID | Exact implementing function(s) and variant accounting | Baseline status / remaining qualification |
| --- | --- | --- |
| A01 | A `constructionAndDomain()`: controller/owner/run zero, invalid bounds, Reject policy | Executed: five; inert construction and private lifecycle seams checked. |
| A02 | A `constructionAndDomain()`: ordinary observe/poll/access/session/clear/ACK/request on diagnostic; all ten diagnostic turn alternatives on Flight; private attach/close on Flight. G `domainIsolation()` and O `ticketsAndDomains()` add guard/owner misuse. | Executed for the enumerated methods. Read-only configured/session queries are truthful, not mutating domain conversions. |
| A03 | O `positiveAndNonAuthority()`: request and ACK without enable | Executed: two packet inputs. Heartbeat/status have no decoded diagnostic ingress alternative at this seam; runtime reception is not applicable here and remains outside the offline transport scope. Explicit absence probes are pending execution; no four-runtime-variant claim. |
| A04 | O `positiveAndNonAuthority()`: prepare-only then request | Executed; prepare confers no selection. |
| A05 | A `bindingsAndEvents()`: 16 `A05 binding` mutations; O `ticketsAndDomains()`: `A05 full map view changed` | Partial: direct adapter exercises seven identity/epoch fields plus nine map fields; owner confirmation presently changes offset and generation together. It does not independently execute every field before broker confirmation. Full confirmation-field additions are pending execution. See field accounting below. |
| A06 | A `bindingsAndEvents()`: duplicate, older, zero, gap, max-exhausted event | Executed: five; duplicate/older do not consume, invalid trusted stream terminalizes. |
| A07 | O `ticketsAndDomains()`: selection ticket via admin, admin ticket via selection, diagnostic ticket via Flight confirmation, unrelated ticket | Executed: matching wrong-route ticket canceled; unrelated ticket preserves pending action. This is not a claim that every conceivable public-route permutation was executed. |
| A08 | O `ticketsAndDomains()` explicit age 51 with limit 50; O `uncertainOwners()` regressed owner time | Executed: two required outcomes, expiry and quarantine. |
| A09 | O `ticketsAndDomains()` consumed confirmation reuse and busy other intent | Executed: one pending ticket, one event/action. |
| A10 | A `administrationAndLifecycle()` selected disable and repeat disabled disable | Executed: two; retained wire replay floor, idempotent epochs. |
| A11 | A `administrationAndLifecycle()` revoke, latched clear, unlatched clear | Executed: three; respective B,O,S / B,O / rejected O orders. O positive cases independently use the real owner/broker. |
| A12 | A `administrationAndLifecycle()` wrong operator, stale clear transition, wrong revoke session, unauthenticated revoke | Executed: four; valid selection/ACK survive rejected admin. |
| A13 | A `administrationAndLifecycle()` three labeled loop iterations | Executed with qualification: clock-absent and map-absent use the same required paired-false flags, so there are two distinct physical input configurations (paired clock/map false; required-state false), not three independently varying flags. Clear never repairs a structurally terminal adapter. |
| A14 | A `administrationAndLifecycle()` after disable; A `distinctSessionRecovery()` after clear | Executed: both old-session rejections, floors retained. |
| A15 | A `distinctSessionRecovery()` | Executed: Disabled staging, explicit enable, distinct session establishment without rebind, new ACK, request/producer-floor reset and wire-floor preservation. |
| A16 | A `physicalStream()`, `mutateMap()` fields 0..8 and physical clock flag 9 | Executed: ten inputs, recoverable rebind versus unpaired validity/controller structural faults distinguished. |
| A17 | A `admissionAndAccess()` next generation with unchanged state | Executed: P,O; selected remains, ACK clears, subsequent request requires ACK. |
| A18 | A `physicalStream()` arm/mode loop | Executed: Armed/Unknown/invalid arm and Selected/Unknown/invalid mode; six. |
| A19 | A `physicalStream()` `A19/A22` prerequisite loop and restoration | Executed: paired clock/map false, transport false, required-state false; three plus restoration, no automatic enable. |
| A20 | A `physicalStream()` continuity loop | Executed: six concrete inputs: changed state, changed map, skipped generation, regressed generation, continuity false, zero. The original row's word "five" undercounted its own enumeration. |
| A21 | A `regressionAndFourRecords()` | Executed: validator regression at poll, request, enable confirmation, clear; four. Only authorized clear recovers the latch, not selection. |
| A22 | A `physicalStream()` stale grant after physical safety prefix | Executed: ordered prefix retained and Completed rejected action; not a no-side-effect NotSubmitted. O stale view rejection before submission is a separate test. |
| A23 | A `administrationAndLifecycle()` two close reasons; O `lifecycleAndReceipts()` detach, unhealthy detach, trusted owner loss | Executed: closure and queued-receipt cancellation. Direct owner loss immediately invalidates authority; a broker-owned pending ticket is rejected/canceled on the next matching confirmation, not cleared by an invented owner-to-broker callback. |
| A24 | A private close without receipt; O `lifecycleAndReceipts()` absent receipt; O `uncertainOwners()` unknown close | Executed: terminal/quarantine and no usable authority. |
| A25 | O `uncertainOwners()` NotSubmitted, Unknown, malformed Completed | Executed: real no-side-effect check versus quarantine. Negative-only result-mutating wrapper is not the positive admission oracle. |
| A26 | O `uncertainOwners()` index, barrier, counter, consumption mutations | Executed: all four named defects quarantined. Additional corruption functions below strengthen this, without replacing JSON E cases. |
| A27 | A `admissionAndAccess()` access and poll following valid request and arm change | Executed: two, never eligible/copied demand. |
| A28 | A `administrationAndLifecycle()` and O `lifecycleAndReceipts()` already-disabled close | Executed: invalid session/ACK retained, no invented selector/transition increment. |

A05/A16 field accounting: the actual `TrustedClockMap` has **nine** fields:
valid, source, session, controller_instance_id, map_epoch,
validator_minus_producer_us, uncertainty_us, valid_from_producer_us,
valid_until_producer_us. `clock_mapping_valid` is the **separate physical
context bit**, not a tenth member of `DiagnosticBinding::map`. A05 must not
be declared fully covered by the sixteen direct-grant mutations alone.

Additional named owner checks are `realOwnerRefusalAndProvenance()` (actual
stale view, five invalid provenance/time conditions, reentry closure),
`corruptedAdministration()` (successful/rejected admin grammar),
`remainingCauseAndLatchCorruptions()` (cause, latch, authentication, and barrier
inconsistencies, including lifecycle), and `malformedSuccessPreconditions()`.
They assert that intended corruptions were reached and that quarantined
authority cannot subsequently act. Offered admin receipts are copied,
one-shot, canceled on close, and not synthesized from a confirmation ticket.

## Golden trace, capacity, isolation, and local evidence

A `golden()` executes all six reference steps with frozen wire protocol 65536,
report IDs 1..10 and operation indexes 1,1,1,0,0,0. Request success has age 12.
The following producer regression has independently calculated age 14, but the
actual unchanged guard latch decision has `age_valid=false`, age zero. The
request rejection is O before S; its result must not be read from the final
closure snapshot. Injected fixture authentication is not measured provenance.

| ID | Implemented/observed case | Status / limits |
| --- | --- | --- |
| C01 | A `capacity()`, four shapes x zero/exact/one-short = twelve fixtures | Partial: all twelve execute; exact-capacity cases assert count/final report ID and generic invariants, not each exact kind order/index. Additive per-shape assertions pending. The reachable request-latch shape is O,S, not P,O,S: a physical barrier would clear ACK before evaluation. Other shapes are P,S,O; P,S,B,O; P,B,O,S. |
| C02 | A `capacity()`, needed transition increments 1/2/3 x exact/one-short | Executed: six, final representable epoch and safe exhaustion. |
| C03 | A `capacity()` selector MAX-1 to MAX, next changing disable, idempotent disabled call at MAX | Executed: final increment, terminal on overflow, no increment for idempotence. |
| C04 | A `capacity()` exhausted case followed by `observe()` | Executed repeated terminal calls: zero second records and stable MAX sentinel. This baseline repeat is observation, not a separately enumerated packet-ingress campaign. |
| C05 | A `capacity()` invalid first generation zero | Executed: truthful invalid state reference and zero epochs/generation. |
| C06 | A `regressionAndFourRecords()` direct synthetic post-prefix admin bindings | Executed: P,S,B,O index 3 and P,B,O,S index 2; two distinct four-record shapes, event/action checked. These are not stale broker tickets. |
| C07 | A `golden()` and `capacity()` producer regression | Executed: O rejection before S close; operation is not final record. |
| C08 | Blocked and missing evidence sink | Pending: two exporter/campaign cases; no C++ owner test substitutes for sink evidence. |
| E01 | Remove domain, change domain, eligible true, advanced Flight history, changed actual mode | Pending: five independent JSON corruptions. |
| E02 | Remove report, reorder reports, duplicate report, truncate input, truncate output, replace earlier snapshot with final snapshot | Pending: six explicit JSON corruptions. The original row's word "five" undercounted these six actions. |
| E03 | Wrong operation index, barrier delta, replay identity, stale ACK, fabricated request success from poll, fabricated success from admin | Pending: six JSON corruptions. Production broker negative tests are not execution of the independent JSON oracle. |
| E04 | Unknown version, key, enum; duplicate key; floating integer; out-of-range integer; false measured provenance | Pending: seven strict-parser/oracle cases. |
| B01 | Default-off 17-test suite; opt-in 28-test suites; I ordinary/diagnostic header probes | Executed: separate libraries/executables and absence of diagnostic public types in ordinary build. |
| B02 | I forbidden-definition loop plus non-Linux probe | Executed: eight explicit definitions (`__PX4_NUTTX`, `__PX4_POSIX`, `__PX4_LINUX`, `__NuttX__`, `CONFIG_ARCH_BOARD`, `ENABLE_LOCKSTEP_SCHEDULER`, `BOARD_LOCKSTEP`, `PX4_SIMULATOR_TIMESTAMP_PROXY`) and non-Linux; these implement the five named target families, not five total probes. |
| B03 | I frozen mapper rejection; G `domainIsolation()` mismatch sentinel | Partial: diagnostic report cannot enter frozen mapper; guard mismatch maps false without changing uint16 destination. New adapter/admin/ingress mismatch report cases lack explicit runtime no-write sentinel assertions in this baseline; additions pending execution. |
| B04 | Baseline default-off and opt-in ordinary regression targets | Executed: recorded suites pass. Preservation/hash claims belong to the verification manifest, not inferred from a passing test count. |

The 31 baseline I probes also cover private diagnostic construction, private
guard evaluation, private operator attachment/lifecycle close, missing opt-in
and standalone markers, forbidden result conversion, facade authority hiding,
and absence of snapshots in console results. Planned new probes have no
execution credit until their own logs are accepted.

## Final C1B-E execution and reconciliation

The following new execution supersedes the historical partial/pending entries
above. Read-only inspection independently verified all five completed logs
under `/var/tmp/c1b_evidence_kwztBqUK`; this reviewer ran no builds or tests.

| Log | Recorded outcome | Elapsed time |
| --- | --- | --- |
| `ctest-debug-final.log` | 31/31; zero failures | 128.48 s |
| `ctest-release-final-1.log` | 31/31; zero failures | 122.22 s |
| `ctest-release-final-2.log` | 31/31; zero failures | 298.47 s |
| `ctest-sanitize-final.log` | 31/31; zero failures | 133.37 s |
| `ctest-default-off.log` | 17/17; zero failures | 2.17 s |

The repeated-Release log SHA-256 was independently checked as
`1835ed872e70487a1383a56186af58d6635f69de6a0ca48e353c4fdbb764505e`.
Debug `Testing/Temporary/LastTest.log` records 12,163 guard checks (unchanged
1,024 tuples and 88 request variants), 3,533 adapter checks, 1,209 owner checks,
33 compile-isolation probes and 11 passing evidence unittest methods. Required
corruption variants are subtests within those methods, not eleven total
mutations. Native assertion totals include setup/helper checks.

The final Debug campaign retained evidence at
`/var/tmp/c1b_evidence_kwztBqUK/debug/c1b-evidence-mdb9lafv`. The registered test
invokes the actual exporter through `run_campaign.py`, then the independent
checker. It records 20 real calls, 19 reports and exactly one correlated
validated-disarmed request in `c1b-owner-reference/v1`. The corruption test's
registered integration invocation likewise obtains its positive baseline from
the real executable; synthetic unit-only artifact fixtures are not presented
as owner execution.

| ID | Final named source and explicit new coverage | Final status |
| --- | --- | --- |
| A03 | I probes `diagnostic owner has no heartbeat admission API` and `diagnostic owner has no status admission API`, plus the two previously executed request/ACK inputs in O `positiveAndNonAuthority()` | Executed with seam qualification: two runtime packet cases and two API-absence probes; not four runtime reception cases. |
| A05 | O `everyPendingBinding()`: owner, run, controller, operator, generation, transition, selector; map valid/source/session/controller/epoch/offset/uncertainty/from/until; physical clock-mapping bit | Executed: seventeen negative-only owner-view dimensions before confirmation, actual retained owner/event checks and stale-ticket retry. Duplicated view fields are kept coherent where possible; structurally inconsistent variants may quarantine. Nine map members remain distinct from the physical bit. |
| R14 | A `admissionAndAccess()`: five explicit fixtures and exact causes | Executed: missing session -> SessionGateRejected; disabled/invalidated session -> SelectionRequired; missing ACK -> AcknowledgementRequired; original stale request binding -> BindingMismatch; additional freshly observed/rebound request with stale ACK -> AcknowledgementRequired. The fifth fixture isolates ACK checking instead of allowing the earlier binding rejection to mask it. |
| C01 | A `capacity()` exact-capacity `shape()` assertions | Executed: P,S,O index 2; O,S index 0; P,S,B,O index 3; P,B,O,S index 2. All four shapes retain zero/exact/one-short capacity variants, twelve fixtures total. |
| B03 | A `frozenMapperIsolation()` / `frozenMapperRejectsMismatch()` | Executed: actual valid ordinary AdapterReport, AdminReport and IngressReport first serialize successfully; changing only the new mismatch code rejects and leaves output bytes identical. Existing guard mismatch and diagnostic-report compile rejection remain covered. |
| C08 | C++ `Campaign::sinkTest()` via `research_sitl_diagnostic_sink_isolation`; Python `test_C08_missing_and_incomplete_delivery_never_count_as_campaign` | Executed: actual owner closed before and while delivery is blocked; blocked/missing sinks rejected; campaign_complete=false. Missing-end/incomplete/sink-schema evidence cannot count as the required campaign. This is controlled synchronization, not a measured operational deadline. |
| E01 | `CampaignMutations.test_E01_domain_eligibility_history_and_actual_mode` | Executed: remove domain, change domain, eligible true, advanced Flight-history flag, changed actual mode; five. |
| E02 | `CampaignMutations.test_E02_order_presence_truncation_and_immutable_snapshots` | Executed: remove, reorder, duplicate report; truncate input; truncate output; substitute final snapshot for earlier report; six. |
| E03 | `CampaignMutations.test_E03_index_counters_identity_ACK_and_false_success` | Executed: wrong operation index, barrier delta, replay identity, stale ACK, poll false success, admin false success; six. |
| E04 | `CampaignMutations.test_E04_unknown_types_ranges_and_provenance` and `test_E04_duplicate_keys_and_nonfinite_JSON` | Executed: required unknown version/key/enum, duplicate key, float integer, out-of-range integer and false measured provenance; seven requirements plus additional malformed types/ranges/spellings. |

Each semantic corruption writes changed campaign bytes and updates its campaign
SHA-256 before checking. Thus stale hashes cannot substitute for semantic
rejection. Raw duplicate-key/nonfinite/truncated JSON variants exercise parsing.
Additional methods reject full-call removal with a rewritten footer, altered
sentinels/age/ACK/derived grants, missing or mismatched artifacts, changed source
registries and supplied objects that differ from hashed campaign bytes. Artifact
file mutations occur only in disposable synthetic fixtures, never the repository.

The initial added R14 assertion failed because BindingMismatch correctly
preceded ACK checking. `ctest-adapter-initial.log` retains that failed run.
The original fixture was preserved with its actual cause, and a fifth fixture
was added to reach the ACK predicate independently. Existing Flight expected
values were not relaxed. See
[C1B-E verification](C1B_EVIDENCE_IMPLEMENTATION_VERIFICATION.md) for complete
build/artifact provenance, retained failures, formatting and preservation audit.

Independent source review covered the C++ exporter, actual forwarding owner
audit, independent Python model/checker, runner and mutation tests. No blocking
false-pass was identified within the declared finite profile. Typed serialization
retains each captured report, bounds diagnostic records to four and labels
reconstructed grants `owner_derived`. Authentication and clocks remain injected
synthetic facts. The runner hashes current source and staged executable bytes,
checks source stability and the standalone CMake source path; fresh build records
and preservation evidence supply their association. This is not cryptographic
source-to-binary attestation.

The finite offline gate combines independently asserted native matrix variants
with complete representative JSON evidence and every specified C08/E01-E04
negative. It does not claim every native fixture was serialized, a general
arbitrary-trace oracle, or exhaustive state-space coverage. No additional missing
finite case was identified after the qualifications above were reconciled.
Operational state/clock/estimator/operator-loss qualification, diagnostic live
profile/schema binding, output-disabled PX4 integration and the Phase 2 exit
audit remain separate work. Offline acceptance is not live promotion.
