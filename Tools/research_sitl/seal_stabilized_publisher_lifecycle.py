#!/usr/bin/env python3
"""Seal real EKF2 lifecycle and state-barrier tests on the exact revision binary."""
import argparse
import json
from pathlib import Path
import shutil

import prepare_stabilized_publisher_lifecycle as prepare
import verify_stabilized_publisher_lifecycle as lifecycle
import verify_stabilized_publisher_epoch as epoch
import verify_stabilized_sitl as base
import verify_stabilized_state_barriers as barriers


def seal(evidence):
    record = epoch.origin(evidence)
    origin = json.loads((evidence / 'native-origin.json').read_text())
    assert origin['parent'] == str(prepare.PARENT) and origin['parent_manifest_sha256'] == prepare.PARENT_SHA256
    assert base.digest(prepare.PARENT / prepare.PARENT_RECEIPT) == prepare.PARENT_SHA256
    parent_manifest = json.loads((prepare.PARENT / prepare.PARENT_RECEIPT).read_text())
    for name, identity in parent_manifest['files'].items():
        assert base.digest(prepare.PARENT / name) == identity, 'Parent artifact changed: ' + name
    assert origin['px4_sha256'] == record['variant_px4_sha256']
    assert origin['plugin_sha256'] == record['plugin_sha256']
    dependencies = json.loads((evidence / 'compiled-dependencies.json').read_text())
    assert base.digest(evidence / 'compiled-dependencies.json') == parent_manifest['files']['compiled-dependencies.json']
    for name, identity in dependencies.items():
        assert base.digest(evidence / 'inherited-compiled-inputs' / name) == identity
        if name in record['patched_sources']:
            assert identity == record['patched_sources'][name]
        else:
            assert base.digest(base.ROOT / name) == identity, 'Current compiled dependency changed: ' + name
    for name, identity in record['patched_sources'].items():
        assert base.digest(evidence / 'variant-inputs' / name) == identity
    for name, identity in record['support_sources'].items():
        assert base.digest(evidence / 'variant-inputs' / name) == identity
    for name in ('sitl-lockstep-build/boardconfig', 'sitl-lockstep-build/compile_commands.json'):
        assert base.digest(evidence / name) == parent_manifest['files'][name]
    chosen = {}
    for path in sorted(evidence.glob('barrier-*/result.json'), key=lambda path: path.stat().st_mtime_ns):
        result = json.loads(path.read_text())
        chosen[(result['barrier_case'], result['nose_up'])] = (path.parent, result)
    assert set(chosen) == {(case, up) for case in barriers.CASES for up in (False, True)}
    for work, result in chosen.values():
        assert result['passed'] and result['lifecycle_verified']
        assert result['lifecycle_runner_sha256'] == base.digest(Path(lifecycle.__file__))
        assert result['lifecycle_runner_sha256'] == base.digest(work / Path(lifecycle.__file__).name)
        derived = lifecycle.inspect(work, result)
        assert all(result[key] == list(value) if isinstance(value, tuple) else result[key] == value
                   for key, value in derived.items())
    # Reuse the stock closed-cycle/runtime/fault/recovery audit. Exact compiled
    # provenance is inherited above; the child did not recompile stock sources.
    barriers.seal(evidence)
    barrier_summary = json.loads((evidence / 'state-barrier-summary.json').read_text())
    summary = dict(scope='eight_actual_ekf2_and_state_barrier_cases_on_private_revision_binary',
                   passed=True, audited_control_cycles=barrier_summary['audited_control_cycles'],
                   actual_ekf2_stop_restart_cases=4, heading_reset_cases=2, unsupported_profile_cases=2,
                   same_instance_same_device_ids=True, restart_entirely_between_owner_polls_qualified=False,
                   general_publisher_identity_qualified=False, accepted_campaign_expansion=False,
                   vehicle_qualification=False, inherited_compiled_dependency_count=len(dependencies),
                   cases=[dict(directory=work.name, **result) for work, result in chosen.values()])
    (evidence / 'publisher-lifecycle-summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    for path in (Path(__file__), Path(lifecycle.__file__)):
        shutil.copy2(path, evidence / path.name)
    files = {}
    for path in evidence.iterdir():
        if path.is_file() and path.name != 'publisher-lifecycle-manifest.json':
            files[path.name] = base.digest(path)
        elif path.is_dir() and path.name.startswith(('barrier-', 'tested-artifacts', 'variant-inputs',
                                                    'inherited-compiled-inputs', 'audited-inputs')):
            for artifact in path.rglob('*'):
                if artifact.is_file() and not artifact.is_symlink():
                    files[str(artifact.relative_to(evidence))] = base.digest(artifact)
    # Regular runtime alias files are archived too; module symlinks target the
    # same sealed ELF and remain in the bin directory for reproduction.
    for path in (evidence / 'sitl-lockstep-build/bin').iterdir():
        if path.is_file() and not path.is_symlink():
            files[str(path.relative_to(evidence))] = base.digest(path)
    for name in ('sitl-lockstep-build/boardconfig', 'sitl-lockstep-build/compile_commands.json',
                 'plugin-build/libresearch_rotational_fixture.so'):
        files[name] = base.digest(evidence / name)
    manifest = dict(scope=summary['scope'], passed=True, physical_authority_granted=False,
                    preserved_anchor_count=origin['preparation_anchors'],
                    parent_manifest_sha256=prepare.PARENT_SHA256, files=files)
    (evidence / 'publisher-lifecycle-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print('Sealed actual lifecycle evidence:', len(files), 'artifacts;', summary['audited_control_cycles'],
          'cycles; manifest', base.digest(evidence / 'publisher-lifecycle-manifest.json'), flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--evidence', type=Path, required=True)
    args = parser.parse_args()
    evidence = args.evidence.resolve()
    assert evidence.parent == Path('/var/tmp') and evidence.name.startswith('research-stabilized-sitl-')
    seal(evidence)
