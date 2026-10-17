# C1E diagnostic payloads

This directory defines the isolated, negative-only C1E diagnostic payload
contract. It does not alter the frozen Flight v1 codec or dialect. Message IDs
200010 (`RESEARCH_DIAGNOSTIC_REQUEST`) and 200011
(`RESEARCH_DIAGNOSTIC_STATUS`) are local C1E experiment IDs, not deployment
assignments.

`codec.py` uses only Python's standard library. Its exact dictionary keys match
the XML fields, including `reserved`, which must be four zero bytes. Public
functions are `encode_request` / `decode_request` and `encode_status` /
`decode_status`; malformed input raises `CodecError`, a `ValueError` subclass.
Encoders emit fixed-size payloads by default. `trim=True` omits a trailing zero
tail. Decoders accept only bytes within the defined minimum and full lengths,
and restore an omitted zero tail before validation.

| Payload | Python struct format | Full size | Minimum trimmed size |
| --- | --- | ---: | ---: |
| Request | `<10Q3fI8B4s` | 108 bytes | 103 bytes |
| Status | `<14QIH14B4s` | 136 bytes | 132 bytes |

All multibyte values are little-endian. Protocol version is `0x00010000`;
domain and profile are both 1. A zero `clock_mapping_epoch` explicitly denotes
an absent qualified map. Status reasons are ClockMappingInvalid=1,
TransportDisconnected=2, StateEstimateInvalid=3, SelectionRequired=4,
ContextChanged=5, SourceContinuityLost=6, OwnerStopped=7, and
ResourceExhausted=8. Rejected outcome 1 is limited to reasons 1–5; closed
outcome 2 is limited to reasons 6–8. Status is always unselected, estimator
invalid, map invalid, and `eligible_for_control=0`.

`transport_session_id` binds this transport experiment; it is not proof of an
established admission session. Payload encoding does not authenticate a frame,
qualify a clock, or prove state, estimator validity, execution origin, or source
truth. No PX4 endpoint, key, parser, replay state, operator action, or native
runtime integration is provided here.

## Generated C/Python conformance

`test_generated_mavlink.py` validates the private dialect with pinned generators,
independent layout/CRC/frame oracles and `GeneratedMavlinkFixture.cpp`. It checks
generated roundtrips separately from strict W1 semantic rejection. This is
unsigned offline frame evidence, not an authenticated receiver.

From the repository root in Ubuntu-24.04 WSL, using existing tools:

```sh
TMPDIR=/var/tmp python3 -B Tools/research_sitl/diagnostic_wire/test_generated_mavlink.py --keep-dir
TMPDIR=/var/tmp python3 -B Tools/research_sitl/diagnostic_wire/test_generated_mavlink.py --optimize --keep-dir
TMPDIR=/var/tmp python3 -B Tools/research_sitl/diagnostic_wire/test_generated_mavlink.py --sanitize --keep-dir
```

`report.json`, generated sources and fixture binaries stay in each reported
directory with `--keep-dir`; without it, only that invocation's temporary
directory is removed. `/var/tmp` is recommended because this environment's
`/tmp` artifacts disappeared between WSL calls. Full evidence and limitations:
[C1E-W2 verification](../../../docs/C1E_GENERATED_WIRE_CONFORMANCE.md).
