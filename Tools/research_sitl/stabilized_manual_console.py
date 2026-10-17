#!/usr/bin/env python3
"""Operate the qualified synthetic manual fixture through local body-axis sticks."""
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

import verify_stabilized_campaigns as campaigns
import verify_stabilized_sitl as base

DEFAULT_PROFILE = base.ROOT / 'Tools/research_sitl/stabilized_manual_default.json'
INSTANCE = 29476
INPUT_PORT = 19781
OUTPUT_PORT = 19782
MODE_NAMES = {0: 'Disabled', 1: 'Ready', 2: 'Body rate', 3: 'Attitude hold', 4: 'Fault'}
FAULT_NAMES = {3: 'Control timing rejected', 5: 'Pilot input rejected', 6: 'Selected state rejected',
               7: 'Selected state changed', 8: 'Sticks were not centered',
               9: 'Controller calculation failed', 10: 'Canard allocation failed'}


def qualified_profile(path):
    profile = json.loads(path.read_text())
    assert profile['schema_version'] == 1 and profile['mode'] == 'slaaps_synthetic_stabilized_manual'
    assert profile['pilot_semantics'] == 'body_rate_with_quaternion_hold_on_release'
    assert profile['axes_frame'] == 'body_frd' and not profile['physical_authority_granted']
    assert profile['fixture_airflow_m_s'] == 20 and profile['fixed_simulated_edf_thrust_n'] == 10
    evidence = Path(profile['qualification_evidence']).resolve()
    assert evidence.parent == Path('/var/tmp') and evidence.name.startswith('research-stabilized-sitl-')
    receipt = evidence / 'revision-qualification-manifest.json'
    assert base.digest(receipt) == profile['manifest_sha256'], 'Default qualification receipt changed'
    manifest = json.loads(receipt.read_text())
    assert manifest['accepted'] and not manifest['physical_authority_granted']
    assert manifest['fresh_cases'] + manifest['inherited_cases'] == 31
    for name, identity in manifest['files'].items():
        relative = Path(name)
        assert not relative.is_absolute() and '..' not in relative.parts
        assert base.digest(evidence / name) == identity, 'Qualified artifact changed: ' + name
    assert base.digest(evidence / 'sitl-lockstep-build/bin/px4') == profile['px4_sha256']
    assert base.digest(evidence / 'plugin-build/libresearch_rotational_fixture.so') == profile['plugin_sha256']
    for path in base.FIXTURE.rglob('*'):
        if path.is_file():
            assert base.digest(path) == base.digest(evidence / 'px4-source' / path.relative_to(base.ROOT)), \
                'Current fixture differs from qualified inputs: ' + str(path)
    return profile, evidence


def session_artifacts(profile, evidence):
    out = Path(tempfile.mkdtemp(prefix='research-stabilized-manual-session-', dir='/var/tmp'))
    shutil.copytree(evidence / 'px4-source', out / 'px4-source')
    shutil.copytree(evidence / 'sitl-lockstep-build/bin', out / 'sitl-lockstep-build/bin', symlinks=True)
    (out / 'plugin-build').mkdir()
    shutil.copy2(evidence / 'plugin-build/libresearch_rotational_fixture.so', out / 'plugin-build/libresearch_rotational_fixture.so')
    (out / 'session-origin.json').write_text(json.dumps(dict(
        scope='operator_session_synthetic_rotational_fixture', qualification=profile,
        console_sha256=base.digest(Path(__file__)), physical_authority_granted=False), indent=2) + '\n')
    shutil.copy2(Path(__file__), out / Path(__file__).name)
    return out


class ConsoleFixture(campaigns.Fixture):
    def __init__(self, evidence, nose_up):
        super().__init__(evidence, 'operator', nose_up)
        from pymavlink import mavutil
        self.instance = INSTANCE
        self.connection.close()
        self.connection = mavutil.mavlink_connection(f'udpout:127.0.0.1:{INPUT_PORT}',
                                                     source_system=255, source_component=190)
        text = self.script.read_text()
        original = 'mavlink start -u 19779 -o 19780 -t 127.0.0.1 -m custom'
        assert text.count(original) == 1
        self.script.write_text(text.replace(original,
            f'mavlink start -u {INPUT_PORT} -o {OUTPUT_PORT} -t 127.0.0.1 -m custom', 1))
        self.result.pop('passed', None)
        self.result.update(operator_session=True, mavlink_input_port=INPUT_PORT, px4_instance=INSTANCE)
        self.active_control = self.work / 'control.csv'
        self.restart_count = 0
        self.metadata = dict(ready=False, work_directory=str(self.work), control_csv=str(self.active_control),
                             truth_csv=str(self.work / 'truth.csv'), instance=INSTANCE, input_port=INPUT_PORT,
                             nose_up=nose_up)
        self.save_metadata()

    def save_metadata(self):
        target = self.evidence / 'session.json'
        temporary = target.with_suffix('.tmp')
        temporary.write_text(json.dumps(self.metadata, indent=2) + '\n')
        temporary.replace(target)

    def status(self):
        output, _ = self.client('research_stabilized_sitl', 'status')
        match = re.search(r'state=(\d+) ready=(\d+) fault=(\d+)', output)
        assert match, output
        state, ready, fault = map(int, match.groups())
        if state == 4 and fault == 2:
            print('Mode: Disabled. Restart opens an explicit new session.', flush=True)
        elif state == 4:
            print('Mode: Fault. ' + FAULT_NAMES.get(fault, 'Control rejected') + '. Restart required.', flush=True)
        else:
            print('Mode:', MODE_NAMES.get(state, 'Unknown'), flush=True)
        return state, ready, fault

    def sticks(self, values):
        assert len(values) == 3 and all(math.isfinite(value) and -1 <= value <= 1 for value in values), \
            'Use three finite stick values from -1 to 1'
        roll, pitch, yaw = values
        # Body FRD pitch follows the existing declared configuration's sign.
        self.command = (-round(pitch * 1000), round(roll * 1000), round(yaw * 1000))
        self.action('operator_sticks', ';'.join(map(str, values)))

    def restart(self):
        self.sticks((0, 0, 0))
        self.client('research_stabilized_sitl', 'stop')
        self.restart_count += 1
        self.active_control = self.work / f'restarted-{self.restart_count}.csv'
        self.client('research_stabilized_sitl', 'start', 'slaaps_fixture', str(self.active_control))
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            status, _ = self.client('research_stabilized_sitl', 'status')
            manual, _ = self.client('listener', 'manual_control_setpoint', '-n', '1')
            axes = [re.search(r'\b' + axis + r':\s*([-+\d.eE]+)', manual) for axis in ('roll', 'pitch', 'yaw')]
            centered = all(match and math.isfinite(float(match[1])) and abs(float(match[1])) <= 0.04 for match in axes)
            if 'state=1' in status and 'ready=1' in status and centered:
                break
            time.sleep(0.02)
        else:
            raise RuntimeError('New session did not receive centered input and valid selected state')
        self.client('research_stabilized_sitl', 'enable')
        self.wait(200000)
        state, _, fault = self.status()
        assert state == 3 and fault == 0, 'Explicit new session did not enter hold'
        self.metadata.update(control_csv=str(self.active_control), restart_count=self.restart_count)
        self.save_metadata()


def console(profile_path, nose_up):
    profile, evidence = qualified_profile(profile_path)
    # All console instances coordinate through this lock. Separate fixed ports
    # and a PX4 instance avoid the campaign harness's endpoints.
    with Path('/var/tmp/research-stabilized-manual-console.lock').open('a+') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
            probe.bind(('127.0.0.1', INPUT_PORT))
        out = session_artifacts(profile, evidence)
        print('Session:', out, flush=True)
        fixture = ConsoleFixture(out, nose_up)
        try:
            fixture.start()
            fixture.metadata['ready'] = True
            fixture.metadata.update(px4_pid=fixture.px4.pid, gazebo_pid=fixture.gz.pid)
            fixture.save_metadata()
            print('Synthetic rotational fixture: airflow 20 m/s, fixed simulated EDF 10 N.', flush=True)
            print('Commands: sticks <roll> <pitch> <yaw> in [-1,1], hold, status, disable, restart, quit.', flush=True)
            print('Sticks persist until changed; hold centers all sticks and captures the released attitude.', flush=True)
            fixture.status()
            while True:
                try:
                    line = input('manual> ')
                except EOFError:
                    break
                words = line.split()
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
                        fixture.client('research_stabilized_sitl', 'disable')
                        fixture.action('operator_disable')
                        print('Disable requested; restart opens an explicit new session.', flush=True)
                    elif words == ['restart']:
                        fixture.restart()
                    elif words == ['quit']:
                        break
                    else:
                        raise ValueError('Use sticks, hold, status, disable, restart or quit')
                except (ValueError, AssertionError) as error:
                    fixture.sticks((0, 0, 0))
                    print(str(error) + '; hold requested.', flush=True)
            fixture.result['operator_exit'] = 'clean'
        except KeyboardInterrupt:
            fixture.result['operator_exit'] = 'interrupted'
        except Exception as error:
            fixture.result['operator_error'] = str(error)
            raise
        finally:
            fixture.metadata['ready'] = False
            fixture.metadata['closed'] = True
            fixture.close()
            fixture.metadata.update(px4_returncode=fixture.px4.poll() if fixture.px4 else None,
                                    gazebo_returncode=fixture.gz.poll() if fixture.gz else None)
            fixture.save_metadata()
        print('Session closed. Logs:', fixture.work, flush=True)


if __name__ == '__main__':
    def interrupt(signum, frame):
        raise KeyboardInterrupt

    signal.signal(signal.SIGTERM, interrupt)
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--profile', type=Path, default=DEFAULT_PROFILE)
    parser.add_argument('--level', action='store_true', help='Start level instead of the default body-X-up pose')
    args = parser.parse_args()
    console(args.profile.resolve(), not args.level)
