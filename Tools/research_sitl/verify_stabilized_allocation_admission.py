#!/usr/bin/env python3
"""Seal full-report synthetic admission against the existing SITL binary."""
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

import compile_stabilized_allocation as compiler
import generate_stabilized_allocation_header as header
import prepare_stabilized_generated_allocation as preparation
import verify_stabilized_sitl as base


SITL = Path('/var/tmp/research-stabilized-sitl-oou0sind')
SITL_MANIFEST_SHA256 = 'cf2068502959c952a5d4ec858ebe4aa905ebba7fc771c4016313807dce53d3be'
TEST = Path(__file__).with_name('test_generate_stabilized_allocation_header.py')


def main():
    preparation.verify_receipt(preparation.ACCEPTED, 'campaign-manifest.json',
                               preparation.ACCEPTED_MANIFEST_SHA256)
    preparation.verify_receipt(preparation.ALLOCATION, 'allocation-manifest.json',
                               preparation.ALLOCATION_MANIFEST_SHA256)
    preparation.verify_receipt(SITL, 'generated-allocation-manifest.json',
                               SITL_MANIFEST_SHA256)
    graph = json.loads((base.ROOT / 'docs/RESEARCH_CODE_GRAPH.json').read_text())
    checkpoint = {}
    for node in graph['nodes']:
        if 'sha256' in node:
            identity = base.digest(base.ROOT / node['path'])
            assert identity == node['sha256'], 'Stale graph: ' + node['path']
            checkpoint[node['path']] = identity
    assert base.digest(SITL / 'sitl-lockstep-build/bin/px4') == (
        '59c8b61f53a98a45fd06f46365af9d132a41d8610cad5803286145d5e4f77527')
    assert base.digest(preparation.ACCEPTED / 'sitl-lockstep-build/bin/px4') == (
        preparation.ACCEPTED_PX4_SHA256)
    out = Path(tempfile.mkdtemp(prefix='research-stabilized-allocation-', dir='/var/tmp'))
    (out / 'checkpoint.json').write_text(json.dumps(checkpoint, indent=2) + '\n')
    example = out / header.EXAMPLE.name
    shutil.copy2(header.EXAMPLE, example)
    compiled = out / 'synthetic-allocation.json'
    with (out / 'compiler.log').open('w') as log:
        subprocess.run([sys.executable, str(Path(compiler.__file__)), '--input', str(example),
                        '--output', str(compiled)], stdout=log, stderr=subprocess.STDOUT, check=True)
    assert compiled.read_bytes() == (preparation.ALLOCATION / compiled.name).read_bytes()
    with (out / 'tests.log').open('w') as log:
        subprocess.run([sys.executable, str(TEST)], cwd=base.ROOT,
                       stdout=log, stderr=subprocess.STDOUT, check=True)
    generated = out / 'GeneratedAllocationProfile.hpp'
    with (out / 'generator.log').open('w') as log:
        subprocess.run([sys.executable, str(Path(header.__file__)), '--compiled', str(compiled),
                        '--output', str(generated)], stdout=log, stderr=subprocess.STDOUT, check=True)
    prior_header = SITL / 'GeneratedAllocationProfile.hpp'
    assert generated.read_bytes() == prior_header.read_bytes()
    report = json.loads(compiled.read_text())
    assert report['input_sha256'] == base.digest(example)
    assert not report['vehicle_qualified'] and not report['physical_acceptance']
    tampered = json.loads(compiled.read_text())
    tampered['allocator_inputs']['effectiveness'][0][0] = 0.3
    altered = out / 'tampered-same-hash.json'
    altered.write_text(json.dumps(tampered, indent=2) + '\n')
    rejected_output = out / 'rejected-header.hpp'
    with (out / 'tamper-rejection.log').open('w') as log:
        result = subprocess.run([sys.executable, str(Path(header.__file__)), '--compiled', str(altered),
                                 '--output', str(rejected_output)], stdout=log, stderr=subprocess.STDOUT)
    assert result.returncode == 2 and not rejected_output.exists()
    assert 'differs from the synthetic source' in (out / 'tamper-rejection.log').read_text()
    for path in (Path(__file__), Path(header.__file__), TEST, Path(compiler.__file__)):
        shutil.copy2(path, out / path.name)
    summary = dict(scope='synthetic_header_full_report_admission_guard',
                   vehicle_qualified=False, sitl_rebuild_required=False,
                   sealed_synthetic_input_sha256=base.digest(example),
                   sealed_compiled_report_sha256=base.digest(compiled),
                   generated_header_sha256=base.digest(generated),
                   prior_sitl_header_sha256=base.digest(prior_header),
                   prior_sitl_manifest_sha256=SITL_MANIFEST_SHA256,
                   prior_sitl_px4_sha256=base.digest(SITL / 'sitl-lockstep-build/bin/px4'),
                   accepted_px4_sha256=preparation.ACCEPTED_PX4_SHA256,
                   tampered_same_hash_report_rejected=True,
                   graph_anchors=len(checkpoint))
    (out / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    files = {}
    for path in out.iterdir():
        if path.is_file() and path.name != 'admission-manifest.json':
            files[path.name] = base.digest(path)
    (out / 'admission-manifest.json').write_text(json.dumps(dict(
        scope=summary['scope'], vehicle_qualified=False, files=files), indent=2) + '\n')
    print(json.dumps(dict(evidence=str(out),
                          manifest_sha256=base.digest(out / 'admission-manifest.json'),
                          files=len(files), graph_anchors=len(checkpoint),
                          header_sha256=base.digest(generated)), indent=2), flush=True)


if __name__ == '__main__':
    main()
