# Stabilized manual: reusable MAIN PWM adapter

Follow-on: [dedicated mixer source](STABILIZED_MANUAL_DEDICATED_ROUTING.md)
replaces generic servo publication with prepared-candidate research functions.
Production supervisor/source binding and physical attachment remain pending;
the evidence below describes the earlier adapter milestone.

2026-10-20, Codex. `ResearchStabilizedOutput` implements the adapter behind the
[mixer transaction hook](STABILIZED_MANUAL_MIXER_TRANSACTION.md). The scheduled
native consumer now uses this production-library class instead of implementing
its own validation/completion policy. No physical driver attaches it yet.

## Contract

The adapter owns the portable driver protocol, one prepared candidate, immutable
granted calibration and one unconsumed result. It attaches once, before the
mixer's first update, and records the MAIN bank width. All calls run in the
serialized output-driver context. It has no publisher, scheduler, automatic
grant or startup path.

Before a grant and every non-disarmed write, the actual mixer must match the
declared minimum/center/maximum values and Servo1–4 routing. Reversal must occur
in the declared calibration only. All channels require zero disarmed/failsafe
values; unused MAIN channels must be disabled. Bank-width changes reject.
Final PWM values must match the candidate, including zero on unused channels.

An explicitly supplied permission/health provider is sampled for grant,
preparation, validation and completion. Non-disarmed writes also require fresh
effective mixer arming state, armed or prearmed, without kill, lockdown,
termination, ESC calibration or actuator-test control. This does not define
vehicle health policy or authenticate publishers. The future provider must
check authoritative state, including anything the mixer may mask for simulation.

Immediately before the write, command age is checked against the original
calculation timestamp. Retained commands must remain within the lease; newly
accepted commands must also remain within the acknowledgment deadline. A delay
after preparation cannot make an expired command eligible for writing.

The final mixer hook substitutes a zero bank on rejection. Such a substitute
cannot acknowledge the rejected candidate or confirm revocation; a subsequent
validated, handled disarm is required. The adapter completes only a pending,
validated candidate with the actual driver result and completion-time permission.

Results can be collected once. An uncollected result blocks another preparation.
Revoke invalidates pending authority and suppresses command/revoke acknowledgment
in a result that completed but has not yet been collected. Cancellation never
acknowledges an abandoned write, and cannot complete an outstanding revocation.
It must only be used before I/O or after I/O is known to have stopped.

## Verification

The native fixture now exercises **eight MAIN channels**, four synthetic canards
and four disabled spares, using the actual mixer and a memory-only driver.
All **40 accepted fresh-process runs** pass **2,888 assertions**, with **821
handled command acknowledgments**.

Twenty scenarios run twice: the previous thirteen, plus assignment of a spare
channel, failsafe drift, reversal drift, permission loss between preparation and
writing, revoke between completion and result collection, stale arming state,
and a prepared command delayed beyond its lease. Each checks blocked handoff on
rejected disarm and successor rate/hold operation. Constructor probes cover
unattached/unprepared operations, permanent attachment, cancellation and late
completion. Every dispatch checks overlapping preparation, uncollected-result
protection, one-shot collection, and every actual bank value.

During test development, a 120 ms delay incorrectly assumed every candidate had
a new acknowledgment due; a retained command still has the 150 ms lease. The
final test crosses that lease and refreshes arming separately, isolating command
age. A cancellation probe was also corrected to expect the existing initial
Revoking state rather than Fault. Earlier failed logs/source revisions remain
preserved; the accepted set is explicitly enumerated in the manifest.

Debug, Release and ASan/UBSan each retain **126,123 portable host checks**.
These host suites do not execute the PX4 mixer adapter. The native observer on
the new binary passes **14,174 checks**, including seven publisher restart cases.
These are software integration tests with fixed synthetic state, not a new
dynamic-plant qualification.

Native evidence: `/var/tmp/research-stabilized-output-native-adapter-20261020`.
Manifest SHA-256:
`c96cad7339520318362bbfb96b82d677d4147ec092b8b65fbe0c5151f7981d88`.
ELF SHA-256:
`57e05701b258d3e6b22da8470413b38193e7059086b5aeb091d27e87d0bd1fb7`.
All **647** manifest-listed files independently rehash. Reproduce with the
runtime runner's prepare/host/native/test/seal actions in a fresh directory.

The adapter compiles against the retained working `e8ea98b404`/NuttX 11 ARM
environment with **1,209 unlinked text bytes**, zero static data/BSS, and an
**848-byte instance** including its protocol/configuration state. The largest
individual compiler-reported stack frame is **128 bytes**; this is not a
whole-call-chain or runtime high-water measurement. No full firmware was linked.

ARM evidence: `build/stabilized_mixer_adapter/20261020-final`, with compiler,
exact commands, source/header snapshots and a compiled type-layout probe.
Receipt SHA-256:
`5a085a18217ea13bf1f0ae43b7f04c0749781d2355904d87124d98341074aaa1`.
All **225 dependency snapshots** rehash. The combined verification record
binds 247 stage files and the native totals:
`4e9c6f62b8215b2f7dd0b0e2ed2bae2828a3bb104f4ca2a5d8a6072275882705`.

## Remaining integration

Implement the supervisor's exclusive ownership and session grant/revoke
protocol, connect authoritative permission/selected-state health, and attach
this adapter in the physical driver context with independent expiry scheduling.
The current native fixture still transports candidate values through generic
`actuator_servos`; exclusive producer routing and production transport remain
integration obligations. A matching final value is not publisher authentication.

Then link the complete candidate and check flash/RAM/scheduling budgets. The
last staged lifecycle-observer image has 3,584 flash bytes free and does not
contain an active adapter. `CONFIG_RESEARCH_STABILIZED_OUTPUT_HOOK` remains
default-off; no production startup or output owner was enabled.

Vehicle calibration, aerodynamic authority and physical output timing remain
pending human measurements. Numeric zero and driver handling do not prove
electrical shutdown or servo response. Stock/bench artifacts, physical acceptance
gates, the 31-case synthetic rotational SITL qualification and acceleration
gates remain unchanged. Nothing was flashed.
