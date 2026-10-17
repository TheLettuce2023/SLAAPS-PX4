# Stock Cessna first-operator checkpoint

> Superseded for current status by [first-operator closure](SITL_BASELINE_FIRST_OPERATOR_CLOSURE.md): a new GUI/headless pair passes, including live human QGC evidence. The record below remains historical.

Recorded 2026-09-27 local project date; captures completed 2026-09-28 UTC.

**Automated checks pass for both fresh runs.** The headless run passes its
single-run diagnostic check. The GUI run and overall first-operator checkpoint
remain **incomplete only for the missing human QGroundControl observation**.
No reply arrived during its live observation window. The second operator is
explicitly deferred by the user; full Phase 1 remains incomplete.

## Implemented workflow

The [baseline tools](../Tools/research_sitl/baseline/README.md) now prepare a
source package, bind a stock SITL build to it, reset pinned parameters, capture
fresh GUI/headless processes, record readiness and timed intervals, shut down
owned processes normally, and score raw ULogs against an immutable registry.
The checkpoint checker also verifies matching GUI/headless inputs and
serialized lifecycles. Missing evidence cannot become a pass.

The source package preserves **846 dirty/untracked or pinned input files** and
**47 initialized repository records**, including full binary diffs. Clean
tracked source is identified by Git revision. The cached stock rebuild and
both runs verified source identity before/after execution. This is a local
provenance record, not cryptographic authentication of the operator or host.

[Revision 2 was explicitly approved](SITL_BASELINE_DIAGNOSTIC_LIMITS_V2_APPROVAL.md)
after the scorer exposed the stock synthetic-pressure publisher's unset
capture timestamp. The only added exception permits an all-zero
`differential_pressure.timestamp_sample` stream for the verified stock
publisher and device 1377548. Publication order/gaps and finite pressure still
apply. Revision 1 and its approval remain unchanged. These are diagnostic
logged-data limits, not flight or control budgets.

## Fresh evidence

Campaign: `slaaps-stock-cessna-phase1-acceptance-02`.
Package in Ubuntu-24.04 WSL:
`/var/tmp/slaaps-stock-cessna-phase1-acceptance-02-e`.

| Check | GUI `operator1-gui-04` | Headless `operator1-headless-01` |
| --- | --- | --- |
| PX4 scored interval | 21.756–81.756 s | 21.656–81.656 s |
| Settling/scoring | 10 s / 60 s | 10 s / 60 s |
| Raw required topic instances | 15 / 15 | 15 / 15 |
| Automated violations | 0 | 0 |
| Logger dropouts | 0 | 0 |
| Maximum logged attitude gap | 4 ms | 4 ms |
| Maximum local/global position gap | 8 ms each | 8 ms each |
| Maximum logged GNSS gap | 1.024 s | 1.024 s |
| Maximum inbound MAVLink advance gap | 1.064 s | 1.064 s |
| Maximum attitude publication/sample age | 0 ms | 0 ms |
| Equal attitude publication pairs inside interval | 0 | 0 |
| Approved synthetic-pressure zero-sample records | 60 | 60 |
| Shutdown / owned-process residue | normal / none | normal / none |
| Human QGC record | missing | not required |
| Single-run result | incomplete | pass |

The GUI stayed live for the human observation window after its machine
interval, then closed normally without a reply. Its longer ULog is therefore
156,583,701 bytes; the headless ULog is 18,582,125 bytes. No live QGC warning or
successful visual check is inferred from machine telemetry. The older human
observation from the separate diagnostic campaign does not satisfy this run.

Both start-boundary UTC observations stepped backward while PX4 monotonic
time advanced: about 0.211 s for GUI and 0.299 s for headless. The report
preserves those observations and flags them explicitly. The approved scoring
clock is PX4 monotonic time; no exact UTC synchronization or operational
clock-map qualification is claimed. The checker was corrected to report wall
clock adjustments instead of imposing an unapproved UTC-monotonicity rule.
Final scoring sources are frozen separately in `checker-final/`; their hashes
are recorded in the result and inventory.

## Artifact bindings

| Artifact | SHA-256 |
| --- | --- |
| Approved revision-2 registry | `d295bcb9a43fae1dec4eb147539a9ea1f980f087a84337f37d749db39239e52d` |
| Source index | `b556d199f0555c257c6d076a7dc8ce95a4eefef6c60f2af5a0d05db618604c83` |
| Dirty/pinned source archive | `5f2b9171c871118ecca958ecba71368f48383b172df0e5299fff4155366c4707` |
| PX4 executable | `b7920bc1c205318fde658a07f2510d038e2e21eca9c2f0a1a8abf45777487ea4` |
| Initial parameter image | `198fdccbc5950c2c5715970f4c6897649a49a3a9811178d8b7b4c87cb9e1fdf1` |
| GUI ULog | `43e22bff4d80cf6bd35d01df3f8e8b813e95e07a5d5c83710f76689e47012f38` |
| Headless ULog | `442f41236640b29d70b5e1807f16bc35134e58c7ab10c4dfee620f4e3dbae854` |
| Checkpoint result | `1c60165b9bd9dbf01a4718267acca88c6c54a169156dd3a866687189ceb2e5a5` |
| 48-file artifact inventory | `54c4453b8b852f671d4818e1dd9cd616417ac7462847265dea574271f4c4beab` |

The package includes Windows/WSL/QGC versions, Python 3.12.3, PyULog 1.2.4,
NumPy 2.5.3, signal units/frames/publishers/consumers, run plans, terminal
transcripts, clock observations, process lifecycle and source verification.
The scorer uses PyULog, so it does not provide an independent ULog decoder.
`REPRODUCE.txt` identifies the frozen scoring entry point.

Three pre-scoring attempts remain preserved in sibling packages ending
`-02-b`, `-02-c` and `-02-d`: asynchronous prompt handling, Gazebo discovery
under the wrong user partition, and default multi-instance listener output
prevented readiness. Those collector defects were corrected. None of those
attempts is substituted for a passing scored run. Seven focused acceptance
tests pass; no broad native regression or flight testing was performed.

## Remaining gate work

1. Obtain a timed human QGC observation during a fresh GUI acceptance capture;
   retain parameters, attitude, map, mode, Inspector traffic and warning timing.
   The current GUI run has already stopped and remains incomplete.
2. Resume second-operator reproduction when the user chooses; it is deferred,
   not established by the assistant's repeated captures.

The custom flight-control implementation remains at accepted C1E-N1 negative
diagnostic scope. This checkpoint adds stock baseline infrastructure/evidence;
it does not validate the research plant, controller or flight behavior.
