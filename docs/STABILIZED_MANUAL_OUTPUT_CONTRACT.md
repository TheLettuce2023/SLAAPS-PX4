# Stabilized manual output boundary

2026-10-19, Codex. Continued G9 software work after the
[output-disabled observer](STABILIZED_MANUAL_HARDWARE_OBSERVER.md).
`StabilizedOutput.hpp/.cpp` adds a portable producer/consumer protocol and
calibrated four-servo mapping. It does not connect the observer to a driver,
publish actuator topics, grant hardware permission, or change the staged firmware.
The existing bench and synthetic SITL consumers retain their original contracts.

## Implemented behavior

Both endpoints require an explicit `begin(now, session, configuration, permitted)`
grant. Construction is disabled. Configuration has no vehicle defaults. A grant
alone leaves consumer output disarmed until it accepts a fresh command.
Frames cannot open sessions, including frames claiming a newer session.

The producer accepts a successful control `Report` with a strictly newer
calculation timestamp and Rate/Hold state. It allows one outstanding frame.
The consumer checks the granted session, next sequence, timestamp, age and all
four normalized positions before mapping and acknowledging the complete frame.
The acknowledgment binds session, sequence and calculation/acceptance times.
It means **validated and mapped by this software consumer**, not that an output
driver or servo applied the values.

The producer faults if the acknowledgment misses its deadline. The consumer
expires independently on every `sample()` dispatch, even if the producer stops.
Both deadlines are measured from the calculation timestamp. Receipt or
acknowledgment time never extends the command lease. Expiry uses `age >= limit`;
unsigned subtraction avoids overflowing an absolute deadline near `UINT64_MAX`.
Initial silence is also bounded. Duplicates, reordered frames, wrong sessions,
future/regressing times, invalid acknowledgments and nonfinite commands reject.
An expired session cannot be renewed by a late frame.

`NotDue` never creates a frame or renews a lease. A failed calculation faults
the producer even while it waits for an acknowledgment; a retained successful
report cannot be used as the result of that failed calculation.

Calibration maps allocator positions -1, 0 and +1 to explicit pulse endpoints
using two linear segments. Increasing or decreasing endpoints encode reversal
exactly once. An independent minimum/maximum operating interval restricts each
channel. The candidate also contains normalized driver controls calculated from
the declared driver minimum, center and maximum. All four channels validate
before the caller's candidate is replaced. This is candidate arithmetic;
rounding, physical units/sign agreement, mixer configuration and actual output
application still need an integrated driver contract.

Fault policy must explicitly select disarmed output or a calibrated fallback
position. Before the first accepted command, output is always disarmed.
**Permission loss suppresses even calibrated fallback**, including permission loss
after an earlier expiry. Restoring permission cannot restart the fallback.
Explicit `disable()` also disarms and clears the current candidate. Recovery
requires disabling both endpoints and granting a strictly newer session.
Neither endpoint is copyable.

## Obligations of the future runtime adapter

These are unresolved integration requirements, not guarantees supplied by the
portable library:

1. Establish one exclusive output owner. Revoke the old consumer before granting
   a new session; reset controller integral, reference and allocation history.
   Keep the observer/bench build exclusion until real ownership is implemented.
2. Grant exactly the same immutable calibration and session to both endpoints.
   Preserve the session high-water mark across endpoint replacement within a
   boot. These objects are not an authentication or publisher-identity mechanism.
3. Validate fresh input, selected estimator state and permission every dispatch,
   including while waiting for an acknowledgment. `permitted` is a caller-supplied
   result; this library does not define production health or arming policy.
4. Poll the producer before advancing the controller. While Waiting, do not
   advance its integrator or allocation history. Deliver the actual calculation
   status with its report; publish only after `submit()` returns Ok.
5. Run consumer expiry in the independent output task, honor the candidate's
   disposition, and acknowledge only a successfully accepted frame. Zero-filled
   Disarmed data is not a zero-microsecond PWM instruction.
6. Verify the configured downstream PWM range, center, reversal, routing and
   safety behavior. No other producer may overwrite the selected outputs.
   Binding to driver application, immediate explicit revoke delivery and its
   acknowledgment remain to be implemented. Producer failure alone is bounded
   by the consumer lease plus measured consumer/driver scheduling latency.
7. Bind allocation coordinates and operating limits to measured servo geometry.
   Supply reviewed fallback positions/timing, loaded servo limits and a supported
   airflow/authority profile. Software fixture values are not vehicle settings.

The protocol rejects missing sequences rather than silently skipping them.
A future transport must preserve delivery/order or treat loss as a fault; it
cannot assume an unconstrained latest-value topic will satisfy this contract.
No EDF channel or thrust command is present.

## Verification

The new suite exercises synthetic asymmetric/reversed calibration, full-channel
atomic rejection, session/sequence corruption, acknowledgment loss, independent
expiry, clock limits, permission inhibition and controller-fault propagation.
It includes 50 actual control-core cycles with rate input followed by centered
attitude hold, mapping each allocator result through the consumer. State is
synthetic fixed truth; this is not a new dynamic-plant or Gazebo qualification.

Reproduce host verification from WSL with:

```bash
python3 Tools/research_sitl/verify_stabilized_manual.py
```

The runner creates a fresh evidence directory, snapshots sources, checks graph
fingerprints, runs Debug/Release/ASan-UBSan and verifies inputs stayed unchanged.
Each configuration passes **126,013 checks**: 123,083 controller, 193 observer
profile and **2,737 output-boundary** checks. Final evidence is
`/var/tmp/research-stabilized-host-lx60q8nq`; its `manifest.json` SHA-256 is
`f2cdfaa6aaec3f2646791e19e3889bbc43038d61d72305dcad9913b635a19155`.
The preceding host-only pass at `research-stabilized-host-cg62g_qi` predates
the NuttX integer-limit correction and is superseded by this final run.

[Verification receipt](../build/stabilized_output/20261019/verification.json)
SHA-256 is `e37181f606f31a639d7401d158cfada48d384e02f2899816bac66313a5ab7340`.
Independent Windows-side rehashing checks 234 host evidence files, 111 ARM
dependency snapshots and all three prior observer firmware artifacts with zero
mismatches. The prior firmware artifacts are unchanged.

The new source also compiles separately against the retained working
`e8ea98b404`/NuttX 11 environment using ARM GCC 13.2.1 and the observer's actual
compilation flags. The retained firmware volume is mounted read-only.
`build/stabilized_output/20261019/` contains the command, script, dependency
snapshots, compiler identity, object, logs and receipt.
Final `arm-receipt.json` SHA-256 is
`fe1b6c1ebc6b29831a012f81c269ba6025393ba41367aea8fe89776c26d0675a`.
The first attempt exposed the unavailable `<limits>` header in minimal NuttX;
the source now uses `UINT64_MAX`. The failed log remains preserved.

The unlinked object contains 2,068 bytes of text and zero static data/BSS.
Largest reported individual compiler stack frame is 88 bytes; this is not a
call-chain bound or measured runtime high-water usage. No complete firmware was
linked for this increment, so its final flash cost is unknown. The prior
observer image's 4,376-byte headroom still refers only to that prior image.

## Next step

Implement the PX4 runtime supervisor/transport and independent driver consumer
around this contract, with observable revoke and acknowledgment behavior, then
qualify scheduled loss/transition cases on native PX4. Production health/source
identity and a fresh linked ARM resource check remain part of that milestone.
Vehicle calibration, physical signal captures and restrained airflow tests remain
separate prerequisites for hardware acceptance.
