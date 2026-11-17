# Stabilized manual: actual EKF2 lifecycle on the revision variant

2026-10-06, Codex. Eight native state-barrier cases pass on the exact private
[publisher-revision binary](STABILIZED_MANUAL_SM4_PUBLISHER_REVISION_VARIANT.md).
This closes the previously missing actual EKF2 module stop/restart check for
these exercised paths. General publisher identity and a restart completed
entirely between owner polls remain unqualified.

## What was exercised

The child reuses the sealed PX4 ELF and Gazebo plugin without rebuilding or
changing production sources. The original eight-case state-barrier runner
remains byte-identical. A new fixture wrapper records EKF2 instance status
after the real `ekf2 stop` and `ekf2 start` commands, and the additional audit
checks the revision tuple before the fault and after explicit recovery.

`ekf2 status` returns zero even when stopped. The wrapper therefore parses
actual `ekf2:<instance> EKF dt:` records and rejects a failed module-lock attempt.
The lifecycle cases record no instance after stop and exactly instance zero
after start. The stop output also records deletion of a running EKF2 instance
zero. These are actual module lifecycle commands in PX4's existing process;
they do not restart the entire PX4 executable.

| Barrier | Poses | Observed revisions: attitude, rate, estimator, flags | Revoke |
| --- | --- | --- | --- |
| EKF2 withdrawal, followed by later restart | Level and body-X-up | `(3,3,3,3)` to `(6,3,6,6)` | `StateRejected` on unavailable binding |
| Same-ID EKF2 stop/start | Level and body-X-up | `(3,3,3,3)` to `(6,3,6,6)` | `StateRejected` on observed withdrawal |
| Actual external-heading reset | Level and body-X-up | Tuple remains `(3,3,3,3)` | `StateChanged` on attitude reset |
| Unsupported multi-estimator profile request | Level and body-X-up | Tuple remains `(3,3,3,3)` | `StateRejected` on invalid profile |

All selected gyro/accelerometer device IDs are preserved across explicit
recovery. The rate publisher's revision stays unchanged when EKF2 restarts,
while the attitude, estimator-status and status-flags publications advance.
All published rows in each individual session retain a constant revision tuple
and core source epoch. Recovery copies state from the new revision, rather than
attributing a previous-revision cached payload to the new publisher lifecycle.

The controller latches the fault, rejects implicit re-enable, keeps simulated
targets neutral and thrust off, and regains centered hold only after an explicit
new owner/session. The consumer turns off **4 ms after revocation in every
case**. Actual heading resets advance the attitude reset count; they do not
spuriously create a publisher revision change.
Withdrawal/restart revokes with attitude samples only 20-24 ms old, below the
100 ms freshness allowance; stale-state expiry cannot satisfy these cases.

The stop/start commands span observed owner polls. They do not prove a real
EKF2 restart hidden entirely between polls. The earlier deterministic hidden
advertisement-cycle and frozen-clock tests continue to cover the separate
uORB revision-observability boundary. No `cycle_publisher` diagnostic is called
in this new actual-lifecycle package.

## Evidence and preservation

Evidence: `/var/tmp/research-stabilized-sitl-ob5jrceo`.
Receipt: `publisher-lifecycle-manifest.json`, SHA-256
`77e36d02abed8ebb6c5268812325f6dda3ba6348c93c291fa88907326a78ffb5`.
The eight cases contain **3,968 audited control cycles**, bind **401 artifacts**
and preserve **432 preparation anchors** at sealing.
All 401 artifact hashes were independently rechecked through Windows with zero
mismatches.

Parent: `/var/tmp/research-stabilized-sitl-rmk764xm`, receipt SHA-256
`9126cd2b2e9e209eb0afadabce5342d15f97fa8940f7cae36f83154dd1729242`.
The child revalidates all parent artifacts, inherits 110 sealed compiled
dependency anchors and archives transformed owner/uORB inputs separately from
the unchanged fixture-input snapshot used by the original Python harness.
Compiler commands retain their original parent build paths because this is
exact artifact reuse, not a new compilation.

Private PX4 SHA-256:
`c85e01a1d346acba95a60491740858251dc90dabf78213d8028a7cb8c3c5cced`.
Plugin SHA-256:
`795a826f771dfd5cfe3b56c719f0f036f897184457fbe4c132b917d4c5c2f044`.
The original accepted 31-case binary/campaign, stock Cessna, H10 bench and
negative-only acceleration gates retain their existing scope. Final process
checks find no live PX4 or Gazebo process for this evidence.

## Next work and inputs

[The complete declared campaign and actual console](STABILIZED_MANUAL_ISOLATED_MODE.md)
subsequently pass, and the console is now the default isolated entry point.
Source-only packaging and production source-identity interface review remain.
A node advertisement revision is an observed lifecycle barrier;
it does not identify individual overlapping publisher handles or qualify
sensor-diverse candidate selection. The control-facing interface must keep
these meanings explicit.

No new human input is needed for those software steps. Vehicle-supported
allocation, tuning and operation still require the pending CAD and accepted
mass/CG/full inertia, canard geometry/effectiveness versus airflow, loaded
servo calibration/dynamics, EDF thrust/lag/wash and physical operating and
loss/recovery limits. The goal remains active.
