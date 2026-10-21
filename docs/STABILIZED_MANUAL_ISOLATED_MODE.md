# SLAAPS isolated stabilized manual mode

Updated 2026-10-20, Codex. The default console now runs the fresh checkout-built,
fully qualified revision-aware
synthetic manual mode. Its complete declared 31-case SITL matrix passes, and an
additional live operator-console test passes. Vehicle-supported control still
awaits measurements; this mode uses the declared rotational fixture.

## Run and operate

From PowerShell on this workstation:

```powershell
wsl -d Ubuntu-24.04 -- python3 '/mnt/c/Projects/SNC SENIOR DESIGN/FC Software + SITL/Tools/research_sitl/stabilized_manual_console.py'
```

The console starts a fresh private PX4/Gazebo session at body-X-up, verifies
the actual estimator parameters and selected-state readiness, then enables
centered attitude hold. Add `--level` for the alternate tested initial pose.
The declared airflow is 20 m/s and enabled simulated EDF thrust is fixed at
10 N. The fixture restrains translation and reacts the thrust at CG; its
airflow is prescribed independently of EDF wash.

| Command | Effect |
| --- | --- |
| `sticks 0.15 -0.10 0.10` | Request body-FRD roll, pitch and yaw stick values; each must be finite and within [-1,1] |
| `hold` | Center all sticks; the controller captures the fresh estimated full quaternion on release and retains it |
| `status` | Show the actual current control mode |
| `disable` | Revoke the current session and stop simulated thrust/targets |
| `restart` | Center input, create an explicit new owner/session, and regain hold only after centered selected input and valid state |
| `quit` | Stop the session and close its PX4/Gazebo processes |

Stick values persist until changed or centered. They become shaped body-rate
commands, with all-axis quaternion hold when centered. Invalid console input
requests centered hold. EOF and Ctrl-C also close the session. Logs and session
metadata are retained under the printed `/var/tmp/research-stabilized-manual-session-*`
directory. The console uses PX4 instance 29476 and UDP ports 19781/19782, separate
from the campaign harness; concurrent console instances coordinate with a lock.

`Tools/research_sitl/stabilized_manual_default.json` pins the exact accepted
qualification receipt, ELF and plugin. Startup verifies their artifact hashes
and the current fixture inputs before launching. The console uses existing
qualified POSIX artifacts, so workspace and WSL evidence must be preserved.
A [fresh build from checkout](STABILIZED_MANUAL_CHECKOUT_BUILD.md) now compiles
both artifacts. The [latest core requalification and console check](STABILIZED_MANUAL_VALIDATOR_REQUALIFICATION.md)
pass all 31 cases over 68,761 cycles and are the current default. The October 6
checkout qualification and earlier package below retain their original scopes.

## Preserved previous revision campaign

Qualification evidence: `/var/tmp/research-stabilized-sitl-og0lfuxu`.
Receipt: `revision-qualification-manifest.json`, SHA-256
`e4a97d74ed3fc388e011871abb23af90aae4849f1992d85823b696052ffa57a9`.
Private ELF SHA-256:
`c85e01a1d346acba95a60491740858251dc90dabf78213d8028a7cb8c3c5cced`.
Plugin SHA-256:
`795a826f771dfd5cfe3b56c719f0f036f897184457fbe4c132b917d4c5c2f044`.

The matrix comprises **23 fresh cases and eight inherited exact-artifact cases**:

| Coverage | Cases | Audited control cycles |
| --- | ---: | ---: |
| Frozen-clock duplicate dispatch | 1 fresh | 354 |
| Full-axis rate/reversal/release/impulse, both poses and orders | 4 fresh | 25,874 |
| Individual-axis, both poses/signs, and six fault/recovery cases | 18 fresh | 38,533 |
| Actual EKF2 lifecycle/reset/profile barriers | 8 inherited | 3,968 |
| Total | 31 | 68,729 |

The inherited cases are sealed in [the actual lifecycle package](STABILIZED_MANUAL_SM4_PUBLISHER_LIFECYCLE.md)
and use the identical ELF, plugin, runtime profile and test sources. They were
revalidated and copied into the combined evidence; they are not represented as
fresh reruns. The existing independent component audits and all numerical limits
remain unchanged. The final receipt binds 1,163 artifacts, all independently
rehashed through Windows with zero mismatches.

| Full-axis case | Final physical hold error, rad | Peak physical error, rad |
| --- | ---: | ---: |
| Level / positive first | 0.005147 | 0.490910 |
| Level / negative first | 0.002102 | 0.491049 |
| Body-X-up / positive first | 0.002832 | 0.497045 |
| Body-X-up / negative first | 0.006522 | 0.490757 |

Maximum full-axis physical rate error is 0.028230 rad/s against 0.08; final
physical hold error is at most 0.006523 rad against 0.02. Peak remains below
0.5 rad with limited margin. Directional antiwindup checks pass. The five frozen
dispatches add no control or lease refresh, and producer-stall expiry remains
100 ms. Every published session keeps a constant bound revision tuple/epoch.
These are finite declared-case results, not a vehicle operating envelope.

## Preserved previous console verification

Evidence: `/var/tmp/research-stabilized-console-check-q4jfno7x`.
Receipt: `console-manifest.json`, SHA-256
`e3755715192e281ab7660f17ef81e40100aca59a9b33d8fa49f0820391bb56a7`.
The actual console was driven through stdin against live PX4/Gazebo at the
default body-X-up pose. Observed control rows verify body-axis stick mapping,
rate-to-hold capture, opposite sticks, invalid-NaN input requesting hold,
consumer disable, explicit new-session recovery and cleanup. Sessions advance
from 5,404,000 to 6,636,000; PX4 and Gazebo both exit with code zero.
Five check files and 41 operator-session files independently rehash with zero
mismatches. Final process checks find no live PX4 or console Gazebo process.

The stock Cessna baseline, H10 bench evidence, original CAD/model and
negative-only C1E-N1 acceleration gates retain their existing scope. Shared
uORB and checkout owner sources remain unchanged; the revision guard is in
the explicitly staged private POSIX build. Node advertisement revisions cover
observable lifecycle transitions, with individual publisher-handle identity
and production flight-health policy still unqualified.

## Remaining work and inputs

The fresh source-build/qualification/console workflow is complete for the
declared synthetic scope. Vehicle-supported
SM5 allocation/tuning still requires accepted CAD mass/CG/full inertia,
canard geometry/signs/effectiveness versus airflow, loaded servo calibration
and dynamics, EDF thrust/lag/wash, physical operating limits and loss/recovery
policy. The user reports measurements unavailable and CAD pending; no new CAD
request or physical action is made here. The goal remains active.
