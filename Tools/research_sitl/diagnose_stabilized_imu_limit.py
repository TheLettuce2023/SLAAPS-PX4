#!/usr/bin/env python3
"""Observe admission and revocation with 100 ms private raw-IMU delivery."""
import argparse
import json
from pathlib import Path
import shutil

import verify_stabilized_campaigns as campaigns
import verify_stabilized_sitl as base


def diagnose(evidence):
    fixture = campaigns.Fixture(evidence, 'imu-boundary-up', nose_up=True, imu_delay_us=100000)
    fixture.result.update(exploratory_only=True, imu_freshness_boundary_probe=True,
                          probe_source_sha256=base.digest(Path(__file__)))
    shutil.copy2(Path(__file__), fixture.work / Path(__file__).name)
    try:
        fixture.start()
        fixture.result['initial_enable_succeeded'] = True
        fixture.wait(500000)
    except Exception as error:
        fixture.result['error'] = str(error)
        fixture.result['initial_enable_succeeded'] = False
    finally:
        if fixture.px4 is not None and fixture.px4.poll() is None:
            try:
                status, _ = fixture.client('research_stabilized_sitl', 'status', check=False)
                fixture.result['status_before_cleanup'] = status.strip()
            except Exception as error:
                fixture.result['status_error'] = str(error)
        fixture.close()

    control_path = fixture.work / 'control.csv'
    truth_path = fixture.work / 'truth.csv'
    events_path = fixture.work / 'control.csv.events.csv'
    control = campaigns.rows(control_path) if control_path.exists() else []
    truth = campaigns.rows(truth_path) if truth_path.exists() else []
    events = campaigns.rows(events_path) if events_path.exists() else []
    imu_path = fixture.work / 'truth.csv.imu.csv'
    packets = campaigns.rows(imu_path) if imu_path.exists() else []
    stale = [row for row in control if int(row['time']) - int(row['rate_sample']) > 100000
             or int(row['time']) - int(row['att_sample']) > 100000]
    first_revoke = next((row for row in events if row['action'] == 'revoke'), None)
    consumer_never_enabled = bool(truth) and all(
        row['enabled'] == '0' and row['thrust'] == '0'
        and all(float(row['u' + str(i)]) == 0 for i in range(4)) for row in truth)
    exact_delay = len(packets) > 1000 and all(
        int(row['delivery_time']) - int(row['sample_time']) == 100000 for row in packets)
    native_rejected_state = bool(first_revoke) and first_revoke['status'] == '6' \
        and 'fault=6' in fixture.result.get('status_before_cleanup', '')
    passed = (not fixture.result['initial_enable_succeeded'] and native_rejected_state
              and not control and not stale and consumer_never_enabled and exact_delay)
    fixture.result.update(observation_complete=bool(truth), control_cycles=len(control),
                          stale_published_cycles=len(stale),
                          consumer_never_enabled=consumer_never_enabled,
                          native_rejected_state=native_rejected_state,
                          relayed_imu_packets=len(packets), exact_100ms_delivery=exact_delay,
                          passed=passed,
                          events=[dict(row) for row in events])
    (fixture.work / 'result.json').write_text(json.dumps(fixture.result, indent=2) + '\n')
    print(json.dumps(dict(work=str(fixture.work), result=fixture.result), indent=2), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--evidence', type=Path, required=True)
    args = parser.parse_args()
    evidence = args.evidence.resolve()
    assert evidence.parent == Path('/var/tmp') and evidence.name.startswith('research-stabilized-sitl-')
    diagnose(evidence)


if __name__ == '__main__':
    main()
