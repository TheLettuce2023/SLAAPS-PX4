#!/usr/bin/env python3
"""H10 timestamp-ordering fix verification; isolated native builds, no hardware."""
import json
import shutil
import sys
from pathlib import Path
import verify_bench_h9 as integration

OUT = Path('/var/tmp/research-bench-h10')
integration.OUT = integration.base.OUT = OUT
integration.SCENARIOS += ('selector_snapshot', 'future')
integration.EDITABLE_INPUTS = {
    'src/modules/research_bench_control/ResearchBenchControl.cpp',
    'src/modules/manual_control/ManualControl.cpp', 'src/modules/manual_control/ManualControl.hpp',
    'src/lib/research_actuator_command/CMakeLists.txt',
    'Tools/research_sitl/verify_bench_h10.py',
}


def stock_tests():
    source = OUT / 'px4-source'
    googletest = OUT / 'googletest'
    if not googletest.exists():
        integration.run(['git', 'clone', '--depth', '1', '--branch', 'v1.16.0',
            'https://github.com/google/googletest.git', str(googletest)], 'stock-gtest-fetch.log')
    integration.run(['git', '-C', str(googletest), 'rev-parse', 'HEAD'], 'stock-gtest-head.log')
    # Use the tree's existing GTest-only helper, without downloading fuzzing
    # dependencies for these non-fuzz tests. Never edit checkout test/CMakeLists.
    test_config = source / 'test/CMakeLists.txt'
    original = test_config.read_bytes()
    private_config = OUT / 'stock-test-CMakeLists.txt'
    private_config.write_text('px4_setup_gtest_without_fuzztest()\nadd_custom_target(test_results)\n')
    build = OUT / 'stock-build'
    try:
        shutil.copy2(private_config, test_config)
        integration.run(['cmake', '-S', str(source), '-B', str(build), '-G', 'Ninja',
            '-DCONFIG=px4_sitl_test', '-DCMAKE_BUILD_TYPE=Debug',
            '-DFETCHCONTENT_SOURCE_DIR_GOOGLETEST=' + str(googletest),
            '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON'], 'stock-configure.log')
        integration.run(['cmake', '--build', str(build), '--target', 'mavlink_c_generate', '-j', '4'], 'stock-mavlink.log')
        integration.run(['cmake', '--build', str(build), '--target', 'functional-ManualControl',
            'unit-ManualControlSelector', 'unit-MovingDiff', '-j', '4'], 'stock-build.log')
        integration.run(['ctest', '--test-dir', str(build), '-V', '--output-on-failure',
            '-R', '^(functional-ManualControl|unit-ManualControlSelector|unit-MovingDiff)$'], 'stock-tests.log')
    finally:
        test_config.write_bytes(original)
    print('Stock manual-control functional/selector/moving-difference tests PASS')


def main():
    if sys.argv[1] == 'stock':
        stock_tests()
        return
    if sys.argv[1] == 'manifest':
        shutil.copy2(Path(__file__), OUT / Path(__file__).name)
        integration.EXTRA_EVIDENCE = [OUT / Path(__file__).name, OUT / 'stock-test-CMakeLists.txt',
            OUT / 'stock-build/CMakeCache.txt', OUT / 'stock-build/compile_commands.json']
        integration.EXTRA_EVIDENCE += [path for path in (OUT / 'stock-build').rglob('*')
            if path.is_file() and path.name in ('functional-ManualControl', 'unit-ManualControlSelector', 'unit-MovingDiff')]
    if sys.argv[1] == 'host':
        integration.run(['astyle', '--options=' + str(integration.ROOT / 'Tools/astyle/astylerc'), '--suffix=none',
            *[str(integration.ROOT / name) for name in (
                'src/modules/manual_control/ManualControl.cpp', 'src/modules/manual_control/ManualControl.hpp',
                'src/lib/research_actuator_command/ManualSnapshot.hpp',
                'src/lib/research_actuator_command/ManualSnapshotTest.cpp')]], 'snapshot-astyle.log')
    integration.main()
    if sys.argv[1] == 'snapshot':
        path = OUT / 'checkpoint.json'
        records = json.loads(path.read_text())
        for name in ('src/modules/manual_control/ManualControl.cpp', 'src/modules/manual_control/ManualControl.hpp'):
            records[name] = integration.digest(integration.ROOT / name)
        path.write_text(json.dumps(records, indent=2) + '\n')


if __name__ == '__main__':
    main()
