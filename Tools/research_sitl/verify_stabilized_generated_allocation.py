#!/usr/bin/env python3
"""Run exploratory native SITL maneuvers with a sealed generated allocation."""
import argparse
import json
from pathlib import Path
import shutil

import verify_stabilized_campaigns as campaigns
import verify_stabilized_mixed as mixed
import verify_stabilized_sitl as base


class GeneratedFixture(campaigns.Fixture):
    def __init__(self, evidence, *args, **kwargs):
        origin = json.loads((evidence / 'generated-allocation-origin.json').read_text())
        assert origin['scope'] == 'private_synthetic_generated_allocation_variant'
        assert origin['vehicle_qualified'] is False
        assert origin['accepted_campaign_expansion'] is False
        assert base.digest(evidence / 'sitl-lockstep-build/bin/px4') == origin['generated_px4_sha256']
        assert base.digest(evidence / 'plugin-build/libresearch_rotational_fixture.so') == origin['plugin_sha256']
        assert base.digest(evidence / 'GeneratedAllocationProfile.hpp') == origin['generated_header_sha256']
        super().__init__(evidence, *args, **kwargs)
        self.profile_sha = origin['synthetic_input_sha256']
        self.result.update(generated_allocation=True, profile_sha256=self.profile_sha,
                           origin_sha256=base.digest(evidence / 'generated-allocation-origin.json'),
                           generated_header_sha256=origin['generated_header_sha256'])
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
    mixed.run_case(evidence, args.nose_up, args.first_sign, fixture_type=GeneratedFixture)


if __name__ == '__main__':
    main()
