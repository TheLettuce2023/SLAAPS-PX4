#!/usr/bin/env python3
"""Locate physical attitude peaks before/after the injected coupled impulse."""
import argparse
import json
from pathlib import Path
import shutil
import tempfile

from qualify_stabilized_session import digest, verify
from verify_stabilized_campaigns import rows, quaternion, separation


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--qualification', type=Path, required=True)
    args = parser.parse_args()
    qualification = args.qualification.resolve()
    verify(qualification / 'qualification-manifest.json')
    result = json.loads((qualification / 'qualification.json').read_text())
    assert result['passed']
    item = next(r for r in result['results'] if r['case'] == 'coupled')
    evidence = Path(item['directory'])
    receipt = evidence / item['receipt_name']
    assert digest(receipt) == item['receipt_sha256']
    verify(receipt)
    results = []
    for folder in sorted(evidence.glob('coupled-*')):
        control, truth = rows(folder / 'control.csv'), rows(folder / 'truth.csv')
        rate_end = max(int(r['time']) for r in control if r['state'] == '2')
        release = next(r for r in control if r['state'] == '3' and int(r['time']) > rate_end)
        stamp = int(release['att_sample'])
        reference = quaternion(min(truth, key=lambda r: abs(int(r['time']) - stamp)))
        impulse = next(int(r['time']) for r in truth if any(abs(float(r['impulse_' + a])) > 0 for a in 'xyz'))
        end = int(control[-1]['time'])
        samples = [(separation(quaternion(r), reference), int(r['time'])) for r in truth if stamp <= int(r['time']) <= end]
        before = max((angle, time) for angle, time in samples if time < impulse)
        after = max((angle, time) for angle, time in samples if time >= impulse)
        # Report observed command/plant timing without inferring causation.
        window = [r for r in control if int(release['time']) <= int(r['time']) < impulse]
        physical = [r for r in truth if int(release['time']) <= int(r['time']) < impulse]
        axes = {}
        for axis in 'xyz':
            direction = 1 if float(release['w' + axis]) >= 0 else -1
            def crossing(records, field):
                match = next((r for r in records if direction * float(r[field]) <= 0), None)
                return None if match is None else int(match['time']) - int(release['time'])
            axes[axis] = dict(rate_at_release_rad_s=float(release['w' + axis]),
                setpoint_at_release_rad_s=float(release['s' + axis]),
                setpoint_zero_delay_us=crossing(window, 's' + axis),
                requested_moment_zero_delay_us=crossing(window, 'm' + axis),
                allocated_moment_zero_delay_us=crossing(window, 'a' + axis),
                physical_rate_zero_delay_us=crossing(physical, 'w' + axis))
        results.append(dict(case=folder.name, release_axes=axes, release_peak_rad=before[0],
            release_peak_delay_us=before[1] - stamp, post_impulse_peak_rad=after[0],
            impulse_delay_us=impulse - stamp, maximum_before_impulse=before[0] > after[0]))
    assert len(results) == 4
    out = Path(tempfile.mkdtemp(prefix='research-stabilized-session-release-analysis-', dir='/var/tmp'))
    (out / 'analysis.json').write_text(json.dumps(dict(scope='physical_peak_phase_diagnosis_no_tuning',
        qualification_sha256=digest(qualification / 'qualification-manifest.json'),
        coupled_receipt_sha256=digest(receipt), results=results), indent=2) + '\n')
    shutil.copy2(__file__, out / Path(__file__).name)
    files = {p.name: digest(p) for p in out.iterdir() if p.is_file()}
    (out / 'analysis-manifest.json').write_text(json.dumps(dict(files=files), indent=2) + '\n')
    verify(out / 'analysis-manifest.json')
    print(out, 'receipt', digest(out / 'analysis-manifest.json'), flush=True)
    print(json.dumps(results, indent=2), flush=True)


if __name__ == '__main__':
    main()
