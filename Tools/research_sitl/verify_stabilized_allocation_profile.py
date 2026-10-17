#!/usr/bin/env python3
"""Seal the synthetic fixture round-trip of the offline allocation compiler."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

import compile_stabilized_allocation as compiler


ROOT = Path(__file__).resolve().parents[2]
EXAMPLE = Path(__file__).with_name('stabilized_allocation_synthetic.json')
TEST = Path(__file__).with_name('test_compile_stabilized_allocation.py')
EXPECTED = ((0.25, -0.25, 0.25, -0.25),
            (0.25, 0.25, -0.25, -0.25),
            (0.25, -0.25, -0.25, 0.25))
SOURCE_FILES = ('Tools/research_sitl/stabilized_manual/FixtureConstants.hpp',
                'Tools/research_sitl/stabilized_manual/SyntheticConfiguration.hpp',
                'Tools/research_sitl/stabilized_manual/world.sdf.in',
                'src/lib/research_stabilized_manual/CanardAllocator.hpp')


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def verify():
    graph = json.loads((ROOT / 'docs/RESEARCH_CODE_GRAPH.json').read_text())
    checkpoint = {}
    for node in graph['nodes']:
        if 'sha256' in node:
            assert digest(ROOT / node['path']) == node['sha256'], 'Stale graph: ' + node['path']
            checkpoint[node['path']] = node['sha256']
    out = Path(tempfile.mkdtemp(prefix='research-stabilized-allocation-', dir='/var/tmp'))
    (out / 'checkpoint.json').write_text(json.dumps(checkpoint, indent=2) + '\n')
    data = json.loads(EXAMPLE.read_text())
    for record in data['provenance'].values():
        assert digest(ROOT / record['reference']) == record['sha256']
    for path in (EXAMPLE, TEST, Path(compiler.__file__), Path(__file__)):
        shutil.copy2(path, out / path.name)
    for name in SOURCE_FILES:
        destination = out / 'source' / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(ROOT / name, destination)
    with (out / 'tests.log').open('w') as stream:
        subprocess.run([sys.executable, str(TEST)], stdout=stream,
                       stderr=subprocess.STDOUT, cwd=ROOT, check=True)
    with (out / 'compiler.log').open('w') as stream:
        subprocess.run([sys.executable, str(Path(compiler.__file__)),
                        '--input', str(out / EXAMPLE.name),
                        '--output', str(out / 'synthetic-allocation.json')],
                       stdout=stream, stderr=subprocess.STDOUT, check=True)
    result = json.loads((out / 'synthetic-allocation.json').read_text())
    assert result['input_sha256'] == digest(out / EXAMPLE.name)
    assert result['claim'] == 'offline_local_linear_allocation_conversion_only'
    assert not result['vehicle_qualified'] and not result['physical_acceptance']
    for actual, expected in zip(result['allocator_inputs']['effectiveness'], EXPECTED):
        assert all(abs(a - b) < 1e-12 for a, b in zip(actual, expected))
    (out / 'summary.json').write_text(json.dumps(dict(
        scope='synthetic_fixture_physical_to_normalized_allocation_round_trip',
        measured_vehicle_profile_present=False, vehicle_qualified=False,
        graph_anchors=len(checkpoint), synthetic_input_sha256=digest(out / EXAMPLE.name),
        output_sha256=digest(out / 'synthetic-allocation.json'),
        source_sha256={name: digest(ROOT / name) for name in SOURCE_FILES},
        fixture_effectiveness_reconstructed=True), indent=2) + '\n')
    files = {}
    for path in out.rglob('*'):
        if path.is_file() and path.name != 'allocation-manifest.json':
            files[str(path.relative_to(out))] = digest(path)
    (out / 'allocation-manifest.json').write_text(json.dumps(dict(
        scope='offline_allocation_conversion_synthetic_fixture_only',
        vehicle_qualified=False, accepted_vehicle_profile=False,
        verifier_sha256=digest(Path(__file__)), files=files), indent=2) + '\n')
    print(json.dumps(dict(evidence=str(out),
                          manifest_sha256=digest(out / 'allocation-manifest.json'),
                          files=len(files), graph_anchors=len(checkpoint)), indent=2), flush=True)


if __name__ == '__main__':
    verify()
