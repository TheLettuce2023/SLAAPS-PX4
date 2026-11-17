# C1C-S1: producer state-receipt continuity checker

2026-09-20. Master design release: isolated host implementation and tests only,
under the already approved output-disabled C1 attachment scope. Independent
review and fresh verification remain required for acceptance. No Commander hook,
uORB schema, live owner, new clock authority or positive live admission is
released by this slice. Geometry is not required.

## Source facts and intended producer seam

Pinned PX4: e880f1cd79e2453ef3dfaf68ee1c067b98ea9a53.
Commander.cpp assigns arming state at arm():738, disarm():779 and initial
setup:812. Active raw nav state is initialized at 817 and assigned by
handleModeIntentionAndFailsafe():2541 and its termination branch:2552.
Commands and action requests both execute before the coalesced publications
at 2136-2150. An arm followed by disarm within that interval can therefore
disappear from both published final values without a publication-generation gap.
This is a source-based inference, not a measured armed experiment.

The future producer must capture initial state and every committed change of
the actual arming/raw-active-nav pair in Commander execution order, including
intermediate assignments. User mode intention, displayed mode and actuator_armed
publication are not substitutes. Queue overflow, restart or a missed source hook
cannot be repaired by publishing the latest state. A later source-hook review
must enumerate every mutation and its synchronization/queue failure behavior.
This checker detects malformed or lost receipts; it cannot prove a producer
instrumented all mutation sites. No claim of lossless live observation follows.

## Exact standalone API contract

New files only: `Tools/research_sitl/state_receipts/`. C++17 standard-library
types, no PX4/admission library dependency. Standalone Linux host-test marker
required; reject PX4/NuttX/board/lockstep/proxy definitions. CMake defaults OFF.
No networking, threading, publication, allocation, command or reset interface.

`State`: raw uint8 arming and nav values. Known arm values are exactly 1
(Disarmed) and 2 (Armed), from msg/versioned/VehicleStatus.msg. Nav is checked
against an explicit immutable caller-supplied uint32 allow-mask and must be <31.
The mask may contain only pinned non-reserved native values: exclude 9,11,16,
and 31. No implicit allowed-mode default. Raw Offboard is not research Selected.

`ReceiptKind`: Initial, Transition, Sample. A receipt carries producer lifetime,
clock epoch, sequence, transition epoch, producer timestamp, kind, before and
after state. Before/after are equal for Initial and Sample (canonical padding
has meaning); a Transition must change the pair.

`Observation` wraps the receipt with observer lifetime, receive time and an
explicit `clock_qualified` fact. These are trusted synthetic fixture facts in
this slice, never guidance-supplied authentication or clock proof.

Immutable `Config`: expected nonzero producer lifetime, observer lifetime,
clock epoch, explicit nonzero max_age_us and nonzero allowed-nav mask. Invalid config
starts faulted. max_age_us is a synthetic test parameter, not an approved live
state freshness budget. There is no default value granting readiness.

`ReceiptChecker::observe(Observation)` and `poll(observer_lifetime, clock_epoch,
now_us, clock_qualified)` return a value snapshot. The checker is noncopyable
and nonmovable. Snapshot exposes code, first retained fault reason, has_state,
last accepted receipt sequence/transition epoch, retained State, producer time,
latest checked receive/now time and age (valid only if has_state and clock and
ordering checks have succeeded). No accessor yields trusted adapter context,
request demand, diagnostic permission or a writable internal reference.

Codes: AwaitingInitial, Fresh, Stale, Faulted. Fresh describes structurally
consistent/fresh evidence, including Armed or any allowed raw nav state; it
does not mean healthy, disarmed, eligible or authorized. Fault reasons distinguish invalid
config, identity/clock mismatch, unqualified clock, invalid/unknown state or kind,
missing/repeated initial, receipt continuity, state/epoch contradiction,
time invalid/regression and counter exhaustion. First fault is retained forever;
subsequent observe/poll cannot change state, counters, time or fault reason.
No public reset, rebind or skip-ahead exists.

Validation order: retained fault; config (construction); identity and clock
qualification; nonzero/nonregressing local time; receipt identity/clock; known
kind/state; nonzero producer time <= receive time, nonregressing versus prior
accepted source time; initial/continuity; kind-specific state/epoch consistency.
Commit accepted state/counters/time only after all structural checks. A malformed
receipt faults without partially installing its fields. Fault snapshots mark
age invalid, with age zero, even if a prior state remains available for evidence.

Initial must be first, sequence=1, transition_epoch=1, before=after. A first
Transition/Sample faults MissingInitial. Thereafter sequence must be exactly
previous+1, checked before arithmetic; MAX exhaustion faults, never wraps.
Repeated Initial faults. Transition.before must equal retained state and
Transition.after must differ; transition epoch must be exactly previous+1
with checked overflow. Sample.before=Sample.after=retained and transition epoch
unchanged. Every receipt advances delivery sequence; only actual pair changes
advance transition epoch. These are source provenance counters, not adapter
state generation, operator event counters or admission replay floors.

Receive/now and producer timestamps may be equal at microsecond resolution;
regressions, zero and future producer time fault. With ordered same-clock
fixture facts, age=now-producer_time is overflow-safe; age <= max_age is Fresh,
otherwise Stale. Stale accepted receipts still consume delivery continuity.
poll never changes receipt sequence/transition epoch/state; it advances checked
local time and reevaluates age. Before Initial it returns AwaitingInitial.
A fresh subsequent valid Sample can leave Stale, but cannot authorize any
operator selection or restore an admission session because neither exists here.

## Finite acceptance tests

Independent tests must cover: invalid config and missing opt-in/build guards;
poll before first; valid initial/sample/transition chain including Armed then
Disarmed; all identity/clock mismatches and false clock qualification; unknown
arm/nav/kind; missing/repeated initial; seq zero/duplicate/regression/gap;
sample state/epoch contradiction; transition before mismatch, unchanged pair,
missing/extra epoch; zero/future/regressing source and local time; equal times;
freshness exact limit and one beyond; stale-to-fresh without authority; poll
non-mutation; sequence/epoch MAX boundaries; sticky fault and no partial commit.
Private host-test peer may seed counters only for overflow fixtures.

Test numerical values are explicitly synthetic. Fresh Debug, repeated Release,
ASan/UBSan and default-off/build-isolation checks, scoped formatting and master
plus independent review are required. Preserve C1B and C1A code/evidence.

Next integration inputs remain actual producer-hook completeness, ownership and
delivery synchronization, qualified clock lifetime, measured/test-profile state
freshness, estimator validity, operator-loss provenance and domain-bound live
diagnostic schema. No new human authority is needed for this standalone slice;
operational requirements and physical/controller releases remain separate gates.
