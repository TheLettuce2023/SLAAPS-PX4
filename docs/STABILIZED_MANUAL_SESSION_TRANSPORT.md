# Dynamic session bridge: transport contract

2026-10-20, Codex. Incremental integration work, not a completed dynamic session.

## Existing boundary and intended connection

The qualified dynamic owner in `stabilized_manual/StabilizedManualSITL.cpp` uses
`StabilizedManualControl` directly and publishes allocator values to Gazebo.
`RotationalFixture.cpp` independently checks `BenchOutputLease`, but has no
acknowledgment channel. Gazebo Publish success cannot establish handled output.
The native scheduled-output fixture already tests the separate session, coherent
grant handshake, selected-state guards and actual mixer with a memory driver.
Those two paths have not yet been joined.

The next integration must keep the controller session, independent selected-input
consumer and explicit supervisor grant as distinct owners. The controller sends a
sequence-bound frame; the consumer validates its source, immutable configuration,
permission and lease at the write boundary. The simulation driver sends a command
to the plant, whose reply must refer to the exact session/sequence/command time
handled. Receipt alone must not grant authority. Revocation and a confirmed
zero-output barrier must precede successor ownership. Missing, reordered, stale
or restarted transport must fail closed, with no automatic recovery. Local uORB
publisher revision is local metadata, not a remotely authenticated Gazebo identity.
An explicit simulator-start/grant binding is still required at the plant endpoint.

## Implemented now

`Tools/research_sitl/stabilized_session/Transport.hpp` defines version-1 command
and acknowledgment payloads in actual Gazebo `Double_V` protobuf messages.
Commands carry configuration, original session/time/sequence, lease, fixed thrust
and all four normalized actuator values. Acknowledgments carry configuration,
session, original command time, sequence and consumer acceptance time.

Both directions reject wrong shape/type/version, nonfinite values, fractional or
out-of-range integer fields, zero required identities, inconsistent timestamps,
lease-end overflow, negative/nonrepresentable thrust and out-of-range actuator
values. All integer values are restricted to the exact double range through
2^53-1 before encoding and before conversion on decode. Decode uses a temporary
record, so rejection cannot expose a partially replaced command. Encoders also
preserve their destination on rejection.

This is structural transport validation only. Profile matching, current-clock
freshness, grant acceptance, source/session continuity, command sequencing,
permission, physical application, and fallback remain endpoint responsibilities.
There is no automatic grant or acknowledgment side effect in this codec.

## Evidence

`verify_stabilized_session_transport.py` builds and runs real protobuf tests in
Debug, Release and ASan/UBSan: 410 checks per configuration, zero failures.
Tests include both message types, actual binary serialization/parsing, all field
positions with NaN/infinity, truncation/extension, timestamp ordering, lease
range, output bounds, integer precision boundaries and unchanged output on error.
The final C++ formatting check reports both files unchanged.

Evidence: `/var/tmp/research-stabilized-session-transport-t2z3hv12`.
Manifest SHA-256: `4fa9d9caa35246d59af6dbd3fd9e9c90cd6408e16eae667182b6578d0c96f2cc`.
All 83 evidence files rehash, and 401 compiled input hashes are recorded and
checked during verification. An initial compile failure using matrix indexing
for the existing fixed-array type is retained in the earlier evidence directory;
the final codec uses the actual fixed-array API.

## Next work and preserved gates

Wire the contract into an isolated opt-in dynamic session/consumer bridge, add
explicit simulator grant/revoke/disarm handling, bind acknowledgment production
to handled plant output, then run the dynamic matrix against that complete path.
The current qualified console, its profile, production firmware and existing
fixture source are unchanged. No simulator or hardware is launched by these
codec tests. Production integration, complete ARM resources and real vehicle
measurements remain open. No physical output authority is granted.
