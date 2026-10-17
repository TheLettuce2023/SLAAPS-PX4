#!/usr/bin/env python3
"""Preserve selected exploratory SITL noise/bias observations with source hashes."""
import argparse
import bisect
import json
import math
from pathlib import Path
import shutil
import tempfile
import xml.etree.ElementTree as ET

import seal_stabilized_campaigns as audit
import verify_stabilized_campaigns as campaigns
import verify_stabilized_sitl as base


def inspect(work):
    assert work.parent.name.startswith('research-stabilized-sitl-') and work.parent.parent == Path('/var/tmp')
    result = json.loads((work / 'result.json').read_text())
    assert result['coupled'] and result['nose_up'] and result['sign'] == -1
    assert result['px4_sha256'] == base.digest(work.parent / 'sitl-lockstep-build/bin/px4')
    assert result['plugin_sha256'] == base.digest(work.parent / 'plugin-build/libresearch_rotational_fixture.so')
    assert result['coupled_source_sha256'] == base.digest(work / 'verify_stabilized_coupled.py')

    for name, identity in result['source_sha256'].items():
        assert base.digest(work / 'source-inputs' / name) == identity

    audit.audit_runtime_profile(work, result)
    cycles = audit.audit_cycles(work / 'control.csv')
    assert cycles == result['audited_control_cycles']
    control = campaigns.rows(work / 'control.csv')
    truth = campaigns.rows(work / 'truth.csv')
    events = campaigns.rows(work / 'control.csv.events.csv')
    assert len([row for row in events if row['action'] == 'dispatch_not_due']) == result['duplicate_dispatches_without_control']
    rate = [row for row in control if row['state'] == '2']
    release = next(row for row in control if row['state'] == '3' and int(row['time']) > int(rate[-1]['time']))
    reference = campaigns.quaternion(release, 'r')
    actual_reference = campaigns.quaternion(min(truth, key=lambda row: abs(int(row['time']) - int(release['att_sample']))))
    final = min(truth, key=lambda row: abs(int(row['time']) - int(control[-1]['time'])))
    physical = campaigns.separation(campaigns.quaternion(final), actual_reference)
    estimated = campaigns.separation(campaigns.quaternion(control[-1]), reference)
    final_rate = math.sqrt(sum(float(final['w' + axis]) ** 2 for axis in 'xyz'))
    peak = max(campaigns.separation(campaigns.quaternion(row), actual_reference) for row in truth
               if int(release['att_sample']) <= int(row['time']) <= int(control[-1]['time']))

    for actual, recorded in ((physical, result['final_physical_error_rad']),
                             (estimated, result['final_estimated_error_rad']),
                             (final_rate, result['final_rate_rad_s']),
                             (peak, result['peak_physical_error_rad'])):
        assert abs(actual - recorded) < 1e-7, 'Result disagrees with closed logs'

    assert result['passed'] == all(result['checks'].values())
    assert result['checks']['final_physical_error'] == (physical < campaigns.FINAL_ANGLE_LIMIT)
    assert result['checks']['peak'] == (peak < campaigns.PEAK_ANGLE_LIMIT)
    assert result['checks']['final_rate'] == (final_rate < campaigns.FINAL_RATE_LIMIT)

    times = [int(row['time']) for row in truth]
    physical_rate_errors = []

    for phase in (result['sign'], -result['sign']):
        selected = [row for row in rate if float(row['roll']) == phase]
        assert selected
        end = int(selected[-1]['time'])
        errors = []

        for row in selected:
            if int(row['time']) < end - 300000:
                continue

            stamp = int(row['rate_sample'])
            index = bisect.bisect_left(times, stamp)
            index = min((max(0, index - 1), min(index, len(truth) - 1)), key=lambda i: abs(times[i] - stamp))
            assert abs(times[index] - stamp) <= 4000
            errors.extend(abs(float(truth[index]['w' + axis]) - phase) for axis in 'xyz')

        physical_rate_errors.append(max(errors))

    world = ET.fromstring((work / 'world.sdf').read_text())
    injected = result.get('injected_gyro_sensor_mean_rad_s', 0)
    assert all(float(world.find(
        f"world/model/link/sensor[@name='imu_sensor']/imu/angular_velocity/{axis}/noise/mean").text) == injected
               for axis in 'xyz')
    trace_mean = None

    if injected:
        assert result['exploratory_only'] and result['continuous_ulog']
        assert base.digest(work / 'world.sdf') == result['generated_world_sha256']
        assert result['diagnostic_source_sha256'] == base.digest(work.parent / 'diagnose_stabilized_coupled.py')
        reports = list(work.glob('estimator-trace-audit-*.json'))
        assert len(reports) == 1
        trace = json.loads(reports[0].read_text())
        assert not trace['logger_dropouts'] and trace['audited_control_cycles'] == cycles
        assert trace['logged_maneuver_passed'] == result['passed']
        assert abs(trace['logged_initial_bias_priors']['EKF2_GBIAS_INIT'] - result['gyro_bias_prior_rad_s']) < 1e-7
        assert abs(trace['logged_initial_bias_priors']['EKF2_ABIAS_INIT'] - result['accel_bias_prior_m_s2']) < 1e-7

        for name, identity in trace['inputs_sha256'].items():
            assert base.digest(work / name) == identity

        assert base.digest(work / ('analyze_stabilized_estimator_trace-' + trace['source_sha256'][:12] + '.py')) == trace['source_sha256']
        trace_mean = trace['sensor_residuals']['sensor_gyro']['mean_body_residual']
        # The Gazebo-to-FRD bridge preserves X and reverses Y/Z.
        assert all(abs(observed - expected) < 0.00008 for observed, expected in
                   zip(trace_mean, (injected, -injected, -injected)))

    assert result['passed'] and max(physical_rate_errors) < campaigns.RATE_TOLERANCE
    return dict(work=str(work), seed=result['seed'], injected_gyro_sensor_mean_rad_s=injected,
                cycles=cycles, physical_rate_errors_rad_s=physical_rate_errors,
                final_physical_error_rad=physical, final_estimated_error_rad=estimated,
                final_rate_rad_s=final_rate, peak_physical_error_rad=peak,
                logged_gyro_mean_residual_rad_s=trace_mean, observed_checks_pass=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work', type=Path, action='append', required=True)
    args = parser.parse_args()
    works = [work.resolve() for work in args.work]
    assert len(works) == len(set(works))
    cases = [inspect(work) for work in works]
    out = Path(tempfile.mkdtemp(prefix='research-stabilized-sitl-probes-', dir='/var/tmp'))
    shutil.copy2(Path(__file__), out / Path(__file__).name)
    (out / 'summary.json').write_text(json.dumps(dict(
        scope='selected_full_axis_nose_up_noise_and_gyro_bias_observations',
        vehicle_qualification=False, campaign_acceptance=False,
        source_sha256=base.digest(Path(__file__)), cases=cases), indent=2) + '\n')
    files = {}

    for work in works:
        for path in work.rglob('*'):
            if path.is_file() and not path.is_symlink():
                files[str(path)] = base.digest(path)

    for path in out.iterdir():
        if path.is_file():
            files[str(path)] = base.digest(path)

    (out / 'manifest.json').write_text(json.dumps(dict(
        scope='source_bound_exploratory_observations_only', vehicle_qualification=False,
        campaign_acceptance=False, cases=len(cases), files=files), indent=2) + '\n')
    print(json.dumps(dict(directory=str(out), manifest_sha256=base.digest(out / 'manifest.json'),
                          case_count=len(cases), file_count=len(files), cycles=sum(case['cycles'] for case in cases)),
                     indent=2), flush=True)


if __name__ == '__main__':
    main()
