#!/usr/bin/env python3
"""Prove a changed publisher revision revokes at frozen control time."""
import argparse
import json
from pathlib import Path
import shutil
import time

import verify_stabilized_campaigns as campaigns
import verify_stabilized_dispatch as dispatch
import verify_stabilized_publisher_epoch as epoch
import verify_stabilized_sitl as base
import seal_stabilized_campaigns as audit


def regression(evidence):
    record = epoch.origin(evidence)
    fixture = campaigns.Fixture(evidence, 'frozen-epoch-attitude')
    fixture.result.update(runner_sha256=base.digest(Path(__file__)),
                          scope='frozen_clock_synthetic_attitude_advertisement_revision_guard')
    shutil.copy2(Path(__file__), fixture.work / Path(__file__).name)
    paused = False
    try:
        fixture.start()
        dispatch.set_paused(fixture, True)
        paused = True
        time.sleep(0.05)
        fixture.client('research_stabilized_sitl', 'dispatch')
        time.sleep(0.05)
        baseline = epoch.closed_rows(fixture.work / 'control.csv')
        stamp = int(baseline[-1]['time'])
        physics_time = fixture.now()
        for _ in range(2):
            fixture.client('research_stabilized_sitl', 'dispatch')
            time.sleep(0.02)
        assert len(epoch.closed_rows(fixture.work / 'control.csv')) == len(baseline)
        status, _ = fixture.client('research_stabilized_sitl', 'status')
        assert 'state=3' in status and 'fault=0' in status
        fixture.client('research_stabilized_sitl', 'cycle_publisher', 'attitude')
        fixture.client('research_stabilized_sitl', 'dispatch')
        time.sleep(0.05)
        status, _ = fixture.client('research_stabilized_sitl', 'status')
        (fixture.work / 'frozen-fault-status.log').write_text(status)
        assert 'state=4' in status and f'fault={epoch.STATE_CHANGED}' in status, status
        assert fixture.now() == physics_time, 'Physics advanced during the frozen-clock guard test'
        assert len(epoch.closed_rows(fixture.work / 'control.csv')) == len(baseline), 'Changed frozen state published a command'
        events = epoch.closed_rows(fixture.work / 'control.csv.events.csv')
        injection = next(row for row in events if row['action'] == 'publisher_cycle')
        revoke = next(row for row in events if row['action'] == 'revoke')
        assert int(injection['time']) == int(revoke['time']) == stamp
        assert int(revoke['status']) == epoch.STATE_CHANGED
        skipped = [row for row in events if row['action'] == 'dispatch_not_due' and int(row['time']) == stamp]
        assert len(skipped) >= 2
        _, code = fixture.client('research_stabilized_sitl', 'enable', check=False)
        assert code != 0
        dispatch.set_paused(fixture, False)
        paused = False
        fixture.wait(200000)
        fixture.client('research_stabilized_sitl', 'stop')
        fixture.client('research_stabilized_sitl', 'start', 'slaaps_fixture', str(fixture.work / 'restarted.csv'))
        deadline = time.monotonic() + 65
        while time.monotonic() < deadline:
            status, _ = fixture.client('research_stabilized_sitl', 'status')
            if 'state=1' in status and 'ready=1' in status:
                break
            time.sleep(0.05)
        else:
            raise RuntimeError('Explicit owner restart did not become Ready')
        fixture.client('research_stabilized_sitl', 'enable')
        fixture.wait(1000000)
        status, _ = fixture.client('research_stabilized_sitl', 'status')
        assert 'state=3' in status and 'fault=0' in status
        fixture.client('research_stabilized_sitl', 'disable')
        fixture.wait(300000)
        fixture.result.update(frozen_control_time_us=stamp, frozen_physics_time_us=physics_time,
                              unchanged_dispatches=len(skipped), published_before=len(baseline),
                              published_after=len(epoch.closed_rows(fixture.work / 'control.csv')),
                              implicit_enable_returncode=code)
    except Exception as error:
        fixture.result['error'] = str(error)
        raise
    finally:
        try:
            if paused and fixture.gz is not None and fixture.gz.poll() is None:
                dispatch.set_paused(fixture, False)
        finally:
            fixture.close()
    truth = epoch.closed_rows(fixture.work / 'truth.csv')
    first_active = next(i for i, row in enumerate(truth) if row['enabled'] == '1')
    first_off = next(i for i in range(first_active + 1, len(truth)) if truth[i]['enabled'] == '0')
    first_recovery = next(i for i in range(first_off + 1, len(truth)) if truth[i]['enabled'] == '1')
    assert int(truth[first_recovery]['session']) > int(truth[first_active]['session']) > 0
    assert all(row['thrust'] == '0' and row['lease_fault'] == '1'
               and all(float(row['t' + str(i)]) == 0 for i in range(4)) for row in truth[first_off:first_recovery])
    assert truth[-1]['thrust'] == '0' and truth[-1]['lease_fault'] == '1'
    off_delay = int(truth[first_off]['time']) - stamp
    assert 0 <= off_delay <= 10000
    cycles = audit.audit_cycles(fixture.work / 'control.csv') + audit.audit_cycles(fixture.work / 'restarted.csv')
    fixture.result.update(passed=True, audited_control_cycles=cycles,
                          consumer_off_delay_us=off_delay, variant_px4_sha256=record['variant_px4_sha256'])
    (fixture.work / 'result.json').write_text(json.dumps(fixture.result, indent=2) + '\n')
    print('PASS frozen-clock lifecycle guard:', fixture.work, 'cycles', cycles, 'off delay us', off_delay, flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--evidence', type=Path, required=True)
    args = parser.parse_args()
    evidence = args.evidence.resolve()
    assert evidence.parent == Path('/var/tmp') and evidence.name.startswith('research-stabilized-sitl-')
    regression(evidence)
