#!/usr/bin/env python3
"""Run unchanged single-axis tracking/impulse criteria on the integrated owner."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import tempfile
import traceback
import uuid

import verify_stabilized_campaigns as campaign
import verify_stabilized_coupled as coupled
from pymavlink import mavutil

ROOT = Path(__file__).resolve().parents[2]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


class SessionFixture(campaign.Fixture):
    def __init__(self, evidence, name, nose_up=False, **kwargs):
        super().__init__(evidence, name, nose_up, **kwargs)
        self.instance = 29480
        self.connection.close()
        self.connection = mavutil.mavlink_connection('udpout:127.0.0.1:19785', source_system=255, source_component=190)
        namespace = '/slaaps_session/' + uuid.uuid4().hex
        self.world.write_text(self.world.read_text()
            .replace('research_stabilized_fixture::RotationalFixture', 'research_stabilized_fixture::RotationalSessionFixture')
            .replace('<truth_csv>', '<session_namespace>' + namespace + '</session_namespace><truth_csv>'))
        self.script.write_text(self.script.read_text()
            .replace('param set COM_RC_IN_MODE 1', 'param set COM_RC_IN_MODE 1\nparam set COM_PREARM_MODE 2')
            .replace('19779', '19785').replace('19780', '19786')
            .replace('research_stabilized_sitl start slaaps_fixture',
                     'research_session_sitl start ' + str(evidence / 'synthetic.profile') + ' ' + namespace))
        self.result['owner'] = 'separately_scheduled_session_producer_consumer'
        self.result['namespace'] = namespace

    def client(self, module, *args, check=True):
        if module == 'research_stabilized_sitl':
            module = 'research_session_sitl'
        return super().client(module, *args, check=check)

    def start(self):
        super().start()
        output, code = self.client('param', 'compare', 'COM_PREARM_MODE', '2', check=False)
        assert code == 0, output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--evidence', type=Path, required=True)
    parser.add_argument('--single', action='store_true', help='Run nose-up positive roll only')
    parser.add_argument('--coupled', action='store_true', help='Run four full-stick coupled reversal/impulse cases')
    args = parser.parse_args()
    if args.single and args.coupled:
        parser.error('--single and --coupled are mutually exclusive')
    build = args.evidence.resolve()
    assert build.parent == Path('/var/tmp') and build.name.startswith('research-stabilized-sitl-')
    receipt = build / 'session-link-manifest.json'
    manifest = json.loads(receipt.read_text())
    for name, identity in manifest['files'].items():
        assert digest(build / name) == identity, name
    out = Path(tempfile.mkdtemp(prefix='research-stabilized-session-maneuvers-', dir='/var/tmp'))
    print(out, flush=True)
    (out / 'sitl-lockstep-build').symlink_to(build / 'sitl-lockstep-build', target_is_directory=True)
    (out / 'px4-source').symlink_to(build / 'px4-source', target_is_directory=True)
    (out / 'plugin-build').mkdir()
    (out / 'plugin-build/libresearch_rotational_fixture.so').symlink_to(build / 'session-plugin-build/libresearch_rotational_session.so')
    # Same explicit synthetic profile as the integrated runtime smoke tests.
    source = (ROOT / 'src/lib/research_stabilized_manual/ObserverProfileTest.cpp').read_text()
    text = source.split('const char *fixture =', 1)[1].split(';', 1)[0]
    tokens = ''.join(re.findall(r'"([^"]*)"', text)).split()
    assert len(tokens) == 86
    tokens[1] = '0'
    for axis, (inertia, damping, p, d) in enumerate(zip([.03, .08, .08], [.03, .04, .04], [.4, .8, .8], [.003, .006, .006])):
        inertia /= .4
        damping /= .4
        tokens[35 + axis] = str(.4 * (damping + p - .4 * (inertia + .05 * damping + d) + .16 * .05 * inertia))
    (out / 'synthetic.profile').write_text(' '.join(tokens) + '\n')
    campaign.Fixture = SessionFixture
    cases = [(True, 0, 1)] if args.single else [(up, axis, sign) for up in (False, True) for axis in range(3) for sign in (-1, 1)]
    if args.coupled:
        cases = [(up, None, sign) for up in (False, True) for sign in (-1, 1)]
    results = []
    for up, axis, sign in cases:
        result = dict(nose_up=up, axis=axis, direction=sign, passed=False)
        try:
            if args.coupled:
                coupled.coupled(out, up, sign, dispatch_diagnostics=False)
            else:
                campaign.campaign(out, axis, up, sign)
            result['passed'] = True
        except Exception:
            result['error'] = traceback.format_exc()
            print(result['error'], flush=True)
        results.append(result)
        (out / 'summary.json').write_text(json.dumps(dict(scope=('coupled_integrated_maneuvers_not_full_campaign' if args.coupled else 'single_axis_integrated_maneuvers_not_full_campaign'),
            build=str(build), build_receipt_sha256=digest(receipt), passed=all(r['passed'] for r in results),
            planned_cases=len(cases), results=results), indent=2) + '\n')
    shutil.copy2(__file__, out / Path(__file__).name)
    files = {str(p.relative_to(out)): digest(p) for p in out.rglob('*') if p.is_file() and not p.is_symlink()
             and not any(part in ('sitl-lockstep-build', 'px4-source') for part in p.relative_to(out).parts)}
    (out / 'maneuver-manifest.json').write_text(json.dumps(dict(files=files), indent=2) + '\n')
    assert all(digest(out / name) == identity for name, identity in files.items())
    print('Verified', len(files), 'files; receipt', digest(out / 'maneuver-manifest.json'), flush=True)
    return 0 if all(r['passed'] for r in results) else 1


if __name__ == '__main__':
    raise SystemExit(main())
