# First-operator baseline closure

Recorded 2026-09-27 local project date; captures completed 2026-09-28 UTC.

**First-operator checkpoint: PASS.** Fresh GUI and headless captures pass the unchanged approved revision-2 criteria. The project user confirmed "All five visible; no Comms Lost" during GUI run `operator1-gui-05`; its receipt was recorded while that run was live. Full Phase 1 remains incomplete solely for second-operator reproduction, explicitly deferred by the user.

## Evidence

WSL package: `/var/tmp/slaaps-stock-cessna-phase1-acceptance-02-f`. Source package: 848 dirty/untracked or pinned files and 47 repository records. Both runs use identical source, executable, initial parameters and registry. Source identity was verified before and after each run.

| Measurement | GUI | Headless |
| --- | --- | --- |
| Run | operator1-gui-05 | operator1-headless-02 |
| Result | pass | pass |
| PX4 interval (s) | [21.756, 81.756] | [21.664, 81.664] |
| Attitude max logged gap (ms) | 8.0 | 4.0 |
| Local/global max logged gap (ms) | 12.0 | 8.0 |
| Attitude max sample age (ms) | 4.0 | 0.0 |
| Equal attitude publication pairs | 5 | 0 |
| Missing evidence / failures | 0 / 0 | 0 / 0 |
| Observed UTC boundary backward steps | 0 | 0 |

All 15 required topic instances passed. Same-tick GUI attitude publications satisfy the approved distinct-sample and age rules. The approved synthetic-pressure exception remains source/device qualified. Both processes closed normally, with no simulator processes or owned mounts left. No new regression suite was added or rerun; verification was the required pair acceptance and artifact integrity check.

## Artifact hashes

| Artifact | SHA-256 |
| --- | --- |
| `source-index.json` | `4e38f5a023b3093ee99d450b94fbee2724c19a56f931227f63ffd4ad69d16479` |
| `source.tar.gz` | `b2586f59b690ab9d7e18ff370633d0ff085c60021aa24d5cc2c3b9573a4a1901` |
| `px4` | `b7920bc1c205318fde658a07f2510d038e2e21eca9c2f0a1a8abf45777487ea4` |
| `parameters.bson` | `198fdccbc5950c2c5715970f4c6897649a49a3a9811178d8b7b4c87cb9e1fdf1` |
| `registry.json` | `d295bcb9a43fae1dec4eb147539a9ea1f980f087a84337f37d749db39239e52d` |
| `operator1-gui-05/qgc.json` | `c061074ed649ff32a7c2dae809973b2995a39e3ae73c162e13b7ecb52899b9da` |
| `operator1-gui-05/log.ulg` | `16d8516db9cdb00c9609fdcc4cdf1b2410b0d75d36e2a05570a541e13b4daf3f` |
| `operator1-headless-02/log.ulg` | `62665d68f92ceb47068a5b5f16a1407998e0f2cf08049e98fc55cf61e90c8143` |
| `checkpoint-result.json` | `fa7f3bd06f7d001d6a0e2ddf6c486032b4aee8b7236006965dd1f4ee6c20419e` |
| `artifact-inventory.json` | `0d0177030108437c14eda59f679b9870f02a571cac527e0a021f04b764c2b1cb` |

The inventory covers 48 files (excluding itself). The exact scoring tools are in `checker-final/`; `REPRODUCE.txt` gives the read-only recheck command. Earlier incomplete attempts and the prior automated-only checkpoint remain preserved. This closes the first-operator gap without retroactively changing earlier results.

## Next checkpoint

Second-operator reproduction remains deferred. Continue geometry-independent positive-admission design, specifying trusted estimator/transport publishers, operator/session lifecycle and producer-to-validator clock mapping while retaining the negative-only C1E-N1 profile. Plant acceptance still requires the original CAD CG/inertia report, component mass scope, axes and units. Custom control, allocation and physical flight remain unvalidated.
