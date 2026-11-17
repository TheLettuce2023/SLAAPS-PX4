# P2F direct selected-state and status-change provenance

2026-09-29, Codex (GPT-5). Closed native observation increment accepted only
at the bounded evidence scope below. It does not qualify estimator health, the
EKF2 selector or multi-instance operation, positive admission, an acceleration
controller, or any output. C1E-N1 remains the only accepted runtime admission
profile and remains negative-only.

## Boundary advanced

P2F extends the accepted P2E private non-lockstep POSIX source view. After the
direct EKF2 publisher advertises its topics, the observer binds the actual EKF
instance and the actual `vehicle_attitude`, `vehicle_local_position` and
`estimator_status_flags` publisher instances. This increment deliberately
accepts only the direct, non-replay, non-multi EKF instance-zero path; it does
not claim that the EKF2 selector selected that instance or observe selector
lifetime, failover or handoff.

The private overlay records the Boolean result of the real
`estimator_status_flags` uORB publication, the published control/fault flags,
the native control/fault change counters and the publisher cache visible at the
end of the same EKF iteration. A counter may be monotonic with at most one step
per retained iteration. A changed counter requires a matching successful
status publication. An uncommitted EKF update cannot carry such a publication,
and a committed update must make the current raw flags equal the publisher
cache. The P2E rows remain present for an independent row-by-row binding.

One initial retained iteration precedes any status publication. It therefore
contains the previous publisher cache rather than the current raw status and is
explicitly counted as one stale-status row. This is observation of a real
startup boundary, not a ready-state allowance. The counter and cache checks
also reject an ABA-style change that returns to the same flag bits.

The observed local-position acceleration remains PX4's NED acceleration of its
body-origin output and is evidence only. It is not substituted for the vehicle
contract's commanded inertial acceleration at the CG in rotating body FRD. The
vehicle remains four independent X-canards and one fixed rear EDF, with no main
wing or thrust vectoring. P2F does not connect H5 or any actuator path.

`SelectedState` owns one fixed 2,048-record archive and one pending status
publication. Its hooks allocate no memory and use lock-free source/lifecycle
bookkeeping. One explicit owner controls begin, stop intent and finish. The
first fault is sticky, recording closes, and later events cannot repair state.
A second EKF constructor is rejected. Export requires explicit stop intent,
observed destructor entry, no pending status and a bounded drain of in-flight
hooks. The 2,048-record and cleanup bounds are inherited diagnostic storage and
cleanup constraints, not flight timing or operational budgets.

Every export uses `p2f-selected-state/v1` and hard-codes
`health_policy_configured=false`, `health_qualified=false`,
`positive_admission=false` and `eligible_for_control=false`. No health mask,
freshness budget, gain, limit, priority, fallback or physical coefficient was
introduced.

## Independent verification

Final package: `/var/tmp/p2f-selected-vl0q1aop` in Ubuntu-24.04 WSL. It was
generated from the exact current source registry, including the independent
variant checker. Manifest SHA-256:
`aba1ffe7da85f9565273a3d8a713d8d4f2068bb4836f92ca558dda50591bac58`.
Independent Debug check SHA-256:
`eb1d479ec299451cbb7deafaca3b59d0b458fabe177a764a49a9a2cfb0d1bca8`.
Debug executable SHA-256:
`9bb65862f640bef0a92e5f74c567ca1332f56a94b2303fe42c34141d313039c2`.

The fresh Debug closed profile compiled 440 targets. The normal process retains
1,199 P2D/P2E/P2F rows; the restart process retains 1,197. Both bind estimator,
attitude publisher, local-position publisher and status publisher instances as
`[0,0,0,0]`. Each observes six successful status publications, one
control-change transition, zero fault-change transitions and one initial stale
status row. Restart retains the first lifetime and rejects the actual second
constructor. Neither process was forcibly terminated.

The fixture contains 1,200 recorded `sensor_combined` inputs. The smaller P2F
row counts reflect native input coalescence before the EKF iteration hook; the
observer does not synthesize a row for an unobserved iteration and evidence was
not cherry-picked. Every retained P2F row passes the inherited P2D raw/typed and
P2E committed-state correspondence checks. The recorded accel and gyro device
ID remains `1310988`, with the P2E-recorded calibration counters 0 and 1. Those
are hash-bound SITL facts, not deployment-sensor qualification.

Eight focused checker tests pass: one valid bound trace and rejection of
invented authority, instance rebinding, a Boolean binding alias, a counter gap,
a missing change publication, a status-cache mismatch and a P2E row mismatch.
The shell startup scripts pass syntax checking, and the Python sources compile
without bytecode retained in the workspace.

Fresh closed-profile Release and ASan/UBSan builds use the same
`px4_sitl_p2f-default` board profile as Debug. All three generated
`boardconfig` files have SHA-256
`aa7cde28fc44eda8741e1590198f2a98dd0c5a85f87ffc7ac00b661ef7f9e121`.
Release normal/restart retain 1,195/1,197 rows; sanitizer normal/restart retain
1,197/1,195 rows. Every case has the same `[0,0,0,0]` binding, six status
publications, one control transition, zero fault transitions, one stale row and
all four authority fields false. Logs contain no PX4 error, ASan, UBSan or
runtime-error signature and no actuator artifact. The independent variant
results and executables are retained as:

- Release check:
  `da24af380a3466230bd196e2e0926ff675740714caab169e08a41d19f9abefd2`;
  executable:
  `245e672bcab09c710f5d1690c05d273404ab79944adac5639b0b4b61ecd0f759`.
- ASan/UBSan check:
  `518f840d8744cbc60dbbc98a31e32b6bb2a407d462da6dcf5b2c563950f7f247`;
  executable:
  `4ca55e572c17b2b2473b9f8e71fad59b89ed1a9223fa52522e6b3499296b1172`.

The Debug P2F object contains 10,357 bytes text, 8 bytes data and 245,903 bytes
BSS. Its largest reported self-frame is the 320-byte dynamic/bounded export
frame. These are closed POSIX diagnostic compilation measurements, not NuttX
acceptance, linked firmware cost or peak-stack proof. Object SHA-256 is
`7418b7697db24012c230e090fba834c4876290b7b5c8f98ba193a2c2f729f2c3`;
stack report SHA-256 is
`534c2daea94847947d9ce0ef330b02df5f8ef5d51b33d477ce0de6275adfdbc2`.

Retain failed and superseded packages as diagnostic history, not acceptance:
`/var/tmp/p2f-selected-mqzp8tpv` failed configuration before runtime;
`/var/tmp/p2f-selected-8bhzuwqm` and
`/var/tmp/p2f-selected-4v3irl3c` are development packages predating the exact
final source/checker set; and `/var/tmp/p2f-selected-6qo1v129` correctly failed
runtime validation after the required EKF2 GPS parameter schema was omitted.
Broad-label `release-fixed3` and `sanitize-fixed` subbuilds under the older
package are diagnostic only because they merged the stock default board rather
than using the exact closed `p2f-default` label.

## Compilation versus linked firmware evidence

Scoped PX4 AStyle passes for the P2F C++ files. The unchanged H1-H5 strict
Debug, Release and ASan/UBSan suites each pass 843 H1, 182 H2, 233 H3, 68 H4
and 73 H5 checks: 1,399 total per configuration.

A clean WSL-native source view at
`/var/tmp/research-acceleration-p2f/px4-source` produced fresh complete default
SITL and Pixhawk 6X builds. ARM GCC 13.2.1 completed the Pixhawk compile, link
and package. The default build logs contain no P2F or research control compile
line, and both default ELFs contain zero matching P2F/H1-H5 research symbols.
Thus these builds prove that the surrounding PX4 source still compiles; they do
not measure linked P2F/H1-H5 firmware cost.

The default SITL ELF is 58,599,328 bytes with SHA-256
`9d2eff414f5b116621e07ccad94e7c605d3153343f7d26bf4282c5c2e00b5b81`.
Its configuration/build logs have SHA-256
`4ce9769c087a762f6a68eaab615795734c46bf2a00a10a403e51d1b0a2e784fe`
and `053d7847b3a0831f81789484f80b40762d900be2cd2febfb5d7cfdf2e43411b7`.

The complete default Pixhawk image reports 1,947,412 bytes of flash, 99.05% of
1,920 KiB. This is linked default-firmware resource evidence that excludes all
research code in this increment:

- Pixhawk ELF, 49,391,556 bytes:
  `7f3dcd1ca526e64cf5b02e9fd032147da1ff4e58a0e4ac298cc0d21829cd76dd`.
- Pixhawk BIN, 1,947,412 bytes:
  `e89a56a2775c70814712af66f39a6ed7952f09730d3cb434d596ce6910367af9`.
- Pixhawk PX4 package, 1,853,994 bytes:
  `aaa6f46f38feddeb90a2fed41db6663728818107a5e5ef564a5c597e8c382829`.
- Configuration, ELF-build and package-log SHA-256:
  `d0cdb60ef66b8de38ecf024cdc6600486ba2389c00298421af68812440dff1e1`,
  `06fb2acd038150d5cceaa985ee0dd26be299a59bd3da7619991cf1e2dc723571`
  and `abc65b2633bcd7a628bb48e4848aee365feb512eaa4c02a5c2a505664cda84d1`.

The link reports 1,939,400 text, 8,012 data and 105,596 BSS bytes. AXI SRAM is
111,420 of 512 KiB (21.25%) and SRAM4 is 2 KiB of 64 KiB (3.12%); the other reported
SRAM banks are zero. These numbers describe only the default linked firmware.

## Remaining gates

P2F verifies actual direct instance-zero publisher binding and status-change
provenance. It does not verify the selected estimator through the EKF2 selector,
multi-instance lifetime, failover/handoff, an admissible health predicate,
complete status-mutation provenance, real deployment sensors, immutable
freshness/skew/latency budgets or a qualified ready-state owner. P3 legacy
sample-clock/provider/map continuity and P4 operator/transport capability plus
a distinct positive protocol also remain open. No startup, registration,
airframe, arming, uORB control publisher, actuator output, fallback, physical
coefficient or operational default was added. No flight readiness follows.
