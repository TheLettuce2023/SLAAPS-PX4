# P2B native observation verification

2026-09-27, Codex (GPT-6). Bounded native observation verified; no positive admission.
Contract: [native output-boundary collector](C1E_P2B_NATIVE_RECEIPT_CONTRACT.md).
The latest accepted admission runtime remains C1E-N1, negative-only.

## Implementation and independent review

The isolated native profile builds actual single-instance EKF2 with a private
source hook immediately before attitude publication. The hook also captures
iterations where invalid attitude causes no publication. Fixed global storage,
single-writer checks and freeze/in-flight ordering preserve bounded observations
without producer I/O, allocation or blocking. No Commander, admission library,
network endpoint, simulator bridge, control or output module is selected.

The user authorized independent agent `provenance_review`. Its source review
accepted the bounded observation API and ordering. It found and re-reviewed
three corrected evidence-checker gaps: exact archived input inventory, exact
private-overlay inventory and executed startup hash binding. Four focused
unit tests pass in WSL, also independently rerun by the reviewer. The archive
test rejects missing directories/files, extra files, empty overlays and changed
startup. Existing row tests cover chronology, gaps, predicates and authority.

## Preserved exploratory attempts

- `/var/tmp/p2b-native-4mdkxnjg`: first compile failed under `-Wshadow` because
  enum `Fault::Capacity` shadowed a resource constant. The constant was renamed
  `RecordCapacity`; failure inputs/logs remain archived. Resumed build linked
  and closed normally, exporting 1200 observations for 1200 recorded IMU inputs,
  all with attitude validity false and collector fault zero. Two missing GPS
  delay parameter errors make this **unaccepted exploratory evidence**, despite
  the earlier runner's `captured_unqualified` completion label. The corrected
  checker rejects these errors. Do not reinterpret it as clean acceptance.
- `/var/tmp/p2b-native-30xfeeqc`: corrected profile adds the existing
  `src/modules/sensors/vehicle_gps_position/module.yaml` parameter metadata,
  without selecting the sensor runtime module. An independent review caught
  PX4's relative-only MODULE_CONFIG handling. The owned pre-runtime configure
  was interrupted and archived, then the relative-path correction was resumed.
  The runner can resume only its failed pre-runtime area; it checks source
  ownership, unrelated source drift and private overlay hashes. An interrupted
  configure with no cache is allowed only if no build.ninja or executable exists.

The fixture has no magnetometer, GNSS, barometer, device identity or other
aiding inputs. A false attitude-valid result is retained as the actual EKF
predicate, never changed to make the experiment positive. `attitude_valid()`
means tilt alignment in this source; it is not yaw alignment or full health.
Tilt convergence is checked by EKF covariance in `EKF/control.cpp`.

## Corrected native result

`/var/tmp/p2b-native-30xfeeqc` in Ubuntu-24.04 WSL is the clean verified package.
All 436 native build steps completed; the process reported no runtime errors
and shut down normally. The complete relevant dirty/untracked source inventory
matched before and after build/run. The separate checker accepted the exact
archive/overlay/startup, executable, build profile and native observations.

- 1200 recorded IMU publications and 1200 source-hook records; no unobserved
  inputs in this finite run, collector fault zero, one observed state change.
- 1200 invalid-attitude observations, zero valid-attitude observations. This
  is real negative-state evidence, including nonpublication; no health is inferred.
- Maximum input-publication-to-hook delay 790 us; maximum hook gap 5897 us.
  These are measurements, not operational timing budgets or worst-case bounds.
- Callback `.su` frame: 128 bytes, static. This excludes its callees and does
  not constitute a whole work-queue stack or timing qualification.
- Manifest SHA-256:
  `18a80adba4833042c51d9fb661c2d5ffb6016d8053621a4ec27508b70d39010e`.
- Checker source SHA-256:
  `6dc0ac7b9a330b2febe503353035a649c54711126903300e62681f3687a575d4`.
  Retained result: `independent-check.json` in the evidence root.

`provenance_review` independently reran the complete checker on that package
and accepted P2B solely at native output-boundary observation scope. No
remaining review blocker applies to that bounded checkpoint.

A native observation result does not close the full P2
publisher/owner gate, P3 map qualification or P4 operator/protocol lifecycle.
No stock baseline repeat, commit, publish or physical operation was performed.
