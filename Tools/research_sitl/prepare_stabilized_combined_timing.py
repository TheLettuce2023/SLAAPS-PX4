#!/usr/bin/env python3
"""Reuse the sealed native sensor-relay variant for combined pilot/state timing tests."""
import json
from pathlib import Path
import shutil
import tempfile

import verify_stabilized_sitl as base


PARENT = Path('/var/tmp/research-stabilized-sitl-z7vo0s62')


def prepare():
    parent = PARENT.resolve()
    receipts = {}
    for name in ('imu-delay-manifest.json', 'imu-boundary-manifest.json'):
        manifest = json.loads((parent / name).read_text())
        for artifact, identity in manifest['files'].items():
            assert base.digest(parent / artifact) == identity, 'Parent evidence changed: ' + artifact
        receipts[name] = base.digest(parent / name)
    origin = json.loads((parent / 'native-origin.json').read_text())
    graph = json.loads((base.ROOT / 'docs/RESEARCH_CODE_GRAPH.json').read_text())
    checkpoint = {}
    for node in graph['nodes']:
        if 'sha256' in node:
            assert base.digest(base.ROOT / node['path']) == node['sha256'], 'Stale graph: ' + node['path']
            checkpoint[node['path']] = node['sha256']
    for path in base.FIXTURE.rglob('*'):
        if path.is_file():
            relative = path.relative_to(base.ROOT)
            assert base.digest(path) == base.digest(parent / 'px4-source' / relative)

    out = Path(tempfile.mkdtemp(prefix='research-stabilized-sitl-', dir='/var/tmp'))
    shutil.copytree(parent / 'px4-source', out / 'px4-source')
    native = out / 'sitl-lockstep-build'
    shutil.copytree(parent / 'sitl-lockstep-build/bin', native / 'bin', symlinks=True)
    for name in ('boardconfig', 'compile_commands.json'):
        shutil.copy2(parent / 'sitl-lockstep-build' / name, native / name)
    plugin = out / 'plugin-build'
    plugin.mkdir()
    shutil.copy2(parent / 'plugin-build/libresearch_rotational_fixture.so',
                 plugin / 'libresearch_rotational_fixture.so')
    (out / 'checkpoint.json').write_text(json.dumps(checkpoint, indent=2) + '\n')
    (out / 'native-origin.json').write_text(json.dumps(dict(
        parent=str(parent), parent_receipts=receipts,
        reuse='exact_sealed_px4_elf_and_private_sensor_relay_plugin',
        px4_sha256=base.digest(native / 'bin/px4'),
        plugin_sha256=base.digest(plugin / 'libresearch_rotational_fixture.so'),
        inherited_origin=origin), indent=2) + '\n')
    assert base.digest(native / 'bin/px4') == origin['px4_sha256']
    assert base.digest(plugin / 'libresearch_rotational_fixture.so') == origin['plugin_sha256']
    shutil.copy2(Path(__file__), out / Path(__file__).name)
    print(out, flush=True)


if __name__ == '__main__':
    prepare()
