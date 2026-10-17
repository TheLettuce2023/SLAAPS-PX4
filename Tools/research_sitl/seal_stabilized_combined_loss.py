#!/usr/bin/env python3
"""Audit two native 80 ms pilot/IMU manual-loss and recovery observations."""
import argparse
import json
from pathlib import Path
import shutil
import xml.etree.ElementTree as ET

import seal_stabilized_campaigns as audit
import verify_stabilized_campaigns as campaigns
import verify_stabilized_combined_loss as loss
import verify_stabilized_sitl as base


def inspect(work, result, evidence):
    assert result['passed'] and result['exploratory_only']
    assert result['case'] == 'combined_input_loss' and result['seed'] == 42
    assert result['manual_send_period_s'] == loss.MANUAL_PERIOD_S
    assert result['imu_transport_delay_us'] == loss.IMU_DELAY_US
    assert result['magnetic_transport_delay_us'] == 0
    assert result['loss_source_sha256'] == base.digest(Path(loss.__file__))
    assert result['loss_source_sha256'] == base.digest(work / Path(loss.__file__).name)
    assert result['px4_sha256'] == base.digest(evidence / 'sitl-lockstep-build/bin/px4')
    assert result['plugin_sha256'] == base.digest(evidence / 'plugin-build/libresearch_rotational_fixture.so')
    assert result['reenable_returncode'] != 0
    assert 'state=4' in result['fault_status'] and 'fault=5' in result['fault_status']
    assert 'state=3' in result['restart_status'] and 'fault=0' in result['restart_status']
    assert audit.audit_case(work, result) > 0

    world = ET.parse(work / 'world.sdf').getroot()
    plugin_delay = world.find('.//plugin[@name="research_stabilized_fixture::RotationalFixture"]'
                              '/imu_transport_delay_us')
    sensor_topic = world.find('.//sensor[@name="imu_sensor"]/topic')
    assert plugin_delay is not None and int(plugin_delay.text) == loss.IMU_DELAY_US
    assert sensor_topic is not None and sensor_topic.text == '/model/slaaps_fixture/imu_raw'
    packets = campaigns.rows(work / 'truth.csv.imu.csv')
    assert len(packets) > 1000
    assert all(int(row['delivery_time']) - int(row['sample_time']) == loss.IMU_DELAY_US
               for row in packets)

    actions = campaigns.rows(work / 'actions.csv')
    blackout = [int(row['sim_time']) for row in actions
                if row['action'] == 'inject' and row['details'] == 'manual_blackout']
    assert len(blackout) == 1
    reenable = [int(row['sim_time']) for row in actions if row['action'] == 'explicit_reenable']
    assert len(reenable) == 1 and reenable[0] > blackout[0]
    control = campaigns.rows(work / 'control.csv')
    restarted = campaigns.rows(work / 'restarted.csv')
    truth = campaigns.rows(work / 'truth.csv')
    assert control and restarted
    assert all(row['state'] == '3' for row in control + restarted)
    last = control[-1]
    last_sample = int(last['manual_sample'])
    last_age = int(last['time']) - last_sample
    assert int(last['time']) >= blackout[0]
    assert last_age <= 100000
    off_time = result['first_off_us']
    assert 100000 <= off_time - last_sample <= 120000
    assert not any(int(row['time']) >= off_time for row in control)
    assert all(int(row['time']) >= reenable[0] for row in restarted)
    assert 0 <= result['first_recovery_us'] - int(restarted[0]['time']) <= 12000
    off = [row for row in truth if off_time <= int(row['time']) < result['first_recovery_us']]
    assert off and all(row['enabled'] == '0' and row['thrust'] == '0'
                       and row['lease_fault'] == '1' for row in off)
    assert all(all(float(row['t' + str(i)]) == 0 for i in range(4)) for row in off)
    assert result['new_session'] > result['old_session'] > 0
    assert truth[-1]['thrust'] == '0' and truth[-1]['lease_fault'] == '1'
    return dict(directory=work.name, nose_up=result['nose_up'],
                control_cycles=len(control) + len(restarted),
                blackout_start_us=blackout[0], last_manual_age_us=last_age,
                off_after_last_manual_sample_us=off_time - last_sample,
                off_duration_us=result['first_recovery_us'] - off_time,
                relayed_imu_packets=len(packets), observed_checks_pass=True)


def seal(evidence):
    paths = sorted(evidence.glob('combined-loss-*/result.json'))
    assert len(paths) == 2
    results = [(path.parent, json.loads(path.read_text())) for path in paths]
    assert {result['nose_up'] for _, result in results} == {False, True}
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
    (evidence / 'combined-loss-summary.json').write_text(json.dumps(dict(
        scope='two_exact_80ms_manual_80ms_imu_loss_and_explicit_recovery_observations',
        accepted_campaign_expansion=False, vehicle_qualification=False,
        audited_control_cycles=sum(case['control_cycles'] for case in cases),
        cases=cases), indent=2) + '\n')
    shutil.copy2(Path(__file__), evidence / Path(__file__).name)
    files = {}
    for path in evidence.iterdir():
        if path.is_file() and path.name != 'combined-loss-manifest.json':
            files[path.name] = base.digest(path)
        elif path.is_dir() and path.name.startswith(('combined-loss-', 'tested-artifacts', 'px4-source')):
            for artifact in path.rglob('*'):
                if artifact.is_file() and not artifact.is_symlink():
                    files[str(artifact.relative_to(evidence))] = base.digest(artifact)
    for name in ('sitl-lockstep-build/bin/px4', 'sitl-lockstep-build/boardconfig',
                 'sitl-lockstep-build/compile_commands.json',
                 'plugin-build/libresearch_rotational_fixture.so'):
        files[name] = base.digest(evidence / name)
    (evidence / 'combined-loss-manifest.json').write_text(json.dumps(dict(
        scope='two_source_bound_exploratory_combined_timing_manual_loss_cases',
        accepted=False, vehicle_qualification=False, observed_checks_pass=True,
        preserved_preparation_anchor_count=len(checkpoint), files=files), indent=2) + '\n')
    print(json.dumps(dict(manifest_sha256=base.digest(evidence / 'combined-loss-manifest.json'),
                          files=len(files), cycles=sum(case['control_cycles'] for case in cases),
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
