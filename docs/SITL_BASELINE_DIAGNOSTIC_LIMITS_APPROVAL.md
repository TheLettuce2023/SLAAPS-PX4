# Stock Cessna diagnostic limits approval

Recorded 2026-09-27T23:28:22Z. The project user approved the
[diagnostic-only draft](SITL_BASELINE_DIAGNOSTIC_LIMITS_PROPOSAL.md) as written
in this conversation. The conversation supplies the approving role, not a
personal name or independent signature. The approved proposal's exact SHA-256
is `9bc5c087a8330ce10fd8321b3c432888d9dd87478c84751cd61bcc0d614db290`.
Keep those bytes unchanged; a future change needs a new proposal, approval and
registry revision.

The machine-readable transcription is
[`diagnostic_limits_v1.json`](../Tools/research_sitl/baseline/diagnostic_limits_v1.json),
revision 1, SHA-256
`42549c019ad500dbb63b184b294170759c315e4333309e8074e3a633e9db7142`.
The registry is bound to future campaign
`slaaps-stock-cessna-phase1-acceptance-01`. Each GUI/headless run and the
required second-operator repeat must name this campaign and exact registry
hash in its plan **before process launch**. Preserve the registry bytes; a
changed rule, value or campaign needs a new revision and run ID.

Approval includes the draft's 10 s settling and uninterrupted 60 s scored
interval; maximum logged gaps of 20 ms attitude, 40 ms local/global position,
250 ms airspeed/actuators and 2.5 s slow topics; 8 ms publication-minus-sample
age for attitude and position; and a 5 s maximum between inbound MAVLink
counter advances in GUI. It also accepts equal attitude publication timestamps
only with distinct, ordered sample times, finite quaternions, both ages within
8 ms, and every pair reported. A live QGroundControl “Comms Lost” is a GUI
failure; a warning only after documented normal shutdown is outside the scored
interval; unknown timing is incomplete. Headless telemetry-link expectation
must be separately declared in its pre-run plan. No 8 ms age bound is assigned
to delayed `estimator_status_flags` samples.

This approves **logged diagnostic criteria for this stock Cessna campaign**.
It does not approve flight/control limits or turn the four earlier diagnostic
ULogs into acceptance runs. Phase 1 still requires a checker that validates the
predeclared interval and provenance, fresh GUI/headless runs, timed human QGC
evidence and a second-operator repetition. The current raw inspector remains
measurement-only and returns incomplete.
