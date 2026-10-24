#!/usr/bin/env python3
"""Stage and build the private synthetic manual mode from the current checkout.

Requires Linux/WSL, initialized submodules, the PX4 SITL dependencies and Gazebo
Harmonic development packages. No historical source tree or binary is consumed.
Building does not qualify a new binary or replace the default operator profile.
"""
import argparse
import difflib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

import prepare_stabilized_publisher_epoch as revision
import verify_stabilized_sitl as base

OVERLAYS = (
    'msg',
    'src/lib/research_actuator_command',
    'src/lib/research_stabilized_manual',
    'src/lib/mixer_module',
    'src/modules/manual_control',
    'Tools/research_sitl/stabilized_manual',
)
PATCHED = (revision.OWNER, revision.NODE_H, revision.NODE_CPP, revision.MODULE)


def git(root, *args):
    return subprocess.check_output(['git', '-C', str(root), *args])


def working_changes(root, head):
    command = ['git', '-C', str(root)]
    # Native Git avoids a very slow stat scan when WSL sources are on a
    # Windows drive. Linux checkouts continue to use the normal Linux client.
    windows_git = shutil.which('git.exe')
    if windows_git and len(root.parts) > 3 and root.parts[1] == 'mnt' and len(root.parts[2]) == 1:
        windows_path = subprocess.check_output(['wslpath', '-w', str(root)]).decode().strip()
        command = [windows_git, '-C', windows_path]
    return {relative_path(os.fsdecode(name)) for name in subprocess.check_output(
        command + ['diff', '--name-only', '--no-renames', '--ignore-submodules=all', '-z', head]
    ).split(b'\0') if name}


def relative_path(name):
    path = Path(name)
    if path.is_absolute() or '..' in path.parts:
        raise RuntimeError('Unsafe source path: ' + name)
    return path


def stage_repository(root, destination, out, inputs, repositories, prefix=Path()):
    """Archive HEAD, apply tracked working changes, and recurse into gitlinks.

    Private Git metadata borrows local objects only for PX4 version generation.
    Every compiled source is copied; no checkout or submodule update runs in the
    user's repository, and no network operation is needed.
    """
    head = git(root, 'rev-parse', '--verify', 'HEAD').decode().strip()
    base.run(['git', 'clone', '--quiet', '--shared', '--no-checkout',
              '--no-recurse-submodules', '--', str(root), str(destination)],
             out, 'source-clone.log')
    base.run(['git', '-C', str(destination), 'read-tree', head], out, 'source-index.log')
    # A shared clone normally inherits the source HEAD. Verify rather than
    # silently building the wrong version if the checkout moved while staging.
    if git(destination, 'rev-parse', 'HEAD').decode().strip() != head:
        raise RuntimeError('Checkout HEAD moved while staging: ' + str(root))
    repositories[prefix.as_posix()] = head
    print('Stage', prefix.as_posix(), head[:12], flush=True)
    # A Git archive avoids thousands of separate Windows/WSL file reads.
    # Unchanged files use committed line endings; changed files retain their
    # working bytes. Files omitted by export attributes are copied below.
    archive = out / 'source-stage.tar'
    with archive.open('wb') as stream:
        subprocess.run(['git', '-C', str(root), 'archive', '--format=tar', head],
                       stdout=stream, check=True)
    base.run(['tar', '-xf', str(archive), '-C', str(destination)], out, 'source-extract.log')
    archive.unlink()
    changed = working_changes(root, head)
    # The archive contains HEAD files, including paths removed by a working-tree
    # rename. Omit those paths from both the snapshot and its input manifest.
    removed = {name for name in changed if not (root / name).exists() and not (root / name).is_symlink()}
    for name in removed:
        target = destination / name
        if target.is_file() or target.is_symlink():
            target.unlink()
        elif target.exists():
            raise RuntimeError('Removed directory/submodule requires explicit handling: ' + name)
    entries = git(root, 'ls-files', '--stage', '-z').split(b'\0')
    for entry in entries:
        if not entry:
            continue
        metadata, raw_name = entry.split(b'\t', 1)
        mode, identity, stage = metadata.decode().split()
        name = relative_path(os.fsdecode(raw_name))
        source, target = root / name, destination / name
        if name in removed and mode in ('100644', '100755', '120000'):
            continue
        full_name = (prefix / name).as_posix()
        if stage != '0':
            raise RuntimeError('Unresolved index entry: ' + full_name)
        if mode == '160000':
            if not (source / '.git').exists():
                raise RuntimeError('Initialize the checkout submodule first: ' + full_name)
            if git(source, 'rev-parse', 'HEAD').decode().strip() != identity:
                raise RuntimeError('Submodule does not match its gitlink: ' + full_name)
            stage_repository(source, target, out, inputs, repositories, prefix / name)
            continue
        target.parent.mkdir(parents=True, exist_ok=True)
        if mode == '120000':
            # Windows may materialize an indexed symlink as its target text.
            link = os.readlink(source) if source.is_symlink() else source.read_text().strip()
            resolved = (source.parent / link).resolve()
            if not resolved.is_relative_to(base.ROOT.resolve()):
                raise RuntimeError('Symlink leaves checkout: ' + full_name)
            if target.exists() or target.is_symlink():
                target.unlink()
            target.symlink_to(link)
            inputs[full_name] = dict(kind='symlink', target=link)
        elif mode in ('100644', '100755'):
            if name in changed or not target.is_file():
                if not source.is_file() or source.is_symlink():
                    raise RuntimeError('Missing indexed working file: ' + full_name)
                shutil.copyfile(source, target)
            target.chmod(0o755 if mode == '100755' else 0o644)
            inputs[full_name] = dict(kind='file', sha256=base.digest(target), mode=mode)
        else:
            raise RuntimeError('Unsupported index mode: ' + mode)


def prepare(out):
    graph = json.loads((base.ROOT / 'docs/RESEARCH_CODE_GRAPH.json').read_text())
    checkpoint = {}
    for node in graph['nodes']:
        if 'sha256' in node:
            if base.digest(base.ROOT / node['path']) != node['sha256']:
                raise RuntimeError('Stale graph: ' + node['path'])
            checkpoint[node['path']] = node['sha256']
    (out / 'checkpoint.json').write_text(json.dumps(checkpoint, indent=2) + '\n')
    source = out / 'px4-source-publisher-epoch'
    inputs, repositories = {}, {}
    stage_repository(base.ROOT, source, out, inputs, repositories)
    for directory in OVERLAYS:
        for path in sorted((base.ROOT / directory).rglob('*')):
            if not path.is_file():
                continue
            if path.is_symlink():
                raise RuntimeError('Unexpected research overlay symlink: ' + str(path))
            name = path.relative_to(base.ROOT)
            target = source / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(path, target)
            inputs[name.as_posix()] = dict(kind='file', sha256=base.digest(target), mode='100644')
    # Preserve exact bytes for curated source anchors, including stock headers
    # and tracked research integrations, regardless of Git line-ending rules.
    for name in checkpoint:
        target = source / name
        if name in inputs or name.startswith(('src/', 'platforms/', 'msg/', 'boards/')):
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(base.ROOT / name, target)
            if name not in inputs:
                inputs[name] = dict(kind='file', mode='100644')
            inputs[name]['sha256'] = base.digest(target)
    originals = {path: (source / path).read_text() for path in PATCHED}
    for path, content in revision.variant_sources(originals).items():
        (source / path).write_text(content)
    support = {}
    for path in sorted(revision.SUPPORT.iterdir()):
        if path.suffix in ('.hpp', '.cpp'):
            target = source / base.FIXTURE.relative_to(base.ROOT) / path.name
            shutil.copyfile(path, target)
            support[str(path.relative_to(base.ROOT))] = base.digest(path)
    changed = [source / path for path in PATCHED if path.suffix in ('.hpp', '.cpp')]
    changed += [source / base.FIXTURE.relative_to(base.ROOT) / Path(name).name for name in support]
    base.run(['astyle', '--options=' + str(source / 'Tools/astyle/astylerc'),
              '--suffix=none', *map(str, changed)], out, 'variant-format.log')
    # These two support files are already formatted in the checkout.
    for name, identity in support.items():
        if base.digest(source / base.FIXTURE.relative_to(base.ROOT) / Path(name).name) != identity:
            raise RuntimeError('Format the private support source first: ' + name)
    patch = ''.join(''.join(difflib.unified_diff(originals[path].splitlines(True),
                        (source / path).read_text().splitlines(True),
                        fromfile='a/' + str(path), tofile='b/' + str(path))) for path in PATCHED)
    (out / 'publisher-epoch.patch').write_text(patch)
    # Existing fixture runners compare original test inputs here. Compiled
    # private inputs live in px4-source-publisher-epoch, not this small snapshot.
    for path in base.FIXTURE.rglob('*'):
        if path.is_file():
            target = out / 'px4-source' / path.relative_to(base.ROOT)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(path, target)
    record = dict(scope='current_checkout_private_posix_stabilized_manual_build',
                  historical_sources_used=False, historical_binaries_used=False,
                  snapshot_semantics='git_head_archive_with_tracked_changes_exact_graph_anchors_and_research_overlays',
                  repositories=repositories, working_inputs=inputs,
                  original_sources={str(path): inputs[str(path)]['sha256'] for path in PATCHED},
                  patched_sources={str(path): base.digest(source / path) for path in PATCHED},
                  support_sources=support, preparation_anchors=len(checkpoint),
                  production_source_changed=False, vehicle_qualified=False,
                  accepted_campaign_expansion=False)
    (out / 'checkout-source-origin.json').write_text(json.dumps(record, indent=2) + '\n')
    shutil.copyfile(Path(__file__), out / Path(__file__).name)
    shutil.copyfile(Path(revision.__file__), out / Path(revision.__file__).name)
    return record


def verify_sources(out, record):
    source = out / 'px4-source-publisher-epoch'
    patched = record['patched_sources']
    for name, metadata in record['working_inputs'].items():
        target = source / relative_path(name)
        if metadata['kind'] == 'symlink':
            if not target.is_symlink() or os.readlink(target) != metadata['target']:
                raise RuntimeError('Staged symlink changed: ' + name)
        elif base.digest(target) != patched.get(name, metadata['sha256']):
            raise RuntimeError('Staged source changed: ' + name)
    for name, identity in record['support_sources'].items():
        target = source / base.FIXTURE.relative_to(base.ROOT) / Path(name).name
        if base.digest(target) != identity:
            raise RuntimeError('Private support changed: ' + name)
    checkpoint = json.loads((out / 'checkpoint.json').read_text())
    for name, identity in checkpoint.items():
        if base.digest(base.ROOT / name) != identity:
            raise RuntimeError('Checkout anchor changed during build: ' + name)
    print('Verified', len(record['working_inputs']), 'staged inputs and', len(checkpoint), 'checkout anchors', flush=True)


def build(out, jobs):
    record = json.loads((out / 'checkout-source-origin.json').read_text())
    verify_sources(out, record)
    source = out / 'px4-source-publisher-epoch'
    native, plugin = out / 'sitl-lockstep-build', out / 'plugin-build'
    # Never let PX4's submodule helper update a staged checkout or access the
    # network. Every gitlink was already checked and staged explicitly.
    env = dict(os.environ, GIT_SUBMODULES_ARE_EVIL='1')
    base.run(['cmake', '-S', str(source / base.FIXTURE.relative_to(base.ROOT)),
              '-B', str(plugin), '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=RelWithDebInfo'],
             out, 'plugin-configure.log', env=env)
    base.run(['cmake', '--build', str(plugin), '-j', str(jobs)], out, 'plugin-build.log', env=env)
    print('Fresh strict Gazebo plugin PASS', flush=True)
    base.run(['cmake', '-S', str(source), '-B', str(native), '-G', 'Ninja',
              '-DCONFIG=px4_sitl_default', '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON',
              '-DCMAKE_CXX_FLAGS=-DRESEARCH_STABILIZED_PUBLISHER_EPOCH=1',
              '-DEXTERNAL_MODULES_LOCATION=' + str(source / base.FIXTURE.relative_to(base.ROOT) / 'px4_external')],
             out, 'sitl-configure.log', env=env)
    base.run(['cmake', '--build', str(native), '--target', 'mavlink_c_generate', '-j', str(jobs)],
             out, 'sitl-mavlink.log', env=env)
    base.run(['cmake', '--build', str(native), '-j', str(jobs)], out, 'sitl-build.log', env=env)
    symbols = subprocess.check_output(['nm', '-C', str(native / 'bin/px4')]).decode()
    for symbol in ('research_stabilized_sitl_main', 'StabilizedManualControl::cycle',
                   'allocateCanards', 'publisherRevisionSelfTest'):
        if symbol not in symbols:
            raise RuntimeError('Missing private linked symbol: ' + symbol)
    for symbol in ('bench_active_test_main', 'research_bench_control_main'):
        if symbol in symbols:
            raise RuntimeError('Inherited active bench module: ' + symbol)
    verify_sources(out, record)
    (out / 'linked-symbols.log').write_text('\n'.join(line for line in symbols.splitlines()
        if 'stabilized' in line or 'Stabilized' in line or 'publisherRevision' in line) + '\n')
    record.update(variant_px4_sha256=base.digest(native / 'bin/px4'),
                  plugin_sha256=base.digest(plugin / 'libresearch_rotational_fixture.so'),
                  builder_sha256=base.digest(Path(__file__)))
    (out / 'publisher-epoch-origin.json').write_text(json.dumps(record, indent=2) + '\n')
    print(json.dumps(dict(evidence=str(out), px4_sha256=record['variant_px4_sha256'],
                         plugin_sha256=record['plugin_sha256'], qualified=False), indent=2), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-only', type=Path, help='Continue a prepared private checkout snapshot')
    parser.add_argument('--jobs', type=int, default=4)
    args = parser.parse_args()
    if os.name != 'posix' or args.jobs < 1:
        parser.error('Use Linux/WSL and a positive job count')
    if args.build_only:
        out = args.build_only.resolve()
        if out.parent != Path('/var/tmp') or not out.name.startswith('research-stabilized-sitl-'):
            parser.error('Build continuation must name a private /var/tmp research-stabilized-sitl-* directory')
    else:
        out = Path(tempfile.mkdtemp(prefix='research-stabilized-sitl-', dir='/var/tmp'))
        print('Fresh checkout build:', out, flush=True)
        prepare(out)
    build(out, args.jobs)


if __name__ == '__main__':
    main()
