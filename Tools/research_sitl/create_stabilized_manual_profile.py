#!/usr/bin/env python3
"""Create a console profile from an exact accepted synthetic qualification."""
import argparse
import json
from pathlib import Path

import stabilized_manual_console as console
import verify_stabilized_sitl as base


def create(evidence, receipt_sha256, output=None):
    evidence = evidence.resolve()
    assert evidence.parent == Path('/var/tmp') and evidence.name.startswith('research-stabilized-sitl-')
    receipt = evidence / 'revision-qualification-manifest.json'
    assert base.digest(receipt) == receipt_sha256, 'Qualification receipt changed'
    manifest = json.loads(receipt.read_text())
    assert manifest['accepted'] and not manifest['physical_authority_granted']
    assert manifest['fresh_cases'] + manifest['inherited_cases'] == 31
    summary = json.loads((evidence / 'revision-qualification-summary.json').read_text())
    assert summary['accepted'] and summary['total_cases'] == 31
    profile = dict(schema_version=1, mode='slaaps_synthetic_stabilized_manual',
                   pilot_semantics='body_rate_with_quaternion_hold_on_release', axes_frame='body_frd',
                   fixture_airflow_m_s=20.0, fixed_simulated_edf_thrust_n=10.0,
                   physical_authority_granted=False, qualification_evidence=str(evidence),
                   manifest_sha256=receipt_sha256, px4_sha256=summary['variant_px4_sha256'],
                   plugin_sha256=summary['plugin_sha256'])
    target = output.resolve() if output else evidence / 'qualified-console-profile.json'
    content = json.dumps(profile, indent=2) + '\n'
    if target.exists():
        assert target.read_text() == content, 'Refuse to overwrite a different profile'
    else:
        with target.open('x') as stream:
            stream.write(content)
    # This rehashes every qualification artifact and checks the actual ELF,
    # plugin and current fixture inputs. It does not start a native session.
    console.qualified_profile(target)
    print('Admitted synthetic console profile:', target, flush=True)
    return target


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--qualification', type=Path, required=True)
    parser.add_argument('--receipt-sha256', required=True)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    create(args.qualification, args.receipt_sha256, args.output)
