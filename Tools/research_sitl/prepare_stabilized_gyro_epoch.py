#!/usr/bin/env python3
"""Build a separate synthetic PX4 binary with same-cycle gyro epoch capture."""
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

import verify_stabilized_sitl as base


ACCEPTED = Path('/var/tmp/research-stabilized-sitl-rjr0eutz')
RELAY = Path('/var/tmp/research-stabilized-sitl-sc7ckzfv')
ACCEPTED_MANIFEST_SHA256 = '447823f2cdb88e22759d58b9365cd0508370053738011aa966fc452b1c050da3'
RELAY_MANIFEST_SHA256 = 'd22c9328b24a9dbed643e8197f47652a73be598f1ce3e3f2493b08251f629227'
ACCEPTED_PX4_SHA256 = 'a2c8229b30a93434f6d6aae3170fc67f354227e5ce236eca83720d7b79397f7d'
RELAY_PLUGIN_SHA256 = 'ee093faddf9979b9ba756c300b25d8d27f912a2d8ba3f3e1e2d07a8990780ada'
SOURCE = Path('Tools/research_sitl/stabilized_manual/StabilizedManualSITL.cpp')
PATCH = base.ROOT / 'Tools/research_sitl/gyro_epoch_probe.patch'


def verify_receipt(root, name, expected):
    receipt = root / name
    assert base.digest(receipt) == expected
    manifest = json.loads(receipt.read_text())
    for path, identity in manifest['files'].items():
        assert base.digest(root / path) == identity, 'Sealed parent changed: ' + path


def build():
    verify_receipt(ACCEPTED, 'campaign-manifest.json', ACCEPTED_MANIFEST_SHA256)
    verify_receipt(RELAY, 'rate-trace-manifest.json', RELAY_MANIFEST_SHA256)
    assert base.digest(ACCEPTED / 'sitl-lockstep-build/bin/px4') == ACCEPTED_PX4_SHA256
    assert base.digest(RELAY / 'sitl-lockstep-build/bin/px4') == ACCEPTED_PX4_SHA256
    assert base.digest(RELAY / 'plugin-build/libresearch_rotational_fixture.so') == RELAY_PLUGIN_SHA256
    graph = json.loads((base.ROOT / 'docs/RESEARCH_CODE_GRAPH.json').read_text())
    checkpoint = {}
    for node in graph['nodes']:
        if 'sha256' in node:
            assert base.digest(base.ROOT / node['path']) == node['sha256'], 'Stale graph: ' + node['path']
            checkpoint[node['path']] = node['sha256']
    original_sha = base.digest(base.ROOT / SOURCE)
    assert original_sha == base.digest(ACCEPTED / 'px4-source' / SOURCE)
    out = Path(tempfile.mkdtemp(prefix='research-stabilized-sitl-', dir='/var/tmp'))
    (out / 'checkpoint.json').write_text(json.dumps(checkpoint, indent=2) + '\n')
    source = out / 'px4-source-gyro-epoch'
    base.run(['cp', '-a', '--reflink=auto', str(ACCEPTED / 'px4-source'), str(source)],
             out, 'source-copy.log')
    assert base.digest(source / SOURCE) == original_sha
    with (out / 'patch.log').open('w') as log:
        subprocess.run(['patch', '--batch', '-p1', '-i', str(PATCH)], cwd=source,
                       stdout=log, stderr=subprocess.STDOUT, check=True)
    modified_sha = base.digest(source / SOURCE)
    assert modified_sha != original_sha
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
    shutil.copy2(RELAY / 'plugin-build/libresearch_rotational_fixture.so',
                 plugin / 'libresearch_rotational_fixture.so')
    assert base.digest(plugin / 'libresearch_rotational_fixture.so') == RELAY_PLUGIN_SHA256
    elf = native / 'bin/px4'
    symbols = subprocess.check_output(['nm', '-C', str(elf)]).decode()
    for symbol in ('research_stabilized_sitl_main', 'StabilizedManualControl::cycle',
                   'allocateCanards'):
        assert symbol in symbols, 'Private owner symbol missing: ' + symbol
    assert 'bench_active_test_main' not in symbols and 'research_bench_control_main' not in symbols
    record = dict(scope='private_synthetic_gyro_epoch_diagnostic_variant',
                  accepted_parent_manifest_sha256=ACCEPTED_MANIFEST_SHA256,
                  relay_parent_manifest_sha256=RELAY_MANIFEST_SHA256,
                  accepted_parent_px4_sha256=ACCEPTED_PX4_SHA256,
                  relay_plugin_sha256=RELAY_PLUGIN_SHA256,
                  original_owner_sha256=original_sha,
                  patched_owner_sha256=modified_sha,
                  patch_sha256=base.digest(PATCH), diagnostic_px4_sha256=base.digest(elf),
                  preparation_anchors=len(checkpoint), production_source_changed=False)
    (out / 'gyro-epoch-origin.json').write_text(json.dumps(record, indent=2) + '\n')
    shutil.copy2(PATCH, out / PATCH.name)
    shutil.copy2(Path(__file__), out / Path(__file__).name)
    print(json.dumps(dict(evidence=str(out), px4_sha256=record['diagnostic_px4_sha256'],
                          patch_sha256=record['patch_sha256'], anchors=len(checkpoint)), indent=2),
          flush=True)


if __name__ == '__main__':
    build()
