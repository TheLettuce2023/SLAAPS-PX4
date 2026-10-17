#!/usr/bin/env python3
"""Aggregate the explicitly scoped first-operator GUI/headless checkpoint."""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import sys

from acceptance import artifact, check_run, reference, utc
from inspect_raw_ulog import digest, load_json, require
from score_interval import REGISTRY_SHA256, registry


def check_checkpoint(root):
    root = root.resolve()
    plan = load_json(root / 'campaign-plan.json')
    require(plan['campaign_id'] == registry()['campaign_id'] and plan['registry_sha256'] == REGISTRY_SHA256, 'checkpoint campaign/policy mismatch')
    expected = plan['planned_runs']
    require(len(expected) == 2 and [row['mode'] for row in expected] == ['gui', 'headless'],
            'checkpoint requires one serialized GUI/headless pair')
    require(len({row['run_id'] for row in expected}) == 2, 'duplicate run IDs')
    results, manifests, plans, lives = [], [], [], []
    for row in expected:
        run = (root / row['run_id']).resolve()
        require(run.is_relative_to(root), 'run path escapes package')
        path = run / 'manifest.json'
        if not path.exists():
            results.append({'run_id': row['run_id'], 'status': 'incomplete', 'missing': ['run manifest absent']})
            continue
        manifest = load_json(path)
        run_plan = load_json(artifact(root, manifest['plan']))
        require(run_plan['run_id'] == row['run_id'] and run_plan['mode'] == row['mode'], 'planned run mismatch')
        result = check_run(root, path)
        results.append(result)
        manifests.append(manifest)
        plans.append(run_plan)
        lives.append((load_json(artifact(root, manifest['artifacts']['launch'])),
                      load_json(artifact(root, manifest['artifacts']['lifecycle']))))
    if len(plans) == 2:
        require(plans[0]['operator'] == plans[1]['operator'], 'first-operator identity mismatch')
        for key in ('source_index', 'source_archive', 'executable', 'parameters', 'registry'):
            require(plans[0]['artifacts'][key]['sha256'] == plans[1]['artifacts'][key]['sha256'],
                    'GUI/headless input mismatch: ' + key)
        require(utc(lives[0][1]['finished_utc']) <= utc(lives[1][0]['utc']), 'GUI/headless runs overlap')
    status = 'fail' if any(r['status'] == 'fail' for r in results) else (
        'pass' if all(r['status'] == 'pass' for r in results) else 'incomplete')
    automated = 'pass' if all(r.get('signals', {}).get('status') == 'pass' and not r.get('failures') and all(m == 'human QGC observation pending' for m in r.get('missing', [])) for r in results) else status
    pending = ['second-operator reproduction deferred by user']
    pending.extend(r.get('run_id', 'run') + ': ' + m for r in results for m in r.get('missing', []))
    environment = {name: reference(root / name, root) for name in (
        'host-environment.json', 'analysis-environment.json', 'signal-topic-inventory.json')}
    for filename, fields in (
            ('host-environment.json', ('windows', 'wsl_version', 'qgc_version')),
            ('analysis-environment.json', ('python', 'pyulog', 'numpy'))):
        record = load_json(root / filename)
        require(all(type(record.get(key)) is str and record[key].strip() for key in fields),
                'missing environment version: ' + filename)
    inventory = load_json(root / 'signal-topic-inventory.json')
    require({row['topic'] for row in inventory['topics']} == set(registry()['required_raw_topics_instance_0']),
            'signal inventory topic mismatch')
    require(all(type(row.get(key)) is str and row[key].strip() for row in inventory['topics']
                for key in ('units', 'frame', 'publisher', 'consumer')), 'incomplete signal provenance')
    return {'schema': 'stock-cessna-first-operator-result/v1', 'created_utc': datetime.now(timezone.utc).isoformat(),
            'status': status, 'scope': 'first-operator stock Cessna diagnostic checkpoint',
            'phase1_result': 'incomplete', 'phase1_pending': pending, 'automated_checks_status': automated,
            'campaign_id': plan['campaign_id'], 'campaign_plan_sha256': digest(root / 'campaign-plan.json'),
            'runs': results, 'environment': environment,
            'prior_incomplete_attempts': plan.get('prior_incomplete_attempts', []),
            'checker_sha256': digest(Path(__file__))}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    try:
        result = check_checkpoint(args.root)
    except Exception as error:
        result = {'status': 'fail', 'phase1_result': 'incomplete', 'reason': str(error)}
    with args.output.open('x', encoding='utf-8') as stream:
        json.dump(result, stream, indent=2, allow_nan=False)
        stream.write('\n')
    print(json.dumps({'status': result['status'], 'automated_checks_status': result.get('automated_checks_status'), 'phase1_result': 'incomplete',
                      'runs': [{'run_id': r.get('run_id'), 'status': r['status'],
                                'missing': r.get('missing', []), 'failure_count': len(r.get('failures', []))}
                               for r in result.get('runs', [])], 'reason': result.get('reason')}))
    return {'pass': 0, 'fail': 2, 'incomplete': 3}[result['status']]


if __name__ == '__main__':
    sys.exit(main())
