# Shared-validator dynamic requalification

2026-10-20, Codex. The extracted numerical/manual validator used by the
controller and independent selected-input guard now passes a fresh dynamic SITL
campaign. The tested console profile is promoted to the default. No acceptance
threshold changed; no hardware was operated.

This campaign exercises the current controller in the existing rotational fixture
owner. The new session, grant handshake, selected-input guards and production
mixer consumer retain their separate native tests; they are not yet integrated
into this dynamic owner. Vehicle-supported tuning still requires measurements.

## Verified evidence

- Fresh build: `/var/tmp/research-stabilized-sitl-e4edt0c8`; 58,114 inputs and
  633 graph anchors checked before and after build. Fourteen uORB self-checks and
  five focused dynamic cases pass over 14,393 cycles; all 387 artifacts rehash.
  Build receipt: `3f37724088623cdab858b4541ca36285208cb7f41abe075b043ac82fe56c6c30`.
- Full campaign: `/var/tmp/research-stabilized-sitl-2u88ldfk`; all 31 cases fresh,
  68,761 audited cycles, all 1,768 artifacts rehash.
  Qualification receipt: `a725a1ff7c1f05f196c5b07b147a2b1fb9a749142f580abba8045ea13a94c970`.
- Console: `/var/tmp/research-stabilized-console-check-g4zkgvxs`; sticks, hold,
  invalid input, disable, explicit restart and cleanup pass. All 5 check and
  41 session artifacts rehash.
  Console receipt: `c3cbe9b44cbab3da7e57e5e1cf7e7a8146f9903fb63ee15e26b81db6de19ce72`.
- PX4 ELF: `40d5334904a467d32a9797f457845747e8ac47a35318d0698601e5dbb0a7611d`.
- Gazebo plugin: `a12dbf5eb8aefbd79df3d7187a0f731433be013171a7bb00fdfd517a2e540f32`.

Worst coupled physical rate error: 0.028249241 rad/s (limit 0.08).
Worst final hold error: 0.003131102 rad (limit 0.02).
Worst peak error: 0.497000768 rad (limit 0.5).

The previous profile is preserved in
`build/stabilized_selected_health/20261020-requalification/previous-qualified-console-profile.json`;
its original evidence remains unchanged. The adjacent `verification.json` records
the independent rehash and promotion. The current default matches the tested
profile byte for byte. See [operator instructions](STABILIZED_MANUAL_ISOLATED_MODE.md).

## Next integration

Carry the verified session/handshake and independent consumer health boundary into
the dynamic fixture, then production mode/profile requests and permission policy,
independent scheduling, and physical driver attachment. Recheck the complete ARM
image with the active path linked; current full-image headroom is limited.
Synthetic airflow remains 20 m/s with fixed simulated EDF thrust 10 N and restrained
translation. Real authority, inertia, loaded servo dynamics and timing remain open.
Stock, bench, H10 and acceleration gates are preserved. Changes are uncommitted.
