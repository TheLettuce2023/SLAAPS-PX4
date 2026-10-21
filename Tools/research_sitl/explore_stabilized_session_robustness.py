#!/usr/bin/env python3
"""Explore coupled response across declared noise seeds without promoting results."""
import argparse
import json
from pathlib import Path
import shutil
import tempfile
import traceback

import verify_stabilized_campaigns as campaigns
import verify_stabilized_coupled as coupled
from verify_stabilized_session_maneuvers import SessionFixture
from qualify_stabilized_session import digest, verify


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--qualification', type=Path, required=True)
    args = parser.parse_args()
    qualification = args.qualification.resolve()
    assert qualification.parent == Path('/var/tmp') and qualification.name.startswith('research-stabilized-session-qualification-')
    verify(qualification / 'qualification-manifest.json')
    summary = json.loads((qualification / 'qualification.json').read_text())
    assert summary['passed'] and summary['completed_cases'] == 30
    tools = Path(__file__).resolve().parent
    for saved in qualification.glob('*.py'):
        assert digest(tools / saved.name) == digest(saved)
    build = Path(summary['build'])
    assert digest(build / 'session-link-manifest.json') == summary['build_receipt_sha256']
    verify(build / 'session-link-manifest.json')
    single = next(r for r in summary['results'] if r['case'] == 'single_axis')
    original = Path(single['directory'])
    assert digest(original / single['receipt_name']) == single['receipt_sha256']
    verify(original / single['receipt_name'])
    out = Path(tempfile.mkdtemp(prefix='research-stabilized-session-robustness-', dir='/var/tmp'))
    print(out, flush=True)
    (out / 'sitl-lockstep-build').symlink_to(build / 'sitl-lockstep-build', target_is_directory=True)
    (out / 'px4-source').symlink_to(build / 'px4-source', target_is_directory=True)
    (out / 'plugin-build').mkdir()
    (out / 'plugin-build/libresearch_rotational_fixture.so').symlink_to(build / 'session-plugin-build/libresearch_rotational_session.so')
    shutil.copy2(original / 'synthetic.profile', out / 'synthetic.profile')
    campaigns.Fixture = SessionFixture
    results = []
    for seed in (7, 13, 73):
        for up in (False, True):
            for sign in (-1, 1):
                item = dict(seed=seed, nose_up=up, sign=sign, meets_existing_criteria=False)
                print('RUN', item, flush=True)
                try:
                    coupled.coupled(out, up, sign, seed, dispatch_diagnostics=False)
                    item['meets_existing_criteria'] = True
                except Exception:
                    item['error'] = traceback.format_exc()
                    print(item['error'], flush=True)
                results.append(item)
                status = dict(scope='additional_seed_exploration_not_qualification', qualification=str(qualification),
                              qualification_sha256=digest(qualification / 'qualification-manifest.json'),
                              build=str(build), seeds=[7, 13, 73], planned_cases=12,
                              completed_cases=len(results), results=results)
                (out / 'exploration.json').write_text(json.dumps(status, indent=2) + '\n')
    shutil.copy2(__file__, out / Path(__file__).name)
    files = {str(p.relative_to(out)): digest(p) for p in out.rglob('*') if p.is_file() and not p.is_symlink()
             and not any(part in ('sitl-lockstep-build','px4-source') for part in p.relative_to(out).parts)}
    receipt = out / 'exploration-manifest.json'
    receipt.write_text(json.dumps(dict(files=files), indent=2) + '\n')
    verify(receipt)
    print('SEALED', len(files), 'files; receipt', digest(receipt), flush=True)
    return 0 if all(r['meets_existing_criteria'] for r in results) else 1


if __name__ == '__main__':
    raise SystemExit(main())
