# Stabilized manual: source-bound output ownership

2026-10-20, Codex. The existing output adapter now binds each explicit grant to a
command publisher lifetime. Its persistent driver protocol continues to enforce
session monotonicity and confirmed disarm before another owner can be granted.
This implements source-bound ownership checks; the production supervisor's
request transport, health policy and physical driver attachment remain open.

## Grant, revocation and handoff

`begin` now requires the expected, nonzero source revision as an out-of-band
argument. That revision must still match the current publisher lifetime.
An active, faulted, revoking, pending-write or uncollected-result state cannot
accept a replacement grant. A frame cannot create or change the source binding.

Each frame carries its lifetime metadata separately from its payload, obtained
in the same uORB critical-section copy. The adapter rejects a frame whose copied
revision differs from the grant, and rechecks the current source during every
preparation, final-bank validation, driver completion and result collection.

Withdrawal, restart or a competing advertisement invalidates the binding and
requests revoke. An invalidated command cannot acknowledge either the command
or revocation, including when a successful driver result is already waiting to
be collected. A fresh, validated, handled disarm must complete before another
grant. The source-fault diagnostic stays latched until a successful new grant.

Replacing a producer requires its own explicit advertisement and a grant naming
that new revision. A request naming the old publisher remains rejected even
after successful disarm. The adapter retains the portable driver's session
high-water mark across producer replacement; it must remain alive with the
output owner. This is not persistence across a boot or adapter destruction.

## Shared lifecycle subscription

The existing bound-copy wrapper moved from the observer module into
`uORB::ResearchSubscription`. `PublisherBinding.hpp` retains its former name
as an alias, so the observer uses the same implementation. The helper imports
PX4 configuration before enforcing its observer-enabled/flat-NuttX requirements.
It does not change the uORB revision or payload-copy algorithm.

The native fixture now advertises a command source before requesting its grant,
uses atomic payload/revision copies, and withdraws the publisher when stopping.
The replacement source is advertised separately and cannot start control until
the output consumer has confirmed disarm and accepted its new binding.

These revisions detect advertisement lifetime changes, not authenticated
publisher identity. They do not identify writes made through a retained handle
within the same lifetime, or establish a simultaneous transaction across topics.
The production supervisor must control who can request grants and bind the
acknowledgment path as well as authoritative permission/selected-state health.

## Verification

All **62 accepted fresh-process native runs** pass **4,794 assertions**, with
**1,307 handled command acknowledgments** through the actual mixer and an
eight-channel memory driver. Thirty-one scenarios run twice.

The seven new cases cover publisher restart with retained payload, withdrawal,
source changes between preparation and writing, during driver handling and
between completion and collection, a competing grant during active control,
and deliberately mismatched copied-frame metadata. Source-fault checks
distinguish lifecycle rejection from ordinary duplicate/sequence rejection.

Every case additionally proves that a grant with zero source revision fails,
a stale requested revision cannot bind the replacement publisher after disarm,
rejected disarm blocks handoff, and the correctly bound successor runs rate/hold
control. Existing expiry, mapping, permission, routing and output-fault cases
remain in the accepted set.

The observer on the **same final ELF** passes **14,150 checks**, including seven
hidden input-publisher restarts. Debug, Release and ASan/UBSan each pass
**126,123 portable host checks**; these host suites do not execute the mixer or
uORB integration. The initial 60-case pass and its observer run are retained
separately from the final accepted set.

Native evidence: `/var/tmp/research-stabilized-output-native-source-20261020`.
Manifest SHA-256:
`f42916cca9356c5cecf4dd0adb0b9006f8644fa89a575a36300c9829c36f9d4f`.
Final ELF SHA-256:
`6bd2fbaa31baf503c1686ffe578246d72783e44f50a9dcb6e50b3fbc24291858`.
All **809 manifest-listed files** independently rehash. Reproduction uses the
runtime runner's prepare/host/native/test/seal actions in a fresh directory.

ARM compilation against the retained working `e8ea98b404`/NuttX 11 environment
passes for the adapter and a standalone shared-subscription probe. An initial
probe exposed an include-order dependency in the helper; importing configuration
inside the header corrected it. The failed stage/log remains preserved.

The adapter now has **1,681 unlinked text bytes**, 384 above the previous version.
Its compiled instance remains **848 bytes**. The subscription probe contains
164 text bytes; this is a probe size, not a complete transport module. Enabled
mixer text remains 8,661 bytes. Baseline and feature-disabled mixer text remain
8,233 bytes, with exactly matching machine code and relocations.

ARM evidence: `build/stabilized_output_source/20261020-final`, including the
consistent working-baseline uORB header overlay, exact commands, layout probe
and dependency snapshots. Receipt SHA-256:
`d6b723784117b8fa3756017acec30b0c27f1c9e00de0ce4c255a9cc9ceecd7a2`.
All **316 receipt-listed files** independently rehash. Combined verification:
`56eaa302ffe86fb7dc62da2f6dd2564dfbec403e6d16189c510a544187967b59`.

## Remaining integration

Connect deliberate operator/mode requests, controller lifecycle, bound command
and acknowledgment transport, authoritative health and physical output
execution into the production supervisor. Preserve independent output expiry
scheduling and the existing disarm barrier. Then link and budget the complete
candidate; object sizes do not prove it fits the remaining firmware flash.

No new firmware was linked or flashed. The last staged lifecycle-observer image
still has 3,584 flash bytes free and has no active stabilized adapter. Synthetic
fixed-state integration evidence does not extend the 31-case rotational SITL
qualification. Vehicle measurements, electrical/servo timing and existing
stock/bench/acceleration acceptance gates remain unchanged.

Follow-on: [controller session and acknowledgment binding](STABILIZED_MANUAL_CONTROLLER_SESSION.md) adds producer-side lifetime checks and health monitoring while waiting.
