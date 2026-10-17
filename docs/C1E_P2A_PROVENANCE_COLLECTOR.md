# P2A: closed archive collector and estimator publisher audit

2026-09-27, Codex (GPT-6). Implemented offline collection and independent
reconstruction of existing real ULog observations. **P2 live provenance and
positive admission remain open.** C1E-N1 remains the accepted native runtime.
This milestone does not claim independent review of P1, a live collector,
lossless publisher receipts, an operator session or a qualified clock map.

## Implementation and exact boundary

[Collector](../Tools/research_sitl/provenance/collect_archive.py) and
[checker](../Tools/research_sitl/provenance/check_archive.py) retain/recompute
hash-bound per-topic/per-instance observation rows. The separate checker does
not call collector extraction or summary functions. Both use PyULog, so raw
binary decoding is a shared dependency. The
[contract/README](../Tools/research_sitl/provenance/README.md) defines resource
bounds, strict JSON, output completion and the explicit missing-trust labels.

Why this slice: N1's isolated build enables Commander but no estimator. Merely
adding uORB subscriptions there would produce no estimator provenance. The
accepted stock archive already contains actual EKF outputs and can establish
what ordinary logging preserves before a separate instrumented profile is
built. These are offline **logged observations**, not fabricated native source
receipts. No baseline was rerun and no native startup/profile was changed.

## Source audit and P1 refinements

The audit binds inspected source bytes to the original baseline source index
where listed, otherwise to unchanged files at its recorded Git revision
`e880f1cd79e2453ef3dfaf68ee1c067b98ea9a53`. Each file and basis is recorded in
the execution evidence. Hashing current files alone would not establish that
they describe the archived executable.

| Source seam | Observed source behavior | Live collector requirement |
| --- | --- | --- |
| `EKF2` constructor | Single mode publishes vehicle topics; multi mode publishes estimator topics. | Freeze build/mode and enumerate both paths; topic instance zero alone is not selected-estimator proof. |
| `EKF2::PublishAttitude` | Normal mode publishes only when `_ekf.attitude_valid()` is true; false validity produces no invalid-attitude message. Replay mode has a different zero-timestamp path. | Observe validity at its producing execution boundary, including nonpublication, with explicit replay exclusion. A last good quaternion cannot represent continuing validity. |
| `EKF2::PublishLocalPosition` | Position/velocity are body-origin NED values; validity flags come from estimator predicates. Vertical flags each permit position OR velocity validity. | Preserve the actual predicate semantics and reference point; do not infer independent vertical position/velocity proof or CG acceleration from these fields. |
| `EKF2::PublishStatusFlags` | Roughly 1 Hz periodic publishing plus observed control/fault changes; sample time is the delayed filter horizon. | Health heartbeat cadence and delayed sample horizon need separate treatment from current attitude; no universal short freshness/skew budget. |
| `EKF2Selector::SelectInstance` | Sensor selection, selected instance and change counter are updated in the selector execution context. | Capture the selection boundary and device identities before relying on later aggregate publications. |
| Selector attitude/local-position republish functions | Filter non-increasing/stale samples, transform reset counts and replace publication time. | Retain original publisher time and reset provenance alongside selected output; a fresh republish cannot refresh sample age. |
| `logged_topics.cpp` | Default and profile-specific logging intervals differ; estimator topics are optional. | No logger profile or absence of logged dropout proves every uORB publication or internal validity transition was retained. |
| POSIX `hrt_absolute_time` | Clock source depends on compiled lockstep/proxy configuration. | Collect actual build/provider facts before deriving any producer-to-validator map. |

This is a bounded audit of outward publisher/selector seams, not an exhaustive
audit of every internal EKF mutation. P1's lossless-readiness requirement still
needs a precise producer boundary: observing one health snapshot after an EKF
update cannot prove every internal invalid-to-valid transition during that
update. Define which committed source facts affect admission, enumerate their
mutation sites and establish observation/expiry budgets before claiming P2.
The live design must not silently rename sampled observation as lossless state.

## Evidence interpretation

Source archives: existing first-operator GUI and headless runs under
`/var/tmp/slaaps-stock-cessna-phase1-acceptance-02-f`; expected ULog hashes come
from [baseline closure](SITL_BASELINE_FIRST_OPERATOR_CLOSURE.md). Full recorded
streams are extracted, not just the baseline's scoring interval. P2A findings
do not rescore or alter its frozen acceptance policy.

| Observation | GUI | Headless |
| --- | --- | --- |
| Extracted records | 30,252 | 30,145 |
| Observed streams | 3 | 3 |
| Logger dropouts | 0 | 0 |
| Attitude equal publication steps | 5 | 0 |
| Largest attitude logged gap | 8 ms | 4 ms |
| Largest local-position logged gap | 12 ms | 8 ms |
| Status-flag sample age at publication | 152 ms | 152 ms |
| Largest status-flag logged gap | 1 s | 1 s |

Both logs contain `vehicle_attitude`, `vehicle_local_position` and
`estimator_status_flags`, instance 0. Neither contains `estimator_attitude`,
`estimator_local_position` or `estimator_selector_status`. Missing optional
streams are retained, not filled with synthetic values or called a source fault.
The archived `SENS_IMU_MODE=1` is configuration evidence consistent with the
single-mode path; P2A does not promote it to authenticated lifetime proof.

The observed 152 ms delayed status horizon is not network delay, queue delay
or a clock-offset estimate. Applying an attitude freshness rule to it would
conflate different publisher semantics. This observation motivates per-input
qualification; it does not approve a new threshold.

## Verification and next coherent increment

Six finite test methods cover equal/backward times, stale sample under fresh
publication, counter wrap, nonfinite values, absent fields/streams, dropouts,
wrong input hash, no-overwrite, changed input during collection, parser-reported
corruption, resource exhaustion, row reorder/value substitution/truncation,
manifest/authority forgery and strict JSON. They are synthetic integrity tests,
not native timing or health evidence. Both real archives are independently
reconstructed row-for-row and summary-for-summary after extraction.

Final evidence: `/var/tmp/c1e-p2a-verified-re2idmzc` in Ubuntu-24.04 WSL.
`execution.json` binds the exact tool/source hashes, analysis environment,
commands, six passing methods and both independently verified archives.
Its SHA-256 is
`2282ed30e1c639f04437a489966606c0238a4c98299fe689f2d265ccdcca4331`.
GUI manifest: `1da1fb4ea707ffb62dc459099341ce3f8c223e3b9170a801030bd662704d9967`;
headless manifest: `3980d1fc5b36997ac46e9a8f87950518e628f3aa60448a83a18ff69f62c2ce91`.
The earlier `/var/tmp/c1e-p2a-archive-zspaljxi` is a preserved preliminary
execution, superseded after adding parser-corruption and journal-stability
checks. It is not the final tool verification. No C/C++ source changed.

Next P2 work: specify the instrumented publisher receipt API and a distinct
output-disabled build containing actual estimator producers. Resolve health
commit-boundary coverage and bounded transfer ordering, then integrate real
receipts while keeping admission closed. Real transport/operator lifecycle
receipts, qualified clock provider, exact budgets and positive protocol remain
separate open gates. Existing stock evidence cannot supply those capabilities.
CAD mass/CG/inertia remains a physical blocker, independent of this software work.
