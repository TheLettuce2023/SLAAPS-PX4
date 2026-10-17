# Stock SITL diagnostic integrity audit

The project user approved the initial diagnostic-only stock Cessna limits on
2026-09-27. See
[`diagnostic_limits_v1.json`](diagnostic_limits_v1.json) and the
[approval record](../../../docs/SITL_BASELINE_DIAGNOSTIC_LIMITS_APPROVAL.md).
The registry SHA-256 is
`42549c019ad500dbb63b184b294170759c315e4333309e8074e3a633e9db7142`
for future campaign `slaaps-stock-cessna-phase1-acceptance-01`. This does not
change the v0 auditor or raw inspector: neither applies the approved limits or
can grant a Phase 1 pass. The new workflow below uses the separately approved
[revision 2 registry](diagnostic_limits_v2.json) and
[revision 2 approval](../../../docs/SITL_BASELINE_DIAGNOSTIC_LIMITS_V2_APPROVAL.md)
for campaign `slaaps-stock-cessna-phase1-acceptance-02`. Its sole policy change
is the verified stock synthetic-pressure capture-time exception.

Run the read-only v0 auditor from Ubuntu-24.04 WSL against the retained campaign:

```text
python3 -B Tools/research_sitl/baseline/audit_diagnostic.py /var/tmp/slaaps-baseline-20260927-01
```

Exit `3` means `incomplete`; exit `2` means integrity failure. The auditor checks
the exact 32-file inventory, SHA-256 and byte counts for every inventoried file
and both ULogs, and cross-links the campaign, build, QGC and measurement records.
It rejects a claimed Phase 1 pass from this v0 package. Run the four synthetic
tamper regressions with:

```text
python3 -B -m unittest discover -s Tools/research_sitl/baseline -p test_audit_diagnostic.py -v
```

`integrity: verified` means the stored artifact bytes and selected metadata
links matched this audit. It does not mean the ULog measurements were
independently recalculated or the runtime transcript was authenticated. The
historical v0 package lacks approved numeric criteria, a complete dirty-source
archive, human QGC visual observations and second-operator repetition. This
auditor cannot return Phase 1 pass. An integrated acceptance checker needs a
new, predeclared registry and campaign manifest with raw-signal checks.

## Independent raw ULog inspection

`inspect_raw_ulog.py` requires the installed PyULog and NumPy packages. It
verifies an externally supplied SHA-256, reads 15 selected topic instances
from the raw ULog (including `sensor_combined`), recomputes timestamp and
sample ordering, maximum logged gaps, finite-field counts and equal attitude
publication-time pairs. With `--archived-measurements`, it compares the 14
shared topics' selected timing, finite-count and attitude-pair fields against
the older measurement-only report using exact JSON types. It does not compare
that report's validity distributions or percentile gaps. It writes only to a
new `--output` path and refuses to overwrite one.

```text
python3 -B Tools/research_sitl/baseline/inspect_raw_ulog.py \
  /var/tmp/slaaps-baseline-20260927-01/build/px4_sitl_default/rootfs/fs/log/2026-09-27/21_20_19.ulg \
  --expected-sha256 98f2c04481a494d0d2bf7cdb0e81c6ab3295be130218738852cee68d01f9be7f \
  --archived-measurements /var/tmp/slaaps-baseline-20260927-01/evidence/ulog-measurements-headless.json \
  --output /var/tmp/slaaps-baseline-20260927-01/my-new-headless-inspection.json
```

Exit `3` is a valid, **incomplete** measurement; exit `2` is a hash, parse,
schema or archived-report consistency failure. This reader does not apply the
newly approved timing registry or a predeclared scoring interval, and it has
no process/source authenticity check or Phase 1 pass path. It measures logged
records, not raw uORB or sensor
publication rates. It uses the same PyULog decoder as the archived one-off
analyzer, so the cross-check exercises separate measurement logic but cannot
detect a shared decoder error. The existing v0 artifact auditor remains
separate.

Run the baseline tests with:

```text
python3 -B -m unittest discover -s Tools/research_sitl/baseline -p 'test_*.py' -v
```

## First-operator acceptance workflow

`prepare_campaign.py` archives every dirty tracked and nonignored untracked
file in the checkout and initialized submodules, plus critical model, airframe
and synthetic-publisher inputs, including symlink targets and file modes.
Clean tracked source is identified by repository revision; complete binary
diffs, working-tree status and absent submodules are recorded. It rebuilds
stock SITL with the existing native cache, checks that
source inputs stayed identical, and retains the executable, build transcript,
registry, approval, parameter image and signal-inventory document together.
Run as WSL root; use a new package path and an unmounted source-mount path.

```text
python3 -B prepare_campaign.py --root <new-package> --source <checkout> \
  --mount <reviewed-source-mount> --build <native-build-directory> \
  --parameters <pinned-parameters.bson>
python3 -B capture_run.py --root <package> --source <checkout> \
  --mount <same-source-mount> --build <same-native-build-directory> \
  --mode gui --run-id operator1-gui-01 --operator <operator-id>
```

The collector resets both parameter files, writes its immutable plan before
launch, requires no existing PX4/Gazebo processes, checks readiness, records
10 s settling and 60 s scoring in PX4 time, and brackets the interval's UTC
boundaries with live listener observations. WSL wall UTC can step backward;
the checker preserves and reports those observations while using the approved
PX4 monotonic clock for interval scoring. It never commands arming. GUI
capture stays live for a human QGC record at the printed `qgc.json` path, for
up to ten minutes after scoring; it then shuts down even if the human record
is missing. The human record contains `run_id`, live observation `utc`, five
boolean fields (`parameter_loading`, `live_attitude`, `gps_map_position`,
`displayed_mode`, `mavlink_inspector_traffic`), and `comms_lost` (`none`, `live`,
`after_normal_shutdown`, or `unknown`). Preserve the observer's actual words
and identity as additional fields. Do not supply a synthetic observation.

Normal shutdown uses the PX4 shell and SIGINT only for recorded Gazebo PIDs
whose kernel start times still match the owned session. Source and executable
hashes are checked again after shutdown. Repeat with `--mode headless` and a
new run ID after GUI closes. This workflow predeclares the same heartbeat/RX
expectation in both modes; only GUI requires the human display observation.

```text
python3 -B acceptance.py <package> <package>/<run-id>/manifest.json \
  --output <package>/<run-id>/result.json
```

The checker verifies archived source bytes and artifact links, lifecycle
chronology, raw readiness/state and signal criteria, and human QGC evidence.
It rejects altered registries and escaping artifact paths. Logged-gap checks
include both interval boundaries; the preceding sample supplies the state
at the left boundary. UTC observations are brackets, not an assertion of
exact wall-clock/simulator synchronization. Runtime bridge/source/channel
checks retain transcript excerpts for review. These records are local capture
evidence, not cryptographic authentication of the operator or host.

Exit codes are 0 for a passing single run, 2 for failure and 3 for incomplete.
`phase1_result` stays `incomplete`: the second-operator reproduction is
explicitly deferred for this checkpoint. The seven focused acceptance tests cover
missing streams, same-tick attitude samples, boundary staleness, stalled RX,
changed policy bytes, escaping evidence paths, asynchronous PX4 prompt output
and observed WSL wall-clock steps.

`check_checkpoint.py <package> --output <new-report.json>` recomputes both
planned run results, checks identical source/executable/parameter/policy
identities and serialized lifecycles, and separates automated success from
pending human QGC evidence and the deferred second-operator gate.
