# C1H: reviewed host-loopback design

2026-09-20. Master design review: approved for implementation within the user's
C1H authority. This is not implementation verification or a Phase 2 exit.

## Boundary and ownership

One existing WSL process has three cooperative capabilities: guidance has only
a loopback socket, disposable test signing material and status input; the
trusted scenario runtime owns one native admission context; an independently
scheduled operator test driver has console capability. Guidance never invokes
operator actions or supplies trusted state/authentication flags. Operator
actions are predeclared and logged, never inferred from establishment traffic.
This is cooperative-local testing, not hostile same-user isolation or a
deployed interactive console.

Two UDP sockets bind 127.0.0.1, OS-assigned ports, with exact peer binding.
No PX4, Gazebo, QGC, TELEM, hardware or non-loopback endpoint is opened. One
logical sender and one serialized receiver own their signing streams. Fresh
32-byte test keys stay in memory; restart gets new key and nonzero run/source/
session/controller identities. Logs contain neither keys nor raw signed frames.
Private link IDs are 7/8; headers are 42/191 and 1/1, not deployment assignments.

The native wrapper owns HarnessSupervisorOwner, broker and console facade.
Its single by-value WireAdapter remains the only guard. Extend the owner with
guarded ACK/request/access and optional tick reports; no adapter reference,
reset, supervisor event or authentication Boolean crosses the C ABI. Existing
offline callers retain behavior. State is labelled harness-simulated; simulated
arming changes no PX4 state. Request access is only logged, never published.

## Parser source review and decisions

Reuse C0c revision/cleanliness/XSD/layout/import-origin checks to generate the
private dialect. Do not modify vendored sources. Generated C parsing lives
inside the raw-frame native boundary. Require signed MAVLink 2, exact single
datagrams, supported flags, directional IDs and contract payload lengths,
CRC and actual signature success. Reject MAVLINK_NO_SIGNATURE_CHECK; require
key/replay pointers since the pinned C parser otherwise permits signed input
without a signing context. No unsigned override callback is allowed.

Transactionally copy signing/replay state per datagram; commit only after MAC,
CRC, header/link/channel/target/payload source/session and current controller/
transition/map binding. Invalid, foreign and stale traffic cannot poison replay.
Signing timestamps are separate 48-bit/10-us replay metadata, not freshness.

Reverse status uses generated Python verification, candidate replay copies and
the strict existing codec. The pinned Python generator updates replay state
before digest verification; UNKNOWN returns before CRC/signature verification;
get_payload() starts at offset 6 rather than 10. Check envelope first, use the
authenticated raw payload at offset 10, and reject MAV_IGNORE_CRC even "0".
These reviewed source facts require wrappers, not claims that C0c proved signing.

## Clock qualification and measurements

Python monotonic_ns and native clock_gettime(CLOCK_MONOTONIC) share one process
and Linux time namespace. Record provider/Python/kernel/boot/namespace/offset/
clocksource provenance and bracket native readings. Unsupported provenance,
regression or coverage fails closed. Native takes the actual evaluation sample.

Identity offset=0, U=1 us is a representation bound relative to this one returned
timeline: C=1000*floor(C/1000)+r, 0<=r<=999 ns. Interval quantization error
(r2-r1)/1000 us has magnitude below 1 us for any pair, including every 80-ms
lifetime. This is not oscillator/physical accuracy, scheduler latency, VM-pause
qualification or Pi-to-PX4 synchronization. CLOCK_MONOTONIC excludes suspend;
reported resolution does not prove accuracy. Keep a fixed predeclared map
interval covering ACK and all creation/expiry endpoints.

Record real creation, send brackets, receive observation, native admission,
access and status return; score completion conservatively. No scheduling delay
is hidden in U. Fault-injected packet timestamps are labelled. After ACK, wait
for a fresh read beyond its uncertainty interval, never manufacture a timestamp.

## Predeclared checks

- Signed establishment alone cannot authorize. Independent attach/prepare/
  confirm is required; simulated armed transition requires fresh ACK of the
  actual barrier. Valid requests reach native access with identical CG inertial
  FRD values and sequence. Actual reports produce signed returned status.
- Reject unsigned/bad-signature/key/CRC, replay/equal/lower time, malformed,
  trailing/concatenated/split frames, unknown IDs/flags/lengths, foreign peers/
  headers/links/targets/payload identity and stale context. Forged high-time
  traffic followed by valid traffic must demonstrate no replay poisoning.
- Test duplication, stale demand, silence, missing ACK, simulated state loss,
  independent revoke and actual producer-regression latch clearing. Restart
  uses fresh key/context and rejects old traffic. No guard reconstruction.
- Status loss and bounded queue backpressure cannot alter admission. Queue
  sizing/drop policy is explicitly test scaffolding. Log omitted deliveries.
- Retain predeclared 50-Hz/20-ms target, 24-ms maximum release gap, 12-ms receive,
  4-ms admission, nominal 20-ms age, hard 60-ms age, raw 80-ms validity, observer
  target 1-ms/max 2-ms, and no-fresh diagnostic 40-ms. Every measured miss or
  incomplete criterion fails that criterion; never relax targets after a run.
- Strict host builds, offline regressions and independent review are required.
  No firmware/simulator build, install, commit/push/PR or physical operation.

## Bounded scheduling refinement

Measurement-driven harness refinement: three unchanged-limit campaigns found
between-call observer overruns; explicit GC callbacks measured a 1.050845-ms
generation-1 collection inside a 2.279-ms gap. The next bounded experiment
defers cyclic collection only during the nominal campaign, collects before it,
restores the prior process setting in finally, caps trace storage at 20,000
rows and rejects a campaign exceeding two host-clock seconds. Reference
counting remains active. No threshold changes, OS tuning, package installation
or operational real-time guarantee follows. Retain all earlier failures and
verify one Debug and three Release campaigns after this change, not retries
until a favorable sample appears.

## When PX4 and Gazebo connect

The stock gz_rc_cessna baseline already has prior project evidence and stays
unchanged. C1H is about the new research path, not first-ever SITL use. After
C1H evidence/review, propose separately approved C1 output-disabled PX4
attachment: receiver integration and PX4-owned state/time/loss provenance,
still no actuator publication. Custom Gazebo closed-loop work additionally
needs approved plant inputs/ownership, controller architecture, normalized
allocation, single publishers and loss-response evidence. Geometry can remain
deferred here, but cannot be invented to release plant or active-control gates.
Ultimately verify acceleration through estimation/control/allocation/outputs
and plant response, not just MAVLink reception.
