# Native common-clock qualification candidate

2026-09-27, Codex (GPT-6). Source proof for the isolated non-lockstep POSIX
profile only. No map is installed, no uncertainty limit is approved and no
positive request is enabled. Deployment/lifetime binding and independent review
remain necessary before P3 closes.

Independent source/mathematical review passed 2026-09-27 for this conditional
argument only. It does not close deployment or map installation gates.

## Provider chain inspected

1. `platforms/posix/src/px4/common/drv_hrt.cpp::hrt_absolute_time` calls
   `px4_clock_gettime(CLOCK_MONOTONIC, &ts)` when lockstep is disabled.
2. `px4_clock_gettime` calls `system_clock_gettime` except for the explicitly
   compiled lockstep or `CONFIG_MUORB_APPS_SYNC_TIMESTAMP` branches.
3. Forced include `src/include/visibility.h` maps `system_clock_gettime` to
   libc `clock_gettime`; the native compile-command database records that include.
4. `src/drivers/drv_hrt.h::ts_to_abstime` computes
   `seconds * 1,000,000 + floor(nanoseconds / 1000)`.
5. A Python producer using `time.clock_gettime_ns(time.CLOCK_MONOTONIC)` and
integer division by 1000 uses the same returned timeline only when it shares
   the actual Linux boot/time namespace and unmodified libc provider.

The isolated profile's configuration audit excludes lockstep, DSP clock proxy
and diagnostic-profile mixing. Clock names alone do not establish the compiled
branch; source hashes, definitions, executable and provider identity must travel
with the map provider receipt. A different executable or namespace invalidates
this argument until requalified. `LD_PRELOAD`/library interposition, custom
clock providers, replay and time namespaces require explicit rejection or a
separate proof. A future runner must bind those facts, not assume them from
nearby clock readings.

Require successful clock reads and a valid timespec with nonnegative seconds
and nanoseconds in `[0, 10^9)`. Legacy `hrt_absolute_time` does not check the
underlying return value, so its source path alone does not establish this
condition. All finite endpoints must fit the checked microsecond arithmetic.
Removing environment interposition does not exclude `/etc/ld.so.preload` or
a different libc. Retain actual loaded provider identities and the operating
system trust premise; hashes bind bytes but do not prove provider semantics.

## Representation bound, not a delay estimate

For one common returned timeline with exact nanosecond value T, let
`p=floor(T/1000)`. At any two readings, discarded remainders each lie in
`[0,999]` ns; their difference has magnitude below one microsecond. A frozen
identity map with O=0 and conservative U=1 us can therefore cover representation
rounding over any chosen finite interval **under the provider/lifetime proof**.
This is not oscillator accuracy against physical time, scheduling precision,
network RTT, VM-pause behavior or a Pi-to-Pixhawk synchronization result.
Queue and scheduling delay remain measured age and are never absorbed into U.

For a finite map interval `[p0,p1]`, retain both endpoints, provider and producer
lifetimes, clock namespace/boot identity, controller/source/prospective-session
binding and a new nonzero map epoch. Checked conversion must cover every ACK,
creation and expiry endpoint. Suspend/provider change/lifetime uncertainty
invalidates qualification; no request can revalidate it. Replacing a map is
an observed barrier with new session/ACK preparation, not in-place adjustment.

This argument cannot connect host monotonic time to a lockstep simulator clock
through pauses or speed changes. It cannot qualify a remote processor's clock.
The preserved stock baseline's estimator timestamps therefore are not retroactive
map evidence for this non-lockstep profile.

## Later checked-read mechanics

[P3B](C1E_P3B_CHECKED_CLOCK_LIFECYCLE.md) independently verifies a separate
Linux host reader and candidate lifecycle: successful-read/representation
checks, raw nanosecond ordering, finite expiry and predecessor-bound replacement.
It does not modify this PX4 provider path or prove deployed provider semantics,
whole-interval continuity or pause behavior. Its candidate interval arithmetic
cannot qualify or install a map. The remaining requirements below still apply.

[P3C](C1E_P3C_NATIVE_CHECKED_RECEIPT_CLOCK.md) subsequently integrates checked
`px4_clock_gettime` reads into the closed native receipt bridge, with exact raw
read/timestamp correspondence and explicit synthetic-error propagation. Its
three concurrent-domain archives and lifecycle cleanup pass independent review.
It leaves scheduler/sample `hrt_absolute_time` unchanged and does not establish
provider continuity, pause semantics or a qualified map. The corrected checker
is separately source-bound to the frozen native package.

## What remains before use

Implement and verify a provider-owned immutable receipt with executable/source
binding, shared boot/time namespace, explicit provider/interposition checks,
finite interval, monotonic bracket measurements and replacement/loss evidence.
The owner must observe those facts independently of guidance packets. Approve
native diagnostic timing/uncertainty limits separately from the frozen baseline
revision 2; measured proximity alone is insufficient. P2B observation records
must continue to report `clock_map_qualified=false` meanwhile.
