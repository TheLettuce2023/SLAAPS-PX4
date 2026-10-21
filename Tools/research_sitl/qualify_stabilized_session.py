#!/usr/bin/env python3
"""Fresh, source-bound integrated candidate campaign; does not promote a console."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / 'Tools/research_sitl'
FAULTS = ('manual_loss', 'wrong_source', 'estimator_loss', 'routing', 'process_stall',
          'producer_stall', 'consumer_stall', 'drop_ack', 'delay_ack', 'drop_command',
          'drop_grant', 'drop_disarm', 'replace_command_source', 'replace_ack_source')


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def verify(receipt):
    data = json.loads(receipt.read_text())
    for name, identity in data['files'].items():
        assert digest(receipt.parent / name) == identity, name
    return data


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--evidence', type=Path, required=True)
    args = parser.parse_args()
    build = args.evidence.resolve()
    assert build.parent == Path('/var/tmp') and build.name.startswith('research-stabilized-sitl-')
    build_receipt = build / 'session-link-manifest.json'
    verify(build_receipt)
    source = build / 'verified-session-source'
    origin = json.loads((build / 'checkout-source-origin.json').read_text())
    donors = {item['destination']: name for name, item in origin['session_message_donors'].items()}
    def current_path(file):
        name = str(file.relative_to(source))
        return ROOT / donors.get(name, name)
    for file in source.rglob('*'):
        if file.is_file():
            current = current_path(file)
            assert current.is_file() and digest(current) == digest(file), str(current)
    out = Path(tempfile.mkdtemp(prefix='research-stabilized-session-qualification-', dir='/var/tmp'))
    print(out, flush=True)
    inputs = {}
    for name in ('qualify_stabilized_session.py', 'verify_stabilized_session_runtime.py',
                 'session_transport_relay.py', 'verify_stabilized_session_maneuvers.py',
                 'verify_stabilized_campaigns.py', 'verify_stabilized_coupled.py', 'seal_stabilized_campaigns.py'):
        inputs[name] = digest(TOOLS / name)
        shutil.copy2(TOOLS / name, out / name)
    jobs = [('single_axis', 'verify_stabilized_session_maneuvers.py', [], 12),
            ('coupled', 'verify_stabilized_session_maneuvers.py', ['--coupled'], 4)]
    jobs += [(case, 'verify_stabilized_session_runtime.py', ['--fault', case], 1) for case in FAULTS]
    results = []
    for name, script, options, expected_count in jobs:
        print('RUN', name, flush=True)
        log = out / (name + '.log')
        with log.open('x') as stream:
            child = subprocess.Popen([sys.executable, str(TOOLS / script), '--evidence', str(build), *options],
                                     cwd=ROOT, stdout=stream, stderr=subprocess.STDOUT, start_new_session=True)
            try:
                code = child.wait(timeout=900)
            except subprocess.TimeoutExpired:
                os.killpg(child.pid, signal.SIGTERM)
                try:
                    child.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    os.killpg(child.pid, signal.SIGKILL)
                    child.wait()
                code = -1
        match = re.search(r'^(/var/tmp/research-stabilized-session-(?:runtime|maneuvers)-[^\s]+)$', log.read_text(), re.M)
        item = dict(case=name, passed=False, returncode=code, expected_cases=expected_count)
        if match:
            directory = Path(match.group(1))
            receipt = directory / ('runtime-manifest.json' if expected_count == 1 else 'maneuver-manifest.json')
            if receipt.exists():
                data = verify(receipt)
                result = json.loads((directory / ('result.json' if expected_count == 1 else 'summary.json')).read_text())
                assert result['build_manifest_sha256' if expected_count == 1 else 'build_receipt_sha256'] == digest(build_receipt)
                if expected_count != 1:
                    assert result['planned_cases'] == len(result['results']) == expected_count
                item.update(passed=(code == 0 and result['passed']), directory=str(directory),
                            receipt_sha256=digest(receipt), receipt_name=receipt.name, files=len(data['files']))
        results.append(item)
        status = dict(scope='30_declared_integrated_cases_not_vehicle_acceptance', build=str(build),
                      build_receipt_sha256=digest(build_receipt), planned_cases=30,
                      completed_cases=sum(r['expected_cases'] for r in results),
                      passed=len(results) == len(jobs) and all(r['passed'] for r in results), results=results)
        (out / 'qualification.json').write_text(json.dumps(status, indent=2) + '\n')
        print('PASS' if item['passed'] else 'FAIL', name, item.get('directory', ''), flush=True)
    for name, identity in inputs.items():
        assert digest(TOOLS / name) == identity, 'Runner changed during campaign: ' + name
    for file in source.rglob('*'):
        if file.is_file():
            assert digest(current_path(file)) == digest(file)
    files = {p.name: digest(p) for p in out.iterdir() if p.is_file()}
    receipt = out / 'qualification-manifest.json'
    receipt.write_text(json.dumps(dict(files=files, child_receipts=results), indent=2) + '\n')
    verify(receipt)
    print('QUALIFICATION', status['passed'], 'receipt', digest(receipt), flush=True)
    return 0 if status['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
