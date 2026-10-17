#!/usr/bin/env python3
"""Independently check closed Linux clock events and exact build bindings."""
import argparse
import hashlib
import json
from pathlib import Path
import re


def require(ok, reason):
    if not ok: raise ValueError(reason)


def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()


def pairs(items):
    out = {}
    for key, value in items:
        require(key not in out, 'duplicate key'); out[key] = value
    return out


def read(path):
    require(path.stat().st_size < 1024 * 1024, 'oversize JSON')
    return json.loads(path.read_text(), object_pairs_hook=pairs,
                      parse_constant=lambda x: (_ for _ in ()).throw(ValueError('nonfinite JSON')))


def trace(value, run_id):
    require(type(run_id) is str and re.fullmatch(r'[1-9][0-9]*', run_id) and int(run_id) < 1 << 64, 'run identity')
    require(set(value) == {'schema', 'map_qualified', 'positive_admission', 'state', 'fault', 'events'}, 'trace fields')
    require(value['schema'] == 'p3b-host-clock/v1' and value['map_qualified'] is False and
            value['positive_admission'] is False, 'invented qualification')
    require(type(value['state']) is int and value['state'] == 3 and type(value['fault']) is int and value['fault'] == 0, 'final disposition')
    rows = value['events']; require(type(rows) is list and 5 <= len(rows) <= 64, 'event count')
    active = False; epoch = 0; interval = None; closure = 0; last_raw = None; replacements = 0; expired = 0
    for index, row in enumerate(rows, 1):
        require(set(row) == {'sequence', 'kind', 'binding', 'interval', 'read', 'predecessor'}, 'event fields')
        require(type(row['sequence']) is int and row['sequence'] == index and type(row['kind']) is int, 'sequence/kind type')
        for key, size in (('binding', 8), ('interval', 2), ('read', 5), ('predecessor', 2)):
            require(type(row[key]) is list and len(row[key]) == size and all(type(x) is int for x in row[key]), 'value types')
        identity = row['binding']; bounds = row['interval']; code, error, seconds, nanoseconds, us = row['read']
        require(all(0 <= x < 1 << 64 for x in identity + bounds + row['predecessor']), 'integer overflow')
        require(code == 0 and error == 0 and 0 <= seconds < 1 << 63 and 0 <= nanoseconds < 10**9 and
                us == seconds * 1000000 + nanoseconds // 1000 and 0 < us < 1 << 64, 'successful read/conversion')
        raw = (seconds, nanoseconds)
        require(last_raw is None or raw >= last_raw, 'raw clock regression'); last_raw = raw
        require(1 < bounds[0] <= bounds[1] < (1 << 64) - 1 and bounds[1] - bounds[0] == 40000, 'experiment interval arithmetic')
        kind = row['kind']
        if kind == 0:
            require(index == 1 and epoch == 0 and row['predecessor'] == [0, 0], 'initial event')
            epoch = 1; interval = bounds; active = True
        elif kind == 4:
            require(not active and epoch == 1 and expired == 1 and row['predecessor'] == [epoch, closure], 'replacement relation')
            epoch = 2; interval = bounds; active = True; replacements += 1
        else:
            require(active and bounds == interval and row['predecessor'] == [0, 0], 'inactive observation/interval rewrite')
        require(identity == [int(run_id), 2, 3, 4, 5, 6, 7 if epoch == 1 else 8, epoch], 'candidate lifetime binding')
        if kind == 2:
            require(us > bounds[1] and epoch == 1, 'premature expiry'); active = False; closure = index; expired += 1
        else:
            require(kind in (0, 1, 3, 4) and bounds[0] <= us <= bounds[1], 'outside candidate interval')
            if kind == 3:
                require(epoch == 2 and index == len(rows), 'retirement order'); active = False; closure = index
    require(not active and epoch == 2 and replacements == 1 and expired == 1, 'missing lifecycle evidence')
    return dict(events=len(rows), epochs=2, expiry_observed=True, replacement_observed=True,
                scope='Linux host clock reads and candidate lifecycle only', map_qualified=False)


def check(area):
    area = Path(area).resolve(); m = read(area / 'manifest.json'); source = area / 'source'
    require(m['schema'] == 'p3b-host-build/v1' and m['status'] == 'observed_unqualified' and
            m['map_qualified'] is False and m['positive_admission'] is False, 'incomplete or promoted package')
    files = {'CheckedProvider.hpp', 'CheckedProvider.cpp', 'ProviderHarness.cpp', 'CMakeLists.txt', 'run_provider.py', 'check_provider.py'}
    require(set(m['source_hashes']) == files and {p.name for p in source.iterdir()} == files, 'source inventory')
    for name in files: require(sha(source / name) == m['source_hashes'][name], 'source drift')
    expected = [('compiler', ['c++', '--version'], True),
                ('default-configure', ['cmake', '-S', str(source), '-B', str(area / 'default')], True),
                ('default-build', ['cmake', '--build', str(area / 'default')], True)]
    for name, flags in (('unmarked', []), ('px4', ['-DRESEARCH_CLOSED_CLOCK_HOST=1', '-D__PX4_POSIX=1']),
                        ('lockstep', ['-DRESEARCH_CLOSED_CLOCK_HOST=1', '-DENABLE_LOCKSTEP_SCHEDULER=1'])):
        expected.append(('reject-' + name, ['c++', '-std=c++17', '-fsyntax-only', '-I', str(source), *flags, str(area / 'probe.cpp')], False))
    for name, sanitize in (('debug', 'OFF'), ('sanitized', 'ON')):
        build = area / name
        expected += [(name + '-configure', ['cmake', '-S', str(source), '-B', str(build), '-DCMAKE_BUILD_TYPE=Debug',
                                           '-DRESEARCH_BUILD_CLOSED_CLOCK=ON', '-DRESEARCH_CLOCK_SANITIZE=' + sanitize], True),
                     (name + '-build', ['cmake', '--build', str(build), '-j', '2'], True),
                     (name + '-test', ['ctest', '--test-dir', str(build), '--output-on-failure'], True)]
    expected.append(('observe', [str(area / 'debug/checked_provider'), '--observe', m['run_id']], True))
    require(len(m['commands']) == len(expected), 'command inventory')
    for recorded, (name, argv, success) in zip(m['commands'], expected):
        require(recorded['name'] == name and recorded['argv'] == argv and type(recorded['exit_code']) is int and
                (recorded['exit_code'] == 0) == success, 'command binding')
        require(sha(area / (name + '.log')) == recorded['log_sha256'], 'command log drift')
        log = (area / (name + '.log')).read_text()
        if name.startswith('reject-'):
            require('error: #error "Closed Linux host clock observation only"' in log, 'wrong profile rejection')
        if name.endswith('-test'):
            require(re.search(r'1/1 Test #1: checked_provider_contract\s+\.+\s+Passed', log) is not None and
                    '100% tests passed, 0 tests failed out of 1' in log and 'No tests were found' not in log, 'contract test not executed')
    artifacts = {'default/CMakeCache.txt', 'probe.cpp', 'observe.log'}
    for name in ('debug', 'sanitized'):
        artifacts |= {name + '/CMakeCache.txt', name + '/CTestTestfile.cmake', name + '/checked_provider',
                      name + '/CMakeFiles/checked_provider.dir/flags.make', name + '/CMakeFiles/checked_provider.dir/link.txt'}
    require(set(m['artifacts']) == artifacts, 'artifact inventory')
    for name in artifacts: require(sha(area / name) == m['artifacts'][name], 'artifact drift')
    require('RESEARCH_BUILD_CLOSED_CLOCK:BOOL=OFF' in (area / 'default/CMakeCache.txt').read_text() and
            not (area / 'default/checked_provider').exists(), 'default enabled')
    require('CMAKE_HOME_DIRECTORY:INTERNAL=' + str(source) + '\n' in (area / 'default/CMakeCache.txt').read_text(), 'default source binding')
    require((area / 'probe.cpp').read_text() == '#include "CheckedProvider.hpp"\n', 'rejection probe drift')
    for name in ('debug', 'sanitized'):
        cache = (area / name / 'CMakeCache.txt').read_text()
        flags = (area / name / 'CMakeFiles/checked_provider.dir/flags.make').read_text()
        link = (area / name / 'CMakeFiles/checked_provider.dir/link.txt').read_text()
        testfile = (area / name / 'CTestTestfile.cmake').read_text()
        require('CMAKE_HOME_DIRECTORY:INTERNAL=' + str(source) + '\n' in cache and
                'RESEARCH_CLOCK_SANITIZE:BOOL=' + ('ON' if name == 'sanitized' else 'OFF') + '\n' in cache, 'source/sanitizer configuration')
        require('add_test(checked_provider_contract "' + str(area / name / 'checked_provider') + '" "--self-test")' in testfile,
                'contract executable binding')
        require('RESEARCH_BUILD_CLOSED_CLOCK:BOOL=ON' in cache and 'CMAKE_BUILD_TYPE:STRING=Debug' in cache and
                '-DRESEARCH_CLOSED_CLOCK_HOST=1' in flags and '-fstack-usage' in flags, 'closed build configuration')
        require(not any(x in flags for x in ('-D__PX4_', '-DENABLE_LOCKSTEP', '-DCONFIG_MUORB', '-DRESEARCH_C1E_')), 'mixed host profile')
        if name == 'sanitized': require('-fsanitize=address,undefined' in flags and '-fsanitize=address,undefined' in link, 'sanitizer drift')
    result = trace(read(area / 'observe.log'), m['run_id'])
    return dict(verified=True, manifest_sha256=sha(area / 'manifest.json'), observation=result)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__); parser.add_argument('area', type=Path)
    print(json.dumps(check(parser.parse_args().area), indent=2))
