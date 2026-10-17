#!/usr/bin/env python3
"""Independently audit four exploratory unequal-axis native SITL maneuvers."""
import argparse
import bisect
import json
import math
from pathlib import Path
import shutil

import seal_stabilized_campaigns as audit
import verify_stabilized_campaigns as campaigns
import verify_stabilized_mixed as mixed
import verify_stabilized_sitl as base


def inspect(work, result, evidence, expected_magnetic_delay_us=0, expected_imu_delay_us=0,
            expected_manual_period_s=0.02):
    assert result['exploratory_only'] and result['mixed_axes']
    assert result.get('manual_send_period_s', 0.02) == expected_manual_period_s, \
        'Changed manual cadence requires separate audit'
    assert result.get('magnetic_transport_delay_us', 0) == expected_magnetic_delay_us, \
        'Changed magnetic delay requires separate audit'
    assert result.get('imu_transport_delay_us', 0) == expected_imu_delay_us, \
        'Changed IMU delay requires separate audit'
    assert result['mixed_source_sha256'] == base.digest(Path(mixed.__file__))
    assert result['mixed_source_sha256'] == base.digest(work / Path(mixed.__file__).name)
    assert result['px4_sha256'] == base.digest(evidence / 'sitl-lockstep-build/bin/px4')
    assert result['plugin_sha256'] == base.digest(evidence / 'plugin-build/libresearch_rotational_fixture.so')
    assert tuple(result['body_sticks']) == mixed.BODY_STICKS
    assert tuple(result['impulse_nms']) == mixed.IMPULSE_NMS
    audit.audit_runtime_profile(work, result)

    for name, identity in result['source_sha256'].items():
        assert base.digest(work / 'source-inputs' / name) == identity
        assert base.digest(base.ROOT / name) == identity

    cycles = audit.audit_cycles(work / 'control.csv')
    assert cycles == result['audited_control_cycles']
    control = campaigns.rows(work / 'control.csv')
    truth = campaigns.rows(work / 'truth.csv')
    times = [int(row['time']) for row in truth]
    rate = [row for row in control if row['state'] == '2']
    physical_rate_error = 0.0
    setpoint_error = 0.0

    for phase in (result['first_sign'], -result['first_sign']):
        raw = [value * phase for value in mixed.BODY_STICKS]
        selected = [row for row in rate if abs(float(row['roll']) - raw[0]) < 1e-5
                    and abs(float(row['pitch']) + raw[1]) < 1e-5
                    and abs(float(row['yaw']) - raw[2]) < 1e-5]
        assert selected
        end = int(selected[-1]['time'])
        settled = [row for row in selected if int(row['time']) >= end - 300000]
        assert len(settled) >= 40

        for row in settled:
            stamp = int(row['rate_sample'])
            index = bisect.bisect_left(times, stamp)
            index = min((max(0, index - 1), min(index, len(truth) - 1)), key=lambda i: abs(times[i] - stamp))
            assert abs(times[index] - stamp) <= 4000

            for axis, stick in zip('xyz', raw):
                target = mixed.expected_rate(stick)
                setpoint_error = max(setpoint_error, abs(float(row['s' + axis]) - target))
                physical_rate_error = max(physical_rate_error, abs(float(truth[index]['w' + axis]) - target))

    release = next(row for row in control if row['state'] == '3' and int(row['time']) > int(rate[-1]['time']))
    reference = campaigns.quaternion(release, 'r')
    capture = campaigns.separation(campaigns.quaternion(release), reference)
    retention = max(campaigns.separation(campaigns.quaternion(row, 'r'), reference) for row in control
                    if int(row['time']) >= int(release['time']))
    physical_reference = campaigns.quaternion(min(truth, key=lambda row:
                                                  abs(int(row['time']) - int(release['att_sample']))))
    final = min(truth, key=lambda row: abs(int(row['time']) - int(control[-1]['time'])))
    physical_final = campaigns.separation(campaigns.quaternion(final), physical_reference)
    estimated_final = campaigns.separation(campaigns.quaternion(control[-1]), reference)
    final_rate = math.sqrt(sum(float(final['w' + axis]) ** 2 for axis in 'xyz'))
    peak = max(campaigns.separation(campaigns.quaternion(row), physical_reference) for row in truth
               if int(release['att_sample']) <= int(row['time']) <= int(control[-1]['time']))
    injected = [row for row in truth if any(float(row['impulse_' + axis]) != 0 for axis in 'xyz')]
    assert len(injected) == 1

    for axis, impulse in zip('xyz', mixed.IMPULSE_NMS):
        assert abs(float(injected[0]['impulse_' + axis]) - impulse * result['first_sign']) < 1e-6

    effectiveness = ((.25, -.25, .25, -.25), (.25, .25, -.25, -.25), (.25, -.25, -.25, .25))

    for row in control:
        controls = [float(row['u' + str(i)]) for i in range(4)]

        for axis, coefficients in zip('xyz', effectiveness):
            assert abs(sum(a*b for a, b in zip(coefficients, controls)) - float(row['a' + axis])) < 1e-6

    assert abs(physical_final - result['final_physical_error_rad']) < 1e-7
    assert abs(estimated_final - result['final_estimated_error_rad']) < 1e-7
    assert abs(final_rate - result['final_rate_rad_s']) < 1e-7
    assert abs(peak - result['peak_physical_error_rad']) < 1e-7
    assert abs(physical_rate_error - result['maximum_physical_rate_error_rad_s']) < 1e-7
    assert abs(setpoint_error - result['maximum_setpoint_error_rad_s']) < 1e-7
    assert all(result['checks'].values()) and result['passed']
    assert physical_rate_error < campaigns.RATE_TOLERANCE and setpoint_error < 1e-4
    assert physical_final < campaigns.FINAL_ANGLE_LIMIT and estimated_final < campaigns.FINAL_ANGLE_LIMIT
    assert final_rate < campaigns.FINAL_RATE_LIMIT and peak < campaigns.PEAK_ANGLE_LIMIT
    assert capture < 2e-5 and retention < 1e-6
    assert result['antiwindup_violations'] == 0 and result['box_saturated_cycles'] > 0
    assert truth[-1]['thrust'] == '0' and truth[-1]['lease_fault'] == '1'
    return dict(directory=work.name, nose_up=result['nose_up'], first_sign=result['first_sign'],
                cycles=cycles, maximum_physical_rate_error_rad_s=physical_rate_error,
                maximum_setpoint_error_rad_s=setpoint_error, final_physical_error_rad=physical_final,
                final_estimated_error_rad=estimated_final, final_rate_rad_s=final_rate,
                peak_physical_error_rad=peak, observed_checks_pass=True)


def seal(evidence):
    paths = sorted(evidence.glob('mixed-*/result.json'))
    assert len(paths) == 4, 'Require the exact four declared mixed-axis attempts'
    cases = {}

    for path in paths:
        result = json.loads(path.read_text())
        key = (result['nose_up'], result['first_sign'])
        assert key not in cases
        cases[key] = inspect(path.parent, result, evidence)

    assert set(cases) == {(nose_up, sign) for nose_up in (False, True) for sign in (-1, 1)}
    checkpoint = json.loads((evidence / 'checkpoint.json').read_text())

    for name, identity in checkpoint.items():
        assert base.digest(base.ROOT / name) == identity, 'Mapped source anchor changed: ' + name

    origin = json.loads((evidence / 'native-origin.json').read_text())
    assert origin['px4_sha256'] == base.digest(evidence / 'sitl-lockstep-build/bin/px4')
    assert origin['plugin_sha256'] == base.digest(evidence / 'plugin-build/libresearch_rotational_fixture.so')
    assert origin['parent_manifest_sha256'] == base.digest(Path(origin['parent']) / 'campaign-manifest.json')
    (evidence / 'mixed-summary.json').write_text(json.dumps(dict(
        scope='four_unequal_signed_body_rate_reversal_release_and_impulse_observations',
        vehicle_qualification=False, campaign_acceptance=False, observed_checks_pass=True,
        audited_control_cycles=sum(case['cycles'] for case in cases.values()),
        cases=list(cases.values())), indent=2) + '\n')
    shutil.copy2(Path(__file__), evidence / Path(__file__).name)
    files = {}

    for path in evidence.iterdir():
        if path.is_file() and path.name != 'mixed-manifest.json':
            files[path.name] = base.digest(path)
        elif path.is_dir() and path.name.startswith(('mixed-', 'tested-artifacts')):
            for artifact in path.rglob('*'):
                if artifact.is_file() and not artifact.is_symlink():
                    files[str(artifact.relative_to(evidence))] = base.digest(artifact)

    for name in ('sitl-lockstep-build/bin/px4', 'sitl-lockstep-build/boardconfig',
                 'sitl-lockstep-build/compile_commands.json', 'plugin-build/libresearch_rotational_fixture.so'):
        files[name] = base.digest(evidence / name)

    (evidence / 'mixed-manifest.json').write_text(json.dumps(dict(
        scope='four_source_bound_exploratory_mixed_axis_cases', accepted=False,
        observed_checks_pass=True, vehicle_qualification=False,
        preserved_anchor_count=len(checkpoint), files=files), indent=2) + '\n')
    print(json.dumps(dict(manifest_sha256=base.digest(evidence / 'mixed-manifest.json'),
                          files=len(files), cycles=sum(case['cycles'] for case in cases.values()),
                          preserved_anchors=len(checkpoint)), indent=2), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--evidence', type=Path, required=True)
    args = parser.parse_args()
    evidence = args.evidence.resolve()
    assert evidence.parent == Path('/var/tmp') and evidence.name.startswith('research-stabilized-sitl-')
    seal(evidence)


if __name__ == '__main__':
    main()
