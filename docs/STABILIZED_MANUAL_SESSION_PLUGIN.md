# Session-aware Gazebo plant bridge

2026-10-20, Codex. Real Gazebo open-loop integration passes. PX4 session/consumer
connection and closed-loop qualification remain pending.

## Runtime integration

The private `stabilized_session` build now generates a separate
`RotationalSessionFixture` from the existing physical fixture using guarded exact
source replacements. The original plugin and qualified console source remain
unchanged. Synthetic inertia, force/moment map, servo lag, damping, sensor models,
translation restraint and physical truth logging are retained. Only the output
transport and lease owner are replaced by `PlantEndpoint`.

`GazeboPlantTransport` owns a bounded callback queue. Gazebo transport callbacks
only enqueue records; the simulation step alone mutates the endpoint. Each run
uses a new namespace supplied out of band in the SDF and a separate transport
partition. This isolates delayed traffic from previous runs; it does not provide
publisher authentication or permit namespace reuse. The isolated fixture admits
only its fixed configuration; production selected-input/permission policy remains
on the pending PX4 consumer integration.

Explicit grant and revoke requests use separate request traffic. Grant replies
confirm endpoint acceptance; disarm replies are emitted only after handled
zero-target/zero-thrust output. The plugin polls the endpoint every step, even
without messages. Queue overflow, malformed requests and conflicting queued
commands inhibit output. Command decoding, immutable lease/thrust matching,
sequence continuity and expiration retain the endpoint's existing checks.

Each PreUpdate prepares one write, sets virtual actuator targets, calculates the
fixture's resulting wrench and calls Gazebo AddWorldWrench. Only then does it
complete the matching ticket and publish a command acknowledgment. Missing pose
or velocity completes as failed. This acknowledges software submission to Gazebo,
not measured force, electrical output or completion of servo dynamics. Disarm
sets target and thrust to zero; the physical surfaces still follow their declared
lag and passive damping remains active.

## Verified real-process behavior

`verify_stabilized_session_plugin.py` builds the strict-warning plugin and client,
runs the existing 410 codec and 887 endpoint checks, then launches an isolated
headless Gazebo server. A C++ transport probe performs two explicit sessions:
20 acknowledged commands each, rejected expired competing grants, independent
lease expiry, a delayed command that cannot restore the session, revoke, handled
disarm and successor grant. The test audits every acknowledgment against the
physical fixture CSV at its handled timestamp: exact session/command time,
matching four targets and fixed 10 N thrust. Both expiry boundaries show zero
targets and zero thrust. Physical rotation is observed.

Final evidence: `/var/tmp/research-stabilized-session-plugin-z8d2g7rv`.
40 acknowledged commands, two sessions, two expiries and two disarms; 243 truth
rows. Maximum physical roll rate is 1.442891376 rad/s in this open-loop exercise;
this is an observed response, not a tracking acceptance threshold.
All 110 evidence files independently rehash, including generated plugin source,
compiled artifacts, source snapshots, world, probe/physics logs and dependency
inventory. Manifest SHA-256:
`2196706dbc4f5243c5dc35675ded2c56555dbbd997cd50145253a6112ce3febc`.
The prior fresh run also passed and is retained at
`/var/tmp/research-stabilized-session-plugin-05dyavmj`.

## Next step

Connect the PX4 session/independent consumer and coherent grant handshake to this
plant namespace. Preserve selected-state validation and pre/post-write permission
checks, bind producer acknowledgments to the actual handled remote command, and
handle asynchronous completion without blocking the output work queue. Verify
clock alignment, timeout/restart/revoke paths, then run the complete closed-loop
maneuver and console campaign before changing the default.

No PX4 process or hardware was operated in this plant-only test. Current console
admission still passes against its preserved qualified pair. Production hardware
integration, ARM full-image resource check and vehicle measurements remain open;
stock, bench, H10 and acceleration gates are unchanged. Changes are uncommitted.
