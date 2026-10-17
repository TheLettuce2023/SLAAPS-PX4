#!/usr/bin/env python3
"""Explore unequal, signed three-axis pilot rates and held attitude in native SITL."""
import argparse
import bisect
import json
import math
from pathlib import Path
import shutil

import seal_stabilized_campaigns as audit
import verify_stabilized_campaigns as campaigns
import verify_stabilized_coupled as coupled
import verify_stabilized_sitl as base


BODY_STICKS = (1.0, -0.7, 0.4)  # Body FRD roll, pitch, yaw before deadzone shaping.
IMPULSE_NMS = (0.024, -0.064, 0.064)
PHASE_US = 8000000
CENTER = 0.04


def expected_rate(stick):
    return math.copysign(max(0.0, (abs(stick) - CENTER) / (1 - CENTER)), stick)


def run_case(evidence, nose_up, first_sign, manual_period_s=0.02, magnetic_delay_us=0,
             imu_delay_us=0, seed=campaigns.SEED, fixture_type=campaigns.Fixture):
    delay_suffix = f'-mag{magnetic_delay_us // 1000}' if magnetic_delay_us else ''
    delay_suffix += f'-imu{imu_delay_us // 1000}' if imu_delay_us else ''
    delay_suffix += f'-seed{seed}' if seed != campaigns.SEED else ''
    fixture = fixture_type(evidence,
                                f'mixed-{"up" if nose_up else "level"}-{first_sign:+d}-period{round(manual_period_s * 1000)}'
                                + delay_suffix, nose_up, manual_period_s=manual_period_s,
                                magnetic_delay_us=magnetic_delay_us, imu_delay_us=imu_delay_us,
                                seed=seed)
    fixture.result.update(exploratory_only=True, mixed_axes=True, first_sign=first_sign,
                          body_sticks=BODY_STICKS, impulse_nms=IMPULSE_NMS,
                          mixed_source_sha256=base.digest(Path(__file__)))
    shutil.copy2(Path(__file__), fixture.work / Path(__file__).name)

    try:
        fixture.start()

        for phase in (first_sign, -first_sign):
            raw = [value * phase for value in BODY_STICKS]
            # MAVLink MANUAL_CONTROL x=pitch, y=roll, r=yaw. PX4's selected
            # pitch is sign-reversed by the declared controller configuration.
            fixture.command = (-round(1000 * raw[1]), round(1000 * raw[0]), round(1000 * raw[2]))
            fixture.action('mixed_body_rate', ';'.join(map(str, raw)))
            fixture.wait(PHASE_US)

        fixture.command = (0, 0, 0)
        fixture.action('release')
        fixture.wait(1000000)
        impulse = [value * first_sign for value in IMPULSE_NMS]
        fixture.action('mixed_impulse', ';'.join(map(str, impulse)))
        base.run(['gz', 'topic', '-t', '/model/slaaps_fixture/impulse', '-m', 'gz.msgs.Double_V',
                  '-p', ' '.join('data: ' + str(value) for value in impulse)],
                 fixture.work, 'impulse.log', env=fixture.env, timeout=10)
        fixture.wait(7000000)
        status, _ = fixture.client('research_stabilized_sitl', 'status')
        (fixture.work / 'active-status.log').write_text(status)
        assert 'state=3' in status and 'fault=0' in status, 'Owner revoked: ' + status
        fixture.client('research_stabilized_sitl', 'disable')
        fixture.wait(300000)

    except Exception as error:
        fixture.result['error'] = str(error)
        raise

    finally:
        fixture.close()

    control = campaigns.rows(fixture.work / 'control.csv')
    truth = campaigns.rows(fixture.work / 'truth.csv')
    count = audit.audit_cycles(fixture.work / 'control.csv')
    manual_ages = sorted(int(row['time']) - int(row['manual_sample']) for row in control)
    times = [int(row['time']) for row in truth]
    rate = [row for row in control if row['state'] == '2']
    estimated_errors = []
    physical_errors = []
    setpoint_errors = []

    for phase in (first_sign, -first_sign):
        raw = [value * phase for value in BODY_STICKS]
        selected = [row for row in rate if abs(float(row['roll']) - raw[0]) < 1e-5
                    and abs(float(row['pitch']) + raw[1]) < 1e-5
                    and abs(float(row['yaw']) - raw[2]) < 1e-5]
        assert selected, 'Missing requested mixed-rate phase'
        end = int(selected[-1]['time'])
        settled = [row for row in selected if int(row['time']) >= end - 300000]
        assert len(settled) >= 40
        target = [expected_rate(value) for value in raw]

        for row in settled:
            stamp = int(row['rate_sample'])
            index = bisect.bisect_left(times, stamp)
            index = min((max(0, index - 1), min(index, len(truth) - 1)), key=lambda i: abs(times[i] - stamp))
            assert abs(times[index] - stamp) <= 4000

            for i, axis in enumerate('xyz'):
                setpoint_errors.append(abs(float(row['s' + axis]) - target[i]))
                estimated_errors.append(abs(float(row['w' + axis]) - target[i]))
                physical_errors.append(abs(float(truth[index]['w' + axis]) - target[i]))

    release = next(row for row in control if row['state'] == '3' and int(row['time']) > int(rate[-1]['time']))
    reference = campaigns.quaternion(release, 'r')
    capture = campaigns.separation(campaigns.quaternion(release), reference)
    retention = max(campaigns.separation(campaigns.quaternion(row, 'r'), reference) for row in control
                    if int(row['time']) >= int(release['time']))
    physical_reference = campaigns.quaternion(min(truth, key=lambda row:
                                                  abs(int(row['time']) - int(release['att_sample']))))
    final = min(truth, key=lambda row: abs(int(row['time']) - int(control[-1]['time'])))
    final_physical = campaigns.separation(campaigns.quaternion(final), physical_reference)
    final_estimated = campaigns.separation(campaigns.quaternion(control[-1]), reference)
    final_rate = math.sqrt(sum(float(final['w' + axis]) ** 2 for axis in 'xyz'))
    peak = max(campaigns.separation(campaigns.quaternion(row), physical_reference) for row in truth
               if int(release['att_sample']) <= int(row['time']) <= int(control[-1]['time']))
    injected = [row for row in truth if any(float(row['impulse_' + axis]) != 0 for axis in 'xyz')]
    impulse = [value * first_sign for value in IMPULSE_NMS]
    box_cycles = sum(any(abs(float(row['u' + str(i)])) >= 0.999 for i in range(4)) for row in control)
    blocked = [0, 0, 0]
    violations = 0

    for previous, row in zip(control, control[1:]):
        for i, axis in enumerate('xyz'):
            error = float(row['s' + axis]) - float(row['w' + axis])
            change = float(row['i' + axis]) - float(previous['i' + axis])
            positive = previous['p' + axis] == '1' and error > 0
            negative = previous['n' + axis] == '1' and error < 0

            if positive or negative:
                blocked[i] += 1
                violations += int((positive and change > coupled.INTEGRAL_AUDIT_EPSILON)
                                  or (negative and change < -coupled.INTEGRAL_AUDIT_EPSILON))

    magnetic_angle, _ = campaigns.magnetic_rigidity(fixture.work, truth)
    checks = dict(setpoint_mapping=max(setpoint_errors) < 1e-4,
                  estimated_rate_tracking=max(estimated_errors) < campaigns.RATE_TOLERANCE,
                  physical_rate_tracking=max(physical_errors) < campaigns.RATE_TOLERANCE,
                  capture=capture < 2e-5, retention=retention < 1e-6,
                  final_physical_error=final_physical < campaigns.FINAL_ANGLE_LIMIT,
                  final_estimated_error=final_estimated < campaigns.FINAL_ANGLE_LIMIT,
                  final_rate=final_rate < campaigns.FINAL_RATE_LIMIT,
                  peak=peak < campaigns.PEAK_ANGLE_LIMIT,
                  actual_impulse=len(injected) == 1 and all(
                      abs(float(injected[0]['impulse_' + axis]) - impulse[i]) < 1e-6
                      for i, axis in enumerate('xyz')),
                  magnitude_saturation=box_cycles > 0,
                  directional_antiwindup=violations == 0,
                  magnetic_rigidity=magnetic_angle < math.radians(1),
                  consumer_disable=truth[-1]['thrust'] == '0' and truth[-1]['lease_fault'] == '1')
    fixture.result.update(passed=all(checks.values()), checks=checks, audited_control_cycles=count,
                          manual_sample_age_p95_us=manual_ages[int(0.95 * (len(manual_ages) - 1))],
                          manual_sample_age_max_us=manual_ages[-1],
                          maximum_setpoint_error_rad_s=max(setpoint_errors),
                          maximum_estimated_rate_error_rad_s=max(estimated_errors),
                          maximum_physical_rate_error_rad_s=max(physical_errors),
                          capture_error_rad=capture, retention_error_rad=retention,
                          final_physical_error_rad=final_physical,
                          final_estimated_error_rad=final_estimated,
                          final_rate_rad_s=final_rate, peak_physical_error_rad=peak,
                          box_saturated_cycles=box_cycles, blocked_axis_cycles=blocked,
                          antiwindup_violations=violations)
    (fixture.work / 'result.json').write_text(json.dumps(fixture.result, indent=2) + '\n')
    print(('PASS' if fixture.result['passed'] else 'FAIL'), fixture.work, fixture.result, flush=True)
    assert fixture.result['passed'], 'Mixed-axis command/recovery checks failed'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--evidence', type=Path, required=True)
    parser.add_argument('--nose-up', action='store_true')
    parser.add_argument('--first-sign', type=int, choices=(-1, 1), default=1)
    parser.add_argument('--manual-period-ms', type=int, choices=(20, 60, 80), default=20)
    parser.add_argument('--magnetic-delay-ms', type=int, choices=(0, 20, 40), default=0)
    parser.add_argument('--imu-delay-ms', type=int, choices=(0, 20, 40, 80), default=0)
    parser.add_argument('--seed', type=int, choices=(42, 43, 44), default=42)
    args = parser.parse_args()
    evidence = args.evidence.resolve()
    assert evidence.parent == Path('/var/tmp') and evidence.name.startswith('research-stabilized-sitl-')
    run_case(evidence, args.nose_up, args.first_sign, args.manual_period_ms / 1000,
             args.magnetic_delay_ms * 1000, args.imu_delay_ms * 1000, args.seed)


if __name__ == '__main__':
    main()
