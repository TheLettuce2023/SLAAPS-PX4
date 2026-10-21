# Stabilized manual: lifecycle-observer firmware

2026-10-19, Codex. A fresh Pixhawk 6X image links successfully with the
[seven-input publisher binding](STABILIZED_MANUAL_OBSERVER_LIFECYCLE.md) and
[PX4IO handled-result correction](STABILIZED_MANUAL_PX4IO_OUTPUT.md).
The observer remains output-disabled. This image has not been flashed, booted
or hardware-qualified.

## Source and build

The image uses working PX4 baseline
`e8ea98b404baf50623a519ac67ec1c03aa240859`, its NuttX 11/apps pair and the verified
92-file overlay. The active checkout remains on `slaaps-sitl` at committed
checkpoint `472a7d6786` with uncommitted development. The staged image is explicitly
labeled dirty; that commit alone does not identify its source.

Source is copied into a separate Docker volume
`px4-stabilized-lifecycle-e8ea-20261019`, excluding the donor's build directory.
The qualified donor and workspace/git metadata are mounted read-only. The build
uses pinned image `px4io/px4-dev:v1.17.0`, image ID
`sha256:288dc1d7f5290855e86dbaf8b8f55cd17f46f27e9df2affb2056df6363bc6fb0`,
and ARM GCC 13.2.1. No prior FMU build objects are reused.

The initial clean attempt stopped at the external Micro-CDR download because
network access was disabled. The retry uses the retained source at requested
tag `v2.0.1`, commit `3d1b17703c7cf4f22def2910bc845bdb5152d7b5`, through an
invocation-local Git URL mapping. It rebuilds that dependency without changing
firmware configuration or accessing the network. The failed log/exit code,
original recipe, exact dependency source archive and successful log are retained.

Before and after building, the recipe checks the source overlay, git base,
submodule revisions and cleanliness, and unchanged startup paths. Export checks
board ID 53, decoded package/bin equality, image capacity, NuttX version and
required/excluded symbols. The linked image contains the observer/control core;
the bench owner, synthetic publishers and active stabilized producer/driver
symbols are absent.

## Resource result

| Measure | Previous observer | Lifecycle observer | Change |
| --- | ---: | ---: | ---: |
| Decoded firmware image | 1,961,704 B | 1,962,496 B | +792 B |
| Flash headroom | 4,376 B | 3,584 B | −792 B |
| ELF text | 1,955,880 B | 1,956,672 B | +792 B |
| Static data | 5,824 B | 5,824 B | 0 |
| Static BSS | 104,816 B | 104,816 B | 0 |

Static totals do not include all dynamically allocated objects. Two separate
unlinked probes use the actual prior/current ARM compilation flags and measure
`sizeof` through object-symbol sizes:

| Type | Previous | Current | Change |
| --- | ---: | ---: | ---: |
| uORB device node | 52 B | 60 B | +8 B per created node |
| Observer object | 1,456 B | 1,496 B | +40 B |
| Diagnostic message payload | 128 B | 160 B | +32 B |

These figures exclude allocator overhead and do not establish runtime heap
availability, stack high-water, critical-section latency or scheduling bounds.
The 6,000-byte observer queue stack provision remains unchanged. Production
output integration will need a fresh size check: its driver/supervisor is not
included in the remaining 3,584-byte budget.

## Artifacts and verification

Stage: `build/stabilized_lifecycle_firmware/e8ea-20261019`.

- [Firmware package](../build/stabilized_lifecycle_firmware/e8ea-20261019/artifacts/px4_fmu-v6x_default_e8ea98b404_dirty_observer_lifecycle.px4)
  SHA-256 `7443525de5d722071949a475d2e1bd144fa2224019caf256e6b7c1c413e12ff5`.
- [Firmware receipt](../build/stabilized_lifecycle_firmware/e8ea-20261019/firmware-receipt.json)
  SHA-256 `a8e066bda786641be5efc3f40a7a3e6ae91e429dbe87c569ec260b26e06c3a64`.
- [ARM layout receipt](../build/stabilized_lifecycle_firmware/e8ea-20261019/layout/layout-receipt.json)
  SHA-256 `b32ca5733af5cbb325ecd32a74c19c526a63857d595935cd10dc34074548bff3`.
- [Independent verification](../build/stabilized_lifecycle_firmware/e8ea-20261019/verification.json)
  rehashes all 28 firmware evidence files, 359 layout evidence files and 92
  source-overlay entries. It independently decodes the package and confirms
  all 11 retained stock, bench and earlier observer artifacts are unchanged.

The stage retains ELF/bin/map files, compiler commands, source manifests,
dependency snapshots, exact Docker argument arrays and recipes.
The source helpers are
`Tools/research_sitl/stabilized_observer/lifecycle_arm_stage.py` and
`lifecycle_arm_layout.py`. Reproduction requires a fresh output stage/volume,
the retained donor and its read-only worktree/object mounts; the scripts refuse
to overwrite a successful build or exported receipt. Preserve the Docker volume,
workspace git metadata and local evidence together.

The prior 28,372 native observer assertions, 126,064 host checks per configuration,
and 22,672 PX4IO method checks per configuration remain the functional evidence
for these exact changes. This full build adds link/resource evidence; it does
not extend synthetic plant qualification or hardware acceptance.

## Next steps

Connect the production output supervisor and sink, enforce exclusive ownership,
and review selected-state health and permission rules. Repeat the link/resource
check after that integration. Keep driver acknowledgments distinct from
electrical/servo feedback and qualify physical expiry separately.

Required measurements remain signed canard authority versus airflow, geometry/
inertia, loaded servo calibration/dynamics and signal/fault timing. Thrust/wash
measurements apply when real propulsion enters the test scope. Existing bench,
acceleration and flight gates remain unchanged; this artifact grants no active
output or flight acceptance.
