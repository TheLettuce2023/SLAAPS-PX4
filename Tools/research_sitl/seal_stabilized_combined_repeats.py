#!/usr/bin/env python3
"""Audit eight near-limit combined-timing repeats at two Gazebo noise seeds."""
import argparse
from collections import Counter
import json
from pathlib import Path
import shutil

import seal_stabilized_combined_timing as combined
import verify_stabilized_campaigns as campaigns
import verify_stabilized_sitl as base


EXPECTED = Counter({(seed, nose_up, sign): 1 for seed in (43, 44)
                    for nose_up in (False, True) for sign in (-1, 1)})
REFERENCE = Path('/var/tmp/research-stabilized-sitl-yoxu1tex')
REFERENCE_MANIFEST_SHA256 = '913dd4b29e79a83bcf2c6109f5efd05ed3e98fe1308a7b943acaf1b7c8a0a6f3'


def sensor_signature(work):
    samples = campaigns.rows(work / 'truth.csv.mag.csv')
    assert len(samples) > 100
    return tuple((row['flu_x_t'], row['flu_y_t'], row['flu_z_t']) for row in samples[:64])


def seal(evidence):
    paths = sorted(evidence.glob('mixed-*/result.json'))
    assert len(paths) == sum(EXPECTED.values()), 'Require exact seed/pose/order matrix'
    results = [(path.parent, json.loads(path.read_text())) for path in paths]
    assert Counter((result['seed'], result['nose_up'], result['first_sign'])
                   for _, result in results) == EXPECTED
    checkpoint = json.loads((evidence / 'checkpoint.json').read_text())
    for name, identity in checkpoint.items():
        current = base.digest(base.ROOT / name)
        if name == 'Tools/research_sitl/seal_stabilized_combined_timing.py':
            assert current != identity, 'Seed-aware audit extension was not source-bound'
        else:
            assert current == identity, 'Unrelated graph anchor changed: ' + name

    origin = json.loads((evidence / 'native-origin.json').read_text())
    parent = Path(origin['parent'])
    for name, identity in origin['parent_receipts'].items():
        assert base.digest(parent / name) == identity
    assert origin['px4_sha256'] == base.digest(parent / 'sitl-lockstep-build/bin/px4')
    assert origin['plugin_sha256'] == base.digest(parent / 'plugin-build/libresearch_rotational_fixture.so')
    assert origin['px4_sha256'] == base.digest(evidence / 'sitl-lockstep-build/bin/px4')
    assert origin['plugin_sha256'] == base.digest(evidence / 'plugin-build/libresearch_rotational_fixture.so')
    reference_manifest = REFERENCE / 'combined-timing-manifest.json'
    assert base.digest(reference_manifest) == REFERENCE_MANIFEST_SHA256
    for name, identity in json.loads(reference_manifest.read_text())['files'].items():
        assert base.digest(REFERENCE / name) == identity
    for path in base.FIXTURE.rglob('*'):
        if path.is_file():
            relative = path.relative_to(base.ROOT)
            assert base.digest(path) == base.digest(evidence / 'px4-source' / relative)

    cases = []
    signatures = {}
    for work, result in results:
        case = combined.inspect(work, result, evidence, expected_seed=result['seed'])
        case['seed'] = result['seed']
        cases.append(case)
        signatures[(result['seed'], result['nose_up'], result['first_sign'])] = sensor_signature(work)
    for nose_up in (False, True):
        for sign in (-1, 1):
            assert signatures[(43, nose_up, sign)] != signatures[(44, nose_up, sign)], \
                'Different declared Gazebo seeds produced identical initial magnetic samples'

    (evidence / 'combined-repeats-summary.json').write_text(json.dumps(dict(
        scope='eight_exact_combined_80ms_manual_80ms_imu_cases_seeds_43_44',
        accepted_campaign_expansion=False, vehicle_qualification=False,
        seed_42_reference_manifest_sha256=REFERENCE_MANIFEST_SHA256,
        audited_control_cycles=sum(case['cycles'] for case in cases),
        distinct_initial_magnetic_noise_by_seed=True, cases=cases), indent=2) + '\n')
    shutil.copy2(Path(__file__), evidence / Path(__file__).name)
    files = {}
    for path in evidence.iterdir():
        if path.is_file() and path.name != 'combined-repeats-manifest.json':
            files[path.name] = base.digest(path)
        elif path.is_dir() and path.name.startswith(('mixed-', 'tested-artifacts', 'px4-source')):
            for artifact in path.rglob('*'):
                if artifact.is_file() and not artifact.is_symlink():
                    files[str(artifact.relative_to(evidence))] = base.digest(artifact)
    for name in ('sitl-lockstep-build/bin/px4', 'sitl-lockstep-build/boardconfig',
                 'sitl-lockstep-build/compile_commands.json', 'plugin-build/libresearch_rotational_fixture.so'):
        files[name] = base.digest(evidence / name)
    (evidence / 'combined-repeats-manifest.json').write_text(json.dumps(dict(
        scope='eight_source_bound_exploratory_combined_timing_seed_repeats', accepted=False,
        vehicle_qualification=False, observed_checks_pass=True,
        preserved_preparation_anchor_count=len(checkpoint), files=files), indent=2) + '\n')
    print(json.dumps(dict(manifest_sha256=base.digest(evidence / 'combined-repeats-manifest.json'),
                          files=len(files), cycles=sum(case['cycles'] for case in cases),
                          preserved_preparation_anchors=len(checkpoint)), indent=2), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--evidence', type=Path, required=True)
    args = parser.parse_args()
    evidence = args.evidence.resolve()
    assert evidence.parent == Path('/var/tmp') and evidence.name.startswith('research-stabilized-sitl-')
    seal(evidence)


if __name__ == '__main__':
    main()
