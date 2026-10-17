#!/usr/bin/env python3
"""Reuse the sealed private revision binary for actual EKF2 lifecycle tests."""
import json
from pathlib import Path
import shutil
import tempfile

import verify_stabilized_sitl as base
import verify_stabilized_publisher_epoch as epoch

PARENT = Path('/var/tmp/research-stabilized-sitl-rmk764xm')
PARENT_RECEIPT = 'publisher-revision-manifest.json'
PARENT_SHA256 = '9126cd2b2e9e209eb0afadabce5342d15f97fa8940f7cae36f83154dd1729242'


def prepare():
    receipt = PARENT / PARENT_RECEIPT
    assert base.digest(receipt) == PARENT_SHA256
    manifest = json.loads(receipt.read_text())
    for name, identity in manifest['files'].items():
        assert base.digest(PARENT / name) == identity, 'Parent artifact changed: ' + name
    record = epoch.origin(PARENT)
    graph = json.loads((base.ROOT / 'docs/RESEARCH_CODE_GRAPH.json').read_text())
    checkpoint = {}
    for node in graph['nodes']:
        if 'sha256' in node:
            assert base.digest(base.ROOT / node['path']) == node['sha256'], 'Stale graph: ' + node['path']
            checkpoint[node['path']] = node['sha256']
    for path in base.FIXTURE.rglob('*'):
        if path.is_file():
            assert base.digest(path) == base.digest(PARENT / 'px4-source' / path.relative_to(base.ROOT))
    out = Path(tempfile.mkdtemp(prefix='research-stabilized-sitl-', dir='/var/tmp'))
    # Preserve the fixture-input snapshot expected by the unchanged harness.
    # It is separate from the sealed transformed inputs that compiled the ELF.
    shutil.copytree(PARENT / 'px4-source', out / 'px4-source')
    shutil.copytree(PARENT / 'variant-inputs', out / 'variant-inputs')
    shutil.copytree(PARENT / 'audited-inputs', out / 'inherited-compiled-inputs')
    for name in ('compiled-dependencies.json', 'publisher-epoch-origin.json', 'publisher-epoch.patch'):
        shutil.copy2(PARENT / name, out / name)
    staged = out / 'px4-source-publisher-epoch'
    for name in record['patched_sources']:
        destination = staged / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(PARENT / 'variant-inputs' / name, destination)
    for name in record['support_sources']:
        destination = staged / base.FIXTURE.relative_to(base.ROOT) / Path(name).name
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(PARENT / 'variant-inputs' / name, destination)
    native = out / 'sitl-lockstep-build'
    shutil.copytree(PARENT / 'sitl-lockstep-build/bin', native / 'bin', symlinks=True)
    for name in ('boardconfig', 'compile_commands.json'):
        shutil.copy2(PARENT / 'sitl-lockstep-build' / name, native / name)
    plugin = out / 'plugin-build'
    plugin.mkdir()
    shutil.copy2(PARENT / 'plugin-build/libresearch_rotational_fixture.so', plugin / 'libresearch_rotational_fixture.so')
    (out / 'checkpoint.json').write_text(json.dumps(checkpoint, indent=2) + '\n')
    origin = dict(scope='actual_ekf2_lifecycle_tests_on_exact_private_revision_artifacts',
                  parent=str(PARENT), parent_receipt=PARENT_RECEIPT, parent_manifest_sha256=PARENT_SHA256,
                  reuse='exact_sealed_private_elf_plugin_and_archived_compiled_inputs_no_rebuild',
                  px4_sha256=base.digest(native / 'bin/px4'), plugin_sha256=base.digest(plugin / 'libresearch_rotational_fixture.so'),
                  inherited_variant_origin=record, preparation_anchors=len(checkpoint))
    assert origin['px4_sha256'] == record['variant_px4_sha256']
    assert origin['plugin_sha256'] == record['plugin_sha256']
    (out / 'native-origin.json').write_text(json.dumps(origin, indent=2) + '\n')
    shutil.copy2(Path(__file__), out / Path(__file__).name)
    epoch.origin(out)
    print(json.dumps(dict(evidence=str(out), px4_sha256=origin['px4_sha256'], anchors=len(checkpoint)), indent=2), flush=True)


if __name__ == '__main__':
    prepare()
