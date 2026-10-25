# SLAAPS configuration and routing implementation

## October 24: acceleration diagnostics compiled; lifecycle regression under investigation

Frozen native `/var/tmp/slaaps-native-impnyvop` LINK PASS. Source manifest SHA256
`9446dd05eb8f023b139e79d28d31db9109f34fe566ccc104d0c418bf532c4332`, binary SHA256
`393853c7ccfbd711e2b60da2249baf1bc40ec9dde3380d7121a97bdb3a659ece`.
All eight diagnostic production files independently match the snapshot and
`build/slaaps_recovery/acceleration-diagnostics-20261024/metadata/result.json`.
Generated diagnostic format596 and existing status1440 remain below logger limit1450.
The new local topic carries historical calculated versus exact all-bank-handled
identities, allocation stage/result and model-limit masks; it grants no authority.

Existing signed supervisor regression `/var/tmp/slaaps-native-runtime-4r3fx80u`
terminated FAIL at "ongoing Acceleration uses fresh selected state and companion
evidence after the untouched pilot sample expires". Its failed result and complete
log are retained. Cause is unproven; no deadline or admission relaxation is authorized
by this failure. New diagnostic-specific fixture checks are still in progress.
Board compilation is held pending investigation. This is link evidence only, not
runtime acceptance, flight readiness or physical qualification. No hardware operated.

Follow-up observation snapshot `/var/tmp/slaaps-native-987m0uek` builds unchanged
production with fixture4117f049 and transfer-test19c5e761 (session13585). The failed
transport record shows accepted request9 at12130152151, then map3-to4/epoch11-to12
at12130162142 with NoEligibleRequest and no latched protocol fault. The existing
supervisor deliberately reports ACTIVATING while awaiting the new map ACK/request
inside the old lease; the assertion samples RUNNING at a fixed pilot-age boundary.
This suggests a sampling race but does not prove one. New failure-only observations
retain actual worker/status/fault and original command/handled deadlines; the
assertion and all production bounds remained unchanged for this observation run.

Observation build987m0uek LINK PASS (13585 exit0), manifest SHA256
`80df070edf90493a07853b4fad0fde0f46465858befb56f32c0f3363bd12e2db`, binary SHA256
`b3d62766ce007d616260d9cd676608a37653673300aae0a7836d144b85f302a8`.
Signed runtime `/var/tmp/slaaps-native-runtime-ryjlhws3` PASS (34951 exit0), actual
receiver/signing generation3, nine clock samples and eleven full requests. This
passes the existing active handoff/driver-boundary lifecycle against diagnostic
production, but did not reproduce or explain the prior failed operand. Source
review independently establishes that a valid map-renewal wait can report ACTIVATING;
the next fixture must prove a fresh handled command after pilot expiry within the
original lease instead of requiring RUNNING at one arbitrary sampling instant.
The eight production hashes remain frozen; board/resource verification is authorized.

Diagnostic board build92447 TERMINAL PASS and independent package/source verifier PASS:
`build/slaaps_firmware/acceleration-diagnostics-20261024/board-result.json`, SHA256
`82c53b50ea50e30b90444c10d267d493c1a3d4fdd2e9c933f5f01ea2ad6bfdbb`.
Root independently rehashed the receipt and matched all eight metadata source hashes
against the verifier's checked-source set. Image1,937,396/free28,684 bytes (+3,088
from capture-disarm); AXI109116/SRAM42048 unchanged. Resource job55913 remains
active at this receipt observation; no final stack/resource acceptance is implied. Hardware boot remains unverified.
Resource55913 subsequently TERMINAL PASS, confirmed by resource-runner-phases exit0:
55 units accounted for,45 fresh and10 reused only after exact object/source/command
identity checks. Static ancestry aggregation remains pending. Individual frames/object
sizes are not runtime stack high-water, scheduler/IRQ bounds or timing qualification.

Final resource aggregate sealed and independently rehashed:
`build/slaaps_firmware/acceleration-diagnostics-20261024/resource-comparison.json`, SHA256
`5fb1bc0db2af709c903a1771bd63d1244c9323c6796652241c3f9496441011e9`.
Identified static paths: supervisor5516/6500, companion4848/6500, MAIN2976/AUX2920,
logger2232/3700 and CLI528/2048. These exclude unmeasured descendants/IRQ/scheduler
and do not qualify total stack, runtime heap, timing or actual ULog behavior.
Artifact `/tmp/px4-firmware/px4_fmu-v6x_slaaps_54b2da3c6b_dirty_acceleration-diagnostics-20261024_20261024T221015Z.px4`,
SHA256 `5b0a3020a04634b44b1833f1a76f3bfc6ae1b6c87afc9ff0b876e2c480e05a02`.

Final fixture snapshot `/var/tmp/slaaps-native-mq082hsd` LINK PASS (26387 exit0),
Vehicleeb31d538/Transfer19c5e761/harness3485c5d8 captured and independently matched.
Signed runtime `/var/tmp/slaaps-native-runtime-ns1d23at` FAIL (88605 exit1) at
"new Acceleration edge accepts only its own post-mode ACK", session7303 epoch12/map10,
active1/entry10/worker0, valid state/feedback, acknowledgement false. This is after
the earlier session7301/7302 checks, before the allocator rejection test. Investigate
the exact signed barrier exchange; do not claim full diagnostic acceptance or relax
production expiry. Both failed runtime and initial sampling-boundary failure are retained.
Buildmq082hsd manifest SHA256 `c71ebdc59e7856e68d9318d85957224478ba1c1b1a9afa7f1c97a94bf9183d84`,
binary SHA256 `de4c8509af944c77bbdae96b99d07c783c5d4340754eb32c7834ca3cd95b90c4`.
Signed evidence shows no received class3 barrier211 for epoch12/map10; therefore
the client correctly sent no epoch12 ACK. Last ACK is epoch11/barrier134, accepted.
The helper's fixed50ms wait is shorter than the existing100ms retained-barrier
retry. Investigate transfer completion within the captured original output lease,
including unchanged outgoing output while awaiting ACK; never increase a production
lease. The initial barrier's absence is not yet localized to queue, guard or wire.
A later WSL connection timeout interrupted reads; a retry subsequently read the
completed build receipt successfully, without restarting WSL or the test.
Feasibility review: output/manual/state/permission ages are100ms in the current
synthetic transfer profile, while barrier retransmission is100ms after the barrier.
The outgoing command predates that barrier, so a missed initial barrier cannot be
rescued by this retry within its original lease. A lease-bounded fixture correction
alone is not a demonstrated cure. Priority delivery of the retained exact barrier
through the existing transmit path is under source review; no production change yet.
The portable suite launcher11019 subsequently ended with WSL Service0x8007274c
before returning any evidence path. Revalidate actual Linux processes before a new
compiler/runtime; a connection failure alone does not prove the child never started.
Authorized next production increment isolates retained barriers on a topic alias
of the existing TX message while ordinary correlated statuses retain their queue.
The MAVLink sender drains at most eight barrier entries to one latest candidate,
then gives it one priority attempt within the existing total eight-frame budget.
All binding/configuration/signing/publisher-revision/deadline guards remain; no wire,
retry or lease change. Existing copyBound rejects queue lengths other than one, so
use copyQueuedBound with a bounded drain; do not weaken the uORB API. The alias
inherits the eight-slot queue and needs explicit queued-publisher teardown and
fresh resource accounting. This increment is not covered by sealed5fb1bc0d.

Portable diagnostic snapshot verification `/var/tmp/slaaps-configuration-y0x_1zx3`
PASS (36101 exit0), receipt SHA256
`e60ee8f632f336d940c1ed0a550836ac1606658d5f73bb5037b5f36684064c31`.
Debug, Release and ASan/UBSan pass the actual configuration/control/worker suite,
including new calculated/final-bank/malformed-ACK diagnostics; stock parameter XML
accepts473 parameters/463 vehicle fields. Source is immutable mq082hsd; the newer
priority-barrier changes are excluded. This does not establish native flight admission.
Before the rerun, authoritative guest process inspection found no11019 verifier or
compiler child. WSL recovered without restart; kernel logs show vsock accept failures
and a VMBus ring allocation failure despite available memory/disk. Underlying cause
remains unproven. Old runtime0u4fbgjq startup396/397 and sleeping px4-param401 were
left untouched. No current verification jobs remain after36101 completed.

Priority-barrier native `/var/tmp/slaaps-native-u5sxsmx9` LINK PASS (38799 exit0):
manifest `6647a242e4f93c54c64e16d92ad0350fbf88b52277ef6fdd7a71d75663f27b0a`,
binary `620a1e1e42fe27e63d77dcb78953d030e35de2ab90c59df8d691daf1f2ff4886`.
Five production and three fixture hashes match the frozen priority-barrier-production.json.
Signed runtime `/var/tmp/slaaps-native-runtime-ws3sj92x` overall FAIL (76388 exit1):
actual C++ supervisor scenarios pass, including all three entries and allocator
diagnostic request16/source1/session7303/stage1/solver5 with actual QGC log. Signed
ingress proof is receiver3/signing3/clocks13/requests16. Final Python assertion requires
a request16 accepted wire status, which is absent (last received accepted history15).
Investigate authoritative guard/caller evidence and cancellation before queued TX;
do not weaken revocation or claim the overall verifier passed. Board refresh is
authorized for unchanged production; exact guarded-request proof remains pending.
The next fixture uses a read-only observation after actual successful captureControlInput
under the existing SLAAPS_SUPERVISOR_TEST_ACCESS seam, correlating original signed
request/capture/diagnostic identities. It must not enable TX after cancellation.

Priority board37324 TERMINAL PASS, package/source/RNG/test-isolation verifier PASS.
Root rehashed `build/slaaps_firmware/priority-barrier-20261024/board-result.json`:
`af1e38c4dfe6b4870f12e49cbcc1d600e1f9543d174b785406f7331bfde56f5c`, and matched
all five frozen production hashes. Image1,937,860/free28,220 (+464 bytes). Resource79699
remains active. Slaaps.cpp is frozen08d9403f; forthcoming test-observer raw-source
edits are excluded, with no source-equivalence or newer-source board claim yet.

## October 24: acceleration transfer board checkpoint sealed

`build/slaaps_firmware/acceleration-transfer-20261024/resource-comparison.json` PASS,
SHA256 `80509128dbe8c8f714753c2e697c21f97117244e6ae4377c0ad62cdb498f9f75`, independently rehashed.
Board link/package/source/RNG/test-isolation checks and 53 fresh ARM probes completed; zero reused.
Image1,932,412 bytes/free33,668, +28,816 from parent; AXI109116/SRAM42048.
Identified static bounds: supervisor5412/6500, companion4744/6500, MAIN2976/AUX2920 of4096.
Unmeasured library, syscall, IRQ/scheduler and other descendants prevent total stack-fit,
runtime high-water or timing qualification. Nested object sizes are not additive allocations.
Artifact `/tmp/px4-firmware/px4_fmu-v6x_slaaps_54b2da3c6b_dirty_acceleration-transfer-20261024_20261024T210429Z.px4`,
1,813,424 bytes, SHA256 `eea8db9fabeb31f6cdca084db07a26ebae5d4ac4bc69275869b0afcf68ed875d`.
Source: sealed s4yyrvya, four immutable parent board overlays and the exact uint32 operand correction.
Newer capture-disarm production changes and signed-flight fixtures are excluded. No boot/hardware operation.

## October 24: capture-disarm board checkpoint sealed

`build/slaaps_firmware/capture-disarm-20261024/resource-comparison.json` PASS,
SHA256 `8e8411c9faf561c5ffa04b3d36283c9d7a0a720f637983bc77b0195ea65accba`, independently rehashed.
Build50874/resource52776 terminalPASS. Exactly three production files refreshed from sealed50clq98f;
nine ARM TUs freshly probed,44 reused only after literal object/source/command identity checks.
Initial independent-verifier metadata filename collision is preserved; unchanged verifier rerun PASS.
Image1,934,308 bytes/free31,772 (+1,896); AXI109116/SRAM42048 unchanged.
Identified static maxima remain supervisor5412/6500, companion4744/6500, MAIN2976/AUX2920 of4096.
Capture frame1176 (+208), Slaaps29208/Protocol3312 (+8 nested, not additive). Unmeasured descendants,
runtime high-water/timing, hardware boot and physical qualification remain outside scope.
Artifact `/tmp/px4-firmware/px4_fmu-v6x_slaaps_54b2da3c6b_dirty_capture-disarm-20261024_20261024T213302Z.px4`,
1,814,916 bytes, SHA256 `ffbb1f5fcb8d68d891bafe98ec72a021ab79edec3f44ec1590ee392a20fd1183`.
No hardware operation or new flight admission. Subsequent authority-diagnostic changes are excluded.

## October 24: signed parser to active acceleration verified

Native50clq98f LINK PASS (71663 exit0); source manifest SHA256
`846dff27fed5569ffed2cfb0837211b0b325c5f91a67bf2e0c2c274b66c8c115`.
Binary SHA256 `0f7d61aa048540d8c3b6b40e212a70c9fdc2b2b55c9fe906668d364b2765b668`.
Focused frozen signed supervisor run68085 exit0: `/var/tmp/slaaps-native-runtime-j_wlgc9k`.
Supervisor receipt SHA256 `0d6bbdcc3c667071444cb9b3a1009296dc71a93ac7ff199080d7b7146c59496e`;
signed evidence SHA256 `5dcf2937813b06e1ca3187c222353e1378dbb55b1b114abbb5aca0a7f80b7a24`.
Actual receiver revision3/signing generation3, nine verified clock samples and eleven full requests;
actual signed UDP/parser/protocol -> active Acceleration -> both exact handled bank callbacks PASS.
Manual/Acceleration handoff, original map/ACK/request deadlines, active capture-disarm/profile restore
and no revival pass in the same source-linked scenario. No injected packet updates occur in signed mode.
Authority, configuration and sensors remain synthetic; callbacks capture outputs. No normal health,
flight admission, physical measurement, QGC display, hardware boot or timing qualification is implied.
Full native signed matrix84499 EXIT0 PASS in `/var/tmp/slaaps-native-runtime-iz6yyzeg`.
Full result SHA256 `75a4019c2e72944b7f9f21319b8c510135c04d4f4e0da46259b4a5ff1750e3c4`;
supervisor receipt SHA256 `0f71373930453cdd7c7d915c79c5aafb0b25ad2f52fe989291f74196cd119bfd`.
That supervisor has signed-parser active handoff=true, receiver/signing3, ten clocks/thirteen full requests.
The separate transport-negative scenario still intentionally ends ModeNotSelected; its false control-admission
fields do not describe the separate synthetic-authority active supervisor scenario. Both receipts are required.
Input/control/mixer, profile export/restore, disarmed servo and forced-test-armed EDF commissioning,
Commander mode guards/discovery and transport authentication/expiry/source rejection checks PASS.
Board agent is released to `capture-disarm-20261024` for the three newer production files; source unchanged.
Fixture8b949c0a includes the checked, flushed, untruncated proof output.

## October 24: signed active scenario reached assertions; receipt rejected

Native `z5jrbnwa` LINK PASS (build16084 exit0), with queued-topic no-injection correction.
Signed runtime `/var/tmp/slaaps-native-runtime-njk0sn7q` (24508 exit1) reaches actual
SLAAPS_FLIGHT_HANDOFF_TEST_PASS post_parser=0 and SLAAPS_SUPERVISOR_TEST_PASS checks5397,
but verifier rejects missing complete receiver/signing/count evidence. PX4_INFO truncates the proof
at `receiver=3 signing=`. Preserve this as a rejected receipt, not a completed signed-ingress PASS.
Root changed only that fixture marker to checked printf+fflush, keeping the full parser assertion.
Child `/var/tmp/slaaps-native-50clq98f` from sealedz5jrbnwa is building in session71663.
No production code or deadline changed; runtime re-verification pending.

## October 24: corrective disarm checkpoint passed; signed ingress fixture failure

Native `twwuu__x` LINK PASS, resume39897 exit0. Initial2924 compile failed only new diagnostic
local `accepted` shadowing the prior request result; root renamed it `capture_accepted`.
Original log retained in `compile-initial-shadow-failure.log`; fixture SHA256
`5613a651b59909d2d72b83eaf890fdccbf1ab17fbd42af28c1c0cc2a3569cead`.
Source manifest SHA256 `adbedfce776d170e746175658d28a2ec83aa76586eedb0e669af3df51ae89b34`;
binary SHA256 `ac1a3aba4faae45a07892cc135cc18a1ffb368b4d1bf3840fd7e0f8c9cadaf3c`.
Frozen default supervisor runtime `/var/tmp/slaaps-native-runtime-9qblwzyg` PASS, process42358 exit0.
Supervisor receipt SHA256 `314c705752be2a3cbfc828af433586e772816a9c01067f37865d04d5150f9136`.
Covers both exact capture-disarm publication orders, source/stale/inhibit/prior-fault precedence,
and real active Acceleration session7302 stop/profile restore/no revival, using synthetic authority and captured outputs.
Active ingress remains post-parser in this PASS; no ordinary flight admission or measured qualification.
Signed variant `/var/tmp/slaaps-native-runtime-q0_rw6co`, process7925 exit1, failed before any companion
instruction: "signed active scenario begins without an injected packet publisher".
Setup signing completed, but instructions/exchanges were zero. Fixture requires packet_3 revision zero
although earlier isolated tests used that queued topic. PublicationBase destructor deliberately retains
queued advertisements; ResearchSubscription revision returns zero only when actually unadvertised.
The correction will drain at most eight retained packets, pin revision/generation and require no new data
and an unadvertised local injection handle throughout signed operation; actual receiver identity checks remain.
This is not evidence of signed active-control success or failure after command admission.

## October 24: capture-observed acceleration disarm (verification pending)

Native child `/var/tmp/slaaps-native-2avvjih0` from sealed s4yyrvya LINK PASS, build61743 exit0.
Frozen default supervisor-only runtime `/var/tmp/slaaps-native-runtime-mc8gmyij` FAILED at
"same-observation disarm or fault rejects control capture without returning a reusable command",
before the active lifecycle. Exact scenario was not logged. Review independently found a concrete
fallthrough: a copied disarm/inhibit that fails ordinary-stop proof can still reach the eligible
getter while the atomic witness remains Armed. Corrected Protocol.cpp SHA256
`4cddc74021fa6e11046a1ce3e1dcc7d39b21ae34a952e8cb115ebf056a8a3f41` closes the map
and rejects any copied disarm or inhibit after failed ordinary-stop proof, before pending/eligible access.
Formatting/whitespace pass; corrective native execution pending.
Subsequent work-queue unavailable messages follow test abort and are not established as its cause.
Explicit refresh: ManualExecution.hpp, CompanionProtocol.hpp/.cpp, Slaaps.cpp and the native harness,
plus the existing five automatic test overlays. Default harness isolates this disarm increment;
subsequent signed-flight fixture edits are excluded.
Protocol capture retains the exact observed actuator tuple and original pre-capture evidence deadlines.
A stop-only outcome requires previously healthy ownership and unchanged estimator/device/reset/publisher identities;
kill, stale evidence, source replacement, prior faults and expired leases retain fault precedence.
The supervisor consumes the outcome only with live configuration/routing/power/condition/ownership/output bounds.
Tests add both publication orders and adverse combinations, plus a fresh actual active Acceleration session7302
with actuator-first disarm injected after caller checks. Stop, profile restore and no revival await execution.
Frozen source hashes: supervisor `cb6c012f3bb7fa0939fa7cb02474f9f6c23d36d7f96a378e9e21ef1f11f13fbc`,
protocol `239ea8306aff872ef317c9f433aeface58b8c811a3885a2bb251e3f9d28e31f2`,
header `52ebd5e1c2e0d6fe0cafbd59eae0fa71a7fa2ade58e26f7c1d1f3d307563017d`,
fixture `84a4f1af869f71b164d6d8bb1d6cefcc74424c16b5afa41ba7edba6538d389dd`.
Formatting and whitespace checks pass. No additional flight admission or hardware authority.

## October 24: scheduled handoff native checkpoint passed

Native `/var/tmp/slaaps-native-s4yyrvya` linked successfully (resume15755 exit0).
Initial build59666 failed only fixture callback-counter `-Wshadow`; renamed local counters preserve behavior.
Initial failure is retained in `compile-initial-shadow-failure.log`.
Binary SHA256 `cd75195fb752edf92c012ffc6393fc90440abd4e2bef44275640d9dfd88044c3`;
source manifest SHA256 `4706261cce7378ab85041389e1e82b93218aa6a01a2704513bf9e9e53050c3d0`.
Full runtime87813 exit0: `/var/tmp/slaaps-native-runtime-e3zw7nym/result.json`,
SHA256 `9d0325d5e0a13ff1a77b8b90679b2c28f0c79708acc9947bbad9b62e3c1653a5`.
Harness SHA256 `c6e517f3ab84be9c7f77d47699b21472edffb9458ce4d031cec7fc6287b10b51`.
Actual supervisor/worker/banks pass scheduled Manual-to-Acceleration-to-Manual handoff,
exact handled ACKs, post-edge input and map renewal, both permission-disarm publication orders,
literal off-value callbacks and failed-stop retry after authority expiry.
Encoded EDF endpoint regression, commissioning persistence and Commander guards pass.
These are synthetic-authority capture tests; active handoff ingress is post-parser.
Signed loopback parser/protocol tests pass separately but end ModeNotSelected, without control admission.
Production supervisor test seam is absent. No ordinary flight/health, physical outputs, GUI or boot qualification follows.
The preceding failure chain remains historical; this PASS does not establish every earlier failure's cause.
Current board/resource checkpoint is delegated at `build/slaaps_firmware/acceleration-transfer-20261024`;
it must freshly cover 53 ARM units and changed call ancestry before any current target-fit claim.
Initial board runner81021 failed after successful clone/refresh: ARM uint32_t is unsigned long,
so ManualExecution.hpp math::max(1U, uint32_t-expression) could not deduce one type.
All three operands now use uint32_t{1}; arithmetic is unchanged. Header SHA256
`3419810250884de541735bd8999f1936b5d93d40fdca43c6024e9c465ca18c44`.
Formatting and diff checks pass; this sole new overlay awaits resumed ARM verification.
The native PASS above covers the preceding header version, not this additional overlay.
Resumed board build produced `board-result.json` with passed=true, SHA256
`0b0f7b593e3575fe0ede732ecc93be854b719691ee1b2389300774a1982e0358`.
Board source manifest SHA256 `9c31fb93a28165d6b9f6e8cc6203ba18990b7f017d5115d18ee4163e4a57a17b`.
Image 1,932,412 bytes/free33,668 (98.29% flash); AXI109116/SRAM42048.
Package 1,813,424 bytes SHA256 `eea8db9fabeb31f6cdca084db07a26ebae5d4ac4bc69275869b0afcf68ed875d`.
The package's inherited git label is not source identity; use the frozen source manifest.
Independent package/source/linked-RNG/test-isolation verification PASS. Resource runner62983
completed 53 fresh ARM translation units with zero reuse; full changed-call-chain fit review
and final artifact staging remain pending. Probe completion alone is not stack-fit evidence.
Fresh nested sizes: Slaaps29200, worker transport11504 containing worker11160 containing
acceleration port4640; configuration1868, parameter snapshot1892, control input448/output176.
These nested sizes must not be summed as separate allocations. No boot or hardware operation.


## October 24: selected acceleration feedback integration (verification in progress)

Latest frozen-board checkpoint: `build/slaaps_firmware/full-command-entry-20261024/resource-comparison.json`
SHA256 `f8ec6184df9abe6660feb9f210a04e171d2f7f8ea2b6332f379a85ee082b4a05`, resume90128 PASS.
Image 1,903,596 bytes/free62,484; AXI109116/SRAM42048 unchanged; all52 ARM units freshly probed.
MAIN2936/AUX2880 of4096; supervisor4476/companion4680 of6500. Static bounds exclude unmeasured descendants/runtime qualification.
Frozen native qkg548yp content plus exactly four prior board-only overlays and two approved positive-zero portability corrections;
no newer controller, map renewal or worker integration is covered. Artifact SHA256
`42830b6d4cabf9277c9a4562c1f1aa1678a0d04cdd584520fa5b6ecab5bfed68` (1,783,796 bytes).
The positive-zero corrections separately passed all nine existing standalone wire CTests after fresh compilation:
`/var/tmp/slaaps-wire-portability-20261024/portability-results.xml`; build16580 exit0.
Root added `CompanionProtocol::captureControlInput`: live reobservation, full-v2 eligible access, coherent selected state/feedback,
and unchanged caller output on rejection. It supplies no actuator history or permission; worker/caller integration and compilation remain pending.
Active ownership: input-config agent numerical controller/tests; manual-entry agent EDF transfer; board agent shared worker/bank transfer;
root protocol/supervisor caller. All output transfer must preserve the prior exact handled history and original outgoing lease.
Subsequent root control increment: `enableFromHandled` seeds manual allocation/rate history from the exact prior handled output,
retains centered-stick admission, and bounds first servo/rate slew from that history. All four existing standalone manual CTests
PASS in `/var/tmp/slaaps-handled-entry-20261024/handled-entry-results.xml` (build69162 exit0); controller CPP SHA256
`3bd798ce3434772991a7879964923437fbcd221f9e6ed68766fb88eb9f18dd65` and test
`b73b7f004eede2d6625d591db922a23ac03b8eee4c9710191ebbd2cfe1365e74`.
Manual EDF transfer input preserves sequence continuity and requires a post-transition raw sample.
Combined Debug build59723 and both controller/configuration + metadata CTests PASS in
`/var/tmp/slaaps-acceleration-transfer-20261024`. A subsequent test-only correction isolates pre-transition input rejection
from future-publication rejection (transfer test SHA256 `4f973e56e308932ddaa3233a23fbae446e30849cd790328e162bdaa482d0d952`);
incremental rebuild and affected configuration CTest PASS. These include the actual numerical controller, shared configuration,
EDF transfer and raw adapter; they exclude production supervisor/worker/bank handoff and target compilation.
Review corrected numerical output expiry to include original maximum command age and explicit trusted-evidence deadlines.
Protocol capture now supplies the minimum original map/link/witness/state deadline; no poll renews those leases.
Cross-observer review found `source_epoch` is local to each `PublisherBinding`, with different manual/state topic masks.
The production capture overload now compares actual state-topic publisher revisions, gyro/accelerometer IDs and attitude-reset count
against the supervisor snapshot; it intentionally does not treat local counter equality as sensor identity. Native coverage is being added.
Worker/bank handoff remains under implementation, so these checks do not establish an operational acceleration mode.
Same-instant ACK review exposed a legitimate handoff-time rejection. The first added manual regression failed because the
existing cycle guard rejects duplicate calculation times; handled entry now permits monitored `NotDue` until its first interval,
while ordinary duplicate-cycle rejection remains unchanged. Rebuild16877 and all four standalone manual CTests PASS.
Current controller CPP SHA256 `5c9b0b540b08bb6b6360051126b546ef7cad0634607ee48653273f2a61243144`;
test formatting after execution changed whitespace only. Acceleration now also waits for both PID and actual handled-output
minimum intervals; its updated numerical and native identity fixtures await the next combined integration build.
Incremental Debug90523 compiled the new worker/bank interfaces against regenerated existing command/permission messages;
existing configuration/controller and metadata CTests both PASS (`worker-integration-results.xml` in acceleration-transfer-20261024).
This checkpoint includes the updated numerical same-ACK wait tests but excludes new FlightTransferTest cases and native
transport/driver/supervisor execution. Protocol capture now refreshes in-memory readiness on every exit and exposes
`awaitingControlInput()` only for a healthy armed/selected session awaiting ACK/new request; source/config/expiry faults cannot
be mistaken for this wait. Production supervisor caller has landed and is under review; no new flight admission is granted.
New `FlightTransferTest` reached a focused development PASS for actual Manual-to-Acceleration and reverse worker/bank transfers,
partial ACKs, held ingress expiry, post-edge completion of pre-edge tickets, and malformed binding/marker rejection.
The first attempt failed synthetic schema validation (EDF coordinate rates1000 exceeded100); the second exposed an
under-regularized synthetic coupled model. GDB confirmed `InvalidConfiguration`: lambda1e-5 was below the scaled matrix's
1.6e-5 numerical floor (EDFspan20, residual weights400). Explicit synthetic lambda1e-4 fixes that fixture; production profile
validation is being connected to the same allocator criterion so an unusable configuration is rejected before entry.
After those prerequisite corrections, reverse entry exposed a production bug: the outgoing manual controller remained in Hold,
so reconfigure rejected. Successful forward transfer now disables that numerical controller and its schedule while preserving
the shared EDF and actual handled history. Focused configuration+metadata CTests then both PASS.
This is incremental development evidence, not a sealed target qualification. Later missing-history test timing isolation and
nonzero handled-rate target seeding are pending the next run. Native actual-supervisor/driver acceptance is being added separately.
Subsequent focused build and both CTests PASS with shared all-corner allocator-scaling admission, nonzero handled-rate seeding,
and the isolated missing-history rejection case (`scaling-and-transfer-results.xml`, same reusable development directory).
Native preparation exposed two additional production boundaries: inactive100ms polling could equal a configured100ms companion
control lease, and Commander publishes VehicleStatus before its atomic companion witness. Supervisor monitoring now requests at
most half the shortest configured companion/status/permission age; ordinary maintenance cadence remains separate. During an
already-authorized Manual-to-Acceleration handoff only, capture can report a healthy old Manual witness predating the exact mode
edge as pending observation. The caller retains the original output deadline and issues no outgoing-mode commands while waiting.
These cadence/publication-order changes and their native assertions await integrated native execution; no flight admission changed.
Further review found that an inactive profile could change on a tick that skips maintenance, retaining the old wake-up interval;
`Run` now applies cadence after every normal iteration. A valid 10ms clock map could also expire with long observation leases;
the cadence is additionally capped at half the shared validated renewal lead and half its conservative observation slack.
The shared helper supplies that slack from the same RTT/stamp/drift calculation used by admission, avoiding a second model.
Native assertions cover a 10ms map (1,999us cadence) and a 500ms map with 240ms RTT (4,999us cadence).
Focused formatting and diff checks passed; native compile/runtime verification of these changes remains pending.
Native `81w2nhd_` initial build89408 exited1: new FlightTransferTest used FixedArray<float> equality, rejected by native
`-Werror=float-equal`. Production Slaaps/CompanionProtocol and the scheduled fixture compiled. The exact handled-copy
assertion now uses `memcmp` over the canard float elements; no tolerance or production behavior changed. Initial log retained
as `compile-initial-float-equality-failure.log`; unsealed snapshot resumed as98459 with only that test refreshed.
Resume98459 exited0, native LINK PASS: binary `b35908f15db90390b99b05292e24ce7c7a8556b02f5fbcaef1b5ef9220861497`,
manifest `c7bbe6cdb3e41ee2ad34a2d73370cc4d0d437d3872a83adcbc4bd0f4211c4cd2`.
Runtime31037 `/var/tmp/slaaps-native-runtime-9pr1epk9` exited1 at the combined first-Manual-command/companion-session predicate.
Final published Manual entry12/worker1/active1 means Manual was Running; companion phase3/map0/session0 does not prove when
or why the session was lost. Acceleration handoff was not reached. The fixture lacked an owner-observed disarmed-Manual
witness fence before arming; that race and later map expiry remain hypotheses until first-loss evidence is captured.
Fixture-only SHA256 `8d0f48aa7fc0b35cc18dece832b7e5b7b40e29778a091d8219e7288783bc7359` adds that observed-entry fence
inside the original timeout and retains latest healthy/first unexpected-loss published snapshots in bounded heap storage.
Failure-only diagnostics distinguish sampled observations from atomic causality; intentional reverse revocation is excluded.
Test-only child build50612 forks sealed81w2nhd_; no production code, command deadline, map interval or admission gate changed.
Child `j_kqocvi` build50612 exited0: binary `616e6cf9b0432de6927896767aa60e98098c89363162002844f028028b421908`.
Focused runtime37479 `/var/tmp/slaaps-native-runtime-zyy571vo` exited1 at the disarmed-Manual PREPARED fence.
First-loss diagnostics show healthy protocol time5221646277 with mapdeadline5221693936 and unchanged Commander transition2;
the first lost sample is5221696667, after that deadline, still disarmed/Manual and PREPARING. The client had not enabled
clock renewal yet (it enabled only after Acceleration Running). This proves map expiry during this preparation run, not a
skipped armed transition; it does not retrospectively prove the earlier9pr1epk9 cause. Fixture renewal during preparation
is being connected to the existing bounded CLOCK exchange without changing production timing or admission.
Fixture `5158637846b17c9005febf625c2b6552ba50ace053bc632d56bad492b2dd355d` now enables genuine renewal immediately
after Grant. Auto-ACK remains separate from renewal; published event receipt/protocol time fence queued renewals before explicit
post-arm and forward handoff ACK checks. Forward records the current map and pauses renewal for its original-map assertion.
No timing limit or production source changed. Native child `p4yhzd6l` build31810 is running from sealedj_kqocvi.
Build31810 exited0: binary `dbb1b2014a9797d89a42ab955bbc19173b00a4b1270f0574bce5b87983096216`.
Focused runtime `/var/tmp/slaaps-native-runtime-lcs5icjd` exited1 earlier in existing ordinary-disarm scenario3, before the new
handoff path. Cancellation at Slaaps.cpp line1432 (permission publication failure) used normal=0, then profile revalidation failed;
earlier successful runs used the normal-disarm branch. Source inspection finds a classification gap: permission rereads authority
after the outer cycle's armed check, but its fresh disarm failure is returned indistinguishably from a fault. The retained log
does not independently capture that second authority tuple. A bounded production classification and deterministic regression
are in progress; no stale/inhibited/configuration failure may become ordinary recovery. This run does not test the renewal fix.
The production fix now returns an optional ordinary-disarm result from the permission function's exact copied authority tuple.
Both active-flight callers use that result for cancellation; no new permission is published. The shared predicate accepts either
first explicit disarm publication, requires both samples fresh, and excludes inhibits; configuration/routing/ownership failures
retain fault handling. Root source review found no blocking issue. The test-only publication handshake/regression is still being
completed, so this worktree increment is not yet compile-verified. Acceleration protocol reobservation before permission is a
separate disarm-classification boundary to assess; this bounded fix must not be described as closing every possible disarm race.
Disarm increment frozen: Slaaps.cpp `b53fdf03b576576c1dddc4b5645151aa3e32fa039731118568216d5eed6a60c2`,
fixture `1a147c7a873011cb61572fd94e9edb57271726fdd5b356d33a4193eaa248969b`. The normal result also excludes expired
output/permission/first-command/transfer leases. Existing scenario3 now injects vehicle-first disarm at the live permission
read; scenario4 injects actuator-first. An atomic request/completion handshake prevents concurrent fixture overwrite;
existing profile restore/no-revival assertions are reused. Test seam is absent from the production supervisor compilation.
Formatting and diff checks pass. Native child `6d73hft6` build56522 forks sealedp4yhzd6l with explicit supervisor refresh.
Build56522 exited0: binary `19dffd6fe483de17d8760f57f57b385cb877ac2aafe1e789db53e70bd3ac7e95`.
Focused runtime2666 `/var/tmp/slaaps-native-runtime-0u4fbgjq` exited1 later at the synthetic manual EDF operating-point wait.
Both deterministic publication orders reached permission-boundary cancellation normal=1 and passed profile restore/no-revival.
Preparation renewal also worked: the retained healthy companion sample has mapepoch3, session+ACK valid, and unchanged armed
Manual transition3. Worker10/ConditionRejected occurs before the five-newton operating point; subsequent protocol loss follows
supervisor cancellation. The old fixture did not print schedule_status, so its specific cause remains unproven.
Root added the existing published schedule status to failure diagnostics. GDB diagnostic `slaaps-schedule-debug-pUjk4c` used
the exact sealed6d73hft6 binary with a breakpoint verified at ControlSchedule's Transient rejection instruction. It completed
SLAAPS_FLIGHT_HANDOFF_TEST_PASS and SLAAPS_SUPERVISOR_TEST_PASS checks4040 without hitting that breakpoint. The daemon was
then explicitly stopped; exec30304 exited0. Debugger execution can perturb timing and is not a normal-runtime PASS receipt.
The earlier `slaaps-schedule-debug-l51H5n` attempt stopped on PX4 SIGCONT before the scenario and provides no control evidence.
Independent IEEE binary32 reproduction found EDF100/s at previous0.1 and37us produces an encoded owner step whose subtraction
slightly exceeds the recomputed scalar bound. Actual-component regression and reachable-endpoint correction are in progress;
this is not yet established as the cause of0u4fbgjq.
Driver audit separately found the ordinary stop waited for both disarm publications; with only one fresh disarm, recovery could
leave prior PWM latched. A session-bound off-only ticket and failed-write retry are being implemented with actual callback-value
tests. The stop must distinguish falling disarm from staggered initial arming and must never authorize new positive output.
Driver increment frozen at `845771e670217be003efa8d2ead5c7f7a5f2ec055b5abe2eb63ce9b7303c10a4` (includes root comment correction).
It requires prior fresh both-armed agreement, then either explicit fresh disarm latches the same driver's calibrated off values.
Failed callbacks retry that off-only ticket after input expiry, without command ACK or positive output. Fixture7ebe8bfb checks
literal successful callback values, both publication orders, failure/retry and no revival; native verification remains pending.
EDF schedule fix `3d370bdc6cb3efe14d37659f81dc49d76fb361d53dd9e4959d0acd02ced73d8f` compares the actual encoded command
with reachable binary32 endpoints, preserving the declared rate and leaving IAS checks unchanged. Actual-component regression
`f447d5f2d7c97dd33b1b5d34a4f25165a61d700cdb144820b9e6fd16a0d1bf69` covers rise/fall37us steps, next-float violations,
zero-time changes and NaN. Fresh focused host12614 exited0, both configuration+metadata CTests PASS; receipt
`/var/tmp/slaaps-edf-endpoint-p_ad0u_l/result.json` SHA256 `e150a03a2e8a7c331738defb14a2da639f98633a8ac5f37add2740de603b91cb`.
The original0u4 schedule rejection is still not causally attributed by that separate reproduction.
Combined native `s4yyrvya` build59666 forks sealed6d73hft6, refreshing the driver, schedule, regression and verifier plus automatic
fixture overlay. The outer sequential-supervisor runner watchdog is60s instead of30s for the expanded lifecycles; all actual
controller/input/map/output deadlines and individual fixture acceptance windows are unchanged.
Cold-entry review separately confirms that exact initial zero-EDF handling solves actuator history only: the current-thrust
allocation still cannot supply stationary gravity compensation without actual modeled force. Ground support/spool/takeoff
policy/model or a genuinely feasible measured zero-EDF envelope is required; no tolerance widening or fictitious trim force is authorized.

Current integrated result: `/var/tmp/slaaps-native-runtime-xadpehnk` session7174 PASS, exit0,
on native `/var/tmp/slaaps-native-u1m36_mw` (binary SHA256
`8d83f7272e43ef6faa9fff38a5d7cac07647acf311d4a8040c815eae6198352c`).
Result SHA256 `5a65063a4c13893090bb7be4fdb834f8747cea46fe0b6a21341e710b7a30af7e`;
protocol receipt `505af5b16eb1b70a6e38be81ce1fbe35e9e7d56a953e2d2222a15b9915b14336`.
Manual preparation, actual Commander/servo and forced-internal-arm EDF commissioning lifecycle,
profile export/restore, signed transport, feedback expiry/replacement and deliberate re-entry PASS.
The run remains synthetic/capture-only: ordinary arming, actual QGC GUI, acceleration control,
target runtime/boot and physical qualification are not established.
Independent receipt checks prove feedback closures preceded original map expiry by6720492us,
7201196us and7193017us for7101/7102/7103; respective feedback results are Stale/InvalidMetadata/InvalidMetadata.
Executed harness SHA256 `0e749fd1ec5a5428887a7a2cc283725aacc46aa87558f2fe7e3a2399cdf531c2`.
Current harness `9fb98570bb2b60544154914a1edd8839ff13a46fb5514651ca036ae13bf1ec96` adds those
two map-deadline assertions; evaluated against retained receipts, Python syntax/diff checks PASS.
No redundant whole runtime rerun is claimed for that assertion-only addition.
Board95873 under `companion-reapply-20261024` sealed PASS: image1,901,268/free64,812 (+8 bytes),
source/package/RNG/native-hook-exclusion checks PASS. Resource receipt SHA256
`45a0b3d252c54ab62408dede22d2e73c3eb09a18e563f03a3fc549fabd33bed2`.
Twelve affected ARM units are fresh;40 bind to literal-identical parent object/source/command evidence.
AXI109116/SRAM42048 and known heap sizes are unchanged. MAIN2936/AUX2880 of4096;
supervisor4476/companion4608 of6500; producer744/2240; CLI424/216 of2048 before unmeasured descendants.
These are static budgets, not measured target runtime high-water qualification.
Artifact `/tmp/px4-firmware/px4_fmu-v6x_slaaps_54b2da3c6b_dirty_companion-reapply-20261024_20261024T105853Z.px4`,
1781152bytes, SHA256 `9daf985943395d9d1318b941352ce16b9ccf1b2f76b30146d6c1c765a175eb79`.
Its frozen overlay SHA256 is `c48a273f0b91c4a3f8b79177043585ffeb77fe53615a848f9343f3742d24515c`.
Subsequent preauthorized arm/mode-entry implementation is a separate unverified increment;
this completed runtime and frozen board snapshot must not be attributed to those later edits.
That entry increment is now frozen: Protocol.cpp `d3766bf2909ebaed1de7c1b70d053911b71534b3661d6fdfc5d40ea4a400653d`,
header `0c5657fd6da3d915b76e2cdc65b982ef951046ce8aa8cc96f5b1b09cc414e506`,
Slaaps.cpp `694e7676fb5c6137f04cf85c57e7824ffdda8828118cb437b5b961d57b57ba77`,
fixture `7e10afefe4de5e6647b62ec619f4d4c8f653e5a7f124c1b91dfb863c946a3771`.
Actual caller/disarmed Grant authorizes bounded expected arm/mode edges; original map expiry stays fixed,
and adapter generation barriers invalidate previous ACK/request. Faults, exits and disarm revoke.
The isolated native supervisor fixture exercises real owner Grant/observe/ACK/request APIs with synthetic authority;
the separate actual-Commander runtime has no added authority injection. Format/diff checks PASS.
Native build97273 `/var/tmp/slaaps-native-4unvru_i` LINK PASS from u1m36_mw, exit0.
Focused `verify_native.py --supervisor-only` receipt `/var/tmp/slaaps-native-runtime-i8r5q2ck` FAIL,
exit1 at the first compound assertion that expected entry preserves session/map deadline and invalidates old ACK.
Debugger receipt `/var/tmp/slaaps-entry-debug-2ndinlt8/gdb.log` captures scenario10/session7010 after arm:
map and original deadline retained, epoch5 from4, ACKfalse. The fixture then redundantly assigns
already-selected Acceleration, increments the Commander transition and expects another preserved edge.
That synthetic same-tuple edge is not a real mode change. The fixture now changes mode only if
the current mode differs, retaining scenario10's actual arm, fresh ACK/request and reverse-exit checks.
Corrected fixture SHA256 `2240c013523279a1cb2c59c99faba336164a0639efac88ebcbf1200021635ddc`;
format/whitespace PASS; all three production hashes unchanged. Build35992 is active from sealed4unvru_i
with the corrected automatic fixture overlay. No v2 wire edits are included and no runtime is concurrent.
Build35992 snapshot is `/var/tmp/slaaps-native-vs5xstpl`. Independently, root connected the production
request decoder to the generated quaternion extension getter and common DecodedRequest type;
current Protocol.cpp SHA256 `1ddede0b46cd577461a5e6e64de51f3bc9a3a470879dc4376445aa5542c77ad7`.
The getter zero-fills absent extension bytes; strict request version/kind validation remains adapter-owned.
This ingress change is not part of the entry snapshot and is not yet native-verified.
Entry snapshot subsequently LINK PASS; focused actual-owner supervisor runtime92699
`/var/tmp/slaaps-native-runtime-sj5pldwz/supervisor/result.json` PASS (2845checks),
receipt SHA256 `321bff1d0f250c5c5bac45816801db5f56da015d84d9f1f2d6d2c5b9647c18c5`.
This validates expected entry/original deadlines/new epoch ACK/request and negative transitions,
with isolated synthetic authority and no actuator permission or physical operation.
V2 standalone receipt `/var/tmp/slaaps-full-command-desabkj7/result.json` PASS,
SHA256 `c06f5fdaf872aa97aa4e7dd5781c2ce0caac589b86506804d3551f308255edfc`.
Nine scoped CTests and9strict v1 Python tests pass; production generated conformance has74roundtrips
and4v2 quaternion vectors; frozen offline dialect has32roundtrips. CRC extras130/188/131 are unchanged.
Twenty exact source hashes are retained in that directory's source-hashes.json and rechecked before build.
Combined native82605 `/var/tmp/slaaps-native-qkg548yp` LINK PASS from vs5xstpl with those20files
plus production Protocol decoder1ddede0b. Harness08e52293 adds actual signed valid/invalid quaternion ingress checks.
Combined runtime85085 `/var/tmp/slaaps-native-runtime-6zy3maol` PASS, exit0. Result SHA256
`a980fc9804fb78d574e628115486dcda45f9fe7a9ef955ecbb81cbe5a4a3423e`; protocol receipt
`07c47937ce2f529cd5795de23ca5f1f488d51971329f8903a9fb1382fd93b153`.
Actual signed q/-q commands reached ModeNotSelected32; zero quaternion reached InvalidAttitude38.
Manual/EDF lifecycle, feedback expiry/replacement/re-entry and isolated actual-owner entry checks also pass.
This is capture-only, with no acceleration controller/output caller or flight admission.
Board agent seals full-command-entry-20261024 from frozen qkg firmware plus four explicit immutable
parent board overlays (RNG/RCC fixes and builder/verifier); live renewal/controller edits are excluded.
Board22494 first attempt FAILED: NuttX's minimal cmath exposes signbit as a C macro, so
the new std::signbit strict-v1 quaternion checks did not compile. Retained failure is not erased.
Root replaced only those checks with finite validation then memcpy of binary32 bits to uint32,
requiring all-zero bits (positive zero only). Negative-zero/nonzero rejection is preserved.
RequestGuard.cpp SHA256 `20a097c240c14a2e982205d49e1cac7f18535cd35029294d4b6ffb7f4ed925fc`;
TrustedTimeMap.cpp `607c6fe30eaedbdb059ab04366d7b3fd8175169ea85da20b41331474adc58926`.
Board agent may copy only these exact files into its frozen stage and resume; earlier native/wire
PASS receipts predate this portability-only delta. Formatting passed; fresh checks pending.
Parallel request-v2 ingress edits are excluded from this snapshot and remain independently unverified.

Latest native `/var/tmp/slaaps-native-ux648h6h` linked successfully (session8876).
Runtime `/var/tmp/slaaps-native-runtime-gvdhvyaw` passed the manual and EDF commissioning
paths, then failed the first feedback-enabled session7101 Grant (session75628, exit1).
Its `companion-protocol-evidence.json` retains mapped/prepared state and Ready feedback
before Grant; the final state has decision79, continuity_fault and no valid map/session.
Decision79 identifies trusted-state/supervisor binding rejection, not a proven root cause.
Feedback-loss scenarios remain unverified. This pass does not explain previous intermittent
manual/EDF entry failures. Those verification processes are terminal; no hardware was operated.

Follow-up: configure reconstructed the adapter with a new controller identity but retained
`_admin_sequence` from the prior adapter. WireAdapter requires the first supervisor event to
be 1 and otherwise latches SupervisorStreamFault (79). Reset the sequence only when constructing
the fresh adapter; ordinary same-identity sessions retain their sequence and replay protections.
Source SHA256 `3d5aca3bf825c9178f29c4c6d0097ad019fa7b6c8a848b9b6d145e68af994d60`.
Focused native build `/var/tmp/slaaps-native-tz4djjzx` (session11502) LINK PASS, exit0.
Capture-only runtime `/var/tmp/slaaps-native-runtime-k6j7q8rs` (session72944) FAILED, exit1,
after manual/EDF paths and successful feedback session7101 Grant/exact ACK. This verifies the
controller-lifetime counter fix. Held feedback closed state/map/session/ACK on Skew (result6),
then fixture status parsing failed because PX4 truncated its long line before raw_revision.
Split the fixture status into two bounded lines. The synthetic feedback fixture now sets skew500ms
to isolate feedback age499ms (500ms age minus1ms latency) and explicitly requires Stale (result5).
Production feedback/skew policy is unchanged. Syntax/format/diff checks passed; complete runtime pending.
Fixture SHA256 `e6633bfd14b5db8e89e8b74bf0cdf25ee29b9993da936df6b59e36ca9becd058`;
verifier `2020d7913c538e55f6f513abe9eafc8fac9f7fc1713fb03781252fb88324e782`.
Native fixture rebuild `/var/tmp/slaaps-native-u1m36_mw` session6421 LINK PASS, exit0,
from sealed tz4djjzx. Capture-only runtime `/var/tmp/slaaps-native-runtime-f068fduk`
session48806 FAILED before companion checks: manual operator-window test cancelled early.
Its first-cause diagnostic records now8067266804 minus state sample8067233906 =32898us,
above the explicit20ms state bound, with valid routing/configuration and unchanged publishers.
This run establishes a real stale-state rejection, not a production expiry bug or proof of earlier causes.
The capture-only lifecycle harness now declares state100ms alongside pilot/status100ms;
production defaults and focused freshness checks are unchanged. This does not qualify target timing.
Independent review also found repeated identical actions could match historical results; the harness
now waits for parameter consumption and a later status publication. Syntax/diff checks pass.
Verifier SHA256 `0e749fd1ec5a5428887a7a2cc283725aacc46aa87558f2fe7e3a2399cdf531c2`.
Runtime `/var/tmp/slaaps-native-runtime-xadpehnk` session7174 subsequently passed on unchanged u1m36_mw.
Feedback-before-map-expiry attribution and the added assertions are verified above.

VehicleAcceleration now reports actual applied calibration/filter/bias provenance and atomically
binds raw samples to their publisher lifetime. The optional selected observer independently checks
both raw and bias lifetimes, so a held output cannot survive replacement merely by retaining valid metadata.
The feedback calculation is `f_IMU + R(q)^T*g - alpha cross r - omega cross(omega cross r)`;
native EKF2_IMU_POS is CG-to-IMU and is not offset by SLA_CG a second time.
Eight explicit feedback policy fields and four existing native parameter intents share atomic capture,
identity and inactive apply. This is a declared latency-bounded estimate, not physical qualification.
Freshness retains original timestamps; source changes require settling and a post-settling raw sample.
When enabled, CompanionProtocol requires Ready feedback, caps state expiry and uses existing session
revocation/re-entry. Disabled feedback retains transport-only commissioning. No acceleration controller
or actuator authority is implemented by this increment; the attitude command contract remains pending.

Host `/var/tmp/slaaps-configuration-e698qyuo` PASS: 4065 checks each Debug/Release/ASan+UBSan,
466 total SLAAPS parameters including 456 profile fields. Strict helper/test syntax checks also pass.
Pre-checks exposed and corrected unsupported matrix `.data()`, brace-list dependencies and implicit
double promotions. Native `/var/tmp/slaaps-native-6vqnso5n` LINK PASS, receipt SHA
`24ab76281204a2c5b748f159a1dca6cd15e7d0648d3eafe4ecc40cbc6770414c`, manifest
`874fc9a01c0e627fdab69d9d958f808911251eb384e276d7b24c19a6f32ced3a` and binary
`337f199149a87afb72b263de8b3d08ce95b78867a517fd1820ee0ccfa475b2ff`.
Current board/resource verification remains active; older receipts below do not verify these source changes.
The first board attempt failed because NuttX lacks `<cfloat>`; replacing it with `<float.h>`
(helper SHA `a03e2abfd4d27b4110a3e12cd7092374d52adee5dad6393e14274d930abfa21d`)
preserves the calculation. The failed attempt is retained; the same checkpoint resumed with only that source refresh.
Resumed board LINK/package/RNG checks pass (image1,900,724 B/free65,356 B); resource probes remain active.
Native `9w3wyzb3` LINK PASS with this exact header correction. Runtime `kjody5o3` failed at startup:
the new raw-publisher replacement fixture assumed a queued PublicationMulti destructor unadvertises.
PublicationBase intentionally retains queued advertisements; actual PX4Accelerometer explicitly unadvertises.
The fixture now does the same before replacement and scope cleanup (including the later raw-replacement scenario).
No production change was needed. Test-only native rebuild `5edni_tu` is active; the failed runtime remains preserved.
Native scenarios now include actual raw/bias publisher replacement,
original sample expiry and deliberate Prepare/Grant/ACK re-entry, but have not yet run.

Board `acceleration-feedback-20261024` is sealed PASS: receipt SHA
`11d27058369d2813a1b19c8380c7dc2ff0d0f4e97a75e7e087d9dbc6edf0baf9`.
Image1,900,724/free65,356 B; AXI109116/SRAM42048 unchanged. MAIN2936/AUX2880 of4096;
supervisor4476 and companion4608 of6500; producer744/2240; CLI status424/start216 of2048,
before unmeasured descendants. Slaaps heap20864 B, banks10200 each, CLI transient3728.
52 ARM units covered (50 fresh, two exact-object/source/command parent bindings), linked symbols,
package, RNG and test isolation pass. Runtime stack high-water and physical operation remain unqualified.
Artifact SHA `ccee87c3c01eb2fa59d5547286446d9c77360dc305f5c420657dfc60cd2ee429`:
`/tmp/px4-firmware/px4_fmu-v6x_slaaps_54b2da3c6b_dirty_acceleration-feedback-20261024_20261024T094851Z.px4`.

Corrected fixture native `5edni_tu` LINK PASS; runtime `6gig6dya` remains FAILED overall.
Startup native input/control/mixer checks and isolated supervisor pass, as does real Commander/supervisor
servo-bench operation with captured outputs. Armed EDF commissioning stops during first-command entry,
before the new companion scenarios: both banks handled sequence1, then the first bench cancellation
was line1477. Subsequent configuration/routing values are post-cleanup and do not establish its cause.
The existing native-only first-cancellation hook now records pre-cleanup configuration/routing/worker
state. Diagnostic native `2dh85q6f` links; runtime `aq0xnbjw` failed earlier at Manual preparation:
rejection status preceded the immutable operator deadline. It never reached EDF or companion scenarios.
Neither failure has a proven initiating cause. Native-only preparation cancellation capture now also records its caller/state.
Independent review identified normal same-lifetime report-generation updates as a recoverable capture conflict.
Generation-only bounded retries are being implemented with a frozen whole-call publisher/instance enumeration;
replacement/enumeration changes remain terminal, all per-pass freshness/configuration checks remain, and explicit-time APIs stay strict.
Existing native routing tests will inject updates at the copy/recheck boundary; no timing sleeps will serve as branch proof.
The board receipt above predates this routing change. Header2cf6efd1 and fixturec0517951 are frozen;
native `cln7m97i` and board `routing-capture-retry-20261024` are building the new snapshot.
Deterministic tests use the existing routing fixture and a native-only post-copy callback: two heartbeat
conflicts then stable third pass; exhausted bound preserves every destination byte; publisher/enumeration
changes terminate both initial and retried passes; malformed/stale/unready reports cannot be retried away.
Strict fixed-time capture remains a single pass and does not invoke the callback.

Routing native `cln7m97i` LINK PASS. Runtime `sbu7jtbd` passes startup (including deterministic retry cases)
but FAILS before initial Manual READY. First-cancellation diagnostics now identify line1913:
`observePrearmInput` returned false, while candidate=1, routing=1, inventory=0 and routing fault=0.
Later power status9 is not the initiating branch. Native-only input fail capture now records its inner caller,
reason, source revision changes, original timestamps and configured age bounds; this further diagnostic is not yet rebuilt.
This result neither attributes nor resolves the earlier `6gig6dya` EDF and `aq0xnbjw` early-entry failures.

Routing board/resource receipt is sealed PASS: `routing-capture-retry-20261024/resource-comparison.json`, SHA
`6514d483fc8d08b0b270df92025976f9506c0cada892fcc7f1c4b4bb8a237481`.
Image1,901,260/free64,820 B (+536); static RAM unchanged. Workspace1600 and each endpoint10240 (+40).
All52 ARM resource units were freshly checked. Identified stack paths retain the previous budgets;
the actual board ELF excludes native hooks/diagnostics. Three-pass timing and runtime stack high-water are unqualified.
Artifact SHA `945358dd68265c8f104e0051d2c1bca37588021a3022258f95895d9918d8a1bb`:
`/tmp/px4-firmware/px4_fmu-v6x_slaaps_54b2da3c6b_dirty_routing-capture-retry-20261024_20261024T102154Z.px4`.

Next production boundary: retrieve eligible requests with Ready feedback and original deadlines;
preserve preauthorized same-session transitions only through fresh epoch ACK/new-request barriers;
support authenticated same-session clock-map renewal before old expiry; then connect the scheduled
controller to coupled allocation and exclusive bank/EDF handoff. Existing EDF begin resets output and
cannot provide an armed transfer unchanged. v1 still lacks an orientation objective; that extension and
translational gains remain open. This finding does not authorize stale requests or a hold-thrust fallback.

## October 24: shared physical wrench model and allocation

The shared typed profile now stores measured canard force derivatives, total trim force/moment,
installed EDF force/moment per thrust and six explicit physical residual tolerances.
It reuses existing moment derivatives, servo conversion/limits and the EDF thrust curve.
The affine model uses absolute normalized C1..C4 and EDF thrust in N; all forces are nongravitational body FRD and moments are about CG.
Fixed-model trim is referenced to condition_thrust; scheduled trim uses the shared forward thrust curve at each EDF-command knot.
ControlSchedule interpolates already-formed physical matrices and offsets with the same filtered coordinates as PID gains and moment effectiveness.
Incomplete acceleration-model data leaves Manual scheduling usable and its wrench_valid flag false.
The 127 additional profile fields have native QGC metadata and participate in existing capture, equality and fingerprint mechanisms.

The coupled allocator uses fixed storage and at most 243 box active sets for six wrench rows and five actuators.
It assumes no independent six-axis force/moment authority. Every successful result meets all six configured residual bounds;
ResidualExceeded rejects the regularized solution but is not proof that no other tolerance-feasible solution exists.
The vehicle adapter uses shared servo bounds and the exact nonlinear EDF command-slew interval, capped by scheduled command knots and transient rate.
Actual previous normalized owner command is required; thrust alone cannot uniquely reconstruct it after binary32 rounding.
The representable EDF target is forward-mapped before final achieved-wrench/residual checks; this is a model prediction, not measured thrust.
All rejected calls preserve the caller's output. No ownership or output permission is supplied by this calculation.

Host `/var/tmp/slaaps-configuration-elt74aq9/result.json` passes Debug, Release and ASan/UBSan,
3963 checks each, 448 profile parameters/458 including controls; receipt SHA256 `2a2beee3a1c19e7b085b550f1ce075a103f8b418ca2db295f628c554b53d12a2`.
Input-hash receipt SHA256 `9d43d6c050fb2406fb1e193e22a2453f018d69c574090aeb7f314139fb418ffb`; source closure passes.
Failed attempt `/var/tmp/slaaps-configuration-9v90q51k` is retained: Debug compiled, but the scheduled synthetic fixture's regularization 0.001 was below its numerical floor 0.001768.
The debugger confirmed InvalidConfiguration and scaled diagonal 17,679,997.864; only that fixture changed to 0.01. Solver guards and physical tolerances were retained.
Native build `/var/tmp/slaaps-native-3_3umct1` and board stage `shared-wrench-allocation-20261024` first failed on the adapter's exact float comparison under PX4 -Werror=float-equal.
The failed logs/manifests are retained; adapter SHA256 `f0bc580dbd9a631f8a0cf15d479a3aaed442291768ff7676e0c28698a554a580` uses equivalent <=/>= comparison and explicit double promotions.
Native session40812 links `/var/tmp/slaaps-native-3_3umct1`; board session36671 links image1,894,324 B/free71,756 B after that adapter-only refresh.
The board resource probes then expose a real CLI stack overflow: print_status240 B plus capture1848 B is2088 B before other overhead, exceeding the2048 B task stack.
This failed-fit checkpoint is preserved; MAIN2852/AUX2796 of4096 and supervisor4484/6500 are identified static chains, not runtime high-water qualification.
No runtime was run on this intermediate native binary. The host receipt above precedes the warning-only correction.

The corrective source adds a caller-owned capture workspace overload with alias rejection and unchanged destination on failed reads.
All scalar reads remain in the existing AtomicTransaction; scratch allocation is outside it.
SLAAPS uses heap-owned serialized scratch for owner/startup captures; CLI allocates independent destination/scratch buffers, so it cannot race the queue's workspace.
Corrected native `/var/tmp/slaaps-native-2z3u7svj` links binary `b26c641bb53c99285f88871b2abfd97f3d3614f34ce6053fa8a226afaeb37033`;
manifest SHA256 `bdb5dedd67eda9980cd6bc4800576861fd94792ec22e521670157f33ac69b2b6`.
Full runtime `/var/tmp/slaaps-native-runtime-ozhqrixm/result.json` passes, SHA256 `38de1f08071eebf5e39ccb30f55584b3cb0efa4ad7cbe19305c5e73fc76dd45c`.
It includes the new capture-coherence/alias/failure checks, existing operational regressions, 2549 supervisor/5623 control/2573 mixer/34 input checks and nine lifecycles/restores.
Board `shared-wrench-stack-20261024/resource-comparison.json` passes, SHA256 `6f403e48d84444957601a45193511f9757342dbff294341641fbefa45e7380df`.
Image1,894,956 B/free71,124 B; AXI109116/SRAM42048 unchanged. MAIN2864/AUX2808 of4096; supervisor4476/6500.
The caller-owned capture frame is40 B; identified CLI status/start paths408/208 B of2048 precede unmeasured overhead.
Supervisor heap19824 B, each bank10080 B and temporary CLI buffers3632 B; 12 fresh ARM units plus38 exact object/source/command-bound parent units cover this checkpoint.
New allocator880 B/adapter512 B ARM frames are compilation evidence only; neither has an operational acceleration caller or qualified runtime budget.
Artifact `/tmp/px4-firmware/px4_fmu-v6x_slaaps_54b2da3c6b_dirty_shared-wrench-stack-20261024_20261024T085351Z.px4`
is1,769,348 bytes, SHA256 `f0673e6e3f525b6b84cf20940cbff395a65e48ae3c8aba73aa64fb8cea3009fd`.
The staging label identifies root HEAD plus dirty content; inherited build-snapshot version metadata is not a new repository commit.
All compiler/runtime jobs are terminal; no hardware was operated.
The failed-fit aggregate is sealed at `shared-wrench-allocation-20261024/resource-comparison.json`, SHA256 `e52a760c2ee6094691ad89237b76b56c69d80c14684d49ef9481a04529b1434c`.
The solver has no production acceleration-controller caller yet. CG feedback, orientation objective, controller, admission and exclusive handoff remain incomplete.

Separate normal-arming audit found no EDF Bench firmware blocker in `5ezkuzs_`:
both ordinary ARM rejections occur while SLAAPS readiness is valid, but Commander correctly rejects missing raw accelerometer/gyro/barometer/compass and avionics system_power reports.
The no-plant producer omits those inputs. Propulsion battery_status is not avionics power evidence; later intentional battery failure is not the cause of those initial rejections.
Positive ordinary-health arming acceptance remains unverified; no health checks were bypassed or relaxed.

## October 24: armed forced-mode admission

UserModeIntention now applies canRun to forced mode requests on armed SLAAPS vehicles, using the existing boot-latched airframe identity.
Disarmed selection and stock-airframe force behavior are preserved; explicit termination remains available and cannot be exited through this API.
This closes forced operator LAND admission, not the independent automatic-failsafe modeFromAction path.
Flight admission remains denied pending measured authority and a vehicle recovery policy.

Native `/var/tmp/slaaps-native-5haithcj` links binary `12ab2e9a18ac75ba4a495c0360f521b9a4b525da847747ad85a268be667a6773`;
source manifest SHA256 `70030ed056ce5aee2d89cbd30f1e1bed8048ab169694dd46ae5e61d8a059c5f7`.
Full runtime `/var/tmp/slaaps-native-runtime-5ezkuzs_/result.json` passes, SHA256 `a479f1a87ee70a5426f869e5c08243b19db1c744c2844a925dd8ecd834584fb1`.
The separately bound corrected harness is `d17477ba500f7761aee9f2a57d4123b2656ebeb48d9a88269eb875c370d3324a`.
Review caught a nonexistent MAV_CMD_NAV_PRECLAND enum before runtime; the test now explicitly uses PX4 VehicleCommand value 23, called NAV_LAND_LOCAL in the pinned common dialect.
No firmware rebuild was needed for this test-only correction; the sealed binary and source manifest were preserved.

Unsigned loopback DO_SET_MODE AUTO/LAND, NAV_LAND and PX4 NAV_PRECLAND each receive TEMPORARILY_REJECTED after one send.
The actual uORB ACK identifies the command/source, and each subsequent vehicle_status timestamp is strictly newer than that ACK.
Actual/intended Bench mode, armed worker, nonzero command session and immutable commissioning deadline remain unchanged.
Detailed evidence `armed-mode-rejection-evidence.json` has SHA256 `c7ad412a46e3e33bc8264a6289c752f8b488f4f6c4a9acaec580893142779511`.
Existing regressions also pass: 2038 supervisor, 5010 control, 2192 mixer and 34 input checks, nine lifecycles/restores.
Normal arming remains denied; EDF tests use explicit internal synthetic arming and capture-only outputs.
Armed SET_MODE LAND conversion and termination are source-reviewed, not directly wire-tested by this increment.
Board aggregate `build/slaaps_firmware/slaaps-forced-mode-20261024/resource-comparison.json` passes,
SHA256 `8fe8ddc9e067c900e2e71118e29e26017d5d38e49a59ae64394855d4f0d216b1`.
Image size is 1,864,884 bytes with 101,196 bytes free (8 additional bytes); AXI109116/SRAM42048 are unchanged.
Seven fresh ARM units include UserModeIntention; 41 unchanged units are bound by exact object/source/command identity.
The direct forced-admission path accounts for 288/3250 stack bytes; the prior reviewed Commander maximum remains 1212 bytes.
Other reviewed stack/heap maxima are unchanged; these are static evidence, not runtime high-water qualification.
The 1,747,276-byte package has SHA256 `3d1c479bc8bd78972bce35a7ac78058458f491d58bbf6e7e1cc6450c360d50ce`.
All build/runtime jobs are terminal. Hardware was not operated.

## October 24: dedicated navigation ownership

The SLAAPS target disables Navigator and VTOL takeoff; its rcS branch no longer starts Navigator.
Commander includes the canonical shared geofence, navigation, RTL and DAA parameter definitions when Navigator is absent.
Their defaults remain unchanged; this does not implement mission, RTL, navigation geofence or DAA services for SLAAPS.

Board checkpoint `build/slaaps_firmware/slaaps-navigation-ownership-20261024/resource-comparison.json` passes,
SHA256 `22e04ee48ab9de2714d645e2c640e93337f7e7346fd258933d2421e9c45653d6`.
Image size is 1,864,876 bytes with 101,204 bytes free, saving 86,024 bytes against the Manual-window checkpoint.
AXI usage is 109,116 bytes (2,048 fewer); SRAM4 remains 2,048 bytes.
The actual ELF lacks navigator_main and retains Commander, MAVLink and SLAAPS; the decoded linked rcS contains the guard.
Verification covers 31 fresh ARM translation units and 16 exact object/source/command bindings to the parent; identified stack and owner-heap maxima are unchanged.
The 1,747,228-byte firmware package has SHA256 `98317325b0ff4d34d05270fa5b8fa5361fe29dd831426e69838b114fb95e1bc3`.
Both intermediate failures for missing shared GF_ACTION and DAA metadata are retained; the final build supplies the original definitions.
No target boot, hardware output or runtime stack qualification was performed.

The subsequent forced-LAND checkpoint above preserves termination and stock-airframe behavior;
it does not resolve automatic failsafe selection or establish an airborne recovery policy.

## October 24: bounded Manual operator preparation window

`SLA_MAN_WAIT` supplies an explicit 1–60 second disarmed operator window; zero is unconfigured.
The deadline is fixed at deliberate Manual entry, including time spent waiting for initial inputs.
The first all-bank READY response must still arrive within one second of the proposal.
After READY, fresh input/state/power/routing and publisher identities remain required; no session or deadline is renewed.
The bank independently bounds the full entry-to-deadline span, so delayed proposals cannot add time.
Activation reserves, the first permission and first-command deadline remain within the original window.
After the first handled command, existing live output leases govern operation instead of imposing a flight-duration limit.
Manual status reports incomplete flight admission; all production flight flags remain denied.

Host `/var/tmp/slaaps-configuration-dod5pe0x` passes 3350 checks each in Debug, Release and ASan/UBSan, with 331 parameters (321 profile fields).
Receipt SHA256: `cf07803b7d78af4591522f2ea7d37d76c32f9cc8dcb2a0e14ea8f63d582f4ac5`.
Native `/var/tmp/slaaps-native-wry6zdek` links binary `4e4bad01ab63bb3cfc35d769b42f80dc187dbd1542c4f3682168804db8c55093`;
source manifest SHA256: `b06b23c172f5b16471606d72585977bcaf5c3b37c87530bef66ed91507610e47`.
Full runtime `/var/tmp/slaaps-native-runtime-7e8f_ssi/result.json` passes (SHA256 `655e5fcdcf85d0a8b7f469dc98be792ddf18b4a4ffc7df682e4e69103a2f32bc`).
It verifies the actual Commander/supervisor prepared session beyond one second without renewal, exact configured expiry, cancellation and deliberate re-entry, and ordinary arming denial.
Existing commissioning, signed companion and mode-discovery regressions also pass: 2038 supervisor, 5010 control, 2192 mixer and 34 input checks; nine lifecycles/restores.

FMUv6X `build/slaaps_firmware/manual-operator-wait-20261024/resource-comparison.json` passes, SHA256 `3dfa8779246e97c8b6f00b377e13f86e22532c75a4b10c4299bbd437ce660a51`.
Image 1,950,900 bytes leaves 15,180 bytes; AXI 111164 and SRAM4 2048 bytes are unchanged.
All 47 current ARM probe units and linked-RNG/current-source checks pass.
Identified MAIN/AUX paths are 2860/2804 of 4096 bytes; supervisor 4404/6500; CLI status/start 1712/1504 of 2048 before unmeasured runtime overhead.
Slaaps object is 15272 bytes, each bank 8816, temporary status snapshot 1308, and queue-one status message 168.
The board artifact SHA256 is `3403376094e662eaf846e0072b87aad1e547bbd9e779cfddfb84efe504cb53f5` (1,826,524 bytes).

Retain `fc0skta7` (old one-second malformed-Manual codec assertion), `egicbsuo` (tests passed but header-drift closure rejected), and `ewenqogf` (wrong generator refresh filename before compilation).
The corrected final snapshots above supersede those attempts without altering them.
All native physical callbacks are capture-only, using synthetic configuration and inputs; ordinary vehicle health, measured authority, QGC GUI, target boot, runtime timing/stack and flight remain unqualified.
The five staged mode-registry production files are unchanged; this readiness increment and its dependent tests remain unstaged.

## October 23: actionable configuration and routing rejection

The supervisor retains the exact rejected-apply parameter while that same
configuration fingerprint, scope and apply counter remain current. Edits clear
old attribution until another apply; successful apply clears the fault. Invalid
inertia no longer incorrectly attributes a tensor failure to SLA_MASS.
Current routing errors expose the actual inventory index and one-based MAIN/AUX
channel only when a fresh inventory identifies it. Missing/stale observations
clear the pin attribution, and missing assignments do not invent a physical pin.

Commander supplies native scalar-argument events with parameter-group guidance.
The supervisor also publishes exact parameter STATUSTEXT once per rejected apply
and changed routing text at most once per second; suppressed newer routing detail
remains eligible to report. These direct notifications handle stock Report's
suppression of argument-only changes. Pending disarm retains priority. This adds
no output authority and does not establish QGC GUI acceptance.

Independent review, formatting, whitespace and event extraction pass (13 new
scalar-only events). Host `/var/tmp/slaaps-configuration-31m8u6ae` passes Debug,
Release and ASan/UBSan (3,082 each) and actual metadata for 308 parameters.
The first native compile exposed the ULog format-description limit. Compact new
field names `cfg_param`, `io_index`, `io_bank`, `io_channel` reduce the generated
maximum to 1,440 bytes: 1,440 + 150 reserved bytes < the existing 1,600-byte buffer.
No existing fields or global logger buffers changed. Native `3eg3u4yv` links;
binary SHA256 `687e4156a6785ec9d6ba24844e59e851a622dee658a6a4067cfb3fe7e03d0446`.
Runtime `uq8klqnm` completed 1,930 isolated actual-source checks, then its Python
marker assertion failed because PX4 truncated the long informational marker.
The retained failure is not a passing runtime receipt. The harness-only correction
uses the complete logged evidence prefix. Full runtime `gjoks2mh` passes on the
same binary with harness SHA256
`dfb63bf04562b9b1319bb54a2feb032e47ebecf5c0caa1e6e91093eda63a9d17`.
The corrected Python harness is an uncompiled delta from the sealed firmware.
Native control/mixer/supervisor checks are 4,742/2,126/1,930. Actual mavlink_log
messages, unchanged-attempt persistence/no-repeat, changed-parameter supersession,
AUX8-to-AUX6 rate-bounded routing messages and stale-detail clearing pass. Existing
manual/bench/power/companion paths and seven lifecycle/export-restore checks pass.
The portable host test prefix is byte-identical after native-only field renaming.
Capture-only and explicit synthetic-authority/forced-test-arm limits remain;
QGC GUI rendering and physical outputs were not tested.

Board checkpoint `build/slaaps_firmware/diagnostics-20261023` passes source/ELF/
package/resource checks: image 1,907,220 B (+3,752), flash headroom 58,860 B;
AXI RAM 111,100 B and SRAM4 2,048 B are unchanged. Nine probe units were compiled
fresh (six affected prior units and three reporting callees); 26 unchanged units
were reused only after exact dependency-content/include/flag comparison.
Identified MAIN/AUX stacks are 2,868/2,812 of 4,096 B; supervisor 3,984 of 6,500 B;
the new Commander diagnostic chain is 1,212 of 3,250 B. These are static identified
chains, not runtime high-water or whole-program timing qualification. Supervisor
object size is 12,480 B (+48), endpoints 8,592 B each (+16 each).
Resource receipt SHA256 `383e4da9da0463cb2f325680a1342cfef2db857723cd801ca786291322ee93c1`.
Source closure SHA256 `0e649f42200c22d4321378eced130eb0ccdb00a96e3895289cf59f8d2d98417f`.
All jobs are terminal. No hardware operation, boot or flight qualification is claimed.

## October 23: selected propulsion power integration

Manual and EDF commissioning now require an explicitly configured `battery_status`
instance, ID, source, original-report age limit and voltage band (`SLA_PWR_*`).
Defaults remain unconfigured; servo-only commissioning can operate independently.
The enabled manual schedule must use the same propulsion battery instance/ID.
The QGC battery selectors expose only the three supported message instances.

The supervisor binds the publisher lifetime and selected identity when minting a
preparation session. Disconnection, warning, fault, imminent power-off, invalid or
out-of-band voltage, stale/future/backwards timestamps, held-report voltage mutation
and publisher replacement deny continuation. Original battery expiry caps output
permission and ready-to-arm deadlines, including external replies and Commander.
EDF commissioning uses its existing stop/ordinary-disarm path; restored telemetry
cannot revive the rejected session. Existing deliberate exit/re-entry and inactive
configuration apply remain required. A concurrent explicit normal stop retains
its established normal-stop semantics and never restores output automatically.

This is selected report-health validation, not a calibrated thrust/voltage model,
sensor authentication or flight admission. `BatteryStatus` supplies neither a
separate measurement timestamp nor hardware device ID; neither is fabricated.
Operator voltage bounds and the physical feed/monitor assignment still require
vehicle definition and acceptance. No airborne recovery policy was invented.

Host `/var/tmp/slaaps-configuration-lka3nypt` passes Debug, Release and ASan/UBSan
(3,082 each) and actual PX4 metadata generation for 308 parameters. Native snapshot
`/var/tmp/slaaps-native-fk9aazlr` links. Runtime `slaaps-native-runtime-3qbzsnvv`
passes the isolated actual-source supervisor (1,912) and reaches actual Commander
power-fault output inhibition/disarm, then fails the new Python re-entry sequence:
it retained EN=1 from the no-restart check rather than observing EN=0 first.
The rejected entry preserves the production deliberate-enable requirement.
The corrected harness explicitly observes EN=0 before leave/re-entry and uses
REJECTED=12 rather than FIRST_COMMAND=9 in the terminal assertion.

Full runtime `/var/tmp/slaaps-native-runtime-ghh0mrhs` passes with unchanged native
binary SHA256 `d112941ab5636304427f33572f63d384588abd3c3129145c2bc2deb69219518a`
and separately recorded corrected-harness SHA256
`a3b95a9ddbe8b1e1f3cf33fd8439d07c60cdb58255ec03c19f11b61f696e1b33`.
Native control/mixer/supervisor checks are 4,742/2,126/1,912. Actual Commander
readiness rejects stale and out-of-band power; an active battery fault produces
fresh inhibited output captures and ordinary disarm. Restored power and reapply
do not restart output; a deliberate second session succeeds and stops normally.
All prior manual/schedule/mixer, seven lifecycle/persistence and signed-companion
checks pass. Synthetic input/power and capture-only outputs remain explicit;
no-plant EDF tests still require recorded internal forced arming because ordinary
vehicle health is unqualified. QGC GUI acceptance and hardware were not exercised.

Board/resource verification passes in `build/slaaps_firmware/power-20261023`.
The image is 1,903,468 B (+4,016 B), leaving 62,612 B flash. AXI RAM remains
111,100 B and SRAM4 2,048 B. All 32 probe units compiled fresh: identified
MAIN/AUX stacks are 2,868/2,812 of 4,096 B; supervisor 3,976 of 6,500 B.
The 49,699-entry firmware source closure matches the sealed native snapshot.
Resource receipt SHA256: `abd43c98d34996855e9cb2920e9ac5860af74b791cda0048a404d4b7799d5ec5`.
PX4 package SHA256: `ef6c622fca3bb0ac387b7f73052c2c81521ad8f1e00fa3b639374173240cb588`.
All jobs are terminal. No boot, runtime timing/stack or physical qualification is claimed.
It uses frozen `fk9aazlr` firmware inputs and records the corrected Python harness
as an uncompiled delta, rather than attributing that correction to a rebuilt binary.

## October 23: companion receipt and preparation integration

The SLAAPS targets now select the repository-owned MAVLink dialect with private
IDs 200010/200011/200012 and inherited development messages. The actual receiver
publishes bounded raw payload, receipt time, event sequence, receiver/source IDs,
signing generation and parser signature-verification evidence on four per-link
queues. Six native parameters configure the selected link/source and receipt-age
bound. The supervisor monitors authenticated receipt and latches publisher/key
replacement or sequence gaps until a new accepted inactive apply. Receipt does
not admit commands, establish producer-clock validity, grant a session, or enable
acceleration control. Wire status return is still missing.

Portable configuration evidence `/var/tmp/slaaps-configuration-frcvrnt7` passes
Debug/Release/ASan-UBSan (3,050 each), including 302 generated parameters.
The first consumer snapshot `/var/tmp/slaaps-native-bhbg3s6c` links after fixing
cross-directory generated-header dependencies. Its first configuration failure
is preserved. Runtime `slaaps-native-runtime-uuzs2dth` failed the pre-existing
EDF disarm retry assertion before companion cases. Instrumented quiet runtime
`slaaps-native-runtime-7trm42ns` recorded three requests with WAIT status on the
same observation as the final request. The supervisor now publishes exhausted
retry status immediately; actual disarming remains the only stop completion.
No retry limit, force policy or timing bound was relaxed.

Fix snapshot `/var/tmp/slaaps-native-f07znuwy` links, and runtime
`/var/tmp/slaaps-native-runtime-f1uftbjf` passes the isolated supervisor (1,821).
The first companion positive case then exposed a production integration defect:
`copyBound` deliberately rejects queues larger than one. The new
`copyQueuedBound` API copies ordered queue data and its publisher revision in
the same critical section, using a capped current-lifetime sample count to
exclude earlier advertisers' buffered data. Queue-one behavior is unchanged.
Derived native snapshot `/var/tmp/slaaps-native-nrgcpc4s` links. Runtime
`/var/tmp/slaaps-native-runtime-id206wl9` passes the queue/control checks (4,531),
mixer (2,108), isolated supervisor (1,821), and existing production lifecycle
cases. The new loopback test then failed at MAVLink startup. Isolated reproduction
`/var/tmp/slaaps-mavlink-start-qnpku7tl` showed its mission-manager constructor
waiting one second for absent dataman, beyond MAVLink's startup wait. The harness
now starts RAM dataman before MAVLink and stops it afterward.

Review also found key setup/reload mutating signing state outside the send mutex
used by parsing and transmission. Both operations now use that existing recursive
mutex; dirty-key notifications use atomic compare/exchange so a concurrent later
notification is retained. Key/timestamp/generation policy is unchanged.

Final native snapshot `/var/tmp/slaaps-native-_r7ah9m7` links; binary SHA256
`5af84dd5904975d1c5c652c98b4bb53894387b24884d454292f71615bda5cae2`.
Runtime `/var/tmp/slaaps-native-runtime-phom4snz` passes the full integration,
including actual MAVLink parsing and actual supervisor receipt monitoring with
a synthetic signing key over loopback UDP. It verifies signature replay,
unsigned/wrong-key rejection, header/payload source and target binding, original
receipt expiry, and same-key reload requiring deliberate inactive reapply.
All seven lifecycle and seven restart/export-restore scenarios pass. Native
control/mixer/supervisor checks are 4,531/2,108/1,821 respectively. Harness SHA256
`b46620e5a29026b4578eb37a1f4b71869b49e7418b179800602732d45f155c84`.

An earlier run of the same binary, `slaaps-native-runtime-6buyl3gj`, rejected an
isolated EDF preparation before WAIT_ARM, before MAVLink was instantiated. Its
supervisor and fixture bytes match the preceding passing queue snapshot. The
failure did not recur in the quiet full run; its root cause is not established.
Retain it as an unresolved timing/fixture limitation, not evidence of qualified
real-time behavior. No deadlines or admission protections were relaxed.

The earlier companion board link in
`build/slaaps_firmware/companion-20261023` predates the disarm and queue fixes.
Keep it as attempt evidence, not final current-source qualification. A derived
receipt-specific board check now passes in `companion-20261023/final` for
`_r7ah9m7`: image 1,899,300 bytes, headroom 66,780; AXI 111,100/SRAM4 2,048.
Identified static chains are MAIN 2,876/AUX 2,820 of 4,096, supervisor 3,976
of 6,500, and receiver publication 2,012 of its actual ARM reservation 5,360.
These sums exclude unmeasured libc/allocator/interrupt/callee contributions.
Each active companion queue allocates 144 x 8 = 1,152 payload bytes, up to
4,608 for four links, plus 76-byte nodes. The node lifetime counter adds four
bytes to each allocated node; receiver objects grow by 56 bytes per link.
Resource receipt SHA256:
`d7ab77592b43c95fd04748bed5b1651b75b01cf5e24ab8898f7ee051d2953f40`.
This receipt predates the following coordinator correction. All native
output checks capture callbacks only; loopback signing uses a public synthetic
key and creates no command or physical-output authority.

Read-only follow-up found a concrete preparation interleaving: the coordinator
publishes its request before a driver calls `listen()`, whose normal cancellation
publishes a fresh zero-identity readiness tombstone. A supervisor poll between
that tombstone and the first positive reply previously rejected preparation.
The coordinator now treats only the exact canonical initial unready shape from
the already-bound bank as Waiting, before any positive readiness for that bank.
Fieldwise checks exclude padding; original deadline, routing and lifetime gates
remain, and revocation after positive readiness still rejects. Deterministic
native regression follows the actual begin/listen/poll order. This establishes
a real defect independently; the old intermittent log cannot prove attribution.
The correction passes native snapshot `/var/tmp/slaaps-native-kywjnp4q`, binary
SHA256 `b187ef31828a54415acaf26e7af9a0032646ae3a8952b279f99458aee64b679d`.
Full runtime `/var/tmp/slaaps-native-runtime-k5zzs5a3` passes the deterministic
production-order regression, original deadline/routing/lifetime/malformed-reply
and post-positive revocation checks, existing manual/bench/EDF lifecycles, mixer
capture, and actual signed companion loopback. Native control/mixer/supervisor
checks are 4,653/2,108/1,821; seven lifecycle and seven persistence cases pass.
Current-source FMUv6X checkpoint `companion-20261023/readiness-final` passes:
image 1,899,452 bytes of 1,966,080, headroom 66,628; AXI 111,100/SRAM4 2,048.
The identified static stack sums and dynamic queue costs above are unchanged.
Supervisor probes are fresh; 29 unchanged translation-unit probes are reused only
after equality of actual Ninja include dependencies and compiler commands (1,525
unique dependency paths). All 49,696 firmware/build manifest entries match native
`kywjnp4q`, with 222 changed anchors directly rehashed. Metadata472a versus current
36a dirty content remains explicitly recorded. Earlier failed runs are retained.

Final evidence under `build/slaaps_firmware/companion-20261023/readiness-final`:

- `resource-comparison.json`: `c3f022e09a1e990d63feaaa36868c3bd2c923c18b769ac945c838dd068002473`.
- `board-result.json`: `3ded059112d7c1304d43df7703379e60ac293ea550fc47d72f14a1233fb9e74e`.
- `source-closure.json`: `444f597303a41b85f08b553bbfee4c70d7a5c7d05cbc16a2aa658b5bc9a7ac54`.
- PX4 package: `99a10d247e7cdcc24c1d86f820b232b345b903ad648423adbabfed4f1d4067b4` (1,776,384 bytes).

Staged WSL artifact:
`/tmp/px4-firmware/px4_fmu-v6x_slaaps_36a17722ef_dirty_companion-20261023_20261024T014539Z.px4`.
All jobs are terminal. No target boot, physical operation, runtime stack high-water,
timing or flight qualification is established. Admission/control remains unfinished.

The separate, previously unlinked acceleration-library ARM check exposed absent
`<limits>` and `<initializer_list>` support. Six production files now use exact
C limit constants and a fixed-array status helper with unchanged semantics.
Nine existing focused host tests pass in
`/var/tmp/slaaps-acceleration-cmake-kJrgun` (`portability-ctest.log` and XML).
All five production translation units also compile with strict board flags in
the separate `companion-20261023/final/unlinked-portability-followup` probe;
result SHA256 `778a97a237783c66146f1c78201f03da475c4efe929566589f3e562d9fec0259`.
Its first probe-command failure is retained alongside the original header failure.
The firmware volume was read-only throughout. This proves ARM portability, not
supervisor use or acceleration control.

## October 23: scheduled manual control

The optional empirical 2x2 table uses selected raw indicated airspeed and the shared
EDF owner's normalized command. The same bounded, filtered interpolation updates
PID gains and signed moment effectiveness; no inferred wash law or extrapolation
is provided. Integral-output compensation preserves P/D output across feasible gain
transitions; infeasible transitions and invalid interpolated rank reject operation.
Original sensor expiry caps command expiry, including while bank ACKs are pending.
Native observation requires positive airspeed confidence and binds publisher,
selected instance, barometer/calibration, temperature-source and battery identity.
Upstream fallback temperature is explicitly not measured-density qualification;
vehicle_air_data does not expose its separate temperature sample clock/device.

The supervisor checks the initial zero-EDF point and controller matrix before
preparation, without obtaining propulsion permission. WAIT_SCHEDULE gives a
Commander error for unusable conditions. A nonzero minimum EDF coordinate does
not authorize running propulsion to reach that range. Ground spool/takeoff policy,
local flow angles/wash and a measured admissible envelope remain separate gaps.

Portable evidence: `/var/tmp/slaaps-configuration-bsxx03zy/result.json`, SHA256
`fd464c4bb79f03b25a68ba5c923a2fc8aa18ec674a8d72b3e1c53f64b2c3bc7f`.
Debug, Release and ASan/UBSan pass the shared configuration, actual PID/controller,
allocator and worker checks (2,968 each); native parameter generation validates
296 parameters. This does not compile or verify the native supervisor/adapter.
The initial reusable Debug run failed a test's Report object-padding comparison:
case 0 correctly returned InvalidSchedule and revoked eligibility. Replacing the
memberwise comparison copy with a byte snapshot fixed the test without production
changes; the full matrix then passed. An initially suspected off-center binding
bug was ruled out by existing provisional-binding cleanup; no redundant fix remains.

Native build `/var/tmp/slaaps-native-sl0rv9ry` and runtime
`/var/tmp/slaaps-native-runtime-k9xpcx7h` PASS. Runtime receipt SHA256:
`58b63654ea82dbc0a61e0ded5c20c92f99321472d45cc1e40fdb2a1a081cb58f`;
binary SHA256 `ac10af1844f015a86e9619f44924234b5ea63d1fbae220a8b202956780e8fcae`.
The actual adapter rejects invalid confidence, stale/original clocks and replaced
publishers while honoring selected nonzero instances. Actual-source supervisor
checks reject out-of-range IAS and an interior rank loss, then accept centered
in-range disarmed preparation without any output or flight permission. Scheduled
commands cross the real worker, transport and mixer to capture-only driver callbacks.
Input/control/mixer/supervisor checks pass (34/4,287/2,090/1,797), as do seven
lifecycles and seven restart/export-restore scenarios. Real Commander servo and EDF
commissioning regressions pass; EDF uses the explicitly recorded internal forced
arm because normal no-plant health denies arming. Normal vehicle health is unqualified.

The native first attempt failed on a duplicate subscription name in the new test
fixture. It was renamed without production changes; the same unsealed build resumed.
`compile-attempt-1-result.json` and manifest revisions retain the failure/correction;
the retry replaced compile.log, so its full original text remains only in tool output.
Final fixture SHA256: `00d252546fafac073fd4e7d14381f6b94f82af9a1b1835bccb134b56045928bc`.

FMUv6X target build, package/ELF/source checks, six fresh ARM probe groups, 237 active
probe dependencies and production test-seam exclusion PASS. Evidence is in
`build/slaaps_firmware/scheduling-20261023`. Image 1,892,652 bytes (+31,368 from EDF),
flash headroom 73,428, AXI SRAM 111,100 (+64), SRAM4 2,048. Endpoint objects are
8,416 bytes each (+1,040); supervisor 12,096 (+2,224). Identified static MAIN/AUX
chains are 2,860/2,804 of 4,096; supervisor maximum 3,976 of 6,500. The new initial
schedule/rank prearm chain is 2,240 (2,352 through activation), and scheduled worker
allocation is 3,568. These sums exclude unmeasured runtime/libc/uORB overhead and
are not stack high-water or timing qualification. No stack reservation was increased.
The first board attempt failed because three adapter calls used metadata pointers
instead of topic IDs; corrected adapter SHA256:
`1748f374ce77b028c98580e4f713acba28a549376a9f8ce43bf34a636d37cff1`.
The same unsealed build resumed; build-attempt-1.log and old source bytes are retained.

The staged package is
`/tmp/px4-firmware/px4_fmu-v6x_slaaps_36a17722ef_dirty_scheduling-20261023_20261023T201832Z.px4`,
1,770,368 bytes, SHA256 `6c0a0ffe8e35b532ab543fab3e530a02346b978ca162ee5bc503628ba995f446`.
It retains baseline embedded Git metadata 472a7d6786; the dirty manifest identifies
its content. Expanded committed-plus-dirty source closure finds no firmware mismatch:
board 127 existing matches/2 correct deletions, with 42 unused Tools differences;
native 164 matches/2 deletions, with five unused Tools differences and current build
runner verified. Differing tools are not referenced by the respective compile commands
or build.ninja. See source-content-closure.json and
`build/slaaps_native/scheduling-20261023/native-current-source-comparison-final.json`.
This is current changed firmware-content evidence, not whole-checkout byte identity.

Current supervisor SHA256:
`a92555dc2dcf67fe9c8f32173cc9aef663ad3705f8bbb1e3e46d9c67bcac9564`.
No physical output, QGC GUI acceptance, target boot, measured tuning, manual flight
admission or vehicle qualification is established. Companion acceleration remains
incomplete; the initial zero-airflow/zero-EDF authority and ground spool/takeoff policy
remain unresolved for the actual vehicle.

## October 23: armed EDF commissioning

Production revision `7a8ad9ff` adds explicit armed EDF commissioning to the shared
Bench path: accepted scope2, target5, zero-default `SLA_EDF_BMAX`, low raw throttle
entry, first-zero handling, capped slew, parked canards, immutable duration and
ordinary disarm requests. Partial arming can retain only the original denied
permission before its first grant. Loss of agreement after a grant terminates
that session; the latch cannot block a later deliberate disarmed re-entry.

Current board/resource checkpoint
`build/slaaps_firmware/shared-edf-bench-final-20261023/resource-comparison.json`
passes (SHA256 `e08dd57228193e6575bc1e1ee7021d04413abbcee0e2821d7e404f70e18d433f`).
Image 1,861,284 bytes leaves 104,796 bytes flash; AXI SRAM 111,036 and SRAM4 2,048
are unchanged. Fresh ARM probes identify MAIN/AUX maxima 2,812/2,756 within 4,096
and supervisor 3,960 within 6,500, before unmeasured overhead. Endpoint objects
are 7,376 bytes each; supervisor 9,872. All 54 compiled production hashes match;
later fixture/harness edits are uncompiled board inputs. A canceled slow WSL Git
scan is recorded separately from the successful unchanged Windows verifier.

Final native build `/var/tmp/slaaps-native-7tlcez9j` and runtime
`/var/tmp/slaaps-native-runtime-uofe3loo` PASS: 34 input, 3,686 control, 1,780
mixer and 1,096 isolated supervisor checks, seven lifecycles and seven restart/
export-restore scenarios. Binary SHA256
`4de0b27d1aecd6e06e6846419f161d48b15ab18ee9980f6fab984f976ae31b90`.
Runtime receipt SHA256
`07a68f27abb4a6b3874e7580081c29fc76b53b2e86b5a2ba8a01993c0425c902`;
local closure `build/slaaps_native/edf-commissioning-20261023/final-verification.json`
SHA256 `2c06e11e5fb38ee7cb3fe9cb2d4d96cf03044d0e0198b5795d719f474a3eff4f`
binds all 63 current relevant source hashes. All owned jobs are terminal.
The normal production supervisor, real Commander and actual mixer callbacks
demonstrated disarmed non-actuating preparation, armed initial-zero/capped EDF,
parked canards, all-bank zero stop, ordinary disarm and no automatic revival.
Accepted scope2 survives export/restore; saved EN=1 restores to EN=0 without
arming or output permission. The no-plant environment denied ordinary arming;
the capture-only test explicitly used stock internal forced arming. It neither
injected SLAAPS permission nor qualified normal vehicle health or flight admission.

Preserve the earlier failed attempts and their corrections:

- `slaaps-native-38ky2la6`: preceding supervisor `ca688ca2`, link only.
- `slaaps-native-bitwxwed`: final production link; runtime `db4h18bd` failed a
  fixture that checked pre-write saved markers. Fixed by recapturing parameters.
- `slaaps-native-rj25pgu_`: corrected fixture link; runtime `o8y7uvhj` exposed an
  unsupported 150 ms idle-maintenance reset assumption. Replaced with observed
  reset, continuous inactive/session checks and unchanged-command verification.
- Reruns `gcriiz7v` and `h7o9g09c` passed isolated supervisor/mixer and real
  Commander servo commissioning. The latter demonstrated capped EDF PWM, neutral
  canards, all-bank zero stop and ordinary disarm. Harness command-name and
  legitimate mode-exit assumptions were corrected after these retained failures.
- Final fixture/harness build `slaaps-native-7tlcez9j` and runtime `uofe3loo`
  close these software verification failures; physical qualification remains open.

Native paths above are under `/var/tmp`, with runtime prefix
`slaaps-native-runtime-`. All callbacks capture outputs only; no hardware was
operated. Neither synthetic calibration nor static stack analysis proves flight
readiness, physical handling, target boot or runtime stack/timing qualification.

## October 22: shared servo-only Bench integration

The production Bench path now uses the shared accepted vehicle profile, logical
canards, asymmetric travel bounds, mapped PWM banks and exact activation/handled
ACKs. It requires genuine disarmed/prearmed authority and a fresh, deliberate
`SLA_BENCH_EN` edge after readiness. `SLA_BENCH_CH` selects combined or individual
canard operation; `SLA_BENCH_SEC` bounds the session. Stock QGC parameter metadata
contains these controls (192 parameters total). EDF commands remain explicitly
inhibited; this is not the requested future armed EDF commissioning capability.
Dedicated-airframe output ownership also blocks stock actuator-test bypasses while
inactive/prepared and keeps endpoint servicing scheduled without stock publishers.

Current integrated software evidence:

- Portable `/var/tmp/slaaps-configuration-vplb804w/result.json` passes 2,361 checks
  each in Debug, Release and ASan/UBSan, plus 192-parameter metadata validation.
  All 102 recorded inputs match at completion. Receipt SHA256
  `42b00c57178afd3b623c6cd94206f49ca35320c743b1b42c73ab455774e73081`.
- Native `/var/tmp/slaaps-native-14eh2_6k` and runtime
  `/var/tmp/slaaps-native-runtime-6mvmc_9r` pass 34 input, 3,616 control, 1,113 mixer,
  and 714 isolated actual-source supervisor checks, seven endpoint lifecycles and
  six restart/BSON scenarios. Binary SHA256
  `7b8d2388ae3b8a1b5e7dcc06bae66ed877f20e185c96560220fe0d275d9c2835`.
- The normal production supervisor has no test admission seam. Real Commander
  selects registered Bench mode 25 and supplies genuine disarmed/prearmed authority.
  A deliberate enable drives the configured individual canard through both actual
  mixer callbacks; captured AUX8 exceeds neutral while EDF AUX4 stays zero. Explicit
  stop produces fresh zero frames in both banks, completed entries cannot revive,
  and ordinary arming remains denied. Configuration/input publishers are synthetic;
  the callbacks capture outputs and do not operate hardware. GUI acceptance remains open.
- The actual-source supervisor fixture also covers combined/individual operation,
  early disable before full preparation, fixed duration, raw loss, profile retention
  on unchanged normal stop and deliberate re-entry. Its authority topics are synthetic
  in a separate process, isolated from the real-Commander production scenario.

The stop-race correction recognizes GUI disable between the two coherent command
snapshots, rejects simultaneous bound target/duration edits, and rechecks current
routing/configuration before retaining an active profile. Shared pure validators
replace disposable owner objects and preserve the original raw-source/mapping checks.
Commander CLI now exposes exact `ext1..ext8` names through its existing mode-command
handler, with invalid names returning failure; this does not change admission.
Final board `build/slaaps_firmware/shared-servo-bench-final-20261022` passes source,
ELF/package, test-seam exclusion, six fresh ARM groups and active-interface probes.
All 152 frozen inputs and 54 current production source hashes match. Image 1,856,060
bytes leaves 110,020 bytes of flash; AXI SRAM 111,036 and SRAM4 2,048 are unchanged.
The pure-validator extraction reduces Bench activation MAIN/AUX chains by 948 bytes
each, to 2,692/2,636. Overall identified physical maxima are now Manual activation
2,812/2,756, leaving 1,284/1,340 in the 4,096-byte queue before unmeasured overhead.
The new supervisor normal-stop routing check is explicitly included: 3,904 bytes
against its 6,500-byte stack. Endpoint objects remain 7,344 bytes each; supervisor
9,808. These are static identified paths, not runtime stack/timing qualification.
Final `resource-comparison.json` SHA256 is
`30c1a56880fe4315aba8c3c4925b77096e6a629391507bc50d26b01697266552`.
The sealed artifact is
`/tmp/px4-firmware/px4_fmu-v6x_slaaps_36a17722ef_dirty_shared-servo-bench-final-20261022_20261023T014701Z.px4`
(1,741,387 bytes), SHA256
`85563aef1045fb2d9f0e2ce6e0ad767431ea72c4c6db53e93a95ceeeb2985dd3`.
Generated configuration and all 11 linked queue constants confirm 4,096 bytes;
the global stock default remains 2,800. All build/probe jobs are terminal. No firmware
was flashed. Earlier evidence is retained below.

Intermediate evidence and failures before those corrections:

- Portable `/var/tmp/slaaps-configuration-c626bqvt`: 2,361 checks each in Debug,
  Release and ASan/UBSan, plus 192-parameter metadata validation. Later native-only
  supervisor fixture additions are not covered by that source receipt.
- Native build `/var/tmp/slaaps-native-rczxflzn` passes. Independent runtime
  `/var/tmp/slaaps-native-mixer-independent-j3s24s12` passes 34 input, 3,616 control
  and 1,113 actual mixer checks, including shared Bench handling and stop cases.
  Binary SHA256 `df12bcd834579cc2af0e75f0889aec0cfa6eeb0c448f330f588cbb55e7a43cd9`.
- Runtime `/var/tmp/slaaps-native-runtime-etgnus5j` failed using the obsolete
  sealed-source harness: its Bench fixture omitted required travel bounds.
  Current harness supplies explicit bounds. Do not treat that run as a pass.
- Current harness run `/var/tmp/slaaps-native-runtime-4bb02ywx` fails the isolated
  actual-source supervisor's ordinary GUI-disable case. A disable between two
  command-parameter snapshots was incorrectly classified as a fault. The corrected
  current runtime above passes; the original failing run remains unchanged.
- Board `shared-servo-bench-20261022` build, source/ELF/package verification,
  test-seam exclusion and six fresh ARM probe groups pass. Image 1,855,892 bytes,
  headroom 110,188; AXI SRAM 111,036 and SRAM4 2,048. Supervisor object 9,808 bytes
  (+888), each physical endpoint 7,344 (+64). Activation chains 3,640/3,584 leave
  only 456/512 bytes in the 4,096-byte queue before unmeasured overhead. This is
  unresolved; removing disposable validation objects is the next correction.
  `resource-comparison.json` explicitly records the superseded source snapshot;
  subsequent stop/fixture/harness changes are not covered by this board result.

All outputs in native tests are capture-only. No hardware was operated. Flight
admission, measured qualification, armed EDF commissioning, scheduled effectiveness
and companion acceleration remain incomplete. Prior evidence below is retained
for its exact source checkpoint, not as acceptance of these later changes.

## October 22: supervisor activation and live observation clocks

The production supervisor now connects prepared banks to activation, the selected
manual worker, shared EDF ownership and sustained command/handled-ACK processing.
It reports ACTIVATING, FIRST_COMMAND and RUNNING separately; RUNNING requires the
first exact command to be handled by every participating bank. Both staggered
arming orders retain the original preparation bound. Partial activation, input or
authority loss, configuration/routing changes and output failure cancel ownership
and require deliberate re-entry. Normal disarm can retain only the unchanged
accepted profile after both fresh disarmed observations; faults require a real
disarmed reapply. Four reply callbacks complement the periodic expiry check.

`SLA_STATUS_AGE`, `SLA_ARM_AGE`, `SLA_ACT_ACK_US` and `SLA_FIRST_CMD_US` are typed,
QGC-editable timing fields. All-zero timing permits inactive profile staging only;
partial timing is invalid and manual preparation requires complete valid timing.
The activation timeout and first-command budget must fit the configured cycle
bound. This adds four parameters, bringing native metadata to 189. Portable host
`/var/tmp/slaaps-configuration-a5522egg` passed 2,314 checks each in Debug,
Release and ASan/UBSan, plus metadata validation. Later clock changes are native.

Actual-source testing exposed a startup deadlock: activation-reply lifetimes were
advertised only after a proposal, while the supervisor required them before its
proposal. Endpoints now advertise both reply lifetimes at initialization, without
publishing authority. Partial advertisement and publisher replacement still reject.
Repeated operation also exposed valid messages being rejected as future-dated
because callers sampled time before copying concurrently published messages.
Live routing, permission, preparation, activation, command and ACK readers now use
post-copy HRT. Existing explicit-time APIs remain strict. Final selected-input,
configuration, identity and expiry checks remain; original wire/sample times and
deadlines are never renewed. Supervisor permission expiry is capped by its copied
Commander/arming freshness, and driver-local permission expiry is further capped
by copied vehicle, actuator, supervisor and routing-report freshness. Existing
bank guards enforce that bound after later reads and before handled ACKs.

Intermediate native `/var/tmp/slaaps-native-jox96upl`, runtime
`/var/tmp/slaaps-native-runtime-ylddtqyj`, passes 3,569 control, 34 input, 626 mixer
and 357 actual-source supervisor checks, seven lifecycles and six restart/BSON
scenarios. This includes both arming orders, first-command handling, sustained
synthetic commands, cancellation, normal disarm and deliberate re-entry. The
positive supervisor fixture runs in a separate PX4 process with capture-only
callbacks and a compile-time test seam. The normal module has no seam; real
Commander ordinary arming remains denied, availability masks remain zero and
production permission flags remain false. Final native
`/var/tmp/slaaps-native-ads0tcog`, runtime
`/var/tmp/slaaps-native-runtime-_hc67oph`, also passes the complete suite with the
two-file authority-expiry clamp. Binary SHA-256 is
`3286242615cfc22325ed2ddf285eb0585ada66b638b3dce6ed900d29a5700fac`;
harness SHA-256 is `0e386a1c1e24eff866a3b6ddc816cc7b0659f78c5ce527d9f107f0d2d2961382`.

Retained failed runs explain the corrections: `eswkm_aq` found the endpoint
advertisement deadlock; `lfcvyvzx` passed the positive scenario but exposed retained
synthetic registration contaminating later Commander tests; `tr8v7zc9` exposed an
inappropriate 100-ms synthetic authority age for real Commander's 2-Hz unchanged
status publication; `44p5iiue` exposed the live ACK clock race. All are native
runtime directories under `/var/tmp/slaaps-native-runtime-`. The real-Commander
fixture now explicitly applies its 1-s synthetic age through parameters; no
production default or qualification claim was introduced.

Board `manual-supervisor-replies-final-20261022` verified the earlier activation
and timing baseline: image 1,834,740 bytes, flash headroom 131,340 bytes, AXI SRAM
111,036 and SRAM4 2,048 bytes. It excludes subsequent live-clock and expiry fixes.
Expiry-clamp board `manual-live-expiry-final-20261022` passes current source,
ELF/package/isolation and static resource checks: image 1,841,708 bytes, flash
headroom 124,372, unchanged static RAM and endpoint/supervisor heap sizes. Fresh
ARM analysis found MAIN/AUX activation chains of 2,680/2,624 bytes, leaving only
120/176 on the previous 2,800-byte hp_default stack before unmeasured overhead.
The dedicated SLAAPS board now uses the existing Kconfig stack setting at 4,096
bytes, without changing stock targets. Final board
`manual-hp-stack-final-20261022` verifies that setting in the generated configuration
and all 11 linked ELF hp_default constants; the global default remains 2,800.
Image size is 1,841,700 bytes with 124,380 flash headroom; static RAM and heap
object sizes remain unchanged. The one shared runtime queue requests 1,296 more
stack bytes. Identified MAIN/AUX margins are 1,416/1,472 before unmeasured overhead;
the supervisor chain is 3,336 of 6,500. All six ARM groups were freshly compiled
because generated configuration changed their dependencies. The active-interface
probe covers 235 compiler dependencies. Current production recheck matches 46
production sources and the full 150-file freeze, with unchanged fixture and no
synthetic authority in the board. See
[`resource-comparison.json`](../build/slaaps_firmware/manual-hp-stack-final-20261022/resource-comparison.json)
for bound receipts, including queue configuration, independent verification,
resource summary, stack chains and test isolation. Firmware is staged at
`/tmp/px4-firmware/px4_fmu-v6x_slaaps_36a17722ef_dirty_manual-hp-stack-final-20261022_20261023T003844Z.px4`,
1,725,915 bytes, SHA-256
`85abc45a7a6f26bff31a1867ca59596b31f6cff5638e95be316279386791abc7`.
Source manifests identify the dirty checkout despite the retained older embedded
tag. No boot, runtime stack/timing or electrical qualification is claimed.
No hardware operated. Production
measured-condition/recovery admission is still absent, so these connected callers
and synthetic successes are not a completed operational flight mode.

## October 22: prepared-bank first-grant ordering

Prepared physical-bank endpoints now have a bounded non-actuating wait for the
first permission grant. Commander may report arming before the supervisor grant,
or vehicle_status and actuator_armed may arrive separately. Both messages must
remain fresh, with valid selected mode, configuration, routing and publisher
bindings; both must report armed before grant acceptance. Waiting never produces
PWM, activation acknowledgment or handled-command acknowledgment. It cannot begin
from an already-armed unprepared bank and cannot extend the original preparation
deadline. The previously captured stop frame remains owned during this interval.

The new explicit `awaitGrant` path leaves ordinary strict permission reads intact.
Only an exact all-denied disarmed decision may remain pending after its lease;
it is never authority. An armed denial, inhibition, stale positive, identity or
publisher change, sequence mutation/regression, or original preparation expiry
rejects. A fresh positive observed before local arming agreement keeps its own
expiry. After the first accepted grant, disagreement or permission loss remains
a fault and cannot return to waiting.

The existing activation endpoint may defer a validated ACTIVATE until permission
arrives. Its exact original publication time and proposal stay fixed; cancellation
and publisher checks remain live. Substitution with an older-session transition
after a deferred current ACTIVATE also rejects. No wire message or runtime flight
admission bypass was added. Production supervisor armed activation remains absent.
Root and independent review covered the two publication-order races and pending
mutation/cancellation behavior. Native `/var/tmp/slaaps-native-3vgutxj5`, runtime
`/var/tmp/slaaps-native-runtime-1t807zj5`, passes 3,386 control, 34 input and
622 actual-mixer checks. The real mixer is pumped between separate arming,
permission and ACTIVATE publications; no PWM or activation/handled ACK occurs
until both arming messages agree and permission is accepted. Existing productive
worker sessions and seven bounded-wait fault scenarios pass, including post-grant
disagreement that cannot resume. Real Commander disarmed entry/re-entry, seven
supervisor lifecycles and six restart/BSON scenarios also pass. Positive flight
authority remains explicitly synthetic and callbacks capture-only. The portable
host behavior did not change in this native-only increment; its prior matrix was
not rerun.

Board `manual-first-grant-final-20261022` passes independent source/ELF/package/
capacity verification, six current ARM stack groups and the active-interface
probe (235 compiler dependencies). The image is 1,825,796 bytes (+712), with
140,284 bytes flash headroom; AXI SRAM remains 111,036 and SRAM4 2,048 bytes.
Endpoint/supervisor heap sizes remain 7,240/7,576 bytes. MAIN/AUX identified
chains are 2,520/2,464 of 2,800, leaving 280/336 bytes before unmeasured overhead.
The final production recheck matches 45 current source hashes and the exact
fixture, with three production refreshes and no drift. See that stage's
`resource-summary.json`, `stack-chain-review.json`, `production-recheck.json`
and `active-interfaces/result.json`. Dirty-labeled firmware is staged under
`/tmp/px4-firmware/` with suffix
`manual-first-grant-final-20261022_20261022T222826Z.px4`, SHA-256
`3ed2bb6a2a60a70a586ebe3a3d825039eb69d6b171d449d7172134d577234fd6`.
Source manifests identify the dirty checkout despite its retained older embedded
tag. Static resource evidence does not establish boot, runtime stack/timing,
electrical behavior or flight qualification. No hardware operated.

## October 22: asynchronous first-command delivery and bank stack reduction

The shared EDF input, owner, worker and output-bank consumer now distinguish the
immutable preparation/session-entry timestamp from a later permission observation
and control decision. A fresh post-entry pilot sample may precede permission and
bank activation. Its raw timestamp and deadline remain unchanged. Samples before
entry still fail; a calculated actuator frame must still postdate actual bank
activation. Activation replies retain actual bank activation time. Renewed
permission cannot extend a held raw sample or the original no-request timeout.
This fixes a real production integration defect hidden by equal-time fixtures;
it does not grant production flight admission or choose airborne recovery behavior.

Each serialized bank transport now owns typed read/decode scratch, separate from
its retained command baseline and the consumer's pending handling ticket. This
removes automatic wire/frame copies on activation and dispatch paths without
changing ownership, replay, expiry or successful-driver-handling requirements.
Root and a second implementation owner reviewed the buffer lifetimes with no
aliasing findings. ARM measurement shows +256 bytes per endpoint and a 136-byte
reduction in the largest identified physical-driver chains.

Host `/var/tmp/slaaps-configuration-8liblhst` passes 2,266 checks in each Debug,
Release and ASan/UBSan configuration; 185 metadata entries validate. New coverage
uses session < raw sample < permission < decision, checks both banks and handled
ACKs, original sample expiry, pre-entry rejection, no-request timeout, actual
activation time, and rejection of frames calculated before bank activation.
The existing native worker/mixer scenario now captures the first pilot sample
before permission and bank activation, then delivers it at a later decision.
Native `/var/tmp/slaaps-native-f4ia3lc_`, runtime
`/var/tmp/slaaps-native-runtime-hepov45a`, passes 3,194 control, 34 input and
446 actual-mixer checks. Three actual worker/mixer sessions deliver the earlier
raw first sample through capture callbacks and true handled acknowledgments,
retaining partial-failure and callback-time authority-revocation coverage. Real
Commander disarmed readiness/cancellation/re-entry and six restart/BSON scenarios
also pass. Active-worker authority remains explicitly synthetic; production
supervisor activation remains disabled. No physical outputs are exercised.

Board `manual-first-command-final-20261022` passes independent source/ELF/package/
capacity checks, all six ARM stack groups and the active-interface ARM probe.
The image is 1,825,084 bytes (+360), with 140,996 bytes flash headroom; AXI SRAM
is 111,036 and SRAM4 2,048 bytes. Endpoints are 7,240 bytes each, supervisor
7,576 bytes (+8 for the owner start clock), capture workspace 1,560 and mixer
1,200 bytes. MAIN/AUX identified maxima are 2,512/2,456 of 2,800, leaving
288/344 bytes before unmeasured overhead. The supervisor chain is 2,504 of
6,500. See `resource-summary.json`, `stack-chain-review.json`,
`production-recheck.json` and `active-interfaces/result.json` in that stage.
All 45 changed production sources match; the fixture hash also matches. These
static measurements do not establish runtime high-water, timing, boot or vehicle
acceptance. `resource-comparison.json` links the final receipts. The dirty-labeled
firmware is staged under `/tmp/px4-firmware/` with suffix
`manual-first-command-final-20261022_20261022T220911Z.px4`, SHA-256
`04386a3f048d3569b9ca343de167c524e7fbf2c5aaffe66f9ccb4e6fed57bfd5`.
The embedded ancestor tag is retained in receipts; source manifests identify the
current dirty checkout. No hardware has been operated.

## October 22: deliberate disarmed manual entry

The supervisor now uses fresh Commander `nav_state_user_intention` and disarmed
state to require an observed departure followed by deliberate SLAAPS Manual
selection. Applying or restoring configuration alone no longer prepares outputs.
Startup already selected, stale Commander state, or rejected bound input cannot
silently establish a new entry. Initial missing configuration, routing, selected
input, centered sticks, low normalized throttle or valid numerical state produces
an actionable readiness status. Once bound, input loss, publisher replacement,
configuration/routing changes and the original preparation deadline cancel the
session; repaired data or another apply cannot revive it without mode re-entry.

The same selected-input observer binds all seven publisher lifetimes, sensor IDs
and source epoch. Disarmed observation is explicit and cannot be used through the
armed guard API. Raw selected throttle is validated alongside attitude axes before
shared endpoint normalization. Initial input must postdate the entry. No output
permission is granted: all availability masks remain zero and the production
supervisor still does not start the manual worker. The 100 ms diagnostic cadence
and original one-second preparation lifetime do not establish a persistent
ready-to-arm state or qualified active-control timing.

Commander emits ten specific entry/readiness events through native PX4 event
metadata, including configuration, routing, selected input, centering, throttle,
state, cancellation/re-entry and the remaining flight-admission boundary. Generated
metadata contains every event; actual QGC GUI operation remains unverified.

Host `/var/tmp/slaaps-configuration-r0klcf4k` passes 2,234 checks each in Debug,
Release and ASan/UBSan, with 185 parameter metadata entries validated. The first
native run `/var/tmp/slaaps-native-runtime-0xtfdrzp` exposed a fixture defect after
publisher withdrawal: `Publication::unadvertise()` retains its handle, making
subsequent `advertise()` a no-op. Production correctly rejected the unavailable
publisher. The fixture now reconstructs the publication in serialized execution,
verifies a changed nonzero lifetime and refuses to count unadvertised publishes.
Corrected native `/var/tmp/slaaps-native-1uyz9w8n`, runtime
`/var/tmp/slaaps-native-runtime-i8ve5y_t`, passes 3,162 control, 34 input and
443 actual-mixer checks, seven supervisor lifecycles and six restart/BSON cases.
The production preparation scenario uses real Commander, supervisor and mixer
endpoints with six synthetic pilot/estimator topics; the fixture never publishes
Commander authority. It verifies centered/low-throttle/numerical-state gating,
input loss, publisher replacement, configuration/routing/expiry cancellation,
explicit mode re-entry and apply not retrying a rejected entry. Prepared sessions
remain disarmed with zero admission masks and zero physical outputs. Existing
active-worker tests use separate explicit synthetic authority and capture-only
callbacks. The initial failed evidence is retained.

Board `manual-entry-final-20261022` links and passes independent source/package/
ELF/capacity verification: image 1,824,724 bytes, flash headroom 141,356 bytes,
AXI SRAM 111,036 bytes and SRAM4 2,048 bytes. Existing five ARM probe groups and
an additional supervisor probe pass. Endpoint objects are 6,984 bytes each;
`sizeof(Slaaps)` is 7,568 bytes. Exact supervisor frames are Run 304 bytes,
updatePreparation 400 bytes and observePrearmInput 160 bytes, on the separate
6,500-byte supervisor queue. These are static measurements, not runtime stack or
timing qualification. The conservative identified supervisor chain is 2,496 bytes
of 6,500; physical MAIN/AUX chains remain 2,648/2,592 of 2,800 before unmeasured
overhead. See `resource-summary.json`, `stack-chain-review.json` and
`production-recheck.json` in that board stage: 41 current changed production
files match, and both old module-path removals remain absent. The board snapshot
retains the original native-only fixture hash; its correction is explicitly
recorded separately and does not participate in the board target. The staged
package SHA-256 is `c42fb41f442f1c23da6287e4675ae1fb11a1e860d8625c7204ee55e42c5ac309`.
Source manifests identify the dirty checkout; the embedded tag remains older.
The corrected build also revalidates all ten event definitions in
`manual-entry-events.json`; actual QGC GUI operation is unverified. No hardware
has been operated.

## October 22: shared pilot input configuration

The shared profile now explicitly selects RC-only or pinned MAVLink-only with
`SLA_IN_SOURCE`; no automatic cross-source fallback is introduced. RC requires
identity -1/0/0, while MAVLink requires the selected instance/system/component.
`SLA_R_INPUT`, `SLA_P_INPUT` and `SLA_Y_INPUT` select a unique permutation of the
three PX4-selected attitude axes. Existing body-axis signs, rate limits, deadband
and centering hysteresis remain in effect. Native metadata exposes source and
axis selectors as QGC enums; an actual QGC GUI session has not been verified.

`SLA_THR_LOW` and `SLA_THR_HIGH` define the raw selected throttle values for zero
and full EDF request. Descending endpoints implement reversal. Manual control
requires finite unequal endpoints in [-1,1] and rejects samples outside their
calibrated span. Raw source, timestamps, duplicate mutation and freshness are
checked before conversion; transformed values cannot conceal a changed raw sample.
Low-throttle entry is evaluated after conversion by the existing shared EDF owner.

The same permutation/signs feed the bench body-axis mix, and `SLA_IN_AGE` now
controls bench input freshness instead of an ignored parameter with a fixed
500 ms timeout. Legacy 44-token bench profiles retain their old identity mapping,
signs, MAVLink policy and 500 ms default. Shared SLAAPS profiles require explicit
source, axis and sign choices; existing accepted markers cannot silently authorize
new defaults. Servo-only bench validation does not require EDF throttle endpoints.
This connects configuration to existing controllers and the worker; it does not
add production supervisor admission or complete operational commissioning.

Host evidence `/var/tmp/slaaps-configuration-_8wwi952/result.json` passes 2,178
checks in each Debug, Release and ASan/UBSan build. The real PX4 metadata pipeline
validates 179 vehicle fields and six apply/acceptance fields, including enum labels.
Tests cover source/identity rejection, mapping consistency between bench and
flight, configured bench-age boundaries, reverse throttle and deliberate low entry.
Existing shared bench/controller CMake suites also pass Debug, Release and
sanitizers at `/var/tmp/slaaps-input-compat-jidoil3y`; no legacy defaults changed.
Native `/var/tmp/slaaps-native-3f1_conj`, runtime
`/var/tmp/slaaps-native-runtime-jlnmav1u`, pass 2,983 control, 34 input and 443
actual mixer checks, seven supervisor lifecycles and six accepted-profile restart
scenarios with BSON export/import. The three actual-worker/mixer sessions use
RC-only input, permutation {2,0,1}, and descending throttle endpoints {.75,-.5}.
Assertions verify raw yaw produces the expected body-roll canard signs and low
then raised throttle produces stopped then slewed EDF PWM. All output callbacks
are capture-only with synthetic authority; production supervisor still grants none.

Current [board verification](../build/slaaps_firmware/input-mapping-final-20261022/independent-verification.json)
passes source/ELF/package/capacity checks: image 1,820,732 bytes, flash headroom
145,348 bytes, linked AXI SRAM 111,036 bytes and SRAM4 2,048 bytes. Each bank
endpoint is 6,976 heap bytes, including the unchanged 1,560-byte capture workspace;
MixingOutput is 1,200 bytes. All five exact ARM probe groups were rerun. The
[stack-chain review](../build/slaaps_firmware/input-mapping-final-20261022/stack-chain-review.json)
identifies MAIN/AUX activation chains of 2,648/2,592 bytes against 2,800, leaving
152/208 before unmeasured runtime/libc overhead. Runtime stack and timing remain
unqualified; these figures cannot establish safe flight operation. No hardware
was operated, and no flight mode availability was enabled.

The next supervisor step can observe deliberate disarmed manual intent without
weakening availability: UserModeIntention already allows disarmed selection,
while fresh `vehicle_status.nav_state_user_intention` identifies the requested
mode. This does not supply measured-condition or recovery admission.


Updated 2026-10-22 by Codex toward the [airframe goal](SLAAPS_AIRFRAME_GOAL.md).
The current milestone is below; earlier dated sections retain their original
evidence and limitations. Software progress is not vehicle or flight acceptance.

## October 22: production manual output endpoints

Real MAIN/AUX `MixingOutput` endpoints now connect existing preparation,
activation/cancellation, worker commands and handled acknowledgments at the
serialized output boundary. The bank checks its physical driver token, complete
observed routing, exact accepted parameter configuration, selected mode and
independent current Commander/local guards. A separate manual-permission topic
binds the supervisor publisher lifetime and original bounded expiry; preparation,
activation, command and reply identities remain bound to their own publishers.
Replacement, malformed/stale data, configuration/routing changes and partial
activation cancel or latch the session; a repaired snapshot cannot resume it.
The original preparation deadline cannot be renewed by duplicate proposals.

Calibrated PWM microseconds go directly to the existing driver's `updateOutputs`
callback. A claimed SLAAPS bank suppresses ordinary output processing. The
endpoint validates authority immediately before the callback and again afterward,
including elapsed time, current configuration/inventory and the exact saved ticket.
A true callback plus successful final validation is required before a handled ACK
is published. A false callback is rejected; callback success is software handling,
not electrical feedback. Already handled retained commands produce no repeated
write or ACK, while their original expiry continues to be checked.

Fresh explicit disarm, kill, lockdown or termination uses the stop frame retained
from the accepted preparation: configured disarmed PWM, or failsafe PWM on
termination. Armed configuration corruption cannot replace that frame. A failed
stop callback remains pending and can retry only under a later fresh explicit
inhibition. Kill release while still armed cannot resume the cancelled session;
a new deliberate preparation is required. Missing or denied permission while
still armed remains `RecoveryRequired`, without synthesizing disarm or inventing
a cutoff/held-thrust policy. Worker ownership, expiry and all-bank handling remain
independent requirements.

The production supervisor owns/advertises this permission lifecycle but publishes
denied permission only. It still does not admit or start the manual worker. All
three registered mode availability masks remain zero. Its actual disarmed
preparation path remains connected to both real mixer endpoints after accepted
manual apply or unchanged accepted-profile restore. Positive permission,
selected-input entry and deliberate mode/arm activation are the next production
boundary; the output endpoint implementation does not complete stabilized manual
flight. Measured condition/recovery policy and broader acceleration/commissioning
requirements remain unresolved.

Recorded host package `/var/tmp/slaaps-configuration-gjk_4u_e` passes 2,075 checks
each in Debug, Release and ASan/UBSan, with native metadata for 179 parameters.
It predates later native endpoint/subscription edits and is not a fresh rerun of
all final sources. The current native build `/var/tmp/slaaps-native-q82yxwli`,
runtime `/var/tmp/slaaps-native-runtime-7uoaroz6`, passes 2,880 control checks,
34 selected-input checks, 419 actual-mixer-output checks, seven supervisor
lifecycles and six accepted-profile restart/BSON export-import scenarios.
Native ELF SHA256:
`aa67d92288f826cb6cea75f5bdc9de6aa94380079a9edd8670a69e94a4dde4e8`.

The native output scenarios use the actual worker and actual MAIN/AUX
`MixingOutput` with labeled synthetic authority and capture-only callbacks.
They cover both-bank activation/handling, exact calibrated frames, rejected
handling, final-write faults, expiry/replacement, retained-command suppression,
accepted stop-frame retention, failed-stop retry and deliberate re-entry.
The actual production supervisor preparation scenarios remain non-actuating and
observe no output callbacks. Native PWM metadata comes from actual driver YAML;
this fixture does not compile the physical PX4IO driver or exercise IO registers.
The later IO-only correction therefore does not supersede its native-compiled evidence.

The PX4IO correction independently forces configured or pending SLAAPS EDF
DISARMED and FAILSAFE register writes to zero, including the high DIS=MIN case.
Readiness additionally reads each EDF channel's actual DISARMED/FAILSAFE register
and requires both to be zero alongside mixer disarmed/actual failsafe values.
PX4IO firmware ignores zero FAILSAFE writes: an old nonzero register can persist,
and readiness now rejects that state. Read failures and mismatches also reject it;
the raw FAIL=65535 sentinel remains invalid. This changes neither the IO protocol
nor airborne recovery policy. Added register-read timing is unqualified.

Focused source-extracted PX4IO evidence
`/var/tmp/slaaps-px4io-disabled-final-uk6akqrd/manifest.json` passes 658 checks each
Debug/Release/ASan-UBSan using the actual update methods and exact Run inventory
block with injected IO semantics. Eight-channel fixtures cover current/pending
EDF assignment, high DIS=MIN, ordinary output preservation, ignored zero FAILSAFE
writes, actual readback/parameter mismatches and read failure. The fixture compiles
with `-Wfloat-equal -Werror`; the tested physical-driver source SHA256 is
`f04792da673021ad17e5eff24ae566d3c814e70375d95fdc0162ac69160a870e`.
An initial board compile rejected a float equality; its retained
[failed attempt](../build/slaaps_firmware/production-manual-output-io-guard-final-20261022/failed-attempt-1.json)
predates the exact-zero range comparison and this final fixture, which supersedes
the earlier 642-check fixture. No physical IO or PWM operation is claimed.

Final board stage
[production-manual-output-io-guard-final-20261022](../build/slaaps_firmware/production-manual-output-io-guard-final-20261022/independent-verification.json)
passes independent current-source, ELF/package and capacity checks, including the
physical PX4IO driver: image 1,818,740 bytes, flash headroom 147,340 bytes, linked
AXI SRAM 111,036 bytes and SRAM4 2,048 bytes. This adds 120 bytes flash versus the
[retained predecessor](../build/slaaps_firmware/production-manual-output-final-20261022/independent-verification.json).
Board receipt SHA256:
`d8a61bcd2543a4531c49ef0975aa46564c78437d766bef4a6251da5df537e4b4`.
Each heap bank endpoint is 6,920 bytes, including its 1,560-byte capture workspace;
`MixingOutput` is 1,200 bytes. Dynamic allocations are additional to linked SRAM.
The final [stack review](../build/slaaps_firmware/production-manual-output-io-guard-final-20261022/stack-chain-review.json)
records identified MAIN/AUX activation chains of 2,632/2,576 bytes against the
2,800-byte `hp_default` stack, leaving 168/224 bytes before unmeasured
runtime/libc/uORB overhead. The fresh physical-driver probe finds PX4IO Run at
144 bytes, 24 more than its predecessor; known workqueue runner frames remain
included. Resolve or qualify this pressure before enabling production outputs;
these sums are not runtime high-water or timing qualification.

The dedicated board variant disables unused stock FW/MC controllers and the
hover-thrust estimator, which SLAAPS startup does not start. Control allocator
remains compiled to supply stock QGC actuator metadata, without being started as
an output owner. The first manual-output build failed because removing it omitted
the metadata mixer section; the next compile caught a shadowed local. Both
[attempt logs](../build/slaaps_firmware/production-manual-output-20261022/) and the
intermediate stack/final stages remain retained. Earlier preparation and board
failures below remain evidence of their original revisions, not current passes.
Dirty source manifests and independent verification identify the overlay; the
retained older embedded Git tag alone does not identify current firmware sources.
No hardware was flashed, energized or operated; no boot, electrical, QGC GUI or
flight acceptance is claimed. Changes remain uncommitted pending user DCO review.

## Historical record

2026-10-20, Codex. First software increment toward the
[airframe goal](SLAAPS_AIRFRAME_GOAL.md); no native mode or physical output
integration is claimed by the host evidence below.

## October 22 previous checkpoint: production supervisor and mixer preparation

Recorded scope at this checkpoint; the current milestone above supersedes its integration boundary.

The production supervisor now discovers real MAIN/AUX preparation-ready and
handled-ack publisher lifetimes, binds the observed physical driver tokens and
routing, and submits one all-bank preparation transaction after an accepted
manual configuration apply or disarmed restore. `MixingOutput` owns a bank
endpoint on each supported PWM bank; the endpoint checks its physical token,
fresh disarmed Commander state, local kill/lockdown/termination guards, current
routing and the exact accepted parameter snapshot before replying ready.
The parameter capture/acceptance adapter moved from `src/modules/slaaps` into
`src/lib/slaaps` so supervisor and driver use the same typed configuration.

Preparation polls even without ordinary output-function traffic. Duplicate
proposals cannot renew their one-second deadline. Expiry, configuration or routing
changes, arming, publisher replacement and supervisor shutdown cancel readiness;
a rejected transaction requires deliberate new apply. Status reports the exact
session, deadline, bank mask and preparation state. The existing one-second
non-actuating transaction bound is not qualified controller/output timing.
Aligning preparation with deliberate mode/arm entry remains necessary; this
diagnostic transaction is not persistent ready-to-arm permission.

This connects production supervisor and mixer preparation, not activation or
actuator operation. Neither registered modes, accepted markers nor ready replies
grant output authority. All mode availability masks remain zero. The next boundary
is a lifetime-bound trusted permission snapshot between queues, existing bank
activation/cancellation, selected-worker entry, and exact handling acknowledgment
around `MixingOutput`'s physical write. `RecoveryRequired` must not silently become
cutoff or held thrust. Measured-condition admission and recovery policy remain
unresolved, alongside the broader manual/acceleration/commissioning requirements.

Current host `/var/tmp/slaaps-configuration-cq5n5q_f` passes 2,066 checks each in
Debug, Release and ASan/UBSan, including native metadata for 179 parameters.
Native `/var/tmp/slaaps-native-45pspsnu`, runtime
`/var/tmp/slaaps-native-runtime-x96410g0`, pass 2,803 control checks, 34 input
checks and seven supervisor lifecycles. The real supervisor and both actual
`MixingOutput` instances complete preparation, reject configuration/routing
changes and expiry, and re-enter after deliberate apply. Capture-only driver
interfaces observe zero outputs throughout; the fixture cannot exercise physical
PWM. It uses synthetic vehicle data and native PWM parameter metadata generated
from the actual physical-driver YAML. The initial native attempt failed because
the native target lacked those PWM parameters; the metadata fixture repaired that
missing test dependency, and the final run above passes.

Final board stage [production-preparation-final-20261022](../build/slaaps_firmware/production-preparation-final-20261022/independent-verification.json)
independently passes current-source, ELF/package and capacity checks: image
1,960,040 bytes, flash headroom 6,040 bytes, linked AXI SRAM 111,356 bytes and
`SRAM4` 2,048 bytes. Each bank endpoint allocates 5,568 bytes on the heap, including
its 1,560-byte capture workspace; these dynamic allocations are not included in
the static SRAM figures. Driver-queue scratch reuse reduces the bank poll frame
from 440 to 176 bytes; direct control-configuration validation reduces its frame
from 1,272 to 848 bytes. The [stack-chain review](../build/slaaps_firmware/production-preparation-final-20261022/stack-chain-review.json)
includes the retained 160-byte workqueue runner frame: known deepest MAIN/AUX
call chains total 2,664/2,632 bytes against the 2,800-byte `hp_default` stack,
leaving only 136/168 bytes before unmeasured runtime/libc overhead. This is tight;
resolve and qualify the stack pressure before enabling outputs. These are static
estimates, not measured runtime high-water marks or timing qualification.
Final source manifests account for the shared adapter move and
deleted module-local files; the older embedded Git tag alone is not the source
identity. No board boot, electrical acceptance or hardware operation is claimed.

## October 22 previous checkpoint: activation transaction and accepted-profile persistence

The preparation coordinator now sends activation/cancellation over the separate
`slaaps_manual_transition` topic using the existing preparation identity schema.
Bank endpoints bind its advertisement lifetime, require independently supplied
trusted permission, publish exact activation replies, and revoke on exact-session
cancellation or publisher replacement. Partial activation cannot complete; timeout
or a rejected peer cancels every participating bank. Preparation readiness is
consumed before activation so the first bank's withdrawn ready reply cannot
invalidate the other bank's activation. Historical activation deadlines are not
ongoing output leases after consumption; live worker/bank dispatch still owns
health, command expiry and handling acknowledgments.

The supervisor now restores an unchanged accepted configuration on fresh disarmed
startup. Four native parameters retain fingerprint words, validation scope and
accepted apply counter. Restoration revalidates the whole typed configuration;
unapplied edits and incomplete markers fail closed. Armed startup cannot defer
restoration until later disarm. Explicit apply is still required for later edits.
These markers record accepted software configuration, not calibration or vehicle
qualification. Ordinary PX4 autosave supplies durability; the marker write is not
an immediate media flush. Export/restore must include all SLAAPS parameters.
Native metadata now contains 179 parameters (173 vehicle fields, two apply/scope
controls, four acceptance markers). Stock QGC parameter support is implemented;
a custom graphical configuration editor and actual GUI acceptance are not.

Fresh native build `/var/tmp/slaaps-native-4y_gqklw`, runtime
`/var/tmp/slaaps-native-runtime-w29p1az1`: 2,803 control checks, 34 selected-input
checks and seven supervisor lifecycles pass. Six new activation scenarios cover
both-bank completion, partial cancellation, missing reply timeout, transition
publisher replacement, cancellation after proposal expiry, and independent AUX
permission denial after MAIN activation. Profile checks cover three accepted
restarts, persisted unapplied edits, incomplete acceptance markers and actual
PX4 parameter BSON save/load. This is module restart plus storage export/import,
not electrical power-loss qualification. Native ELF SHA256:
`6e4358702ef08f07f52f12e2e718f71dcc93600fcf8cc7640a00674efe2c1309`.
The first native compile caught mismatched generated topic names; its unsealed
snapshot was explicitly refreshed and the incremental build/runtime pass is final.

Final host package `/var/tmp/slaaps-configuration-z4au8guu` passes 2,066 checks
each in Debug, Release and ASan/UBSan, with all input hashes checked after the run.
Current board stage [activation-profile-final-20261022](../build/slaaps_firmware/activation-profile-final-20261022/independent-verification.json)
passes ELF/package/source/capacity verification: 1,944,928-byte image, 21,152 bytes
flash headroom, 111,356 bytes linked AXI SRAM (runtime heap/stack not qualified).
This is +1,560 bytes flash versus the prior active-loop-headroom image.
Package SHA256 `fcb9d84117804e14e11120c9a604d55c2ad18d5143733976ea07a1bb068e812d`.
The incremental source manifest derives from the retained `472a7d6786` build
and explicitly overlays current files; the embedded tag retains that parent
identity. The dirty build manifest, not that old tag alone, identifies sources.
Volume `px4-slaaps-activation-profile-final-20261022` and preliminary stage are
retained. Transaction endpoint headers are not yet instantiated by production
drivers; this resource result does not predict their eventual integrated cost.
All validation jobs are terminal; no board boot or hardware acceptance is claimed.

Production admission and physical driver attachment remain absent. The native
transaction scenarios use synthetic permission and memory-backed uORB, not
physical drivers. Final handling must attach at MixingOutput's serialized write
boundary, with coherent configuration/inventory, independent trusted permission,
physical driver token comparison and actual completion acknowledgment. No mode
availability flag was enabled. Manual flight, acceleration, scheduled airflow
control and the expanded EDF commissioning path remain incomplete. No hardware
was flashed, energized or operated.

## Manual propulsion request adapter

The user authorized flight-capable control with propulsion. `ManualEdfInput.hpp`
now connects selected manual throttle to the common `EdfOwner` request contract
in host tests. It reuses `ManualControlInput` for MAVLink instance/sender,
freshness, finite range and sample-continuity validation. PX4 throttle -1 means
zero command; +1 means full command; center means 50% and is rejected by the
owner's low-throttle entry check.

Requests retain their original sample timestamp and a deadline bounded by both
manual freshness and the output lease. Held or republished samples generate no
new request. Invalid input latches the adapter inactive and preserves the caller's
request; the future worker must handle rejection as input loss, rather than resend
the preserved request. Reconfiguration is not flight admission: the common owner
independently enforces session replay, permission, low entry, slew and expiry.
Only throttle is validated here; the attitude controller must validate its axes
before the worker admits a combined control cycle.

Evidence `/var/tmp/slaaps-configuration-72_4fyda` passes 1,554 checks each in
Debug, Release and ASan/UBSan, plus actual PX4 metadata generation (175 parameters).
The 53 new checks cover the adapter with the real EDF owner, source rejection,
malformed/stale/pre-grant/future/overflow samples, immutable identity, held-input
expiry, centered-throttle entry rejection and fault latching.
This is synthetic host evidence. The native supervisor does not yet instantiate
the adapter or publish propulsion. Prior native and board artifacts remain bound
to their original sources; this increment does not requalify those artifacts.

## Combined manual worker core

`src/lib/slaaps/ManualWorker.hpp` composes the actual stabilized attitude/rate
controller, allocator, selected throttle adapter and shared EDF owner. Entry
requires the applied configuration fingerprint, centered attitude sticks, low
throttle and a new common-owner session. The supervisor retains the EDF owner
across worker replacement and must serialize stop/ownership transfer.

A successful cycle returns four canard commands and the EDF command together.
Its deadline is the earliest of propulsion expiry, attitude/rate sample expiry
and maximum control-cycle gap. Early polls produce no replacement output.
Failures latch the worker inactive and revoke the shared owner; the caller must
not republish preserved output. RecoveryRequired remains an explicit unresolved
supervisor flight-loss decision, not an automatic airborne thrust-cut policy.
Fixed simulated thrust metadata is cleared and never used as propulsion demand.

The worker now requires an explicit routing/lifetime binding and one software
acknowledgment from each participating MAIN/AUX bank for every combined frame.
Each reply must match session, source, configuration, routing, driver lifetime,
combined sequence and calculation timestamp, with a nonfuture acceptance time.
Partial replies keep the worker waiting; duplicate, malformed, late or unexpected
replies latch a fault. Every cycle checks the live binding, so routing changes or
driver replacement cannot inherit the old command stream. The future transport
must verify the actual publisher; fields supplied in a message are not proof of
publisher identity or electrical application.

While waiting, the existing controller's monitor validates input and estimator
health without integrating PID or advancing allocation history. The throttle
adapter continues continuity checks and retains the latest fresh request with
its original timestamp. The EDF target is not changed until all bank replies
arrive. The combined deadline is never extended by polling or acknowledgment;
expiry revokes the worker even if a late reply would otherwise match. Same-time
replies may complete acknowledgment but cannot cause a second integration.
This is producer-side protocol behavior; independent driver expiry and native
transport remain required.

The native supervisor does not instantiate this worker yet. Output acknowledgment,
per-bank admission and independent expiry still need integration before any
publication is eligible. The existing one-condition allocation model still needs
measured airflow/control-authority admission; variable throttle alone cannot prove
that the measured canard operating condition remains valid. Tests supply synthetic
permission and configuration and do not establish flight acceptance.

Host evidence `/var/tmp/slaaps-configuration-fsfptqs2` passes 1,611 checks each
in Debug, Release and ASan/UBSan plus the 175-parameter metadata checks. The 57
new checks exercise combined allocation/propulsion, entry, cycle timing, session
replay and faults in either subsystem. No native/board rebuild is claimed.

Latest host evidence `/var/tmp/slaaps-configuration-b92d75tq` passes 1,688 checks
each in Debug, Release and ASan/UBSan and 175-parameter metadata validation.
The 77 added checks cover per-bank acknowledgment, pending live-health faults,
routing/driver replacement, deadline enforcement and retained throttle requests.
Previous evidence remains scoped to its original sources.

## Independent manual bank consumer

`ManualOutput.hpp` now holds the shared combined-frame, bank-binding and
acknowledgment types. `ManualOutputBank.hpp` implements one independently clocked
consumer per participating MAIN/AUX bank. Entry requires a new session, a complete
validated vehicle configuration, a matching fingerprint, explicit driver/routing
lifetimes and a valid complete routing inventory. Failed entry consumes the
session; neither frames nor acknowledgments can grant a session.

Each dispatch checks permission, immutable routing/calibration and the stored
command deadline. It validates the combined frame and maps calibrated canards
and forward-only EDF commands to that bank's assigned PWM channels. Logical
servo reversal is applied once through the measured endpoint calibration.
Duplicate/out-of-order frames, invalid commands, permission loss, driver/routing
changes and expiry latch faults. Polling without new traffic does not renew the
lease; initial silence is bounded too. Armed faults return RecoveryRequired,
while disarm/kill/lockdown/termination/bench return Inhibited. Neither disposition
is a command to write zero-valued PWM data or an implemented airborne fallback.

The current API separates preparation from handling confirmation: `accept()`
returns no acknowledgment. `confirmHandled()` releases one only after the trusted
driver adapter reports successful handling of the exact sampled ticket. Native
transport must bind the actual publisher, provide fresh complete inventory, and
serialize validation, writes and confirmation with routing/permission changes. This code does not supply that native adapter,
cross-bank atomic admission, electrical feedback or a flight-loss policy.
Producer-side control and EDF owners still enforce slew; independent driver-side
health and final actuation acceptance remain integration work.

Latest consumer evidence `/var/tmp/slaaps-configuration-kq6bnc5k` passes 1,761
checks each in Debug, Release and ASan/UBSan plus 175-parameter metadata validation.
The 73 added checks connect the real worker to both consumers, including nonzero
slewed propulsion PWM, mixed-bank routing, reversed calibration, independent
expiry and malformed-frame/live-permission faults. Initial consumer evidence
`/var/tmp/slaaps-configuration-frwzp7ib` remains retained (1,757 checks); the later
run adds four checks for nonzero propulsion through successive acknowledged frames.
No native/board rebuild or physical output test is claimed.

## Driver-handling confirmation

`ManualOutputBank::accept()` now prepares a candidate without producing an
acknowledgment. The sampled output carries session, source, configuration,
routing/driver lifetimes, command timestamp, sequence, channel mask, deadline and
PWM payload. The trusted driver adapter reports successful handling of that exact
ticket through `confirmHandled()`. Live permission, routing and expiry are
rechecked before an acknowledgment is released. Its acceptance time is the
confirmation time, not the earlier preparation time.

Failed handling, altered PWM or metadata, duplicate confirmation, stale-session
tickets and changes during preparation/handling latch faults without modifying
the caller's acknowledgment. A new frame cannot replace an unhandled candidate.
The host fixture explicitly uses a memory-only successful-handler stand-in;
there is still no physical-driver adapter or electrical feedback. Caller reports
of success must come from the real serialized driver path before flight admission.

Host evidence `/var/tmp/slaaps-configuration-5b7km9ro` passes 1,821 checks each in
Debug, Release and ASan/UBSan plus actual parameter metadata generation. The 60
new checks cover delayed and failed handling, ticket integrity, stale sessions,
premature/duplicate confirmation, pending replacement and live permission/routing
changes. Earlier evidence remains bound to the pre-confirmation API.

The pending native rebuild completed before the user-requested pause. After
resume, its binary and manifest hashes were verified, and the current consumer
header and test source were confirmed identical to the sealed snapshot. Native
build `/var/tmp/slaaps-native-cldwgvji` and runtime
`/var/tmp/slaaps-native-runtime-5gglvtox` pass all 1,821 control checks and 119
successful runtime commands, including the supervisor lifecycle/routing checks.
Binary SHA-256:
`f26dbe7e9694de9a812815f8a3c776e2747581faa6d065b5e39aa09e12e34182`.
All 77 source hashes bound by the host receipt still match the current worktree.
The rebuilt native fixture still has no driver transport or physical output.

The native builder now supports explicit `--refresh-file` entries with
`--test-from`: each refreshed path is recorded with before/after hashes in the
fork's manifest, while the sealed parent remains unchanged. Paths are normalized
inside the workspace. This rebuild explicitly refreshed the bank consumer header
and shared test source; prior evidence is retained under its original identity.

## Manual command and acknowledgment wire boundary

`SlaapsManualCommand` transports one combined manual frame, retaining calculation
time, original deadline, combined and EDF request sequences, session/source/
configuration identities, routing/driver lifetimes, logical canard values and
normalized EDF output. Publication time is separate and cannot renew the command.
`SlaapsManualAck` uses separate `slaaps_manual_ack_main` and
`slaaps_manual_ack_aux` topics so one bank cannot overwrite the other bank's reply.
Acknowledgment publication, calculation and driver-handling times remain distinct.
These are internal control messages, not new MAVLink streams or QGC parameters.

`ManualTransport.hpp` encodes/decodes these generated schemas. It rejects missing
identities, inconsistent/future times, expired commands, unsupported bank masks,
wrong-bank replies and nonfinite/out-of-range commands while preserving caller
output. Stateful replay, permission and lease-bound checks remain in the worker
and bank consumers. The codec neither authenticates a publisher nor grants a
session. Payload source fields are not proof of actual publisher identity.

The older atomic uORB publisher-revision support is currently gated on
`CONFIG_MODULES_RESEARCH_STABILIZED_MANUAL`; SLAAPS does not silently inherit it.
Production transport must bind the publisher and copied payload atomically,
reject publisher replacement and connect a real serialized driver adapter before
mode availability can change. Current native round-trip fixtures exercise uORB
publication/copy only, with synthetic timestamps and no actuator application.

Host evidence `/var/tmp/slaaps-configuration-qr1u1rlb` passes 1,879 checks each in
Debug, Release and ASan/UBSan, including 58 new codec checks and actual generated
message headers plus the unchanged 175-parameter metadata checks. Valid combined
worker frames traverse the codec in the bank fixture. Invalid-input consumer
fault tests retain their direct path so the consumer's fault latch is also tested.

Native wire evidence: `/var/tmp/slaaps-native-mah0jeye`, runtime
`/var/tmp/slaaps-native-runtime-72xsqqdr`. The suite passes 1,886 checks: the
1,879 shared checks plus seven native uORB publication/copy/decoding checks.
All 116 runtime commands succeed, preserving supervisor configuration/routing
and availability gates. Binary SHA-256:
`db2926843032b2dbc0f6af4b186e65ecaced358b78ee185532efbfe4307f0cef`.
Binary/manifest hashes, the changed schema/codec/test sources and all 80 host
source hashes were verified against the retained receipts. The native receipt's
no-driver-transport scope remains accurate: these are message round trips, not
publisher-authenticated driver application or flight qualification.

## Native control-core verification

The explicit `px4_sitl_slaaps-test` target now compiles the shared configuration,
worker and bank-consumer suite as `slaaps_control_test`. It links the real PX4
SLAAPS libraries and generated headers under native warning/optimization flags.
The command is guarded by the existing POSIX wall-clock-only test Kconfig option
and is not present in vehicle targets. It uses synthetic inputs and publishes no
actuator command. This is native execution of the portable pipeline, not native
uORB transport, driver dispatch, electrical output or plant qualification.

`build_native.py --wall-clock-test` stages the current checkout directly for this
test target. The source overlay now includes the shared test source. The runtime
verifier requires the command's success marker and at least 1,761 checks before
running the existing supervisor checks. The native compiler exposed three exact
float assertions in the test harness; they now use explicit bitwise equality,
retaining strict zero/preservation tests without relaxing compiler warnings.

Evidence: build `/var/tmp/slaaps-native-wg2bcgil`, runtime
`/var/tmp/slaaps-native-runtime-965uvaxu`. Binary SHA-256:
`5de9612fa3ed5559c20598f674d265ac0c3bc82badbd6a369cd8b374f6ebb3a9`.
The native command passes 1,761 checks; all 118 runtime commands succeed.
Registration, zero availability masks, explicit configuration application,
routing permutations/faults/expiry and three supervisor restarts pass on that
binary. The retained failed-compile log and source-manifest revision record bind
the assertion correction. Binary and manifest hashes were independently rechecked.
The same final test source passes 1,761 checks each in Debug, Release and
ASan/UBSan at `/var/tmp/slaaps-configuration-nxd0ulhp`, with the 175-parameter
metadata checks. Prior host evidence `qwmc48dy` predates the assertion correction.
No board firmware rebuild, physical operation or QGC GUI acceptance is claimed.

## Native integration in progress

Airframe 22001 and dedicated startup, native mode supervisor, parameter snapshot
adapter, commander availability guard and separate SLAAPS board targets are now
present. `SLA_CFG_KIND` selects bench/manual validation and `SLA_CFG_APPLY`
requires an explicit counter increase after completed disarmed edits. Saved
counter values do not apply a configuration on supervisor restart. These two
controls bring the generated parameter count to 175; the vehicle schema remains
173 fields. Native module CMake now registers the metadata.

The supervisor registers three named external modes and requests direct-actuator
control flags. All availability masks remain zero until control/driver ownership
is implemented; registration is not operational bench or flight integration.
The commander guard rejects stock modes for this airframe and stale supervisor
status. Active bench/manual workers, EDF output, the acceleration algorithm,
land detection, driver write admission and QGC GUI acceptance remain pending.

### Driver-reported routing

The PWMOut and PX4IO drivers now publish effective PWM-bank observations through
`SlaapsOutputBank`. Reports include the driver lifetime token, current function
assignments, actual channel mask/rates and mixer calibration/reversal/off values.
PX4IO reads back rate groups and rates and reports serial servo output unsupported.
Publication occurs on the driver work queue before any work-queue switch.
The dedicated fmu-v6x path requires fresh MAIN and AUX reports, even if all user
assignments occupy one bank. Missing, duplicate, stale or unready banks invalidate
the observed routing. Unavailable hardware, OneShot/unsupported rates, duplicate
functions, unresolved failsafe defaults and calibration mismatches are rejected.

The supervisor checks the reported inventory against the applied configuration
and exposes `routing_snapshot_valid`, `inventory_fault` and `routing_fault`.
This is a diagnostic observation, not an atomic cross-bank output transaction,
proof that every possible non-PWM transport is absent, or a permission to write.
The final driver boundary still needs live routing revalidation, immutable
session handoff, expiry and acknowledgments. No SLAAPS function provider exists
yet, and all mode availability masks remain zero.

Host evidence `/var/tmp/slaaps-configuration-waxqub5y` passes 1,501 checks each
Debug/Release/ASan-UBSan, including 30 new report-to-routing tests and the NuttX
portability correction to the EDF limiter. Native production snapshot
`/var/tmp/slaaps-native-y8no43f3` links. Its explicit test overlay
`/var/tmp/slaaps-native-8v54g2g_` adds a synthetic bank publisher available only in
the wall-clock test target. Runtime evidence
`/var/tmp/slaaps-native-runtime-q28ecapp` passes registration/apply/restart plus
valid cross-bank routing, duplicate/unavailable/miscalibrated/stale reports and
heartbeat expiry. It does not exercise a physical driver or actual QGC GUI.

### Board compile and resource evidence

The current dirty `slaaps-sitl` checkout at `472a7d6786` now builds target
`px4_fmu-v6x_slaaps`. Evidence is retained in
`build/slaaps_firmware/routing-20261020` and Docker volume
`px4-slaaps-routing-20261020`. Source comes from the sealed native snapshot plus
explicit `board_revisions` in `board-source-manifest.json`. Those revisions use
PX4 math instead of unavailable NuttX `<algorithm>`, avoid an unnecessary C++
limits dependency, and omit optional MSP video overlay and camera-gimbal control
from this dedicated target. Existing stock/bench/observer target configurations
are unchanged; no binary-equivalence claim is made for the shared new messages.

The initial link exceeded flash by 10,812 bytes. Final image: 1,952,100 bytes,
13,980 bytes flash headroom; linked AXI SRAM: 111,356 bytes. The `.px4` package is
1,850,960 bytes. `independent-verification.json` rehashes artifacts, decodes the
package, checks board ID 53 and binary equality/capacity, and confirms thirteen
changed production paths match the recorded sources. Board receipt SHA-256:
`a5f29150dc750781dcc704f18eab8a4320961eb16335e17239187576c244e592`.

Package:
`build/slaaps_firmware/routing-20261020/artifacts/px4_fmu-v6x_slaaps_472a7d6786_dirty_output.px4`.
Temporary WSL stage:
`/tmp/px4-firmware/px4_fmu-v6x_slaaps_472a7d6786_dirty_routing_20261020.px4`.
Package SHA-256:
`7ea5849775ddd7457246f5f46ce4f78c0ea662826e0a0ee9b3b9632c5c35499e`.
This is the current checkout's build, not a completed port to the known-working
e8ea98b404/NuttX 11 baseline. It has not been flashed or boot-tested and does not
establish physical output behavior. Further active integration must stay within
the actual remaining flash/stack budget.

Fresh host evidence after native compiler compatibility fixes:
`/var/tmp/slaaps-configuration-u9j4z0ws`, all 1,347 checks in Debug, Release and
ASan/UBSan, plus actual PX4 metadata generation for all 175 parameters.
Native build `/var/tmp/slaaps-native-dkdvdxzc` links successfully. Source revisions
record corrections to event parsing and exact PWM comparisons. The separate
wall-clock test variant (`boards/px4/sitl/slaaps-test.px4board`) builds from its
sealed source snapshot at `/var/tmp/slaaps-native-bmrdeobx`. This variant advances
time without a simulator; the normal SLAAPS SITL target retains lockstep.

`Tools/slaaps/verify_native.py` passes against that binary, evidence
`/var/tmp/slaaps-native-runtime-bc9732od`: three unique registered modes
(23/24/25), zero availability masks, explicit apply, edit invalidation retained
after reverting values, rejection of bench-only data for manual scope, recovery
after a new valid bench apply, and three stop/start cycles with slot reuse and
no saved-counter auto-apply. Native snapshots precede one whitespace-only final
formatter change to VehicleConfiguration.cpp. These are supervisor lifecycle
checks, not flight behavior, physical outputs or actual QGC GUI acceptance.

## Implemented

### Shared EDF request owner and acceleration development port

`EdfOwner` now defines the portable common propulsion-request path. One grant
binds a monotonically increasing boot session, source-instance token,
configuration-generation token and selected manual/acceleration mode. Requests
carry the same binding, increasing sequence/time and a bounded absolute deadline.
Manual requests use normalized [0,1] throttle; acceleration requests use newtons
converted through the measured five-point curve. Both require valid measured
configuration, fresh admitted/healthy armed permission and a valid curve
condition. These permission fields are supervisor evidence, not new QGC switches.

The first demand must satisfy the configured low-throttle entry threshold.
Rise/fall limits apply to elapsed owner time; a newly arrived demand cannot spend
slew budget from before its arrival. Polling never refreshes the request deadline.
Foreign, malformed, stale or expired requests latch revocation; restoring values
does not restore ownership. Replacement and failed replacement discard the prior
grant. The future driver handoff must separately confirm old-output revocation.

Bench, disarmed, kill, lockdown and termination produce `Inhibited`. Other
armed ownership/health/request loss produces `RecoveryRequired`, with no command
authority. This deliberately does not decide whether an airborne vehicle should
hold, stop or transition thrust. A qualified flight recovery policy is required
before the supervisor may set `flight_admitted`. Physical output attachment,
independent driver expiry, actual throttle adapter and acknowledgment handling
remain pending; this class is not an operational motor controller.

`AccelerationControllerPort.hpp` preserves the existing guarded canonical
body-FRD inertial-acceleration-at-CG request and selected-state types. Its output
is logical canard deflections plus EDF thrust in newtons; the latter must enter
`EdfOwner`. The default implementation is unavailable and preserves caller output.
It defines no alternate wire parser and never enables an acceleration flight mode.
Future controller development must keep request guard/session/time-map checks,
body-fixed behavior between updates and shared supervisor/driver ownership.

Host evidence `/var/tmp/slaaps-configuration-dmiir12d` passes 1,471 checks in
Debug, Release and ASan/UBSan, including 124 new EDF/port checks. The EDF tests
exercise both request units, low entry, causal rise/fall slew, original expiry,
session replacement, wrong owner/configuration, stale/replayed/malformed data,
health/condition/permission loss and unconditional bench/kill inhibition.
Fresh native target `/var/tmp/slaaps-native-xj9r0n6l` links and compiles the EDF
owner with PX4 flags. Receipt SHA-256:
`6403a722a0167d535faa685db4943754cc82240c3232225ba20856df49233382`.
EDF object SHA-256:
`4520995a53d6919f34e8f3f03a0198e4c963b262964dfb2912a986ea12a2b886`.
The supervisor does not instantiate it yet; object compilation is not evidence
of linked active propulsion or mode behavior.

`src/lib/slaaps` now supplies a typed vehicle configuration, adapters to the
existing `BenchControlOwner` and `StabilizedManualControl`, an immutable
configuration store, a measured EDF curve interpolation function and a
logical-to-physical routing validator. The library is registered in the PX4
library tree with `EXCLUDE_FROM_ALL`; the new SLAAPS module links it explicitly.

`VehicleParameters.def` is the single scalar schema. It generates 173 PX4
parameters in `src/modules/slaaps/module.yaml`, grouped by vehicle, individual
canard, axis, input/timing, control and EDF. They include units, descriptions,
ranges and deliberately unconfigured physical defaults. Generate or check
the metadata with:

```text
python Tools/slaaps/generate_parameters.py
python Tools/slaaps/generate_parameters.py --check
```

The actual PX4 YAML-to-C-to-XML/JSON toolchain accepts every parameter.
This prepares QGC parameter editing; it is not evidence that those parameters
are present in an installed firmware or visible in an actual QGC session.
The `slaaps` module registers the metadata in its CMake target.

Validation checks scalar bounds, calibration ordering, full inertia positive
definiteness and rigid-body principal-moment constraints, source identity,
timing, local physical effectiveness conversion, actual allocator rank/
configuration checks, controller configuration and a monotonic EDF curve.
The bench adapter does not require aerodynamic or propulsion data. It retains
the existing bench module's 13-degree maximum deflection contract.

Configuration candidates are immutable after activation. An observed change
latches invalidation even if the values later revert; explicit disable and a
new valid commit are required. Failed candidate replacement cannot silently
fall back to an older candidate. The store retains the mode class used for
validation. Its fieldwise comparison, rather than its diagnostic FNV hash,
determines equality. No hash claims authentication or measured acceptance.

The EDF helper maps thrust in newtons through a five-point, forward-only
measured curve at normalized commands 0, .25, .5, .75 and 1. It rejects
out-of-range demand instead of silently clamping it. Voltage/airflow
applicability and the propulsion owner must still be implemented. The
controller's legacy `fixed_simulated_thrust_n` report is set to zero and is
never interpreted as a propulsion request.

## Configurable channel mapping

Reserved logical function IDs are `SLAAPS_Canard1..4` = 10100..10103 and
`SLAAPS_EDF` = 10104. They are excluded from ordinary actuator testing.
Existing bench/stabilized function IDs and providers remain unchanged.
These new names have no output provider yet and must not be presented as a
working actuator path.

The routing validator accepts arbitrary unique physical pins across MAIN and
AUX in a supplied complete driver inventory. Calibration follows the logical
canard, not the inventory position. It checks physical output limits, center
and reversal against that canard's calibration; requires pulses to fit the
PWM period; rejects duplicate functions/pins, unsupported protocols and raw
motor/servo/passthrough functions that would bypass the intended owner.

Initial supported routing is PWM at at most 400 Hz, subject to subsequent
measured hardware limits. Other protocols are rejected explicitly. Canard
disarmed/failsafe settings currently require no pulse. EDF disarmed/failsafe
settings must agree and be either no pulse or the calibrated minimum pulse;
reversal is rejected. These are proposed software output boundaries, not
qualified airborne recovery behavior. Flight requires an EDF assignment;
bench may leave it unassigned.

The native inventory adapter must enumerate all drivers, including unsupported
active outputs. It must check shared timer configuration and actual supported
pins rather than assuming that every index representable in the portable
inventory exists on the board. Driver-side acknowledgments must cover every
bank in the validated plan. No native enumeration or physical routing test
has been performed.

## Verification

Run in the configured Linux/WSL environment:

```text
python3 Tools/slaaps/verify_configuration.py
```

Final evidence: `/var/tmp/slaaps-configuration-9d9e_9ht`.
`result.json` SHA-256:
`cf55d60132b0c67264ea4961c4137ba15175e748b7bb641f8d5863e8ea524b6e`.
Each Debug, Release and ASan/UBSan build passes 1,347 checks. Coverage includes:

- Missing/malformed data, every scalar field's invalid values and identity
  participation, invalid inertia, calibration, rank and thrust curves.
- Actual bench and stabilized-controller entry/cycles using the new adapters.
- Configuration edit/reversion, active replacement rejection and failed
  replacement invalidation.
- All 120 orderings of a mixed MAIN/AUX five-output inventory, plus duplicate,
  missing, incompatible and bypass assignments, reversal and EDF off values.
- Real PX4-generated output-function IDs and all 173 generated parameters.

The verifier records source hashes and snapshots for its declared inputs and
checks that they did not change during the run. These are host tests, not a
complete firmware source manifest, native runtime, GUI acceptance, timing
qualification or vehicle qualification. Prior finite passes and the initial
unsupported metadata-unit failure remain in their own `/var/tmp` directories.
New C++ files use the repository astyle configuration; changed tracked files
pass whitespace checks. No hardware was operated or firmware flashed.

## Architecture findings and next implementation

The six mode-message schemas inspected (`RegisterExtComponentRequest/Reply`,
`SetpointConfig/Reply`, `UnregisterExtComponent`, `ArmingCheckReply`) have no
diff between the working hardware baseline `e8ea98b404` and this checkout.
Both expose direct-actuator setpoint configuration. Commander implementation
and complete resource compatibility still require a real build.

Individual `param_get` calls do not produce a coherent multi-parameter
transaction. The backend has a recursive POSIX mutex / NuttX interrupt
critical-section `AtomicTransaction`. A native snapshot facility must use the
backend's synchronization, avoid allocation inside it, and measure its cost.
Do not substitute two unguarded scans or an asynchronously published
`parameter_update` count for an atomic snapshot. An explicit user apply
workflow is also needed to distinguish completed GUI edits from an in-progress
multi-field edit. Active edits must invalidate the session before new data is
used; the existing immutable store is ready for that integration.

Next implement that native parameter/apply adapter and the `slaaps` supervisor,
then the airframe/startup and named-mode lifecycle. Reuse the existing control
libraries; the legacy observer and bench executables intentionally reject
being enabled together. Provide servo-bench mode entry/exit through its
existing controller/module integration, one shared canard mapping, independent
output expiry, a single EDF owner and a deliberately unavailable acceleration
port. Complete mode requirements, stock-mode exclusion/failsafe behavior,
native integration tests, working-baseline board build/resource checks and
QGC validation remain required by the original goal.

Do not regard this structurally valid single-condition model as proof of
trim bias, robust authority, flight envelope, calibration provenance or
operational gains. The forthcoming measured data and production admission
policy remain separate gates. No additional user answer is needed to continue
the software integration.

## Atomic publisher-lifetime binding (2026-10-20)

The uORB advertisement revision/payload critical section is now enabled for
MODULES_SLAAPS as well as the existing research module. Ordinary builds with
neither option retain the original path. `BoundSubscription<T>` requires an
explicit expected revision supplied at admission, checks payload size, copies
payload and revision atomically, and latches rejection on missing or replaced
publishers. Re-admission is explicit; caller output remains unchanged on failure.
It cannot authenticate other writers sharing a uORB publisher handle and grants
no actuator permission. Production supervisor/driver attachment remains pending.

Final host evidence `/var/tmp/slaaps-configuration-attaje33` passes 1,879 checks
in each Debug/Release/ASan-UBSan configuration and 175 generated PX4/QGC parameter
metadata checks; all 81 input hashes match the final working sources.
Native build `/var/tmp/slaaps-native-uxm29537` passes compile/link; binary SHA256
`14d5658da8b232c3f8556de2429af3ec028afe70368e403f6ac71d9a6a267bfc`.
The first compile's topic-identifier mismatch was corrected; its log is retained
as `compile-attempt1.log`, and the source manifest records explicit revisions.
Runtime `/var/tmp/slaaps-native-runtime-uhnifmw3` passes 1,900 checks and 116
supervisor/runtime commands. Fourteen added checks cover invalid/zero binding,
payload size, withdrawal, replacement before first publication, identical-payload
hidden replacement, latched rejection, explicit re-admission and revoke. The
seven previous actual uORB round-trip checks remain included. All five modified
native source anchors match the sealed snapshot. No physical outputs, target
firmware rebuild, QGC GUI acceptance or flight qualification are claimed.

The goal record now explicitly captures the requested future geometry/mass/
constraints/wiring GUI workflow and its model-derivation boundary. Current
parameters describe a measured effectiveness model; geometry-derived allocation
and automatic controller tuning remain future implementation, not implied by
parameter metadata or configuration validation.

## Native manual bank transport integration (2026-10-20)

`ManualBankTransport<0/1>` now connects the combined command uORB topic to the
independent MAIN/AUX consumers and their separate acknowledgment topics. The
endpoint requires a supervisor-supplied command advertisement revision and the
existing explicit configuration/routing/permission grant. It preserves its
consumer across sessions and rejects copying of the endpoint object.

Every dispatch first polls permission, live routing and the original deadline,
including initial silence and unchanged latest traffic. Initial advertisement
without a payload yields no PWM candidate and can wait only until the existing
consumer deadline. `BoundSubscription::read` distinguishes that waiting state
from a changed lifetime; the older strict `copy` interface retains its rejection
semantics. Queue sizes other than one cannot bind.

A new frame is decoded and accepted once. Re-reading the exact wire snapshot
neither re-accepts it nor extends its lease; changing any byte under the same
sequence revokes the endpoint. Command and reply advertisement replacement are
checked before preparation/confirmation. Handling confirmation rechecks current
transport, permission and routing, then requires the exact sampled output ticket
before encoding and publishing the bank reply. Failed handling, invalid traffic,
failed publication or a changed ticket revoke the endpoint.

This is a native transport adapter, not a PWM/PX4IO hardware attachment. Its
caller must serialize dispatch, the actual driver write, and confirmation with
permission/routing updates, and supply a qualified recovery policy. Advertisement
revisions are lifecycle observations, not authentication of shared publisher
handles. Production supervisor admission and physical driver attachment remain
pending; mode availability stays zero.

Verification: host `/var/tmp/slaaps-configuration-xlqxumxs` passes 1,879 checks
in each Debug/Release/ASan-UBSan configuration and all 175 parameter definitions;
all 82 source hashes match. Native `/var/tmp/slaaps-native-92q_qjy_` compiles and
links, binary SHA256
`ff33119cb42975440f335a9e1fd9338fe303f2b0f7b0cd7a5f888c140628cb0d`.
Runtime `/var/tmp/slaaps-native-runtime-culvq__8` passes 1,982 checks, including
82 added checks across nine transport scenarios, and 114 successful runtime
commands. The verifier now requires at least 1,982 checks and labels the scope
`native_bank_transport_synthetic_no_physical_driver`. Both changed headers and
the native test source match the sealed build snapshot. No new vehicle firmware
build, physical outputs, QGC GUI acceptance or flight qualification is claimed.

## PWMOut handling result correction (2026-10-20)

Before attaching SLAAPS handling acknowledgments to physical drivers, review found
that `PWMOut::updateOutputs` returned true while uninitialized and discarded
`up_pwm_servo_set` errors. It now reports false for either condition. It still
attempts the remaining hardware-mask channels after a write error and retains
existing disabled-channel, rounding and OneShot-trigger behavior. This reports
register handling only; a partial write is not rolled back and electrical output
is not verified. The eventual owner must apply its qualified recovery policy.

`Tools/slaaps/verify_pwm_output.py` extracts the actual method verbatim, injects
register calls, and binds the method, fixture, verifier, low-level declaration and
STM32 implementation by hash. Evidence `/var/tmp/slaaps-pwm-output-ep6_7lz3`
reproduces nine false-success failures in committed baseline `472a7d6786` and
passes all 25 checks in each corrected Debug/Release/ASan-UBSan build. Every
channel is independently fault-injected; writes to later channels still occur.
This is source-method host verification, not a board firmware or hardware test.

Separate board verification is in progress in
`build/slaaps_firmware/pwm-handling-20261020`, using the sealed current native
snapshot plus explicitly recorded production-source refreshes. Do not treat that
stage as a successful firmware build until its board receipt is present and
verified. The previous routing-20261020 board artifact remains unchanged.

The first PWM-handling board link passed code/package integrity with a 1,952,468 B
image, 13,612 B flash headroom and 111,356 B linked AXI SRAM. Independent ELF/bin
comparison found two matching flash load segments and all current changed
production paths matched the source manifest. However, the container omitted the
original Git alternate-object path, and CMake embedded its fallback `v0.0.0` tag.
That artifact is retained for diagnosis, not staged as the accepted candidate.
Regenerating the header with the same fallback tag reproduced it; that comparison
alone does not validate the version choice.

The build helper now verifies the same resolved Git description used by CMake,
rejects fallback tags, reconfigures derived builds and verifies the generated
version header before sealing. `Tools/slaaps/verify_board.py` independently checks
artifact hashes, package/ELF/bin equivalence, board/linker capacity, embedded tag
and current changed production-source hashes. ELF comparison was checked against
the preserved earlier board image and rejects payload corruption, wrong machine
and truncation. The old successful receipts are not modified.

A separate derived volume `px4-slaaps-pwm-metadata-20261020` and stage
`build/slaaps_firmware/pwm-handling-20261020-metadata` preserve the first build and
repair the missing original Git mount. Build exec handle 76054 is now terminal with exit zero. Final package verification
is recorded below; no build process remains active.

### Corrected board result

Stage `build/slaaps_firmware/pwm-handling-20261020-metadata` and Docker volume
`px4-slaaps-pwm-metadata-20261020` retain the successful derived build. Target
`px4_fmu-v6x_slaaps`, dirty `slaaps-sitl` checkpoint `472a7d6786`, embeds
`v1.18.0-beta1-679-g472a7d6786f`. Image 1,952,524 B; flash headroom 13,556 B;
linked AXI SRAM 111,356 B. Package size 1,851,204 B, SHA256
`46f36c861a7dad99f6c082193d739b98b3ad0eeb6dcd03cf37cf67b331c4c7ae`.
The independently checked package equals the binary, both flash ELF load segments
match it, the expected version string is present, and 83 current production and
board-configuration source hashes agree. Board receipt SHA256
`8b58c866f12ac4130ff3d3e2c51de6c50aa36f701cb0cf220f9854ce4e0727aa`.
The exact verifier is preserved beside its receipt. The corrected build log has
no unresolved-alternate or fatal Git messages. PWMOut::updateOutputs is linked.

Package: `build/slaaps_firmware/pwm-handling-20261020-metadata/artifacts/px4_fmu-v6x_slaaps_472a7d6786_dirty_output.px4`.
Distinct temporary copy:
`/tmp/px4-firmware/px4_fmu-v6x_slaaps_472a7d6786_dirty_pwm_handling_metadata_20261020.px4`.
No flashing, boot verification, physical driver attachment, electrical feedback,
flight qualification or working-baseline port is claimed. Original baseline,
routing and first diagnostic-build artifacts remain preserved.

## Native manual worker transport (2026-10-20)

`ManualWorkerTransport` now connects the actual portable controller and common
EDF owner to the combined uORB command publisher and the two bank reply topics.
Entry requires explicit command/reply advertisement revisions and the existing
configuration, selected-input/state, routing and permission grant. It validates
all participating reply transports before advancing the controller, drains each
new reply once, monitors the controller while waiting, and publishes only a fresh
`Command` result. Waiting/NotDue/fault paths do not republish retained output or
renew a deadline. Transport faults stop the worker and revoke its EDF ownership.

Same-publisher session re-entry uses prior-session command/reply snapshots only
as an ignored baseline. A retained expired command cannot drive the newly granted
bank, nor can an old reply complete the new session. New/current/future session
traffic at entry is rejected. An admitted worker's destruction stops ownership;
an unadmitted object's destruction does not revoke another owner. The supervisor
must still serialize ownership handoff and stop the old admitted worker before
transferring the shared owner.

Native tests now exercise the actual controller, uORB command transport, both
bank consumers, exact-ticket handling confirmations and uORB replies together.
They cover stick release, subsequent nonzero EDF slew, separate/simultaneous bank
replies, publisher replacement, missing/late replies, explicit stop, expired
retained traffic on re-entry and destruction. Their handling sink remains
memory-only. Production supervisor admission, selected sensor/input attachment,
PWMOut/PX4IO attachment and flight-loss policy are still pending. The most recent
board artifact predates this producer adapter; its resource figures do not yet
include an active production control path.

Final evidence: host `/var/tmp/slaaps-configuration-yvr4scwg` passes 1,879 checks
each Debug/Release/ASan-UBSan plus 175 parameter metadata definitions, with all 83
input hashes matching. Native `/var/tmp/slaaps-native-c5za5i48` compiles/links;
ELF SHA256 `e0d97bcf084ff49b48a21232c1af12e4ff8f6c85497055fc893c5974bda7e80d`.
Runtime `/var/tmp/slaaps-native-runtime-i1y50e_t` passes 2,034 checks and 115
successful supervisor/runtime commands. The 52 added checks cover the complete
worker/transport/bank path and lifecycle cases described above. Three changed
native source anchors match the sealed snapshot. The earlier producer build
`/var/tmp/slaaps-native-5v2lwsgw` is retained as compile-only evidence before the
final lifecycle changes; it is not the final runtime-qualified candidate.

Next verify the active controller/transport headers with the NuttX target
compiler, then attach the production supervisor, selected sensor/input path and
physical driver transaction. The last board image does not include this active
control path, so its remaining flash cannot be assumed sufficient for attachment.

## Active-path NuttX portability and resource probe (2026-10-20)

The active controller/transport headers were compiled with the retained
`px4_fmu-v6x_slaaps` compiler command, including Cortex-M7 hard-float, NuttX flat
configuration, `-nostdinc++`, disabled exceptions/RTTI and production warnings.
The first probe failed because `<algorithm>` is unavailable. ManualEdfInput,
ManualOutputBank and ManualWorker now use PX4 math minimum helpers and UINT64_MAX
instead of unavailable standard-library algorithm/limits facilities. Deadline
selection and overflow rejection semantics are unchanged.

`Tools/slaaps/probe_active_target.py` stages the current SLAAPS headers and a
forced-interface probe while mounting the retained board source/build read-only.
Final evidence `build/slaaps_active_target/20261020-verified` compiles producer
creation/entry/cycle/stop/destruction and both bank endpoint interfaces, including
construction/destruction, with ARM GCC 13.2.1. Its 22 input source hashes, 213
compiler dependencies and resulting object rehash successfully. The initial
failure and intermediate probe remain in separate attempt directories.

Final unlinked object: 9,362 B text, zero static data/BSS. Target `sizeof` values:
EDF owner 848 B; worker transport 1,632 B; MAIN and AUX transport 1,336 B each.
Their 5,152 B combined object storage excludes dynamic uORB allocations, supervisor
state and stacks. Largest individual compiler-reported stack frame: 712 B;
this is not call-chain stack use or runtime high-water evidence.
Object SHA256 `6e40de9ec85e46b182025ad73628819c0006eac9d6286adc21ce4fdaddf912c0`.

This is an unlinked API/resource probe, not a new firmware artifact or proof that
the active path fits the remaining flash. A production link must also include
out-of-line controller dependencies, supervisor and physical driver attachment.
The prior accepted board artifact remains unchanged and predates this fix.

Post-fix regression: host `/var/tmp/slaaps-configuration-dwx_y5zi` passes 1,879
checks in each Debug/Release/ASan-UBSan configuration and all 175 parameter
metadata definitions; all 83 input hashes match. Native build
`/var/tmp/slaaps-native-1da46ouc` links, ELF SHA256
`fb32f1532b14b3ee8a0c045f58648bdf66c771d6700fb0e5ba45c501416903ea`.
Runtime `/var/tmp/slaaps-native-runtime-t37a43n4` passes all 2,034 checks and
117 successful supervisor/runtime commands. The three portability-corrected
headers match the native sealed source. All verification processes are terminal.
Next production supervisor/selected-state/driver attachment and a linked active
resource check; no firmware fit or physical operation is inferred from the probe.

## Shared selected-input observation (2026-10-20)

`SelectedStateObservation.hpp` now owns the common single-estimator capture in
`src/lib/research_stabilized_manual`. It preserves original sensor/manual sample
timestamps, selected gyro/accelerometer identity, reset counter, atomic publisher
revision capture and a monotonically changing source epoch. Estimator alignment,
selected-device agreement and status freshness are metadata prerequisites only.
The raw `actuator_armed` observation and its freshness are separate from sensor
metadata validity, so armed sensor observation does not grant output permission.
Numerical checks, manual sender/range/freshness checks, epoch binding across a
control session and flight-health/condition admission remain required.

The existing module `SelectedStateInput` delegates capture to the shared library
and retains its disarmed, no-kill/lockdown/termination/ESC-calibration gates. Its
prearmed servo condition is unchanged. `PublisherBinding` also moved into the
library with a compatibility include at the original module path.

The production SLAAPS supervisor now captures these observations for applied
manual configurations at its existing 100 ms diagnostic cadence. `slaaps_status`
and `slaaps status` expose metadata validity and source epoch. The one-second
status freshness bound is diagnostic, not a control-loop timing contract. The
new fields are internal uORB diagnostics; no MAVLink parameter or QGC protocol
was changed. All mode availability masks remain zero, and no worker or physical
driver is admitted by these observations.

Native tests run before commander or sensor modules start to own the synthetic
publishers exclusively. They restore estimator parameters before the normal
supervisor lifecycle tests. Verification results are recorded below.

Initial native link `/var/tmp/slaaps-native-viqvcw26` passed, but its runtime
`/var/tmp/slaaps-native-runtime-xm494ju0` failed the new restart fixture. The
fixture incorrectly called `advertise()` on an explicitly unadvertised
`uORB::Publication`, whose stored handle remains non-null. The corrected test
uses a distinct publication object, as the existing transport lifecycle tests
do. The failed evidence is retained and is not a passing qualification.

Corrected native build `/var/tmp/slaaps-native-_vc0agyo` passes compile/link and
source sealing. Runtime `/var/tmp/slaaps-native-runtime-2o2ql6u_` passes all
34 selected-input checks, 2,034 existing control checks, and 117 successful
registration/configuration/restart/routing commands. ELF SHA-256:
`b75875b449f2d981f7a5ccfa18dc8ba84cd1fe338c4fc484bf5d89b4a757cab2`.
Seven changed production/test sources independently match the sealed manifest.
The verifier checks initial input metadata invalid/epoch zero and retains zero
mode availability. All test processes are terminal. No board build, hardware
operation or QGC GUI acceptance is claimed for this refactor.

## Selected-input session binding (2026-10-20)

`SelectedManualInput` binds all seven observed publisher revisions, selected
gyro/accelerometer IDs and source epoch at worker admission. Each cycle checks
that those identities remain unchanged, metadata timestamps/capture time do not
regress, and estimator/flags/permission samples remain fresh at consumption.
It requires observed armed state and rejects kill, lockdown, termination and
ESC-calibration. Freshness limits are explicit supervisor policy inputs with no
permissive defaults. The observation now retains the original flags timestamp.

`ManualWorkerTransport` accepts the complete selected snapshot through new
entry/cycle overloads. Metadata faults stop the actual controller and shared EDF
owner before another publication, including while bank replies are pending. A
raw-sample cycle cannot bypass an already selected-input session. Failed entry
consumes the common-owner session, and corrected inputs cannot recover a faulted
worker without deliberate new admission. Existing controller/manual numerical
and sample-continuity checks remain in the real worker, without duplication in
the metadata guard. This does not authenticate publishers or establish a flight
permission: the supervisor must still supply independently admitted permission,
output bindings and advertisement identities. Production scheduling/admission
and physical-driver attachment remain outstanding.

ARM probe `build/slaaps_active_target/20261020-selected-input-verified` forces
compilation of both selected-snapshot worker methods and both bank transports
with retained Cortex-M7/NuttX board flags. Unlinked object text is 10,116 B;
worker object 1,736 B, EDF owner 848 B, each bank 1,336 B. Largest individual
static stack frame is 712 B. This is neither incremental linked flash cost nor
full call-chain/runtime stack measurement. Initial probe
`20261020-selected-input` failed because its overlay omitted a sibling library
header; the corrected probe includes the current shared library headers.
All 32 probe source bindings and 234 compiler dependencies independently rehash.
Corrected method routing was captured before the native snapshot was sealed.
Native build `/var/tmp/slaaps-native-0b9tf4_a` and runtime
`/var/tmp/slaaps-native-runtime-u6w9qkp4` pass 2,157 control checks (123 new),
34 input-observation checks and 115 successful supervisor commands. ELF SHA-256:
`a326a5c18f7aa266f0cbdf8c4178ecd3e746a2e17cbe2da5617ea426c8298898`.
The four changed native production/test sources match the sealed manifest.
All jobs are terminal; no hardware operation or complete firmware link occurred.
The supervisor still only observes inputs until flight/output admission is attached.

## Supervisor-owned inactive endpoint (2026-10-20)

The production `Slaaps` supervisor now owns one `EdfOwner` and one
`ManualWorkerTransport`, declared in that order so the worker is destroyed
before its shared propulsion owner. Startup advertises the command endpoint
after parameter-schema initialization and fails if the endpoint cannot be
created. It publishes no command and performs no worker admission. Shutdown
stops an active manual worker and revokes the EDF owner before unregistering
modes. Configuration ineligibility stops an active manual worker.

The endpoint pins its own advertisement revision. A replacement cannot be
adopted merely by calling `advertise()` again, and entry must match that owned
revision. The supervisor exposes the live revision (zero if lost) and worker
active state in its internal status. These fields grant neither driver nor
flight permission. Allocation, boot-time object construction and advertisement
are now production code; active session scheduling/admission remain pending.

Synthetic worker/bank tests now finish before commander and the supervisor
start, because their test publishers must not replace the supervisor's live
endpoint. A separate read-only native test binds the actual supervisor revision
and requires `Waiting` (no current-lifetime payload), inactive worker and zero
mode availability. Startup plus three restarts exercise this boundary. The
standalone link receipt no longer claims whether ownership is attached; runtime
lifecycle evidence must establish that separately.
Native build `/var/tmp/slaaps-native-g2703s75` and runtime
`/var/tmp/slaaps-native-runtime-06gm3u6e` pass 2,157 control checks, 34
input-observation checks, all four supervisor endpoint lifecycles (16 checks),
and 120 successful runtime commands. Each restart produces a distinct owned
revision and no current-lifetime command payload. ELF SHA-256:
`8b0c0c3cddf8aabf128694f5cb56466aa24a3fbd6a98f7c2b9774dd3a2818e4b`.
All four changed production/test sources match the sealed native snapshot.
All jobs are terminal. This increment is native verified; the preceding ARM
probe and firmware artifacts do not qualify the new supervisor ownership.
Next integration must generate fresh admission session IDs across supervisor
restarts, attach the driver handshake and qualified flight permission, and
schedule selected-snapshot cycles at the configured control period.

## Serialized manual execution scheduling (2026-10-20)

The supervisor now contains the active selected-snapshot cycle path and switches
its requested work-queue interval to the lesser of the validated minimum control
interval and the 100 ms supervisor interval. Registration, routing diagnostics
and status publication remain on a separate 100 ms maintenance cadence. Arming
check replies and parameter invalidation are serviced on every run. Shutdown
returns to the slow interval before draining mode unregister requests.

An active cycle captures inputs, then vehicle status, then reads decision time.
It requires the configured manual mode to be selected and armed with fresh
vehicle status, an eligible configuration, and the owned command endpoint. The
shared execution gate revokes the worker on lost mode/configuration/endpoint;
otherwise it calls the selected-input transport path with the existing permission
and binding. It never renews permission or command expiry by polling. Slow
maintenance may still cause scheduling latency; requested intervals are not
proof of deadline compliance or board stack/CPU margins.

The production admission transaction is still absent. Its permission, binding
and input-policy fields remain empty, no worker `begin` is invoked, and all mode
availability masks remain zero. Completing admission must atomically establish
these fields, fresh session identity, commander/driver ownership and the qualified
flight-loss/condition policy. The full active `Slaaps::Run` path cannot yet be
claimed exercised by a real admitted supervisor. Native tests exercise its shared
execution gate with the actual worker/controller/EDF and verify idle lifecycle
cadence in the actual module.

Timing validation now requires cycle maximum, output lease and initial manual
request age to extend strictly beyond the earliest permitted integration tick.
Expiry checks reject at the deadline itself; equality previously admitted a
configuration that could never issue its first valid command. Rejections identify
`SLA_CYCLE_MAX`, `SLA_OUT_LEASE` or `SLA_IN_AGE` respectively. Bench validation
remains unchanged.
Verification results follow.

A recovery-mechanism question is pending from 2026-10-20: whether loss of
attitude estimation/control authority in powered flight should invoke a
parachute, another recovery system, or propulsion cutoff. Do not infer an
airborne policy from the existing bench inhibit or `RecoveryRequired` status.
Independent software integration can continue while this decision and measured
vehicle data are pending.

Host `/var/tmp/slaaps-configuration-1rjyj4wi` passes 1,882 checks each in
Debug, Release and ASan/UBSan, plus generation of all 175 PX4 parameters; all
87 input hashes match current sources. Earlier host `aud5bmif` is retained.
Intermediate native `/var/tmp/slaaps-native-g4_him5o` and runtime
`/var/tmp/slaaps-native-runtime-y3v5zq3q` pass 2,182 control checks, 34 input
checks and four endpoint lifecycles, but precede two native test-only changes
that exercise successful cycles through the execution gate. The final native
snapshot `/var/tmp/slaaps-native-7sgk_m1l` includes those changes.

Final native build `/var/tmp/slaaps-native-7sgk_m1l` and runtime
`/var/tmp/slaaps-native-runtime-5kg4gbbd` pass all 2,182 control checks, 34 input
checks, four endpoint lifecycles with requested idle-cadence checks, and 130
successful runtime commands. ELF SHA-256:
`49168973a334912c400675a3d7588bc13a4f8654faf88a36d4bdb1d54babfdbf`.
Five changed production/test sources match the sealed snapshot. Full active
supervisor scheduling is still not exercised without admission. All jobs are
terminal; no hardware was operated. Because `Slaaps::Run` now references the
active controller path, the next board link must assess actual flash and stack
feasibility rather than reuse the earlier inactive-supervisor firmware evidence.

## Full active-loop board link and flash capacity (2026-10-20)

Using the build-px4 workflow, a new read-only-parent-derived Docker volume
`px4-slaaps-active-loop-20261020` refreshed 15 source paths and built the current
`px4_fmu-v6x_slaaps` target. Stage `build/slaaps_firmware/active-loop-20261020`
passes independent package/bin/ELF/version/source verification, but the image
uses 1,965,132 B of 1,966,080 B flash: only 948 B remained. This is retained
evidence, not the selected development artifact.

The dedicated SLAAPS board variant now omits `CONFIG_MODULES_VTOL_ATT_CONTROL`.
Its startup explicitly does not run stock control/allocation modules, and the
SLAAPS commander check rejects stock flight modes. Stock winged-VTOL transition
control is not a supported recovery mode for this fixed-EDF vehicle. No stock
`default.px4board`, baseline, driver, estimator or manual-input configuration was
changed. This is a dedicated SLAAPS target, not a replacement stock VTOL image.

Derived volume `px4-slaaps-active-loop-headroom-20261020` builds successfully.
Stage `build/slaaps_firmware/active-loop-headroom-20261020` independently matches
all 88 changed production anchors and the package's decompressed image to both
the bin and two ELF flash-load segments. The correct embedded version is
`v1.18.0-beta1-679-g472a7d6786f` at dirty checkpoint `472a7d6786`. Image
1,943,368 B; headroom 22,712 B; linked AXI SRAM 111,356 B. The 21,764 B reduction
comes from excluding that unused controller and associated generated content.
Runtime heap/stack use is not included in linked static RAM figures.

The final package is
`artifacts/px4_fmu-v6x_slaaps_472a7d6786_dirty_output.px4` under the final stage,
1,842,548 B, SHA-256
`094353fb3f385383579edacb3e22d634bdbcfe047d7689875e1a8360d7e7fdd6`.
An additional retained effective `boardconfig` and symbol check prove the actual
stabilized cycle and EDF sampling code are linked, SLAAPS is enabled, stock
VTOL control is absent, and native test commands are excluded. The first config
lookup assumed `.config`; its failed log is retained, and the successful check
uses this PX4 build's actual `boardconfig` file.

The package was staged and rehashed at
`/tmp/px4-firmware/px4_fmu-v6x_slaaps_472a7d6786_dirty_active_loop_headroom_20261020.px4`.
That path is temporary; the workspace package and receipts are the retained
artifacts. All processes are terminal. Nothing was flashed or boot-tested.
Flight admission remains disabled; driver integration, qualified recovery policy,
measured vehicle data and runtime scheduling/stack qualification remain required.
The new headroom must be remeasured when those paths are linked.

## Non-actuating bank preparation (2026-10-20)

`ManualBankPreparation` stores a bounded, one-use proposal tied to the full
validated configuration, exact routing plan, session/source, command publisher
revision and every participating bank's reply publisher revision. Preparation
produces no PWM candidate, EDF permission or handled acknowledgment. Failed,
expired and consumed proposals require a new session token. Activation rechecks
routing and identity before the existing bank independently checks armed flight
permission. A proposal cannot disturb an already active bank.

`ManualBankTransport` pins its own reply advertisement lifetime and exposes
`prepare` / `beginPrepared`. A replacement bank rejects a retained proposal
addressed to its predecessor; an existing object cannot adopt a replacement
publisher. Shared routing equality includes channel assignment, protocol, rate,
calibration, disarmed/failsafe values and reversal. Raw trusted `begin` remains
for existing core callers; production driver integration must use the prepared
path. No prepare/ready wire protocol or physical driver attachment exists yet.

Final host evidence `/var/tmp/slaaps-configuration-xiqp8bo_` passes 1,932 checks
in Debug, Release and ASan/UBSan plus 175 generated parameters; all 88 inputs
rehash. The negative bank-set test first failed in
`/var/tmp/slaaps-configuration-_j2orxk6`, exposing a missing validation loop;
that failure is retained and the corrected contract passes. Earlier preparation
receipts are intermediate evidence, not the final revision.

Native build `/var/tmp/slaaps-native-f67d12gr` and runtime
`/var/tmp/slaaps-native-runtime-df4yx30j` pass 2,276 control checks, 34 input
checks and four actual supervisor endpoint lifecycles. Five changed production/
test files match the sealed snapshot. ELF SHA-256:
`42dde9e51e711b1e39080e9556d52103aad9d1790b76f0626e7470310eec6097`.
This exercises synthetic in-process preparation and memory handling, not a
physical driver, active production admission or QGC GUI acceptance.

Final ARM evidence `build/slaaps_active_target/20261020-prepared-bank-verified`
compiles both bank templates with actual board/NuttX flags. All 34 source bindings
and 235 compiler dependencies independently rehash. Unlinked object text is
11,500 B; EDF owner 848 B, worker transport 1,736 B, each bank transport 1,552 B.
Largest individual static frame is 712 B, not total stack usage. Object SHA-256:
`b12636173c42cf87b03b4872f46876b2dac98faa2a9fe0aea30bf5303a131ffb`.
The previous full board package predates this preparation API; its flash headroom
cannot stand in for a future link with admission and driver attachment.

Next work is the prepare/ready transport and supervisor transaction, followed by
serialized PWMOut/PX4IO attachment and actual handled acknowledgments. Duplicate
wire proposals must be deduplicated by that adapter; the typed core deliberately
rejects replayed session tokens. Measured vehicle data and the requested recovery
mechanism remain human inputs. Independent transport/software work can continue.
No hardware was operated, no flight permission was enabled, and no commit was made.

## Preparation/readiness message contract (2026-10-20)

`SlaapsManualPreparation` defines separate proposal, MAIN readiness and AUX
readiness topics. The common payload echoes every proposal identity field,
including peer acknowledgment lifetimes and original deadline. A proposal uses
REQUEST_BANK and zero prepared time; replies identify their actual bank and the
original preparation time. This is an internal protocol with no MAVLink or QGC
changes and no activation grant. `ManualPreparationTransport` validates message
role, publication ordering, bounded expiry, participating bank identities and
exact proposal equality. Failed decode preserves both caller outputs. Shared
request validation is also used by the bank preparation core.

Host evidence `/var/tmp/slaaps-configuration-po3p369q` passes 1,959 checks each
Debug/Release/ASan-UBSan and 175 generated parameters; all 90 source bindings
rehash. The 27 new checks exercise proposal/reply round trips, cross-bank and
role rejection, malformed timestamps and identities, original expiry and exact
peer/deadline matching. These are generated-header codec checks, not a supervisor
transaction. The transport adapter must still bind actual publisher lifetimes,
deduplicate retained proposals and compare readiness against the active proposal.

The previously verified ARM preparation probe predates this shared validation
refactor and the new wire schema. Neither that probe nor the earlier firmware
package establishes current protocol attachment or flash fit. Physical driver
consumers and supervisor admission remain pending; availability remains zero.

Native build `/var/tmp/slaaps-native-vy_7mdmv`, runtime
`/var/tmp/slaaps-native-runtime-9pa4j6i9`, passes 2,309 control checks, 34 input
checks and four supervisor endpoint lifecycles. Six new native checks publish
and decode all three preparation topics through atomic lifetime-bound
subscriptions. Five changed production/test sources match the snapshot.
ELF SHA-256 `5c13371d88782d361ac106592b72c83a1d6770ece00139c469a44585fdaa4c5d`.
No driver was attached or operated. All test jobs are terminal.

## Bank preparation endpoint (2026-10-20)

`ManualPreparationEndpoint<Bank>` now connects a lifetime-bound proposal
subscription to the existing bank preparation and readiness publication. The
trusted caller supplies the expected proposal advertisement revision. The endpoint
pins its own readiness publisher lifetime; replacement cannot be silently adopted.
The driver must serialize endpoint, bank, inventory and configuration operations,
and preserve the bank instance across preparation sessions.

The first valid proposal prepares once and publishes an exact readiness echo.
Retained or republished duplicates preserve the original preparation/publication
times and deadline. Changed same-session content, malformed roles, expired data,
backward polling time, changed configuration/routing and replaced proposal,
command, handled-reply or readiness publishers cancel preparation. Every ready
poll rechecks live bank state. Cancellation and consumption publish a zero-identity
invalidation payload that fails the readiness decoder, replacing retained positive
readiness. This invalidation is not a negative flight grant or recovery command.

`activate` polls once more and then consumes preparation through the bank's
independent trusted armed/flight permission checks. Later preparation traffic or
cancellation does not revoke an already active flight bank. No physical outputs,
supervisor admission transaction or flight availability are enabled by this API.
The complete routing inventory and eventual activation binding must come from
the trusted driver/supervisor integration; a decoded proposal is not provenance.

Host `/var/tmp/slaaps-configuration-putueyok` passes 1,959 checks each Debug,
Release and ASan/UBSan plus 175 parameters; all 91 source bindings rehash.
The host run covers existing core/codec regressions; the uORB endpoint itself is
exercised by the native fixture. ARM and full-board evidence remain earlier
milestones and must be refreshed after the new endpoint is attached.

Native build `/var/tmp/slaaps-native-u2hg106n`, runtime
`/var/tmp/slaaps-native-runtime-q9kubd_z`, passes 2,443 control checks, 34 input
checks and four supervisor endpoint lifecycles. Four changed source files match
the sealed snapshot. The 134 added checks exercise MAIN/AUX endpoints, initial
silence, non-actuating readiness, duplicate preservation, one-use activation,
active-bank isolation, configuration/routing edits, expiry, backward time,
same-session mutation, publisher replacement, wrong topic role, denied flight
permission and explicit cancellation. Native ELF SHA-256:
`a1a73ba7e75cda3ed924aac298eb331abfdc66b7692f2bf15337ca8ec07996e0`.
All jobs are terminal; no physical driver has been attached or operated.
Next is the supervisor transaction requiring readiness from every participating
bank, followed by serialized driver attachment and qualified activation/recovery.

## All-bank preparation coordinator (2026-10-20)

`ManualPreparationCoordinator` owns the proposal publisher and binds every
participating readiness publisher before publishing a request. The caller supplies
trusted expected readiness revisions. Begin validates measured configuration,
complete routing, request metadata and live command/handled-reply lifetimes;
failed entry consumes its session token. Exact pre-proposal reply payloads are
captured as baselines and cannot satisfy the new transaction. All participating
banks must echo the entire proposal before the coordinator reports Ready.

Polling continues to validate configuration/routing, deadline, monotonic time,
proposal/command/handled-reply/readiness lifetimes and exact reply identity.
An already accepted reply cannot change its preparation/publication time or
return to a pre-proposal baseline. Cancellation publishes an invalid proposal so
listening banks discard preparation. A new inactive transaction does not emit a
transient cancellation before its proposal, avoiding an unnecessary listener
cancellation race. Replacing a pending transaction explicitly cancels it; callers
must coordinate listening again before a fresh proposal.

This API collects readiness only. It is not attached to production Slaaps::Run,
does not grant flight permission and does not implement distributed activation
or rollback. Keep the proposal live during the separate trusted activation phase;
bank activation consumes/invalidates its readiness. The supervisor must transition
out of readiness collection when activation starts, track activation completion
separately and implement failure handling before any physical output attachment.
No physical driver, QGC behavior or availability mask is changed here.

Host evidence `/var/tmp/slaaps-configuration-6a_4weui` passes 1,959 checks each
Debug/Release/ASan-UBSan and 175 parameters; all 92 input hashes revalidate.
Native build `/var/tmp/slaaps-native-0dimfwqb`, runtime
`/var/tmp/slaaps-native-runtime-gm4ecx43`, passes 2,567 control checks, 34 input
checks and four supervisor endpoint lifecycles. Four changed source files match
the sealed snapshot. Native ELF SHA-256:
`bf738dcc3091b4d144d8f66510c0ef2b5428c07503e3447af88a7e6afb96eced`.
The 124 added native checks cover both-bank coordination, partial readiness,
retained baseline exclusion, silent-bank expiry, independent activation,
configuration/routing edits, clock reversal, cancellation, publisher replacement,
same-publisher identity/time/role mutation and failed-entry replay rejection.
Three payload-mutation cases use controlled fixture publishers to isolate payload
validation from advertisement replacement; the normal path uses real endpoints.

Production Slaaps::updateRouting currently assembles diagnostic inventory in a
local variable from ordinary multi-instance subscriptions; it does not yet retain
a coherent admission snapshot with driver tokens/publisher bindings. Implement
that boundary and a distinct activation/completion phase before attaching the
coordinator to production control. Do not interpret the fixture's sequential
trusted bank activation as distributed activation/rollback qualification.
All jobs are terminal. No physical output or flight permission was enabled.

## Driver report observation binding (2026-10-20)

`OutputRoutingObservation` now enumerates every uORB output-bank instance and
atomically copies each report with its advertisement revision. It validates the
complete MAIN/AUX inventory, then rechecks all instances and captured publication
generations before accepting a snapshot. A changed enumeration, revision or
generation rejects the observation pass with UnstablePublisher. Successful
snapshots retain driver tokens, report times, publisher revisions and topic
instances indexed by physical bank, plus the complete effective inventory.

`Slaaps::updateRouting` now uses this collector and retains its snapshot. A failed
capture leaves the old snapshot untouched while clearing routing-valid state;
callers must check the current result. `sameReportedDrivers` compares captured
physical driver/publisher/instance identities independently of heartbeat times.
It is an identity comparison, not freshness, routing equivalence or permission.

This is observation consistency, not an atomic physical-driver transaction or
publisher authentication. Final driver handling still needs independent live
routing/permission checks. The new snapshot must be bound into the future
admission/activation transaction; this change does not populate flight permissions
or enable any mode. ARM/full-board resource evidence predates this new production
observation path and must be refreshed before vehicle deployment claims.

Host `/var/tmp/slaaps-configuration-t0ou9zq1` passes 1,959 checks per Debug,
Release and ASan/UBSan build plus 175 parameters; all 93 input hashes revalidate.
The shared host suite is regression evidence; native tests exercise the actual
multi-instance collector and production supervisor routing diagnostics.

Native build `/var/tmp/slaaps-native-zm0ehra4`, runtime
`/var/tmp/slaaps-native-runtime-2k17w7l4`, passes 2,585 control checks, 34 input
checks and four supervisor endpoint lifecycles. Four changed inputs match the
sealed snapshot. Native ELF SHA-256:
`efba15f7d50e3de7b662275ec00c851c7de1f38278ad04817df414bb328449f9`.
Eighteen new checks cover physical-bank/instance mapping, complete inventory,
heartbeat identity, changed device tokens, identical publisher restart, missing
and extra instances, stale/unready/duplicate/future reports and preserved snapshot
on failure. Existing production supervisor routing-fault checks also pass with
the new collector. The second-pass generation logic was reviewed against uORB's
atomic payload/generation/revision copy; no deterministic inter-pass race injection
was performed in this fixture. No physical output operation occurred.

Next, bind the captured driver identities and current inventory into preparation
and activation so a successful fresh observation cannot silently replace an
admitted driver's identity. Complete activation acknowledgments/rollback before
physical attachment; the snapshot alone is not an admission decision.

## Observation-bound preparation (2026-10-20)

`ObservedRoutingBinding` now binds validated configuration/routing and both
physical driver identities from a successful capture. The proposed participating
device tokens must match the actual report tokens. Explicit, nonzero capture-age
and report-age limits are required; report age cannot exceed the collector's
one-second ceiling. No vehicle timing policy is supplied by default. Checks
require current successful capture status, bounded age, monotonic capture/report
times, stable device tokens, advertisement revisions and topic instances, and
unchanged configuration/effective routing. A fault latches until explicit entry.
A subsequent fresh observation cannot repair an existing failed binding.

`ManualPreparationCoordinator::beginObserved` performs this binding before
publishing a proposal. `pollObserved` rechecks it throughout readiness collection.
The raw inventory polling interface rejects an observed transaction, preventing
a caller from bypassing capture/identity checks. Failed observed entry consumes
the proposal session token, as other coordinator failures do. Existing raw entry
remains a trusted core/fixture API; production integration must use observed entry.
The actual supervisor currently captures reports but does not yet start either
preparation path or grant flight permission. Activation completion/rollback and
physical output attachment remain outstanding.

Host `/var/tmp/slaaps-configuration-bobhr10t` passes 1,997 checks each Debug,
Release and ASan/UBSan build plus 175 generated parameters. The 38 added checks
cover heartbeat acceptance, cancellation, capture errors, driver and publisher
replacement, instance reassignment, future/zero/backward/stale timestamps,
configuration/routing mutation, explicit policy bounds and device-token mismatch.
These are controlled software fixtures, not measured timing qualification.

All 94 host source hashes independently revalidate. Native build
`/var/tmp/slaaps-native-3_biy2k9`, runtime `/var/tmp/slaaps-native-runtime-yybdmne_`,
passes 2,651 control checks, 34 input checks and four supervisor endpoint
lifecycles; five changed production/test sources match the sealed snapshot.
Native ELF SHA-256:
`8ae8279e318686c41d95ab7f3bdba8fdf4dbbc6efd8ac038553d961702a9e5e9`.
The 28 new native-only checks combine observed entry with both actual bank
preparation endpoints, and reject capture failure, replacement identity, future
capture time and raw-poll bypass. Failed observed entry cannot reuse the repaired
session. Snapshot values in those cases are explicit fixtures; real report capture
has separate native evidence from the preceding milestone. No physical driver was
attached or operated, and no production permission was populated.

All validation jobs are terminal. Complete activation acknowledgments and
partial-failure handling before attaching observed preparation to the production
supervisor and physical outputs. The previously requested recovery mechanism
and measured vehicle inputs remain pending; independent software work continues.

## Activation completion barrier (2026-10-20)

`ManualActivationBarrier` collects exact acknowledgments from every required bank
before allowing one-use consumption of the earliest original first-command
deadline. Its timing budget is explicit and bounded by the configured cycle
limits. Duplicate replies cannot extend activation or command deadlines.
`ManualOutputBank::activationAck` confirms only the initial command-wait state;
`ManualBankTransport` adds the actual command and handled-reply revisions.
These acknowledgments are not actuator-handled acknowledgments or electrical
feedback. Permission loss, invalid identity, missing peers, expiry and reversed
time prevent completion. On rejection the caller must cancel every required bank,
including a bank whose successful activation reply was lost.

Host evidence `/var/tmp/slaaps-configuration-ne9t1jdd` passes 2,046 checks per
Debug/Release/ASan-UBSan build and 175-parameter metadata validation. Native
`/var/tmp/slaaps-native-ecgq_q0f`, runtime
`/var/tmp/slaaps-native-runtime-5fnlz8fi`, passes 2,721 control checks, 34 input
checks and four supervisor lifecycles. The increment adds 49 shared checks and
21 native-only checks. Native fixtures exercise both actual bank transports,
synthetic first-command handling, lost replies and permission loss.

The barrier is not attached to production admission. Activation request/reply
publisher binding, cancellation transport, current health/observation checks and
physical driver execution remain integration work. The fixture's 5,000 us budget
is not a qualified vehicle default. No new flight availability is enabled;
current board resource evidence must be refreshed before deployment claims.

## Activation reply wire contract (2026-10-20)

`SlaapsManualActivation.msg` defines separate `slaaps_manual_active_main` and
`slaaps_manual_active_aux` topics. `ManualActivationTransport.hpp` encodes and
validates exact transaction metadata and original activation/first-command expiry.
Publication time cannot renew either timestamp. Codec rejection preserves the
caller's output. Receiving bank role is supplied independently, and the payload's
command/handled-reply revisions do not authenticate this activation publisher.
The existing bound subscription must check the actual activation-topic lifetime;
the activation barrier must separately check transaction identity and time budget.

Host `/var/tmp/slaaps-configuration-i5fq2ify` passes 2,066 checks per build and
175 parameters; all 97 input hashes match. Native `/var/tmp/slaaps-native-lxes_0o5`,
runtime `/var/tmp/slaaps-native-runtime-q2m41or6`, passes 2,751 control checks,
34 input checks and four supervisor lifecycles. Four changed native inputs match
the sealed snapshot. Twenty shared checks cover wire faults and original expiry;
ten native checks cover generated topic delivery, wrong-bank interpretation and
latched publisher-replacement rejection for both banks.

These topics/codecs are not yet published by production bank endpoints or consumed
by production supervisor admission. Request/cancellation transport, lifetime-bound
completion collection and driver attachment remain. No flight availability or
physical outputs were enabled, and full-board resource evidence is still older.
## October 24 late-link external-mode discovery

Commander now publishes the complete current external-mode registry as queue-one
state. `AVAILABLE_MODES` reads that snapshot even for a one-shot request before its
first scheduled update. A MAVLink link created after registration can discover the
registered names; removal publishes an empty/updated registry, triggers the normal
monitor sequence and clears stale names. This describes registration/selectability,
not mode readiness, arming permission or companion request admission.

Native `/var/tmp/slaaps-native-runtime-ajojy07i/result.json`, SHA256
`c46ddabd313bf68b88dcce388da6cc94d8ed5bd738a0ca9d61e652ac09f7e303`, passes on
`/var/tmp/slaaps-native-rhnmnoda`. Binary SHA256
`e272a4a96c2a94cb3389fc5c1cfa3683caf20298f5c66d40f648562a6fa56ba9`;
source manifest SHA256 `9a5b0d5f27a1ad5bf2cc91e26b03d156fb9290f64d7ae67326eb95008c4b96f3`.
The existing 2034 isolated supervisor, 4987 control, 2189 mixer and 34 input checks
pass, with nine endpoint lifecycles and nine configuration restart checks.

The separate `mode-protocol-evidence.json`, SHA256
`0628ae92f088d6780233ab87184aa61d8e59b9c0e804862db088049dbc86a9fa`, verifies a
link started after all three registrations. Wire names/custom modes are SLAAPS Manual
184811520, SLAAPS Acceleration 201588736 and SLAAPS Bench 218365952. Real Commander
accepts wire SET_MODE Bench and reports actual/intended CURRENT_MODE; a wire
DO_SET_MODE exit returns its ACK. Removal changes AVAILABLE_MODES_MONITOR and removes
the stale SLAAPS names; registration restoration recovers the names without
permission. Readiness, workers, preparation and flight admission remain denied in
that discovery-only test. QGC GUI rendering and hardware behavior are unverified.

The bounded five-production-file plus harness review is
`build/slaaps_recovery/mode-registry-20261024/reviewed-integration.patch`, SHA256
`fe1f07022bdc87420b23e1ce64d82cd2149eeace31bafd37df172c451fabcfd3`.
This is a combined review, not an independently applicable six-file commit: the wire
test depends on the earlier uncommitted companion harness. The production-only draft
`build/slaaps_recovery/mode-registry-20261024/draft/mode-registry.patch` passes the
cached applicability check for its five production files. The original six-file
review request was corrected to this boundary. Only those five production files
are now staged via the production draft (74 insertions/41 deletions); cached checks
pass, but no commit has been made and DCO review is pending. The wire regression
and earlier companion changes remain unstaged for their own checkpoint review.
No push or hardware action is implied.

Frozen board `build/slaaps_firmware/durable-mode-registry-20261024` has
[aggregate resource receipt](../build/slaaps_firmware/durable-mode-registry-20261024/resource-comparison.json)
SHA256 `1d5598fcbd16d4d4e40982094af90ec424d317e4e969e569f6608a4885aba36e`.
The image uses 1,949,636 of 1,966,080 bytes, leaving 16,444 (+288 versus provenance);
AXI SRAM remains 111,164 and SRAM4 2,048. Link, current source, package/ELF and linked
RNG checks pass. Forty-seven fresh current ARM units plus two separate parent
baseline units preserve prior maximum identified chains: MAIN/AUX 2868/2812 of 4096,
supervisor 4404 of 6500, receiver 2824 of 5360, sender 1296 of 3246 and Commander 1212 of 3250.
New registry paths are Commander registration 1092/removal 916, sender registry-copy 896/
signed-monitor 792, and receiver requested-reply 2512. CLI status/start remain 1704/1496
of 2048 before unmeasured overhead; these sums are not runtime high-water qualification.
The source closure binds 52,335 firmware entries and 358 changed paths, distinguishing
seven proved-uncompiled Tools differences from compiled source identity.

Commander resident size becomes 7656 (+8, including nested registry state). The
AVAILABLE_MODES stream grows 72→288 bytes but removes the prior 200-byte lazy name
cache: initialized-cache payload grows 16 per link; the shared queue-one registry
buffer is 216 bytes. These exclude allocator overhead and do not grant readiness.
The staged package `/tmp/px4-firmware/px4_fmu-v6x_slaaps_54b2da3c6b_dirty_durable-mode-registry-20261024_20261024T065106Z.px4`
has SHA256 `751a716456ef0c879d48f855630b39ce8921d74641d27109bd18eaca621e6643`.
All checkpoint jobs are terminal. QGC GUI, boot, physical entropy, runtime timing/
stack qualification and flight admission remain outside these receipts.

Next bounded manual task: replace the diagnostic one-second prepared wait with
explicitly bounded disarmed operator readiness, separate from the short immutable
activation handshake, and report concrete admission blockers instead of generic
output-integration unavailability. Do not renew output authority or treat an
operator-declared Qualified record as sufficient flight admission.

## October 24 operator-declared profile provenance

The shared typed profile now records SLAAPS profile schema version `SLA_DATA_VER=1`,
external record ID `SLA_DATA_REC`, record revision `SLA_DATA_REV`, and operator-declared
assessment `SLA_DATA_STATE` (Incomplete, Calibrated, Qualified). Calibrated/Qualified
requires a positive record ID and revision. Unsupported values identify the exact
parameter. These fields belong to coherent capture, equality, fingerprint, apply,
acceptance markers and export/restore. Incomplete servo commissioning remains valid.
No assessment is promoted automatically, no external artifact format or authenticity
is verified, and Qualified grants no measured-condition, flight or actuator authority.

The CLI and accepted-configuration log show only the retained accepted profile.
Unapplied numerical edits revoke the claim; a rejected apply cannot display a newer
unaccepted record as accepted. Native scenarios accept Calibrated record 1771/revision 7
and Qualified record 1772/revision 8, reject a missing `SLA_DATA_REC`, and verify saved
export/restart restoration without permission. Independent Bench selectability is
preserved while Manual/Acceleration availability, workers and preparation remain denied.

Host `/var/tmp/slaaps-configuration-z4peb795/result.json`, SHA256
`3d3afc9746d2cd3e3300309548defbf418c27bd13e7f0862379ab6353757e27b`, passes 3327
checks each in Debug, Release and ASan/UBSan and all 330 parameters (320 profile fields).
All 111 captured inputs matched at completion. Native
`/var/tmp/slaaps-native-runtime-wpmehoao/result.json`, SHA256
`370d765eed286a72db330549c54734e98b98e0894a2fc95120038ce534fcf00e`, passes on
`/var/tmp/slaaps-native-49ga5tdx`: 2034 isolated supervisor, 4987 control, 2189 mixer
and 34 input checks, eight endpoint lifecycles and eight configuration restart checks.
Binary SHA256 `1af416a373bc02402c5718fd2f99ae3f6a72cb7ff6276b2c1eaa1c23aed58ac6`;
source manifest SHA256 `52ce6ed024a09f72bc4c978f76b5e4508a2dd44e89f83559aff4808129ef08b8`.
The production supervisor has no test seam. Existing Commander EDF commissioning
and signed companion sessions pass; the no-plant EDF scenario retains explicitly
forced internal arming, and companion requests remain ModeNotSelected with no
eligible acceleration control. Physical outputs, ordinary vehicle health and QGC GUI
are not qualified.

Frozen board checkpoint `build/slaaps_firmware/companion-provenance-20261024` has
[aggregate resource receipt](../build/slaaps_firmware/companion-provenance-20261024/resource-comparison.json)
SHA256 `ead14553614670ec13ac5d401baf41c4780c05b810f512fbbe6c80706da803a9`.
Its explicit supervisor source is `40459e84a56fa26a485bf3aea7e6a035c00b21d2b9ab22e4135c6e276d7b3b1d`.
The image uses 1,949,348 of 1,966,080 bytes, leaving 16,732 (+1168 image bytes);
AXI SRAM remains 111,164 and SRAM4 2,048. Package/ELF, source closure, production-test
isolation and linked STM32H7 RNG checks pass. Twelve fresh ARM groups cover 46 units.
Identified MAIN/AUX chains are 2868/2812 of 4096 bytes; supervisor is 4404 of 6500.
These are static path sums, not measured runtime high-water or timing qualification.

The [CLI review](../build/slaaps_firmware/companion-provenance-20261024/CLI-resource-review.json)
binds the actual 2048-byte built-in child task: status/start capture paths are
1704/1496 bytes, leaving 344/552 before unmeasured callees, argv and interrupt overhead.
Status uses one checked temporary 1304-byte heap snapshot, released before return;
startup reuses the instance's existing heap scratch before scheduling. Supervisor
resident size is 15,240 (+40) and each bank endpoint 8800 (+24), excluding allocator
overhead. The unsupported `std::nothrow` build failure is retained as attempt 1.
The staged package `/tmp/px4-firmware/px4_fmu-v6x_slaaps_54b2da3c6b_dirty_companion-provenance-20261024_20261024T063704Z.px4`
has SHA256 `f59b3fbdc3ec69503d52b69f68f1e245351463f3c5df16c2a102cffeca9e34a6`.
Retained Git metadata does not identify these dirty source overlays by itself.

The later external-mode discovery checkpoint is recorded above and has separate
source/verification receipts; this provenance checkpoint remains immutable. The two
RNG fixes remain dirty in the NuttX submodule. User review and permission to publish
a fork/gitlink remain pending at `build/slaaps_recovery/nuttx-rng-20261024/commit-review.patch`.
No remotes, submodules, hardware or flight-admission gates were changed by this record.
Prior `n7vhuheh` and `0q09coit` failures remain unresolved as scoped below.

## October 23 companion admission integration in progress

October 24 native checkpoint: `/var/tmp/slaaps-native-runtime-gqw82a2b/result.json`
passes on `/var/tmp/slaaps-native-_ghvhhwp`. Runtime receipt SHA256
`d5762ec3eb2d1fa403c842be6d187305467d3b4cc11bf6b2ac61505eafdd1d39`;
binary SHA256 `780cdd177ec56568fa4451782513aa8b4f650e1202ec40ac3e1d0f3a896122c2`;
source manifest SHA256 `38df48561005913531090e1c57667a9454148f52d2cdaba3b85f9b253cac2ae0`.
It passes 2010 isolated actual-source supervisor checks, 4930 control checks,
2180 actual-mixer checks, 34 input checks, seven endpoint lifecycles and seven
configuration restart checks. The normal production supervisor has no test seam.
Real Commander servo/EDF commissioning, selected-power fault shutdown, deliberate
reentry and accepted-profile export/restore pass with synthetic inputs and captured
callbacks. EDF arming uses the recorded forced internal test command because normal
vehicle health is unqualified; no synthetic output permission is supplied.

The same full run passes signed parser receipt and three companion sessions
7001/7002/7003 through fresh Commander witness plus estimator-only synthetic data:
matched signed clock exchange, separate Prepare and Grant, exact current barrier
ACK, wrong-ACK rejection, revoke and immutable map expiry requiring deliberate new
entry. Current requests return `ModeNotSelected`; acceleration remains unselected,
eligible control stays false, and no acceleration controller or physical output is
enabled. Companion evidence SHA256
`c9463f09f1ced29e0d5a13b0373ca5a6c49639e32683efd58d22b571b96a43b1`.
The bounded drain consumes 1871 datagrams, reaches its 64-datagram limit twice and
never its 5 ms budget. The three replied challenges arrive about 15.9, 9.1 and
16.0 ms after creation. These finite loopback observations do not qualify deployment
timing, measured clock assumptions, physical entropy or flight admission.

Preserved `slaaps-native-runtime-0q09coit` on `slaaps-native-y2vzpemd` failed the
existing routing-message interval assertion before activation. The reporter now
timestamps after actual emission and the unchanged interval assertion retains
counts/times on failure. The passing current run does not establish the earlier
scalar-report failure's cause. Earlier `n7vhuheh` remains independently unresolved
as described below. Current board/resource verification is recorded separately.

Native `/var/tmp/slaaps-native-d3xiijuu` links; focused supervisor runtime
`/var/tmp/slaaps-native-runtime-s_nggjk7` passes 2002 checks. Full runtime
`/var/tmp/slaaps-native-runtime-pvp2q5uw` passes isolated activation, then the first
real Commander EDF session and its deliberate power-fault stop. Its second real
Commander session rejects activation before worker entry. Both bank activation
replies carry first-command deadline `5886886617`; their activation times
`5886808226` and `5886808287` leave only 78,391 and 78,330 us, below the configured
80,000 us first-command reserve. `ManualActivationBarrier::accept` correctly rejects
that remaining budget. The fixture declared a 60,000 us activation window and
80,000 us reserve but inherited a 100,000 us battery age. The original battery
expiry caps supervisor permission, which the bank retains when reporting its
first-command deadline. This profile could succeed with sufficiently early replies,
but could not support the complete declared activation window plus reserve.
This establishes the second-session timing inconsistency, not a scheduler-load or
clock-order cause. Sampled evidence is retained in `px4.log` and
`commander-edf-first-failure.json` (SHA256
`8fcab86f1d893f07257c2edbf487877fdb497286d3fb656216ca12f538f78859`).

Shared configuration now computes checked positive `activationReserveUs` and requires
the full declared ACK window plus first-command budget to fit the exclusive output
and raw-input leases, inclusive Commander/actuator age span, and required propulsion
battery age span. Failures identify `SLA_OUT_LEASE`, `SLA_ARM_AGE`, `SLA_IN_AGE` or
`SLA_PWR_AGE`. All-zero Manual timing can still stage; servo Bench does not require
power. EDF prepared readiness expires at the earliest original preparation,
permission or raw-input deadline minus reserve. EDF raw expiry uses the original
sample timestamp plus `min(manual_age_ms, output_lease_ms) * 1000`, exclusively.
Immediately before ACTIVATE, the supervisor rechecks the live positive permission,
preparation and EDF raw-input deadlines for
strictly more than that reserve. No output must already be active, no deadline is
renewed, and the barrier's existing inequalities remain unchanged. Native fixtures
explicitly supply a synthetic 200,000 us power age for the 60,000 + 80,000 us case;
these values are not measured timing qualification or production defaults.

Final exclusive-raw host `/var/tmp/slaaps-configuration-tmuuw_ad/result.json`, SHA256
`e9063e696d22d48645f0fb58618aa7bd6e5de3c000aa4633731b64013e8f3dbe`, passes 3270
checks each in Debug, Release and ASan/UBSan, plus all 327 parameter definitions.
It captures configuration core `af618ab4...` and fixture `d0901b52...`; equality at
the exclusive raw reserve boundary rejects. The native fixture also pauses only raw
pilot publication while authority and power stay fresh, checks readiness loss at
the original shorter EDF lease minus reserve before raw expiry, and checks fresh
input can restore disarmed readiness without renewing duration or activating output.
The host build excludes that native-only scenario; the current 2010-check native
supervisor pass executes it with the later fixture `369a97e7...`.

The earlier host `/var/tmp/slaaps-configuration-udsufpsn/result.json`, SHA256
`0b55bafabf0a91ae7f6858456caa924a447c9feaaaffad10a212f3c527a85185`, retains its
3270-per-build pass for the preceding inclusive-raw draft; it does not verify the
exclusive correction.
The failed first attempt `/var/tmp/slaaps-configuration-bc2lkzpd` is preserved: a new
EDF output-lease assertion expected the later timing validator, but the existing
Bench cycle-gap check correctly rejected it first. Only the regression's validation
order and passing boundary fixture were corrected. The intermediate
`/var/tmp/slaaps-native-y2vzpemd` link and its failed `0q09coit` runtime remain
preserved; the later current native result is recorded above.

Full `/var/tmp/slaaps-native-runtime-7q7msf9w` on `slaaps-native-x2a2ngrl` passes
the real Commander EDF sessions/reentry, then reaches signed companion session 7001
and remains prepared with an invalid map. Its sole recorded replied clock challenge
was created at `431682078` us, before Prepare at `432078251` us/binding 2. The last
received signed status is dated `433678195` us while the final owner status is
`440078146` us, about 6.4 seconds ahead. This supports a harness receive backlog
concern; it does not establish an owner map-validation defect. Immutable clock
evidence SHA256: `2daff40af14b643ff6f11a01bb23002c859e9f34fe7da620c161f7877f9ffd07`.
The harness now drains at most 64 UDP datagrams or 5 ms per pump without blocking,
instead of one datagram between listener calls. A 1 ms idle sleep bounds empty wire
polling. Existing timeouts, signature/source checks and clock bounds are unchanged.
Per-type counts, a bounded clock-receipt history, actual reply times and final
control/packet observations persist in the now-passing current result. Harness SHA256:
`96c9af014f73150d52f02a8930ba26784d2f4e1aeb2dafcf4e08948f8e6dc462`.

Earlier `/var/tmp/slaaps-native-runtime-n7vhuheh` first-zero handling cancellation
remains unresolved. Its later `InvalidInventory` routing status reflects candidate
removal during cleanup and does not identify the first failing guard. Do not attribute
that failure to the second real Commander session's timing inconsistency. Likewise,
the nonzero retained power deadline in `slaaps-native-runtime-x9fdv8pf` proves its
later power status 9 is retained-sample expiry, not an observed battery rejection.
Native-only pre-cleanup cancellation capture remains available. These earlier
failures remain separate from the later signed companion map wait; no physical
outputs operated.

The preceding `/var/tmp/slaaps-native-b3zmro9h` snapshot retains binary SHA256
`5b80d6082f88b7cbb48ee39aadc57c479d3b55e90282ba86b7dd7a852ca78c55` and manifest
SHA256 `902e8f4495ea2c3e9990fbec1c9f7141db5bc8b51980c7524f029482884844a2`.
Its focused `slaaps-native-runtime-ojor7_to` passes 2002 checks; its full
`slaaps-native-runtime-x9fdv8pf` failure remains preserved.

The current FMUv6X checkpoint is
`build/slaaps_firmware/companion-reserve-emission-20261023`. Its
[resource receipt](../build/slaaps_firmware/companion-reserve-emission-20261023/resource-comparison.json)
has SHA256 `d0cf90e2bf0141ac0f681f72c125e1f90f5dfd3e887ced3f563ce64ca7e2af11`.
It includes the activation-reserve/original-expiry corrections and routing
diagnostic rate limit measured from actual emission time (`Slaaps.cpp` SHA256
`b844649956bfeafbbaf45d898760577966f6bd859f5637a092866bee3602ece6`). The image
uses 1,948,180 of 1,966,080 flash bytes, leaving 17,900: 480 bytes more than the
preceding clean checkpoint and eight more than the retained reserve build.
AXI SRAM remains 111,164 bytes and SRAM4 remains 2,048.

Package/ELF, current compiled-source, production-test isolation and actual linked
STM32H7 RNG checks pass. The [source closure](../build/slaaps_firmware/companion-reserve-emission-20261023/source-closure.json)
binds 52,334 firmware entries to the sealed native snapshot plus explicit current
overlays; a separate 355-path check proves current firmware content, with seven
unused Tools differences recorded. Documentation/agent metadata and the older Git
base remain distinct from firmware identity. Ten fresh ARM probe groups compile
43 translation units. The [stack review](../build/slaaps_firmware/companion-reserve-emission-20261023/companion-resource-review-final.json)
retains MAIN 2,884/AUX 2,828 of 4,096 bytes; supervisor 4,404 of 6,500; receiver
2,824 of 5,360; sender 1,296 of 3,246; Commander 1,212 of 3,250. Measured heap and
topic-ring sizes are unchanged. New reserve-helper frames and emitted call edges
are checked separately. These static sums exclude the unmeasured overhead below;
no boot, physical entropy, runtime high-water, timing or flight qualification is
claimed. The prior reserve/runtime overlap and superseded source-check failure
remain recorded without attributing the routing interval failure to host load.

The preceding clean FMUv6X checkpoint is
`build/slaaps_firmware/companion-admission-clean-20261023`.
Its [resource receipt](../build/slaaps_firmware/companion-admission-clean-20261023/resource-comparison.json)
has SHA256 `c59eec10b7b2c3d9088bbf1858a705c2c56539f48288efa053ca3e048407defc`.
The image uses 1,947,700 of 1,966,080 flash bytes, leaving 18,380; AXI SRAM is
111,164 bytes and SRAM4 is 2,048. Relative to diagnostics, flash grows 40,480 bytes
and AXI SRAM grows 64. Package, ELF/bin, current compiled-source and production-test
isolation checks pass. The source manifest binds 49,708 firmware entries with
explicit dirty NuttX RNG/RCC and inline-owner portability overlays. Retained Git
metadata identifies the old snapshot base, not the dirty firmware content.

The prior candidate in `companion-admission-20261023` remains failed entropy
closure: its obsolete `libdrivers.a/dev_urandom.o` resolved the software provider
before the real RNG object in `libarch.a`, despite the correct generated config.
The clean volume copied only manifest-declared sources and Git metadata, excluding
all generated kernel archives and the target build tree without deleting the
candidate. The `<optional>` compilation failure and stale-provider artifact remain
preserved. The strengthened [board verifier](../Tools/slaaps/verify_board.py) reads
defined executable functions from the actual ELF plus captured generated kernel
config/header hashes. It rejects the stale artifact and accepts the clean one;
[regression receipt](../build/slaaps_firmware/companion-admission-clean-20261023/rng-verifier-regression.json)
SHA256 `941937482dd6d1d3d1b477a8538eda05d4173ee589a51c412203c4a37c583bfa`.
Future builds export that configuration automatically. Existing immutable receipts
use an explicit board-receipt-bound post-link capture. This establishes configured
and linked STM32H7 RNG code, not physical entropy quality or a successful boot.

Ten fresh ARM probe groups cover 43 translation units. Identified static chains are
MAIN 2,884/AUX 2,828 of 4,096 bytes; companion supervisor 4,404 of 6,500; receiver
2,824 of 5,360; MAVLink sender 1,296 of 3,246; Commander 1,212 of 3,250.
The [companion stack review](../build/slaaps_firmware/companion-admission-clean-20261023/companion-resource-review-final.json)
has SHA256 `25ea68dd1cf4b49e5f9fe542c46bb37bcac81129e8c35685a389cb62c1ba9b67`.
`Slaaps` is 15,200 bytes, including the 2,448-byte protocol owner and its 824-byte
adapter; these nested sizes must not be added again. MAIN/AUX endpoints are 8,776
bytes each. Packet rings require 1,408 bytes per active link, up 256; new control,
TX, local status and Commander-witness buffers total 1,544 bytes when allocated.
The event stream adds 32 bytes per link. Allocator, libc/kernel, interrupt and
other unmeasured overhead remain outside these static sums.

An isolated-test-only `cancelBench` capture hook was added after this board freeze.
[Actual board preprocessing](../build/slaaps_firmware/companion-admission-clean-20261023/postfreeze-test-only/result.json)
excludes the hook and yields identical output after normalizing only two reviewed
signature line wraps; SHA256 `4d991f1ee72320f1a9c58ed90cbd29e97d3b8df8d59d5a569784529c79457dde`.
The frozen source and artifacts stay unchanged. Updated native fixtures and unused
host-tool differences are recorded separately, not claimed as board compilation.

Earlier complete native link passes at `/var/tmp/slaaps-native-xi8g44n7` after a
const-correctness fix in the serialized owner helper. Binary SHA256:
`9dcf7e3db301609fff0997d60d6a515aa360590b81658b9b3691097c9ab63bf5`;
manifest SHA256 `ce3ba6347c5afb4b6f8a21ede77561383216d413ddbb2f9d717c6164a59e204a`.
The initial compiler failure is preserved as `compile-attempt1.log`.
Runtime remains unverified: `slaaps-native-runtime-wtcpjxpz` failed positive EDF
output after first-zero acknowledgement; `slaaps-native-runtime-f8l2116_` failed
staggered-arming non-actuation. Both stop before the companion scenario. The same
binary passes focused supervisor reproduction `slaaps-native-runtime-n79_0umv`
(2002 checks); prior sealed diagnostics binary passes control run
`slaaps-native-runtime-78ks356f` (1930 checks). These passes do not explain or erase
the failures. Targeted causal capture is being added without changing safety bounds.
All runtime directories above are under `/var/tmp/`; no physical outputs operated.

The newer unstaged companion configuration and transport work is separate from the
committed diagnostics checkpoint `54b2da3c6b`. Shared configuration exposes explicit admission,
acceleration and clock bounds plus a transient deliberate action parameter. No
measured clock assumptions or acceleration-control authority are supplied by defaults.
The first host attempt, `/var/tmp/slaaps-configuration-lhhcewny`, failed stock PX4
parameter XML generation because `ppm` is not a supported unit. The correction
retains ppm in the description and leaves the unit tag empty.

Earlier corrected host evidence: `/var/tmp/slaaps-configuration-xcz_f4op/result.json`, SHA256
`c6fe67e6924ca6435ea68a7147f0d435f028753bf5e76d0977d36f2789d1ed43`.
Debug, Release and ASan/UBSan each pass 3250 checks; stock metadata accepts 327
parameters (317 vehicle and 10 controls). The receipt captures 111 unchanged inputs,
including the newly consumed acceleration-contract header chain. This verifies
configuration/conversion and existing host behavior, not companion runtime admission.

Signed clock/status transport, full-width MAVLink stream IDs and the protocol owner
are linked; current frozen board/resource verification passes as bounded above.
Integrated native protocol runtime now passes at the bounded scope above. No hardware
was operated and no flight mode was enabled. Preserve the committed diagnostics
checkpoint independently of this work.

Focused native compilation of the actual MAVLink main, receiver, stream registry
and Commander translation units passes with current generated topics and the sealed
native build's strict flags. Receipt:
`/var/tmp/slaaps-companion-compile-aifwn9lg/attempt3/result.json`, SHA256
`833f2ccd8d54e583c95aebd5cdcec8663fe1d0cef478e42dbcb828eae10bfc8d`.
The original sealed build is unchanged; two earlier scratch-include setup failures
are retained. This is compilation evidence, not a full link or runtime result.

Dedicated-board identity follow-up explicitly selects HSI48 for RNG after checking
readiness, preserving unrelated clock bits. Register/error simulation, a failing
pre-fix control, actual ARM compilation and style checks pass; receipt
`build/slaaps_identity/rng-mux-followup-20261023/result.json`, SHA256
`2267522ff7e6df97dd003319ba2bf8ced45ce275ed9d03b300012c20931f7db8`.
The RNG object grows 20 text bytes. Full firmware fit and actual linked-driver
selection are verified above; board startup, entropy quality and hardware timing
remain unverified.
