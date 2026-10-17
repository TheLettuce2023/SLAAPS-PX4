#!/usr/bin/env python3
"""Exploratory coupled EKF/field diagnostics; never acceptance substitution."""
import argparse
import json
import math
from pathlib import Path
import shutil
import subprocess
import tempfile
import xml.etree.ElementTree as ET

import verify_stabilized_coupled as coupled
import verify_stabilized_campaigns as campaigns
import verify_stabilized_sitl as base

TRACE_TOPICS = (
    'sensor_gyro', 'sensor_accel', 'vehicle_imu', 'vehicle_imu_status',
    'vehicle_attitude', 'vehicle_angular_velocity', 'vehicle_local_position',
    'sensor_selection', 'vehicle_magnetometer', 'estimator_states',
    'estimator_status', 'estimator_status_flags', 'estimator_sensor_bias',
    'estimator_aid_src_gravity', 'estimator_aid_src_mag',
    'estimator_aid_src_gnss_vel', 'estimator_aid_src_gnss_pos',
    'estimator_aid_src_baro_hgt', 'estimator_innovations',
    'estimator_innovation_variances', 'estimator_innovation_test_ratios',
    'manual_control_setpoint')


def magnetic_field(source, out):
    # Call the exact compiled PX4 geographic lookup, not an estimated attitude
    # or simulator truth. A declared constant earth field can match its model.
    probe = out / 'magnetic-field-probe.cpp'
    probe.write_text('''#include <cmath>
#include <cstdio>
float get_mag_declination_degrees(float, float);
float get_mag_inclination_degrees(float, float);
float get_mag_strength_tesla(float, float);
int main() {
 const float lat = 47.39797F, lon = 8.54616F;
 const double radians = std::acos(-1.0) / 180.0;
 const double decl = get_mag_declination_degrees(lat, lon) * radians;
 const double incl = get_mag_inclination_degrees(lat, lon) * radians;
 const double strength = get_mag_strength_tesla(lat, lon);
 std::printf("%.17g %.17g %.17g\\n", strength * std::cos(incl) * std::sin(decl),
   strength * std::cos(incl) * std::cos(decl), -strength * std::sin(incl));
}
''')
    archive = source / 'sitl-lockstep-build/src/lib/world_magnetic_model/libworld_magnetic_model.a'
    if not archive.is_file():
        archive = source / 'world_magnetic_model.a'  # Runtime-only variant archives the exact linked lookup.
    executable = out / 'magnetic-field-probe'
    subprocess.run(['g++', str(probe), str(archive), '-o', str(executable)], check=True)
    field = list(map(float, subprocess.check_output([str(executable)]).decode().split()))
    (out / 'magnetic-field-inputs.json').write_text(json.dumps(dict(
        field_enu_tesla=field, compiled_lookup_sha256=base.digest(archive),
        source_sha256={str(path.relative_to(base.ROOT)): base.digest(path)
                       for path in (base.ROOT / 'src/lib/world_magnetic_model').glob('geo_*') if path.is_file()}), indent=2) + '\n')
    return field


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-evidence', type=Path, required=True)
    parser.add_argument('--match-field', action='store_true')
    parser.add_argument('--nose-up', action='store_true')
    parser.add_argument('--sign', type=int, choices=(-1, 1), default=1)
    parser.add_argument('--seed', type=int, default=campaigns.SEED)
    parser.add_argument('--imu-control', type=int, choices=range(8),
                        help='Exploratory stock IMU-control bitmask; 4 retains gravity fusion with fixed zero IMU biases')
    parser.add_argument('--ulog', action='store_true', help='Continuous stock logger trace in this isolated fixture only')
    parser.add_argument('--gyro-bias-prior', type=float,
                        help='Exploratory gyro switch-on bias sigma for this known zero-bias fixture')
    parser.add_argument('--gyro-sensor-mean', type=float,
                        help='Inject the same fixed rad/s gyro bias on all three synthetic sensor axes')
    args = parser.parse_args()
    if args.gyro_bias_prior is not None:
        assert math.isfinite(args.gyro_bias_prior) and 0 < args.gyro_bias_prior <= 0.2
    if args.gyro_sensor_mean is not None:
        assert math.isfinite(args.gyro_sensor_mean) and abs(args.gyro_sensor_mean) <= 0.01
    source = args.source_evidence.resolve()
    assert source.parent == Path('/var/tmp') and source.name.startswith('research-stabilized-sitl-')
    out = Path(tempfile.mkdtemp(prefix='research-stabilized-sitl-coupled-diagnostic-', dir='/var/tmp'))
    for name in ('px4-source', 'plugin-build', 'sitl-lockstep-build'):
        (out / name).symlink_to(source / name, target_is_directory=True)
    shutil.copy2(Path(__file__), out / Path(__file__).name)
    field = magnetic_field(source, out)
    fixture_type = campaigns.Fixture

    class DiagnosticFixture(fixture_type):
        def __init__(self, *values, **keywords):
            super().__init__(*values, **keywords)
            self.counter = 0
            declared = ET.fromstring(self.world.read_text()).find('world/magnetic_field').text
            original = list(map(float, declared.split()))
            self.result.update(exploratory_only=True, matched_magnetic_field=args.match_field,
                               field_enu_tesla=field if args.match_field else original,
                               exploratory_imu_control=args.imu_control,
                               continuous_ulog=args.ulog,
                               diagnostic_source_sha256=base.digest(Path(__file__)))
            if args.imu_control is not None:
                self.script.write_text(self.script.read_text().replace(
                    'ekf2 start\n', f'param set EKF2_IMU_CTRL {args.imu_control}\nekf2 start\n'))
            if args.gyro_bias_prior is not None:
                original_prior = f'param set EKF2_GBIAS_INIT {campaigns.GYRO_BIAS_PRIOR:.9g}'
                assert self.script.read_text().count(original_prior) == 1
                self.script.write_text(self.script.read_text().replace(
                    original_prior, f'param set EKF2_GBIAS_INIT {args.gyro_bias_prior:.9g}'))
                self.result['gyro_bias_prior_rad_s'] = args.gyro_bias_prior
            if args.match_field:
                self.world.write_text(self.world.read_text().replace(
                    '<magnetic_field>' + declared + '</magnetic_field>',
                    '<magnetic_field>' + ' '.join(map(str, field)) + '</magnetic_field>'))
            if args.gyro_sensor_mean is not None:
                world = ET.fromstring(self.world.read_text())

                for axis in 'xyz':
                    mean = world.find(f"world/model/link/sensor[@name='imu_sensor']/imu/angular_velocity/{axis}/noise/mean")
                    assert mean is not None and float(mean.text) == 0
                    mean.text = f'{args.gyro_sensor_mean:.9g}'

                self.world.write_text(ET.tostring(world, encoding='unicode'))
                self.result['injected_gyro_sensor_mean_rad_s'] = args.gyro_sensor_mean

            self.result['generated_world_sha256'] = base.digest(self.world)
            if args.ulog:
                # This POSIX build uses PX4_STORAGEDIR="fs" relative to -w.
                # Standard logger storage and cleanup stay in the new case.
                storage = self.work / 'fs/etc/logging'
                storage.mkdir(parents=True)
                assert storage.resolve().is_relative_to(self.work.resolve())
                (storage / 'logger_topics.txt').write_text(''.join(name + ' 0 0\n' for name in TRACE_TOPICS))
                self.script.write_text(self.script.read_text().replace(
                    'mavlink boot_complete\n', 'param set SDLOG_PROFILE 0\n'
                    'logger start -m file -f -p vehicle_imu -b 512\nmavlink boot_complete\n'))

        def wait(self, duration_us):
            super().wait(duration_us)
            for topic in ('estimator_states', 'estimator_sensor_bias', 'estimator_status_flags', 'vehicle_odometry'):
                output, code = self.client('listener', topic, '-n', '1', check=False)
                (self.work / f'diagnostic-{self.counter:02d}-{topic}.log').write_text(output)
                assert code == 0
            self.counter += 1

        def close(self):
            if args.ulog and self.px4 is not None and self.px4.poll() is None:
                output, _ = self.client('logger', 'status', check=False)
                (self.work / 'logger-status.log').write_text(output)
            super().close()

    campaigns.Fixture = DiagnosticFixture
    print('Exploratory evidence:', out, 'PX4 model ENU field:', field, flush=True)
    try:
        coupled.coupled(out, args.nose_up, args.sign, seed=args.seed)
    finally:
        paths = list(out.glob('coupled-*/result.json'))
        if paths and any(row['state'] == '2' for row in campaigns.rows(paths[0].parent / 'control.csv')):
            campaigns.inspect_campaign(paths[0].parent)


if __name__ == '__main__':
    main()
