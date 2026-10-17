#!/usr/bin/env python3
"""Omit one scheduled pilot packet and verify native fault/recovery in SITL."""
import argparse
import json
from pathlib import Path
import shutil
import time

import verify_stabilized_campaigns as campaigns
import verify_stabilized_mixed as mixed
import verify_stabilized_phased_jitter as phased
import verify_stabilized_sitl as base


SKIP_INDEX = 50
OBSERVE_AFTER_START_US = 5500000


class SkipFixture(phased.PhaseFixture):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.skip_trace = (self.work / 'manual-skip.csv').open('w', buffering=1)
        self.skip_trace.write('index,target_sim_time,observed_sim_time\n')
        self.result.update(single_skip_source_sha256=base.digest(Path(__file__)),
                           single_skip_index=SKIP_INDEX)
        shutil.copy2(Path(__file__), self.work / Path(__file__).name)

    def send(self):
        next_wall = time.monotonic()
        index = 0
        try:
            while not self.stopped.is_set():
                if self.phase_enabled:
                    observed = self.phase_now()
                    if observed >= self.next_target_us:
                        if index == SKIP_INDEX:
                            self.skip_trace.write(f'{index},{self.next_target_us},{observed}\n')
                        else:
                            pitch, roll, yaw = self.command
                            self.connection.mav.manual_control_send(1, pitch, roll, 500, yaw, 0)
                            self.send_trace.write(f'{index},{self.next_target_us},{observed},'
                                                  f'{pitch},{roll},{yaw}\n')
                        self.next_target_us += phased.SEND_INTERVALS_US[index % len(phased.SEND_INTERVALS_US)]
                        index += 1
                elif self.sending and time.monotonic() >= next_wall:
                    pitch, roll, yaw = self.command
                    self.connection.mav.manual_control_send(1, pitch, roll, 500, yaw, 0)
                    next_wall = time.monotonic() + phased.MANUAL_PERIOD_S
                self.stopped.wait(0.001)
        except Exception as error:
            (self.work / 'sender-error.txt').write_text(repr(error))

    def close(self):
        try:
            super().close()
        finally:
            self.skip_trace.close()


def run_case(evidence, nose_up, mode):
    fixture = SkipFixture(evidence,
                          f'phased-skip-{"up" if nose_up else "level"}-{mode}',
                          nose_up=nose_up, manual_period_s=phased.MANUAL_PERIOD_S,
                          imu_delay_us=phased.IMU_DELAY_US)
    fixture.result.update(exploratory_only=True, case='phased_single_skip',
                          skip_mode=mode, observed_fault_expected=5)
    try:
        fixture.start()
        if mode == 'rate':
            raw = mixed.BODY_STICKS
            fixture.command = (-round(1000 * raw[1]), round(1000 * raw[0]),
                               round(1000 * raw[2]))
        fixture.action('single_skip_mode', mode)
        fixture.wait(OBSERVE_AFTER_START_US)
        status, _ = fixture.client('research_stabilized_sitl', 'status')
        (fixture.work / 'fault-status.log').write_text(status)
        assert 'state=4' in status and 'fault=5' in status, 'Single skip did not latch manual fault: ' + status
        response, code = fixture.client('research_stabilized_sitl', 'enable', check=False)
        (fixture.work / 'reenable.log').write_text(response)
        assert code != 0, 'Faulted owner accepted enable'
        fixture.command = (0, 0, 0)
        fixture.action('center_for_recovery')
        fixture.wait(300000)
        status, _ = fixture.client('research_stabilized_sitl', 'status')
        assert 'state=4' in status, 'Owner recovered without a new session'
        fixture.result.update(fault_status=status.strip(), reenable_returncode=code)

        fixture.client('research_stabilized_sitl', 'stop')
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
        (fixture.work / 'restart-status.log').write_text(status)
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
    parser.add_argument('--mode', choices=('hold', 'rate'), required=True)
    args = parser.parse_args()
    evidence = args.evidence.resolve()
    assert evidence.parent == Path('/var/tmp') and evidence.name.startswith('research-stabilized-sitl-')
    run_case(evidence, args.nose_up, args.mode)


if __name__ == '__main__':
    main()
