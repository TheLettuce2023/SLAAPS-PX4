# C0c: offline generated MAVLink conformance

Status: master/independent design and implementation review PASS (2026-09-19).
Strict Debug, optimized and ASan/UBSan host runs each passed 32 round-trip
vectors, 8 framing-rejection cases and 6 semantic-rejection cases. This is a Phase 2 software
increment, not permission for live ingress, arming or actuator output.

## Contract and ownership

Implement only the three frozen payloads in ACCELERATION_WIRE_CONTRACT.md in
parent-owned `Tools/research_sitl/wire/research.xml`. No includes, extensions,
standard-message changes or submodule edits. Use local offline-test IDs
200000/200001/200002 for REQUEST/SESSION/STATUS respectively. These fit the
MAVLink 2 24-bit field and cannot be represented in MAVLink 1. They are not a
global registry reservation or a deployment ID approval. Scan every pinned
message-definition XML for collisions before generation, not just common.xml.

Generate C and Python3 using the in-tree generator, with MAVLink 2 selected
explicitly and mandatory schema validation. Pins:

- MAVLink: `1b1e896aa3be0d591166e086e6b9d8613bd03716`.
- Nested pymavlink: `9e9a991731cc209ef9fc04006ff11721f0695726`.

The installed pymavlink package is not the generator authority. Generated
headers/modules and the host executable stay in a unique temporary directory;
do not add them to firmware or change a submodule pointer. Record tool versions,
source hashes, generated CRC extras and test results. No dependency installation.
Fail rather than silently skip XML validation or a missing C compiler.
Require exact, clean MAVLink and nested generator repositories; prove the
generator import comes from that tree and the generated Python import comes
from the unique output directory. Detect duplicate numeric IDs and symbolic
names against all pinned XML definitions. Independently derive CRC extras
from the frozen field table, not from the new XML. Verify every field offset
after primitive-width sorting and enforce logical lengths before C decode.
On this Windows checkout the runner uses command-local `core.autocrlf=true`
for Git checks under WSL. This matches Windows text conversion, not an override
of source changes; no Git configuration or pinned source file is modified.

Generated CRC extras verified against the independent oracle are request 130,
session 188 and status 131. Generated base minimum lengths are 90/76/106;
the separate logical trimmed ranges remain 85--90, 71--76 and 106.

Root owns design/gate/docs and a standalone C++ fixture. A worker may own only
the XML and Python conformance runner after this design passes independent
review. Existing payload codec, request guard, adapter and supervisor stay
unchanged. No parallel simulator builds are involved.

## Predeclared evidence

1. Match generated IDs, every field offset/order, primitive type/array width,
   nominal length (90/76/106) and CRC extras against a table-derived oracle.
   Distinguish generated base MIN_LEN from contract logical minima 85/71/106;
   MAVLink 2 zero trimming is not permission to accept malformed semantics.
2. Use existing independently transcribed payload golden bytes, not values
   produced by the new XML, to check all three messages. Compare entire C and
   Python generated frames at fixed non-operational header identities/sequence.
   Independently verify the MAVLink 2 header and X25 checksum including extra.
3. Parse C-generated frames in Python and Python-generated frames in C. Verify
   message identity and exact decoded payload fields, not merely parser success.
   Include valid zero-tail trimming and every permitted partial padded tail.
4. Reject corrupt checksum, incomplete frame and unknown message identity in
   the isolated conformance harness. Explicitly reject MAVLink 1 as this custom
   dialect. Test malformed payload semantics through the unchanged codec even
   when generic framing accepts a CRC-correct packet; framing is not admission.
5. Exercise field boundaries/float32 representation and existing C++ status
   vectors where available. Rerun S1/C0b/O1/O2 and codec regressions.

The C++ fixture is a local command-line/stdout test tool, not an operator console or
guidance application. It uses generated encoders/parsers, not a handwritten
production MAVLink implementation. The independent checksum/layout code is a
test oracle only. Unsigned fixtures prove no signing/authentication, trusted
clock, source authorization, session consent, command eligibility or control.

## Exit and remaining Phase 2 boundary

Accept C0c only after master inspection, independent review, reproducible
cross-language tests and preservation checks. Record failures and fixes.
This does not close Phase 2 by itself: operational rate/deadline budgets,
residual-attitude policy, live producer-to-consumer integration and associated
approval/evidence remain governed by the roadmap and decision checkpoint.
The live attachment and Phase 4 implementation are separate later gates, not
circular prerequisites to accepting an appropriately scoped Phase 2 contract.
Do not invent geometry, gains, command limits or flight fallback to close them.
