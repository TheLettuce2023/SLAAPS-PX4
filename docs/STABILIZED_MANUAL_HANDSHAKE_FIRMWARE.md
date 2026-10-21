# Stabilized manual: current-core and grant-schema firmware

2026-10-20, Codex. A clean Pixhawk 6X firmware build now includes the current
control core and registered grant request/response schemas. It remains an
output-disabled observer image; active stabilized session/driver and synthetic
test publishers are excluded. It has not been flashed or hardware-tested.

## Source and isolation

Target: `px4_fmu-v6x_default`, board ID 53. Source baseline is
`e8ea98b404baf50623a519ac67ec1c03aa240859` with its working NuttX 11/apps pair.
The checkout remains `slaaps-sitl` at `472a7d6786` with uncommitted development;
that commit alone does not identify this explicitly dirty overlay build.

The 103-file overlay includes 54 exact current-checkout inputs bound to the
[final native handshake evidence](STABILIZED_MANUAL_GRANT_HANDSHAKE.md).
Older-baseline ports, the PX4IO handled-result correction and startup paths retain
their prior verified bytes. Source is copied without build objects into the fresh
Docker volume `px4-stabilized-handshake-e8ea-20261020`. Donor/workspace Git metadata
are read-only. Before/after guards check overlay hashes, base commit, required
submodule revisions/cleanliness and unchanged startup paths.

Build image: `px4io/px4-dev:v1.17.0`, ID
`sha256:288dc1d7f5290855e86dbaf8b8f55cd17f46f27e9df2affb2056df6363bc6fb0`.
ARM GCC 13.2.1; network disabled; retained Micro-CDR source at the verified v2.0.1
commit is rebuilt through an invocation-local mapping. No compiled donor object
is reused. Exact Docker arguments, source manifests, overlay, recipes, dependency
archive, build log, ELF/bin/map and compiler commands remain in the stage.

## Linked resources

| Measure | Previous lifecycle image | Current image | Change |
| --- | ---: | ---: | ---: |
| Decoded image | 1,962,496 B | 1,962,816 B | +320 B |
| Flash headroom | 3,584 B | 3,264 B | -320 B |
| ELF text | 1,956,672 B | 1,956,992 B | +320 B |
| Static data | 5,824 B | 5,824 B | 0 |
| Static BSS | 104,816 B | 104,816 B | 0 |
| Observer object | 1,496 B | 1,504 B | +8 B |

The ARM layout comparison uses each image's actual compilation flags. uORB node
and diagnostic payload sizes remain 60 and 160 bytes. Object sizes exclude
allocator overhead; these checks do not measure runtime heap, stack high-water
or scheduling latency. The active supervisor/session/output driver remains absent,
so its total flash/RAM cost is not established by this remaining margin.

Export checks both new `__orb_stabilized_grant_*` symbols, required observer/core
symbols, excluded bench/test/active-output symbols, NuttX version, board identity,
decoded package/bin equality and capacity. No flight or output authority is added.

## Artifacts and independent verification

Stage: `build/stabilized_handshake_firmware/e8ea-20261020`.

- [Firmware package](../build/stabilized_handshake_firmware/e8ea-20261020/artifacts/px4_fmu-v6x_default_e8ea98b404_dirty_observer_handshake_20261020.px4),
  1,858,911 package bytes; SHA-256
  `1f47ee9ea8d12f428b3edc8682149c034a7d0ade52ab7eaed5f4afd356ed3b15`.
- Firmware receipt SHA-256: `391b55fe9e1bde9ad1d1aacff933bc6e32f3806726a7d938afa94a3325a6adf5`.
- Layout receipt SHA-256: `b66c2c0b81bc8a85192d83bedf10f27d60ab57b6ba87426f4c610e822bcb7ab9`.
- Independent verification SHA-256: `eba6de282d1fe9e1753e98735c8ad9949229a4a74233832f8da9bc6b19afe3c9`.

All 131 firmware receipt files, 361 layout files, 103 overlay entries and 54
current-root sources independently rehash. All 14 retained stock, bench and
previous observer artifacts are unchanged. The uniquely named temporary copy is
`/tmp/px4-firmware/px4_fmu-v6x_default_e8ea98b404_dirty_checkout472a7d6786_handshake_20261020T081821Z.px4`;
this temporary path may disappear on reboot. Preserve the durable stage and
Docker/workspace evidence together.

## Next boundary

Integrate deliberate production mode/profile requests, authoritative selected-state
health, independent scheduling and physical driver attachment. Repeat the full
resource check with that active path linked; the current margin is only 3,264 bytes.
The qualified synthetic console and all hardware/bench/acceleration gates retain
their scope. Vehicle geometry/inertia, control authority, servo dynamics and timing
measurements remain pending; no new human input is needed for the next software work.
