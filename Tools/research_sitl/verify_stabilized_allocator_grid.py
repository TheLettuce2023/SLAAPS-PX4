#!/usr/bin/env python3
"""Compile and audit the private generated-profile allocator on a finite box grid."""
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
STATIC = Path('/var/tmp/research-stabilized-allocation-x42ielic')
STATIC_MANIFEST_SHA256 = '913cb7b27308dfa2a158e6a05b53ed1a08dd0a48b83f1c332e7f9ba981169f6a'
PROBE = Path(__file__).with_name('probe_stabilized_allocator_grid.cpp')
DEMAND = Path(__file__).with_name('stabilized_static_demand_synthetic.json')


def audit(rows, compiled, demand):
    values = compiled['allocator_inputs']
    matrix = values['effectiveness']
    trim = values['trim']
    expected_indices = {(mode, ix, iy, iz)
                        for mode in ('static', 'slew')
                        for ix in range(-5, 6)
                        for iy in range(-5, 6)
                        for iz in range(-5, 6)}
    seen = set()
    maximum = {'static': dict(residual_norm=0.0, command_abs=0.0),
               'slew': dict(residual_norm=0.0, command_abs=0.0)}
    slew_limited = 0
    for row in rows:
        mode = row['mode']
        indices = tuple(int(row[axis]) for axis in ('ix', 'iy', 'iz'))
        key = (mode, *indices)
        assert key in expected_indices and key not in seen, 'Grid coverage or duplicate'
        seen.add(key)
        assert row['profile'] == compiled['input_sha256'] == demand['profile_input_sha256']
        request = [float(row[f'request_{axis}']) for axis in 'xyz']
        command = [float(row[f'u{channel}']) for channel in range(4)]
        achieved = [float(row[f'achieved_{axis}']) for axis in 'xyz']
        residual = [float(row[f'residual_{axis}']) for axis in 'xyz']
        objective = float(row['objective'])
        assert all(math.isfinite(value) for value in request + command + achieved + residual)
        assert math.isfinite(objective) and objective >= 0.0
        for axis in range(3):
            assert math.isclose(request[axis], indices[axis] * 0.02, abs_tol=2e-8)
            assert abs(request[axis]) <= demand['axis_abs_bound'][axis] + 2e-8
            reconstructed = sum(matrix[axis][channel] * (command[channel] - trim[channel])
                                for channel in range(4))
            assert math.isclose(achieved[axis], reconstructed, abs_tol=5e-8)
            assert math.isclose(residual[axis], request[axis] - reconstructed, abs_tol=5e-8)
        dt_s = 1.0 if mode == 'static' else 0.01
        for channel in range(4):
            lower = max(values['minimum'][channel],
                        trim[channel] - dt_s * values['negative_slew_per_s'][channel])
            upper = min(values['maximum'][channel],
                        trim[channel] + dt_s * values['positive_slew_per_s'][channel])
            assert lower - 2e-7 <= command[channel] <= upper + 2e-7
            maximum[mode]['command_abs'] = max(maximum[mode]['command_abs'], abs(command[channel]))
            if mode == 'slew' and abs(abs(command[channel]) - 0.05) < 2e-7:
                slew_limited += 1
        objective_independent = sum(value * value for value in residual) + 1e-5 * sum(
            (command[channel] - trim[channel]) ** 2 for channel in range(4))
        assert math.isclose(objective, objective_independent, abs_tol=1e-7)
        norm = math.sqrt(sum(value * value for value in residual))
        if norm > maximum[mode]['residual_norm']:
            maximum[mode]['residual_norm'] = norm
            maximum[mode]['worst_request'] = request
    assert seen == expected_indices
    assert maximum['static']['residual_norm'] < 0.0011
    assert maximum['slew']['residual_norm'] > maximum['static']['residual_norm']
    assert slew_limited > 0
    return dict(cases=len(seen), cases_per_mode=len(seen) // 2,
                maximum=maximum, slew_limit_contacts=slew_limited)


def main():
    preparation.verify_receipt(SITL, 'generated-allocation-manifest.json', SITL_MANIFEST_SHA256)
    preparation.verify_receipt(STATIC, 'static-authority-manifest.json', STATIC_MANIFEST_SHA256)
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
    assert base.digest(DEMAND) == base.digest(STATIC / DEMAND.name)
    assert compiled['input_sha256'] == origin['synthetic_input_sha256']
    assert demand['axis_abs_bound'] == [0.1, 0.1, 0.1]
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
    executable = out / 'allocator-grid-probe'
    command = binding.compile_args(owner_commands[0], variant, out / PROBE.name,
                                   allocator, executable)
    (out / 'compile-command.json').write_text(json.dumps(command, indent=2) + '\n')
    with (out / 'build.log').open('w') as log:
        subprocess.run(command, cwd=owner_commands[0]['directory'], stdout=log,
                       stderr=subprocess.STDOUT, check=True)
    with (out / 'grid.csv').open('w') as log:
        subprocess.run([str(executable)], cwd=out, stdout=log,
                       stderr=subprocess.STDOUT, check=True)
    with (out / 'grid.csv').open(newline='') as source_csv:
        result = audit(csv.DictReader(source_csv), compiled, demand)
    summary = dict(scope='private_cpp_synthetic_allocator_finite_grid_diagnostic',
                   accepted=False, vehicle_qualified=False, physical_test=False,
                   continuous_tracking_qualified=False, closed_loop_qualified=False,
                   generated_sitl_manifest_sha256=SITL_MANIFEST_SHA256,
                   static_screen_manifest_sha256=STATIC_MANIFEST_SHA256,
                   generated_px4_sha256=base.digest(SITL / 'sitl-lockstep-build/bin/px4'),
                   synthetic_profile_sha256=compiled['input_sha256'],
                   probe_source_sha256=base.digest(PROBE),
                   allocator_source_sha256=base.digest(allocator),
                   graph_anchors=len(checkpoint), grid=result)
    (out / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    files = {str(path.relative_to(out)): base.digest(path) for path in out.rglob('*')
             if path.is_file() and path.name != 'grid-manifest.json'}
    (out / 'grid-manifest.json').write_text(json.dumps(dict(
        scope=summary['scope'], accepted=False, vehicle_qualified=False, files=files), indent=2) + '\n')
    print(json.dumps(dict(evidence=str(out),
                          manifest_sha256=base.digest(out / 'grid-manifest.json'),
                          files=len(files), graph_anchors=len(checkpoint), grid=result), indent=2), flush=True)


if __name__ == '__main__':
    main()
