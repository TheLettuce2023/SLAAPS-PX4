# Software-contract evidence checker

`check_evidence.py` is a dependency-free, offline Python 3 checker for the first, **software_contract-only** evidence slice. It never starts a simulator, parses ULogs, scores flight metrics, or makes an integrated-SITL, hardware, control-authority, or flight-qualification claim. `integrated_sitl` and `hardware` profiles are rejected.

Run it against a self-contained evidence package:

```text
python3 -B Tools/research_sitl/evidence/check_evidence.py path/to/package/manifest.json --registry-sha256 <predeclared-lowercase-sha256>
```

It writes one JSON report to stdout. Exit `0` is recomputed `pass`, `2` is `fail`, and `3` is `incomplete`; parser/usage errors use `4`. The external `--registry-sha256` pin is required for a pass: its absence is incomplete and a malformed or mismatching value fails. A manifest's claimed `pass` is never trusted: the checker independently evaluates all immutable required cases. Fail has precedence over incomplete.

## Implemented contract

The manifest declares the checker/schema hashes and hashes a requirements registry. Its host-source list records each source file hash and, when dirty, hashes every declared dirty file. Paths are portable package-relative POSIX paths; absolute paths, `..`, backslashes, missing files, and symlink escapes are rejected. Raw input, raw output, and expected fixture artifacts must be declared, nonempty, complete, and match their SHA-256 and byte count.

The registry has a nonempty immutable `required_cases` set, non-operational `fixture_assertions`, exact first-slice exemptions, and separately listed unapproved `REQ_*` requirements. This slice supports only one independently checkable assertion: `exact_json_equal`, comparing strict JSON expected and observed artifacts. The expected and observed records must carry the declared case ID and raw-input SHA-256, so a stale or wrong fixture cannot be silently reused. It rejects duplicate keys, `NaN`, `Infinity`, overflowing numeric literals, unknown fields, unsupported versions, boolean-as-number fields, corrupt/truncated input, missing/unexecuted cases, duplicate IDs, and unapproved `REQ_*` criteria. A `not_run` required case is incomplete. Registry predeclaration is evidence of stated timing, not cryptographic proof that it existed before execution.

`manifest.schema.json` is the structural interchange schema, not a replacement for the checker. Cross-file hashes, unique IDs, safe paths, artifact roles, applicability and recomputed outcomes require the direct validator. An empty supplied case list is structurally allowed but cannot pass a nonempty required-case registry. Integer metadata must use integer JSON tokens, not booleans or floating-point tokens.

The pinned registry includes each criterion's `input_sha256` and `expected_sha256`, not just filenames. All declared artifacts in this profile are strict JSON, including artifacts for not-run or unresolved cases. Comparisons preserve JSON/Python value types, including distinguishing `true`, `1` and `1.0`. The checker is also the scorer for this limited exact-equality profile; there is no separate flight scorer.

Internal symlinks are allowed only when they resolve inside the selected package; escapes and same-file aliases across a criterion's artifact roles (including hard links) are rejected. Packages must remain immutable during checking. Concurrent malicious filesystem mutation, authenticity of source/observations, and historical run-ID uniqueness across packages are not established by this offline checker. The external registry pin must come from a separately retained pre-execution record, not be freshly calculated from an untrusted submitted package.

Run the fixture regressions with `python3 -B -m unittest discover -s Tools/research_sitl/evidence -p test_check_evidence.py -v`. They create disposable temporary packages; no vehicle or simulator is exercised. Schema interoperability additionally runs when the optional, already-installed `jsonschema` package is available; it is not a runtime dependency.

## Limits and integrity

SHA-256 and an externally retained registry pin detect an expectation rewrite only after a trusted digest was captured separately; neither proves execution or that an authorized person preapproved it. Hashes do not authenticate an author, timestamp, host, or prior declaration. Host-source dirty attribution is a declared, file-hash-based record, not a Git-status verifier. A software-evidence pass is not an S1/controller pass and does not establish authentic provenance, hardware availability, controller behavior, simulator fidelity, or flight safety. Those require later reviewed registry, runner, log/parser, scorer, and operational evidence work.
