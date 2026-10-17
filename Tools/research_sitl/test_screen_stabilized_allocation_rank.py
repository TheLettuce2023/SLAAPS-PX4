#!/usr/bin/env python3
"""Check sufficient interval-rank proof and deliberately inconclusive cases."""
import copy
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

import compile_stabilized_allocation as compiler
import screen_stabilized_allocation_rank as screen


ROOT = Path(__file__).resolve().parents[2]
EXAMPLE = Path(__file__).with_name('stabilized_allocation_synthetic.json')


class RankScreenTests(unittest.TestCase):
    def setUp(self):
        self.profile = json.loads(EXAMPLE.read_text())

    def report(self):
        return compiler.compile_profile(self.profile)

    def test_fixture_zero_uncertainty_has_exact_positive_lower_bound(self):
        report = self.report()
        result = screen.screen_matrices(report['allocator_inputs']['effectiveness'],
                                        report['effectiveness_abs_uncertainty'])
        self.assertEqual(result['status'], 'sufficient_full_row_rank_for_reported_intervals')
        self.assertEqual((result['gram_eigenvalue_lower_numerator'],
                          result['gram_eigenvalue_lower_denominator']), (1, 4))
        self.assertFalse(result['interval_box_contains_zero_matrix'])

    def test_mock_measured_schema_with_small_uncertainty_passes_only_rank_screen(self):
        self.profile['profile_kind'] = 'measured_single_condition'
        for canard in self.profile['canards']:
            canard['deflection_slope_uncertainty_rad_per_command'] = 0.001
            canard['moment_derivative_uncertainty_nm_per_rad'] = [0.01] * 3
        report = self.report()
        result = screen.screen_matrices(report['allocator_inputs']['effectiveness'],
                                        report['effectiveness_abs_uncertainty'])
        self.assertEqual(result['status'], 'sufficient_full_row_rank_for_reported_intervals')
        self.assertGreater(result['gram_eigenvalue_lower_approx'], 0)
        self.assertFalse(report['vehicle_qualified'])
        self.assertFalse(report['physical_acceptance'])

    def test_large_intervals_contain_a_rank_deficient_zero_matrix(self):
        self.profile['profile_kind'] = 'measured_single_condition'
        for canard in self.profile['canards']:
            canard['deflection_slope_uncertainty_rad_per_command'] = 0.001
            canard['moment_derivative_uncertainty_nm_per_rad'] = [0.44] * 3
        report = self.report()
        result = screen.screen_matrices(report['allocator_inputs']['effectiveness'],
                                        report['effectiveness_abs_uncertainty'])
        self.assertEqual(result['status'], 'interval_box_contains_rank_deficient_zero_matrix')
        self.assertTrue(result['interval_box_contains_zero_matrix'])
        self.assertLessEqual(result['gram_eigenvalue_lower_approx'], 0)

    def test_gershgorin_inconclusive_is_not_reported_as_rank_loss(self):
        a = [[1, 0, 0, 0], [0.9, 0.1, 0, 0], [0, 0, 1, 0]]
        u = [[0] * 4 for _ in range(3)]
        result = screen.screen_matrices(a, u)
        self.assertEqual(result['status'], 'inconclusive')
        self.assertFalse(result['interval_box_contains_zero_matrix'])

    def test_rejects_bad_uncertainty_and_provenance(self):
        with self.assertRaisesRegex(ValueError, 'nonnegative'):
            screen.screen_matrices([[0.25] * 4] * 3, [[-0.01] * 4] * 3)
        with self.assertRaisesRegex(ValueError, 'finite numeric data'):
            screen.screen_matrices([[10 ** 1000] * 4] * 3, [[0] * 4] * 3)
        profile = copy.deepcopy(self.profile)
        profile['provenance']['cad']['sha256'] = '0' * 64
        with self.assertRaisesRegex(ValueError, 'provenance hash mismatch'):
            screen.check_provenance(profile, ROOT)
        profile = copy.deepcopy(self.profile)
        profile['provenance']['cad']['reference'] = '../outside.txt'
        with self.assertRaisesRegex(ValueError, 'within evidence root'):
            screen.check_provenance(profile, ROOT)

    def test_cli_source_binding_and_same_hash_tamper_rejection(self):
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            source = work / 'profile.json'
            source.write_bytes(EXAMPLE.read_bytes())
            report = self.report()
            report['input_sha256'] = hashlib.sha256(source.read_bytes()).hexdigest()
            compiled = work / 'compiled.json'
            compiled.write_text(json.dumps(report, indent=2) + '\n')
            output = work / 'rank.json'
            command = [sys.executable, str(Path(screen.__file__)), '--input', str(source),
                       '--compiled', str(compiled), '--evidence-root', str(ROOT),
                       '--output', str(output)]
            self.assertEqual(subprocess.run(command, capture_output=True).returncode, 0)
            self.assertEqual(json.loads(output.read_text())['status'],
                             'sufficient_full_row_rank_for_reported_intervals')
            report['allocator_inputs']['effectiveness'][0][0] = 0.3
            compiled.write_text(json.dumps(report, indent=2) + '\n')
            command[-1] = str(work / 'rejected.json')
            rejected = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(rejected.returncode, 2)
            self.assertIn('differs from the source profile', rejected.stderr)
            self.assertFalse(Path(command[-1]).exists())


if __name__ == '__main__':
    unittest.main()
