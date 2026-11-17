# Integrated stabilized-manual SITL candidate

2026-10-20, Codex. The separate producer/session, independent output consumer and
session-aware Gazebo plant now pass one fresh 30-case campaign on the same binary,
plus the actual operator console test. This is a runnable synthetic candidate;
the legacy qualified default console remains unchanged. No hardware was operated.

## Operate the candidate

From WSL Ubuntu-24.04 in the project directory:

```sh
python3 Tools/research_sitl/stabilized_session_console.py \
  --qualification /var/tmp/research-stabilized-session-qualification-_e84t5eb
```

The console verifies the qualification, child receipts, binary/plugin and current
runner identities before startup. It uses a separate PX4 instance and ports, and
a unique Gazebo partition. The default pose is nose-up; `--level` starts level.
Airflow is prescribed at 20 m/s, simulated EDF thrust is fixed at 10 N, and a ball
restraint prevents translation. These are synthetic assumptions, not vehicle data.

- `sticks <roll> <pitch> <yaw>` commands normalized body-axis sticks in [-1, 1].
  Commands persist until changed. For example, `sticks 0.2 0 0` requests roll rate.
- `hold` centers all sticks; the controller captures quaternion attitude on release.
- `status` reports the mode and fault code.
- `disable` centers sticks and waits for confirmed plant disarm.
- `restart` confirms disarm first, then opens an explicit new session with centered
  sticks. It works from active or disabled state; it cannot bypass pending disarm.
- `quit` confirms disarm and shuts down both PX4 and Gazebo. Invalid stick values
  or unrecognized commands center the sticks and request hold.

The command prints the retained session/log directory. This candidate does not
replace `stabilized_manual_console.py` or its default profile.

## Current request-owner candidate

The producer now uses the shared `SessionRequestOwner` for bounded grant requests,
latched revoke and explicit restart admission after confirmed disarm. Pilot
semantics, plant, gains and performance limits are unchanged.

- Build `/var/tmp/research-stabilized-sitl-0o5n1g_b`.
- ELF `8495c11d2452c86ef22f1b7c28f6d6d987850a9377425c99651f716bf0d5656b`.
- Build receipt `de24b85bd0fb41383efa7da8c3b3d0514d3cc0375f4e0ef1d68c344ed696dc4f`.
- Qualification `/var/tmp/research-stabilized-session-qualification-_e84t5eb`,
  receipt `62722f917fdf5a048b2f5a5da014c754dc449fd127a8d2c7f58fb830ff2d8209`.
- Console `/var/tmp/research-stabilized-session-console-check-uk_m1tzr`, receipt
  `86e11af05ca16569bed94d2dcf03eb7376b901fe8af464cd74f972d2b54d6093`.
  Actual session `/var/tmp/research-stabilized-session-console-92e9nysn`.

All 30 fresh cases and eight actual console checks pass. Independent audit
rehashes 786 child artifacts and audits 24,311 maneuver cycles. Worst rate error
is 0.02871573 rad/s (<0.08), final physical hold error 0.00574914 rad (<0.02),
and peak physical hold error 0.49008303 rad (<0.5). The peak margin remains narrow;
this run does not establish a broader robustness envelope or vehicle tuning.
The console artifacts and session files also independently rehash.

This supersedes the earlier candidate as the documented integrated console input;
its binaries/evidence remain retained. Legacy default is unchanged. Production
hardware scope is still pending, and no physical authority is granted. See the
[ownership record](STABILIZED_MANUAL_PRODUCTION_OWNERSHIP.md) for next integration work.

## Earlier candidate evidence (retained)


Build: `/var/tmp/research-stabilized-sitl-vj5bpho5`.
ELF SHA-256: `27e9a13244ff98c9006d12f64bbb313a1708c5f793ff260f00c3fc82ef896e40`.
Build manifest: `65c28ae6a2c673a65598076dc4459387ba584d80b76dbe2ba81209f9a94b570d`.

Qualification: `/var/tmp/research-stabilized-session-qualification-pj64a0bz`.
Manifest SHA-256: `0d486c1291677620083b3683fabc8636e2f5d2d2786c780829570f749c3716a4`.
All 786 child artifacts independently rehash. The separate `independent-audit.json`
records 24,214 audited maneuver cycles; it does not overwrite sealed receipts.

The 30 fresh cases include twelve single-axis rate/release/impulse cases, four
coupled full-stick reversal/saturation/anti-windup cases, and fourteen fault cases:
manual loss, replacement-source traffic, estimator loss, routing change, process
stall, each queue independently stalled, command loss, acknowledgment loss/delay,
grant/disarm reply loss, and command/ack publisher replacement. Gains, timing
limits and performance criteria are unchanged.

| Metric | Worst measured | Existing limit |
| --- | ---: | ---: |
| Rate error | 0.02853727 rad/s | 0.08 rad/s |
| Final physical hold error | 0.01308045 rad | 0.02 rad |
| Peak physical hold error | 0.49519025 rad | 0.5 rad |

The peak margin is narrow. The campaign establishes these declared fixture/seed
cases, not a broad robustness envelope or installed-vehicle tuning.

Console evidence: `/var/tmp/research-stabilized-session-console-check-5phbp386`.
Manifest SHA-256: `401069384725d149427c04daf0812c456be7f910fb08bfb9466419fd869927d4`.
Actual stdin interaction verifies sticks, hold, invalid-input centering, active
restart, confirmed disable, disabled-state restart, quit disarm and process cleanup.
Session: `/var/tmp/research-stabilized-session-console-u209a9dh`.

Reproduce the campaign with `qualify_stabilized_session.py --evidence` pointing to
the build above, then run `verify_stabilized_session_console.py --qualification`
with the newly produced qualification directory. Each run retains fresh evidence.

## Additional seed exploration and release diagnosis

The unchanged candidate passed twelve additional coupled cases across seeds 7,
13 and 73, level and nose-up attitudes, and both command signs. All 305 evidence
files rehash; 30,396 control cycles were audited. Worst rate error was
0.02871478 rad/s, final physical hold error 0.00775293 rad, and peak physical
hold error 0.49550473 rad against the unchanged 0.5 rad limit. This is additional
seed exploration at the same synthetic condition, not expanded qualification.

Evidence: `/var/tmp/research-stabilized-session-robustness-xy5fsxve`.
Manifest SHA-256: `590a2d68c5bbbea56ac9a6dbfc394234a6fce0a97bac8abdb460b1c537a73e49`.
Runner: `Tools/research_sitl/explore_stabilized_session_robustness.py`.

Analysis of the original four qualified coupled cases locates the peak physical
hold error 484 ms after full-stick release, before the later injected impulse.
Pre-impulse peaks were 0.48926?0.49519 rad; post-impulse peaks were
0.26429?0.27411 rad. This identifies release braking as the phase to examine
for additional margin; it does not establish a physical root cause or justify
vehicle tuning. No gains, profiles, limits or qualified runners changed.

Analysis: `/var/tmp/research-stabilized-session-release-analysis-vrx6kdrl`.
Manifest SHA-256: `6113a4ce55105653649cfe718a48bf91972090364607db460d42580401b1078b`.
Tool: `Tools/research_sitl/diagnose_stabilized_session_release.py`.

The extended per-axis timing analysis preserves the original receipt and adds
`/var/tmp/research-stabilized-session-release-analysis-1m5jnfka`, manifest
`f4a192b6a1a4b2442357c7393d07f0d7c4d8b6a3694c8924d94f1bfdbc1b9811`.
Relative to the first emitted hold row, rate setpoints cross zero in 40?52 ms,
requested moments oppose initial rotation within 0?12 ms, allocated moments
within 0?84 ms, and physical rates first cross zero in 320?560 ms. These are
sampled first crossings, not sustained settling times or measured actuator torque.
The earlier 484 ms peak delay is relative to the captured attitude sample, so
these timestamps must not be conflated. This supports examining constrained
plant/controller braking response; causal attribution requires a controlled
comparison. No controller change or relaxation of limits is justified by timing
alone. The next useful comparison should keep pilot semantics, plant parameters,
and acceptance limits fixed and preserve the qualified candidate separately.

## Active portable components: ARM resource probe

The current session, output driver and output contract compile with the retained
Pixhawk 6X e8ea/NuttX 11 board flags and ARM GCC 13.2.1. The pinned Docker image
and read-only baseline are unchanged; no firmware is linked or hardware operated.
Stage: `build/stabilized_active_resources/20261020`. Its recipe, compiler commands,
dependency snapshots, object files, symbol sizes and stack reports are retained.

| Component | Unlinked object text | Object storage |
| --- | ---: | ---: |
| StabilizedManualSession | 1,014 B | 1,120 B |
| StabilizedOutputDriver | 1,058 B | 512 B |
| StabilizedOutput producer/consumer contract | 2,068 B | Included in owners above |

The 4,140 B text subtotal exceeds the prior image's 3,264 B spare flash, but is
**not an incremental linked-size estimate**: section garbage collection, shared
functions, alignment and other integration code affect the final result. This
makes a full active-path link/resource assessment a priority; it neither proves
that the image fits nor proves overflow. Production scheduling, permission and
physical-driver glue are not measured by this probe.

Compiler-reported local stack frames include session update 272 B, driver prepare
136 B, complete 176 B and dispatch 112 B. These exclude callees, interrupt context
and runtime stack high-water; do not use them as queue stack allocations.
Receipt SHA-256: `9f2da1f37bbee42d4490618a695d99bda0655f688dafa0fa5ba12c64c62a5ff5`.
All 156 receipt files rehash and 8 current-root dependency snapshots match.

## Diagnostic firmware relink: flash constraint confirmed

Stage `build/stabilized_active_resources/20261020-link` retains the exact original
Ninja link command, redirected diagnostic commands, maps, logs, forced-symbol list
and successful diagnostic ELF/bin outputs. The baseline relink reproduced the
retained 1,962,816-byte firmware binary byte-for-byte. Replacing the core and
validator with the current ARM probe objects linked at 1,962,880 bytes (+64 B).
Adding the session/output objects and forcing retention of their defined text
symbols failed specifically with **FLASH overflow by 960 bytes**: 1,967,040 B
used against 1,966,080 B available. There were no unresolved-symbol errors.

This is a retained-object diagnostic, not a clean production build or runnable
active mode. Forced retention includes optional entry points and private methods;
it does not establish the minimal reachable production size. It adds no active
owner instances, queue stacks, scheduling, permission supervisor or physical
output glue. Its RAM report therefore does not represent active-mode RAM needs.
The next resource step is an exact intended-entry-point link and a deliberate
research-target flash budget, followed by full integration and a clean build.
Do not remove stock features or enlarge the linker region to conceal overflow.

Manifest SHA-256: `4e13a235770b7676eda3fe6c92fc2627b09198c11599a78880eabb5cd65f4f76`.
All 17 manifest files independently rehash. Original firmware, source, board configuration
and retained build volume remained read-only. No package was produced or flashed.

The follow-up entry-point probe at
`build/stabilized_active_resources/20261020-entrypoints` retains only the eight
out-of-line APIs used by the current producer and split consumer: session
begin/update/disable and driver begin/cancel/revoke/prepare/complete. Dependencies
remain linker-reachable normally; inline accessors require no separate roots.
It still overflows FLASH by **832 B** (1,966,912 B used), saving only 128 B versus
full API retention. This is evidence for a deliberate separate research-target
feature budget, not permission to alter stock configuration. Full production glue,
RAM instances and runtime checks remain absent. Keep actual board capacity fixed.
Manifest `02cf1be54da4bdde33b7234f7c4ad75e77c98d61909c4242e098eebdfe2324af`; all 7 files rehash.

## Separate research build configuration

`boards/px4/fmu-v6x/slaaps-research.px4board` defines the separate
`px4_fmu-v6x_slaaps-research` development target. It inherits default and disables
only stock fixed-wing and multicopter autotune, which do not tune the custom
SLAAPS controller. It enables the existing output-disabled research observer;
the experimental output hook stays disabled. There is no automatic startup.
All other resolved symbols match default, including sensors, stock control modes,
telemetry and logging. The stock board file remains unchanged.

Actual current-checkout Kconfig resolution verifies exactly those three effective
changes, with evidence in `build/stabilized_active_resources/20261020-config`.
Manifest SHA-256: `fd91b6f5270eac73a925b66deca40d15df3077e963b596d9d8dcf8143b69f4e1`.
This is a configuration check, not a built or hardware-qualified image. Next port
this exact feature delta onto the retained working e8ea baseline in an isolated
source copy, perform a clean build and measure its resource margin. Do not use the
newer root baseline's known boot failure as a substitute for that working-baseline
build. The active permission owner and physical output integration remain pending.

## Clean working-baseline research firmware

The separate variant now clean-builds on e8ea/NuttX 11 in volume
`px4-stabilized-research-e8ea-20261020`, using the verified prior observer sources
plus the single variant file. Network and compiler cache were disabled; no build
objects were copied. Before/after source and submodule guards passed.

Stage: `build/stabilized_research_firmware/e8ea-20261020`.
Target: `px4_fmu-v6x_slaaps-research`, board ID 53, explicitly dirty overlay.
The decoded image is **1,943,864 B**, leaving **22,216 B** of actual flash capacity,
a saving of **18,952 B** versus the prior default observer. ELF text is 1,938,072 B,
data 5,792 B and BSS 104,816 B. Generated config differs from the prior observer
only by removal of the two stock autotune modules. Symbol checks confirm their
entry points, bench/test owners and active session/output owners are absent.

Firmware receipt SHA-256: `14b605d9f2f99b24766125e218731aa73bbae68d0c05f01df07f1343fd4ca91e`.
Package SHA-256: `31a34415a4fcc93a223c813d579ff3a081b892aef4a1aaeabacba4f6041211b8`.
Independent verification rehashes all 24 new receipt files and 131 parent files,
checks package/bin equality and exact resolved configuration differences.
This is an output-disabled budget baseline, not active-mode firmware or hardware
acceptance. Next repeat the intended-entry-point probe against this actual image,
then integrate production ownership and perform the full clean resource/runtime
checks. Stock firmware, startup and hardware/bench/acceleration gates are unchanged.

The intended-entry-point probe now passes against the actual research image.
Stage: `build/stabilized_active_resources/20261020-research-link`.
The baseline binary again reproduces byte-for-byte. Current core/validator plus
the eight session/split-driver roots produce a 1,947,960 B binary, a **4,096 B**
increment, leaving **18,120 B**. ELF text 1,942,168 B, data 5,792 B, BSS 104,816 B.
Manifest `408f9b1112d31d321243eb069e2f4bb45b046468e69d3c64ff67387f97fbd07f`; all 14 files rehash.
No firmware package is exported from this diagnostic. Owner instances, supervisor,
selected-input integration, queues and physical adapter are still absent, so
this does not prove the complete active mode fits or establish runtime resources.

Source inspection confirms the production `ResearchStabilizedManual` is still
observation-only, with one diagnostic publisher. `ResearchStabilizedOutput` is
an unattached mixer adapter requiring an owner-provided permission/source contract.
The next integration task is a deliberate production request/profile owner and
independently scheduled producer/consumer, reusing the verified selected-state,
grant and lifecycle contracts. Keep physical authority gated on reviewed vehicle
profile/permission admission and the existing hardware gates. Preserve the
observer and stock paths while developing and testing the new owner; do not
substitute the synthetic SITL permission predicate for vehicle admission.

## Coverage boundaries and next steps

| Requirement | Evidence and limit |
| --- | --- |
| Pilot rate/hold and allocation | Integrated 16-maneuver campaign and cycle audit; synthetic plant only. |
| Independent consumer and plant expiry | Integrated queue/process stalls, timeout, revoke and restart checks. Sampled stalls are not exhaustive lock-contention proof. |
| Command/ack publisher lifetimes | Integrated identical-payload replacement before timeout. Lifetime continuity is not authentication. |
| Selected-input and grant request/response hidden replacements | Separate native tests using the same shared helpers; not reexecuted as hidden replacements against the integrated plant. |
| Operator path | Actual candidate console passes; legacy default is preserved. |
| Production board/physical output | Not granted by these tests. Supervisor policy, active-path full ARM resource review and physical driver integration remain open. |

The native coverage review at `/var/tmp/research-stabilized-session-coverage-review-_k8dqh_n`
rehashes all 735 selected-health native files and confirms the current selected-state
sampler/guard, input validator and grant-handshake sources exactly match final
native snapshots. Review SHA-256: `995221195ed575901fe27ecd2bc9de7d250e6a5ff949554845943452e0edf631`.
That fixture uses synthetic inputs and a memory sink; its evidence remains distinct.

Next software work should address the remaining coverage/robustness boundaries and
active-path ARM resources before any production integration claim. Vehicle-specific
work requires CAD mass/CG/inertia and axes, signed canard authority versus flow,
loaded servo calibration/dynamics, sensor/input timing and reviewed operating limits.
Measured EDF thrust/lag/wash is needed for real propulsion. The user has already
reported that measurements are unavailable. Stock, bench, acceleration and H10 gates
remain preserved; no physical authority or firmware-flash permission is implied.

Changes are uncommitted. Preserve the local WSL evidence and workspace for a fresh
chat; the repository alone does not contain the retained native binaries and logs.
