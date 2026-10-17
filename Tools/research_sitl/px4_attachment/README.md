# C1A passive PX4 attachment

This opt-in Linux/WSL experiment builds a minimal PX4 SITL executable and reads
actual Commander state and native clocks. It is **not** the research receiver,
custom mode, estimator, controller, plant or command-admission implementation.
No research network endpoint or admission owner is instantiated. PX4 logging
may publish diagnostic `log_message`; the probe publishes no research/control
topics. Commander publishes its own state/control-mode topics.

Read `docs/C1_PX4_ATTACHMENT_DESIGN.md` for the reviewed scope and known gaps.
Use only the existing Ubuntu distribution and installed dependencies:

```sh
python3 -B Tools/research_sitl/px4_attachment/run_probe.py
python3 -B Tools/research_sitl/px4_attachment/run_probe.py --run
python3 -B -m unittest discover -s Tools/research_sitl/px4_attachment -p 'test_*.py'
python3 -B Tools/research_sitl/px4_attachment/check_probe_evidence.py /var/tmp/c1a_px4_<id>/manifest.json
```

The first command builds only. `--run` additionally starts two serial fresh
PX4 processes with private runtime directories and a two-second passive probe.
By default each command creates a new retained `/var/tmp/c1a_px4_*` evidence directory.
`--resume-build /var/tmp/c1a_px4_<id>` permits an incremental retry only of this
runner's failed same-source, same-revision build before any runtime was created.
It validates the private source/build paths and cached source identity, archives
the prior failure and configuration, and repeats configuration and builtin checks.
Such evidence is labelled an incremental retry, not a clean-from-empty build.
It never reuses the stock cache, mounts anything, installs/fetches dependencies,
starts Gazebo/QGC, arms, enters Offboard, or enables actuator output.

The source view contains symlinks to this checkout, with private `.vscode` and
build paths. Source is neither moved nor copied. The explicit minimal defconfig
does not merge stock default modules. Build-time submodule updates are disabled;
missing dependencies are failures to resolve explicitly, not permission to fetch.

`px4_external` deliberately avoids the globally ignored name `external`.
The external-module hook does not modify the default PX4 board or stock dialect.
Its compile guards reject hardware, non-Linux, lockstep and proxy-clock builds.
Six existing Navigator/sensor/RC/logger parameter YAMLs supply definitions
referenced by Commander; their registration does not select those runtime modules.
Startup sets `COM_ARM_ON_BOOT=0` and `COM_ARMABLE=0` in the disposable runtime,
starts Commander, runs the passive probe, records advertised topics, stops
Commander and returns to the shell for normal shutdown. These parameters do
not substitute for actual observed disarmed-state evidence.

Retain `configure.log`, `build.log`, `source_hashes.json`, `manifest.json` and
each run's `stdout.log`/`run.json`, including failures. Build inputs are hashed
before/after; root source hashes do not recurse into Git submodules. Recursive
submodule revisions and the separate Gazebo revision/status and Cessna SDF hash
are recorded, not misrepresented as an archive of every submodule byte.

`built_runs_unscored` is incomplete, not a pass. The independent checker verifies
actual compact records, timing order, counts, disarmed publications, completion,
normal exit and log hashes. Its synthetic mutation tests are not measured PX4
evidence. Master review must also inspect builtins/build config and source to
accept the bounded no-output scope. A topic snapshot alone cannot prove every
instant of the run; publication generation cannot prove every internal Commander
state transition. Neither clock agreement nor compilation qualifies a live map.

Clock keys: `b/a` are host monotonic nanoseconds before/after a PX4 hrt sample;
`h` is PX4 hrt microseconds. State keys: `us` timestamp, `g` generation, `d`
generation delta, `f` post-start freshness, `s` arming state or `a` armed flag.
Counts `c/v/a/ns` mean valid clock samples, fresh status samples, fresh armed
samples and elapsed host nanoseconds. Units/domains are never interchangeable.

No scope here authorizes a hardware build/flash, physical test, commit or PR.
