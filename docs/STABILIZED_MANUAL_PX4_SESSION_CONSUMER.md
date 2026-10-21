# PX4 session consumer: implemented, compile-verified

2026-10-20, Codex. The private POSIX consumer translation unit compiles against
the verified observer-enabled native PX4 headers. It has not yet been linked into
a new PX4 executable or run. Do not infer runtime or closed-loop qualification.

The consumer was subsequently [linked into a fresh private PX4 executable](STABILIZED_MANUAL_SESSION_LINK.md). This record retains its original compile-only evidence; queue runtime remains pending.

## Implemented path

`stabilized_session/Px4SessionConsumer.hpp` owns an independent scheduled work
queue. It reads actual lifetime-bound uORB grant requests and command frames,
publishes coherent grant responses and handled output acknowledgments, and owns
its own `SelectedStateInput`/`SelectedStateGuard`. The trusted supervisor lifetime
is bound at explicit startup; command/ack/response lifetimes remain checked.

Gazebo callbacks enqueue bounded messages. Only the consumer queue mutates the
portable driver, remote-completion gate, grant and health state. Startup waits
for a fresh handled-disarm status from the unique plant namespace. Grant handling
requires centered, valid selected inputs, the live request bindings and its
unexpired deadline; local acceptance follows the matching plant grant reply.
The cached accepted response is invalidated before asynchronous revocation waits.

Commands are prepared by the portable output driver and sent with the immutable
shared simulation profile. The work queue remains scheduled while a remote write
is pending and checks health/source/timeout each dispatch. A matched remote receipt
must pass the completion gate and a fresh local permission check before a local
uORB acknowledgment is published. Grant and command deadlines are rechecked after
live input capture and before final acceptance/publication. No Gazebo publish
return is treated as a handled command.

Revocation waits for a matching remote disarm reply before canceling abandoned
local I/O, then completes the local disarm barrier. Revoke requests retry without
refreshing the original command deadline. Automatic faults latch; deliberate
revoke/stop has a separate recovery path. The owner is retained until process exit,
so an asynchronous stop cannot destroy pending I/O. A private module entry point
and external-module CMake target are included.

The hardware mixer hook remains synchronous and unchanged. This private consumer
currently uses the portable driver directly; actual mixer integration has separate
native evidence and must not be inferred from this compilation.

## Verification and limits

`verify_stabilized_session_consumer_compile.py` rehashes the prior sealed native
PX4 evidence, reuses its real compiler configuration/generated messages, overlays
current research headers and compiles the complete new consumer translation unit.
It retains all compiler dependencies and symbols, including the actual scheduled
Run implementation. The baseline checkout and its binaries are read-only inputs.

Final compile evidence:
`/var/tmp/research-stabilized-session-consumer-t2f7m6ye`.
All 581 evidence files rehash. Receipt:
`7f83fddc11f77cf3675d364e4555ef13abc99b59ce3ae24688f06ffd912b50b5`.
Initial strict-build failures are preserved: codec integer comparison was changed
to an equivalent ordering test, and Gazebo pkg-config flags no longer replace
PX4's GNU C++ dialect. The final source also invalidates grant responses on revoke,
binds response lifetime, retains future disarm replies and rechecks deadlines.

Shared codec/profile regression tests pass in Debug/Release/ASan-UBSan: 410 codec,
887 plant endpoint and 170 remote completion checks per configuration.
Evidence: `/var/tmp/research-stabilized-session-transport-j5v9adyl`, 111 files;
receipt `4735aa604a6465d9d46e71233154ad2e8b7984b74cde49dee38362b6bab9f3bc`.
Real Gazebo plant/remote gate still passes 40 acks, two sessions/expiries/disarms,
235 truth rows. Evidence: `/var/tmp/research-stabilized-session-plugin-a5sfr96b`,
120 files; receipt
`87b3cbc1a131d6ccf7504b52a966038daa0f1dbbbce9e015dfb8e77b67c67d59`.
Those tests cover shared code and the plant-side bridge, not the new PX4 queue;
the final consumer-only deadline review follows those runs and has its own compile
receipt above. No runtime test has exercised this consumer yet.

## Next required work

Link a fresh private PX4 executable with this external module, current observer
lifecycle support, private command/ack schemas and the grant schemas. Add the
separately scheduled producer using `StabilizedManualSession` and coherent grant
confirmation. Start the real estimator/selected manual input, verify prearmed
servo permission explicitly, connect to the same unique plant namespace and
verify simulation clock alignment. Exercise startup, hold/rate, independent
health/source loss, delayed/lost replies, revoke/stop and successor grants. Then
run the full closed-loop maneuver/console matrix before promoting any default.

No new PX4 executable was linked or run, no hardware was operated, and the current
qualified console, original fixture, stock/bench/H10/acceleration gates remain
unchanged. Complete-image resources and real vehicle measurements remain open.
