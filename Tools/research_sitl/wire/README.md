# C0a Research Acceleration Payload Codec

`codec.py` is a standard-library-only, stateless codec for the frozen C0a
payload layouts in `docs/ACCELERATION_WIRE_CONTRACT.md`.
It accepts plain dictionaries and payload `bytes` for request, session, and
status records.

It deliberately does not implement MAVLink framing, message IDs, CRC extras,
signing, routing, serial/network I/O, clock mapping, source/session
authorisation, replay protection, `RequestGuard`, mode handling, or
control/actuator publication.
A successful decode means only that the payload is structurally valid under
C0a; it is not an admitted request and does not claim tracking, allocation, or
physical output.

The optional `trim=True` encoder behaviour models only allowed trailing-zero
payload trimming.
Payload fixtures are offline codec inputs, not generated MAVLink
interoperability evidence.
Generated offline MAVLink conformance is now provided separately below; it does
not change this codec into an authenticated receiver or live interface.

The public functions are `encode_request`, `decode_request`, `encode_session`,
`decode_session`, `encode_status`, and `decode_status`.
Invalid data raises `CodecError`, a `ValueError` subclass; no decode path
returns a partial object.

## C0c generated-code conformance

`research.xml` defines only the three frozen messages. IDs 200000--200002 are
local offline-test IDs, not global reservations or deployed assignments.
`test_generated_mavlink.py` requires the exact clean MAVLink and nested
pymavlink revisions in `docs/C0C_GENERATED_WIRE_CONFORMANCE.md`, installed lxml
and g++. It performs mandatory XSD validation, generates C/Python in a unique
temporary directory, compiles `GeneratedMavlinkFixture.cpp` and checks both
directions against an independent field/layout/CRC/full-frame oracle.

From the repository root in WSL, without installing anything:

```sh
python3 -B Tools/research_sitl/wire/test_generated_mavlink.py
python3 -B Tools/research_sitl/wire/test_generated_mavlink.py --optimize
python3 -B Tools/research_sitl/wire/test_generated_mavlink.py --sanitize
```

Add `--keep-dir` to retain generated files/executable for inspection; otherwise
only that invocation's temporary output is removed. The JSON result records
tool versions, source hashes, pins, CRC extras, compiler flags and case counts.
Git checks explicitly use command-local `core.autocrlf=true` to match the
Windows checkout when running under WSL; actual content changes still fail.

Accepted evidence: 32 round-trip vectors (including all allowed zero tails,
float/width boundaries and nine existing status vectors), 8 framing-negative
cases and 6 correctly framed semantic-negative cases. Generated parsers may
accept a semantically invalid payload; the unchanged C0a codec rejects it.
The test fixture accepts no signed frames and makes no authentication claim.
No socket, serial device, PX4 module, operator console or actuator path is used.
