# C1: output-disabled PX4 attachment design

2026-09-20. User approval: the latest "Proceed" releases the scope in
C1_PX4_ATTACHMENT_PROPOSAL.md. Master source review releases **C1A passive
state/clock/build attachment only** below. C1 overall remains open. This is
not flight-mode, estimator, plant, command admission or tracking acceptance.

## Source-backed findings

- Root CMakeLists.txt:224-227,452-459 provides EXTERNAL_MODULES_LOCATION.
  A parent-owned external module avoids changing the stock MAVLink dialect,
  board default, startup and separate Gazebo repository.
- cmake/px4_config.cmake:51,105-117 permits an explicit config file with
  vendor/model/label/board directory. cmake/kconfig.cmake:74 uses the default
  label branch without merging stock modules; BOARD_NOLOCKSTEP disables
  lockstep. Build output must independently verify the resulting config.
- platforms/posix/CMakeLists.txt:154 writes source .vscode/launch.json.
  A private source view must isolate that path as well as build output.
- Existing live/CMakeLists.txt and research_acceleration/CMakeLists.txt are
  standalone-only. Do not add_subdirectory either into PX4. Future ingress
  must explicitly reuse reviewed portable sources, excluding HostOwner.cpp's
  simulated-state ABI, and retain exactly one admission owner.
- RequestGuard.cpp checks required state and research selection before
  ValidateOnly disarmed handling. An absent estimator and NotSelected mode
  must not be converted to ValidatedDisarmed by inventing trusted flags.
- Commander.cpp:2092-2118 processes command and action inputs before its
  status publication at 2135-2150. arm()/disarm() change state before that
  publication. A topic generation counter proves publication continuity,
  not lossless internal state-transition observation. A reviewed producer
  transition hook is still needed for the stronger owner contract.

These are observations of pinned local source, not measured runtime behavior.

## C1A implementation release

Allowed new paths: Tools/research_sitl/px4_attachment/** and this document.
Master owns build/runner and documentation; bounded worker owns the external
probe source and CMake only. Existing source, baseline, libraries and geometry
remain untouched. Builds are serial and no package installation is allowed.

Use a dedicated Linux-native /var/tmp directory with a source view of existing
top-level entries via symlinks. The view has private .vscode and build entries;
it does not relocate/copy the checkout, use the old cache, or create mounts.
Record source identity and explicit build/cache/runtime paths. Do not alter
global Git configuration or use broad ownership exceptions. Each run owns its
PX4 instance, runtime directory, process and log; no existing process is killed.

An explicit minimal POSIX SITL config builds Commander and diagnostic commands
plus the external passive probe. No MAVLink receiver, Gazebo bridge, estimator,
flight controller, allocator, output driver or actuator-test module is selected.
The custom startup runs only the inspected services and probe, not stock rcS.
No arming, Offboard command or vehicle_command publication is permitted.

The probe subscribes to actual vehicle_status and actuator_armed, samples
PX4 hrt and host CLOCK_MONOTONIC, logs observations and exits after a bounded
window. It has no research/control publisher; PX4 logging may publish diagnostic
log_message. It does not construct an admission owner, claim
a valid clock map, establish a session, open a socket, or produce demand.
The runner brackets the child process lifetime, records exact executable and
input hashes, retains nonzero/missing evidence as failure/incomplete, and uses
normal owned-process shutdown. Host monotonic and PX4 hrt remain separately
labelled; apparent numerical agreement alone is not synchronization proof.

## Predeclared C1A checks

1. Fresh isolated configure/build succeeds using installed tools only. Inspect
   boardconfig and linked builtins for forbidden output/control/transport modules.
2. A bounded no-Gazebo run observes actual Commander disarmed publications;
   absence, unknown state, armed state or missing completion fails the smoke test.
   Missing estimator validity is expected and keeps admission closed.
3. Host clock brackets each native hrt sample; samples must be finite/nonzero
   and nondecreasing. Report observed deltas; no operational deadline, drift or
   qualified map follows from this diagnostic.
4. Admission and the research network are not instantiated; this is not a test
   of request rejection or eligibility. No research control/output publisher
   is added. Source/module/startup inspection, a strict linked-builtins allowlist
   and a runtime advertised-topic snapshot are required. The snapshot is not
   proof of every instant of execution. Source/module/startup inspection and
   runtime evidence are both required; compilation alone is insufficient.
5. Repeat from a fresh process/runtime, retain both outcomes, and verify the
   original source hashes, stock Cessna and separate Gazebo repository.

Verification refinement before the final clean-area run: independently
recompute the native uint64 conversion bound, microsecond bracket, first
completion sample and state-row ordering instead of trusting probe flags.
Reject unresolved parameter lookup/type errors. Six existing metadata YAMLs
are explicitly registered for Commander dependencies; none enables its owning
runtime module. Earlier defective configurations and observations are retained.

## Still required for full C1

The signed request -> actual parser -> single admission owner -> authenticated
status path must be attached inside PX4, with independent local operator
capability and fresh ephemeral identities/keys. Do not claim this from C1A.
The separate C1H host evidence is not PX4 evidence.

Before that implementation, review lossless Commander transition provenance,
separate producer/validator clocks and map lifetime proof, state freshness,
real research-selection semantics, and allowed missing-state behavior. Retain
malformed/foreign/replay/stale/unknown clock-state/restart/disconnect/competing
owner checks from the approved C1 proposal. True disarmed validation, when
legitimately reachable, must report ValidatedDisarmed with eligible=false;
it is never Accepted, never an active request, and never a control publication.

No geometry input is needed for this interface work. Full Phase 2 operational
budgets and later physical/control gates remain distinct open requirements.

## Selection semantics decision for the positive-validation branch

The existing contract associates Selected with research mode selection; the
approved operator console cannot select a flight mode. In current PX4 neither
running this probe nor receiving an authenticated packet is research selection.
The passive slice therefore does not manufacture Selected or required-state
validity. Guard/source behavior stays unchanged.

The user has approved design of the separate SITL-only diagnostic state.
See C1B_SITL_DIAGNOSTIC_SELECTION_DESIGN.md and its test specification for the
accepted architecture: immutable diagnostic domain, independent selection,
honest NotSelected/Disarmed evidence and no eligible cache. The existing
flight-mode Selected flag and all estimator/clock requirements remain intact.
This is design-only acceptance, not implementation or positive live validation.
The subsequent C1B-I exact offline API/test review now passes; see
C1B_OFFLINE_API_CONTRACT.md. Bounded offline implementation and its fresh test
evidence are next. No live diagnostic schema or PX4 integration is released;
live provenance gates and full C1 acceptance remain required.
