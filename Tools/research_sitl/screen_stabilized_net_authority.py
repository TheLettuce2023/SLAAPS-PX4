#!/usr/bin/env python3
"""Sufficient static net-moment screen including uncertain trim bias."""
import argparse
from fractions import Fraction
import hashlib
import json
from pathlib import Path
import sys

import compile_stabilized_allocation as compiler
import screen_stabilized_allocation_rank as rank_screen
import screen_stabilized_static_authority as static_screen


FIELDS = ('schema_version', 'purpose', 'profile_input_sha256', 'body_frame',
          'condition', 'canard_trim_commands', 'provenance', 'bias_nm_frd',
          'bias_abs_uncertainty_nm_frd', 'desired_net_moment_abs_bound_nm_frd')
PROVENANCE_ROLES = ('bias', 'demand')


def triple(value, name, nonnegative=False, positive=False):
    if not isinstance(value, list) or len(value) != 3:
        raise ValueError(name + ' must have three entries')
    result = [rank_screen.rational(item, f'{name}[{axis}]')
              for axis, item in enumerate(value)]
    if nonnegative and any(item < 0 for item in result):
        raise ValueError(name + ' must be nonnegative')
    if positive and any(item <= 0 for item in result):
        raise ValueError(name + ' must be positive on every axis')
    return result


def screen_net_box(profile, report, specification):
    static_screen.exact_object(specification, FIELDS, 'net authority specification')
    if type(specification['schema_version']) is not int or specification['schema_version'] != 1:
        raise ValueError('net authority schema_version must be 1')
    if specification['profile_input_sha256'] != report['input_sha256']:
        raise ValueError('net authority profile hash differs from compiled input')
    if specification['body_frame'] != 'FRD' or specification['condition'] != report['condition']:
        raise ValueError('net authority condition must match the body-FRD profile')
    expected_purpose = ('synthetic_fixture_demonstration_only'
                        if report['profile_kind'] == 'synthetic_fixture' else
                        'candidate_vehicle_requirement_unreviewed')
    if specification['purpose'] != expected_purpose:
        raise ValueError('net authority purpose does not match profile kind')
    provenance = static_screen.exact_object(specification['provenance'], PROVENANCE_ROLES,
                                             'net authority provenance')
    for role, record in provenance.items():
        static_screen.exact_object(record, ('reference', 'sha256'), role + ' provenance')
        if not isinstance(record['reference'], str) or not isinstance(record['sha256'], str):
            raise ValueError(role + ' provenance must contain string reference and SHA-256')
        if len(record['sha256']) != 64 or any(ch not in '0123456789abcdef' for ch in record['sha256']):
            raise ValueError(role + ' provenance must contain lowercase SHA-256')
    allocation = report['allocator_inputs']
    trim = [rank_screen.rational(value, 'trim') for value in allocation['trim']]
    minimum = [rank_screen.rational(value, 'minimum') for value in allocation['minimum']]
    maximum = [rank_screen.rational(value, 'maximum') for value in allocation['maximum']]
    declared_trim = specification['canard_trim_commands']
    if not isinstance(declared_trim, list) or len(declared_trim) != 4:
        raise ValueError('canard_trim_commands must have four entries')
    if [rank_screen.rational(value, 'canard_trim_commands') for value in declared_trim] != trim:
        raise ValueError('net authority trim differs from allocation trim')
    if not (len(trim) == len(minimum) == len(maximum) == 4):
        raise ValueError('allocation must contain four bounded canards')
    radius = min(margin for channel in range(4)
                 for margin in (trim[channel] - minimum[channel],
                                maximum[channel] - trim[channel]))
    if radius <= 0:
        raise ValueError('declared trim must lie strictly inside every canard bound')
    scale = triple(profile['moment_scale_nm'], 'moment_scale_nm', positive=True)
    bias = triple(specification['bias_nm_frd'], 'bias_nm_frd')
    uncertainty = triple(specification['bias_abs_uncertainty_nm_frd'],
                         'bias_abs_uncertainty_nm_frd', nonnegative=True)
    if report['profile_kind'] == 'measured_single_condition' and any(value <= 0 for value in uncertainty):
        raise ValueError('measured trim-bias uncertainty must be positive on every axis')
    demand = triple(specification['desired_net_moment_abs_bound_nm_frd'],
                    'desired_net_moment_abs_bound_nm_frd', nonnegative=True)
    if not any(value > 0 for value in demand):
        raise ValueError('desired net-moment box must have a positive axis')
    # Every desired net moment and every reported trim bias needs a correction
    # whose component magnitude is bounded by this centered normalized box.
    correction = [(demand[axis] + abs(bias[axis]) + uncertainty[axis]) / scale[axis]
                  for axis in range(3)]
    rank = rank_screen.screen_matrices(allocation['effectiveness'],
                                       report['effectiveness_abs_uncertainty'])
    lower = Fraction(rank['gram_eigenvalue_lower_numerator'],
                     rank['gram_eigenvalue_lower_denominator'])
    needed_squared = sum(value * value for value in correction)
    guaranteed_squared = radius * radius * lower if lower > 0 else Fraction(0)
    status = ('rank_not_proven_for_reported_intervals' if lower <= 0 else
              'sufficient_static_net_box_reachable' if needed_squared <= guaranteed_squared else
              'inconclusive_for_declared_net_box')
    return dict(status=status, rank_status=rank['status'],
                rank_gram_lower=static_screen.fraction_record(lower),
                symmetric_command_radius=static_screen.fraction_record(radius),
                correction_axis_abs_bound_normalized=[static_screen.fraction_record(value)
                                                      for value in correction],
                correction_norm_squared=static_screen.fraction_record(needed_squared),
                guaranteed_norm_squared=static_screen.fraction_record(guaranteed_squared),
                method='uncertain net bias enclosed by centered correction box; minimum-norm right inverse',
                assumptions='single-condition local linear moment and bounded reported trim bias; static commands only',
                vehicle_qualified=False, physical_acceptance=False,
                full_envelope_qualified=False, slew_and_dynamics_qualified=False,
                implemented_allocator_tracking_qualified=False,
                actual_zero_moment_trim_measured=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', type=Path, required=True)
    parser.add_argument('--compiled', type=Path, required=True)
    parser.add_argument('--evidence-root', type=Path, required=True)
    parser.add_argument('--net-demand', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    try:
        if args.output.exists():
            raise ValueError('net authority output already exists')
        source_bytes = args.input.read_bytes()
        profile = json.loads(source_bytes, parse_constant=compiler.reject_constant,
                             object_pairs_hook=compiler.unique_object)
        recomputed = compiler.compile_profile(profile)
        recomputed['input_sha256'] = hashlib.sha256(source_bytes).hexdigest()
        report = rank_screen.read_json(args.compiled)
        if json.dumps(report, sort_keys=True, allow_nan=False) != json.dumps(
                recomputed, sort_keys=True, allow_nan=False):
            raise ValueError('compiled report differs from source profile')
        profile_provenance = rank_screen.check_provenance(profile, args.evidence_root)
        specification = rank_screen.read_json(args.net_demand)
        result = screen_net_box(profile, report, specification)
        net_provenance = rank_screen.check_provenance(
            {'provenance': specification['provenance']}, args.evidence_root)
        result.update(scope='single_condition_static_net_moment_box_screen_only',
                      input_sha256=recomputed['input_sha256'],
                      compiled_sha256=hashlib.sha256(args.compiled.read_bytes()).hexdigest(),
                      net_demand_sha256=hashlib.sha256(args.net_demand.read_bytes()).hexdigest(),
                      condition=report['condition'], profile_provenance=profile_provenance,
                      net_provenance=net_provenance,
                      net_demand=dict(body_frame='FRD', purpose=specification['purpose'],
                                      bias_nm_frd=specification['bias_nm_frd'],
                                      bias_abs_uncertainty_nm_frd=specification['bias_abs_uncertainty_nm_frd'],
                                      desired_net_moment_abs_bound_nm_frd=specification[
                                          'desired_net_moment_abs_bound_nm_frd']))
        args.output.write_text(json.dumps(result, indent=2, allow_nan=False) + '\n')
    except (OSError, ValueError, TypeError, KeyError, OverflowError) as error:
        print(f'Net authority screen rejected: {error}', file=sys.stderr)
        return 2
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
