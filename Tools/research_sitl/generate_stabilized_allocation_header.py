#!/usr/bin/env python3
"""Emit a private synthetic SITL C++ allocation header from a sealed report."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import sys

import compile_stabilized_allocation as compiler


ROOT = Path(__file__).resolve().parents[2]
EXAMPLE = ROOT / 'Tools/research_sitl/stabilized_allocation_synthetic.json'
EXPECTED_ORDER = ['synthetic_0', 'synthetic_1', 'synthetic_2', 'synthetic_3']
FIELDS = ('trim', 'minimum', 'maximum', 'positive_slew_per_s', 'negative_slew_per_s')


def literal(value):
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
        raise ValueError('nonfinite generated allocation value')
    result = format(float(value), '.17g')
    if '.' not in result and 'e' not in result:
        result += '.0'
    return result + 'F'


def render(report, expected_sha):
    if not isinstance(report, dict):
        raise ValueError('compiled allocation report must be an object')
    content = EXAMPLE.read_bytes()
    actual_sha = hashlib.sha256(content).hexdigest()
    if expected_sha != actual_sha:
        raise ValueError('synthetic input hash changed')
    source = json.loads(content, parse_constant=compiler.reject_constant,
                        object_pairs_hook=compiler.unique_object)
    recomputed = compiler.compile_profile(source)
    recomputed['input_sha256'] = actual_sha
    if json.dumps(report, sort_keys=True, allow_nan=False) != json.dumps(
            recomputed, sort_keys=True, allow_nan=False):
        raise ValueError('compiled allocation report differs from the synthetic source')
    if (report.get('profile_kind') != 'synthetic_fixture'
            or report.get('input_sha256') != expected_sha
            or report.get('vehicle_qualified') is not False
            or report.get('physical_acceptance') is not False):
        raise ValueError('only the exact unqualified synthetic fixture input is allowed')
    values = report.get('allocator_inputs')
    if not isinstance(values, dict) or values.get('canard_order') != EXPECTED_ORDER:
        raise ValueError('synthetic canard order changed')
    matrix = values.get('effectiveness')
    if not isinstance(matrix, list) or len(matrix) != 3 or any(
            not isinstance(row, list) or len(row) != 4 for row in matrix):
        raise ValueError('expected 3x4 effectiveness matrix')
    for name in FIELDS:
        if not isinstance(values.get(name), list) or len(values[name]) != 4:
            raise ValueError('missing four-channel allocation field: ' + name)
    rows = [', '.join(literal(value) for value in row) for row in matrix]
    lines = ['#pragma once', '',
             '// Private synthetic SITL data. Never use as measured SLAAPS authority.',
             'namespace research_stabilized_fixture::generated', '{',
             f'inline constexpr char kInputSha256[] = "{expected_sha}";',
             'inline constexpr float kEffectiveness[3][4] = {']
    lines += ['\t{' + row + '},' for row in rows]
    lines.append('};')
    for name in FIELDS:
        elements = ', '.join(literal(value) for value in values[name])
        cpp_name = ''.join(part.capitalize() for part in name.split('_'))
        lines.append(f'inline constexpr float k{cpp_name}[4] = {{{elements}}};')
    lines += ['} // namespace research_stabilized_fixture::generated', '']
    return '\n'.join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiled', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    try:
        if args.output.exists():
            raise ValueError('generated header path already exists')
        expected_sha = hashlib.sha256(EXAMPLE.read_bytes()).hexdigest()
        report = json.loads(args.compiled.read_text(), parse_constant=compiler.reject_constant,
                            object_pairs_hook=compiler.unique_object)
        args.output.write_text(render(report, expected_sha))
    except (OSError, ValueError, TypeError) as error:
        print(f'Generated allocation rejected: {error}', file=sys.stderr)
        return 2
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
