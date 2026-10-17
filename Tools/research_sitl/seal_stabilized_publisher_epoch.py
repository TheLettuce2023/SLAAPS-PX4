#!/usr/bin/env python3
"""Seal private uORB revision, frozen-clock and coupled-control evidence."""
import argparse
import json
from pathlib import Path
import re
import shutil
import subprocess

import seal_stabilized_campaigns as audit
import seal_stabilized_mixed as mixed_audit
import verify_stabilized_publisher_epoch as epoch
import verify_stabilized_publisher_epoch_frozen as frozen
import verify_stabilized_sitl as base


def seal(evidence):
    record = epoch.origin(evidence)
    epoch.seal(evidence)
    source = evidence / 'px4-source-publisher-epoch'
    checkpoint = json.loads((evidence / 'checkpoint.json').read_text())
    dependencies = {}
    for name, identity in checkpoint.items():
        if not name.startswith(('src/', 'platforms/', 'msg/', 'boards/')) or not (source / name).is_file():
            continue
        expected = record['patched_sources'].get(name, identity)
        assert base.digest(source / name) == expected, 'Compiled graph dependency differs: ' + name
        destination = evidence / 'audited-inputs' / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source / name, destination)
        dependencies[name] = expected
    (evidence / 'compiled-dependencies.json').write_text(json.dumps(dependencies, indent=2) + '\n')
    symbols = subprocess.check_output(['nm', '-C', str(evidence / 'sitl-lockstep-build/bin/px4')]).decode()
    assert 'research_stabilized_sitl_main' in symbols and 'StabilizedManualControl::cycle' in symbols
    assert 'research_bench_control_main' not in symbols and 'bench_active_test_main' not in symbols
    frozen_paths = sorted(evidence.glob('frozen-epoch-*/result.json'), key=lambda p: p.stat().st_mtime_ns)
    assert frozen_paths
    frozen_work = frozen_paths[-1].parent
    result = json.loads(frozen_paths[-1].read_text())
    assert result['passed'] and result['runner_sha256'] == base.digest(Path(frozen.__file__))
    assert result['runner_sha256'] == base.digest(frozen_work / Path(frozen.__file__).name)
    assert result['variant_px4_sha256'] == result['px4_sha256'] == record['variant_px4_sha256']
    assert result['published_before'] == result['published_after'] and result['unchanged_dispatches'] >= 2
    assert result['implicit_enable_returncode'] != 0
    audit.audit_runtime_profile(frozen_work, result)
    control = epoch.closed_rows(frozen_work / 'control.csv')
    events = epoch.closed_rows(frozen_work / 'control.csv.events.csv')
    stamp = result['frozen_control_time_us']
    injection = next(row for row in events if row['action'] == 'publisher_cycle')
    revoke = next(row for row in events if row['action'] == 'revoke')
    assert int(injection['time']) == int(revoke['time']) == stamp == int(control[-1]['time'])
    assert int(revoke['status']) == epoch.STATE_CHANGED and int(injection['status']) == 0
    diagnostics = re.findall(r'publisher_cycle topic=(\d+) before=(\d+) after=(\d+) publication=(\d+) last_control=(\d+) success=(\d+)',
                             (frozen_work / 'px4.log').read_text())
    assert len(diagnostics) == 1
    topic, before, after, _, last_control, success = map(int, diagnostics[0])
    assert topic == 1 and after > before > 0 and last_control == stamp and success == 1
    assert all(int(row['att_revision']) == before for row in control)
    assert all(int(row['att_revision']) == after for row in epoch.closed_rows(frozen_work / 'restarted.csv'))
    frozen_cycles = audit.audit_cycles(frozen_work / 'control.csv') + audit.audit_cycles(frozen_work / 'restarted.csv')
    assert frozen_cycles == result['audited_control_cycles']
    mixed = {}
    for path in sorted(evidence.glob('mixed-*/result.json'), key=lambda p: p.stat().st_mtime_ns):
        item = json.loads(path.read_text())
        mixed[(item['nose_up'], item['first_sign'])] = (path.parent, item)
    assert set(mixed) == {(False, 1), (True, 1)}, 'Require the two declared private-variant mixed cases'
    mixed_results = []
    for work, item in mixed.values():
        inspected = mixed_audit.inspect(work, item, evidence)
        rows = epoch.closed_rows(work / 'control.csv')
        assert all(row['source_epoch'] == rows[0]['source_epoch'] for row in rows)
        assert all(tuple(row[field] for field in epoch.REVISION_FIELDS) ==
                   tuple(rows[0][field] for field in epoch.REVISION_FIELDS) for row in rows)
        mixed_results.append(inspected)
    barrier_summary = json.loads((evidence / 'publisher-epoch-summary.json').read_text())
    summary = dict(scope='private_posix_revision_selftest_eight_barriers_frozen_guard_two_mixed_cases',
                   passed=True, uorb_selftest_checks=14, barrier_cases=8, frozen_cases=1, mixed_cases=2,
                   audited_control_cycles=barrier_summary['audited_control_cycles'] + frozen_cycles
                                          + sum(item['cycles'] for item in mixed_results),
                   frozen_guard=dict(directory=frozen_work.name, **result), mixed=mixed_results,
                   compiled_graph_dependency_count=len(dependencies),
                   actual_ekf_process_restart_tested=False, general_publisher_identity_qualified=False,
                   accepted_campaign_expansion=False, vehicle_qualification=False)
    (evidence / 'publisher-revision-summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    shutil.copy2(Path(__file__), evidence / Path(__file__).name)
    files = {}
    for path in evidence.iterdir():
        if path.is_file() and path.name != 'publisher-revision-manifest.json':
            files[path.name] = base.digest(path)
        elif path.is_dir() and path.name.startswith(('epoch-', 'frozen-epoch-', 'mixed-', 'publisher-selftest-',
                                                    'tested-artifacts', 'variant-inputs', 'audited-inputs')):
            for artifact in path.rglob('*'):
                if artifact.is_file() and not artifact.is_symlink():
                    files[str(artifact.relative_to(evidence))] = base.digest(artifact)
    for name in ('sitl-lockstep-build/bin/px4', 'sitl-lockstep-build/compile_commands.json',
                 'sitl-lockstep-build/boardconfig', 'plugin-build/libresearch_rotational_fixture.so'):
        files[name] = base.digest(evidence / name)
    manifest = dict(scope=summary['scope'], passed=True, physical_authority_granted=False,
                    preserved_anchor_count=len(checkpoint), files=files)
    (evidence / 'publisher-revision-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print('Sealed private revision variant:', len(files), 'artifacts;', summary['audited_control_cycles'],
          'cycles; manifest', base.digest(evidence / 'publisher-revision-manifest.json'), flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--evidence', type=Path, required=True)
    args = parser.parse_args()
    evidence = args.evidence.resolve()
    assert evidence.parent == Path('/var/tmp') and evidence.name.startswith('research-stabilized-sitl-')
    seal(evidence)
