# Stabilized manual: scheduled output integration

Current-source follow-on: [matching prepare/complete boundary](STABILIZED_MANUAL_OUTPUT_COMPLETION.md)
adds two completion-time fault scenarios and corrects false revoke confirmation
if a revoke arrives during a command write. The original sixteen-run evidence
below covers its stated scenarios and remains preserved.

Follow-on: [PX4IO acknowledgment correction](STABILIZED_MANUAL_PX4IO_OUTPUT.md)
verifies truthful register-transfer results in the production driver. Production
supervisor/sink integration and physical timing acceptance remain pending.

2026-10-19, Codex. Continues the
[portable output contract](STABILIZED_MANUAL_OUTPUT_CONTRACT.md).
This increment adds a reusable driver boundary and verifies a native PX4
prototype through separate work queues, uORB, the actual mixer and a memory-only
output driver. It does not enable the staged hardware observer or install an
active hardware mode.

## Driver boundary

`StabilizedOutputDriver` owns one consumer session and calls an injected
`StabilizedOutputSink`. A sink returns whether it handled the write and a
completion timestamp sampled after the call. Command acknowledgments are released
only after successful handling and a completion-time deadline check.
The timestamp remains tied to the original control calculation; the driver cannot
refresh it by acknowledging later.

Before the first grant, the sink must handle a disarmed write. A competing grant
cannot replace the active session. Explicit revoke invalidates the previous
disarm confirmation; a new session remains blocked until the sink handles the new
disarmed write. A rejected disarm is retried by subsequent independent dispatches.
Driver failures latch, suppress command acknowledgments and request disarmed
output on subsequent dispatches. Explicit revoke and a fresh grant are required
for recovery.

Existing independent expiry, calibrated fallback and permission inhibition are
retained. Permission loss suppresses fallback as well as normal command output.
The driver wrapper has no topic publisher, physical I/O or automatic startup.
It is compiled into the reusable library; deployment still requires a production
sink and supervisor.

## Native prototype

The private `stabilized_output_test` module is POSIX-only and default-off.
Its two message schemas, test parameters and module registration are copied into
an isolated working-baseline source tree. They are not registered as production
messages or output functions in the checkout.

- A scheduled producer runs the actual rate/attitude-hold control core on
  explicitly synthetic fixed state. It sends one outstanding command at a time
  and waits for a matching driver-handled acknowledgment before integrating again.
- A separately scheduled consumer validates the frame, maps the four canards,
  publishes normalized servo values and calls the actual `MixingOutput`.
- A memory-only `OutputModuleInterface::updateOutputs()` callback verifies every
  resulting PWM candidate against the expected calibrated pulse, or zero when
  disarmed. It can deliberately reject a write. No physical output driver starts.
- A test supervisor grants the initial session, stops the old producer, requests
  revoke, verifies that rejected disarm prevents handoff, then grants a new
  session after successful disarm. A fresh controller/producer drains the retained
  old acknowledgment and demonstrates rate commands followed by hold again.

This tests one retained consumer across producer replacement. It does not
implement arbitration between the production bench owner and stabilized owner.
The existing build exclusion remains. Objects in each private test process are
retained until process exit, after scheduled work stops, to avoid claiming
destructor-race qualification on the older work-queue implementation.

## Verification

All **16 fresh-process native runs pass**: two repetitions each of explicit
revoke, producer-queue stall, consumer-queue stall, lost acknowledgment, rejected
driver write, slow driver call, permission loss and duplicate command.
Together they perform **626 assertions and 329 driver-handled acknowledgments**.
Every run also verifies blocked handoff during rejected disarm and successful
rate/hold operation by the successor.

Debug, Release and ASan/UBSan each pass **126,064 checks**, including **2,788
output/driver checks**. The new portable cases directly exercise completion-time
rejection, sink failure, blocked competing grants and confirmed revoke recovery.
The existing native observer regression passes **9,007 checks on the same new
binary**. Its assertion count depends on observed diagnostic records.
These results do not replace or extend the historical 31-case Gazebo qualification.

Evidence: `/var/tmp/research-stabilized-output-native-20261019`.
`runtime-manifest.json` SHA-256:
`68ad5e5afe5d76837579b676b55e10afaa249c975ebcd0c23140227e49db1984`.
Native ELF SHA-256:
`21500daaa3d2495366d7465dc8f72fd7d553169c7261b710c54a528c1338aca4`.
The manifest includes source bindings, inherited mixer/observer sources, private
patches, configuration, accepted results, failed attempts and source revisions.

[Independent verification receipt](../build/stabilized_output_runtime/20261019/verification.json)
SHA-256: `b19b3337a9f33723a514e13c32ecf525510ca06d867229b9a88ae764a9365f7f`.
Windows-side rehashing verifies **275 evidence files and 112 ARM dependency
snapshots**, with zero mismatches.

The driver wrapper also compiles with ARM GCC 13.2.1 against the working
`e8ea98b404`/NuttX 11 headers. The prior firmware volume was mounted read-only.
Its unlinked object is **570 text bytes, zero static data/BSS**; the largest
individual reported stack frame is 152 bytes. These are compiler diagnostics,
not a complete firmware size, stack bound or runtime high-water measurement.
ARM receipt SHA-256:
`9d5c12da67679bb16a32e8d7a6ce4725a9d0b746084e2b440550e60f06578ada`.
No new ARM firmware image was linked or flashed.

Reproduce with a fresh directory:

```bash
python3 Tools/research_sitl/verify_stabilized_output_runtime.py prepare --evidence /var/tmp/research-stabilized-output-native-NEW
python3 Tools/research_sitl/verify_stabilized_output_runtime.py host --evidence /var/tmp/research-stabilized-output-native-NEW
python3 Tools/research_sitl/verify_stabilized_output_runtime.py native --evidence /var/tmp/research-stabilized-output-native-NEW
python3 Tools/research_sitl/verify_stabilized_output_runtime.py test --evidence /var/tmp/research-stabilized-output-native-NEW
python3 Tools/research_sitl/verify_stabilized_output_runtime.py seal --evidence /var/tmp/research-stabilized-output-native-NEW
```

`sync` supports diagnosed source corrections before sealing and retains the
previous copied sources. The initial compiler failure used an unsupported
boolean atomic bitwise operation; integer request slots corrected it.
The first native run exposed missing stock-SITL per-channel PWM parameters.
An explicit private parameter bank corrected the fixture. Result logging was
split into bounded lines to avoid PX4 log truncation. No acceptance limit was
relaxed; those attempts remain in the evidence.

## Remaining work and limits

Driver-handled acknowledgments are software acceptance, **not electrical PWM or
servo-position feedback**. A consumer cannot service expiry while its own queue
or driver call is stalled. The stalled-consumer test verifies fault handling
on resumption; it does not establish a physical maximum hold time. A late handled
write is followed by disarm on the next dispatch, so production scheduling and
downstream failsafe behavior must bound that interval.

Next implement the production supervisor/transport and hardware sink around the
tested contract, bind actual selected state and reviewed health/permission rules,
and enforce exclusivity with other output owners. Preserve the independent
consumer schedule, explicit revoke acknowledgment and session high-water mark.
Verify configuration changes, real driver failure semantics, publisher lifecycle
and mode transitions, then produce a fresh linked ARM resource report.
Installed calibration, authority measurements, signal captures and restrained
airflow acceptance remain open. No hardware was operated in this increment.
