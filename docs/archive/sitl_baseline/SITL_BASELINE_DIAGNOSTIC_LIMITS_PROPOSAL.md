# Proposed stock Cessna baseline smoke limits

**Draft for review, 2026-09-27. Not approved and not a flight or control
budget.** These are candidate *logged-evidence* limits for a future, fresh,
disarmed `gz_rc_cessna` GUI/headless Phase 1 campaign. They cannot be applied
retroactively to turn the recorded diagnostic runs into acceptance passes.
Approval must identify an approver, date, revision/hash and the exact future
campaign before execution. Changing a value requires a new registry revision
and run ID. The integrated checker is not implemented yet.

## Observation interval

Start each run from a pinned parameter file and fresh PX4/Gazebo processes.
Record startup and wait for an explicit ready checkpoint: model spawned,
`commander` preflight OK, disarmed Hold, estimator tilt/yaw/GNSS aligned,
valid position, GNSS 3D fix, airspeed source identified, and (for GUI) a
QGroundControl link. After that checkpoint, allow **10 s settling** and score
an uninterrupted **60 s** interval. Record the checkpoint and interval bounds
in PX4 monotonic microseconds and wall-clock UTC. Headless has no QGC visual
requirement, but must separately state its telemetry-link expectation. If the
checkpoint or full interval is absent, classify `incomplete`; a state that
becomes invalid inside the scored interval is `fail`. These durations are
proposals, not approved values.

The following observed maxima are intervals between ULog records over entire
runs, including startup. Column order is first GUI, headless, follow-up GUI;
these three runs preceded this proposal. The later timed QGC recheck did not
set any candidate value or approve them.
ULog logger subscriptions may be rate-limited, so these observations do not
measure raw sensor publication rate, hardware latency or control deadlines.

| Logged signal | Observed maximum gap | Proposed maximum in scored interval |
| --- | ---: | ---: |
| `vehicle_attitude` | 8 / 4 / 8 ms | 20 ms |
| `vehicle_local_position`, `vehicle_global_position` | 12 / 8 / 12 ms | 40 ms each |
| `airspeed_validated`, `actuator_motors`, `actuator_servos` | 100–104 ms | 250 ms each |
| `sensor_gps`, `sensor_baro`, `sensor_mag`, `differential_pressure`, `estimator_status_flags` | 1.000–1.024 s | 2.5 s each |
| `telemetry_status` instance 0 | 1.672 / 1.044 / 1.108 s | 2.5 s |

These candidate bounds allow several missed fast-topic log cycles and one
missed nominal slow-topic record. They are intentionally diagnostic margins
around the observed stock system, not a claim that those gaps are safe for the
research vehicle. A checker must recompute all gaps from raw ULogs for the
predeclared interval, require each listed topic and instance, and report sample
count, maximum and violating timestamps. Zero logger dropouts are proposed.

## Time ordering and validity

Within the scored interval, require strictly increasing `timestamp_sample`
where present and nondecreasing publication `timestamp`; any backward step,
non-finite required position/attitude/GNSS/baro/mag value, or negative
publication-minus-sample age fails. For attitude and local/global
position, propose publication-minus-sample age **at most 8 ms**. Missing
required timestamp fields or a missing raw stream make the run `incomplete`;
present but invalid or backward timestamps make it `fail`. Do not apply
that value to `estimator_status_flags`: its delayed-horizon sample age was
152 ms in all three diagnostics and needs a separate source-based policy.

An equal `vehicle_attitude.timestamp` is provisionally permissible only if
both records have distinct, strictly increasing sample times, finite
quaternions, each age is within the 8 ms proposal, and the pair is listed in
the report. The original GUI had 29 such pairs and the follow-up GUI had 10;
every pair had a 4 ms sample difference and ages of 4 ms then 0 ms. The
underlying scheduling trigger remains unknown. Approval must decide whether
equal publication time is acceptable at all; if strict publication-time
increase is required, the observed GUI behavior is a failure to investigate.

Require local/global position validity, 3D GNSS fix, aligned estimator flags,
preflight OK and disarmed state throughout the scored interval. Require
`airspeed_validated.airspeed_source == 1` with finite calibrated/true airspeed
and finite differential pressure after readiness. The startup `source == -1`
and associated non-finite airspeed are recorded as startup behavior only.
Disarmed `actuator_motors` NaN fields follow the declared PX4 disarmed
semantics and must not be scored as commanded thrust. Record that stock
airspeed is ground-truth-derived through `sensor_airspeed_sim`; it is not an
independent Gazebo airspeed sensor.

## QGroundControl and MAVLink

For GUI, require a human record of parameter loading, live attitude,
GPS/map position, displayed mode and MAVLink Inspector traffic, with the
observation time and any warning. A visible **“Comms Lost” while PX4 is live**
is a failed GUI-link check, even if PX4 reports a valid GCS heartbeat or
inbound bytes. A warning only after the documented normal shutdown is outside
the scored live interval. If its timing cannot be established, this check is
`incomplete`.

As a separate machine check, propose that MAVLink instance 0 reports a valid
GCS heartbeat and that `telemetry_status.rx_message_count` never decreases
and advances at least once every **5 s** within the GUI interval. The
follow-up GUI ULog showed 259 counter increases and a maximum 1.108 s gap
between them, but QGC's rendering and connection status still need direct
human observation. Do not infer QGC screen health from the PX4 counter.

## Required approval and implementation work

Review the five numeric gap groups, the 8 ms age value, 10 s settling, 60 s
scoring, 5 s inbound-counter bound and the equal-publication-time policy.
Record the approved registry hash **before** the next acceptance campaign.
The checker must freeze that registry, independently parse the raw ULogs,
validate source/model/parameter and process provenance, join the timed QGC
record, and return `fail` or `incomplete` on any unmet requirement. The current
v0 integrity auditor verifies archived bytes and cross-links; a separate raw
ULog inspector now recomputes measurements but does not score these draft
limits or grant Phase 1 pass.
