#!/usr/bin/env python3
"""Observe an EKF stop/start while Gazebo's controller clock is frozen."""
import argparse
import json
from pathlib import Path
import shutil
import time

import seal_stabilized_campaigns as audit
import verify_stabilized_campaigns as campaigns
import verify_stabilized_dispatch as dispatch
import verify_stabilized_sitl as base


def diagnose(evidence, nose_up):
    fixture = campaigns.Fixture(evidence, 'epoch-pause-up' if nose_up else 'epoch-pause-level', nose_up)
    fixture.result.update(exploratory_only=True, epoch_pause_probe=True,
                          epoch_probe_source_sha256=base.digest(Path(__file__)))
    shutil.copy2(Path(__file__), fixture.work / Path(__file__).name)
    paused = False

    try:
        fixture.start()
        dispatch.set_paused(fixture, True)
        paused = True
        time.sleep(0.05)
        before = campaigns.rows(fixture.work / 'control.csv')
        events_before = campaigns.rows(fixture.work / 'control.csv.events.csv')
        frozen_time = fixture.now()
        fixture.action('frozen_ekf_stop')
        output, _ = fixture.client('ekf2', 'stop')
        (fixture.work / 'ekf-stop.log').write_text(output)
        fixture.action('frozen_ekf_start')
        output, _ = fixture.client('ekf2', 'start')
        (fixture.work / 'ekf-start.log').write_text(output)
        time.sleep(0.05)
        frozen_control = campaigns.rows(fixture.work / 'control.csv')
        frozen_events = campaigns.rows(fixture.work / 'control.csv.events.csv')
        frozen_status, _ = fixture.client('research_stabilized_sitl', 'status')
        (fixture.work / 'frozen-status.log').write_text(frozen_status)
        fixture.result.update(frozen_physics_time_us=frozen_time,
                              frozen_control_before=len(before), frozen_control_after=len(frozen_control),
                              frozen_event_before=len(events_before), frozen_event_after=len(frozen_events),
                              frozen_status=frozen_status.strip())
        assert fixture.now() == frozen_time, 'Physics advanced during stopped-clock restart'
        dispatch.set_paused(fixture, False)
        paused = False
        fixture.action('physics_resumed')
        fixture.wait(300000)
        status, _ = fixture.client('research_stabilized_sitl', 'status')
        (fixture.work / 'observed-status.log').write_text(status)
        events = campaigns.rows(fixture.work / 'control.csv.events.csv')
        control = campaigns.rows(fixture.work / 'control.csv')
        revoke = [row for row in events if row['action'] == 'revoke']
        fixture.result.update(observed_status=status.strip(),
                              observed_events_before_cleanup=len(events),
                              revoke_before_cleanup=[row for row in revoke],
                              observed_control_before_cleanup=len(control),
                              new_controls_after_frozen=len(control) - len(frozen_control))

    except Exception as error:
        fixture.result['error'] = str(error)
        raise

    finally:
        try:
            if paused and fixture.gz is not None and fixture.gz.poll() is None:
                dispatch.set_paused(fixture, False)
        finally:
            fixture.close()

    control = campaigns.rows(fixture.work / 'control.csv')
    truth = campaigns.rows(fixture.work / 'truth.csv')
    count = audit.audit_cycles(fixture.work / 'control.csv')
    assert count >= len(before) and truth[-1]['thrust'] == '0' and truth[-1]['lease_fault'] == '1'
    fixture.result.update(observation_complete=True, audited_control_cycles=count,
                          baseline_reset_counter=int(before[-1]['reset']),
                          final_reset_counter=int(control[-1]['reset']),
                          controller_polled_during_pause=(len(frozen_control) != len(before)
                                                            or len(frozen_events) != len(events_before)))
    (fixture.work / 'result.json').write_text(json.dumps(fixture.result, indent=2) + '\n')
    print(json.dumps(dict(work=str(fixture.work), observed_status=fixture.result['observed_status'],
                          frozen_control_delta=len(frozen_control)-len(before),
                          frozen_event_delta=len(frozen_events)-len(events_before),
                          new_controls_after_frozen=fixture.result['new_controls_after_frozen'],
                          revoke_before_cleanup=fixture.result['revoke_before_cleanup'],
                          baseline_reset_counter=fixture.result['baseline_reset_counter'],
                          final_reset_counter=fixture.result['final_reset_counter'],
                          audited_control_cycles=count), indent=2), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--evidence', type=Path, required=True)
    parser.add_argument('--nose-up', action='store_true')
    args = parser.parse_args()
    evidence = args.evidence.resolve()
    assert evidence.parent == Path('/var/tmp') and evidence.name.startswith('research-stabilized-sitl-')
    diagnose(evidence, args.nose_up)


if __name__ == '__main__':
    main()
