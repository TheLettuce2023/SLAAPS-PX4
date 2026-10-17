#!/usr/bin/env python3
"""Exercise the operator console through stdin against actual PX4/Gazebo feedback."""
import argparse
import csv
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import time

import stabilized_manual_console as console
import verify_stabilized_campaigns as campaigns
import verify_stabilized_sitl as base


def rows(path):
    if not path.exists():
        return []
    with path.open() as stream:
        return [row for row in csv.DictReader(stream) if None not in row and None not in row.values()]


def hold_entry(path, after):
    data = rows(path)
    return next((row for previous, row in zip(data, data[1:])
                 if previous['state'] == '2' and row['state'] == '3'
                 and int(row['time']) > after
                 and all(float(row[axis]) == 0 for axis in ('roll', 'pitch', 'yaw'))), None)


def verify(profile_path):
    profile, _ = console.qualified_profile(profile_path)
    out = Path(tempfile.mkdtemp(prefix='research-stabilized-console-check-', dir='/var/tmp'))
    log_path = out / 'console.log'
    with log_path.open('w') as log:
        process = subprocess.Popen([sys.executable, str(Path(console.__file__)), '--profile', str(profile_path)],
                                   stdin=subprocess.PIPE, stdout=log, stderr=subprocess.STDOUT, text=True)
        session = None

        def wait(predicate, timeout=95):
            deadline = time.monotonic() + timeout
            while time.monotonic() < deadline:
                value = predicate()
                if value:
                    return value
                if process.poll() is not None:
                    raise RuntimeError('Console exited before expected state: ' + log_path.read_text()[-3000:])
                time.sleep(0.02)
            raise RuntimeError('Live console did not produce expected observation; cleanup follows')

        def metadata():
            if session is None or not (session / 'session.json').exists():
                return {}
            return json.loads((session / 'session.json').read_text())

        def send(command):
            assert process.poll() is None
            process.stdin.write(command + '\n')
            process.stdin.flush()

        try:
            found = wait(lambda: re.search(r'^Session: (.+)$', log_path.read_text(), re.MULTILINE))
            session = Path(found[1])
            assert session.parent == Path('/var/tmp') and session.name.startswith('research-stabilized-manual-session-')
            wait(lambda: metadata().get('ready'))
            initial = metadata()
            assert initial['nose_up'] and initial['instance'] == console.INSTANCE and initial['input_port'] == console.INPUT_PORT
            control = Path(initial['control_csv'])
            truth = Path(initial['truth_csv'])
            first_session = next(int(row['session']) for row in rows(truth) if row['enabled'] == '1')
            send('sticks 0.15 -0.10 0.10')
            rate = wait(lambda: next((row for row in reversed(rows(control)) if row['state'] == '2'
                and abs(float(row['roll']) - 0.15) < 1e-6 and abs(float(row['pitch']) - 0.10) < 1e-6
                and abs(float(row['yaw']) - 0.10) < 1e-6), None), 15)
            send('hold')
            held = wait(lambda: hold_entry(control, int(rate['time'])), 15)
            assert campaigns.separation(campaigns.quaternion(held), campaigns.quaternion(held, 'r')) < 2e-5
            # Capture belongs to the first transition row. Subsequent attitude
            # estimates can evolve while the controller retains its reference.
            retained = wait(lambda: next((row for row in reversed(rows(control)) if row['state'] == '3'
                and int(row['time']) >= int(held['time']) + 20000), None), 15)
            assert campaigns.separation(campaigns.quaternion(held, 'r'), campaigns.quaternion(retained, 'r')) < 2e-5
            send('sticks -0.15 0.10 -0.10')
            reversed_rate = wait(lambda: next((row for row in reversed(rows(control)) if row['state'] == '2'
                and int(row['time']) > int(held['time']) and float(row['roll']) < -0.14), None), 15)
            send('sticks nan 0 0')
            invalid_hold = wait(lambda: hold_entry(control, int(reversed_rate['time'])), 15)
            assert campaigns.separation(campaigns.quaternion(invalid_hold), campaigns.quaternion(invalid_hold, 'r')) < 2e-5
            send('disable')
            disabled = wait(lambda: next((row for row in reversed(rows(truth)) if row['enabled'] == '0'
                and int(row['session']) == first_session and int(row['time']) > int(invalid_hold['time'])), None), 15)
            assert disabled['thrust'] == '0' and disabled['lease_fault'] == '1'
            assert all(float(disabled['t' + str(i)]) == 0 for i in range(4))
            send('restart')
            restarted = wait(lambda: metadata() if metadata().get('restart_count') == 1 else None, 20)
            recovered = rows(Path(restarted['control_csv']))
            assert recovered and recovered[-1]['state'] == '3'
            second_session = wait(lambda: next((int(row['session']) for row in reversed(rows(truth))
                if row['enabled'] == '1' and int(row['session']) > first_session), None), 15)
            send('status')
            send('quit')
            process.stdin.close()
            process.wait(timeout=30)
            assert process.returncode == 0, log_path.read_text()[-3000:]
            final = metadata()
            assert final['closed'] and not final['ready']
            assert final['px4_returncode'] is not None and final['gazebo_returncode'] is not None
            assert 'hold requested.' in log_path.read_text()
            result = dict(passed=True, scope='actual_console_sticks_hold_invalid_input_disable_restart_cleanup',
                          default_qualification_manifest_sha256=profile['manifest_sha256'],
                          session=str(session), first_session=first_session, second_session=second_session,
                          capture_row_time_us=int(held['time']), retained_row_time_us=int(retained['time']),
                          capture_error_rad=campaigns.separation(campaigns.quaternion(held), campaigns.quaternion(held, 'r')),
                          reference_retention_error_rad=campaigns.separation(campaigns.quaternion(held, 'r'), campaigns.quaternion(retained, 'r')),
                          invalid_input_capture_error_rad=campaigns.separation(campaigns.quaternion(invalid_hold), campaigns.quaternion(invalid_hold, 'r')),
                          console_sha256=base.digest(Path(console.__file__)), verifier_sha256=base.digest(Path(__file__)),
                          px4_sha256=profile['px4_sha256'], plugin_sha256=profile['plugin_sha256'])
            (out / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
        finally:
            if process.poll() is None:
                try:
                    send('quit')
                    process.wait(timeout=30)
                except (BrokenPipeError, OSError, subprocess.TimeoutExpired):
                    process.terminate()  # Console SIGTERM handler runs fixture cleanup.
                    process.wait(timeout=30)
    for path in (Path(console.__file__), Path(__file__), profile_path):
        shutil.copy2(path, out / path.name)
    files = {str(path.relative_to(out)): base.digest(path) for path in out.iterdir() if path.is_file()}
    session_files = {str(path.relative_to(session)): base.digest(path) for path in session.rglob('*')
                     if path.is_file() and not path.is_symlink()}
    (out / 'console-manifest.json').write_text(json.dumps(dict(scope=result['scope'], passed=True,
        physical_authority_granted=False, files=files, session=str(session), session_files=session_files), indent=2) + '\n')
    print('PASS actual manual console:', out, 'manifest', base.digest(out / 'console-manifest.json'), flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--profile', type=Path, default=console.DEFAULT_PROFILE)
    args = parser.parse_args()
    verify(args.profile.resolve())
