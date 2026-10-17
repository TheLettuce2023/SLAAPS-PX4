#!/usr/bin/env python3
"""Audit four single-packet-loss faults under phase-controlled pilot timing."""
import argparse
import bisect
from collections import Counter
import json
import math
from pathlib import Path
import shutil
import xml.etree.ElementTree as ET

import seal_stabilized_campaigns as audit
import verify_stabilized_campaigns as campaigns
import verify_stabilized_mixed as mixed
import verify_stabilized_phased_jitter as phased
import verify_stabilized_phased_skip as skip
import verify_stabilized_sitl as base


EXPECTED = Counter({(nose_up, mode): 1 for nose_up in (False, True)
                    for mode in ('hold', 'rate')})


def inspect(work, result, evidence):
    assert result['passed'] and result['exploratory_only']
    assert result['case'] == 'phased_single_skip' and result['seed'] == 42
    assert result['skip_mode'] in ('hold', 'rate') and result['single_skip_index'] == skip.SKIP_INDEX
    assert result['observed_fault_expected'] == 5
    assert result['manual_send_period_s'] == phased.MANUAL_PERIOD_S
    assert result['imu_transport_delay_us'] == phased.IMU_DELAY_US
    assert result['magnetic_transport_delay_us'] == 0
    for key, module in (('phased_source_sha256', phased), ('single_skip_source_sha256', skip)):
        assert result[key] == base.digest(Path(module.__file__))
        assert result[key] == base.digest(work / Path(module.__file__).name)
    assert result['phased_intervals_us'] == list(phased.SEND_INTERVALS_US)
    assert result['phase_clock'] == 'flushed_250hz_raw_imu_delivery_log'
    assert result['px4_sha256'] == base.digest(evidence / 'sitl-lockstep-build/bin/px4')
    assert result['plugin_sha256'] == base.digest(evidence / 'plugin-build/libresearch_rotational_fixture.so')
    assert 'state=4' in result['fault_status'] and 'fault=5' in result['fault_status']
    assert 'state=3' in result['restart_status'] and 'fault=0' in result['restart_status']
    assert result['reenable_returncode'] != 0
    cycles = audit.audit_case(work, result)

    world = ET.parse(work / 'world.sdf').getroot()
    plugin_delay = world.find('.//plugin[@name="research_stabilized_fixture::RotationalFixture"]'
                              '/imu_transport_delay_us')
    sensor_topic = world.find('.//sensor[@name="imu_sensor"]/topic')
    assert plugin_delay is not None and int(plugin_delay.text) == phased.IMU_DELAY_US
    assert sensor_topic is not None and sensor_topic.text == '/model/slaaps_fixture/imu_raw'
    packets = campaigns.rows(work / 'truth.csv.imu.csv')
    assert len(packets) > 2000
    previous_sample = 0
    for packet in packets:
        sample = int(packet['sample_time'])
        assert sample > previous_sample
        assert int(packet['delivery_time']) - sample == phased.IMU_DELAY_US
        assert all(math.isfinite(float(packet[name])) for name in
                   ('gyro_x', 'gyro_y', 'gyro_z', 'accel_x', 'accel_y', 'accel_z'))
        previous_sample = sample

    sends = campaigns.rows(work / 'manual-send.csv')
    skipped = campaigns.rows(work / 'manual-skip.csv')
    assert len(sends) > 70 and len(skipped) == 1
    missing = skipped[0]
    assert int(missing['index']) == skip.SKIP_INDEX
    indexes = [int(row['index']) for row in sends]
    assert indexes == [i for i in range(indexes[-1] + 1) if i != skip.SKIP_INDEX]
    target0 = result['phase_anchor_us'] + phased.FIRST_SEND_AFTER_ARM_US
    targets = [target0]
    for i in range(indexes[-1]):
        targets.append(targets[-1] + phased.SEND_INTERVALS_US[i % 2])
    assert int(missing['target_sim_time']) == targets[skip.SKIP_INDEX]
    assert 0 <= int(missing['observed_sim_time']) - targets[skip.SKIP_INDEX] <= 12000
    actual = [int(row['observed_sim_time']) for row in sends]
    assert all(int(row['target_sim_time']) == targets[i] for i, row in zip(indexes, sends))
    assert all(0 <= observed - targets[i] <= 12000 for i, observed in zip(indexes, actual))
    intervals = [right - left for left, right in zip(actual, actual[1:])]
    skip_gap = intervals[skip.SKIP_INDEX - 1]
    assert 148000 <= skip_gap <= 172000
    assert all(68000 <= interval <= 92000 for i, interval in enumerate(intervals)
               if i != skip.SKIP_INDEX - 1)
    assert not (work / 'sender-error.txt').exists()

    control = campaigns.rows(work / 'control.csv')
    restarted = campaigns.rows(work / 'restarted.csv')
    truth = campaigns.rows(work / 'truth.csv')
    assert control and restarted
    last = control[-1]
    last_sample = int(last['manual_sample'])
    last_age = int(last['time']) - last_sample
    off_time = result['first_off_us']
    assert last_sample < targets[skip.SKIP_INDEX] <= off_time
    assert last_age <= 100000
    assert 100000 <= off_time - last_sample <= 120000
    assert all(int(row['time']) < off_time for row in control)
    assert result['new_session'] > result['old_session'] > 0
    actions = campaigns.rows(work / 'actions.csv')
    reenable = [int(row['sim_time']) for row in actions if row['action'] == 'explicit_reenable']
    assert len(reenable) == 1 and all(int(row['time']) >= reenable[0] for row in restarted)
    assert 0 <= result['first_recovery_us'] - int(restarted[0]['time']) <= 12000
    off = [row for row in truth if off_time <= int(row['time']) < result['first_recovery_us']]
    assert off and all(row['enabled'] == '0' and row['thrust'] == '0'
                       and row['lease_fault'] == '1' for row in off)
    assert all(all(float(row['t' + str(i)]) == 0 for i in range(4)) for row in off)
    assert truth[-1]['thrust'] == '0' and truth[-1]['lease_fault'] == '1'

    before_skip = [row for row in control if int(row['time']) < targets[skip.SKIP_INDEX]]
    assert before_skip
    physical_rate_error = None
    if result['skip_mode'] == 'hold':
        assert all(row['state'] == '3' for row in control)
        reference = campaigns.quaternion(before_skip[0], 'r')
        assert max(campaigns.separation(campaigns.quaternion(row, 'r'), reference)
                   for row in before_skip) < 1e-6
    else:
        rate = [row for row in before_skip if row['state'] == '2']
        assert len(rate) > 400
        raw = mixed.BODY_STICKS
        settled = [row for row in rate if targets[skip.SKIP_INDEX] - 500000 <= int(row['time'])
                   <= targets[skip.SKIP_INDEX] - 100000]
        assert len(settled) >= 50
        truth_times = [int(row['time']) for row in truth]
        errors = []
        for row in settled:
            assert abs(float(row['roll']) - raw[0]) < 1e-5
            assert abs(float(row['pitch']) + raw[1]) < 1e-5
            assert abs(float(row['yaw']) - raw[2]) < 1e-5
            stamp = int(row['rate_sample'])
            index = bisect.bisect_left(truth_times, stamp)
            nearest = min((max(0, index - 1), min(index, len(truth) - 1)),
                          key=lambda i: abs(truth_times[i] - stamp))
            assert abs(truth_times[nearest] - stamp) <= 4000
            for axis, stick in zip('xyz', raw):
                errors.append(abs(float(truth[nearest]['w' + axis]) - mixed.expected_rate(stick)))
        physical_rate_error = max(errors)
        assert physical_rate_error < campaigns.RATE_TOLERANCE

    selected = sorted({int(row['manual_sample']) for row in before_skip
                       if int(row['time']) >= target0 + 200000})
    assert len(selected) >= 35
    before_sends = actual[:skip.SKIP_INDEX]
    skew = []
    for sample in selected:
        index = bisect.bisect_left(before_sends, sample)
        candidates = before_sends[max(0, index - 1):index + 1]
        assert candidates
        skew.append(min(abs(sample - sent) for sent in candidates))
    assert max(skew) <= 12000
    rate_ages = [int(row['time']) - int(row['rate_sample']) for row in control]
    assert max(rate_ages) <= 100000
    return dict(directory=work.name, nose_up=result['nose_up'], mode=result['skip_mode'],
                cycles=cycles, skipped_index=skip.SKIP_INDEX,
                observed_single_gap_us=skip_gap, last_manual_age_us=last_age,
                off_after_last_manual_sample_us=off_time - last_sample,
                off_duration_us=result['first_recovery_us'] - off_time,
                maximum_selected_rate_age_us=max(rate_ages),
                maximum_sample_to_send_skew_us=max(skew),
                pre_skip_physical_rate_error_rad_s=physical_rate_error,
                relayed_imu_packets=len(packets), observed_checks_pass=True)


def seal(evidence):
    paths = sorted(evidence.glob('phased-skip-*/result.json'))
    assert len(paths) == sum(EXPECTED.values())
    results = [(path.parent, json.loads(path.read_text())) for path in paths]
    assert Counter((result['nose_up'], result['skip_mode'])
                   for _, result in results) == EXPECTED
    checkpoint = json.loads((evidence / 'checkpoint.json').read_text())
    for name, identity in checkpoint.items():
        assert base.digest(base.ROOT / name) == identity, 'Mapped source changed: ' + name
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
    (evidence / 'phased-skip-summary.json').write_text(json.dumps(dict(
        scope='four_exact_single_pilot_packet_omission_80ms_manual_80ms_imu_cases',
        accepted_campaign_expansion=False, vehicle_qualification=False,
        audited_control_cycles=sum(case['cycles'] for case in cases),
        cases=cases), indent=2) + '\n')
    shutil.copy2(Path(__file__), evidence / Path(__file__).name)
    files = {}
    for path in evidence.iterdir():
        if path.is_file() and path.name != 'phased-skip-manifest.json':
            files[path.name] = base.digest(path)
        elif path.is_dir() and path.name.startswith(('phased-skip-', 'tested-artifacts', 'px4-source')):
            for artifact in path.rglob('*'):
                if artifact.is_file() and not artifact.is_symlink():
                    files[str(artifact.relative_to(evidence))] = base.digest(artifact)
    for name in ('sitl-lockstep-build/bin/px4', 'sitl-lockstep-build/boardconfig',
                 'sitl-lockstep-build/compile_commands.json',
                 'plugin-build/libresearch_rotational_fixture.so'):
        files[name] = base.digest(evidence / name)
    (evidence / 'phased-skip-manifest.json').write_text(json.dumps(dict(
        scope='four_source_bound_exploratory_single_pilot_packet_loss_cases',
        accepted=False, vehicle_qualification=False, observed_checks_pass=True,
        preserved_preparation_anchor_count=len(checkpoint), files=files), indent=2) + '\n')
    print(json.dumps(dict(manifest_sha256=base.digest(evidence / 'phased-skip-manifest.json'),
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
