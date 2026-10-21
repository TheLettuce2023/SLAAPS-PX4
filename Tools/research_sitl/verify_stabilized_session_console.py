#!/usr/bin/env python3
"""Exercise the actual candidate console over stdin and audit its plant/control logs."""
import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import queue
import re
import shutil
import subprocess
import sys
import tempfile
import threading
import time

ROOT = Path(__file__).resolve().parents[2]


def rows(path):
    if not path.exists():
        return []
    with path.open() as stream:
        return [r for r in csv.DictReader(stream) if None not in r and all(v is not None for v in r.values())]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--qualification', type=Path, required=True)
    args = parser.parse_args()
    out = Path(tempfile.mkdtemp(prefix='research-stabilized-session-console-check-', dir='/var/tmp'))
    print(out, flush=True)
    proc = subprocess.Popen([sys.executable, str(ROOT / 'Tools/research_sitl/stabilized_session_console.py'),
                             '--qualification', str(args.qualification)], cwd=ROOT,
                            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            text=True, bufsize=1)
    lines = queue.Queue()
    log = (out / 'console.log').open('w', buffering=1)
    def collect():
        for line in proc.stdout:
            log.write(line)
            lines.put(line)
    reader = threading.Thread(target=collect, daemon=True)
    reader.start()
    def expect(pattern, timeout=100):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            try:
                line = lines.get(timeout=.2)
            except queue.Empty:
                assert proc.poll() is None, 'Console exited before ' + pattern
                continue
            match = re.search(pattern, line)
            if match:
                return match
        raise RuntimeError('Console timeout: ' + pattern)
    def send(command):
        proc.stdin.write(command + '\n')
        proc.stdin.flush()
    result = dict(passed=False, qualification=str(args.qualification))
    session = None
    try:
        session = Path(expect(r'Session: (/var/tmp/[^\s]+)').group(1))
        expect('Mode: Attitude hold')
        metadata = json.loads((session / 'session.json').read_text())
        control = Path(metadata['control_csv'])
        truth = Path(metadata['truth_csv'])
        def wait_for(predicate, path, timeout=10):
            deadline = time.monotonic() + timeout
            while time.monotonic() < deadline:
                data = rows(path)
                if data and predicate(data[-1]):
                    return data[-1]
                assert proc.poll() is None
                time.sleep(.02)
            raise RuntimeError('Recorded state did not meet expectation: ' + str(path))
        send('sticks 0.3 0 0'); expect('Body-axis sticks requested')
        wait_for(lambda r: r['state']=='2' and float(r['roll'])>.29, control)
        send('hold'); expect('Attitude hold requested')
        wait_for(lambda r: r['state']=='3' and float(r['roll'])==0, control)
        send('sticks 0.2 0 0'); expect('Body-axis sticks requested')
        wait_for(lambda r: r['state']=='2', control)
        send('sticks nan 0 0'); expect('finite stick values.*hold requested')
        wait_for(lambda r: r['state']=='3' and float(r['roll'])==0, control)
        send('restart'); expect('plant disarm confirmed'); expect('Mode: Attitude hold')
        wait_for(lambda r: r['enabled']=='1' and float(r['thrust'])==10, truth)
        send('disable'); expect('plant disarm confirmed')
        wait_for(lambda r: r['enabled']=='0' and float(r['thrust'])==0, truth)
        send('restart'); expect('Mode: Attitude hold')
        wait_for(lambda r: r['enabled']=='1' and float(r['thrust'])==10, truth)
        send('quit')
        proc.wait(timeout=20)
        assert proc.returncode == 0
        metadata = json.loads((session / 'session.json').read_text())
        assert metadata['closed'] and not metadata['ready']
        for key in ('px4_pid', 'gazebo_pid'):
            assert not Path('/proc', str(metadata[key])).exists(), 'Process survived console cleanup'
        physical = rows(truth)
        assert physical[-1]['enabled']=='0' and float(physical[-1]['thrust'])==0
        assert all(float(physical[-1]['t'+str(i)])==0 for i in range(4))
        assert len({r['session'] for r in physical if r['enabled']=='1'})==3
        result.update(passed=True, session=str(session), checks=['sticks','hold','invalid_input_centers',
                      'active_restart','confirmed_disable','explicit_restart','quit_disarm','process_cleanup'])
    except Exception as error:
        result['error'] = repr(error)
    finally:
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=20)
            except subprocess.TimeoutExpired:
                proc.kill(); proc.wait()
        reader.join(timeout=5)
        log.close()
    (out / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    shutil.copy2(__file__, out / Path(__file__).name)
    digest = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    files = {str(p.relative_to(out)): digest(p) for p in out.rglob('*') if p.is_file()}
    session_files = {}
    if session is not None:
        for p in session.rglob('*'):
            if p.is_file() and not p.is_symlink() and not any(x in ('sitl-lockstep-build','px4-source') for x in p.relative_to(session).parts):
                session_files[str(p.relative_to(session))] = digest(p)
    receipt = out / 'console-manifest.json'
    receipt.write_text(json.dumps(dict(files=files, session=str(session), session_files=session_files), indent=2)+'\n')
    assert all(digest(out/name)==h for name,h in files.items())
    assert all(digest(session/name)==h for name,h in session_files.items())
    print(json.dumps(result,indent=2), flush=True)
    print('receipt',digest(receipt),flush=True)
    return 0 if result['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
