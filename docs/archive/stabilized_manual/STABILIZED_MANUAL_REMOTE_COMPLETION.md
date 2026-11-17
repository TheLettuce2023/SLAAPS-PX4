# Nonblocking remote completion boundary

2026-10-20, Codex. Private helper and real Gazebo probe verified; the PX4 consumer
work queue is not yet connected to the remote plant.

## Contract

The existing `ResearchOutputTransaction` completes synchronously after its driver
returns. That hardware contract is unchanged. A Gazebo publish result must not be
forwarded as handled actuator output. `stabilized_session/RemoteCompletion.hpp`
provides one pending remote-write gate for the future serialized SITL consumer.
Transport callbacks must enqueue messages; only the consumer queue calls the gate.

Start binds an already authorized pending local ticket, command identity, immutable
configuration, trusted local command publisher revision and acknowledgment budget.
It does not create a grant, allocate a driver ticket or validate the selected input
itself. The owner must independently poll it every dispatch and supply freshly
checked permission and actual source revision. A fault preserves the pending ticket
until its remote I/O is known to have stopped.

A remote acknowledgment must match configuration, session, sequence and original
command time; its handled time must be no earlier than local preparation. Both
receipt time and handled time must fit the original deadline. A future lockstep
completion waits for the local clock to catch up without refreshing that deadline.
Only a matching timely receipt yields `Handled` with the original ticket and
remote handled time. The owner must revalidate permission/source at the actual
local driver completion boundary as well.

Revocation invalidates pending command completion. A correctly addressed disarm
reply handled no earlier than the explicit revoke request proves the remote write
has stopped. `Disarmed` returns the abandoned ticket for cancellation, not for
successful command completion; the owner must then run its normal local disarm
barrier before accepting another grant. The helper never grants ownership or
substitutes for the local driver's state machine. Namespace binding comes from
the separately configured per-run Gazebo transport, not network authentication.

## Evidence

Debug, Release and ASan/UBSan each pass 170 remote-completion checks plus the
existing 410 codec and 887 plant endpoint checks. Cases cover identity mismatches,
permission/source loss, exact timeout boundaries, future and regressing clocks,
late/duplicate acknowledgments, overlapping writes, stale/wrong-session disarm
replies and explicit recovery. A new regression reproduced acceptance after a
clock regression following a rejected disarm reply; the gate now advances its
observed clock even for rejected replies. Failed attempts remain preserved.

Host evidence: `/var/tmp/research-stabilized-session-transport-k558gplk`.
All 108 files rehash; 408 compiled input hashes are recorded.
Receipt: `6f1b1b38e2f31e001ffc7414bdd1a53cf57f037225eebc2afa6adaf803de7e65`.

The real Gazebo probe now routes all command acknowledgments through this gate.
It passes 40 handled acknowledgments across two sessions, two expiries and two
disarms; 246 physical truth rows. Every handled ack still matches physical fixture
logging. The probe uses a declared constant local source revision; actual uORB
revision and selected-health binding belong to the pending PX4 integration.
Evidence: `/var/tmp/research-stabilized-session-plugin-vquwnabm`.
All 116 files rehash. Receipt:
`d09d330b9395c4416c491fa64b4bcb50659cbf903cd48064bde3bff20faca332`.

## Remaining integration

Implement the independent PX4 consumer queue with bounded remote-message queues,
actual selected-state guards, publisher lifecycle bindings and coherent grant
responses. Hold the local write pending until a `Handled` result; on fault send
remote revoke, wait for matched disarm, cancel abandoned I/O and complete the
local disarm barrier. Keep the producer/session on its separate schedule. Then
connect the control core and qualify the complete dynamic and console path.

No PX4 process or hardware was operated in these tests. The qualified default,
stock firmware, bench/H10/acceleration gates and original physical fixture source
remain unchanged. Real vehicle measurements and full-image resources are pending.
