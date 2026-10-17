#!/usr/bin/env python3
"""Audit exact exploratory manual-input cadence runs against closed SITL logs."""
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


EXPECTED = {(True, 0.06), (True, 0.08), (False, 0.08)}


def inspect(work, result, evidence):
    assert result['exploratory_only'] and result['mixed_axes'] and result['first_sign'] == -1
    assert result.get('magnetic_transport_delay_us', 0) == 0, 'Changed magnetic delay requires separate audit'
    assert result.get('imu_transport_delay_us', 0) == 0, 'Changed IMU delay requires separate audit'
    assert result['mixed_source_sha256'] == base.digest(Path(mixed.__file__))
    assert result['mixed_source_sha256'] == base.digest(work / Path(mixed.__file__).name)
    assert result['px4_sha256'] == base.digest(evidence / 'sitl-lockstep-build/bin/px4')
    assert result['plugin_sha256'] == base.digest(evidence / 'plugin-build/libresearch_rotational_fixture.so')
    audit.audit_runtime_profile(work, result)

    for name, identity in result['source_sha256'].items():
        assert base.digest(work / 'source-inputs' / name) == identity
        assert base.digest(base.ROOT / name) == identity

    cycles = audit.audit_cycles(work / 'control.csv')
    assert cycles == result['audited_control_cycles']
    control = campaigns.rows(work / 'control.csv')
    truth = campaigns.rows(work / 'truth.csv')
    ages = sorted(int(row['time']) - int(row['manual_sample']) for row in control)
    assert ages[-1] == result['manual_sample_age_max_us'] < 100000
    assert ages[int(.95 * (len(ages) - 1))] == result['manual_sample_age_p95_us']
    assert ages[-1] > 24000, 'Cadence perturbation was not visible in selected input'
    rate = [row for row in control if row['state'] == '2']
    times = [int(row['time']) for row in truth]
    physical_rate_error = 0.0

    for phase in (-1, 1):
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
                physical_rate_error = max(physical_rate_error, abs(float(truth[index]['w' + axis]) - target))

    release = next(row for row in control if row['state'] == '3' and int(row['time']) > int(rate[-1]['time']))
    reference = campaigns.quaternion(release, 'r')
    physical_reference = campaigns.quaternion(min(truth, key=lambda row:
                                                  abs(int(row['time']) - int(release['att_sample']))))
    final = min(truth, key=lambda row: abs(int(row['time']) - int(control[-1]['time'])))
    final_physical = campaigns.separation(campaigns.quaternion(final), physical_reference)
    final_estimated = campaigns.separation(campaigns.quaternion(control[-1]), reference)
    final_rate = math.sqrt(sum(float(final['w' + axis]) ** 2 for axis in 'xyz'))
    peak = max(campaigns.separation(campaigns.quaternion(row), physical_reference) for row in truth
               if int(release['att_sample']) <= int(row['time']) <= int(control[-1]['time']))
    assert abs(physical_rate_error - result['maximum_physical_rate_error_rad_s']) < 1e-7
    assert abs(final_physical - result['final_physical_error_rad']) < 1e-7
    assert abs(final_estimated - result['final_estimated_error_rad']) < 1e-7
    assert abs(final_rate - result['final_rate_rad_s']) < 1e-7
    assert abs(peak - result['peak_physical_error_rad']) < 1e-7
    assert result['passed'] and all(result['checks'].values())
    assert physical_rate_error < campaigns.RATE_TOLERANCE
    assert final_physical < campaigns.FINAL_ANGLE_LIMIT and final_estimated < campaigns.FINAL_ANGLE_LIMIT
    assert final_rate < campaigns.FINAL_RATE_LIMIT and peak < campaigns.PEAK_ANGLE_LIMIT
    assert truth[-1]['thrust'] == '0' and truth[-1]['lease_fault'] == '1'
    return dict(directory=work.name, nose_up=result['nose_up'], manual_send_period_s=result['manual_send_period_s'],
                cycles=cycles, manual_sample_age_p95_us=result['manual_sample_age_p95_us'],
                manual_sample_age_max_us=ages[-1], maximum_physical_rate_error_rad_s=physical_rate_error,
                final_physical_error_rad=final_physical, peak_physical_error_rad=peak,
                observed_checks_pass=True)


def seal(evidence):
    paths = sorted(evidence.glob('mixed-*/result.json'))
    assert len(paths) == len(EXPECTED), 'Require exact declared cadence attempts'
    cases = {}

    for path in paths:
        result = json.loads(path.read_text())
        key = (result['nose_up'], result['manual_send_period_s'])
        assert key not in cases
        cases[key] = inspect(path.parent, result, evidence)

    assert set(cases) == EXPECTED
    checkpoint = json.loads((evidence / 'checkpoint.json').read_text())

    for name, identity in checkpoint.items():
        assert base.digest(base.ROOT / name) == identity

    origin = json.loads((evidence / 'native-origin.json').read_text())
    assert origin['px4_sha256'] == base.digest(evidence / 'sitl-lockstep-build/bin/px4')
    assert origin['plugin_sha256'] == base.digest(evidence / 'plugin-build/libresearch_rotational_fixture.so')
    assert origin['parent_manifest_sha256'] == base.digest(Path(origin['parent']) / 'campaign-manifest.json')
    (evidence / 'transport-summary.json').write_text(json.dumps(dict(
        scope='three_exact_slower_manual_input_cadence_mixed_axis_observations',
        vehicle_qualification=False, campaign_acceptance=False, observed_checks_pass=True,
        audited_control_cycles=sum(case['cycles'] for case in cases.values()),
        cases=list(cases.values())), indent=2) + '\n')
    shutil.copy2(Path(__file__), evidence / Path(__file__).name)
    files = {}

    for path in evidence.iterdir():
        if path.is_file() and path.name != 'transport-manifest.json':
            files[path.name] = base.digest(path)
        elif path.is_dir() and path.name.startswith(('mixed-', 'tested-artifacts')):
            for artifact in path.rglob('*'):
                if artifact.is_file() and not artifact.is_symlink():
                    files[str(artifact.relative_to(evidence))] = base.digest(artifact)

    for name in ('sitl-lockstep-build/bin/px4', 'sitl-lockstep-build/boardconfig',
                 'sitl-lockstep-build/compile_commands.json', 'plugin-build/libresearch_rotational_fixture.so'):
        files[name] = base.digest(evidence / name)

    (evidence / 'transport-manifest.json').write_text(json.dumps(dict(
        scope='three_source_bound_exploratory_manual_link_cadence_cases', accepted=False,
        observed_checks_pass=True, vehicle_qualification=False,
        preserved_anchor_count=len(checkpoint), files=files), indent=2) + '\n')
    print(json.dumps(dict(manifest_sha256=base.digest(evidence / 'transport-manifest.json'),
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
