# Stabilized output: matching driver completion

Follow-on: [production mixer transaction hook](STABILIZED_MANUAL_MIXER_TRANSACTION.md)
adds final-bank validation and actual driver completion; the hardware adapter
and supervisor remain pending. Evidence below retains its original scope.

2026-10-19, Codex. This adapts the existing output contract to the production
mixer's separate value-preparation and driver-result steps. The production
hardware callback integration and supervisor remain pending.

## Corrected behavior

The old synchronous wrapper could prepare a command, receive a revoke during
the sink call, and treat that command's successful return as confirmed disarm.
A regression compiled from preserved actual old source reproduces
`command_write=1 revoke_ack=1 disarmed_confirmed=1`.
Current source reports `command_write=1 revoke_ack=0 disarmed_confirmed=0`;
only a later handled disarmed write permits a new grant.

`StabilizedOutputDriver` now provides:

- `prepare`: validates the command and returns its candidate with a ticket.
  Only one write can be pending. Another preparation cannot overwrite it.
- `pending`: lets the serialized caller check that a deferred candidate is still
  valid immediately before starting its write.
- `complete`: accepts only the matching ticket, samples driver-completion timing
  and current permission, and emits acknowledgments only when those checks pass.
  Old, duplicate and unrelated completions cannot consume a newer pending write.
- `cancel`: abandons a matching prepared write without acknowledgment. A later
  disarm is required; cancellation cannot release ownership.
- The existing synchronous `dispatch` method uses these same phases.

Revoke invalidates any pending candidate and acknowledgment. A command already
handled when the revoke arrives cannot be undone, but its completion cannot
confirm the newly requested disarm. Completion-time permission loss also latches
the fault. Explicit revoke, a matching handled disarm and a fresh session remain
necessary for recovery. Ticket exhaustion permanently disables authoritative
acknowledgments while allowing terminal disarm attempts; tickets never wrap into
an earlier admitted command ID.

The API operates in one serialized consumer context. It separates synchronous
mixer/driver stages; it does not synchronize concurrent callers or cancel
physical I/O. `cancel` is valid only for a write that has not started or is known
to have terminated. An in-progress physical write must finish before another
physical write starts. A future mixer adapter must validate its routing and
actual mapped values before passing the driver's handled result to `complete`.

## Verification

The scheduled native consumer now calls `prepare`, passes the candidate through
the actual PX4 mixer into its memory-only capture driver, and calls `complete`
with the handling result and current permission.

All **20 fresh-process runs** pass: the previous eight scenarios plus revoke
during handling and permission loss during handling, each twice. Together they
perform **786 assertions and 408 handled command acknowledgments**. Every run
also checks rejected disarm, blocked handoff and successor rate/hold operation.
The two new scenarios explicitly reject both command and revoke acknowledgments
for the invalidated command write.

Debug, Release and ASan/UBSan each pass **126,123 host checks**, including **2,847
output checks**. Added cases cover overlapping preparation, wrong/duplicate/
delayed completions, cancellation, deadline expiry, revoked in-flight commands,
permission changes and recovery. The actual observer on the same new native ELF
passes **14,070 checks**, including its seven hidden publisher restart cases.

Native evidence: `/var/tmp/research-stabilized-output-native-phases-20261019`.
Manifest SHA-256:
`d193df8e011a5824d0ca105a5a9306b17acda48fc5fd2468ef63f7675d158033`.
ELF SHA-256:
`44ff9bcc49d80dee7e725b78aef271ccfad539058a23c58293389e5a69f7b8b5`.
All 251 manifest-listed files independently rehash.

The actual PX4IO output/register methods also pass **22,672 checks per
configuration** with this new driver; the old PX4IO return path still fails
76 checks as expected. Evidence: `/var/tmp/research-px4io-output-hqm6foot`;
manifest SHA-256
`44cd3ea773fc66634872b8e1ce38d7d4925b68342fa5414e51a5ba2ee961996b`.
All 150 files and current source bindings rehash. This uses injected transport,
not live UART or hardware.

The driver compiles with working-baseline ARM/NuttX 11 flags into a **1,058-byte
text object**, with zero static data/BSS. The largest individual compiler-reported
stack frame is 176 bytes, not a runtime high-water or whole-call-chain bound.
Evidence is under `build/stabilized_output_phases/20261019`; ARM receipt SHA-256:
`b5b64751305fba786da93e280fc2eb738a787ce0dbbef45d2ac22fe08f854871`.
All 112 dependency snapshots rehash.

That directory also retains the actual old/current-source negative regression
and its receipt
`f7391580eb7c637f098cc6a8ae5c343fe12c584f628c9c8f7e70932b37adc049`.
The combined `verification.json` binds these result totals and evidence packages.
Reproduction uses the scheduled-output runner's existing prepare/host/native/
test/seal commands with a fresh `/var/tmp/research-stabilized-output-native-*`
directory.

## Remaining work

Connect these phases to the production mixer callback and implement the
supervisor's explicit ownership/grant/revoke protocol, configuration checks and
reviewed permission/health rules. Keep the independent consumer schedule and
confirm disarm before handing the channels to another owner.

Driver handling is software acceptance, not electrical or servo feedback.
A blocked queue/driver still cannot enforce its own physical expiry deadline.
The latest full observer image remains the prior output-disabled build with
3,584 bytes of flash headroom; this new driver revision was compiled separately
and is not linked into that image. No hardware was operated, and no physical,
bench, acceleration or flight acceptance gate was changed.
