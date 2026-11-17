# C1B-D diagnostic-selection acceptance and future tests

2026-09-20. Design specification, **not executed test evidence**. Companion
document: C1B_SITL_DIAGNOSTIC_SELECTION_DESIGN.md. User approval is design-only.
No existing C1A/C1H result covers these new behaviors.

Subsequent C1B-I API/test design is accepted in C1B_OFFLINE_API_CONTRACT.md
and C1B_OFFLINE_API_TEST_MATRIX.md. Their typed APIs, finite expected outcomes
and report-capacity rules refine this architecture-level specification.
Neither document is executed evidence; implementation acceptance remains open.

## Design review oracle

Each future case must preserve actual flight-mode/arm observations, use one
adapter/guard and explicitly label provenance synthetic or measured. Success
means only a correlated diagnostic validation result, never flight acceptance,
a cached demand, actuator output or a statement of acceleration tracking.

| Case group | Required independent assertion |
| --- | --- |
| Flight compatibility | Existing Flight request, admin, poll, getter, status and wire tests pass with unchanged expectations; no new default permission |
| Immutable purpose | Invalid domain, normal-target diagnostic construction, runtime switching, copy/reset/reconstruction workarounds fail closed |
| Selection authority | Guidance requests/heartbeats/ACKs/status cannot select; prepare is non-authorizing; only fresh independently confirmed exact-context ticket can select |
| Grant binding | Wrong domain, owner/controller lifetime, operator attachment, state generation, selector/transition/map epoch, stale/duplicate/foreign ticket rejected without refresh or floor reset |
| Truth table | Cross product of known/unknown/invalid arm and flight-mode values, selected/disabled/faulted selector, policy Reject/ValidateOnly and configured/unconfigured owner matches design |
| Prerequisites | Invalid/missing/stale/inconsistent estimator, arm, clock, map or connection prevents enable and validation; no fake validity or Selected fields |
| Positive diagnostic request | Independent session authorization, new session, post-barrier ACK and fresh request required; all payload/time/bounds checks executed; result diagnostic-only and eligible=false |
| Replay and latches | Valid diagnostic request advances sequence/time floors exactly once; duplicates, older times, overflow and regressing clocks follow shared rules; selection toggle never resets floors |
| No cached demand | Sentinel getter output byte-identical before/after all operations; accepted-flight history unchanged; arm after diagnostic success never promotes old request |
| Invalidation | Authorized disable/revoke/clear, detach, trusted context loss/rebind and mode/arm changes close selector/session/ACK before later ingress; rejected admin attempts do not, and recovery does not auto-enable |
| Ordering | State invalidation wins over concurrent confirm/request; all admitted operations have one serialization order; console never calls adapter/guard directly |
| Administrative barriers | Separate selector epoch; no fake physical-state generation; exact report order/capacity; ordinary establish does not accidentally disable selection; ACK refers to latest barrier |
| Terminal behavior | Missing continuity, counter exhaustion and invalid structural stream terminalize without wrap; no UI clear/reset/reuse of lifetime |
| Evidence | Domain and actual state captured at each report, not copied from a later snapshot; poll/session success cannot impersonate request success |
| Output/wire isolation | No new result silently serialized as frozen v1 Flight status; no new uORB control publisher, output driver, default startup, generated dialect or physical transport |

## Required state sequences

1. Start Disabled. A correctly authenticated candidate alone cannot select.
   Prepare then mutate each binding before confirmation; each variant rejects.
2. Select with valid synthetic context, authorize a distinct session, ACK after
   the newest barrier, evaluate a valid request, then access. Verify diagnostic
   success, replay-floor advancement, no cached demand and unchanged getter output.
3. Disable and immediately replay old grant, session, ACK and request. None
   restores validation; repeat enable requires new authorization and session.
   Qualify the prospective new-session map while Disabled, select against it,
   then establish that same session without a map rebind. Only successful
   distinct-session establishment performs its existing session-scoped request/
   producer floor reset; selector changes do not reset any floor. Rejected
   revoke/clear attempts leave valid selection unchanged.
4. From the positive case inject actual Armed or flight Selected, then return
   to Disarmed/NotSelected. Selection remains disabled; no demand was ever copied.
   These are offline state fixtures, never an instruction to arm live PX4.
5. Inject loss/staleness/regression for each required input at prepare, confirm,
   session establishment, ACK, request, poll and access boundaries. Invalidation
   precedes a queued operation; no later valid sample restores old authority.
6. Exercise authorized session establish/revoke and latch-clear while selected
   and disabled. Check selector and adapter epochs independently, floors and
   exact report bounds. Clear never selects or establishes a replacement session.
7. Saturate report/counter capacity and stall the evidence sink. Closure remains
   immediate; inability to retain complete evidence makes the campaign incomplete,
   never an inferred pass. Test final representable and first unrepresentable IDs.

## Independent evidence rejection tests

Mutate one field at a time: absent/wrong domain, actual mode rewritten Selected,
eligible=true, flight accepted-history advanced, missing grant/state provenance,
reused epoch/lifetime, stale ACK, duplicate/future/regressing clocks, omitted
transition, missing rejection, reordered report, fabricated getter success,
truncated evidence, mislabelled fixture as measured, or an unexpected output
topic. Each corrupted package must fail the external oracle. Unknown record
versions/enum values are failures; the checker must not copy implementation
helpers or accept summary booleans as proof.

Before offline code release, specify exact typed APIs, predicate precedence,
counter bounds, evidence schema and a finite enumerated matrix with expected
results. Before live release, qualify actual clock/state/estimator/operator-loss
provenance, diagnostic status/profile binding and owned no-output runtime paths.
Do not invent numerical budgets or inherit the C1A sampling period as a deadline.
Host fixture tests do not replace PX4 integration or full Phase 2 evidence.
