#!/usr/bin/env python3
"""Build a private synthetic steady-moment-bias Gazebo plugin for native SITL."""
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

import prepare_stabilized_generated_allocation as preparation
import verify_stabilized_allocation_binding as binding
import verify_stabilized_sitl as base


PARENT = binding.SITL
PARENT_MANIFEST_SHA256 = binding.SITL_MANIFEST_SHA256
BIAS_NM_FRD = (0.01, -0.015, 0.012)
PLUGIN_SOURCE = Path('Tools/research_sitl/stabilized_manual/RotationalFixture.cpp')


def replace_once(text, old, new):
    assert text.count(old) == 1, 'Unexpected fixture source shape: ' + old
    return text.replace(old, new)


def main():
    preparation.verify_receipt(PARENT, 'generated-allocation-manifest.json', PARENT_MANIFEST_SHA256)
    graph = json.loads((base.ROOT / 'docs/RESEARCH_CODE_GRAPH.json').read_text())
    checkpoint = {}
    for node in graph['nodes']:
        if 'sha256' in node:
            identity = base.digest(base.ROOT / node['path'])
            assert identity == node['sha256'], 'Stale graph: ' + node['path']
            checkpoint[node['path']] = identity
    parent_origin = json.loads((PARENT / 'generated-allocation-origin.json').read_text())
    assert parent_origin['scope'] == 'private_synthetic_generated_allocation_variant'
    assert parent_origin['vehicle_qualified'] is False
    assert base.digest(PARENT / 'sitl-lockstep-build/bin/px4') == parent_origin['generated_px4_sha256']
    for path in base.FIXTURE.rglob('*'):
        if path.is_file():
            assert base.digest(path) == base.digest(PARENT / 'px4-source' / path.relative_to(base.ROOT))
    out = Path(tempfile.mkdtemp(prefix='research-stabilized-sitl-', dir='/var/tmp'))
    (out / 'checkpoint.json').write_text(json.dumps(checkpoint, indent=2) + '\n')
    shutil.copy2(Path(__file__), out / Path(__file__).name)
    # The unmodified source tree is for the existing runner's baseline source
    # comparison. Only the separately recorded bias-source builds the plugin.
    baseline_fixture = out / 'px4-source' / base.FIXTURE.relative_to(base.ROOT)
    baseline_fixture.parent.mkdir(parents=True)
    shutil.copytree(base.FIXTURE, baseline_fixture)
    bias_source = out / 'bias-source'
    bias_fixture = bias_source / base.FIXTURE.relative_to(base.ROOT)
    bias_fixture.parent.mkdir(parents=True)
    shutil.copytree(base.FIXTURE, bias_fixture)
    actuator = Path('src/lib/research_actuator_command')
    (bias_source / actuator).parent.mkdir(parents=True)
    shutil.copytree(base.ROOT / actuator, bias_source / actuator)
    source = (bias_fixture / PLUGIN_SOURCE.name).read_text()
    source = replace_once(source, 'namespace research_stabilized_fixture\n{',
                          'namespace research_stabilized_fixture\n{\n'
                          'constexpr double kSyntheticEnabledBiasNm[3] = {0.01, -0.015, 0.012};')
    source = replace_once(source, 't0,t1,t2,t3,a0,a1,a2,a3\\n',
                          't0,t1,t2,t3,a0,a1,a2,a3,bias_x,bias_y,bias_z\\n')
    source = replace_once(source, 'moment[axis] -= kDampingNmS[axis] * rate[axis];',
                          'moment[axis] -= kDampingNmS[axis] * rate[axis];\n'
                          '\t\t\tmoment[axis] += _enabled ? kSyntheticEnabledBiasNm[axis] : 0.0;')
    source = replace_once(source,
                          "for (double surface : _surface) { _log << ',' << surface *kMaximumDeflectionRad; }",
                          "for (double surface : _surface) { _log << ',' << surface *kMaximumDeflectionRad; }\n"
                          "\t\tfor (double bias : kSyntheticEnabledBiasNm) { _log << ',' << (_enabled ? bias : 0.0); }")
    (bias_fixture / PLUGIN_SOURCE.name).write_text(source)
    native = out / 'sitl-lockstep-build'
    native.symlink_to(PARENT / 'sitl-lockstep-build', target_is_directory=True)
    plugin = out / 'plugin-build'
    with (out / 'plugin-configure.log').open('w') as log:
        subprocess.run(['cmake', '-S', str(bias_fixture), '-B', str(plugin), '-G', 'Ninja',
                        '-DCMAKE_BUILD_TYPE=RelWithDebInfo'], stdout=log,
                       stderr=subprocess.STDOUT, check=True)
    with (out / 'plugin-build.log').open('w') as log:
        subprocess.run(['cmake', '--build', str(plugin), '-j', '4'], stdout=log,
                       stderr=subprocess.STDOUT, check=True)
    binary = plugin / 'libresearch_rotational_fixture.so'
    assert binary.exists() and base.digest(binary) != parent_origin['plugin_sha256']
    origin = dict(scope='private_synthetic_enabled_moment_bias_variant',
                  accepted=False, vehicle_qualified=False, physical_test=False,
                  parent_sitl_manifest_sha256=PARENT_MANIFEST_SHA256,
                  parent_generated_px4_sha256=parent_origin['generated_px4_sha256'],
                  parent_plugin_sha256=parent_origin['plugin_sha256'],
                  generated_profile_sha256=parent_origin['synthetic_input_sha256'],
                  original_fixture_sha256=base.digest(base.ROOT / PLUGIN_SOURCE),
                  patched_fixture_sha256=base.digest(bias_fixture / PLUGIN_SOURCE.name),
                  bias_nm_frd=BIAS_NM_FRD, bias_active_only_with_simulated_thrust=True,
                  plugin_sha256=base.digest(binary), graph_anchors=len(checkpoint))
    (out / 'bias-origin.json').write_text(json.dumps(origin, indent=2) + '\n')
    print(json.dumps(dict(evidence=str(out), origin=origin), indent=2), flush=True)


if __name__ == '__main__':
    main()
