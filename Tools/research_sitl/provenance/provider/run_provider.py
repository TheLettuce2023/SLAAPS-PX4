#!/usr/bin/env python3
"""Source-bound closed Linux host clock observations; no PX4 or map qualification."""
import hashlib
import json
from pathlib import Path
import secrets
import shutil
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
FILES = ('CheckedProvider.hpp', 'CheckedProvider.cpp', 'ProviderHarness.cpp', 'CMakeLists.txt',
         'run_provider.py', 'check_provider.py')


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    area = Path(tempfile.mkdtemp(prefix='p3b-provider-', dir='/var/tmp'))
    print(area, flush=True)
    source = area / 'source'; source.mkdir()
    before = {name: sha(HERE / name) for name in FILES}
    for name in FILES: shutil.copy2(HERE / name, source / name)
    m = dict(schema='p3b-host-build/v1', status='incomplete', source_hashes=before, commands=[],
             run_id=str(secrets.randbits(63) + 1), map_qualified=False, positive_admission=False)

    def run(name, argv, success=True):
        result = subprocess.run(argv, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=120, text=True)
        log = area / (name + '.log'); log.write_text(result.stdout)
        m['commands'].append(dict(name=name, argv=argv, exit_code=result.returncode, log_sha256=sha(log)))
        if (result.returncode == 0) != success: raise RuntimeError(name + ' failed: ' + result.stdout[-2000:])

    try:
        run('compiler', ['c++', '--version'])
        run('default-configure', ['cmake', '-S', str(source), '-B', str(area / 'default')])
        run('default-build', ['cmake', '--build', str(area / 'default')])
        if (area / 'default/checked_provider').exists(): raise RuntimeError('enabled by default')
        probe = area / 'probe.cpp'; probe.write_text('#include "CheckedProvider.hpp"\n')
        for name, flags in (('unmarked', []), ('px4', ['-DRESEARCH_CLOSED_CLOCK_HOST=1', '-D__PX4_POSIX=1']),
                            ('lockstep', ['-DRESEARCH_CLOSED_CLOCK_HOST=1', '-DENABLE_LOCKSTEP_SCHEDULER=1'])):
            run('reject-' + name, ['c++', '-std=c++17', '-fsyntax-only', '-I', str(source), *flags, str(probe)], False)
        for name, sanitize in (('debug', 'OFF'), ('sanitized', 'ON')):
            build = area / name
            run(name + '-configure', ['cmake', '-S', str(source), '-B', str(build), '-DCMAKE_BUILD_TYPE=Debug',
                                     '-DRESEARCH_BUILD_CLOSED_CLOCK=ON', '-DRESEARCH_CLOCK_SANITIZE=' + sanitize])
            run(name + '-build', ['cmake', '--build', str(build), '-j', '2'])
            run(name + '-test', ['ctest', '--test-dir', str(build), '--output-on-failure'])
        run('observe', [str(area / 'debug/checked_provider'), '--observe', m['run_id']])
        retained = ['default/CMakeCache.txt', 'probe.cpp', 'observe.log']
        for name in ('debug', 'sanitized'):
            retained += [name + '/CMakeCache.txt', name + '/CTestTestfile.cmake', name + '/checked_provider',
                         name + '/CMakeFiles/checked_provider.dir/flags.make', name + '/CMakeFiles/checked_provider.dir/link.txt']
        m['artifacts'] = {name: sha(area / name) for name in retained}
        if before != {name: sha(HERE / name) for name in FILES}: raise RuntimeError('source drift')
        m['status'] = 'observed_unqualified'
    except BaseException as error:
        m['status'] = 'failed'; m['error'] = str(error)
        raise
    finally:
        (area / 'manifest.json').write_text(json.dumps(m, indent=2, sort_keys=True) + '\n')


if __name__ == '__main__': main()
