# Servo-only bench physical acceptance: open

2026-10-01, Codex. This is the acceptance sequence and evidence contract, not
permission to flash, energize actuators or run the tunnel. [H8](MANUAL_BENCH_CONTROL_H8.md)
now adds the explicitly authorized default-off module and dedicated output path.
Its integrated images are software evidence, not physically accepted firmware.
C1E-N1 remains negative-only; flight and positive
acceleration admission are outside this gate. See [H7](MANUAL_BENCH_CONTROL_H7.md)
for hardware assumptions, independent stop architecture and retained build hashes.

The [commissioning procedure](MANUAL_BENCH_FLASH_AND_TEST_PROCEDURE.md), added
2026-10-13, gives the USB flashing, actual PWM/fault measurement, calibration,
powered/wind and subsequent stabilized hardware development steps. Its staged
H10 candidate is revalidated software evidence; all physical gates below remain open.

## Ordered exit conditions

| Stage | Required result | Current status |
| --- | --- | --- |
| Software integration | One explicitly controlled PX4 owner; approved MAIN1-4 output path; EDF/AUX4 inhibited; no automatic startup or enable | H8 implemented; operational profile and end-to-end acceptance remain open |
| Source-bound verification | Strict Debug/Release/sanitizer regression; fresh SITL and ARM compilation; integrated ELF symbol/map and stack/resource review | H8 portable suites pass; see H8 for linked images and limits; runtime stack/deadline qualification open |
| Output fault design | Reviewed behavior on source loss/change, owner stall/crash, disarm/disable, reboot and PX4IO communication loss | H8 owner/consumer latches implemented; full-system failure and physical response remain unqualified |
| De-energized installation | Channel labels, hinge/TED arrows, reversal convention, power topology, EDF physical isolation, fixture and independent stop review | Assumptions documented; installed checks absent |
| Signal-only capture | Servos and EDF disconnected; capture MAIN1-4 waveforms through startup, enable, commands, loss, scheduling failure and reboot | Not run; requires integrated firmware and separate flash authorization |
| Powered no-wind | Authorized restrained test: per-channel angle/pulse calibration, directions, coupled commands, loaded rail/current, neutral/loss/stop/restart behavior | Not run; requires earlier reviews and separate powered-test authorization |
| Staged wind | Approved finite speed/load envelope, restraint, holding/slew/current/temperature measurements, controlled normal and emergency stops | Not run; >40 m/s is not a finite upper bound |
| Acceptance review | Named test authority reviews results, deviations and residual risks for this exact configuration | Open; not an automated approval |

Do not test hazardous failure cases under wind before reviewing their no-wind
results and fixture suitability. E-stop removes energy; it does not guarantee
neutral or immobilized surfaces. Wind decay and free surface movement remain
mechanical hazards. A firmware neutral request is not the independent stop.

The test authority must set numerical acceptance limits before measurements
are marked passing. Existing 20 ms/500 ms timing and +/-13-degree geometry
assumptions do not establish scheduler, radio, thermal or aerodynamic margins.
Record exact QGC daily build, gamepad/calibration, radio configuration and observed
identities. MAVLink routing IDs do not authenticate the operator. EDF enabled
operation would require a separate scope and acceptance record.

## Read-only evidence preflight

`Tools/research_sitl/check_bench_acceptance.py` supplies a blank JSON record and
checks twelve mandatory evidence categories. It has no PX4 connection, actuator
API, subprocess execution, file-writing path or firmware/parameter mutation.

```sh
python3 Tools/research_sitl/check_bench_acceptance.py --template
python3 Tools/research_sitl/check_bench_acceptance.py RECORD.json --evidence-root EVIDENCE_DIRECTORY
python3 -B -m unittest discover -s Tools/research_sitl -p test_bench_acceptance.py -v
```

The template prints to stdout; the operator creates a separate evidence record.
`baseline` references a hash-pinned source/configuration manifest inside the
evidence directory. Each mandatory entry needs `status: reviewed`, reviewer,
UTC review timestamp, an explanatory summary and hash-pinned relative artifact
paths. Summaries should identify measured values, approved limits, outcome and
exceptions. Never mark an assumed specification as an installed measurement.
The reviewer must establish that all artifacts apply to the baseline; the checker
cannot infer configuration consistency from arbitrary document contents.

Exit 1 means incomplete/mismatched evidence; exit 2 means malformed/unreadable
input or invocation error. Exit 0 means **record completeness and byte integrity
only**, not physical acceptance. Every report keeps `physical_acceptance_granted`,
`flash_authorized` and `outputs_authorized` false, even with a complete record.
Hashes neither establish factual truth nor authenticate the named reviewer.
This is a review aid, not a trusted admission mechanism or runtime interlock.

The checker rejects missing categories, assumptions in place of review, missing
metadata, malformed timestamps, bad hashes, missing/corrupted artifacts,
absolute/traversal/escaping-symlink paths, duplicate JSON keys, nonfinite JSON
constants and unexpected schema fields. Synthetic complete records test the
success path without asserting any real physical result.

## Verification and remaining authority boundary

Ubuntu-24.04 WSL: 19 unittest cases pass, including CLI exit 0/1/2 behavior,
all twelve individually missing categories, corruption, path escapes and explicit
non-authorization on complete evidence. This is Python host-tool verification,
not new firmware evidence. C++ sources and build configuration are unchanged;
AStyle, Debug/Release/ASan-UBSan, SITL and ARM builds were not rerun for this
tool-only increment. Prior H7 counts/resources remain historical evidence.
Exact tool/test/document SHA-256 anchors are in `RESEARCH_CODE_GRAPH.json`.

The user subsequently authorized the default-off bench module and output path,
implemented separately in H8. This checker remains an evidence-review aid only.
No airframe defaults, arming changes, physical coefficients or measured timing
budgets are supplied. Physical acceptance and enabled manual-flight capability
remain open; H8 source/link evidence does not fill in missing hardware results.
