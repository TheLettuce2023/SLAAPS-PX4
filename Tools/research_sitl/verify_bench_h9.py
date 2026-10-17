#!/usr/bin/env python3
"""Private actual-PX4 selector/owner/mixer tests. Memory sink, never hardware."""
import argparse
import json
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
import verify_bench_h8 as base

ROOT = base.ROOT
OUT = Path('/var/tmp/research-bench-h9')
base.OUT = OUT
run = base.run
digest = base.digest
EDITABLE_INPUTS = set()
EXTRA_EVIDENCE = []
SCENARIOS = ('nominal', 'source', 'invalid', 'loss', 'lease', 'routing',
             'prearm', 'armed', 'kill', 'termination', 'lockdown', 'stall', 'restart',
             'wire_nominal', 'wire_source', 'wire_invalid', 'wire_target')
BOUND_INPUTS = ['src/modules/mavlink/mavlink_receiver.cpp', 'src/modules/mavlink/mavlink_receiver.h',
                'src/modules/mavlink/mavlink_main.cpp', 'src/modules/mavlink/mavlink_main.h',
                'platforms/common/px4_work_queue/WorkItem.cpp',
                'platforms/common/px4_work_queue/WorkQueue.cpp',
                'platforms/common/px4_work_queue/ScheduledWorkItem.cpp']


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['snapshot', 'host', 'prepare', 'sitl', 'test', 'debug', 'board', 'manifest'])
    parser.add_argument('--scenario', choices=SCENARIOS)
    args = parser.parse_args()
    OUT.mkdir(exist_ok=True)
    source = OUT / 'px4-source'
    if args.action in ('snapshot', 'host'):
        base.main()
    elif args.action == 'prepare':
        if not (OUT / 'checkpoint.json').exists():
            raise RuntimeError('Snapshot required first')
        if not source.exists():
            run(['cp', '-a', '/var/tmp/research-bench-h8/px4-source', str(source)], 'source-copy.log')
        for folder in base.OVERLAYS + ['src/modules/manual_control']:
            shutil.copytree(ROOT / folder, source / folder, dirs_exist_ok=True)
        shutil.copy2(ROOT / 'src/lib/CMakeLists.txt', source / 'src/lib/CMakeLists.txt')
        fixture = ROOT / 'Tools/research_sitl/bench_active'
        run(['astyle', '--options=' + str(ROOT / 'Tools/astyle/astylerc'), '--suffix=none',
             str(fixture / 'BenchActiveTest.cpp')], 'fixture-astyle.log')
        shutil.copytree(fixture, source / 'src/examples/bench_active_test', dirs_exist_ok=True)
        for board in ('px4/sitl', 'px4/fmu-v6x'):
            relative = Path('boards') / board / 'default.px4board'
            config = (ROOT / relative).read_text() + '\nCONFIG_MODULES_RESEARCH_BENCH_CONTROL=y\n'
            if board == 'px4/sitl':
                config += 'CONFIG_BOARD_NOLOCKSTEP=y\nCONFIG_EXAMPLES_BENCH_ACTIVE_TEST=y\n'
            if (source / relative).read_text() != config:
                (source / relative).write_text(config)
        print('Private source prepared; checkout startup and board defaults untouched')
    elif args.action in ('sitl', 'board'):
        label = 'sitl' if args.action == 'sitl' else 'v6x'
        if label == 'v6x' and not (OUT / 'native-clean-complete.log').exists():
            sys.argv = [sys.argv[0], 'clean-native']
            base.main()
        build = OUT / (label + '-build')
        config = 'px4_sitl_default' if label == 'sitl' else 'px4_fmu-v6x_default'
        run(['cmake', '-S', str(source), '-B', str(build), '-G', 'Ninja', '-DCONFIG=' + config,
             '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON'], label + '-configure.log')
        run(['cmake', '--build', str(build), '--target', 'mavlink_c_generate', '-j', '4'], label + '-mavlink.log')
        run(['cmake', '--build', str(build), '-j', '4'], label + '-full.log')
        elf = build / ('bin/px4' if label == 'sitl' else 'px4_fmu-v6x_default.elf')
        prefix = '' if label == 'sitl' else 'arm-none-eabi-'
        symbols = subprocess.check_output([prefix + 'nm', '-C', str(elf)]).decode()
        for symbol in ('research_bench_control_main', 'BenchControlOwner::cycle', 'BenchOutputLease::update'):
            if symbol not in symbols:
                raise RuntimeError('Missing linked symbol: ' + symbol)
        if ('bench_active_test_main' in symbols) != (label == 'sitl'):
            raise RuntimeError('Private fixture linkage isolation failed')
        (OUT / (label + '-linked-symbols.log')).write_text('\n'.join(
            line for line in symbols.splitlines() if 'Bench' in line or 'bench_' in line) + '\n')
        run([prefix + 'size', str(elf)], label + '-linked-size.log')
        if label == 'v6x':
            run(['arm-none-eabi-g++', '--version'], 'compiler.log')
            members = subprocess.check_output(['arm-none-eabi-ar', 't',
                str(source / 'platforms/nuttx/NuttX/apps/libapps.a')]).decode()
            tables = [line for line in members.splitlines() if line.startswith('builtin_list.')]
            if len(tables) != 1 or OUT.name not in tables[0]:
                raise RuntimeError('Stale NuttX builtin table')
            (OUT / 'clean-native-members.log').write_text(members)
        print(label + ' fresh linked build PASS')
    elif args.action in ('test', 'debug'):
        results = []
        binary = OUT / 'sitl-build/bin/px4'
        binary_hash = digest(binary)
        retained_binary = OUT / ('tested-px4-' + binary_hash)
        if not retained_binary.exists():
            shutil.copy2(binary, retained_binary)
        for index, scenario in enumerate((args.scenario,) if args.scenario else SCENARIOS):
            working = Path(tempfile.mkdtemp(prefix=scenario + '-', dir=OUT))
            profile = working / 'synthetic-profile.txt'
            profile.write_text('1 0 255 190 200000 0.05 0.05 500000\n'
                '1400 1500 1600 10 1000 1000 1 1 1\n'
                '1600 1500 1400 10 1000 1000 1 -1 -1\n'
                '1400 1500 1600 10 1000 1000 -1 1 -1\n'
                '1400 1500 1600 10 1000 1000 -1 -1 1\n')
            script = working / 'rc.capture'
            wire_start = ('dataman start -r\nmavlink start -u 19759 -o 19760 -t 127.0.0.1 -m custom\n'
                          if scenario.startswith('wire_') else '')
            wire_stop = 'mavlink stop-all\ndataman stop\n' if scenario.startswith('wire_') else ''
            script.write_text('#!/bin/sh\nset -e\n. px4-alias.sh\n'
                + wire_start + f'bench_active_test {scenario} {profile}\n' + wire_stop
                + 'echo H9_CAPTURE_COMPLETE\nshutdown\n')
            name = scenario + '-test.log'
            command = [str(OUT / 'sitl-build/bin/px4'), '-d', '-i', str(29300 + index),
                       '-w', str(working), '-s', str(script)]
            if args.action == 'debug':
                debug = working / 'fault.gdb'
                debug.write_text('set pagination off\nset confirm off\n'
                    'handle SIGPIPE nostop noprint pass\n'
                    'handle SIGCONT nostop noprint pass\n'
                    'break research_actuator_command::BenchControlOwner::latch\ncommands\nsilent\n'
                    'printf "H9_OWNER_FAULT fault=%d input_status=%d\\n", fault, this->_input_status\n'
                    'bt 4\nframe 1\n'
                    'printf "H9_TIMES now=%llu publication=%llu sample=%llu\\n", now_us, sample.timestamp_us, sample.timestamp_sample_us\n'
                    'continue\nend\nrun\n')
                command = ['gdb', '--batch', '-x', str(debug), '--args'] + command
                name = scenario + '-debug.log'
            try:
                run(command, name, cwd=working, timeout=50)
                log = (OUT / name).read_text()
                passed = 'failures=0 ' in log and 'H9_CAPTURE_COMPLETE' in log and 'CHECK_FAIL' not in log
            except (subprocess.CalledProcessError, subprocess.TimeoutExpired):
                passed = False
            log = (OUT / name).read_text()
            counts = re.search(r'H9_RESULT scenario=\S+ checks=(\d+) failures=(\d+) capture_writes=(\d+)', log)
            results.append({'scenario': scenario, 'passed': passed, 'log': name,
                'log_sha256': digest(OUT / name), 'binary_sha256': binary_hash,
                'script': str(script.relative_to(OUT)), 'profile_sha256': digest(profile),
                'checks': int(counts[1]) if counts else None,
                'failures': int(counts[2]) if counts else None,
                'capture_writes': int(counts[3]) if counts else None})
            print(scenario, 'PASS' if passed else 'FAIL', flush=True)
        results_path = OUT / (('results-' + args.scenario + '-' + args.action + '.json') if args.scenario else 'results.json')
        if results_path.exists():
            index = 1
            while results_path.with_suffix(f'.json.{index}').exists():
                index += 1
            shutil.copy2(results_path, results_path.with_suffix(f'.json.{index}'))
        results_path.write_text(json.dumps(results, indent=2) + '\n')
        if not all(result['passed'] for result in results):
            raise RuntimeError('Capture integration suite failed; inspect retained logs')
    else:
        checkpoint = json.loads((OUT / 'checkpoint.json').read_text())
        allowed = {'docs/PROJECT_CONTEXT.md', 'docs/PHASE_GATE_STATUS.md',
                   'docs/EDF_CANARD_CONTROL_FOUNDATION.md', 'docs/RESEARCH_CODE_GRAPH.json'}
        allowed.update(EDITABLE_INPUTS)
        preserved = 0
        for name, expected in checkpoint.items():
            if name in allowed or name.startswith('Tools/research_sitl/bench_active/') or name == 'Tools/research_sitl/verify_bench_h9.py':
                continue
            if digest(ROOT / name) != expected:
                raise RuntimeError('Unrelated checkpoint file changed: ' + name)
            preserved += 1
        for folder in base.OVERLAYS + ['src/modules/manual_control']:
            for path in (ROOT / folder).rglob('*'):
                if path.is_file() and digest(path) != digest(source / path.relative_to(ROOT)):
                    raise RuntimeError('Native source mismatch: ' + str(path))
        for path in (ROOT / 'Tools/research_sitl/bench_active').iterdir():
            if digest(path) != digest(source / 'src/examples/bench_active_test' / path.name):
                raise RuntimeError('Native fixture mismatch: ' + str(path))
        for name in BOUND_INPUTS:
            if digest(ROOT / name) != digest(source / name):
                raise RuntimeError('Native dependency mismatch: ' + name)
        for path in (OUT / 'host-source').iterdir():
            if path.is_file() and digest(path) != digest(ROOT / base.OVERLAYS[0] / path.name):
                raise RuntimeError('Portable source mismatch: ' + str(path))
        (OUT / 'preservation.log').write_text(f'{preserved} checkpoint files hash-preserved\n')
        shutil.copy2(Path(__file__), OUT / Path(__file__).name)
        shutil.copy2(ROOT / 'Tools/research_sitl/verify_bench_h8.py', OUT / 'verify_bench_h8.py')
        git = shutil.which('git.exe') or 'git'
        run([git, 'status', '--porcelain', '--untracked-files=all'], 'status-after.log', cwd=ROOT)
        run([git, 'diff', '--cached', '--stat'], 'index-after.log', cwd=ROOT)
        files = list(OUT.glob('*.log*')) + [OUT / 'checkpoint.json', OUT / 'results.json', OUT / Path(__file__).name]
        files += list(OUT.glob('results*.json*')) + [OUT / 'verify_bench_h8.py']
        files += list(OUT.glob('tested-px4-*'))
        files += EXTRA_EVIDENCE
        files += [source / name for name in BOUND_INPUTS]
        files += list((OUT / 'host-source').glob('*'))
        for folder in base.OVERLAYS + ['src/modules/manual_control', 'src/examples/bench_active_test']:
            files += list((source / folder).rglob('*'))
        for label, board in [('sitl', 'px4/sitl'), ('v6x', 'px4/fmu-v6x')]:
            build = OUT / (label + '-build')
            files += [build / 'compile_commands.json', build / 'CMakeCache.txt', build / 'px4_boardconfig.h',
                      build / 'parameters.xml', source / 'boards' / board / 'default.px4board']
            files += [build / 'bin/px4'] if label == 'sitl' else list(build.glob('px4_fmu-v6x_default.*'))
        files += list(OUT.glob('*/synthetic-profile.txt')) + list(OUT.glob('*/rc.capture'))
        files += list(OUT.glob('*/fault.gdb'))
        records = {str(path.relative_to(OUT)): {'sha256': digest(path), 'bytes': path.stat().st_size}
                   for path in sorted(set(files)) if path.is_file()}
        (OUT / 'manifest.json').write_text(json.dumps(records, indent=2) + '\n')
        print(digest(OUT / 'manifest.json'))


if __name__ == '__main__':
    main()
