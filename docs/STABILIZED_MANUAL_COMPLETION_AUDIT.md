# Stabilized manual: completion audit and vehicle-data gate

2026-10-06, Codex. The full goal is **not complete**. Its isolated synthetic
implementation and reproducible operator workflow have bounded evidence.
SM5 requires accepted SLAAPS plant data and reviewed operating requirements,
which are unavailable. The goal is blocked; preserve the qualified default.

## Requirements and authoritative evidence

This audit retains the objective and ordered milestones in
[the original goal record](STABILIZED_MANUAL_SITL_GOAL.md).

| Requirement | Current evidence | Finding |
| --- | --- | --- |
| SM0/SM1: explicit pilot behavior, fixture, thrust and numerical contracts | Confirmed body-FRD rate sticks, full quaternion capture on all-axis centering, synthetic rotational fixture, fixed 10 N thrust; `FixtureConstants.hpp` and `SyntheticConfiguration.hpp` match their graph hashes | Established for the declared fixture |
| SM2: one owner, bounded reference/PID/allocation, rotations and fault/reset handling | Unchanged `StabilizedManualControl.cpp` and `StabilizedManualTest.cpp`; source tests cover capture, reference retention, q/-q/near-pi errors, invalid/future/reset data, saturation and windup; retained strict host verification | Implemented and tested at the documented host scope |
| SM3: rotational feedback with actuator dynamics and disturbances | Synthetic fixture has declared inertia, effectiveness, lag, damping and quantitative limits; closed control/physics records and retained host cases | Established for synthetic parameters |
| SM4: selected manual input and actual PX4 estimation, commands, disturbance/loss/reset/restart/source handling | Current `0crhkf9n` qualification has 12 nominal, four coupled, six fault/recovery and eight actual lifecycle/state cases plus frozen-clock dispatch; 31 fresh cases / 68,813 cycles | Proven for the admitted single-estimator fixture profile |
| Reproducible source build and run path | Current-checkout build, exact build receipt, full qualifier, profile creator and live console; 1,475 qualification artifacts independently rehashed | Complete for finite synthetic scope |
| Operator capture, hold, invalid input, disable, explicit restart and cleanup | `__heq56u` live-console receipt; first-entry capture and reference retention verified separately; five check and 41 session files independently rehashed | Proven for the default body-X-up operator path |
| Preserve stock baseline, H10 and negative-only acceleration gates | Shared checkout uORB/owner bytes retain their anchors; current profile grants no physical authority; previous default and evidence packages preserved | Existing acceptance boundaries retained |
| SM5: accepted vehicle plant, trim, finite-airflow authority, gain scheduling and flight requirements | No measured allocation profile in the known input location; supplied vehicle model still fails inertia intake; operating requirements and installation evidence remain missing | Not achieved |
| Broader publisher/health and vehicle deployment policy | Current summary explicitly records `general_publisher_identity_qualified=false`, `vehicle_supported_tuning=false`; individual publisher-handle identity and production flight-health policy remain unqualified | Open; fixture results do not close deployment scope |

The current qualification receipt is
`899a1ddd82e3226f715e74be1b4a90fa30c714b8759f276978a8cb904f3bfc3a`.
The live-console receipt is
`c65771974a65c8601a520d802e038adab825e82d41227a14a7d9ff1e5207a76c`.
Details and commands are in
[fresh checkout qualification](STABILIZED_MANUAL_CHECKOUT_QUALIFICATION.md).

## Current-state vehicle-input check

The known `Tools/research_sitl` allocation JSON inputs contain exactly one
`profile_kind`: `stabilized_allocation_synthetic.json`, labelled
`synthetic_fixture`. There is no `measured_single_condition` profile there.
The current admitted qualification contains no vehicle-profile artifact and
explicitly declines vehicle-supported tuning.

The supplied derivative remains byte-identical to its graph anchor:
`models/research_vehicle/model.sdf`, SHA-256
`a90644454080c2eba006cb29050d28a2e70fde5be33cafeae1185d7d844afa6d`.
A fresh read-only `geometry/check_sdf_intake.py` run reports:

- Base inertia violates physical triangle/second-moment constraints.
- Total declared link mass is 3.291337 kg, versus provisional all-up 3.041337 kg.
- All six mesh references resolve, which establishes packaging only.
- Structural readiness remains blocked.

Its [package provenance](../models/research_vehicle/provenance.json) and
[original CAD requirements](../models/research_vehicle/README.md) explain the
component/axis/sign records needed to resolve this. The model's physical values
have not been changed. The user already reports measurements unavailable and
CAD pending; this audit does not repeat the CAD request or initiate physical work.

## Required human inputs and next dependent work

| Human evidence or decision | Work it enables |
| --- | --- |
| Accepted assembly mass/CG/full inertia, units, axes, reference point, product-of-inertia convention and component scope | Reconcile mass, validate/transform the tensor, and construct an accepted vehicle plant |
| Canard geometry/hinge signs, moment derivatives and uncertainty versus declared airflow/EDF condition, trim bias and damping | Build physical allocation matrices, assess trim and usable moment authority |
| Loaded servo signed calibration, limits, slew, lag and latency | Apply installation-specific actuator mapping and dynamics |
| EDF thrust/lag/battery and measured local wash at each canard | Model propelled conditions and evaluate from-rest authority without guessing wash |
| Reviewed axes/rate/error/settling limits, operating envelope, sensor mounting/timing, entry/exit and loss/recovery policy | Define vehicle tuning, gain schedules, health/transition policy and acceptance tests |

The [SM5 input contract](STABILIZED_MANUAL_SM5_ALLOCATION_INPUT_CONTRACT.md)
already specifies source hashes, units, uncertainty and ordered canard IDs.
Once the evidence arrives, independently review its records, compile the
condition-specific profile, screen rank/net authority, fit the accepted plant,
then tune and qualify the vehicle-supported variant. Synthetic constants cannot
be promoted into those missing records.

The current console can still be operated and existing records inspected
without these inputs. General software review can inform later deployment,
but it cannot supply the absent physical authority, trim or gain envelope.
No live PX4 or Gazebo fixture job remains to wait for. The next required SM5
implementation is data-dependent. Three consecutive goal turns confirmed the same missing vehicle evidence and requirements. The second and third checks made no implementation progress; all 445 source anchors remained unchanged and only the synthetic allocation profile was present. The goal is now blocked, not complete. Resume when accepted inputs become available; the demonstrated synthetic console remains usable.
