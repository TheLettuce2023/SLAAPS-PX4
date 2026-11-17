# C1E-W1 diagnostic payload verification

2026-09-24. **ACCEPTED for the isolated payload contract only.**
PX4 HEAD remains e880f1cd79e2453ef3dfaf68ee1c067b98ea9a53.
No native parser, owner connection, control path or output was enabled.

## Change and review

Added a separate diagnostic XML and strict standard-library Python request/status
codec under Tools/research_sitl/diagnostic_wire/. New private test IDs 200010/200011
preserve frozen Flight v1. The payload requires unavailable map, unselected state
and false eligibility; it cannot encode a successful/eligible request.
Transport-session binding is explicitly not admission-session authority.
The codec validates structure; it cannot establish authentic source/provenance.

Codex orchestration/source review: GPT-6; source tracing and implementation:
GPT-6 Luna; independent test implementation/source review: GPT-6 Sol;
clock/state mathematics and payload-contract review: GPT-6 Astra.
Astra approved the payload-only contract after clarifying real request versus
poll causes, binary32 rounding and the profile-specific generation invariant.
Independent Sol review found no remaining implementation blocker.

## Verification actually run

All Python execution used installed Ubuntu-24.04 WSL Python 3.12.3; no installs.

| Check | Result |
| --- | --- |
| New independent diagnostic suite | 8/8 unittest methods PASS, with field/boundary subtests |
| Existing Flight payload codec suite | 8/8 methods PASS |
| Existing status-vector checker | All nine historical vectors PASS using retained default-off executable |
| New diagnostic XML against checked-out MAVLink mavschema.xsd | PASS using installed lxml |
| Source fingerprints, graph references and whitespace | Checked in final handoff |

Commands from repository root inside WSL:

```sh
python3 -B Tools/research_sitl/diagnostic_wire/test_codec.py
python3 -B Tools/research_sitl/wire/test_codec.py
python3 -B Tools/research_sitl/wire/test_status_vectors.py /var/tmp/c1d_host_portability_UqqJjYfk/default-off/research_acceleration_wire_status_test
python3 -B -c 'from lxml import etree; import sys; etree.XMLSchema(etree.parse(sys.argv[1])).assertValid(etree.parse(sys.argv[2])); print(True)' src/modules/mavlink/mavlink/pymavlink/generator/mavschema.xsd Tools/research_sitl/diagnostic_wire/diagnostic.xml
```

The retained C++ executable SHA-256 is
55e2ffb236a7a6e653fc402c6f8e73018d4a3866000abb413ee2ee0bec9531cb.
This run checks existing vector compatibility; it is not a fresh C++ build or
new native PX4 acceptance. No C++ source changed, so no firmware/sanitizer
rebuild or C/C++ formatting was needed for W1.

Independent tests use their own wire field order, offsets and packed-byte
oracle, not the implementation's format constants. They cover all valid tail
lengths, each malformed field through encode and raw-byte decode, exact types,
unknown enums, zero-only eligibility/readiness/map, time/order equalities,
uint64 boundaries, binary32 rounding/signed-zero underflow/overflow/nonfinite
values, XML layout and Flight cross-decoding. Count is test methods, not a
claim that every possible byte string or concurrent execution was tested.

## Corrections and limitations retained

- Review caught XML reserved fields typed char[4]; corrected to the approved
  uint8_t[4]. A parent test run then found the independent oracle still expecting
  char[4] (two failing XML subtests); corrected to the contract and rerun PASS.
- The first status-vector command omitted its required executable argument and
  failed its usage check. The corrected retained-binary invocation passed.
- Windows Python was only a Store alias for the implementation worker; actual
  tests ran successfully in the existing WSL environment.
- Native DiagRequest with Disabled selection reports SelectionRequired, even
  when a preceding poll reports ClockMappingInvalid. This is now explicit in
  the design; W1 does not implement the native projection.
- Generated C/Python MAVLink conformance, CRC/signature/replay handling, status
  projection and runtime serialization remain unimplemented/unverified for C1E.
  XML/XSD and payload tests do not establish those properties.

## Artifact identity and next gate

- Tools/research_sitl/diagnostic_wire/codec.py: `a7e6e9a20653794cebc2bbcd0b5a5007e39f31664936ce76bf715e7cc61d7b9f`
- Tools/research_sitl/diagnostic_wire/diagnostic.xml: `d0cee5a90d65e5ff00092286b5cacfcd634011f4d15fde09f81d82c9c28690b5`
- Tools/research_sitl/diagnostic_wire/test_codec.py: `45bed70acb89946b21059127d4fabd6c38e0dfd02f5811f2267b6f2ee6b14cb3`
- Tools/research_sitl/diagnostic_wire/README.md: `7ab4dd2dffd0f78793a1b29235fbac6e56ce33f0ab735542d7d76b5b6e0e163f`

Next bounded code increment: **C1E-W2 generated C/Python conformance** for this
new dialect, including message IDs, layout/CRC, trimming and semantic negatives.
Reuse existing pinned-generation practices; do not change the Flight contract.
Then complete the C1E-N1 exact parser/candidate/disposition/owner API review,
bounded ownership and closure rules, build profile and full evidence projection
before native integration. No additional geometry is needed for that software.
Full C1, Phase 2, plant/control and physical validation gates remain open.
