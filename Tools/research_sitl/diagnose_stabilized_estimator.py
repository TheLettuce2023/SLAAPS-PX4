#!/usr/bin/env python3
"""Exploratory EKF diagnostics; never an acceptance run or hardware profile."""
import argparse
import json
from pathlib import Path
import re
import shutil
import tempfile

import verify_stabilized_campaigns as campaigns
import verify_stabilized_sitl as base


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-evidence', type=Path, required=True)
    parser.add_argument('--mag-type', type=int, choices=(0, 6), default=0)
    parser.add_argument('--seed', type=int, default=42)
    parser.add_argument('--field-enu-tesla', type=float, nargs=3)
    parser.add_argument('--synthetic-imu-priors', action='store_true',
                        help='Known zero-bias fixture: initial bias sigma is ten times the declared white-noise sigma')
    args = parser.parse_args()
    campaigns.SEED = args.seed
    source = args.source_evidence.resolve()
    assert source.parent == Path('/var/tmp') and source.name.startswith('research-stabilized-sitl-')
    out = Path(tempfile.mkdtemp(prefix='research-stabilized-sitl-diagnostic-', dir='/var/tmp'))
    for name in ('px4-source', 'plugin-build', 'sitl-lockstep-build'):
        (out / name).symlink_to(source / name, target_is_directory=True)
    shutil.copy2(Path(__file__), out / Path(__file__).name)
    fixture_type = campaigns.Fixture

    class DiagnosticFixture(fixture_type):
        def __init__(self, *values, **keywords):
            super().__init__(*values, **keywords)
            self.counter = 0
            self.result.update(exploratory_only=True, mag_type=args.mag_type,
                               synthetic_imu_priors=args.synthetic_imu_priors,
                               diagnostic_source_sha256=base.digest(Path(__file__)))
            script = self.script.read_text().replace('param set SYS_HAS_MAG 1',
                'param set SYS_HAS_MAG 1\nparam set EKF2_MAG_TYPE ' + str(args.mag_type))
            gyro_prior, accel_prior = ((0.003394, 0.04) if args.synthetic_imu_priors else (0.1, 0.2))
            for name, value in (('EKF2_GBIAS_INIT', gyro_prior), ('EKF2_ABIAS_INIT', accel_prior)):
                script = re.sub(r'param set ' + name + r' [^\n]+', 'param set ' + name + ' ' + str(value), script)
            self.result.update(gyro_bias_prior_rad_s=gyro_prior, accel_bias_prior_m_s2=accel_prior)
            self.script.write_text(script)
            if args.field_enu_tesla:
                self.world.write_text(self.world.read_text().replace(
                    '<magnetic_field>6e-6 2.3e-5 -4.2e-5</magnetic_field>',
                    '<magnetic_field>' + ' '.join(map(str, args.field_enu_tesla)) + '</magnetic_field>'))
                self.result['field_enu_tesla'] = args.field_enu_tesla

        def wait(self, duration_us):
            super().wait(duration_us)
            for topic in ('estimator_status', 'estimator_status_flags', 'estimator_states',
                          'estimator_sensor_bias', 'vehicle_odometry', 'vehicle_local_position'):
                output, code = self.client('listener', topic, '-n', '1', check=False)
                (self.work / f'diagnostic-{self.counter:02d}-{topic}.log').write_text(output)
                assert code == 0, 'Diagnostic listener failed: ' + topic
            self.counter += 1

    campaigns.Fixture = DiagnosticFixture
    print('Exploratory evidence:', out, flush=True)
    try:
        campaigns.campaign(out, 2, True, -1)
    finally:
        paths = list(out.glob('rate-*/result.json'))
        if paths:
            campaigns.inspect_campaign(paths[0].parent)
            print(json.dumps(json.loads(paths[0].read_text()), indent=2), flush=True)


if __name__ == '__main__':
    main()
