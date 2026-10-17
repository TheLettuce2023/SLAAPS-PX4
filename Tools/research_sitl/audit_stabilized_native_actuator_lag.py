#!/usr/bin/env python3
"""Audit timestamped PX4 commands and physical servo lag in sealed SITL traces."""
import csv
import json
import math
from pathlib import Path
import shutil
import tempfile

import prepare_stabilized_generated_allocation as preparation
import verify_stabilized_allocation_binding as binding
import verify_stabilized_sitl as base


SITL = binding.SITL
SITL_MANIFEST_SHA256 = binding.SITL_MANIFEST_SHA256
FIXTURE = Path('Tools/research_sitl/stabilized_manual/RotationalFixture.cpp')
CONSTANTS = Path('Tools/research_sitl/stabilized_manual/FixtureConstants.hpp')
SERVO_TIME_CONSTANT_S = 0.05
MAXIMUM_DEFLECTION_RAD = 0.22689280275926285


def rows(path):
    with path.open(newline='') as source:
        return list(csv.DictReader(source))


def audit_case(directory, expected):
    control_path = directory / 'control.csv'
    truth_path = directory / 'truth.csv'
    controls = rows(control_path)
    truth = rows(truth_path)
    assert controls and truth
    controls_by_time = {int(row['time']): row for row in controls}
    assert len(controls_by_time) == len(controls) == expected['cycles']
    enabled_count = 0
    largest_response_gap = 0.0
    largest_recurrence_error = 0.0
    largest_deflection_error = 0.0
    largest_target_error = 0.0
    previous = None
    for row in truth:
        now = int(row['time'])
        target = [float(row[f't{channel}']) for channel in range(4)]
        surface = [float(row[f'u{channel}']) for channel in range(4)]
        angle = [float(row[f'a{channel}']) for channel in range(4)]
        assert all(math.isfinite(value) for value in target + surface + angle)
        assert all(abs(value) <= 1.0000001 for value in target + surface)
        assert row['enabled'] in ('0', '1') and row['lease_fault'] in ('0', '1')
        if previous is not None:
            dt_s = (now - int(previous['time'])) / 1e6
            assert 0 < dt_s <= 0.02
            decay = math.exp(-dt_s / SERVO_TIME_CONSTANT_S)
            for channel in range(4):
                prior = float(previous[f'u{channel}'])
                reconstructed = prior * decay + target[channel] * (1.0 - decay)
                largest_recurrence_error = max(largest_recurrence_error,
                                               abs(surface[channel] - reconstructed))
        for channel in range(4):
            largest_deflection_error = max(largest_deflection_error,
                                           abs(angle[channel] - surface[channel] * MAXIMUM_DEFLECTION_RAD))
            largest_response_gap = max(largest_response_gap, abs(target[channel] - surface[channel]))
        if row['enabled'] == '1':
            enabled_count += 1
            assert row['lease_fault'] == '0' and math.isclose(float(row['thrust']), 10.0)
            command_time = int(row['command_time'])
            assert command_time in controls_by_time and command_time <= now
            command = controls_by_time[command_time]
            for channel in range(4):
                largest_target_error = max(largest_target_error,
                                           abs(target[channel] - float(command[f'u{channel}'])))
        else:
            assert math.isclose(float(row['thrust']), 0.0)
            assert all(abs(value) < 1e-8 for value in target)
        previous = row
    assert enabled_count == expected['cycles']
    assert largest_recurrence_error < 1e-10
    assert largest_deflection_error < 1e-10
    assert largest_target_error < 1e-7
    assert largest_response_gap > 0.05, 'No exercised actuator lag'
    return dict(directory=directory.name, truth_rows=len(truth), enabled_rows=enabled_count,
                control_sha256=base.digest(control_path), truth_sha256=base.digest(truth_path),
                maximum_target_to_surface_gap=largest_response_gap,
                maximum_recurrence_error=largest_recurrence_error,
                maximum_deflection_error=largest_deflection_error,
                maximum_timestamped_target_error=largest_target_error)


def main():
    preparation.verify_receipt(SITL, 'generated-allocation-manifest.json', SITL_MANIFEST_SHA256)
    graph = json.loads((base.ROOT / 'docs/RESEARCH_CODE_GRAPH.json').read_text())
    checkpoint = {}
    for node in graph['nodes']:
        if 'sha256' in node:
            identity = base.digest(base.ROOT / node['path'])
            assert identity == node['sha256'], 'Stale graph: ' + node['path']
            checkpoint[node['path']] = identity
    for path in (FIXTURE, CONSTANTS):
        identity = base.digest(base.ROOT / path)
        assert identity == base.digest(SITL / 'px4-source' / path)
    source = (base.ROOT / FIXTURE).read_text()
    constants = (base.ROOT / CONSTANTS).read_text()
    assert '_surface[i] += (target - _surface[i]) * (1.0 - std::exp(-dt / kServoTimeConstantS));' in source
    assert 'surface *kMaximumDeflectionRad' in source
    assert 'kServoTimeConstantS = 0.05;' in constants
    assert 'kMaximumDeflectionRad = 0.22689280275926285;' in constants
    origin = json.loads((SITL / 'generated-allocation-origin.json').read_text())
    parent = json.loads((SITL / 'generated-allocation-summary.json').read_text())
    assert parent['scope'] == 'two_exploratory_generated_synthetic_allocation_mixed_axis_cases'
    assert not parent['vehicle_qualified'] and not parent['accepted_campaign_expansion']
    assert parent['generated_px4_sha256'] == origin['generated_px4_sha256']
    assert parent['generated_profile_sha256'] == origin['synthetic_input_sha256']
    assert len(parent['cases']) == 2 and {case['nose_up'] for case in parent['cases']} == {True, False}
    results = []
    for case in parent['cases']:
        directory = SITL / case['directory']
        assert directory.is_dir() and case['observed_checks_pass']
        for path in (FIXTURE, CONSTANTS):
            assert base.digest(directory / 'source-inputs' / path) == base.digest(base.ROOT / path)
        results.append(audit_case(directory, case))
    assert sum(case['enabled_rows'] for case in results) == parent['audited_control_cycles'] == 12953
    out = Path(tempfile.mkdtemp(prefix='research-stabilized-actuator-lag-', dir='/var/tmp'))
    (out / 'checkpoint.json').write_text(json.dumps(checkpoint, indent=2) + '\n')
    shutil.copy2(Path(__file__), out / Path(__file__).name)
    shutil.copy2(base.ROOT / FIXTURE, out / FIXTURE.name)
    shutil.copy2(base.ROOT / CONSTANTS, out / CONSTANTS.name)
    summary = dict(scope='sealed_native_generated_profile_synthetic_servo_lag_trace_audit',
                   accepted=False, vehicle_qualified=False, physical_test=False,
                   parent_manifest_sha256=SITL_MANIFEST_SHA256,
                   parent_px4_sha256=origin['generated_px4_sha256'],
                   parent_plugin_sha256=origin['plugin_sha256'],
                   parent_profile_sha256=origin['synthetic_input_sha256'],
                   fixture_source_sha256=base.digest(base.ROOT / FIXTURE),
                   fixture_constants_sha256=base.digest(base.ROOT / CONSTANTS),
                   graph_anchors=len(checkpoint), cases=results)
    (out / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    files = {path.name: base.digest(path) for path in out.iterdir()
             if path.is_file() and path.name != 'actuator-lag-manifest.json'}
    (out / 'actuator-lag-manifest.json').write_text(json.dumps(dict(
        scope=summary['scope'], accepted=False, vehicle_qualified=False, files=files), indent=2) + '\n')
    print(json.dumps(dict(evidence=str(out),
                          manifest_sha256=base.digest(out / 'actuator-lag-manifest.json'),
                          files=len(files), graph_anchors=len(checkpoint),
                          cases=results), indent=2), flush=True)


if __name__ == '__main__':
    main()
