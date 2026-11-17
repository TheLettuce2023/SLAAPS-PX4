# Stabilized manual: private publisher revision variant

2026-10-06, Codex. A separate POSIX PX4/Gazebo build now binds selected state
to uORB advertisement revisions and rejects a lifecycle change hidden between
owner polls. Its native uORB selftest and eleven synthetic SITL cases pass.
The accepted 31-case campaign remains at its original binary and scope.

## Contract and implementation

The [publisher-epoch audit](STABILIZED_MANUAL_SM4_PUBLISHER_EPOCH_AUDIT.md)
identified a limit in the accepted owner: its constant source epoch cannot
distinguish an unadvertise/re-advertise completed between polls. This variant
addresses the observable uORB lifecycle transitions with an explicit revision.
It does not establish the identity of each individual publisher handle.

`prepare_stabilized_publisher_epoch.py` copies the sealed accepted source to a
new `/var/tmp` tree and applies exact, unique source transformations there.
The checkout's owner and shared uORB sources remain unchanged. The private
compile definition `RESEARCH_STABILIZED_PUBLISHER_EPOCH=1` applies throughout
that native build and rejects use outside POSIX.

The private `DeviceNode` revision advances on every `mark_as_advertised` and
`unadvertise`, including an additional instance-zero advertisement. It counts
API transitions, not messages or exact numbers of publisher incarnations;
one advertisement may invoke more than one mark. Zero permanently invalidates
an exhausted counter. Normal publications retain the current revision.

The existing node lock protects revision changes, publication payloads and a
new queue-one copy method. Each publication records its revision. A bound copy
requires an advertised node, valid data, a nonzero revision and payload revision
equal to current revision. Re-advertisement without a new publication therefore
cannot attribute the previous publisher's cached payload to the new revision.
Queued topics are rejected by this private method.

The variant owner copies selected attitude, angular velocity, estimator status
and status flags with their revisions. It checks that these revisions still
match the nodes before reading the evaluation clock, then maps the complete
four-topic tuple to a process-local `StateSample::source_epoch`. Tuple changes
advance the core epoch without a tuple hash or collision risk. Once enabled,
changed or unavailable binding revokes before duplicate-clock suppression.
Re-entry continues to require an explicit new owner/session.

This is a per-node atomic payload/revision binding. There is no transaction
spanning all four topics and output publication. A transition after an admitted
snapshot can be observed on a subsequent dispatch. A pre-existing handle can
also publish after another handle's advertisement without announcing its own
identity. Positive multi-estimator handoff, per-publisher identity, general
concurrency/timing qualification and physical authority remain open.

## Native evidence

Evidence: `/var/tmp/research-stabilized-sitl-rmk764xm`.
Combined receipt: `publisher-revision-manifest.json`, SHA-256
`9126cd2b2e9e209eb0afadabce5342d15f97fa8940f7cae36f83154dd1729242`.
Private PX4 SHA-256:
`c85e01a1d346acba95a60491740858251dc90dabf78213d8028a7cb8c3c5cced`.
It uses the accepted synthetic fixture plugin unchanged, SHA-256
`795a826f771dfd5cfe3b56c719f0f036f897184457fbe4c132b917d4c5c2f044`.

- Fourteen native disposable-`orb_test` checks pass: continuous publication,
  withdrawal, node reuse, rejection of prior-revision cache, publication in a
  new revision, a complete hidden withdrawal/re-advertisement, core epoch tuple
  mapping, unavailable token and an additional instance-zero advertisement.
  Ordinary subscription copying remains successful after the hidden cycle;
  the new bound copy exposes its changed revision.
- Eight active-controller cases pass: each of the four selected-state topics
  at level and body-X-up. A diagnostic runs withdrawal/re-advertisement entirely
  between owner snapshots, on the same node with unchanged copied message,
  IMU IDs and attitude reset count. The first following snapshot revokes with
  `StateChanged`, while attitude age is 8 ms, below the freshness allowance.
  All eight consumer shutdown delays are 4 ms. Implicit re-enable is rejected;
  explicit new sessions regain hold.
- One frozen-clock case passes. Two unchanged duplicate dispatches publish no
  additional command. An attitude lifecycle injection at that same clock
  revokes before duplicate suppression, with no additional control row.
  The consumer turns off at the first resumed physics step, 4 ms later in
  simulation time; an explicit new session recovers.
- Two unequal-axis rate/reversal/release/impulse cases pass all fourteen
  existing checks at level and body-X-up. Revisions remain constant during
  normal feedback. Maximum physical tracking error is 0.015371 rad/s against
  0.08; final physical hold error is at most 0.006299 rad against 0.02. Directional
  antiwindup violations remain zero.

The eleven SITL cases contain **18,384 audited control cycles**. The sealer
checks actual runtime estimator parameters, exact variant source identities,
compiler definitions, linked-owner symbols, mapped compiled dependencies,
closed logs and the existing coupled-control audit. It binds 453 artifacts and
425 preparation anchors. All 453 artifact hashes were independently rechecked
through Windows with zero mismatches, and the accepted parent firmware/plugin
hashes still match. C++ formatting and Python syntax checks pass. Final process
checks find no live PX4 or Gazebo process for this evidence.

These diagnostic advertisement cycles do **not** stop and restart the EKF
process; they exercise the precise uORB lifecycle event that was unobservable
at the former adapter boundary. They also do not promote the private binary
into the accepted 31-case campaign.

## Resume and inputs

[A subsequent exact-artifact child](STABILIZED_MANUAL_SM4_PUBLISHER_LIFECYCLE.md)
now passes eight actual EKF2 lifecycle/reset/profile cases.
[Full declared campaign qualification and a runnable console](STABILIZED_MANUAL_ISOLATED_MODE.md)
subsequently pass on this binary. Next is source-only packaging and
source-identity interface review before broader
selected-state continuity or candidate-handoff claims. New synthetic software
tests need no human input. Vehicle-supported allocation and tuning still await
the pending CAD, accepted mass/CG/full inertia, canard geometry and effectiveness
versus airflow, loaded servo calibration/dynamics, EDF thrust/lag/wash and
physical operating/loss-recovery limits.

The stock Cessna baseline, H10 bench evidence and negative-only C1E-N1
acceleration gates retain their existing scope. The goal remains active.
