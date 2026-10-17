#!/usr/bin/env python3
"""Seal the synthetic single-condition static incremental moment-box screen."""
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

import screen_stabilized_static_authority as authority
import verify_stabilized_sitl as base


ALLOCATION = Path('/var/tmp/research-stabilized-allocation-4qimab45')
ALLOCATION_MANIFEST_SHA256 = 'd7f68d120f21543ddb70289a039189f6fed9d48db46d5f3f5a418ea551b0ed1c'
RANK = Path('/var/tmp/research-stabilized-allocation-3ttfw2an')
RANK_MANIFEST_SHA256 = '3c648e090fb2c5edcaa12c7d92348470a9a61a0d27997341fa466811613cd443'
TEST = Path(__file__).with_name('test_screen_stabilized_static_authority.py')
DEMAND = Path(__file__).with_name('stabilized_static_demand_synthetic.json')


def verify_parent(root, name, expected):
    receipt = root / name
    assert base.digest(receipt) == expected
    for path, identity in json.loads(receipt.read_text())['files'].items():
        assert base.digest(root / path) == identity, 'Parent changed: ' + path


def main():
    verify_parent(ALLOCATION, 'allocation-manifest.json', ALLOCATION_MANIFEST_SHA256)
    verify_parent(RANK, 'rank-screen-manifest.json', RANK_MANIFEST_SHA256)
    graph = json.loads((base.ROOT / 'docs/RESEARCH_CODE_GRAPH.json').read_text())
    checkpoint = {}
    for node in graph['nodes']:
        if 'sha256' in node:
            identity = base.digest(base.ROOT / node['path'])
            assert identity == node['sha256'], 'Stale graph: ' + node['path']
            checkpoint[node['path']] = identity
    out = Path(tempfile.mkdtemp(prefix='research-stabilized-allocation-', dir='/var/tmp'))
    (out / 'checkpoint.json').write_text(json.dumps(checkpoint, indent=2) + '\n')
    for path in (Path(__file__), Path(authority.__file__), TEST, DEMAND,
                 Path(__file__).with_name('screen_stabilized_allocation_rank.py'),
                 Path(__file__).with_name('compile_stabilized_allocation.py')):
        shutil.copy2(path, out / path.name)
    source = out / 'stabilized_allocation_synthetic.json'
    compiled = out / 'synthetic-allocation.json'
    shutil.copy2(ALLOCATION / source.name, source)
    shutil.copy2(ALLOCATION / compiled.name, compiled)
    assert base.digest(source) == base.digest(base.ROOT / 'Tools/research_sitl' / source.name)
    with (out / 'tests.log').open('w') as log:
        subprocess.run([sys.executable, str(TEST)], cwd=base.ROOT,
                       stdout=log, stderr=subprocess.STDOUT, check=True)
    result = out / 'synthetic-static-authority.json'
    with (out / 'screen.log').open('w') as log:
        subprocess.run([sys.executable, str(Path(authority.__file__)),
                        '--input', str(source), '--compiled', str(compiled),
                        '--evidence-root', str(base.ROOT), '--demand', str(out / DEMAND.name),
                        '--output', str(result)], stdout=log, stderr=subprocess.STDOUT, check=True)
    screened = json.loads(result.read_text())
    assert screened['status'] == 'sufficient_static_incremental_box_reachable'
    assert screened['rank_gram_lower'] == dict(numerator=1, denominator=4)
    assert screened['symmetric_command_radius'] == dict(numerator=1, denominator=1)
    assert screened['demand_norm_squared'] == dict(numerator=3, denominator=100)
    assert screened['guaranteed_norm_squared'] == dict(numerator=1, denominator=4)
    assert not screened['vehicle_qualified'] and not screened['physical_acceptance']
    assert not screened['full_envelope_qualified'] and not screened['slew_and_dynamics_qualified']
    assert not screened['implemented_allocator_tracking_qualified']
    summary = dict(scope='synthetic_fixture_single_condition_static_incremental_box_screen',
                   vehicle_qualified=False, physical_acceptance=False,
                   accepted_campaign_expansion=False,
                   allocation_parent_manifest_sha256=ALLOCATION_MANIFEST_SHA256,
                   rank_parent_manifest_sha256=RANK_MANIFEST_SHA256,
                   profile_input_sha256=base.digest(source), demand_sha256=base.digest(out / DEMAND.name),
                   output_sha256=base.digest(result), graph_anchors=len(checkpoint),
                   six_host_tests_passed=True)
    (out / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    files = {path.name: base.digest(path) for path in out.iterdir()
             if path.is_file() and path.name != 'static-authority-manifest.json'}
    (out / 'static-authority-manifest.json').write_text(json.dumps(dict(
        scope=summary['scope'], vehicle_qualified=False, accepted=False, files=files), indent=2) + '\n')
    print(json.dumps(dict(evidence=str(out),
                          manifest_sha256=base.digest(out / 'static-authority-manifest.json'),
                          files=len(files), graph_anchors=len(checkpoint)), indent=2), flush=True)


if __name__ == '__main__':
    main()
