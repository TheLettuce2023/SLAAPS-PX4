#!/usr/bin/env python3
"""Audit exact raw-IMU-delay mixed-axis SITL observations from closed logs."""
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


EXPECTED = Counter({(True, 0): 1, (True, 20000): 1, (True, 40000): 1,
                    (True, 80000): 1, (False, 0): 1, (False, 40000): 1,
                    (False, 80000): 1})
DIRECT_TOPIC = '/world/stabilized_fixture/model/slaaps_fixture/link/base_link/sensor/imu_sensor/imu'
RAW_TOPIC = '/model/slaaps_fixture/imu_raw'
VARIANT_INPUTS = {'RotationalFixture.cpp', 'world.sdf.in'}


def inspect(work, result, evidence):
    delay = result['imu_transport_delay_us']
    assert result['seed'] == 42 and result['first_sign'] == -1
    assert result['manual_send_period_s'] == 0.02
    assert result['magnetic_transport_delay_us'] == 0
    physical = mixed_audit.inspect(work, result, evidence, expected_imu_delay_us=delay)
    world = ET.parse(work / 'world.sdf').getroot()
    plugin_delay = world.find('.//plugin[@name="research_stabilized_fixture::RotationalFixture"]'
                              '/imu_transport_delay_us')
    sensor_topic = world.find('.//sensor[@name="imu_sensor"]/topic')
    assert plugin_delay is not None and int(plugin_delay.text) == delay
    assert sensor_topic is not None and sensor_topic.text == (RAW_TOPIC if delay else DIRECT_TOPIC)
    control = campaigns.rows(work / 'control.csv')
    ages = [int(row['time']) - int(row['rate_sample']) for row in control]
    assert min(ages) == delay and max(ages) == delay + 4000
    assert max(ages) < 100000

    imu_log = work / 'truth.csv.imu.csv'
    count = 0
    if delay:
        packets = campaigns.rows(imu_log)
        assert len(packets) >= 7500
        previous_sample = 0
        previous_delivery = 0
        for packet in packets:
            sample = int(packet['sample_time'])
            delivery = int(packet['delivery_time'])
            assert sample > previous_sample and delivery > previous_delivery
            assert delivery - sample == delay
            assert all(math.isfinite(float(packet[name])) for name in
                       ('gyro_x', 'gyro_y', 'gyro_z', 'accel_x', 'accel_y', 'accel_z'))
            previous_sample, previous_delivery = sample, delivery
        count = len(packets)
    else:
        assert not imu_log.exists(), 'Direct IMU path unexpectedly traversed relay'
    return dict(**physical, declared_imu_delay_us=delay,
                selected_rate_age_min_us=min(ages), selected_rate_age_max_us=max(ages),
                relayed_imu_packets=count)


def seal(evidence):
    paths = sorted(evidence.glob('mixed-*/result.json'))
    assert len(paths) == sum(EXPECTED.values()), 'Require exact declared IMU timing attempts'
    results = [(path.parent, json.loads(path.read_text())) for path in paths]
    assert Counter((result['nose_up'], result['imu_transport_delay_us'])
                   for _, result in results) == EXPECTED
    checkpoint = json.loads((evidence / 'checkpoint.json').read_text())
    changed_after_prepare = {}
    for name, identity in checkpoint.items():
        current = base.digest(base.ROOT / name)
        if name == 'Tools/research_sitl/seal_stabilized_mixed.py':
            assert current != identity
            changed_after_prepare[name] = dict(preparation_sha256=identity, audit_sha256=current)
        else:
            assert current == identity, 'Unrelated graph anchor changed: ' + name

    origin = json.loads((evidence / 'native-origin.json').read_text())
    parent = Path(origin['parent'])
    assert origin['parent_manifest_sha256'] == base.digest(parent / 'campaign-manifest.json')
    assert origin['px4_sha256'] == base.digest(parent / 'sitl-lockstep-build/bin/px4')
    assert origin['px4_sha256'] == base.digest(evidence / 'sitl-lockstep-build/bin/px4')
    assert origin['plugin_sha256'] == base.digest(evidence / 'plugin-build/libresearch_rotational_fixture.so')
    assert set(origin['variant_inputs']) == VARIANT_INPUTS
    for path in base.FIXTURE.rglob('*'):
        if path.is_file():
            relative = path.relative_to(base.ROOT)
            assert base.digest(path) == base.digest(evidence / 'px4-source' / relative)
    for name, hashes in origin['variant_inputs'].items():
        relative = base.FIXTURE.relative_to(base.ROOT) / name
        assert hashes['parent_sha256'] == base.digest(parent / 'px4-source' / relative)
        assert hashes['variant_sha256'] == base.digest(evidence / 'px4-source' / relative)
        assert hashes['parent_sha256'] != hashes['variant_sha256']

    cases = [inspect(work, result, evidence) for work, result in results]
    (evidence / 'imu-delay-summary.json').write_text(json.dumps(dict(
        scope='seven_exact_exploratory_raw_imu_delay_mixed_axis_observations',
        accepted_campaign_expansion=False, vehicle_qualification=False,
        audited_control_cycles=sum(case['cycles'] for case in cases),
        preparation_to_audit_script_changes=changed_after_prepare,
        cases=cases), indent=2) + '\n')
    shutil.copy2(Path(__file__), evidence / Path(__file__).name)
    files = {}
    for path in evidence.iterdir():
        if path.is_file() and path.name != 'imu-delay-manifest.json':
            files[path.name] = base.digest(path)
        elif path.is_dir() and path.name.startswith(('mixed-', 'tested-artifacts', 'px4-source')):
            for artifact in path.rglob('*'):
                if artifact.is_file() and not artifact.is_symlink():
                    files[str(artifact.relative_to(evidence))] = base.digest(artifact)
    for name in ('sitl-lockstep-build/bin/px4', 'sitl-lockstep-build/boardconfig',
                 'sitl-lockstep-build/compile_commands.json', 'plugin-build/libresearch_rotational_fixture.so'):
        files[name] = base.digest(evidence / name)
    (evidence / 'imu-delay-manifest.json').write_text(json.dumps(dict(
        scope='seven_source_bound_exploratory_raw_imu_transport_delay_cases', accepted=False,
        vehicle_qualification=False, observed_checks_pass=True,
        preserved_preparation_anchor_count=len(checkpoint), files=files), indent=2) + '\n')
    print(json.dumps(dict(manifest_sha256=base.digest(evidence / 'imu-delay-manifest.json'),
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
