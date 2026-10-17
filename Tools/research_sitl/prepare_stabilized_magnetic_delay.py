#!/usr/bin/env python3
"""Build a changed private sensor relay against the accepted SITL ELF."""
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

import verify_stabilized_sitl as base


PARENT = Path('/var/tmp/research-stabilized-sitl-rjr0eutz')
VARIANT_INPUTS = {'RotationalFixture.cpp', 'world.sdf.in'}


def prepare():
    parent = PARENT.resolve()
    manifest_path = parent / 'campaign-manifest.json'
    manifest = json.loads(manifest_path.read_text())
    assert manifest['accepted'] and not manifest['failed_cases']
    for name, identity in manifest['files'].items():
        assert base.digest(parent / name) == identity, 'Parent evidence changed: ' + name

    changes = {}
    for path in base.FIXTURE.rglob('*'):
        if not path.is_file():
            continue
        name = str(path.relative_to(base.FIXTURE))
        before = base.digest(parent / 'px4-source' / path.relative_to(base.ROOT))
        after = base.digest(path)
        if before != after:
            assert name in VARIANT_INPUTS, 'Compiled native fixture input changed: ' + name
            changes[name] = dict(parent_sha256=before, variant_sha256=after)
    assert set(changes) == VARIANT_INPUTS
    for path in (base.ROOT / 'src/lib/research_actuator_command').rglob('*'):
        if path.is_file():
            relative = path.relative_to(base.ROOT)
            assert base.digest(path) == base.digest(parent / 'px4-source' / relative), \
                'Shared plugin/native dependency changed: ' + str(relative)

    graph = json.loads((base.ROOT / 'docs/RESEARCH_CODE_GRAPH.json').read_text())
    checkpoint = {}
    for node in graph['nodes']:
        if 'sha256' in node:
            assert base.digest(base.ROOT / node['path']) == node['sha256'], 'Stale graph: ' + node['path']
            checkpoint[node['path']] = node['sha256']

    out = Path(tempfile.mkdtemp(prefix='research-stabilized-sitl-', dir='/var/tmp'))
    source = out / 'px4-source'
    shutil.copytree(base.FIXTURE, source / base.FIXTURE.relative_to(base.ROOT))
    shutil.copytree(base.ROOT / 'src/lib/research_actuator_command',
                    source / 'src/lib/research_actuator_command')
    native = out / 'sitl-lockstep-build'
    shutil.copytree(parent / 'sitl-lockstep-build/bin', native / 'bin', symlinks=True)
    for name in ('boardconfig', 'compile_commands.json'):
        shutil.copy2(parent / 'sitl-lockstep-build' / name, native / name)
    (out / 'checkpoint.json').write_text(json.dumps(checkpoint, indent=2) + '\n')
    (out / 'variant-source-changes.json').write_text(json.dumps(changes, indent=2) + '\n')
    plugin = out / 'plugin-build'
    for label, args in (
        ('plugin-configure', ['cmake', '-S', str(source / base.FIXTURE.relative_to(base.ROOT)),
                              '-B', str(plugin), '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=RelWithDebInfo']),
        ('plugin-build', ['cmake', '--build', str(plugin), '-j', '4']),
    ):
        with (out / (label + '.log')).open('w') as stream:
            subprocess.run(args, stdout=stream, stderr=subprocess.STDOUT, check=True)
    (out / 'native-origin.json').write_text(json.dumps(dict(
        parent=str(parent), parent_manifest_sha256=base.digest(manifest_path),
        reuse='exact_accepted_px4_elf_fresh_source_bound_private_sensor_relay_plugin',
        px4_sha256=base.digest(native / 'bin/px4'),
        plugin_sha256=base.digest(plugin / 'libresearch_rotational_fixture.so'),
        variant_inputs=changes), indent=2) + '\n')
    assert base.digest(native / 'bin/px4') == base.digest(parent / 'sitl-lockstep-build/bin/px4')
    print(out, flush=True)


if __name__ == '__main__':
    prepare()
