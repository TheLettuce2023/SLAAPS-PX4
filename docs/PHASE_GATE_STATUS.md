# Research vehicle phase gates

2026-10-06 SM5 synthetic steady-bias native diagnostic: a separate private
Gazebo plugin applies `(+0.010, -0.015, +0.012) N.m` body torque only while
fixed simulated EDF thrust is enabled. The unchanged generated-profile PX4
binary passes two level/body-X-up mixed-axis cases, 14/14 existing checks
and 12,931 control cycles. All 12,944 enabled physics rows log the bias and
match timestamped PX4 targets; final mean canard moment counters the bias
within `0.0002193 N.m` per component in the sampled windows. The
[133-file receipt](STABILIZED_MANUAL_SM5_STEADY_BIAS_SITL.md)
`706ff1e6b19cb81d1d29cd74d656bd73c4a9025d07235a380c432dafcdbc893a`
and 88-file parent independently rehash clean. This is exploratory synthetic
torque, not a measured EDF moment or accepted/physical scope expansion.

2026-10-06 SM5 one-condition net-moment screen: source-bound trim bias,
uncertainty and demanded physical net moment are now included in a
conservative local-linear static test. The zero-bias synthetic fixture's
`+/-0.04 N.m` demand per axis needs squared normalized correction `3/100`,
below guaranteed `1/4`. Six tests and the
[16-file receipt](STABILIZED_MANUAL_SM5_NET_AUTHORITY.md)
`ff487d1ad8ef830a9e456f5df8789d0c0e6e7c1a717ba8e765169e93835fe00d`
pass and rehash clean. Real bias measurements, demand approval, slew,
operating-envelope and physical gates remain open; no SITL scope changed.

2026-10-06 SM5 native synthetic actuator trace: the two existing
generated-profile SITL cases have 12,953 enabled fixture rows, each matching
its timestamped PX4 canard command. All 15,817 successive truth-row
transitions satisfy the declared 50 ms virtual-servo recurrence, and all
15,819 rows satisfy the logged angle scale. The
[5-file audit receipt](STABILIZED_MANUAL_SM5_NATIVE_ACTUATOR_LAG.md)
`d4821661c7fdc0ea23960105b58ec1c6c666add1e319c75319c61cd33ccee127`
and 88-file native parent rehash clean. This adds no native case or accepted
campaign expansion; loaded-servo, aerodynamic and vehicle gates remain open.

2026-10-06 SM5 allocator command history: the actual private C++ allocator
passes eight corner hold/reversal/release sequences at 10 ms, 288 calls total.
Independent checks confirm previous-command continuity, recomputed moments,
objective, bounds and signed slew. The longest sampled time to allocator
residual norm at most 0.0011 is 4/8/4 calls by phase; this is not physical
settling. The [13-file receipt](STABILIZED_MANUAL_SM5_ALLOCATOR_SEQUENCE.md)
`009e14b45ade7890b67ce429cb7b58d3b34216105ef93a316b1c9058ffcf8f57`
rehashes clean. Actuator lag, closed-loop and vehicle gates remain open.

2026-10-06 SM5 compiled allocator grid: the actual private C++ allocator,
compiled with the recorded PX4-owner flags and generated profile, passes
2,662 independent synthetic moment requests from trim. All output commands,
achieved moments, residuals and objectives match the declared profile;
the 10 ms slew limit binds and yields up to 0.129904 normalized residual,
versus 0.00000693 at 1 s. The
[13-file receipt](STABILIZED_MANUAL_SM5_ALLOCATOR_GRID.md)
`c73cead601efd5fb796393648eca7c9cc1c72b35079107e22903a27cd815f005`
rehashes clean. This is a finite host diagnostic, not a new accepted SITL
campaign, dynamic tracking or vehicle/physical qualification.

2026-10-06 SM5 static incremental moment-box screen: the exact one-condition
rank lower bound and symmetric canard command margin give a sufficient
local-linear guarantee for the declared synthetic +/-0.1 normalized moment
box (`3/100 <= 1/4` in squared norm). Six tests cover conservative and
rejection boundaries; the [13-file receipt](STABILIZED_MANUAL_SM5_STATIC_AUTHORITY.md)
`913cb7b27308dfa2a158e6a05b53ed1a08dd0a48b83f1c332e7f9ba981169f6a`
rehashes clean. No net trim, slew, EDF-bias, vehicle envelope or physical
acceptance follows. The accepted 31-case SITL and other gates are unchanged.

2026-10-06 SM5 one-condition rank screen: exact rational bounds on the
compiled 3x4 allocation matrix and reported elementwise uncertainty give a
conservative sufficient full-row-rank result for the zero-uncertainty
synthetic fixture (`L = 1/4`). Six tests cover positive, inconclusive,
rank-deficient interval-box and source-tamper cases. The
[11-file receipt](STABILIZED_MANUAL_SM5_INTERVAL_RANK.md)
`3c648e090fb2c5edcaa12c7d92348470a9a61a0d27997341fa466811613cd443`
rehashes clean. No measured profile, bounded moment envelope or physical
acceptance follows; all existing SITL, stock and bench gates are unchanged.

2026-10-06 SM5 generated-allocation C++ binding: the private configuration
and actual allocator were compiled with the sealed owner's flags. Six
independent copied-header changes to effectiveness, trim, each bound and
each slew direction produce corresponding command changes under declared
requests. The [53-file receipt](STABILIZED_MANUAL_SM5_BINDING_PROBE.md)
`d0f781066bf19909468a3088fd812c5a50401cf2b73ad3ef9455015dd5d35e06`
rehashes clean. These altered values are host diagnostics, not admitted
synthetic or vehicle profiles; no SITL/physical acceptance gate changed.

2026-10-06 SM5 synthetic report admission guard: the header generator now
compares the whole report with a fresh compile of the exact fixture input,
rejecting changed control values even if `input_sha256` is copied. The
[14-file receipt](STABILIZED_MANUAL_SM5_ADMISSION_GUARD.md)
`b4e79b8a3bf6e5160b55bd52640945d919703279b7cadb6c1519675206e74ce2`
rehashes clean; generated C++ is byte-identical to the earlier sealed SITL
header. No PX4 rebuild, new runtime case or vehicle-profile admission is
claimed. All physical gates remain open.

2026-10-06 SM5 generated synthetic allocation SITL: a separate private PX4
binary consumes a header from the sealed offline fixture profile and reports
the complete input SHA-256 at runtime. Two exploratory mixed-axis maneuvers
pass at level/body-X-up over 12,953 audited cycles; the 88-file
[integration receipt](STABILIZED_MANUAL_SM5_GENERATED_SITL.md)
`cf2068502959c952a5d4ec858ebe4aa905ebba7fc771c4016313807dce53d3be`
rehashes clean. The first build's one-line status truncated the hash and was
rejected before command phases; a fresh build fixed status output. The
accepted 31-case binary is unchanged. No measured plant profile, vehicle
gain tuning, physical authority or flight acceptance follows.

2026-10-06 SM5 allocation-input preparation: a strict **offline single-
condition compiler** converts signed body-FRD canard moment derivatives and
loaded deflection slopes to the allocator's normalized 3×4 matrix. The
source-hashed synthetic fixture reproduces its existing matrix exactly;
five tests cover sign, uncertainty, invalid inertia/rank, malformed and
duplicate-key data, and an existing-report overwrite guard.
The [input contract and bounded verification](STABILIZED_MANUAL_SM5_ALLOCATION_INPUT_CONTRACT.md),
receipt `d7f68d120f21543ddb70289a039189f6fed9d48db46d5f3f5a418ea551b0ed1c`,
preserves 379 preparation anchors and independently rehashes 13 artifacts.
No measured SLAAPS profile, operational gains, robust authority or physical
acceptance follows. Accepted SITL scope stays 31 corrected-profile native
cases; stock, H10, negative-only C1E-N1 and physical gates are unchanged.

2026-10-05 SM4 direct owner gyro-epoch probe: a **separate private PX4
diagnostic ELF** passes one exploratory body-X-up/+ mixed-axis case over
6,478 cycles. A first one-copy gyro probe was invalidated by the eight-entry
uORB queue and is retained with a 56-file receipt. The corrected bounded
queue drain matches selected gyro ID and raw relay samples on every active
cycle; at most two entries were drained, and both gyro and selected-rate
sample ages peak at 84 ms. The original 20 ms rate stall did not recur, so
its cause/frequency remain unknown. The [source-bound audit](STABILIZED_MANUAL_SM4_GYRO_EPOCH.md)
receipt `768bd8f151c5f7f1edc107fefadfcf79c9986c0cdd6e38ab000f3c4143c7cbba`
preserves 370 preparation anchors and independently rehashes 56 artifacts.
Accepted scope stays 31 corrected-profile native cases. Stock, H10,
negative-only C1E-N1 and physical gates are unchanged; vehicle measurements
remain unavailable.

2026-10-05 SM4 stock ULog rate-path repeats: **three exploratory body-X-up/+
cases pass** over 19,423 active control cycles under alternating 76/84 ms
pilot sends and exact 80 ms raw-IMU delivery. The earlier 20 ms active
selected-rate stall did not recur; selected-rate age stayed at most 84 ms.
Logged active gyro/rate sample gaps were at most 4/8 ms, but each zero-dropout
ULog omitted 323–443 distinct rate samples selected by the owner. The
[bounded rate-path audit](STABILIZED_MANUAL_SM4_RATE_TRACE.md), receipt
`d22c9328b24a9dbed643e8197f47652a73be598f1ce3e3f2493b08251f629227`,
preserves 364 preparation anchors and independently rehashes 152 artifacts.
The original stall's cause and frequency remain unresolved. Accepted scope
stays 31 corrected-profile native cases. Stock, H10, negative-only C1E-N1
and physical gates are unchanged; vehicle measurements remain unavailable.

2026-10-05 SM4 selected-rate boundary diagnosis: the earlier body-X-up/+
phase-controlled case has a **20 ms selected angular-rate publication gap**
and four rate ages 88/92/96/100 ms. Five raw IMU relay publishes remained
4 ms apart at exact 80 ms offsets, selected attitude advanced and manual age
was only 36–48 ms. The [read-only closed-log correlation](STABILIZED_MANUAL_SM4_RATE_BOUNDARY.md),
receipt `cf76f66bec769fd731675b7e41317599484d20148b5dcf7a935e25856bb55b3c`,
revalidates all 169 parent artifacts and four derived files. The exact
bridge/gyro/rate producer cause remains unresolved; no state-age margin is
claimed. Accepted scope remains 31 native cases. Stock, H10, negative-only
C1E-N1 and physical gates are unchanged.

2026-10-05 SM4 controlled single-packet loss: **four exploratory hold/rate,
level/body-X-up negative cases pass**. One scheduled pilot packet is omitted
at alternating 76/84 ms cadence, producing an exact 160 ms send gap while
raw IMU delivery remains 80 ms. The native owner latches manual fault 5;
last published manual age is 100 ms and consumer-off begins 108 ms after the
last sample in every case. Centered packets do not auto-recover; an explicit
new session holds. [Exact omission audit](STABILIZED_MANUAL_SM4_PHASED_SKIP.md),
receipt `f7a4e9431f4464cc2d5faf1e1235441b9945fda447a12eea93d0a2f19ce21307`:
5,983 cycles and 185 artifacts independently rehashed. A first recovery
script failed `NotCentered` because it enabled before PX4 selected centered
input; preserved separately. Accepted scope remains 31 cases. No vehicle
or physical gate changes; stock, H10 and negative-only C1E-N1 unchanged.

2026-10-05 SM4 phase-controlled pilot timing: **four exploratory level/
body-X-up, both-order mixed-axis cases pass** with logged 76/84 ms pilot-send
intervals and exact 80 ms raw-IMU packet delay. 25,860 audited cycles;
manual age reaches 88 ms, while four body-X-up/+ selected-rate cycles reach
88/92/96/**100 ms**, the unchanged freshness boundary. Worst physical rate
tracking 0.017210 rad/s, peak hold 0.424013 rad, final hold 0.003933 rad
remain within unchanged limits. The [exact phase/physical audit](STABILIZED_MANUAL_SM4_PHASED_JITTER.md),
receipt `3e2e43a4ba4f205a3bc7e3e284323e5044f974a259d13164f98798ecd727724d`,
independently rehashes 169 artifacts. A first scheduler using a 100 ms-flushed
truth log failed with manual fault 5 and is preserved. These observations do
not establish a general timing margin. Accepted scope stays 31 cases; vehicle
and physical gates remain open. Stock, H10 and negative-only C1E-N1 remain
unchanged.

2026-10-05 SM4 combined-timing input loss: **two exploratory level/body-X-up
negative cases pass** at an 80 ms pilot send period and exact 80 ms raw-IMU
delay. The native owner latches manual fault 5, rejects re-enable in the same
session, and the consumer turns off 108 ms after the last manual sample in
both cases. A new explicit session restores hold. The
[closed-log loss audit](STABILIZED_MANUAL_SM4_COMBINED_LOSS.md), receipt
`a78b02f86b118e31848a7a8729d1d6d9505e5945b99c14491761d23e211d888b`,
independently rehashes 111 artifacts and audits 1,014 control cycles. Sender
phase was uncontrolled; this is not a jitter or reaction-time envelope.
Accepted scope stays 31 cases; vehicle and physical gates remain open. Stock,
H10 and negative-only C1E-N1 are unchanged.

2026-10-05 SM4 combined-timing repeats: **eight exploratory seed 43/44,
body-X-up/level and both-order mixed maneuvers pass** with 80 ms pilot send
period and exact 80 ms raw-IMU delay. 51,747 audited cycles; selected-manual
and selected-rate ages reach 88/84 ms. Worst physical rate tracking is
0.017599 rad/s, peak hold 0.439332 rad and final hold 0.006657 rad against
unchanged 0.08/0.5/0.02 limits. The [repeat audit](STABILIZED_MANUAL_SM4_COMBINED_REPEATS.md)
receipt `279793a3bc0eb399db517c8a07a3b6869d7aa59273676c90be4083c3cc4d0ab0`
independently rehashes all 269 artifacts. With the earlier seed 42 matrix,
this setting has 12 finite cases/77,625 cycles, not a statistical or jitter
envelope; wall-clock sender phase was uncontrolled. Accepted scope remains
31 cases; vehicle and physical gates remain open. Stock, H10 and negative-only
C1E-N1 are unchanged.

2026-10-05 SM4 combined timing: **four exploratory body-X-up/level, both-order
mixed maneuvers pass** with 80 ms pilot send period and 80 ms raw IMU delivery.
25,878 audited cycles; maximum selected-manual/state ages 88/84 ms against
unchanged 100 ms limits. Worst physical rate tracking is 0.017526 rad/s, peak
hold 0.437710 rad and final hold 0.006555 rad against unchanged 0.08/0.5/0.02
limits. [Exact combined-timing audit](STABILIZED_MANUAL_SM4_COMBINED_TIMING.md),
manifest `913dd4b29e79a83bcf2c6109f5efd05ed3e98fe1308a7b943acaf1b7c8a0a6f3`;
161 artifacts independently rehash clean. This does not establish timing
jitter or deployment margin. Accepted scope stays 31 cases; vehicle and all
physical gates remain open. Stock, H10 and negative-only C1E-N1 are unchanged.

2026-10-05 SM4 raw IMU transport: **seven exploratory estimated-state mixed
maneuvers pass** over 45,284 audited control cycles with direct/20/40/80 ms
IMU delivery. The selected-rate age reaches 84 ms at 80 ms delay; worst peak
physical hold is 0.426482 rad against the unchanged 0.5 rad limit. At exactly
100 ms raw delivery, the owner latches `StateRejected` (fault 6) during initial
hold, publishes no control cycles, and the consumer never enables. The first
boundary probe is retained with a corrected subsequent negative score.
[Exact source, timing and physical audit](STABILIZED_MANUAL_SM4_IMU_DELAY.md):
positive manifest `7f82292dea39a624657629a3de4a03813163297495a30749e4d3992872a2254b`,
boundary manifest `f96cc4f8038eb9e2e2b8019ce5bca8e5747ef0570988e30e9e69eb3738aa2c35`;
242 and 62 artifacts independently rehash clean. Accepted scope remains 31
cases; timing repeatability, publisher identity, vehicle and physical gates
remain open. Stock, H10 and negative-only C1E-N1 are unchanged.

2026-10-05 SM4 magnetic packet transport: **six exploratory mixed-axis runs
pass** over 38,799 audited cycles with declared 0/20/40 ms added delay. Actual
sample-to-delivery offsets are 4/20/40 ms; the zero setting incurs one 4 ms
fixture tick. The two level 40 ms runs end at 0.016142 and 0.004134 rad
physical hold error against the unchanged 0.02 rad limit, so repeatability and
a general delay margin remain open. Exact PX4 ELF unchanged; strict private
Gazebo plugin rebuilt. [Source, packet and physical audit](STABILIZED_MANUAL_SM4_MAGNETIC_DELAY.md),
manifest `8f42ff4c6eef5da40313313d8415287680d457526a6491ab5aad0fa864f0702e`;
211 artifacts independently rehash clean. Accepted scope remains 31 native
cases. No stock, bench, acceleration or physical gate changes.

2026-10-05 SM4 manual-link cadence: **three exploratory live-clock cases pass**
over 19,408 audited cycles with 60/80 ms pilot send periods. Maximum selected
sample age is 84 ms versus the unchanged 100 ms limit; worst physical tracking
is 0.015877 rad/s and final hold 0.005229 rad. The 88-file receipt rehashes
clean: `0354b1817e97aa1942537bcd20203c87e16676a739e5de4a06954e9a4c5ab2ab`.
[Exact cadence and publisher-epoch record](STABILIZED_MANUAL_SM4_CADENCE_EPOCH.md).
A paused-physics `ekf2 stop` timed out before restart, so that separate test
does not establish between-poll publisher identity. The accepted checkpoint
remains 31 cases; broader jitter, sensor delay and publisher-epoch testing
remain open. No physical or positive-acceleration gate changes.

2026-10-05 SM4 mixed pilot commands: **four exploratory level/body-X-up,
both-order unequal-axis/reversal cases pass** over 25,851 audited cycles.
Body-rate setpoints match `(1, -0.6875, 0.375)` rad/s and its reverse within
`1e-7` rad/s; worst physical rate tracking is 0.015560 rad/s and final hold
error 0.006917 rad. Each case has actual three-axis impulse, saturation
feedback, retained quaternion reference and consumer disable.
[Exact mixed-axis scope and receipt](STABILIZED_MANUAL_SM4_MIXED_AXES.md),
manifest `e81ee37b5f1850de9f01f2a6274b3792fe438fd6d10d19d4b867b694eb48a937`;
114 artifacts independently rehash with zero failures. The accepted checkpoint
remains 31 cases; timing/transport, broader input patterns and publisher identity
remain open.

2026-10-05 SM4 noise/bias follow-up: **five exploratory full-axis nose-up
observations pass** across 32,370 audited cycles: two additional Gazebo seeds
and three signed gyro-bias injections at ±0.0010182 rad/s. Continuous ULog
confirms injected sensor means in the three bias cases with zero dropouts.
Worst biased final physical hold error is 0.019376 rad < 0.02, a narrow margin.
[Exact probe scope and receipt](STABILIZED_MANUAL_SM4_ROBUSTNESS_PROBES.md),
manifest `9bf864c0f4db9f97dfedc018161abac624e57765d2fe1fce18a410251b58f0de`;
219 files independently rehashed with zero failures. This does not change the
31-case accepted checkpoint below or qualify a bias/noise envelope or vehicle.
Unequal-axis, timing/transport and broader publisher identity remain open.

2026-10-05 SM4 corrected runtime profile: **31 actual native cases PASS**:
one deterministic same-clock dispatch, four full-axis coupled cases, 12
individual-axis/six fault-recovery cases and eight native state barriers.
68,726 published cycles audited; all live parameter comparisons pass.
Worst coupled final physical error 0.009438 rad < unchanged 0.02 rad limit;
tracking 0.028713 rad/s < 0.08; independent producer-stall expiry exactly 100 ms.
[Exact qualification and historical profile erratum](STABILIZED_MANUAL_SM4_RUNTIME_PROFILE_FIX.md).
The prior startup-script bug omitted bias settings, so earlier matched-prior
claims are withdrawn while numeric artifacts remain preserved. Corrected
receipts: `rjr0eutz` campaign `447823f2cdb88e22759d58b9365cd0508370053738011aa966fc452b1c050da3`,
coupled `40753f83d103cfca58cc65f2f2481102185521fa20bf0d3671ac72037a3c2aa2`;
`qmf78glt` barriers `1536d2f78598e942746b4a9e52e2194eb1a151c705e908aca82450f432698948`.
This advances the declared synthetic software scope only. Broader robustness,
epoch/candidate identity, accepted vehicle plant and physical gates remain open.
Measurements unavailable, CAD pending; goal active. Stock baseline, H10 and
negative-only C1E-N1 are unchanged.

2026-10-05 SM4 coupled-control checkpoint: **INCOMPLETE, three of four cases
pass**. The current synthetic candidate improves full-axis tracking to 0.028649
rad/s, but body-X-up/- physical hold is 0.026301 rad against the unchanged 0.02
limit while the estimate reports 0.003065 rad. All four pass saturation feedback
and anti-windup checks; 25,869 published cycles are audited. Native duplicate
dispatch filtering is a candidate fix with zero observed branch executions in
this matrix, so regression coverage remains open. Fresh private native/plugin
builds pass; no coupled acceptance or current-binary 18/eight-case regression
closure follows. [Exact incomplete record and next steps](STABILIZED_MANUAL_SM4_COUPLED_CONTROL.md),
evidence `research-stabilized-sitl-mm7_zto8`, manifest
`1da51d0b2c27f71c71a316c8bad2e437cde7c5fdc11ead2f2eaca3ed5c0deeb2`.
Earlier accepted profiles and every failed predecessor remain preserved.
The user reports measurements are not yet available. Synthetic diagnosis can
continue; vehicle-specific allocation/tuning await CAD and measured authority,
servo and EDF inputs. All physical/positive-acceleration gates remain unchanged.

2026-10-05 SM4 native state barriers: **eight level/body-X-up actual reset,
publisher-withdrawal, rapid same-ID restart and unsupported multi-estimator
profile cases with explicit recovery PASS**. Unchanged firmware/plugin;
3,994 audited cycles and 313 preserved checkpoint anchors. Withdrawal/restart
revoke with state ages 20-24 ms, actual reset counters advance 1 -> 2 without
publishing across the reset, and each consumer off follows revoke by 4 ms.
[Exact scope](STABILIZED_MANUAL_SM4_STATE_BARRIERS.md), evidence
`research-stabilized-sitl-zacmmnz6`, manifest
`fe6c5d26ad54dd8b732ba7a97a77246651b45dd7f3824edb483dadf4f65971f6`.
General between-poll publisher-epoch/candidate identity, sensor-diverse failover,
broader bias/delay/transport robustness and vehicle-supported control remain
open. No production code, baseline, physical or positive-acceleration gate changed.

2026-10-05 SM4 bounded campaign checkpoint: **12 actual estimated-state
level/body-X-up, three-axis, both-sign rate/release/impulse cases and six
fault-and-explicit-recovery cases PASS**. Audit: 38,617 published cycles;
302 checkpoint anchors preserved. Worst final physical hold error 0.006601 rad
against the unchanged 0.02 rad limit. Suspended PX4 with live Gazebo expires
the independent consumer exactly 100 ms after its last command. The declared
zero-bias synthetic IMU uses explicit initial bias priors; no physical sensor
qualification follows. [Exact result and retained failed run](STABILIZED_MANUAL_SM4_CAMPAIGNS.md),
evidence `/var/tmp/research-stabilized-sitl-r27tx0nr`, manifest
`d60c0465e69c7838b76d59cb14fbf039544338dee53122b80a5e549afa8cbac1`.
Broader SM4 reset/switch/lifecycle/robustness coverage, SM5 vehicle-supported
tuning, full flight-mode admission and all physical gates remain open. H10,
stock Cessna and negative-only C1E-N1 are preserved.

2026-10-05 SM3 integration: **private native PX4/Gazebo linkage, physical
three-axis fixture check and centered estimated-state hold/disable smoke PASS**.
Final repeat: 1,255 Hold cycles over 5.016 simulated seconds, fixed 10 N thrust,
consumer disable to zero thrust. Refreshed strict host configurations retain
123,083 checks each. [Exact scope/evidence](STABILIZED_MANUAL_SM3_SITL_INTEGRATION.md),
manifest `69d9aa65a7931c7273425180a043e42e2407512f7428bbb645d0160744ef5970`.
SITL pilot-rate, disturbance, nose-up and loss/supervisor campaigns remain open;
this is a connection/lifecycle smoke, not stabilization or physical acceptance.
H10, stock Cessna and negative-only C1E-N1 remain unchanged.

2026-10-05 SM2 / host SM3: **output-disabled stabilized manual core and synthetic
host rotational feedback verification PASS**. Single owner implements confirmed
body-rate sticks and quaternion hold on release, reuses actual PX4 rate PID and
raw-manual validation, and allocates four canards with bounded weighted least
squares, slew limits and saturation feedback. Strict Debug/Release/ASan-UBSan
each pass 123,083 checks including 12 level/nose-up pilot/impulse campaigns and
independent allocation KKT checks. [Exact record](STABILIZED_MANUAL_SM2_SM3_VERIFICATION.md),
evidence `/var/tmp/research-stabilized-host-0bqkxvm9`, manifest
`a519c5feb3e00456ac5ac58867d2e46cebaa021665bc7f3ff016004673dff808`.
No PX4/Gazebo runtime, firmware build or physical output; all 281 earlier graph
anchors preserved. Estimated-state SITL, vehicle plant and physical acceptance
remain open. H10 and negative-only C1E-N1 scopes are unchanged.

2026-10-05 SM0: the user set **stabilized manual SLAAPS control** as the new
priority and confirmed body-rate commands with attitude hold on release, a
synthetic rotational fixture at declared airflow and fixed simulated EDF thrust.
[SM0 goal/input record](STABILIZED_MANUAL_SITL_GOAL.md) defines the proposed
feedback architecture and software/physical dependencies. This advances the
goal and decision record only: no stabilized controller, allocator, Gazebo
fixture or acceptance result exists yet. Synthetic software development can
proceed without the already-pending CAD; vehicle-supported tuning and flight
still require valid plant/authority evidence. H10, stock Cessna, C1E-N1 and all
physical/positive-acceleration gates retain their recorded scopes.

2026-10-02 H10 fix: **snapshot-ordering defect closure and scoped capture-only
manual bench integration verification PASS**. This supersedes the H9 software
blocker below, not its retained evidence or any physical gate. Selected input and
stock selector candidates are captured before reading their evaluation clocks;
genuine future timestamps still reject and latch neutral. Strict portable suites
pass 2,431 checks each; stock compatibility 28 and evidence checker 19 tests pass.
Three consecutive full native runs pass all 19 cases / 3,541 checks each. Fresh
SITL/Pixhawk 6X compilation and required ELF symbols pass; final linked ARM
text/data/BSS are 1,948,768/8,044/105,596 bytes, not runtime stack/timing evidence.
[H10 exact evidence](MANUAL_BENCH_CONTROL_H10_FIX.md), manifest
`b8176eaf9f23098b9800e8ab4c33bc8b0e41adf0015aa222310672631acb9c43`.
Installed physical profile, signals, QGC/gamepad/SiK, power, stop and wind/fixture
acceptance remain open. No startup/default changes, EDF, arming policy, flashing,
staging or physical actuation; C1E-N1 and all positive/flight gates remain intact.

2026-10-02 H9 automated active-path testing: **acceptance FAIL; no acceptance
gate advanced**. H8 remains the last advanced implementation gate. Final actual
PX4 selector/owner/mixer and MAVLink-loopback suite passes 11/17 cases; debugger
confirms an H8 clock-before-copy race causing `InputRejected/InvalidTime` and
latched neutral. Production behavior is unchanged. Fix snapshot/time ordering,
add deterministic interleaving regression and rerun before flashing acceptance.
[H9 exact evidence](MANUAL_BENCH_CONTROL_H9_TESTS.md) records all failures and
scope: 2,413 portable checks per strict Debug/Release/ASan-UBSan, 19 checker tests,
fresh linked SITL/Pixhawk 6X compilation PASS; these do not override runtime FAIL.
Manifest `58d787ec77bd36c775ac5452caa37a4624345776727a26907fe525e2e1e8511f`.
No hardware, output drivers, startup/default changes, EDF, flashing or staging.
Physical acceptance, C1E-N1 negative-only and positive/flight gates are unchanged.

2026-10-01 H8 software integration: explicit user approval permits a default-off
registered bench module and dedicated MAIN1-4 servo publisher/consumer path.
Gate advanced: **H8 explicit bench runtime/output software integration**, not
physical acceptance. Strict host suites pass 2,413 checks per configuration.
[H8 verification](MANUAL_BENCH_CONTROL_H8.md) records actual integrated ELF
evidence, distinct from H7's excluded objects. No default profile, automatic
startup, EDF command, arming change, flash or actuator operation occurred.
C1E-N1 remains negative-only and flight/positive-admission gates remain intact.
H8 integrated ARM flash is 1,956,724 bytes with required module symbols present;
the private disabled-only SITL lifecycle smoke passes. Manifest SHA-256
`0b934dde9c339d08e5fc295ec4087dd41fcf99b80224db785a8fac6695f6ac0f`.
No active manual-to-driver runtime, installed neutral, IO-loss or physical stop
acceptance is asserted. See H8 for failed-attempt diagnostics and exact hashes.

2026-10-01 acceptance-preflight tooling: twelve required evidence categories,
artifact integrity and explicit non-authorization are checked by a read-only host
tool; 19 synthetic Python tests pass. See [acceptance sequence](MANUAL_BENCH_PHYSICAL_ACCEPTANCE.md).
The physical acceptance gate remains OPEN, as do integrated output firmware,
installed measurements and independent stop qualification. No new firmware
compilation, linked-resource, actuator or flight evidence is claimed.

2026-10-01 H7 documentation follow-up: user bench inputs and researched BEC/
servo specifications are recorded in [the H7 contract](MANUAL_BENCH_CONTROL_H7.md).
An independent hardwired stop arrangement is specified, not implemented or
verified. Assumed IDs, ideal angles/slew and 5% deadzone are not deployed defaults.
H7 remains output-disabled; C1E-N1 and all positive/physical gates are unchanged.
No new test/build or linked firmware resource evidence is claimed.

Initial review date: 2026-09-20. Latest increment: 2026-10-01 by Codex.
The evidence-and-preservation context below records the original Wave 2
software-groundwork scope; the latest dated increment supersedes its historical
release boundary. Historical Wave 1 scope: prompts A/B/C followed by master review D from
`CODEX_AGENT_PROMPTS.txt`. These prompt letters differ from the older assignment
letters in the roadmap. The roadmap remains the source of phase exit criteria.

## Evidence and preservation

Current authorization (2026-10-01): user accepted the proposed servo-only
wind-tunnel bench scheme using QGC gamepad/SiK/PX4, MAIN1-4, and confirmed
NACA 0012. The accepted provisional policy and remaining measured inputs are
in [H7](MANUAL_BENCH_CONTROL_H7.md). H7 adds an output-disabled bench candidate
owner and selected-manual scheduling seam; EDF remains inhibited. User accepted
20 ms scheduling, 500 ms input age and +/-13 degree candidate surface envelope,
not their physical or link qualification. H6 remains the input foundation.
P2J separately observes an actual stock two-EKF
selector timeout handoff using duplicated recorded sensor payloads, with every
authority and qualification field false. Neither
establishes plant feasibility nor permits guessed effectiveness, gains, timing,
source priority, positive acceleration admission, active actuator publication,
flashing or physical operation.

## Current control increment: H7 manual servo bench candidate (2026-10-01)

H7 advances the fixed-capacity command and lifecycle boundary for the accepted
manual bench scope. One noncopyable owner accepts only the configured selected
MAVLink identity, requires centered explicit enable, uniformly scales coupled
roll/pitch/yaw, applies caller-measured slew and pulse calibration, and latches
input/timing faults with separate neutral intent. Four-axis H6 defaults remain;
the explicit H7 mask ignores throttle, including NaN, and prohibits a nonzero
disabled mapping. No H2/H3 aerodynamic coefficients are invented for bench use.

The PX4 adapter remains an inert library type with no module/startup, parameter,
airframe, arming or actuator publication. Installed linkage/signs, source
identity, approved slew/center/scheduling data and physical neutral behavior
block the enabled bench-output gate. C1E-N1 and acceleration admission are
unchanged. See the H7 record and foundation for final evidence and resource scope.

H7 verification: scoped AStyle, strict Debug/Release and ASan/UBSan each pass
2,180 checks (649 H7 + unchanged 1,531 H1-H6). Fresh full SITL/Pixhawk 6X and
separate POSIX/ARM GCC 13.2.1 research-library compilation pass. Ten unlinked
ARM objects total 7,501 text/0 data/0 BSS; linked default flash 1,947,412 bytes
excludes H1-H7, confirmed by absent symbols. Evidence
`/var/tmp/research-bench-h7/manifest.json` SHA-256
`ec260f2d798ef98eccbd9bb3141f1993ac315f8f4a5ca24c667a96ed0a84bc03`.
This advances only the **H7 output-disabled servo-bench candidate/lifecycle
foundation**; no physical operation or enabled output is released.

Current confirmed decisions are in the updated requirements/interface: inertial FRD acceleration only in research Offboard; MAVLink host script for SITL and Pi translator/TELEM serial for hardware; DroneCAN pitot/GPS/Pixhawk 6X sensors; vertical +X-up release-from-rest intent and headfirst net recovery. Mass near 2.5 kg and thrust near 5.5 kgf are user expectations without measurement uncertainty. The user explicitly confirmed CG reference and rotating-body FRD hold semantics during C0b.2 review (2026-09-17); these are no longer provisional conventions.

Model ownership decision for development: future custom resources will be versioned in this parent project under `models/research_vehicle/` and supplied by an explicit Gazebo resource path. This is outside the separate `Tools/simulation/gz` repository; no model directory/asset is created now. A separate research model name, future resource-path integration and hashes will preserve the stock Cessna. Choosing ownership does not approve physics.

Completed disjoint planning assignments: Prompt E (`gpt-5.6-terra`, high) delivered `docs/SITL_TEST_SPECIFICATION.md`; preliminary H (`gpt-5.6-sol`, high) delivered `docs/CONTROL_ARCHITECTURE.md`. Astra owns their post-handoff corrections, requirements, interface, this gate and synthesis. H is limited to software boundaries/reuse analysis because physical data is unavailable. Portable code ownership is authorized below.

All earlier project files were hashed at entry to this increment. The requirements/interface/gate are intentionally updated; original setup/architecture/workflow documents, launcher scripts, roadmap, prompt file and baseline report remain outside the edit scope.

- Observed PX4 HEAD: `e880f1cd79e2453ef3dfaf68ee1c067b98ea9a53`.
- Observed separate `Tools/simulation/gz` HEAD and parent gitlink:
  `bb0b9cf974acf4f1bcb5f5fcf80b88841562dea9`; initially clean.
- Pre-existing work: intent-to-add `docs/PX4_FORK_WORKFLOW.md`,
  `docs/SITL_ARCHITECTURE.md`, `docs/SITL_WSL_SETUP.md`, and three
  `scripts/sitl/*.sh` files; untracked roadmap and execution-prompts files.
  Content hashes were captured before worker edits. All belong to the user.
- The three Wave 1 output files and this gate file were absent at entry to that historical wave; they existed at entry to the current increment.
- Historical Wave 1 permitted only documentation/read-only inspection. Current S1 additionally permits the isolated library, scoped formatting and host tests below, not a firmware/simulator build or model operation.
- Repository root guidance was read. No nested guidance applies to these
  top-level `docs/*.md` outputs; `docs/en/**` guidance does not match them.

Use **fact** for a user-confirmed constraint or pinned source observation,
**measurement** for a recorded experiment with provenance, **assumption** for
an explicitly provisional input, and **inference** for a conclusion drawn from
named evidence. An observed installed version is not a runtime test result.
No physical feasibility or acceleration-tracking claim follows from a document,
a received message, a telemetry display, or a successful compilation alone.

## Current control increment: H6 default-off PX4/GCS manual boundary (2026-09-30)

H6 adds one optional input implementation for the H5 runtime without enabling
the runtime. Stock PX4 remains the manual-source owner: a GCS joystick sends
`MAVLINK_MSG_ID_MANUAL_CONTROL`; `MavlinkReceiver` normalizes and publishes a
multi-instance `manual_control_input`; the stock `manual_control` module applies
its existing selection policy and publishes one `manual_control_setpoint`; only
that selected topic is consumed by `Px4ManualControlInputSource`. RC follows the
same stock selector. No new GCS protocol, source priority, parameter or fallback
was introduced.

The portable `ManualControlInput` is fixed-capacity, noncopyable/nonmovable and
failure-preserving. The caller must provide the admitted source policy, maximum
sample age, exact MAVLink instance/system/component identity when applicable,
and complete affine maps from roll/pitch/yaw/throttle to both six normalized
control axes and the EDF-plus-four-canard desired command. It validates PX4's
normalized axis domain, finite arithmetic, source identity, freshness,
chronology, held samples, regression and same-timestamp payload conflict. No
coefficient, gain, limit, trim, priority, age, identity or fallback is supplied
as an operational default.

The PX4 adapter is only an injected H5 `RuntimeInputSource`; its read method now
receives the exact H5 schedule timestamp. It publishes nothing and has no module
entry, Kconfig/board selection, airframe, parameter, startup, arming, allocator,
actuator function or output driver. The `EXCLUDE_FROM_ALL` library is not
instantiated or linked by either default build. C1E-N1 is unchanged and remains
negative-only; manual input cannot make an acceleration request eligible.

Scoped PX4 AStyle reports all seven changed C/C++ files unchanged. Strict
Debug, Release and ASan/UBSan configurations each pass 843 H1, 182 H2, 233 H3,
68 H4, 73 H5 and 132 H6 checks: 1,531 per configuration. Release passes three
consecutive repetitions and the sanitizer log has no diagnostic. Evidence is
retained under `/var/tmp/research-manual-h6`.

The fresh WSL-native SITL and Pixhawk 6X build views compile all eight research
objects. ARM GCC 13.2.1 reports 5,197 bytes text and zero data/BSS across those
objects. This is **object compilation evidence only**. Complete default SITL and
Pixhawk firmware also build; both ELFs contain zero
`research_actuator_command::` symbols. The Pixhawk link reports 1,947,428 bytes
of flash (99.05% of 1,920 KiB), which is **linked baseline firmware resource
evidence excluding H1-H6**, not an enabled H6 cost. The initial parallel board
pass hit stock MAVLink generated-header ordering in UAVCAN; explicitly running
the existing `mavlink_c_generate` target and resuming without source changes
completed ELF, BIN and PX4 artifacts. Exact source/artifact hashes and object
sizes are recorded in `EDF_CANARD_CONTROL_FOUNDATION.md` and
`RESEARCH_CODE_GRAPH.json`.

This advances exactly **H6 default-off PX4-selected manual ingress and mapping
boundary**. It is compile-backed manual/GCS option plumbing, not live GCS
operation, an acceleration controller, an allocator or flight readiness. The
next control gate is a reviewed output-disabled validated-input/status
integration boundary after positive-admission clock, estimator,
operator/transport and protocol authority. Physical stabilization/allocation,
operational maps and timing, startup, arming and output publication remain
separate closed gates.

## Current acceleration increment: P2J actual two-EKF timeout handoff (2026-10-01)

P2J advances the actual multi-instance selector path without changing a stock
estimator, selector gate or admission decision. A private non-lockstep POSIX
profile starts two actual EKF2 instances and one actual `EKF2Selector`. Two
`vehicle_imu` instances receive duplicated copies of all 1,200 hash-pinned P2I
IMU payloads while the complete source-derived system-state fixture is retained.
The actual EKF/multi-EKF/publisher/subscription bindings are
`[0,0,0,0,0]` and `[1,1,1,1,1]`. This is deliberate closed evidence with one
recorded sensor identity, not deployment-sensor diversity.

After the unchanged selector reports both instances available, primary zero and
both raw `healthy[]` bits true, the feeder invokes stock `ekf2 stop 0` and
continues the finite fixture. Each normal and restart process observes exactly
one unrequested stock timeout selection from instance zero to instance one,
selected attitude and local position on both sides, one attitude reset barrier
and all six local-position reset-counter barriers. Source and selected
quaternion, reset and acceleration payloads remain bit-identical. The observed
timeout is not adopted as an operational timing budget or fallback.

Final independently verified Debug, Release and ASan/UBSan packages are
`/var/tmp/p2j-handoff-u4up5z6_`, `/var/tmp/p2j-handoff-t7z5qo9k` and
`/var/tmp/p2j-handoff-6ziwnuyt`. Their manifest hashes are
`e445b041336a1b703af9559d0ada68ca2397f89e8f93653689a1ec7d7370559c`,
`8a352498e57c2549ba78d4b6958190120b96da6a13219bebd4e5114367aedaa2`
and `6502503cfc4ce40b6a3eccc877e9ee7bee4b9a4fd720be38ab49b7b316ec3523`.
All six runtime cases publish 2,400 messages from 1,200 duplicated inputs and
retain both sides of the handoff. Eleven corruption tests and scoped PX4 AStyle
pass. Unchanged H1-H6 regressions pass 1,531 checks per Debug, each of three
explicit Release passes and ASan/UBSan; no sanitizer diagnostic is present.

Fresh default SITL and Pixhawk 6X builds pass from the clean WSL-native source
view under `/var/tmp/research-selector-handoff-p2j`. ARM GCC 13.2.1 produces a
1,947,428-byte board BIN and the link reports 1,947,428 bytes of flash (99.05%).
Default module lists, build logs and ELFs contain no P2J module, compile line or
`research_handoff` symbol. That is linked baseline-firmware resource evidence
which excludes P2J. The separate POSIX P2J observer/feeder/recorded-state
objects total 37,473 text, 48 data and 490,272 BSS bytes and are compilation
diagnostics only, not NuttX or linked flight-firmware cost.

P2J closes only actual two-EKF lifecycle, binding and timeout-handoff
observation. Duplicated data from one recorded sensor cannot qualify sensor
diversity, deployment sensors or failover. Complete mutation coverage, an
admissible health predicate, immutable operational budgets, provider/sample-
clock continuity and operator/transport/distinct-positive-protocol authority
remain open. All health, failover, mutation, sensor, admission, eligibility and
actuator-publication fields stay false. C1E-N1 remains negative-only; no startup,
airframe, arming, controller, allocator, output, fallback, operational default
or flight-readiness claim was added. Exact source, evidence, object, regression
and firmware hashes are in `EDF_CANARD_CONTROL_FOUNDATION.md` and
`RESEARCH_CODE_GRAPH.json`.

## Prior acceleration increment: P2I valid selected-attitude provenance (2026-09-30)

P2I advances exactly the source-derived valid selected-attitude path left open
by P2H. A private non-lockstep POSIX view replays one actual EKF2 and one actual
`EKF2Selector` with the existing 1,200 recorded IMU samples plus the complete
overlapping system-state payloads from the same hash-pinned stock Cessna ULog:
14 `vehicle_status` and six `vehicle_land_detected` records. The independent
checker reconstructs all 20 records from ULog SHA-256
`62665d68f92ceb47068a5b5f16a1407998e0f2cf08049e98fc55cf61e90c8143`;
the derived fixture hash is
`130175ecef2110ceb184db35cd47942b654476c70747dff349e1e5b29604ac68`.
Only publication timestamps are transformed to fresh HRT. No state, physical
coefficient, gain, limit, priority, timing budget or fallback is invented.

The unchanged EKF attitude-valid branch now transitions during the fixture.
Debug normal/restart publish 1,111/1,109 actual selected attitudes, Release
1,111/1,111 and ASan/UBSan 1,111/1,111. All 6,664 selected quaternion/reset
payloads are bit-identical to their actual estimator sources, and every
candidate is monotonic and fresh under the unchanged stock 10-ms selector
branch. Selected acceleration remains 589 publications per Debug/Release case
and 590 per sanitizer case with exact bits. Restart rejects a second actual
selector with sticky fault 4. Fixed 32-record and 2,048-record stores, one
administrative owner, sticky first failure, bounded finish and guaranteed input
closure preserve the evidence lifecycle.

Final independently verified packages are
`/var/tmp/p2i-attitude-vkyya591` (Debug),
`/var/tmp/p2i-attitude-2_co807l` (Release) and
`/var/tmp/p2i-attitude-zxtllgub` (ASan/UBSan). Their manifest hashes are
`7bba3b578be0d3aa730970512926fface7e191bcc20ddcd49075694a1024cc0b`,
`303b9f863b514928b1fef5e6cb8655a42d1272d4fec944f8f7ff881dbf1fcc79`
and `3a6a7fb29c6831fa3066ceb22d752b40fb8561404822423d5e6e02ac982ce0cb`.
Eleven checker corruption tests, scoped PX4 AStyle and whitespace/syntax checks
pass. Unchanged H1-H6 regressions pass 1,531 checks per Debug, three explicit
Release passes and ASan/UBSan configuration; no sanitizer diagnostic is
present.

Fresh complete default SITL and Pixhawk 6X builds pass from the WSL-native view
under `/var/tmp/research-selected-attitude-p2i`. ARM GCC 13.2.1 produces a
1,947,412-byte Pixhawk BIN and a linked flash report of 1,947,412 bytes
(99.05%). Both default ELFs contain zero P2I/P2H/P2G/H1-H6 symbols, and actual
default build logs contain no research compile line. This is linked baseline
firmware resource evidence excluding all research code. The separate 57,899-
text/48-data/933,763-BSS POSIX observer/feeder total is host compilation
evidence only, not NuttX or linked flight-firmware cost.

P2I closes only valid selected-attitude payload provenance for this recorded
single-instance fixture. Complete mutation and deployment-sensor coverage,
real multi-instance failover/handoff, an admissible health predicate, immutable
operational timing budgets, provider/sample-clock continuity and
operator/transport/distinct-positive-protocol authority remain open. All
health, failover, admission, eligibility and actuator-publication fields stay
false. C1E-N1 remains negative-only; no startup, airframe, arming, controller,
allocator, output, fallback, operational default or flight-readiness claim was
added. Exact source, evidence, object, regression and firmware hashes are in
`EDF_CANARD_CONTROL_FOUNDATION.md` and `RESEARCH_CODE_GRAPH.json`.

## Prior acceleration increment: P2H selected-output disposition provenance (2026-09-29)

P2H closes the upstream-attitude-validity and selected-output disposition gap
for the frozen single-instance selector fixture without changing a PX4 gate or
creating authority. A private source view observes each actual EKF
`PublishAttitude()` validity/publication branch and each actual selector
attitude/local-position candidate decision. It records the unchanged stock
monotonic and 10-ms/20-ms freshness decisions as source facts, not operational
budgets. Workspace EKF2 source and every default PX4 profile remain unchanged.

All six Debug, Release and ASan/UBSan normal/restart cases record 1,200 EKF
attitude entries. Every entry is attitude-invalid, so no attitude publication is
attempted, the selector sees no attitude candidate and its 10-ms gate is never
reached. This corrects P2G's earlier attribution of the zero selected-attitude
count to freshness; it does not qualify a valid selected-attitude payload.
Debug records 589 local-position checks/candidates/publications per case and
Release 588. ASan/UBSan records 594/593 checks and 590 candidates/publications
for normal/restart, with explicit candidate-absent rows for the difference.
Every present candidate is monotonic and fresh under the stock branch, and every
requested publication preserves the inherited P2G acceleration bits exactly.

Normal stops and exports the first real selector/EKF lifecycle. Restart rejects
the second real selector constructor with sticky fault 4 while preserving the
first lifecycle. Three fixed 2,048-record arrays, lock-free reservation, one
administrative owner, sticky first failure and bounded finish keep the observer
deterministic. Nine checker tests and all three independent evidence checks pass;
ASan/UBSan reports no diagnostic. All health, failover, admission and control-
eligibility fields remain false.

Final packages are `/var/tmp/p2h-disposition-bv29hkud`,
`/var/tmp/p2h-disposition-gnzmttte` and
`/var/tmp/p2h-disposition-ps2oxj5a`. Their manifest SHA-256 values are
`e015e46c659be1ce9fc4aef7a0b5ce2845c0ea44738acc742894acdb06928b06`,
`f495ad5f68e794126cad16015cf6b59269d2d8a645b82e40caaf4d0ecb1d9f6f`
and `4112944c84edaf34bbd78a15dfa50db2d967dd8128859fc1a1f39ff91d8e1adb`.
The observer's 15,915-text/8-data/360,564-BSS object and 448-byte largest
bounded frame are host compilation diagnostics only, not linked firmware
resource evidence.

Scoped PX4 AStyle passes. Unchanged H1-H5 strict Debug, Release and ASan/UBSan
configurations each pass 1,399 checks. A fresh clean WSL-native source under
`/var/tmp/research-acceleration-p2h` builds complete default SITL and Pixhawk 6X
firmware with ARM GCC 13.2.1. The default ELFs contain zero P2H/P2G/H1-H5
symbols and no corresponding compile line; the 1,947,428-byte Pixhawk BIN is
linked default-firmware resource evidence that excludes the research code, not
P2H/H5 linked-cost evidence. Exact source, evidence, object, test, build and
package hashes are in `EDF_CANARD_CONTROL_FOUNDATION.md` and
`RESEARCH_CODE_GRAPH.json`.

P2H closes only disposition provenance for the current invalid-attitude,
single-instance fixture. Full P2 remains open for a valid selected-attitude
payload, complete mutation/deployment-sensor coverage, real multi-instance
failover/handoff, an approved health predicate and immutable timing budgets.
P3 clock/map and P4 operator/transport/distinct-positive-protocol gates remain
open. C1E-N1 is unchanged and negative-only; no startup, airframe, arming,
controller, allocator, actuator publication, fallback or operational default
was added.

## Prior increment: P2G selector-enabled acceleration provenance (2026-09-29)

P2G advances the acceleration interface's closed estimator-provenance boundary
without creating authority. A private non-lockstep POSIX overlay runs the actual
`EKF2Selector` with one actual EKF, observes the selected-instance transition
255 -> 0 and binds selected attitude, local-position, selector-status and source
`vehicle_imu` instances as `[0,0,0,0,0]`. It observes actual sensor-selection,
selector-status and selected-output publication results and preserves selected
local-position acceleration as exact float bit patterns. Workspace EKF2 source
is unchanged; the evidence-only profile has no startup/default registration.

The feeder independently verifies final P2F evidence before converting its
1,200 recorded 4,000-us rate/acceleration samples into `vehicle_imu` deltas. All
six Debug, Release and ASan/UBSan normal/restart cases publish all 1,200 inputs
on instance 0, publish no actuator topic and transition only to selector instance
0. Debug retains 589 selected local-position acceleration payloads per case,
Release 588 and ASan/UBSan 590. Status counts are 5/6 in Debug and 5/5 in the
other configurations. Restart rejects the real second selector constructor
while preserving the first lifecycle. Eight focused checker tests pass; no
sanitizer diagnostic is present.

Final packages are `/var/tmp/p2g-selector-4ek3kb0n`,
`/var/tmp/p2g-selector-5n54u5ds` and `/var/tmp/p2g-selector-9b5xbf6q`.
Their manifest SHA-256 values are
`46ddb231f4613014c3e7ce50693a0dbe2473c527023afce6e4d84338d5c040eb`,
`275ddf7b43e085ce22973b0ce0781ce9c521907cbe228704394a395c58eb19d5`
and `18e4dabcc063e3a6a72a9d2af772924ef1655066ce45b5beecb0ecfee3bfd4cb`.
The bridge has fixed 2,048-output/64-status capacity, one owner and sticky first
failure. The 17,521-text/8-data/265,386-BSS observer and
5,788-text/8-data/67,258-BSS feeder objects are closed host compilation
diagnostics, not linked firmware resource evidence.

All five exported authority fields remain false. Raw selector `healthy[]` is
observed but not interpreted as admission. P2H later established that every EKF
attitude source entry was invalid and no attitude candidate reached the selector,
so the stock 10-ms gate was not exercised. P2G does not qualify attitude or
health. One instance cannot qualify failover. The
observed acceleration remains the PX4 NED body-origin publication; it is not the
commanded inertial-at-CG rotating-body-FRD vector and no frame conversion is
introduced.

Unchanged H1-H5 strict Debug, Release and ASan/UBSan each pass 1,399 checks.
Fresh clean WSL-native default SITL and Pixhawk 6X builds pass under
`/var/tmp/research-acceleration-p2g`; ARM GCC 13.2.1 links and packages the
board. Both default ELFs contain zero P2G/H1-H5 symbols and no corresponding
compile line. The board BIN is 1,947,412 bytes. This is linked default-firmware
resource evidence that excludes the research code, not P2G/H5 linked-cost
evidence. Exact source, object, test, build and package hashes are recorded in
`EDF_CANARD_CONTROL_FOUNDATION.md` and `RESEARCH_CODE_GRAPH.json`.

This closes only selector-enabled single-instance lifecycle and selected
local-position acceleration provenance. Full P2 remains open for admissible
health, complete mutation and selected-attitude coverage, real multi-instance
failover/handoff, deployment sensors and immutable timing budgets. P3 clock/map
and P4 operator/transport/distinct-positive-protocol gates remain open. C1E-N1
is unchanged and negative-only; no startup, airframe, arming, controller,
allocator, actuator publication, fallback or operational default was added.

## Prior increment: P2F direct selected-state provenance (2026-09-29)

P2F advances the acceleration interface's closed estimator-provenance boundary
without creating authority. A private non-lockstep POSIX overlay binds the
direct EKF instance and the actual attitude, local-position and status publisher
instances after topic advertisement, observes the real status publication
result, and relates control/fault change counters and the publisher cache to the
same completed EKF iteration. Only the non-replay, non-multi instance-zero path
is accepted. This is direct publisher provenance, not proof that the EKF2
selector selected that instance or that selector failover is safe.

Final evidence `/var/tmp/p2f-selected-vl0q1aop` passes the independent P2D/P2E/
P2F checker and eight focused corruption tests. Debug normal/restart retain
1,199/1,197 rows; exact closed-profile Release retains 1,195/1,197; ASan/UBSan
retains 1,197/1,195. Every case binds `[0,0,0,0]`, observes six successful status
publications, one control transition, zero fault transitions and one initial
pre-publication stale-cache row. The native row-count variation is input
coalescence; no unobserved row is synthesized or selected away. All authority
fields remain false. Restart rejects the actual second EKF constructor.

The bridge has fixed 2,048-record storage, a single explicit owner, sticky first
failure and bounded finish. Its Debug object is 10,357 text, 8 data and 245,903
BSS bytes; its largest reported self-frame is the 320-byte dynamic/bounded
export frame. These are closed POSIX diagnostic compilation measurements, not
NuttX acceptance or linked firmware cost. Exact source, manifest, checker,
variant and artifact hashes are in `C1E_P2F_SELECTED_STATE_PROVENANCE.md`.

Unchanged H1-H5 strict Debug, Release and ASan/UBSan configurations each pass
1,399 checks. Fresh complete default SITL and Pixhawk 6X builds pass from the
WSL-native `/var/tmp/research-acceleration-p2f/px4-source`; ARM GCC 13.2.1 links
and packages the board. Both default ELFs contain zero P2F/H1-H5 symbols. The
1,947,412-byte flash report is linked default-firmware resource evidence that
excludes the research code, not P2F or H5 linked-cost evidence.

This advances only direct instance-zero publisher/status-change observation.
Full P2 remains open for selector-enabled selected-instance lifetime, an
explicit admissible health predicate, complete mutation coverage, deployment
sensor qualification and immutable freshness/skew/latency budgets. P3 clock/map
continuity and P4 operator/transport/protocol authority remain open. C1E-N1 is
unchanged and negative-only; no startup, registration, arming, controller,
allocator, actuator publication or physical/operational default was added.

## Prior acceleration increment: P2E committed estimator provenance (2026-09-28)

P2E advances one closed acceleration-interface prerequisite: the native
observer now binds the actual `Ekf::update()` commit result to the corresponding
local-position publication, optional attitude publication, five reset counters,
control/fault flags and recorded accel/gyro device/calibration identities. It
extends the P2D private single-instance, non-lockstep POSIX view; no workspace
PX4 source is patched. Its NED body-origin acceleration observation is evidence
only and is not substituted for the commanded inertial-at-CG body-FRD vector.

Final evidence `/var/tmp/p2e-committed-zs4snisq` passes the independent checker
and seven focused corruption tests. Debug normal/restart cases each retain 1,200
rows, 600 actual successful commits and one reset-counter transition; restart
rejects the second EKF constructor. The hash-pinned baseline ULog supplies device
ID `1310988` for both sensors and calibration counters 0/1. All attitude
predicates remain false, so no attitude payload and no health-qualified bundle
exists. Release and ASan/UBSan normal/restart runs also pass; all four retain
1,200 observations/600 commits. Final review added a nonblocking destructor
writer guard and regenerated every P2E artifact. No record is synthesized for
an unobserved input.

The bridge uses a fixed 2,048-record archive. Its Debug object is 11,538 text,
8 data and 327,909 BSS bytes; compiler self-frames are 64 bytes for output and
iteration hooks, 128/160 bytes for source start/end, and 416 dynamic/bounded
bytes for export. These are closed POSIX diagnostic measurements, not embedded
resource acceptance. `health_qualified`, `positive_admission` and
`eligible_for_control` remain false in every artifact. The complete contract
and hashes are in `C1E_P2E_COMMITTED_STATE_PROVENANCE.md`.

Unchanged H1-H5 strict Debug, Release and ASan/UBSan configurations each pass
1,399 checks. Fresh complete default SITL and Pixhawk 6X builds pass from the
WSL-native `/var/tmp/research-acceleration-p2e/px4-source`; ARM GCC 13.2.1 links
the board. Both default ELFs contain zero P2E/H1-H5 symbols. The 1,947,428-byte
flash report is linked default-firmware resource evidence that excludes the
research code, not P2E or H5 linked-cost evidence.

This advances only committed-output/reset/recorded-sensor provenance
observation. Full P2 remains open for an explicit admissible health predicate,
selected-instance/selector lifetime, complete mutation coverage, real deployment
sensor qualification and immutable freshness/skew/latency budgets. P3 clock/map
continuity and P4 operator/transport/protocol authority remain open. C1E-N1 is
unchanged and negative-only; no startup, arming, controller, allocator,
actuator publication or physical/operational default was added.

## Prior control increment: H5 default-off PX4 runtime owner (2026-09-28)

H5 adds a single allocation-free `ControlRuntimeOwner` for the H4 pipeline and
an inert `Px4ControlRuntime` `ScheduledWorkItem` adapter. Construction and
configuration do not schedule work. The caller must supply every H4 value and a
nonzero interval no greater than its supplied maximum timestep, then explicitly
start and prime the owner. Successful cycles alone advance the owned timestamp
and H4 state. Configuration, time, input or pipeline failure preserves state and
the caller report; stop revokes configuration and requires explicit reconfigure.

The adapter is compiled only inside the excluded research library. It has an
injected input interface, caller-selected work queue and interval, and a local
discarded report. It has no module entry, Kconfig/board selection, startup,
parameter, uORB, airframe, arming, allocator or actuator/output path. No H5
object is instantiated by default. C1E-N1 is unchanged and remains accepted
only for bounded negative diagnostic admission. The future enclosing owner must
serialize lifecycle calls against `Run()`; actual concurrent stop coordination
is not qualified by this compile-only adapter.

Scoped PX4 AStyle passes. Strict Debug, Release and ASan/UBSan runs each pass
843 H1, 182 H2, 233 H3, 68 H4 and 73 H5 checks: 1,399 total with zero failures.
Fresh complete `px4_sitl_default` and `px4_fmu-v6x_default` builds pass from the
WSL-native `/var/tmp/research-runtime-h5/px4-source`; retain their builds at
`px4-sitl-build` and `px4-v6x-build` under that root. ARM GCC 13.2.1 compiles
all six research objects under PX4 warnings. Exact source and artifact hashes,
including the recovered stock MAVLink generation-order failure, are recorded in
`EDF_CANARD_CONTROL_FOUNDATION.md` and `RESEARCH_CODE_GRAPH.json`.

The six ARM objects contain 3,745 bytes of text and no data/BSS. This is compile
evidence only. The default SITL and Pixhawk ELFs contain zero matching research
symbols. The complete board link still reports the baseline 1,947,412 bytes of
flash (99.05% of 1,920 KiB), so it is linked firmware resource evidence that
**excludes** H1-H5, not an integrated H5 cost. The BIN SHA-256 is
`7bc4784a8cb3bdacc166c2a7ba9cf0e29c0db775c1211740bca517971b82bf08`;
the excluded ARM archive SHA-256 is
`ad8560bf64904e3750804b006ad3243a2e8cba3032b805f67bd7837289413a88`.

This advances the exact default-off runtime ownership, lifecycle and PX4
scheduling boundary. Phase 4 remains open. No scheduler/clock provider,
operational timing, positive input, matrix/policy, controller, allocator,
fallback or output is qualified. The next control gate is an output-disabled
validated-input adapter/status boundary after the positive-admission provenance,
clock, operator and transport gates; physical control remains separately gated.

### Current roadmap status through all gates

| Phase | Current result | Gate status |
| --- | --- | --- |
| 0: requirements, data and feasibility | Vehicle intent and command semantics are recorded. Original CAD component mass/CG/inertia, local-flow/effectiveness, installed actuator response, feasible envelope and numerical requirements remain unverified. | Incomplete |
| 1: reproducible stock baseline and logging | The first operator's GUI and headless revision-2 baseline passes with frozen evidence. The user deferred the required second-operator reproduction. | Incomplete |
| 2: acceleration interface contract and source trace | FRD inertial-at-CG semantics, transport choice, portable guard/frame math, wire conformance and bounded negative native ingress/receipt evidence exist. P2J starts two actual EKF2 instances and the actual selector, binds their two IMU/publisher paths and observes the stock unrequested timeout handoff with selected payload and reset barriers preserved. Both instances consume duplicated data from one recorded sensor, so sensor diversity, deployment qualification, failover qualification, admissible health, positive admission, qualified clocks/provider, complete mutation coverage, operational budgets/loss policy and the complete request-to-control path remain open. | Incomplete; P2J actual two-EKF timeout-handoff observation advanced |
| 3: custom Gazebo plant and sensors | A derivative package resolves six mesh URIs. Supplied inertia is invalid, mass scope disagrees with the provisional all-up mass, and physics/forces/sensors are not validated. | Incomplete |
| 4: PX4 research control and allocation | H1-H6 provide tested limiting, normalized effectiveness diagnostics, prioritized structural authority, one default-off runtime owner, an inert PX4 scheduling boundary and an optional PX4-selected RC/GCS manual-to-runtime mapping boundary. No validated positive acceleration input, active controller, allocator, operational map/value or tracking evidence exists. | In progress; H6 manual boundary advanced, gate open |
| 5: companion and Pi-in-the-loop | Pi/TELEM is the intended hardware path; no approved physical Pi closed-loop timing, reconnect or fallback evidence exists. | Not passed |
| 6: quantitative robustness and regression | No integrated plant/controller exists and no evidence-backed performance thresholds can be exercised. | Not released |
| 7: Pixhawk build and hardware-simulation assessment | ARM GCC 13.2.1 and Pixhawk 6X compilation pass, including a fresh complete default firmware package. P2J and H1-H6 remain unlinked by default; the current 1,947,428-byte flash report excludes them, and integrated runtime/timing/hardware-interface evidence is absent. | Early compile milestone only; gate open |
| 8: bench and research flight validation | No bench, flash, powered actuator or flight test is authorized or claimed. | Not released |

## Prior checkpoint: P3C native checked receipt clock (2026-09-27)

[P3C verification](C1E_P3C_NATIVE_CHECKED_RECEIPT_CLOCK.md) records the separate
closed native receipt-clock integration. The 440-step build and both processes
complete normally. All 1200 normal raw/merged observations and seven clock
purposes match; injected source read 26 retains the expected 25-observation
prefix, sticky failure and real destructor evidence while refusing raw success.
Native evidence `/var/tmp/p3c-native-8oh4gg31` is frozen. A terminal-field checker
correction is separately retained at `/var/tmp/p3c-verifier-lbz2xeg4`; final
independent acceptance `/var/tmp/p3c-final-review-an2v_qgt` binds both. Ten initial
corruptions and two corrected terminal-type probes are rejected.

This closes checked receipt-clock error propagation, not the legacy scheduler
or native sample clock, provider/namespace/pause continuity or qualified maps.
Estimator-health and operator/transport authority remain open. N1 stays
negative-only; no baseline repetition, commit, publishing or physical operation.

## Prior checkpoint: P3B checked host clock lifecycle (2026-09-27)

[P3B verification](C1E_P3B_CHECKED_CLOCK_LIFECYCLE.md) records a separate
Linux host-only checked reader and finite candidate lifecycle. Debug and
ASan/UBSan contracts, default-off/profile probes, and a real 12-event trace
across expiry, exact predecessor replacement and retirement pass. Final
independent review accepts the bounded scope and rejects five evidence
corruptions. Final package `/var/tmp/p3b-provider-5vpaar72`; review
`/var/tmp/p3b-final-review-uv_rdov5`. Preliminary package `-_u4r36lf` and
`/var/tmp/p3b-independent-review-cf87d_bp` remain preserved.

The core preserves raw nanosecond chronology, explicit read failures and sticky
loss; it cannot be copied/reset or mint a map. The 40-ms experiment intervals
are not approved timing budgets. Native PX4 error propagation, actual provider
semantics, whole-interval/namespace/pause continuity and owner integration
remain open. No map or positive authority is qualified; N1 is unchanged.

## Prior checkpoint: P2D typed native observation merge (2026-09-27)

[P2D verification](C1E_P2D_NATIVE_TYPED_MERGE.md) records the closed native
integration of typed EKF observations, actual constructor/destructor-entry
facts and the P2C bounded merge. Both native processes close normally: all
1200 normal rows match the parallel raw recorder, and an actual second EKF
constructor is rejected in the restart process. No source input changed during
build/run. Final package `/var/tmp/p2d-native-od002jmn`; independent review
`/var/tmp/p2d-independent-review-hp9h4jgq` rejects seven corruptions and accepts
this bounded scope. Failed configure-only attempt `-7io33289` is preserved.

This closes typed native observation transfer and one-shot EKF lifetime
observation, not health qualification, complete mutation coverage, sensor
provenance, clock mapping, operator/transport authority or positive admission.
N1 stays negative-only; baseline, frozen policy and deferred second operator
are unchanged. Full P2/P3/P4/P5 and physical gates remain open.

## Prior checkpoint: P2C closed host receipt merge (2026-09-27)

[P2C contract/verification](C1E_P2C_CLOSED_RECEIPT_MERGE.md) records the new
standalone bounded per-source queues, immutable identity/sequence checks,
recorded cyclic owner order and reserved terminal evidence. Debug and
ASan/UBSan finite cases pass, including 1000 concurrent payload handoffs;
default-off and forbidden-profile probes pass. Independent reconstruction
preserves all 1200 existing P2B observations in an explicitly offline replay.

Independent review accepts this closed scope after terminal-lane attribution
and exact build/configuration evidence fixes. Final package:
`/var/tmp/p2c-merge-vvtmfdr0`; final corruption review:
`/var/tmp/p2c-final-review-6rx0877y`. Prior package/review remain preserved.
No new native/baseline run occurred. This closes the standalone merge mechanics
slice, not typed live source integration, health, clock or positive authority.
N1 remains negative-only. Full P2/P3/P4/P5 and physical gates remain open.

## Prior checkpoint: P1 review, P2B native observation and P3A deployment evidence (2026-09-27)

P1 independent requirements review passed. The separate output-disabled P2B
profile built and ran actual EKF2, retaining 1200/1200 input-bound observations,
all attitude-invalid, with no collector fault or runtime error. Independent
source and artifact review accepted that bounded observation scope. See
[P2B verification](C1E_P2B_NATIVE_VERIFICATION.md), evidence
`/var/tmp/p2b-native-30xfeeqc`; failed/exploratory attempts are preserved.

[P3A](C1E_P3A_CLOCK_DEPLOYMENT.md) then reused the verified executable in a new
closed process and retained before/after clock deployment identities plus 16
executable/library objects, with independently checked chronology. Evidence:
`/var/tmp/p3a-clock-pds1ofii`. Conditional clock arithmetic/source review passed;
no map is qualified or installed. Full P2/P3/P4/P5 remain open: committed health
semantics and publisher lifetimes, owner merge, actual provider semantics/read
failure handling, operator/transport lifecycle and a distinct positive protocol.

C1E-N1 remains the accepted admission runtime, negative-only. Baseline revision
2, first-operator acceptance and deferred second operator are unchanged. User
confirmed CAD is still in process and authorized continuing independent software.

## Prior checkpoint: P2A closed archive collector (2026-09-27)

[P2A audit/verification](C1E_P2A_PROVENANCE_COLLECTOR.md) records implemented
offline extraction and independent reconstruction of 60,397 actual estimator
observations across both existing baseline ULogs. Six finite integrity tests
pass. Source audit is bound to archived source identity; no baseline was rerun.

This completes the bounded archive-collection slice, not P2 live collection,
independent P1 review or positive-runtime acceptance. Ordinary logs omit source
lifetimes/owner order and can miss invalid attitude nonpublication. The next
coherent native increment needs an exact publisher receipt API, defined health
commit boundaries and a separate output-disabled estimator profile. C1E-N1
remains accepted only at negative scope; physical and clock gates stay open.

## Prior checkpoint: positive-admission P1 design and plant packaging (2026-09-27)

[P1 design](C1E_POSITIVE_ADMISSION_DESIGN.md) records the source-grounded
estimator/transport provenance, operator lifecycle and producer-clock map
contracts and finite release gates. Design work is complete for this checkpoint;
independent review, closed collector implementation, clock qualification and
positive protocol/API remain open. This is not positive native acceptance.
C1E-N1 remains the latest accepted custom runtime, negative-only and
disarmed/output-disabled. No baseline runs were repeated.

The derivative `models/research_vehicle/` now contains model metadata and
byte-identical meshes with six corrected URIs. Its provenance manifest and
intake report record source preservation and unchanged physical values.
Physical acceptance remains blocked by the invalid base inertia, component
mass scope and missing original CAD evidence. No simulator was launched.
See [checkpoint verification](C1E_P1_DESIGN_VERIFICATION.md) for exact scope,
source/hash checks and the retained SDFormat rejection.

## Prior checkpoint: first-operator stock baseline PASS (2026-09-27)

Fresh GUI/headless runs pass the approved revision-2 diagnostic acceptance
workflow with matching source/build/parameter inputs. The user confirmed all
five QGC items and no Comms Lost while the GUI run was live. See
[first-operator closure](SITL_BASELINE_FIRST_OPERATOR_CLOSURE.md) for evidence
and hashes. Full Phase 1 remains incomplete only for the explicitly deferred
second-operator reproduction. This stock baseline does not advance the custom
runtime beyond the accepted negative-only C1E-N1 scope below.

## Latest custom native increment: C1E-N1 negative native diagnostic ingress ACCEPTED (2026-09-25)

The private output-disabled profile now links a signed diagnostic parser,
bounded UDP worker/mailbox and Commander source-thread owner. A source-stable
485/485-step build passed 29/29 profile probes and an independent 31,296-byte
stack bound under its 32,768-byte ceiling. One literal zero-datagram process,
two source-bound signed negative processes and four live hostile/replay
processes passed independent checking. Bounded source/worker phase fixtures,
N11 corruption checks and N12 sink-failure injections also passed; see
[N1 verification](C1E_N1_IMPLEMENTATION_VERIFICATION.md) for exact counts,
hashes, failed attempts and applicability decisions.

This accepts only disarmed, negative diagnostic ingress with actual
`Rejected/SelectionRequired` status and no eligible demand, authority or
control output. N06 diagnostic operator administration and native N09/N10
trusted publisher/map injection are structurally unavailable in the frozen
profile; generic bounded fixtures cover their applicable guard behavior.
Actual simultaneous scheduler races, real kernel saturation, positive
estimator/transport/map provenance, physical plant, active control and flight
remain unaccepted. Full C1 and roadmap Phase 2 remain OPEN.

## Completed increment: C1E-W2 generated diagnostic wire ACCEPTED (2026-09-24)

Added isolated pinned C/Python generation and a C++ conformance fixture.
Debug, optimized and ASan/UBSan each pass 53 roundtrips, including 38 valid-frame
semantic negatives rejected by unchanged W1 validation; four known CRC/truncation,
one unknown dispatch and eleven wrapper-policy rejections are separately counted.
W1/Flight payload suites each pass 8/8. Sol implemented, Luna independently
reviewed, and Codex reviewed/verified. See C1E_GENERATED_WIRE_CONFORMANCE.md
for hashes, retained /var/tmp reports, corrected attempts and exact limitations.

No signing/replay, native ingress/owner change, firmware/model change or output
capability was added. C1D-N2 remains the latest native acceptance. Next: exact
C1E-N1 authenticated candidate/disposition/owner transaction API, serialization,
loss/closure and status-projection review before negative-only native integration.
No physical input is needed for that software work. Full C1 and Phase 2 are OPEN.

## Completed increment: C1E-W1 diagnostic payload foundation ACCEPTED (2026-09-24)

Implemented isolated diagnostic request/status XML and strict Python payload
codec, preserving frozen Flight v1 and the C1D closed-owner experiment. Astra
reviewed clock/state mathematics and payload layout; Luna implemented; Sol
provided independent tests/source review; Codex integrated and verified.
New suite: 8/8 methods; existing Flight codec: 8/8; nine status vectors against
the retained default-off executable and XML/XSD validation pass. See
C1E_WIRE_VERIFICATION.md for source hashes, commands, corrections and limits.

This accepts payload structure only: no C1E generated-wire, authentication,
replay, owner connection or native runtime claim. C1D-N2 remains the latest
native acceptance. Next bounded code slice is C1E-W2 generated C/Python wire
conformance, then exact C1E-N1 parser/candidate/disposition/owner review before
negative-only native integration. C1E_NATIVE_INGRESS_DESIGN.md and its finite
test matrix preserve unavailable map/estimator, Disabled selection and no
eligible demand. No geometry or new blanket permission is needed for this
software progression. Full C1 and Phase 2 remain OPEN.

## Completed increment: C1D-N2 native closed-owner ACCEPTED (2026-09-21)

C1C-S1 standalone receipt checker passes master/independent review and fresh
Debug, repeated Release and ASan/UBSan tests: 2/2 targets each, 139 finite
variants/815 checks and 22 isolation probes. Default-off creates no checker
target. See C1C_STATE_RECEIPT_VERIFICATION.md. It detects malformed/lost supplied
state receipts; it does not prove actual producer-hook completeness or confer
diagnostic authority. Fresh can describe Armed evidence and is not permission.

C1C-S2 now passes master verification and independent review. A fresh isolated
PX4 build and two disarmed processes captured 193/194 receipts, each including
one actual raw-nav change. Complete traces/final tails, startup/configuration,
source/executable bindings and preservation checks pass. Host Debug, repeated
Release and ASan/UBSan each pass 3/3 (424 producer checks, 29 evidence and 15
configuration unittest methods). See C1C_PX4_CAPTURE_VERIFICATION.md for exact
measurements, hashes and retained failures. Original Commander, C1A, separate
Gazebo and stock Cessna remain unchanged. No admission, control or output ran.

The C1D-N1 shared profile promotion and private closed owner now pass master
verification and independent review. Fresh Debug, repeated Release and
ASan/UBSan each pass 37/37; default-OFF passes 17/17. New owner: 1,911 checks;
61 new isolation probes; all 201 staged source files match the checkout.
The 934,496-byte owner is heap-resident; the estimated host failure call chain
is 16,344 bytes, not a whole-task native stack qualification. N1 uses explicitly
SyntheticNativeContract execution, not actual PX4. See
C1D_NATIVE_OWNER_VERIFICATION.md for exact results and hashes.
N2 now passes master verification and independent final review: actual native
PX4 build, 27 actual-profile probes, reviewed 28,672-byte synchronous stack bound,
and two fresh disarmed processes. Each produced 186 source records/189 actual
adapter reports and one raw-nav transition, with normal closure and identical
repeat exports. All 26 actual-export corruptions were rejected; 17 synthetic
checker/wrapper methods pass. The four strict-warning portability fixes also
pass Debug37, Release37 twice, ASan/UBSan37 and default-off17 host targets.
The final build is an explicitly retained incremental correction, not a claimed
fresh clean-area build. Missing-sensor warnings preclude health-readiness claims.
See C1D_NATIVE_INTEGRATION_VERIFICATION.md for evidence, limits and failure history.
Stock Commander, separate Gazebo and Cessna remain unchanged. No ingress,
operator/session authority, estimator, controller or actuator output was enabled.
Next exact gate: technical design review of native diagnostic ingress/owner
integration, including serialized operator/loss/session ownership, authenticated
parser/status, qualified clock mapping and honest estimator validity. N2 does
not automatically release those interfaces or establish Phase 2 exit.
Native provenance cannot be relabelled as offline fixtures; no healthy estimator,
operator or guidance-clock facts may be invented. Further live ingress needs
its own reviewed inputs. Full C1 and roadmap Phase 2 remain OPEN. No additional
human direction is needed for this approved disarmed/output-disabled scope.

## Completed increment: C1B-E finite offline evidence (2026-09-20)

Master verification and independent review PASS for the finite offline C1B
G/A/O/E contract. See C1B_EVIDENCE_IMPLEMENTATION_VERIFICATION.md and the
variant-level C1B_OFFLINE_EXECUTION_LEDGER.md. Full live C1B, C1 and roadmap
Phase 2 remain OPEN. No flight-control or physical readiness is claimed.

Implemented C++ actual-owner evidence export, retained source/binary-bound
campaigns, a separate strict Python oracle, blocked/missing-sink checks and
deliberate malformed-evidence tests. Filled the pending binding, cause,
capacity-order and frozen-mapper assertions without relaxing ordinary tests.
Final Debug, two Release runs and ASan/UBSan each pass 31/31; fresh default-off
passes 17/17. Diagnostic counts are 12,163 guard, 3,533 adapter and 1,209 owner
checks, 33 build probes and 11 evidence unittest methods. These are synthetic
software checks, not flight scenarios or measurements of live timing/trust.

Next exact gate: C1C-S1 source-traced state-receipt contract and standalone
continuity/freshness checks. The previously approved C1 proposal already
authorizes detailed output-disabled attachment development; no new blanket
human approval is needed. Master design release is bounded by
C1C_STATE_RECEIPT_CONTRACT.md. Commander hooks, actual estimator/clock/operator
provenance and diagnostic wire integration require their subsequent technical
reviews. Existing offline diagnostic guards are not weakened or connected live.
Geometry remains deferred until plant/controller feasibility needs it.

## Completed increment: C1B-O diagnostic owner/broker implementation (2026-09-20)

Master implementation verification and independent static review PASS for the
bounded offline owner/broker slice. See C1B_OWNER_IMPLEMENTATION_VERIFICATION.md
for exact source/artifact hashes, commands, failures and scope. Full C1B, C1
and roadmap Phase 2 remain OPEN.

Implemented one diagnostic owner, redacted console facade, shared one-shot
selection/admin tickets, exact observation/receipt binding, synchronous
disable/detach/loss closure and independent broker result validation. No live
PX4 endpoint, mode selection, control demand or actuator publisher was added.
Operator loss is injected offline; production loss detection is not implemented.

Measured: 28/28 targets pass final Debug, two Release runs and ASan/UBSan;
fresh default-off Release passes 17/17. The owner suite executes 1,124 checks
with zero failures; all 31 compile-isolation probes pass. Synthetic fixtures
do not establish live state/clock trust, signing or acceleration tracking.
Review found and fixed malformed-result acceptance and lifecycle/reentry gaps;
the earlier failed callback assertion and its correction remain documented.

Preservation: 713 entry files. Six existing source/build files and four current
gate documents intentionally changed; three source/test files and one owner
verification document added. Prior guard/adapter implementations and tests,
geometry, C1A/C1H artifacts, original setup files and separate Gazebo/Cessna
baseline remain outside this slice. No install, simulator/firmware build,
runtime connection, commit, push, flash or physical operation.

Next exact gate: C1B-E local JSON campaign/export/checking, evidence-sink
independence, deliberate corruption tests and variant-level matrix reconciliation.
That offline implementation is released under the existing contract and user
continuation request; no additional geometry or human decision is needed.
Future live diagnostic schema/profile and actual PX4 state/clock/estimator/
operator-loss provenance require their own review before promotion.

## Completed increment: C1B-A diagnostic adapter implementation (2026-09-20)

Master implementation verification and independent static review PASS for the
offline single-adapter slice. See C1B_ADAPTER_IMPLEMENTATION_VERIFICATION.md.
Full C1B owner/broker/evidence integration, C1 and roadmap Phase 2 remain OPEN.

Concrete C++ now connects the private guard to a separate operator-selected
diagnostic state, exact grant bindings, session establishment/revocation,
post-barrier ACK admission, request evaluation and synchronous lifecycle close.
Reports are bounded to four with explicit operation indexes; counter exhaustion
is reserved before guard mutation. Ordinary Flight APIs reject diagnostic
instances, diagnostic APIs cannot return demand, and frozen v1 status does not
serialize diagnostic reports. Supplied physical arm/mode values are preserved;
successful validation requires actual Disarmed/NotSelected.

Measured: 24/24 targets pass Debug, two Release runs and ASan/UBSan; fresh
default-off Release passes 17/17. The adapter executes 3,400 explicit checks;
the existing guard still executes 1,024 tuples/12,163 checks. All 25 compile
isolation probes pass. These are synthetic offline software tests, not flight
scenarios, measured link timing, live operator provenance or acceleration tracking.
The first build found two unhandled appended enum values; the initial adapter
test found an incorrect age-evidence expectation. Both were corrected with
failures documented, without changing existing Flight test expectations.

Preservation: entry inventory 709 files; five existing source/build files and
four current documentation files intentionally changed, three source/test
files and one verification document added. Geometry, ordinary test sources,
existing owner/broker, C1A/C1H evidence, six original setup files and separate
Gazebo/Cessna baseline are outside this slice. No PX4/simulator build, live
connection, install, commit, push, hardware operation or physical test.
Final SHA-256 audit confirms exactly those nine intended existing-file changes;
all other entry files are unchanged. Both Git pins and clean Gazebo state remain.

Next exact gate: implement the diagnostic owner/broker facade, shared one-shot
confirmation tickets, operator-loss closure and independent result validation;
then complete the local diagnostic evidence writer/checker and remaining matrix.
Existing approval permits that offline work without new geometry or human
input. Live PX4 promotion still needs actual state/clock/estimator/operator-loss
provenance and output-disabled integration acceptance. No active controller
or physical feasibility claim is released by this gate.

## Completed increment: C1B-G diagnostic guard implementation (2026-09-20)

Concrete C++ implementation, master verification and independent code review
are PASS for the private guard slice. See C1B_GUARD_IMPLEMENTATION_VERIFICATION.md
for source/executable hashes, commands, logs and coverage limits. Full C1B
adapter/owner implementation, C1 and roadmap Phase 2 remain OPEN.

Implemented private immutable diagnostic construction, separate typed results,
shared payload/context/time/replay validation, diagnostic session invalidation
and latch recovery, no-cache/no-eligibility invariants, and cross-domain API
rejection. The ordinary Flight request cache assignment remains exclusively in
its original-purpose wrapper; no physical state or flight Selected flag is forged.
The default-off standalone target excludes diagnostic types from ordinary
builds and rejects firmware/lockstep/proxy use. No live owner or output is added.

Measured: 20/20 targets pass final Debug, two Release runs and ASan/UBSan.
Fresh default-off Release passes the existing 17/17 targets. The new guard test
executes 1,024 predicate tuples, 88 request evaluations and 12,163 checks without
failure; 18 compile-isolation probes pass. Counts include setup assertions,
not flight scenarios or exhaustive coverage. Existing tests/expected values are
unchanged. Only synthetic offline state and timing were used.

Entry preservation inventory: 704 files. Changes are limited to RequestGuard
header/source, its standalone CMake and five current-gate documentation files;
four new implementation/test files and one verification document are added.
Geometry, existing tests, WireAdapter/owner, C1A/C1H artifacts and the separate
Gazebo/Cessna baseline remain outside the edit scope. No installs, simulator/
firmware builds, runtime links, commits or physical operation.
Final 704-file SHA-256 audit confirms only the eight intended existing-file
changes; final inventory is 709. Both Git pins and the clean Gazebo state are
unchanged; scoped AStyle dry-run and whitespace checks pass.

Next exact coding gate: implement the accepted diagnostic single-adapter
selector, session/ACK barriers and bounded reports around this guard, followed
by owner/broker closure and the full diagnostic evidence matrix. No further
design approval or geometry is needed for this offline continuation. Passing
guard tests does not qualify the unimplemented adapter or operational inputs.

## Completed increment: C1B-I offline API/test design (2026-09-20)

Master and independent exact-design review are PASS. New artifacts:
C1B_OFFLINE_API_CONTRACT.md and C1B_OFFLINE_API_TEST_MATRIX.md. This is design
acceptance only: no new source implementation, build or runtime test was done.
Full C1 and roadmap Phase 2 remain OPEN. C1A passive PX4 evidence is unchanged.

The contract specifies immutable diagnostic construction, private single-guard
entry points, typed owner/broker administration and synchronous lifecycle close,
four-record bounded reporting with an explicit operation index, and a separate
local evidence schema. The acceptance specification includes 1,024 guard-state
tuples and named ownership, replay, arithmetic, capacity and evidence negatives.
Counts are specified cases, not executed assertions or coverage claims.

Review corrected ACK chronology, structural versus health failures, no-latch
clear behavior, post-prefix grant fixtures, missing lifecycle API and console
result typing. Hand arithmetic and pinned-source comparisons pass; no test
failure was silently discarded or reclassified as a runtime pass.

Entry inventory: 702 pre-existing files. Only six existing design/status
documents are intentionally updated; two API/test documents are new. Source,
tests, C1A artifacts, geometry and the separate Gazebo/Cessna baseline are outside
this edit scope. No install, build, runtime connection, commit or hardware action.
Final SHA-256 audit of all 702 entry files found only those six intended
documentation changes; the final inventory has 704 files. Whitespace checks
pass, both Git pins are unchanged, and the separate Gazebo repository is clean.

Next exact gate: implement the accepted contract in an opt-in standalone offline
target, then pass unchanged Flight regressions and the diagnostic matrix in
Debug, repeated Release and ASan/UBSan, with independent code/evidence review.
Master permits this bounded implementation under existing software authority;
there is no further design-only approval step or current human-input blocker.
Live diagnostic/PX4 promotion still requires lossless state/estimator/clock/
operator-loss provenance, explicit diagnostic namespace/profile binding and
no-output integration acceptance. Geometry remains deferred for this lane.

## Completed increment: C1B-D diagnostic-selection design (2026-09-20)

User approved design of a separately labelled SITL diagnostic selection state.
Master and independent architecture review are PASS; implementation is NOT
released. Artifacts: C1B_SITL_DIAGNOSTIC_SELECTION_DESIGN.md and
C1B_SITL_DIAGNOSTIC_SELECTION_TEST_SPEC.md. The test matrix is a specification,
not executed evidence. Full C1 and roadmap Phase 2 remain OPEN.

The design uses an immutable diagnostic validation domain and a separate
operator-owned selector. Actual flight-mode Selected is unchanged; actual
Disarmed/NotSelected, required estimator state, clock proof and every shared
request/session/ACK check remain required. Diagnostic results cannot cache or
copy control demand, and cannot silently use the frozen v1 Flight status schema.
Selector changes explicitly invalidate sessions/ACKs through reviewed barriers;
they do not fabricate physical-state generations or reset replay floors.

Review corrected rejected-admin mutation, recoverable versus terminal continuity
handling, prospective-session map ordering, and session-scoped floor-reset
semantics. Existing source, tests, wire contracts, C1A artifacts, baseline and
all geometry remain unchanged. Entry inventory covers 700 pre-existing files;
only four existing gate/status documents and two new design documents are in
scope. No builds, runtime connections, installs, commits, flashing or outputs.

Next exact gate: C1B-I offline API/evidence review, including all guard entry
points, explicit selector/admin barriers and report-capacity bounds, before any
implementation release. Positive live validation additionally needs qualified
PX4 transition/estimator/clock/operator-loss provenance and diagnostic schema/
endpoint binding. The C1A estimator-free probe cannot satisfy that gate. No new
geometry or objectives clarification is needed for the completed design.

## Completed increment: C1A passive PX4 attachment (2026-09-20)

The latest user "Proceed" approves C1_PX4_ATTACHMENT_PROPOSAL.md. Master
design review released its first bounded slice in C1_PX4_ATTACHMENT_DESIGN.md:
an isolated, output-disabled PX4 SITL build and passive actual state/clock probe.
Implementation, source/configuration review and measured acceptance are PASS
at passive C1A scope; see C1A_PX4_PROBE_VERIFICATION.md. A fresh isolated Debug
build completed 471 steps. Two fresh PX4 processes each yielded 21 clock samples
and three fresh disarmed publications per state topic, passed the independent
oracle and shut down normally. Eleven synthetic checker methods pass.
Full C1 and Phase 2 remain OPEN.

Earlier compilation/configuration/runtime dependency failures remain retained.
Six existing parameter YAMLs resolve Commander dependencies without enabling
their owning modules. Final logs have no unresolved parameter errors, but retain
missing-sensor/safety/preflight and event-capacity warnings: no estimator or
complete health-report readiness follows. No thresholds were relaxed.

The 689-file entry audit preserves all 574 geometry files and existing software/
baseline content except four intentionally updated approval/status documents.
Build/run source hashes are stable; separate Gazebo and stock Cessna are intact.
No install, commit/push/PR, hardware build/flash or powered physical operation.

This supersedes the historical no-PX4-build boundary below, only within the
approved passive scope. No receiver, admission owner, estimator, Gazebo bridge,
controller, allocator or output module is instantiated. Stock Cessna and the
separate Gazebo repository remain outside the edit/build paths. Real state
observations must not be replaced by simulated trusted flags.

The subsequent user-approved C1B-D design above resolves the diagnostic-selection
design decision without changing Offboard/Selected semantics or granting output
authority. Positive implementation and live validation remain separate gates.

## Completed increment: C1H live host-loopback gate (2026-09-20)

The user approved the bounded PHASE_2_LIVE_HOST_PROPOSAL.md with "Proceed".
Detailed parser/clock/owner design preceded implementation. C1H is now **PASS**
at its output-disabled host-only scope; full roadmap Phase 2 remains **OPEN**.
See C1H_HOST_LOOPBACK_DESIGN.md and C1H_HOST_LOOPBACK_VERIFICATION.md.

Real signed UDP requests now traverse generated authentication, exact channel/
identity/context binding, the single existing admission owner, actual native
access and authenticated returned status. Authority uses independent cooperative
operator prepare/confirm; vehicle state remains explicitly simulated. No PX4,
Gazebo, QGC, TELEM, hardware or actuator connection was made.

One Debug and three Release campaigns passed with unchanged criteria, each
verifying 20 nominal requests and 27 returned statuses plus separate asserted
fault scenarios. Maximum observer gap across them: 1.552 ms against 2 ms.
The nominal oracle rejects 20 corrupted-artifact mutations. Existing 17-target
regressions pass fresh strict Debug, three Release repetitions and ASan/UBSan;
new native tests pass all configurations and 58 new Python test methods pass.

Earlier experiments failed observer timing at 2.967, 2.161 and 2.279 ms and
remain retained. Instrumentation measured 1.050845 ms of cyclic collection
inside the third gap. A reviewed, trace-capped finite-window GC deferral with
finally restoration removed that observed disruption in the four predeclared
verification runs. This is finite host evidence, not worst-case real-time or
Pi/serial qualification. No timing target was relaxed.

Entry preservation covers 685 existing files including 574 geometry assets.
Only the named status/proposal/decision docs and bounded supervisor-owner
extension change existing content. Guard/codec/control foundations, geometry,
launchers and stock Cessna remain intact; the separate Gazebo repo is clean.
No install, commit/push/PR, firmware/simulator build, flash or physical test.

Next human authority boundary: C1_PX4_ATTACHMENT_PROPOSAL.md, for reviewed
output-disabled PX4 SITL attachment, optionally with disarmed stock Cessna
Gazebo state plumbing. Approval is still required before those connections or
builds. No geometry measurement is needed for this interface gate. Active
custom-vehicle control still needs its own plant/control/normalized-allocation
and complete acceleration-path evidence.

## Completed increment: remaining offline gates P2T2/P2T3 (2026-09-19)

User direction: continue through all development gates not requiring approval.
P2T2 status-delivery/diagnostic tests and P2T3 acceleration-only contract tests
plus declarative policy review are **PASS** at their bounded scope. Master and
independent source reviews accepted the implementation and corrections.

Verification: 17/17 CTest targets pass strict Debug, Release repeated three
times and ASan/UBSan. P2T2 checks 4,110 ordered events and rejects 36 corrupted
evidence/JSON cases, including actual returned payload corruption. P2T3 has six
test methods with exhaustive uint8 semantic-tag alternatives and forbidden
objective/extension tests. Existing body-held/frame/guard/state/session/status/
operator-owner tests are rerun; Python codec 8 and evidence checker 17 pass.
No sanitizer diagnostics. Detailed logs/hashes/reproduction and fixes are in
PHASE_2_STATUS_DELIVERY_VERIFICATION.md and PHASE_2_POLICY_VERIFICATION.md.

Review found an ID-only return could not prove queue payload preservation;
actual returned bytes and an independent corruption check now close that gap.
Strict Release compilation found insufficiently explicit array bounds in a
queue shift; depth assertions and a capacity-bounded loop fixed it without
weakening warnings. No guard, codec or flight-library behavior changed.

PHASE_2_OFFLINE_COVERAGE.md accounts for all seven approved profile groups,
separating new tests, freshly rerun inherited tests and policy-only obligations.
The released offline campaign is accepted at that scope, not as exhaustive
fault-combination coverage or an executable residual-attitude controller.
Full roadmap Phase 2 remains **OPEN** for operational budget/contract evidence.
The complete acceleration path still has explicit missing live/control/output
links; neither received frames nor status bits establish tracking.

Pre-existing content was snapshotted across 678 files, including all 574
geometry files. Only assigned new test/docs paths, standalone test CMake and
named status/architecture documents are changed. Existing geometry, flight
libraries, codecs, supervisor implementation, stock Cessna and separate Gazebo
repository remain unchanged. No install, commit, push, PR, firmware/simulator
build, live link, keys, flashing or physical operation occurred.

Next necessary authority boundary: C1H host-only live-loopback qualification
as proposed in PHASE_2_LIVE_HOST_PROPOSAL.md. It needs explicit approval for
local I/O, disposable test signing material and clock qualification. No live
PX4/Gazebo/QGC/TELEM/hardware connection is proposed. Geometry is not needed
for this decision. The proposal is not implemented; stop before that boundary.

## Completed increment: P2T1 approved offline timing profile (2026-09-19)

The user approved PHASE_2_SITL_PROFILE_PROPOSAL.md for offline SITL testing only.
The timing values and residual-attitude/inactive-loss policies are now approved
synthetic test/design targets, not measured operational requirements.
No live link, keys, mode, controller, output or hardware authority was approved.

P2T1 bounded implementation gate: **PASS**, master and independent source review.
The new Tools/research_sitl/profile fixture exercises the actual adapter/guard
and status encoder with injected state/time/authentication inputs. Independent
Python evidence checks verify all status fields and timing/correlation records.
Strict Debug, three repeated Release runs and ASan/UBSan each pass 14/14 CTest
targets. Each checks 113 timed records and rejects 27 malformed packages;
the C++ fixture additionally checks nine cache faults and 2001 accesses with
independent injected heartbeat events. Codec 8 and evidence 17 regressions pass.
See PHASE_2_PROFILE_VERIFICATION.md for commands, hashes, failures corrected,
coverage limits and retained logs. No flight/admission source/default changed.

Entry hashes covered 675 existing files including supplied geometry. Only the
standalone test CMake entry and named phase/profile/status documentation are
intentionally edited; all geometry, existing flight libraries, codecs, supervisor
implementation, Cessna baseline, launchers and separate Gazebo repo are preserved.
No install, commit, push, PR, firmware/simulator build, flash or physical test.

Full Phase 2 remains **OPEN**. P2T1 is not the full seven-group profile campaign,
host/Pi/link timing evidence or active acceleration tracking. Local accessor
snapshots are not transmitted diagnostics. Next offline gate: reviewed
status-loss/reorder/backpressure and diagnostic scheduling instrumentation;
retain explicit pending residual-attitude policy-only coverage and deployment
decisions. This work needs neither new geometry nor reapproval of the profile.
Plant/control promotion remains blocked by its own physical evidence gates.

## Completed increment: Phase 2 exit audit and C0c conformance (2026-09-19)

User direction: continue to the end of Phase 2, keeping geometry deferred until
necessary. PHASE_2_EXIT_AUDIT.md maps each roadmap criterion to existing evidence
and missing approval. Phase 2 remains OPEN; it is a contract/source-trace gate,
not a requirement to finish the Phase 4 controller first. No runtime or physical
readiness follows from accepted offline libraries.

C0c design is accepted with the false-pass controls in
C0C_GENERATED_WIRE_CONFORMANCE.md. Parent-owned research.xml, a generated-C host
fixture and an independent Python conformance runner are the only implementation
paths. IDs 200000/200001/200002 are offline-local, not a global allocation or live
deployment choice. No MAVLink/PX4/Gazebo submodule is edited, and no live I/O,
signing keys, firmware integration, controller or output is enabled.

C0c implementation gate: **PASS**, master and independent review. Strict Debug,
optimized (-O2), and ASan/UBSan host runs each pass 32 round-trip vectors,
8 framing rejections and 6 semantic rejections. Generated CRC extras are
130/188/131 for request/session/status. Mandatory schema validation, exact clean
source pins, import provenance, every C field offset/type and base length,
independent checksum/header oracle and actual C/Python bidirectional decoding
are checked. Tests include all permitted partial zero tails, uint64/float-bit
boundaries, nine status vectors, CRC-correct unknown-ID traffic and actual
MAVLink-1 pack rejection. Unsigned tests do not prove authentication/admission.

Observed toolchain: GCC 13.3.0, Python 3.12.3, lxml 6.1.3. Retained successful
generated outputs/executables are `/tmp/c0c_mavlink_5s9esiqa` (Debug),
`/tmp/c0c_mavlink_lnmqgtq5` (optimized) and `/tmp/c0c_mavlink_ov_msumi`
(ASan/UBSan). The runner prints JSON pins/hashes/flags/results; reproduction is
documented in Tools/research_sitl/wire/README.md. Existing 12-target interface
suites pass Debug/Release/sanitizers; Python codec 8 and evidence 17 pass.

Failures corrected before acceptance: generated base lengths were initially
confused with logical trimmed minima; Windows/WSL CRLF status differences
required explicit command-local Git text conversion, not source edits; the
Python generator needed its owned output directory created. The C fixture's
nominal payload report initially included checksum bytes stored after trimmed
payload length by MAVLink finalization. It now prints transmitted payload plus
zero-filled absent tail, with an independent whole-frame round-trip check.
Review also corrected the unknown-ID checksum fixture and added true Python
decoding and separate codec semantic negatives. No validation was relaxed.

The user requested a provisional SITL-only operating proposal while this work
ran. PHASE_2_SITL_PROFILE_PROPOSAL.md supplies candidate timing, PX4-owned
residual-attitude policy, inactive loss rules and a measurement plan. Values
are proposals, not operational settings or measurements. No geometry input is
required for the current review; no control/physics constants were invented.

Phase 2 signoff still needs approved command timing/control-link budgets and
residual-attitude/loss-response requirements. Live clock, signing, source and
supervisor deployment procedures remain a separate gate. A provisional test
profile is not a flight requirement or a substitute for the missing approval.

## Completed increment: O2 offline owner implementation (2026-09-19)

User direction: continue without geometry until it is actually required.
Master releases the already-reviewed O2 design for isolated host implementation.
No new geometric input is needed or requested for this software gate. Supplied
geometry and the assumed 3.041337 kg total mass remain outside executable
configuration; no plant parameters, mixer effectiveness or gains are invented.

Disjoint ownership: wire_design_increment owns new HarnessSupervisorOwner.hpp
and .cpp (including the thin console facade); evidence_checker_increment owns
HarnessSupervisorOwnerTest.cpp; baseline_agent reviews read-only. Root owns
standalone BUILD_TESTING-only integration, documentation and all build/gate
verification. O1/adapter/guard/codec/physics source remains unchanged.

Acceptance: actual adapter-backed session cases, receipt immutability and
replay/age boundary matrix, permanent provenance-fault behavior, complete
view comparison, no polling from UI calls, independent later tick behavior,
strict Debug/repeated Release/ASan+UBSan, old regressions and preservation.
O2 offline implementation gate: **PASS**, master and independent review.
No runtime scheduler, console terminal, live link or controller/output is
released. This is Phase 2 software-foundation progress, not promotion through
the Phase 2 live-interface gate or Phase 3/4 plant/control gates.

Completed artifacts: HarnessSupervisorOwner.hpp/.cpp own one real adapter by
value and one immutable ingress slot; the facade exposes only attach/detach
and prepare/confirm. HarnessSupervisorOwnerTest.cpp exercises real establish,
revoke, clear, consumed rejection, provenance closure, complete view comparison,
packet/ingress validation, expiry at offer and dispatch, terminal/quarantine,
restart isolation and no-wrap receipt IDs. CMake includes both owner and test
only under the standalone host project's BUILD_TESTING. Existing adapter,
broker, request guard, codec and normalized-output limiter source is unchanged.

Measured software verification (Ubuntu-24.04 WSL, GCC 13.3.0):

- Strict Debug: 12/12 CTest targets pass, including 169 O2 assertions.
- Strict Release: all 12 targets pass three consecutive runs each.
- ASan + UBSan: 12/12 pass. Strict flags in every build are
  `-Wall -Wextra -Wpedantic -Werror`; sanitizer flags are
  `-fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie`, with
  executable linker flags `-fsanitize=address,undefined -no-pie`.
- Existing Python codec/evidence/synthetic geometry suites: 8/17/8 methods
  pass respectively. Separate H1 strict Debug limiter regression: 1/1 pass.
- BUILD_TESTING=OFF lists no supervisor or owner targets. Repository AStyle
  dry run reports all three new C++ files unchanged; tracked diff whitespace
  check passes. No whole-tree format or firmware/simulator build was run.

Builds and CTest logs are under `/var/tmp/research-o2-owner.AKfOxRKQ/` in the
`debug`, `release`, `sanitize`, `h1` and `no-tests` directories. Reproduction
commands are in Tools/research_sitl/supervisor/README.md. Counts include setup
assertions and do not claim exhaustive coverage or operational performance.

Failures found and resolved: early test packets incorrectly populated ACK-only
fields for establish/revoke; those fixtures now use required zero values.
A fixture then incorrectly expected armed administrative revocation to succeed.
The unchanged adapter requires disarmed administration: tests now assert the
consumed ContextRejected result while armed, then an explicit disarmed state
transition and successful revoke. Source review also tightened invalid-intent,
invalid-configuration, clear-tuple and impossible-correlation handling before
acceptance. No validation condition was relaxed to make tests pass.

Evidence limits: fixtures supply state, time and positive parser verdicts;
they are not live authentication or clock measurements. Source review verifies
one poll per usable tick and no UI polling; this is not a scheduling/deadline
proof. The impossible corrupted-result correlation branch is source-reviewed,
not directly injected in this concrete owner; O1 tests cover unknown/malformed
owner results and quarantine. No concurrent-thread safety claim is made.
There is still no complete acceleration-to-control/allocation/plant tracking
path. Ground truth is not introduced, and no new command publisher or allocator
unit convention is introduced by O2.

Preservation: the 86-file entry inventory shows only the explicitly scoped
CMake/README/status/design/decision/timeline changes. All 574 geometry files
retain aggregate SHA256
`D1A5CED61C6A058AC9F7F027CFF9340E273E0EA75705A5E7C6900408D721971E`.
Separate Gazebo repository remains clean at the pinned revision above; stock
Cessna, existing launcher and unrelated pre-existing changes are preserved.

Next release gate: master and independent review of a bounded offline
generated-dialect/framing conformance design, with pinned tool/message inputs,
disjoint ownership and a payload-to-generated-layout oracle. That design work
needs no geometry or new human decision. It must not silently release live
I/O, trusted clocks, signing keys, authority, mode selection or actuator output.
Live-interface implementation still needs the deployment choices in
RESEARCH_DECISION_CHECKPOINT.md. Physical data becomes necessary at validated
plant/effectiveness/control-law promotion, not at this offline software boundary.

## Completed review: O2 design and G1 geometry intake (2026-09-19)

Roadmap position: Phase 2 interface/software foundations, with Phase 3 geometry
intake in parallel. These O/G labels are internal increments, not additional
roadmap phases. Phase 0 feasible-envelope evidence and Phase 1 reproducible
stock-baseline runtime evidence remain open. Phase 4 active control is not
released; the H1 normalized-output limiter is not a mixer or controller.

G1 intake: the new `docs/vehicle_geometry/GAZEBO FILES/` SDF and three meshes
are inventoried without modification. See GAZEBO_GEOMETRY_INTAKE_REVIEW.md.
User-directed working assumption is **3.041337 kg all-up including canards and
rotor**; the old approximately 2.5 kg estimate is superseded, not retroactively
converted into a measurement. The input SDF still sums to 3.291337 kg due to
five additional placeholder masses. No arbitrary per-link correction is made.

The read-only intake checker and eight independent synthetic test methods pass
their software gate. The supplied MODEL does not pass: physical inertia
constraint failure and six unresolved mesh URI instances. Installed WSL
SDFormat 14.9.0 independently returns Error Code 19 for base_link inertia.
No Gazebo simulation or model installation ran; geometry is not calibrated
physics. The original CAD inertia/CG report with units/axes/component scope
is the minimum immediate human-owned input for correcting that physical gate.

O2 design review PASS (master and independent): SITL_SUPERVISOR_OWNER_O2_DESIGN.md specifies the deterministic
offline owner/facade boundary. It retains one by-value adapter, immutable copied
one-shot ingress receipts, explicit fixture ticks, full retained-view checks,
redacted existing console results and permanent provenance-fault disable.
It creates no scheduler, interactive terminal, live link or new flight authority.
The O2 owner/facade source and integration tests are NOT implemented by this
checkpoint. Design acceptance permits a bounded source/test assignment under
the predeclared oracle, followed by strict
Debug/Release/sanitizer regression; design acceptance alone is not O2 completion.

Fresh checks this increment: new geometry tests 8/8; unchanged codec 8/8 and
evidence 17/17; existing O1 Debug build rerun 11/11. No new C++ build was needed
or performed. Earlier Release/sanitizer evidence remains historical O1 evidence.
An initial checker syntax error from command quoting was fixed before the
structured audit; independent review found no remaining serious checker blocker.
Final preservation check: all 81 entry-hashed research files compared; only
the four intended requirements/decision/status/timeline documents changed.
All 574 geometry files remain unchanged, aggregate SHA-256
`D1A5CED61C6A058AC9F7F027CFF9340E273E0EA75705A5E7C6900408D721971E`.
The separate Gazebo repository remains clean at its unchanged recorded HEAD.

Full acceleration-path status: offline payload/map/admission/status and session
authority components exist. A live authenticated receiver, operational current
state/clock owner, selectable PX4 mode, acceleration controller, compatible
normalized allocator, servo/EDF bridge and validated plant are still missing.
No message reception or administrative success is reported as tracking.

Next roadmap promotion: Phase 3 needs resolved mass/inertia and CAD frames,
physical parameter provenance, representative sensors/actuators, separate model
packaging and reviewed open-loop tests, alongside the Phase 1 baseline evidence.
Phase 4 follows an approved feasible envelope, complete interface/ownership
contract and accepted plant. Timing, control limits and fallback behavior remain
human-owned engineering decisions; none are inferred from fixture values.

## Completed checkpoint: O1 cooperative supervisor broker PASS (2026-09-19)

User selected option 1: cooperative trusted-local SITL. This is not hostile
same-user isolation or hardware authority. Master accepts the bounded concrete
SITL_SUPERVISOR_BROKER_API.md for host-only implementation and independent
oracle review. Output directory Tools/research_sitl/supervisor was absent.
Root owns SupervisorBroker.hpp/.cpp, README and BUILD_TESTING-only registration;
root also owns SupervisorBrokerMatrixTest.cpp for exhaustive fault-injected
owner results, boundaries and confirmation invariants, separate from the
worker's actual-adapter fixture.
wire_design_increment owns the independent oracle and, after header handoff,
SupervisorBrokerTest.cpp. baseline_agent reviews read-only. Existing adapter,
guard, status, codec, controller, root build and Gazebo source stay unchanged.

This slice is the broker core plus real-adapter fixtures, not an interactive
console or runtime owner/scheduler. No physical values, live ingress, signing,
network/serial endpoints or operational confirmation lifetime are chosen.
Master accepts this bounded offline core after source/oracle review and
independent review. This does not complete the console or roadmap phase gates.

Measured host verification at `/var/tmp/research-o1-broker.c2VM3F6b`:

- GCC 13.3, C++17, `-Wall -Wextra -Wpedantic -Werror`: Debug 11/11,
  Release 11/11 and ASan/UBSan 11/11; each test repeated three times in each
  final configuration. No sanitizer diagnostics. Scoped AStyle completed.
- Supervisor fixture: 22 checks, including actual establish, consumed ingress
  rejection, revoke, pre-latched clear and same-owner polling after quarantine.
  An operator tuple without independent packet evidence makes no adapter call.
- Synthetic broker matrix: 188 checks including malformed owner responses,
  reentry, changed/expired confirmation, boundary arithmetic, last usable event,
  reserved report sequence, unconsumed terminal outcome and no reset/retry.
- Existing nine acceleration tests remain unchanged and pass. Python codec
  8/8, evidence checker 17/17, and fresh strict H1 host regression 1/1 pass.
- `BUILD_TESTING=OFF` exposes no supervisor target. No root/PX4 build wiring,
  simulator build, firmware build, live link, hardware operation or commit.

Failures resolved before acceptance: initial independent fixture had a narrowing
conversion; master fixture revisions briefly used incorrect member names.
Strict compilation rejected both, and they were corrected. Master review also
replaced submit-time packet fabrication with independent injected evidence,
fixed final-view comparison and same-owner quarantine polling. Independent
review found the reserved nonterminal report ID gap; source and regression now
reject it. Exact session-gate correctness on a rejected action remains the
trusted owner's responsibility; broker checks known values and exact success
gates without duplicating the adapter's state machine.

Preservation: compared all 74 entry-hashed files. Only five intended existing
documents and standalone CMake registration changed; old libraries, codecs,
evidence checker, setup/launchers and roadmap are unchanged. All 570 geometry
files retain aggregate SHA256
`2E27AB49BFA2FB4CAB5E9BC773745B8276B73421847FAA28652A6609F5F536E4`.
The separate Gazebo repository remains clean at the unchanged gitlink/HEAD
recorded above. Cessna runtime was not rerun; preservation is not a smoke-test claim.

Fact: the user selected cooperative trusted-local option 1. Assumption:
participating local processes cooperate; C++ roles are not hostile-process
isolation. Measurements above establish offline software behavior only.
Inference: O1 is suitable for the next offline owner/console design review,
not deployment, acceleration tracking, stabilization or actuator authority.

Next gate O2: master/independent review of a bounded host owner and local
console harness, with immutable independent ingress receipts, explicit fixture
clock/state provenance, one serialization domain, nonblocking observation,
redacted UI results and restart/unknown-outcome tests. No new human input is
required for that offline design. Live transport still requires approved
identity/authentication, timing and state/clock provenance decisions. No gains,
physical effectiveness, launch envelope or hardware fallback have been inferred.

## Completed checkpoint: C0b.3 offline status implementation PASS (2026-09-19)

Master accepts report-time evidence capture and pure status conversion. Each
historical report carries its own state, origin, diagnostics and accepted
sequence history. Status generation cannot poll, revoke, admit, allocate a
sequence or rewrite a report. Explicit named S1 conversion and precise local
causes feed the frozen 106-byte payload; missing identity/evidence and
unrepresentable outcomes fail without writing output. Acceptance/eligibility
is not execution, tracking, control allocation, output or flight readiness.

Fresh verification at `/var/tmp/research-c0b3-status.xSjnTWaj`:

- Strict C++17 Debug (`-Wall -Wextra -Wpedantic -Werror`): 9/9 pass.
- Release: 9/9 pass, each repeated three times.
- ASan/UBSan: 9/9 pass without diagnostics, including vector subprocess.
- Status tests: 47 historical/direct checks and 1266 matrix checks; nine actual
  adapter-history payloads match independent JSON fields and the unchanged
  Python codec, including the hand-transcribed accepted-request golden bytes.
- Existing guard/frame/map/state/session/admission suites all pass unchanged.
  Fresh H1 standalone test passes; Python codec 8 and evidence 17 methods pass.
- Independent review: no remaining source/oracle blocker. Master inspected
  each changed source, fixture and claim; scoped AStyle completed.

New tests cover every named S1 map, local cause families, all report classes,
recipient/flag/state consistency, 64-bit field extremes, unchanged failures,
historical establish/arm/ACK/request/access/expiry, rejected packets retaining
old-cache history, revoke/re-establish, ValidateOnly, producer/validator latches,
post-clear chronology, grants, invalid map/first state and terminal silence.
Matrix cases include deliberately synthetic typed reports, distinguished from
real adapter integration; check counts are assertions, not flight cases or an
exhaustive state-space proof. Generated MAVLink interoperability remains open.

Issues caught and corrected: missing grant owner/session and clear-binding
cause mapping; stale post-clear administrative-barrier diagnostics; a wrong
expiry member name; a missing return for an unknown gate enum; and contradictory
synthetic wrapper/S1 success reports. One grant fixture was structurally invalid
before its intended binding check and was corrected without weakening source.
A transient vector-name mismatch occurred while fixture expansion was still
in progress. All final configurations were rerun after final edits. A review
misread of post-clear ordering was explicitly corrected against source/oracle.

Tested SHA-256 (library paths relative to src/lib/research_acceleration):

| File | SHA-256 |
| --- | --- |
| WireAdapter.hpp | A3779F6B89335346CBAA36FA21B1F9C28143A14CA713D7B0BCD45A4308AE8316 |
| WireAdapter.cpp | 0D694C363722380E5D34447439C895E1F99B81E03EEA17C77D1F26D58009E544 |
| WireStatus.hpp | ACECED440536E88CE509A4A7AF88847CBCE374FBE466B842AD8C513CEB4A88B7 |
| WireStatus.cpp | A1BCD774CEA800D4E1663398C9C27CA9E66DF868546F105D1DDF1E1804B5B948 |
| WireStatusTest.cpp | 25FEF1FB4C924CE0156DE74B8BA68BA092289D0062B0918D770070077096DD95 |
| WireStatusMatrixTest.cpp | F1E07D5A50665A837CDB6DB0274AFEAE9A4BA98337656D63317B8BE6B0F7DE31 |
| CMakeLists.txt | E7BEE6AEC77ED174BA8C76D6DBE86CCBA5E64FDED19BAAF630E0E58CD66EF011 |
| Tools/research_sitl/wire/test_status_vectors.py (root-relative) | C8C7B091D56E9CBA4402997E2D0A30AB2E0F442C125FF82CB47AC8E19ADC917B |
| Tools/research_sitl/wire/status_vectors.json (root-relative) | B3E999EE585909C3CC3550CCF5717FA9ADC631FE8734CF2FC83BB7D1633FB9C9 |

Next boundary: C0b host admission/status foundations are accepted. Broader C0
generated dialect/framing interoperability and C1 live ingress remain separate
unpassed gates. User approved DESIGN ONLY of a SITL-local operator console for
independent session establish/revoke/latch-clear authority. The resulting artifact
is SITL_SUPERVISOR_CONSOLE_DESIGN.md, with no arming, output, deployment defaults,
hardware authority, live receiver or console implementation released. Physical
measurements still block validated plant/control work, not this design.

Console master review: architectural design accepted subject to the one human
policy choice in its trust-model section. The design separates operator intent
from authenticated guidance packets, keeps event reservation/consumption with
one broker and all adapter calls in one serialized owner domain, redacts UI
results, and never blocks trusted polling on UI/audit I/O. Unknown action
completion quarantines authority; a new test run requires an explicit operator
reset and new controller identity. No source API or implementation paths are
released. Independent review found UI-result and serialization ambiguities;
master corrected both, plus immutable ingress evidence and operator identity
provenance. Internal engineering semantics are not deferred to the human.

Human decision now: choose cooperative trusted-local SITL (no protection from
a malicious same-user guidance process) or OS-isolated operator authority.
That choice permits the concrete console API/test-oracle implementation-release
review. It does not unlock C1 live MAVLink or plant/controller/output gates.
No further geometry measurement is required for this console decision.

Preservation at handoff: 42 of 51 originally captured artifacts are byte-identical;
the nine intentional updates are the adapter/wire design headers, admission
oracle, timeline, decision checkpoint, gate, WireAdapter pair and standalone
CMake. New scoped source/tests/designs are additions. Supplied geometry remains
570 files with entry aggregate SHA-256
`2E27AB49BFA2FB4CAB5E9BC773745B8276B73421847FAA28652A6609F5F536E4`.
Stock source/launchers and separate gz HEAD/gitlink remain unchanged; gz is
clean. Tracked and scoped untracked whitespace checks pass. No staging,
commit, push, PR, installation, simulator/firmware build, live link, flash or
powered physical test occurred. Only isolated native host tests were built.

## Historical release: C0b.3 offline status design PASS (2026-09-19)

All design/oracle/review handoffs were inspected. Master accepts
`C0B3_STATUS_DESIGN.md` and its independent oracle with explicit report-time
capture, exhaustive named S1 mapping, aggregate-detail priorities, healthy
barrier code 70, no fabricated prestate acceptance, and fail-closed missing
epochs/unrepresentable local clear outcomes. Wire-contract terminal/no-report
exceptions are now explicit; this does not promise live feedback availability.

Disjoint implementation ownership: wire_design_increment may edit only
WireAdapter.hpp/.cpp to append/capture evidence without changing admission
decisions, guard calls or counters. Astra owns new pure WireStatus.hpp/.cpp and
standalone CMake integration. evidence_checker_increment owns new
WireStatusTest.cpp and Tools/research_sitl/wire/test_status_vectors.py plus
status_vectors.json. baseline_agent performs independent read-only review.
All paths are inactive host-library/test paths; no old tests, codec/S1/map
sources, deployment defaults, live transport or flight-output paths are released.
Acceptance requires real historical adapter reports, unchanged regression
results, exact C++/Python payload conformance, strict Debug/repeated Release/
sanitizer tests, and independent review. Implementation is not yet accepted.

Master also owns a separate `WireStatusMatrixTest.cpp` for exhaustive typed
status failures and real lifecycle regressions. After source handoff,
wire_design_increment owns the nine historical fixtures in WireStatusTest.cpp
and the matching Python/JSON vectors; the original test worker is inactive.
This keeps source review and independent fixture expansion non-overlapping.

## Completed increment: C0b.2 admission implementation PASS (2026-09-19)

Master and independent review accept the isolated ACK/request-admission slice.
The complete decoded request now passes current trusted-state observation,
immutable post-transition ACK, checked time-map conversion and real S1
validation before the existing gated accessor can return it. One private guard
owns the sole request cache. This is not a MAVLink receiver, custom flight mode,
acceleration controller, allocator, plant or actuator-output path.

Verification at `/var/tmp/research-c0b2-admission.HJbZwMJ0`:

- Strict C++17 Debug: 6/6 executables pass; admission 603, session 361,
  state 907, guard 231, frame 165 and map 191 fixture checks.
- Release: 6/6 pass, each repeated three times.
- AddressSanitizer/UndefinedBehaviorSanitizer: 6/6 pass, no diagnostics.
- Fresh unchanged H1: 843 checks; Python codec: 8 methods; evidence: 17 methods.
- Independent final source and oracle review: PASS. Scoped AStyle and whitespace
  checks pass. Counts include setup assertions, not independent flight cases.

The oracle covers full decoded fields/ingress/bindings, strict ACK and request
ordering, signed/unsigned timing boundaries, retained-cache rejection and fresh
replacement, real S1 replay/bounds/latches, disarmed policies, public revoke,
re-establish/reconnect/rebind, all public barrier paths, structural faults,
invalid-first state, counter/epoch/floor maxima and stable terminal silence.
Converter branches preempted by wrapper checks remain in direct C0b.1 tests;
no private guard/session/ACK mutation manufactures otherwise unreachable cases.

Review history: source review corrected incomplete ACK-invariant checks and an
unused private parameter. Initial tests had an invalid AdapterResult member and
a hard-coded helper report number; corrected before the first build. The first
build passed historical tests but three new fixtures failed: two accidentally
changed maps within one generation, and one expected ordering ahead of timing.
Those fixtures were corrected without weakening production checks. Master
expanded the initial oracle; all final configurations were rerun afterward.

Tested SHA-256 under `src/lib/research_acceleration/`:

| File | SHA-256 |
| --- | --- |
| WireAdapter.hpp | A1DAECD83427B199D3CD0A13BEF8BA8F524AA9407877EE6798835E5E6D77215E |
| WireAdapter.cpp | 3B7A013DBB314140355056D99A26D18827D732051BD57426986561FCD17DE171 |
| WireAdapterAdmissionTest.cpp | 4428AFEAC6C07BAC853A941C35001FF886ABDF1A68871E085BC96C8CDD65AFD9 |
| CMakeLists.txt | AEC4DE03C943E4419C0E37BAD7759D77D11181BD3577ACEC1B0E3BD292A07723 |

All entry-hashed artifacts except the explicitly edited adapter/CMake,
admission oracle, gate and wire-status exception wording remain byte-identical.
New admission design/test and candidate C0b.3 design/oracle are additional files.
The separate gz repository remains clean. No simulator/firmware build, install,
commit, push, PR, flashing or physical operation occurred. Geometry was only
filename-inventoried (568 STEP files, ZIP and README), not physically qualified.

Next gate: finish the concrete C0b.3 immutable report-time evidence and pure
status/payload mapping review, then its named isolated implementation/tests.
Candidate `C0B3_STATUS_DESIGN.md` and `C0B3_STATUS_TEST_SPEC.md` exist but are
not implementation releases. No human deployment values are needed for that
offline slice. Live attachment still requires authority/identity/signing/clock
and timing decisions; plant-supported controls require measured physical data.

### Prior admission design release (2026-09-19)

Master disposition: PASS for the bounded offline design in
`C0B2_ADMISSION_DESIGN.md` and its independently authored
`C0B2_ADMISSION_TEST_SPEC.md`. All three worker results were reviewed. Root
resolved the review findings before release: explicit packet-kind None for
barriers, corrected integer/timing fixtures and rejection precedence, legacy
grant-consumption compatibility, and no ACK-created transition barrier.
Only the newly reachable ordinary poll/S1 Accepted mapping becomes Observed;
existing disarmed/no-cache mappings and unmodified historical tests are retained.

Released paths: source owner wire_design_increment owns only
`src/lib/research_acceleration/WireAdapter.hpp/.cpp`; independent test owner
evidence_checker_increment owns only new `WireAdapterAdmissionTest.cpp`.
Astra owns minimal standalone CMake registration, formatting, host tests and
gate synthesis; baseline_agent reviews read-only. No S1/map source, old test,
root build, model, live receiver, operational control or output path is released.
Fresh host output: `/var/tmp/research-c0b2-admission.HJbZwMJ0`. Before source
changes, strict Debug passes the five existing acceleration executables; fresh
H1 passes 843 checks, codec 8 methods and evidence checker 17 methods.
Implementation acceptance remains pending source review and fresh complete
Debug, repeated Release and sanitizer verification.

The user resumed development and requested continuing until human input is
required. The prior pause is revoked. Resume starts with the saved admission
design and independent test oracle; it does not skip their review or authorize
live ingress, physical defaults, simulator/firmware builds or hardware operation.
The three prior workers have resumed their disjoint design/oracle/read-only
review assignments. Implementation remains unreleased until master acceptance.

### Historical pause checkpoint (2026-09-18)

The user requested pausing development during the ACK/request-admission design
gate. All three workers were interrupted. This increment has not passed a
design gate and releases no implementation. No code, build, simulator, firmware,
transport or physical operation was performed during this paused increment.

Saved work: `C0B2_ADMISSION_TEST_SPEC.md` is an incomplete, unreviewed draft.
`C0B2_ADMISSION_DESIGN.md` had not been created when paused. At pause, all 62
entry-hashed pre-existing artifacts were byte-identical; only this checkpoint
subsequently updates the gate record. The separate gz repository remains clean.
The last completed gate remains the session-administration implementation below.

### Resume from here

Read this checkpoint, required repository instructions and the full roadmap;
inspect Git status before continuing. Resume the admission design and independent
oracle, not flight-controller or live-ingress implementation. Prior worker
ownership was `wire_design_increment` for the new design document,
`evidence_checker_increment` for its test specification, and `baseline_agent`
for read-only independent review; Astra owns synthesis and gate disposition.

Provisional design direction, not yet approved:

- A separate `processAcknowledgement` handles frozen operation 3 without a
  supervisor grant; existing `processSession` keeps operation 3 unsupported.
  ACK success shares the established session-operation floor, but creates no
  new epoch/barrier. Request sequences remain separate S1 per-session state.
- A complete decoded request reaches C0b.1 conversion and S1 only after current
  session, authenticated/bound ingress, immutable ACK and strict raw/conservative
  post-ACK ordering checks. Keep one private guard and no second request cache.
- The existing getter may become usable only through a reviewed gate and S1's
  own accessor/internal poll; ordinary methods still perform exactly one poll.
  ACK-gate openness is distinct from an eligible cached request.
- Every transition/admin-success/terminal clears ACK; observed S1 latching
  also closes it. Rejected packets/admin operations must preserve a valid ACK
  and prior request unless independently observed state/time or S1 rules revoke.
- A newly installed barrier cannot be acknowledged in the same call: earliest
  ACK must be greater than barrier time yet no later than receive time, and
  those two times coincide for that call.
- Pre-poll expiry/age rejection of an old cached request must not prevent a
  fresh valid replacement from being evaluated in the same request call.
  A rejected packet's S1 decision does not alone describe retained-cache health.
- Resolve exact typed API/codes, precedence, report capacity, ACK point-mapping
  arithmetic, ordinary/admin result semantics and numerical fixtures before
  authorizing source/test paths. The draft has not received final consistency
  review; do not treat its example statements as accepted requirements.

No additional physical measurements are needed to resume this offline review.
Operational authority/timing approvals and measured plant/control inputs remain
later dependencies. The 2026-09-19 user direction above authorizes resuming.

## Previous increment: C0b.2 session-administration implementation (2026-09-18)

User requested continued development and a full completed-to-remaining timeline,
and reconfirmed non-weaponized research use. `DEVELOPMENT_STATUS_TIMELINE.md`
records that scope and the end-goal dependencies. No targeting/interception
capability, live transport, command admission or output-producing integration
is included in this slice.

**Gate result: PASS for isolated C0b.2 session administration.** The prior
design gate below released only `WireAdapter.hpp/.cpp`, the independent new
`WireAdapterSessionTest.cpp`, and minimal standalone CMake registration. Source
and tests have separate worker owners; Astra owns formatting, host verification,
post-handoff corrections, preservation and gate disposition. The baseline
worker performs independent read-only review. No S1/time-map source or existing
tests changed. The completed slice establishes/revokes sessions, validates the
independent ordered grant stream, retains distinct replay floors and supports
disarmed latched-fault recovery. Ordinary two-record results and closed getter
behavior remain unchanged; the admin result permits three ordered records.
An established session is still not permission to admit an acceleration request.

Fresh host verification directory: `/var/tmp/research-c0b2-session.wU6gnxDb`.
Installed WSL Ubuntu-24.04, GCC 13.3.0, CMake 3.28.3 and AStyle 3.1; no package
installation or simulator cache/mount use.

### Verification and independent review

| Check | Final result |
| --- | --- |
| C++17 Debug, `-Wall -Wextra -Wpedantic -Werror` | 5/5 executables pass: session 361, state core 907, guard 231, frame 165, map 191 checks |
| Release, same strict warnings | Same 5/5 pass, every executable repeated three times |
| Debug AddressSanitizer/UndefinedBehaviorSanitizer | Same 5/5 pass; no sanitizer diagnostics |
| Fresh unchanged H1 regression | 843 checks pass |
| Fresh C0a/E1 Python regressions | 8 codec and 17 evidence methods pass, no skips |
| Formatting/whitespace | Three changed C++ files unchanged by final scoped AStyle dry-run; all changed files whitespace-clean |
| Final independent review | PASS, no production blocker or mandatory reachable oracle gap identified |

The session suite covers first and subsequent state observations, explicit
packet/ingress/grant predicates and precedence, administrative report order,
single-use grant consumption, map rebind on all ordinary APIs, first-zero and
strictly increasing packet-operation floors, close-only revoke through context
loss, latch clearing before/after a session, distinct re-establishment, retained
terminal causes, checked report/epoch/event extrema and unchanged closed output.
All test identities/times/limits are synthetic, not operational settings. Counts
include setup/helper assertions, not flight scenarios or exhaustive coverage.

`S1ActionRejected` handling was source-reviewed but not forced by tests: with
the real S1, immutable valid configuration, serialized calls and the wrapper's
complete preconditions, an establish/clear action rejection is unreachable.
No mock guard, race or forbidden state injection was introduced to manufacture
that result. Deployment authentication/authority and clock provenance remain
injected facts, not implemented security services.

Review/failure history: master review corrected missing malformed-grant shape
checks, action-specific map/context/gate precedence and an unused private
parameter before final builds. Test review corrected a same-generation arm
mutation fixture and expanded the handed-off smoke/boundary coverage. The first
complete strict build then failed on an unused test variable; it was removed
without weakening warning flags. All subsequent executable runs passed.
The initial shell-wrapped H1 command produced configuration output only; direct
WSL commands were used to execute and verify every regression explicitly.

Command pattern (PowerShell passes each flags string as one native argument):

```text
wsl.exe -d Ubuntu-24.04 -- cmake -S src/lib/research_acceleration -B /var/tmp/research-c0b2-session.wU6gnxDb/debug -G Ninja -DCMAKE_BUILD_TYPE=Debug '-DCMAKE_CXX_FLAGS=-Wall -Wextra -Wpedantic -Werror'
wsl.exe -d Ubuntu-24.04 -- cmake --build /var/tmp/research-c0b2-session.wU6gnxDb/debug --parallel 2
wsl.exe -d Ubuntu-24.04 -- ctest --test-dir /var/tmp/research-c0b2-session.wU6gnxDb/debug --verbose
```

Release substitutes `release`/`Release` and CTest `--repeat until-fail:3`.
Sanitizer uses `sanitize`/Debug plus CXX flags
`-fsanitize=address,undefined -fno-omit-frame-pointer` and linker flags
`-fsanitize=address,undefined`. H1 uses its unchanged standalone project and
the `h1` directory. Python uses `python3 -B -m unittest discover` with
`Tools/research_sitl/wire`/`test_codec.py` and
`Tools/research_sitl/evidence`/`test_check_evidence.py` respectively.

### Tested identities and preservation

SHA-256 under `src/lib/research_acceleration/`:

| File | SHA-256 |
| --- | --- |
| `WireAdapter.hpp` | `18102ac8d2f66ed11511a5a26ef6b4175575be3642b0e9b9ef0c42cf10fc2ca9` |
| `WireAdapter.cpp` | `f9ce6353019cd2df2a997d557a29ca8789c7c1892647502f68fdaca468fcc7be` |
| `WireAdapterSessionTest.cpp` | `305a18e11a02cb47b3772093ed021a39c6526ae4202225bd848bbf5c498dc6ce` |
| `CMakeLists.txt` | `c42a996f461fcab5760c812e4439ee3d28c26857a934e410b6c541d621a9b61a` |

CTest logs remain at each configuration's `Testing/Temporary/LastTest.log`.
SHA-256: Debug `04e43af4540968a191e719008f739fe2e6f560a1a1b395817f31f8100cce69a7`;
Release `cc226793d76d647ca95144565822aae4de64aebea1b17a982fe8c7ea811b238d`;
sanitizer `3c472a38d04fd51bc5537c972da6ee5799fdc49aeb8c6fa4db8bb26f02558385`;
H1 `4d6a7484c25529310cbc53f940b310f3753cf4f8225efd70cabe3619638e56c5`.

Of 60 entry-hashed existing project artifacts, only the two assigned adapter
sources, standalone CMake and this gate changed; the other 56 remain byte-identical.
The session test and development timeline are new. All 570 geometry identities
remain unchanged, aggregate SHA-256
`4552c8dedcfa853d4cbb6d9e7a7d5707d7464a7909a861d3584bd96bccd45f16`.
Parent HEAD remains `e880f1cd79e2453ef3dfaf68ee1c067b98ea9a53` and separate
gz HEAD/gitlink remains `bb0b9cf974acf4f1bcb5f5fcf80b88841562dea9`, clean.
Stock Cessna, root firmware/startup, S1/time-map source, existing tests,
requirements, accepted design/oracle and roadmap are untouched. No commit,
push, PR, installation, simulator/firmware build, flash or physical test occurred.

### Exact next gate and project status

This pass permits **ACK/request-admission API and test-oracle review** next,
not automatic live integration. Freeze immutable acknowledgement binding,
post-transition raw/conservative ordering, one shared successful wire-operation
floor, C0b.1 mapping/S1 evaluation handoff, report capacity and full loss/reentry
cases before assigning that next implementation. Status encoding and live
MAVLink/Offboard integration remain separate subsequent gates.

No immediate physical measurement or human decision is required for that offline
review. Before live attachment, authority/authentication/identity/clock and
timing budgets must be approved. Plant-supported control still needs measured
mass properties, effectiveness, actuator response, launch/recovery envelope
and quantitative criteria. Normalized allocation and one-publisher ownership
remain obligations, not completed integration. Simulator truth remains scoring-
only. There is no end-to-end acceleration-to-actuator path or operational custom
flight mode yet. Roadmap Phase 2 and the overall definition of done are incomplete.

## Previous increment: C0b.2 session-administration design review (2026-09-18)

User requested resumption. This increment freezes the next geometry-independent
offline session API and its independent test oracle; it changes no source or
runtime behavior. No new human input is needed for this slice. Wire and evidence
workers owned separate new documents; Astra reconciled their contracts with
the checked-out S1/state-core implementation and owns this gate. The baseline
worker independently reviewed the design and oracle.

**Gate result: PASS for the bounded session-administration API/test design.**
This releases only the next isolated implementation assignment listed below;
no implementation or executable verification was performed in this increment.
New artifacts:
`C0B2_SESSION_ADMIN_DESIGN.md` and `C0B2_SESSION_ADMIN_TEST_SPEC.md`.
Parent adapter/state-contract banners identify their superseding session scope.
The closed state-core implementation evidence below remains historical evidence,
not a new test run in this design-only increment.

### Accepted boundary and remaining project limitations

The design retains one private S1 guard, one serialized administration owner,
and unchanged ordinary two-record results/closed getter. Session administration
has a separate three-record result and explicit packet/grant correlation. A
well-formed immediate trusted grant is consumed before packet/action rejection;
the wire-operation floor advances only on successful establish/revoke and
permits zero first. Structural faults and whole-call capacity are resolved
before exogenous mutation. Clear requires an actual S1 latch plus disarmed
state; it cannot reset an unlatched guard. No acknowledgement or acceleration
request is admitted, even after establishment.

Facts: CG-reference and rotating-body FRD hold are confirmed; source inspection
establishes S1 internal-poll and void-revoke behavior. Test identities, clocks
and boundary counters are synthetic, not physical measurements or deployed
settings. Inference: the reviewed contract supports a bounded offline software
implementation; it provides no evidence of vehicle feasibility or tracking.

The complete intended acceleration path remains unfinished: companion wire
ingress -> trusted admission -> force/attitude control -> stabilization ->
normalized allocation -> EDF/four servos -> measured vehicle response. This
gate covers only session administration at the admission boundary. Operational
inputs remain estimator/sensor-derived, never simulator ground truth. Later
controller/allocation gates still require one publisher per stage, explicit
normalized-output semantics and quantitative end-to-end tests.

### Review, verification and preservation

Master/independent review found and corrected design-oracle inconsistencies:
post-observation epoch fixtures, the required map-B transition, grant consumption
on rejected/unsupported packets, maximum-event behavior, latch flag versus
decision code, void-revoke reporting, worst-case capacity even on rejection,
and precise malformed-event/error precedence. These were documentation review
failures, not executed software-test failures. All five changed documentation
files pass whitespace checks. No compiler, executable test,
simulator or hardware was run this increment.
Final independent review returned PASS with no remaining design/oracle
contradiction or implementation-blocking ambiguity. This is design acceptance,
not proof of the unimplemented behavior.

The 58 entry-hashed pre-existing project artifacts are preserved except for
three intentional documentation updates: this gate, the parent C0b design and
the state contract. All 55 others remain byte-identical, including every
existing source/test, standalone CMake, requirements, roadmap and baseline.
All 570 geometry artifacts retain aggregate SHA-256
`4552c8dedcfa853d4cbb6d9e7a7d5707d7464a7909a861d3584bd96bccd45f16`.
Parent HEAD and separate gz HEAD/gitlink remain the identities recorded above;
gz remains clean and stock Cessna untouched. No install, commit, push, PR,
firmware/simulator build, flash, live transport or physical operation occurred.

### Exact next gate and human decisions

The next bounded implementation assignment is existing
`src/lib/research_acceleration/WireAdapter.hpp` and `WireAdapter.cpp`, new
independently authored `WireAdapterSessionTest.cpp`, and minimal standalone
`CMakeLists.txt` registration. S1/time-map sources and ordinary public behavior
must stay unchanged. Source and test ownership must be disjoint; Astra reviews
both and runs strict-warning Debug/Release, repeated Release and ASan/UBSan in
a fresh native-WSL host-test directory, plus state-core/S1/time-map, H1, codec
and evidence regressions. No simulator mount/cache is reused.

Implementation acceptance requires every administration oracle, closed-output
invariant, report/floor/counter boundary and ordinary regression to pass, followed
by independent review. That acceptance still does not release ACK/request
admission, status transport, live MAVLink/Offboard or controller integration.

No immediate clarification is needed. Before live attachment, the owner must
approve supervisor authority/event provenance, authentication/source/target and
key lifecycle, trusted clock provenance/timing bounds and status ownership.
Measured mass/CG/inertia, aerodynamic/control effectiveness, EDF/servo response,
flight envelope, launch/recovery feasibility and command-loss behavior remain
later physical/control requirements. Approximate weight/thrust are not approved
plant constants. Phase 2 and the roadmap definition of done remain incomplete.

## Previous increment: C0b.2 closed state-core implementation (2026-09-18)

User requested continued development and asked whether clarification was needed.
No new human input is needed for this previously approved offline slice. Existing
wire worker owns `WireAdapter.hpp/.cpp`; the evidence worker independently owns
`WireAdapterStateTest.cpp`; Astra owns standalone CMake, host verification,
post-handoff corrections and this gate. The baseline worker reviews independently.

**Gate result: PASS for the isolated closed state-core implementation.** The new
library is outside root firmware/startup and exposes no packet/session/ACK,
live transport, command admission or actuator output. Its getter remains closed
and preserves the caller's output. Existing S1 and time-map source are unchanged.

Host verification directory: `/var/tmp/research-c0b2-state.Odpbcssc`, freshly
allocated under native WSL, separate from simulator mounts/caches. Installed
Ubuntu-24.04/GCC 13.3.0, CMake 3.28.3, Ninja and AStyle 3.1 are used without
package installation. Strict-warning library-only compilation and the final
independent state tests/regressions passed as recorded below.

This work does not choose physical effectiveness, gains, acceleration limits,
operational timing or fallback behavior. CG/body-held FRD semantics remain
confirmed; physical measurements and live-deployment approvals remain later
requirements. No simulator or firmware build/run or physical operation is in scope.

### Verification and review

| Check | Result |
| --- | --- |
| Strict-warning C++17 Debug | 4/4 executables pass: 907 adapter checks, 231 guard, 165 frame, 191 mapping |
| Strict-warning Release | Same 4/4 executables, each repeated three times; all pass |
| Debug ASan/UBSan | Same 4/4 pass; no sanitizer diagnostics |
| Fresh H1 regression | 843 actuator-limiter checks pass |
| C0a / E1 Python regressions | 8 codec / 17 evidence methods pass, no skips |
| Scoped AStyle / whitespace | Three new C++ files unchanged by final formatting dry-run; whitespace checks pass |

Counts include setup/helper assertions, not independent flight scenarios or a
state-space proof. Tests cover STATE-001 through STATE-013: fixed report order,
one actual S1 poll per valid public call, malformed state/map and continuity,
clock high-water/latch behavior, immutable construction, closed accessor output,
counter/epoch extremes and repeated terminal snapshots. The test-only friend
seeds local counters and inspects guard address; it cannot populate an S1 request
or session. Source inspection confirms no alternate request cache or publisher.

Independent review found no actionable production issue. Initial test handoff
passed 222 checks; review identified coverage gaps, and Astra added the remaining
boundary cases before the final 907-check runs. No production-logic fix or failed
executable test was needed. Infrastructure notes: the worker's first native
add-file patch failed before editing, then direct patch succeeded; a root lookup
for `.astylerc` found no such file, then located and used `Tools/astyle/astylerc`.
Neither event changed unrelated content or weakened acceptance criteria.

Command pattern from the workspace through `wsl.exe -d Ubuntu-24.04 --`:

```text
cmake -S src/lib/research_acceleration -B /var/tmp/research-c0b2-state.Odpbcssc/debug -G Ninja -DCMAKE_BUILD_TYPE=Debug "-DCMAKE_CXX_FLAGS=-Wall -Wextra -Wpedantic -Werror"
cmake --build /var/tmp/research-c0b2-state.Odpbcssc/debug --parallel 2
ctest --test-dir /var/tmp/research-c0b2-state.Odpbcssc/debug --verbose
```

Release substitutes `release`/`Release` and adds `--repeat until-fail:3` to
CTest. Sanitizer substitutes `sanitize`/Debug, appends
`-fsanitize=address,undefined -fno-omit-frame-pointer` to CXX flags, and adds
`-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined`. H1 uses its unchanged
standalone directory and `h1-regression` with the Debug flags. Python commands
are `python3 -B -m unittest discover -s Tools/research_sitl/wire -p test_codec.py`
and the equivalent `evidence` / `test_check_evidence.py`. No packages installed.

### Tested identities and preservation

SHA-256 under `src/lib/research_acceleration/`:

| File | SHA-256 |
| --- | --- |
| `WireAdapter.hpp` | `3b94a19bb8389bab878ea087a29a365f835be9b186d028d4e884a45542356a41` |
| `WireAdapter.cpp` | `1838d5d8c59da5abb0a28d6ada98983efcddc360b4f687b38f820970c6577023` |
| `WireAdapterStateTest.cpp` | `f9a23e06d7cf1f94bc38d04b3f55f26db38b2f967e2d0f505c3c752cfe039d11` |
| `CMakeLists.txt` | `ebeddd5e94e8d0dca2cd85f5b6d949d6b46eec67d2657384932cfabe14183f8f` |

CTest logs remain under each configuration's `Testing/Temporary/LastTest.log`.
SHA-256: Debug `723d2c095234fcb8f43ffd594d7b154368efd49d3a725eddf27d396104c6673d`;
Release `9789240112989f5a299bd8d17307a0733fa96db3ebf1773f56de2c12a959aafb`;
sanitizer `9028d882358ce7c4b358569709886ca3b5adaa7a529545f9150acd8b05103568`;
H1 `80f07f455395bc2e326ee3e522915a89fa8baf4abdf6e14a78af44adb710964d`.
These are local software-development records, not qualified flight evidence.

Of 55 captured pre-existing project artifacts, only standalone S1 CMake and
three status/design documents (this gate, C0b adapter design and state contract)
are intentionally updated. The other 51 remain byte-identical, including all
existing source/tests, interface requirements, baseline/launchers and roadmap.
All 570 geometry-file identities remain unchanged (aggregate SHA-256
`4552c8dedcfa853d4cbb6d9e7a7d5707d7464a7909a861d3584bd96bccd45f16`).
No CAD content was interpreted. Parent HEAD remains
`e880f1cd79e2453ef3dfaf68ee1c067b98ea9a53`; separate gz HEAD/gitlink remains
`bb0b9cf974acf4f1bcb5f5fcf80b88841562dea9`, clean. Stock Cessna is untouched.
No commit, push, PR, install, root firmware/startup change, simulator/firmware
build/run, flash or physical test occurred.

### Exact next gate

This pass permits **C0b.2 session-administration API/test review**, not automatic
implementation. Freeze the independently trusted event stream, authenticated
packet/grant conjunction, retained session/revoked gates, clear-latch predicates,
administrative replay floors and multi-barrier report capacity before naming
that slice's source/test assignments. Packet ingress, ACK/request admission,
status transport and live MAVLink each remain later gates; no current public
method admits an acceleration request.

No user clarification or physical measurement is needed for that offline review.
Operational command timing/trust deployment, plant data, flight envelope,
performance limits and command-loss/recovery policy are still required before
their respective live/plant/control gates. Normalized allocation semantics,
the complete acceleration-to-actuator path and the roadmap definition of done
remain incomplete. Simulator ground truth has not entered operational inputs.

## Previous increment: C0b.2 state-core design correction gate (2026-09-18)

User requested continuation to the next gate. This increment resolves the prior
design findings without implementing code or advancing into live integration.
Astra owns the corrected `C0B2_STATE_CONTRACT.md`, cross-document release
wording and this disposition. The existing evidence worker owns only
`C0B2_TRANSITION_TEST_SPEC.md`; the baseline worker performs independent
read-only review. No worker modifies code, geometry or the separate gz repository.

**Gate result: PASS for the closed state-core design and named implementation
assignment only.** Independent read-only review found the API and fixture
contracts consistent with the existing S1 API and no remaining design blocker.
Astra inspected the revised documents and actual S1 poll/access/map source,
corrected the review's two minor clarifications (explicit epoch-exhaustion code
and guard-eligibility wording), and accepts the bounded assignment below.
This is not an implementation/test pass or completion of the full C0b.2 adapter.

Reviewed corrections:

- One private immutable configuration and one noncopyable/nonmovable guard;
  construction validates controller identity and fixed wire protocol as well as S1.
- Fixed capacity for two returned reports: barrier first when needed, operation
  second. An unchanged observation deliberately emits one operation report.
- All three state-core public operations run one private observation helper and
  exactly one S1 poll after structural/capacity checks. Standalone observation
  also checks validator time; closed access never calls a second polling accessor.
- Structural controller mismatch terminates at every generation; coherent
  source/session changes are ordinary barriers. Map shape/validity and generation
  checks now distinguish faults from unavailable but consistently reported state.
- Explicit normal-versus-terminal report capacity, checked epoch/generation
  boundaries, terminal snapshot retention and no partial transition on exhaustion.
- A test-only friend may inspect adapter state and seed local counter boundaries;
  there is no production reset/setter or duplicate guard. Later packet/admin/ACK
  tests are separated from the state-core assignment.

The state core remains permanently closed to command admission: no session,
request, ACK or supervisor input, no active cache, and no output command. This
is a software lifecycle foundation, not a controller or completed Phase 2.
Body-held FRD inertial acceleration at CG remains user-confirmed; measured CG
location, physical effectiveness and flight feasibility remain unknown here.

### Exact implementation boundary released

Only these paths are released for the next isolated implementation increment:

- New `src/lib/research_acceleration/WireAdapter.hpp` and `WireAdapter.cpp`.
- Independent `src/lib/research_acceleration/WireAdapterStateTest.cpp`.
- Minimal standalone `src/lib/research_acceleration/CMakeLists.txt` registration.

Required implementation evidence: STATE fixture assertions, strict-warning
Debug/Release and ASan/UBSan host tests, unchanged S1/C0b.1 regressions, immutable
ownership/source inspection, independent review and Astra synthesis. Use a new
native-WSL host-test directory, with no simulator mount/cache sharing. Existing
S1 APIs/source, root build/startup, live ingress and output paths stay untouched.

No human input is needed for this bounded slice. Deployment timing/authority,
physical model evidence, tracking limits and loss/recovery policy remain later
gates in `RESEARCH_DECISION_CHECKPOINT.md`. No numbers from fixtures become
physical or operational requirements. The complete acceleration-to-allocation-
to-actuator path remains absent; simulator truth remains scoring-only, and
normalized-output allocation semantics are not established by this adapter.

### Verification, preservation and next gate

This increment used document/source inspection and independent design review,
not builds or executable tests. The fixture table now specifies STATE-001 through
STATE-013, including actual S1 high-water behavior on validator-time regression.
Earlier packet/admin/ACK golden cases remain explicitly deferred. No physical
values or operational timing/authority limits were selected.

Entry hashing captured 55 pre-existing project artifacts. Only five assigned
documents changed: this gate, state contract, transition specification, parent
adapter design and decision checkpoint. The other 50 were byte-identical on
verification, including all existing standalone code/tests, baseline/setup,
launcher scripts and roadmap. The entire 570-file geometry manifest remained
identical (SHA-256 `4552c8dedcfa853d4cbb6d9e7a7d5707d7464a7909a861d3584bd96bccd45f16`).
No CAD content was interpreted or edited. Future implementation paths were
checked and are absent, with no output-path conflict.

Parent HEAD remains `e880f1cd79e2453ef3dfaf68ee1c067b98ea9a53`; separate gz
HEAD/gitlink remains `bb0b9cf974acf4f1bcb5f5fcf80b88841562dea9`, clean.
No source/build configuration, Cessna asset, package, live interface, command
publisher or output changed. No commit, push, PR, install, simulator/firmware
build/run, flash or physical operation occurred. The initial sandbox reader
failed before executing; an approved read-only retry succeeded.

**Exact next gate:** implement only the released state core and pass its
predeclared host tests, existing regressions, source ownership checks and
independent review. That implementation gate must pass before proposing session
administration or ingress APIs. Live acceleration admission, the validated plant,
stabilization/allocation, normalized output mapping and the roadmap definition
of done remain open. No new human input is needed for the released offline work.

## Previous increment: C0b.2 design and human-decision review (2026-09-17)

The user requested continued development and asked whether human input was
needed. During review the user explicitly confirmed that inertial acceleration
requests refer to the vehicle CG and retain their numerical components in the
rotating body FRD axes between updates, until superseded or expired. Requirements,
interface, wire contract and control architecture now record both as confirmed
facts. Actual CG position and IMU lever arms remain unmeasured here.

Artifacts: `C0B2_STATE_CONTRACT.md` (bounded state-core proposal),
`C0B2_TRANSITION_TEST_SPEC.md` (independent offline transition oracle), and
`RESEARCH_DECISION_CHECKPOINT.md` (current versus later human decisions).
Existing wire/evidence workers produced disjoint documents; Astra reviewed both
and requested independent read-only review. The older C0b adapter proposal is
explicitly superseded for C0b.2 ownership/generation design, not for C0b.1 results.

**Gate result: design progress only; C0b.2 implementation is not released.**
Private guard/configuration ownership, trusted-observation precedence, one
exogenous generation, paired packet/supervisor authority, immutable ACK ordering
and fail-closed counter policy are now explicit design directions. No active
controller, allocator, output, MAVLink receiver or complete acceleration path
has been implemented by this increment.

Remaining engineering corrections before the state-core implementation gate:

- Reconcile a single returned report with a call that requires both barrier and
  operation records; define retention/access and unchanged-observation results.
- Distinguish explicit S1 poll calls from the poll performed inside S1 eligible
  access; remove the conflicting exact-one-poll promise.
- Reconcile transition oracles for immutable controller-binding mismatches
  versus recoverable map changes, and zero supervisor IDs versus terminal faults.
- Make counter terminal-report precedence unambiguous throughout the contract,
  then review the bounded public test seam and assign exact implementation paths.

Independent read-only review confirmed all four API/oracle contradictions:
double polling, single-report return capacity, controller-binding classification
and zero-event behavior. It additionally requires unchanged-observation report
semantics and scoping later ACK/admin test fields out of the state-core slice.
These findings prevent implementation release; they are not executable failures.

These are engineering issues, not missing human decisions or physical inputs.
No further user clarification is required for their offline resolution.
Operational timing/trust deployment, feasible envelope, measured plant data,
performance criteria and physical loss/abort policy remain later approvals as
listed in the decision checkpoint. Fixtures cannot supply those values.

Verification in this increment is document/source inspection only; no code,
builds, executable tests, simulator, firmware or physical operations were run.
Earlier C0b.1/S1/H1/C0a/E1 results below are historical, not rerun evidence.
No geometry analysis, simulator-ground-truth controller input or new command
publisher was introduced. The stock Cessna baseline remains outside edit scope.

**Exact next gate:** reconcile the listed API/oracle issues and obtain Astra
acceptance of the state-core-only API, observable tests and explicit file scope.
Only that acceptance permits its isolated implementation. Session administration,
ingress, live deployment, plant/control integration and roadmap completion remain
separate unpassed gates.

## Previous increment: C0b.1 trusted time-map conversion (2026-09-17)

User requested continued development after the C0b.1 design release. Astra assigned only `TrustedTimeMap.hpp/.cpp` to the existing `wire_design_increment` (terra/high), wrote independent `TrustedTimeMapTest.cpp`, and added the minimal standalone S1 CMake registration. Existing `baseline_agent` performed independent read-only review. Astra owns this gate and the reconciled adapter design. No stateful wrapper, live receiver, controller or actuator integration was released.

**Gate result: C0b.1 accepted for offline arithmetic/conversion only.** The new pure function validates trusted map bindings and interval, fixed v1 protocol/enums, nonzero transition epoch, finite FRD components and raw timestamp ordering. It applies one frozen signed offset to both uint64 endpoints without overflow/underflow, including INT64_MIN, and preserves duration. It constructs an S1 canonical request only on success; failure leaves output unchanged. Map uncertainty is passed through, not treated as proof of admissibility. No map synchronization or provenance service is implemented.

Predeclared acceptance: independently specified signed-offset/extreme-value vectors, exact copied canonical fields, inclusive map endpoints, distinct local failure codes, byte-preserved failure outputs/inputs, signed-zero vectors, and tests using the unchanged S1 guard to demonstrate age/replay/expiry/uncertainty admission remains separate. `WireIdentityInvalid` is a local result for zero transition epoch, not a wire diagnostic allocation. Source/controller/map labels are not authentication.

### Verification

Installed WSL Ubuntu-24.04/GCC 13.3.0 host builds used a new directory `/var/tmp/research-c0b1.aqOxaBKM`, separate from all simulator mounts/caches. No packages were installed.

| Check | Final result |
| --- | --- |
| Strict-warning C++17 Debug | 3/3 CTest executables pass: 191 mapping checks, 231 unchanged guard checks, 165 unchanged frame checks |
| Release, same warnings | Same 3/3 executables, three repeats each, all pass |
| Debug ASan/UBSan | Same 3/3 pass; no sanitizer diagnostics |
| Fresh H1 host regression | 1/1 CTest; 843 checks pass |
| C0a payload regression | 8 methods pass, no skips |
| E1 evidence regression | 17 methods pass, no skips |

Counts include fixture/setup assertions and subcases; they are not flight scenarios or full state-space proofs. A mapping-success fixture is accepted by the unchanged S1 guard only with valid trusted context; the same guard rejects replay, expires the command at its conservative expiry and rejects excess uncertainty. This demonstrates that local conversion is not admission, not that the live acceleration path exists.

Command pattern, run from the workspace through `wsl.exe -d Ubuntu-24.04 --`:

```text
cmake -S src/lib/research_acceleration -B /var/tmp/research-c0b1.aqOxaBKM/debug -G Ninja -DCMAKE_BUILD_TYPE=Debug "-DCMAKE_CXX_FLAGS=-Wall -Wextra -Wpedantic -Werror"
cmake --build /var/tmp/research-c0b1.aqOxaBKM/debug --parallel 2
ctest --test-dir /var/tmp/research-c0b1.aqOxaBKM/debug --verbose
```

Release substitutes `release`/`Release` and adds CTest `--repeat until-fail:3`. Sanitizer substitutes `sanitize`/Debug, appends `-fsanitize=address,undefined -fno-omit-frame-pointer` to CXX flags, and supplies `-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined`. H1 uses `src/lib/research_actuator_command` and `h1-regression` with the Debug flags. Python regressions use `python3 -B -m unittest discover -s Tools/research_sitl/wire -p test_codec.py` and the equivalent evidence directory/`test_check_evidence.py`.

Independent review found no actionable correctness issue. Astra added public API comments documenting omitted C0a fields, trusted-map ownership, output preservation and the absence of admission. Scoped AStyle dry-run reports all three new C++ files unchanged. No production logic correction or failing runtime test was needed. Infrastructure failures: the sandbox patch helper failed; a native apply-patch approval timed out once and succeeded on its allowed retry. No change occurred from those failed calls.

### Identities and preservation

SHA-256 under `src/lib/research_acceleration/`:

| File | SHA-256 |
| --- | --- |
| `TrustedTimeMap.hpp` | `e2bd822f38f3c7df7ea038c040d811569293cf0ae06e9a6998852daf5de11202` |
| `TrustedTimeMap.cpp` | `3f8d0406294dd1ca79a68ceadeaab2d2f6df7c8e3bbacc231bb65ad832f82da4` |
| `TrustedTimeMapTest.cpp` | `1d94f49ae2b51964c18d3fef5a585606c6a86243ea517b89da8dafde5b2f8df0` |
| `CMakeLists.txt` | `029170d1538c410dd37cbb1b73f62d626955441310f42c928514fca9f40487d5` |

CTest logs are retained under each configuration's `Testing/Temporary/LastTest.log`. SHA-256: Debug `d3b042a445611f26ca83bbf0c7fe2332cf35218089d2a58fe5e6383fbb655b75`; Release `7673740c4f025a7943fa5c7174823e47bd58b041ff9bdb44f8e5f97469b334e3`; sanitizer `b817195647f19587ff5d5f3bcd06a6eec6766215412b7e5c54ec55e9e8c61c9d`. These remain local development records, not qualified flight evidence.

All 46 captured pre-existing non-geometry files outside the three intended edits (this gate, adapter design, standalone S1 CMake) remain byte-identical. Original S1 source/tests, H1, C0a, E1, baseline, launchers and roadmap were preserved. The geometry directory now contains 570 files, including CAD uploads. The initial bulk hash output was truncated: 159 complete geometry identities were recoverable and verified unchanged; no full-directory entry-hash claim is made. A truncation-spliced pseudo-path was excluded after a read-only comparison could not resolve it. No geometry file was opened for analysis, edited, moved or deleted by this increment.

Parent HEAD remains `e880f1cd79e2453ef3dfaf68ee1c067b98ea9a53`; separate gz HEAD/gitlink remains `bb0b9cf974acf4f1bcb5f5fcf80b88841562dea9`, clean. No stock Cessna resources, root build/startup, modules, parameters or output drivers changed. No commit, push, PR, install, simulator/firmware build/run, flash or physical test occurred.

### Exact next gate

C0b.1 success permits the **C0b.2 concrete state/ownership API review**, not automatic implementation of the provisional wrapper. That review must resolve guard/config construction consistency, trusted state generation and observer continuity, epoch/status ownership, authenticated ingress separation, frozen ACK age/future/order tests, supervisor de-duplication, counter exhaustion and complete transition/loss tests before naming implementation paths. C0b.3 status/cross-language conformance and C1 live ingress remain later gates. No physical measurements are needed for the C0b.2 review.

The uploaded CAD package can be inventoried and reviewed in a separate Phase 0 geometry increment; its contents have not yet established mass/CG/inertia, local flow, actuator signs/effectiveness or a feasible envelope. Required flight-loss policy, normalized allocation semantics and numerical performance limits remain unapproved. The request-to-control-to-allocation-to-actuator path is still incomplete; no simulator truth input or new command publisher was introduced. Full controller/plant/output gates and the roadmap definition of done remain open.

## Previous increment: C0b design and H1 EDF/four-servo foundation (2026-09-17)

User authorization: continue to the next gate and start developing flight-controller code for the EDF and four independent servos. Astra reviewed the existing H architecture before implementation and released only the geometry-independent H1 command-domain math contract in [EDF/canard foundation](EDF_CANARD_CONTROL_FOUNDATION.md). This explicitly narrows the implementation: no PID/gains, wrench scaling, effectiveness model, allocation solver, attitude resolver or active outputs are released without plant evidence. Independent preliminary review rejected arbitrary physical-wrench normalization because it could obscure coupled/unavailable authority.

Disjoint work: existing `wire_design_increment` (terra/high) authored the new [C0b design](ACCELERATION_ADAPTER_C0B_DESIGN.md); existing `evidence_checker_increment` (terra/high) implemented the new H1 header/source/standalone CMake only after master API approval; Astra independently wrote the tests, owned the H1 contract, inspected all artifacts and performed verification/corrections; existing `baseline_agent` performed read-only preliminary and final K review. All 43 existing project artifacts were captured at entry; only this gate document was authorized to change among them.

### Gate outcomes

**H1 accepted for inactive software math only.** `src/lib/research_actuator_command/` contains exactly `CommandLimiter.hpp`, `CommandLimiter.cpp`, `CommandLimiterTest.cpp` and `CMakeLists.txt`. Logical channel 0 is EDF [0,1]; channels 1..4 are independent normalized servos [-1,1]. Caller-required bounds, asymmetric rise/fall rates and maximum dt are validated. Amplitude and slew masks are distinct. Every failure leaves output and report unchanged. No command value is designated a safe fallback, and normalized values have no inferred PWM, angle, thrust or geometry mapping.

**C0b.1 design accepted as the next bounded implementation input.** It specifies checked signed-offset time mapping, map identity/interval validation, explicit v1-to-S1 field conversion and failure-output preservation. No adapter code was implemented in this increment. C0b.2 serialized session/epoch/ACK orchestration and C0b.3 status/cross-language evidence remain provisional, with concrete open review items in the design disposition. The generated dialect and live C1 gate remain closed.

### Verification and failures

Observed installed host: WSL Ubuntu-24.04, GCC 13.3.0, C++17, CMake/Ninja, existing AStyle. No packages installed. A unique native build directory was allocated: `/var/tmp/research-h1.fcnNTLBk`; no simulator mount/cache was used. All final H1 tests execute 843 checks including setup/subcases, not independent flight scenarios.

| Configuration | Result |
| --- | --- |
| H1 Debug, strict warnings as errors | 1/1 CTest; 843 checks, zero failures |
| H1 Release, same warnings | 1/1 CTest, repeated three times; all pass |
| H1 Debug ASan/UBSan | 1/1 CTest; no sanitizer diagnostics |
| Fresh unchanged S1 Debug regression | 2/2 CTest; 231 guard + 165 frame checks pass |
| C0a Python payload regression | 8 methods pass, no skips |
| E1 Python evidence regression | 17 methods pass, no skips |

Exact host command pattern (from workspace through `wsl.exe -d Ubuntu-24.04 --`):

```text
cmake -S src/lib/research_actuator_command -B /var/tmp/research-h1.fcnNTLBk/debug -G Ninja -DCMAKE_BUILD_TYPE=Debug "-DCMAKE_CXX_FLAGS=-Wall -Wextra -Wpedantic -Werror"
cmake --build /var/tmp/research-h1.fcnNTLBk/debug --parallel 2
ctest --test-dir /var/tmp/research-h1.fcnNTLBk/debug --verbose
```

Release substitutes `release`/`Release` and adds `--repeat until-fail:3` to CTest. Sanitizer substitutes `sanitize`/Debug, appends `-fsanitize=address,undefined -fno-omit-frame-pointer` to CXX flags and supplies `-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined`. S1 uses `src/lib/research_acceleration` and `s1-regression` with the Debug flags. Python regressions use `python3 -B -m unittest discover -s Tools/research_sitl/wire -p test_codec.py` and the equivalent evidence directory/`test_check_evidence.py`.

Failure retained: initial 837 checks passed, but Astra added an adversarial endpoint-rounding regression which reproduced a one-float-step slew violation (839 checks, one failure). Wide arithmetic alone did not suffice: a double endpoint addition could round outward to a float boundary. The correction uses a TwoSum residual to round the endpoint conservatively before float conversion. Upper, lower and opposite-sign subnormal regressions now pass; final independent review cleared the correction. Verified assumptions are IEEE binary32/binary64 round-to-nearest with gradual underflow and no fast-math/reassociation; future firmware must verify them. CTest registration omitted in the worker draft was added before execution. A sandbox patch-helper failure and a formatting-context patch mismatch made no changes and were resolved with scoped apply-patch retries.

AStyle dry-run reports all three C++ files unchanged. Release archive undefined symbols are only `nextafter`, `nextafterf` and stack protection; source review finds no runtime PX4, MAVLink, Gazebo, time, parameter, allocator or output dependency. Host tests are not Pixhawk compilation, scheduling or performance evidence.

### Final source identities and preservation

SHA-256 for the tested H1 files:

| File | SHA-256 |
| --- | --- |
| `CMakeLists.txt` | `d09514b153d09e37f614bb80047be3f77c73ea753ed471c17b3dcf980a19f90c` |
| `CommandLimiter.cpp` | `480ef70b944c19def8b6a8ea46e14ef399b2cf7e0f7852c566419b9ff99503b8` |
| `CommandLimiter.hpp` | `558e627962b3c298a8315a2d1ac71ad74a19d47a28748f84123b2173d4e4c000` |
| `CommandLimiterTest.cpp` | `f416a239da53e0fd0d25933dff4d6f8ef42a8a71672f0cd55a75a77f2511881b` |

CTest logs remain under each configuration's `Testing/Temporary/LastTest.log`. Debug hash `b4f433c53b79547a53849f5c521dd91b98b7d3288807e59bdf0f918b15f8fd0f`; Release `98984bc0081ec5ee03422c33a3897244c95a9272c8f6d820208bd165c2273cb5`; sanitizer `439027d90c506fdeae4c3bd24bd275939f58bdc6b17c5eeec8df8cf71a932c40`. These are local development records, not a qualified flight-evidence package.

All 42 captured existing files outside this gate remain byte-identical, including S1, C0a, E1, wire/architecture contracts, roadmap, baseline and launchers. Parent HEAD remains `e880f1cd79e2453ef3dfaf68ee1c067b98ea9a53`; separate gz HEAD/gitlink remains `bb0b9cf974acf4f1bcb5f5fcf80b88841562dea9`, clean. No stock baseline resource changed. No root build/startup/module integration, install, staging, commit, push, PR, simulator/firmware build/run, flash or physical test occurred.

### Exact next gate and unresolved work

Next software assignment may implement only C0b.1 `TrustedTimeMap.hpp/.cpp`, `TrustedTimeMapTest.cpp` and its scoped standalone S1 CMake registration, with independent tests/review and S1 regression. C0b.1 success would establish mapping arithmetic only; C0b.2 still needs its own concrete state/ownership review. No new user measurements are required for that work.

The complete acceleration path is still absent: codec, guard, map design and H1 limiter are isolated components, not a linked acceleration-to-actuator controller. No operational controller input, truth path or command publisher was added. Physical-to-normalized wrench semantics and coupled residual/anti-windup feedback remain required before allocator integration; H1 masks are not wrench residuals or measured saturation.

Full flight-law Gate D still needs measured mass/CG/inertia, geometry/signs/local flow, installed propulsion/servo dynamics, feasible authority/envelope, residual-attitude policy, quantitative requirements and approved loss behavior. Output Gate E additionally needs single-publisher startup/transition proof and estimator-in-the-loop tracking/allocation/output tests. User mass/thrust values remain estimates. Static launch, net capture, flight tracking, board portability and the roadmap definition of done remain unvalidated.

## Previous increment: C0a stateless offline payload codec (2026-09-17)

User continued after E1. Astra freezes the C0a subset in [the wire contract](ACCELERATION_WIRE_CONTRACT.md): protocol value, payload lengths/offsets, all adapter diagnostic values, strict primitive rules, status consistency and the future trusted-map/state boundary. Only stateless payload serialization is released now; the broader C0b S1 adapter, generated dialect and live C1 attachment remain closed.

Disjoint ownership:

- Existing `wire_design_increment` (`gpt-5.6-terra`, high), narrowed B/G: new `Tools/research_sitl/wire/codec.py` and `README.md`.
- Existing `evidence_checker_increment` (`gpt-5.6-terra`, high), narrowed E: new `Tools/research_sitl/wire/test_codec.py`, independently table-derived expected bytes, not codec-derived expectations.
- Existing `baseline_agent`: independent read-only K review; Astra owns contract/gate edits, final inspection, reruns and synthesis.

Predeclared acceptance: six encode/decode functions match all three payload tables and independent golden vectors; all allowed zero-tail lengths round-trip; invalid primitive width/type/bool, binary32 non-finite/overflow, version, enum, reserved/extra/missing fields and inconsistent status claims reject with `CodecError`; signed-zero/binary32 quantization semantics are explicit; caller dictionaries remain unchanged. No live I/O, protocol framing/IDs/CRC/signing, command admission, clock conversion or replacement S1 state machine is implemented. Existing 17-method E1 regression remains passing; source/byte hashes and separate gz state are retained.

Initial new wire directory is absent. Captured all existing top-level docs, seven S1 files, three launcher scripts and four E1 files. Only the wire contract and this gate may change among pre-existing files. No installation, simulator/firmware build/run, commit, push, PR, flash or physical test is authorised by this slice. Shared WSL simulator mounts/caches are not used. Findings and results below will govern the next gate; no physical measurement or performance claim can follow from codec tests.

### Final gate result: offline payload slice C0a

**Accepted:** C0a passes its stateless offline payload gate after disjoint implementation/test work, independent read-only K review and Astra inspection/reruns. This is not completion of the broader C0 adapter harness, Phase 2, generated MAVLink interoperability or the acceleration-to-actuator path.

Completed artifacts: [codec usage](../Tools/research_sitl/wire/README.md), six encode/decode functions in `codec.py`, independent table-derived golden vectors and conformance tests in `test_codec.py`, and the frozen C0a API/status rules in [the wire contract](ACCELERATION_WIRE_CONTRACT.md). Request/session/status nominal sizes are 90/76/106 bytes; permitted minima are 85/71/106. These are tested payload layouts, not complete MAVLink frames.

Observed verification on installed WSL Ubuntu-24.04 / Python 3.12.3:

```text
wsl.exe -d Ubuntu-24.04 -- python3 -B -m unittest discover -s Tools/research_sitl/wire -p test_codec.py -v
Ran 8 tests -- OK
wsl.exe -d Ubuntu-24.04 -- python3 -B -m unittest discover -s Tools/research_sitl/evidence -p test_check_evidence.py -v
Ran 17 tests -- OK
```

All methods passed without skips. Codec subcases cover independent bytes, every permitted trimmed length, field/type/width/bool rejection, version/enums/reserved data, binary32 finite/overflow/subnormal/rounding/signed-zero behavior, status coherence and caller-dictionary preservation. Astra additionally checked every single-bit mutation of the three full golden payloads: 2,176 mutations, 1,806 structurally valid with byte-exact decode/re-encode, and 370 rejected with `CodecError`; no unexpected exception. All candidate lengths from zero through nominal-plus-one were checked. This is a parser consistency probe, not proof of authentication, admissibility or physical correctness. Python AST syntax checks passed without generating bytecode.

Review/failure history: a draft test incorrectly treated zero age with the age-valid flag as invalid; corrected before the completed-code run. Status zero-tail wording was clarified: the final state enum is nonzero, so a valid status is always 106 bytes. An early test collection attempted while the status functions were still being written raised ImportError; final completed-source runs pass. Oversized Windows patch invocations failed before writing and were split into smaller patches. Independent K final review reported no actionable finding. No operational values or physical measurements were invented.

Final SHA-256 identities:

| Artifact | SHA-256 |
| --- | --- |
| `wire/codec.py` | `31fa0e71ec44453c80c549e84da9faed5f9181fdb6ec581c884a05fc3a4ed5fd` |
| `wire/test_codec.py` | `c12183c4320ca936d25ffc2d4ed5089e7dfc24190755cd91363f3186cb71d435` |
| `wire/README.md` | `3cfc22f05bde3ae856e211a8754c4b589331944aa44b8cc05c06ed37a51f4df9` |
| `ACCELERATION_WIRE_CONTRACT.md` | `1281105e59d8e3dd59c4f2e3ce639abd76b2b664be51fad76fac5d24ca519ccf` |

Preservation: all 38 captured pre-existing files outside the two explicitly assigned master documents remain byte-identical, including S1, E1, diagrams, roadmap and launchers. Only three new wire files exist; no cache artifacts remain there. The separate Gazebo repository remains clean at `bb0b9cf974acf4f1bcb5f5fcf80b88841562dea9`. Stock Cessna resources and all existing user changes were preserved. No package installation, firmware/simulator build/run, staging, commit, push, PR, flash or physical operation occurred.

**Still incomplete:** generated dialect/framing/signing interoperability; trusted clock conversion; source/session/supervisor and epoch integration; live ingress; full request-to-control-to-normalized-allocation-to-output evidence; baseline reproduction; physical model/envelope and operational loss policy. The codec reports only structural consistency, even when a status claims eligibility. No controller inputs, simulator-truth inputs, command-stage publishers or allocator output semantics were changed.

**Exact next gate:** C0a completion permits C0b design/API review. Before C0b implementation, Astra must approve concrete C++ wrapper signatures, serialized transition/session ownership, checked clock-map conversion, explicit wire-to-S1 mapping, test cases and named source/test paths. The next isolated harness must invoke existing S1 `RequestGuard`, not duplicate it in Python, and must remain outside live PX4 startup/output paths. Generated-dialect work requires its own reviewed allocation and conformance evidence; no message ID is guessed. C1 live ingress separately requires the deployment approvals in the wire contract. Plant F and active controller I remain closed pending physical evidence and full H review.

**User inputs now:** none needed for the next offline C0b design increment. Geometry, mass/CG/inertia, local flow, installed propulsion/servo behavior, numerical performance requirements, deployed trust/clock policy and approved flight-loss response remain unresolved for later gates. User mass/thrust figures remain estimates; successful software tests do not validate static launch or net capture.

## Previous increment: wire design and offline evidence tooling (2026-09-17)

User authorization: "Continue the development process" after the proposed geometry-independent next steps. This increment releases two disjoint assignments, not active flight integration:

- Wire design, narrowed B/G: `docs/ACCELERATION_WIRE_CONTRACT.md` only, `gpt-5.6-terra` / high. Proposed field types, source/session trust, conservative clock mapping, transition epochs and conformance cases. No dialect, receiver or publisher implementation.
- Evidence tooling, narrowed E: `Tools/research_sitl/evidence/README.md`, `manifest.schema.json`, `check_evidence.py` and `test_check_evidence.py` only, `gpt-5.6-terra` / high. Standard-library offline validation for a first `software_contract` profile; no simulator runner or flight-metric scorer.
- Astra owns this gate document and final verification/synthesis. Independent read-only review follows worker handoff. The previously used reviewer could not be newly spawned because of the thread limit; the existing baseline agent is assigned the bounded evidence review, with Astra reviewing both deliverables.

Predeclared acceptance: strict versioned input/schema consistency; nonempty registry-declared required cases; hashed source/registry/input/output attribution; fixture-only expectations and explicit applicability exemptions; recomputed assertion outcomes; missing, corrupt, non-finite, duplicate or path-escaping evidence cannot pass; unsupported SITL/hardware profiles cannot qualify; unresolved operational requirements are incomplete. Tests must distinguish `fail`, `incomplete` and `not_run`, including failure precedence. Schema/checker hashes do not establish authenticity or independently prove when a registry was approved.

Wire acceptance requires compatibility with S1 APIs, explicit FRD inertial/CG/current-body-held conventions, no use of untrusted source-supplied clock mappings, bounded uncertainty over original creation-to-expiry, no replay/re-entry resurrection, one owner, status meaning distinct from achieved tracking, and an explicit implementation gate. Operational timing, flight response, physics and normalized output integration remain unresolved and cannot be filled with fixture values.

Initial output paths were absent; the parent/submodule revisions and all pre-existing top-level documentation, S1 files and SITL script hashes were captured before changes. Only this gate may modify an existing file. Scoped code-review and CI/build guidance applies to the new `Tools` files; no nested instructions were found along the assigned paths.

At assignment: design/tooling verification pending. No baseline rerun, simulator/firmware build, package installation, active Offboard control, actuator output, commit, push, PR, flashing or physical test is part of this increment. The exact next software gate is Astra acceptance of the reviewed wire design and passing offline checker tests; any subsequent implementation receives separate named file ownership. Physical plant/control gates remain closed.

### Final gate result: offline evidence slice E1

**Accepted:** the four-file software-contract checker slice passes its bounded gate after worker implementation, independent read-only review and Astra corrections. The wire contract is accepted as a preliminary offline design input only. This is not acceptance of the full Phase 1/2 evidence/interface gates, a generated MAVLink dialect, an active adapter or a flight-development phase.

Completed artifacts:
- [Wire design](ACCELERATION_WIRE_CONTRACT.md): proposed MAVLink 2 raw-producer-time request/session/status layouts, explicit wire-to-S1 enums, PX4-owned time mapping, trusted session/epoch barriers, diagnostic semantics and offline/live gate separation. Nominal payload sizes are design arithmetic (90/76/106 bytes), not measured/generated-code interoperability.
- [Checker and usage](../Tools/research_sitl/evidence/README.md), structural JSON schema, stdlib Python validator/scorer and regression tests: only exact-JSON software-contract evidence. The externally retained registry digest pins case criteria and immutable input/expected artifact hashes. It cannot authenticate execution, historic approval, source authorship or flight capability.

Verification on installed WSL Ubuntu-24.04 / Python 3.12.3:

```text
wsl.exe -d Ubuntu-24.04 -- python3 -B -m unittest discover -s Tools/research_sitl/evidence -p test_check_evidence.py -v
Ran 17 tests
OK
```

All 17 methods passed without skips, including installed optional JSON Schema interoperability, CLI result/usage codes, strict types/JSON, pinned expectation rewrites, missing/corrupt/truncated evidence, unapproved criteria, required-case coverage, path escape, internal symlink policy and hard-link role alias rejection. These are methods with subcases, not 17 flight scenarios. Runtime has no third-party dependency; no package was installed. Astra also ran 77 manifest-type and 77 registry-type mutation probes: no uncaught exception; the only passing registry mutation was the already-valid empty optional requirements list. Source syntax and whitespace were checked. Bytecode generated by the worker syntax check was removed; only the four assigned tooling source/document files remain.

Review/failure history: an initial test incorrectly expected a returned failure report where the internal API raises `EvidenceError`; corrected. Review corrected an unsatisfiable draft nested schema, boolean/numeric equality, overflow-to-infinity parsing, unresolved-criterion failure masking, missing-pin early return, unsafe nested type access, missing raw-input validation and unpinned expected/input bytes. Final Astra hardening added parser-complexity handling, validation of declared artifacts even for unexecuted cases, manifest confinement, same-file role checks and durable negative tests. The original independent-review findings and final deltas were re-reviewed; no actionable finding remains for this limited slice. Internal symlinks confined to the package remain deliberately allowed. Concurrent malicious filesystem mutation and cross-package historical run-ID uniqueness remain outside this tool's guarantee.

Final SHA-256 identities (gate-document edits do not alter these artifacts):

| Artifact | SHA-256 |
| --- | --- |
| `ACCELERATION_WIRE_CONTRACT.md` | `448169045c9f9806bd966524937b5f1eb509034706a973d891ef090a5f781271` |
| `check_evidence.py` | `599a3b65f6c149300d5472302c1ce46ce102a41b7cff4aaa487977a9cb68d742` |
| `test_check_evidence.py` | `0df6adc10a7e2a718df9a37ac65c6377de57f1b59055676a2598700d70abcc78` |
| `manifest.schema.json` | `bd2346239735eb2e77d2e19e5280c794cc71d958a52a8e8f68505d05f15ae4c7` |
| `README.md` | `41e3cf23497eca0997b552ef3a95d682e0670e1ccd1980f93ad9e8f782ef36f9` |

Preservation: 34 pre-existing captured files outside this gate remain byte-identical, including all S1 source, existing architecture diagrams, roadmap, test specification, baseline/setup documents and launchers. Parent HEAD stays `e880f1cd79e2453ef3dfaf68ee1c067b98ea9a53`; separate Gazebo HEAD/gitlink stays `bb0b9cf974acf4f1bcb5f5fcf80b88841562dea9`, clean. No existing file except this gate was changed. No staging, commit, push, PR, install, firmware/simulator build/run, flash or physical test occurred.

**Still incomplete:** baseline runtime reproduction; full manifest/runner/ULog and flight-metric support; live clock/authentication/session integration; generated dialect conformance; entire request-to-control-to-normalized-allocation-to-actuator path; physical model/envelope; board and experimental validation. Neither this checker nor a request acceptance result verifies achieved acceleration. No direct simulator truth input or competing actuator publisher was introduced because no live path was implemented.

**Exact next gate:** C0 may proceed only after Astra freezes the remaining adapter-only decision values and injected trusted-map/state API in the wire contract and assigns concrete codec/test paths. Recommended next bounded assignment is `gpt-5.6-terra` / high, B/G narrowed to offline payload codec/conformance tests under proposed `Tools/research_sitl/wire/{README.md,codec.py,test_codec.py}`, with Astra-owned contract updates and independent K review. These proposed files are not created or released by this handoff; no dialect ID is guessed and no network/serial publisher is permitted. A later canonical adapter harness must reuse S1 rather than implement a divergent validator. C1 live ingress separately requires reviewed authentication, source/supervisor binding, clock-map evidence and transition integration. Plant F and active controller I remain closed pending physical evidence/full H review.

**User inputs now:** none needed for the next pure-software design/codec increment. Deployment authority, operational timing/loss policy, quantitative requirements and measured geometry/mass/CG/inertia/local flow/installed actuator response remain later human-owned decisions. An SDF/CAD/parts package remains useful provenance, not proof of dynamics.

## Previous isolated software release: S1 (2026-09-17)

Wave 2 planning gate accepted by Astra: Prompt E evidence/test specification is reviewed; model ownership is assigned above. This approves planning and pure software work only, not the physical plant or integrated runs.

Preliminary H has been reviewed against the pinned fixed-wing, allocator and matrix sources. Astra approves ONLY the geometry-independent S1 request guard and frame mathematics, with the following binding API corrections: polling/access must recheck the complete context and current monotonic time; validator-clock regression is a fixed fault; configuration cannot reset replay/latch state; all math returns explicit failure status. Foreign/malformed requests never refresh or replace a valid owner request. Context loss revokes it without resurrection. Identity/session metadata is not authentication.

Exclusive implementation assignment (Prompt I narrowed to S1, gpt-5.6-terra/high): `src/lib/research_acceleration/CMakeLists.txt`, `RequestGuard.hpp`, `RequestGuard.cpp`, `FrameMath.hpp`, `FrameMath.cpp`, `RequestGuardTest.cpp`, `FrameMathTest.cpp`. No root build wiring, flight module, uORB/MAVLink changes, board/airframe/startup configuration, parameters, model, gains or actuator outputs. Existing matrix headers are reused. Bounds/times are mandatory caller configuration; tests use explicitly non-operational fixtures.

Verification authorized: scoped formatting and standalone C++17 host configure/build/CTest in a newly allocated WSL temporary build directory, followed by read-only Prompt K independent review and Astra inspection. No PX4 firmware/simulator build or shared SITL cache use. Gate B requires passing boundary/invalid-input/time/session/re-entry/frame/gravity tests and independent review; evidence gaps remain incomplete, never presumed passed.

No physical inputs are requested for S1. Future plant/control release still needs measured mass/CG/inertia, geometry/local flow, propulsion/servo response, feasible envelope and approved flight-loss response. Full H, active Offboard acceleration tracking and the roadmap definition of done remain incomplete.

### Current gate result and next assignments

Completed: Wave 2 planning artifacts/model ownership, preliminary H software-boundary review, bounded S1 implementation, independent K review and Astra synthesis. **S1 standalone Gate B passes for the inactive library/test scope.** This does not complete a roadmap flight-development phase.

Verified final source: 231 request/state assertions and 165 frame-math assertions; two CTest executables passed strict warning-as-error Debug, Release (three repeated runs each), and ASan/UBSan builds. Assertions include fixture/setup checks, not independent flight scenarios. Source hashes, commands, retained logs, review fixes and failure history are in [S1 verification](RESEARCH_ACCELERATION_S1_VERIFICATION.md). The header-only matrix requires an existing minimal platform definitions header; no ROS middleware or PX4 runtime is linked. All code remains outside the root firmware build/startup configuration.

| Current roadmap phase | Result | Remaining release evidence |
| --- | --- | --- |
| 0: requirements/feasibility | Incomplete | Four independent servos and sensor/transport/launch intent are now known; geometry, mass/CG/inertia, local flow, actuator response, feasible envelope and numerical requirements are not measured/approved. |
| 1: baseline/evidence | Incomplete | Stock baseline is preserved but not rerun; attributable GUI/headless, freshness, repeatability and second-operator tests plus machine-readable evidence tooling remain. |
| 2: acceleration interface | Incomplete | FRD inertial semantics/MAVLink selected; standalone validation/math tested. Wire format, trusted clock/source adapter, supervisor/status integration, operational limits/loss policy and full request-to-actuator path remain unimplemented. |
| 3 onward: plant, active controller, integration, board/hardware | Not released | No physics, gain/effectiveness, acceleration tracking, actuator output, board or physical result is implied by S1. |

Failures corrected in this increment: transitive matrix header dependency; a mistaken infeasible test fixture; incomplete transition/diagnostic coverage; administrative rejection counting. The initial volatile WSL /tmp build disappeared; final builds/logs are in unique `/var/tmp/research-acceleration-s1.b3JJ9r0v/`. No shared simulator build/cache or launcher was changed. Independent final review reports no remaining correctness finding for S1. Schema/checker, clock synchronization/authentication, operational fallback and flight performance remain explicit limitations, not hidden passes.

| Next candidate | Agent/model/prompt | Reserved ownership and exact gate |
| --- | --- | --- |
| Inactive MAVLink wire/adapter design | `gpt-5.6-terra`, high, B/G narrowed to design | `docs/ACCELERATION_WIRE_CONTRACT.md` only. S1 acceptance permits this design assignment; versioned fields, source/session trust, producer-clock mapping, body-held semantics, transition epochs and SITL/Pi conformance must receive Astra approval before any dialect/receiver/publisher implementation. Not launched in this increment. |
| Evidence schema/checker | `gpt-5.6-terra`, high, E | Proposed isolated `Tools/research_sitl/evidence/` paths; master must assign concrete schema/checker/test files and fixture acceptance first. No simulator runner or existing launcher edits. |
| Custom plant F / active controller I | `gpt-5.6-terra`, high | Still blocked by approved physical/effectiveness data and full H review; no paths released. |

No additional user input is needed for the next geometry-independent design work. A parts list or SDF/CAD package can later establish provenance/configuration, but cannot alone establish installed thrust, servo dynamics, inertia accuracy or low-speed control authority. Physical values and operational decisions remain with the human team. No powered test is requested.

Preservation: final hashes match all eight original setup/launcher/planning files and the unchanged baseline report. Parent HEAD/index were not changed or staged; the separate gz repository and its gitlink remain unchanged/clean. Source additions are exactly seven isolated library/build/test files, plus current documentation; no root build wiring, receiver, controller, allocator, startup, model or board file changed. No commit, push, PR, install, firmware/simulator run, flash or physical operation occurred.

## Historical Wave 1 gate checklist (superseded where noted above)

Wave 1 documentation review is complete. The artifacts are accepted as planning inputs, not as approval of a physical envelope, operational interface or runtime baseline. No roadmap phase passes yet.

| Roadmap phase | Status | Evidence needed to pass |
| --- | --- | --- |
| 0: requirements and feasibility | Incomplete | Missing geometry/independent actuator topology, mass/CG/inertia, aero/EDF/servo evidence, initial joint envelope, launch/recovery and numerical requirements. Neither physical feasibility nor a demonstrated physical-design blocker is established. |
| 1: stock baseline and logging | Incomplete | Installed environment and prior cache inventoried; GUI/headless, freshness, shutdown/repeatability and second-operator reproduction remain unexecuted. Existing binary/six ULogs lack run-attributed manifests. No executable evidence validator exists. |
| 2: acceleration interface | Incomplete | Source audited through reception, Commander, fixed-wing conversion/control, normalized allocation and Gazebo outputs. Research semantics, transport/dependencies, freshness/status/ownership/fallback remain unapproved; no executable frame/time/stale tests or full-vector research consumer exist. |

Master review criteria applied to all three artifacts:

- Confirm facts, measurements, assumptions and inferences are distinguishable.
- Resolve NED/ENU, FRD/FLU, gravity, CG/IMU reference-point and time semantics.
- Distinguish message acceptance, offboard availability, per-command freshness,
  control support, allocation and resulting actuator outputs.
- Require one owner/publisher per command stage and normalized body-axis
  thrust/torque semantics, including explicit scaling for any physical units.
- Keep simulator truth available only to independent evaluation/diagnostics;
  identify hardware-available estimator/sensor inputs for operational control.
- Check that unknown numeric requirements and physical values stay unknown.
- Keep stock `gz_rc_cessna` and the separate model repository unchanged.
- Verify ownership, diffs, initial-file hashes and model gitlink at handoff.

## Historical Wave 1 decisions and next-wave boundary

Accepted artifacts:

- [Requirements and missing-data register](RESEARCH_VEHICLE_REQUIREMENTS.md), Prompt A, `gpt-5.6-sol` / high. Confirmed configuration, named unresolved requirements, proposed human-owner roles, conditional feasibility checks and no invented physical values. Human roles still need named owners.
- [Acceleration interface/source trace](ACCELERATION_INTERFACE.md), Prompt B, `gpt-5.6-terra` / high, corrected by Astra after review. Accepted as a source audit and contract proposal, not an approved interface. MAVLink is recommended only for initial plumbing; final transport and any extension remain undecided.
- [Stock baseline inventory and procedures](SITL_BASELINE_REPRODUCIBILITY.md), Prompt C, `gpt-5.6-luna` / high, corrected by Astra after review. Accepted inventory and conceptual manifest/checking requirements; procedures are unexecuted and schema/checker implementation belongs to Wave 2 or a later explicit assignment.

Evidence-backed conclusions:

1. **Source fact:** MAVLink and DDS can publish acceleration fields, but stock fixed-wing conversion requires position or horizontal velocity for validity. It removes along-velocity acceleration from its curvature calculation and has no vertical-acceleration tracking consumer. **Further source fact:** velocity-plus-acceleration alone does not activate AUTO_PATH; its position flag is disabled. Position-plus-velocity permits the path's curvature feed-forward. These are not full-vector tracking (`FixedWingModeManager.cpp:387-399,1060-1091,2189-2255`).
2. **Source fact:** MAVLink stamps receipt time; DDS converts timestamps through the XRCE offset and caps them at receipt time, substituting receipt time for zero. Offboard availability checks mode-message age, not independent payload age/sequence (`mavlink_receiver.cpp:1301-1312`; `dds_topics.h.em:225-240`; `Tools/msg/templates/ucdr/msg.h.em:156-170`; `offboardCheck.cpp:47-54`). **Inference:** research freshness cannot be proved by a live heartbeat or a freshly republished old request.
3. **Source fact:** thrust and torque are normalized body-axis commands. Allocation is triggered by torque publications and uses cached thrust; output functions feed the Gazebo servo-radian and motor-speed topic mappings. **Design requirement:** any physical-force/moment control calculation needs an explicit compatible normalization and coordinated publication contract.
4. **Source fact:** stock Cessna enables `sensor_airspeed_sim`; its scheduled 8 Hz calculation uses truth velocity/altitude, estimated attitude and no wind subtraction in the inspected pressure calculation. The model's independent airspeed sensor block is commented out. **Inference:** stock airspeed fidelity cannot qualify the research envelope. A simulator sensor model may use plant truth, but the operational controller must receive hardware-representative measurements/estimates, never direct truth. Preserve stock 4003 and resolve research sensing separately.
5. **Observed inventory:** Ubuntu 24.04.5, Gazebo Sim 8.15.0, kernel 6.6.114.1, native build/cache and six prior ULogs exist. The CMake source path `/tmp/slaaps-px4-mount` is absent. Existing artifact presence is not source attribution, a runtime measurement or baseline reproduction.

Command ownership required for future integration:

| Stage | Required sole active owner | Current evidence / residual work |
| --- | --- | --- |
| Research request | One selected Pi producer | No application or selected transport yet; competing-source/replay tests required. |
| PX4 validated request | One ingress validator selected for the research mode | Existing MAVLink/DDS ingress does not establish the research freshness/sequence/ownership contract. |
| Feasible acceleration and attitude/thrust objectives | One reviewed PX4 control path | Missing plant/interface approval; stock navigation is not the research controller. |
| Rate and normalized torque/thrust demand | One selected PX4 stabilization path | Audit supports possible reuse, not approval; startup must prevent competing stock/research publishers. |
| Actuator allocation | PX4 control allocator with approved effectiveness and normalization | Research actuator count, signs, rank, limits and scaling unknown. |
| Output | Selected PX4 output driver/bridge | Stock gz path traced; custom canard/EDF channel mapping and eventual board I/O remain unverified. |
| Supervision/loss | PX4 mode and failsafe logic | Stock generic fallback choices are not approved as feasible for this vehicle. |

The one-owner table is a requirement, not a claim that competing sources are already prevented.

The master can accept documentation quality, pinned source facts, and planning
dependencies. The master cannot approve missing physical measurements, derive
an operating envelope without evidence, or silently choose the user's research
objective, acceleration semantics, performance thresholds or recovery concept.

The user requested only Wave 1 for this turn; the later resume request continued that scope. Wave 2 is the next candidate,
not an already-started assignment. Evidence specification can proceed without
invented plant values after the reviewed Wave 1 artifacts are accepted for that
planning purpose. Model ownership must be decided before any model edit.

| Next assignment | Model / prompt | Exclusive output | Release condition |
| --- | --- | --- | --- |
| Evidence/test specification | `gpt-5.6-terra`, high, E | `docs/SITL_TEST_SPECIFICATION.md` only | Master accepts Wave 1 planning inputs; user authorizes proceeding beyond this wave. No runner implementation implied. |
| Model ownership decision | Astra and user | Decision recorded in this file | Choose project model repository, forked submodule, or explicit external resource path before plant implementation. |
| Plant | `gpt-5.6-terra`, high, F | Exact model/test paths to be assigned later | Approved essential physical inputs/provisional ranges with provenance, model ownership and evidence conventions. |
| Companion prototype | `gpt-5.6-terra`, high, G | Exact application/test paths to be assigned later | Approved interface contract, transport, test specification and application ownership. |

Controller design H additionally depends on accepted plant/interface evidence;
implementation I requires Astra's architecture review. No parallel simulator
builds until worktree-specific mount/cache/build paths are established.

The exact next-wave gate is acceptance of these reviewed documents as planning inputs plus user direction to proceed beyond Wave 1. Phase 0 physical measurements are not a prerequisite for Prompt E: it must keep unresolved numeric limits named and must not implement a runner. Wave 2 also records a model-ownership choice. It does not unlock F/G automatically. Full Phase 1/2 acceptance still requires the runtime/tests listed above; research controller implementation waits for approved architecture and plant/interface evidence.

## Historical Wave 1 minimum user inputs (answered items superseded above)

Provide existing evidence first; do not perform new powered tests for this request.

1. **Configuration/data package:** available dimensioned CAD/sketch identifying canard count and independent servos, positions/orientations, deflection directions/limits, passive surfaces and EDF axis/flow relationship; current mass and CG; measured versus CAD-estimated inertia if available. Include units, coordinate reference, configuration/revision and uncertainty. Identify the person who owns missing data. Without geometry and independent actuation, plant/control authority cannot be assessed.
2. **Operating objective:** intended first speed/attitude regime, launch and recovery concept, requested acceleration axes, heading/speed policy and meaning of acceleration. Proposed convention is inertial CG acceleration `a = dv_NED/dt`, with specific force `f = a - g`; it is not approved. Supply available EDF/ESC, servo and sensor identifiers/data with their provenance. Aero/local-flow and transient measurements remain required before corresponding plant claims, but need not delay evidence specification.
3. **Next planning decision:** proceed to Wave 2 evidence specification and choose model ownership (project model repository, forked gz submodule, or explicit external resource path). A model outside the stock submodule is a planning recommendation to preserve the baseline; no repository/path choice or source write is implied. Companion physical link/OS and transport choice can be settled during the contract review; no guessed update rate or deadline is adopted now.

Tracking-error, settling, latency, saturation, state-validity and loss-response thresholds still need research-owner approval before acceptance runs. They are not demanded as guessed numbers before feasibility/data review.

## Historical Wave 1 final review record

All three worker deliverables were read in full; requirements and interface wording was corrected against source rather than accepted from summaries. The master independently checked critical controller/mode/allocator/Gazebo paths, DDS deserialization, failsafe defaults, airspeed provenance, installed WSL versions, prior binary/log inventory and cache mount metadata.

Review corrections included: missing evidence versus physical infeasibility; inertial acceleration versus specific force; finite versus non-NaN ingress checks; conversion versus active AUTO_PATH; DDS clock adjustment; torque-triggered allocation; conditional unpowered trim; sequential GUI/headless execution; sample-series/repeatability requirements; semantic NaN exceptions; and manifest lifecycle/dirty-input attribution. No simulator failure or tracking result is claimed because no simulator test ran.

Tooling interruption: a usage-limit rejection stopped two workers and a read-only probe. After the user resumed, the baseline worker completed; the interface worker handed source-backed corrections to Astra. Windows sandbox initialization continued to fail for edits, so the same native `apply_patch` helper was invoked through approved elevated execution. No file was edited using an alternate write mechanism. A WSL probe quoting error was corrected and rerun; `rg` was unavailable inside WSL, so the read-only cache search used `grep`.

Preservation verification: SHA-256 checks matched all eight pre-existing files. Parent Git status contains only the original changes plus the four assigned documentation artifacts. The index was not staged, parent HEAD stayed fixed, and the gz repository remained clean with the original gitlink. `git diff --check` passed; new-document structure/whitespace checks are part of final handoff verification. No firmware, model, launcher or build configuration changed.

At the historical Wave 1 handoff: planning artifacts and Prompt D were complete; no Wave 2 worker had yet been launched. That execution status is superseded by the current Wave 2/S1 record above. Roadmap Phases 0/1/2 and the project definition of done remain incomplete; there is still no experimental vehicle measurement or approved numerical flight envelope.
