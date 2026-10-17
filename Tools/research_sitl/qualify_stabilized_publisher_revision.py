#!/usr/bin/env python3
"""Qualify the declared 31-case synthetic campaign on the exact revision binary."""
import argparse
import contextlib
import io
import json
from pathlib import Path
import shutil

import prepare_stabilized_publisher_lifecycle as preparation
import seal_stabilized_campaigns as campaign_audit
import seal_stabilized_coupled as coupled_audit
import verify_stabilized_campaigns as campaigns
import verify_stabilized_coupled as coupled
import verify_stabilized_dispatch as dispatch
import verify_stabilized_publisher_epoch as epoch
import verify_stabilized_publisher_lifecycle as lifecycle
import verify_stabilized_sitl as base

LIFECYCLE = Path('/var/tmp/research-stabilized-sitl-ob5jrceo')
LIFECYCLE_RECEIPT = 'publisher-lifecycle-manifest.json'
LIFECYCLE_SHA256 = '77e36d02abed8ebb6c5268812325f6dda3ba6348c93c291fa88907326a78ffb5'


def verify_inherited(root):
    assert base.digest(root / LIFECYCLE_RECEIPT) == LIFECYCLE_SHA256
    manifest = json.loads((root / LIFECYCLE_RECEIPT).read_text())
    assert manifest['passed'] and not manifest['physical_authority_granted']
    for name, identity in manifest['files'].items():
        relative = Path(name)
        assert not relative.is_absolute() and '..' not in relative.parts
        assert base.digest(root / name) == identity, 'Lifecycle artifact changed: ' + name
    return manifest


def prepare():
    manifest = verify_inherited(LIFECYCLE)
    summary = json.loads((LIFECYCLE / 'publisher-lifecycle-summary.json').read_text())
    assert summary['passed'] and len(summary['cases']) == 8
    output = io.StringIO()
    with contextlib.redirect_stdout(output):
        preparation.prepare()
    out = Path(json.loads(output.getvalue())['evidence'])
    inherited = out / 'inherited-lifecycle'
    inherited.mkdir()
    for name in manifest['files']:
        destination = inherited / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(LIFECYCLE / name, destination)
    shutil.copy2(LIFECYCLE / LIFECYCLE_RECEIPT, inherited / LIFECYCLE_RECEIPT)
    verify_inherited(inherited)
    record = epoch.origin(out)
    assert record['variant_px4_sha256'] == base.digest(inherited / 'sitl-lockstep-build/bin/px4')
    assert record['plugin_sha256'] == base.digest(inherited / 'plugin-build/libresearch_rotational_fixture.so')
    (out / 'qualification-origin.json').write_text(json.dumps(dict(
        scope='complete_declared_revision_variant_campaign', inherited_lifecycle_parent=str(LIFECYCLE),
        inherited_lifecycle_manifest_sha256=LIFECYCLE_SHA256, fresh_cases_required=23,
        inherited_cases=8, source=record, runner_sha256=base.digest(Path(__file__))), indent=2) + '\n')
    shutil.copy2(Path(__file__), out / Path(__file__).name)
    print(json.dumps(dict(evidence=str(out), px4_sha256=record['variant_px4_sha256'],
                          fresh_cases_required=23, inherited_cases=8), indent=2), flush=True)


def capture(out, name, action):
    path = out / ('qualification-' + name + '.log')
    if path.exists():
        index = 1
        while path.with_name(path.name + '.' + str(index)).exists():
            index += 1
        shutil.copy2(path, path.with_name(path.name + '.' + str(index)))
    print('RUN', name, flush=True)
    with path.open('w') as stream, contextlib.redirect_stdout(stream):
        action()
    print('PASS', name, flush=True)


def run(out, phase):
    epoch.origin(out)
    verify_inherited(out / 'inherited-lifecycle')
    if phase in ('all', 'dispatch'):
        capture(out, 'dispatch', lambda: dispatch.regression(out))
    if phase in ('all', 'coupled'):
        for up in (False, True):
            for sign in (1, -1):
                capture(out, f'coupled-{up}-{sign}', lambda up=up, sign=sign: coupled.coupled(out, up, sign))
    if phase in ('all', 'nominal'):
        for up in (False, True):
            for axis in (0, 1, 2):
                for sign in (1, -1):
                    capture(out, f'rate-{up}-{axis}-{sign}',
                            lambda up=up, axis=axis, sign=sign: campaigns.campaign(out, axis, up, sign))
    if phase in ('all', 'failures'):
        for case in campaigns.FAILURES:
            capture(out, 'failure-' + case, lambda case=case: campaigns.failure(out, case))


def stable_revision_rows(path):
    rows = epoch.closed_rows(path)
    tuple_before = tuple(int(rows[0][field]) for field in epoch.REVISION_FIELDS)
    source_epoch = int(rows[0]['source_epoch'])
    assert source_epoch > 0 and all(value > 0 for value in tuple_before)
    assert all(int(row['source_epoch']) == source_epoch and
               tuple(int(row[field]) for field in epoch.REVISION_FIELDS) == tuple_before for row in rows), \
        'Published across a revision/epoch change: ' + str(path)
    return len(rows)


def seal(out):
    record = epoch.origin(out)
    inherited = out / 'inherited-lifecycle'
    verify_inherited(inherited)
    origin = json.loads((out / 'qualification-origin.json').read_text())
    assert origin['runner_sha256'] == base.digest(Path(__file__))
    assert origin['fresh_cases_required'] == 23 and origin['inherited_cases'] == 8
    assert origin['inherited_lifecycle_manifest_sha256'] == LIFECYCLE_SHA256
    assert base.digest(out / 'compiled-dependencies.json') == base.digest(inherited / 'compiled-dependencies.json')
    dependencies = json.loads((out / 'compiled-dependencies.json').read_text())
    for name, identity in dependencies.items():
        assert base.digest(out / 'inherited-compiled-inputs' / name) == identity
        if name in record['patched_sources']:
            assert identity == record['patched_sources'][name]
        else:
            assert base.digest(base.ROOT / name) == identity, 'Compiled dependency changed: ' + name
    for name, identity in record['patched_sources'].items():
        assert base.digest(out / 'variant-inputs' / name) == identity
    for name, identity in record['support_sources'].items():
        assert base.digest(out / 'variant-inputs' / name) == identity
    # Component receipt order avoids overwriting a file already bound by a
    # previous receipt. The inherited eight-case package stays immutable.
    dispatch.seal(out)
    coupled_audit.seal(out)
    campaign_audit.seal(out, False)
    components = {}
    summaries = []
    for name in ('dispatch', 'coupled', 'campaign'):
        receipt = out / (name + '-manifest.json')
        component = json.loads(receipt.read_text())
        assert component['accepted'] and not component['physical_authority_granted']
        for artifact, identity in component['files'].items():
            assert base.digest(out / artifact) == identity, 'Component artifact changed: ' + artifact
        components[name] = base.digest(receipt)
    dispatch_result = json.loads((out / 'dispatch-manifest.json').read_text())['result']
    summaries.extend(json.loads((out / 'coupled-summary.json').read_text())['cases'])
    summaries.extend(json.loads((out / 'campaign-summary.json').read_text())['cases'])
    fresh_cycles = dispatch_result['audited_control_cycles']
    stable_revision_rows(out / sorted(out.glob('dispatch-*/result.json'), key=lambda p: p.stat().st_mtime_ns)[-1].parent.name / 'control.csv')
    for result in summaries:
        work = out / result['directory']
        stable_revision_rows(work / 'control.csv')
        if (work / 'restarted.csv').exists():
            stable_revision_rows(work / 'restarted.csv')
        assert result['passed'] and result['px4_sha256'] == record['variant_px4_sha256']
        assert result['plugin_sha256'] == record['plugin_sha256']
        # The component summaries independently audit each case's cycle count.
    fresh_cycles += json.loads((out / 'coupled-summary.json').read_text())['audited_control_cycles']
    fresh_cycles += json.loads((out / 'campaign-summary.json').read_text())['audited_control_cycles']
    inherited_summary = json.loads((inherited / 'publisher-lifecycle-summary.json').read_text())
    assert len(inherited_summary['cases']) == 8 and inherited_summary['passed']
    inherited_cycles = 0
    for result in inherited_summary['cases']:
        work = inherited / result['directory']
        assert result['passed'] and result['lifecycle_verified']
        assert result['lifecycle_runner_sha256'] == base.digest(Path(lifecycle.__file__))
        derived = lifecycle.inspect(work, result)
        assert all(result[key] == list(value) if isinstance(value, tuple) else result[key] == value
                   for key, value in derived.items())
        campaign_audit.audit_runtime_profile(work, result)
        inherited_cycles += campaign_audit.audit_cycles(work / 'control.csv') + campaign_audit.audit_cycles(work / 'restarted.csv')
        stable_revision_rows(work / 'control.csv')
        stable_revision_rows(work / 'restarted.csv')
        assert result['px4_sha256'] == record['variant_px4_sha256'] and result['plugin_sha256'] == record['plugin_sha256']
    assert inherited_cycles == inherited_summary['audited_control_cycles']
    assert len(summaries) == 22
    summary = dict(scope='31_declared_native_cases_on_private_publisher_revision_binary', accepted=True,
                   fresh_cases=23, inherited_cases=8, total_cases=31,
                   fresh_control_cycles=fresh_cycles, inherited_control_cycles=inherited_cycles,
                   audited_control_cycles=fresh_cycles + inherited_cycles,
                   component_receipts=components, inherited_lifecycle_manifest_sha256=LIFECYCLE_SHA256,
                   variant_px4_sha256=record['variant_px4_sha256'], plugin_sha256=record['plugin_sha256'],
                   vehicle_supported_tuning=False, general_publisher_identity_qualified=False,
                   previous_accepted_campaign_replaced=False,
                   coupled_cases=json.loads((out / 'coupled-summary.json').read_text())['cases'],
                   dispatch=dispatch_result)
    (out / 'revision-qualification-summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    shutil.copy2(Path(__file__), out / Path(__file__).name)
    files = {}
    for path in out.iterdir():
        if path.is_file() and path.name != 'revision-qualification-manifest.json':
            files[path.name] = base.digest(path)
        elif path.is_dir() and path.name.startswith(('rate-', 'failure-', 'coupled-', 'dispatch-', 'tested-artifacts',
                                                    'variant-inputs', 'inherited-compiled-inputs', 'inherited-lifecycle',
                                                    'audited-inputs')):
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
        fresh_cases=23, inherited_cases=8, files=files), indent=2) + '\n')
    print('31-case revision qualification PASS;', summary['audited_control_cycles'], 'cycles;', len(files),
          'artifacts; manifest', base.digest(out / 'revision-qualification-manifest.json'), flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prepare', action='store_true')
    parser.add_argument('--evidence', type=Path)
    parser.add_argument('--phase', choices=('all', 'dispatch', 'coupled', 'nominal', 'failures'), default='all')
    parser.add_argument('--seal', action='store_true')
    args = parser.parse_args()
    if args.prepare:
        assert args.evidence is None and not args.seal
        prepare()
    else:
        assert args.evidence is not None
        evidence = args.evidence.resolve()
        assert evidence.parent == Path('/var/tmp') and evidence.name.startswith('research-stabilized-sitl-')
        if args.seal:
            seal(evidence)
        else:
            run(evidence, args.phase)
