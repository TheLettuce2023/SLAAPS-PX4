#!/usr/bin/env python3
"""Verify a checkout-built private mode; this is not full campaign admission."""
import argparse
import json
from pathlib import Path
import shutil
import sys

import build_stabilized_manual_checkout as builder
import seal_stabilized_campaigns as audit
import verify_stabilized_publisher_epoch as epoch
import verify_stabilized_publisher_lifecycle as lifecycle
import verify_stabilized_sitl as base

CASES = (
    ('selftest', 'verify_stabilized_publisher_epoch.py', ('--selftest',)),
    ('dispatch', 'verify_stabilized_dispatch.py', ()),
    ('coupled-level', 'verify_stabilized_coupled.py', ()),
    ('coupled-up', 'verify_stabilized_coupled.py', ('--nose-up',)),
    ('advertisement-up', 'verify_stabilized_publisher_epoch.py', ('--case', 'attitude', '--nose-up')),
    ('restart-up', 'verify_stabilized_publisher_lifecycle.py', ('--case', 'rapid_restart', '--nose-up')),
)
COUPLED_CHECKS = {
    'rate_tracking', 'capture', 'retention', 'final_physical_error', 'final_estimated_error',
    'final_rate', 'peak', 'magnitude_saturation', 'all_axis_feedback',
    'directional_antiwindup', 'magnetic_rigidity', 'consumer_disable',
}


def external_sources(out):
    """Capture stock CMake-fetched Git inputs, separate from checkout files."""
    native = out / 'sitl-lockstep-build'
    dependencies = {}
    for directory in sorted(native.rglob('.git')):
        root = directory.parent
        prefix = root.relative_to(native)
        files = {}
        for name in builder.git(root, 'ls-files', '-z').split(b'\0'):
            if not name:
                continue
            relative = builder.relative_path(name.decode())
            source = root / relative
            assert source.is_file() and not source.is_symlink()
            target = out / 'external-compiled-inputs' / prefix / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, target)
            files[str(relative)] = base.digest(target)
        dependencies[str(prefix)] = dict(head=builder.git(root, 'rev-parse', 'HEAD').decode().strip(),
                                         tracked_sources=files)
    (out / 'external-source-origin.json').write_text(json.dumps(dependencies, indent=2) + '\n')
    return dependencies


def run(out):
    record = epoch.origin(out)
    # Exercise the continuation guard on a copied control source. No build or
    # simulator is running here, and the original bytes are always restored.
    target = out / 'px4-source-publisher-epoch/src/lib/research_stabilized_manual/CanardAllocator.cpp'
    original = target.read_bytes()
    rejected = False
    try:
        target.write_bytes(original + b'\n// private snapshot tamper probe\n')
        try:
            builder.verify_sources(out, record)
        except RuntimeError as error:
            assert str(error) == 'Staged source changed: src/lib/research_stabilized_manual/CanardAllocator.cpp'
            rejected = True
    finally:
        target.write_bytes(original)
    assert rejected and target.read_bytes() == original
    builder.verify_sources(out, record)
    (out / 'checkout-source-guard.json').write_text(json.dumps(dict(
        passed=True, scope='copied_source_tamper_rejected_before_build_with_exact_restore',
        restored_sha256=base.digest(target), no_compilation_during_probe=True), indent=2) + '\n')
    for name, script, arguments in CASES:
        print('RUN checkout-built', name, flush=True)
        base.run([sys.executable, str(Path(__file__).parent / script), '--evidence', str(out), *arguments],
                 out, 'checkout-verification-' + name + '.log')
        print('PASS checkout-built', name, flush=True)


def seal(out):
    record = epoch.origin(out)
    assert not record['historical_sources_used'] and not record['historical_binaries_used']
    assert not record['production_source_changed'] and not record['vehicle_qualified']
    assert base.digest(Path(builder.__file__)) == record['builder_sha256']
    assert base.digest(out / Path(builder.__file__).name) == record['builder_sha256']
    guard = json.loads((out / 'checkout-source-guard.json').read_text())
    assert guard['passed'] and guard['no_compilation_during_probe']
    assert guard['restored_sha256'] == record['working_inputs'][
        'src/lib/research_stabilized_manual/CanardAllocator.cpp']['sha256']
    builder.verify_sources(out, record)
    commands = json.loads((out / 'sitl-lockstep-build/compile_commands.json').read_text())
    source = out / 'px4-source-publisher-epoch'
    for name in ('StabilizedManualSITL.cpp', 'PublisherRevisionSelfTest.cpp', 'uORBDeviceNode.cpp'):
        entries = [entry for entry in commands if Path(entry['file']).name == name]
        assert entries and all('RESEARCH_STABILIZED_PUBLISHER_EPOCH=1' in entry['command'] for entry in entries)
        assert all(Path(entry['file']).is_relative_to(source) for entry in entries)
    groups = ('dispatch-*', 'coupled-level-*', 'coupled-up-*', 'epoch-attitude-up-*', 'barrier-rapid_restart-up-*')
    results, cycles = [], 0
    for group in groups:
        paths = sorted(out.glob(group + '/result.json'), key=lambda path: path.stat().st_mtime_ns)
        assert paths, 'Missing finite checkout-built case: ' + group
        path = paths[-1]
        work, result = path.parent, json.loads(path.read_text())
        assert result['passed'], 'Latest finite case failed: ' + str(work)
        assert result['px4_sha256'] == record['variant_px4_sha256']
        assert result['plugin_sha256'] == record['plugin_sha256']
        audit.audit_runtime_profile(work, result)
        for name, identity in result['source_sha256'].items():
            assert base.digest(base.ROOT / name) == identity
            assert base.digest(work / 'source-inputs' / name) == identity
        count = 0
        for name in ('control.csv', 'restarted.csv'):
            if not (work / name).exists():
                continue
            rows = epoch.closed_rows(work / name)
            first = (rows[0]['source_epoch'], *(rows[0][field] for field in epoch.REVISION_FIELDS))
            assert all(int(value) > 0 for value in first)
            assert all((row['source_epoch'], *(row[field] for field in epoch.REVISION_FIELDS)) == first for row in rows)
            count += audit.audit_cycles(work / name)
        if 'audited_control_cycles' in result:
            assert count == result['audited_control_cycles']
        else:
            # The stock state-barrier runner leaves counting to its sealer.
            assert result.get('barrier_case') == 'rapid_restart' and result['lifecycle_verified']
        if result.get('coupled'):
            assert set(result['checks']) == COUPLED_CHECKS and all(result['checks'].values())
            assert result['antiwindup_violations'] == 0
            assert result['coupled_source_sha256'] == base.digest(work / 'verify_stabilized_coupled.py')
        if result.get('barrier_case'):
            assert result['lifecycle_verified']
            derived = lifecycle.inspect(work, result)
            for key, value in derived.items():
                assert result[key] == (list(value) if isinstance(value, tuple) else value)
        results.append(dict(directory=work.name, audited_control_cycles=count, **{
            key: result[key] for key in ('final_physical_error_rad', 'rate_errors_rad_s', 'consumer_off_delay_us')
            if key in result}))
        cycles += count
    paths = sorted(out.glob('publisher-selftest-*/result.json'), key=lambda path: path.stat().st_mtime_ns)
    assert paths
    selftest = json.loads(paths[-1].read_text())
    assert selftest['passed'] and selftest['checks'] == 14
    assert selftest['px4_sha256'] == record['variant_px4_sha256']
    dependencies = external_sources(out)
    summary = dict(scope='checkout_built_private_native_five_case_smoke_and_revision_checks',
                   passed=True, native_uorb_checks=14, finite_sitl_cases=5,
                   audited_control_cycles=cycles, cases=results,
                   indexed_and_overlay_inputs=len(record['working_inputs']),
                   repositories=len(record['repositories']),
                   external_git_dependencies={name: value['head'] for name, value in dependencies.items()},
                   variant_px4_sha256=record['variant_px4_sha256'], plugin_sha256=record['plugin_sha256'],
                   physical_authority_granted=False, full_campaign_qualified=False,
                   existing_default_replaced=False)
    (out / 'checkout-verification-summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    shutil.copyfile(Path(__file__), out / Path(__file__).name)
    files = {}
    for path in out.iterdir():
        if path.is_file() and path.name != 'checkout-build-manifest.json':
            files[path.name] = base.digest(path)
        elif path.is_dir() and path.name.startswith(('dispatch-', 'coupled-', 'epoch-', 'barrier-',
                                                    'publisher-selftest-', 'tested-artifacts', 'px4-source',
                                                    'external-compiled-inputs')):
            # The full staged Git tree is separately bound by its indexed input
            # inventory. Include only the small original fixture snapshot here.
            if path.name == 'px4-source-publisher-epoch':
                continue
            for artifact in path.rglob('*'):
                if artifact.is_file() and not artifact.is_symlink():
                    files[str(artifact.relative_to(out))] = base.digest(artifact)
    for name in ('sitl-lockstep-build/bin/px4', 'sitl-lockstep-build/compile_commands.json',
                 'sitl-lockstep-build/boardconfig', 'sitl-lockstep-build/CMakeCache.txt',
                 'plugin-build/CMakeCache.txt', 'plugin-build/libresearch_rotational_fixture.so'):
        files[name] = base.digest(out / name)
    (out / 'checkout-build-manifest.json').write_text(json.dumps(dict(**summary, files=files), indent=2) + '\n')
    print('PASS fresh checkout build verification:', cycles, 'control cycles;', len(files), 'artifacts;',
          'manifest', base.digest(out / 'checkout-build-manifest.json'), flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--evidence', type=Path, required=True)
    parser.add_argument('--seal', action='store_true')
    args = parser.parse_args()
    out = args.evidence.resolve()
    assert out.parent == Path('/var/tmp') and out.name.startswith('research-stabilized-sitl-')
    seal(out) if args.seal else run(out)
