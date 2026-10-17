#!/usr/bin/env python3
"""Qualify all 31 declared cases freshly on a sealed checkout-built pair."""
import argparse
import json
import os
from pathlib import Path
import shutil
import tempfile

import build_stabilized_manual_checkout as builder
import qualify_stabilized_publisher_revision as previous
import seal_stabilized_campaigns as campaign_audit
import seal_stabilized_coupled as coupled_audit
import verify_stabilized_campaigns as campaigns
import verify_stabilized_coupled as coupled
import verify_stabilized_dispatch as dispatch
import verify_stabilized_publisher_epoch as epoch
import verify_stabilized_publisher_lifecycle as lifecycle
import verify_stabilized_sitl as base
import verify_stabilized_state_barriers as barriers


def receipt(root, name, expected):
    assert base.digest(root / name) == expected, 'Receipt identity changed'
    manifest = json.loads((root / name).read_text())
    assert manifest['passed'] and not manifest['physical_authority_granted']
    for name, identity in manifest['files'].items():
        relative = builder.relative_path(name)
        assert base.digest(root / relative) == identity, 'Receipt artifact changed: ' + name
    return manifest


def stage_artifacts(source, out, record):
    shutil.copytree(source / 'px4-source', out / 'px4-source')
    shutil.copytree(source / 'sitl-lockstep-build/bin', out / 'sitl-lockstep-build/bin', symlinks=True)
    for name in ('boardconfig', 'compile_commands.json'):
        shutil.copy2(source / 'sitl-lockstep-build' / name, out / 'sitl-lockstep-build' / name)
    (out / 'plugin-build').mkdir()
    shutil.copy2(source / 'plugin-build/libresearch_rotational_fixture.so',
                 out / 'plugin-build/libresearch_rotational_fixture.so')
    for name, identity in record['patched_sources'].items():
        target = out / 'px4-source-publisher-epoch' / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source / 'px4-source-publisher-epoch' / name, target)
        assert base.digest(target) == identity
    for name, identity in record['support_sources'].items():
        target = out / 'px4-source-publisher-epoch' / base.FIXTURE.relative_to(base.ROOT) / Path(name).name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source / 'px4-source-publisher-epoch' / base.FIXTURE.relative_to(base.ROOT) / Path(name).name, target)
        assert base.digest(target) == identity
    slim = {name: value for name, value in record.items() if name not in ('working_inputs', 'repositories')}
    (out / 'publisher-epoch-origin.json').write_text(json.dumps(slim, indent=2) + '\n')
    epoch.origin(out)


def prepare(source, expected):
    manifest = receipt(source, 'checkout-build-manifest.json', expected)
    assert manifest['native_uorb_checks'] == 14 and manifest['finite_sitl_cases'] == 5
    assert not manifest['full_campaign_qualified']
    record = epoch.origin(source)
    assert not record['historical_sources_used'] and not record['historical_binaries_used']
    assert base.digest(Path(builder.__file__)) == record['builder_sha256']
    staged = source / 'px4-source-publisher-epoch'
    for name, metadata in record['working_inputs'].items():
        target = staged / builder.relative_path(name)
        if metadata['kind'] == 'symlink':
            assert target.is_symlink() and os.readlink(target) == metadata['target']
        else:
            assert base.digest(target) == record['patched_sources'].get(name, metadata['sha256']), name
    graph = json.loads((base.ROOT / 'docs/RESEARCH_CODE_GRAPH.json').read_text())
    checkpoint, compiled = {}, {}
    for node in graph['nodes']:
        if 'sha256' not in node:
            continue
        name, identity = node['path'], node['sha256']
        assert base.digest(base.ROOT / name) == identity, 'Stale graph: ' + name
        checkpoint[name] = identity
        if name in record['working_inputs'] and name.startswith(('src/', 'platforms/', 'msg/', 'boards/', 'Tools/')):
            assert record['working_inputs'][name]['sha256'] == identity, 'Compiled checkout input changed: ' + name
            compiled[name] = record['patched_sources'].get(name, identity)
    for path in base.FIXTURE.rglob('*'):
        if path.is_file():
            assert base.digest(path) == base.digest(source / 'px4-source' / path.relative_to(base.ROOT))
    out = Path(tempfile.mkdtemp(prefix='research-stabilized-sitl-', dir='/var/tmp'))
    print('Fresh qualification:', out, flush=True)
    proof = out / 'build-evidence'
    proof.mkdir()
    for name in manifest['files']:
        target = proof / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source / name, target)
    shutil.copy2(source / 'checkout-build-manifest.json', proof / 'checkout-build-manifest.json')
    receipt(proof, 'checkout-build-manifest.json', expected)
    stage_artifacts(source, out, record)
    child = out / 'fresh-lifecycle'
    child.mkdir()
    stage_artifacts(out, child, record)
    for directory in (out, child):
        (directory / 'checkpoint.json').write_text(json.dumps(checkpoint, indent=2) + '\n')
    for name, identity in compiled.items():
        target = out / 'compiled-inputs' / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(staged / name, target)
        assert base.digest(target) == identity
    (out / 'compiled-inputs.json').write_text(json.dumps(compiled, indent=2) + '\n')
    origin = dict(scope='all_31_cases_fresh_on_sealed_checkout_built_pair', build=str(source),
                  build_receipt_sha256=expected, runner_sha256=base.digest(Path(__file__)),
                  fresh_cases_required=31, inherited_cases=0,
                  variant_px4_sha256=record['variant_px4_sha256'], plugin_sha256=record['plugin_sha256'])
    (out / 'qualification-origin.json').write_text(json.dumps(origin, indent=2) + '\n')
    shutil.copy2(Path(__file__), out / Path(__file__).name)
    print(json.dumps(dict(evidence=str(out), fresh_cases=31, inherited_cases=0), indent=2), flush=True)


def run(out, phase):
    epoch.origin(out)
    if phase in ('all', 'dispatch'):
        previous.capture(out, 'dispatch', lambda: dispatch.regression(out))
    if phase in ('all', 'coupled'):
        for up in (False, True):
            for sign in (1, -1):
                previous.capture(out, f'coupled-{up}-{sign}', lambda up=up, sign=sign: coupled.coupled(out, up, sign))
    if phase in ('all', 'nominal'):
        for up in (False, True):
            for axis in range(3):
                for sign in (1, -1):
                    previous.capture(out, f'rate-{up}-{axis}-{sign}',
                                     lambda up=up, axis=axis, sign=sign: campaigns.campaign(out, axis, up, sign))
    if phase in ('all', 'failures'):
        for case in campaigns.FAILURES:
            previous.capture(out, 'failure-' + case, lambda case=case: campaigns.failure(out, case))
    if phase in ('all', 'lifecycle'):
        child = out / 'fresh-lifecycle'
        for up in (False, True):
            for case in barriers.CASES:
                previous.capture(child, 'lifecycle-' + case + '-' + str(up),
                                 lambda up=up, case=case: lifecycle.run(child, case, up))


def verify_component(out, name):
    path = out / (name + '-manifest.json')
    manifest = json.loads(path.read_text())
    assert manifest['accepted'] and not manifest['physical_authority_granted']
    for name, identity in manifest['files'].items():
        assert base.digest(out / builder.relative_path(name)) == identity, 'Component artifact changed: ' + name
    return base.digest(path)


def seal(out):
    origin = json.loads((out / 'qualification-origin.json').read_text())
    assert origin['runner_sha256'] == base.digest(Path(__file__))
    receipt(out / 'build-evidence', 'checkout-build-manifest.json', origin['build_receipt_sha256'])
    record = epoch.origin(out)
    assert record['variant_px4_sha256'] == origin['variant_px4_sha256']
    assert record['plugin_sha256'] == origin['plugin_sha256']
    compiled = json.loads((out / 'compiled-inputs.json').read_text())
    for name, identity in compiled.items():
        assert base.digest(out / 'compiled-inputs' / name) == identity
        if name not in record['patched_sources']:
            assert base.digest(base.ROOT / name) == identity
    # Freeze each component in its own scope; no later component overwrites a
    # previously bound preservation file. Lifecycle evidence has its own tree.
    dispatch.seal(out)
    coupled_audit.seal(out)
    campaign_audit.seal(out, False)
    child = out / 'fresh-lifecycle'
    barriers.seal(child)
    components = {name: verify_component(out, name) for name in ('dispatch', 'coupled', 'campaign')}
    components['state-barrier'] = verify_component(child, 'state-barrier')
    lifecycle_summary = json.loads((child / 'state-barrier-summary.json').read_text())
    assert len(lifecycle_summary['cases']) == 8
    for result in lifecycle_summary['cases']:
        work = child / result['directory']
        assert result['lifecycle_verified'] and result['passed']
        assert result['lifecycle_runner_sha256'] == base.digest(Path(lifecycle.__file__))
        assert base.digest(work / Path(lifecycle.__file__).name) == result['lifecycle_runner_sha256']
        for key, value in lifecycle.inspect(work, result).items():
            assert result[key] == (list(value) if isinstance(value, tuple) else value)
        previous.stable_revision_rows(work / 'control.csv')
        previous.stable_revision_rows(work / 'restarted.csv')
        assert result['px4_sha256'] == origin['variant_px4_sha256'] and result['plugin_sha256'] == origin['plugin_sha256']
    coupled_summary = json.loads((out / 'coupled-summary.json').read_text())
    campaign_summary = json.loads((out / 'campaign-summary.json').read_text())
    dispatch_result = json.loads((out / 'dispatch-manifest.json').read_text())['result']
    previous.stable_revision_rows(max(out.glob('dispatch-*/result.json'), key=lambda p: p.stat().st_mtime_ns).parent / 'control.csv')
    for result in coupled_summary['cases'] + campaign_summary['cases']:
        work = out / result['directory']
        previous.stable_revision_rows(work / 'control.csv')
        if (work / 'restarted.csv').exists():
            previous.stable_revision_rows(work / 'restarted.csv')
    cycles = (dispatch_result['audited_control_cycles'] + coupled_summary['audited_control_cycles']
              + campaign_summary['audited_control_cycles'] + lifecycle_summary['audited_control_cycles'])
    summary = dict(scope='31_fresh_declared_native_cases_on_checkout_built_revision_pair', accepted=True,
                   fresh_cases=31, inherited_cases=0, total_cases=31, audited_control_cycles=cycles,
                   component_receipts=components, build_receipt_sha256=origin['build_receipt_sha256'],
                   variant_px4_sha256=origin['variant_px4_sha256'], plugin_sha256=origin['plugin_sha256'],
                   vehicle_supported_tuning=False, general_publisher_identity_qualified=False,
                   coupled_cases=coupled_summary['cases'], lifecycle_cases=lifecycle_summary['cases'],
                   dispatch=dispatch_result)
    (out / 'revision-qualification-summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    shutil.copy2(Path(__file__), out / Path(__file__).name)
    prefixes = ('rate-', 'failure-', 'coupled-', 'dispatch-', 'tested-artifacts', 'compiled-inputs',
                'fresh-lifecycle', 'build-evidence', 'audited-inputs', 'px4-source')
    files = {}
    for path in out.iterdir():
        if path.is_file() and path.name != 'revision-qualification-manifest.json':
            files[path.name] = base.digest(path)
        elif path.is_dir() and path.name.startswith(prefixes):
            for artifact in path.rglob('*'):
                if artifact.is_file() and not artifact.is_symlink():
                    files[str(artifact.relative_to(out))] = base.digest(artifact)
    for path in (out / 'sitl-lockstep-build/bin').iterdir():
        if path.is_file() and not path.is_symlink():
            files[str(path.relative_to(out))] = base.digest(path)
    for name in ('sitl-lockstep-build/boardconfig', 'sitl-lockstep-build/compile_commands.json',
                 'plugin-build/libresearch_rotational_fixture.so'):
        files[name] = base.digest(out / name)
    (out / 'revision-qualification-manifest.json').write_text(json.dumps(dict(
        scope=summary['scope'], accepted=True, physical_authority_granted=False,
        fresh_cases=31, inherited_cases=0, files=files), indent=2) + '\n')
    print('31 fresh cases PASS;', cycles, 'cycles;', len(files), 'artifacts; receipt',
          base.digest(out / 'revision-qualification-manifest.json'), flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--from-build', type=Path)
    parser.add_argument('--build-receipt-sha256')
    parser.add_argument('--evidence', type=Path)
    parser.add_argument('--phase', choices=('all', 'dispatch', 'coupled', 'nominal', 'failures', 'lifecycle'), default='all')
    parser.add_argument('--seal', action='store_true')
    args = parser.parse_args()
    if args.from_build:
        assert args.evidence is None and args.build_receipt_sha256 and not args.seal
        prepare(args.from_build.resolve(), args.build_receipt_sha256)
    else:
        assert args.evidence is not None
        out = args.evidence.resolve()
        assert out.parent == Path('/var/tmp') and out.name.startswith('research-stabilized-sitl-')
        seal(out) if args.seal else run(out, args.phase)
