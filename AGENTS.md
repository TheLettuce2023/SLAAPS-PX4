# PX4-Autopilot

Safety-critical C/C++ flight control firmware for autopilots, plus SITL
simulation and Python tooling.

- **Commits:** use the `$commit` skill. Conventional commit format with
  topic-based scope: `type(scope): description`.
- **Pull requests:** use the `$pr` skill.
- **Attribution:** use the actual assistant identity, never Claude's when
  running another client. No generated-by footer in PR bodies.
- **Style:** run `make format` on changed C/C++ before committing; CI
  enforces `make check_format`.
- **Scoped guidance:** before editing or reviewing, read only the
  `.github/instructions/*.instructions.md` files whose `applyTo` patterns
  match the affected paths, plus any nested `AGENTS.md` along those paths.
- **Research context:** start with `docs/PROJECT_CONTEXT.md`; use its task
  index and `docs/RESEARCH_CODE_GRAPH.json` before broad searches. Revalidate
  changed graph anchors, including untracked files; avoid rereading unchanged
  context. Update the compact handoff and affected graph when milestones change.
- **Handoff:** end successful turns with one sentence of codebase state and
  explicitly state whether clearing history or starting a fresh chat is safe.

## Delivery priorities

- **Default to implementation.** For coding requests, deliver working code and
  appropriate verification. Keep planning brief and proceed with authorized
  work; a design document, interface, or test-count increase alone does not
  complete an integration task. Honor explicit requests for research or review.
- **Work in bounded increments.** Identify the observable behavior to deliver,
  the affected production path, acceptance checks, and stopping point. Prefer
  completing one usable path through existing components before adding more
  abstractions. Report exactly which integration boundary remains incomplete.
- **Keep architecture proportional.** Reuse existing PX4 and project mechanisms.
  Add a layer, protocol, framework, or document only when the current task needs
  it; explain the concrete requirement it satisfies. Do not build speculative
  future capabilities or repeatedly redesign settled interfaces.
- **Close integration gaps.** Prioritize connecting implemented components to
  their production callers and consumers. Test real integration boundaries when
  feasible; isolated helper tests do not establish production behavior.
- **Handle blockers precisely.** Separate missing vehicle measurements or human
  decisions from independent software work. Continue the latter within scope.
  State the exact dependency once; do not repeatedly audit an unchanged blocker
  or expand into unrelated infrastructure to remain busy.
- **Preserve safety requirements.** Keep ownership, expiry, stale-data,
  publisher-replacement, partial-failure, and admission protections. Use clearly
  labeled synthetic fixtures for software tests; never replace missing measured
  data or recovery policy with invented flight defaults. Implementation-first
  work does not authorize flashing, hardware operation, or flight admission.

## Efficient verification and review

- During implementation, use incremental builds and focused regression tests.
  At integration checkpoints, run the relevant host matrix and native scenarios.
  Run current board/resource checks when production changes affect target
  compatibility or capacity, and before deployment claims. Required repository
  checks still apply; do not rerun unchanged suites without a concrete reason.
- Keep reusable development builds separate from immutable qualification
  artifacts. Reuse documented build commands and existing verifiers. Prefer
  automating recurring checks in existing CI over manual agent supervision when
  validation tooling is in scope.
- Track flash/RAM impact for embedded changes and qualify timing/stack behavior
  where relevant. Old board evidence does not prove current fit or readiness.
- Keep changes and commits bounded and independently reviewable. Avoid combining
  unrelated implementation, tooling, and historical documentation into large
  checkpoints. Follow the commit skill when a commit is requested or authorized.
- Report delivered behavior, checks actually run, and remaining limitations.
  Distinguish recorded prior evidence from fresh verification. Measure progress
  by completed capabilities and resolved defects, not lines or assertion counts.

## Keep context small and current

- Read the current handoff and relevant task entries first; query only needed
  graph nodes and source ranges. Do not load the entire historical graph or
  ledger for routine coding work.
- Keep the active handoff approximately 100-150 lines, with the current goal,
  implemented/missing behavior, latest evidence links, blockers, and next three
  tasks. Put navigation near the top and remove superseded priority labels.
- When updating a milestone, replace superseded current-state prose and move
  history to a linked archive without losing evidence. Do not append another
  full milestone narrative to the startup context. Do not undertake a historical
  cleanup campaign as a prerequisite to unrelated code changes.
- Keep detailed results in one evidence record and link to it from the handoff;
  use the graph for dependencies and freshness. Update only affected records,
  avoiding duplicate narratives and unchanged hash sweeps.
