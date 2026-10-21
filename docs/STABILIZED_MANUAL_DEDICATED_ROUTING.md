# Stabilized manual: dedicated mixer source

Follow-on: [source-bound output ownership](STABILIZED_MANUAL_SOURCE_OWNERSHIP.md)
binds explicit grants and copied commands to publisher lifetimes, including
completion and handoff checks. Evidence below retains its earlier scope.

2026-10-20, Codex. Stabilized candidates now reach the mixer directly from the
[output adapter](STABILIZED_MANUAL_MIXER_ADAPTER.md), through a dedicated function
provider. The consumer no longer publishes candidate values on `actuator_servos`.
This removes the shared-servo topic from the research output-value path; it does
not establish production supervisor ownership or authenticate upstream commands.

## Implementation

`FunctionResearchStabilized` has no topic subscription. It reads normalized
controls from the permanently attached transaction's prepared candidate.
Unprepared, cancelled, revoked, disarmed, completed and out-of-range requests
return NAN. Final mapping, permission, command-age and completion checks remain
in the adapter and mixer hook.

New function IDs `Research_Stabilized1` through `Research_Stabilized4` occupy
10010–10013. All **71 existing named function indexes** remain unchanged, and
header generation checks for collisions. These new functions are excluded from
normal actuator-test metadata. Direct actuator-test messages are still handled
by the existing mixer test path and rejected by the adapter's final validation.

The mixer allocates this provider only for an attached MAIN bank, channels 1–4,
with function order matching channel order. AUX, spare-channel and swapped
research routes cannot obtain the provider. The adapter requires these dedicated
functions, so remapping a canard to a generic servo also rejects the entire bank.
The new functions use the declared servo center/minimum/maximum mapping.

The provider receives its owner through a context field compiled only with
`CONFIG_RESEARCH_STABILIZED_OUTPUT_HOOK`. There is no automatic attachment,
grant, startup or new physical driver. The output owner remains responsible for
independent periodic preparation/dispatch, including expiry and disarm after
producer failure. Normal stock/bench function behavior is unchanged.

## Verification

All **48 fresh-process native runs** pass: the previous twenty scenarios and
four dedicated-routing scenarios, each twice. They execute **3,564 assertions**
with **1,009 handled command acknowledgments** through the actual mixer and an
eight-channel memory driver.

The competing-servo case publishes 200 alternating full-scale generic servo
messages while the legitimate producer continues rate/hold operation. Every
captured output still matches the prepared research candidate, and command
acknowledgments continue. The other new cases remap a canard to a generic servo,
swap its research function, and assign a research function to a spare channel;
each rejects before driver application and faults the producer. Every scenario
still verifies rejected-disarm handoff blocking and successful successor control.
The native attachment checks cover MAIN restriction; no physical AUX device is
operated by this suite.

Debug, Release and ASan/UBSan each pass **126,123 portable host checks**.
These host suites do not execute the mixer provider. The existing observer on
the same native ELF passes **14,230 checks**, including seven hidden publisher
restart cases. Synthetic state is fixed in this scheduled fixture; the earlier
31-case dynamic rotational SITL qualification is unchanged.

Native evidence: `/var/tmp/research-stabilized-output-native-routing-20261020`.
Manifest SHA-256:
`9ac8e9fd96b1e42f52cb3ab469c852b3aeee4133f4c1ab0787057c6ae2bc1059`.
ELF SHA-256:
`f038b19c74637f08a517ba0fd04f166eadf70fa87c5c1ef9d3010fabac794ea2`.
All **400 manifest-listed files** independently rehash. Reproduction uses the
runtime runner's prepare/host/native/test/seal actions with a fresh directory.

On the retained working `e8ea98b404`/NuttX 11 ARM build environment, complete
baseline and feature-disabled mixer translation units both contain **8,233 text
bytes**, with exactly matching machine code and relocations. The enabled mixer
contains **8,661 bytes**, 428 above baseline and 288 above the earlier hook-only
object. The updated adapter contains **1,297 unlinked text bytes**, 88 above the
previous adapter. Its compiled instance size remains **848 bytes**. These
objects are not final firmware size or runtime scheduling measurements.

ARM evidence: `build/stabilized_output_routing/20261020`. The recipe regenerates
the function header, checks existing indexes, compiles baseline/disabled/enabled
mixers plus the adapter and layout probe, and archives dependencies.
Receipt SHA-256:
`639fce881d98b0f9b0267376140172b74a95d9aeee4906aed3598bbfb203704d`.
All **288 receipt-listed files** independently rehash. Combined verification:
`b6a6b2001a75b24be059d7104fa3f4df127e2b42cadfd0c837c76cdced3831a9`.

## Next integration

Implement the production supervisor's exclusive bank ownership, session
grant/revoke and source binding, connect authoritative permission/selected-state
health, and attach the adapter to the physical driver's serialized execution.
The native fixture's command and acknowledgment topics remain private test
transport; dedicated mixer values do not authenticate their upstream publisher.
Independent output scheduling and confirmed disarm before ownership transfer
remain required.

Then link and budget the complete candidate. The last staged lifecycle-observer
image has 3,584 flash bytes free and contains no active stabilized adapter.
No new firmware was linked or flashed here. Vehicle calibration, aerodynamic
authority, electrical timing and servo response still require human measurements;
stock/bench artifacts and existing physical/acceleration gates remain unchanged.
