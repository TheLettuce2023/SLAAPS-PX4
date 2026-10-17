#!/usr/bin/env python3
"""Isolated SM2/SM3 host evidence; no PX4 runtime, Gazebo, outputs or hardware."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'src/lib/research_stabilized_manual'


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run(command, log, environment=None):
    with log.open('w') as output:
        result = subprocess.run(command, stdout=output, stderr=subprocess.STDOUT,
                                env=environment, check=False)
    if result.returncode:
        print(log.read_text()[-6000:], flush=True)
        raise RuntimeError(f'Exit {result.returncode}; retained log {log}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--format', action='store_true', help='Format only the new C++ sources before capture')
    args = parser.parse_args()
    evidence = Path(tempfile.mkdtemp(prefix='research-stabilized-host-', dir='/var/tmp'))
    print(f'Evidence: {evidence}', flush=True)
    if args.format:
        run(['astyle', '--options=' + str(ROOT / 'Tools/astyle/astylerc'), '--suffix=none',
             *[str(path) for path in sorted(SOURCE.iterdir()) if path.suffix in ('.hpp', '.cpp')]],
            evidence / 'format.log')

    graph = json.loads((ROOT / 'docs/RESEARCH_CODE_GRAPH.json').read_text())
    preserved = {node['path']: digest(ROOT / node['path']) for node in graph['nodes']
                 if node.get('path') and node.get('sha256')}
    for node in graph['nodes']:
        if node.get('sha256') and preserved[node['path']] != node['sha256']:
            raise RuntimeError('Changed pre-existing graph anchor: ' + node['id'])
    dependencies = [ROOT / 'src/lib/rate_control/rate_control.cpp', ROOT / 'src/lib/rate_control/rate_control.hpp',
                    ROOT / 'src/lib/research_actuator_command/ManualControlInput.cpp',
                    ROOT / 'src/lib/research_actuator_command/ManualControlInput.hpp',
                    ROOT / 'src/lib/research_actuator_command/FixedArray.hpp', ROOT / 'msg/RateCtrlStatus.msg',
                    ROOT / 'platforms/ros2/include/px4_platform_common/defines.h',
                    ROOT / 'platforms/common/uORB/uORB.h', ROOT / 'src/lib/mathlib/mathlib.h', Path(__file__).resolve()]
    dependencies += list((ROOT / 'src/lib/research_actuator_command').glob('*.hpp'))
    for folder in (SOURCE, ROOT / 'src/lib/matrix', ROOT / 'src/lib/mathlib/math', ROOT / 'Tools/msg'):
        dependencies += [path for path in folder.rglob('*') if path.is_file() and '__pycache__' not in path.parts]
    inputs = {str(path.relative_to(ROOT)): digest(path) for path in sorted(set(dependencies))}
    for name in inputs:
        destination = evidence / 'source-inputs' / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(ROOT / name, destination)
        if digest(destination) != inputs[name]:
            raise RuntimeError('Input snapshot differs: ' + name)
    (evidence / 'inputs.json').write_text(json.dumps(inputs, indent=2) + '\n')
    (evidence / 'preservation-before.json').write_text(json.dumps(preserved, indent=2) + '\n')

    for name, build_type, flags in (('debug', 'Debug', ''), ('release', 'Release', ''),
                                   ('sanitize', 'Debug', '-fsanitize=address,undefined -fno-omit-frame-pointer')):
        build = evidence / name
        run(['cmake', '-S', str(SOURCE), '-B', str(build), '-G', 'Ninja',
             '-DCMAKE_BUILD_TYPE=' + build_type, '-DCMAKE_CXX_FLAGS=' + flags,
             '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON'], evidence / f'{name}-configure.log')
        run(['cmake', '--build', str(build), '-j', '2'], evidence / f'{name}-build.log')
        environment = dict(os.environ, ASAN_OPTIONS='halt_on_error=1:detect_leaks=1', UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
        run(['ctest', '--test-dir', str(build), '-V', '--output-on-failure'], evidence / f'{name}-test.log', environment)
        print(f'{name}: PASS', flush=True)

    if any(digest(ROOT / path) != expected for path, expected in inputs.items()):
        raise RuntimeError('Production/test input changed during verification')
    if any(digest(ROOT / path) != expected for path, expected in preserved.items()):
        raise RuntimeError('Pre-existing graph anchor changed during verification')
    files = {str(path.relative_to(evidence)): digest(path) for path in evidence.rglob('*') if path.is_file()}
    manifest = {'scope': 'host_truth_feedback_only', 'px4_runtime_tested': False,
                'gazebo_tested': False, 'physical_authority_granted': False,
                'preserved_anchor_count': len(preserved), 'files': files}
    (evidence / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(f'Manifest SHA-256: {digest(evidence / "manifest.json")}', flush=True)


if __name__ == '__main__':
    main()
