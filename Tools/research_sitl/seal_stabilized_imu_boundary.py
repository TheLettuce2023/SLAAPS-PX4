#!/usr/bin/env python3
"""Seal the two actual 100 ms raw-IMU boundary observations, including the preliminary probe."""
import argparse
import json
import math
from pathlib import Path
import shutil

import verify_stabilized_campaigns as campaigns
import verify_stabilized_sitl as base


def inspect(work, result, evidence):
    assert result['imu_freshness_boundary_probe'] and result['exploratory_only']
    assert result['imu_transport_delay_us'] == 100000 and result['magnetic_transport_delay_us'] == 0
    assert result['px4_sha256'] == base.digest(evidence / 'sitl-lockstep-build/bin/px4')
    assert result['plugin_sha256'] == base.digest(evidence / 'plugin-build/libresearch_rotational_fixture.so')
    assert result['probe_source_sha256'] == base.digest(work / 'diagnose_stabilized_imu_limit.py')
    for name, identity in result['source_sha256'].items():
        assert base.digest(work / 'source-inputs' / name) == identity
        assert base.digest(base.ROOT / name) == identity
    packets = campaigns.rows(work / 'truth.csv.imu.csv')
    assert len(packets) > 1000
    previous = 0
    for row in packets:
        sample = int(row['sample_time'])
        assert sample > previous and int(row['delivery_time']) - sample == 100000
        assert all(math.isfinite(float(row[name])) for name in
                   ('gyro_x', 'gyro_y', 'gyro_z', 'accel_x', 'accel_y', 'accel_z'))
        previous = sample
    control = campaigns.rows(work / 'control.csv')
    truth = campaigns.rows(work / 'truth.csv')
    events = campaigns.rows(work / 'control.csv.events.csv')
    first_revoke = next(row for row in events if row['action'] == 'revoke')
    assert first_revoke['status'] == '6' and first_revoke['session'] == '0'
    assert 'state=4 ready=0 fault=6' in result['status_before_cleanup']
    assert not control and truth
    assert all(row['enabled'] == '0' and row['thrust'] == '0'
               and all(float(row['u' + str(i)]) == 0 for i in range(4)) for row in truth)
    assert not result['initial_enable_succeeded']
    return dict(directory=work.name, diagnostic_source_sha256=result['probe_source_sha256'],
                diagnostic_passed=result['passed'], relayed_imu_packets=len(packets),
                first_revoke_us=int(first_revoke['time']), first_revoke_reason=int(first_revoke['status']),
                published_control_cycles=0, consumer_never_enabled=True)


def seal(evidence):
    positive_manifest = evidence / 'imu-delay-manifest.json'
    positive = json.loads(positive_manifest.read_text())
    assert positive['observed_checks_pass'] and not positive['accepted']
    for name, identity in positive['files'].items():
        assert base.digest(evidence / name) == identity, 'Positive evidence changed: ' + name
    paths = sorted(evidence.glob('imu-boundary-up-*/result.json'), key=lambda path: path.stat().st_mtime_ns)
    assert len(paths) == 2, 'Require preliminary and refined boundary probes'
    observations = [inspect(path.parent, json.loads(path.read_text()), evidence) for path in paths]
    assert not observations[0]['diagnostic_passed'] and observations[1]['diagnostic_passed']
    assert observations[0]['diagnostic_source_sha256'] != observations[1]['diagnostic_source_sha256']
    checkpoint = json.loads((evidence / 'checkpoint.json').read_text())
    for name, identity in checkpoint.items():
        if name == 'Tools/research_sitl/seal_stabilized_mixed.py':
            assert base.digest(base.ROOT / name) != identity
        else:
            assert base.digest(base.ROOT / name) == identity, 'Unrelated graph anchor changed: ' + name
    (evidence / 'imu-boundary-summary.json').write_text(json.dumps(dict(
        scope='actual_100ms_imu_delay_initial_state_rejection_without_consumer_enable',
        accepted_campaign_expansion=False, positive_manifest_sha256=base.digest(positive_manifest),
        preliminary_probe_retained=True, final_negative_observation_pass=True,
        observations=observations), indent=2) + '\n')
    shutil.copy2(Path(__file__), evidence / Path(__file__).name)
    files = {}
    for path in evidence.iterdir():
        if path.is_file() and path.name != 'imu-boundary-manifest.json':
            files[path.name] = base.digest(path)
        elif path.is_dir() and path.name.startswith('imu-boundary-up-'):
            for artifact in path.rglob('*'):
                if artifact.is_file() and not artifact.is_symlink():
                    files[str(artifact.relative_to(evidence))] = base.digest(artifact)
    for name in ('sitl-lockstep-build/bin/px4', 'plugin-build/libresearch_rotational_fixture.so'):
        files[name] = base.digest(evidence / name)
    (evidence / 'imu-boundary-manifest.json').write_text(json.dumps(dict(
        scope='two_source_bound_100ms_imu_delay_state_rejection_observations', accepted=False,
        vehicle_qualification=False, negative_observation_pass=True,
        positive_manifest_sha256=base.digest(positive_manifest), files=files), indent=2) + '\n')
    print(json.dumps(dict(manifest_sha256=base.digest(evidence / 'imu-boundary-manifest.json'),
                          files=len(files), preliminary_and_final=len(observations)), indent=2), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--evidence', type=Path, required=True)
    args = parser.parse_args()
    evidence = args.evidence.resolve()
    assert evidence.parent == Path('/var/tmp') and evidence.name.startswith('research-stabilized-sitl-')
    seal(evidence)


if __name__ == '__main__':
    main()
