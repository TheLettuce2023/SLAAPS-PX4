# C1E-W2 generated diagnostic wire conformance

2026-09-24. **ACCEPTED: offline generated payload/frame conformance only.**
PX4 HEAD: e880f1cd79e2453ef3dfaf68ee1c067b98ea9a53.
W1 codec/XML/tests, frozen Flight wire and native owner remain unchanged.

## Implemented scope and review

Added diagnostic_wire/test_generated_mavlink.py and GeneratedMavlinkFixture.cpp.
The runner requires clean pinned MAVLink/pymavlink, validates XML against XSD
and independent field/ID/type/offset tables, checks collisions, generates C and
Python in a unique temporary directory, and verifies import origins.
Compiled C offsets/types/constants and both languages' complete frames must
agree with an independent X25/CRC-extra/byte oracle.

Pinned MAVLink: 1b1e896aa3be0d591166e086e6b9d8613bd03716.
Pinned pymavlink: 9e9a991731cc209ef9fc04006ff11721f0695726.
CRC extras: RESEARCH_DIAGNOSTIC_REQUEST=187; RESEARCH_DIAGNOSTIC_STATUS=159.
Generated base MIN_LEN remains 108/136, distinct from W1 logical minima 103/132.

GPT-6 Sol implemented; GPT-6 Luna independently reviewed source and found no
remaining blocker; Codex parent reviewed, ran optimized/sanitizer verification,
and integrated the handoff. No extra high-math assignment was needed for this
established wire-conformance workflow.

## Verification actually run

Ubuntu-24.04 WSL: Python 3.12.3, g++ 13.3.0, installed lxml 6.1.3.
No installation, simulator/PX4 firmware build, endpoint or actuator operation.

| Run | Result | Retained directory containing report.json |
| --- | --- | --- |
| Final Debug | PASS | /var/tmp/c1e_w2_mavlink_1rt7zfws |
| Optimized (-O2) | PASS | /var/tmp/c1e_w2_mavlink_zo5l2buz |
| ASan + UBSan | PASS | /var/tmp/c1e_w2_mavlink_8ouk91k2 |

Each run reports the same finite cases:

- 53 roundtrips: 2 canonical, 11 allowed zero-tail lengths, 2 valid numeric
  boundaries, and 38 intentionally semantically invalid payloads.
- All 38 semantic negatives have valid framing/CRC and traverse generated C
  and Python; the unchanged W1 semantic codec rejects them.
- 4 known-frame CRC/truncation rejections, 1 unknown-ID typed-dispatch rejection,
  and 11 exact-frame wrapper-policy rejections. These are distinct claims.
- Numeric boundaries retain uint64 maxima, signed zero, smallest binary32
  subnormal and largest finite binary32, with valid W1 metadata/time relationships.

The C single-frame wrapper rejects unsupported lengths/flags/concatenation and
V1 input before dispatch. An unknown message is not delivered as a diagnostic
message; that alone is not a generated-parser framing-rejection claim.
Malformed framing checks do not exercise signatures or replay.

Additional checks: W1 payload suite 8/8; frozen Flight payload suite 8/8;
scoped astyle dry-run clean; MAV_IGNORE_CRC=0 deliberately rejected with exit 1.
This environment control is rejected on presence, not truthiness.
Sanitizer builds use -fno-sanitize-recover=all; source fingerprints before/after
the run must match. Report JSON retains tool versions, pins, compiler flags,
CRC extras, source hashes and generated-Python hash.

Run from repository root in WSL:

```sh
TMPDIR=/var/tmp python3 -B Tools/research_sitl/diagnostic_wire/test_generated_mavlink.py --keep-dir
TMPDIR=/var/tmp python3 -B Tools/research_sitl/diagnostic_wire/test_generated_mavlink.py --optimize --keep-dir
TMPDIR=/var/tmp python3 -B Tools/research_sitl/diagnostic_wire/test_generated_mavlink.py --sanitize --keep-dir
python3 -B Tools/research_sitl/diagnostic_wire/test_codec.py
python3 -B Tools/research_sitl/wire/test_codec.py
astyle --options=Tools/astyle/astylerc --dry-run --formatted Tools/research_sitl/diagnostic_wire/GeneratedMavlinkFixture.cpp
```

These are isolated generated-code fixture builds; no flight source was rebuilt.
Temporary artifacts are retained for review but can disappear with OS cleanup;
missing reports must not be treated as evidence of a passing rerun.

## Source identity and corrections

- test_generated_mavlink.py:
  26c56653d3e11f230000227d9683d753ad0ec3a088a6be35b7d9bed8a5d6b2dd
- GeneratedMavlinkFixture.cpp:
  806b39fc1ddb2924e228bd46d5f0edbfcca4ca66e174d048fe3182a3d40d5f1e
- Generated Python, identical across final runs:
  4595d6219ee0d426b74b39f202177a0c4f897657ddeb87b5fe75c66d90e4c7c0

Earlier attempts are not final acceptance:
a CRC-correct under-minimum frame was delivered by generated Python, exposing
a mistaken expectation that generic framing enforces the W1 logical minimum.
The harness now labels that as wrapper policy. A zero target-component status
also trimmed below the logical minimum; the valid-framed target semantic case
uses zero target-system with a nonzero final target-component instead. W1 unit
tests still cover the zero target-component rejection.
An intermediate Debug pass had 51 roundtrips; review added the two required
numeric boundary vectors and refined rejection labels before the final runs.
Initial and intermediate /tmp artifacts were absent on a subsequent read,
including the reported /tmp/c1e_w2_mavlink_sneriglf directory. Their stdout
results are historical, not retained artifact evidence. All three final variants
were rerun using command-local TMPDIR=/var/tmp; reports were successfully reread
in separate WSL invocations. No source change was needed for that correction.
Independent review also required precise parser/dispatch/wrapper attribution;
final counts above reflect the corrected distinction.

## Next gate

Complete the exact C1E-N1 native candidate/disposition/owner API and serialized
transaction design: authenticated diagnostic binding with unavailable map,
single outstanding candidate, replay commit/discard ownership, truthful
request-vs-poll status projection, transport/source loss and bounded closure.
Then review the new private native profile, evidence and stack/build effects
before implementation/runtime integration. W2 does not authenticate packets,
qualify a clock/estimator, establish a session, enable control or close full C1
or Phase 2. Geometry remains unnecessary for that next software work.
