#!/usr/bin/env python3
"""Convert one declared FRD moment-derivative profile to SM allocator inputs."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import sys


class ProfileError(ValueError):
    """The declared plant/actuator input is incomplete or inconsistent."""


PROVENANCE_ROLES = ('cad', 'canard_moments', 'servo_calibration', 'edf_condition')
PROFILE_KINDS = ('synthetic_fixture', 'measured_single_condition')
CANARD_FIELDS = ('id', 'trim_command', 'minimum_command', 'maximum_command',
                 'positive_slew_per_s', 'negative_slew_per_s',
                 'deflection_rad_per_normalized_command_at_trim',
                 'deflection_slope_uncertainty_rad_per_command',
                 'moment_derivative_nm_per_rad_frd',
                 'moment_derivative_uncertainty_nm_per_rad',
                 'loaded_lag_time_constant_s', 'loaded_command_latency_s')
RANK_RESOLUTION = 1e-12  # Numerical diagnostic only; no physical authority threshold.


def exact_object(value, fields, name):
    if not isinstance(value, dict) or set(value) != set(fields):
        raise ProfileError(f'{name} must contain exactly: {", ".join(fields)}')
    return value


def finite(value, name):
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
        raise ProfileError(f'{name} must be finite numeric data')
    return float(value)


def vector(value, size, name):
    if not isinstance(value, list) or len(value) != size:
        raise ProfileError(f'{name} must have {size} entries')
    return [finite(item, f'{name}[{index}]') for index, item in enumerate(value)]


def matrix(value, rows, columns, name):
    if not isinstance(value, list) or len(value) != rows:
        raise ProfileError(f'{name} must have {rows} rows')
    return [vector(row, columns, f'{name}[{index}]') for index, row in enumerate(value)]


def positive(value, name, allow_zero=False):
    number = finite(value, name)
    if number < 0 or (number == 0 and not allow_zero):
        raise ProfileError(f'{name} must be {"nonnegative" if allow_zero else "positive"}')
    return number


def check_inertia(value):
    inertia = matrix(value, 3, 3, 'rigid_body.inertia_kg_m2_frd')
    scale = max(abs(item) for row in inertia for item in row)
    if scale == 0:
        raise ProfileError('inertia must be positive definite')
    for row in range(3):
        for column in range(row):
            if abs(inertia[row][column] - inertia[column][row]) > 1e-9 * scale:
                raise ProfileError('inertia must be symmetric in body FRD')
    a, b, c = inertia[0]
    _, d, e = inertia[1]
    _, _, f = inertia[2]
    leading_two = a * d - b * b
    determinant = a * (d * f - e * e) - b * (b * f - c * e) + c * (b * e - c * d)
    if not (math.isfinite(leading_two) and math.isfinite(determinant)
            and a > 0 and leading_two > 0 and determinant > 0):
        raise ProfileError('inertia must be positive definite')
    return inertia


def row_independence(effectiveness):
    norms = [math.sqrt(sum(value * value for value in row)) for row in effectiveness]
    if not all(math.isfinite(norm) and norm > 0 for norm in norms):
        raise ProfileError('each body moment axis needs nonzero local effectiveness')
    normalized = [[value / norm for value in row] for row, norm in zip(effectiveness, norms)]
    independence_squared = []
    for axis in range(3):
        for previous in range(axis):
            projection = sum(a * b for a, b in zip(normalized[axis], normalized[previous]))
            normalized[axis] = [a - projection * b for a, b in zip(normalized[axis], normalized[previous])]
        score = sum(value * value for value in normalized[axis])
        independence_squared.append(score)
        if not math.isfinite(score) or score <= RANK_RESOLUTION:
            raise ProfileError('local moment effectiveness lacks numerical full-axis rank')
        normalized[axis] = [value / math.sqrt(score) for value in normalized[axis]]
    return norms, independence_squared


def compile_profile(data):
    exact_object(data, ('schema_version', 'profile_kind', 'provenance', 'condition',
                        'rigid_body', 'moment_scale_nm', 'canards'), 'profile')
    if type(data['schema_version']) is not int or data['schema_version'] != 1:
        raise ProfileError('schema_version must be 1')
    kind = data['profile_kind']
    if kind not in PROFILE_KINDS:
        raise ProfileError('profile_kind must identify synthetic or measured single-condition data')
    provenance = exact_object(data['provenance'], PROVENANCE_ROLES, 'provenance')
    for role in PROVENANCE_ROLES:
        record = exact_object(provenance[role], ('reference', 'sha256'), f'provenance.{role}')
        if not isinstance(record['reference'], str) or not record['reference'].strip():
            raise ProfileError(f'provenance.{role}.reference is required')
        if not isinstance(record['sha256'], str) or not re.fullmatch(r'[0-9a-f]{64}', record['sha256']):
            raise ProfileError(f'provenance.{role}.sha256 must be lowercase SHA-256')
    condition = exact_object(data['condition'], ('body_frame', 'airflow_mps', 'alpha_rad',
                            'beta_rad', 'edf_thrust_n', 'canard_wash_mps'), 'condition')
    if condition['body_frame'] != 'FRD':
        raise ProfileError('moment derivatives must use body FRD')
    positive(condition['airflow_mps'], 'condition.airflow_mps')
    finite(condition['alpha_rad'], 'condition.alpha_rad')
    finite(condition['beta_rad'], 'condition.beta_rad')
    positive(condition['edf_thrust_n'], 'condition.edf_thrust_n', allow_zero=True)
    wash = vector(condition['canard_wash_mps'], 4, 'condition.canard_wash_mps')
    if any(value < 0 for value in wash):
        raise ProfileError('canard wash speeds must be nonnegative')
    rigid = exact_object(data['rigid_body'], ('mass_kg', 'cg_m_frd', 'inertia_kg_m2_frd'),
                         'rigid_body')
    positive(rigid['mass_kg'], 'rigid_body.mass_kg')
    vector(rigid['cg_m_frd'], 3, 'rigid_body.cg_m_frd')
    check_inertia(rigid['inertia_kg_m2_frd'])
    scale = vector(data['moment_scale_nm'], 3, 'moment_scale_nm')
    if any(value <= 0 for value in scale):
        raise ProfileError('moment scales must be positive')
    canards = data['canards']
    if not isinstance(canards, list) or len(canards) != 4:
        raise ProfileError('exactly four independent canards are required')
    ids = set()
    ordered_ids = []
    derivatives = []
    derivative_uncertainties = []
    slopes = []
    slope_uncertainties = []
    dynamics = []
    trims, minimums, maximums, positive_slew, negative_slew = [], [], [], [], []
    for index, canard in enumerate(canards):
        name = f'canards[{index}]'
        exact_object(canard, CANARD_FIELDS, name)
        identity = canard['id']
        if (not isinstance(identity, str) or not identity.strip()
                or identity != identity.strip() or identity in ids):
            raise ProfileError('canard IDs must be distinct nonempty strings')
        ids.add(identity)
        ordered_ids.append(identity)
        trim = finite(canard['trim_command'], f'{name}.trim_command')
        minimum = finite(canard['minimum_command'], f'{name}.minimum_command')
        maximum = finite(canard['maximum_command'], f'{name}.maximum_command')
        if not (-1 <= minimum < trim < maximum <= 1):
            raise ProfileError(f'{name} requires an interior trim within normalized command bounds')
        trims.append(trim)
        minimums.append(minimum)
        maximums.append(maximum)
        positive_slew.append(positive(canard['positive_slew_per_s'], f'{name}.positive_slew_per_s'))
        negative_slew.append(positive(canard['negative_slew_per_s'], f'{name}.negative_slew_per_s'))
        slope = finite(canard['deflection_rad_per_normalized_command_at_trim'],
                       f'{name}.deflection_rad_per_normalized_command_at_trim')
        slope_uncertainty = positive(canard['deflection_slope_uncertainty_rad_per_command'],
                                     f'{name}.deflection_slope_uncertainty_rad_per_command',
                                     allow_zero=True)
        if slope == 0 or slope_uncertainty >= abs(slope):
            raise ProfileError(f'{name} deflection sign must remain defined under uncertainty')
        slopes.append(slope)
        slope_uncertainties.append(slope_uncertainty)
        derivative = vector(canard['moment_derivative_nm_per_rad_frd'], 3,
                            f'{name}.moment_derivative_nm_per_rad_frd')
        uncertainty = vector(canard['moment_derivative_uncertainty_nm_per_rad'], 3,
                             f'{name}.moment_derivative_uncertainty_nm_per_rad')
        if any(value < 0 for value in uncertainty):
            raise ProfileError(f'{name} derivative uncertainty must be nonnegative')
        if kind == 'measured_single_condition' and (slope_uncertainty == 0
                or any(value == 0 for value in uncertainty)):
            raise ProfileError(f'{name} measured derivatives and slope require positive uncertainty')
        derivatives.append(derivative)
        derivative_uncertainties.append(uncertainty)
        lag = positive(canard['loaded_lag_time_constant_s'], f'{name}.loaded_lag_time_constant_s')
        latency = positive(canard['loaded_command_latency_s'], f'{name}.loaded_command_latency_s',
                           allow_zero=True)
        dynamics.append(dict(id=identity, loaded_lag_time_constant_s=lag,
                             loaded_command_latency_s=latency,
                             local_deflection_slope_rad_per_command=slope))
    effectiveness = []
    uncertainty = []
    for axis in range(3):
        effectiveness.append([derivatives[channel][axis] * slopes[channel] / scale[axis]
                              for channel in range(4)])
        uncertainty.append([(abs(derivatives[channel][axis]) * slope_uncertainties[channel]
                             + abs(slopes[channel]) * derivative_uncertainties[channel][axis]
                             + slope_uncertainties[channel] * derivative_uncertainties[channel][axis])
                            / scale[axis] for channel in range(4)])
    if any(not math.isfinite(value) for rows in (effectiveness, uncertainty)
           for row in rows for value in row):
        raise ProfileError('normalized effectiveness overflowed')
    row_norms, independence = row_independence(effectiveness)
    return dict(schema_version=1, profile_kind=kind,
                claim='offline_local_linear_allocation_conversion_only',
                vehicle_qualified=False, physical_acceptance=False,
                provenance=provenance, condition=condition, rigid_body=rigid,
                conversion='A[i,j] = dM_i/d(deflection_j) * d(deflection_j)/d(command_j) / moment_scale_i',
                allocator_inputs=dict(canard_order=ordered_ids,
                                      effectiveness=effectiveness, trim=trims,
                                      minimum=minimums, maximum=maximums,
                                      positive_slew_per_s=positive_slew,
                                      negative_slew_per_s=negative_slew),
                loaded_actuator_dynamics=dynamics,
                effectiveness_abs_uncertainty=uncertainty,
                nominal_row_norms=row_norms,
                nominal_row_independence_squared=independence,
                full_envelope_or_robust_rank_qualified=False)


def reject_constant(value):
    raise ProfileError(f'nonfinite JSON constant: {value}')


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ProfileError(f'duplicate JSON field: {key}')
        result[key] = value
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    try:
        if args.output.exists():
            raise ProfileError('output path already exists; choose a new report path')
        content = args.input.read_bytes()
        data = json.loads(content, parse_constant=reject_constant, object_pairs_hook=unique_object)
        result = compile_profile(data)
        result['input_sha256'] = hashlib.sha256(content).hexdigest()
        args.output.write_text(json.dumps(result, indent=2, allow_nan=False) + '\n')
    except (OSError, ValueError, TypeError) as error:
        print(f'Invalid allocation profile: {error}', file=sys.stderr)
        return 2
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
