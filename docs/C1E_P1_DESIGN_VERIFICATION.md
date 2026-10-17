# P1 design and derivative plant packaging verification

2026-09-27, Codex (GPT-6). Documentation and packaging checkpoint only.
Independent requirements review passed later on 2026-09-27 (see below).
Positive-runtime implementation and acceptance remain open.

- Read root guidance, compact task index, applicable source instructions and
  relevant graph edges. Verified all 51 pre-existing graph anchor hashes,
  including untracked research sources; none differed.
- Inspected the native negative owner, diagnostic adapter, checked clock-map
  conversion, POSIX hrt, selected estimator publishers and message schemas.
  Findings and proposed release gates are in
  [P1 design](C1E_POSITIVE_ADMISSION_DESIGN.md). No firmware source changed.
- All four supplied geometry input hashes match the original G1 record, before
  and after packaging. All three derivative meshes are byte-identical.
- Parsed original/derivative SDF elements are identical after normalizing the
  model name and six URI prefixes. Physical values were not corrected.
- The existing read-only intake audit resolves all six derivative mesh URIs and
  reports exactly one error: base inertia violates physical realizability.
  Declared link mass remains 3.291337 kg versus provisional 3.041337 kg all-up;
  mass scope is a separate acceptance blocker, not an automated checker error.
- `gz sdf -k models/research_vehicle/model.sdf` independently reports Error
  Code 19, invalid base_link inertia. The exact output and exit code are retained
  in the package, with SHA-256 hashes in its provenance manifest.

Artifacts: [package README](../models/research_vehicle/README.md),
[manifest](../models/research_vehicle/provenance.json),
[intake report](../models/research_vehicle/intake_report.json),
[SDFormat result](../models/research_vehicle/sdformat_validation.txt).

No new tests or broad regression runs were needed for these documentation and
asset-layout changes. Structural equivalence, source/mesh hashes, the existing
intake checker and independent SDFormat validation verify the changed scope.
No simulator/baseline/native run, commit, publish or physical operation occurred.
N1 runtime and baseline revision 2 remain unchanged. Second operator is deferred.

Next at the original checkpoint: independently review P1 and specify/implement the closed provenance
collector under the documented gates. Original CAD mass/CG/inertia evidence,
source-hook completeness, qualified clock provider and measured lifecycle/state
budgets remain open. Physical acceptance and positive runtime are not claimed.

## Independent requirements review, 2026-09-27

The user explicitly requested a separate review agent. `provenance_review`
independently inspected P1, the P2B source seam and collector, source evidence
binding and the native clock candidate. It verified 15 relevant pre-existing
graph hashes including untracked sources. P1 is accepted as a requirements
checkpoint; this closes only P1's independent review gate.

The reviewer found three concrete native evidence-checker gaps: absent archive
files could escape validation, an empty overlay map could bypass source checks,
and executed startup lacked source-hash binding. All three were corrected and
four focused checker tests passed independently. It also caught the absolute
MODULE_CONFIG path incompatibility; the isolated module now computes the
required relative path. Native capture acceptance is recorded separately in
the P2B verification record once complete. Full P2/P3/P4/P5 remain open.
