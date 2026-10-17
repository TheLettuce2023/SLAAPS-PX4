#!/usr/bin/env python3
"""Seal the offline synthetic single-condition interval-rank screen."""
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

import screen_stabilized_allocation_rank as screen
import verify_stabilized_allocation_profile as allocation
import verify_stabilized_sitl as base


PARENT = Path('/var/tmp/research-stabilized-allocation-4qimab45')
PARENT_MANIFEST_SHA256 = 'd7f68d120f21543ddb70289a039189f6fed9d48db46d5f3f5a418ea551b0ed1c'
TEST = Path(__file__).with_name('test_screen_stabilized_allocation_rank.py')
COMPILER = Path(__file__).with_name('compile_stabilized_allocation.py')


def verify_parent():
    receipt = PARENT / 'allocation-manifest.json'
    assert base.digest(receipt) == PARENT_MANIFEST_SHA256
    for name, identity in json.loads(receipt.read_text())['files'].items():
        assert base.digest(PARENT / name) == identity, 'Parent changed: ' + name


def main():
    verify_parent()
    graph = json.loads((base.ROOT / 'docs/RESEARCH_CODE_GRAPH.json').read_text())
    checkpoint = {}
    for node in graph['nodes']:
        if 'sha256' in node:
            identity = base.digest(base.ROOT / node['path'])
            assert identity == node['sha256'], 'Stale graph: ' + node['path']
            checkpoint[node['path']] = identity
    assert base.digest(Path(screen.__file__)) == checkpoint[
        'Tools/research_sitl/screen_stabilized_allocation_rank.py']
    out = Path(tempfile.mkdtemp(prefix='research-stabilized-allocation-', dir='/var/tmp'))
    (out / 'checkpoint.json').write_text(json.dumps(checkpoint, indent=2) + '\n')
    for path in (Path(__file__), Path(screen.__file__), TEST, COMPILER):
        shutil.copy2(path, out / path.name)
    source = out / 'stabilized_allocation_synthetic.json'
    compiled = out / 'synthetic-allocation.json'
    shutil.copy2(PARENT / source.name, source)
    shutil.copy2(PARENT / compiled.name, compiled)
    assert base.digest(source) == base.digest(allocation.EXAMPLE)
    with (out / 'tests.log').open('w') as log:
        subprocess.run([sys.executable, str(TEST)], cwd=base.ROOT,
                       stdout=log, stderr=subprocess.STDOUT, check=True)
    result = out / 'synthetic-rank-screen.json'
    with (out / 'screen.log').open('w') as log:
        subprocess.run([sys.executable, str(Path(screen.__file__)),
                        '--input', str(source), '--compiled', str(compiled),
                        '--evidence-root', str(base.ROOT), '--output', str(result)],
                       stdout=log, stderr=subprocess.STDOUT, check=True)
    screened = json.loads(result.read_text())
    assert screened['status'] == 'sufficient_full_row_rank_for_reported_intervals'
    assert (screened['gram_eigenvalue_lower_numerator'],
            screened['gram_eigenvalue_lower_denominator']) == (1, 4)
    assert not screened['interval_box_contains_zero_matrix']
    assert not screened['vehicle_qualified'] and not screened['physical_acceptance']
    assert not screened['full_envelope_qualified']
    summary = dict(scope='synthetic_fixture_single_condition_elementwise_rank_screen',
                   vehicle_qualified=False, physical_acceptance=False,
                   accepted_campaign_expansion=False,
                   source_sha256=base.digest(source), compiled_sha256=base.digest(compiled),
                   rank_screen_sha256=base.digest(result),
                   parent_manifest_sha256=PARENT_MANIFEST_SHA256,
                   graph_anchors=len(checkpoint), exact_gram_lower_fraction='1/4',
                   six_host_tests_passed=True)
    (out / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    files = {path.name: base.digest(path) for path in out.iterdir()
             if path.is_file() and path.name != 'rank-screen-manifest.json'}
    (out / 'rank-screen-manifest.json').write_text(json.dumps(dict(
        scope=summary['scope'], vehicle_qualified=False, accepted=False, files=files), indent=2) + '\n')
    print(json.dumps(dict(evidence=str(out),
                          manifest_sha256=base.digest(out / 'rank-screen-manifest.json'),
                          files=len(files), graph_anchors=len(checkpoint)), indent=2), flush=True)


if __name__ == '__main__':
    main()
