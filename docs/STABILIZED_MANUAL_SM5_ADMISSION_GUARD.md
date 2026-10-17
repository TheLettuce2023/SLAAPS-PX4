# SM5 synthetic allocation report admission guard

2026-10-06, Codex. This is a bounded source-provenance correction to the
[generated synthetic SITL integration](STABILIZED_MANUAL_SM5_GENERATED_SITL.md),
not a new flight or vehicle qualification result.

The first header generator required the report to *claim* the expected
synthetic input SHA-256, but did not prove that its allocator values came from
that input. A report with a modified effectiveness entry and the original
`input_sha256` field would have passed its metadata check. The
[generator](../Tools/research_sitl/generate_stabilized_allocation_header.py)
now recompiles the exact source-hashed fixture input and compares the complete
report with the recomputed report, including numeric types, before emitting
C++. Its CLI also rejects duplicate JSON fields and nonfinite JSON constants.
It still rejects measured or qualified profiles; it does not admit vehicle
data.

Five [header tests](../Tools/research_sitl/test_generate_stabilized_allocation_header.py)
pass, including altered effectiveness, trim, command limit, slew, provenance,
shape/order and qualification flags. The
[source-bound verifier](../Tools/research_sitl/verify_stabilized_allocation_admission.py)
rehashed the accepted 31-case, offline allocation and two-case generated SITL
parent receipts; checked 387 current graph anchors; recompiled the synthetic
input to byte-identical JSON; and ran a CLI negative test in which a modified
effectiveness entry retained the original claimed input hash. The generator
returned rejection status 2 and created no header.

The strengthened generator emits header SHA-256
`8e36ef6717906ba0c161e16b4e0b18068479be520df1682f4e11d238a91b9def`,
byte-identical to the header used by the previously sealed PX4 binary
`59c8b61f53a98a45fd06f46365af9d132a41d8610cad5803286145d5e4f77527`.
No C++ input or runtime behavior changed, so the prior two SITL maneuvers were
not repeated. Their 12,953 audited cycles remain the exact observed scope.

New evidence is `/var/tmp/research-stabilized-allocation-ycippbfi`;
`admission-manifest.json` SHA-256 is
`b4e79b8a3bf6e5160b55bd52640945d919703279b7cadb6c1519675206e74ce2`.
All 14 files independently rehashed from Windows with zero mismatches. The
accepted 31-case binary and stock baseline remain unchanged. Actual SLAAPS
canard, loaded-servo, EDF, CG/inertia and airflow measurements are still
unavailable, so vehicle-specific allocation and tuning remain open.
