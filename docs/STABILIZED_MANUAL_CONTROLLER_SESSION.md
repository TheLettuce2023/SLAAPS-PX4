# Stabilized manual: controller session and acknowledgment binding

2026-10-20, Codex. `StabilizedManualSession` owns the control core and output
producer protocol. An explicit grant binds the session to a nonzero acknowledgment
publisher lifetime. Atomic payload/revision copies and current-source checks reject
withdrawal, restart and mismatched acknowledgment metadata. Session identifiers
remain monotonic across disable; a fault requires explicit disable and a new grant.

While waiting for output completion, live manual/state/permission checks continue.
The new core `monitor` path checks freshness and observation ordering without
advancing PID integration, rate slew, allocation history or the held quaternion.
A same-clock acknowledgment does not cause a duplicate control calculation.
Failures disable the producer and core; the independently scheduled consumer still
owns output expiry and disarm. This portable session does not grant authority or
replace the production supervisor, request transport or health policy.

## Verification

- Debug, Release and ASan/UBSan each pass 126,287 host checks, including 164 new
  session checks and comparisons against an independently advanced control core.
- 34 native scenarios repeated twice pass 5,254 assertions and 1,435 acknowledgments.
  Added cases cover acknowledgment restart, copied revision mismatch and invalid
  state while waiting. The same final ELF passes 14,222 observer checks.
- All 547 sealed native evidence files and 147 ARM receipt files independently
  rehash. Native evidence: `/var/tmp/research-stabilized-output-native-session-20261020`.
  Manifest SHA-256: `b38bdcca07a9521a574ee26f6cb6bb5911b6334d3bdc5e0178f8134bad557e09`.
- ARM evidence: `build/stabilized_manual_session/20261020`; receipt SHA-256:
  `321ef4c0a9ce0461cfc34e6718337bcc39200455d9cd81dae935d8ebd141b498`.
  Core text grows from 4,226 to 4,290 bytes; session text is 1,014 bytes.
  Core instance is 872 bytes and session instance, including its core, is 1,120.
  The largest individual session static stack frame is 272 bytes; this is not
  a runtime stack high-water measurement. No complete firmware image was linked.

## Remaining integration

The changed control-core monitoring now passes [fresh dynamic SITL qualification](STABILIZED_MANUAL_SESSION_REQUALIFICATION.md); the default console is promoted to the tested pair. The new session wrapper retains separate native coverage and is not yet integrated into the dynamic owner.
Next are production request/controller/acknowledgment integration, authoritative
health, physical driver attachment and independent scheduling, followed by a full
image resource check. Native tests use an in-memory sink and synthetic authority;
they establish no physical PWM or vehicle-control acceptance. No hardware was
operated. Vehicle geometry, authority, servo dynamics and physical timing inputs
remain pending. See [source ownership](STABILIZED_MANUAL_SOURCE_OWNERSHIP.md).
