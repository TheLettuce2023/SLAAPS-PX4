#!/usr/bin/env python3
"""Prove the private C++ configuration and allocator consume header values."""
import json
import math
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile

import prepare_stabilized_generated_allocation as preparation
import verify_stabilized_sitl as base


SITL = Path('/var/tmp/research-stabilized-sitl-oou0sind')
SITL_MANIFEST_SHA256 = 'cf2068502959c952a5d4ec858ebe4aa905ebba7fc771c4016313807dce53d3be'
PROBE = Path(__file__).with_name('probe_stabilized_allocation_binding.cpp')
HEADER = 'GeneratedAllocationProfile.hpp'


def replace_once(content, old, new):
    assert content.count(old) == 1, 'Unexpected sealed header shape: ' + old
    return content.replace(old, new)


def near(actual, expected):
    return math.isclose(actual, expected, rel_tol=0.0, abs_tol=1e-6)


def compile_args(entry, variant, probe, allocator, output):
    source = Path(entry['file'])
    command = shlex.split(entry['command'])
    flags = []
    index = 1
    while index < len(command):
        token = command[index]
        if token == '-o':
            index += 2
            continue
        if token == '-c' or token == str(source):
            index += 1
            continue
        flags.append(token)
        index += 1
    return [command[0], '-I' + str(variant), *flags, str(probe), str(allocator), '-o', str(output)]


def compile_and_run(entry, variant, out, allocator):
    name = variant.name
    executable = out / (name + '-probe')
    command = compile_args(entry, variant, out / PROBE.name, allocator, executable)
    (out / (name + '-compile-command.json')).write_text(json.dumps(command, indent=2) + '\n')
    with (out / (name + '-build.log')).open('w') as log:
        subprocess.run(command, cwd=entry['directory'], stdout=log,
                       stderr=subprocess.STDOUT, check=True)
    with (out / (name + '-output.json')).open('w') as log:
        subprocess.run([str(executable)], cwd=out, stdout=log,
                       stderr=subprocess.STDOUT, check=True)
    return json.loads((out / (name + '-output.json')).read_text())


def main():
    preparation.verify_receipt(SITL, 'generated-allocation-manifest.json', SITL_MANIFEST_SHA256)
    graph = json.loads((base.ROOT / 'docs/RESEARCH_CODE_GRAPH.json').read_text())
    checkpoint = {}
    for node in graph['nodes']:
        if 'sha256' in node:
            identity = base.digest(base.ROOT / node['path'])
            assert identity == node['sha256'], 'Stale graph: ' + node['path']
            checkpoint[node['path']] = identity
    origin = json.loads((SITL / 'generated-allocation-origin.json').read_text())
    assert origin['scope'] == 'private_synthetic_generated_allocation_variant'
    assert not origin['vehicle_qualified'] and not origin['accepted_campaign_expansion']
    source = SITL / 'px4-source-generated-allocation'
    owner = source / preparation.OWNER
    config = source / preparation.CONFIG
    fixture = config.with_name('FixtureConstants.hpp')
    generated = source / preparation.HEADER
    for path, key in ((owner, 'patched_owner_sha256'), (config, 'patched_config_sha256'),
                      (generated, 'generated_header_sha256')):
        assert base.digest(path) == origin[key]
    assert base.digest(generated) == base.digest(SITL / HEADER)
    allocator = source / 'src/lib/research_stabilized_manual/CanardAllocator.cpp'
    assert base.digest(allocator) == base.digest(base.ROOT /
                                                 'src/lib/research_stabilized_manual/CanardAllocator.cpp')
    commands = json.loads((SITL / 'sitl-lockstep-build/compile_commands.json').read_text())
    owner_commands = [entry for entry in commands if entry['file'] == str(owner)]
    assert len(owner_commands) == 1
    out = Path(tempfile.mkdtemp(prefix='research-stabilized-allocation-', dir='/var/tmp'))
    (out / 'checkpoint.json').write_text(json.dumps(checkpoint, indent=2) + '\n')
    shutil.copy2(PROBE, out / PROBE.name)
    shutil.copy2(Path(__file__), out / Path(__file__).name)
    baseline = out / 'baseline'
    baseline.mkdir()
    for path in (config, fixture, generated):
        shutil.copy2(path, baseline / path.name)
    original_header = (baseline / HEADER).read_text()
    expected_sha = origin['synthetic_input_sha256']
    assert f'kInputSha256[] = "{expected_sha}"' in original_header
    variations = {
        'effectiveness-only': ('\t{0.25F, -0.25F, 0.25F, -0.25F},',
                               '\t{0.35F, -0.25F, 0.25F, -0.25F},'),
        'trim-only': ('kTrim[4] = {0.0F', 'kTrim[4] = {0.1F'),
        'minimum-only': ('kMinimum[4] = {-1.0F', 'kMinimum[4] = {-0.8F'),
        'maximum-only': ('kMaximum[4] = {1.0F', 'kMaximum[4] = {0.7F'),
        'positive-slew-only': ('kPositiveSlewPerS[4] = {5.0F',
                               'kPositiveSlewPerS[4] = {4.0F'),
        'negative-slew-only': ('kNegativeSlewPerS[4] = {5.0F',
                               'kNegativeSlewPerS[4] = {6.0F')
    }
    variants = [baseline]
    for name, (old, new) in variations.items():
        variant = out / name
        shutil.copytree(baseline, variant)
        changed = replace_once(original_header, f'kInputSha256[] = "{expected_sha}"',
                               f'kInputSha256[] = "DIAGNOSTIC_{name.upper().replace("-", "_")}"')
        (variant / HEADER).write_text(replace_once(changed, old, new))
        variants.append(variant)
    observations = {variant.name: compile_and_run(owner_commands[0], variant, out, allocator)
                    for variant in variants}
    normal = observations['baseline']
    assert normal['profile'] == expected_sha
    assert all(near(normal[field], value) for field, value in (
        ('a00', 0.25), ('trim0', 0), ('minimum0', -1), ('maximum0', 1),
        ('positive_slew0', 5), ('negative_slew0', 5)))
    field_expectations = {
        'effectiveness-only': ('a00', 0.35),
        'trim-only': ('trim0', 0.1),
        'minimum-only': ('minimum0', -0.8),
        'maximum-only': ('maximum0', 0.7),
        'positive-slew-only': ('positive_slew0', 4),
        'negative-slew-only': ('negative_slew0', 6)
    }
    for name, (field, expected) in field_expectations.items():
        result = observations[name]
        assert result['profile'] == 'DIAGNOSTIC_' + name.upper().replace('-', '_')
        assert near(result[field], expected), name + ': header value not in C++ configuration'
    command_delta = lambda name: max(abs(a - b) for a, b in
                                     zip(normal['commands'], observations[name]['commands']))
    deltas = {
        'effectiveness-only': command_delta('effectiveness-only'),
        'trim-only': command_delta('trim-only'),
        'minimum-only': abs(normal['negative_bound_u0'] - observations['minimum-only']['negative_bound_u0']),
        'maximum-only': abs(normal['positive_bound_u0'] - observations['maximum-only']['positive_bound_u0']),
        'positive-slew-only': abs(normal['positive_slew_u0'] - observations['positive-slew-only']['positive_slew_u0']),
        'negative-slew-only': abs(normal['negative_slew_u0'] - observations['negative-slew-only']['negative_slew_u0'])
    }
    assert all(value > 0.001 for value in deltas.values()), 'Diagnostic input did not change allocation'
    summary = dict(scope='private_cpp_generated_allocation_data_flow_diagnostic',
                   accepted=False, vehicle_qualified=False, physical_test=False,
                   parent_sitl_manifest_sha256=SITL_MANIFEST_SHA256,
                   parent_sitl_px4_sha256=base.digest(SITL / 'sitl-lockstep-build/bin/px4'),
                   baseline_header_sha256=base.digest(generated), graph_anchors=len(checkpoint),
                   values=observations, targeted_command_delta=deltas)
    (out / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    files = {}
    for path in out.rglob('*'):
        if path.is_file() and path.name != 'binding-manifest.json':
            files[str(path.relative_to(out))] = base.digest(path)
    (out / 'binding-manifest.json').write_text(json.dumps(dict(
        scope=summary['scope'], accepted=False, vehicle_qualified=False, files=files), indent=2) + '\n')
    print(json.dumps(dict(evidence=str(out),
                          manifest_sha256=base.digest(out / 'binding-manifest.json'),
                          files=len(files), graph_anchors=len(checkpoint),
                          targeted_command_delta=deltas), indent=2), flush=True)


if __name__ == '__main__':
    main()
