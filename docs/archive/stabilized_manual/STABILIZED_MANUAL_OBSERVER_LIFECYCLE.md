# Stabilized observer: publisher lifecycle binding

Follow-on: [full ARM image and memory review](STABILIZED_MANUAL_LIFECYCLE_FIRMWARE.md)
now verifies a clean link on the working baseline. The native and unlinked-object
evidence below retains its original scope.

2026-10-19, Codex. This advances the selected-state integration needed before
production stabilized output. The observer remains output-disabled.

## Behavior

The observer previously assigned every state sample the same source epoch.
Freshness checks and device IDs alone cannot distinguish a publisher that
withdraws and re-advertises between polls. Current source binds these seven
queue-one inputs: attitude, angular velocity, estimator status, estimator flags,
manual input, sensor selection and actuator permission.

In observer-enabled builds, uORB advances an unsigned revision on advertisement
and withdrawal. Publication records its current revision alongside the payload.
The observer copies payload and revision under the same critical section used
by publication: the node mutex on POSIX, interrupt critical section on NuttX.
Withdrawal and a new advertisement without new data reject cached payloads.
Revision exhaustion stays invalid rather than reusing an admitted token.

A second revision pass rejects lifecycle changes during the sequential input
snapshot. The complete tuple maps to a session-local epoch without hashing.
Any observed change during an admitted session latches the observer off before
duplicate-clock/control-cycle suppression. New valid data cannot clear the latch.
Stop/start and an explicit centered observation request are required for recovery.
Diagnostics expose the epoch and all seven revisions.

The uORB additions are conditional on `CONFIG_MODULES_RESEARCH_STABILIZED_MANUAL`;
ordinary builds retain their existing node fields and behavior. Direct observer
subscriptions explicitly reject protected NuttX builds; the checked target uses
flat NuttX. No new parameter or output-enable command is introduced.

## Evidence

Two fresh native processes exercise the actual observer, uORB and work queue,
passing **14,198 and 14,174 assertions**. Both also test the pre-existing manual,
reset, health, device-change, permission, timing and recovery paths.

Each run restarts all seven publishers individually on the observer's own queue,
so withdrawal and republication complete entirely between its snapshots. Payloads,
timestamps and device IDs are preserved during that operation. Each case advances
the affected revision, retains fresh valid inputs, revokes the old session and
rejects implicit recovery. No actuator publication occurs. Separate contract
checks cover withdrawal, empty re-advertisement, continuous publication and tuple
changes. These are real uORB advertisement operations with synthetic input
publishers, not actual EKF2 module restarts or a physical plant simulation.

Debug, Release and ASan/UBSan each pass **126,064 existing host checks**.
The isolated working-baseline native ELF is
`cc8962dc4ae6615779029a1f2b255744837b4e5744234ea8a1525a840219b7fa`.
Evidence: `/var/tmp/research-stabilized-observer-lifecycle-20261019`.
Manifest SHA-256:
`a932b9514868470406a4f179f4bb3523a2728fdcbd6e05727812d6c56dc32774`.
All 139 listed files independently rehash.

The complete changed uORB and observer translation units compile under ARM GCC
13.2.1 with working baseline `e8ea98b404` / NuttX 11 flags. Unlinked text sizes
are respectively 1,718 and 4,144 bytes; these are not whole-image sizes.
The first compile exposed duplicate physical header paths in the verification
overlay; a consistent copied uORB tree resolved it. The failed log is retained.
No production acceptance criterion was relaxed.

ARM recipe, commands, dependency snapshots and receipt are in
`build/stabilized_lifecycle/20261019`.
`arm-receipt.json` SHA-256:
`15c8eca74ea2472dd2ade36dabb874349477aea87323ab77b185f4208aa6be54`.
All 256 listed files rehash, and the five changed staged sources match the native
working-baseline port exactly. The combined `verification.json` also confirms
the three prior observer firmware artifacts are unchanged. No new image was
linked, flashed or operated.

Reproduce in WSL using a fresh directory:

```sh
python3 Tools/research_sitl/verify_stabilized_observer.py prepare --evidence /var/tmp/research-stabilized-observer-NEW
python3 Tools/research_sitl/verify_stabilized_observer.py host --evidence /var/tmp/research-stabilized-observer-NEW
python3 Tools/research_sitl/verify_stabilized_observer.py native --evidence /var/tmp/research-stabilized-observer-NEW
python3 Tools/research_sitl/verify_stabilized_observer.py test --evidence /var/tmp/research-stabilized-observer-NEW
python3 Tools/research_sitl/verify_stabilized_observer.py seal --evidence /var/tmp/research-stabilized-observer-NEW
```

## Limits and next work

Advertisement revisions are lifecycle metadata, not authentication. uORB shares
handles; a writer using an existing handle is not identified by this mechanism.
The sequential snapshot is not a simultaneous multi-topic transaction, and a
restart after the final revision check is observed on a later dispatch. Sensor
health and permission policy still require production review.

The follow-on image closes the full-link/resource step. Next connect the
production output supervisor and sink with exclusive ownership and bounded
fault handling. Physical pulse/expiry timing, servo calibration, signed canard
authority and vehicle inertia remain separate measurement gates. Prior synthetic
SITL qualification, bench acceptance and acceleration gates retain their scope.
