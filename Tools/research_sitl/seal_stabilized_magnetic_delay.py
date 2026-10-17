#!/usr/bin/env python3
"""Independently audit six exact exploratory magnetic-transport-delay SITL runs."""
import argparse
from collections import Counter
import json
from pathlib import Path
import shutil
import xml.etree.ElementTree as ET

import seal_stabilized_mixed as mixed_audit
import verify_stabilized_campaigns as campaigns
import verify_stabilized_sitl as base


EXPECTED = Counter({(True, 0): 1, (True, 20000): 1, (True, 40000): 1,
                    (False, 0): 1, (False, 40000): 2})
PREPARED_BEFORE_RUNNER_EDIT = {
    'Tools/research_sitl/verify_stabilized_mixed.py',
    'Tools/research_sitl/seal_stabilized_mixed.py',
    'Tools/research_sitl/seal_stabilized_transport.py',
}
VARIANT_INPUTS = {'RotationalFixture.cpp', 'world.sdf.in'}


def inspect(work, result, evidence):
    delay = result['magnetic_transport_delay_us']
    assert result['seed'] == 42 and result['first_sign'] == -1
    assert result['manual_send_period_s'] == 0.02
    assert result.get('imu_transport_delay_us', 0) == 0, 'Changed IMU delay requires separate audit'
    physical = mixed_audit.inspect(work, result, evidence, expected_magnetic_delay_us=delay)
    world = ET.parse(work / 'world.sdf').getroot()
    declared = world.find('.//plugin[@name="research_stabilized_fixture::RotationalFixture"]'
                          '/magnetic_transport_delay_us')
    assert declared is not None and int(declared.text) == delay
    packets = campaigns.rows(work / 'truth.csv.mag.csv')
    assert len(packets) >= 2500
    actual = []
    previous_sample = 0
    previous_delivery = 0
    for packet in packets:
        sample = int(packet['time'])
        delivery = int(packet['delivery_time'])
        assert sample > previous_sample and delivery > previous_delivery
        previous_sample, previous_delivery = sample, delivery
        actual.append(delivery - sample)
        assert abs(float(packet['packet_x']) - float(packet['flu_y_t']) * 1e4) < 1e-10
        assert abs(float(packet['packet_y']) + float(packet['flu_x_t']) * 1e4) < 1e-10
        assert abs(float(packet['packet_z']) + float(packet['flu_z_t']) * 1e4) < 1e-10
    # With this 4 ms lockstep fixture, zero additional delay reaches the
    # PreUpdate relay on the next tick. The positive settings are exact.
    assert set(actual) == {4000 if delay == 0 else delay}
    return dict(**physical, declared_magnetic_delay_us=delay,
                actual_magnetic_delivery_delay_us=actual[0], magnetic_packets=len(actual))


def seal(evidence):
    paths = sorted(evidence.glob('mixed-*/result.json'))
    assert len(paths) == sum(EXPECTED.values()), 'Require every exact delay attempt, including repeat'
    results = [(path.parent, json.loads(path.read_text())) for path in paths]
    assert Counter((result['nose_up'], result['magnetic_transport_delay_us'])
                   for _, result in results) == EXPECTED
    checkpoint = json.loads((evidence / 'checkpoint.json').read_text())
    changed_after_prepare = {}
    for name, identity in checkpoint.items():
        current = base.digest(base.ROOT / name)
        if name in PREPARED_BEFORE_RUNNER_EDIT:
            changed_after_prepare[name] = dict(preparation_sha256=identity, run_sha256=current)
        else:
            assert current == identity, 'Unrelated graph anchor changed: ' + name
    assert all(before['preparation_sha256'] != before['run_sha256']
               for before in changed_after_prepare.values())

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
    (evidence / 'magnetic-delay-summary.json').write_text(json.dumps(dict(
        scope='six_exact_exploratory_magnetic_packet_delay_mixed_axis_observations',
        accepted_campaign_expansion=False, vehicle_qualification=False,
        audited_control_cycles=sum(case['cycles'] for case in cases),
        preparation_to_run_script_changes=changed_after_prepare, cases=cases), indent=2) + '\n')
    shutil.copy2(Path(__file__), evidence / Path(__file__).name)
    files = {}
    for path in evidence.iterdir():
        if path.is_file() and path.name != 'magnetic-delay-manifest.json':
            files[path.name] = base.digest(path)
        elif path.is_dir() and path.name.startswith(('mixed-', 'tested-artifacts', 'px4-source')):
            for artifact in path.rglob('*'):
                if artifact.is_file() and not artifact.is_symlink():
                    files[str(artifact.relative_to(evidence))] = base.digest(artifact)
    for name in ('sitl-lockstep-build/bin/px4', 'sitl-lockstep-build/boardconfig',
                 'sitl-lockstep-build/compile_commands.json', 'plugin-build/libresearch_rotational_fixture.so'):
        files[name] = base.digest(evidence / name)
    (evidence / 'magnetic-delay-manifest.json').write_text(json.dumps(dict(
        scope='six_source_bound_exploratory_magnetic_transport_delay_cases', accepted=False,
        vehicle_qualification=False, observed_checks_pass=True,
        preserved_preparation_anchor_count=len(checkpoint), files=files), indent=2) + '\n')
    print(json.dumps(dict(manifest_sha256=base.digest(evidence / 'magnetic-delay-manifest.json'),
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
