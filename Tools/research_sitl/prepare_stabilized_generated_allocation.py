#!/usr/bin/env python3
"""Build a separate synthetic SITL owner from the sealed allocation report."""
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

import verify_stabilized_sitl as base


ACCEPTED = Path('/var/tmp/research-stabilized-sitl-rjr0eutz')
ALLOCATION = Path('/var/tmp/research-stabilized-allocation-4qimab45')
ACCEPTED_MANIFEST_SHA256 = '447823f2cdb88e22759d58b9365cd0508370053738011aa966fc452b1c050da3'
ALLOCATION_MANIFEST_SHA256 = 'd7f68d120f21543ddb70289a039189f6fed9d48db46d5f3f5a418ea551b0ed1c'
ACCEPTED_PX4_SHA256 = 'a2c8229b30a93434f6d6aae3170fc67f354227e5ce236eca83720d7b79397f7d'
PLUGIN_SHA256 = '795a826f771dfd5cfe3b56c719f0f036f897184457fbe4c132b917d4c5c2f044'
PATCH = base.ROOT / 'Tools/research_sitl/generated_allocation_adapter.patch'
GENERATOR = base.ROOT / 'Tools/research_sitl/generate_stabilized_allocation_header.py'
OWNER = Path('Tools/research_sitl/stabilized_manual/StabilizedManualSITL.cpp')
CONFIG = Path('Tools/research_sitl/stabilized_manual/SyntheticConfiguration.hpp')
HEADER = Path('Tools/research_sitl/stabilized_manual/GeneratedAllocationProfile.hpp')


def verify_receipt(root, name, expected):
    receipt = root / name
    assert base.digest(receipt) == expected
    manifest = json.loads(receipt.read_text())
    for path, identity in manifest['files'].items():
        assert base.digest(root / path) == identity, 'Sealed parent changed: ' + path


def build():
    verify_receipt(ACCEPTED, 'campaign-manifest.json', ACCEPTED_MANIFEST_SHA256)
    verify_receipt(ALLOCATION, 'allocation-manifest.json', ALLOCATION_MANIFEST_SHA256)
    assert base.digest(ACCEPTED / 'sitl-lockstep-build/bin/px4') == ACCEPTED_PX4_SHA256
    assert base.digest(ACCEPTED / 'plugin-build/libresearch_rotational_fixture.so') == PLUGIN_SHA256
    graph = json.loads((base.ROOT / 'docs/RESEARCH_CODE_GRAPH.json').read_text())
    checkpoint = {}
    for node in graph['nodes']:
        if 'sha256' in node:
            assert base.digest(base.ROOT / node['path']) == node['sha256'], 'Stale graph: ' + node['path']
            checkpoint[node['path']] = node['sha256']
    for path in (OWNER, CONFIG):
        assert base.digest(base.ROOT / path) == base.digest(ACCEPTED / 'px4-source' / path)
    out = Path(tempfile.mkdtemp(prefix='research-stabilized-sitl-', dir='/var/tmp'))
    (out / 'checkpoint.json').write_text(json.dumps(checkpoint, indent=2) + '\n')
    source = out / 'px4-source-generated-allocation'
    base.run(['cp', '-a', '--reflink=auto', str(ACCEPTED / 'px4-source'), str(source)],
             out, 'source-copy.log')
    with (out / 'patch.log').open('w') as log:
        subprocess.run(['patch', '--batch', '-p1', '-i', str(PATCH)], cwd=source,
                       stdout=log, stderr=subprocess.STDOUT, check=True)
    compiled = ALLOCATION / 'synthetic-allocation.json'
    header = source / HEADER
    assert not header.exists()
    base.run([sys.executable, str(GENERATOR), '--compiled', str(compiled),
              '--output', str(header)], out, 'header-generation.log')
    # The sealed report is checked above; repeat generation to check deterministic bytes.
    second = out / 'GeneratedAllocationProfile.hpp'
    base.run([sys.executable, str(GENERATOR), '--compiled', str(compiled),
              '--output', str(second)], out, 'header-repeat.log')
    assert base.digest(second) == base.digest(header)
    baseline = out / 'px4-source'
    for path in base.FIXTURE.rglob('*'):
        if path.is_file():
            destination = baseline / path.relative_to(base.ROOT)
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, destination)
    native = out / 'sitl-lockstep-build'
    external = source / base.FIXTURE.relative_to(base.ROOT) / 'px4_external'
    base.run(['cmake', '-S', str(source), '-B', str(native), '-G', 'Ninja',
              '-DCONFIG=px4_sitl_default', '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON',
              '-DEXTERNAL_MODULES_LOCATION=' + str(external)], out, 'sitl-configure.log')
    base.run(['cmake', '--build', str(native), '--target', 'mavlink_c_generate', '-j', '4'],
             out, 'sitl-mavlink.log')
    base.run(['cmake', '--build', str(native), '-j', '4'], out, 'sitl-build.log')
    plugin = out / 'plugin-build'
    plugin.mkdir()
    shutil.copy2(ACCEPTED / 'plugin-build/libresearch_rotational_fixture.so',
                 plugin / 'libresearch_rotational_fixture.so')
    assert base.digest(plugin / 'libresearch_rotational_fixture.so') == PLUGIN_SHA256
    elf = native / 'bin/px4'
    symbols = subprocess.check_output(['nm', '-C', str(elf)]).decode()
    for symbol in ('research_stabilized_sitl_main', 'StabilizedManualControl::cycle',
                   'allocateCanards'):
        assert symbol in symbols, 'Private owner symbol missing: ' + symbol
    assert 'bench_active_test_main' not in symbols and 'research_bench_control_main' not in symbols
    assert base.digest(elf) != ACCEPTED_PX4_SHA256
    record = dict(scope='private_synthetic_generated_allocation_variant',
                  accepted_parent_manifest_sha256=ACCEPTED_MANIFEST_SHA256,
                  allocation_parent_manifest_sha256=ALLOCATION_MANIFEST_SHA256,
                  accepted_parent_px4_sha256=ACCEPTED_PX4_SHA256,
                  original_owner_sha256=base.digest(base.ROOT / OWNER),
                  patched_owner_sha256=base.digest(source / OWNER),
                  original_config_sha256=base.digest(base.ROOT / CONFIG),
                  patched_config_sha256=base.digest(source / CONFIG),
                  patch_sha256=base.digest(PATCH), generator_sha256=base.digest(GENERATOR),
                  compiled_report_sha256=base.digest(compiled),
                  synthetic_input_sha256=base.digest(ALLOCATION / 'stabilized_allocation_synthetic.json'),
                  generated_header_sha256=base.digest(header),
                  generated_px4_sha256=base.digest(elf), plugin_sha256=PLUGIN_SHA256,
                  preparation_anchors=len(checkpoint), vehicle_qualified=False,
                  accepted_campaign_expansion=False, production_source_changed=False)
    (out / 'generated-allocation-origin.json').write_text(json.dumps(record, indent=2) + '\n')
    for path in (PATCH, GENERATOR, Path(__file__)):
        shutil.copy2(path, out / path.name)
    shutil.copy2(compiled, out / compiled.name)
    shutil.copy2(ALLOCATION / 'stabilized_allocation_synthetic.json', out / 'stabilized_allocation_synthetic.json')
    print(json.dumps(dict(evidence=str(out), px4_sha256=record['generated_px4_sha256'],
                          profile_sha256=record['synthetic_input_sha256'],
                          anchors=len(checkpoint)), indent=2), flush=True)


if __name__ == '__main__':
    build()
