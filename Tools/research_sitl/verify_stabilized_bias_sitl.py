#!/usr/bin/env python3
"""Run an exploratory native maneuver with declared synthetic enabled torque bias."""
import argparse
import json
from pathlib import Path
import shutil

import prepare_stabilized_generated_allocation as preparation
import verify_stabilized_campaigns as campaigns
import verify_stabilized_mixed as mixed
import verify_stabilized_sitl as base


class BiasFixture(campaigns.Fixture):
    def __init__(self, evidence, *args, **kwargs):
        origin = json.loads((evidence / 'bias-origin.json').read_text())
        assert origin['scope'] == 'private_synthetic_enabled_moment_bias_variant'
        assert not origin['accepted'] and not origin['vehicle_qualified']
        assert origin['bias_active_only_with_simulated_thrust']
        assert base.digest(evidence / 'sitl-lockstep-build/bin/px4') == origin['parent_generated_px4_sha256']
        assert base.digest(evidence / 'plugin-build/libresearch_rotational_fixture.so') == origin['plugin_sha256']
        assert base.digest(evidence / 'bias-source/Tools/research_sitl/stabilized_manual/RotationalFixture.cpp') == origin[
            'patched_fixture_sha256']
        super().__init__(evidence, *args, **kwargs)
        self.profile_sha = origin['generated_profile_sha256']
        self.result.update(synthetic_enabled_bias_nm_frd=origin['bias_nm_frd'],
                           exploratory_bias_variant=True,
                           bias_origin_sha256=base.digest(evidence / 'bias-origin.json'),
                           patched_fixture_sha256=origin['patched_fixture_sha256'])
        shutil.copy2(Path(__file__), self.work / Path(__file__).name)

    def start(self):
        super().start()
        status = (self.work / 'readiness.log').read_text()
        assert 'profile_sha=' + self.profile_sha in status, 'Runtime owner profile hash mismatch'
        self.result['runtime_profile_sha_observed'] = True


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--evidence', type=Path, required=True)
    parser.add_argument('--nose-up', action='store_true')
    parser.add_argument('--first-sign', type=int, choices=(-1, 1), required=True)
    args = parser.parse_args()
    evidence = args.evidence.resolve()
    assert evidence.parent == Path('/var/tmp') and evidence.name.startswith('research-stabilized-sitl-')
    origin = json.loads((evidence / 'bias-origin.json').read_text())
    preparation.verify_receipt(Path('/var/tmp/research-stabilized-sitl-oou0sind'),
                               'generated-allocation-manifest.json',
                               origin['parent_sitl_manifest_sha256'])
    mixed.run_case(evidence, args.nose_up, args.first_sign, fixture_type=BiasFixture)


if __name__ == '__main__':
    main()
