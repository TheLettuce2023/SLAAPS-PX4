#!/usr/bin/env python3
"""Probe native manual-input loss and explicit recovery with combined 80 ms timing."""
import argparse
import json
from pathlib import Path
import shutil
import time

import verify_stabilized_campaigns as campaigns
import verify_stabilized_sitl as base


MANUAL_PERIOD_S = 0.08
IMU_DELAY_US = 80000


def run_case(evidence, nose_up):
    fixture = campaigns.Fixture(evidence, 'combined-loss-' + ('up' if nose_up else 'level'),
                                nose_up=nose_up, manual_period_s=MANUAL_PERIOD_S,
                                imu_delay_us=IMU_DELAY_US)
    fixture.result.update(exploratory_only=True, case='combined_input_loss',
                          loss_source_sha256=base.digest(Path(__file__)))
    shutil.copy2(Path(__file__), fixture.work / Path(__file__).name)
    try:
        fixture.start()
        fixture.action('inject', 'manual_blackout')
        fixture.sending = False
        fixture.wait(300000)
        status, _ = fixture.client('research_stabilized_sitl', 'status')
        (fixture.work / 'fault-status.log').write_text(status)
        assert 'state=4' in status and 'fault=5' in status, 'Manual loss did not latch: ' + status
        response, code = fixture.client('research_stabilized_sitl', 'enable', check=False)
        (fixture.work / 'reenable.log').write_text(response)
        assert code != 0, 'Faulted owner accepted enable'
        fixture.wait(300000)
        status, _ = fixture.client('research_stabilized_sitl', 'status')
        assert 'state=4' in status, 'Owner recovered without a new session'
        fixture.result.update(fault_status=status.strip(), reenable_returncode=code)

        fixture.client('research_stabilized_sitl', 'stop')
        fixture.sending = True
        fixture.client('research_stabilized_sitl', 'start', 'slaaps_fixture',
                       str(fixture.work / 'restarted.csv'))
        deadline = time.monotonic() + 65
        while time.monotonic() < deadline:
            status, _ = fixture.client('research_stabilized_sitl', 'status')
            if 'state=1' in status and 'ready=1' in status:
                break
            time.sleep(0.05)
        else:
            raise RuntimeError('Explicit restart did not regain centered inputs')
        fixture.action('explicit_reenable')
        fixture.client('research_stabilized_sitl', 'enable')
        fixture.wait(1000000)
        status, _ = fixture.client('research_stabilized_sitl', 'status')
        assert 'state=3' in status and 'fault=0' in status, 'New session did not regain Hold'
        fixture.result['restart_status'] = status.strip()
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
    first_recovery = next(i for i in range(first_off + 1, len(truth)) if truth[i]['enabled'] == '1')
    off = truth[first_off:first_recovery]
    assert all(row['thrust'] == '0' and row['lease_fault'] == '1' for row in off)
    assert all(float(row['t' + str(i)]) == 0 for row in off for i in range(4))
    old_session = int(truth[first_active]['session'])
    new_session = int(truth[first_recovery]['session'])
    assert new_session > old_session
    assert truth[-1]['thrust'] == '0' and truth[-1]['lease_fault'] == '1'
    fixture.result.update(passed=True, first_active_us=int(truth[first_active]['time']),
                          first_off_us=int(truth[first_off]['time']),
                          first_recovery_us=int(truth[first_recovery]['time']),
                          old_session=old_session, new_session=new_session, final_thrust_n=0)
    (fixture.work / 'result.json').write_text(json.dumps(fixture.result, indent=2) + '\n')
    print('PASS', fixture.work, fixture.result, flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--evidence', type=Path, required=True)
    parser.add_argument('--nose-up', action='store_true')
    args = parser.parse_args()
    evidence = args.evidence.resolve()
    assert evidence.parent == Path('/var/tmp') and evidence.name.startswith('research-stabilized-sitl-')
    run_case(evidence, args.nose_up)


if __name__ == '__main__':
    main()
