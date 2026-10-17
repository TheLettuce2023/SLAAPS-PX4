#!/usr/bin/env python3
"""Run stock EKF2 stop/restart, reset and profile barriers on the revision variant."""
import argparse
import contextlib
import json
from pathlib import Path
import re
import shutil

import verify_stabilized_campaigns as campaigns
import verify_stabilized_publisher_epoch as epoch
import verify_stabilized_sitl as base
import verify_stabilized_state_barriers as barriers

BASE_FIXTURE = campaigns.Fixture
RUNNER = Path(__file__).resolve()


class LifecycleFixture(BASE_FIXTURE):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.result['lifecycle_runner_sha256'] = base.digest(RUNNER)
        self.result['ekf2_lifecycle_observations'] = []
        shutil.copy2(RUNNER, self.work / RUNNER.name)

    def client(self, module, *args, check=True):
        output, code = super().client(module, *args, check=check)
        if module == 'ekf2' and args in (('stop',), ('start',)):
            status, status_code = super().client('ekf2', 'status', check=False)
            action = args[0]
            instances = list(map(int, re.findall(r'ekf2:(\d+) EKF dt:', status)))
            (self.work / ('ekf2-status-after-' + action + '.log')).write_text(status)
            self.result['ekf2_lifecycle_observations'].append(dict(
                action=action, command_returncode=code, status_returncode=status_code,
                reported_instances=instances, observed_sim_time_us=self.now(), status=status))
            # EKF2's status command returns zero even when no instance exists.
            # Parse the actual instance records and reject a failed lock attempt.
            assert code == 0 and status_code == 0 and 'module locked' not in status
            if action == 'stop':
                assert not instances, 'EKF2 still reports a running module after stop'
            else:
                assert instances == [0], 'EKF2 start did not register exactly instance zero'
        return output, code


def inspect(work, result):
    control = epoch.closed_rows(work / 'control.csv')
    restarted = epoch.closed_rows(work / 'restarted.csv')
    before = tuple(int(control[0][field]) for field in epoch.REVISION_FIELDS)
    after = tuple(int(restarted[0][field]) for field in epoch.REVISION_FIELDS)
    assert all(value > 0 for value in before + after)
    assert all(tuple(int(row[field]) for field in epoch.REVISION_FIELDS) == before for row in control)
    assert all(tuple(int(row[field]) for field in epoch.REVISION_FIELDS) == after for row in restarted)
    assert all(row['source_epoch'] == control[0]['source_epoch'] for row in control)
    assert all(row['source_epoch'] == restarted[0]['source_epoch'] for row in restarted)
    observed = result['ekf2_lifecycle_observations']
    for item in observed:
        status = (work / ('ekf2-status-after-' + item['action'] + '.log')).read_text()
        assert status == item['status'] and 'module locked' not in status
        assert list(map(int, re.findall(r'ekf2:(\d+) EKF dt:', status))) == item['reported_instances']
    if result['barrier_case'] in ('publisher_withdrawal', 'rapid_restart'):
        assert 'stopping ekf2 instance 0' in (work / 'estimator-stop.log').read_text()
        assert [item['action'] for item in observed] == ['stop', 'start']
        assert observed[0]['command_returncode'] == 0 and observed[0]['status_returncode'] == 0
        assert observed[0]['reported_instances'] == []
        assert observed[1]['command_returncode'] == 0 and observed[1]['status_returncode'] == 0
        assert observed[1]['reported_instances'] == [0]
        assert after[0] > before[0] and after[2] > before[2] and after[3] > before[3]
        assert after[1] == before[1], 'Gyro/rate publisher unexpectedly restarted with EKF2'
    else:
        assert not observed and after == before, 'Non-lifecycle barrier changed publisher revisions'
    events = epoch.closed_rows(work / 'control.csv.events.csv')
    assert all(row['action'] != 'publisher_cycle' for row in events), 'Synthetic advertisement injection used'
    revoke = next(row for row in events if row['action'] == 'revoke')
    expected_fault = 7 if result['barrier_case'] == 'heading_reset' else 6
    assert int(revoke['status']) == expected_fault
    off_delay = result['first_off_us'] - int(revoke['time'])
    assert 0 <= off_delay <= 10000
    return dict(revisions_before=before, revisions_after=after,
                consumer_off_delay_us=off_delay, revoke_status=int(revoke['status']))


def run(evidence, case, nose_up):
    epoch.origin(evidence)
    # The stock barrier implementation remains byte-identical. Only the fixture
    # captures module-status observations for this separate source-bound test.
    campaigns.Fixture = LifecycleFixture
    try:
        log = evidence / ('lifecycle-' + case + ('-up' if nose_up else '-level') + '.log')
        if log.exists():
            index = 1
            while log.with_name(log.name + '.' + str(index)).exists():
                index += 1
            shutil.copy2(log, log.with_name(log.name + '.' + str(index)))
        with log.open('w') as stream, contextlib.redirect_stdout(stream):
            barriers.barrier(evidence, case, nose_up)
    finally:
        campaigns.Fixture = BASE_FIXTURE
    paths = sorted(evidence.glob('barrier-' + case + ('-up-' if nose_up else '-level-') + '*/result.json'),
                   key=lambda path: path.stat().st_mtime_ns)
    assert paths
    work = paths[-1].parent
    result = json.loads(paths[-1].read_text())
    result.update(inspect(work, result))
    result['lifecycle_verified'] = True
    paths[-1].write_text(json.dumps(result, indent=2) + '\n')
    print('PASS actual barrier', case, 'up' if nose_up else 'level', result['revisions_before'],
          '->', result['revisions_after'], 'off delay us', result['consumer_off_delay_us'], flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--evidence', type=Path, required=True)
    parser.add_argument('--case', choices=('all', *barriers.CASES), default='all')
    parser.add_argument('--nose-up', action='store_true')
    args = parser.parse_args()
    evidence = args.evidence.resolve()
    assert evidence.parent == Path('/var/tmp') and evidence.name.startswith('research-stabilized-sitl-')
    for case in (barriers.CASES if args.case == 'all' else (args.case,)):
        run(evidence, case, args.nose_up)
