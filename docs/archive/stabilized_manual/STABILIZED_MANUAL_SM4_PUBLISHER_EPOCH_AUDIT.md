# Stabilized manual: selected-state publisher epoch audit

2026-10-06. Source/API audit of the isolated synthetic SITL owner. This is a
qualification finding, not a new native flight-control campaign or a change to
the accepted 31-case result.

## Finding

The core accepts an adapter-supplied nonzero `StateSample::source_epoch` and
revokes eligibility if it changes during a session. The isolated owner supplies
the constant value `1`. Its other checks reject observed publisher withdrawal,
state staleness, attitude resets, estimator timestamp regression, and changed
selected gyro/accelerometer IDs. They do not establish publisher identity if
the selected-state publisher withdraws and re-advertises between two owner
polls, then supplies plausible newer samples with the same IDs and reset count.

This is possible at the current API boundary:

1. The owner snapshots each uORB subscription once per work-queue run and reads
   the evaluation clock afterward. There is no asynchronous publisher-lifecycle
   callback in this adapter.
2. `Subscription::copy` calls `Manager::orb_data_copy`. Its advertisement check
   rejects a node while unadvertised, which explains the previously observed
   EKF stop/restart revocations. If a new publisher has already advertised by
   the next copy, that Boolean check passes.
3. `DeviceNode::unadvertise` clears `_advertised` without deleting the node;
   `DeviceMaster` explicitly reuses an unadvertised node and marks it advertised
   again. `DeviceNode::_generation` increments on publication, not publisher
   incarnation, and `Subscription::get_last_generation()` is this message
   sequence. Neither is a restart epoch.
4. Selected attitude, angular velocity and estimator-status messages used by
   this owner supply timestamps, reset count and device IDs, but no guaranteed
   same-instance publisher incarnation. A restart could keep those observed
   fields monotonic and unchanged where required.

Thus two histories can yield the same values at the owner's next poll: one
continuous publisher, or a withdrawal/re-advertisement between polls followed
by fresh, plausible data. No predicate over the currently copied values can
distinguish them. This is an API-observability counterexample, **not** a claim
that an EKF restart with those exact conditions was observed in SITL.

## Verification and scope

The unchanged standalone Debug host target was configured and built in
`/var/tmp/research-stabilized-epoch-host`; CTest passed 1/1. Its existing fault
matrix includes a changed `source_epoch` case that revokes eligibility. The
earlier eight native state-barrier cases remain valid for their observed
withdrawal, rapid restart, heading reset, profile rejection and explicit
recovery paths. They do not cover the indistinguishable between-poll history.
No production source, firmware, simulator fixture, tuning, output mapping or
accepted artifact was changed for this audit.

## Next implementation boundary

Before claiming general selected-state publisher continuity, provide a
monotonic publisher-incarnation token whose change is observable even if
withdrawal and re-advertisement occur between owner polls. Bind that token to
the selected attitude/rate/estimator source and pass it as `source_epoch` to
the existing core. A regression must force a same-instance, same-device-ID
restart entirely between owner polls, with otherwise fresh monotonic samples,
and prove revocation and explicit-session recovery. Also test ordinary
continuous publication and a token change without an observed withdrawal.

The token's publication and selection semantics need design review against
uORB's instance-zero multi-publisher behavior and any future estimator
candidate switching. Until then, describe the isolated owner as qualified only
for its exercised state-barrier paths; keep physical outputs disabled. This
software design/test work needs no new vehicle measurements. Vehicle-specific
allocation and tuning still require the pending CAD and measured canard,
loaded-servo, EDF and mass-property data.
