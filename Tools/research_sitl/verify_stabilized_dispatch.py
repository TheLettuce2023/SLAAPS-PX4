#!/usr/bin/env python3
"""Actual private work-queue dispatch at frozen simulation time; no fake state."""
import argparse
import json
from pathlib import Path
import shutil
import time

import seal_stabilized_campaigns as audit
import verify_stabilized_campaigns as campaigns
import verify_stabilized_sitl as base

REPEATS = 5


def set_paused(fixture, paused):
    base.run(['gz', 'service', '-s', '/world/stabilized_fixture/control',
              '--reqtype', 'gz.msgs.WorldControl', '--reptype', 'gz.msgs.Boolean',
              '--timeout', '2000', '--req', 'pause: ' + str(paused).lower()],
             fixture.work, 'pause-' + str(paused).lower() + '.log', env=fixture.env, timeout=10)


def regression(out):
    fixture = campaigns.Fixture(out, 'dispatch')
    fixture.result.update(dispatch_regression=True, dispatch_source_sha256=base.digest(Path(__file__)))
    shutil.copy2(Path(__file__), fixture.work / Path(__file__).name)
    paused = False
    try:
        fixture.start()
        set_paused(fixture, True)
        paused = True
        time.sleep(0.05)  # Drain already delivered sensor/work-queue callbacks.
        fixture.client('research_stabilized_sitl', 'dispatch')
        time.sleep(0.05)  # Prime the last successful control time at the stopped clock.
        baseline = campaigns.rows(fixture.work / 'control.csv')
        before = campaigns.rows(fixture.work / 'control.csv.events.csv')
        assert baseline
        stamp = int(baseline[-1]['time'])
        virtual_time = fixture.now()
        for _ in range(REPEATS):
            fixture.client('research_stabilized_sitl', 'dispatch')
            time.sleep(0.02)
        after = campaigns.rows(fixture.work / 'control.csv')
        events = campaigns.rows(fixture.work / 'control.csv.events.csv')
        skipped = events[len(before):]
        assert fixture.now() == virtual_time, 'Physics advanced during frozen-clock regression'
        assert len(after) == len(baseline), 'Duplicate dispatch published a control update'
        assert len(skipped) >= REPEATS and all(row['action'] == 'dispatch_not_due'
               and int(row['time']) == stamp and row['status'] == '4' for row in skipped)
        status, _ = fixture.client('research_stabilized_sitl', 'status')
        assert 'state=3' in status and 'fault=0' in status
        fixture.result.update(frozen_control_time_us=stamp, frozen_physics_time_us=virtual_time,
                              published_before=len(baseline), published_after=len(after),
                              duplicate_dispatches=len(skipped), requested_dispatches=REPEATS)
        set_paused(fixture, False)
        paused = False
        fixture.wait(200000)
        status, _ = fixture.client('research_stabilized_sitl', 'status')
        assert 'state=3' in status and 'fault=0' in status, 'Resume faulted'
        fixture.client('research_stabilized_sitl', 'disable')
        fixture.wait(300000)
    except Exception as error:
        fixture.result['error'] = str(error)
        raise
    finally:
        try:
            if paused and fixture.gz is not None and fixture.gz.poll() is None:
                set_paused(fixture, False)
        finally:
            fixture.close()
    truth = campaigns.rows(fixture.work / 'truth.csv')
    assert truth[-1]['thrust'] == '0' and truth[-1]['lease_fault'] == '1'
    active = [row for row in truth if stamp <= int(row['time']) <= stamp + 100000]
    assert active and all(row['enabled'] == '1' and row['lease_fault'] == '0' for row in active), \
        'Consumer rejected a replay or lost the resumed session'
    fixture.result.update(passed=True, audited_control_cycles=audit.audit_cycles(fixture.work / 'control.csv'))
    (fixture.work / 'result.json').write_text(json.dumps(fixture.result, indent=2) + '\n')
    print('PASS deterministic native duplicate dispatch:', fixture.work, fixture.result, flush=True)


def seal(out):
    paths = sorted(out.glob('dispatch-*/result.json'), key=lambda path: path.stat().st_mtime_ns)
    assert paths, 'No dispatch regression'
    path = paths[-1]
    result = json.loads(path.read_text())
    assert result['passed'] and not result.get('exploratory_only')
    assert result['dispatch_source_sha256'] == base.digest(Path(__file__))
    assert result['duplicate_dispatches'] >= REPEATS
    assert result['published_before'] == result['published_after']
    audit.audit_runtime_profile(path.parent, result)
    for name, identity in result['source_sha256'].items():
        assert base.digest(base.ROOT / name) == base.digest(path.parent / 'source-inputs' / name) == identity
    assert result['px4_sha256'] == base.digest(out / 'sitl-lockstep-build/bin/px4')
    assert result['plugin_sha256'] == base.digest(out / 'plugin-build/libresearch_rotational_fixture.so')
    audit.audit_cycles(path.parent / 'control.csv')
    files = {str(item.relative_to(out)): base.digest(item) for item in path.parent.rglob('*') if item.is_file()}
    (out / 'dispatch-manifest.json').write_text(json.dumps(dict(
        scope='one_native_frozen_clock_duplicate_dispatch_only', accepted=True,
        physical_authority_granted=False, result=result, files=files), indent=2) + '\n')
    print('Dispatch manifest:', base.digest(out / 'dispatch-manifest.json'), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--evidence', type=Path, required=True)
    parser.add_argument('--seal', action='store_true')
    args = parser.parse_args()
    out = args.evidence.resolve()
    assert out.parent == Path('/var/tmp') and out.name.startswith('research-stabilized-sitl-')
    if args.seal:
        seal(out)
    else:
        regression(out)


if __name__ == '__main__':
    main()
