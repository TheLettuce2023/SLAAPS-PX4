# Stock Cessna diagnostic limits revision 2 approval

Recorded 2026-09-28T00:03:37Z (2026-09-27 local project date). The project user
explicitly approved revision 2 as written in this conversation. The approval
identifies the project-user role; it is not an independently signed identity.

Approved proposal: [revision 2](SITL_BASELINE_DIAGNOSTIC_LIMITS_V2_PROPOSAL.md),
SHA-256 `9c1549b215808dabfbe779c9171f46a426a2ea87e13145fbe5fcdd9aa45b21e6`.
Frozen registry:
[`diagnostic_limits_v2.json`](../Tools/research_sitl/baseline/diagnostic_limits_v2.json),
SHA-256 `d295bcb9a43fae1dec4eb147539a9ea1f980f087a84337f37d749db39239e52d`.
Future campaign: `slaaps-stock-cessna-phase1-acceptance-02`.

Revision 2 inherits every revision-1 criterion and adds only the approved
all-zero synthetic differential-pressure capture-time exception. Its source
verification binds the stock publisher bytes to SHA-256
`dec9c7bc16d9d01edb7cd0b392a0a03135649c7f0d8dbd9a6b43c50360c5811f`.
The exception requires the recorded stock publisher and device 1377548;
publication order, the 2.5 s logged-gap limit and finite pressure still apply.
Mixed zero/nonzero capture times do not qualify. The zero-sample count must
be reported, and no raw-sensor capture-time claim may be made.

Revision 1 and its approval remain unchanged. The user asked to complete
first-operator evidence and defer the second person for this checkpoint;
that direction does not establish second-operator reproducibility or close
the full Phase 1 gate. Both revisions remain diagnostic-only criteria.
