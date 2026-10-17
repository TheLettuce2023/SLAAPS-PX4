# Research PX4 project: coding-agent handoff

Follow root AGENTS.md and the user's current request. Attribute work to the
actual assistant/model, not a previous agent. Existing files and uncommitted
changes belong to the user; inspect git status before work and preserve them.
Before reviewing/editing a path, read only matching applyTo instruction files
under .github/instructions and nested AGENTS.md files. Do not load every scope.

For development, start with docs/PROJECT_CONTEXT.md and its task-specific index.
Use docs/RESEARCH_CODE_GRAPH.json for source anchors and freshness checks; read
only the relevant roadmap phase and current increment in PHASE_GATE_STATUS.md.
Historical gates are not current authorization. C1E-W2 offline generated C/Python
wire conformance now passes; see docs/C1E_GENERATED_WIRE_CONFORMANCE.md.
Next is the exact C1E-N1 native parser/owner API and serialization review.
C1D-N2 remains the latest native acceptance. Full C1 and Phase 2 remain open;
no active flight controller is released. Use Astra for math-intensive work and
smaller models for scoped coding/testing under the primary orchestrator.

Research only; no targeting/interception. Companion requests are inertial
acceleration at CG in rotating body FRD, held body-fixed between updates; NED
navigation. Four independent X-canard servos, fixed rear EDF, no main wing or
thrust vectoring. PX4 owns estimation, stabilization, allocation and command
loss. Never invent physical values or substitute simulator truth for estimates.
Keep one owner per command stage and explicit normalized allocator semantics.

Tools/simulation/gz is a separate Git repository; preserve stock gz_rc_cessna.
No commit, push, PR, package installation, flashing or physical test without
explicit user authorization. No broad cleanup or destructive Git operations.
Use existing Ubuntu-24.04 WSL tools and isolated native build/cache paths for
PX4 tests; do not run concurrent simulator builds or reuse shared mount caches.
Accepted native artifacts are under /var/tmp/c1d_px4_58c0t5nm in that distro;
artifact absence after cleanup is not evidence of a passing rerun.

Work in bounded increments. Separate facts, measurements, assumptions and
inferences; report tests actually run and exact remaining gate. Keep updates
brief, batch related checks, and avoid repeatedly loading unchanged documents.
Do not claim independent review from another pass by the same agent. Request
review when a gate requires it. Do not run OpenCode with --auto for this project.
