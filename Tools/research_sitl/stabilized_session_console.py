#!/usr/bin/env python3
"""Operate a qualified private session fixture; leaves the legacy default unchanged."""
import argparse
import fcntl
import json
import math
from pathlib import Path
import re
import shutil
import signal
import socket
import tempfile
import time

from pymavlink import mavutil
from qualify_stabilized_session import FAULTS, digest, verify
from verify_stabilized_session_maneuvers import SessionFixture

INSTANCE, INPUT_PORT, OUTPUT_PORT = 29481, 19787, 19788


def qualified_artifacts(evidence):
    evidence = evidence.resolve()
    assert evidence.parent == Path('/var/tmp') and evidence.name.startswith('research-stabilized-session-qualification-')
    verify(evidence / 'qualification-manifest.json')
    for recorded in evidence.glob('*.py'):
        current = Path(__file__).resolve().parent / recorded.name
        assert current.is_file() and digest(current) == digest(recorded), 'Qualified runner changed: ' + recorded.name
    result = json.loads((evidence / 'qualification.json').read_text())
    assert result['passed'] and result['completed_cases'] == result['planned_cases'] == 30
    assert {r['case'] for r in result['results']} == {'single_axis', 'coupled', *FAULTS}
    for item in result['results']:
        assert item['passed'] and item['returncode'] == 0
        receipt = Path(item['directory']) / item['receipt_name']
        assert digest(receipt) == item['receipt_sha256']
        verify(receipt)
    build = Path(result['build'])
    receipt = build / 'session-link-manifest.json'
    assert digest(receipt) == result['build_receipt_sha256']
    verify(receipt)
    profile = Path(next(r['directory'] for r in result['results'] if r['case'] == 'single_axis')) / 'synthetic.profile'
    out = Path(tempfile.mkdtemp(prefix='research-stabilized-session-console-', dir='/var/tmp'))
    (out / 'sitl-lockstep-build').symlink_to(build / 'sitl-lockstep-build', target_is_directory=True)
    (out / 'px4-source').symlink_to(build / 'px4-source', target_is_directory=True)
    (out / 'plugin-build').mkdir()
    (out / 'plugin-build/libresearch_rotational_fixture.so').symlink_to(build / 'session-plugin-build/libresearch_rotational_session.so')
    shutil.copy2(profile, out / 'synthetic.profile')
    shutil.copy2(__file__, out / Path(__file__).name)
    (out / 'origin.json').write_text(json.dumps(dict(qualification=str(evidence),
        qualification_receipt_sha256=digest(evidence / 'qualification-manifest.json'),
        build=str(build), physical_authority_granted=False), indent=2) + '\n')
    return out


class ConsoleFixture(SessionFixture):
    def __init__(self, evidence, nose_up):
        super().__init__(evidence, 'operator', nose_up)
        self.instance = INSTANCE
        self.connection.close()
        self.connection = mavutil.mavlink_connection(f'udpout:127.0.0.1:{INPUT_PORT}', source_system=255, source_component=190)
        self.script.write_text(self.script.read_text().replace('19785', str(INPUT_PORT)).replace('19786', str(OUTPUT_PORT)))
        self.metadata = dict(ready=False, closed=False, work_directory=str(self.work), instance=INSTANCE,
                             input_port=INPUT_PORT, control_csv=str(self.work / 'control.csv'), truth_csv=str(self.work / 'truth.csv'))
        self.save_metadata()

    def save_metadata(self):
        temporary = self.evidence / 'session.tmp'
        temporary.write_text(json.dumps(self.metadata, indent=2) + '\n')
        temporary.replace(self.evidence / 'session.json')

    def status(self):
        text, _ = self.client('research_session_sitl', 'status')
        values = {k: int(v) for k, v in re.findall(r'(phase|state|consumer|fault|commands|acknowledgements)=(\d+)', text)}
        assert len(values) == 6, text
        mode = {1: 'Ready', 2: 'Body rate', 3: 'Attitude hold', 4: 'Fault'}.get(values['state'], 'Disabled')
        print('Mode:', mode, 'fault=' + str(values['fault']), flush=True)
        return values

    def sticks(self, values):
        if len(values) != 3 or not all(math.isfinite(v) and -1 <= v <= 1 for v in values):
            raise ValueError('Use three finite stick values from -1 to 1')
        roll, pitch, yaw = values
        self.command = (-round(pitch * 1000), round(roll * 1000), round(yaw * 1000))
        self.action('operator_sticks', ';'.join(map(str, values)))

    def disable(self):
        self.sticks((0, 0, 0))
        self.client('research_session_sitl', 'disable')
        self.action('operator_disable')
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            text, _ = self.client('research_session_sitl', 'status')
            if 'phase=3' in text and 'consumer=1' in text:
                print('Disabled; plant disarm confirmed. Restart required.', flush=True)
                return
            time.sleep(.02)
        raise RuntimeError('Plant disarm confirmation pending; restart remains blocked')

    def restart(self):
        self.disable()
        self.sticks((0, 0, 0))
        self.wait(200000)
        self.client('research_session_sitl', 'restart')
        self.action('operator_restart')
        self.wait(1000000)
        status = self.status()
        assert status['state'] == 3 and status['fault'] == 0, 'Explicit restart did not regain hold'


def console(qualification, nose_up):
    with Path('/var/tmp/research-stabilized-session-console.lock').open('a+') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
            probe.bind(('127.0.0.1', INPUT_PORT))
        out = qualified_artifacts(qualification)
        print('Session:', out, flush=True)
        fixture = ConsoleFixture(out, nose_up)
        try:
            fixture.start()
            fixture.metadata.update(ready=True, px4_pid=fixture.px4.pid, gazebo_pid=fixture.gz.pid)
            fixture.save_metadata()
            print('Synthetic rotational fixture: 20 m/s airflow, fixed simulated EDF 10 N.', flush=True)
            print('Commands: sticks <roll> <pitch> <yaw>, hold, status, disable, restart, quit.', flush=True)
            print('Sticks persist; hold centers all axes and captures attitude on release.', flush=True)
            fixture.status()
            while True:
                try:
                    words = input('manual> ').split()
                except EOFError:
                    break
                if not words:
                    continue
                try:
                    if words[0] == 'sticks' and len(words) == 4:
                        fixture.sticks(tuple(map(float, words[1:])))
                        print('Body-axis sticks requested.', flush=True)
                    elif words == ['hold']:
                        fixture.sticks((0, 0, 0))
                        print('Attitude hold requested.', flush=True)
                    elif words == ['status']:
                        fixture.status()
                    elif words == ['disable']:
                        fixture.disable()
                    elif words == ['restart']:
                        fixture.restart()
                    elif words == ['quit']:
                        break
                    else:
                        raise ValueError('Use sticks, hold, status, disable, restart or quit')
                except (ValueError, AssertionError) as error:
                    fixture.sticks((0, 0, 0))
                    print(str(error) + '; hold requested.', flush=True)
        finally:
            try:
                if fixture.px4 is not None and fixture.px4.poll() is None:
                    fixture.disable()
            finally:
                fixture.close()
                fixture.metadata.update(ready=False, closed=True,
                    px4_returncode=fixture.px4.poll() if fixture.px4 else None,
                    gazebo_returncode=fixture.gz.poll() if fixture.gz else None)
                fixture.save_metadata()
        print('Session closed. Logs:', fixture.work, flush=True)


if __name__ == '__main__':
    def interrupted(signum, frame):
        raise KeyboardInterrupt
    signal.signal(signal.SIGTERM, interrupted)
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--qualification', type=Path, required=True)
    parser.add_argument('--level', action='store_true')
    args = parser.parse_args()
    console(args.qualification, not args.level)
