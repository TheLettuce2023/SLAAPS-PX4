#!/usr/bin/env python3
"""Audit and seal exact synthetic SITL evidence, without rerunning simulations."""
import argparse
import json
import math
from pathlib import Path
import shutil

import verify_stabilized_campaigns as campaigns
import verify_stabilized_sitl as base

ROOT = base.ROOT


def audit_cycles(path):
    data = campaigns.rows(path)
    assert data, 'No published control cycles'
    previous = None
    for row in data:
        assert all(math.isfinite(float(value)) for value in row.values()), 'Nonfinite control log value'
        now = int(row['time'])
        assert row['state'] in ('2', '3') and row['thrust'] == '10'
        assert (row['sysid'], row['compid'], row['instance'], row['manual_valid']) == ('255', '190', '0', '1')
        for publication, sample in (('att_time', 'att_sample'), ('rate_time', 'rate_sample'), ('manual_time', 'manual_sample')):
            assert 0 < int(row[sample]) <= int(row[publication]) <= now
            assert now - int(row[sample]) <= 100000
        assert abs(int(row['att_sample']) - int(row['rate_sample'])) <= 50000
        for axis in 'xyz':
            assert abs(float(row['s' + axis])) <= 1.000001
            assert abs(float(row['b' + axis])) <= 1.000001
            assert abs(float(row['i' + axis])) <= 0.200001
            residual = float(row['m' + axis]) - float(row['a' + axis])
            assert abs(residual - float(row['e' + axis])) < 1e-6
            if abs(abs(residual) - 0.001) > 1e-6:
                assert int(row['p' + axis]) == int(residual > 0.001)
                assert int(row['n' + axis]) == int(residual < -0.001)
        for channel in range(4):
            assert abs(float(row['u' + str(channel)])) <= 1.000001
        if previous is not None:
            interval = now - int(previous['time'])
            assert 4000 <= interval <= 60000
            for channel in range(4):
                assert abs(float(row['u' + str(channel)]) - float(previous['u' + str(channel)])) <= 5 * interval / 1e6 + 1e-6
            for axis in 'xyz':
                assert abs(float(row['s' + axis]) - float(previous['s' + axis])) <= 20 * interval / 1e6 + 1e-6
        previous = row
    return len(data)


def audit_runtime_profile(work, result):
    runtime = json.loads((work / 'runtime-profile-check.json').read_text())
    expected = {'EKF2_GBIAS_INIT': result['gyro_bias_prior_rad_s'],
                'EKF2_ABIAS_INIT': result['accel_bias_prior_m_s2'], 'EKF2_MAG_TYPE': 0}
    assert set(runtime) == set(expected)
    assert all(runtime[name]['returncode'] == 0 and runtime[name]['expected'] == value
               for name, value in expected.items()), 'Actual runtime profile was not verified'


def audit_case(work, result):
    counts = audit_cycles(work / 'control.csv')
    audit_runtime_profile(work, result)
    if 'case' in result:
        counts += audit_cycles(work / 'restarted.csv')
        assert result['reenable_returncode'] != 0
        assert result['new_session'] > result['old_session'] > 0
        assert result['final_thrust_n'] == 0
        if result['case'] == 'producer_stall':
            assert 100000 <= result['consumer_expiry_delay_us'] <= 104000
    else:
        numerical = dict(rate_tracking=result['rate_error_rad_s'] < campaigns.RATE_TOLERANCE,
                         capture=result['capture_error_rad'] < 2e-5,
                         retention=result['retention_error_rad'] < 1e-6,
                         final_physical_error=result['final_physical_error_rad'] < campaigns.FINAL_ANGLE_LIMIT,
                         final_estimated_error=result['final_estimated_error_rad'] < campaigns.FINAL_ANGLE_LIMIT,
                         final_rate=result['final_rate_rad_s'] < campaigns.FINAL_RATE_LIMIT,
                         peak=result['peak_physical_error_rad'] < campaigns.PEAK_ANGLE_LIMIT)
        assert all(result['checks'][name] == outcome for name, outcome in numerical.items())
        assert result['passed'] == all(result['checks'].values())
        truth = campaigns.rows(work / 'truth.csv')
        q = campaigns.quaternion(truth[0])
        body_x_down = 2 * (q[1] * q[3] - q[0] * q[2])
        assert abs(body_x_down - (-1 if result['nose_up'] else 0)) < 1e-5
        angle, samples = campaigns.magnetic_rigidity(work, truth)
        assert angle < math.radians(1) and samples > 1000
    for name, identity in result['source_sha256'].items():
        assert base.digest(ROOT / name) == identity, 'Current test input changed: ' + name
        assert base.digest(work / 'source-inputs' / name) == identity, 'Archived case input changed: ' + name
    return counts


def seal(out, incomplete=False):
    expected = {('rate', up, axis, sign) for up in (False, True) for axis in range(3) for sign in (-1, 1)}
    expected |= {('failure', case) for case in campaigns.FAILURES}
    chosen = {}
    for path in sorted(out.glob('*/result.json'), key=lambda p: p.stat().st_mtime_ns):
        result = json.loads(path.read_text())
        assert not result.get('exploratory_only'), 'Exploratory results cannot satisfy campaign acceptance'
        if result.get('dispatch_regression'):
            dispatch = json.loads((out / 'dispatch-manifest.json').read_text())
            assert dispatch['accepted'] and dispatch['files'][str(path.relative_to(out))] == base.digest(path)
            continue
        if result.get('coupled'):
            # The separate four-case audit must already seal these exact cases.
            coupled_manifest = json.loads((out / 'coupled-manifest.json').read_text())
            assert coupled_manifest['accepted']
            name = str(path.relative_to(out))
            assert coupled_manifest['files'][name] == base.digest(path)
            continue
        if not result.get('source_sha256'):
            continue
        assert 'case' in result or all(name in result for name in ('nose_up', 'axis', 'direction')), \
            'Unclassified failed attempt: ' + str(path)
        key = ('failure', result['case']) if 'case' in result else ('rate', result['nose_up'], result['axis'], result['direction'])
        chosen[key] = (path.parent, result)
    assert set(chosen) == expected, 'Missing campaign coverage: ' + repr(expected - set(chosen))
    failed = [work.name for work, result in chosen.values() if not result['passed']]
    assert incomplete or not failed, 'Latest applicable campaigns failed: ' + repr(failed)
    accepted = not failed
    px4 = base.digest(out / 'sitl-lockstep-build/bin/px4')
    plugin = base.digest(out / 'plugin-build/libresearch_rotational_fixture.so')
    cycles = 0
    for work, result in chosen.values():
        assert result['px4_sha256'] == px4 and result['plugin_sha256'] == plugin
        assert result.get('gyro_bias_prior_rad_s') == campaigns.GYRO_BIAS_PRIOR
        assert result.get('accel_bias_prior_m_s2') == campaigns.ACCEL_BIAS_PRIOR
        cycles += audit_case(work, result)
    checkpoint = json.loads((out / 'checkpoint.json').read_text())
    allowed = set()  # Every mapped campaign/build input is frozen at preparation.
    preserved = {}
    for name, identity in checkpoint.items():
        if name in allowed:
            continue
        assert base.digest(ROOT / name) == identity, 'Unrelated mapped source changed: ' + name
        preserved[name] = identity
    # Revalidate compiled control/estimation/transport anchors, including core
    # inputs not duplicated into each case's small fixture snapshot.
    native = out / 'px4-source'
    inputs = {}
    graph = json.loads((ROOT / 'docs/RESEARCH_CODE_GRAPH.json').read_text())
    for node in graph['nodes']:
        name = node['path']
        if name.startswith(('src/', 'platforms/', 'msg/', 'boards/')) and (native / name).is_file():
            identity = base.digest(ROOT / name)
            assert base.digest(native / name) == identity, 'Native dependency differs: ' + name
            inputs[name] = identity
    snapshot = out / 'audited-inputs'
    for name in inputs:
        destination = snapshot / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(ROOT / name, destination)
    shutil.copy2(Path(__file__), out / Path(__file__).name)
    (out / 'audited-inputs.json').write_text(json.dumps(inputs, indent=2) + '\n')
    (out / 'campaign-summary.json').write_text(json.dumps(dict(
        scope='synthetic_estimated_state_rate_hold_disturbance_loss_and_explicit_recovery',
        accepted=accepted, failed_cases=failed,
        positive_cases=12, positive_passed=sum(result['passed'] for key, (_, result) in chosen.items() if key[0] == 'rate'),
        failure_recovery_cases=6, audited_control_cycles=cycles,
        cases=[dict(directory=work.name, **result) for work, result in chosen.values()]), indent=2) + '\n')
    (out / 'preservation.json').write_text(json.dumps(dict(preserved=preserved,
        intentional_test_harness_changes=sorted(allowed)), indent=2) + '\n')
    files = {}
    for path in out.iterdir():
        if path.is_file() and path.name != 'campaign-manifest.json':
            files[path.name] = base.digest(path)
        elif path.is_dir() and path.name.startswith(('rate-', 'failure-', 'tested-artifacts', 'audited-inputs', 'runtime-inputs-before')):
            for artifact in path.rglob('*'):
                if artifact.is_file():
                    files[str(artifact.relative_to(out))] = base.digest(artifact)
    for name in ('sitl-lockstep-build/bin/px4', 'sitl-lockstep-build/boardconfig',
                 'sitl-lockstep-build/compile_commands.json', 'plugin-build/libresearch_rotational_fixture.so'):
        files[name] = base.digest(out / name)
    record = dict(scope='synthetic_sitl_campaigns_only', physical_authority_granted=False,
                  vehicle_supported_tuning=False, accepted=accepted, failed_cases=failed,
                  positive_cases=12, failure_recovery_cases=6,
                  preserved_anchor_count=len(preserved), files=files)
    (out / 'campaign-manifest.json').write_text(json.dumps(record, indent=2) + '\n')
    print(f'18 cases / {cycles} published cycles audited; {len(preserved)} anchors preserved', flush=True)
    print('Manifest SHA-256:', base.digest(out / 'campaign-manifest.json'), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--evidence', type=Path, required=True)
    parser.add_argument('--incomplete', action='store_true', help='Seal failed attempts as explicit incomplete evidence')
    args = parser.parse_args()
    out = args.evidence.resolve()
    assert out.parent == Path('/var/tmp') and out.name.startswith('research-stabilized-sitl-')
    seal(out, args.incomplete)


if __name__ == '__main__':
    main()
