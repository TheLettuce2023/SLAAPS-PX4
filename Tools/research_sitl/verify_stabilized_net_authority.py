#!/usr/bin/env python3
"""Seal a source-bound synthetic net-moment authority screen and its tests."""
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

import prepare_stabilized_generated_allocation as preparation
import verify_stabilized_sitl as base


ALLOCATION = preparation.ALLOCATION
ALLOCATION_MANIFEST_SHA256 = preparation.ALLOCATION_MANIFEST_SHA256
STATIC = Path('/var/tmp/research-stabilized-allocation-x42ielic')
STATIC_MANIFEST_SHA256 = '913cb7b27308dfa2a158e6a05b53ed1a08dd0a48b83f1c332e7f9ba981169f6a'
SCREEN = Path(__file__).with_name('screen_stabilized_net_authority.py')
TEST = Path(__file__).with_name('test_screen_stabilized_net_authority.py')
SPECIFICATION = Path(__file__).with_name('stabilized_net_authority_synthetic.json')
DEMAND_SOURCE = Path(__file__).with_name('stabilized_net_demand_synthetic.txt')
BIAS_SOURCE = Path(__file__).parent / 'stabilized_manual/RotationalFixture.cpp'


def main():
    preparation.verify_receipt(ALLOCATION, 'allocation-manifest.json', ALLOCATION_MANIFEST_SHA256)
    preparation.verify_receipt(STATIC, 'static-authority-manifest.json', STATIC_MANIFEST_SHA256)
    graph = json.loads((base.ROOT / 'docs/RESEARCH_CODE_GRAPH.json').read_text())
    checkpoint = {}
    for node in graph['nodes']:
        if 'sha256' in node:
            identity = base.digest(base.ROOT / node['path'])
            assert identity == node['sha256'], 'Stale graph: ' + node['path']
            checkpoint[node['path']] = identity
    specification = json.loads(SPECIFICATION.read_text())
    assert specification['profile_input_sha256'] == base.digest(
        ALLOCATION / 'stabilized_allocation_synthetic.json')
    assert specification['provenance']['bias']['sha256'] == base.digest(BIAS_SOURCE)
    assert specification['provenance']['demand']['sha256'] == base.digest(DEMAND_SOURCE)
    out = Path(tempfile.mkdtemp(prefix='research-stabilized-allocation-', dir='/var/tmp'))
    (out / 'checkpoint.json').write_text(json.dumps(checkpoint, indent=2) + '\n')
    for path in (Path(__file__), SCREEN, TEST, SPECIFICATION, DEMAND_SOURCE, BIAS_SOURCE,
                 Path(__file__).with_name('screen_stabilized_static_authority.py'),
                 Path(__file__).with_name('screen_stabilized_allocation_rank.py'),
                 Path(__file__).with_name('compile_stabilized_allocation.py')):
        shutil.copy2(path, out / path.name)
    for name in ('stabilized_allocation_synthetic.json', 'synthetic-allocation.json'):
        shutil.copy2(ALLOCATION / name, out / name)
    assert base.digest(out / 'stabilized_allocation_synthetic.json') == base.digest(
        base.ROOT / 'Tools/research_sitl/stabilized_allocation_synthetic.json')
    with (out / 'tests.log').open('w') as log:
        subprocess.run([sys.executable, str(TEST)], cwd=base.ROOT, stdout=log,
                       stderr=subprocess.STDOUT, check=True)
    result_path = out / 'synthetic-net-authority.json'
    with (out / 'screen.log').open('w') as log:
        subprocess.run([sys.executable, str(SCREEN), '--input',
                        str(out / 'stabilized_allocation_synthetic.json'),
                        '--compiled', str(out / 'synthetic-allocation.json'),
                        '--evidence-root', str(base.ROOT), '--net-demand',
                        str(out / SPECIFICATION.name), '--output', str(result_path)],
                       stdout=log, stderr=subprocess.STDOUT, check=True)
    result = json.loads(result_path.read_text())
    assert result['status'] == 'sufficient_static_net_box_reachable'
    assert result['correction_norm_squared'] == dict(numerator=3, denominator=100)
    assert result['guaranteed_norm_squared'] == dict(numerator=1, denominator=4)
    assert result['rank_gram_lower'] == dict(numerator=1, denominator=4)
    assert not result['vehicle_qualified'] and not result['physical_acceptance']
    assert not result['slew_and_dynamics_qualified']
    assert not result['implemented_allocator_tracking_qualified']
    summary = dict(scope='synthetic_fixture_single_condition_static_net_moment_box_screen',
                   vehicle_qualified=False, physical_acceptance=False,
                   accepted_campaign_expansion=False,
                   allocation_parent_manifest_sha256=ALLOCATION_MANIFEST_SHA256,
                   static_parent_manifest_sha256=STATIC_MANIFEST_SHA256,
                   profile_input_sha256=base.digest(out / 'stabilized_allocation_synthetic.json'),
                   net_specification_sha256=base.digest(out / SPECIFICATION.name),
                   output_sha256=base.digest(result_path), graph_anchors=len(checkpoint),
                   six_host_tests_passed=True)
    (out / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    files = {path.name: base.digest(path) for path in out.iterdir()
             if path.is_file() and path.name != 'net-authority-manifest.json'}
    (out / 'net-authority-manifest.json').write_text(json.dumps(dict(
        scope=summary['scope'], vehicle_qualified=False, accepted=False, files=files), indent=2) + '\n')
    print(json.dumps(dict(evidence=str(out),
                          manifest_sha256=base.digest(out / 'net-authority-manifest.json'),
                          files=len(files), graph_anchors=len(checkpoint),
                          result_status=result['status']), indent=2), flush=True)


if __name__ == '__main__':
    main()
