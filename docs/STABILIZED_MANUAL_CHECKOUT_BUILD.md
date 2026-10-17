# Stabilized manual: fresh build from checkout

2026-10-06, Codex. A private PX4/plugin pair now builds from the current
checkout and initialized submodules, without consuming historical SITL source
trees or binaries. Fourteen native uORB checks and five focused SITL cases pass
over 14,375 audited control cycles. All 387 receipt artifacts independently
rehash from Windows with zero mismatches. Subsequent
[full fresh qualification and console verification](STABILIZED_MANUAL_CHECKOUT_QUALIFICATION.md)
now pass all 31 cases over 68,813 cycles, and the
[qualified console default](STABILIZED_MANUAL_ISOLATED_MODE.md) uses this pair.
The original focused build scope below remains preserved.

## Build and verify

This workstation has Ubuntu-24.04 WSL, the PX4 Python/compiler dependencies,
CMake, Ninja, astyle and Gazebo Harmonic development packages. The checkout's
46 submodules must be initialized at their indexed commits. PX4's stock DDS
build fetches its pinned Micro-CDR dependency when needed, so a clean build
also needs access to that public dependency.

From PowerShell:

```powershell
wsl -d Ubuntu-24.04 -- python3 '/mnt/c/Projects/SNC SENIOR DESIGN/FC Software + SITL/Tools/research_sitl/build_stabilized_manual_checkout.py'
```

The command prints a new `/var/tmp/research-stabilized-sitl-*` directory.
Use that exact directory in both verification commands:

```powershell
wsl -d Ubuntu-24.04 -- python3 '/mnt/c/Projects/SNC SENIOR DESIGN/FC Software + SITL/Tools/research_sitl/verify_stabilized_checkout_build.py' --evidence /var/tmp/research-stabilized-sitl-q90g9o8f
wsl -d Ubuntu-24.04 -- python3 '/mnt/c/Projects/SNC SENIOR DESIGN/FC Software + SITL/Tools/research_sitl/verify_stabilized_checkout_build.py' --evidence /var/tmp/research-stabilized-sitl-q90g9o8f --seal
```

`--jobs` controls build parallelism, default four. A prepared build can be
continued with `--build-only <printed-directory>` while its staged inputs and
preparation anchors remain unchanged. The source guard runs before and after
compilation. Verification deliberately alters a copied allocator source,
observes rejection, restores the exact bytes, and then runs the native cases.
Use a new build directory for changed inputs or a new qualification scope.

## Source boundary

The successful snapshot contains **57,547 indexed and research inputs** from
PX4 and 46 local submodules. Private Git metadata borrows local objects for
version generation. Git archives stage committed files; tracked working
changes, explicit research overlays and exact graph anchors supply current
checkout content. Unchanged archived files retain committed line endings.
The staging command uses native Git for Windows-drive working-file scans under
WSL and Linux Git for Linux checkouts. Indexed files omitted by a submodule's
export rules are copied directly from the checkout.

The registered untracked `BenchServoCommand.msg` and mixer/input integration
files are included explicitly. The private node/owner revision transform is
applied only under `px4-source-publisher-epoch`; shared checkout uORB, the
constant-epoch owner template and stock board configuration retain their bytes.
Native linkage checks require the private owner, allocator and revision test,
and reject inherited active bench modules. The original eight fixture inputs
are separately copied under `px4-source` for the existing harness contract.

The actual build-fetched Micro-CDR commit is
`3d1b17703c7cf4f22def2910bc845bdb5152d7b5`. Its tracked sources and identity are
captured separately in `external-compiled-inputs` and `external-source-origin.json`.
Installed compiler/Gazebo dependencies are described by configure logs and
CMake caches. Build paths and environment affect artifact hashes; this is a
repeatable source workflow, with exact output identities recorded per build.

## Finite evidence

Evidence: `/var/tmp/research-stabilized-sitl-q90g9o8f`.
Receipt: `checkout-build-manifest.json`, SHA-256
`95e129cbcad336fce318bdb47817e9a8e3ea96a464757997be0606e4e3ea493b`.
Fresh ELF SHA-256:
`67a6bab516dcdf0b9d7df76d548b3931f6c03fcb93b7814242e966690a6a0193`.
Fresh plugin SHA-256:
`a5addecdf9ed5378ffba95a1dd0dde62df44d08240c4a26541393edb577fb789`.

| Case | Audited control cycles | Result |
| --- | ---: | --- |
| Frozen-clock duplicate dispatch | 337 | Pass |
| Full-axis rate/reversal/release/impulse, level | 6,473 | All 12 named performance/feedback checks pass |
| Full-axis rate/reversal/release/impulse, body-X-up | 6,454 | All 12 named performance/feedback checks pass |
| Hidden attitude advertisement cycle, body-X-up | 614 | Revoke, explicit fresh-session recovery, consumer off in 4 ms |
| Actual rapid EKF2 stop/start, body-X-up | 497 | Module lifecycle observed, revoke/recovery, consumer off in 4 ms |

The worst coupled physical rate error is 0.028800 rad/s against 0.08; the
worst final physical hold error is 0.009562 rad against 0.02. Both cases have
zero directional antiwindup violations. Publisher-revision tuples stay bound
within every published session. Native self-test passes 14 checks. Final
process checks find no live PX4 or Gazebo process for this fixture.

Direct-copy and Linux-Git-scan staging attempts, an export-rule failure and
the initial missing-message build remain preserved as incomplete attempts.
Receipt-audit schema corrections did not change runtime results or limits.

## Next boundary

The [full campaign and operator workflow](STABILIZED_MANUAL_CHECKOUT_QUALIFICATION.md)
is now admitted on this exact pair, with all 31 cases fresh and the console
verified before default promotion. Earlier eight-case receipts belong to a
different ELF/plugin pair and retain their original scope. The five cases here
establish the separately sealed focused build check.

The fixture remains synthetic: 20 m/s prescribed airflow, fixed enabled 10 N
EDF thrust at CG and restrained translation. Vehicle-supported allocation and
tuning still need accepted CAD mass/CG/full inertia, canard geometry/signs and
effectiveness versus airflow, loaded servo calibration/dynamics, EDF
thrust/lag/wash, operating limits and loss/recovery policy. Measurements remain
unavailable and CAD pending. The goal remains active.
