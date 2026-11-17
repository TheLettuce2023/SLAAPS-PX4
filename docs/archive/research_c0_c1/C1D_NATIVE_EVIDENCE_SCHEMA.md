# C1D native closed-owner evidence

2026-09-21. Implementation contract; N2 runtime acceptance remains pending.

Schema `research-c1d-native-owner/v1` is a single JSON object with exactly:
`schema`, `header`, `records`, `closure`, `summary`, `lifecycle`, `footer`.
The independent implementation is `c1d_closed/check_closed_owner.py`.

- Header binds `c1d-closed-native/v1`, compiled `NativePosixSitl` origin,
  actual native hrt clock, full immutable NativeClosedOwnerConfig, 256-record
  capacity, actual owner size and the 2,000,000 us observation window.
- Records preserve every committed NativeSourceRecord field and all four
  DiagnosticResult report slots, including unused zero-initialized slots.
  Closure preserves the separate actual NativeClosureRecord. Summary is
  separately copied from the stopped owner, not reconstructed from the export.
- Lifecycle records actual bootstrap/claim counts, normal completion,
  retirement, actual Commander stop return, sticky bridge fault and source
  stack measurements. Export requires successful normal stop and acquired
  completion/retirement before any owner read or sink access.
- Footer reconciles normal-record count, total emitted adapter reports and
  last report sequence with both replay and the retained closure/summary.

All uint64/int64 fields are canonical decimal strings; smaller integer fields
are JSON integers, Booleans remain Booleans, enums use explicit names and
optionals use null. Finite float fields preserve float32 round-trip precision.
Duplicate keys, unknown/missing fields, wrong types, nonfinite values,
truncation and trailing JSON are rejected.

External six-field identity binding: `permitted_source`,
`controller_instance_id`, `owner_lifetime_id`, `native_run_id`,
`producer_lifetime_id`, `clock_epoch`; each a nonzero uint64 string.

The checker independently derives source sequence/generation/transition epoch,
physical inputs, barriers, decisions, report ordering, snapshots and closure.
Unarchived valid unchanged assignments may advance only the checked-time floor
between records. Normal Stop advances that floor but consumes no source record.
Actual runs must remain disarmed, research NotSelected, with no valid map,
estimator/transport readiness, operator, selected diagnostic state, successful
session, ACK, request history or eligible control demand. Closed polls truthfully
reject ClockMappingInvalid; normal OwnerStop returns AlreadyDisabled.

`run.json` uses `research-c1d-run/v1` and binds launch command, runtime/instance,
PID, six identities, host monotonic interval, normal exit and hashes of staged
startup, stdout, trace and repeated trace. Repeated exports must be identical.
The outer runner separately verifies full source, exact Commander overlay,
configuration, executable, compiler-stack artifacts and independent native
review. A JSON origin label alone never proves native execution.

Post-stop uORB inspection is only an advertised-topic snapshot, not proof that
a topic never existed. Missing-parent, device and FIFO sinks are rejected before
writing; these are not low-level write-error injections. Partial traces cannot
pass. Export never consumes or frees retained source-owned evidence.

The 17 Python methods cover synthetic checker guardrails and run-wrapper
bindings, including rehashed semantic/startup/topic corruption and forced exits.
Their fixture reports use the oracle model; they do not prove C++/Python
agreement or native execution. The separate N2 verification now records two
actual source-bound exports, 26 rejected actual-export corruptions and accepted
independent review; see C1D_NATIVE_INTEGRATION_VERIFICATION.md.
