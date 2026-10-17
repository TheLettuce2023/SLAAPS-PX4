# P2C closed receipt merge contract

2026-09-27, Codex (GPT-6). Standalone host implementation and verification.
No PX4 or admission library linkage; N1 and P2B remain unchanged.

## Boundary and purpose

The closed merge core preserves independent, explicitly bound source streams
and records the order in which one owner consumes them. It does not derive
readiness, adapter state generation, clock qualification or operator authority.
Payload is seven opaque uint64 observation words, not a trusted state API.
Schemas and any future semantic projection must be separately reviewed. Raw
changes, including A/B/A, remain distinct records; no latest-value coalescing
is permitted. The historical arming/nav ReceiptChecker is single-source and
cannot provide this queue/merge boundary. N1's mailbox is one transaction slot.

## Fixed ownership and resource bounds

Construction fixes one to four source bindings. Each has nonzero run, owner,
controller, publisher, publisher lifetime and source-clock identities. Run,
owner and controller agree across lanes; publisher IDs are distinct. No rebind
or reset exists. Each lane has a 16-entry FIFO with a single producer and one
owner consumer. A per-lane atomic writer flag detects overlapping producer
calls and publishes a sticky terminal fault. Sequential calls from a different
thread cannot prove publisher identity; the trusted bootstrap must own that
capability before live use. This API is not exposed to guidance.

Producer payload writes precede release publication of the write counter.
Owner acquire of that counter precedes copying. Owner release of the read
counter follows copying; producer acquire precedes slot reuse. Counters never
wrap: exhaustion is terminal. Full queues reject without overwrite or retry.
A separate atomic first-fault cell can publish loss even when the FIFO is full.
No queue operation allocates, blocks, performs I/O or waits for another thread.

The owner archives up to 4096 merged records and has a separate reserved terminal
record. These are tooling storage bounds, not flight timing or health policy.
Archive exhaustion closes before another ordinary record is committed. Queued
but unconsumed receipts are not silently claimed as merged evidence.

## Receipt and owner chronology

Receipts carry the complete expected identity, source sequence, source
observation time, kind (Initial, Sample or Lost), and immutable observation
words. Initial is exactly sequence one and occurs once per lane. Later sequence
is previous plus one with checked arithmetic. Source time is nonzero and may
repeat at microsecond resolution, but cannot regress within that stream.
Unknown kinds, wrong identities, missing/repeated Initial, gaps and regression
close the owner without replacing retained observations. Lost is a validated
terminal receipt, retained in closure rather than committed as ordinary state.

Each owner step consumes at most one receipt. It starts after the previously
served lane and selects the next nonempty lane in cyclic order. Continuously
nonempty lanes therefore cannot starve another nonempty lane. The record gets
an owner order and the supplied nonzero, nonregressing owner observation time.
Raw source times are never sorted or compared across clock domains, or compared
to owner time without qualification. Enqueue time is not measured by this core.
Neither owner order nor source sequence establishes physical-time causality.

Owner fault acquisition before mutation is the linearization point: a sticky
fault already observed wins over the candidate; one published afterward closes
on the next owner step without relabeling the earlier record. Owner clock
regression and explicit stop also close. First terminal cause and lane, plus any
owner-selected rejected candidate and its separate lane, are retained. A racing
fault may originate in a different lane from that candidate. Producer-side
overflow/concurrency rejection retains cause/lane, not the rejected payload.
Ordinary archive capacity cannot prevent closure.

## Shutdown, validation and remaining release boundary

The caller must stop/join all producers before destroying queues or exporting
their storage. Owner closure is not producer quiescence: an already-entered
producer can finish publication after closure, but no later owner commit occurs.
Archive access is owner-thread-only or after the owner is joined. There is no
public mutable snapshot or adapter pointer. Positive authority is structurally
absent, not controlled by a user-set enable flag.

Validation targets FIFO ownership, round-robin order, retained ABA, concurrent
handoff, overflow/fault precedence, identity/restart rejection, exact sequence
and clock rules, and evidence exhaustion. A source-bound P2B replay will retain
the original native observations as **offline replayed evidence**; its synthetic
merge identities are not claims about the original publisher's lifetime. Native
hook completeness, live typed receipts, stale/lease budgets, owner integration,
estimator health and P3/P4 qualification remain separate gates.

## Verification and retained evidence

Implementation is in `Tools/research_sitl/provenance/merge/`, with CMake default
OFF and explicit Linux host guards. `run_merge.py` copies exact source inputs
to a new WSL build area, builds Debug and ASan/UBSan targets, runs the finite
contract harness, and replays only independently verified P2B rows. The replay
records explicit normal owner closure. `check_merge.py` independently reconstructs
every input and merged observation from P2B, verifies order/timestamps/closure,
and checks exact source, executable, command, build configuration and log bindings.

Corrected package: `/var/tmp/p2c-merge-vvtmfdr0` in Ubuntu-24.04 WSL.
Manifest SHA-256:
`d0033f541394ef5609d5a6239e0e5074a2b96e1a6bea82ea9383441cbe82763e`.
Retained result: `independent-check.json`, 1200/1200 P2B observations reconstructed.
The P2B reference package remains `/var/tmp/p2b-native-30xfeeqc`.

- Debug and ASan/UBSan contract runs pass: cyclic order/ABA preservation,
  lifetime/clock identity, sequence/kind/initial/loss handling, first-fault
  precedence over a full queue, queue counter exhaustion, archive exhaustion
  with reserved terminal evidence, and 1000 concurrent payload handoffs.
- The concurrent producer's test-only capacity throttle uses relaxed loads;
  production queue acquire/release provides payload publication and slot-reuse
  synchronization. This is a finite execution plus source review, not exhaustive
  scheduler-race or thread-sanitizer proof.
- Default-off build creates no merge executable. Unmarked, PX4 and lockstep
  compile probes reject with the expected profile-guard diagnostic.
- Debug static self-frames are 192 bytes for publish and 448 bytes for owner
  step. These exclude callees; no native work-queue or flight-resource claim
  follows. The large fixed archive belongs to isolated host tooling.
- Source files match their archived bytes before/after verification. Scoped
  PX4 astyle formatting is clean. No native/baseline run, commit, publish or
  physical operation was performed in P2C.

Independent review corrected terminal candidate attribution: fault lane and
owner-selected candidate lane are now separate. It also found a checker gap
where altered build argv escaped validation. The corrected checker binds all
12 command invocations, seven generated configuration files, explicit profile
switches and sanitizer compile/link flags. Preserve preliminary package
`/var/tmp/p2c-merge-0_bq8qdf` and review
`/var/tmp/p2c-independent-review-miv1c44v/review.json`; its replay result alone
did not independently establish the complete build/sanitizer evidence binding.

Final independent review accepted closed host merge/offline replay scope after
rechecking the corrected package. Four focused corruptions were rejected:
changed command argv, missing sanitizer compile flag, missing sanitizer link
flag and disabled sanitizer configuration, including recomputed configuration
hashes. Evidence: `/var/tmp/p2c-final-review-6rx0877y/review.json`. Its rebased
temporary control copy is explicitly synthetic; original executed evidence was
preserved and rechecked. No review blocker remains at this bounded scope.

Next is typed source-commit/lifetime integration with an independently reviewed
native closed owner. Opaque observation words must not be promoted to trusted
admission state. Full P2 remains open alongside P3/P4/P5.
