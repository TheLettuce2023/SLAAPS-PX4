# Stabilized manual SM4: native state barriers

**Runtime-profile erratum, 2026-10-05:** all eight scripts omitted the claimed
bias overrides. Their observed behavior is preserved at the actual startup/default
profile. Corrected-profile coverage requires new runs with live parameter checks;
see the [configuration defect and requalification](STABILIZED_MANUAL_SM4_RUNTIME_PROFILE_FIX.md).

2026-10-05, Codex. Extends the [18-case synthetic campaign checkpoint](STABILIZED_MANUAL_SM4_CAMPAIGNS.md).
This increment tests actual PX4 estimator reset/lifecycle behavior and deliberate
manual-owner recovery. The controller, private owner, firmware and plugin are
unchanged; no new stock source patch, publication hook or default registration
is needed.
Exact result: **all eight declared native state-barrier/recovery cases PASS**.

## Tested state transitions

`Tools/research_sitl/verify_stabilized_state_barriers.py` exercises four cases
at level and body-X-up attitudes:

| Case | Actual native action | Required result |
| --- | --- | --- |
| Publisher withdrawal | Stop EKF2, observe before ordinary 100 ms sample-age expiry | Fault/ready-zero; no cached-state continuation; consumer neutral targets and zero thrust |
| Rapid same-ID restart | Consecutive EKF2 stop/start commands, with no deliberate dwell | Latched Fault despite restored estimator; enable rejected; deliberate new owner session required |
| Quaternion reference reset | MAVLink command 620 invokes stock EKF external heading reset to 45 degrees | Actual reset counter advances; owner publishes no commands with the changed counter; Fault remains latched |
| Single-estimator profile | Set `EKF2_MULTI_IMU=2`, then restore zero | Revoke while unsupported profile is requested; restoring it does not implicitly recover the owner |

Each case then deliberately stops/starts the manual owner, enables from centered
Ready, holds for one simulated second and disables. The consumer must accept
only a newer session. Recovery uses the same gyro and accelerometer device IDs.
Between first revoke and explicit recovery, all consumer targets remain neutral,
thrust remains zero and the lease fault stays latched. Physical servo states
decay through their existing lag; they are not overwritten instantaneously.

The multi-estimator case tests the private target's rejection policy. It does
not start a second estimator or qualify a successful candidate handoff. The
first target deliberately permits only one EKF.

## Why withdrawal already revokes

Reading only `uORB::DeviceNode::copy` could suggest that a valid cached sample
survives publisher withdrawal. The owner calls `uORB::Subscription::copy`,
which routes through `uORB::Manager::orb_data_copy`. That public manager first
rejects an unadvertised node. Single-sample `PublicationBase` destruction
withdraws the advertisement. Actual EKF stop/restart tests exercise this path;
the owner also rejects missing inputs, changed/broken device binding, observed
reset counters and stale state.

No redundant advertisement check or invasive publisher instrumentation was
added. The existing fixed `source_epoch=1` remains a limited fixture convention.
These runs prove the exercised stop/start/reset paths, not a general guarantee
that every restart entirely between polls or every alternative publisher
identity is detected. Sensor-diverse failover and a qualified flight-health
policy remain separate work; C1E-N1/P2J scope is not promoted.

## Evidence

Final evidence: `/var/tmp/research-stabilized-sitl-zacmmnz6`, manifest SHA-256
`fe6c5d26ad54dd8b732ba7a97a77246651b45dd7f3824edb483dadf4f65971f6`.
All eight latest cases pass. The audit covers **3,994 published control cycles**
and preserves **313 checkpoint anchors**, with no input-change exceptions.
All **336 sealed artifact hashes** were independently rechecked with Windows
SHA-256, with zero failures.
Withdrawal/restart revoke at attitude ages **20-24 ms**, below the 100 ms
freshness allowance. Both real external-heading cases change the reset counter
**1 -> 2**, and all pre-revoke published commands retain counter 1. The
consumer reaches its first revoked/off sample exactly **4 ms after the
producer's revoke event** in every case. All implicit-enable attempts return
255; all deliberate new sessions hold and disable with the same IMU IDs
1310988/1310988.

The evidence reuses exact verified firmware
and plugin from `research-stabilized-sitl-r27tx0nr`; `native-origin.json`
records that parent manifest and the immutable artifact hashes. The new runner
and matching native dependencies were added to the research graph before
preparation. Every case archives the exact test script, runtime sources,
configuration/world, commands, event logs, normal estimator snapshots and
consumer output history.

The final sealer requires all eight latest cases to pass and rejects changed
source/profile inputs. It verifies closed CSV structure, audits each active
control cycle through the existing bounded-cycle checker, revalidates compiled
mapped dependencies, preserves the frozen checkpoint and binds every retained
artifact. Publisher-withdrawal/restart cases must revoke with attitude age
below 100 ms, so a generic stale-state timeout cannot satisfy those tests.

Earlier exploratory three-case level evidence remains in
`research-stabilized-sitl-4b9in48k`. Those initial tests omit the final
post-run device/counter/age audit and do not satisfy final coverage.

## Remaining work and inputs

Broader publisher-epoch/candidate-switch qualification, sensor-bias/delay and
transport perturbations, repeatability and real vehicle plant/tuning remain
open. The passing 18-case performance profile remains sealed separately and
is not replaced by negative state-barrier evidence.

No new human input is needed for the remaining synthetic software work.
Vehicle-supported control still requires accepted mass/CG/full inertia,
canard geometry/signs and effectiveness versus airflow, loaded servo
calibration/dynamics, EDF thrust/lag/wash, physical control limits and an
accepted loss/recovery policy. CAD is already pending; no new CAD request is
made here. Hardware flashing, powered operation and flight remain outside this
increment. Stock Cessna, H10 bench and negative-only acceleration gates are
preserved.
