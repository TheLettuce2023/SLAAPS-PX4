# C1H proposal: output-disabled live host-loopback qualification

Status: **approved for bounded C1H design, implementation and host-only runs**
by the user's subsequent "Proceed" instruction (2026-09-20). Approval does not
include PX4 attachment, an operational clock provider or hardware/flight control.
Implementation and evidence are tracked separately; approval is not a test pass.
The implemented bounded experiment is now verified in
C1H_HOST_LOOPBACK_VERIFICATION.md; the next unapproved authority scope is
C1_PX4_ATTACHMENT_PROPOSAL.md.

## Requested scope

Approve detailed design, implementation and bounded runs of an isolated host
MAVLink loopback experiment with real framing/signature checks and measured
host timestamps. It must remain separate from PX4/Gazebo/QGC/TELEM and hardware.
Master design/source review must precede implementation of the live owner and
parser binding. Approval is for this experiment, not full C1 PX4 attachment.

Proposed constraints for approval:

1. One existing WSL environment only. Bind IPv4 loopback `127.0.0.1` with
   OS-assigned private test ports. No remote address, public interface, firewall
   change, existing simulator/telemetry port, or discovery broadcast. Record
   nonsecret run configuration and clean up only owned processes/resources.
2. One logical sender and one serialized admission owner, with the approved
   cooperative-local independent operator-consent mechanism. Guidance cannot
   establish/revoke/clear its own authority. Harness-only state is labelled
   simulated, never PX4 telemetry; no arm/mode/control/output capability exists.
3. Only private test identities: candidate MAVLink sender `(42,191)`, receiver
   `(1,1)`, per-run source/session/controller identifiers and explicit channel
   binding. Offline-local message IDs remain private to this isolated dialect.
   These values are not approved Pi/TELEM/PX4 deployment assignments.
4. Fresh per-run test signing material; never read/reuse a hardware key or log
   secret bytes. Use only an owned private temporary location if the key must
   cross process boundaries. Terminate the run and invalidate its context on
   teardown/restart. The detailed design must specify replay/link-ID and key
   lifecycle tests before admission is enabled. Cooperative same-user trust
   does not claim protection from a hostile process with that user's access.
5. Qualify a shared host monotonic-clock domain for the host sender/validator;
   do not treat it as a Pi-to-PX4 synchronization solution. Clock implementation,
   quantization, reset/epoch handling and whole-lifetime uncertainty justification
   must be reviewed. A measured offset sample or the synthetic 4 ms ceiling
   alone cannot authorize a map. If a bound is unsupported, keep it invalid
   and test rejection; never fabricate clock evidence to admit traffic.
6. Keep the approved timing profile as predeclared experimental targets.
   Measure creation/send/receipt/admission/access/status return and declared
   uncertainty provenance; report misses and incomplete evidence without
   relaxing the targets. Distinguish actual elapsed host time from virtual
   injection. Host results do not qualify Pi scheduling, serial baud or controls.
7. Use real authenticated parser results, never injected `true` verdicts, for
   live packets. Test unsigned/bad-signature/replayed/foreign/restarted traffic
   and status loss/backpressure. Unknown identity or clock provenance fails
   closed. Real decoded request access remains only a logged host result.
8. No active flight module, uORB objective or torque/thrust/actuator publication;
   no simulator/firmware build, hardware, package installation or repository
   commit/push/PR. Existing offline regressions and Cessna resources stay intact.

## Exit and exclusions

This experiment can establish bounded host transport/parser/timestamp evidence
and expose integration gaps. It cannot prove operational control deadlines,
whole-vehicle state provenance, PX4 mode ownership, plant feasibility, physical
fallback, Pi/serial performance or acceleration tracking. Those retain their
own approval/evidence gates.

The host-only authority decision is recorded above. No geometry measurement is
required for this experiment. A later PX4 attachment remains separately gated.
