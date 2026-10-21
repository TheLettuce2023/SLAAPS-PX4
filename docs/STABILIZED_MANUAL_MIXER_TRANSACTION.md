# Stabilized manual: mixer transaction hook

Follow-on: [reusable MAIN PWM adapter](STABILIZED_MANUAL_MIXER_ADAPTER.md)
implements candidate/configuration validation and completion in the production
library, tested with an eight-channel memory driver. Physical attachment and
the supervisor remain pending; evidence below retains its original scope.

2026-10-20, Codex. The production mixer now has an optional validation and
completion hook. The attached policy and driver remain a private native test
fixture; no production hardware adapter or supervisor has been installed.

## Behavior

`ResearchOutputTransaction` validates the final bank after mixing, limits,
arming and actuator-test overrides, immediately before the driver call.
Rejected candidates are replaced with zero PWM across the bank, and report
unsuccessful completion even if that substitute write succeeds. A subsequent
validated, handled disarm is required to release ownership.

The completion hook receives the driver's actual return value and a clock
sample taken after it returns. The existing prepared ticket binds that result
to the command. Revocation or permission loss during handling cannot acknowledge
the command or confirm disarm.

Attachment is permanent for the mixer lifetime, restricted to a MAIN PWM bank
of four to eight channels, and must occur before its first update. Replacement,
late attachment and AUX attachment are rejected. Once attached, removal of every
output function cannot suppress the bank's disarm dispatches. Calls and lifetime
management must remain on the serialized driver context.

`CONFIG_RESEARCH_STABILIZED_OUTPUT_HOOK` defaults off. Enabling it only makes
the interface available; it does not install a guard or grant authority.

## Verification

The private four-channel native fixture validates pending tickets, actual
mapped PWM values, routing, calibration, reversal, arming-state freshness,
permission and actuator-test exclusion. It uses the real mixer with a memory
driver. This is a test policy, not a reviewed vehicle health policy.

All **26 accepted fresh-process runs** pass **1,174 assertions**, with **537
handled command acknowledgments**. Thirteen scenarios run twice: explicit
revoke, producer stall, consumer stall, lost acknowledgment, rejected write,
slow write, permission loss, duplicate command, revoke during handling,
permission loss during handling, calibration change, removal of all routing,
and competing actuator-test control. Each checks disarm and successor handoff.

An initial slow-driver run exposed a test synchronization race: a fixed 30 ms
wait could inspect a stale revoke flag while the driver was still sleeping for
120 ms. The harness now waits for explicit consumer processing of the request
before checking rejected disarm. Failed logs and the original source snapshot
are retained alongside the accepted rerun.

Debug, Release and ASan/UBSan each pass **126,123 host checks**. The existing
observer on the same native binary passes **14,158 checks**, including hidden
restarts of its seven publishers.

Native evidence: `/var/tmp/research-stabilized-output-native-hook-20261020`.
Manifest SHA-256:
`13a21f4b91c5e164d53eb0e2e55aaf5fbe6013133c197590a5bff912c21b2af4`.
ELF SHA-256:
`a4435e1815fcbcd50bb11a6c7fd2b699be7fe143d00a53d33882bf56be17cf9a`.
All 405 manifest-listed files independently rehash.

Three complete mixer translation units compile using the retained working
`e8ea98b404`/NuttX 11 ARM build environment. Baseline and hook-disabled objects
both contain **8,233 text bytes**, and their disassembled machine code and
relocations match exactly. Hook-enabled text is **8,373 bytes**, an increase
of **140 bytes**, with zero static data/BSS. This excludes attached adapter
code, object-instance memory and final link effects.

ARM evidence and reproduction recipe:
`build/stabilized_mixer_hook/20261020`. Receipt SHA-256:
`c47e10938c40baacaed47f662fa364c183c7de052268a2b02b4ff67250b6b8d4`.
All 246 receipt-listed files independently rehash. `verification.json` binds
the native and ARM results.

## Next work and limits

Implement the production driver adapter and supervisor, with explicit exclusive
bank ownership, reviewed grant/revoke and permission/health rules, independent
expiry scheduling, and confirmed disarm before handoff. Then build and budget
the complete image; the prior lifecycle-observer image has only 3,584 flash
bytes free and does not contain this new integration.

No firmware was linked or flashed in this increment. Numeric zero PWM and a
handled driver result are software evidence, not electrical shutdown or servo
feedback. Eight-channel hardware mapping, scheduling and physical timing still
need acceptance. Vehicle geometry, signed aerodynamic authority and loaded
servo measurements remain pending. The existing synthetic rotational SITL
qualification and physical bench gates are unchanged.
