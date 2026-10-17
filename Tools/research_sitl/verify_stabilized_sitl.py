#!/usr/bin/env python3
"""Isolated native PX4/Gazebo fixture; never flash, arm or use physical I/O."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import threading
import time

ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / 'Tools/research_sitl/stabilized_manual'


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run(args, out, name, **kwargs):
    path = out / name
    if path.exists():
        index = 1
        while path.with_name(path.name + '.' + str(index)).exists():
            index += 1
        shutil.copy2(path, path.with_name(path.name + '.' + str(index)))
    with path.open('w') as log:
        subprocess.run(args, stdout=log, stderr=subprocess.STDOUT, check=True, **kwargs)


def prepare(out):
    graph = json.loads((ROOT / 'docs/RESEARCH_CODE_GRAPH.json').read_text())
    checkpoint = {}
    for node in graph['nodes']:
        if 'sha256' in node and 'path' in node:
            path = ROOT / node['path']
            if digest(path) != node['sha256']:
                raise RuntimeError('Graph anchor changed: ' + node['path'])
            checkpoint[node['path']] = node['sha256']
    (out / 'checkpoint.json').write_text(json.dumps(checkpoint, indent=2) + '\n')
    source = out / 'px4-source'
    run(['cp', '-a', '/var/tmp/research-bench-h10/px4-source', str(source)], out, 'source-copy.log')
    for relative in ('src/lib/research_actuator_command', 'src/lib/research_stabilized_manual',
                     'src/modules/manual_control', 'src/lib/mixer_module',
                     'Tools/research_sitl/stabilized_manual'):
        shutil.copytree(ROOT / relative, source / relative, dirs_exist_ok=True)
    for relative in ('src/lib/CMakeLists.txt', 'msg/versioned/ManualControlSetpoint.msg',
                     'msg/CMakeLists.txt'):
        shutil.copy2(ROOT / relative, source / relative)
    # Replace only private board config. Remove inherited bench-active registration.
    board = ROOT / 'boards/px4/sitl/default.px4board'
    (source / board.relative_to(ROOT)).write_text(board.read_text())
    shutil.copy2(Path(__file__), out / Path(__file__).name)
    (out / 'fixture-inputs.json').write_text(json.dumps({
        str(p.relative_to(ROOT)): digest(p) for p in FIXTURE.rglob('*') if p.is_file()
    }, indent=2) + '\n')
    print('Private native source prepared:', out, flush=True)


def build(out):
    source = out / 'px4-source'
    plugin = out / 'plugin-build'
    native = out / 'sitl-lockstep-build'
    run(['cmake', '-S', str(source / FIXTURE.relative_to(ROOT)), '-B', str(plugin), '-G', 'Ninja',
         '-DCMAKE_BUILD_TYPE=RelWithDebInfo'], out, 'plugin-configure.log')
    run(['cmake', '--build', str(plugin), '-j', '4'], out, 'plugin-build.log')
    print('Strict Gazebo plugin build PASS', flush=True)
    run(['cmake', '-S', str(source), '-B', str(native), '-G', 'Ninja', '-DCONFIG=px4_sitl_default',
         '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON',
         '-DEXTERNAL_MODULES_LOCATION=' + str(source / FIXTURE.relative_to(ROOT) / 'px4_external')],
        out, 'sitl-configure.log')
    run(['cmake', '--build', str(native), '--target', 'mavlink_c_generate', '-j', '4'], out, 'sitl-mavlink.log')
    run(['cmake', '--build', str(native), '-j', '4'], out, 'sitl-build.log')
    symbols = subprocess.check_output(['nm', '-C', str(native / 'bin/px4')]).decode()
    for symbol in ('research_stabilized_sitl_main', 'StabilizedManualControl::cycle', 'allocateCanards'):
        if symbol not in symbols:
            raise RuntimeError('Missing linked symbol: ' + symbol)
    if 'bench_active_test_main' in symbols or 'research_bench_control_main' in symbols:
        raise RuntimeError('Private stabilized profile inherited active bench module')
    (out / 'linked-symbols.log').write_text('\n'.join(
        line for line in symbols.splitlines() if 'stabilized' in line or 'Stabilized' in line) + '\n')
    run(['size', str(native / 'bin/px4')], out, 'linked-size.log')
    print('Fresh native PX4 linkage PASS', flush=True)


def smoke(out):
    """Sensor/estimator readiness and centered enable; no stabilization acceptance."""
    import csv
    from pymavlink import mavutil
    work = Path(tempfile.mkdtemp(prefix='smoke-', dir=out))
    env = os.environ.copy()
    env.update(GZ_PARTITION=work.name, GZ_IP='127.0.0.1',
               GZ_SIM_SYSTEM_PLUGIN_PATH=str(out / 'plugin-build'))
    world = work / 'world.sdf'
    world.write_text((FIXTURE / 'world.sdf.in').read_text().replace('@PITCH@', '0')
                     .replace('@TRUTH_CSV@', str(work / 'truth.csv'))
                     .replace('@MAGNETIC_DELAY_US@', '0')
                     .replace('@IMU_DELAY_US@', '0')
                     .replace('@IMU_TOPIC@', '/world/stabilized_fixture/model/slaaps_fixture/link/base_link/sensor/imu_sensor/imu'))
    script = work / 'rc.fixture'
    script.write_text('''#!/bin/sh
set -e
. px4-alias.sh
param select fs/parameters.bson
param set MAV_SYS_ID 1
param set COM_RC_IN_MODE 1
param set COM_RC_LOSS_T 0.1
param set EKF2_MULTI_IMU 0
param set EKF2_MULTI_MAG 0
param set SENS_IMU_MODE 1
param set SENS_MAG_MODE 1
param set CAL_ACC0_ID 1310988
param set CAL_GYRO0_ID 1310988
param set CAL_MAG0_ID 197388
param set SENS_BOARD_X_OFF 0.000001
param set SENS_DPRES_OFF 0.001
param set SYS_HAS_MAG 1
param set SYS_HAS_BARO 1
param set EKF2_REQ_GPS_H 0.5
param set IMU_GYRO_FFT_EN 0
dataman start -r
gz_bridge start -w stabilized_fixture -n slaaps_fixture
manual_control start
sensors start
commander start
ekf2 start
mavlink start -u 19779 -o 19780 -t 127.0.0.1 -m custom
research_stabilized_sitl start slaaps_fixture ''' + str(work / 'control.csv') + '''
mavlink boot_complete
echo SM3_STARTUP_COMPLETE
''')
    native = out / 'sitl-lockstep-build/bin'
    instance = 29471
    def client(module, *args):
        return subprocess.check_output([str(native / ('px4-' + module)), '--instance', str(instance), *args],
                                       env=env, cwd=work, timeout=10).decode()
    gz_log = (work / 'gazebo.log').open('w')
    px4_log = (work / 'px4.log').open('w')
    gz = subprocess.Popen(['gz', 'sim', '-s', '-r', str(world)], stdout=gz_log, stderr=subprocess.STDOUT, env=env)
    px4 = None
    connection = mavutil.mavlink_connection('udpout:127.0.0.1:19779', source_system=255, source_component=190)
    stop_sender = threading.Event()
    def send_manual():
        while not stop_sender.is_set():
            connection.mav.manual_control_send(1, 0, 0, 500, 0, 0)
            stop_sender.wait(0.02)
    sender = threading.Thread(target=send_manual)
    sender.start()
    try:
        time.sleep(2)
        px4 = subprocess.Popen([str(native / 'px4'), '-d', '-i', str(instance), '-w', str(work), '-s', str(script)],
                               stdout=px4_log, stderr=subprocess.STDOUT, env=env, cwd=work)
        deadline = time.monotonic() + 55
        ready = False
        status = ''
        while time.monotonic() < deadline:
            if 'SM3_STARTUP_COMPLETE' in (work / 'px4.log').read_text():
                status = client('research_stabilized_sitl', 'status')
                if 'ready=1' in status:
                    ready = True
                    break
            if px4.poll() is not None or gz.poll() is not None:
                raise RuntimeError('Fixture process exited before readiness')
            time.sleep(0.05)
        (work / 'readiness.log').write_text(status)
        if not ready:
            for topic in ('vehicle_attitude', 'vehicle_angular_velocity', 'sensor_selection',
                          'estimator_status', 'estimator_status_flags', 'actuator_armed', 'manual_control_setpoint'):
                (work / (topic + '.log')).write_text(client('listener', topic, '-n', '1'))
            raise RuntimeError('Estimated-state fixture not ready; see ' + str(work))
        print('Actual Gazebo sensors / PX4 EKF / selected MAVLink input ready', flush=True)
        (work / 'enable.log').write_text(client('research_stabilized_sitl', 'enable'))
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            time.sleep(0.04)
        active_status = client('research_stabilized_sitl', 'status')
        (work / 'active-status.log').write_text(active_status)
        if 'state=3' not in active_status or 'fault=0' not in active_status:
            raise RuntimeError('Centered hold did not remain eligible: ' + str(work))
        (work / 'disable.log').write_text(client('research_stabilized_sitl', 'disable'))
        time.sleep(0.3)
    finally:
        stop_sender.set()
        sender.join(timeout=2)
        connection.close()
        if px4 is not None and px4.poll() is None:
            try:
                client('research_stabilized_sitl', 'stop')
                client('shutdown')
                px4.wait(timeout=10)
            except (subprocess.SubprocessError, OSError):
                px4.terminate()
                px4.wait(timeout=10)
        gz.terminate()
        gz.wait(timeout=10)
        px4_log.close()
        gz_log.close()
    control = list(csv.DictReader((work / 'control.csv').open()))
    truth = list(csv.DictReader((work / 'truth.csv').open()))
    if not control or not truth or any(row['state'] != '3' or row['thrust'] != '10' for row in control):
        raise RuntimeError('Incomplete centered-hold evidence: ' + str(work))
    if truth[-1]['thrust'] != '0' or truth[-1]['lease_fault'] != '1':
        raise RuntimeError('Disable did not reach independent simulation consumer: ' + str(work))
    record = dict(passed=True, scope='centered_estimated_state_hold_smoke_only',
                  control_cycles=len(control), first_control_us=int(control[0]['time']),
                  last_control_us=int(control[-1]['time']),
                  fixed_simulated_thrust_n=10, disable_consumer_thrust_n=0)
    (work / 'result.json').write_text(json.dumps(record, indent=2) + '\n')
    print('Fixture smoke PASS:', work, flush=True)


def physics(out):
    """Confirm the physical ball constraint permits rotation, without PX4."""
    import csv
    work = Path(tempfile.mkdtemp(prefix='physics-', dir=out))
    env = dict(os.environ, GZ_PARTITION=work.name, GZ_IP='127.0.0.1',
               GZ_SIM_SYSTEM_PLUGIN_PATH=str(out / 'plugin-build'))
    world = work / 'world.sdf'
    world.write_text((FIXTURE / 'world.sdf.in').read_text().replace('@PITCH@', '0')
                     .replace('@TRUTH_CSV@', str(work / 'truth.csv'))
                     .replace('@MAGNETIC_DELAY_US@', '0')
                     .replace('@IMU_DELAY_US@', '0')
                     .replace('@IMU_TOPIC@', '/world/stabilized_fixture/model/slaaps_fixture/link/base_link/sensor/imu_sensor/imu'))
    with (work / 'gazebo.log').open('w') as log:
        gz = subprocess.Popen(['gz', 'sim', '-s', '-r', str(world)], stdout=log, stderr=subprocess.STDOUT, env=env)
        try:
            time.sleep(2)
            for axis in range(3):
                values = [0.0, 0.0, 0.0]
                values[axis] = (0.024, 0.064, 0.064)[axis]
                run(['gz', 'topic', '-t', '/model/slaaps_fixture/impulse', '-m', 'gz.msgs.Double_V',
                     '-p', ' '.join('data: ' + str(value) for value in values)], out,
                    str(work.relative_to(out) / f'impulse-{axis}.log'), env=env, timeout=10)
                time.sleep(1)
        finally:
            gz.terminate()
            gz.wait(timeout=10)
    rows = list(csv.DictReader((work / 'truth.csv').open()))
    translation_error = max(abs(float(row[key]) - expected) for row in rows
                            for key, expected in (('x', 0), ('y', 0), ('z', 1)))
    peaks = [max(abs(float(row[key])) for row in rows) for key in ('wx', 'wy', 'wz')]
    passed = translation_error < 1e-5 and min(peaks) > 0.5
    (work / 'result.json').write_text(json.dumps(dict(
        passed=passed, translation_error_m=translation_error, peak_body_rates_rad_s=peaks), indent=2) + '\n')
    if not passed:
        raise RuntimeError('Fixture rotational degrees of freedom check failed: ' + str(work))
    print('Actual Gazebo three-axis rotation / translation restraint PASS:', work, flush=True)


def manifest(out):
    checkpoint = json.loads((out / 'checkpoint.json').read_text())
    intentional = {'src/lib/research_stabilized_manual/CanardAllocator.cpp',
                   'src/lib/research_stabilized_manual/StabilizedManualControl.cpp',
                   'src/lib/research_stabilized_manual/CMakeLists.txt'}
    preserved = {}
    for name, expected in checkpoint.items():
        if name in intentional:
            continue
        if digest(ROOT / name) != expected:
            raise RuntimeError('Unrelated mapped input changed: ' + name)
        preserved[name] = expected
    source = out / 'px4-source'
    inputs = {}
    for folder in (FIXTURE, ROOT / 'src/lib/research_stabilized_manual'):
        for path in folder.rglob('*'):
            if not path.is_file():
                continue
            relative = path.relative_to(ROOT)
            if digest(path) != digest(source / relative):
                raise RuntimeError('Native source mismatch: ' + str(relative))
            inputs[str(relative)] = digest(path)
    # Bind normal estimated-state/selected-input/clock adapters and reused PID/lease.
    for name in ('src/lib/rate_control/rate_control.cpp', 'src/lib/rate_control/rate_control.hpp',
                 'src/lib/research_actuator_command/BenchOutputLease.cpp',
                 'src/lib/research_actuator_command/Px4ManualControlInput.cpp',
                 'src/lib/research_actuator_command/ManualControlInput.cpp',
                 'src/modules/manual_control/ManualControl.cpp',
                 'src/modules/simulation/gz_bridge/GZBridge.cpp',
                 'src/modules/ekf2/EKF2.cpp', 'src/modules/sensors/sensors.cpp',
                 'src/modules/sensors/data_validator/DataValidator.cpp',
                 'platforms/posix/src/px4/common/drv_hrt.cpp', 'boards/px4/sitl/default.px4board'):
        if digest(ROOT / name) != digest(source / name):
            raise RuntimeError('Native dependency mismatch: ' + name)
        inputs[name] = digest(ROOT / name)
    for name in inputs:
        destination = out / 'final-inputs' / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(ROOT / name, destination)
    shutil.copy2(Path(__file__), out / Path(__file__).name)
    (out / 'final-inputs.json').write_text(json.dumps(inputs, indent=2) + '\n')
    (out / 'preservation.json').write_text(json.dumps(dict(
        preserved=preserved, intentional_control_changes=sorted(intentional)), indent=2) + '\n')
    files = {}
    for folder in ('final-inputs',):
        for path in (out / folder).rglob('*'):
            if path.is_file():
                files[str(path.relative_to(out))] = digest(path)
    for path in out.iterdir():
        if path.is_file() and path.name != 'manifest.json':
            files[path.name] = digest(path)
        elif path.is_dir() and path.name.startswith(('smoke-', 'physics-')):
            for evidence in path.rglob('*'):
                if evidence.is_file():
                    files[str(evidence.relative_to(out))] = digest(evidence)
    for name in ('sitl-lockstep-build/bin/px4', 'sitl-lockstep-build/compile_commands.json',
                 'sitl-lockstep-build/boardconfig', 'plugin-build/libresearch_rotational_fixture.so'):
        files[name] = digest(out / name)
    record = dict(scope='isolated_estimated_state_fixture_smoke_only',
                  stabilized_campaign_acceptance=False, physical_authority_granted=False,
                  preserved_anchor_count=len(preserved), files=files)
    (out / 'manifest.json').write_text(json.dumps(record, indent=2) + '\n')
    print('Evidence manifest SHA-256:', digest(out / 'manifest.json'), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['prepare', 'build', 'smoke', 'physics', 'manifest'])
    parser.add_argument('--evidence', type=Path)
    args = parser.parse_args()
    out = args.evidence or Path(tempfile.mkdtemp(prefix='research-stabilized-sitl-', dir='/var/tmp'))
    out = out.resolve()
    if out.parent != Path('/var/tmp') or not out.name.startswith('research-stabilized-sitl-'):
        raise RuntimeError('Evidence must be a private /var/tmp/research-stabilized-sitl-* directory')
    out.mkdir(exist_ok=True)
    if args.action == 'prepare':
        prepare(out)
    elif args.action == 'build':
        build(out)
    elif args.action == 'smoke':
        smoke(out)
    elif args.action == 'physics':
        physics(out)
    else:
        manifest(out)


if __name__ == '__main__':
    main()
