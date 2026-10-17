#!/usr/bin/env python3
"""Audit four simultaneous pilot-cadence and raw-IMU-delay SITL maneuvers."""
import argparse
from collections import Counter
import json
import math
from pathlib import Path
import shutil
import xml.etree.ElementTree as ET

import seal_stabilized_mixed as mixed_audit
import verify_stabilized_campaigns as campaigns
import verify_stabilized_sitl as base


EXPECTED = Counter({(nose_up, sign): 1 for nose_up in (False, True) for sign in (-1, 1)})
RAW_TOPIC = '/model/slaaps_fixture/imu_raw'


def inspect(work, result, evidence, expected_seed=42):
    assert result['seed'] == expected_seed and result['magnetic_transport_delay_us'] == 0
    physical = mixed_audit.inspect(work, result, evidence, expected_imu_delay_us=80000,
                                   expected_manual_period_s=0.08)
    world = ET.parse(work / 'world.sdf').getroot()
    plugin_delay = world.find('.//plugin[@name="research_stabilized_fixture::RotationalFixture"]'
                              '/imu_transport_delay_us')
    sensor_topic = world.find('.//sensor[@name="imu_sensor"]/topic')
    assert plugin_delay is not None and int(plugin_delay.text) == 80000
    assert sensor_topic is not None and sensor_topic.text == RAW_TOPIC
    control = campaigns.rows(work / 'control.csv')
    manual_ages = sorted(int(row['time']) - int(row['manual_sample']) for row in control)
    rate_ages = [int(row['time']) - int(row['rate_sample']) for row in control]
    assert manual_ages[-1] == result['manual_sample_age_max_us'] < 100000
    assert manual_ages[int(0.95 * (len(manual_ages) - 1))] == result['manual_sample_age_p95_us']
    assert min(rate_ages) == 80000 and max(rate_ages) == 84000
    packets = campaigns.rows(work / 'truth.csv.imu.csv')
    assert len(packets) >= 7500
    previous_sample = 0
    for packet in packets:
        sample = int(packet['sample_time'])
        assert sample > previous_sample and int(packet['delivery_time']) - sample == 80000
        assert all(math.isfinite(float(packet[name])) for name in
                   ('gyro_x', 'gyro_y', 'gyro_z', 'accel_x', 'accel_y', 'accel_z'))
        previous_sample = sample
    return dict(**physical, manual_age_p95_us=result['manual_sample_age_p95_us'],
                manual_age_max_us=manual_ages[-1], rate_age_min_us=min(rate_ages),
                rate_age_max_us=max(rate_ages), relayed_imu_packets=len(packets))


def seal(evidence):
    paths = sorted(evidence.glob('mixed-*/result.json'))
    assert len(paths) == sum(EXPECTED.values()), 'Require the exact four combined-timing attempts'
    results = [(path.parent, json.loads(path.read_text())) for path in paths]
    assert Counter((result['nose_up'], result['first_sign']) for _, result in results) == EXPECTED
    checkpoint = json.loads((evidence / 'checkpoint.json').read_text())
    for name, identity in checkpoint.items():
        current = base.digest(base.ROOT / name)
        if name == 'Tools/research_sitl/seal_stabilized_mixed.py':
            assert current != identity, 'Mixed audit extension was not source-bound'
        else:
            assert current == identity, 'Unrelated graph anchor changed: ' + name
    origin = json.loads((evidence / 'native-origin.json').read_text())
    parent = Path(origin['parent'])
    for name, identity in origin['parent_receipts'].items():
        assert base.digest(parent / name) == identity
    assert origin['px4_sha256'] == base.digest(parent / 'sitl-lockstep-build/bin/px4')
    assert origin['plugin_sha256'] == base.digest(parent / 'plugin-build/libresearch_rotational_fixture.so')
    assert origin['px4_sha256'] == base.digest(evidence / 'sitl-lockstep-build/bin/px4')
    assert origin['plugin_sha256'] == base.digest(evidence / 'plugin-build/libresearch_rotational_fixture.so')
    for path in base.FIXTURE.rglob('*'):
        if path.is_file():
            relative = path.relative_to(base.ROOT)
            assert base.digest(path) == base.digest(evidence / 'px4-source' / relative)
    cases = [inspect(work, result, evidence) for work, result in results]
    (evidence / 'combined-timing-summary.json').write_text(json.dumps(dict(
        scope='four_exact_80ms_manual_cadence_and_80ms_raw_imu_delay_mixed_axis_observations',
        accepted_campaign_expansion=False, vehicle_qualification=False,
        audited_control_cycles=sum(case['cycles'] for case in cases),
        cases=cases), indent=2) + '\n')
    shutil.copy2(Path(__file__), evidence / Path(__file__).name)
    files = {}
    for path in evidence.iterdir():
        if path.is_file() and path.name != 'combined-timing-manifest.json':
            files[path.name] = base.digest(path)
        elif path.is_dir() and path.name.startswith(('mixed-', 'tested-artifacts', 'px4-source')):
            for artifact in path.rglob('*'):
                if artifact.is_file() and not artifact.is_symlink():
                    files[str(artifact.relative_to(evidence))] = base.digest(artifact)
    for name in ('sitl-lockstep-build/bin/px4', 'sitl-lockstep-build/boardconfig',
                 'sitl-lockstep-build/compile_commands.json', 'plugin-build/libresearch_rotational_fixture.so'):
        files[name] = base.digest(evidence / name)
    (evidence / 'combined-timing-manifest.json').write_text(json.dumps(dict(
        scope='four_source_bound_exploratory_combined_manual_and_imu_timing_cases', accepted=False,
        vehicle_qualification=False, observed_checks_pass=True,
        preserved_preparation_anchor_count=len(checkpoint), files=files), indent=2) + '\n')
    print(json.dumps(dict(manifest_sha256=base.digest(evidence / 'combined-timing-manifest.json'),
                          files=len(files), cycles=sum(case['cycles'] for case in cases),
                          preserved_preparation_anchors=len(checkpoint)), indent=2), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--evidence', type=Path, required=True)
    args = parser.parse_args()
    evidence = args.evidence.resolve()
    assert evidence.parent == Path('/var/tmp') and evidence.name.startswith('research-stabilized-sitl-')
    seal(evidence)


if __name__ == '__main__':
    main()
