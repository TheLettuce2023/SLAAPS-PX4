# Selected-input health at both output endpoints

Updated 2026-10-20 by Codex. Software-only, uncommitted follow-on to 472a7d6786.

The native scheduled-output fixture now feeds actual lifecycle-bound uORB inputs
to the controller session and the independent mixer consumer. Each endpoint owns
a separate `SelectedStateInput` and `SelectedStateGuard`. The guard reuses the
controller's extracted numerical-state and manual-input validation without running
the controller or advancing its integral/reference state.

Explicit grant acceptance requires centered sticks, valid configuration, fresh
selected state and manual input, and the disarmed/prearmed servo prerequisite.
Once bound, changes to publisher lifetimes, selected sensor identities, estimator
time continuity, supported estimator parameters, or numerical/input health latch
a failure. Restoring healthy inputs does not recover ownership: revoke/disarm and
a new explicit handshake are required. The consumer checks independently at the
output permission boundary, including while the controller work queue is stalled.
This remains a single-estimator research prerequisite, not reviewed flight health.

## Verification

- Debug, Release and ASan/UBSan: 126,287 host checks per configuration.
- Native: 45 scenarios twice, 11,322 assertions and 2,757 handled acknowledgments.
- Eight added scenarios cover bad estimator health, stale manual input, NaN
  attitude, attitude publisher restart, selected device change, controller stall
  during health loss, estimator profile change and fresh-but-regressed estimator time.
- Existing output-disabled observer: 14,464 checks, zero failures, same final ELF.
- All 735 sealed native files and 231 ARM evidence files independently rehash.
- PX4 astyle reports the five affected C++ files unchanged after formatting.
- ARM GCC 13.2.1, pinned px4-dev v1.17.0, working e8ea/NuttX 11 baseline:
  guard wrapper text 1,012 bytes; guard instance 408 bytes; shared validator text
  738 bytes; controller text 3,626 bytes. These are unlinked object measurements,
  not complete-image resource or runtime stack measurements.

Native evidence: `/var/tmp/research-stabilized-output-native-health-20261020`.
Manifest SHA-256: `d1ad43871180a22b3d1eb7d0cf827418a2369fa49d288f6140ee20f2b1374b4e`.
Final ELF SHA-256: `e9388865af929b44f52e26ccdaa3262b8604463f9731c09c23394305a9762b4a`.
ARM evidence: `build/stabilized_selected_health/20261020-final`.
Receipt SHA-256: `3f952307add7ebcf93cd32aeadaebf465debe08f9c7a603c11dc808e34c1bc60`.
Recipes and combined rehash results are retained there. Initial compiler failures
are preserved: a controller-local bounds helper was restored, and an overly broad
ARM include overlay was narrowed to avoid mixing baseline math headers.

## Remaining work

The numerical validator extraction subsequently passed [fresh dynamic qualification
and console verification](STABILIZED_MANUAL_VALIDATOR_REQUALIFICATION.md). The
default is now promoted to that tested pair; the previous profile is preserved.
This native fixture still uses synthetic inputs, a fixture-owned permission topic,
and a memory-only driver; it does not demonstrate plant stabilization.

Production mode requests/profile selection, authoritative permission policy,
independent scheduling and physical driver attachment remain to be integrated.
A full ARM link/resource review is required with that active path included.
Vehicle authority, inertia, loaded servo calibration, timing and operator limits
remain pending; no hardware was operated and no physical authority was granted.
