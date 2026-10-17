#!/usr/bin/env python3
"""Sufficient static incremental moment-box screen at one declared condition."""
import argparse
from fractions import Fraction
import hashlib
import json
from pathlib import Path
import sys

import compile_stabilized_allocation as compiler
import screen_stabilized_allocation_rank as rank_screen


DEMAND_FIELDS = ('schema_version', 'purpose', 'profile_input_sha256', 'body_frame',
                 'quantity', 'axis_abs_bound')
QUANTITY = 'incremental_normalized_moment_about_declared_trim'


def exact_object(value, fields, name):
    if not isinstance(value, dict) or set(value) != set(fields):
        raise ValueError(name + ' must contain exactly: ' + ', '.join(fields))
    return value


def fraction_record(value):
    return dict(numerator=value.numerator, denominator=value.denominator)


def screen_box(report, demand):
    exact_object(demand, DEMAND_FIELDS, 'demand')
    if type(demand['schema_version']) is not int or demand['schema_version'] != 1:
        raise ValueError('demand schema_version must be 1')
    if demand['profile_input_sha256'] != report['input_sha256']:
        raise ValueError('demand profile hash differs from compiled input')
    if demand['body_frame'] != 'FRD' or demand['quantity'] != QUANTITY:
        raise ValueError('demand must be an incremental normalized body-FRD moment box')
    expected_purpose = ('synthetic_fixture_demonstration_only'
                        if report['profile_kind'] == 'synthetic_fixture' else
                        'candidate_vehicle_requirement_unreviewed')
    if demand['purpose'] != expected_purpose:
        raise ValueError('demand purpose does not match profile kind')
    bounds = demand['axis_abs_bound']
    if not isinstance(bounds, list) or len(bounds) != 3:
        raise ValueError('demand axis_abs_bound must have three entries')
    requested = [rank_screen.rational(value, f'demand.axis_abs_bound[{axis}]')
                 for axis, value in enumerate(bounds)]
    if any(value < 0 for value in requested) or not any(value > 0 for value in requested):
        raise ValueError('demand box must have nonnegative bounds and at least one positive axis')
    allocation = report['allocator_inputs']
    trim = [rank_screen.rational(value, 'trim') for value in allocation['trim']]
    minimum = [rank_screen.rational(value, 'minimum') for value in allocation['minimum']]
    maximum = [rank_screen.rational(value, 'maximum') for value in allocation['maximum']]
    if not (len(trim) == len(minimum) == len(maximum) == 4):
        raise ValueError('allocation must contain four bounded canards')
    radius = min(margin for channel in range(4)
                 for margin in (trim[channel] - minimum[channel],
                                maximum[channel] - trim[channel]))
    if radius <= 0:
        raise ValueError('declared trim must lie strictly inside every canard bound')
    rank = rank_screen.screen_matrices(allocation['effectiveness'],
                                       report['effectiveness_abs_uncertainty'])
    lower = Fraction(rank['gram_eigenvalue_lower_numerator'],
                     rank['gram_eigenvalue_lower_denominator'])
    demand_squared = sum(value * value for value in requested)
    guaranteed_squared = radius * radius * lower if lower > 0 else Fraction(0)
    status = ('rank_not_proven_for_reported_intervals' if lower <= 0 else
              'sufficient_static_incremental_box_reachable' if demand_squared <= guaranteed_squared else
              'inconclusive_for_declared_demand_box')
    return dict(status=status, rank_status=rank['status'],
                rank_gram_lower=fraction_record(lower),
                symmetric_command_radius=fraction_record(radius),
                demand_norm_squared=fraction_record(demand_squared),
                guaranteed_norm_squared=fraction_record(guaranteed_squared),
                method='minimum-norm right inverse with a centered Euclidean actuator ball',
                assumptions='single-condition local linear incremental moments; reported elementwise intervals; static commands only',
                vehicle_qualified=False, physical_acceptance=False,
                full_envelope_qualified=False, slew_and_dynamics_qualified=False,
                implemented_allocator_tracking_qualified=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', type=Path, required=True)
    parser.add_argument('--compiled', type=Path, required=True)
    parser.add_argument('--evidence-root', type=Path, required=True)
    parser.add_argument('--demand', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    try:
        if args.output.exists():
            raise ValueError('static authority output already exists')
        source_bytes = args.input.read_bytes()
        profile = json.loads(source_bytes, parse_constant=compiler.reject_constant,
                             object_pairs_hook=compiler.unique_object)
        recomputed = compiler.compile_profile(profile)
        recomputed['input_sha256'] = hashlib.sha256(source_bytes).hexdigest()
        report = rank_screen.read_json(args.compiled)
        if json.dumps(report, sort_keys=True, allow_nan=False) != json.dumps(
                recomputed, sort_keys=True, allow_nan=False):
            raise ValueError('compiled report differs from source profile')
        provenance = rank_screen.check_provenance(profile, args.evidence_root)
        demand = rank_screen.read_json(args.demand)
        result = screen_box(report, demand)
        result.update(scope='single_condition_static_incremental_moment_box_screen_only',
                      input_sha256=recomputed['input_sha256'],
                      compiled_sha256=hashlib.sha256(args.compiled.read_bytes()).hexdigest(),
                      demand_sha256=hashlib.sha256(args.demand.read_bytes()).hexdigest(),
                      condition=report['condition'], provenance=provenance,
                      demand=dict(body_frame='FRD', quantity=QUANTITY,
                                  axis_abs_bound=demand['axis_abs_bound'],
                                  purpose=demand['purpose']))
        args.output.write_text(json.dumps(result, indent=2, allow_nan=False) + '\n')
    except (OSError, ValueError, TypeError, KeyError, OverflowError) as error:
        print(f'Static authority screen rejected: {error}', file=sys.stderr)
        return 2
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
