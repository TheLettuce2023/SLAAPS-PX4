#!/usr/bin/env python3
"""Seal full-axis native saturation/hold evidence; never vehicle qualification."""
import argparse
import bisect
import csv
import json
from pathlib import Path
import shutil

import verify_stabilized_coupled as coupled
import verify_stabilized_campaigns as campaigns
import verify_stabilized_sitl as base
import seal_stabilized_campaigns as audit


def seal(out):
    expected = {(up, sign) for up in (False, True) for sign in (-1, 1)}
    chosen = {}
    for path in sorted(out.glob('coupled-*/result.json'), key=lambda path: path.stat().st_mtime_ns):
        result = json.loads(path.read_text())
        assert not result.get('exploratory_only'), 'Exploratory runs cannot satisfy coupled acceptance'
        assert result['seed'] == campaigns.SEED, 'Changed sensor-noise seed requires separate qualification'
        chosen[(result['nose_up'], result['sign'])] = (path.parent, result)
    assert set(chosen) == expected, 'Missing full-axis coupled coverage'
    assert all(result['passed'] for _, result in chosen.values()), 'Latest coupled attempt failed'
    cycles = 0
    duplicates = 0
    cases = []
    for work, result in chosen.values():
        audit.audit_runtime_profile(work, result)
        assert result['coupled_source_sha256'] == base.digest(Path(coupled.__file__))
        assert base.digest(work / Path(coupled.__file__).name) == result['coupled_source_sha256']
        assert result['px4_sha256'] == base.digest(out / 'sitl-lockstep-build/bin/px4')
        assert result['plugin_sha256'] == base.digest(out / 'plugin-build/libresearch_rotational_fixture.so')
        for name in ('control.csv', 'truth.csv', 'control.csv.events.csv'):
            with (work / name).open() as stream:
                records = list(csv.DictReader(stream))
            assert records and all(None not in row and None not in row.values() for row in records), 'Malformed closed CSV'
        cycles += audit.audit_cycles(work / 'control.csv')
        skipped = [row for row in campaigns.rows(work / 'control.csv.events.csv') if row['action'] == 'dispatch_not_due']
        assert all(row['status'] == '4' for row in skipped)
        assert len(skipped) == result['duplicate_dispatches_without_control']
        duplicates += len(skipped)
        control = campaigns.rows(work / 'control.csv')
        truth = campaigns.rows(work / 'truth.csv')
        times = [int(row['time']) for row in truth]
        actual_errors = []
        for phase in (result['sign'], -result['sign']):
            selected = [row for row in control if row['state'] == '2' and float(row['roll']) == phase]
            end = int(selected[-1]['time'])
            errors = []
            for row in selected:
                if int(row['time']) < end - 300000:
                    continue
                stamp = int(row['rate_sample'])
                index = bisect.bisect_left(times, stamp)
                index = min((max(0, index - 1), min(index, len(truth) - 1)), key=lambda i: abs(times[i] - stamp))
                assert abs(times[index] - stamp) <= 4000
                errors.extend(abs(float(truth[index]['w' + axis]) - phase) for axis in 'xyz')
            actual_errors.append(max(errors))
        assert max(actual_errors) < campaigns.RATE_TOLERANCE, 'Physical full-axis tracking failed'
        for row in control:
            controls = [float(row['u' + str(i)]) for i in range(4)]
            effectiveness = ((.25, -.25, .25, -.25), (.25, .25, -.25, -.25), (.25, -.25, -.25, .25))
            for axis, coefficients in zip('xyz', effectiveness):
                assert abs(sum(a*b for a, b in zip(coefficients, controls)) - float(row['a' + axis])) < 1e-6
        for name, identity in result['source_sha256'].items():
            assert base.digest(base.ROOT / name) == base.digest(work / 'source-inputs' / name) == identity
        assert all(result['checks'].values()) and result['antiwindup_violations'] == 0
        assert all(value > 0 for value in result['blocked_axis_cycles']) and result['box_saturated_cycles'] > 0
        cases.append(dict(directory=work.name, physical_rate_errors_rad_s=actual_errors, **result))
    dispatch_manifest = out / 'dispatch-manifest.json'
    proof = json.loads(dispatch_manifest.read_text())
    assert proof['accepted'] and proof['result']['duplicate_dispatches'] >= 5
    assert proof['result']['px4_sha256'] == base.digest(out / 'sitl-lockstep-build/bin/px4')
    assert proof['result']['plugin_sha256'] == base.digest(out / 'plugin-build/libresearch_rotational_fixture.so')
    for name, identity in proof['files'].items():
        assert base.digest(out / name) == identity, 'Dispatch proof artifact changed: ' + name
    (out / 'coupled-dispatch-proof.json').write_text(json.dumps(dict(
        manifest_sha256=base.digest(dispatch_manifest), tested_dispatches=proof['result']['duplicate_dispatches']), indent=2) + '\n')
    checkpoint = json.loads((out / 'checkpoint.json').read_text())
    for name, identity in checkpoint.items():
        assert base.digest(base.ROOT / name) == identity, 'Mapped input changed: ' + name
    inputs = {}
    graph = json.loads((base.ROOT / 'docs/RESEARCH_CODE_GRAPH.json').read_text())
    for node in graph['nodes']:
        name = node['path']
        if name.startswith(('src/', 'platforms/', 'msg/', 'boards/')) and (out / 'px4-source' / name).is_file():
            identity = base.digest(base.ROOT / name)
            assert base.digest(out / 'px4-source' / name) == identity, 'Compiled dependency mismatch: ' + name
            inputs[name] = identity
            destination = out / 'coupled-audited-inputs' / name
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(base.ROOT / name, destination)
    (out / 'coupled-audited-inputs.json').write_text(json.dumps(inputs, indent=2) + '\n')
    (out / 'coupled-summary.json').write_text(json.dumps(dict(
        scope='four_full_axis_rate_reversal_box_saturation_and_hold_cases', accepted=True,
        audited_control_cycles=cycles, duplicate_dispatches_without_control=duplicates,
        cases=cases), indent=2) + '\n')
    (out / 'coupled-preservation.json').write_text(json.dumps(checkpoint, indent=2) + '\n')
    shutil.copy2(Path(__file__), out / Path(__file__).name)
    files = {}
    for path in out.iterdir():
        if path.is_file() and not path.name.endswith('-manifest.json'):
            files[path.name] = base.digest(path)
        elif path.is_dir() and path.name.startswith(('coupled-', 'tested-artifacts', 'runtime-inputs-before')):
            for artifact in path.rglob('*'):
                if artifact.is_file():
                    files[str(artifact.relative_to(out))] = base.digest(artifact)
    for name in ('sitl-lockstep-build/bin/px4', 'sitl-lockstep-build/boardconfig',
                 'sitl-lockstep-build/compile_commands.json', 'plugin-build/libresearch_rotational_fixture.so'):
        files[name] = base.digest(out / name)
    (out / 'coupled-manifest.json').write_text(json.dumps(dict(
        scope='synthetic_coupled_manual_rate_and_hold_only', accepted=True,
        physical_authority_granted=False, preserved_anchor_count=len(checkpoint), files=files), indent=2) + '\n')
    print('4 coupled cases /', cycles, 'cycles audited;', len(checkpoint), 'anchors preserved', flush=True)
    print('Manifest SHA-256:', base.digest(out / 'coupled-manifest.json'), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--evidence', type=Path, required=True)
    args = parser.parse_args()
    out = args.evidence.resolve()
    assert out.parent == Path('/var/tmp') and out.name.startswith('research-stabilized-sitl-')
    seal(out)


if __name__ == '__main__':
    main()
