#!/usr/bin/env python3
"""Guard the synthetic-only allocation header boundary."""
import copy
import hashlib
import json
import unittest

import compile_stabilized_allocation as compiler
import generate_stabilized_allocation_header as header


class HeaderTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.sha = hashlib.sha256(header.EXAMPLE.read_bytes()).hexdigest()
        cls.report = compiler.compile_profile(json.loads(header.EXAMPLE.read_text()))
        cls.report['input_sha256'] = cls.sha

    def test_sealed_synthetic_profile_renders(self):
        result = header.render(self.report, self.sha)
        self.assertIn('kInputSha256[] = "' + self.sha + '"', result)
        self.assertIn('{0.25F, -0.25F, 0.25F, -0.25F}', result)
        self.assertIn('kPositiveSlewPerS[4] = {5.0F', result)

    def test_measured_or_qualified_profile_rejected(self):
        for field, value in (('profile_kind', 'measured_vehicle'),
                             ('vehicle_qualified', True), ('physical_acceptance', True),
                             ('input_sha256', '0' * 64)):
            report = copy.deepcopy(self.report)
            report[field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                header.render(report, self.sha)

    def test_wrong_shape_order_or_nonfinite_value_rejected(self):
        for field, value in (('canard_order', ['wrong'] * 4),
                             ('effectiveness', [[0.0] * 3] * 3),
                             ('trim', [float('nan')] * 4),
                             ('minimum', [-1.0] * 3)):
            report = copy.deepcopy(self.report)
            report['allocator_inputs'][field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                header.render(report, self.sha)

    def test_same_claimed_hash_cannot_cover_changed_control_values(self):
        for field, index, value in (('effectiveness', (0, 0), 0.3),
                                    ('trim', (0,), 0.1),
                                    ('minimum', (0,), -0.8),
                                    ('positive_slew_per_s', (0,), 4.0)):
            report = copy.deepcopy(self.report)
            target = report['allocator_inputs'][field]
            for position in index[:-1]:
                target = target[position]
            target[index[-1]] = value
            with self.subTest(field=field), self.assertRaisesRegex(
                    ValueError, 'differs from the synthetic source'):
                header.render(report, self.sha)

    def test_same_claimed_hash_cannot_cover_changed_provenance(self):
        report = copy.deepcopy(self.report)
        report['condition']['airflow_mps'] = 30.0
        with self.assertRaisesRegex(ValueError, 'differs from the synthetic source'):
            header.render(report, self.sha)


if __name__ == '__main__':
    unittest.main()
