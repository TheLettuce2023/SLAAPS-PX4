#!/usr/bin/env python3
"""Full three-axis manual commands, saturation feedback and hold in native SITL."""
import argparse
import json
import math
from pathlib import Path
import shutil

import verify_stabilized_campaigns as campaigns
import verify_stabilized_sitl as base
import seal_stabilized_campaigns as audit

RATE_PHASE_US = 8000000
INTEGRAL_AUDIT_EPSILON = 2e-7  # Log decimal/float roundoff, not a gain or control tolerance.


def coupled(evidence, nose_up, sign, seed=campaigns.SEED, *, dispatch_diagnostics=True):
    fixture = campaigns.Fixture(evidence, f'coupled-{"up" if nose_up else "level"}-{sign:+d}-seed{seed}',
                                nose_up, seed=seed)
    fixture.result.update(coupled=True, sign=sign, exploratory_only=(seed != campaigns.SEED),
                          coupled_source_sha256=base.digest(Path(__file__)))
    shutil.copy2(Path(__file__), fixture.work / Path(__file__).name)
    try:
        fixture.start()
        for phase in (sign, -sign):
            fixture.command = (-1000 * phase, 1000 * phase, 1000 * phase)
            fixture.action('coupled_full_rate', f'all_axes={phase}')
            fixture.wait(RATE_PHASE_US)
        fixture.command = (0, 0, 0)
        fixture.action('release')
        fixture.wait(1000000)
        impulses = [value * sign for value in (0.024, 0.064, 0.064)]
        fixture.action('coupled_impulse', ';'.join(map(str, impulses)))
        base.run(['gz', 'topic', '-t', '/model/slaaps_fixture/impulse', '-m', 'gz.msgs.Double_V',
                  '-p', ' '.join('data: ' + str(v) for v in impulses)], fixture.work, 'impulse.log',
                 env=fixture.env, timeout=10)
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
    rate = [row for row in control if row['state'] == '2']
    errors = []
    for phase in (sign, -sign):
        phase_rows = [row for row in rate if int(float(row['roll']) * phase) == 1]
        assert phase_rows, 'Missing full-rate phase'
        end = int(phase_rows[-1]['time'])
        errors.append(max(abs(float(row['w' + axis]) - phase) for row in phase_rows
                          if int(row['time']) >= end - 300000 for axis in 'xyz'))
    rate_end = int(rate[-1]['time'])
    release = next(row for row in control if row['state'] == '3' and int(row['time']) > rate_end)
    reference = campaigns.quaternion(release, 'r')
    capture = campaigns.separation(campaigns.quaternion(release), reference)
    retention = max(campaigns.separation(campaigns.quaternion(row, 'r'), reference) for row in control
                    if int(row['time']) >= int(release['time']))
    actual_reference = campaigns.quaternion(min(truth, key=lambda row: abs(int(row['time']) - int(release['att_sample']))))
    final = min(truth, key=lambda row: abs(int(row['time']) - int(control[-1]['time'])))
    physical_error = campaigns.separation(campaigns.quaternion(final), actual_reference)
    estimated_error = campaigns.separation(campaigns.quaternion(control[-1]), reference)
    final_rate = math.sqrt(sum(float(final['w' + axis]) ** 2 for axis in 'xyz'))
    peak = max(campaigns.separation(campaigns.quaternion(row), actual_reference) for row in truth
               if int(release['att_sample']) <= int(row['time']) <= int(control[-1]['time']))
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
                violations += int((positive and change > INTEGRAL_AUDIT_EPSILON)
                                  or (negative and change < -INTEGRAL_AUDIT_EPSILON))
    magnetic_angle, _ = campaigns.magnetic_rigidity(fixture.work, truth)
    # Informational legacy-owner diagnostic; never an acceptance criterion.
    # The integrated session owner has no events CSV: report unavailable, not zero.
    duplicates = (sum(row['action'] == 'dispatch_not_due'
                      for row in campaigns.rows(fixture.work / 'control.csv.events.csv'))
                  if dispatch_diagnostics else None)
    checks = dict(rate_tracking=max(errors) < campaigns.RATE_TOLERANCE,
                  capture=capture < 2e-5, retention=retention < 1e-6,
                  final_physical_error=physical_error < campaigns.FINAL_ANGLE_LIMIT,
                  final_estimated_error=estimated_error < campaigns.FINAL_ANGLE_LIMIT,
                  final_rate=final_rate < campaigns.FINAL_RATE_LIMIT,
                  peak=peak < campaigns.PEAK_ANGLE_LIMIT,
                  magnitude_saturation=box_cycles > 0,
                  all_axis_feedback=all(value > 0 for value in blocked),
                  directional_antiwindup=violations == 0,
                  magnetic_rigidity=magnetic_angle < math.radians(1),
                  consumer_disable=truth[-1]['thrust'] == '0' and truth[-1]['lease_fault'] == '1')
    fixture.result.update(passed=all(checks.values()), checks=checks,
                          rate_errors_rad_s=errors, final_physical_error_rad=physical_error,
                          final_estimated_error_rad=estimated_error, final_rate_rad_s=final_rate,
                          peak_physical_error_rad=peak, capture_error_rad=capture, retention_error_rad=retention,
                          box_saturated_cycles=box_cycles, blocked_axis_cycles=blocked,
                          antiwindup_violations=violations, audited_control_cycles=count,
                          duplicate_dispatches_without_control=duplicates)
    (fixture.work / 'result.json').write_text(json.dumps(fixture.result, indent=2) + '\n')
    print(('PASS' if fixture.result['passed'] else 'FAIL'), fixture.work, fixture.result, flush=True)
    assert fixture.result['passed'], 'Coupled performance/feedback checks failed'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--evidence', type=Path, required=True)
    parser.add_argument('--nose-up', action='store_true')
    parser.add_argument('--sign', type=int, choices=(-1, 1), default=1)
    parser.add_argument('--seed', type=int, default=campaigns.SEED)
    args = parser.parse_args()
    evidence = args.evidence.resolve()
    assert evidence.parent == Path('/var/tmp') and evidence.name.startswith('research-stabilized-sitl-')
    coupled(evidence, args.nose_up, args.sign, args.seed)


if __name__ == '__main__':
    main()
