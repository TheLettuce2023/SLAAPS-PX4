#!/usr/bin/env python3
"""Audit and seal private constant-bias native SITL cases without rerunning them."""
import csv
import json
import math
from pathlib import Path
import shutil

import prepare_stabilized_generated_allocation as preparation
import screen_stabilized_net_authority as net_screen
import seal_stabilized_campaigns as campaigns_audit
import verify_stabilized_allocation_binding as binding
import verify_stabilized_sitl as base


PARENT = binding.SITL
PARENT_MANIFEST_SHA256 = binding.SITL_MANIFEST_SHA256
EVIDENCE = Path('/var/tmp/research-stabilized-sitl-mfn4yrcb')
PATCHED_FIXTURE = Path('bias-source/Tools/research_sitl/stabilized_manual/RotationalFixture.cpp')
MATRIX = ((0.25, -0.25, 0.25, -0.25),
          (0.25, 0.25, -0.25, -0.25),
          (0.25, -0.25, -0.25, 0.25))
MOMENT_SCALE_NM = 0.4
SERVO_TIME_CONSTANT_S = 0.05


def rows(path):
    with path.open(newline='') as source:
        return list(csv.DictReader(source))


def audit_case(directory, origin):
    result = json.loads((directory / 'result.json').read_text())
    assert result['passed'] and result['exploratory_bias_variant']
    assert len(result['checks']) == 14 and all(result['checks'].values())
    assert result['px4_sha256'] == origin['parent_generated_px4_sha256']
    assert result['plugin_sha256'] == origin['plugin_sha256']
    assert result['bias_origin_sha256'] == base.digest(EVIDENCE / 'bias-origin.json')
    assert result['patched_fixture_sha256'] == origin['patched_fixture_sha256']
    assert result['synthetic_enabled_bias_nm_frd'] == origin['bias_nm_frd']
    assert result['runtime_profile_sha_observed']
    control_path = directory / 'control.csv'
    truth_path = directory / 'truth.csv'
    count = campaigns_audit.audit_cycles(control_path)
    controls = rows(control_path)
    truth = rows(truth_path)
    assert count == result['audited_control_cycles'] == len(controls)
    controls_by_time = {int(row['time']): row for row in controls}
    assert len(controls_by_time) == len(controls)
    enabled = 0
    accepted_command_times = set()
    maximum_target_error = 0.0
    maximum_servo_error = 0.0
    maximum_angle_error = 0.0
    maximum_surface_gap = 0.0
    previous = None
    expected_bias = origin['bias_nm_frd']
    for row in truth:
        now = int(row['time'])
        active = row['enabled'] == '1'
        assert row['enabled'] in ('0', '1')
        target = [float(row[f't{channel}']) for channel in range(4)]
        surface = [float(row[f'u{channel}']) for channel in range(4)]
        angle = [float(row[f'a{channel}']) for channel in range(4)]
        bias = [float(row[f'bias_{axis}']) for axis in 'xyz']
        assert all(math.isfinite(value) for value in target + surface + angle + bias)
        assert all(abs(value) <= 1.0000001 for value in target + surface)
        assert all(abs(bias[axis] - (expected_bias[axis] if active else 0.0)) < 1e-12
                   for axis in range(3))
        if previous is not None:
            dt_s = (now - int(previous['time'])) / 1e6
            assert 0 < dt_s <= 0.02
            decay = math.exp(-dt_s / SERVO_TIME_CONSTANT_S)
            for channel in range(4):
                prior = float(previous[f'u{channel}'])
                reconstructed = prior * decay + target[channel] * (1 - decay)
                maximum_servo_error = max(maximum_servo_error,
                                          abs(surface[channel] - reconstructed))
        for channel in range(4):
            maximum_angle_error = max(maximum_angle_error,
                                      abs(angle[channel] - surface[channel] * 0.22689280275926285))
            maximum_surface_gap = max(maximum_surface_gap, abs(target[channel] - surface[channel]))
        if active:
            enabled += 1
            assert row['lease_fault'] == '0' and math.isclose(float(row['thrust']), 10.0)
            command_time = int(row['command_time'])
            assert command_time in controls_by_time and command_time <= now
            accepted_command_times.add(command_time)
            for channel in range(4):
                maximum_target_error = max(maximum_target_error,
                                           abs(target[channel] - float(controls_by_time[command_time][f'u{channel}'])))
        else:
            assert math.isclose(float(row['thrust']), 0.0)
            assert all(abs(value) < 1e-8 for value in target)
        previous = row
    assert enabled > 0 and accepted_command_times
    assert maximum_servo_error < 1e-10 and maximum_angle_error < 1e-10
    assert maximum_target_error < 1e-7 and maximum_surface_gap > 0.05
    last_enabled_time = max(int(row['time']) for row in truth if row['enabled'] == '1')
    final = [row for row in truth if row['enabled'] == '1'
             and int(row['time']) >= last_enabled_time - 300000]
    assert len(final) >= 50
    mean_canard = [sum(MOMENT_SCALE_NM * sum(MATRIX[axis][channel] * float(row[f'u{channel}'])
                                               for channel in range(4)) for row in final) / len(final)
                   for axis in range(3)]
    mean_canard_plus_bias = [mean_canard[axis] + expected_bias[axis] for axis in range(3)]
    assert all(abs(value) < 0.001 for value in mean_canard_plus_bias)
    return dict(directory=directory.name, nose_up=result['nose_up'], first_sign=result['first_sign'],
                control_cycles=count, truth_rows=len(truth), enabled_truth_rows=enabled,
                distinct_observed_command_times=len(accepted_command_times),
                control_publications_without_enabled_target=len(set(controls_by_time) - accepted_command_times),
                final_window_rows=len(final), result_sha256=base.digest(directory / 'result.json'),
                control_sha256=base.digest(control_path), truth_sha256=base.digest(truth_path),
                maximum_target_error=maximum_target_error,
                maximum_servo_recurrence_error=maximum_servo_error,
                maximum_deflection_scale_error=maximum_angle_error,
                maximum_target_surface_gap=maximum_surface_gap,
                mean_final_canard_moment_nm_frd=mean_canard,
                mean_final_canard_plus_bias_nm_frd=mean_canard_plus_bias,
                final_physical_error_rad=result['final_physical_error_rad'],
                maximum_physical_rate_error_rad_s=result['maximum_physical_rate_error_rad_s'])


def main():
    preparation.verify_receipt(PARENT, 'generated-allocation-manifest.json', PARENT_MANIFEST_SHA256)
    graph = json.loads((base.ROOT / 'docs/RESEARCH_CODE_GRAPH.json').read_text())
    checkpoint = {}
    for node in graph['nodes']:
        if 'sha256' in node:
            identity = base.digest(base.ROOT / node['path'])
            assert identity == node['sha256'], 'Stale graph: ' + node['path']
            checkpoint[node['path']] = identity
    origin = json.loads((EVIDENCE / 'bias-origin.json').read_text())
    assert origin['scope'] == 'private_synthetic_enabled_moment_bias_variant'
    assert not origin['accepted'] and not origin['vehicle_qualified']
    assert origin['parent_sitl_manifest_sha256'] == PARENT_MANIFEST_SHA256
    assert base.digest(EVIDENCE / PATCHED_FIXTURE) == origin['patched_fixture_sha256']
    assert base.digest(EVIDENCE / 'plugin-build/libresearch_rotational_fixture.so') == origin['plugin_sha256']
    assert base.digest(EVIDENCE / 'sitl-lockstep-build/bin/px4') == origin['parent_generated_px4_sha256']
    assert base.digest(base.ROOT / 'Tools/research_sitl/stabilized_manual/RotationalFixture.cpp') == origin[
        'original_fixture_sha256']
    assert origin['bias_nm_frd'] == [0.01, -0.015, 0.012]
    source = (EVIDENCE / PATCHED_FIXTURE).read_text()
    assert 'kSyntheticEnabledBiasNm[3] = {0.01, -0.015, 0.012}' in source
    assert 'moment[axis] += _enabled ? kSyntheticEnabledBiasNm[axis] : 0.0;' in source
    report = json.loads((PARENT / 'synthetic-allocation.json').read_text())
    profile = json.loads((PARENT / 'stabilized_allocation_synthetic.json').read_text())
    assert report['input_sha256'] == origin['generated_profile_sha256']
    assert report['allocator_inputs']['effectiveness'] == [list(row) for row in MATRIX]
    assert profile['moment_scale_nm'] == [MOMENT_SCALE_NM] * 3
    specification = json.loads((base.ROOT / 'Tools/research_sitl/stabilized_net_authority_synthetic.json').read_text())
    specification['bias_nm_frd'] = origin['bias_nm_frd']
    specification['provenance']['bias']['sha256'] = origin['patched_fixture_sha256']
    screen = net_screen.screen_net_box(profile, report, specification)
    assert screen['status'] == 'sufficient_static_net_box_reachable'
    directories = sorted(directory for directory in EVIDENCE.iterdir()
                         if directory.is_dir() and (directory / 'result.json').exists())
    assert len(directories) == 2
    results = [audit_case(directory, origin) for directory in directories]
    assert {result['nose_up'] for result in results} == {False, True}
    assert sum(result['control_cycles'] for result in results) == 12931
    summary = dict(scope='two_exploratory_private_synthetic_steady_bias_native_cases',
                   accepted=False, vehicle_qualified=False, physical_test=False,
                   parent_manifest_sha256=PARENT_MANIFEST_SHA256,
                   generated_px4_sha256=origin['parent_generated_px4_sha256'],
                   bias_plugin_sha256=origin['plugin_sha256'],
                   patched_fixture_sha256=origin['patched_fixture_sha256'],
                   declared_bias_nm_frd=origin['bias_nm_frd'],
                   static_net_screen_status=screen['status'],
                   static_net_screen_correction_norm_squared=screen['correction_norm_squared'],
                   graph_anchors=len(checkpoint), cases=results)
    (EVIDENCE / 'bias-audit-summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    shutil.copy2(Path(__file__), EVIDENCE / Path(__file__).name)
    files = {str(path.relative_to(EVIDENCE)): base.digest(path)
             for path in EVIDENCE.rglob('*') if path.is_file()
             and 'sitl-lockstep-build' not in path.relative_to(EVIDENCE).parts
             and path.name != 'bias-manifest.json'}
    (EVIDENCE / 'bias-manifest.json').write_text(json.dumps(dict(
        scope=summary['scope'], accepted=False, vehicle_qualified=False,
        parent_manifest_sha256=PARENT_MANIFEST_SHA256, files=files), indent=2) + '\n')
    print(json.dumps(dict(evidence=str(EVIDENCE),
                          manifest_sha256=base.digest(EVIDENCE / 'bias-manifest.json'),
                          files=len(files), graph_anchors=len(checkpoint),
                          total_cycles=sum(result['control_cycles'] for result in results),
                          cases=results), indent=2), flush=True)


if __name__ == '__main__':
    main()
