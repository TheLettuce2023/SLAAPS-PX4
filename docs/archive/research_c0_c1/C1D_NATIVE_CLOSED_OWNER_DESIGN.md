# C1D-N: native closed diagnostic owner design

2026-09-20. C1D-N1 implementation and host verification are ACCEPTED by master
and independent review; see C1D_NATIVE_OWNER_VERIFICATION.md. C1C-S2 passed.
This is within the approved
disarmed/output-disabled C1 scope; it does not authorize flight mode, outputs,
hardware, operational timing limits or positive live request validation.

## Intended next useful increment

Attach the existing real diagnostic WireAdapter to one Commander-owned native
owner, initially exposing only source observation/poll and lifecycle closure.
Do not build another observation-only probe, generic callback executor or
replacement validator. Preserve the accepted S2 capture experiment separately.
One adapter owns one guard; no externally supplied trusted-state/authentication
Boolean or fixture-state ABI enters this native owner.

Source findings at pinned PX4 revision e880f1cd79e2453ef3dfaf68ee1c067b98ea9a53:

- HarnessSupervisorOwner.hpp requires caller serialization; its plain reentry
  flag is not cross-thread exclusion. Do not promote that fixture owner to PX4.
- DiagnosticAdapter.cpp commits state and admission history during a turn.
  Detecting a concurrent source change after a turn cannot undo that history.
- The reviewed Commander arm/nav assignment sites and loop boundary provide
  an execution context in which those source changes cannot interleave with
  a synchronous, bounded owner turn. Source review remains required; a queued
  receipt or unchanged pair alone cannot prove this property.
- WireAdapter::structurallyValid permits an invalid map when context validity
  agrees and controller identity matches. Real diagnostic polling can therefore
  remain honestly closed without inventing estimator or map readiness.

These are source facts/inferences, not measured native-owner behavior.

## Build and API review requirements

The released declarations/build-isolation requirements are:

1. A distinct native Linux POSIX SITL diagnostic profile and construction tag.
   Preserve offline and ordinary builds. Reject mixed profiles, host-marker
   spoofing, hardware, lockstep and timestamp proxies. Derive shared capability
   only from a validated profile; do not expose an independent enable-all macro.
   All translation units sharing class definitions use the same profile.
2. Reuse diagnostic guard/adapter predicates and wrong-domain protections.
   Native construction must not pass DiagnosticOfflineTag internally. Ordinary
   construction remains Flight. Do not add the standalone library CMake project
   to PX4 or use the host ROS2 include shim in a native build.
3. Native identity/binding/ingress labels must distinguish native run lifetime
   from fixture_run_id. Preserve existing offline APIs/evidence. No native
   result may serialize as research-c1b-offline/v1 or frozen v1 Flight status.
4. Native-only private friendship for the one native owner; no offline fixture
   owner or test-peer access in its PX4 build. No public adapter reference,
   reset, operator attach, selection, grants, packet ingress or demand accessor
   in this first owner's integration interface.
5. Truthful native lifecycle reasons (owner stop and source-continuity loss),
   without pretending an operator existed or disconnected. Owner stop is
   permanent for its lifetime even if shared adapter closure is recoverable
   for the separately defined offline operator-detach workflow.

## Source ownership and closed-state semantics

Use a separate opt-in private Commander overlay, not permanent stock edits.
Bootstrap immutable nonzero identities/configuration before construction.
Commander alone owns and calls this native instance, from its actual Initial,
each arm/raw-nav Transition, periodic Sample and final Stop sites. Calls are
synchronous and bounded: no networking, parser, sink I/O or foreign callbacks
inside the source turn. Do not add another execution thread.

Retain actual raw arm/nav and native hrt chronology, checked source counters,
and distinct adapter physical generation. Initial generation is one; each
committed arm/raw-nav change advances it exactly once, even if research mode
mapping stays NotSelected. A sample does not increment generation merely
because time advanced. Arm-to-disarm or nav-away-and-back must not collapse.
Unknown state, source contradiction, time regression, exhaustion or lifetime
loss must be rejected BEFORE any ordinary adapter turn: the adapter can install
its physical sample before its guard notices a time regression. Close with
nullopt, preserve the actual last-valid reference, and record rejected raw input
separately. Required terminal/session/report closure bookkeeping may change;
that is not permission to install the malformed observation.

Research Selected is never inferred from raw Offboard. Only arm values 1/2 and
the pinned nonreserved nav set are recognized. In this configuration every
known native nav maps to research NotSelected; unknown raw values permanently
fault the source. Retain raw nav in evidence. Required estimator validity, transport connection,
clock-map validity and map.valid remain false; operator unattached, selection
Disabled, no successful-session history and ACK absent. Initial session gate is
NoTrustedSession; a normal Stop can truthfully leave EstablishmentRequired.
Validator time is actual native hrt,
not a claim that a guidance clock map exists.

Use explicit profile `c1d-closed-native/v1`, borrowing only the synthetic
configuration in profile/TimingProfileTest.cpp and live/HostOwner.cpp: protocol
v1, ValidateOnly, per-axis bounds [-10,10] m/s2, norm bound 12 m/s2, validity
80,000/80,000 us, hard age 60,000 us, future skew 8,000 us, uncertainty ceiling
4,000 us and quaternion tolerance 0.01. Source/controller/owner/run/clock
identities are immutable nonzero per-run bootstrap inputs. The numeric values
are construction scaffolding for closed polling, NOT selected vehicle bounds,
operational defaults, actual map uncertainty or freshness qualification. Name
and bind every value in evidence; later ingress cannot silently inherit them.
Expected real poll rejection is
ClockMappingInvalid under the existing prerequisite order; do not alter that
order to produce a more attractive status. No request can become eligible.

Actual invalid map fields are zero except controller_instance_id, which must
match the owner. Missing map/estimator/transport is not itself a source fault.
Normal OwnerStop closes with the valid current receipt and permanently stops
the owner. SourceContinuityLost must terminalize with no replacement receipt.
After either outcome, repeated events return retained final evidence without
advancing adapter reports or source counters.

Host tests use a distinct native-contract test profile, not fake PX4 defines:
RESEARCH_SITL_DIAGNOSTIC_NATIVE_CONTRACT_TEST=1, with explicit synthetic-native
execution origin. Actual native compilation instead uses
RESEARCH_SITL_DIAGNOSTIC_NATIVE=1 and RESEARCH_C1D_CLOSED_OWNER_TARGET=1,
with native POSIX execution origin. Both are mutually exclusive with existing
offline macros; all mixed pairs and caller-defined capability bypasses fail.
Native identity/binding/ingress counterparts use native_run_id plus compiled
origin; internal aliases/accessors reuse algorithms while preserving offline
field names, layouts and schema. Native construction is private to the owner;
host-only test peers do not enter the actual PX4 profile.

This serialization addresses the actual arm/nav source. It does not magically
serialize estimator publishers, operator/transport loss or map replacement.
Their later ownership, freshness and invalidation mechanisms need review before
positive validation. A completed result is historical, never continuing consent.

## Verification required before release/acceptance

First review exact API/profile/lifecycle changes and independent tests. Then:

- Preserve ordinary and complete finite offline C1B regression behavior; fresh
  Debug, repeated Release, sanitizers and build-isolation probes.
- Test real adapter poll and truthful stop/fault closure, every mutation seam,
  source/generation distinction, ABA transitions, malformed chronology, counter
  exhaustion, permanent closure and no eligible/session/operator history.
  Synthetic source transitions stay explicitly synthetic and never arm PX4.
- Verify native configuration, source/overlay identity and one-owner construction
  before an isolated build. No estimator, MAVLink receiver, controller, allocator,
  output driver or Gazebo is required for this first closed-only increment.
- Two fresh disarmed native runs with complete source-bound reports, actual
  adapter results and final closure reconciliation; independent semantic checks
  and corruption tests. Export only outside source execution and after stop.
  A missing/blocked sink cannot block source closure or turn partial data into
  passing evidence.

Acceptance would establish native execution of the real closed adapter only.
Signed ingress, live diagnostic schema/status, independent operator-loss
detection, qualified guidance clocks and actual estimator validity remain
necessary for full C1. None of this verifies acceleration tracking, allocation,
plant feasibility or Phase 2 operational signoff.

## C1D-N1 exact implementation release

N1 promotes the shared diagnostic build domain and implements the portable
closed owner with explicitly synthetic native-contract host tests. N2 will
attach it to actual Commander only after native build, serialization, archive
lifetime and post-stop export review. N1 is real guard/adapter reuse, not a
replacement observation probe, but is not native-runtime acceptance.

Add DiagnosticBuildProfile.hpp before conditional declarations. Three mutually
exclusive profiles preserve ordinary Flight and the existing offline profile;
native-contract HOST and actual native POSIX have distinct compiled execution
origins. Mixed, zero/orphan and externally predefined capability markers fail.
Native construction uses its own private tag; never pass DiagnosticOfflineTag
internally. Preserve existing offline identity/binding/ingress layout, names and
schema. Native counterparts use native_run_id and compiled origin; shared
private aliases/accessors reuse the algorithms. Actual native builds exclude
all fixture/test-peer friendships, including existing ordinary adapter peers.

NativeClosedDiagnosticOwner is final, noncopyable/nonmovable, with one by-value
WireAdapter. Construction and initial/transition/sample/stop/sourceContinuityLost are private to
NativeClosedOwnerSourceBridge; a narrow test peer exists only in the explicit
native-contract host profile. No adapter reference, demand, generic turn,
operator, grant, reset, packet, callback or mutable authority API is public.

RawState retains uint8 arm/nav. NativeSourceObservation contains kind
(Initial/Transition/Sample/Stop), before/after and source_time_us. Source snapshot
contains initialized, retained raw state, sequence, transition_epoch,
physical_generation, committed_time_us and checked_time_us. These are distinct
from adapter report sequence/transition epoch. Immutable configuration binds
the named profile, permitted source and owner/run/controller/producer/clock IDs.

Initial sets source sequence/epoch/physical generation to one. Changed Transition
increments all three; Sample increments only sequence. Both poll the real
adapter. Equal nonzero times are allowed; zero/regression closes. Invoked valid
unchanged assignments validate before/after and time, advance checked_time only,
and consume no sequence, generation, poll or record. Initial100 -> unchanged200
-> Sample150 must fail. No claim is made that unchanged assignments are all
individually archived; checked and last committed times remain separate.

Compute each candidate locally, validate and reserve evidence before adapter
mutation, and commit candidate only after a nonterminal available actual result.
Expected Rejected/ClockMappingInvalid is a valid closed observation. Terminal,
NotConfigured or WrongDomain results go directly into final evidence with the
candidate uncommitted, never reconstructed and never followed by a second
closure turn. Initial invalid configuration also retains the actual nullopt
closure result, including a truthful NotConfigured return.

Retain 256 normal NativeSourceRecords (observation, before/after source snapshot,
actual adapter input and full DiagnosticResult) plus one independently reserved
NativeClosureRecord (first fault, optional attempted raw observation, retained
source, optional actual adapter input and full actual DiagnosticResult). Reserve
before mutation. Record257 closes without overwriting the prefix or installing
the attempted state. Use native OwnerEvidenceExhausted/nullopt for storage loss,
SourceContinuityLost/nullopt for malformed source/counters, and OwnerStop with
the valid current receipt for normal stop. Stop requires initialized source,
exact retained pair and valid chronology, consumes no source sequence/normal
slot, and succeeds at 255 or 256 records. Invalid Stop is source loss. The first
closure is immutable; repeated calls cannot advance any state or reports.
The private sourceContinuityLost route records SourceLifetimeLost with absent
attempted raw observation and actual SourceContinuityLost/nullopt closure. It
does not fabricate a malformed physical state to represent bootstrap loss.
Later cross-thread bridge invalidation may only set a sticky atomic flag; only
the serialized Commander source path may invoke this owner route.

Public summary is a compact value; indexed copyRecord and copyClosure succeed
only after closure and leave output sentinels unchanged on failure. These are
quiescent inspection APIs, not concurrent reads. No borrowed mutable storage.
The owner contains its fixed array, has no runtime allocation or I/O, and must
fit a compile-time 2MiB maximum. That ceiling is a host-diagnostic memory bound,
not a hardware or Commander-stack budget. Host tests allocate owner on the heap
and report exact sizeof values. N2 must retain storage past Commander destruction
without putting this array on its task stack or blocking source turns on export.

Implementation ownership: shared library profiles/guard/adapter and new owner;
independent new owner/isolation tests; master standalone CMake and documentation.
Existing offline tests, serializers, supervisor fixtures, S2, stock Commander,
Cessna and geometry are not edited. Fresh ordinary/offline/native-contract
Debug, repeated Release, sanitizers and default-OFF checks, source preservation
and independent code review are the exact gate to N1 acceptance. No new human
design-direction decision is required for this closed-only increment.

## C1D-N2 implementation release (2026-09-21)

Master and independent review approved the closed-only integration design and
subsequent compile-only release. New code is confined to
Tools/research_sitl/px4_attachment/c1d_closed. Stock Commander and accepted S2
remain unchanged: exactly three pinned Commander files are copied into a
private source view and patched with zero fuzz. No runtime control/output,
estimator, MAVLink receiver or Gazebo module is selected.

Bootstrap release-publishes one immutable heap owner; one acquire/CAS claims
it. Source hooks run synchronously on the actual Commander task using native
hrt. Normal closure occurs after buzzer_deinit and before run returns, then
release-publishes completion. Destructor only publishes retirement/abandonment.
All archive storage remains process-lifetime retained even after forced stop.
Control commands serialize separately; source/destructor never take that lock.
Foreign invalidation is a sticky atomic fault, not an owner callback.

The export command itself calls commander_main(stop), retaining its real return.
It requires normal completion and retirement before copying any owner data or
opening a sink. Repeated read-only exports do not consume/reset evidence.
Actual lifecycle values, full results and final summary use the distinct schema
in C1D_NATIVE_EVIDENCE_SCHEMA.md. Export has no request/authority interface.

Independent review found initialization-only stack sampling insufficient.
The implementation now checks a current source-stack marker before every owner
call, including fault closure. Guard size is conservatively subtracted from the
reported stack interval. Failure disables all deep owner calls and export;
even an attempted fault closure cannot bypass failed stack qualification.
The retained marker is the minimum observed offset, not an exact stack pointer.

Provisional diagnostic-only runtime criterion: native compiler/disassembly
review must bound the entire deeper source call chain plus marker-to-SP gap
within 32,768 bytes; each measured hook needs at least 49,152 bytes available.
The additional 16,384 bytes are engineering margin, not certified worst-case
stack usage or a hardware requirement. Runtime release still needs independent
review of actual native artifacts. No task stack increase is authorized by this
criterion. The runner defaults to compile-only and refuses runtime without a
source/executable/stack-bound accepted review record.

N2 acceptance completed on 2026-09-21: actual native compilation and 27 isolation
probes, native stack review, two fresh disarmed runs, actual C++/Python trace
agreement, corruption/preservation checks and independent final review pass.
See C1D_NATIVE_INTEGRATION_VERIFICATION.md. No live ingress, positive validation,
operational authority, control or full Phase 2 acceptance follows from this gate.
