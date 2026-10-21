# Stabilized manual: dynamic core requalification

2026-10-20, Codex. The control-core monitoring changes accompanying
`StabilizedManualSession` now pass a fresh dynamic SITL qualification. The default
operator console has been promoted to the tested pair. This campaign exercises
the existing dynamic fixture owner with the updated core; it does not dynamically
integrate the new session wrapper or production mixer/driver path. Their separate
native coverage remains in [the session record](STABILIZED_MANUAL_CONTROLLER_SESSION.md).

The default was subsequently refreshed by the [shared-validator requalification](STABILIZED_MANUAL_VALIDATOR_REQUALIFICATION.md). This record preserves its original evidence.

## Evidence

A fresh checkout snapshot verifies 58,107 inputs and 600 preparation anchors.
Build evidence: `/var/tmp/research-stabilized-sitl-af0qwcyf`.
Fourteen native uORB checks and five focused dynamic cases pass over 14,374 cycles.
All 387 focused receipt artifacts rehash. Build receipt SHA-256:
`db12e8b7420836b46a28789609dea8d6fe06946554ab94d9a4cb86e2dc028707`.

Full qualification: `/var/tmp/research-stabilized-sitl-_cgf_4k7`.
All 31 cases were run freshly: one duplicate-dispatch case, four coupled maneuvers,
twelve individual-axis maneuvers, six fault/recovery cases and eight estimator
lifecycle/reset/profile barriers. They pass over 68,648 audited control cycles.
All 1,761 receipt artifacts independently rehash. Receipt SHA-256:
`953299045db41b5ba943c755a82bbd8909630643c00bc67877d590b036732e6f`.

Worst coupled physical rate error is 0.028285 rad/s against 0.08; final physical
hold error is 0.004503 rad against 0.02; peak error is 0.490994 rad against 0.5.
All declared component audits pass. No acceptance threshold was changed.

The actual console passes sticks, hold capture/retention, invalid-input handling,
disable, deliberate restart and cleanup:
`/var/tmp/research-stabilized-console-check-p942m7z8`.
Console receipt SHA-256:
`c7b7a35d3c1e2a6de037b78560e7e783d685bec8cdfaeb3b2d65ee800d75415e`.
Its check and session artifacts independently rehash. The promoted default profile
matches this tested profile exactly. The previous default is preserved at
`build/stabilized_manual_session/20261020/previous-qualified-console-profile.json`
and its original qualification package remains unchanged.

PX4 ELF SHA-256: `3f5705e4eade911cf9466662060038aebe8b4465a2246dda379ee2718f633ad3`.
Gazebo plugin SHA-256: `f103c6dcfcdbee97c9114486e09dc0a6a67d9f73938b333684c519fc8cc69f21`.
The [checkout build](STABILIZED_MANUAL_CHECKOUT_BUILD.md) and
[qualification workflow](STABILIZED_MANUAL_CHECKOUT_QUALIFICATION.md) reproduce
this scope using the new receipt and directories above.

## Remaining boundary

Body-rate commands and all-axis quaternion hold on centered sticks retain the
20 m/s prescribed-airflow, fixed simulated 10 N EDF, restrained-translation scope.
Production request/session transport, authoritative health, physical driver
attachment, independent scheduling and a complete-image resource check remain.
Measured geometry/inertia, canard authority, loaded servo dynamics and physical
timing are still pending. Stock/bench artifacts, H10 and acceleration gates are
unchanged. No firmware was flashed or hardware operated.
