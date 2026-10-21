# Manual servo bench R09 and R10 operator report

Recorded2026-10-20 by Codex from the operator's statement. This is an operator
acceptance record; the measurement date/time was not supplied separately.

| Report | Result | Basis |
| --- | --- | --- |
| R09 Measured calibration and power review | **PASS** | Operator-reported; all servos closely matched manufacturer documentation |
| R10 Powered joystick test | **PASS** | Operator explicitly reports acceptance and requests this result recorded |

Operator statement:

> All servo's closely matched given manufacturing documentation. Record this and mark R09 and R10 as passed.

The project identifies EMAX ES3059MD servos on MAIN1..4 and a Pixhawk6X working
bench image. The operator's report applies to all servos tested in this sequence.
The [earlier R01..R08 report](../build/bench_commissioning/bench-e8ea98b404-step3/hardware-joystick-signal-report-20261020.json)
remains preserved, so R01 through R10 now have operator-reported passes for the
stated manual servo bench scope.

The [structured acceptance record](../build/bench_commissioning/bench-e8ea98b404-step3/hardware-r09-r10-operator-pass-20261020.json)
retains the exact statement, prior context bindings and pre-update procedure/form
snapshots. Numerical PWM/angle/current/voltage/timing values, raw scope/video
captures, exact tested profile/hash and load/linkage configuration were not
supplied in this closeout message. Those fields remain unspecified; no values
are substituted from the manufacturer document and no independent raw-data review
is claimed. The blank report template remains unchanged for future runs.

This closes R09 and R10 as operator-reported passes. It does not add results to
the separate unreported G4 fault cases, wind tests or stabilized hardware tests.
No firmware, parameter or profile change accompanies this record.
