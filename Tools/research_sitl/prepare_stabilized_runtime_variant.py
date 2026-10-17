#!/usr/bin/env python3
"""Reuse exact built code with an explicitly archived runtime SDF-only change."""
import argparse
import json
from pathlib import Path
import shutil
import tempfile
import xml.etree.ElementTree as ET

import verify_stabilized_sitl as base
import diagnose_stabilized_coupled as diagnostic


def seal_incomplete_parent(out):
    assert not (out / 'campaign-manifest.json').exists(), 'Parent is already sealed'
    files = {}
    for path in out.iterdir():
        if path.is_file() and not path.name.endswith('-manifest.json'):
            files[path.name] = base.digest(path)
        elif path.is_dir() and path.name.startswith(('coupled-', 'tested-artifacts')):
            for artifact in path.rglob('*'):
                if artifact.is_file():
                    files[str(artifact.relative_to(out))] = base.digest(artifact)
    for name in ('sitl-lockstep-build/bin/px4', 'sitl-lockstep-build/boardconfig',
                 'sitl-lockstep-build/compile_commands.json', 'plugin-build/libresearch_rotational_fixture.so'):
        files[name] = base.digest(out / name)
    (out / 'campaign-manifest.json').write_text(json.dumps(dict(
        scope='retained_incomplete_synthetic_coupled_trials', accepted=False,
        native_provenance='exact_binary_runtime_variant' if (out / 'native-origin.json').exists() else 'fresh_native_build',
        physical_authority_granted=False, files=files), indent=2) + '\n')
    print('Incomplete parent manifest:', base.digest(out / 'campaign-manifest.json'), flush=True)


def prepare(parent):
    manifest_path = parent / 'campaign-manifest.json'
    manifest = json.loads(manifest_path.read_text())
    for name, identity in manifest['files'].items():
        assert base.digest(parent / name) == identity, 'Parent artifact changed: ' + name
    changes = {}
    for path in base.FIXTURE.rglob('*'):
        if not path.is_file():
            continue
        name = str(path.relative_to(base.ROOT))
        before = base.digest(parent / 'px4-source' / name)
        after = base.digest(path)
        if before != after:
            assert name == 'Tools/research_sitl/stabilized_manual/world.sdf.in', 'Compiled fixture input changed: ' + name
            changes[name] = dict(previous_sha256=before, new_sha256=after)
    assert changes, 'Require an explicit runtime SDF variant'
    graph = json.loads((base.ROOT / 'docs/RESEARCH_CODE_GRAPH.json').read_text())
    checkpoint = {}
    for node in graph['nodes']:
        if 'sha256' in node:
            assert base.digest(base.ROOT / node['path']) == node['sha256'], 'Stale graph: ' + node['path']
            checkpoint[node['path']] = node['sha256']
    out = Path(tempfile.mkdtemp(prefix='research-stabilized-sitl-', dir='/var/tmp'))
    field = diagnostic.magnetic_field(parent, out)
    declared = list(map(float, ET.fromstring((base.FIXTURE / 'world.sdf.in').read_text())
                         .find('world/magnetic_field').text.split()))
    assert len(declared) == 3 and all(abs(a - b) < 1e-15 for a, b in zip(field, declared)), \
        'Declared field differs from the exact compiled geographic lookup'
    shutil.copy2(parent / 'sitl-lockstep-build/src/lib/world_magnetic_model/libworld_magnetic_model.a',
                 out / 'world_magnetic_model.a')
    base.run(['cp', '-a', str(parent / 'px4-source'), str(out / 'px4-source')], out, 'source-copy.log')
    for name in changes:
        archived = out / 'runtime-inputs-before' / name
        archived.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(parent / 'px4-source' / name, archived)
        shutil.copy2(base.ROOT / name, out / 'px4-source' / name)
    (out / 'sitl-lockstep-build').mkdir()
    shutil.copytree(parent / 'sitl-lockstep-build/bin', out / 'sitl-lockstep-build/bin', symlinks=True)
    for name in ('boardconfig', 'compile_commands.json'):
        shutil.copy2(parent / 'sitl-lockstep-build' / name, out / 'sitl-lockstep-build' / name)
    (out / 'plugin-build').mkdir()
    shutil.copy2(parent / 'plugin-build/libresearch_rotational_fixture.so', out / 'plugin-build/libresearch_rotational_fixture.so')
    (out / 'checkpoint.json').write_text(json.dumps(checkpoint, indent=2) + '\n')
    (out / 'runtime-source-changes.json').write_text(json.dumps(changes, indent=2) + '\n')
    (out / 'native-origin.json').write_text(json.dumps(dict(
        parent=str(parent), parent_manifest_sha256=base.digest(manifest_path),
        reuse='unchanged_compiled_runtime_only_declared_sdf_earth_field_changed',
        px4_sha256=base.digest(out / 'sitl-lockstep-build/bin/px4'),
        plugin_sha256=base.digest(out / 'plugin-build/libresearch_rotational_fixture.so')), indent=2) + '\n')
    shutil.copy2(Path(__file__), out / Path(__file__).name)
    print('Prepared runtime-only variant:', out, flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--parent', type=Path, required=True)
    parser.add_argument('--seal-incomplete-parent', action='store_true')
    args = parser.parse_args()
    parent = args.parent.resolve()
    assert parent.parent == Path('/var/tmp') and parent.name.startswith('research-stabilized-sitl-')
    if args.seal_incomplete_parent:
        seal_incomplete_parent(parent)
    else:
        prepare(parent)


if __name__ == '__main__':
    main()
