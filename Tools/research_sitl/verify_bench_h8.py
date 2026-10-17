#!/usr/bin/env python3
"""Build H8 and run a private output-driver-free SITL smoke test; no hardware."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]
OUT = Path('/var/tmp/research-bench-h8')
OVERLAYS = ['src/lib/research_actuator_command', 'src/modules/research_bench_control',
            'src/lib/mixer_module', 'msg']


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def run(cmd, name, **kwargs):
    path = OUT / name
    if path.exists():
        index = 1
        while path.with_suffix(path.suffix + f'.{index}').exists():
            index += 1
        shutil.copy2(path, path.with_suffix(path.suffix + f'.{index}'))
    with path.open('w') as stream:
        subprocess.run(cmd, stdout=stream, stderr=subprocess.STDOUT, check=True, **kwargs)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('action', choices=['snapshot', 'host', 'clean-native', 'px4', 'smoke', 'manifest'])
    args = parser.parse_args()
    OUT.mkdir(exist_ok=True)
    git = shutil.which('git.exe') or 'git'
    if args.action == 'snapshot':
        checkpoint = OUT / 'checkpoint.json'
        if checkpoint.exists():
            raise RuntimeError('Checkpoint already exists')
        paths = subprocess.check_output([git, 'ls-files', '-z', '--modified', '--others',
                                         '--exclude-standard'], cwd=ROOT).decode().split('\0')
        records = {p: digest(ROOT / p) for p in paths if p and (ROOT / p).is_file()}
        # Also bind tracked integration inputs, which may be clean at this point.
        for folder in OVERLAYS:
            for path in (ROOT / folder).rglob('*'):
                if path.is_file():
                    records[str(path.relative_to(ROOT))] = digest(path)
        checkpoint.write_text(json.dumps(records, indent=2) + '\n')
        graph = json.loads((ROOT / 'docs/RESEARCH_CODE_GRAPH.json').read_text())
        for node in graph['nodes']:
            if 'bench' in node['id'] or 'manual' in node['id']:
                if digest(ROOT / node['path']) != node['sha256']:
                    raise RuntimeError(node['path'])
        run([git, 'status', '--porcelain', '--untracked-files=all'], 'status-before.log', cwd=ROOT)
        print(f'Checkpointed {len(records)} files; existing bench/manual anchors verified')
    elif args.action == 'host':
        files = list((ROOT / 'src/modules/research_bench_control').glob('*.cpp'))
        files += list((ROOT / 'src/lib/research_actuator_command').glob('BenchOutput*.*'))
        files += [ROOT / 'src/lib/mixer_module/functions/FunctionBenchServos.hpp',
                  ROOT / 'src/lib/mixer_module/mixer_module.cpp']
        run(['astyle', '--options=' + str(ROOT / 'Tools/astyle/astylerc'), '--suffix=none',
             *map(str, files)], 'astyle.log')
        source = OUT / 'host-source'
        shutil.copytree(ROOT / OVERLAYS[0], source, dirs_exist_ok=True)
        for label, kind, flags in [('debug', 'Debug', ''), ('release', 'Release', ''),
                ('sanitize', 'Debug', '-fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie')]:
            build = OUT / label
            run(['cmake', '-S', str(source), '-B', str(build), '-G', 'Ninja',
                 '-DCMAKE_BUILD_TYPE=' + kind, '-DCMAKE_CXX_FLAGS=-Wall -Wextra -Werror -Wpedantic ' + flags,
                 '-DCMAKE_EXE_LINKER_FLAGS=' + ('-fsanitize=address,undefined -no-pie' if flags else '')], label+'-configure.log')
            run(['cmake', '--build', str(build), '-j', '4'], label+'-build.log')
            env = dict(os.environ, ASAN_OPTIONS='detect_leaks=1:abort_on_error=1', UBSAN_OPTIONS='halt_on_error=1')
            run(['ctest', '--test-dir', str(build), '-V', '--output-on-failure'], label+'-tests.log', env=env)
            print(label + ' PASS', flush=True)
        run(['python3', '-B', '-m', 'unittest', 'discover', '-s', str(ROOT / 'Tools/research_sitl'),
             '-p', 'test_bench_acceptance.py', '-v'], 'acceptance-checker-tests.log')
    elif args.action == 'clean-native':
        native = (OUT / 'px4-source/platforms/nuttx/NuttX').resolve()
        if not native.is_relative_to(OUT.resolve()):
            raise RuntimeError('Native cleanup outside evidence source view')
        archive = native / 'apps/libapps.a'
        retained = OUT / 'rejected-stale-libapps.a'
        if archive.exists() and not retained.exists():
            shutil.copy2(archive, retained)
            run(['arm-none-eabi-ar', 't', str(archive)], 'rejected-stale-members.log')
        run(['make', '-C', str(native / 'apps'), 'TOPDIR=' + str(native / 'nuttx'), 'clean'], 'native-apps-clean.log')
        # PX4's ARCHSCRIPT contains linker -T flags, not a cleanable filename.
        run(['make', '-C', str(native / 'nuttx'), 'ARCHSCRIPT=', 'clean'], 'native-nuttx-clean.log')
        (OUT / 'native-clean-complete.log').write_text('Generated native products cleaned; retained source trees untouched\n')
    elif args.action == 'px4':
        source = OUT / 'px4-source'
        if not source.exists():
            run(['cp', '-a', '/var/tmp/research-bench-h7/px4-source', str(source)], 'source-copy.log')
        if not (OUT / 'native-clean-complete.log').exists():
            raise RuntimeError('Run clean-native on the isolated view before integrated builds')
        for folder in OVERLAYS:
            shutil.copytree(ROOT / folder, source / folder, dirs_exist_ok=True)
        shutil.copy2(ROOT / 'src/lib/CMakeLists.txt', source / 'src/lib/CMakeLists.txt')
        run(['arm-none-eabi-g++', '--version'], 'compiler.log')
        for label, config, board in [('sitl', 'px4_sitl_default', 'px4/sitl'),
                                    ('v6x', 'px4_fmu-v6x_default', 'px4/fmu-v6x')]:
            # Explicit build-only opt-in. User checkout board defaults remain untouched.
            boardfile = source / 'boards' / board / 'default.px4board'
            contents = (ROOT / 'boards' / board / 'default.px4board').read_text()
            opted_in = contents + '\nCONFIG_MODULES_RESEARCH_BENCH_CONTROL=y\n'
            if label == 'sitl':
                # Private bench smoke has no simulator clock source.
                opted_in += 'CONFIG_BOARD_NOLOCKSTEP=y\n'
            if boardfile.read_text() != opted_in:
                boardfile.write_text(opted_in)
            build = OUT / (label + '-build')
            run(['cmake', '-S', str(source), '-B', str(build), '-G', 'Ninja',
                 '-DCONFIG=' + config, '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON'], label+'-configure.log')
            run(['cmake', '--build', str(build), '--target', 'mavlink_c_generate', '-j', '4'], label+'-mavlink.log')
            run(['cmake', '--build', str(build), '-j', '4'], label+'-full.log')
            elf = build / ('bin/px4' if label == 'sitl' else 'px4_fmu-v6x_default.elf')
            nm = 'nm' if label == 'sitl' else 'arm-none-eabi-nm'
            symbols = subprocess.check_output([nm, '-C', str(elf)]).decode()
            for required in ['research_bench_control_main', 'BenchControlOwner::cycle', 'BenchOutputLease::update']:
                if required not in symbols:
                    raise RuntimeError('Missing linked symbol: ' + required)
            (OUT / (label+'-linked-symbols.log')).write_text('\n'.join(
                line for line in symbols.splitlines() if 'Bench' in line or 'research_bench_control' in line) + '\n')
            run([('size' if label == 'sitl' else 'arm-none-eabi-size'), str(elf)], label+'-linked-size.log')
            if label == 'v6x':
                archive = source / 'platforms/nuttx/NuttX/apps/libapps.a'
                members = subprocess.check_output(['arm-none-eabi-ar', 't', str(archive)]).decode()
                tables = [line for line in members.splitlines() if line.startswith('builtin_list.')]
                if len(tables) != 1 or 'research-bench-h8' not in tables[0]:
                    raise RuntimeError('Stale or duplicate NuttX builtin table')
                (OUT / 'clean-native-members.log').write_text(members)
            print(label + ' integrated build PASS', flush=True)
    elif args.action == 'smoke':
        import tempfile
        working = Path(tempfile.mkdtemp(prefix='disabled-smoke-', dir=OUT))
        fixture = working / 'synthetic-profile.txt'
        fixture.write_text('1 0 255 190 40000 0.05 0.05 100000\n' +
                           '1400 1500 1600 10 1000 1000 1 -1 1\n' * 4)
        script = working / 'rc.disabled-smoke'
        # Set shell options before PX4 aliases redefine the `set` command.
        script.write_text('#!/bin/sh\nset -e\n. px4-alias.sh\n' +
            'if research_bench_control status; then exit 11; fi\n' +
            f'if research_bench_control start {working}/absent; then exit 12; fi\n' +
            f'research_bench_control start {fixture}\nsleep 0.1\nresearch_bench_control status\n' +
            'research_bench_control enable\nsleep 0.1\nresearch_bench_control status\n' +
            'research_bench_control disable\nresearch_bench_control stop\n' +
            'if research_bench_control status; then exit 13; fi\n' +
            'echo H8_DISABLED_SMOKE_COMPLETE\nshutdown\n')
        run([str(OUT / 'sitl-build/bin/px4'), '-d', '-i', '28307', '-w', str(working),
             '-s', str(script)], 'disabled-smoke.log', cwd=working, timeout=20)
        log = (OUT / 'disabled-smoke.log').read_text()
        for marker in ['invalid profile; outputs remain disabled', 'state=0', 'state=2', 'H8_DISABLED_SMOKE_COMPLETE']:
            if marker not in log:
                raise RuntimeError('Missing smoke marker: ' + marker)
        print('Private disabled-only SITL lifecycle smoke PASS; no output drivers or hardware')
    else:
        records = {}
        for folder in OVERLAYS:
            for path in (ROOT / folder).rglob('*'):
                if path.is_file():
                    counterpart = OUT / 'px4-source' / path.relative_to(ROOT)
                    if digest(path) != digest(counterpart):
                        raise RuntimeError('Source mismatch: ' + str(path))
                    if folder == OVERLAYS[0] and digest(path) != digest(OUT / 'host-source' / path.name):
                        raise RuntimeError('Host source mismatch: ' + str(path))
        allowed = {
            'Tools/research_sitl/verify_bench_h8.py', 'msg/CMakeLists.txt',
            'src/lib/research_actuator_command/CMakeLists.txt',
            'src/lib/mixer_module/CMakeLists.txt', 'src/lib/mixer_module/mixer_module.cpp',
            'src/lib/mixer_module/mixer_module.hpp', 'src/lib/mixer_module/output_functions.yaml',
            'docs/PROJECT_CONTEXT.md', 'docs/PHASE_GATE_STATUS.md', 'docs/EDF_CANARD_CONTROL_FOUNDATION.md',
            'docs/RESEARCH_CODE_GRAPH.json', 'docs/MANUAL_BENCH_PHYSICAL_ACCEPTANCE.md',
            'docs/MANUAL_BENCH_CONTROL_H7.md'}
        checkpoint = json.loads((OUT / 'checkpoint.json').read_text())
        preserved = 0
        for relative, expected in checkpoint.items():
            if relative in allowed:
                continue
            if digest(ROOT / relative) != expected:
                raise RuntimeError('Unrelated changed: ' + relative)
            preserved += 1
        (OUT / 'preservation.log').write_text(f'{preserved} unrelated checkpoint files hash-preserved\n')
        shutil.copy2(Path(__file__), OUT / Path(__file__).name)
        run([git, 'rev-parse', 'HEAD'], 'checkout-head.log', cwd=ROOT)
        run([git, 'status', '--porcelain', '--untracked-files=all'], 'status-after.log', cwd=ROOT)
        files = list(OUT.glob('*.log*')) + [OUT / 'checkpoint.json', OUT / Path(__file__).name]
        files += list((OUT / 'host-source').glob('*'))
        for label in ['sitl', 'v6x']:
            files += [OUT / (label+'-build/compile_commands.json')]
            files += [OUT / (label+'-build/CMakeCache.txt'), OUT / (label+'-build/px4_boardconfig.h')]
        files += [OUT / 'px4-source/boards/px4/sitl/default.px4board',
                  OUT / 'px4-source/boards/px4/fmu-v6x/default.px4board']
        files += [OUT / 'sitl-build/bin/px4']
        files += list(OUT.glob('disabled-smoke-*/*'))
        files += [OUT / 'rejected-stale-libapps.a']
        files += list((OUT / 'v6x-build').glob('px4_fmu-v6x_default.*'))
        for folder in OVERLAYS:
            files += list((OUT / 'px4-source' / folder).rglob('*'))
        for path in sorted(set(files)):
            if path.is_file():
                records[str(path.relative_to(OUT))] = {'sha256': digest(path), 'bytes': path.stat().st_size}
        (OUT / 'manifest.json').write_text(json.dumps(records, indent=2) + '\n')
        print(digest(OUT / 'manifest.json'))


if __name__ == '__main__':
    main()
