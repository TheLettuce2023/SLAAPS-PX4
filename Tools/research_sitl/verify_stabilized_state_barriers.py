#!/usr/bin/env python3
"""Native state reset/lifecycle regressions in the isolated synthetic fixture."""
import argparse
import csv
import json
from pathlib import Path
import re
import shutil
import time

import verify_stabilized_campaigns as campaigns
import verify_stabilized_sitl as base
import seal_stabilized_campaigns as audit

CASES = ('publisher_withdrawal', 'rapid_restart', 'heading_reset', 'single_estimator_profile')


def attitude_snapshot(fixture, name):
    output, code = fixture.client('listener', 'vehicle_attitude', '-n', '1', check=False)
    (fixture.work / (name + '-attitude.log')).write_text(output)
    assert code == 0
    return int(re.search(r'quat_reset_counter: (\d+)', output).group(1))


def barrier(evidence, case, nose_up=False):
    fixture = campaigns.Fixture(evidence, 'barrier-' + case + ('-up' if nose_up else '-level'), nose_up)
    fixture.result.update(barrier_case=case, nose_up=nose_up, passed=False,
                          barrier_source_sha256=base.digest(Path(__file__)))
    shutil.copy2(Path(__file__), fixture.work / Path(__file__).name)
    try:
        fixture.start()
        before = attitude_snapshot(fixture, 'before')
        fixture.result['reset_counter_before'] = before
        fixture.action('inject', case)
        if case == 'heading_reset':
            # Stock PX4 VehicleCommand/MAVLink command 620, not an attitude
            # publisher or truth injection. Read the value from this checkout.
            text = (base.ROOT / 'msg/versioned/VehicleCommand.msg').read_text()
            command = int(re.search(r'VEHICLE_CMD_EXTERNAL_ATTITUDE_ESTIMATE = (\d+)', text).group(1))
            fixture.connection.mav.command_long_send(1, 1, command, 0, 0, 0, 45, 0, 0, 0, 2)
            fixture.result['heading_reset_command'] = command
            fixture.wait(300000)
            after = attitude_snapshot(fixture, 'after')
            fixture.result['reset_counter_after'] = after
            assert after != before, 'Stock EKF did not publish an actual attitude reset'
        elif case in ('publisher_withdrawal', 'rapid_restart'):
            fixture.result['stop_request_us'] = fixture.now()
            output, _ = fixture.client('ekf2', 'stop')
            (fixture.work / 'estimator-stop.log').write_text(output)
            fixture.result['stop_return_us'] = fixture.now()
            if case == 'rapid_restart':
                output, _ = fixture.client('ekf2', 'start')
                (fixture.work / 'estimator-restart.log').write_text(output)
                fixture.result['restart_return_us'] = fixture.now()
                fixture.wait(300000)
            else:
                # Less than the 100 ms attitude freshness allowance. The
                # withdrawn publisher must not remain eligible on cached data.
                fixture.wait(80000)
        elif case == 'single_estimator_profile':
            # This target permits one EKF. A request for a multi-candidate
            # profile must revoke before any controller can use that profile.
            fixture.client('param', 'set', 'EKF2_MULTI_IMU', '2')
            fixture.wait(100000)
            fixture.client('param', 'set', 'EKF2_MULTI_IMU', '0')
        status, _ = fixture.client('research_stabilized_sitl', 'status')
        fixture.result['fault_status'] = status.strip()
        (fixture.work / 'fault-status.log').write_text(status)
        assert 'state=4' in status and 'ready=0' in status, 'State barrier did not revoke: ' + status
        output, code = fixture.client('research_stabilized_sitl', 'enable', check=False)
        (fixture.work / 'reenable.log').write_text(output)
        fixture.result['reenable_returncode'] = code
        assert code != 0, 'State fault accepted implicit re-enable'
        fixture.wait(200000)
        status, _ = fixture.client('research_stabilized_sitl', 'status')
        assert 'state=4' in status, 'State barrier silently recovered'
        fixture.client('research_stabilized_sitl', 'stop')
        if case == 'publisher_withdrawal':
            fixture.client('ekf2', 'start')
        fixture.client('research_stabilized_sitl', 'start', 'slaaps_fixture', str(fixture.work / 'restarted.csv'))
        deadline = time.monotonic() + 65
        while time.monotonic() < deadline:
            status, _ = fixture.client('research_stabilized_sitl', 'status')
            if 'state=1' in status and 'ready=1' in status:
                break
            time.sleep(0.05)
        else:
            raise RuntimeError('Deliberate restart did not reach Ready')
        fixture.action('explicit_reenable')
        fixture.client('research_stabilized_sitl', 'enable')
        fixture.wait(1000000)
        status, _ = fixture.client('research_stabilized_sitl', 'status')
        fixture.result['restart_status'] = status.strip()
        assert 'state=3' in status and 'fault=0' in status
        fixture.client('research_stabilized_sitl', 'disable')
        fixture.wait(300000)
    except Exception as error:
        fixture.result['error'] = str(error)
        raise
    finally:
        fixture.close()
    truth = campaigns.rows(fixture.work / 'truth.csv')
    first_active = next(i for i, row in enumerate(truth) if row['enabled'] == '1')
    first_off = next(i for i in range(first_active + 1, len(truth)) if truth[i]['enabled'] == '0')
    recovery = next(i for i in range(first_off + 1, len(truth)) if truth[i]['enabled'] == '1')
    off = truth[first_off:recovery]
    assert all(row['thrust'] == '0' and row['lease_fault'] == '1' for row in off)
    assert all(float(row['t' + str(i)]) == 0 for row in off for i in range(4))
    old, new = int(truth[first_active]['session']), int(truth[recovery]['session'])
    assert new > old > 0
    assert truth[-1]['thrust'] == '0' and truth[-1]['lease_fault'] == '1'
    control = campaigns.rows(fixture.work / 'control.csv')
    restarted = campaigns.rows(fixture.work / 'restarted.csv')
    assert all(row['reset'] == str(before) for row in control), 'Owner published across an attitude reset'
    assert all((row['gyro_id'], row['accel_id']) == (control[0]['gyro_id'], control[0]['accel_id']) for row in restarted), \
        'Recovery unexpectedly changed IMU identity'
    events = campaigns.rows(fixture.work / 'control.csv.events.csv')
    revoke = next(row for row in events if row['action'] == 'revoke')
    withdrawal_age = int(revoke['time']) - int(control[-1]['att_time'])
    if case in ('publisher_withdrawal', 'rapid_restart'):
        assert withdrawal_age < 100000, 'Lifecycle test only exercised ordinary stale-state expiry'
    fixture.result.update(passed=True, old_session=old, new_session=new,
                          first_off_us=int(truth[first_off]['time']), final_thrust_n=0,
                          revoke_time_us=int(revoke['time']), attitude_age_at_revoke_us=withdrawal_age,
                          gyro_id=int(control[0]['gyro_id']), accel_id=int(control[0]['accel_id']))
    (fixture.work / 'result.json').write_text(json.dumps(fixture.result, indent=2) + '\n')
    print('PASS', fixture.work, fixture.result, flush=True)


def seal(out):
    expected = {(case, up) for case in CASES for up in (False, True)}
    chosen = {}
    for path in sorted(out.glob('barrier-*/result.json'), key=lambda p: p.stat().st_mtime_ns):
        result = json.loads(path.read_text())
        chosen[(result['barrier_case'], result['nose_up'])] = (path.parent, result)
    assert set(chosen) == expected, 'Incomplete level/nose-up state barrier coverage'
    assert all(result['passed'] for _, result in chosen.values()), 'Latest state barrier attempt failed'
    cycles = 0
    for work, result in chosen.values():
        audit.audit_runtime_profile(work, result)
        assert result['barrier_source_sha256'] == base.digest(Path(__file__))
        assert base.digest(work / Path(__file__).name) == result['barrier_source_sha256']
        assert result['px4_sha256'] == base.digest(out / 'sitl-lockstep-build/bin/px4')
        assert result['plugin_sha256'] == base.digest(out / 'plugin-build/libresearch_rotational_fixture.so')
        for name in ('control.csv', 'restarted.csv', 'truth.csv', 'control.csv.events.csv', 'restarted.csv.events.csv'):
            with (work / name).open() as stream:
                records = list(csv.DictReader(stream))
                assert records and all(None not in row and None not in row.values() for row in records), 'Malformed closed log: ' + name
        cycles += audit.audit_cycles(work / 'control.csv') + audit.audit_cycles(work / 'restarted.csv')
        assert result['reenable_returncode'] != 0 and result['new_session'] > result['old_session'] > 0
        assert result['final_thrust_n'] == 0
        if result['barrier_case'] == 'heading_reset':
            assert result['reset_counter_after'] != result['reset_counter_before']
        if result['barrier_case'] in ('publisher_withdrawal', 'rapid_restart'):
            assert 0 <= result['attitude_age_at_revoke_us'] < 100000
        for name, identity in result['source_sha256'].items():
            assert base.digest(base.ROOT / name) == base.digest(work / 'source-inputs' / name) == identity
    checkpoint = json.loads((out / 'checkpoint.json').read_text())
    for name, identity in checkpoint.items():
        assert base.digest(base.ROOT / name) == identity, 'Mapped input changed: ' + name
    inputs = {}
    graph = json.loads((base.ROOT / 'docs/RESEARCH_CODE_GRAPH.json').read_text())
    for node in graph['nodes']:
        name = node['path']
        if name.startswith(('src/', 'platforms/', 'msg/', 'boards/')) and (out / 'px4-source' / name).is_file():
            identity = base.digest(base.ROOT / name)
            assert base.digest(out / 'px4-source' / name) == identity, 'Compiled dependency differs: ' + name
            inputs[name] = identity
            destination = out / 'audited-inputs' / name
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(base.ROOT / name, destination)
    (out / 'audited-inputs.json').write_text(json.dumps(inputs, indent=2) + '\n')
    (out / 'state-barrier-summary.json').write_text(json.dumps(dict(
        scope='eight_synthetic_native_state_barrier_and_explicit_recovery_cases', accepted=True,
        audited_control_cycles=cycles, cases=[dict(directory=work.name, **result) for work, result in chosen.values()]), indent=2) + '\n')
    (out / 'preservation.json').write_text(json.dumps(checkpoint, indent=2) + '\n')
    shutil.copy2(Path(__file__), out / Path(__file__).name)
    files = {}
    for path in out.iterdir():
        if path.is_file() and path.name != 'state-barrier-manifest.json':
            files[path.name] = base.digest(path)
        elif path.is_dir() and path.name.startswith(('barrier-', 'tested-artifacts', 'audited-inputs')):
            for artifact in path.rglob('*'):
                if artifact.is_file():
                    files[str(artifact.relative_to(out))] = base.digest(artifact)
    for name in ('sitl-lockstep-build/bin/px4', 'sitl-lockstep-build/boardconfig',
                 'sitl-lockstep-build/compile_commands.json', 'plugin-build/libresearch_rotational_fixture.so'):
        files[name] = base.digest(out / name)
    (out / 'state-barrier-manifest.json').write_text(json.dumps(dict(
        scope='synthetic_native_state_barrier_cases_only', accepted=True,
        physical_authority_granted=False, general_publisher_epoch_qualification=False,
        preserved_anchor_count=len(checkpoint), files=files), indent=2) + '\n')
    print('8 state barriers /', cycles, 'cycles audited;', len(checkpoint), 'anchors preserved', flush=True)
    print('Manifest SHA-256:', base.digest(out / 'state-barrier-manifest.json'), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--evidence', type=Path, required=True)
    parser.add_argument('--case', choices=('all', *CASES), default='all')
    parser.add_argument('--nose-up', action='store_true')
    parser.add_argument('--seal', action='store_true')
    args = parser.parse_args()
    evidence = args.evidence.resolve()
    assert evidence.parent == Path('/var/tmp') and evidence.name.startswith('research-stabilized-sitl-')
    if args.seal:
        seal(evidence)
        return
    for case in (CASES if args.case == 'all' else (args.case,)):
        barrier(evidence, case, args.nose_up)


if __name__ == '__main__':
    main()
