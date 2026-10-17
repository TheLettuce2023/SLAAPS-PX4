# P2D typed native observation merge

2026-09-27, Codex (GPT-6). Native typed observation/merge independently verified. This extends
the closed observation pipeline, not native admission or estimator-health
qualification. N1, stock baseline policy/evidence, accepted P2B and P2C sources
remain unchanged. Original CAD is still required for physical acceptance.

## Typed boundary and lifetime

`EstimatorObservation` copies the actual sample time, four validity predicates,
control/fault flags and replay/multi mode at P2B's existing EKF output boundary.
It is deliberately not named a readiness or health receipt. The same iteration
first goes to the unchanged P2B raw recorder, then to the typed callback. Their
sample/predicate/flag values must match exactly; separately sampled hrt values
are ordered observations, not required to be equal.

New private hooks at EKF2 constructor entry and destructor entry bind one actual
source lifetime. The constructor must occur after observation bootstrap and
before any typed sample. Pointer equality is only a local consistency check:
the first constructor is one-shot, so another constructor is rejected even if
the allocator reuses the same address. Replay/multi mode, missing or mismatched
source, unexpected destruction and repeated end all fail the closed capture.

The random native run namespaces owner/controller/publisher/lifetime/clock
identifiers. These identify the closed experiment's objects, not a Commander
controller identity, calibrated sensor or qualified clock provider. Constructor
and destructor hooks only publish atomic lifetime facts. The EKF work queue
alone writes observation receipts. No queue producer ownership is transferred
to the constructor/destructor thread. The single constructor must return before
the normal EKF scheduling path can publish; a racing destruction/observation is
not assumed safe and cannot be accepted if chronology or lifecycle contradicts.

## Owner, timing and closure

The existing finite feeder's sequential administration domain is the merge
owner. It pumps at most four records per fixture iteration before the existing
4-ms sleep. No additional thread, Commander, admission adapter, control/output
module, network endpoint or simulator bridge is added. An atomic administration
guard rejects overlapping calls and serializes any thread handoff between CLI
commands. Queue ownership remains the reviewed P2C acquire/release protocol.

The core's supplied owner time is sampled before `step`. A source can enqueue
after that sample but before dequeue, so it must not be relabeled as an exact
evaluation time. The bridge additionally samples completion after each accepted
step and retains the bracket. Source time must be no later than completion;
the checker does not falsely require it to precede owner entry. This remains
same-process observation evidence, not a producer-to-validator map.

After the full finite feed, expected stop names the already-bound lifetime;
it never authorizes a replacement constructor. `ekf2 stop` must produce the
matching destructor event. Export freezes the typed hooks, waits at most 1000
one-ms cleanup iterations for in-flight callbacks, and requires the observed
end event. Freeze/quiescence alone cannot substitute for destructor evidence.
At most the fixed 16-entry queue is drained after quiescence. Normal closure
requires every successfully formed source receipt to be committed, and the
independent checker additionally requires an exact raw-to-merged correspondence.
Any fault or missing tail rejects acceptance rather than silently truncating.

`end_us` is the observed destructor **entry**, not completed teardown or proof
of scheduler quiescence. Successful return of the stop command and framework
teardown are separate lifecycle evidence before export. The hook alone must
not be promoted to proof that an EKF work item has finished.

Terminal source/bridge faults remain sticky through stop/export. A second
native process intentionally stops and restarts EKF2 after the feed: the second
constructor must retain SourceStart rejection, even if the address is reused.
Its typed fault artifact is retained but cannot be presented as normal raw
capture success. Both processes must close normally without forced termination.

## Source and build isolation

The runner verifies pinned originals, then makes an owned private source view.
Private changes are limited to EKF lifecycle/observation hooks and linkage,
P2B feeder bootstrap/pump/expected-stop/export calls, and the merge header's
explicit P2D non-lockstep POSIX guard. `ReceiptMerge.cpp` remains byte-identical
to accepted P2C. The original host header still rejects PX4 builds. All overlay
hashes and native/merge/tool sources are archived. No firmware source in the
workspace is modified by the build.

The independent checker must verify exact source/archive inventory, overlays,
profile definitions, module/builtin allowlists, commands, build logs, executable,
startup/stimulus identity, source lifetime, receipt identity/order, owner brackets,
complete normal raw correspondence and actual restart rejection. It retains
positive admission and map qualification false. Source self-frames and fixed
storage are reviewed as diagnostic POSIX resources, not embedded acceptance.

## Native verification

Final native package: `/var/tmp/p2d-native-od002jmn` in Ubuntu-24.04 WSL.
Manifest SHA-256:
`cafa0312ea26e5a273216102cec7d0f3f843c6ccd6664039f311eaec3339a248`.
The full before/after source registry matched, including dirty/untracked inputs.
The 438-step Debug build and both native processes completed normally.
`independent-check.json` retains the separate checker's result.

The normal process retained all 1200 typed rows, exactly matching the parallel
raw recorder's sample, flags and predicates. The restart process retained its
1200 pre-restart rows and sticky SourceStart rejection (bridge fault 4), and
produced no successful raw export. Both owners closed with OwnerStop (15).
Maximum source-to-owner-completion delay was 29,257 us normally and 54,798 us
in the restart case. These are measurements of these runs, not approved
latency/freshness budgets or qualified clock uncertainty. All attitude-valid
observations remained false.

`resource-review.json` retains compiler stack reports and object symbols;
SHA-256 `afacb579b6dc96caddf1c5402f0b7f2f741b1d2f8d1ecdc3137ebfbbd22aba78`.
Self-frames exclude callees: capture 240 bytes, constructor hook 128, destructor
hook 144, consume-one 192, merge publish 176 and merge step 432, all static.
Export has a 768-byte dynamic/bounded frame outside the source callback.
The fixed optional owner occupies 631,328 bytes and completion array 32,768
bytes, in static storage. This diagnostic archive is not an embedded resource
acceptance claim or a measured peak stack.

Preserve failed configure-only attempt `/var/tmp/p2d-native-7io33289`.
It stopped before runtime because the external module was parsed before the
PX4 library helper definition. The final build uses the existing external
module for all three sources. Independent source review found a checker hole
for zero owner-entry time; that check was corrected before the final build.
The destructor-entry/teardown distinction was clarified at the same time.

Independent review accepted this bounded scope after rerunning the full
checker and rejecting seven targeted corruptions: zero owner-entry time,
invalid lifecycle, falsely accepted restart, typed/raw payload mismatch,
missing P2D compile marker, mixed N1 profile and changed build argv. Review
artifact: `/var/tmp/p2d-independent-review-hp9h4jgq/review.json`. Probes used
a clearly labeled synthetic rebased copy or in-memory trace copies; that copy
is not executed native evidence. The original package passed recheck unchanged.

## Remaining gates

P2D does not declare the admissible health commit predicate or instrument every
internal filter mutation. Sensor/device provenance, resets and selected-instance
policy, immutable health/stale budgets, source-complete readiness publication,
Commander/adapter integration, and clock/operator/transport authority remain
open. The boundary here is typed output observation plus actual one-shot EKF
lifetime and bounded merge; it must not silently become an admission API.
