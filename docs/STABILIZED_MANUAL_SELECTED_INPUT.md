# Stabilized manual: shared selected-state input

2026-10-20, Codex. The production output-disabled observer now uses the shared,
noncopyable `SelectedStateInput` sampler. The same adapter can supply the future
active controller without duplicating the observer's uORB input and health logic.
Calls belong to one serialized owner. It allocates no dynamic memory itself.

## Input contract

The sampler atomically copies payload/lifetime metadata for attitude, rates,
estimator status/flags, manual input, sensor selection and actuator permission.
It checks the complete revision tuple again after the sequential copies, then
reads the evaluation clock. The tuple detects observed publisher lifetime changes;
it is neither publisher authentication nor a simultaneous multi-topic transaction.

The snapshot contains the manual sample, control state sample, evaluation clock,
estimator timestamp, selected device IDs and revision tuple. It preserves the
single-estimator parameter checks, alignment/fault checks, selected sensor identity
matching, status/permission freshness and disarmed/no-kill/no-lockdown/no-termination/
no-ESC-calibration gates. Zero freshness limits are rejected. Sensor selection is
an event-driven identity; estimator and permission freshness are checked separately.
The observer still binds enabled source epoch/device IDs and rejects regressing
estimator time, while the core retains numerical, manual-input and state checks.

`prearmed_servo_condition` additionally requires the fresh prearmed flag. It is
only a metadata/permission prerequisite: it does not validate all numerical inputs,
authorize a mode, accept calibration, grant ownership or establish flight health.
Observation validity alone cannot satisfy it. The sampler may report recovered
input facts after a fault; existing owners still latch off and require deliberate
re-entry. No output publisher or enable command was added to the observer.

## Verification

The native fixture now directly exercises the sampler against actual uORB data:
observation versus prearm separation, armed/kill/lockdown/termination/calibration
faults, zero/future/stale permission timestamps, restoration without a grant, all
seven publisher lifetime changes, and unspecified freshness rejection. Existing
observer fault and hidden lifecycle tests also run through the shared adapter.

The final ELF passes 14,404 observer checks. All 74 session/output regression runs
pass 8,822 assertions and 2,263 acknowledgments. Portable Debug/Release/ASan-UBSan
checks remain 126,287 each; those host checks do not exercise the uORB sampler.
Native evidence: `/var/tmp/research-stabilized-output-native-selected-input-20261020`.
All 590 sealed files rehash. Manifest SHA-256:
`b3ca455a9e5b80e6e729b558c948ee3f8cbceb318917cf028eb0ea5b03252f4e`.
The runner now explicitly snapshots the production observer directory and exact
observer regression source alongside the output fixture.

ARM evidence: `build/stabilized_selected_input/20261020`. All 225 receipt files
rehash. Receipt SHA-256:
`8029f1dc88715addee6467041d765baf46fbec02c1ea8df7166d94bda079241e`.
The complete observer translation unit compiles with actual working-target flags.
Unlinked text grows from 4,140 to 4,254 bytes; data remains 20 bytes. The observer
instance remains 1,504 bytes; the sampler and returned snapshot are 120/176 bytes.
Compiler-reported static frames are 544 bytes for `Run` and a separate 544-byte
`capture` frame, versus the former 960-byte `Run`. These are individual static
frames, not runtime stack high-water or a complete call-chain bound.

## Next boundary

Wire this sampler into active-session startup/monitoring and independently
scheduled output permission checks, binding source/device continuity to the
explicit grant. Production mode/profile selection, reviewed active-output health
policy, physical driver attachment and runtime timing remain open.

The [last staged full image](STABILIZED_MANUAL_HANDSHAKE_FIRMWARE.md) predates this
extraction and retains its original 3,264-byte margin. This increment has ARM
translation-unit evidence only; a new full link is required for its exact image
resource result. The qualified dynamic console remains unchanged. No hardware was
operated; existing vehicle-data, bench, H10 and acceleration gates are preserved.
