# P3A closed clock deployment evidence

2026-09-27, Codex (GPT-6). Closed deployment observation verified. This is
deployment observation for the [common-clock candidate](C1E_NATIVE_CLOCK_QUALIFICATION.md),
not a qualified provider receipt, installed map or positive runtime release.

## Concrete scope

Reuse the independently checked P2B executable and exact recorded-IMU startup
in a new owned output-disabled process. Preserve P2B's evidence; do not rebuild
or replace its executable, alter N1, repeat the stock baseline or launch a
simulator. The process still contains no admission adapter or control outputs.

Record the Python observer and native child before and after the observation
window: executable path, boot identity, time/mount namespaces, raw process maps,
and every executable file mapping's device/inode, size and SHA-256. Check each
mapping against the actual process-root file, rejecting deleted/replaced paths.
Retain the corresponding executable/library bytes as content-addressed objects
so later checks do not depend on a future system library upgrade. Retain exact
collector/checker scripts, stimulus, startup, stdout and native observations.

Reject observer `LD_*` environment and a present `/etc/ld.so.preload`; remove
loader and PX4/simulator/replay overrides from the child's environment. Bind
the child to the previously verified executable and compare parent/child boot
and time namespace. Provider files and namespace identities must match at both
observations. Successful Python monotonic reads bracket the process lifetime;
native sample/hook timestamps must lie inside that bracket. An independent
checker reconstructs map-file inventory, source/artifact bindings, chronology
and native observation summaries. It must retain all qualification fields false.

## Limits that cannot be erased by a passing checker

Two process snapshots are observations, not continuous monitoring. They do not
prove no transient namespace/provider change, successful return from every
native clock call, dynamic symbol binding, absence of all interception, or libc
semantics. Hashing a provider identifies bytes; it does not authenticate an
operating system installation or prove behavior. The cooperative trusted-local
threat model still applies. Anonymous executable mappings and vDSO require an
explicit later operating-system/provider argument; ordinary file retention does
not qualify them. This increment does not calculate a synchronization error
from timestamp proximity or infer a health/age budget.

P3 still requires the actual trusted provider API, successful-read handling,
finite prospective interval, representable arithmetic, source/controller/session
lifetime binding, provider loss/expiry/replacement behavior and owner merge.
O=0/U=1 us remains a conditional representation argument only. A retrospective
process bracket is never installed as a prospective map. CAD is unrelated to
this closed software evidence and remains required for physical acceptance.

## Native evidence and review

The real package is `/var/tmp/p3a-clock-pds1ofii` in Ubuntu-24.04 WSL, with
retained result `independent-check.json`. Manifest SHA-256:
`b3d6101fc0539e21389b8c06604591050e12289efe24a7fd46e043394fd3d2d1`.
It references the verified P2B package `/var/tmp/p2b-native-30xfeeqc` and keeps
16 unique executable/library objects, including the native executable, loader,
libc and the Python observer's loaded executable files. Parent/child boot/time
namespace identities matched; each process's before/after executable-file
inventory matched. The native process closed normally without runtime errors.

All 1200 inputs produced source observations; all attitude-valid flags were
false, collector fault zero. Maximum input-to-hook delay was 1182 us and maximum
hook gap 5546 us. These are a second closed-process measurement, not a baseline
rerun, rate guarantee, provider uncertainty or positive estimator evidence.

Independent review found and resolved descriptor/path identity races, duplicate
map entries that could overwrite prior evidence, and missing parent executable
binding. Hash/copy now use one opened descriptor with before/after identity
checks; the independent checker enforces all three conditions. Qualification,
map installation and positive admission remain explicitly false.

The independent reviewer reran the full checker and rejected five targeted
corruptions: inconsistent duplicate mapping, absent parent executable binding,
namespace mismatch, missing retained object and invented qualification. Evidence
is `/var/tmp/p3a-independent-review-1zerkhz4/review.json`; the original package
was preserved and rechecked successfully. The reviewer accepted this bounded
deployment-observation scope; full provider qualification remains open.
