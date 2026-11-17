# Proposed revision 2: stock synthetic differential-pressure sample time

Draft, 2026-09-27. Requires explicit project-user approval before use.
Revision 1 and its earlier approval remain unchanged.

The new interval scorer found that stock
`src/modules/simulation/sensor_airspeed_sim/SensorAirspeedSim.cpp:162-169`
zero-initializes `differential_pressure`, assigns its publication `timestamp`,
and never assigns `timestamp_sample`. The message definition calls the latter
the raw-data capture time. This stock synthetic publisher supplies no such
time. A development read of the preserved headless log confirmed all selected
samples have `timestamp_sample == 0`; revision 1 correctly rejects them.

Proposed change for a new future campaign
`slaaps-stock-cessna-phase1-acceptance-02`:

- For **only** `differential_pressure` instance 0 from the stock
  `sensor_airspeed_sim` publisher with `device_id == 1377548`, permit an
  all-zero `timestamp_sample` stream as explicitly unavailable capture time.
- Record the exception and zero-sample count. Do not calculate sample order or
  publication-minus-sample age from these zero values, and make no raw-sensor
  timing claim for this synthetic signal.
- Continue checking its publication timestamp ordering, 2.5 s maximum logged
  gap, finite pressure, declared synthetic source and all other approved
  criteria. Missing fields, mixed zero/nonzero sample times, another device ID,
  or an unverified publisher do not qualify for this exception.
- If all sample timestamps are nonzero, apply the existing strict sample-order
  and nonnegative-age checks. All other topics retain revision-1 rules.

No numerical limit, settling/scoring duration, QGC warning rule or attitude
timestamp exception changes. This is diagnostic-only policy for the stock
Cessna. No earlier ULog becomes acceptance evidence retroactively.
