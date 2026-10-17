#!/usr/bin/env python3
"""Audit four mixed-axis SITL cases with observed bounded pilot-send timing."""
import argparse
import bisect
from collections import Counter
import json
import math
from pathlib import Path
import shutil
import xml.etree.ElementTree as ET

import seal_stabilized_mixed as mixed_audit
import verify_stabilized_campaigns as campaigns
import verify_stabilized_phased_jitter as phased
import verify_stabilized_sitl as base


EXPECTED = Counter({(nose_up, sign): 1 for nose_up in (False, True) for sign in (-1, 1)})
MAX_SEND_LATENESS_US = 12000
MIN_OBSERVED_INTERVAL_US = 68000
MAX_OBSERVED_INTERVAL_US = 92000
MAX_SAMPLE_TO_SEND_SKEW_US = 12000


def inspect(work, result, evidence):
    assert result['phased_source_sha256'] == base.digest(Path(phased.__file__))
    assert result['phased_source_sha256'] == base.digest(work / Path(phased.__file__).name)
    assert result['phased_intervals_us'] == list(phased.SEND_INTERVALS_US)
    assert result['phase_first_send_offset_us'] == phased.FIRST_SEND_AFTER_ARM_US
    assert result['startup_sender_wall_period_s'] == phased.MANUAL_PERIOD_S
    assert result['phase_clock'] == 'flushed_250hz_raw_imu_delivery_log'
    physical = mixed_audit.inspect(work, result, evidence,
                                   expected_imu_delay_us=phased.IMU_DELAY_US,
                                   expected_manual_period_s=phased.MANUAL_PERIOD_S)
    world = ET.parse(work / 'world.sdf').getroot()
    plugin_delay = world.find('.//plugin[@name="research_stabilized_fixture::RotationalFixture"]'
                              '/imu_transport_delay_us')
    sensor_topic = world.find('.//sensor[@name="imu_sensor"]/topic')
    assert plugin_delay is not None and int(plugin_delay.text) == phased.IMU_DELAY_US
    assert sensor_topic is not None and sensor_topic.text == '/model/slaaps_fixture/imu_raw'
    packets = campaigns.rows(work / 'truth.csv.imu.csv')
    assert len(packets) >= 7500
    previous_sample = 0
    for packet in packets:
        sample = int(packet['sample_time'])
        assert sample > previous_sample
        assert int(packet['delivery_time']) - sample == phased.IMU_DELAY_US
        assert all(math.isfinite(float(packet[name])) for name in
                   ('gyro_x', 'gyro_y', 'gyro_z', 'accel_x', 'accel_y', 'accel_z'))
        previous_sample = sample
    trace = campaigns.rows(work / 'manual-send.csv')
    assert len(trace) >= 250, 'Insufficient logged phase-controlled sends'
    targets = [int(row['target_sim_time']) for row in trace]
    actual = [int(row['observed_sim_time']) for row in trace]
    assert targets[0] == result['phase_anchor_us'] + phased.FIRST_SEND_AFTER_ARM_US
    assert [int(row['index']) for row in trace] == list(range(len(trace)))
    target_intervals = [right - left for left, right in zip(targets, targets[1:])]
    assert all(delta == phased.SEND_INTERVALS_US[i % len(phased.SEND_INTERVALS_US)]
               for i, delta in enumerate(target_intervals))
    lateness = [stamp - target for stamp, target in zip(actual, targets)]
    assert min(lateness) >= 0 and max(lateness) <= MAX_SEND_LATENESS_US
    intervals = [right - left for left, right in zip(actual, actual[1:])]
    assert min(intervals) >= MIN_OBSERVED_INTERVAL_US
    assert max(intervals) <= MAX_OBSERVED_INTERVAL_US
    assert not (work / 'sender-error.txt').exists()

    control = campaigns.rows(work / 'control.csv')
    manual_all = [int(row['time']) - int(row['manual_sample']) for row in control]
    rate_ages = [int(row['time']) - int(row['rate_sample']) for row in control]
    assert max(manual_all) == result['manual_sample_age_max_us'] < 100000
    assert min(rate_ages) == phased.IMU_DELAY_US and max(rate_ages) <= 100000
    selected = sorted({int(row['manual_sample']) for row in control
                       if int(row['time']) >= targets[0] + 200000})
    assert len(selected) >= 200
    skew = []
    for sample in selected:
        index = bisect.bisect_left(actual, sample)
        candidates = actual[max(0, index - 1):index + 1]
        assert candidates
        skew.append(min(abs(sample - sent) for sent in candidates))
    assert max(skew) <= MAX_SAMPLE_TO_SEND_SKEW_US
    phase_control = [row for row in control if int(row['time']) >= targets[0] + 200000]
    manual_ages = [int(row['time']) - int(row['manual_sample']) for row in phase_control]
    assert max(manual_ages) < 100000
    return dict(**physical, scheduled_sends=len(trace),
                manual_age_max_us=max(manual_all),
                rate_age_min_us=min(rate_ages), rate_age_max_us=max(rate_ages),
                rate_age_above_84ms_cycles=sum(age > 84000 for age in rate_ages),
                relayed_imu_packets=len(packets),
                scheduled_interval_min_us=min(target_intervals),
                scheduled_interval_max_us=max(target_intervals),
                observed_interval_min_us=min(intervals),
                observed_interval_max_us=max(intervals),
                maximum_send_lateness_us=max(lateness),
                maximum_sample_to_send_skew_us=max(skew),
                maximum_phase_manual_age_us=max(manual_ages))


def seal(evidence):
    paths = sorted(evidence.glob('mixed-*/result.json'))
    assert len(paths) == sum(EXPECTED.values())
    results = [(path.parent, json.loads(path.read_text())) for path in paths]
    assert Counter((result['nose_up'], result['first_sign'])
                   for _, result in results) == EXPECTED
    checkpoint = json.loads((evidence / 'checkpoint.json').read_text())
    for name, identity in checkpoint.items():
        current = base.digest(base.ROOT / name)
        if name == 'Tools/research_sitl/seal_stabilized_phased_jitter.py':
            assert current != identity, 'Boundary-aware audit extension was not source-bound'
        else:
            assert current == identity, 'Unrelated mapped source changed: ' + name
    origin = json.loads((evidence / 'native-origin.json').read_text())
    parent = Path(origin['parent'])
    for name, identity in origin['parent_receipts'].items():
        assert base.digest(parent / name) == identity
    for name, identity in (('sitl-lockstep-build/bin/px4', 'px4_sha256'),
                           ('plugin-build/libresearch_rotational_fixture.so', 'plugin_sha256')):
        assert base.digest(parent / name) == origin[identity]
        assert base.digest(evidence / name) == origin[identity]
    for path in base.FIXTURE.rglob('*'):
        if path.is_file():
            relative = path.relative_to(base.ROOT)
            assert base.digest(path) == base.digest(evidence / 'px4-source' / relative)
    cases = [inspect(work, result, evidence) for work, result in results]
    (evidence / 'phased-jitter-summary.json').write_text(json.dumps(dict(
        scope='four_exact_mixed_axis_80ms_manual_80ms_imu_alternating_phase_cases',
        accepted_campaign_expansion=False, vehicle_qualification=False,
        audited_control_cycles=sum(case['cycles'] for case in cases),
        cases=cases), indent=2) + '\n')
    shutil.copy2(Path(__file__), evidence / Path(__file__).name)
    files = {}
    for path in evidence.iterdir():
        if path.is_file() and path.name != 'phased-jitter-manifest.json':
            files[path.name] = base.digest(path)
        elif path.is_dir() and path.name.startswith(('mixed-', 'tested-artifacts', 'px4-source')):
            for artifact in path.rglob('*'):
                if artifact.is_file() and not artifact.is_symlink():
                    files[str(artifact.relative_to(evidence))] = base.digest(artifact)
    for name in ('sitl-lockstep-build/bin/px4', 'sitl-lockstep-build/boardconfig',
                 'sitl-lockstep-build/compile_commands.json',
                 'plugin-build/libresearch_rotational_fixture.so'):
        files[name] = base.digest(evidence / name)
    (evidence / 'phased-jitter-manifest.json').write_text(json.dumps(dict(
        scope='four_source_bound_exploratory_phased_pilot_send_cases',
        accepted=False, vehicle_qualification=False, observed_checks_pass=True,
        preserved_preparation_anchor_count=len(checkpoint) - 1,
        sealer_preparation_sha256=checkpoint['Tools/research_sitl/seal_stabilized_phased_jitter.py'],
        sealer_sha256=base.digest(Path(__file__)), files=files), indent=2) + '\n')
    print(json.dumps(dict(manifest_sha256=base.digest(evidence / 'phased-jitter-manifest.json'),
                          files=len(files), cycles=sum(case['cycles'] for case in cases),
                          preserved_preparation_anchors=len(checkpoint) - 1), indent=2), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--evidence', type=Path, required=True)
    args = parser.parse_args()
    evidence = args.evidence.resolve()
    assert evidence.parent == Path('/var/tmp') and evidence.name.startswith('research-stabilized-sitl-')
    seal(evidence)


if __name__ == '__main__':
    main()
