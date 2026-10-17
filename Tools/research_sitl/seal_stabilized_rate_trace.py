#!/usr/bin/env python3
"""Audit three exact-setting phase-controlled gyro/rate ULog observations."""
import argparse
import json
from pathlib import Path
import shutil

from pyulog import ULog

import seal_stabilized_phased_jitter as phased_audit
import verify_stabilized_campaigns as campaigns
import verify_stabilized_rate_trace as trace
import verify_stabilized_sitl as base


REQUIRED_TOPICS = ('sensor_gyro', 'vehicle_angular_velocity',
                   'vehicle_attitude', 'vehicle_imu')


def inspect(work, result, evidence):
    assert result['rate_trace_source_sha256'] == base.digest(Path(trace.__file__))
    assert result['rate_trace_source_sha256'] == base.digest(work / Path(trace.__file__).name)
    assert result['rate_trace_topics'] == list(trace.TRACE_TOPICS)
    assert result['logger_topics_sha256'] == base.digest(work / 'fs/etc/logging/logger_topics.txt')
    assert result['generated_rc_sha256'] == base.digest(work / 'rc.fixture')
    assert result['nose_up'] and result['first_sign'] == 1 and result['seed'] == 42
    physical = phased_audit.inspect(work, result, evidence)
    ulogs = list((work / 'fs/log').rglob('*.ulg'))
    assert len(ulogs) == 1, 'Require exactly one closed stock logger trace'
    ulog = ULog(str(ulogs[0]))
    assert not ulog.dropouts, 'Logger reported dropped buffer data'
    data = {topic.name: topic.data for topic in ulog.data_list if topic.multi_id == 0}
    assert set(REQUIRED_TOPICS) <= set(data)
    control = campaigns.rows(work / 'control.csv')
    first, last = int(control[0]['time']), int(control[-1]['time'])
    selected = {int(row['rate_sample']) for row in control}
    raw = {int(row['sample_time']) for row in campaigns.rows(work / 'truth.csv.imu.csv')}
    topic_metrics = {}
    for name in REQUIRED_TOPICS:
        stamps = list(map(int, data[name]['timestamp_sample']))
        assert len(stamps) > 1000 and all(right > left for left, right in zip(stamps, stamps[1:]))
        active = [stamp for stamp in stamps if first <= stamp <= last]
        assert len(active) > 1000
        gaps = [right - left for left, right in zip(stamps, stamps[1:])]
        active_gaps = [right - left for left, right in zip(active, active[1:])]
        assert max(active_gaps) <= 8000, 'Rate-path gap recurred in logged active control'
        topic_metrics[name] = dict(samples=len(stamps),
                                   maximum_sample_gap_us=max(gaps),
                                   maximum_active_sample_gap_us=max(active_gaps))
    logged_gyro = set(map(int, data['sensor_gyro']['timestamp_sample']))
    logged_rate = set(map(int, data['vehicle_angular_velocity']['timestamp_sample']))
    assert logged_gyro <= raw, 'A logged PX4 gyro timestamp lacks a relayed raw IMU sample'
    assert max(int(row['time']) - int(row['rate_sample']) for row in control) == 84000
    return dict(**physical, repeat_id=result['trace_repeat_id'],
                logger_dropouts=len(ulog.dropouts),
                ulog_sha256=base.digest(ulogs[0]),
                topic_metrics=topic_metrics,
                logged_gyro_samples_missing_raw=len(logged_gyro - raw),
                selected_rate_samples_absent_from_ulog=len(selected - logged_rate),
                observed_20ms_active_stall=False)


def seal(evidence):
    paths = sorted(evidence.glob('mixed-*/result.json'))
    assert len(paths) == 3
    results = [(path.parent, json.loads(path.read_text())) for path in paths]
    assert {result['trace_repeat_id'] for _, result in results} == {0, 1, 2}
    checkpoint = json.loads((evidence / 'checkpoint.json').read_text())
    for name, identity in checkpoint.items():
        assert base.digest(base.ROOT / name) == identity, 'Mapped source changed: ' + name
    origin = json.loads((evidence / 'native-origin.json').read_text())
    parent = Path(origin['parent'])
    for name, identity in origin['parent_receipts'].items():
        assert base.digest(parent / name) == identity
    for name, identity in (('sitl-lockstep-build/bin/px4', 'px4_sha256'),
                           ('plugin-build/libresearch_rotational_fixture.so', 'plugin_sha256')):
        assert base.digest(parent / name) == origin[identity]
        assert base.digest(evidence / name) == origin[identity]
    for path in base.FIXTURE.rglob('*'):
        if path.is_file():
            relative = path.relative_to(base.ROOT)
            assert base.digest(path) == base.digest(evidence / 'px4-source' / relative)
    cases = [inspect(work, result, evidence) for work, result in results]
    (evidence / 'rate-trace-summary.json').write_text(json.dumps(dict(
        scope='three_exact_setting_nose_up_stock_gyro_rate_ulog_nonrecurrence_observations',
        accepted_campaign_expansion=False, vehicle_qualification=False,
        original_20ms_stall_cause_resolved=False,
        audited_control_cycles=sum(case['cycles'] for case in cases), cases=cases), indent=2) + '\n')
    shutil.copy2(Path(__file__), evidence / Path(__file__).name)
    files = {}
    for path in evidence.iterdir():
        if path.is_file() and path.name != 'rate-trace-manifest.json':
            files[path.name] = base.digest(path)
        elif path.is_dir() and path.name.startswith(('mixed-', 'tested-artifacts', 'px4-source')):
            for artifact in path.rglob('*'):
                if artifact.is_file() and not artifact.is_symlink():
                    files[str(artifact.relative_to(evidence))] = base.digest(artifact)
    for name in ('sitl-lockstep-build/bin/px4', 'sitl-lockstep-build/boardconfig',
                 'sitl-lockstep-build/compile_commands.json',
                 'plugin-build/libresearch_rotational_fixture.so'):
        files[name] = base.digest(evidence / name)
    (evidence / 'rate-trace-manifest.json').write_text(json.dumps(dict(
        scope='three_source_bound_exploratory_logged_nose_up_rate_path_cases',
        accepted=False, vehicle_qualification=False, observed_checks_pass=True,
        original_20ms_stall_cause_resolved=False,
        preserved_preparation_anchor_count=len(checkpoint),
        sealer_sha256=base.digest(Path(__file__)), files=files), indent=2) + '\n')
    print(json.dumps(dict(manifest_sha256=base.digest(evidence / 'rate-trace-manifest.json'),
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
