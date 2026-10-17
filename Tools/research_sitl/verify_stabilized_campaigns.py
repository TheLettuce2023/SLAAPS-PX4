#!/usr/bin/env python3
"""Actual estimated-state campaigns in the synthetic fixture; no hardware I/O."""
import argparse
import bisect
import csv
import hashlib
import json
import math
import os
from pathlib import Path
import signal
import shutil
import subprocess
import tempfile
import threading
import time

import verify_stabilized_sitl as base

ROOT = base.ROOT
SEED = 42
RATE_TARGET = 0.375  # (0.4 - 0.04) / (1 - 0.04) * 1 rad/s.
RATE_TOLERANCE = 0.08
FINAL_ANGLE_LIMIT = 0.02
FINAL_RATE_LIMIT = 0.02
PEAK_ANGLE_LIMIT = 0.5
# Known zero-bias synthetic fixture only: a finite calibrated gyro-bias prior
# at the raw-noise scale. The ten-noise-sigma candidate still drifted in the
# full-axis nose-up trace. Neither prior is an installed sensor calibration.
GYRO_BIAS_PRIOR = 0.0003394
ACCEL_BIAS_PRIOR = 10 * 0.004


def rows(path):
    with path.open() as stream:
        return [row for row in csv.DictReader(stream) if None not in row and all(v is not None for v in row.values())]


def quaternion(row, prefix='q'):
    values = [float(row[prefix + key]) for key in ('w', 'x', 'y', 'z')]
    norm = math.sqrt(sum(value * value for value in values))
    return [value / norm for value in values]


def separation(left, right):
    # Quaternion geodesic angle, stable near zero and invariant to q/-q.
    dot = sum(a * b for a, b in zip(left, right))
    cross = [left[2] * right[3] - left[3] * right[2],
             left[3] * right[1] - left[1] * right[3],
             left[1] * right[2] - left[2] * right[1]]
    imaginary = [left[0] * right[i] - right[0] * left[i] - cross[i - 1] for i in (1, 2, 3)]
    return 2 * math.atan2(math.sqrt(sum(v * v for v in imaginary)), abs(dot))


def magnetic_rigidity(work, truth):
    """Score the measured sensor field in the true world frame; no feedback use."""
    samples = rows(work / 'truth.csv.mag.csv')
    times = [int(row['time']) for row in truth]
    directions = []
    for sample in samples:
        stamp = int(sample['time'])
        if not times[0] <= stamp <= times[-1]:
            continue
        index = bisect.bisect_left(times, stamp)
        index = min((max(0, index - 1), min(index, len(times) - 1)), key=lambda i: abs(times[i] - stamp))
        q = quaternion(truth[index])
        # Actual legacy bridge output, decoded from its compatibility packet.
        field = [-float(sample['packet_y']), -float(sample['packet_x']), float(sample['packet_z'])]
        x, y, z = field
        w, a, b, c = q
        world = [(1-2*(b*b+c*c))*x + 2*(a*b-w*c)*y + 2*(a*c+w*b)*z,
                 2*(a*b+w*c)*x + (1-2*(a*a+c*c))*y + 2*(b*c-w*a)*z,
                 2*(a*c-w*b)*x + 2*(b*c+w*a)*y + (1-2*(a*a+b*b))*z]
        norm = math.sqrt(sum(v*v for v in world))
        directions.append([v / norm for v in world])
    if not directions:
        raise RuntimeError('No measured magnetic sensor samples')
    reference = directions[0]
    angles = [math.acos(max(-1, min(1, sum(a*b for a, b in zip(reference, direction)))))
              for direction in directions]
    return max(angles), len(directions)


def inspect_campaign(work):
    control, truth = rows(work / 'control.csv'), rows(work / 'truth.csv')
    rate_end = max(int(row['time']) for row in control if row['state'] == '2')
    release = next(row for row in control if row['state'] == '3' and int(row['time']) > rate_end)
    def multiply(left, right):
        return [left[0]*right[0] - sum(left[i]*right[i] for i in (1,2,3)),
                left[0]*right[1]+left[1]*right[0]+left[2]*right[3]-left[3]*right[2],
                left[0]*right[2]-left[1]*right[3]+left[2]*right[0]+left[3]*right[1],
                left[0]*right[3]+left[1]*right[2]-left[2]*right[1]+left[3]*right[0]]
    def inverse(q):
        return [q[0], -q[1], -q[2], -q[3]]
    reference_truth = quaternion(min(truth, key=lambda row: abs(int(row['time'])-int(release['att_sample']))))
    initial_offset = multiply(quaternion(release), inverse(reference_truth))
    diagnostics=[]
    for elapsed in (0, 1000000, 4000000, 7000000):
        sample = min(control, key=lambda row: abs(int(row['time']) - int(release['time']) - elapsed))
        actual = quaternion(min(truth, key=lambda row: abs(int(row['time'])-int(sample['att_sample']))))
        physical_error = multiply(inverse(reference_truth), actual)
        diagnostics.append(dict(elapsed_us=int(sample['time'])-int(release['time']),
                                physical_rotation_imag=physical_error[1:],
                                estimated_hold_error_rad=separation(quaternion(sample), quaternion(release, 'r')),
                                estimate_world_offset_change_rad=separation(
                                    multiply(quaternion(sample), inverse(actual)), initial_offset)))
    print(json.dumps(diagnostics, indent=2), flush=True)


class Fixture:
    def __init__(self, evidence, name, nose_up=False, seed=SEED, manual_period_s=0.02,
                 magnetic_delay_us=0, imu_delay_us=0):
        from pymavlink import mavutil
        assert math.isfinite(manual_period_s) and 0.005 <= manual_period_s <= 0.2
        assert isinstance(magnetic_delay_us, int) and 0 <= magnetic_delay_us <= 100000
        assert isinstance(imu_delay_us, int) and 0 <= imu_delay_us <= 100000
        self.evidence = evidence
        self.work = Path(tempfile.mkdtemp(prefix=name + '-', dir=evidence))
        self.native = evidence / 'sitl-lockstep-build/bin'
        self.env = dict(os.environ, GZ_PARTITION=self.work.name, GZ_IP='127.0.0.1',
                        GZ_SIM_SYSTEM_PLUGIN_PATH=str(evidence / 'plugin-build'))
        self.instance = 29472
        self.command = (0, 0, 0)  # MAVLink pitch/x, roll/y, yaw/r.
        self.sending = True
        self.stopped = threading.Event()
        self.connection = mavutil.mavlink_connection('udpout:127.0.0.1:19779', source_system=255, source_component=190)
        self.sender = threading.Thread(target=self.send, daemon=True)
        self.actions = (self.work / 'actions.csv').open('w', buffering=1)
        self.actions.write('sim_time,action,details\n')
        self.gz_log = (self.work / 'gazebo.log').open('w')
        self.px4_log = (self.work / 'px4.log').open('w')
        self.px4 = None
        self.gz = None
        self.paused = False
        self.seed = seed
        self.manual_period_s = manual_period_s
        self.result = dict(passed=False, seed=seed, nose_up=nose_up,
                           manual_send_period_s=manual_period_s,
                           magnetic_transport_delay_us=magnetic_delay_us,
                           imu_transport_delay_us=imu_delay_us,
                           gyro_bias_prior_rad_s=GYRO_BIAS_PRIOR,
                           accel_bias_prior_m_s2=ACCEL_BIAS_PRIOR)
        inputs = {}
        for path in sorted(base.FIXTURE.rglob('*')):
            if not path.is_file():
                continue
            name = str(path.relative_to(ROOT))
            native_path = evidence / 'px4-source' / name
            if base.digest(path) != base.digest(native_path):
                raise RuntimeError('Native test source mismatch: ' + name)
            inputs[name] = base.digest(path)
            destination = self.work / 'source-inputs' / name
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, destination)
        for path in (Path(__file__).resolve(), Path(base.__file__).resolve()):
            name = str(path.relative_to(ROOT))
            inputs[name] = base.digest(path)
            destination = self.work / 'source-inputs' / name
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, destination)
        self.result['source_sha256'] = inputs
        artifacts = evidence / 'tested-artifacts'
        artifacts.mkdir(exist_ok=True)
        for key, path in (('px4', self.native / 'px4'),
                          ('plugin', evidence / 'plugin-build/libresearch_rotational_fixture.so')):
            identity = base.digest(path)
            destination = artifacts / (key + '-' + identity)
            if not destination.exists():
                shutil.copy2(path, destination)
            self.result[key + '_sha256'] = identity
        world = self.work / 'world.sdf'
        world.write_text((base.FIXTURE / 'world.sdf.in').read_text()
                         .replace('@PITCH@', str(-math.pi / 2 if nose_up else 0))
                         .replace('@TRUTH_CSV@', str(self.work / 'truth.csv'))
                         .replace('@MAGNETIC_DELAY_US@', str(magnetic_delay_us))
                         .replace('@IMU_DELAY_US@', str(imu_delay_us))
                         .replace('@IMU_TOPIC@', '/model/slaaps_fixture/imu_raw' if imu_delay_us else
                                  '/world/stabilized_fixture/model/slaaps_fixture/link/base_link/sensor/imu_sensor/imu'))
        self.world = world
        self.script = self.work / 'rc.fixture'
        self.script.write_text(('''#!/bin/sh
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
research_stabilized_sitl start slaaps_fixture ''' + str(self.work / 'control.csv') + '''
mavlink boot_complete
echo CAMPAIGN_STARTUP_COMPLETE
''').replace('param set IMU_GYRO_FFT_EN 0',
            'param set IMU_GYRO_FFT_EN 0\nparam set EKF2_MAG_TYPE 0\n'
            + f'param set EKF2_GBIAS_INIT {GYRO_BIAS_PRIOR:.9g}\n'
            + f'param set EKF2_ABIAS_INIT {ACCEL_BIAS_PRIOR:.9g}'))

    def send(self):
        try:
            while not self.stopped.is_set():
                if self.sending:
                    pitch, roll, yaw = self.command
                    self.connection.mav.manual_control_send(1, pitch, roll, 500, yaw, 0)
                self.stopped.wait(self.manual_period_s)
        except Exception as error:
            (self.work / 'sender-error.txt').write_text(repr(error))

    def client(self, module, *args, check=True):
        result = subprocess.run([str(self.native / ('px4-' + module)), '--instance', str(self.instance), *args],
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                env=self.env, cwd=self.work, timeout=10)
        if check and result.returncode:
            raise RuntimeError('Native client failed: ' + result.stdout.decode())
        return result.stdout.decode(), result.returncode

    def action(self, name, details=''):
        self.actions.write(f'{self.now()},{name},{details}\n')

    def now(self):
        path = self.work / 'truth.csv'
        if not path.exists():
            return 0
        with path.open('rb') as stream:
            commas = stream.readline().decode().count(',')
            stream.seek(max(0, path.stat().st_size - 4096))
            lines = stream.read().decode().splitlines()
        complete = [line for line in lines if line.count(',') == commas and line.split(',')[0].isdigit()]
        return int(complete[-1].split(',')[0]) if complete else 0

    def wait(self, duration_us):
        target = self.now() + duration_us
        deadline = time.monotonic() + max(20, duration_us / 1e6 * 3)
        while self.now() < target:
            if time.monotonic() >= deadline:
                raise RuntimeError('Simulation observation did not advance; processes are retained until cleanup')
            if self.gz.poll() is not None or self.px4.poll() is not None:
                raise RuntimeError('Fixture process exited during campaign')
            time.sleep(0.02)

    def start(self):
        self.gz = subprocess.Popen(['gz', 'sim', '-s', '-r', '--seed', str(self.seed), str(self.world)],
                                   stdout=self.gz_log, stderr=subprocess.STDOUT, env=self.env)
        self.sender.start()
        time.sleep(2)
        self.px4 = subprocess.Popen([str(self.native / 'px4'), '-d', '-i', str(self.instance),
                                    '-w', str(self.work), '-s', str(self.script)],
                                   stdout=self.px4_log, stderr=subprocess.STDOUT, env=self.env, cwd=self.work)
        deadline = time.monotonic() + 65
        while time.monotonic() < deadline:
            if 'CAMPAIGN_STARTUP_COMPLETE' in (self.work / 'px4.log').read_text():
                status, _ = self.client('research_stabilized_sitl', 'status')
                if 'ready=1' in status:
                    # Read the actual PX4 parameter values before enable. A
                    # source hash or result metadata alone cannot prove that
                    # a startup-script transformation took effect.
                    expected = {'EKF2_GBIAS_INIT': self.result['gyro_bias_prior_rad_s'],
                                'EKF2_ABIAS_INIT': self.result['accel_bias_prior_m_s2'],
                                'EKF2_MAG_TYPE': 0}
                    comparisons = {}
                    for name, value in expected.items():
                        output, code = self.client('param', 'compare', name, f'{value:.9g}', check=False)
                        comparisons[name] = dict(expected=value, returncode=code, output=output)
                    (self.work / 'runtime-profile-check.json').write_text(json.dumps(comparisons, indent=2) + '\n')
                    assert all(item['returncode'] == 0 for item in comparisons.values()), 'Actual PX4 profile mismatch'
                    (self.work / 'readiness.log').write_text(status)
                    self.action('enable_request')
                    self.client('research_stabilized_sitl', 'enable')
                    self.wait(1000000)
                    status, _ = self.client('research_stabilized_sitl', 'status')
                    if 'state=3' not in status or 'fault=0' not in status:
                        raise RuntimeError('Owner failed initial centered hold: ' + status)
                    return
            if self.gz.poll() is not None or self.px4.poll() is not None:
                raise RuntimeError('Fixture exited during readiness')
            time.sleep(0.05)
        for topic in ('vehicle_attitude', 'vehicle_angular_velocity', 'estimator_status_flags', 'actuator_armed'):
            output, _ = self.client('listener', topic, '-n', '1', check=False)
            (self.work / (topic + '.log')).write_text(output)
        raise RuntimeError('Estimated-state fixture readiness timeout')

    def close(self):
        self.stopped.set()
        if self.sender.is_alive():
            self.sender.join(timeout=2)
        self.connection.close()
        if self.px4 is not None and self.px4.poll() is None:
            if self.paused:
                self.px4.send_signal(signal.SIGCONT)
                self.paused = False
            try:
                self.client('research_stabilized_sitl', 'stop', check=False)
                self.client('shutdown', check=False)
                self.px4.wait(timeout=10)
            except (subprocess.SubprocessError, OSError):
                self.px4.terminate()
                self.px4.wait(timeout=10)
        if self.gz is not None and self.gz.poll() is None:
            self.gz.terminate()
            self.gz.wait(timeout=10)
        self.actions.close()
        self.gz_log.close()
        self.px4_log.close()
        (self.work / 'result.json').write_text(json.dumps(self.result, indent=2) + '\n')


def campaign(evidence, axis, nose_up, direction):
    fixture = Fixture(evidence, f'rate-{"up" if nose_up else "level"}-{axis}-{direction:+d}', nose_up)
    try:
        fixture.start()
        raw = [0, 0, 0]
        raw[(1, 0, 2)[axis]] = (400 if axis != 1 else -400) * direction
        fixture.command = tuple(raw)
        fixture.action('pilot_rate', f'axis={axis};direction={direction}')
        fixture.wait(2000000)
        fixture.command = (0, 0, 0)
        fixture.action('release')
        fixture.wait(1000000)
        impulses = [0.0, 0.0, 0.0]
        impulses[axis] = (0.024, 0.064, 0.064)[axis] * direction
        fixture.action('impulse_request', f'axis={axis};Nms={impulses[axis]}')
        base.run(['gz', 'topic', '-t', '/model/slaaps_fixture/impulse', '-m', 'gz.msgs.Double_V',
                  '-p', ' '.join('data: ' + str(v) for v in impulses)], fixture.work, 'impulse.log',
                 env=fixture.env, timeout=10)
        fixture.wait(7000000)
        status, _ = fixture.client('research_stabilized_sitl', 'status')
        (fixture.work / 'active-status.log').write_text(status)
        if 'state=3' not in status or 'fault=0' not in status:
            raise RuntimeError('Owner revoked during positive campaign: ' + status)
        fixture.client('research_stabilized_sitl', 'disable')
        fixture.wait(300000)
    except Exception as error:
        fixture.result['error'] = str(error)
        raise
    finally:
        fixture.close()
    control, truth = rows(fixture.work / 'control.csv'), rows(fixture.work / 'truth.csv')
    rate_rows = [row for row in control if row['state'] == '2']
    assert rate_rows, 'Pilot command never entered Rate'
    rate_end = int(rate_rows[-1]['time'])
    tracking = [float(row['w' + 'xyz'[axis]]) for row in rate_rows if int(row['time']) >= rate_end - 300000]
    expected = RATE_TARGET * direction
    rate_error = max(abs(rate - expected) for rate in tracking)
    release = next(row for row in control if row['state'] == '3' and int(row['time']) > rate_end)
    release_time = int(release['att_sample'])
    reference = quaternion(release, 'r')
    capture_error = separation(quaternion(release), reference)
    held = [row for row in control if int(row['time']) >= int(release['time'])]
    retention_error = max(separation(quaternion(row, 'r'), reference) for row in held)
    physical_capture = min(truth, key=lambda row: abs(int(row['time']) - release_time))
    physical_reference = quaternion(physical_capture)
    end_time = int(control[-1]['time'])
    final_truth = min(truth, key=lambda row: abs(int(row['time']) - end_time))
    final_physical_error = separation(quaternion(final_truth), physical_reference)
    final_estimated_error = separation(quaternion(control[-1]), reference)
    final_rate = math.sqrt(sum(float(final_truth['w' + key]) ** 2 for key in 'xyz'))
    peak = max(separation(quaternion(row), physical_reference) for row in truth
               if release_time <= int(row['time']) <= end_time)
    injected = [row for row in truth if abs(float(row['impulse_' + 'xyz'[axis]])) > 0]
    magnetic_angle, magnetic_samples = magnetic_rigidity(fixture.work, truth)
    checks = dict(rate_tracking=rate_error < RATE_TOLERANCE,
                  capture=capture_error < 2e-5, retention=retention_error < 1e-6,
                  final_physical_error=final_physical_error < FINAL_ANGLE_LIMIT,
                  final_estimated_error=final_estimated_error < FINAL_ANGLE_LIMIT,
                  final_rate=final_rate < FINAL_RATE_LIMIT, peak=peak < PEAK_ANGLE_LIMIT,
                  actual_impulse=len(injected) == 1,
                  magnetic_rigidity=magnetic_angle < math.radians(1),
                  thrust=all(row['thrust'] == '10' for row in control),
                  consumer_disable=truth[-1]['thrust'] == '0' and truth[-1]['lease_fault'] == '1')
    fixture.result.update(passed=all(checks.values()), axis=axis, direction=direction, checks=checks,
                          rate_error_rad_s=rate_error, capture_error_rad=capture_error,
                          retention_error_rad=retention_error, final_physical_error_rad=final_physical_error,
                          final_estimated_error_rad=final_estimated_error, final_rate_rad_s=final_rate,
                          peak_physical_error_rad=peak, control_cycles=len(control))
    fixture.result.update(maximum_magnetic_world_angle_deg=math.degrees(magnetic_angle), magnetic_samples=magnetic_samples)
    (fixture.work / 'result.json').write_text(json.dumps(fixture.result, indent=2) + '\n')
    print(('PASS' if fixture.result['passed'] else 'FAIL'), fixture.work, fixture.result, flush=True)
    if not fixture.result['passed']:
        raise RuntimeError('Campaign metrics failed: ' + str(fixture.work))


FAILURES = ('input_loss', 'wrong_source', 'estimator_loss', 'permission_loss', 'routing', 'producer_stall')


def prepare_reusing_native(source):
    """New parameter campaigns, exact already-built firmware and plugin identity."""
    source = source.resolve()
    assert source.parent == Path('/var/tmp') and source.name.startswith('research-stabilized-sitl-')
    manifest_path = source / 'campaign-manifest.json'
    manifest = json.loads(manifest_path.read_text())
    for name, identity in manifest['files'].items():
        assert base.digest(source / name) == identity, 'Parent evidence changed: ' + name
    graph = json.loads((ROOT / 'docs/RESEARCH_CODE_GRAPH.json').read_text())
    checkpoint = {}
    for node in graph['nodes']:
        if 'sha256' in node:
            assert base.digest(ROOT / node['path']) == node['sha256'], 'Stale graph anchor: ' + node['path']
            checkpoint[node['path']] = node['sha256']
    # Runtime/module/core inputs must be byte-identical to the compiled parent.
    for path in base.FIXTURE.rglob('*'):
        if path.is_file():
            assert base.digest(path) == base.digest(source / 'px4-source' / path.relative_to(ROOT))
    out = Path(tempfile.mkdtemp(prefix='research-stabilized-sitl-', dir='/var/tmp'))
    (out / 'checkpoint.json').write_text(json.dumps(checkpoint, indent=2) + '\n')
    (out / 'px4-source').symlink_to(source / 'px4-source', target_is_directory=True)
    (out / 'sitl-lockstep-build').mkdir()
    shutil.copytree(source / 'sitl-lockstep-build/bin', out / 'sitl-lockstep-build/bin', symlinks=True)
    for name in ('boardconfig', 'compile_commands.json'):
        shutil.copy2(source / 'sitl-lockstep-build' / name, out / 'sitl-lockstep-build' / name)
    (out / 'plugin-build').mkdir()
    shutil.copy2(source / 'plugin-build/libresearch_rotational_fixture.so', out / 'plugin-build/libresearch_rotational_fixture.so')
    record = dict(parent=str(source), parent_manifest_sha256=base.digest(manifest_path),
                  reuse='unchanged_compiled_runtime_with_source_bound_synthetic_test_inputs',
                  px4_sha256=base.digest(out / 'sitl-lockstep-build/bin/px4'),
                  plugin_sha256=base.digest(out / 'plugin-build/libresearch_rotational_fixture.so'))
    (out / 'native-origin.json').write_text(json.dumps(record, indent=2) + '\n')
    print('Prepared exact native reuse:', out, flush=True)


def failure(evidence, case):
    fixture = Fixture(evidence, 'failure-' + case)
    try:
        fixture.start()
        fixture.action('inject', case)
        if case == 'input_loss':
            fixture.sending = False
            fixture.wait(300000)
        elif case == 'wrong_source':
            from pymavlink import mavutil
            fixture.sending = False
            wrong = mavutil.mavlink_connection('udpout:127.0.0.1:19779', source_system=254, source_component=190)
            try:
                target = fixture.now() + 300000
                while fixture.now() < target:
                    wrong.mav.manual_control_send(1, 0, 0, 500, 0, 0)
                    time.sleep(0.02)
            finally:
                wrong.close()
        elif case == 'estimator_loss':
            output, _ = fixture.client('ekf2', 'stop')
            (fixture.work / 'estimator-stop.log').write_text(output)
            fixture.wait(300000)
        elif case == 'permission_loss':
            output, _ = fixture.client('commander', 'stop')
            (fixture.work / 'commander-stop.log').write_text(output)
            fixture.wait(800000)
        elif case == 'routing':
            fixture.client('param', 'set', 'SIM_GZ_SV_FUNC1', '201')
            fixture.wait(300000)
            fixture.client('param', 'set', 'SIM_GZ_SV_FUNC1', '0')
        elif case == 'producer_stall':
            fixture.px4.send_signal(signal.SIGSTOP)
            fixture.paused = True
            fixture.wait(500000)
            fixture.action('resume_producer')
            fixture.px4.send_signal(signal.SIGCONT)
            fixture.paused = False
            fixture.wait(300000)
        status, _ = fixture.client('research_stabilized_sitl', 'status')
        (fixture.work / 'fault-status.log').write_text(status)
        assert 'state=4' in status, 'Owner did not latch Fault: ' + status
        output, code = fixture.client('research_stabilized_sitl', 'enable', check=False)
        assert code != 0, 'Faulted owner accepted enable request'
        (fixture.work / 'reenable.log').write_text(output)
        fixture.wait(300000)
        status, _ = fixture.client('research_stabilized_sitl', 'status')
        assert 'state=4' in status, 'Enable silently recovered a faulted owner'
        fixture.result.update(case=case, fault_status=status.strip(), reenable_returncode=code)
        # A deliberate new owner lifecycle is the only recovery path. Restore
        # the lost producer/permission before enabling its new session.
        fixture.client('research_stabilized_sitl', 'stop')
        fixture.sending = True
        if case == 'estimator_loss':
            fixture.client('ekf2', 'start')
        if case == 'permission_loss':
            fixture.client('commander', 'start')
        fixture.client('research_stabilized_sitl', 'start', 'slaaps_fixture', str(fixture.work / 'restarted.csv'))
        deadline = time.monotonic() + 65
        while time.monotonic() < deadline:
            status, _ = fixture.client('research_stabilized_sitl', 'status')
            if 'state=1' in status and 'ready=1' in status:
                break
            time.sleep(0.05)
        else:
            raise RuntimeError('Explicit restart did not regain valid centered inputs')
        fixture.action('explicit_reenable')
        fixture.client('research_stabilized_sitl', 'enable')
        fixture.wait(1000000)
        status, _ = fixture.client('research_stabilized_sitl', 'status')
        assert 'state=3' in status and 'fault=0' in status, 'New explicit session did not regain Hold'
        fixture.result['restart_status'] = status.strip()
        fixture.client('research_stabilized_sitl', 'disable')
        fixture.wait(300000)
    except Exception as error:
        fixture.result['error'] = str(error)
        raise
    finally:
        fixture.close()
    truth = rows(fixture.work / 'truth.csv')
    first_active = next(i for i, row in enumerate(truth) if row['enabled'] == '1')
    first_off = next(i for i in range(first_active + 1, len(truth)) if truth[i]['enabled'] == '0')
    first_recovery = next(i for i in range(first_off + 1, len(truth)) if truth[i]['enabled'] == '1')
    off = truth[first_off:first_recovery]
    assert all(row['thrust'] == '0' and row['lease_fault'] == '1' for row in off), 'Consumer resumed faulted session'
    assert all(float(row['t' + str(i)]) == 0 for row in off for i in range(4)), 'Fault target was not neutral'
    if case == 'producer_stall':
        expiry_delay = int(truth[first_off]['time']) - int(truth[first_off]['command_time'])
        assert 100000 <= expiry_delay <= 104000, 'Consumer did not expire independently within one physics step'
        fixture.result['consumer_expiry_delay_us'] = expiry_delay
    old_session = int(truth[first_active]['session'])
    new_session = int(truth[first_recovery]['session'])
    assert new_session > old_session, 'Consumer accepted recovery without a newer explicit session'
    assert truth[-1]['thrust'] == '0' and truth[-1]['lease_fault'] == '1', 'Restarted disable did not reach consumer'
    fixture.result.update(passed=True, first_active_us=int(truth[first_active]['time']),
                          first_off_us=int(truth[first_off]['time']), final_thrust_n=0,
                          old_session=old_session, new_session=new_session)
    (fixture.work / 'result.json').write_text(json.dumps(fixture.result, indent=2) + '\n')
    print('PASS', fixture.work, fixture.result, flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--evidence', type=Path)
    parser.add_argument('--reuse-native-from', type=Path, help='Prepare fresh campaign evidence from verified unchanged native artifacts')
    parser.add_argument('--axis', type=int, choices=(0, 1, 2))
    parser.add_argument('--nose-up', action='store_true')
    parser.add_argument('--direction', type=int, choices=(-1, 1), default=1)
    parser.add_argument('--case', choices=('rate', 'failures', *FAILURES), default='rate')
    parser.add_argument('--inspect', type=Path)
    args = parser.parse_args()
    if args.reuse_native_from:
        assert args.evidence is None and args.inspect is None
        prepare_reusing_native(args.reuse_native_from)
        return
    if args.evidence is None:
        parser.error('--evidence is required to run or inspect campaigns')
    evidence = args.evidence.resolve()
    if evidence.parent != Path('/var/tmp') or not evidence.name.startswith('research-stabilized-sitl-'):
        raise RuntimeError('Require an isolated prepared evidence directory')
    if args.inspect:
        inspect_campaign(args.inspect)
    elif args.case == 'failures':
        for case in FAILURES:
            failure(evidence, case)
    elif args.case in FAILURES:
        failure(evidence, args.case)
    elif args.axis is not None:
        campaign(evidence, args.axis, args.nose_up, args.direction)
    else:
        for nose_up in (False, True):
            for axis in (0, 1, 2):
                for direction in (1, -1):
                    campaign(evidence, axis, nose_up, direction)


if __name__ == '__main__':
    main()
