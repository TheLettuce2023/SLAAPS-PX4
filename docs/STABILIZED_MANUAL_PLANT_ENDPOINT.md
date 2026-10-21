# Dynamic bridge: handled-output plant endpoint

2026-10-20, Codex. Private endpoint implemented and tested; Gazebo plugin wiring
and complete dynamic session integration remain pending.

`Tools/research_sitl/stabilized_session/PlantEndpoint.hpp` wraps the existing
`StabilizedOutputDriver` transaction state machine. It does not introduce a second
lease/sequence implementation. An immutable output configuration, configuration
identity and fixed simulated thrust are supplied at construction. This endpoint
requires disarmed fallback. It is a serialized owner that must be retained across
producer replacement; asynchronous callbacks must enqueue work for that owner.

An explicit grant requires a matching configuration identity, unexpired deadline,
valid session time, permission, and a previously handled disarmed write. Command
traffic cannot grant ownership. Commands pass through the versioned protobuf
codec, must match the configured lease and thrust, then undergo the existing
consumer's session, next-sequence, timestamp, lease and mapping checks.

Each simulation step must call prepare even without incoming traffic. It returns
a ticket, candidate and simulated thrust; fault/expiry/permission loss produces
disarmed output and zero thrust. The caller must recheck permission and pending
validity immediately before applying the candidate. Only completion of the exact
handled write can generate an acknowledgment. Late, failed, canceled, revoked or
permission-lost writes never acknowledge commands. An acknowledgment reports
software acceptance, not measured force, servo position or electrical feedback.

Revoke invalidates pending authority. A fresh handled disarm is required before
successor ownership. Faulted endpoints do not recover from healthy command traffic.
Invalid configurations cannot grant. Replayed session tokens remain rejected by
the retained driver/consumer high-water marks.

## Verification

The test links the actual portable producer, consumer, mapper and split driver.
Real protobuf serialization connects producer frames to the endpoint and returns
handled acknowledgments to the producer. Tests cover ten successive transactions,
held output without duplicate acknowledgments, independent lease expiry, late or
wrong-ticket completion, cancellation, failed handling, permission loss, revoke,
malformed/profile/lease/thrust/sequence/session traffic, recovery, ungranted input,
pending-write replacement, replayed grants and invalid configurations.

Debug, Release and ASan/UBSan each pass 887 endpoint checks plus the existing 410
codec checks. Final evidence:
`/var/tmp/research-stabilized-session-transport-frm9uhi9`.
Manifest SHA-256: `3f479d3d327ffca18549a7fa81ba2da388e600bdb598f0a76c1b51514f2c06a2`.
All 97 evidence files independently rehash; 406 compiled input hashes are recorded.
The runner retains earlier attempts and the previous codec-only evidence.

## Next integration

Install the endpoint on the Gazebo simulation step, with transport callbacks only
queuing immutable requests. Add explicit simulator-start identity, grant/revoke
messages and handled-disarm replies; data messages cannot authorize a new plant
instance. Connect the independent PX4 consumer/session to this endpoint and retain
selected-input checks at the actual write boundary. Verify queue/transport loss,
restarts, clock alignment and finite acknowledgment timing with real PX4/Gazebo
before running the full maneuver matrix. Do not replace the qualified default
until the complete path passes its own dynamic and console qualification.

The existing dynamic plugin, controller owner and qualified console are unchanged.
These tests launch no simulator or hardware. Production scheduling/driver policy,
full ARM image resources and measured vehicle inputs remain open; stock, bench,
H10 and acceleration gates remain preserved.
