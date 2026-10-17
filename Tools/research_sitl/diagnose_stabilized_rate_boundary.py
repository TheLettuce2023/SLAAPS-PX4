#!/usr/bin/env python3
"""Locate the selected angular-rate stall in sealed phased-manual SITL logs."""
import csv
import json
from pathlib import Path
import shutil
import tempfile

import verify_stabilized_sitl as base


PARENT = Path('/var/tmp/research-stabilized-sitl-kmupr1nc')
PARENT_MANIFEST_SHA256 = '3e2e43a4ba4f205a3bc7e3e284323e5044f974a259d13164f98798ecd727724d'
RAW_IMU_DELAY_US = 80000
TICK_US = 4000
NORMAL_RATE_AGE_MAX_US = 84000
FRESHNESS_LIMIT_US = 100000


def rows(path):
    with path.open() as stream:
        data = list(csv.DictReader(stream))
    assert data and all(None not in row and all(value is not None for value in row.values())
                        for row in data)
    return data


def inspect(work, result):
    assert result['passed'] and result['exploratory_only'] and result['mixed_axes']
    assert result['seed'] == 42 and result['manual_send_period_s'] == 0.08
    assert result['imu_transport_delay_us'] == RAW_IMU_DELAY_US
    control = rows(work / 'control.csv')
    packets = rows(work / 'truth.csv.imu.csv')
    assert len(control) == result['audited_control_cycles']
    assert len(packets) > 7500
    packet_by_delivery = {}
    previous_sample = 0
    for packet in packets:
        sample = int(packet['sample_time'])
        delivery = int(packet['delivery_time'])
        assert sample > previous_sample and delivery - sample == RAW_IMU_DELAY_US
        assert delivery not in packet_by_delivery
        packet_by_delivery[delivery] = sample
        previous_sample = sample

    ages = [int(row['time']) - int(row['rate_sample']) for row in control]
    assert min(ages) >= RAW_IMU_DELAY_US and max(ages) <= FRESHNESS_LIMIT_US
    assert max(int(row['time']) - int(row['manual_sample']) for row in control) < FRESHNESS_LIMIT_US
    older = [row for row, age in zip(control, ages) if age > NORMAL_RATE_AGE_MAX_US]
    key = (result['nose_up'], result['first_sign'])
    if key != (True, 1):
        assert not older, 'Unexpected selected-rate stall outside declared case'
        return dict(directory=work.name, nose_up=key[0], first_sign=key[1],
                    cycles=len(control), maximum_rate_age_us=max(ages),
                    rate_cycles_above_84ms=0)

    assert [int(row['time']) - int(row['rate_sample']) for row in older] == \
        [88000, 92000, 96000, 100000]
    times = [int(row['time']) for row in older]
    assert all(right - left == TICK_US for left, right in zip(times, times[1:]))
    old_rate_publication = int(older[0]['rate_time'])
    old_rate_sample = int(older[0]['rate_sample'])
    assert all(int(row['rate_time']) == old_rate_publication
               and int(row['rate_sample']) == old_rate_sample for row in older)
    after = next(row for row in control if int(row['time']) > times[-1]
                 and int(row['rate_sample']) > old_rate_sample)
    new_rate_publication = int(after['rate_time'])
    new_rate_sample = int(after['rate_sample'])
    assert new_rate_publication - old_rate_publication == 20000
    assert new_rate_sample - old_rate_sample == 20000
    assert int(after['time']) - new_rate_sample <= NORMAL_RATE_AGE_MAX_US

    attitude_samples = [int(row['att_sample']) for row in older]
    assert len(set(attitude_samples)) >= 3 and attitude_samples[-1] > attitude_samples[0]
    manual_ages = [int(row['time']) - int(row['manual_sample']) for row in older]
    assert max(manual_ages) < FRESHNESS_LIMIT_US
    relayed = [(delivery, sample) for delivery, sample in packet_by_delivery.items()
               if old_rate_publication < delivery <= new_rate_publication]
    assert len(relayed) == 5
    assert all(right[0] - left[0] == TICK_US and right[1] - left[1] == TICK_US
               for left, right in zip(relayed, relayed[1:]))
    assert relayed[0][1] > old_rate_sample and relayed[-1][1] == new_rate_sample
    return dict(directory=work.name, nose_up=True, first_sign=1, cycles=len(control),
                maximum_rate_age_us=max(ages), rate_cycles_above_84ms=len(older),
                boundary_control_times_us=times,
                boundary_selected_rate_ages_us=[int(row['time']) - int(row['rate_sample'])
                                                for row in older],
                old_rate_publication_us=old_rate_publication,
                new_rate_publication_us=new_rate_publication,
                old_rate_sample_us=old_rate_sample, new_rate_sample_us=new_rate_sample,
                rate_publication_gap_us=new_rate_publication - old_rate_publication,
                attitude_samples_during_stall_us=attitude_samples,
                manual_ages_during_stall_us=manual_ages,
                raw_relay_packets_during_gap=[dict(sample_us=sample, delivery_us=delivery)
                                              for delivery, sample in relayed],
                next_control_rate_age_us=int(after['time']) - new_rate_sample,
                cause_location='unresolved_between_relay_publish_and_selected_angular_rate_copy')


def diagnose():
    parent = PARENT.resolve()
    manifest_path = parent / 'phased-jitter-manifest.json'
    assert base.digest(manifest_path) == PARENT_MANIFEST_SHA256
    manifest = json.loads(manifest_path.read_text())
    assert manifest['observed_checks_pass'] and not manifest['accepted']
    for name, identity in manifest['files'].items():
        assert base.digest(parent / name) == identity, 'Parent artifact changed: ' + name
    graph = json.loads((base.ROOT / 'docs/RESEARCH_CODE_GRAPH.json').read_text())
    checkpoint = {}
    for node in graph['nodes']:
        if 'sha256' in node:
            assert base.digest(base.ROOT / node['path']) == node['sha256'], 'Stale graph: ' + node['path']
            checkpoint[node['path']] = node['sha256']
    paths = sorted(parent.glob('mixed-*/result.json'))
    assert len(paths) == 4
    results = [(path.parent, json.loads(path.read_text())) for path in paths]
    assert {(result['nose_up'], result['first_sign']) for _, result in results} == \
        {(nose_up, sign) for nose_up in (False, True) for sign in (-1, 1)}
    cases = [inspect(work, result) for work, result in results]
    out = Path(tempfile.mkdtemp(prefix='research-stabilized-sitl-', dir='/var/tmp'))
    (out / 'checkpoint.json').write_text(json.dumps(checkpoint, indent=2) + '\n')
    (out / 'rate-boundary-diagnostic.json').write_text(json.dumps(dict(
        scope='sealed_four_case_selected_angular_rate_publication_stall_diagnostic',
        parent_manifest_sha256=PARENT_MANIFEST_SHA256,
        raw_relay_to_stock_gyro_causality_proven=False,
        cause_location_qualified=False, cases=cases), indent=2) + '\n')
    shutil.copy2(manifest_path, out / 'parent-phased-jitter-manifest.json')
    shutil.copy2(Path(__file__), out / Path(__file__).name)
    files = {path.name: base.digest(path) for path in out.iterdir() if path.is_file()}
    (out / 'rate-boundary-manifest.json').write_text(json.dumps(dict(
        scope='read_only_source_bound_selected_rate_boundary_diagnostic', accepted=False,
        vehicle_qualification=False, parent_manifest_sha256=PARENT_MANIFEST_SHA256,
        preserved_graph_anchor_count=len(checkpoint), files=files), indent=2) + '\n')
    print(json.dumps(dict(evidence=str(out),
                          manifest_sha256=base.digest(out / 'rate-boundary-manifest.json'),
                          parent_artifacts_verified=len(manifest['files']),
                          preserved_graph_anchors=len(checkpoint), cases=cases), indent=2), flush=True)


if __name__ == '__main__':
    diagnose()
