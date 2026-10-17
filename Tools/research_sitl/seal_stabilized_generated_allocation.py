#!/usr/bin/env python3
"""Independently seal targeted generated-allocation SITL evidence."""
import json
from pathlib import Path
import shutil

import generate_stabilized_allocation_header as header_generator
import prepare_stabilized_generated_allocation as preparation
import seal_stabilized_mixed as mixed_audit
import verify_stabilized_generated_allocation as generated
import verify_stabilized_sitl as base


def seal(evidence):
    origin_path = evidence / 'generated-allocation-origin.json'
    origin = json.loads(origin_path.read_text())
    assert origin['scope'] == 'private_synthetic_generated_allocation_variant'
    assert origin['vehicle_qualified'] is False and origin['accepted_campaign_expansion'] is False
    preparation.verify_receipt(preparation.ACCEPTED, 'campaign-manifest.json',
                               preparation.ACCEPTED_MANIFEST_SHA256)
    preparation.verify_receipt(preparation.ALLOCATION, 'allocation-manifest.json',
                               preparation.ALLOCATION_MANIFEST_SHA256)
    source = evidence / 'px4-source-generated-allocation'
    identities = ((preparation.PATCH, 'patch_sha256'), (preparation.GENERATOR, 'generator_sha256'),
                  (source / preparation.OWNER, 'patched_owner_sha256'),
                  (source / preparation.CONFIG, 'patched_config_sha256'),
                  (source / preparation.HEADER, 'generated_header_sha256'),
                  (evidence / 'sitl-lockstep-build/bin/px4', 'generated_px4_sha256'),
                  (evidence / 'plugin-build/libresearch_rotational_fixture.so', 'plugin_sha256'),
                  (evidence / 'synthetic-allocation.json', 'compiled_report_sha256'),
                  (evidence / 'stabilized_allocation_synthetic.json', 'synthetic_input_sha256'))
    for path, key in identities:
        assert base.digest(path) == origin[key], 'Variant identity mismatch: ' + key
    report = json.loads((evidence / 'synthetic-allocation.json').read_text())
    assert report['input_sha256'] == origin['synthetic_input_sha256']
    generated_text = header_generator.render(report, origin['synthetic_input_sha256'])
    assert (source / preparation.HEADER).read_text() == generated_text
    assert (evidence / 'GeneratedAllocationProfile.hpp').read_text() == generated_text
    assert origin['accepted_parent_px4_sha256'] == base.digest(
        preparation.ACCEPTED / 'sitl-lockstep-build/bin/px4')
    checkpoint = json.loads((evidence / 'checkpoint.json').read_text())
    for path, identity in checkpoint.items():
        assert base.digest(base.ROOT / path) == identity, 'Changed graph anchor: ' + path
    cases = {}
    for path in sorted(evidence.glob('mixed-*/result.json')):
        result = json.loads(path.read_text())
        key = result['nose_up'], result['first_sign']
        assert key not in cases
        assert result['generated_allocation'] and result['runtime_profile_sha_observed']
        assert result['profile_sha256'] == origin['synthetic_input_sha256']
        assert result['origin_sha256'] == base.digest(origin_path)
        assert result['generated_header_sha256'] == origin['generated_header_sha256']
        assert result['px4_sha256'] == origin['generated_px4_sha256']
        assert result['plugin_sha256'] == origin['plugin_sha256']
        assert base.digest(path.parent / Path(generated.__file__).name) == base.digest(Path(generated.__file__))
        for status in ('readiness.log', 'active-status.log'):
            assert 'profile_sha=' + origin['synthetic_input_sha256'] in (path.parent / status).read_text()
        cases[key] = mixed_audit.inspect(path.parent, result, evidence)
    assert set(cases) == {(False, -1), (True, 1)}, 'Require both declared targeted maneuvers'
    summary = dict(scope='two_exploratory_generated_synthetic_allocation_mixed_axis_cases',
                   vehicle_qualified=False, accepted_campaign_expansion=False,
                   generated_profile_sha256=origin['synthetic_input_sha256'],
                   generated_px4_sha256=origin['generated_px4_sha256'],
                   audited_control_cycles=sum(case['cycles'] for case in cases.values()),
                   cases=list(cases.values()))
    (evidence / 'generated-allocation-summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    shutil.copy2(Path(__file__), evidence / Path(__file__).name)
    files = {}
    for path in evidence.iterdir():
        if path.is_file() and path.name != 'generated-allocation-manifest.json':
            files[path.name] = base.digest(path)
        elif path.is_dir() and path.name.startswith(('mixed-', 'tested-artifacts')):
            for artifact in path.rglob('*'):
                if artifact.is_file() and not artifact.is_symlink():
                    files[str(artifact.relative_to(evidence))] = base.digest(artifact)
    for name in ('sitl-lockstep-build/bin/px4', 'sitl-lockstep-build/boardconfig',
                 'sitl-lockstep-build/compile_commands.json',
                 'plugin-build/libresearch_rotational_fixture.so',
                 str((source / preparation.OWNER).relative_to(evidence)),
                 str((source / preparation.CONFIG).relative_to(evidence)),
                 str((source / preparation.HEADER).relative_to(evidence))):
        files[name] = base.digest(evidence / name)
    for path in (base.FIXTURE).rglob('*'):
        if path.is_file():
            name = 'px4-source/' + str(path.relative_to(base.ROOT))
            files[name] = base.digest(evidence / name)
    (evidence / 'generated-allocation-manifest.json').write_text(json.dumps(dict(
        scope=summary['scope'], vehicle_qualified=False, accepted=False,
        preserved_anchor_count=len(checkpoint), files=files), indent=2) + '\n')
    print(json.dumps(dict(evidence=str(evidence),
                          manifest_sha256=base.digest(evidence / 'generated-allocation-manifest.json'),
                          cases=len(cases), cycles=summary['audited_control_cycles'],
                          files=len(files)), indent=2), flush=True)


if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--evidence', type=Path, required=True)
    args = parser.parse_args()
    evidence = args.evidence.resolve()
    assert evidence.parent == Path('/var/tmp') and evidence.name.startswith('research-stabilized-sitl-')
    seal(evidence)
