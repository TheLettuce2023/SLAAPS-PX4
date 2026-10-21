# Fresh PX4 session-consumer link

2026-10-20, Codex. The actual private session consumer is now linked into a fresh
PX4 executable, and its command-registration smoke test passes. The consumer
queue was not started; full runtime and closed-loop behavior remain unverified.

## Build integration

`build_stabilized_session_checkout.py` stages the current worktree and initialized
submodules into a new private checkout. It enables the output-disabled observer's
uORB lifecycle metadata, registers the private command/ack messages and adds the
session consumer external module. The new session-aware plant plugin is built
from the same snapshot. No stock board configuration in the root is changed.

The full build exposed a conflict hidden by separate prior configurations: the
legacy dynamic fixture instrumentation and current observer implementation both
define uORB lifecycle fields. This new executable uses the current production
observer implementation only. Its private checkout restores the current root
DeviceNode files, omits the legacy fixture owner target and removes the legacy
instrumentation compiler define. There is one lifecycle implementation. The
legacy qualified console and its binary/plugin pair remain untouched.

Failed source revisions and build/configuration logs are preserved. The new plugin
has a separate build directory, avoiding reuse of the old plugin's CMake cache.
Future build continuation explicitly rejects sealed evidence directories; that
rejection was exercised and the sealed files rehashed unchanged. The archived
builder is the exact version used to build; the current root adds this subsequent
immutability guard and positive-job-count validation.

## Evidence

Directory: `/var/tmp/research-stabilized-sitl-oxyhow9f`.
58,130 staged inputs and 658 preparation graph anchors verified before/after build.
PX4 ELF SHA-256:
`6653b9edd363af5d418cf247cbdbfd74a2e14fbc1f030baf1115b8592e151e87`.
Final manifest: `session-link-manifest.json`, SHA-256
`4efae72afeaeef557dc447d13308988b61c2056632071c495cb62a1331e019ab`.
All 257 manifest files independently rehash. The retained source origin binds the
full staged snapshot; verified session/uORB/library source snapshots, compile
commands, board configuration, binaries, plugin, generated physical plugin source
and logs are included in the evidence.

Linked symbol checks find the actual consumer Run implementation and driver
completion. The legacy dynamic fixture and research bench owner are absent.
The executable starts with a private script, invokes the consumer usage command
and shuts down successfully. That verifies registration/loading only: it creates
no consumer, grant, output transaction or Gazebo connection.

The freshly built plugin-side tests also pass: 410 codec, 887 plant endpoint and
170 remote completion checks. This does not exercise the new PX4 queue. Earlier
real Gazebo plant evidence remains valid for its stated open-loop scope.

## Next required work

Add the separately scheduled producer using `StabilizedManualSession`, actual
selected inputs and coherent grant confirmation. Start the real estimator/manual
input pipeline and explicitly verify disarmed/prearmed permission. Connect both
queues to the unique plant namespace, check simulation clock alignment and run
startup, body-rate/hold, health/source loss, lost/delayed reply, revoke/stop and
successor-grant tests. Only then run the full maneuver and console campaign.

The current default console is unchanged. No output queue or physical hardware was
operated, and no firmware was flashed. Stock, bench, H10 and acceleration gates
remain preserved. Vehicle measurements and active-path ARM resources remain open.

## Producer link and startup follow-up

The separately scheduled producer and consumer link together in
`/var/tmp/research-stabilized-sitl-5b48_f56`: 58,131 staged inputs, 662 anchors.
Both command-registration smoke checks and all plugin-side tests pass.
The 252-file manifest SHA-256 is
`8b0c2e71b550f3ea318225a6d0c8a95c249ea5c013ad2e116079dbdb4ced4759`;
the PX4 ELF SHA-256 is
`258f92709f05cee7ee1c8ba79597ab977a3db642385011eb3a1a3fc1448218ae`.
Neither scheduled queue was started by this registration check.

Subsequent startup review found that observer profile parsing deliberately clears
thrust. Both private CLI entry points now set the declared synthetic 10 N thrust
after parsing; the production observer parser remains output-disabled. Both
endpoints now require the isolated estimator/manual-input configuration,
COM_PREARM_MODE=2 and all Gazebo output functions unassigned. These checks apply
at startup and to grant/active permission checks, so configuration drift revokes
authority. Actual selected-state checks still require disarmed/prearmed state.
The producer translation unit (including the consumer class) compiles with real
PX4 headers; 582 preserved files in
`/var/tmp/research-stabilized-session-consumer-p6ckggw5`, manifest SHA-256
`683bf1b932a99cc29af3088d03903c7a536e9bcf1c362697e9908afe496aa359`.
A fresh full link of these startup corrections is pending.

Runtime startup must explicitly set COM_PREARM_MODE=2, provide a profile using
current SyntheticConfiguration gains and manual instance zero, and retain all
isolation settings. The first actual two-queue closed-loop run, fault/restart
checks and full campaign remain pending.

## First integrated runtime and successor regression

The startup-corrected build `/var/tmp/research-stabilized-sitl-7lhhhdmr` passes
link/registration/plugin checks (252 files, manifest
`0d91ed935b1b0352d0c0c4b2cb894d7c22761835736f3f208b00bd172650b6dd`).
Its ELF is `b7f67e2a42c873d0c22992c3837c52ceb8d70695b9313ca15b67c82952e406ad`.
The first actual nose-up two-queue run, real EKF and MAVLink manual input, passes
initial hold, small roll/release and manual-loss fault/disarm:
`/var/tmp/research-stabilized-session-runtime-szleffio`, 518 handled acks.
Plant logs show zero thrust and targets after loss (servo lag decays physically).
This is a smoke test, not maneuver qualification.

The extended run reproduced successor-grant failure after manual loss:
`/var/tmp/research-stabilized-session-runtime-tbrz1ipd`, manifest
`a6b7288abef3703ea0d687c7dc463e6b8f294afbe9780847b3715b09bdd83be6`.
The consumer could encounter an unread previous-session command immediately
after granting a new session. It now drains the cached command before publishing
acceptance, checking revision, permission and deadline again. Compile passes
(582 files; `/var/tmp/research-stabilized-session-consumer-2au0na0k`, manifest
`77f3eecfacafe8211b5c79cec79ddab8d8158825e1816dbabcd585d9acbbb88e`).
The corrected full link and extended runtime rerun now pass; see below.
`verify_stabilized_session_runtime.py` preserves the exploration as a repeatable
smoke test with restart, permission loss and plant-side checks.

## Corrected two-session runtime smoke: passed

Fresh build: `/var/tmp/research-stabilized-sitl-2mxx3z6l`, 58,131 staged inputs
and 663 anchors; all 252 sealed files rehash. Manifest SHA-256:
`a4de7a06312a5434ddc7917371f287d4b551f5aa64326e4130e26ad0b6997270`.
ELF SHA-256:
`598ff4cca9d1ac9851f8dba92c2b47735b51532d3b83b92da5a085e9f9667520`.
Both registrations and 410 codec / 887 endpoint / 170 remote checks pass.

Runtime: `/var/tmp/research-stabilized-session-runtime-jf973_5_`.
Manifest SHA-256:
`9e6fd947f755038413863d9c8db8364811a2011139824ca6d9c696468c88892f`.
Reproduce with:

```sh
python3 Tools/research_sitl/verify_stabilized_session_runtime.py \
  --evidence /var/tmp/research-stabilized-sitl-2mxx3z6l
```

With actual EKF, MAVLink input and separate producer/consumer queues, initial
centered hold, a small roll command and release, manual-input-loss fault/disarm,
explicit successor restart, and COM_PREARM_MODE permission-loss fault/disarm pass.
The consumer records 611 handled acknowledgments across two sessions. The plant
records exactly 10 N thrust while enabled and zero thrust/targets after the final
permission loss. Peak roll rate is 0.15453258 rad/s: this is only a response sanity
check, not a tracking-quality criterion. Physical servo states decay with the
synthetic lag after targets go to zero. Failed successor evidence remains intact.

This smoke does not qualify the full maneuver envelope, queue stalls, publisher
replacement, missing/delayed replies, estimator resets or active-path ARM resources.
These fault campaigns and the full dynamic/console requalification are next.
The legacy qualified console remains the default. No hardware operation or flash.

## Integrated fault smoke extension

Five fresh runs use the same sealed producer/consumer/plant binaries. Each checks
initial hold and roll/release, injected fault, rejection of ordinary enable,
sustained zero plant thrust/targets until explicit restart, a distinct successor
session, and final permission-loss disarm. All five pass, with 2945
handled acknowledgments across ten sessions. All 135 retained runtime files
rehash. Summary: `/var/tmp/research-stabilized-session-faults-5izcilfr`, `summary.json` SHA-256 `d5704376741af19b875db93a7cf99f8f4fa1c0bbc1bf78dda04ebf7638e5f85a`.

| Case | Runtime directory suffix | Receipt SHA-256 |
| --- | --- | --- |
| process_stall | `b11jzgnk` | `608f74556e7bf49ebb16fc644454fa4fa9330a4be9c5aa561db8a93e7b5f5319` |
| estimator_loss | `vfiztvwh` | `53f7c09ae21c945c2bd6835e7cd91ef2e8eba1e5baa718dcd9b7643e43ccec95` |
| wrong_source | `fwcs3hyi` | `cdba999e2efa14c604223060b9ea6ab43a8b5b3c5381c250db81d3b0d4f7d828` |
| routing | `45pdz4db` | `85834b8666130d72dfaaf976a83f6ec1534606474d90e7ceb3f2a4781cb985c1` |
| manual_loss | `1xp7u26j` | `4c92f92091f39378aa51839cd0755c0d39fa63700558ed903ca716bfd869cc59` |

Directory prefix is `/var/tmp/research-stabilized-session-runtime-`.
The process-stall case stops the entire PX4 process while Gazebo keeps stepping;
the independent plant expires the last command after exactly 100,000 us, within
one 4,000 us simulation step of its lease. This is not a single-work-queue stall
test. The wrong-source case withdraws system 255 input and sends system 254
traffic; it proves that replacement traffic does not sustain authority, not
authentication or which upstream layer rejected it. Estimator loss stops/restarts
EKF2. Routing changes SIM_GZ_SV_FUNC1 from zero to 201 and restores it before
explicit restart. All restored conditions remain fault-latched until restart.

Run a case with `verify_stabilized_session_runtime.py --evidence` pointing to the
sealed build above and `--fault manual_loss|wrong_source|estimator_loss|routing|process_stall`
(select one value). Post-run plant-check failures now retain failed result evidence
and return nonzero. Independent producer/consumer stalls, delayed/lost replies,
publisher replacement without data loss, and full maneuver/console campaigns
remain pending. No stock baseline, default console or hardware gate is changed.

## Independent queue stalls: passed

The private queue names are now `wq:slaaps_prod` and `wq:slaaps_cons`, within
Linux's 15-character thread-name limit. The prior longer names could not be
identified through `/proc`; the failed injection attempt (before any stall) is
retained at `/var/tmp/research-stabilized-session-runtime-ahm74f7k`.

Fresh build `/var/tmp/research-stabilized-sitl-dz3j_4_i` verifies 58,131 inputs and
663 preparation anchors. All 252 sealed files rehash; manifest SHA-256:
`8c5b0a443b293c48c0ebc572d153224f64be3f268f3987861bf2e910f38a5043`.
ELF SHA-256: `5af2d9c6bf48c2b74b185ec000dcabb98a220c203a6e0dd7612db660e0f02129`.
Link/registration and all 1,467 plugin-side checks pass.

The runtime verifier now supports `--fault producer_stall` and
`--fault consumer_stall`. It requires a unique named thread, uses Linux
PTRACE_SEIZE/INTERRUPT, confirms its ptrace stop with waitpid, advances simulation
500 ms, records the other endpoint's reaction while the selected thread remains
stopped, and detaches before ordinary fault/restart checks. No whole-process
SIGSTOP is used in these two cases. This samples one arbitrary suspension point
per run; it is not an exhaustive lock-contention or scheduler timing proof.

Both initial explorations and final assertion-bearing reruns pass. Final results:

- Producer stall: consumer reaches Fault while producer remains stopped.
  `/var/tmp/research-stabilized-session-runtime-i5nmy4rs`, receipt
  `e79eb6330596e98e02755a1ee5d3599bd916f1d91a9e0c9552f6f7c49b76f37b`.
- Consumer stall: producer reaches Fault while consumer remains stopped; plant
  lease expires exactly 100,000 us after its last command.
  `/var/tmp/research-stabilized-session-runtime-pj2zwu4j`, receipt
  `1aee8c5e1615d73da33be40d4a8d271d6f0632f0d157869c3c78a63e8052ca06`.

All 54 final runtime files independently rehash. The two final runs record 1,227
handled acknowledgments across four sessions, sustained disarm, rejected ordinary
enable, successful explicit restart and final permission-loss disarm. Initial
explorations are retained at runtime suffixes `xapbph74` and `zwoeqdla`.

Next: transport delay/loss and hidden publisher-lifetime changes, followed by
full maneuver and console qualification on the integrated owner. The current
default console, stock/bench/acceleration gates and hardware are unchanged.

## Integrated single-axis maneuver campaign: passed

The same sealed named-queue build now passes twelve fresh tracking, release-hold
and physical impulse-recovery cases: roll/pitch/yaw, both signs, level and nose-up.
`verify_stabilized_session_maneuvers.py` adapts only fixture startup to the session
owner and uses `verify_stabilized_campaigns.campaign` unchanged, including every
existing acceptance criterion. Gains and acceptance limits were not changed.
The preliminary positive nose-up roll run is retained separately at
`/var/tmp/research-stabilized-session-maneuvers-0hk72ecl`.

Full twelve-case evidence:
`/var/tmp/research-stabilized-session-maneuvers-b3jhz5ye`.
All 293 manifest files independently rehash; maneuver manifest SHA-256:
`6491dbf1a33d1eaf1a35e85e3e7a917e657441574513ad180c3344e077746aa7`.
A separate `independent-cycle-audit.json` records application of the existing
cycle-level audit to all 14,021 control cycles. The original maneuver manifest
and its covered files remain unchanged.

| Metric | Worst measured | Existing limit |
| --- | ---: | ---: |
| Rate tracking error | 0.02188313 rad/s | 0.08 rad/s |
| Final physical hold error | 0.00623222 rad | 0.02 rad |
| Peak physical hold error | 0.16555521 rad | 0.5 rad |

All cases also pass quaternion reference capture/retention, final estimated hold
error and rate, one actual physical impulse, magnetic rigidity, fixed 10 N thrust,
and plant disarm. Reproduce with:

```sh
python3 Tools/research_sitl/verify_stabilized_session_maneuvers.py \
  --evidence /var/tmp/research-stabilized-sitl-dz3j_4_i
```

This qualifies the stated twelve single-axis synthetic cases, not the full
integrated envelope. Coupled full-stick reversal/saturation/antiwindup and the
remaining transport/lifecycle faults are next, then console requalification.
The existing coupled runner requires the old owner's events CSV for an
informational dispatch count; the new producer does not emit that file. Adapt
that diagnostic explicitly without inventing events or weakening performance
checks. Existing default console and all vehicle/hardware gates remain unchanged.

## Integrated coupled maneuver campaign: passed

All four coupled full-stick/reversal/impulse cases pass on the same integrated
owner build: level and nose-up, both initial signs, seed 42. All existing
performance/feedback acceptance criteria remain unchanged, including saturation
and directional anti-windup. No gains changed. The legacy informational dispatch
count is explicitly `null` for the integrated owner; no events are fabricated.
The original runner still requires its events CSV by default.

Evidence: `/var/tmp/research-stabilized-session-maneuvers-tayvlgzy`.
All 105 manifest files independently rehash; maneuver manifest SHA-256:
`dae3def22359de8571bafd2a101bd922b96161ffb9c1641992a93b8a1cfd125b`.
The existing cycle audit covers 10,300 control cycles. All cases exercise
actuator magnitude saturation and blocked integration on each axis; there are
zero directional anti-windup violations.

| Metric | Worst measured | Existing limit |
| --- | ---: | ---: |
| Coupled rate error | 0.02870530 rad/s | 0.08 rad/s |
| Final physical hold error | 0.00678342 rad | 0.02 rad |
| Peak physical hold error | 0.48976020 rad | 0.5 rad |

The peak margin is small; these results establish the declared seed/fixture
cases, not a general robustness margin or vehicle tuning. Together with the
twelve single-axis cases, 16 integrated maneuver cases now pass over 24,321
audited cycles. Reproduce the coupled cases with:

```sh
python3 Tools/research_sitl/verify_stabilized_session_maneuvers.py \
  --evidence /var/tmp/research-stabilized-sitl-dz3j_4_i --coupled
```

Remaining integrated work includes transport-delay/loss and hidden publisher
lifetime faults, consolidated qualification evidence and console qualification.
Default console promotion and vehicle/hardware acceptance remain separate.

## External transport faults: passed

A private Python Gazebo relay now separates consumer and plant namespaces within
the unique test partition. It forwards requests, commands, status and replies;
it can drop a command, drop a handled acknowledgment, or release an acknowledgment
at least 80 ms after its acceptance timestamp using simulation time. Its JSONL
log preserves actual received message payloads and injection/release events.
Both tested binaries remain the sealed `dz3j_4_i` pair; the controller's 50 ms
completion deadline and 100 ms plant lease are unchanged. The relay does not
grant authority or fabricate acknowledgments.

Three fresh cases pass: fault latch, sustained zero plant targets/thrust, rejected
ordinary enable, explicit restart into a second session, and final permission-loss
disarm. Each log proves one intended message was dropped/delayed. In the delayed
case the relay republishes the original acknowledgment at least 80 ms late;
control remains fault-latched. This is logged relay delivery plus plant-state
evidence, not instrumentation of the consumer callback for that late message.

| Case | Runtime directory suffix | Receipt SHA-256 |
| --- | --- | --- |
| drop_ack | `u8rhhqzv` | `7834f97f3ec61c2eb5d0cb300bb4fbf51aea01d70ee6340eebd333c4949599a0` |
| delay_ack | `lvrztheg` | `c9f9c6759a3080648ccf09eaf5f524f40019fbd66cbaf2dd2e62d916441fce8b` |
| drop_command | `du3jq1ds` | `6aa96247aecd1bb8e3e76c7f7a378feaff73dfcfb8ce936ee995cb1d75397c73` |

Directory prefix is `/var/tmp/research-stabilized-session-runtime-`. All 87
files independently rehash; 1,348 handled acknowledgments span six sessions.
The exact relay/harness used by each run is retained. Cases are available through
`verify_stabilized_session_runtime.py --evidence` with the sealed build and
`--fault drop_command`, `--fault drop_ack`, or `--fault delay_ack`.

These cases cover command and completion-message faults after activation.
Grant/revoke reply loss and publisher-lifetime changes still need integrated
coverage, followed by consolidated qualification and console testing. The relay
adds transport latency, so these runs do not replace the maneuver campaign.
The qualified default console and all hardware gates remain unchanged.

## Grant/disarm reply loss: passed

The external relay now independently drops grant replies (wire kind 6) or
handled-disarm replies (kind 7), forwarding other traffic unchanged. Both cases
pass on the unchanged `dz3j_4_i` binary pair and timing limits.

- Lost disarm reply: after manual loss, 11 matching replies are withheld. Plant
  targets/thrust remain zero, the consumer remains Revoking, and explicit restart
  is rejected. Restoring replies allows confirmed disarm and then explicit restart.
- Lost grant reply: after a confirmed disable, a successor request remains
  GrantPending without producer command publication. The unconfirmed grant
  expires, output stays disabled, and recovery requires another explicit restart.

Both runs also verify initial rate/hold, sustained disarm, rejection of ordinary
enable and final permission-loss disarm. There are 922 handled acknowledgments
across four physically active sessions; the rejected/unconfirmed grant adds no
active plant session.

| Case | Runtime directory suffix | Receipt SHA-256 |
| --- | --- | --- |
| drop_disarm | `j_t79uyd` | `9c4e51ecc2803c68a3397c22091f04dbb7cf420fc4ec956a25b113f674869fd5` |
| drop_grant | `9ocz70p9` | `886cc8fdc77a81d8ce9e88ed79f238dc8303486f6285ed80409ad767df8807b9` |

All 58 files independently rehash. Prefix:
`/var/tmp/research-stabilized-session-runtime-`. Select `--fault drop_disarm` or
`--fault drop_grant` in the runtime verifier with the sealed build.

An initial injector implementation accidentally made its drop predicate a
truthy tuple, blocking startup status. This was an injector failure before
activation, not a controller failure. It is preserved at suffix `npk5c1_9`,
receipt `6a97e64c0aa2493fb1be4ce08478889b08f6cc4b8e87362a6a53ac22471e9ccb`.
The predicate was corrected; 30 mode/message selection combinations passed
before the two successful live reruns. Their exact corrected relay is retained.

Next: hidden publisher-lifetime changes and consolidated integrated qualification,
then the operator console. Synthetic vehicle-data and hardware gates remain open.

## Hidden command/ack publisher replacement: passed

Private owning-queue hooks `replace_command_source` and `replace_ack_source`
unadvertise and immediately republish the exact prior payload. A private derived
publication wrapper clears its own handle after successful unadvertise; no
production publication API is changed. Both hooks are confined to the POSIX
SITL source, require Active phase, and record revision, payload equality, elapsed
simulation time and payload age. They do not directly invoke fault on success;
normal source-binding checks reject the new advertisement lifetime.

Fresh build `/var/tmp/research-stabilized-sitl-vj5bpho5`: 58,131 inputs and
665 anchors verified; all 252 sealed files rehash. Manifest SHA-256:
`65c28ae6a2c673a65598076dc4459387ba584d80b76dbe2ba81209f9a94b570d`.
ELF SHA-256: `27e9a13244ff98c9006d12f64bbb313a1708c5f793ff260f00c3fc82ef896e40`.
Both registration checks and all 1,467 plugin-side checks pass. An initial
constructor-type compile failure remains in `research-stabilized-session-consumer-tbn1xbox`;
corrected compilation is preserved in `research-stabilized-session-consumer-cl13v1x1`
(582 files; receipt `9319f6b23214504f44097644fc030ad1baa6f5986d6ada971f7339936a0e1992`).

Both integrated runtime cases pass. Each changes revision 3 to 5 with a byte-equal
payload aged 4,000 us and zero elapsed simulation time, below the unchanged
50,000 us completion budget. Normal endpoint checks fault the session, plant
outputs stay disabled, ordinary enable is rejected, and explicit restart captures
the new source lifetime and succeeds. Final permission-loss disarm also passes.

| Case | Runtime suffix | Receipt SHA-256 |
| --- | --- | --- |
| replace_command_source | `r568zuzn` | `72e977e8b70326434a393514084d98d4214f9ad53e026b3c2d9c5803518a90e0` |
| replace_ack_source | `mmyhnrwr` | `9c0f9e37bc5016b58644ac1a18190e1504057e12331eabeff0e43b5fc5273ffb` |

Runtime directory prefix `/var/tmp/research-stabilized-session-runtime-`.
All 54 files rehash; 1,179 handled acknowledgments span four sessions. Select
the corresponding `--fault` name in the runtime verifier using the new build.
This proves command/ack advertisement-lifetime handling, not publisher
authentication or all selected-input/request/response hidden replacements. Those
retain their separate native tests and require an explicit coverage review.
Earlier maneuver/transport evidence remains bound to `dz3j_4_i`; it is not
silently promoted to this new ELF. Next: consolidate current-binary coverage
and qualify the operator console. No physical output or hardware operation.
