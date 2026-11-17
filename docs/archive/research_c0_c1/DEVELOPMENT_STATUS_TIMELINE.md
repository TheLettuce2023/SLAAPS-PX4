# SLAAPS research vehicle: development status and completion timeline

Updated 2026-09-21. Scope: the user reconfirmed non-weaponized research use.
SLAAPS is the project/vehicle name; no targeting or interception capability is
part of this development plan. The dated current entry in
`PHASE_GATE_STATUS.md` is authoritative for implementation/test acceptance.

## Outcome being built

A maintained PX4 fork with an opt-in research acceleration-guided mode and a
separately named custom vehicle configuration/model. A host research sequencer
in SITL, then a Raspberry Pi 4B guidance application, supplies inertial
acceleration requests at the CG in rotating body FRD axes. Components remain
body-held until replacement or expiry; NED remains the navigation convention.
The input objective is acceleration only, not direct EDF or servo commands.

PX4 owns estimation, validity checks, feasible-command limiting, stabilization,
allocation, mode supervision, actuator output and command-loss response. The
physical vehicle has four independently actuated X-pattern canards, one rear
fixed-axis EDF, no thrust vectoring and no main fixed wing. The software must
demonstrate attainable tracking, not assume arbitrary instantaneous force in
all three axes. Air-relative velocity is state information, not a second
companion objective. The planned scalar DroneCAN pitot alone does not measure
a complete air-relative velocity vector.

The research mode is intended to specialize the companion-guided/Offboard
workflow. Exact commander integration, mode identifiers and user-interface
selection remain an integration design decision: no new selectable PX4 mode
or operational MAVLink dialect exists from the isolated libraries alone.

## Completed development history

Dates below follow the recorded local gates, not release dates or flight hours.

| Recorded period | Completed artifact or gate | What it does not establish |
| --- | --- | --- |
| Pre-roadmap baseline | Existing WSL/Gazebo/Cessna setup, launchers and prior logs were inventoried. | Prior runtime claims/logs are not a freshly reproduced, provenance-complete baseline. |
| 2026-09-16: Wave 1 | Requirements/missing-data register, interface/source audit, reproducibility inventory and master review. | Physical feasibility and full baseline runtime acceptance remain open. |
| 2026-09-17: architecture/evidence | Test/evidence conventions, custom-model ownership decision and preliminary control architecture. | No aerodynamic model, tuned controller or allocation matrix was approved. |
| 2026-09-17: S1 | Portable request guard and frame/gravity mathematics; 231 guard and 165 frame checks in recorded host verification. | No transport, PX4 mode or actuator path. |
| 2026-09-17: E1 | Offline evidence checker, schemas and 17 test methods. | A valid evidence package is not automatically proof of flight performance. |
| 2026-09-17: C0a | Stateless payload codec and 8 test methods. | No MAVLink framing, authentication, link or request admission. |
| 2026-09-17: H1 | Isolated normalized EDF/four-servo command limiter, 843 checks; rounding regression was found and fixed. | This is not an X/plus mixer, control allocator, aerodynamic effectiveness model or flight controller. |
| 2026-09-17: C0b.1 | Checked trusted clock-map conversion, 191 checks. | No deployed clock synchronization/provenance service. |
| 2026-09-17 to 18: C0b.2 state core | Reviewed single-guard ownership and implemented closed state observation/barriers/reports; 907 checks. | Its getter never admits a request. |
| 2026-09-18: session design | Reviewed session establish/revoke, trusted grant/replay rules, latch recovery and independent oracle. | Design acceptance alone is not executable verification. |
| 2026-09-18: session administration | Implemented; 361 checks plus unchanged regressions pass Debug, repeated Release and ASan/UBSan; independent review passed. | No live authority mechanism. |
| 2026-09-19: C0b.2 admission | Immutable post-transition ACK and full request admission through checked map/S1; 603 checks and regressions pass all three configurations; independent review accepted. | This is decoded-payload admission, not a live receiver or controller. |
| 2026-09-19: C0b.3 status | Report-time evidence, pure status mapper/encoder, 47 historical/direct and 1266 matrix checks, nine independent C++/Python vectors; 9/9 tests pass Debug/repeated Release/ASan+UBSan. | No generated MAVLink interoperability, status transport, mode or physical performance claim. |
| 2026-09-19: SITL console design | User-authorized brokered operator-console design reviewed; user selected cooperative trusted-local option 1. | No hostile same-user isolation, arming, outputs or hardware authority. |
| 2026-09-19: O1 broker core | Offline prepare/confirm broker, one-owner event sequencing, fail-closed outcomes, real-adapter fixtures and fault matrix; 11/11 tests pass Debug, repeated Release and ASan/UBSan. | No interactive console, runtime owner/scheduler, deployed ingress, live MAVLink or flight-control integration. |
| 2026-09-19: O2 offline owner/facade | Real single-adapter owner, immutable one-shot ingress, fixture provenance, redacted facade and 169 assertions; 12/12 tests pass Debug, repeated Release and ASan/UBSan; independent review accepted. | No runtime freshness, parser authentication, live console/link, PX4 mode or actuator path. |
| 2026-09-19: G1 geometry intake | Supplied SDF/meshes audited without modification; intake checker has 8 passing synthetic test methods. | Supplied model fails inertia and resource checks; geometry audit is not a validated plant. |
| 2026-09-19: C0c generated wire | Parent-owned offline dialect, pinned generated C/Python interoperability, independent layout/CRC/frame oracle; 32 round-trip vectors, 8 framing negatives and 6 semantic negatives pass Debug, optimized and ASan/UBSan; independent review accepted. | No live receiver, signing, clock service, source authority, mode or output integration. |
| 2026-09-19: Phase 2 exit audit/proposal | Roadmap criterion audit and user-requested provisional SITL timing/residual-attitude/loss-rule measurement proposal prepared. | A proposal is not approved operational requirements or measured link/control evidence; Phase 2 remains open. |
| 2026-09-19: P2T1 approved offline profile | User approved inactive software testing; actual adapter/accessor fixture and complete-status oracle pass 14/14 targets in Debug, repeated Release and ASan/UBSan; 113 timed records and 27 malformed-package rejections. | Not the complete profile campaign, measured host/Pi/link timing, deployed interface or acceleration tracking. |
| 2026-09-19: P2T2/P2T3 remaining offline campaign | Status delivery/diagnostics: 4,110 checked events and 36 rejected corruptions. Acceleration-only codec constraints: six methods; seven declarative policy cases reviewed. Complete 17-target suite passes Debug, repeated Release and ASan/UBSan; independent review accepted. | Virtual clocks and fixture state, not measured host/Pi/link performance. Residual policy implementation and full Phase 2 operational signoff remain open. |
| 2026-09-20: C1H host-loopback | User-approved, output-disabled signed UDP path through actual native admission/access and authenticated status. One Debug/three Release campaigns pass, 20 nominal requests and 27 status returns each; independent review accepted. | No PX4/Gazebo attachment, operational real-time guarantee or flight control. Earlier timing failures are retained; bounded host GC scheduling was corrected without relaxing targets. |
| 2026-09-20: C1A passive PX4 attachment | User-approved isolated Debug PX4 build; two fresh disarmed state/clock runs, 21 clock samples and three fresh publications per state topic each. Eleven checker methods and master/independent review pass. | No PX4 request admission, qualified clock map, Gazebo, active research mode or control/output path. Earlier missing-parameter failures remain retained; missing-sensor and event-capacity warnings prevent health-readiness claims. |
| 2026-09-20: C1B-D diagnostic selection | User-approved design; master/independent review accepted immutable diagnostic domain, separate operator selection, session/ACK barriers and diagnostic-only evidence/test requirements. | Design only: no guard/adapter code change, new wire schema, live validation or additional runtime test. Existing flight Selected remains unchanged. |
| 2026-09-20: C1B-I offline API/test design | Master/independent review accepted exact diagnostic APIs, private lifecycle closure, four-record bounds, local evidence rules and a finite matrix including 1,024 guard-state tuples. | Specification only, not executed tests or implementation acceptance. No new build/runtime connection; full C1 and Phase 2 remain open. |
| 2026-09-20: C1B-G guard implementation | Private diagnostic C++ guard, shared validation, domain isolation and default-off build implemented. 20/20 targets pass Debug, two Release runs and ASan/UBSan; default-off 17/17 passes. New guard: 1,024 tuples, 12,163 checks; 18 compile probes. Independent review passes. | Direct offline guard fixtures only. Diagnostic adapter/selector, owner/broker, report/evidence integration and live PX4 admission are not implemented by this slice. |
| 2026-09-20: C1B-A adapter implementation | Single diagnostic adapter selector, exact grants, session/ACK admission, bounded reports and private lifecycle closure implemented. 24/24 targets pass Debug, two Release runs and ASan/UBSan; default-off 17/17. New adapter: 3,400 checks; 25 compile probes; independent static review passes. | Offline decoded-input fixtures only. Diagnostic owner/broker, local JSON oracle and live PX4 admission remain pending; no mode, controller or outputs. |
| 2026-09-20: C1B-O owner/broker implementation | Real single owner, redacted facade, shared tickets, immutable ingress and fail-closed lifecycle/result checks. Final 28/28 targets pass Debug, repeated Release and ASan/UBSan; default-off 17/17. Owner: 1,124 checks; 31 compile probes; independent review passes. | Offline synthetic receipt/authentication facts only. Local evidence integration and live PX4 diagnostic admission remain pending; no controller or outputs. |
| 2026-09-20: C1B-E complete finite offline gate | Actual-owner C++ JSON campaign, independent oracle/corruptions, sink independence and remaining native assertions accepted. Final 31/31 pass Debug, repeated Release and ASan/UBSan; default-off 17/17. Adapter 3,533 checks, owner 1,209, 33 build probes and 11 evidence methods. | One finite JSON reference plus native matrix fixtures; not live state/clock/estimator/operator qualification, control or flight validation. |
| 2026-09-20: C1C-S1 receipt checker | Structural receipt continuity, source-transition counters, explicit freshness and sticky faults implemented. Debug, repeated Release and ASan/UBSan each pass 2/2; 139 variants, 815 checks and 22 compile probes. Default-off has no checker target. | Synthetic supplied receipts, not real producer-hook completeness, live clock trust, estimator readiness or admission authority. |
| 2026-09-20: C1C-S2 actual source capture | Fresh isolated PX4 build and two disarmed runs accepted; 193/194 receipts, actual nav changes, complete final tails and strict source/build/startup bindings. Host Debug/repeated Release/sanitizers pass 3/3; independent review accepted. | Capture only, not request admission, concurrent-source serialization, healthy estimation, qualified guidance clocks or acceleration control. |
| 2026-09-20: C1D-N1 closed-owner software | Shared build profiles and private source-owned real adapter implemented; 37/37 Debug, repeated Release and sanitizer tests; default-OFF 17/17. 1,911 owner checks, 61 isolation probes, exact memory/stack evidence and independent review accepted. | Explicitly synthetic native-contract host execution, not actual PX4 native integration, live ingress or flight control. |
| 2026-09-21: C1D-N2 native closed owner | Actual PX4 build and 27 native-profile probes; reviewed stack bound; two disarmed runs with 186 source records/189 adapter reports each, normal closure and identical repeat exports. All 26 actual-export corruptions rejected; host regressions and independent final review pass. | Closed polling only: no live ingress, operator/session authority, healthy estimator, qualified guidance clock, custom mode, controller or outputs. Full C1/Phase 2 remain open. |

Check counts include helper/setup assertions. They are not independent flight
scenarios, coverage percentages or exhaustive state-space proofs. Existing
tests are rerun where the current gate records a fresh result; otherwise the
table refers to historical evidence.

## Remaining development timeline

Current position (2026-09-21): Phase 2 interface foundations, O1/O2, C0c and
P2T1/P2T2/P2T3 passed at their recorded scope. Approved C1H host-only live
MAVLink qualification now passes four bounded campaigns with actual native
access and returned status. No PX4 live receiver, custom mode or flight-control
integration is claimed. The user has now approved C1 output-disabled PX4
attachment. Its first C1A passive state/clock slice now passes a fresh isolated
Debug build, two actual disarmed PX4 runs and the independent evidence checks;
full C1 remains open. No Gazebo connection was made in this slice.
The subsequent C1B-D architecture and C1B-I exact offline API/test design are
accepted. C1B-G private guard, C1B-A single-adapter selector/barrier and C1B-O
owner/broker software are implemented and pass ordinary/diagnostic verification.
Local evidence export/checking and finite-matrix reconciliation now pass C1B-E.
C1C-S1 source-traced receipt continuity/freshness checks now pass standalone.
C1C-S2 actual Commander capture now passes in a private source overlay, with
early subscription and complete producer-summary reconciliation. It remains
no-output and no-admission; capture does not solve source/admission concurrency.
C1D-N1 shared native build-domain and closed-owner software passes host review.
C1D-N2 now also passes actual private Commander integration: normal run-end
closure, retained archive, native stack/build evidence and strict independent
post-stop replay from two disarmed runs. See C1D_NATIVE_INTEGRATION_VERIFICATION.md.
Next is the technical design review for native diagnostic ingress/owner
integration: serialized operator/session/loss handling, authenticated parser
and status, qualified clocks and actual estimator-state provenance. The
approved C1 authority permits that design work without another blanket approval;
it does not permit fabricated readiness or automatic live ingress activation.
C1B-I remains a historical design gate; complete finite OFFLINE C1B G/A/O/E
evidence is not complete live C1B or operational Phase 2 acceptance.
Parallel Phase 3 intake G1 has audited the new Gazebo assets and found a
physical inertia blocker. Phase 0 feasibility and Phase 1 runtime reproduction
remain open; Phase 4 active control is not yet released. See the current
PHASE_GATE_STATUS.md entry for the exact evidence and next promotion gates.

This is a dependency/exit-gate timeline, not a promised calendar schedule.
Measurements, engineering review and hardware availability determine the
critical path; assigning dates or a completion percentage now would invent
precision. Independent lanes may overlap only within their approved scope.

| Order / lane | Remaining work | Evidence required to exit |
| --- | --- | --- |
| 1. Phase 2 contract promotion | Released offline campaign accepted; all seven groups accounted for in PHASE_2_OFFLINE_COVERAGE.md. Obtain required operational budget/contract evidence without treating later Phase 4 implementation as a circular prerequisite. | Synthetic fixtures and policy-only review are not operational tracking or hardware/control requirements. |
| Current: C1 PX4 attachment | C1A, finite offline C1B G/A/O/E, C1C-S1/S2 and C1D-N1/N2 closed owner pass, including actual native execution. Next: native diagnostic ingress/owner technical design review. | No arming, active mode or outputs. Positive live validation still needs estimator/transition/clock/operator-loss provenance, synchronization and explicit diagnostic schema/profile binding. Closed polling is not full C1 acceptance. |
| 2. Deployment interface | Approve authority/identity, command timing and clocks; implement custom MAVLink dialect/framing, trusted receiver and status feedback; desktop deterministic research sender. | Wire-to-canonical-request trace, independent authentication/authorization, acceptance status, delay/restart/competing-source tests. Reception alone is not a pass. |
| Parallel: baseline | Reproduce stock Cessna GUI/headless, telemetry, bridge and shutdown tests with manifests. Establish distinct worktree-specific simulator build/cache/mount paths before concurrent simulator builds. | Another operator reproduces the pinned baseline and incomplete evidence fails closed. |
| Parallel: physical inputs | Review supplied CAD/parts and measured mass properties, local airflow, EDF/servo dynamics and sensor layout. Resolve launch/recovery feasibility and initial envelope. | Provenance and uncertainty for model/control inputs; explicit blocked directions/low-speed regimes rather than guessed coefficients. |
| 3. Custom vehicle/plant | Build separately named Gazebo model and simulation airframe under approved parent-project model ownership; represent body/canard forces, EDF, servos and hardware-like sensors. | Open-loop sign, force/moment, trim, timestep and sensor checks against independent data; stock Cessna remains runnable. |
| 4. Control-law gate | Review achievable acceleration mapping, residual attitude/roll policy, reuse of PX4 stabilization, gain scheduling and explicit normalized allocator contract. | Approved plant-supported architecture and numerical acceptance limits before control implementation. |
| 5. Control implementation | Inner-loop stabilization first, then feasible acceleration tracking and X-canard/EDF allocation; saturation, slew, anti-windup and authority telemetry. | Estimator-in-the-loop tracking across the declared initial envelope; unattainable commands limited and reported. |
| 6. PX4 mode/airframe integration | Add opt-in module/build/startup/parameters, research Offboard selection, entry/exit supervision and approved loss behavior. Ensure one publisher per command stage. | No competing stock controller, no stale demand across transitions, consistent normalized outputs and reviewed sensor/authority failures. |
| 7. Integrated SITL regression | Exercise the full sender -> receiver -> admission -> control -> allocation -> actuators -> plant/sensors -> estimator loop. | Predeclared per-axis error, settling, saturation, deadline and fault thresholds; truth used only by independent scoring. |
| 8. Pi-in-the-loop | Run the companion/translator on Pi 4B; qualify serial/link and scheduling behavior against host simulation. | Measured rates, age/jitter, restart/disconnect behavior and status logs. This is not Pixhawk hardware validation. |
| 9. Robustness and independent review | Sweep justified uncertainty, wind, estimation, actuator and communication failures; compare software/model revisions separately. | Quantitative required-case coverage and independent engineering acceptance without relaxing limits after failures. |
| Parallel early / final late: Pixhawk portability | Compile the same portable flight source for verified Pixhawk 6X, check memory/scheduling/I/O/configuration and establish a supported hardware-simulation route. | Versioned board artifacts/resource evidence; explicit simulator/HITL compatibility gaps. A SITL executable cannot be flashed. |
| 10. Human-led validation | Separately authorized propulsion-isolated bench checks, supported HIL, then contained ground/flight experiments with abort/recovery criteria. | Real measurements support the declared envelope; discrepancies update model uncertainty and trigger regression reruns. |
| 11. Maintainable fork release | Pin source/submodules/models/parameters, document mode/vehicle setup, archive accepted evidence and recovery configuration. | Repeatable builds/tests and traceable qualified configuration. Commit/push/PR still require explicit requests. |

The main physical critical path is measured feasibility -> validated plant ->
control/allocator -> integrated robustness -> hardware evidence. Offline
interface, test infrastructure, companion preparation and compile portability
can progress independently where their contracts are approved.

## Known requirements and unresolved inputs

Confirmed: four separate X-canard servos; single fixed-axis EDF; FRD/NED axes;
CG-referenced inertial acceleration; body-held components; Pi 4B/TELEM MAVLink
translator for hardware and host-script MAVLink for SITL; Pixhawk 6X onboard
sensors plus GNSS and DroneCAN pitot; PX4 ownership of all flight-critical
decisions. Simulator truth is not an operational control input.

Intent, not validated performance: nose-up (+X upward) release from rest under
EDF power, with extra launch speed only if validated results require it;
headfirst net recovery after reducing speed. Approximate 2.5 kg mass and
5.5 kgf static thrust are historical user expectations, not measured model constants.
The 2026-09-19 update replaces the old mass estimate with an assumed 3.041337 kg
all-up mass, including moving canards and EDF rotor. The supplied SDF currently
double-counts placeholder component masses against that convention and fails
the installed SDFormat inertia check; see GAZEBO_GEOMETRY_INTAKE_REVIEW.md.
No approved stall/low-authority speed or net-entry envelope follows from them.

Human inputs needed before their respective gates:

- Physical: measured/as-built mass, CG and inertia; canard geometry/axes/signs,
  linkage/travel, local-flow evidence and aerodynamic uncertainty; EDF/ESC
  thrust/lag versus command and power; servo range/rate/latency/load response.
  A parts list helps identify components but cannot supply all installation-
  dependent measurements. CAD/SDF can describe structure, not prove physics.
- Interface/deployment: supervisor authority and event provenance, source/target
  identity and authentication/key lifecycle, clock provenance, command-rate,
  latency/age/validity/jitter/uncertainty budgets and status ownership.
- Controls/test: feasible speed/attitude envelope, residual attitude/roll
  priorities, quantitative acceleration-tracking criteria and approved
  command-loss/estimate-loss/authority-loss response. Software revocation alone
  is not a physically safe recovery maneuver.
- Hardware/operations: confirmed board variant, wiring/power/sensor mounting,
  test lead, containment, independent abort/recovery plan and separate explicit
  permission before flashing or powered physical operation.

No additional geometry input is needed for offline authority/interface work.
O2, generated wire conformance, P2T1/P2T2/P2T3, C1H and passive C1A are accepted
at their recorded scope. C1B-O now completes the bounded offline owner/broker
increment; local diagnostic evidence tooling proceeds under existing approval.
Future live diagnostic schema/profile, deployment trust boundaries, timing and
clock provenance still need review before promotion. Missing measurements block validated plant/control work;
they do not justify inventing a mixer, gain, low-speed authority or fallback.

## Definition of complete

The end goal is not achieved by receiving a vector, compiling a module or
showing plausible motion. Completion requires versioned custom physics with
documented validation limits, a tested Pi command interface, portable PX4
control source and custom vehicle/mode configuration, quantitative simulation
regression, verified Pixhawk firmware/configuration, and real bench/flight
evidence for the declared research envelope. The project is not yet complete.
