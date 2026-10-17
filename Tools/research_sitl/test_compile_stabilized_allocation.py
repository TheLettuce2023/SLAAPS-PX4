#!/usr/bin/env python3
"""Cross-check physical-to-normalized allocation conversion and rejection gates."""
import copy
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from compile_stabilized_allocation import ProfileError, compile_profile


ROOT = Path(__file__).resolve().parents[2]
EXAMPLE = Path(__file__).with_name('stabilized_allocation_synthetic.json')
EXPECTED = ((0.25, -0.25, 0.25, -0.25),
            (0.25, 0.25, -0.25, -0.25),
            (0.25, -0.25, -0.25, 0.25))


class AllocationProfileTest(unittest.TestCase):
    def setUp(self):
        self.profile = json.loads(EXAMPLE.read_text())

    def test_declared_fixture_reconstructs_actual_allocator(self):
        for record in self.profile['provenance'].values():
            self.assertEqual(hashlib.sha256((ROOT / record['reference']).read_bytes()).hexdigest(),
                             record['sha256'])
        output = compile_profile(self.profile)
        for actual_row, expected_row in zip(output['allocator_inputs']['effectiveness'], EXPECTED):
            for actual, expected in zip(actual_row, expected_row):
                self.assertAlmostEqual(actual, expected, places=12)
        self.assertEqual(output['allocator_inputs']['trim'], [0.0] * 4)
        self.assertEqual(output['allocator_inputs']['canard_order'],
                         ['synthetic_0', 'synthetic_1', 'synthetic_2', 'synthetic_3'])
        self.assertEqual(output['allocator_inputs']['positive_slew_per_s'], [5.0] * 4)
        self.assertEqual(output['effectiveness_abs_uncertainty'], [[0.0] * 4 for _ in range(3)])
        self.assertEqual([item['loaded_lag_time_constant_s']
                          for item in output['loaded_actuator_dynamics']], [0.05] * 4)
        for score in output['nominal_row_independence_squared']:
            self.assertAlmostEqual(score, 1.0)
        self.assertFalse(output['vehicle_qualified'])
        self.assertFalse(output['full_envelope_or_robust_rank_qualified'])

    def test_signed_loaded_deflection_mapping_and_uncertainty(self):
        canard = self.profile['canards'][0]
        canard['deflection_rad_per_normalized_command_at_trim'] *= -1
        canard['deflection_slope_uncertainty_rad_per_command'] = 0.001
        canard['moment_derivative_uncertainty_nm_per_rad'] = [0.01] * 3
        output = compile_profile(self.profile)
        self.assertAlmostEqual(output['allocator_inputs']['effectiveness'][0][0], -0.25, places=12)
        expected_bound = ((0.44073676548524865 * 0.001)
                          + (0.22689280275926285 * 0.01) + 0.001 * 0.01) / 0.4
        self.assertAlmostEqual(output['effectiveness_abs_uncertainty'][0][0], expected_bound)
        self.assertFalse(output['vehicle_qualified'])

    def test_rejects_unobservable_full_axis_and_invalid_inertia(self):
        for canard in self.profile['canards']:
            canard['moment_derivative_nm_per_rad_frd'][2] = \
                canard['moment_derivative_nm_per_rad_frd'][1]
        with self.assertRaisesRegex(ProfileError, 'full-axis rank'):
            compile_profile(self.profile)
        invalid = json.loads(EXAMPLE.read_text())
        invalid['rigid_body']['inertia_kg_m2_frd'][0][1] = 0.01
        with self.assertRaisesRegex(ProfileError, 'symmetric'):
            compile_profile(invalid)
        invalid = json.loads(EXAMPLE.read_text())
        invalid['rigid_body']['inertia_kg_m2_frd'][0][0] = -0.03
        with self.assertRaisesRegex(ProfileError, 'positive definite'):
            compile_profile(invalid)

    def test_measured_kind_requires_uncertainty_but_never_grants_acceptance(self):
        self.profile['profile_kind'] = 'measured_single_condition'
        with self.assertRaisesRegex(ProfileError, 'positive uncertainty'):
            compile_profile(self.profile)
        for canard in self.profile['canards']:
            canard['deflection_slope_uncertainty_rad_per_command'] = 0.001
            canard['moment_derivative_uncertainty_nm_per_rad'] = [0.01] * 3
        output = compile_profile(self.profile)
        self.assertFalse(output['vehicle_qualified'])
        self.assertFalse(output['physical_acceptance'])
        self.profile['canards'][0]['deflection_slope_uncertainty_rad_per_command'] = 0.23
        with self.assertRaisesRegex(ProfileError, 'sign must remain defined'):
            compile_profile(self.profile)

    def test_rejects_missing_or_nonfinite_data_and_cli_writes_no_output(self):
        for change in ('missing', 'unknown', 'nan'):
            invalid = copy.deepcopy(self.profile)
            if change == 'missing':
                del invalid['canards'][2]['moment_derivative_nm_per_rad_frd']
            elif change == 'unknown':
                invalid['condition']['assumed_wash_mps'] = 1.0
            else:
                invalid['moment_scale_nm'][0] = float('nan')
            with self.assertRaises(ProfileError):
                compile_profile(invalid)
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'invalid.json'
            target = Path(directory) / 'output.json'
            source.write_text(EXAMPLE.read_text().replace('"airflow_mps": 20.0',
                                                          '"airflow_mps": NaN'))
            result = subprocess.run([sys.executable, str(Path(__file__).with_name(
                'compile_stabilized_allocation.py')), '--input', str(source), '--output', str(target)],
                capture_output=True, text=True)
            self.assertEqual(result.returncode, 2)
            self.assertFalse(target.exists())
            source.write_text(EXAMPLE.read_text().replace('"airflow_mps": 20.0',
                                                          '"airflow_mps": 20.0, "airflow_mps": 30.0'))
            result = subprocess.run([sys.executable, str(Path(__file__).with_name(
                'compile_stabilized_allocation.py')), '--input', str(source), '--output', str(target)],
                capture_output=True, text=True)
            self.assertEqual(result.returncode, 2)
            self.assertIn('duplicate JSON field', result.stderr)
            self.assertFalse(target.exists())
            source.write_text(EXAMPLE.read_text())
            target.write_text('older report\n')
            result = subprocess.run([sys.executable, str(Path(__file__).with_name(
                'compile_stabilized_allocation.py')), '--input', str(source), '--output', str(target)],
                capture_output=True, text=True)
            self.assertEqual(result.returncode, 2)
            self.assertEqual(target.read_text(), 'older report\n')


if __name__ == '__main__':
    unittest.main()
