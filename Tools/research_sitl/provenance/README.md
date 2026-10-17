# Closed archive provenance collector

P2A is an offline observation tool, not the live P2 admission owner. It extracts
real logged estimator values from existing ULogs without running PX4, modifying
baseline evidence or opening an endpoint. No adapter, publisher, operator grant
or clock-map installation API is imported or exposed.

Requires Python 3.9+, PyULog and NumPy. Verified environment is recorded with
each checkpoint; it is not installed or changed by these scripts.

```sh
python3 -B Tools/research_sitl/provenance/collect_archive.py /path/to/log.ulg \
  --sha256 EXPECTED_HASH_FROM_RETAINED_EVIDENCE --output /new/output/directory
python3 -B Tools/research_sitl/provenance/check_archive.py /path/to/log.ulg \
  --sha256 EXPECTED_HASH_FROM_RETAINED_EVIDENCE --archive /new/output/directory
python3 -B -m unittest discover -s Tools/research_sitl/provenance -p test_archive.py -v
```

Output must be a new directory. `manifest.json` is written last and is the
completion marker; `.partial` files and directories without it are incomplete.
Expected ULog hash is required independently, checked before and after parsing.
The parser's corruption flag rejects collection. A complete extraction proves
only faithful extraction of the selected parsed records, not completeness of
the original flight log or observation of every publisher transition.

The tool examines all logged instances of estimated attitude/local position,
estimator health flags and selector status. Groundtruth and external-ins topics
are excluded. It preserves each stream's logged order, including equal or
backward timestamps, without manufacturing a global receipt order. Stream
names/instance numbers are logged identities, not authenticated publisher
lifetimes. `logged_index` starts at zero and is an extraction index, never a
uORB/source sequence or transition epoch.

`receipts.jsonl` stores selected field values exactly as decoded; nonfinite
values use explicit `nonfinite` tags. Missing topics/fields, logger dropouts,
timestamp regressions, sample age at publication, observed counter/flag changes
and nonfinite counts remain evidence. No age, health or gap thresholds are
invented. Counters wrapping or changing do not prove how many transitions
occurred between records. Ordinary logs cannot qualify publisher continuity.

Resource bounds are 128 MiB input, 200,000 selected records and 32 KiB per
output record. PyULog loads the selected arrays into memory; this is not a
constant-memory native collector. Receipt overflow rejects rather than
truncating with a success marker. Existing destinations are never overwritten.

The checker reconstructs every row and every summary from the hash-bound ULog
using a separate implementation. It shares only field/schema and JSON helpers,
and shares the external PyULog decoder, so this is not an independent binary
ULog parser. It rejects extra/missing/reordered/modified rows, mismatched
summaries, duplicate JSON keys, untagged nonfinite numbers and invented authority.
Successful verification grants no positive diagnostic or control admission.

See [P2A source audit and verification](../../../docs/C1E_P2A_PROVENANCE_COLLECTOR.md)
for real archive results, source binding and the next live collector gates.

## Native observation and clock deployment

P2B builds a separate output-disabled EKF observation profile. P3A reuses its
verified executable for a separate closed deployment capture. Neither installs
a map, establishes estimator health, links an admission adapter or permits
positive requests. See the [P2B contract](../../../docs/C1E_P2B_NATIVE_RECEIPT_CONTRACT.md),
[verification](../../../docs/C1E_P2B_NATIVE_VERIFICATION.md) and
[P3A scope/results](../../../docs/C1E_P3A_CLOCK_DEPLOYMENT.md).

In Ubuntu-24.04 WSL from the repository root:

```sh
python3 -B Tools/research_sitl/provenance/native/run_capture.py --ulog /path/to/pinned/headless/log.ulg
python3 -B Tools/research_sitl/provenance/native/check_capture.py /var/tmp/p2b-native-NEW
python3 -B Tools/research_sitl/provenance/clock/capture_deployment.py /var/tmp/p2b-native-VERIFIED
python3 -B Tools/research_sitl/provenance/clock/check_deployment.py /var/tmp/p3a-clock-NEW
```

The native runner verifies the exact preserved baseline ULog hash, original
overlay sources, allowlisted modules and dirty/untracked build inputs. It creates
an isolated build and never modifies original PX4 sources. `--resume-build` is
restricted to its own failed pre-runtime build with unchanged unrelated inputs;
earlier attempts are archived. Do not resume or overwrite completed captures.
The clock tool requires a passing native package and retains mapped executable
bytes. Preserve both evidence roots: its independent checker revalidates P2B.

## Closed receipt merge

P2C implements bounded per-source queues and recorded owner order in a separate
default-off host target. It checks Debug and ASan/UBSan builds, then replays the
verified P2B observations offline; it starts no PX4 or simulator process.

```sh
python3 -B Tools/research_sitl/provenance/merge/run_merge.py /var/tmp/p2b-native-VERIFIED
python3 -B Tools/research_sitl/provenance/merge/check_merge.py /var/tmp/p2c-merge-NEW
```

See [contract and verification](../../../docs/C1E_P2C_CLOSED_RECEIPT_MERGE.md).
The payload remains opaque observations. No live publisher, readiness, map,
adapter or positive authority is provided by the merge API. Retain the original
build-area paths: independent command/configuration checks bind those paths.

## Typed native observation merge

P2D connects real EKF constructor/destructor-entry facts and typed output
observations to the P2C bounded merge in a separate closed POSIX profile.
The original P2B recorder runs alongside it for independent row comparison.
A second native process must reject an actual EKF restart in the same owner.

```sh
python3 -B Tools/research_sitl/provenance/native_merge/run_native_merge.py --ulog /path/to/pinned/headless/log.ulg
python3 -B Tools/research_sitl/provenance/native_merge/check_native_merge.py /var/tmp/p2d-native-NEW
```

See [typed boundary and verification](../../../docs/C1E_P2D_NATIVE_TYPED_MERGE.md).
No simulator, baseline repetition, admission adapter or actuator output is
involved. Destructor-entry time is distinct from completed teardown; owner
times bracket queue consumption. No health, clock map or positive authority
is qualified. Preserve both processes and failed build attempts.

## Checked host clock and candidate lifecycle

P3B provides successful-read checks, raw nanosecond ordering, finite candidate
expiry and exact predecessor-bound replacement in a separate default-off Linux
host target. It does not change PX4's clock implementation or install a map.

```sh
python3 -B Tools/research_sitl/provenance/provider/run_provider.py
python3 -B Tools/research_sitl/provenance/provider/check_provider.py /var/tmp/p3b-provider-NEW
```

See [contract and verification](../../../docs/C1E_P3B_CHECKED_CLOCK_LIFECYCLE.md).
The real host trace and Debug/sanitizer contracts use finite experiment
intervals, not operational timing budgets. Provider/pause/namespace continuity
and native integration remain open; qualification is always false.

## Native checked receipt clocks

P3C extends the private P2D profile with checked PX4 clock reads at all seven
receipt-clock sites. Three bounded lanes retain raw values, purposes and
results. A separate process injects a labeled read error and must retain the
fault through real destructor observation while rejecting successful export.

```sh
python3 -B Tools/research_sitl/provenance/native_clock/run_native_clock.py --ulog /path/to/pinned/headless/log.ulg
python3 -B Tools/research_sitl/provenance/native_clock/check_native_clock.py /var/tmp/p3c-native-NEW
```

See [native receipt-clock verification](../../../docs/C1E_P3C_NATIVE_CHECKED_RECEIPT_CLOCK.md).
The scheduler and native sample clock remain unchanged. This verifies checked
receipt-clock propagation, not provider continuity or a qualified map. The
verification record binds a corrected post-capture checker separately; preserve
both that verifier bundle and the frozen native package.
