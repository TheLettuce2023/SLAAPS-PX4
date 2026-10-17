# P3C native checked receipt clock

2026-09-27, Codex (GPT-6). Independently accepted at native checked receipt-clock
observation scope. Separate closed output-disabled profile. This integrates checked
reads into P2D's receipt pipeline; no map, readiness or positive authority is
qualified, and the accepted N1 admission runtime is unchanged.

## Native boundary and source isolation

The private P2D source view is extended with a native `NativeClock` wrapper.
Every receipt-bridge clock site uses a purpose-tagged checked read:
constructor entry, destructor entry, source observation, owner step entry and
completion, expected-stop intent, and owner closure. The actual wrapper calls
`px4_clock_gettime(CLOCK_MONOTONIC, ...)`, captures return and errno immediately,
and uses the byte-identical P3B conversion to validate timespec, overflow and
nonzero microseconds. Raw seconds/nanoseconds and converted microseconds are
retained. The P3B host header is overlaid with the private P3C native guard;
its original host guard and implementation stay unchanged in the workspace.

PX4's scheduler `hrt_absolute_time` implementation is untouched. Native IMU
sample timestamps and the parallel P2B raw recorder still originate through
that legacy path. Successful receipt-clock reads do not prove successful reads
there. This distinction prevents a checked observer from being mistaken for
a repaired global clock API or qualified producer-to-validator map.

The original P2C algorithm and P3B conversion implementation are byte-identical
in the private build. The runner pins originals, archives dirty/untracked input
identities and overlays, and checks the full source registry before/after.
The build retains the closed EKF/module/builtin allowlists with no admission
adapter, Commander, simulator bridge, network endpoint or output driver.

## Concurrent read ownership and failure ordering

Three lanes separate owner administration, EKF work-queue source and lifecycle
CLI reads. Each has its own fixed 4096-record archive, contiguous local sequence,
raw monotonic comparison and nonblocking writer guard. Ordinary owner/source
overlap is allowed; simultaneous writers to one lane are faults. Archive order
never invents a cross-thread clock order. Equal raw reads are permitted; a raw
regression is rejected even if microsecond truncation hides it.

Each record carries its purpose, result, errno, raw value, converted value and
synthetic-injection flag. Successful owner-entry reads that consume no receipt
are retained too. The independent checker requires exact source, lifecycle,
stop/closure and accepted owner-bracket correspondence with merged evidence.

A global atomic first-fault code publishes independently of archive capacity.
Only its winning writer stores the reserved fault details; those details are
read only after all read hooks have quiesced. Failed conversion/read or local
regression returns zero, causing the bridge's source/time checks to reject.
The owner also acquires the global fault at each pump boundary before normal
consumption. A fault racing that acquisition may be observed after an earlier
observation step; no admission mutation exists here. Cleanup may drain already
formed observations using valid reads while the original fault remains sticky.
No subsequent successful read repairs a fault or permits a successful export.

The private bridge separates lifecycle observation from normal sample recording.
A source fault disables samples but leaves lifecycle hooks enabled for the
expected destructor during cleanup. Export freezes both flags before waiting
on their shared in-flight counter. This preserves real destructor-entry evidence
after failure; it does not synthesize an end time or clear the original fault.

CLI bootstrap and export are sequential; immutable run/case configuration is
published before estimator scheduling. Export follows successful EKF stop,
freezes the existing raw/merge hooks, completes merge closure, then freezes
clock hooks and waits at most 1000 one-ms cleanup steps for in-flight readers.
Read archives are exported only after quiescence. An archive or cleanup/sink
failure rejects acceptance; no partial export is promoted to success.

## Normal and explicitly injected cases

Both cases use a fresh native process and run identity. Normal captures the
same 1200 pinned recorded IMU inputs used by P2D and requires exact raw/merged
payload correspondence plus checked timestamp correspondence.

The separate `read_error` case sets immutable `P3C_CASE=read_error` before
bootstrap. Source read 26 deliberately supplies synthetic `-1/EIO` to the
conversion instead of calling the OS. The record and terminal evidence must
both label this injection; it must never be reported as an observed OS error.
The expected prefix has 25 merged source observations, followed by sticky clock
Read failure and bridge SourceClock rejection. On pump rejection, the feeder
still records expected-stop intent before stopping EKF. Fault exports retain
both clock and merge evidence, while successful raw export is refused. Both
processes must shut down normally without forced termination or runtime error.

This is checked-read propagation through a real native receipt pipeline.
It does not qualify provider semantics, libc/vDSO binding, boot/time namespace,
VM pause or whole-interval continuity, sensor/health provenance, a clock map,
operator/transport capability, or positive admission. No baseline is repeated.


## Native evidence

Package `/var/tmp/p3c-native-8oh4gg31` in Ubuntu-24.04 WSL retains the 440-step
Debug build and two native processes. Manifest SHA-256:
`578e827dcfb69fdbb4a6a22a5d1230470ba717bb5bcc7ead8ce4c1779aa6efe4`.
The complete dirty/untracked source registry matched before and after both
runs. Both processes exited normally with no runtime error or forced shutdown.
`independent-check.json` retains the separate checker's initial passing result.

Normal: 1200 raw/merged observations match, with 3603 owner reads, 1200 source
reads and two lifecycle reads. Every used timestamp matches its checked read;
clock fault is zero. The largest source-to-owner completion delay was 31,155 us.

Injected read error: exactly 25 observations precede synthetic source read 26.
There are 79 owner reads, 26 source reads and two lifecycle reads. Sticky clock
Read fault (4), bridge SourceClock fault (6), reserved failed-read evidence and
real expected-stop/destructor-entry times are retained; raw success is absent.
The largest prefix source-to-owner completion delay was 5659 us. These are
observed delays, not approved timing or uncertainty limits.

`resource-review.json` retains stack reports and object symbols; SHA-256:
`2d1fa95cdb86c505b07ae7311aa871029908b8250c36ab6c19d738ba39a3bc51`.
Checked read has a 336-byte compiler-labeled dynamic/bounded self-frame;
bootstrap is 240 and export 496, also bounded. The three fixed lane archives
occupy 688,272 bytes; the separate fault record is 56 bytes. These exclude
callees, merge/raw archives and other runtime storage, and do not qualify an
embedded stack or memory budget.

Source review identified and corrected the sample-fault/destructor-hook
coupling before native execution. Private source-only review views
`/var/tmp/p3c-view-review-ensdpq5a` and `-iooeiu14` are preserved as review
material, not native evidence. There was no failed native execution in this
package and no baseline repetition.


## Checker correction and independent review

Independent artifact review reran the complete package and rejected ten
focused corruptions; evidence is
`/var/tmp/p3c-independent-review-dsxq8m8q/review.json`. A separate terminal-type
probe confirmed that the captured checker used Python dictionary equality for
the reserved fault record, permitting Boolean `true` to compare equal to integer
code 1. Neither real native artifact contained this corruption.

The checker now validates the terminal record's exact fields and integer/Boolean
types before comparing values. This is a verifier-only correction; no compiled
source, executable, native capture, input archive or manifest was rewritten.
The corrected checker and both imported checker dependencies are retained under
`/var/tmp/p3c-verifier-lbz2xeg4`. `verification.json` binds the unchanged native
manifest, verifier sources, passing full recheck and two rejected terminal-type
probes. Its SHA-256 is
`f9611798bf815d1ff88c12af7802aa3ca0a92aaa05ba707e83eb3ab2d0528fc3`.
Corrected checker SHA-256:
`e1d18e93dc803c538356363c4bb20c60392fda7bf560e074c2e734398d2b1f1d`.
Use this corrected verification with the frozen capture; the original package's
archived checker remains historical evidence of the executed source snapshot.

Final independent disposition is
`/var/tmp/p3c-final-review-an2v_qgt/review.json`, SHA-256
`437b908a8f45cf47e4a617ed06201c3b6e5910c5919fe77f041f1a414d03c088`.
The reviewer checked all three retained verifier source hashes, passed the
full frozen-package check and rejected both terminal Boolean probes. Acceptance
is limited to native checked receipt-clock observation and error propagation;
provider/map qualification and positive admission remain open.
