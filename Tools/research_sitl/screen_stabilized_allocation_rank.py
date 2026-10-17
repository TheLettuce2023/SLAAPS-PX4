#!/usr/bin/env python3
"""Conservative single-condition rank screen for reported allocation intervals."""
import argparse
from decimal import Decimal
from fractions import Fraction
import hashlib
import json
import math
from pathlib import Path
import sys

import compile_stabilized_allocation as compiler


def rational(value, name):
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ValueError(name + ' must be finite numeric data')
    try:
        finite = math.isfinite(value)
    except OverflowError as error:
        raise ValueError(name + ' must be finite numeric data') from error
    if not finite:
        raise ValueError(name + ' must be finite numeric data')
    return Fraction(Decimal(str(value)))


def matrix(value, name):
    if not isinstance(value, list) or len(value) != 3:
        raise ValueError(name + ' must have three rows')
    result = []
    for axis, row in enumerate(value):
        if not isinstance(row, list) or len(row) != 4:
            raise ValueError(f'{name}[{axis}] must have four columns')
        result.append([rational(item, f'{name}[{axis}][{channel}]')
                       for channel, item in enumerate(row)])
    return result


def screen_matrices(effectiveness, uncertainty):
    """Exact rational Gershgorin lower bound for every matrix in A +/- U."""
    a = matrix(effectiveness, 'effectiveness')
    u = matrix(uncertainty, 'uncertainty')
    if any(item < 0 for row in u for item in row):
        raise ValueError('effectiveness uncertainty must be nonnegative')
    gram = [[sum(a[i][k] * a[j][k] for k in range(4)) for j in range(3)]
            for i in range(3)]
    error = [[sum(abs(a[i][k]) * u[j][k] + u[i][k] * abs(a[j][k])
                  + u[i][k] * u[j][k] for k in range(4)) for j in range(3)]
             for i in range(3)]
    row_lower = [gram[i][i] - error[i][i]
                 - sum(abs(gram[i][j]) + error[i][j] for j in range(3) if j != i)
                 for i in range(3)]
    lower = min(row_lower)
    zero_in_box = all(u[i][k] >= abs(a[i][k]) for i in range(3) for k in range(4))
    status = ('sufficient_full_row_rank_for_reported_intervals' if lower > 0 else
              'interval_box_contains_rank_deficient_zero_matrix' if zero_in_box else
              'inconclusive')
    return dict(status=status, gram_eigenvalue_lower_numerator=lower.numerator,
                gram_eigenvalue_lower_denominator=lower.denominator,
                gram_eigenvalue_lower_approx=float(lower),
                interval_box_contains_zero_matrix=zero_in_box,
                method='Gershgorin of A*A_transpose with exact rational elementwise Gram error bounds',
                assumptions='independent reported elementwise intervals at one declared condition')


def read_json(path):
    return json.loads(path.read_text(), parse_constant=compiler.reject_constant,
                      object_pairs_hook=compiler.unique_object)


def check_provenance(profile, evidence_root):
    root = evidence_root.resolve(strict=True)
    identities = {}
    for role, record in profile['provenance'].items():
        reference = Path(record['reference'])
        if reference.is_absolute() or '..' in reference.parts:
            raise ValueError('provenance path must stay within evidence root: ' + role)
        path = (root / reference).resolve(strict=True)
        if not path.is_relative_to(root) or not path.is_file():
            raise ValueError('provenance path must be a file within evidence root: ' + role)
        identity = hashlib.sha256(path.read_bytes()).hexdigest()
        if identity != record['sha256']:
            raise ValueError('provenance hash mismatch: ' + role)
        identities[role] = dict(reference=str(reference), sha256=identity)
    return identities


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', type=Path, required=True)
    parser.add_argument('--compiled', type=Path, required=True)
    parser.add_argument('--evidence-root', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    try:
        if args.output.exists():
            raise ValueError('rank-screen output already exists')
        source_bytes = args.input.read_bytes()
        profile = json.loads(source_bytes, parse_constant=compiler.reject_constant,
                             object_pairs_hook=compiler.unique_object)
        recomputed = compiler.compile_profile(profile)
        recomputed['input_sha256'] = hashlib.sha256(source_bytes).hexdigest()
        report = read_json(args.compiled)
        if json.dumps(report, sort_keys=True, allow_nan=False) != json.dumps(
                recomputed, sort_keys=True, allow_nan=False):
            raise ValueError('compiled report differs from the source profile')
        sources = check_provenance(profile, args.evidence_root)
        rank = screen_matrices(report['allocator_inputs']['effectiveness'],
                               report['effectiveness_abs_uncertainty'])
        result = dict(scope='single_condition_elementwise_rank_screen_only',
                      input_sha256=recomputed['input_sha256'],
                      compiled_sha256=hashlib.sha256(args.compiled.read_bytes()).hexdigest(),
                      condition=report['condition'], provenance=sources,
                      vehicle_qualified=False, physical_acceptance=False,
                      full_envelope_qualified=False, **rank)
        args.output.write_text(json.dumps(result, indent=2, allow_nan=False) + '\n')
    except (OSError, ValueError, TypeError, OverflowError) as error:
        print(f'Allocation rank screen rejected: {error}', file=sys.stderr)
        return 2
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
