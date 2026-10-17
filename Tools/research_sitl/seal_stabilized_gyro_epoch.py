#!/usr/bin/env python3
"""Audit one private-ELF same-cycle selected gyro/rate timestamp trace."""
import argparse
import json
from pathlib import Path
import shutil

import seal_stabilized_phased_jitter as phased_audit
import verify_stabilized_campaigns as campaigns
import verify_stabilized_gyro_epoch as epoch
import verify_stabilized_sitl as base


GYRO_QUEUE_LENGTH = 8  # msg/SensorGyro.msg ORB_QUEUE_LENGTH in this sealed PX4 source.


def inspect(work, result, evidence):
    assert result['passed'] and result['gyro_epoch_diagnostic_variant']
    assert result['gyro_epoch_queue_drain']
    assert result['gyro_epoch_runner_sha256'] == base.digest(Path(epoch.__file__))
    assert result['gyro_epoch_runner_sha256'] == base.digest(work / Path(epoch.__file__).name)
    origin_path = evidence / 'gyro-epoch-origin.json'
    origin = json.loads(origin_path.read_text())
    assert result['gyro_epoch_origin_sha256'] == base.digest(origin_path)
    assert result['gyro_epoch_patch_sha256'] == origin['patch_sha256']
    assert result['gyro_epoch_modified_owner_sha256'] == origin['patched_owner_sha256']
    assert result['px4_sha256'] == origin['diagnostic_px4_sha256']
    assert result['plugin_sha256'] == origin['relay_plugin_sha256']
    physical = phased_audit.inspect(work, result, evidence)
    control = campaigns.rows(work / 'control.csv')
    raw = {int(row['sample_time']) for row in campaigns.rows(work / 'truth.csv.imu.csv')}
    assert len(control) == physical['cycles']
    gyro_samples = []
    excursions = []
    max_rate_age = 0
    max_gyro_age = 0
    max_gyro_ahead = 0
    drained_updates = []
    for row in control:
        now = int(row['time'])
        gyro_time = int(row['gyro_time'])
        gyro_sample = int(row['gyro_sample'])
        rate_sample = int(row['rate_sample'])
        assert int(row['gyro_copy']) == 1
        drained = int(row['gyro_drained_updates'])
        assert 0 <= drained <= GYRO_QUEUE_LENGTH
        drained_updates.append(drained)
        assert int(row['gyro_device_id']) == int(row['gyro_id']) != 0
        assert gyro_sample in raw and 0 < gyro_sample <= gyro_time <= now
        gyro_samples.append(gyro_sample)
        gyro_age = now - gyro_sample
        rate_age = now - rate_sample
        max_gyro_age = max(max_gyro_age, gyro_age)
        max_rate_age = max(max_rate_age, rate_age)
        max_gyro_ahead = max(max_gyro_ahead, gyro_sample - rate_sample)
        if rate_age > 84000:
            excursions.append(dict(time_us=now, rate_age_us=rate_age,
                                   gyro_age_us=gyro_age, gyro_sample_us=gyro_sample,
                                   rate_sample_us=rate_sample,
                                   gyro_ahead_us=gyro_sample - rate_sample))
    assert all(right >= left for left, right in zip(gyro_samples, gyro_samples[1:]))
    assert len(excursions) == physical['rate_age_above_84ms_cycles']
    return dict(**physical, direct_gyro_copy_cycles=len(control),
                selected_gyro_device_matches=True, gyro_samples_in_raw_relay=True,
                maximum_gyro_sample_age_us=max_gyro_age,
                maximum_rate_sample_age_us=max_rate_age,
                maximum_gyro_ahead_of_rate_us=max_gyro_ahead,
                maximum_drained_updates=max(drained_updates),
                full_queue_drain_cycles=sum(count == GYRO_QUEUE_LENGTH for count in drained_updates),
                excursions=excursions,
                original_stall_cause_resolved=False)


def seal(evidence):
    paths = sorted(evidence.glob('mixed-*/result.json'))
    assert len(paths) == 1
    work = paths[0].parent
    result = json.loads(paths[0].read_text())
    assert result['nose_up'] and result['first_sign'] == 1 and result['seed'] == 42
    checkpoint = json.loads((evidence / 'checkpoint.json').read_text())
    for name, identity in checkpoint.items():
        assert base.digest(base.ROOT / name) == identity, 'Mapped source changed: ' + name
    origin = json.loads((evidence / 'gyro-epoch-origin.json').read_text())
    assert origin['preparation_anchors'] == len(checkpoint)
    assert base.digest(base.ROOT / epoch.PATCH.relative_to(base.ROOT)) == origin['patch_sha256']
    assert base.digest(evidence / epoch.PATCH.name) == origin['patch_sha256']
    assert base.digest(base.ROOT / epoch.VARIANT_SOURCE) == origin['original_owner_sha256']
    variant = evidence / 'px4-source-gyro-epoch' / epoch.VARIANT_SOURCE
    assert base.digest(variant) == origin['patched_owner_sha256']
    assert base.digest(evidence / 'sitl-lockstep-build/bin/px4') == origin['diagnostic_px4_sha256']
    assert base.digest(evidence / 'plugin-build/libresearch_rotational_fixture.so') == \
        origin['relay_plugin_sha256']
    commands = json.loads((evidence / 'sitl-lockstep-build/compile_commands.json').read_text())
    compiled = [entry for entry in commands if entry['file'].endswith(str(epoch.VARIANT_SOURCE))]
    assert len(compiled) == 1 and compiled[0]['file'] == str(variant)
    for path in base.FIXTURE.rglob('*'):
        if path.is_file():
            relative = path.relative_to(base.ROOT)
            assert base.digest(path) == base.digest(evidence / 'px4-source' / relative)
    case = inspect(work, result, evidence)
    (evidence / 'gyro-epoch-summary.json').write_text(json.dumps(dict(
        scope='single_private_variant_same_cycle_selected_gyro_rate_observation',
        accepted_campaign_expansion=False, vehicle_qualification=False,
        original_stall_cause_resolved=False, case=case), indent=2) + '\n')
    shutil.copy2(Path(__file__), evidence / Path(__file__).name)
    files = {}
    for path in evidence.iterdir():
        if path.is_file() and path.name != 'gyro-epoch-manifest.json':
            files[path.name] = base.digest(path)
        elif path.is_dir() and path.name.startswith(('mixed-', 'tested-artifacts', 'px4-source')) \
                and path.name != 'px4-source-gyro-epoch':
            for artifact in path.rglob('*'):
                if artifact.is_file() and not artifact.is_symlink():
                    files[str(artifact.relative_to(evidence))] = base.digest(artifact)
    for name in ('sitl-lockstep-build/bin/px4', 'sitl-lockstep-build/boardconfig',
                 'sitl-lockstep-build/compile_commands.json',
                 'plugin-build/libresearch_rotational_fixture.so',
                 str(variant.relative_to(evidence))):
        files[name] = base.digest(evidence / name)
    (evidence / 'gyro-epoch-manifest.json').write_text(json.dumps(dict(
        scope='source_bound_single_private_gyro_epoch_diagnostic_case',
        accepted=False, vehicle_qualification=False, observed_checks_pass=True,
        original_stall_cause_resolved=False, preparation_anchors=len(checkpoint),
        sealer_sha256=base.digest(Path(__file__)), files=files), indent=2) + '\n')
    print(json.dumps(dict(manifest_sha256=base.digest(evidence / 'gyro-epoch-manifest.json'),
                          files=len(files), cycles=case['cycles'],
                          maximum_gyro_age_us=case['maximum_gyro_sample_age_us'],
                          maximum_rate_age_us=case['maximum_rate_sample_age_us'],
                          excursions=len(case['excursions'])), indent=2), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--evidence', type=Path, required=True)
    args = parser.parse_args()
    evidence = args.evidence.resolve()
    assert evidence.parent == Path('/var/tmp') and evidence.name.startswith('research-stabilized-sitl-')
    seal(evidence)


if __name__ == '__main__':
    main()
