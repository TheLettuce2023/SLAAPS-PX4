#!/usr/bin/env python3
"""Audit held, reversed and released requests through the private C++ allocator."""
import csv
import json
import math
from pathlib import Path
import shutil
import subprocess
import tempfile

import prepare_stabilized_generated_allocation as preparation
import verify_stabilized_allocation_binding as binding
import verify_stabilized_sitl as base


SITL = binding.SITL
SITL_MANIFEST_SHA256 = binding.SITL_MANIFEST_SHA256
GRID = Path('/var/tmp/research-stabilized-allocation-38ewevkd')
GRID_MANIFEST_SHA256 = 'c73cead601efd5fb796393648eca7c9cc1c72b35079107e22903a27cd815f005'
PROBE = Path(__file__).with_name('probe_stabilized_allocator_sequence.cpp')
DEMAND = Path(__file__).with_name('stabilized_static_demand_synthetic.json')
PHASES = ('hold', 'reverse', 'zero')
STEPS_PER_PHASE = 12
DT_S = 0.01


def audit(rows, compiled, demand):
    values = compiled['allocator_inputs']
    matrix = values['effectiveness']
    trim = values['trim']
    assert demand['axis_abs_bound'] == [0.1, 0.1, 0.1]
    results = []
    slew_contacts = 0
    largest_step = 0.0
    previous_expected = None
    row_count = 0
    for index, row in enumerate(rows):
        row_count += 1
        corner = index // (len(PHASES) * STEPS_PER_PHASE)
        phase_index = (index // STEPS_PER_PHASE) % len(PHASES)
        phase = PHASES[phase_index]
        step = index % STEPS_PER_PHASE
        assert corner < 8 and int(row['corner']) == corner
        assert row['phase'] == phase and int(row['step']) == step
        assert row['profile'] == compiled['input_sha256'] == demand['profile_input_sha256']
        signs = [1 if corner & (1 << axis) else -1 for axis in range(3)]
        scale = 1 if phase == 'hold' else (-1 if phase == 'reverse' else 0)
        requested = [float(row[f'request_{axis}']) for axis in 'xyz']
        previous = [float(row[f'previous_u{channel}']) for channel in range(4)]
        command = [float(row[f'u{channel}']) for channel in range(4)]
        achieved = [float(row[f'achieved_{axis}']) for axis in 'xyz']
        residual = [float(row[f'residual_{axis}']) for axis in 'xyz']
        objective = float(row['objective'])
        assert all(math.isfinite(value) for value in requested + previous + command + achieved + residual)
        assert math.isfinite(objective) and objective >= 0
        for axis in range(3):
            assert math.isclose(requested[axis], scale * signs[axis] * 0.1, abs_tol=2e-8)
            independent = sum(matrix[axis][channel] * (command[channel] - trim[channel])
                              for channel in range(4))
            assert math.isclose(achieved[axis], independent, abs_tol=5e-8)
            assert math.isclose(residual[axis], requested[axis] - independent, abs_tol=5e-8)
        if step == 0 and phase == 'hold':
            assert all(math.isclose(previous[channel], trim[channel], abs_tol=1e-8)
                       for channel in range(4))
        else:
            assert previous_expected is not None
            assert all(math.isclose(previous[channel], previous_expected[channel], abs_tol=1e-8)
                       for channel in range(4))
        for channel in range(4):
            lower = max(values['minimum'][channel],
                        previous[channel] - DT_S * values['negative_slew_per_s'][channel])
            upper = min(values['maximum'][channel],
                        previous[channel] + DT_S * values['positive_slew_per_s'][channel])
            assert lower - 2e-7 <= command[channel] <= upper + 2e-7
            change = abs(command[channel] - previous[channel])
            largest_step = max(largest_step, change)
            if abs(change - 0.05) < 2e-7:
                slew_contacts += 1
        independent_objective = sum(value * value for value in residual) + 1e-5 * sum(
            (command[channel] - trim[channel]) ** 2 for channel in range(4))
        assert math.isclose(objective, independent_objective, abs_tol=1e-7)
        previous_expected = command
        norm = math.sqrt(sum(value * value for value in residual))
        if step == 0:
            results.append(dict(corner=corner, phase=phase, first_residual_norm=norm,
                                final_residual_norm=None, first_small_residual_step=None))
        current = results[-1]
        if current['first_small_residual_step'] is None and norm <= 0.0011:
            current['first_small_residual_step'] = step + 1
        if step == STEPS_PER_PHASE - 1:
            current['final_residual_norm'] = norm
            assert norm < 0.0001, 'Held request did not reach static allocator accuracy'
    assert row_count == 8 * len(PHASES) * STEPS_PER_PHASE, 'Incomplete sequence trace'
    assert len(results) == 8 * len(PHASES), 'Incomplete phase coverage'
    assert all(result['final_residual_norm'] is not None for result in results)
    assert all(result['first_small_residual_step'] is not None for result in results)
    assert slew_contacts > 0 and largest_step <= 0.0500002
    by_phase = {}
    for phase in PHASES:
        selected = [result for result in results if result['phase'] == phase]
        by_phase[phase] = dict(max_first_residual_norm=max(result['first_residual_norm'] for result in selected),
                               max_final_residual_norm=max(result['final_residual_norm'] for result in selected),
                               max_steps_to_small_residual=max(result['first_small_residual_step'] for result in selected))
    assert by_phase['reverse']['max_first_residual_norm'] > by_phase['hold']['max_first_residual_norm']
    return dict(rows=row_count, trajectories=8, phases=PHASES,
                steps_per_phase=STEPS_PER_PHASE, dt_s=DT_S,
                small_residual_threshold=0.0011, phase_summary=by_phase,
                slew_limit_contacts=slew_contacts, largest_command_step=largest_step)


def main():
    preparation.verify_receipt(SITL, 'generated-allocation-manifest.json', SITL_MANIFEST_SHA256)
    preparation.verify_receipt(GRID, 'grid-manifest.json', GRID_MANIFEST_SHA256)
    graph = json.loads((base.ROOT / 'docs/RESEARCH_CODE_GRAPH.json').read_text())
    checkpoint = {}
    for node in graph['nodes']:
        if 'sha256' in node:
            identity = base.digest(base.ROOT / node['path'])
            assert identity == node['sha256'], 'Stale graph: ' + node['path']
            checkpoint[node['path']] = identity
    origin = json.loads((SITL / 'generated-allocation-origin.json').read_text())
    source = SITL / 'px4-source-generated-allocation'
    owner = source / preparation.OWNER
    config = source / preparation.CONFIG
    fixture = config.with_name('FixtureConstants.hpp')
    generated = source / preparation.HEADER
    allocator = source / 'src/lib/research_stabilized_manual/CanardAllocator.cpp'
    assert base.digest(owner) == origin['patched_owner_sha256']
    assert base.digest(config) == origin['patched_config_sha256']
    assert base.digest(generated) == origin['generated_header_sha256']
    assert base.digest(allocator) == base.digest(
        base.ROOT / 'src/lib/research_stabilized_manual/CanardAllocator.cpp')
    assert 'c.allocation.regularization = 1e-5F;' in config.read_text()
    assert 'c.allocation.axis_weight = Vector3f(1.0F, 1.0F, 1.0F);' in config.read_text()
    compiled = json.loads((SITL / 'synthetic-allocation.json').read_text())
    demand = json.loads(DEMAND.read_text())
    assert base.digest(DEMAND) == base.digest(GRID / DEMAND.name)
    assert compiled['input_sha256'] == origin['synthetic_input_sha256']
    commands = json.loads((SITL / 'sitl-lockstep-build/compile_commands.json').read_text())
    owner_commands = [entry for entry in commands if entry['file'] == str(owner)]
    assert len(owner_commands) == 1
    out = Path(tempfile.mkdtemp(prefix='research-stabilized-allocation-', dir='/var/tmp'))
    (out / 'checkpoint.json').write_text(json.dumps(checkpoint, indent=2) + '\n')
    variant = out / 'generated-profile'
    variant.mkdir()
    for path in (config, fixture, generated):
        shutil.copy2(path, variant / path.name)
    for path in (PROBE, Path(__file__), DEMAND):
        shutil.copy2(path, out / path.name)
    shutil.copy2(SITL / 'synthetic-allocation.json', out / 'synthetic-allocation.json')
    executable = out / 'allocator-sequence-probe'
    command = binding.compile_args(owner_commands[0], variant, out / PROBE.name,
                                   allocator, executable)
    (out / 'compile-command.json').write_text(json.dumps(command, indent=2) + '\n')
    with (out / 'build.log').open('w') as log:
        subprocess.run(command, cwd=owner_commands[0]['directory'], stdout=log,
                       stderr=subprocess.STDOUT, check=True)
    with (out / 'sequence.csv').open('w') as log:
        subprocess.run([str(executable)], cwd=out, stdout=log,
                       stderr=subprocess.STDOUT, check=True)
    with (out / 'sequence.csv').open(newline='') as source_csv:
        result = audit(csv.DictReader(source_csv), compiled, demand)
    summary = dict(scope='private_cpp_synthetic_allocator_sequential_diagnostic',
                   accepted=False, vehicle_qualified=False, physical_test=False,
                   actuator_lag_qualified=False, closed_loop_qualified=False,
                   generated_sitl_manifest_sha256=SITL_MANIFEST_SHA256,
                   grid_parent_manifest_sha256=GRID_MANIFEST_SHA256,
                   generated_px4_sha256=base.digest(SITL / 'sitl-lockstep-build/bin/px4'),
                   synthetic_profile_sha256=compiled['input_sha256'],
                   probe_source_sha256=base.digest(PROBE),
                   allocator_source_sha256=base.digest(allocator),
                   graph_anchors=len(checkpoint), sequence=result)
    (out / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    files = {str(path.relative_to(out)): base.digest(path) for path in out.rglob('*')
             if path.is_file() and path.name != 'sequence-manifest.json'}
    (out / 'sequence-manifest.json').write_text(json.dumps(dict(
        scope=summary['scope'], accepted=False, vehicle_qualified=False, files=files), indent=2) + '\n')
    print(json.dumps(dict(evidence=str(out),
                          manifest_sha256=base.digest(out / 'sequence-manifest.json'),
                          files=len(files), graph_anchors=len(checkpoint), sequence=result), indent=2), flush=True)


if __name__ == '__main__':
    main()
