#!/usr/bin/env python3
"""Native private advertisement-revision regressions; synthetic lifecycle injection."""
import argparse
import csv
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import time

import verify_stabilized_sitl as base
import verify_stabilized_campaigns as campaigns
import seal_stabilized_campaigns as audit

TOPICS = ('attitude', 'rate', 'estimator', 'flags')
REVISION_FIELDS = ('att_revision', 'rate_revision', 'est_revision', 'flags_revision')
STATE_CHANGED = 7  # Current Status::StateChanged; source hash is sealed below.


def origin(evidence):
    record = json.loads((evidence / 'publisher-epoch-origin.json').read_text())
    assert base.digest(evidence / 'sitl-lockstep-build/bin/px4') == record['variant_px4_sha256']
    assert base.digest(evidence / 'plugin-build/libresearch_rotational_fixture.so') == record['plugin_sha256']
    source = evidence / 'px4-source-publisher-epoch'
    for name, identity in record['patched_sources'].items():
        assert base.digest(source / name) == identity, 'Patched source changed: ' + name
    for name, identity in record['support_sources'].items():
        assert base.digest(base.ROOT / name) == identity, 'Private support changed: ' + name
        assert base.digest(source / base.FIXTURE.relative_to(base.ROOT) / Path(name).name) == identity
    return record


def selftest(evidence):
    record = origin(evidence)
    work = Path(tempfile.mkdtemp(prefix='publisher-selftest-', dir=evidence))
    script = work / 'rc.selftest'
    script.write_text('#!/bin/sh\nset -e\n. px4-alias.sh\nresearch_stabilized_sitl revision_selftest\nshutdown\n')
    with (work / 'px4.log').open('w') as log:
        result = subprocess.run([str(evidence / 'sitl-lockstep-build/bin/px4'), '-d', '-i', '29475',
                                 '-w', str(work), '-s', str(script)], cwd=work,
                                stdout=log, stderr=subprocess.STDOUT, timeout=30)
    output = (work / 'px4.log').read_text()
    match = re.search(r'publisher_revision_selftest checks=(\d+) failures=(\d+)', output)
    assert result.returncode == 0 and match and int(match[1]) == 14 and int(match[2]) == 0, output
    (work / 'result.json').write_text(json.dumps(dict(passed=True, checks=14,
        scope='native_disposable_uorb_topic_lifecycle_and_cache_binding',
        px4_sha256=record['variant_px4_sha256']), indent=2) + '\n')
    print('PASS native publisher revision selftest:', work, flush=True)


def closed_rows(path):
    with path.open() as stream:
        rows = list(csv.DictReader(stream))
    assert rows and all(None not in row and None not in row.values() for row in rows), 'Malformed closed CSV: ' + str(path)
    return rows


def barrier(evidence, topic, nose_up):
    record = origin(evidence)
    index = TOPICS.index(topic)
    fixture = campaigns.Fixture(evidence, 'epoch-' + topic + ('-up' if nose_up else '-level'), nose_up)
    fixture.result.update(epoch_topic=topic, runner_sha256=base.digest(Path(__file__)),
                          scope='synthetic_same_node_advertisement_cycle_between_owner_snapshots')
    shutil.copy2(Path(__file__), fixture.work / Path(__file__).name)
    try:
        fixture.start()
        fixture.wait(500000)
        fixture.action('cycle_publisher_request', topic)
        output, code = fixture.client('research_stabilized_sitl', 'cycle_publisher', topic)
        (fixture.work / 'cycle-request.log').write_text(output)
        assert code == 0
        fixture.wait(100000)
        status, _ = fixture.client('research_stabilized_sitl', 'status')
        (fixture.work / 'fault-status.log').write_text(status)
        assert 'state=4' in status and 'ready=0' in status and f'fault={STATE_CHANGED}' in status, status
        output, code = fixture.client('research_stabilized_sitl', 'enable', check=False)
        (fixture.work / 'implicit-enable.log').write_text(output)
        assert code != 0
        fixture.result['implicit_enable_returncode'] = code
        fixture.wait(200000)
        status, _ = fixture.client('research_stabilized_sitl', 'status')
        assert 'state=4' in status, 'Fault latch silently recovered'
        fixture.client('research_stabilized_sitl', 'stop')
        fixture.client('research_stabilized_sitl', 'start', 'slaaps_fixture', str(fixture.work / 'restarted.csv'))
        deadline = time.monotonic() + 65
        while time.monotonic() < deadline:
            status, _ = fixture.client('research_stabilized_sitl', 'status')
            if 'state=1' in status and 'ready=1' in status:
                break
            time.sleep(0.05)
        else:
            raise RuntimeError('Explicit fresh owner did not become Ready')
        fixture.action('explicit_reenable')
        fixture.client('research_stabilized_sitl', 'enable')
        fixture.wait(1000000)
        status, _ = fixture.client('research_stabilized_sitl', 'status')
        assert 'state=3' in status and 'fault=0' in status, status
        fixture.client('research_stabilized_sitl', 'disable')
        fixture.wait(300000)
    except Exception as error:
        fixture.result['error'] = str(error)
        raise
    finally:
        fixture.close()
    control = closed_rows(fixture.work / 'control.csv')
    recovery = closed_rows(fixture.work / 'restarted.csv')
    events = closed_rows(fixture.work / 'control.csv.events.csv')
    injection = next(row for row in events if row['action'] == 'publisher_cycle')
    revoke = next(row for row in events if row['action'] == 'revoke')
    assert int(injection['status']) == 0 and int(revoke['status']) == STATE_CHANGED
    assert injection['time'] == revoke['time'], 'Changed revision was not revoked in the injection dispatch'
    diagnostics = re.findall(r'publisher_cycle topic=(\d+) before=(\d+) after=(\d+) publication=(\d+) last_control=(\d+) success=(\d+)',
                             (fixture.work / 'px4.log').read_text())
    assert len(diagnostics) == 1
    diagnostic = list(map(int, diagnostics[0]))
    assert diagnostic[0] == index + 1 and diagnostic[2] > diagnostic[1] > 0 and diagnostic[5] == 1
    assert diagnostic[4] == int(control[-1]['time']) <= int(injection['time'])
    assert all(row['source_epoch'] == control[0]['source_epoch'] for row in control)
    assert all(tuple(row[field] for field in REVISION_FIELDS) == tuple(control[0][field] for field in REVISION_FIELDS) for row in control)
    assert int(control[-1][REVISION_FIELDS[index]]) == diagnostic[1]
    assert all(int(row[REVISION_FIELDS[index]]) == diagnostic[2] for row in recovery)
    assert all((row['gyro_id'], row['accel_id'], row['reset']) ==
               (control[0]['gyro_id'], control[0]['accel_id'], control[0]['reset']) for row in recovery)
    age = int(revoke['time']) - int(control[-1]['att_time'])
    assert 0 <= age < 100000, 'Only a stale-state expiry was exercised'
    truth = closed_rows(fixture.work / 'truth.csv')
    first_active = next(i for i, row in enumerate(truth) if row['enabled'] == '1')
    first_off = next(i for i in range(first_active + 1, len(truth)) if truth[i]['enabled'] == '0')
    first_recovery = next(i for i in range(first_off + 1, len(truth)) if truth[i]['enabled'] == '1')
    assert all(row['thrust'] == '0' and row['lease_fault'] == '1'
               and all(float(row['t' + str(i)]) == 0 for i in range(4)) for row in truth[first_off:first_recovery])
    assert int(truth[first_recovery]['session']) > int(truth[first_active]['session']) > 0
    assert truth[-1]['thrust'] == '0' and truth[-1]['lease_fault'] == '1'
    off_delay = int(truth[first_off]['time']) - int(revoke['time'])
    assert 0 <= off_delay <= 10000
    cycles = audit.audit_cycles(fixture.work / 'control.csv') + audit.audit_cycles(fixture.work / 'restarted.csv')
    fixture.result.update(passed=True, audited_control_cycles=cycles,
                          revision_before=diagnostic[1], revision_after=diagnostic[2],
                          revoke_time_us=int(revoke['time']), attitude_age_at_revoke_us=age,
                          consumer_off_delay_us=off_delay,
                          variant_px4_sha256=record['variant_px4_sha256'])
    (fixture.work / 'result.json').write_text(json.dumps(fixture.result, indent=2) + '\n')
    print('PASS', topic, 'up' if nose_up else 'level', 'cycles', cycles, 'off delay us', off_delay, flush=True)


def seal(evidence):
    record = origin(evidence)
    chosen = {}
    for path in sorted(evidence.glob('epoch-*/result.json'), key=lambda p: p.stat().st_mtime_ns):
        result = json.loads(path.read_text())
        chosen[(result['epoch_topic'], result['nose_up'])] = (path.parent, result)
    assert set(chosen) == {(topic, up) for topic in TOPICS for up in (False, True)}
    assert all(result['passed'] for _, result in chosen.values()), 'Latest native epoch case failed'
    selftests = sorted(evidence.glob('publisher-selftest-*/result.json'), key=lambda p: p.stat().st_mtime_ns)
    assert selftests and json.loads(selftests[-1].read_text())['passed']
    checkpoint = json.loads((evidence / 'checkpoint.json').read_text())
    for name, identity in checkpoint.items():
        assert base.digest(base.ROOT / name) == identity, 'Preparation anchor changed: ' + name
    cycles = 0
    for work, result in chosen.values():
        assert result['runner_sha256'] == base.digest(Path(__file__)) == base.digest(work / Path(__file__).name)
        assert result['variant_px4_sha256'] == result['px4_sha256'] == record['variant_px4_sha256']
        assert result['plugin_sha256'] == record['plugin_sha256']
        audit.audit_runtime_profile(work, result)
        cycles += audit.audit_cycles(work / 'control.csv') + audit.audit_cycles(work / 'restarted.csv')
    source = evidence / 'px4-source-publisher-epoch'
    archived = evidence / 'variant-inputs'
    for name, identity in record['patched_sources'].items():
        destination = archived / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source / name, destination)
        assert base.digest(destination) == identity
    for name, identity in record['support_sources'].items():
        destination = archived / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(base.ROOT / name, destination)
        assert base.digest(destination) == identity
    compile_commands = json.loads((evidence / 'sitl-lockstep-build/compile_commands.json').read_text())
    for name in ('StabilizedManualSITL.cpp', 'PublisherRevisionSelfTest.cpp', 'uORBDeviceNode.cpp'):
        entries = [entry for entry in compile_commands if Path(entry['file']).name == name]
        assert entries and all('RESEARCH_STABILIZED_PUBLISHER_EPOCH=1' in entry['command'] for entry in entries)
        assert all(str(source) in entry['file'] for entry in entries)
    shutil.copy2(Path(__file__), evidence / Path(__file__).name)
    summary = dict(scope='eight_synthetic_native_advertisement_revision_and_explicit_recovery_cases',
                   passed=True, selftest_checks=14, audited_control_cycles=cycles,
                   accepted_campaign_expansion=False, actual_ekf_process_restart_tested=False,
                   general_publisher_identity_qualified=False,
                   cases=[dict(directory=work.name, **result) for work, result in chosen.values()])
    (evidence / 'publisher-epoch-summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    files = {}
    for path in evidence.iterdir():
        if path.is_file() and path.name != 'publisher-epoch-manifest.json':
            files[path.name] = base.digest(path)
        elif path.is_dir() and path.name.startswith(('epoch-', 'publisher-selftest-', 'tested-artifacts', 'variant-inputs')):
            for artifact in path.rglob('*'):
                if artifact.is_file():
                    files[str(artifact.relative_to(evidence))] = base.digest(artifact)
    for name in ('sitl-lockstep-build/bin/px4', 'sitl-lockstep-build/compile_commands.json',
                 'sitl-lockstep-build/boardconfig', 'plugin-build/libresearch_rotational_fixture.so'):
        files[name] = base.digest(evidence / name)
    manifest = dict(scope=summary['scope'], passed=True, physical_authority_granted=False,
                    preserved_anchor_count=len(checkpoint), files=files)
    (evidence / 'publisher-epoch-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print('Sealed', len(files), 'artifacts;', cycles, 'cycles; manifest', base.digest(evidence / 'publisher-epoch-manifest.json'), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--evidence', type=Path, required=True)
    parser.add_argument('--case', choices=('all', *TOPICS), default='all')
    parser.add_argument('--nose-up', action='store_true')
    parser.add_argument('--selftest', action='store_true')
    parser.add_argument('--seal', action='store_true')
    args = parser.parse_args()
    evidence = args.evidence.resolve()
    assert evidence.parent == Path('/var/tmp') and evidence.name.startswith('research-stabilized-sitl-')
    if args.seal:
        seal(evidence)
    elif args.selftest:
        selftest(evidence)
    else:
        for topic in (TOPICS if args.case == 'all' else (args.case,)):
            barrier(evidence, topic, args.nose_up)


if __name__ == '__main__':
    main()
