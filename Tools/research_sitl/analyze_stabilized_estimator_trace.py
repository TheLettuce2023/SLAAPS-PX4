#!/usr/bin/env python3
"""Offline actual-sensor/estimator trace audit; no control or vehicle acceptance."""
import argparse
import bisect
import json
from pathlib import Path
import shutil

import numpy as np
from pyulog import ULog

import seal_stabilized_campaigns as audit
import verify_stabilized_campaigns as campaigns
import verify_stabilized_sitl as base


def analyze(work):
    paths = list((work / 'fs/log').rglob('*.ulg'))
    assert len(paths) == 1, 'Require one closed isolated logger trace'
    ulog = ULog(str(paths[0]))
    assert not ulog.dropouts, 'Trace has logger dropouts'
    data = {topic.name: topic.data for topic in ulog.data_list if topic.multi_id == 0}
    control = campaigns.rows(work / 'control.csv')
    truth = campaigns.rows(work / 'truth.csv')
    events = campaigns.rows(work / 'control.csv.events.csv')
    cycles = audit.audit_cycles(work / 'control.csv')
    times = np.array([int(row['time']) for row in truth], dtype=np.int64)
    q = np.array([campaigns.quaternion(row) for row in truth])
    rates = np.array([[float(row['w' + axis]) for axis in 'xyz'] for row in truth])

    def nearest(stamps):
        index = np.clip(np.searchsorted(times, stamps), 1, len(times) - 1)
        index -= np.abs(stamps - times[index - 1]) < np.abs(stamps - times[index])
        return index

    sensors = {}
    for name in ('sensor_gyro', 'sensor_accel'):
        d = data[name]
        stamps = d['timestamp_sample'].astype(np.int64)
        valid = (stamps >= times[0]) & (stamps <= times[-1])
        ix = nearest(stamps[valid])
        assert np.max(np.abs(times[ix] - stamps[valid])) <= 4000
        actual = np.column_stack([d[axis][valid] for axis in 'xyz'])
        if name == 'sensor_gyro':
            expected = rates[ix]
        else:
            w, x, y, z = q[ix].T
            # At the constrained CG, true specific force is -gravity in body FRD.
            expected = -9.80665 * np.column_stack([2*(x*z-w*y), 2*(y*z+w*x), 1-2*(x*x+y*y)])
        residual = actual - expected
        sensors[name] = dict(samples=len(ix), mean_body_residual=np.mean(residual, axis=0).tolist(),
                             rms_body_residual=np.sqrt(np.mean(residual**2, axis=0)).tolist(),
                             max_abs_body_residual=np.max(np.abs(residual), axis=0).tolist())
    timing = {}
    for name in ('sensor_gyro', 'sensor_accel', 'vehicle_imu', 'vehicle_attitude', 'estimator_states'):
        d = data[name]
        stamps = d['timestamp_sample'].astype(np.int64)
        timing[name] = dict(samples=len(stamps), publication_minus_sample_us=
                            np.percentile(d['timestamp'].astype(np.int64)-stamps, [0, 50, 100]).tolist(),
                            sample_interval_us=np.percentile(np.diff(stamps), [0, 50, 100]).tolist())
    gnss = data['estimator_aid_src_gnss_vel']
    gnss_observation = np.column_stack([gnss[f'observation[{i}]'] for i in range(3)])
    imu = data['vehicle_imu']
    calibration = {name: sorted(map(int, np.unique(imu[name]))) for name in
                   ('gyro_calibration_count', 'accel_calibration_count')}
    # Check the actual duplicate-dispatch branch independently of maneuver
    # performance. One successful publication (or enable), then no second
    # publication at that clock and no sink replay/fault around the event.
    control_times = [int(row['time']) for row in control]
    assert len(set(control_times)) == len(control_times)
    duplicates = [row for row in events if row['action'] == 'dispatch_not_due']
    for duplicate in duplicates:
        stamp = int(duplicate['time'])
        assert duplicate['status'] == '4' and duplicate['state'] in ('2', '3')
        previous = bisect.bisect_right(control_times, stamp) - 1
        if previous >= 0:
            assert control_times[previous] == stamp
        else:
            assert any(row['action'] == 'enable' and int(row['time']) == stamp for row in events)
        start = bisect.bisect_left(times, stamp + 4000)
        end = bisect.bisect_right(times, stamp + 12000)
        window = truth[start:end]
        assert window and all(row['enabled'] == '1' and row['lease_fault'] == '0' for row in window)
        # Accepted sink command times around the duplicate must correspond to
        # real, unique published cycles, never an extra dispatcher-only update.
        assert all(int(row['command_time']) in control_times for row in window)
        assert all(int(row['command_time']) <= stamp or int(row['command_time']) >= stamp + 4000 for row in window)
    result = json.loads((work / 'result.json').read_text())
    assert len(duplicates) == result['duplicate_dispatches_without_control']
    report = dict(scope='offline_exploratory_sensor_estimator_and_dispatch_observation',
                  vehicle_qualification=False, maneuver_accepted=False,
                  logged_maneuver_passed=result['passed'], audited_control_cycles=cycles,
                  logger_dropouts=len(ulog.dropouts), sensor_residuals=sensors, timing=timing,
                  gnss_velocity_observation_range_m_s=[np.min(gnss_observation, axis=0).tolist(),
                                                      np.max(gnss_observation, axis=0).tolist()],
                  calibration_counts=calibration, duplicate_dispatches_observed=len(duplicates),
                  logged_initial_bias_priors={name: ulog.initial_parameters[name]
                                             for name in ('EKF2_GBIAS_INIT', 'EKF2_ABIAS_INIT')},
                  duplicate_event_times_us=[int(row['time']) for row in duplicates],
                  source_sha256=base.digest(Path(__file__)), inputs_sha256={
                      str(path.relative_to(work)): base.digest(path) for path in
                      (paths[0], work / 'truth.csv', work / 'control.csv', work / 'control.csv.events.csv',
                       work / 'result.json')})
    output = work / ('estimator-trace-audit-' + report['source_sha256'][:12] + '.json')
    encoded = json.dumps(report, indent=2) + '\n'
    assert not output.exists() or output.read_text() == encoded, 'Never replace an earlier different audit'
    output.write_text(encoded)
    shutil.copy2(Path(__file__), work / (Path(__file__).stem + '-' + report['source_sha256'][:12] + '.py'))
    print(json.dumps(report, indent=2), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work', type=Path, required=True)
    args = parser.parse_args()
    work = args.work.resolve()
    assert work.parent.parent == Path('/var/tmp') and work.parent.name.startswith('research-stabilized-sitl-')
    analyze(work)


if __name__ == '__main__':
    main()
