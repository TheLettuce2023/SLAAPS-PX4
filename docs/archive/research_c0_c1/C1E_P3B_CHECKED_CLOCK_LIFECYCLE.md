# P3B checked host clock observations and candidate lifecycle

2026-09-27, Codex (GPT-6). Independently accepted at closed host-observation
and candidate-lifecycle scope. This closes the checked-read and candidate-lifecycle
mechanics slice only. It does not qualify a provider, install a map or change
PX4's legacy `hrt_absolute_time` error handling. N1 remains negative-only.

## Checked read and exact ownership

`Tools/research_sitl/provenance/provider/` is a separate default-off Linux host
target. It rejects PX4/NuttX, lockstep and proxy builds and imports no adapter,
map or readiness API. One serialized owner operates each `CheckedProvider`;
this is not a concurrent publisher interface. Copy/move construction and
assignment are deleted, preventing silent cloning or reset of a lost owner.
Future integration must enforce its single-owner calling domain.

The production reader zero-initializes `timespec`, calls
`clock_gettime(CLOCK_MONOTONIC, ...)`, captures `errno` immediately, and checks
the syscall result, nonnegative seconds, nanoseconds in `[0, 10^9)`, multiplication
and addition bounds, and nonzero microseconds. It preserves raw seconds and
nanoseconds alongside the checked floor-to-microsecond conversion. A pure
conversion function makes finite failure arithmetic inspectable; it provides
no provider identity or trust. Runtime methods obtain their own clock reads;
only the explicitly named host test peer injects readings into the core.

Syscall failure, invalid timespec, conversion overflow and zero remain distinct
`ReadCode` values inside sticky terminal read-failure evidence. Comparing raw
seconds/nanoseconds catches a regression even if truncated microseconds are
equal. Equal raw readings are allowed. A failed read is never assigned the
previous successful timestamp.

## Interval and replacement contract

Bindings contain nonzero run, owner, controller, producer, provider, clock,
prospective-session and epoch names. These identify candidate objects in this
closed experiment; they are not authenticated publishers or established
admission sessions. An inclusive interval must satisfy `1 < first <= last <
UINT64_MAX`. This reserves representable +/-1-us candidate rounding endpoints;
it does not approve an uncertainty policy or prove continuity into the future.

Begin requires its actual read inside the interval. Before-first and
after-last initial reads have separate terminal causes. Active polling permits
both endpoints and expires only at `now > last`, without needing ingress.
Explicit retirement inside the interval records Retired; retirement after the
last endpoint records Expired. Epoch closure is retained as an ordinary event.

Replacement requires the exact predecessor binding, interval and closure
sequence from Expired or Retired. Run/owner/controller/producer/provider/clock
identities stay fixed; the prospective session changes and the epoch strictly
increases (gaps are permitted, reuse/wrap are not). The successor read must lie
inside its new interval and must not regress against the prior raw read.
Capacity for the old-closure/new-candidate relationship is reserved before the
active binding changes. The replacement event names the old epoch and exact
closure sequence; the full old closure and its cause remain in the archive.
Lost is terminal and cannot be replaced or reset.

Call-order errors are intentional faults: a repeated begin, stale replacement
key, or poll after Expired/Retired installs sticky Order/Lost. Callers inspect
state before polling a closed candidate; closed polling is not idempotent.
Precedence is existing sticky loss, API-order/binding/interval validation,
read error, raw regression, interval disposition, then evidence capacity.
The public method obtains a read before entering this validation sequence, but
an invalid call is not relabeled by that read's contents. There are 64 ordinary
events and one independent terminal slot; exhaustion closes without silently
discarding an accepted transition. This is observation mechanics, not a claim
that kernel clock reads or host scheduling have bounded latency.

## Evidence and practical limits

Package `/var/tmp/p3b-provider-5vpaar72` in Ubuntu-24.04 WSL retains exact
sources, commands, compiler version output, build configuration, binaries,
logs and `independent-check.json`. Manifest SHA-256:
`1d74a09e62298d500797dd848f3900ccbaef5ec39de54168342d9e2bff614d0e`.
Debug and ASan/UBSan contract checks pass; default-off and three profile
rejection probes pass. Source hashes matched before and after verification.

Finite checks cover syscall error/errno, invalid timespec, overflow/zero,
sub-microsecond regression, inclusive endpoints, expiry without ingress,
exact predecessor replacement, stale key, epoch reuse, sticky read loss,
and ordinary-capacity exhaustion with reserved terminal evidence.

The separate real observation uses successful **Linux host clock reads** and
a fresh run identity. It records 12 events: candidate 1 begins and expires,
candidate 2 explicitly replaces it and retires. Its 40-ms intervals and 5-ms
poll sleep are finite experiment parameters, not approved operational leases,
freshness limits or scheduling guarantees. The independent checker reconstructs
raw-to-microsecond conversion, raw ordering, identity, interval immutability,
inclusive expiry, exact predecessor relationship and final disposition. It
also checks exact source/artifact inventory, commands, configuration, compiler
marker and compile/link sanitizer flags.

`map_qualified=false` and `positive_admission=false` remain mandatory. P3A's
retained deployment evidence is not reused as proof about these new processes.
No PX4 process, simulator, network endpoint or baseline run is started here.

Source review corrected an implicit copy/move reset path before the first
capture. Preliminary package `/var/tmp/p3b-provider-_u4r36lf` and review
`/var/tmp/p3b-independent-review-cf87d_bp` remain preserved. Six trace/source
corruption classes were rejected, but review found missing source-directory
and actual-test-execution checks. The final package was freshly rebuilt after
binding CMake source/sanitizer settings, exact CTest invocation and successful
one-test execution, intended profile-rejection diagnostics and mixed profiles.

`resource-review.json` retains the final compiler self-frame inventory:
readMonotonic is 80 bytes static; provider method self-frames are at most 240
bytes, static or compiler-labeled dynamic/bounded. These exclude callees and
are not total peak-stack, scheduling or embedded resource acceptance.

Final independent review accepted the bounded scope, reran the complete
checker and rejected five focused evidence corruptions: absent tests, wrong
configured source, wrong sanitizer switch, unrelated rejection diagnostic and
substituted test executable. Evidence:
`/var/tmp/p3b-final-review-uv_rdov5/review.json`. Original package preserved;
temporary rebased controls are explicitly synthetic checker fixtures.

## Remaining P3 gate

The native PX4 read path still lacks successful-read/error propagation.
Provider semantics and actual loaded binding, boot/time namespace, pause and
whole-interval continuity, producer/controller/session lifetime coupling and
owner receipt integration remain open. Two deployment snapshots and many
successful readings cannot exclude a transient provider/namespace change or
qualify VM-pause behavior. A caller-chosen future interval is not proof of a
frozen map. The checked core may support a later private native provider, but
cannot be promoted to one by changing a qualification Boolean.
